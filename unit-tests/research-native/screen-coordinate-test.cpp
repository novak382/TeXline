#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "TeXDocumentWindow.h"
#include "PDFDocumentWindow.h"
#include "ResearchAppearance.h"
#include "Settings.h"
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QScreen>
#include <QThread>
#include <QTimer>
#include <QWindow>
#include <windows.h>
static void settle(int ms){QElapsedTimer t;t.start();while(t.elapsed()<ms){QCoreApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QThread::msleep(3);}}
int main(int argc,char **argv){QString report=QString::fromLocal8Bit(argv[1]),pdf=QString::fromLocal8Bit(argv[2]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
QTimer::singleShot(100,&app,[&]{
 QJsonArray checks,samples;int failures=0;auto check=[&](QString name,bool pass){checks.append(QJsonObject{{"name",name},{"passed",pass}});failures+=!pass;};
 Tw::Settings().setValue("openPDFwithTeX",false);Tw::Settings().setValue("researchFocusMode",false);auto *source=new TeXDocumentWindow();auto *preview=new PDFDocumentWindow(pdf);
 // A new untitled source starts maximized. Resizing that state directly
 // clears Qt's flag while Windows can retain WS_MAXIMIZE; explicitly restore
 // it just as the window control does before testing ordinary window moves.
 for(auto *w:{static_cast<QMainWindow*>(source),static_cast<QMainWindow*>(preview)}){
  w->showNormal();w->resize(500,400);settle(100);
  check(QString("%1 explicitly restores the native and Qt window state before resize").arg(w==source?"source":"PDF"),!w->isMaximized()&&!IsZoomed(reinterpret_cast<HWND>(w->winId())));
 }
 settle(150);const auto screens=QGuiApplication::screens();
 for(int n=0;n<screens.size();++n){auto *target=screens[n];const auto area=target->availableGeometry();source->move(area.topLeft()+QPoint(24,36));preview->move(area.topLeft()+QPoint(qMin(550,qMax(30,area.width()-510)),100));settle(180);
  for(bool focus:{false,true}){Research::setFocusMode(focus);settle(200);
   for(auto *w:{static_cast<QMainWindow*>(source),static_cast<QMainWindow*>(preview)}){
    const HWND handle=reinterpret_cast<HWND>(w->winId());RECT client{};GetClientRect(handle,&client);POINT origin{0,0};ClientToScreen(handle,&origin);MONITORINFO monitor{};monitor.cbSize=sizeof(monitor);GetMonitorInfoW(MonitorFromWindow(handle,MONITOR_DEFAULTTONEAREST),&monitor);
    const auto qt=w->mapToGlobal(QPoint()),screen=w->windowHandle()->screen()->geometry().topLeft();const qreal dpr=w->devicePixelRatioF();const int dx=qAbs(monitor.rcMonitor.left+qRound((qt.x()-screen.x())*dpr)-origin.x),dy=qAbs(monitor.rcMonitor.top+qRound((qt.y()-screen.y())*dpr)-origin.y),dw=qAbs(qRound(w->width()*dpr)-client.right),dh=qAbs(qRound(w->height()*dpr)-client.bottom);
    const QString role=w==source?"source":"PDF",mode=focus?"focus":"normal";check(QString("%1 %2 on monitor %3 agrees with native client coordinates within one pixel").arg(role,mode).arg(n),dx<=1&&dy<=1&&dw<=1&&dh<=1);check(QString("%1 %2 is actually on requested monitor %3").arg(role,mode).arg(n),w->windowHandle()->screen()==target);
    samples.append(QJsonObject{{"role",role},{"mode",mode},{"screen",n},{"targetScreen",target->name()},{"dpr",dpr},{"qtOrigin",QJsonArray{qt.x(),qt.y()}},{"qtSize",QJsonArray{w->width(),w->height()}},{"qtWindowSize",QJsonArray{w->windowHandle()->width(),w->windowHandle()->height()}},{"nativeClientSize",QJsonArray{int(client.right),int(client.bottom)}},{"maximized",w->isMaximized()},{"nativeMaximized",bool(IsZoomed(handle))},{"minimized",w->isMinimized()},{"qtScreenOrigin",QJsonArray{screen.x(),screen.y()}},{"nativeOrigin",QJsonArray{int(origin.x),int(origin.y)}},{"nativeScreenOrigin",QJsonArray{int(monitor.rcMonitor.left),int(monitor.rcMonitor.top)}},{"pixelErrors",QJsonArray{dx,dy,dw,dh}}});w->grab().save(QFileInfo(report).absolutePath()+QString("/monitor-%1-%2-%3.png").arg(n).arg(role,mode));
   }
  }
 }
 source->setModified(false);source->close();preview->close();settle(100);QSaveFile file(report);if(file.open(QIODevice::WriteOnly)){file.write(QJsonDocument(QJsonObject{{"checks",checks},{"samples",samples},{"screens",screens.size()},{"failures",failures}}).toJson());file.commit();}app.exit(failures?1:0);
});return app.exec();}
