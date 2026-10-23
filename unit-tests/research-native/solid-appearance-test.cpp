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
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMenu>
#include <QSaveFile>
#include <QThread>
#include <QTimer>
#include <windows.h>
#include <dwmapi.h>
static bool put(const QString &path,const QByteArray &bytes) {
  QDir().mkpath(QFileInfo(path).absolutePath());QSaveFile file(path);
  return file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size()&&file.commit();
}
static void settle(int ms=260) {
  QElapsedTimer timer;timer.start();
  while(timer.elapsed()<ms){QCoreApplication::processEvents();QThread::msleep(2);}
}
static bool menuRemoved(QMainWindow *window) {
  for(auto *menu:window->findChildren<QMenu *>())QMetaObject::invokeMethod(menu,"aboutToShow",Qt::DirectConnection);
  for(auto *action:window->findChildren<QAction *>())
    if(action->objectName()=="researchAppearanceToggle"||action->text().contains(QStringLiteral("磨砂外观"))||action->text().contains("Frosted appearance"))return false;
  return true;
}
static bool solid(QWidget *window) {
  return window->property("researchMaterial").toString()=="Solid"&&!window->property("researchDwmExtended").toBool()&&
    window->property("researchDwmAttribute38").toInt()==1&&!window->testAttribute(Qt::WA_TranslucentBackground);
}
static bool workArea(QMainWindow *window) {
  const auto handle=reinterpret_cast<HWND>(window->winId());RECT frame{},client{};POINT origin{};
  MONITORINFO monitor{};monitor.cbSize=sizeof(monitor);
  return SUCCEEDED(DwmGetWindowAttribute(handle,DWMWA_EXTENDED_FRAME_BOUNDS,&frame,sizeof(frame)))&&GetClientRect(handle,&client)&&ClientToScreen(handle,&origin)&&GetMonitorInfoW(MonitorFromWindow(handle,MONITOR_DEFAULTTONEAREST),&monitor)&&
    EqualRect(&frame,&monitor.rcWork)&&origin.x==monitor.rcWork.left&&origin.y==monitor.rcWork.top&&origin.x+client.right==monitor.rcWork.right&&origin.y+client.bottom==monitor.rcWork.bottom;
}
int main(int argc,char **argv) {
  const QString report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
  QTimer::singleShot(100,&app,[&] {
    const QString folder=QFileInfo(report).absolutePath();QJsonArray checks;int failures=0;
    auto check=[&](const QString &name,bool passed){checks.append(QJsonObject{{"name",name},{"passed",passed}});failures+=!passed;
      put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"physicalInputVerified",false},{"dropboxTouched",false}}).toJson());};
    Tw::Settings().setValue("openPDFwithTeX",false);
    for(bool legacy:{true,false}) {
      const QString prefix=legacy?"legacy-on-zh":"legacy-off-en",path=folder+"/"+prefix+"/main.tex";
      const QByteArray source="\\documentclass{article}\n\\begin{document}\nIndependent paper.\n\\end{document}\n";
      Tw::Settings().setValue("researchFrostedAppearance",legacy);Research::setFocusMode(false);app.applyTranslation(legacy?"zh_CN":"en_US");
      check(prefix+" independent source and PDF",put(path,source)&&QFile::copy(QCoreApplication::applicationDirPath()+"/fixtures/main/main.pdf",QFileInfo(path).absolutePath()+"/main.pdf"));
      auto *window=new TeXDocumentWindow(path);window->resize(960,720);window->show();window->goToPreview();settle();
      auto *pdf=window->pdfDocument();check(prefix+" preview opened",pdf!=nullptr);
      if(pdf){pdf->resize(900,700);pdf->show();}
      const auto originalHandle=window->winId();
      for(bool focus:{false,true}) {
        Research::setFocusMode(focus);settle();
        for(QMainWindow *view:{static_cast<QMainWindow *>(window),static_cast<QMainWindow *>(pdf)})if(view) {
          const QString label=prefix+(view==window?" source":" PDF")+(focus?" focus":" normal");
          check(label+" removed from all settings menus",menuRemoved(view));
          check(label+" opaque native surface",solid(view));
          const QImage image=view->grab().toImage();const QColor top=image.pixelColor(image.width()/2,2);
          check(label+" opaque paper header",top.alpha()==255&&top.red()>225&&top.green()>235&&top.blue()>240);
          view->grab().save(folder+"/"+label.simplified().replace(' ','-')+".png");
          view->showMaximized();settle(80);check(label+" maximized frame fits work area",workArea(view));view->showNormal();settle(80);
        }
        auto *editor=window->editor();const QString original=editor->toPlainText();editor->insertPlainText("x");editor->undo();
        check(prefix+(focus?" focus":" normal")+" editing and undo preserved",editor->toPlainText()==original&&!window->isModified());
      }
      {
        Research::MaterialDialog palette(window);palette.setObjectName("researchPalette");palette.resize(500,260);palette.show();settle(60);
        check(prefix+" command palette opaque",solid(&palette)&&palette.grab().toImage().pixelColor(20,20).alpha()==255);
      }
      QEvent theme(QEvent::ThemeChange);QCoreApplication::sendEvent(window,&theme);settle(60);
      check(prefix+" theme refresh keeps native window and solid surface",window->winId()==originalHandle&&solid(window));
      check(prefix+" legacy preference not migrated or rewritten",Tw::Settings().value("researchFrostedAppearance").toBool()==legacy);
      if(pdf)pdf->close();
      window->setModified(false);window->close();settle(30);
    }
    app.exit(failures?1:0);
  });return app.exec();
}
