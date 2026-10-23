#include "ActualAuthoringRecording.h"
#include <QThreadPool>
#include <QRunnable>
#include <QSemaphore>
#include <QThread>
#include <atomic>
int main(int argc,char **argv){
 const auto report=argument(1);argc=1;std::atomic_bool complete{false};QSemaphore started;bool finishedBeforeAppDestruction=false;
 {
  TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
  QObject::connect(&app,&QObject::destroyed,[&]{finishedBeforeAppDestruction=complete.load();});
  QTimer::singleShot(60,&app,[&]{
   QThreadPool::globalInstance()->start(QRunnable::create([&]{started.release();QThread::msleep(350);complete.store(true);}));
   started.acquire();app.exit(0);
  });app.exec();
 }
 const QJsonArray checks{QJsonObject{{"name","in-flight worker finishes before application destruction"},{"passed",finishedBeforeAppDestruction}}};
 put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",finishedBeforeAppDestruction?0:1}}).toJson());
 return finishedBeforeAppDestruction?0:1;
}
