#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "TeXDocumentWindow.h"
#include "PDFDocumentWindow.h"
#include "ResearchAppearance.h"
#include "CompletingEdit.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFontInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPdfWriter>
#include <QPushButton>
#include <QStatusBar>
#include <QThread>
#include <QTimer>

static void settle(int ms=250){QElapsedTimer time;time.start();while(time.elapsed()<ms){QCoreApplication::processEvents();QThread::msleep(3);}}
// Include light antialiased stems. A hard dark-pixel threshold can mistake
// a complete fractional-DPI glyph for a truncated reference. Exclude the
// status background and its low-contrast border instead.
static QRect ink(const QImage &image,int width){QRect result;for(int y=1;y<image.height();++y)for(int x=0;x<qMin(image.width(),qRound(width*image.devicePixelRatio()));++x){const auto c=image.pixelColor(x,y);if(qMax(qMax(235-c.red(),245-c.green()),249-c.blue())>25)result|=QRect(x,y,1,1);}return result;}
int main(int argc,char **argv){
 if(!qEnvironmentVariable("TEXLINE_TEST_DESKTOP").startsWith("TeXlineValidation-"))return 2;
 const QString report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
 QTimer::singleShot(60,&app,[&]{
  const auto folder=QFileInfo(report).absolutePath();QJsonArray checks,details;int failures=0;
  auto check=[&](const QString &name,bool passed){checks.append(QJsonObject{{"name",name},{"passed",passed}});failures+=!passed;QFile f(report);if(f.open(QIODevice::WriteOnly))f.write(QJsonDocument(QJsonObject{{"checks",checks},{"details",details},{"failures",failures}}).toJson());};
  const auto pdf=folder+"/reading.pdf";{QPdfWriter writer(pdf);QPainter painter(&writer);painter.drawText(100,100,"Independent PDF fixture");}
  const bool traditional=qEnvironmentVariable("TEXLINE_TEST_LOCALE")=="zh_TW";
  const bool half=qEnvironmentVariableIsSet("TEXLINE_TEST_HALF");
  const QStringList languages=traditional?QStringList{"zh_TW"}:QStringList{"zh_CN","en_US"};
  for(const QString &language:languages){
   app.applyTranslation(language);Research::setFocusMode(false);
   for(bool source:{true,false}){
    QMainWindow *w=source?static_cast<QMainWindow*>(new TeXDocumentWindow):static_cast<QMainWindow*>(new PDFDocumentWindow(pdf));w->resize(half?600:1050,620);w->show();settle();
    const auto kind=source?QString("source"):QString("pdf");
    for(bool transient:{false,true}){
     const QString sample=language=="zh_TW"?QString("編譯完成 · 2 條提示，返回原始碼查看"):(language=="zh_CN"?QString("编译完成 · 2 条提示，返回源码查看"):QString("Compile complete · 2 notices · gjpqy ÅÉ"));
     auto *bar=w->statusBar();bar->clearMessage();
     if(transient)Research::showTransientStatusMessage(w,sample,10000);else bar->showMessage(sample);
     settle(450);auto *label=w->findChild<QLabel*>("researchFocusMessage");const auto font=transient?label->font():bar->font();
     const auto dpr=bar->devicePixelRatioF();const auto at=bar->mapTo(w,QPoint());auto actual=w->grab().toImage().copy(QRect(qRound(at.x()*dpr),qRound(at.y()*dpr),qRound(bar->width()*dpr),qRound(bar->height()*dpr)));actual.setDevicePixelRatio(dpr);
     QImage reference(QSize(500,80)*dpr,QImage::Format_ARGB32_Premultiplied);reference.setDevicePixelRatio(dpr);reference.fill(QColor("#ebf5f9"));QPainter painter(&reference);painter.setFont(font);painter.setPen(QColor("#48484a"));painter.drawText(QRect(12,0,480,80),Qt::AlignLeft|Qt::AlignVCenter|Qt::TextSingleLine,sample);painter.end();
     auto outline=reference;outline.fill(QColor("#ebf5f9"));{QPainter p(&outline);p.setRenderHint(QPainter::Antialiasing);QPainterPath path;path.addText(QPointF(12,45),font,sample);p.fillPath(path,QColor("#48484a"));}outline.save(folder+"/"+language+"-"+kind+"-outline.png");
     const auto a=ink(actual,490),b=ink(reference,490);const auto name=language+"-"+kind+(transient?"-notification":"-persistent");
     details.append(QJsonObject{{"name",name},{"windowWidth",w->width()},{"windowHeight",w->height()},{"fontFamily",font.family()},{"resolvedFamily",QFontInfo(font).family()},{"fontPixels",font.pixelSize()},{"fontHeight",QFontMetricsF(font).height()},{"ink",a.height()},{"referenceInk",b.height()},{"outlineInk",ink(outline,490).height()},{"barHeight",bar->height()},{"labelHeight",label->height()},{"labelWidth",label->width()},{"dpr",bar->devicePixelRatioF()}});
     check(name+": complete glyph height has room above and below",!a.isEmpty()&&qAbs(a.height()-b.height())<=1&&a.top()>=qRound(2*bar->devicePixelRatioF())&&a.bottom()<actual.height()-qRound(2*bar->devicePixelRatioF()));
     actual.copy(0,0,qMin(actual.width(),qRound(500*dpr)),actual.height()).save(folder+"/"+name+".png");reference.save(folder+"/"+name+"-reference.png");
     if(transient)check(name+": uses the actual animated notification",label&&label->isVisible()&&label->parentWidget()==bar);
     if(transient)check(name+": inline content keeps the whole text height",label->contentsRect().height()>=QFontMetricsF(font).height()&&label->contentsMargins().top()==0&&label->contentsMargins().bottom()==0);
     check(name+": original status font size is retained",font.pixelSize()==14);
     // Test each glyph separately: a tall intact neighbor must not hide a
     // clipped upper/lower stroke in a shorter character.
     for(const auto character:traditional?QString("編譯完成目前行總數選取中文警告"):QString("编译完成当前行总数选区中文警告")){
      const QString text(character);
      if(transient){w->setProperty("researchFocusStatus",text);Research::refreshFocusMessage(w);}else bar->showMessage(text);
      settle(12);const auto offset=bar->mapTo(w,QPoint());auto pixels=w->grab().toImage().copy(QRect(qRound(offset.x()*dpr),qRound(offset.y()*dpr),qRound(bar->width()*dpr),qRound(bar->height()*dpr)));pixels.setDevicePixelRatio(dpr);
      QImage glyph(QSize(100,80)*dpr,QImage::Format_ARGB32_Premultiplied);glyph.setDevicePixelRatio(dpr);glyph.fill(QColor("#ebf5f9"));{QPainter p(&glyph);p.setFont(font);p.setPen(QColor("#48484a"));p.drawText(QRect(12,0,80,80),Qt::AlignLeft|Qt::AlignVCenter|Qt::TextSingleLine,text);}
      const auto actualInk=ink(pixels,90),expectedInk=ink(glyph,90);
      const bool complete=!actualInk.isEmpty()&&qAbs(actualInk.height()-expectedInk.height())<=1&&actualInk.top()>=qRound(2*dpr)&&actualInk.bottom()<pixels.height()-qRound(2*dpr);
      details.append(QJsonObject{{"name",name+"-glyph-"+text},{"actualTop",actualInk.top()},{"actualBottom",actualInk.bottom()},{"actualHeight",actualInk.height()},{"referenceHeight",expectedInk.height()},{"imageHeight",pixels.height()},{"dpr",dpr}});
      if(!complete){pixels.copy(0,0,qMin(pixels.width(),qRound(100*dpr)),pixels.height()).save(folder+"/"+name+"-glyph-"+QString::number(character.unicode())+".png");glyph.save(folder+"/"+name+"-glyph-"+QString::number(character.unicode())+"-reference.png");}
      check(name+": complete individual glyph "+text,!actualInk.isEmpty()&&qAbs(actualInk.height()-expectedInk.height())<=1&&actualInk.top()>=qRound(2*dpr)&&actualInk.bottom()<pixels.height()-qRound(2*dpr));
     }
     if(transient){w->setProperty("researchFocusStatus",sample);Research::refreshFocusMessage(w);}else bar->showMessage(sample);
    }
    if(source)static_cast<TeXDocumentWindow*>(w)->setModified(false);delete w;
   }
   const auto file=folder+"/save-"+language+".tex";const QByteArray original="Independent saved content.\n";{QFile f(file);f.open(QIODevice::WriteOnly);f.write(original);}
   for(const auto choice:{QMessageBox::Cancel,QMessageBox::Discard,QMessageBox::Save}){
    auto *w=new TeXDocumentWindow(file);w->resize(half?600:800,620);w->show();settle();w->editor()->insertPlainText("Changed ");
    bool handled=false;
    QTimer::singleShot(100,&app,[&]{
     auto *box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());check(language+": real save prompt opens",box!=nullptr);if(!box)return;
     auto *ignore=box->button(QMessageBox::Discard);check(language+": discard action is labelled Ignore",ignore&&ignore->text().remove('&')==(language.startsWith("zh_")?"忽略":"Ignore"));
     auto *icon=box->findChild<QLabel*>("qt_msgboxex_icon_label");const auto size=icon?icon->pixmap().deviceIndependentSize():QSizeF();
     details.append(QJsonObject{{"dialogLanguage",language},{"iconWidth",size.width()},{"iconHeight",size.height()}});
     check(language+": warning uses a compact square icon",icon&&size==QSizeF(24,24)&&!icon->hasScaledContents());
     if(choice==QMessageBox::Cancel)box->grab().save(folder+"/save-dialog-"+language+".png");handled=true;box->button(choice)->click();
    });
    const bool closed=w->close();check(language+": correct close result for "+QString::number(choice),handled&&closed==(choice!=QMessageBox::Cancel));
    QFile f(file);f.open(QIODevice::ReadOnly);const auto saved=f.readAll();check(language+": save choice preserves its intended file behavior",choice==QMessageBox::Save?saved.contains("Changed "):saved==original);
    if(choice==QMessageBox::Cancel){w->setModified(false);w->close();}settle(50);
   }
  }
  app.exit(failures?1:0);
 });QTimer::singleShot(90000,&app,[&]{app.exit(92);});return app.exec();
}
