#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "TeXDocumentWindow.h"
#include "PDFDocumentWindow.h"
#include "CompletingEdit.h"
#include "ResearchAppearance.h"
#include "ResearchFind.h"
#include "Settings.h"
#include <QElapsedTimer>
#include <QAction>
#include <QAbstractNativeEventFilter>
#include <QGraphicsOpacityEffect>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPointer>
#include <QSaveFile>
#include <QShortcut>
#include <QThread>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>

static void settle(int ms=260) { QElapsedTimer t;t.start();while(t.elapsed()<ms){QCoreApplication::processEvents();QThread::msleep(2);} }
static void dismiss(QMainWindow *w) { QKeyEvent key(QEvent::KeyPress,Qt::Key_Shift,Qt::ShiftModifier);QApplication::sendEvent(w,&key);settle(); }
static void moveNative(QMainWindow *w,QPoint point) {
  const auto h=reinterpret_cast<HWND>(w->winId());
  PostMessageW(h,WM_MOUSEMOVE,0,MAKELPARAM(qRound(point.x()*w->devicePixelRatioF()),qRound(point.y()*w->devicePixelRatioF())));settle();
}
static bool visible(QMainWindow *w) {
  auto *chrome=w->findChild<QWidget*>("researchFocusChrome");auto *close=w->findChild<QToolButton*>("researchWindowClose");
  return chrome&&close&&chrome->isVisible()&&close->isVisible()&&close->isEnabled()&&static_cast<QGraphicsOpacityEffect*>(chrome->graphicsEffect())->opacity()>.99;
}
class MotionProbe final : public QAbstractNativeEventFilter {
public:
  int nativeMoves{0};
  bool nativeEventFilter(const QByteArray &,void *message,qintptr *) override {
    auto *m=static_cast<MSG*>(message);
    if(m&&m->message==WM_MOUSEMOVE&&QWidget::find(reinterpret_cast<WId>(m->hwnd)))++nativeMoves;
    return false;
  }
};
int main(int argc,char **argv) {
  const QString report=QString::fromLocal8Bit(argv[1]),pdfPath=QString::fromLocal8Bit(argv[2]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
  MotionProbe probe;app.installNativeEventFilter(&probe);
  QTimer::singleShot(80,&app,[&]{
    QJsonArray checks,geometry;int failures=0;auto check=[&](QString name,bool pass){checks.append(QJsonObject{{"name",name},{"passed",pass}});failures+=!pass;};
    auto workArea=[&](QMainWindow *w,const QString &label) {
      const auto h=reinterpret_cast<HWND>(w->winId());RECT rectangle{},client{},visible{};POINT origin{};MONITORINFO monitor{};monitor.cbSize=sizeof(monitor);
      const bool measured=GetWindowRect(h,&rectangle)&&GetMonitorInfoW(MonitorFromWindow(h,MONITOR_DEFAULTTONEAREST),&monitor)&&GetClientRect(h,&client)&&ClientToScreen(h,&origin)&&SUCCEEDED(DwmGetWindowAttribute(h,DWMWA_EXTENDED_FRAME_BOUNDS,&visible,sizeof(visible)));
      geometry.append(QJsonObject{{"case",label},{"dpr",w->devicePixelRatioF()},{"zoomed",bool(IsZoomed(h))},{"qtMaximized",w->isMaximized()},
        {"window",QJsonArray{int(rectangle.left),int(rectangle.top),int(rectangle.right),int(rectangle.bottom)}},
        {"visibleFrame",QJsonArray{int(visible.left),int(visible.top),int(visible.right),int(visible.bottom)}},
        {"client",QJsonArray{int(origin.x),int(origin.y),int(origin.x+client.right),int(origin.y+client.bottom)}},
        {"work",QJsonArray{int(monitor.rcWork.left),int(monitor.rcWork.top),int(monitor.rcWork.right),int(monitor.rcWork.bottom)}},
        {"monitor",QJsonArray{int(monitor.rcMonitor.left),int(monitor.rcMonitor.top),int(monitor.rcMonitor.right),int(monitor.rcMonitor.bottom)}}});
      check(label+" maximized client fills the work area",measured&&IsZoomed(h)&&!w->isFullScreen()&&
        qAbs(origin.x-monitor.rcWork.left)<=1&&qAbs(origin.y-monitor.rcWork.top)<=1&&
        qAbs(origin.x+client.right-monitor.rcWork.right)<=1&&qAbs(origin.y+client.bottom-monitor.rcWork.bottom)<=1);
      // GetWindowRect includes invisible native resize borders. Check the actual
      // visible DWM frame as well as the client, so an opaque taskbar overlap fails.
      check(label+" visible native frame excludes the taskbar",measured&&
        qAbs(visible.left-monitor.rcWork.left)<=1&&qAbs(visible.top-monitor.rcWork.top)<=1&&
        qAbs(visible.right-monitor.rcWork.right)<=1&&qAbs(visible.bottom-monitor.rcWork.bottom)<=1);
    };
    Tw::Settings().setValue("openPDFwithTeX",false);Research::setFocusMode(true);
    auto *source=new TeXDocumentWindow();source->editor()->setPlainText("first line\nsecond line\n");source->setModified(false);source->resize(900,700);source->show();
    auto *pdf=new PDFDocumentWindow(pdfPath);pdf->resize(900,700);pdf->show();settle();
    for(auto *w:{static_cast<QMainWindow*>(source),static_cast<QMainWindow*>(pdf)}) {
      const QString kind=w==source?"source":"PDF";
      check(kind+" blank client margin receives pointer motion",w->hasMouseTracking());
      for(const QString &state:{QString("normal"),QString("maximized"),QString("fullscreen")}) {
        if(state=="normal")w->showNormal();else if(state=="maximized")w->showMaximized();else if(w==pdf)pdf->toggleFullScreen();else w->showFullScreen();
        settle();dismiss(w);check(kind+" "+state+" controls start hidden",!visible(w));
        if(state=="maximized") {
          workArea(w,kind+" focus showMaximized");
        }
        if(state=="fullscreen"&&w==pdf)workArea(w,"PDF focus fullscreen command");
        moveNative(w,QPoint(w->width()-2,2));check(kind+" "+state+" native top-right motion reveals close",visible(w));
        // Returning to the same coordinate must reveal after editing hides it.
        dismiss(w);moveNative(w,QPoint(w->width()-2,2));check(kind+" "+state+" same-position re-entry reveals close",visible(w));
        if(w==source) {
          source->menuBar()->show();settle();dismiss(w);moveNative(w,QPoint(w->width()-2,2));
          check(kind+" "+state+" menu-height offset keeps corner reveal reachable",visible(w));source->menuBar()->hide();settle();
          Research::showFind(source);settle();auto *find=w->findChild<QToolBar *>("researchFindBar");check(kind+" "+state+" search row exists",find&&find->isVisible());
          if(find) {
            find->show();settle();moveNative(w,QPoint(w->width()-2,2));
            auto *chrome=w->findChild<QWidget *>("researchFocusChrome");auto *close=w->findChild<QToolButton *>("researchWindowClose");
            const QPoint approach(close->mapTo(w,close->rect().center()).x(),chrome->y()+4);
            moveNative(w,approach);check(kind+" "+state+" search row leaves a continuous path to window buttons",visible(w));
            find->hide();settle();
          }
          const QPoint firstLine=source->editor()->cursorRect(QTextCursor(source->editor()->document())).center();
          const QPoint firstLocal=source->editor()->viewport()->mapTo(w,firstLine);
          POINT firstNative{qRound(firstLocal.x()*w->devicePixelRatioF()),qRound(firstLocal.y()*w->devicePixelRatioF())};ClientToScreen(reinterpret_cast<HWND>(w->winId()),&firstNative);
          check(kind+" "+state+" first line never becomes a native drag target",SendMessageW(reinterpret_cast<HWND>(w->winId()),WM_NCHITTEST,0,MAKELPARAM(firstNative.x,firstNative.y))==HTCLIENT);
          QMouseEvent body(QEvent::MouseMove,QPointF(firstLine),QPointF(source->editor()->viewport()->mapToGlobal(firstLine)),Qt::NoButton,Qt::NoButton,Qt::NoModifier);
          QApplication::sendEvent(source->editor()->viewport(),&body);settle();check(kind+" "+state+" source movement keeps first line uncovered",!visible(w));
        }
        if(state=="fullscreen"&&w==pdf) {
          moveNative(w,QPoint(w->width()-10,3));pdf->findChild<QToolButton*>("researchWindowMaximize")->click();settle();
          check("PDF restore button exits through the fullscreen manager",!pdf->isFullScreen());
          auto *bar=pdf->findChild<QToolBar*>("toolBar");Research::setFocusMode(false);settle();
          check("PDF restore leaves normal toolbar available",bar&&bar->isVisible());Research::setFocusMode(true);settle();
        } else if(state=="maximized") {
          moveNative(w,QPoint(w->width()-10,3));w->findChild<QToolButton*>("researchWindowMaximize")->click();settle();
          check(kind+" maximized restore button returns to a normal window",!w->isMaximized()&&!w->isFullScreen());
        } else w->showNormal();
      }
    }
    for(bool focus:{false,true}) {
      Research::setFocusMode(focus);settle();pdf->showNormal();settle();
      auto *full=pdf->findChild<QAction*>("actionFull_Screen");full->trigger();settle();
      workArea(pdf,focus?"PDF focus fullscreen menu":"PDF normal fullscreen menu");
      check("fullscreen menu reflects work-area maximization",full->isChecked()&&pdf->isMaximized()&&!pdf->isFullScreen());
      QShortcut *escape=nullptr;
      for(auto *shortcut:pdf->findChildren<QShortcut*>())if(shortcut->parent()==pdf&&shortcut->key()==QKeySequence(Qt::Key_Escape)&&shortcut->isEnabled())escape=shortcut;
      check("expanded PDF has an enabled Escape restore",escape!=nullptr);
      if(escape)QMetaObject::invokeMethod(escape,"activated",Qt::DirectConnection);settle();
      check("Escape restores PDF and clears the fullscreen menu",!pdf->isMaximized()&&!pdf->isFullScreen()&&!full->isChecked());
      bool idleEscape=true;for(auto *shortcut:pdf->findChildren<QShortcut*>())if(shortcut->parent()==pdf&&shortcut->key()==QKeySequence(Qt::Key_Escape))idleEscape&=!shortcut->isEnabled();
      check("Escape cannot maximize an already restored PDF",idleEscape);
    }
    for(bool focus:{false,true}) {
      Research::setFocusMode(focus);settle();
      for(auto *w:{static_cast<QMainWindow*>(source),static_cast<QMainWindow*>(pdf)}) {
        for(bool system:{false,true}) {
          w->showNormal();settle();
          RECT normalBefore{};GetWindowRect(reinterpret_cast<HWND>(w->winId()),&normalBefore);
          if(focus){dismiss(w);moveNative(w,QPoint(w->width()-2,2));}
          if(system)PostMessageW(reinterpret_cast<HWND>(w->winId()),WM_SYSCOMMAND,SC_MAXIMIZE,0);
          else w->findChild<QToolButton*>("researchWindowMaximize")->click();
          settle();
          const QString label=QString(w==source?"source ":"PDF ")+(focus?"focus ":"normal ")+(system?"system maximize":"maximize button");
          workArea(w,label);
          if(focus)moveNative(w,QPoint(w->width()-2,2));
          w->findChild<QToolButton*>("researchWindowMaximize")->click();settle();
          check(label+" restore button returns to normal",!w->isMaximized()&&!w->isFullScreen());
          RECT normalAfter{};GetWindowRect(reinterpret_cast<HWND>(w->winId()),&normalAfter);
          geometry.append(QJsonObject{{"case",label+" restore"},{"normalBefore",QJsonArray{int(normalBefore.left),int(normalBefore.top),int(normalBefore.right),int(normalBefore.bottom)}},{"normalAfter",QJsonArray{int(normalAfter.left),int(normalAfter.top),int(normalAfter.right),int(normalAfter.bottom)}}});
          check(label+" restores the normal outer position and size",qAbs(normalBefore.left-normalAfter.left)<=2&&qAbs(normalBefore.top-normalAfter.top)<=2&&qAbs(normalBefore.right-normalAfter.right)<=2&&qAbs(normalBefore.bottom-normalAfter.bottom)<=2);
        }
      }
    }
    Research::setFocusMode(true);settle();
    check("window controls preserve the source",source->editor()->toPlainText()=="first line\nsecond line\n");
    source->setModified(false);QPointer<TeXDocumentWindow> sourceGuard(source);QPointer<PDFDocumentWindow> pdfGuard(pdf);
    moveNative(source,QPoint(source->width()-2,2));source->findChild<QToolButton*>("researchWindowClose")->click();settle();
    check("source close button runs the ordinary close controller",sourceGuard.isNull());
    moveNative(pdf,QPoint(pdf->width()-2,2));pdf->findChild<QToolButton*>("researchWindowClose")->click();settle();
    check("PDF close button runs the ordinary close controller",pdfGuard.isNull());
    if(sourceGuard)delete sourceGuard;
    if(pdfGuard)delete pdfGuard;
    settle();
    check("posted native client motion reaches the application dispatcher",probe.nativeMoves>=16);
    QSaveFile f(report);if(f.open(QIODevice::WriteOnly)){f.write(QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"nativeMotionMessages",probe.nativeMoves},{"geometry",geometry}}).toJson());f.commit();}app.exit(failures?1:0);
  });return app.exec();
}
