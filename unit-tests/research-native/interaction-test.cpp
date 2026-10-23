#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "TeXDocumentWindow.h"
#include "PDFDocumentWindow.h"
#include "CompletingEdit.h"
#include "ResearchAppearance.h"
#include "Settings.h"
#include <QElapsedTimer>
#include <QAbstractNativeEventFilter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QMenu>
#include <QPointer>
#include <QSaveFile>
#include <QScrollBar>
#include <QStatusBar>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <windows.h>
#include <psapi.h>
#include <algorithm>
#include <cmath>
#include <functional>
static bool waitFor(const std::function<bool()> &ready,int ms=8000){QElapsedTimer t;t.start();while(!ready()&&t.elapsed()<ms){QCoreApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QThread::msleep(3);}return ready();}
static void settle(int ms=100){QElapsedTimer t;t.start();waitFor([&]{return t.elapsed()>=ms;},ms+1000);}
static qint64 memory(){PROCESS_MEMORY_COUNTERS_EX s{};s.cb=sizeof(s);using F=BOOL(WINAPI*)(HANDLE,PPROCESS_MEMORY_COUNTERS,DWORD);auto fn=reinterpret_cast<F>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"K32GetProcessMemoryInfo"));return fn&&fn(GetCurrentProcess(),reinterpret_cast<PPROCESS_MEMORY_COUNTERS>(&s),sizeof(s))?qint64(s.PrivateUsage):0;}
static void mouse(QWidget *w,QPoint p,QEvent::Type type,Qt::MouseButtons buttons){QMouseEvent e(type,QPointF(p),QPointF(w->mapToGlobal(p)),Qt::LeftButton,buttons,Qt::NoModifier);QApplication::sendEvent(w,&e);}
static void click(QWidget *w){const QPoint p=w->rect().center();mouse(w,p,QEvent::MouseMove,Qt::NoButton);settle(140);mouse(w,p,QEvent::MouseButtonPress,Qt::LeftButton);mouse(w,p,QEvent::MouseButtonRelease,Qt::NoButton);}
static bool cleanTrack(QScrollBar *bar){auto image=bar->grab().toImage();if(image.isNull())return true;for(int x=0;x<image.width();x+=3)for(int y=0;y<image.height();y+=3){auto c=image.pixelColor(x,y);if(c.alpha()&&std::min({c.red(),c.green(),c.blue()})<130)return false;}return true;}
class FixtureInput final:public QAbstractNativeEventFilter{public:int ignored=0;bool nativeEventFilter(const QByteArray &type,void *message,qintptr *result)override{if(!message||(type!="windows_generic_MSG"&&type!="windows_dispatcher_MSG"))return false;auto *m=static_cast<MSG*>(message);if(!QWidget::find(reinterpret_cast<WId>(m->hwnd)))return false;if(m->message==WM_MOUSEMOVE||m->message==WM_NCMOUSEMOVE||(m->message>=WM_KEYFIRST&&m->message<=WM_KEYLAST)||(m->message>=WM_LBUTTONDOWN&&m->message<=WM_MBUTTONDBLCLK)){++ignored;if(result)*result=0;return true;}return false;}};
int main(int argc,char **argv){QString report=QString::fromLocal8Bit(argv[1]),pdf=QString::fromLocal8Bit(argv[2]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);FixtureInput isolation;app.installNativeEventFilter(&isolation);
QTimer::singleShot(80,&app,[&]{QJsonArray checks,rounds;int failures=0;QList<int> gaps;QElapsedTimer heartbeat;heartbeat.start();QTimer pulse;pulse.setInterval(25);QObject::connect(&pulse,&QTimer::timeout,&app,[&]{gaps.append(int(heartbeat.restart()));});pulse.start();
auto save=[&]{QSaveFile f(report);if(f.open(QIODevice::WriteOnly)){f.write(QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"rounds",rounds}}).toJson());f.commit();}};
auto check=[&](QString name,bool pass){checks.append(QJsonObject{{"name",name},{"passed",pass}});failures+=!pass;save();};
Tw::Settings().setValue("openPDFwithTeX",false);Tw::Settings().setValue("researchFocusMode",true);
QString source=QCoreApplication::applicationDirPath()+"/fixtures/main/main.tex";check("independent real manuscript exists",QFileInfo::exists(source));
const int initialSourceWindows=TeXDocumentWindow::documentList().size();
QList<qint64> retained;bool scrolls=true,menus=true,focus=true;
for(int round=0;round<16;++round){QElapsedTimer t;t.start();auto *w=new TeXDocumentWindow(source);w->resize(900,700);w->show();auto *v=new PDFDocumentWindow(pdf);v->resize(960,740);v->show();settle(120);qint64 open=t.elapsed();
auto *view=v->widget();auto *zoom=v->findChild<QToolButton*>("researchFocusZoom"),*more=w->findChild<QToolButton*>("researchFocusMore");
if(round==0){w->grab().save(QFileInfo(report).absolutePath()+"/source-real-focus.png");v->grab().save(QFileInfo(report).absolutePath()+"/pdf-real-focus.png");}
bool opened=false;auto c=QObject::connect(more->menu(),&QMenu::aboutToShow,[&]{opened=true;QTimer::singleShot(120,more->menu(),[&]{if(round==0)more->menu()->grab().save(QFileInfo(report).absolutePath()+"/focus-menu-mouse.png");more->menu()->hide();});});click(more);QObject::disconnect(c);menus&=opened;
QJsonArray steps;for(int n=0;n<36;++n){if(n%6==0)click(zoom);view->goToPage((n*23+round*11)%view->document().toStrongRef()->numPages());QPoint p=view->viewport()->rect().center();QWheelEvent wheel(QPointF(p),QPointF(view->viewport()->mapToGlobal(p)),QPoint(),QPoint(0,(n%2?-1:1)*1200),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);QApplication::sendEvent(view->viewport(),&wheel);steps.append(QJsonObject{{"step",n},{"page",view->currentPage()},{"zoom",view->zoomLevel()},{"privateBytes",double(memory())}});QSaveFile progress(QFileInfo(report).absolutePath()+"/interaction-current.json");if(progress.open(QIODevice::WriteOnly)){progress.write(QJsonDocument(QJsonObject{{"round",round},{"steps",steps}}).toJson());progress.commit();}settle(12);scrolls&=std::isfinite(view->zoomLevel())&&view->zoomLevel()>0&&cleanTrack(view->horizontalScrollBar())&&cleanTrack(view->verticalScrollBar());}
if(round%4==0){w->showMaximized();v->showMaximized();settle(50);w->showNormal();v->showNormal();settle(50);}
app.applyTranslation(round%2?"en_US":"zh_CN");Research::setFocusMode(false);settle(30);Research::setFocusMode(true);settle(30);focus&=w->findChild<QToolBar*>("toolBar_run")->isHidden()&&v->findChild<QToolBar*>("toolBar")->isHidden()&&w->property("researchFocus").toBool()&&v->property("researchFocus").toBool();click(zoom);
Research::showTransientStatusMessage(w,"Older transient",30);settle(50);Research::showTransientStatusMessage(w,"Replacement transient",700);settle(260);check(QString("round %1: old fade cannot clear a newer message").arg(round),w->statusBar()->currentMessage()=="Replacement transient");
const qreal dpi=v->devicePixelRatioF();auto weakDocument=view->document();QPointer<TeXDocumentWindow> guard(w);QPointer<PDFDocumentWindow> pdfGuard(v);w->setModified(false);bool sourceClosed=w->close(),pdfClosed=v->close();settle(100);QStringList left;for(auto *item:TeXDocumentWindow::documentList())left.append(item->textDoc()->absoluteFilePath());retained.append(memory());rounds.append(QJsonObject{{"round",round},{"openMs",double(open)},{"elapsedMs",double(t.elapsed())},{"retainedPrivateBytes",double(retained.last())},{"sourceClosed",sourceClosed},{"pdfClosed",pdfClosed},{"sourceGuard",bool(guard)},{"pdfGuard",bool(pdfGuard)},{"sourceWindows",QJsonArray::fromStringList(left)},{"pdfWindows",PDFDocumentWindow::documentList().size()},{"documentRetained",!weakDocument.isNull()},{"gdiObjects",int(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS))},{"userObjects",int(GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS))},{"topLevelWidgets",QApplication::topLevelWidgets().size()},{"devicePixelRatio",dpi}});save();
check(QString("round %1: pending notice and render close without a stale window").arg(round),!guard&&!pdfGuard&&TeXDocumentWindow::documentList().size()==initialSourceWindows&&PDFDocumentWindow::documentList().isEmpty());
}
check("explicit mouse input opens the focus menu every round",menus);check("576 rapid page/wheel operations keep clean tracks and finite zoom",scrolls);check("mode and language changes preserve the full-panel focus surface",focus);
settle(600);check("closing all files releases project-update coordinators",qApp->findChildren<QObject*>("researchProjectUpdates").isEmpty());
auto sorted=gaps;std::sort(sorted.begin(),sorted.end());QJsonObject metrics{{"privateStartWarm",double(retained.value(3))},{"privateEnd",double(retained.last())},{"retainedWarmGrowth",double(retained.last()-retained.value(3))},{"heartbeatSamples",sorted.size()},{"p95HeartbeatGapMs",sorted.isEmpty()?0:sorted[int(sorted.size()*.95)]},{"maxHeartbeatGapMs",sorted.isEmpty()?0:sorted.last()},{"ignoredUnrelatedPhysicalInput",isolation.ignored},{"menuCloseTimerArmedAfterAboutToShow",true}};
QSaveFile f(QFileInfo(report).absolutePath()+"/interaction-measures.json");if(f.open(QIODevice::WriteOnly)){f.write(QJsonDocument(metrics).toJson());f.commit();}pulse.stop();save();app.exit(failures?1:0);
});return app.exec();}
