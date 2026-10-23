#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "TeXDocumentWindow.h"
#include "CompletingEdit.h"
#include "ResearchAppearance.h"
#include "Settings.h"
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QListView>
#include <QSaveFile>
#include <QThread>
#include <QTimer>
#include <QPointer>
#include <QScreen>
#include <QWindow>
#include <QPixmap>
#include <QSet>
#include <QAbstractNativeEventFilter>
#include <windows.h>
#include <psapi.h>
static void settle(int ms=0){QElapsedTimer t;t.start();do{QCoreApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);if(ms)QThread::msleep(2);}while(t.elapsed()<ms);}
static bool put(QString p,QByteArray d){QSaveFile f(p);return f.open(QIODevice::WriteOnly)&&f.write(d)==d.size()&&f.commit();}
static void key(QWidget *w,int code,QString text={},Qt::KeyboardModifiers mods=Qt::NoModifier){QKeyEvent a(QEvent::KeyPress,code,mods,text),b(QEvent::KeyRelease,code,mods);QApplication::sendEvent(w,&a);QApplication::sendEvent(w,&b);}
static qint64 memory(){PROCESS_MEMORY_COUNTERS_EX s{};s.cb=sizeof(s);using F=BOOL(WINAPI*)(HANDLE,PPROCESS_MEMORY_COUNTERS,DWORD);auto f=reinterpret_cast<F>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"K32GetProcessMemoryInfo"));return f&&f(GetCurrentProcess(),reinterpret_cast<PPROCESS_MEMORY_COUNTERS>(&s),sizeof(s))?qint64(s.PrivateUsage):0;}
class FixtureInput:public QAbstractNativeEventFilter{public:bool nativeEventFilter(const QByteArray &type,void *p,qintptr *r)override{if(type!="windows_generic_MSG"&&type!="windows_dispatcher_MSG")return false;auto *m=static_cast<MSG*>(p);if(!m||!QWidget::find(reinterpret_cast<WId>(m->hwnd)))return false;if((m->message>=WM_KEYFIRST&&m->message<=WM_KEYLAST)||(m->message>=WM_LBUTTONDOWN&&m->message<=WM_MBUTTONDBLCLK)){if(r)*r=0;return true;}return false;}};
int main(int argc,char **argv){const QString report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);FixtureInput guard;app.installNativeEventFilter(&guard);QTimer::singleShot(100,&app,[&]{
QString path=QFileInfo(report).absolutePath()+"/performance.tex",text=QString("% long document\n").repeated(25000)+"\\fr";put(path,text.toUtf8());Tw::Settings s;s.setValue("openPDFwithTeX",false);s.setValue("researchFocusMode",false);s.setValue("researchCommandCompletionMode",0);s.remove("researchCommandCompletionUsage");
settle(220);const auto existing=TeXDocumentWindow::documentList();QSet<TeXDocumentWindow*> initialWindows;QStringList initialPaths;for(auto *window:existing){initialWindows.insert(window);initialPaths.append(window->textDoc()->absoluteFilePath());}
QJsonArray checks,rounds,retained;int failures=0;auto check=[&](QString n,bool pass){checks.append(QJsonObject{{"name",n},{"passed",pass}});failures+=!pass;};
for(int r=0;r<6;++r){s.setValue("researchCommandCompletionMode",0);QElapsedTimer start;start.start();auto *w=new TeXDocumentWindow(path);w->showNormal();w->resize(800,650);w->move(QGuiApplication::primaryScreen()->availableGeometry().topLeft()+QPoint(30,30));w->raise();w->activateWindow();auto *e=w->editor();e->setFocus();settle(180);const bool firstScreenDrawn=!e->viewport()->grab().isNull();double open=double(start.nsecsElapsed())/1e6;check(QString("round %1 disabled completion allocates no popup").arg(r),!w->findChild<QListView*>("researchCommandCompletion")&&firstScreenDrawn);
auto firstCursor=e->textCursor();firstCursor.movePosition(QTextCursor::End);e->setTextCursor(firstCursor);settle();QElapsedTimer firstInput;firstInput.start();key(e,Qt::Key_X,"x");const bool firstGlyphDrawn=!e->viewport()->grab().isNull();const double firstInputRasterMs=double(firstInput.nsecsElapsed())/1e6;check(QString("round %1 first input reaches the source raster").arg(r),e->toPlainText().endsWith("\\frx")&&firstGlyphDrawn);s.setValue("researchCommandCompletionMode",1);Research::setFocusMode(r%2);settle(160);
QJsonArray queries;bool correct=true;for(int i=0;i<200;++i){key(e,Qt::Key_Escape);const QString prefix=QStringList{"\\fr","\\al","\\ma","\\int","\\le","\\ri","\\sec","\\text","\\ge","\\bet"}[i%10];auto c=e->textCursor();c.movePosition(QTextCursor::End);c.movePosition(QTextCursor::StartOfBlock,QTextCursor::KeepAnchor);c.insertText(prefix);e->setTextCursor(c);QElapsedTimer timer;timer.start();key(e,Qt::Key_Slash,{},Qt::AltModifier);double ms=double(timer.nsecsElapsed())/1e6;auto *popup=w->findChild<QListView*>("researchCommandCompletion");bool shown=popup&&popup->isVisible()&&popup->model()->rowCount()>0;const auto raster=shown?popup->viewport()->grab():QPixmap();const double rasterMs=double(timer.nsecsElapsed())/1e6;correct&=shown&&!raster.isNull();queries.append(QJsonObject{{"query",prefix},{"ms",ms},{"rasterMs",rasterMs},{"rasterPresent",!raster.isNull()},{"rows",popup?popup->model()->rowCount():0},{"shown",shown}});settle();}
check(QString("round %1 all 200 mixed-family long-file queries work").arg(r),correct);const qreal dpr=w->devicePixelRatioF();const QString screen=w->windowHandle()->screen()->name();QPointer<TeXDocumentWindow> pointer(w);w->setModified(false);w->close();settle(160);check(QString("round %1 closing releases the source and popup").arg(r),pointer.isNull());retained.append(double(memory()));rounds.append(QJsonObject{{"round",r},{"focus",bool(r%2)},{"dpr",dpr},{"screen",screen},{"offOpenMs",open},{"firstInputRasterMs",firstInputRasterMs},{"queries",queries},{"privateBytesAfterClose",double(memory())},{"gdi",int(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS))},{"user",int(GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS))}});put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"rounds",rounds},{"retained",retained},{"documentLines",25001}}).toJson());}
QSet<TeXDocumentWindow*> finalWindows;QStringList finalPaths;for(auto *window:TeXDocumentWindow::documentList()){finalWindows.insert(window);finalPaths.append(window->textDoc()->absoluteFilePath());}check("six repeated source lifecycles retain only the identical pre-existing startup windows",finalWindows==initialWindows);
int gdiMin=1000000,gdiMax=0,userMin=1000000,userMax=0;for(int n=1;n<rounds.size();++n){const auto item=rounds[n].toObject();gdiMin=qMin(gdiMin,item["gdi"].toInt());gdiMax=qMax(gdiMax,item["gdi"].toInt());userMin=qMin(userMin,item["user"].toInt());userMax=qMax(userMax,item["user"].toInt());}
check("native handles stabilize after warmup with at most two handles of variation",gdiMax-gdiMin<=2&&userMax-userMin<=2&&gdiMax<128&&userMax<128);put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"rounds",rounds},{"retained",retained},{"documentLines",25001},{"initialWindows",QJsonArray::fromStringList(initialPaths)},{"finalWindows",QJsonArray::fromStringList(finalPaths)}}).toJson());app.exit(failures?1:0);
});return app.exec();}
