#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "PDFBackend.h"
#include <QCryptographicHash>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTimer>
#include <cmath>
int main(int argc,char **argv) {
  const QString report=QString::fromLocal8Bit(argv[1]);argc=1;
  TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
  QTimer::singleShot(80,&app,[&] {
    QJsonArray checks,images;int failures=0;
    const QString fixture=QCoreApplication::applicationDirPath()+"/fixtures/pdf/";
    auto check=[&](QString name,bool pass){checks.append(QJsonObject{{"name",name},{"passed",pass}});failures+=!pass;};
    for(const QString &name:{QString("base14-fonts"),QString("poppler-data"),QString::fromUtf8("中文文件-🧮")}) {
      auto doc=QtPDF::Backend::Document::newDocument(fixture+name+".pdf");
      check(name+": valid one-page PDF",doc&&doc->isValid()&&doc->numPages()==1);
      if(!doc||!doc->isValid())continue;
      auto page=doc->page(0).toStrongRef();check(name+": page exists",bool(page));if(!page)continue;
      const QImage image=page->renderToImage(150,150).convertToFormat(QImage::Format_RGB32);
      check(name+": nonempty rendered image",!image.isNull());
      if(image.isNull())continue;
      image.save(QFileInfo(report).absolutePath()+"/"+name+".png");
      const bool cjk=name=="poppler-data";
      const QImage ref=(QImage(fixture+(cjk?"poppler-data-1.png":"base14-fonts-1.png"))).convertToFormat(QImage::Format_RGB32);
      double difference=0;bool sameSize=image.size()==ref.size()&&!ref.isNull();
      if(sameSize){for(qsizetype i=0;i<image.sizeInBytes();++i)difference+=std::abs(int(image.constBits()[i])-int(ref.constBits()[i]));difference/=image.width()*image.height();}
      check(name+": rendering agrees with reference",sameSize&&difference<(cjk?3:15));
      if(cjk)check("CJK encoding maps preserve selected Chinese text",page->selectedText({QPolygonF(QRectF(0,75,95,15))})==QString::fromUtf8("香港交易"));
      else {
        check(name+": selected Latin text",page->selectedText({QPolygonF(QRectF(0,105,400,10))})=="The quick brown fox jumps over the lazy dog");
        const auto dingbats=image.copy(QRect(200,1468,840,30));bool ink=false;
        for(int y=0;y<dingbats.height();++y)for(int x=0;x<dingbats.width();++x)if(qGray(dingbats.pixel(x,y))<180)ink=true;
        check(name+": ZapfDingbats glyphs render",ink);
      }
      images.append(QJsonObject{{"name",name},{"difference",difference},{"sha256",QString::fromLatin1(QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char*>(image.constBits()),image.sizeInBytes()),QCryptographicHash::Sha256).toHex())}});
    }
    auto locked=QtPDF::Backend::Document::newDocument(fixture+"base14-fonts-locked.pdf");
    check("password-protected PDF remains locked",locked&&locked->isValid()&&locked->isLocked());
    check("wrong PDF password rejected",locked&&!locked->unlock("wrong"));
    check("valid PDF password unlocks",locked&&locked->unlock("123")&&!locked->isLocked());
    QSaveFile f(report);if(!f.open(QIODevice::WriteOnly)){app.exit(2);return;}f.write(QJsonDocument(QJsonObject{{"checks",checks},{"images",images},{"failures",failures}}).toJson());if(!f.commit()){app.exit(2);return;}app.exit(failures?1:0);
  });return app.exec();
}
