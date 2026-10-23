#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#define USE_POPPLERQT
class QDomNode;
#include "TWApp.h"
#include "PDFBackend.h"
#include "PDFAnnotations.h"
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSemaphore>
#include <QThread>
#include <QTimer>
#include <QtConcurrent>
#include <thread>

class ContendedDocument : public QtPDF::Backend::PopplerQt::Document {
public:
  using Document::Document;
  QMutex *gate() { return _poppler_docLock; }
};
int main(int argc,char **argv) {
  const QString report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
  QTimer::singleShot(80,&app,[&] {
    QJsonArray checks,times;int failures=0;
    auto check=[&](QString name,bool passed){checks.append(QJsonObject{{"name",name},{"passed",passed}});failures+=!passed;};
    const QString path=QCoreApplication::applicationDirPath()+"/fixtures/pdf/link-metadata.pdf";
    for(bool annotations:{false,true}) {
      auto doc=QSharedPointer<ContendedDocument>::create(path);check("real link PDF loads",doc->isValid());
      if(!doc->isValid())continue;
      auto page=doc->page(0).toStrongRef();QSemaphore locked,started;
      std::thread holder([&]{QMutexLocker lock(doc->gate());locked.release();QThread::msleep(400);});locked.acquire();
      auto future=QtConcurrent::run([&]{started.release();return annotations?int(page->loadAnnotations().size()):int(page->loadLinks().size());});started.acquire();QThread::msleep(60);
      QElapsedTimer clock;clock.start();const auto size=page->pageSizeF();const double ms=double(clock.nsecsElapsed())/1.e6;
      times.append(QJsonObject{{"annotations",annotations},{"pageSizeMs",ms}});
      check(annotations?"annotation worker waiting for Poppler keeps page geometry responsive":"link worker waiting for Poppler keeps page geometry responsive",size==QSizeF(595,842)&&ms<180);
      holder.join();future.waitForFinished();
      const auto links=page->loadLinks();check("both URI and internal PDF links remain available",links.size()==2&&links[0]->actionOnActivation()&&links[1]->actionOnActivation());
      bool metadata=links.size()==2;
      for(const auto &link:links)metadata&=!link->name().isEmpty()&&!link->contents().isEmpty()&&!link->page().isNull()&&!link->rect().isEmpty();
      check("links retain their annotation text, name, page and clickable area",metadata);
    }
    bool complete=true;
    for(int n=0;n<24;++n){auto doc=QSharedPointer<ContendedDocument>::create(path);auto page=doc->page(0).toStrongRef();QList<QFuture<int>> workers;for(int i=0;i<8;++i)workers.append(QtConcurrent::run([page]{return int(page->loadLinks().size());}));for(auto &worker:workers){worker.waitForFinished();complete&=worker.result()==2;}}
    check("192 concurrent link requests each receive the complete list",complete);
    QSaveFile file(report);if(file.open(QIODevice::WriteOnly)){file.write(QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"times",times}}).toJson());file.commit();}
    app.exit(failures?1:0);
  });return app.exec();
}
