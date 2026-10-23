// Rebuilding the window list must release obsolete actions and separators.
#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "TeXDocumentWindow.h"
#include <QMenu>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTimer>
#include <QPointer>
#include <windows.h>
#include <shellapi.h>
int main(int argc,char **argv){
 if(!qEnvironmentVariable("TEXLINE_TEST_DESKTOP").startsWith("TeXlineValidation-"))return 2;
 int n=0;auto **args=CommandLineToArgvW(GetCommandLineW(),&n);const QString report=QString::fromWCharArray(args[1]);LocalFree(args);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
 QTimer::singleShot(80,&app,[&]{
  QJsonArray checks;int failures=0;auto check=[&](QString name,bool good){failures+=!good;checks.append(QJsonObject{{"name",name},{"passed",good}});};
  auto *a=new TeXDocumentWindow,*b=new TeXDocumentWindow;
  auto *menu=a->findChild<QMenu*>("menuWindow");check("actual window menu exists",menu);
  if(menu){
   QMetaObject::invokeMethod(a,"updateWindowMenu",Qt::DirectConnection);
   const int before=menu->findChildren<QObject*>().size(),visible=menu->actions().size();
   QPointer<QAction> previous;for(auto *action:menu->actions())if(action->metaObject()->className()==QByteArray("SelWinAction")){previous=action;break;}
   auto *shared=a->findChild<QAction*>("actionStack");QPointer<QAction> sharedGuard(shared);
   for(int i=0;i<400;++i)QMetaObject::invokeMethod(a,"updateWindowMenu",Qt::DirectConnection);
   check("superseded window actions are destroyed",previous.isNull());
   check("window menu retains bounded objects",menu->findChildren<QObject*>().size()<=before+2);
   check("visible window choices are stable",menu->actions().size()==visible);
   check("existing shared commands survive refresh",shared&&sharedGuard==shared&&menu->actions().contains(shared));
  }
  a->setModified(false);b->setModified(false);delete a;delete b;
  QSaveFile file(report);if(file.open(QIODevice::WriteOnly)){file.write(QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures}}).toJson());file.commit();}app.exit(failures?1:0);
 });return app.exec();
}
