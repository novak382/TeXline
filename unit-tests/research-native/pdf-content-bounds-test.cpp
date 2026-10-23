#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "PDFBackend.h"
#include "PDFDocumentScene.h"
#include "PDFDocumentView.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardItemModel>
#include <QTimer>
#include <cmath>
class BoundsPage : public QtPDF::Backend::Page {
public:
  QImage image; QSizeF size{600,800}; mutable int renders=0;
  BoundsPage(QtPDF::Backend::Document *doc,QSharedPointer<QReadWriteLock> lock):Page(doc,0,lock){image=QImage(101,99,QImage::Format_RGB32);image.fill(Qt::white);}
  QSizeF pageSizeF() const override{return size;}
  QImage renderToImage(double,double,QRect={},bool=false) const override{++renders;return image;}
  QList<QSharedPointer<QtPDF::Annotation::Link>> loadLinks() override{return {};}
  QList<QtPDF::Backend::SearchResult> search(const QString &,const QtPDF::Backend::SearchFlags &) const override{return {};}
};
class BoundsDocument : public QtPDF::Backend::Document {
public:
  QSharedPointer<BoundsPage> probe;
  bool mutableLayers=false;mutable QStandardItemModel layers;
  BoundsDocument():Document(QString()){_numPages=1;probe=QSharedPointer<BoundsPage>::create(this,_docLock);_pages.append(probe);}
  bool isValid() const override{return true;}bool isLocked() const override{return false;}bool unlock(QString) override{return true;}void reload() override{}
  QAbstractItemModel *optionalContentModel() const override{return mutableLayers?&layers:nullptr;}
};
int main(int argc,char **argv){const QString report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
  QTimer::singleShot(80,&app,[&]{QJsonArray checks;int failures=0;auto check=[&](QString name,bool pass){checks.append(QJsonObject{{"name",name},{"passed",pass}});failures+=!pass;};
    auto blank=QSharedPointer<BoundsDocument>::create();check("blank page has an empty nonnegative content box",blank->probe->getContentBoundingBox()==QRectF());
    for(QPoint corner:{QPoint(100,0),QPoint(0,98),QPoint(100,98)}){auto doc=QSharedPointer<BoundsDocument>::create();doc->probe->image.setPixelColor(corner,Qt::black);check(QString("different corner %1,%2 retains the whole page").arg(corner.x()).arg(corner.y()),doc->probe->getContentBoundingBox()==QRectF(0,0,600,800));}
    auto ink=QSharedPointer<BoundsDocument>::create();for(int y=20;y<=40;++y)for(int x=10;x<=30;++x)ink->probe->image.setPixelColor(x,y,Qt::black);QRectF expected(10.*600/101,20.*800/99,21.*600/101,21.*800/99);check("content coordinates use actual raster dimensions",ink->probe->getContentBoundingBox()==expected);
    for(int i=0;i<32;++i){ink->probe->getContentBoundingBox();}
    check("repeated fit-content requests reuse immutable page bounds",ink->probe->renders==1);
    for(int i=0;i<32;++i){blank->probe->getContentBoundingBox();}
    check("empty page bounds are cached too",blank->probe->renders==1);
    auto layered=QSharedPointer<BoundsDocument>::create();layered->mutableLayers=true;layered->probe->image=ink->probe->image;check("visible optional-layer content has the correct bounds",layered->probe->getContentBoundingBox()==expected);layered->probe->image.fill(Qt::white);check("optional-layer visibility changes invalidate the rendered bounds",layered->probe->getContentBoundingBox()==QRectF()&&layered->probe->renders==2);
    auto invalid=QSharedPointer<BoundsDocument>::create();invalid->probe->size={0,800};check("invalid page dimensions do not render",invalid->probe->getContentBoundingBox().isEmpty()&&invalid->probe->renders==0);
    auto retry=QSharedPointer<BoundsDocument>::create();retry->probe->image=QImage();check("failed rendering returns empty bounds",retry->probe->getContentBoundingBox().isEmpty());retry->probe->image=ink->probe->image;check("failed rendering can be retried",retry->probe->getContentBoundingBox()==expected&&retry->probe->renders==2);
    auto scene=QSharedPointer<QtPDF::PDFDocumentScene>::create(blank,nullptr,72,72);scene->setWatchForDocumentChangesOnDisk(false);QtPDF::PDFDocumentView view;view.resize(800,600);view.setScene(scene);view.show();app.processEvents();view.zoomFitContentWidth();check("blank-page fit-content produces a finite usable zoom",std::isfinite(view.zoomLevel())&&view.zoomLevel()>0.1&&view.zoomLevel()<10);
    QSaveFile file(report);if(file.open(QIODevice::WriteOnly)){file.write(QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"boundsRenders",ink->probe->renders}}).toJson());file.commit();}app.exit(failures?1:0);
  });return app.exec();}
