#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "PDFBackend.h"
#include "PDFAnnotations.h"
#include "PDFDocumentScene.h"
#include "PDFDocumentView.h"
#include "PDFPageProcessingThread.h"
#include "InfoWidgets.h"
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QSaveFile>
#include <QStyleOptionGraphicsItem>
#include <QThread>
#include <QTimer>
#include <QtConcurrent>
#include <atomic>
#include <functional>
class ProbePage : public QtPDF::Backend::Page {
public:
  std::atomic<int> annotationLoads{0};mutable std::atomic<bool> rendering{false};
  ProbePage(QtPDF::Backend::Document *doc,QSharedPointer<QReadWriteLock> lock):Page(doc,0,lock){}
  QSizeF pageSizeF() const override {return {595,842};}
  QList<QSharedPointer<QtPDF::Annotation::Link>> loadLinks() override {return {};}
  QList<QtPDF::Backend::SearchResult> search(const QString &,const QtPDF::Backend::SearchFlags &) const override {return {};}
  QImage renderToImage(double xres,double yres,QRect box={},bool cache=false) const override {
    rendering=true;QThread::msleep(35);QImage image(box.isEmpty()?QSize(595,842):box.size(),QImage::Format_RGB32);image.fill(Qt::white);
    if(cache)QtPDF::Backend::Document::pageCache().setImage(QtPDF::Backend::PDFPageTile(xres,yres,box,_parent,_n),QSharedPointer<QImage>::create(image),QtPDF::Backend::PDFPageCache::CURRENT);
    rendering=false;return image;
  }
  QList<QSharedPointer<QtPDF::Annotation::AbstractAnnotation>> loadAnnotations() override {
    ++annotationLoads;QThread::msleep(80);auto text=QSharedPointer<QtPDF::Annotation::Text>::create();text->setRect({20,30,15,15});text->setContents("Delayed annotation");return {text};
  }
};
class ProbeDocument : public QtPDF::Backend::Document {
public:
  mutable int fontLoads=0;
  QSharedPointer<ProbePage> probe;
  ProbeDocument():Document(QString()){_numPages=1;probe=QSharedPointer<ProbePage>::create(this,_docLock);_pages.append(probe);}
  bool isValid() const override{return true;}bool isLocked() const override{return false;}
  bool unlock(QString) override{return true;}void reload() override{}
  QList<QtPDF::Backend::PDFFontInfo> fonts() const override {++fontLoads;return {};}
};
class ProbeFonts : public QtPDF::PDFFontsInfoWidget {public:ProbeFonts():PDFFontsInfoWidget(nullptr){}using PDFFontsInfoWidget::initFromDocument;};
static bool waitFor(const std::function<bool()> &ready,int ms=5000){QElapsedTimer t;t.start();while(!ready()&&t.elapsed()<ms){QCoreApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QThread::msleep(1);}return ready();}
static void paint(QtPDF::PDFPageGraphicsItem *item){QImage image(595,842,QImage::Format_RGB32);image.fill(Qt::white);QPainter painter(&image);QStyleOptionGraphicsItem option;option.exposedRect=item->boundingRect();item->paint(&painter,&option,nullptr);}
int main(int argc,char **argv){const QString report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
  QTimer::singleShot(80,&app,[&]{QJsonArray checks;int failures=0;double firstPaint=0;
    auto save=[&]{QSaveFile f(report);if(f.open(QIODevice::WriteOnly)){f.write(QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"firstPaintMs",firstPaint}}).toJson());f.commit();}};
    auto check=[&](QString name,bool pass){checks.append(QJsonObject{{"name",name},{"passed",pass}});failures+=!pass;save();};
    auto doc=QSharedPointer<ProbeDocument>::create();auto scene=QSharedPointer<QtPDF::PDFDocumentScene>::create(doc,nullptr,72,72);scene->setWatchForDocumentChangesOnDisk(false);
    ProbeFonts fonts;fonts.initFromDocument(doc);QEvent language(QEvent::LanguageChange);QApplication::sendEvent(&fonts,&language);
    check("hidden font panel language changes avoid a complete PDF font scan",doc->fontLoads==0);
    fonts.show();QCoreApplication::processEvents();check("showing the font panel loads document fonts",doc->fontLoads==1);fonts.hide();
    auto *item=dynamic_cast<QtPDF::PDFPageGraphicsItem*>(scene->pageAt(0));QElapsedTimer clock;clock.start();paint(item);firstPaint=double(clock.nsecsElapsed())/1e6;
    check("first paint does not wait for an 80 ms annotation parse",firstPaint<50);
    check("delayed annotation reaches the GUI item",waitFor([&]{return item->childItems().size()==1;}));
    paint(item);paint(item);check("repaints do not duplicate markup or annotation requests",doc->probe->annotationLoads==1&&item->childItems().size()==1);
    auto real=QtPDF::Backend::Document::newDocument(QCoreApplication::applicationDirPath()+"/fixtures/pdf/annotations.pdf");
    check("real annotation fixture loads",real&&real->isValid());
    if(real&&real->isValid()) {auto realScene=QSharedPointer<QtPDF::PDFDocumentScene>::create(real,nullptr,72,72);realScene->setWatchForDocumentChangesOnDisk(false);auto *p=dynamic_cast<QtPDF::PDFPageGraphicsItem*>(realScene->pageAt(0));paint(p);check("all eight supported PDF markup annotations remain available",waitFor([&]{int count=0;for(auto *child:p->childItems())count+=child->type()==QtPDF::PDFMarkupAnnotationGraphicsItem::Type;return count==8;}));}
    bool complete=true;
    for(int n=0;n<32;++n){auto parallelDoc=QtPDF::Backend::Document::newDocument(QCoreApplication::applicationDirPath()+"/fixtures/pdf/annotations.pdf");if(!parallelDoc||!parallelDoc->isValid()){complete=false;break;}auto page=parallelDoc->page(0).toStrongRef();QList<QFuture<int>> requests;for(int i=0;i<8;++i)requests.append(QtConcurrent::run([page]{return int(page->loadAnnotations().size());}));for(auto &request:requests){request.waitForFinished();complete&=request.result()==8;}}
    check("256 concurrent annotation requests each receive a complete eight-item list",complete);
    if(qEnvironmentVariableIsSet("PDF_LIFECYCLE_PROBE")){
      bool rebuilt=true;
      for(int n=0;n<24;++n){auto lifeDoc=QSharedPointer<ProbeDocument>::create();auto lifeScene=QSharedPointer<QtPDF::PDFDocumentScene>::create(lifeDoc,nullptr,72,72);lifeScene->setWatchForDocumentChangesOnDisk(false);auto *p=dynamic_cast<QtPDF::PDFPageGraphicsItem*>(lifeScene->pageAt(0));paint(p);rebuilt&=waitFor([&]{return lifeDoc->probe->rendering.load();});lifeScene->setResolution(73,73);p=dynamic_cast<QtPDF::PDFPageGraphicsItem*>(lifeScene->pageAt(0));paint(p);rebuilt&=waitFor([&]{for(const auto &tile:QtPDF::Backend::Document::pageCache().tiles())if(tile.doc==lifeDoc.data()&&QtPDF::Backend::Document::pageCache().getStatus(tile)==QtPDF::Backend::PDFPageCache::PLACEHOLDER)return false;return p->childItems().size()==1;});lifeScene.clear();lifeDoc->processingThread().clearWorkStack();QCoreApplication::processEvents();}
      check("24 scene rebuilds retire callbacks while the PDF document remains alive",rebuilt);
    }
    scene.clear();doc->processingThread().clearWorkStack();save();app.exit(failures?1:0);
  });return app.exec();}
