#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "TeXDocumentWindow.h"
#include "PDFDocumentWindow.h"
#include "CompletingEdit.h"
#include "ResearchAppearance.h"
#include "Settings.h"
#include <QAction>
#include <QAbstractNativeEventFilter>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QGraphicsOpacityEffect>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QMessageBox>
#include <QMouseEvent>
#include <QMenu>
#include <QScrollBar>
#include <QScreen>
#include <QSaveFile>
#include <QThread>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVariantAnimation>
#include <QWindow>
#include <dwmapi.h>
#include <functional>
#include <windows.h>
#include <windowsx.h>
static bool waitFor(const std::function<bool()> &ready,int ms=5000) {QElapsedTimer t;t.start();while(!ready()&&t.elapsed()<ms){QCoreApplication::processEvents();QThread::msleep(5);}return ready();}
static void settle(int ms=320){QElapsedTimer t;t.start();waitFor([&]{return t.elapsed()>=ms;},ms+500);}
static bool put(const QString &path,const QByteArray &data){QDir().mkpath(QFileInfo(path).absolutePath());QSaveFile f(path);return f.open(QIODevice::WriteOnly)&&f.write(data)==data.size()&&f.commit();}
static QByteArray read(const QString &path){QFile f(path);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray();}
static HWND hwnd(QWidget *w){return reinterpret_cast<HWND>(w->winId());}
static bool paper(HWND handle){RECT a{},b{};POINT origin{};GetWindowRect(handle,&a);GetClientRect(handle,&b);ClientToScreen(handle,&origin);auto *w=QWidget::find(reinterpret_cast<WId>(handle));if(IsZoomed(handle)||(w&&w->isFullScreen()))return b.right>0&&b.bottom>0;const int side=origin.x-a.left;return side>0&&origin.y-a.top==1&&a.right-origin.x-b.right==side&&a.bottom-origin.y-b.bottom==side;}
static QJsonObject clientCoordinates(QWidget *w){
  RECT client{};GetClientRect(hwnd(w),&client);POINT origin{0,0};ClientToScreen(hwnd(w),&origin);
  const qreal dpr=w->devicePixelRatioF();const QPoint qtOrigin=w->mapToGlobal(QPoint());
  // Qt subtracts Windows' invisible resize margins from frameMargins(), so
  // that API can legitimately be negative for an extended-client window.
  // The behavioral contract is agreement between Qt and the native client.
  MONITORINFO monitor{};monitor.cbSize=sizeof(monitor);GetMonitorInfoW(MonitorFromWindow(hwnd(w),MONITOR_DEFAULTTONEAREST),&monitor);
  const QPoint screenOrigin=w->windowHandle()->screen()->geometry().topLeft();
  // Windows keeps each screen's native origin in Qt's global coordinates.
  // Scale the offset within that screen, not the global monitor position.
  const int xError=qAbs(monitor.rcMonitor.left+qRound((qtOrigin.x()-screenOrigin.x())*dpr)-origin.x),yError=qAbs(monitor.rcMonitor.top+qRound((qtOrigin.y()-screenOrigin.y())*dpr)-origin.y);
  const int widthError=qAbs(qRound(w->width()*dpr)-client.right),heightError=qAbs(qRound(w->height()*dpr)-client.bottom);
  const auto margins=w->windowHandle()->frameMargins();
  return {{"xErrorPixels",xError},{"yErrorPixels",yError},{"widthErrorPixels",widthError},{"heightErrorPixels",heightError},
    {"frameMargins",QJsonArray{margins.left(),margins.top(),margins.right(),margins.bottom()}},{"qtScreenOrigin",QJsonArray{screenOrigin.x(),screenOrigin.y()}},{"nativeScreenOrigin",QJsonArray{int(monitor.rcMonitor.left),int(monitor.rcMonitor.top)}},{"agrees",xError<=1&&yError<=1&&widthError<=1&&heightError<=1}};
}
static LRESULT hit(QWidget *w,QPoint local){POINT p{qRound(local.x()*w->devicePixelRatioF()),qRound(local.y()*w->devicePixelRatioF())};ClientToScreen(hwnd(w),&p);return SendMessageW(hwnd(w),WM_NCHITTEST,0,MAKELPARAM(p.x,p.y));}
static void motion(QWidget *target,QPoint position){QMouseEvent e(QEvent::MouseMove,QPointF(position),QPointF(target->mapToGlobal(position)),Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(target,&e);}
static bool nativeCaptionMove(QWidget *w){POINT p{qRound(200*w->devicePixelRatioF()),qRound(20*w->devicePixelRatioF())};ClientToScreen(hwnd(w),&p);SendMessageW(hwnd(w),WM_NCMOUSEMOVE,HTCAPTION,MAKELPARAM(p.x,p.y));return true;}
// A stationary-input case must not consume unrelated physical pointer motion
// from the human working in other apps while the fixture is being exercised.
class StationaryInput final : public QObject, public QAbstractNativeEventFilter {
public:
  bool active=false;
  int unrelatedNativeMoves=0;
  int unrelatedNativeKeysOrButtons=0;
  bool nativeEventFilter(const QByteArray &type,void *message,qintptr *result) override {
    if(!active||!message||(type!="windows_generic_MSG"&&type!="windows_dispatcher_MSG"))return false;
    const auto *native=static_cast<MSG*>(message);
    if((native->message==WM_MOUSEMOVE||native->message==WM_NCMOUSEMOVE)&&QWidget::find(reinterpret_cast<WId>(native->hwnd))) {
      ++unrelatedNativeMoves;if(result)*result=0;return true;
    }
    if(QWidget::find(reinterpret_cast<WId>(native->hwnd)) && ((native->message>=WM_KEYFIRST&&native->message<=WM_KEYLAST)||(native->message>=WM_LBUTTONDOWN&&native->message<=WM_MBUTTONDBLCLK))) {
      ++unrelatedNativeKeysOrButtons;if(result)*result=0;return true;
    }
    return false;
  }
  bool eventFilter(QObject *,QEvent *event) override {
    if(!active)return false;
    return event->type()==QEvent::Enter||event->type()==QEvent::Leave||event->type()==QEvent::HoverMove||
      ((event->type()==QEvent::MouseMove||event->type()==QEvent::NonClientAreaMouseMove)&&event->spontaneous());
  }
};
static QImage nativeFrame(QWidget *w){
  RECT rect{};GetWindowRect(hwnd(w),&rect);const int width=rect.right-rect.left,height=rect.bottom-rect.top;
  HDC screen=GetWindowDC(hwnd(w)),copy=CreateCompatibleDC(screen);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
  void *data=nullptr;HBITMAP bitmap=CreateDIBSection(copy,&info,DIB_RGB_COLORS,&data,nullptr,0);const HGDIOBJ previous=SelectObject(copy,bitmap);
  const bool captured=data&&PrintWindow(hwnd(w),copy,2);QImage result=captured?QImage(static_cast<uchar*>(data),width,height,QImage::Format_RGB32).copy():QImage();
  SelectObject(copy,previous);DeleteObject(bitmap);DeleteDC(copy);ReleaseDC(hwnd(w),screen);return result;
}
static bool heavyBlueEdge(const QImage &image,QWidget *window){
  RECT frame{},client{};POINT origin{};const auto handle=hwnd(window);
  if(image.isNull()||!GetWindowRect(handle,&frame)||!GetClientRect(handle,&client)||!ClientToScreen(handle,&origin))return true;
  const QRect clientArea(origin.x-frame.left,origin.y-frame.top,client.right,client.bottom);
  // Sample the actual non-client frame. A fixed eight-pixel strip can include
  // legitimate blue PDF text touching the viewport edge at a different DPR.
  int blue=0,samples=0;for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x){if(clientArea.contains(x,y))continue;const auto c=image.pixelColor(x,y);++samples;if(c.blue()>c.red()+20&&c.blue()>c.green()+8)++blue;}return !samples||blue>samples/100;
}
int main(int argc,char **argv){
  const QString report=QString::fromLocal8Bit(argv[1]),reference=QString::fromLocal8Bit(argv[2]);argc=1;
  TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);app.applyTranslation("zh_CN");
  StationaryInput stationary;app.installEventFilter(&stationary);app.installNativeEventFilter(&stationary);
  QTimer::singleShot(100,&app,[&]{
    const QString folder=QFileInfo(report).absolutePath(),fixture=folder+"/paper-"+QDateTime::currentDateTimeUtc().toString("yyyyMMddHHmmsszzz"),path=fixture+"/main.tex";
    const QByteArray original="\\documentclass{article}\n\\begin{document}\nPaper controls test.\n\\end{document}\n";
    QJsonArray checks;int failures=0;
    auto check=[&](const QString &name,bool pass){checks.append(QJsonObject{{"name",name},{"passed",pass}});if(!pass)++failures;put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"scale",qEnvironmentVariable("QT_SCALE_FACTOR")}}).toJson());};
    Tw::Settings().setValue("openPDFwithTeX",false);Tw::Settings().setValue("researchFocusMode",false);
    const int initialSources=TeXDocumentWindow::documentList().size();
    put(path,original);QFile::copy(reference,fixture+"/main.pdf");
    auto *source=new TeXDocumentWindow(path);source->resize(1000,740);source->show();source->goToPreview();settle();auto *pdf=source->pdfDocument();
    check("real source and separate preview are available",pdf!=nullptr);if(!pdf){app.exit(1);return;}
    pdf->resize(900,740);pdf->show();settle();
    const HWND sourceHandle=hwnd(source),pdfHandle=hwnd(pdf);RECT oldRect{};GetWindowRect(sourceHandle,&oldRect);const LONG_PTR originalLayered=GetWindowLongPtrW(sourceHandle,GWL_EXSTYLE)&WS_EX_LAYERED;
    source->grab().save(folder+"/default-before.png");
    const int oldTop=source->editor()->mapToGlobal(QPoint()).y(),oldWidth=source->editor()->viewport()->width();const QString text=source->editor()->toPlainText();
    stationary.active=true;
    QElapsedTimer transitionClock;transitionClock.start();Research::setFocusMode(true);
    auto *transition=source->findChild<QObject*>("researchWorkspaceTransition");
    check("switch covers the visible client with one transient frame and no toolbar or document effects",transition&&transition->findChild<QVariantAnimation*>()->state()!=QAbstractAnimation::Stopped&&source->findChild<QWidget*>("researchChromeVeil")->isVisible()&&source->findChild<QWidget*>("researchChromeVeil")->size()==source->size()&&!source->findChild<QToolBar*>("toolBar_run")->graphicsEffect()&&!source->findChild<QToolButton*>("researchFocusMore")->graphicsEffect()&&!source->editor()->graphicsEffect()&&!pdf->widget()->graphicsEffect());
    settle();RECT newRect{};GetWindowRect(sourceHandle,&newRect);
    check("mode switch keeps both HWND identities and the exact outer position",hwnd(source)==sourceHandle&&hwnd(pdf)==pdfHandle&&EqualRect(&oldRect,&newRect));
    check("mode switch never changes the native drawing surface to a layered window",(GetWindowLongPtrW(sourceHandle,GWL_EXSTYLE)&WS_EX_LAYERED)==originalLayered);
    check("custom captions retain the native resize frame and one-pixel top edge",paper(sourceHandle)&&paper(pdfHandle));
    const auto sourceCoordinates=clientCoordinates(source),pdfCoordinates=clientCoordinates(pdf);
    put(folder+"/native-client-coordinates.json",QJsonDocument(QJsonObject{{"source",sourceCoordinates},{"pdf",pdfCoordinates}}).toJson());
    check("Qt and native client coordinates agree in both paper windows",sourceCoordinates.value("agrees").toBool()&&pdfCoordinates.value("agrees").toBool());
    const bool transitionFinished=waitFor([&]{return source->findChild<QWidget*>("researchChromeVeil")->isHidden()&&!source->findChild<QToolBar*>("toolBar_run")->graphicsEffect()&&!source->findChild<QToolButton*>("researchFocusMore")->graphicsEffect();},600);
    put(folder+"/transition-timing.json",QJsonDocument(QJsonObject{{"finished",transitionFinished},{"elapsedMs",double(transitionClock.elapsed())},{"animationState",int(transition->findChild<QVariantAnimation*>()->state())},{"currentTime",transition->findChild<QVariantAnimation*>()->currentTime()},{"duration",transition->findChild<QVariantAnimation*>()->duration()}}).toJson());
    check("header transition disappears after bounded completion",transitionFinished&&transitionClock.elapsed()<900);
    check("source editor gains height without changing text or cursor",source->editor()->mapToGlobal(QPoint()).y()<oldTop&&source->editor()->toPlainText()==text);
    check("paper keeps manuscript wrap width instead of reflowing the whole file",source->editor()->viewport()->width()==oldWidth);
    auto *strip=source->findChild<QWidget*>("researchPaperControls"),*pdfStrip=pdf->findChild<QWidget*>("researchPaperControls");
    auto *close=source->findChild<QToolButton*>("researchWindowClose"),*minimize=source->findChild<QToolButton*>("researchWindowMinimize"),*maximize=source->findChild<QToolButton*>("researchWindowMaximize");
    auto *chrome=source->findChild<QWidget*>("researchFocusChrome"),*pdfChrome=pdf->findChild<QWidget*>("researchFocusChrome");
    check("idle focus hides the white surface and all five controls in both windows",chrome->isHidden()&&pdfChrome->isHidden()&&strip->isHidden()&&pdfStrip->isHidden()&&!source->findChild<QToolButton*>("researchFocusMore")->isVisible()&&!source->findChild<QToolButton*>("researchFocusCompile")->isVisible()&&!pdf->findChild<QToolButton*>("researchFocusZoom")->isVisible());
    QTextCursor firstLine(source->editor()->document()); firstLine.movePosition(QTextCursor::Start);
    const QRect firstLineRect = source->editor()->cursorRect(firstLine);
    check("focus protects the full first source line and keeps PDF edge-to-edge",chrome->isHidden()&&source->editor()->viewport()->rect().contains(firstLineRect)&&firstLineRect.height()>0&&pdf->contentsMargins().top()==0&&pdf->widget()->mapTo(pdf,QPoint()).y()==0);
    check("hidden PDF header leaves top text as a real client selection area",hit(pdf,QPoint(220,20))==HTCLIENT&&hit(pdf,QPoint(220,40))==HTCLIENT);
    check("all five controls use one common opacity surface",chrome->graphicsEffect()&&chrome->isAncestorOf(strip)&&chrome->isAncestorOf(source->findChild<QToolButton*>("researchFocusMore"))&&chrome->isAncestorOf(source->findChild<QToolButton*>("researchFocusCompile"))&&!strip->graphicsEffect());
    check("source caption fits the 24px writing margin while PDF retains its 40px caption",chrome->height()==24&&strip->height()==24&&pdfStrip->height()==40&&pdf->findChild<QToolButton*>("researchWindowClose")->size()==QSize(46,40)&&chrome->geometry().bottom()<source->editor()->viewport()->mapTo(source,firstLineRect.topLeft()).y()&&close->size()==QSize(46,24)&&minimize->size()==close->size()&&maximize->size()==close->size()&&maximize->x()-minimize->geometry().right()-1==0&&close->x()-maximize->geometry().right()-1==0&&strip->geometry().right()==source->width()-1&&strip->geometry().center().y()==source->findChild<QToolButton*>("researchFocusMore")->geometry().center().y());
    check("source and PDF entries stay on the left separate from window controls",source->findChild<QToolButton*>("researchFocusCompile")->x()==16&&source->findChild<QToolButton*>("researchFocusMore")->x()==56&&pdf->findChild<QToolButton*>("researchFocusZoom")->x()==16&&pdf->findChild<QToolButton*>("researchFocusMore")->x()==56&&pdfStrip->geometry().right()==pdf->width()-1);
    check("empty writing margin drags while source text and hidden PDF header remain client areas",hit(source,QPoint(220,20))==HTCAPTION&&hit(source,source->editor()->mapTo(source,QPoint(80,40)))==HTCLIENT&&hit(pdf,QPoint(72,24))==HTCLIENT);
    bool edges=true;const QList<QPoint> positions{{1,1},{source->width()/2,1},{source->width()-2,1},{1,source->height()/2},{source->width()-2,source->height()/2},{1,source->height()-2},{source->width()/2,source->height()-2},{source->width()-2,source->height()-2}};
    const QList<int> expected{HTTOPLEFT,HTTOP,HTTOPRIGHT,HTLEFT,HTRIGHT,HTBOTTOMLEFT,HTBOTTOM,HTBOTTOMRIGHT};
    for(int i=0;i<positions.size();++i)edges&=hit(source,positions[i])==expected[i];
    check("eight native resize edges and corners remain usable",edges);
    source->grab().save(folder+"/paper-idle.png");
    stationary.active=true;motion(source->editor()->viewport(),QPoint(180,140));settle(150);
    check("source body movement keeps the header hidden and first-line editing clear",chrome->isHidden()&&strip->isHidden());
    motion(chrome,QPoint(180,20));settle(70);const qreal appearing=static_cast<QGraphicsOpacityEffect*>(chrome->graphicsEffect())->opacity();
    check("header mouse movement fades in the white strip and all five controls together",chrome->isVisible()&&strip->isVisible()&&source->findChild<QToolButton*>("researchFocusCompile")->isVisible()&&source->findChild<QToolButton*>("researchFocusMore")->isVisible()&&appearing>0&&appearing<1);settle(100);
    check("visible window buttons are real client click targets",hit(source,close->mapTo(source,close->rect().center()))==HTCLIENT);
    source->grab().save(folder+"/paper-moving.png");
    const bool hiddenWhenIdle=waitFor([&]{return strip->isHidden();},2600);
    auto *idleTimer=source->findChild<QObject*>("researchPaperController")->findChild<QTimer*>();
    auto *focused=QApplication::focusWidget();const auto cursor=strip->mapFromGlobal(QCursor::pos());
    put(folder+"/idle-diagnostic.json",QJsonDocument(QJsonObject{{"hidden",hiddenWhenIdle},{"timerActive",idleTimer->isActive()},{"remainingMs",idleTimer->remainingTime()},{"opacity",static_cast<QGraphicsOpacityEffect*>(chrome->graphicsEffect())->opacity()},{"focusName",focused?focused->objectName():QString()},{"focusClass",focused?QString::fromLatin1(focused->metaObject()->className()):QString()},{"focusInStrip",strip->isAncestorOf(focused)},{"cursorInStrip",strip->rect().contains(cursor)},{"cursorX",cursor.x()},{"cursorY",cursor.y()},{"dpr",source->devicePixelRatioF()}}).toJson());
    check("stationary mouse hides the whole surface after the short idle interval",hiddenWhenIdle&&chrome->isHidden()&&!source->findChild<QToolButton*>("researchFocusMore")->isVisible());
    const auto pdfRect=pdf->widget()->geometry();const qreal pdfZoom=pdf->widget()->zoomLevel();const int pdfX=pdf->widget()->horizontalScrollBar()->value(),pdfY=pdf->widget()->verticalScrollBar()->value();
    motion(pdf->widget()->viewport(),QPoint(180,140));settle(150);pdf->grab().save(folder+"/pdf-moving.png");
    check("PDF motion reveals its fit more and window controls together",pdfChrome->isVisible()&&pdfStrip->isVisible()&&pdf->findChild<QToolButton*>("researchFocusZoom")->isVisible());
    const bool pdfHidden=waitFor([&]{return pdfChrome->isHidden();},2600);pdf->grab().save(folder+"/pdf-idle.png");
    check("PDF overlay never changes fit scroll or viewport geometry",pdfHidden&&pdf->widget()->geometry()==pdfRect&&pdf->widget()->zoomLevel()==pdfZoom&&pdf->widget()->horizontalScrollBar()->value()==pdfX&&pdf->widget()->verticalScrollBar()->value()==pdfY&&hit(pdf,QPoint(220,20))==HTCLIENT);
    motion(chrome,QPoint(250,24));settle(150);
    const bool sourceHeaderIdle=waitFor([&]{return chrome->isHidden();},2600);source->grab().save(folder+"/paper-stationary-header.png");
    check("stationary pointer inside the source white header still hides all controls",sourceHeaderIdle&&!source->findChild<QToolButton*>("researchFocusMore")->isVisible()&&!source->findChild<QToolButton*>("researchFocusCompile")->isVisible()&&!strip->isVisible());
    motion(pdfChrome,QPoint(250,24));settle(150);
    const bool pdfHeaderIdle=waitFor([&]{return pdfChrome->isHidden();},2600);pdf->grab().save(folder+"/pdf-stationary-header.png");
    check("stationary pointer inside the PDF white header still hides the full row without moving the page",pdfHeaderIdle&&!pdf->findChild<QToolButton*>("researchFocusMore")->isVisible()&&!pdf->findChild<QToolButton*>("researchFocusZoom")->isVisible()&&!pdfStrip->isVisible()&&pdf->widget()->geometry()==pdfRect&&pdf->widget()->zoomLevel()==pdfZoom&&pdf->widget()->horizontalScrollBar()->value()==pdfX&&pdf->widget()->verticalScrollBar()->value()==pdfY);
    auto *more=source->findChild<QToolButton*>("researchFocusMore");
    motion(chrome,QPoint(250,12));settle(150);
    auto *compile=source->findChild<QToolButton*>("researchFocusCompile");
    motion(compile,QPoint(compile->width()/2,compile->height()-3));settle(150);
    check("full compile-button hover area keeps focus controls visible",chrome->isVisible()&&compile->isEnabled());
    motion(more,QPoint(more->width()/2,more->height()-3));settle(150);
    check("full more-button hover area keeps focus controls visible",chrome->isVisible()&&more->isEnabled());
    bool menuStayed=false,menuShown=false;QJsonArray menuEvents;QElapsedTimer menuClock;menuClock.start();
    auto opening=QObject::connect(more->menu(),&QMenu::aboutToShow,[&]{menuShown=true;menuEvents.append(QJsonObject{{"event","show"},{"elapsedMs",double(menuClock.elapsed())}});QTimer::singleShot(2300,more->menu(),[&]{menuStayed=more->menu()->isVisible()&&chrome->isVisible()&&more->isEnabled();menuEvents.append(QJsonObject{{"event","deadline"},{"menuVisible",more->menu()->isVisible()},{"chromeVisible",chrome->isVisible()},{"buttonEnabled",more->isEnabled()},{"sourceActive",source->isActiveWindow()},{"elapsedMs",double(menuClock.elapsed())}});more->menu()->hide();});});
    auto hiding=QObject::connect(more->menu(),&QMenu::aboutToHide,[&]{menuEvents.append(QJsonObject{{"event","hide"},{"elapsedMs",double(menuClock.elapsed())}});});more->click();QObject::disconnect(opening);QObject::disconnect(hiding);put(folder+"/menu-idle-diagnostic.json",QJsonDocument(QJsonObject{{"shown",menuShown},{"stayed",menuStayed},{"events",menuEvents},{"ignoredNativeButtonsOrKeys",stationary.unrelatedNativeKeysOrButtons}}).toJson());
    check("idle never removes a menu while its user is choosing",menuStayed);
    QKeyEvent dismiss(QEvent::KeyPress,Qt::Key_Shift,Qt::NoModifier);QApplication::sendEvent(source->editor(),&dismiss);settle(60);motion(chrome,QPoint(260,20));settle(250);
    check("moving during fade reverses it without stale hiding or disabled buttons",chrome->isVisible()&&chrome->isEnabled()&&more->isEnabled()&&static_cast<QGraphicsOpacityEffect*>(chrome->graphicsEffect())->opacity()==1);
    QApplication::sendEvent(source->editor(),&dismiss);settle(250);
    motion(chrome,QPoint(280,20));QApplication::sendEvent(source->editor(),&dismiss);settle(250);
    check("typing before the first reveal frame cancels it completely",chrome->isHidden()&&!more->isVisible()&&static_cast<QGraphicsOpacityEffect*>(chrome->graphicsEffect())->opacity()==0);
    stationary.active=false;
    motion(chrome,QPoint(230,20));settle(150);maximize->setFocus();
    QKeyEvent spacePress(QEvent::KeyPress,Qt::Key_Space,Qt::NoModifier),spaceRelease(QEvent::KeyRelease,Qt::Key_Space,Qt::NoModifier);
    QApplication::sendEvent(maximize,&spacePress);QApplication::sendEvent(maximize,&spaceRelease);settle();
    check("keyboard activation remains usable without the controls fading underneath it",source->isMaximized()&&strip->isEnabled());source->showNormal();source->editor()->setFocus();settle();
    nativeCaptionMove(source);settle(140);check("moving over native draggable header also reveals controls",strip->isVisible());
    stationary.active=true;
    motion(chrome,QPoint(240,20));settle(140);QKeyEvent typing(QEvent::KeyPress,Qt::Key_A,Qt::NoModifier,QStringLiteral("a"));QApplication::sendEvent(source->editor(),&typing);
    const bool typingDismissed=waitFor([&]{return strip->isHidden();},500)&&source->editor()->toPlainText()!=text;
    stationary.active=false;
    check("typing dismisses controls while preserving ordinary editing",typingDismissed);
    source->editor()->undo();check("undo after typing restores the document exactly",source->editor()->toPlainText()==text);
    motion(chrome,QPoint(260,20));settle(150);maximize->click();settle();MONITORINFO monitor{};monitor.cbSize=sizeof(monitor);GetMonitorInfoW(MonitorFromWindow(sourceHandle,MONITOR_DEFAULTTONEAREST),&monitor);POINT origin{0,0};ClientToScreen(sourceHandle,&origin);RECT client{};GetClientRect(sourceHandle,&client);
    check("maximize fits the work area without cutting the paper or covering taskbar",source->isMaximized()&&origin.x==monitor.rcWork.left&&origin.y==monitor.rcWork.top&&client.right==monitor.rcWork.right-monitor.rcWork.left&&client.bottom==monitor.rcWork.bottom-monitor.rcWork.top);
    check("maximize button becomes restore in both languages",maximize->accessibleName()==QStringLiteral("还原窗口"));app.applyTranslation("en_US");settle();check("window actions are correctly localized",close->accessibleName()=="Close window"&&minimize->accessibleName()=="Minimize"&&maximize->accessibleName()=="Restore window");
    const bool restoreEnabledBeforeMotion=maximize->isEnabled(),restoreVisibleBeforeMotion=strip->isVisible();
    // A real click first moves onto the target. Locale refresh may outlast
    // the focus idle timeout; clicking a disabled hidden target is not input.
    motion(maximize,maximize->rect().center());settle(150);
    const bool restoreEnabledAfterMotion=maximize->isEnabled();
    maximize->click();settle();GetWindowRect(sourceHandle,&newRect);
    put(folder+"/restore-target-state.json",QJsonDocument(QJsonObject{{"enabledBeforeMotion",restoreEnabledBeforeMotion},{"visibleBeforeMotion",restoreVisibleBeforeMotion},{"enabledAfterMotion",restoreEnabledAfterMotion},{"oldRect",QJsonArray{int(oldRect.left),int(oldRect.top),int(oldRect.right),int(oldRect.bottom)}},{"newRect",QJsonArray{int(newRect.left),int(newRect.top),int(newRect.right),int(newRect.bottom)}}}).toJson());
    check("restore returns to exactly the original outer rectangle",!source->isMaximized()&&EqualRect(&oldRect,&newRect)&&paper(sourceHandle));
    motion(minimize,minimize->rect().center());settle(150);minimize->click();settle(100);check("minimize keeps the same native window",source->isMinimized()&&hwnd(source)==sourceHandle);source->showNormal();settle();
    POINT caption{qRound(220*source->devicePixelRatioF()),qRound(20*source->devicePixelRatioF())};ClientToScreen(sourceHandle,&caption);SendMessageW(sourceHandle,WM_NCLBUTTONDBLCLK,HTCAPTION,MAKELPARAM(caption.x,caption.y));settle();check("blank header double click still maximizes through Windows",source->isMaximized());source->showNormal();settle();
    bool repeat=true;
    for(int i=0;i<12;++i){Research::setFocusMode(false);settle();repeat&=paper(sourceHandle)&&paper(pdfHandle)&&strip->isVisible();Research::setFocusMode(true);settle();repeat&=paper(sourceHandle)&&paper(pdfHandle)&&hwnd(source)==sourceHandle&&hwnd(pdf)==pdfHandle&&source->editor()->toPlainText()==text;}
    check("repeated mode changes keep one frame with normal controls and no HWND or data changes",repeat);
    // Windows must not fall back to classic non-client painting just because
    // the native caption is replaced. Exercise activation and NC repaints,
    // which can otherwise draw a pale blue border over the paper.
    bool activationClean=true;
    for(QWidget *window:{static_cast<QWidget*>(source),static_cast<QWidget*>(pdf)}){
      SendMessageW(hwnd(window),WM_NCACTIVATE,FALSE,0);SendMessageW(hwnd(window),WM_NCACTIVATE,TRUE,0);
      SendMessageW(hwnd(window),WM_NCPAINT,1,0);const auto captured=nativeFrame(window);
      activationClean&=!heavyBlueEdge(captured,window);captured.save(folder+QString("/native-activation-%1.png").arg(window==source?"source":"pdf"));
    }
    check("activation and native repaint never redraw classic blue borders on paper",activationClean);
    bool frameCaptures=true,noBlue=true;QJsonArray frames;
    for(int mode=0;mode<4;++mode){Research::setFocusMode(mode%2);for(int frame=0;frame<4;++frame){settle(frame?25:1);for(QWidget *window:{static_cast<QWidget*>(source),static_cast<QWidget*>(pdf)}){const auto captured=nativeFrame(window);const QString role=window==source?"source":"pdf";frameCaptures&=!captured.isNull()&&captured.width()>800;noBlue&=!heavyBlueEdge(captured,window);captured.save(folder+QString("/native-transition-%1-%2-%3.png").arg(role).arg(mode).arg(frame));frames.append(QJsonObject{{"role",role},{"mode",mode},{"frame",frame},{"captured",!captured.isNull()},{"heavyBlueEdge",heavyBlueEdge(captured,window)}});}}}
    put(folder+"/native-transition-frames.json",QJsonDocument(frames).toJson());
    check("native-frame transition samples contain no heavy blue edge",frameCaptures&&noBlue);
    const auto restoredSource=clientCoordinates(source),restoredPdf=clientCoordinates(pdf);
    put(folder+"/restored-client-coordinates.json",QJsonDocument(QJsonObject{{"source",restoredSource},{"pdf",restoredPdf}}).toJson());
    check("Qt and native client dimensions still agree after maximize minimize and repeated mode changes",restoredSource.value("agrees").toBool()&&restoredPdf.value("agrees").toBool());
    const auto restoredImage=source->grab().toImage();restoredImage.save(folder+"/focus-after-native-restore.png");
    const auto bottom=restoredImage.pixelColor(restoredImage.width()/2,restoredImage.height()-2);
    check("restored focus source keeps a clean white bottom edge",bottom.red()>248&&bottom.green()>248&&bottom.blue()>248);
    Research::setFocusMode(false);settle(40);Research::setFocusMode(true);settle(40);Research::setFocusMode(false);
    const bool lastModeReady=waitFor([&]{return !source->property("researchFocus").toBool()&&!pdf->property("researchFocus").toBool()&&paper(sourceHandle)&&!source->findChild<QToolBar*>("toolBar_run")->graphicsEffect();},900);
    check("rapid interrupted transitions finish in the last requested mode",lastModeReady);
    Research::setFocusMode(true);settle();
    source->editor()->insertPlainText("unsaved");motion(chrome,QPoint(300,20));settle(150);
    bool asked=false;QTimer::singleShot(100,&app,[&]{for(auto *w:QApplication::topLevelWidgets())if(auto *box=qobject_cast<QMessageBox*>(w);box&&box->isVisible()){asked=true;box->done(QMessageBox::Cancel);}});close->click();settle();check("close still asks about unsaved text and Cancel preserves the window and disk",asked&&source->isVisible()&&source->isModified()&&read(path)==original);
    source->editor()->undo();source->save();motion(chrome,QPoint(320,20));settle(150);Research::setFocusMode(false);settle(30);source->setModified(false);delete source;for(auto *viewer:PDFDocumentWindow::documentList())delete viewer;settle();
    check("closing during active control and mode animations leaves no fixture document windows",TeXDocumentWindow::documentList().size()==initialSources&&PDFDocumentWindow::documentList().isEmpty()&&read(path)==original);
    stationary.active=true;
    Tw::Settings().setValue("researchFocusMode",true);auto *reopened=new TeXDocumentWindow(path);reopened->show();check("first show creates the paper frame directly without a later titlebar change",paper(hwnd(reopened))&&reopened->findChild<QWidget*>("researchPaperControls")->isHidden());settle();reopened->grab().save(folder+"/paper-reopened.png");reopened->setModified(false);delete reopened;
    put(folder+"/stationary-input-isolation.json",QJsonDocument(QJsonObject{{"ignoredUnrelatedNativeMoves",stationary.unrelatedNativeMoves},{"onlyTestProcessWindows",true}}).toJson());app.exit(failures?1:0);
  });return app.exec();
}
