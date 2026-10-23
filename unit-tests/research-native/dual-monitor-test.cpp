#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "TeXDocumentWindow.h"
#include "PDFDocumentWindow.h"
#include "CompletingEdit.h"
#include "ResearchAppearance.h"
#include "Settings.h"
#include <PDFDocumentView.h>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QScreen>
#include <QThread>
#include <QTimer>
#include <QWindow>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QListView>
#include <QToolButton>
#include <QMenu>
#include <QScrollBar>
#include <QAbstractNativeEventFilter>
#include <windows.h>
#include <psapi.h>
static void settle(int ms=0){QElapsedTimer t;t.start();do{QCoreApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);if(ms)QThread::msleep(3);}while(t.elapsed()<ms);}
static bool put(QString p,QByteArray d){QSaveFile f(p);return f.open(QIODevice::WriteOnly)&&f.write(d)==d.size()&&f.commit();}
static void key(QWidget *w,int code,QString text={},Qt::KeyboardModifiers mods=Qt::NoModifier){QKeyEvent a(QEvent::KeyPress,code,mods,text),b(QEvent::KeyRelease,code,mods);QApplication::sendEvent(w,&a);QApplication::sendEvent(w,&b);}
class FixtureInput:public QAbstractNativeEventFilter{public:bool nativeEventFilter(const QByteArray &type,void *p,qintptr *r)override{if(type!="windows_generic_MSG"&&type!="windows_dispatcher_MSG")return false;auto *m=static_cast<MSG*>(p);if(!m||!QWidget::find(reinterpret_cast<WId>(m->hwnd)))return false;if((m->message>=WM_KEYFIRST&&m->message<=WM_KEYLAST)||(m->message>=WM_LBUTTONDOWN&&m->message<=WM_MBUTTONDBLCLK)){if(r)*r=0;return true;}return false;}};
int main(int argc,char **argv){QString report=QString::fromLocal8Bit(argv[1]),pdfPath=QString::fromLocal8Bit(argv[2]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);FixtureInput guard;app.installNativeEventFilter(&guard);
QTimer::singleShot(100,&app,[&]{const QString folder=QFileInfo(report).absolutePath(),path=folder+"/dual.tex";put(path,"\\documentclass{article}\n\\begin{document}\nText\n\\end{document}\n");Tw::Settings s;s.setValue("openPDFwithTeX",false);s.setValue("researchFocusMode",false);s.setValue("researchCommandCompletionMode",1);s.setValue("launchOption",0);auto *source=new TeXDocumentWindow(path);auto *preview=new PDFDocumentWindow(pdfPath);auto *e=source->editor();auto *view=preview->widget();
QJsonArray checks,samples,monitors,rounds;int failures=0;auto save=[&]{put(report,QJsonDocument(QJsonObject{{"checks",checks},{"samples",samples},{"monitors",monitors},{"rounds",rounds},{"failures",failures}}).toJson());};auto check=[&](QString name,bool pass){checks.append(QJsonObject{{"name",name},{"passed",pass}});failures+=!pass;save();};
const auto screens=QGuiApplication::screens();for(auto *screen:screens)monitors.append(QJsonObject{{"name",screen->name()},{"geometry",QJsonArray{screen->geometry().x(),screen->geometry().y(),screen->geometry().width(),screen->geometry().height()}},{"available",QJsonArray{screen->availableGeometry().x(),screen->availableGeometry().y(),screen->availableGeometry().width(),screen->availableGeometry().height()}},{"dpr",screen->devicePixelRatio()}});check("at least two actual Windows displays are available",screens.size()>=2);
for(auto *w:{static_cast<QMainWindow*>(source),static_cast<QMainWindow*>(preview)}){w->showNormal();w->resize(560,520);}settle(200);
auto popup=[&]{return source->findChild<QListView*>("researchCommandCompletion");};auto summon=[&]{key(e,Qt::Key_Escape);e->setPlainText("\\frac");auto c=e->textCursor();c.movePosition(QTextCursor::End);e->setTextCursor(c);source->raise();source->activateWindow();e->setFocus();settle(30);key(e,Qt::Key_Slash,{},Qt::AltModifier);settle(30);return popup()&&popup()->isVisible();};
source->move(screens.first()->availableGeometry().topLeft()+QPoint(30,80));settle(120);check("completion opens before moving the source window",summon());QRect oldPopup=popup()?popup()->geometry():QRect();source->move(source->pos()+QPoint(100,60));settle(80);const bool cancelled=!popup()||!popup()->isVisible();check("a same-monitor window move cancels or reanchors an open popup",cancelled||popup()->geometry().topLeft()==oldPopup.topLeft()+QPoint(100,60));if(popup()){source->grab().save(folder+"/move-source.png");popup()->grab().save(folder+"/move-popup.png");}key(e,Qt::Key_Escape);
for(int n=0;n<screens.size();++n){auto *target=screens[n];const auto area=target->availableGeometry();source->showNormal();preview->showNormal();source->move(area.topLeft()+QPoint(20,55));preview->move(area.topLeft()+QPoint(qMax(25,area.width()-590),95));settle(160);
 for(bool focus:{false,true})for(const auto &locale:QStringList{"zh_CN","en_US"}){Research::setFocusMode(focus);app.applyTranslation(locale);settle(180);
  for(auto *w:{static_cast<QMainWindow*>(source),static_cast<QMainWindow*>(preview)}){const HWND h=reinterpret_cast<HWND>(w->winId());RECT client{};GetClientRect(h,&client);POINT origin{0,0};ClientToScreen(h,&origin);MONITORINFO mi{};mi.cbSize=sizeof(mi);GetMonitorInfoW(MonitorFromWindow(h,MONITOR_DEFAULTTONEAREST),&mi);auto qt=w->mapToGlobal(QPoint()),screen=w->windowHandle()->screen()->geometry().topLeft();qreal dpr=w->devicePixelRatioF();int dx=qAbs(mi.rcMonitor.left+qRound((qt.x()-screen.x())*dpr)-origin.x),dy=qAbs(mi.rcMonitor.top+qRound((qt.y()-screen.y())*dpr)-origin.y),dw=qAbs(qRound(w->width()*dpr)-client.right),dh=qAbs(qRound(w->height()*dpr)-client.bottom);QString role=w==source?"source":"PDF",label=QString("%1 %2 %3 monitor %4").arg(role,focus?"focus":"normal",locale).arg(n);check(label+" matches native screen and client coordinates within one pixel",w->windowHandle()->screen()==target&&dx<=1&&dy<=1&&dw<=1&&dh<=1);bool inside=true;for(auto *b:w->findChildren<QToolButton*>())if(b->isVisible()&&b->objectName().startsWith("research")){QRect r(b->mapTo(w,QPoint()),b->size());inside&=w->rect().contains(r);}check(label+" keeps every visible research control inside its window",inside);samples.append(QJsonObject{{"role",role},{"focus",focus},{"locale",locale},{"screen",n},{"dpr",dpr},{"errors",QJsonArray{dx,dy,dw,dh}}});w->grab().save(folder+QString("/screen-%1-%2-%3-%4.png").arg(n).arg(role,focus?"focus":"normal",locale));}
  source->resize(420,300);settle(100);summon();QRect caret(e->viewport()->mapToGlobal(e->cursorRect().topLeft()),e->cursorRect().size()),owner(source->mapToGlobal(QPoint()),source->size());bool fits=popup()&&popup()->isVisible()&&owner.contains(popup()->geometry())&&target->availableGeometry().contains(popup()->geometry())&&!caret.intersects(popup()->geometry());check(QString("popup clears caret and screen/window edges on monitor %1 %2 %3").arg(n).arg(focus?"focus":"normal",locale),fits);if(popup())popup()->grab().save(folder+QString("/popup-%1-%2-%3.png").arg(n).arg(focus?"focus":"normal",locale));key(e,Qt::Key_Escape);source->resize(560,520);
 }
 Research::setFocusMode(false);settle(160);
 for(auto *w:{static_cast<QMainWindow*>(source),static_cast<QMainWindow*>(preview)}){auto *b=w->findChild<QToolButton*>("researchWindowMaximize");b->click();settle(180);HWND h=reinterpret_cast<HWND>(w->winId());RECT client{};GetClientRect(h,&client);POINT origin{0,0};ClientToScreen(h,&origin);MONITORINFO mi{};mi.cbSize=sizeof(mi);GetMonitorInfoW(MonitorFromWindow(h,MONITOR_DEFAULTTONEAREST),&mi);check(QString("%1 maximize uses monitor %2 work area without clipping/taskbar overlap").arg(w==source?"source":"PDF").arg(n),w->isMaximized()&&IsZoomed(h)&&origin.x==mi.rcWork.left&&origin.y==mi.rcWork.top&&client.right==mi.rcWork.right-mi.rcWork.left&&client.bottom==mi.rcWork.bottom-mi.rcWork.top);b->click();settle(120);check(QString("%1 restore is consistent on monitor %2").arg(w==source?"source":"PDF").arg(n),!w->isMaximized()&&!IsZoomed(h));}
}
// Repeated cross-DPI transfers interleaved with real page renders and zooms.
const DWORD beforeGdi=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS),beforeUser=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS);bool transfers=true,popupCancel=true,pdfValid=true;QElapsedTimer workload;workload.start();
for(int i=0;i<48;++i){auto *target=screens[i%screens.size()];auto area=target->availableGeometry();Research::setFocusMode(i%2);source->move(area.topLeft()+QPoint(35+i%7,60+i%11));preview->move(area.topLeft()+QPoint(qMax(25,area.width()-600),100));settle(80);transfers&=source->windowHandle()->screen()==target&&preview->windowHandle()->screen()==target;bool shown=summon();QPoint previousPopup=popup()?popup()->geometry().topLeft():QPoint();source->move(source->pos()+QPoint(9,7));settle(15);popupCancel&=shown&&(!popup()||!popup()->isVisible()||popup()->geometry().topLeft()==previousPopup+QPoint(9,7));key(e,Qt::Key_Escape);view->goToPage((i*7)%100);if(i%3==0)view->zoomFitWindow();else if(i%3==1)view->zoomFitWidth();else view->zoomFitContentWidth();view->zoomIn();view->zoomOut();settle(35);pdfValid&=qIsFinite(view->zoomLevel())&&view->zoomLevel()>0&&view->viewport()->width()>0;rounds.append(QJsonObject{{"round",i},{"screen",i%screens.size()},{"dpr",preview->devicePixelRatioF()},{"page",int(view->currentPage())},{"zoom",view->zoomLevel()},{"gdi",int(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS))},{"user",int(GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS))}});if(i%8==0)save();}
check("48 repeated interleaved cross-screen transfers reach their requested screens",transfers);check("repeated popup/move/cancel cycles remain safe",popupCancel);check("PDF page renders and three fit states remain valid across DPI transfers",pdfValid);check("cross-screen repetitions do not leak native handles beyond a small lazy-allocation allowance",GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<=beforeGdi+8&&GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS)<=beforeUser+10);
put(folder+"/dual-performance.json",QJsonDocument(QJsonObject{{"rounds",48},{"elapsedMs",double(workload.elapsed())},{"gdiBefore",int(beforeGdi)},{"gdiAfter",int(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS))},{"userBefore",int(beforeUser)},{"userAfter",int(GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS))}}).toJson());source->setModified(false);source->close();preview->close();settle(100);save();app.exit(failures?1:0);
});return app.exec();}
