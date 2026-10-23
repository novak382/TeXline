#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "PDFDocumentWindow.h"
#include "TeXDocumentWindow.h"
#include "ResearchAppearance.h"
#include <QAction>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPdfWriter>
#include <QSpinBox>
#include <QStatusBar>
#include <QThread>
#include <QTimer>
#include <windows.h>
#include <shellapi.h>

static void settle(int ms=200){QElapsedTimer t;t.start();while(t.elapsed()<ms){QCoreApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QThread::msleep(2);}}
static void key(QWidget *w,int code,const QString &text={}){QKeyEvent down(QEvent::KeyPress,code,Qt::NoModifier,text),up(QEvent::KeyRelease,code,Qt::NoModifier,text);QApplication::sendEvent(w,&down);QApplication::sendEvent(w,&up);}
static void pdf(const QString &path,int pages){QPdfWriter writer(path);writer.setPageSize(QPageSize(QPageSize::A4));QPainter painter(&writer);for(int i=0;i<pages;++i){if(i)writer.newPage();painter.drawText(QRect(100,100,2000,500),QString("Independent page %1").arg(i+1));}}
static QRect ink(const QImage &image,int logicalWidth){
 QRect bounds;const int limit=qMin(image.width(),qRound(logicalWidth*image.devicePixelRatio()));
 for(int y=1;y<image.height();++y)for(int x=0;x<limit;++x){const auto c=image.pixelColor(x,y);if(c.red()<170&&c.green()<170&&c.blue()<170)bounds|=QRect(x,y,1,1);}return bounds;
}
int main(int argc,char **argv){
 if(!qEnvironmentVariable("TEXLINE_TEST_DESKTOP").startsWith("TeXlineValidation-"))return 2;
 int n=0;auto args=CommandLineToArgvW(GetCommandLineW(),&n);const auto report=QString::fromWCharArray(args[1]);LocalFree(args);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
 QTimer::singleShot(100,&app,[&]{
  QJsonArray checks,geometry;int failures=0;const auto folder=QFileInfo(report).absolutePath();
  auto check=[&](const QString &name,bool passed){checks.append(QJsonObject{{"name",name},{"passed",passed}});failures+=!passed;QFile f(report);if(f.open(QIODevice::WriteOnly))f.write(QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"geometry",geometry}}).toJson());};
  pdf(folder+"/reading.pdf",55);pdf(folder+"/long.pdf",1055);
  for(const auto &language:{QString("zh_CN"),QString("en_US")}){
   app.applyTranslation(language);Research::setFocusMode(false);auto *p=new PDFDocumentWindow(folder+"/reading.pdf");p->resize(960,720);p->show();settle(400);QApplication::setActiveWindow(p);p->widget()->setFocus();
   auto *page=p->findChild<QSpinBox *>("researchPdfPageField");auto *zoom=p->findChild<QDoubleSpinBox *>("researchPdfZoomField");auto *total=p->findChild<QLabel *>("researchPdfPageTotal");auto *group=p->findChild<QWidget *>("researchPdfPageControl");
   check(language+": compact fields and separate total exist",page&&zoom&&total&&group);if(!page||!zoom||!total||!group){delete p;continue;}
   p->widget()->goToPage(2);p->widget()->setZoomLevel(.9569);settle();
   check(language+": precise scale and independent total are preserved",qAbs(zoom->value()-95.69)<.001&&page->value()==3&&page->suffix().isEmpty()&&total->text()=="/ 55");
   check(language+": sample controls use compact widths",zoom->width()>=72&&zoom->width()<=80&&page->width()>=32&&page->width()<=40&&group->width()<=78);
   auto fits=[](QAbstractSpinBox *field){auto *line=field->findChild<QLineEdit *>();return line&&line->contentsRect().width()>=QFontMetrics(line->font()).horizontalAdvance(line->text());};
   check(language+": editable values fit without clipping",fits(page)&&fits(zoom));
   auto inside=[&](QWidget *field){return p->statusBar()->rect().contains(QRect(field->mapTo(p->statusBar(),QPoint()),field->size()));};
   check(language+": controls share a 22px row inside the compact adaptive bar",p->statusBar()->height()>=24&&p->statusBar()->height()<=28&&page->height()==22&&zoom->height()==22&&inside(page)&&inside(zoom)&&inside(total)&&page->mapTo(p->statusBar(),QPoint()).y()==zoom->mapTo(p->statusBar(),QPoint()).y());
   geometry.append(QJsonObject{{"language",language},{"devicePixelRatio",p->devicePixelRatioF()},{"zoomWidth",zoom->width()},{"pageWidth",page->width()},{"pageGroupWidth",group->width()},{"barHeight",p->statusBar()->height()},{"pageHeight",page->height()},{"zoomHeight",zoom->height()}});
   p->statusBar()->grab().save(folder+"/status-"+language+".png");p->grab().save(folder+"/pdf-"+language+".png");
   auto textFit=[&](QMainWindow *window,const QString &kind){
    auto *bar=window->statusBar();const QString sample=language=="zh_CN"?QString("无法得到 SyncTeX 数据  编辑完成"):QString("SyncTeX unavailable  gjpqy ÅÉ");bar->showMessage(sample);settle();
    const auto actual=bar->grab().toImage();QImage reference(QSize(420,64)*bar->devicePixelRatioF(),QImage::Format_ARGB32_Premultiplied);reference.setDevicePixelRatio(bar->devicePixelRatioF());reference.fill(QColor("#ebf5f9"));QPainter painter(&reference);painter.setFont(bar->font());painter.setPen(bar->palette().color(QPalette::WindowText));painter.drawText(QRect(6,0,410,64),Qt::AlignLeft|Qt::AlignVCenter|Qt::TextSingleLine,sample);painter.end();
    const auto rendered=ink(actual,410),complete=ink(reference,410);const qreal dpr=bar->devicePixelRatioF();
    check(language+": "+kind+" text retains the full reference ink height",!rendered.isEmpty()&&qAbs(rendered.height()-complete.height())<=1&&rendered.top()>=qRound(2*dpr)&&rendered.bottom()<actual.height()-qRound(2*dpr));
    check(language+": "+kind+" uses readable text without increasing the bar",bar->font().pixelSize()==14&&bar->height()>=24&&bar->height()<=28);
    geometry.append(QJsonObject{{"kind",kind},{"language",language},{"font",bar->font().family()},{"actualInkHeight",rendered.height()},{"referenceInkHeight",complete.height()},{"inkTop",rendered.top()},{"inkBottom",rendered.bottom()}});actual.save(folder+"/text-"+kind+"-"+language+".png");reference.save(folder+"/text-reference-"+kind+"-"+language+".png");
   };
   textFit(p,"pdf");auto *source=new TeXDocumentWindow();source->resize(960,620);source->show();settle();textFit(source,"source");source->setModified(false);delete source;QApplication::setActiveWindow(p);
   if(qEnvironmentVariableIsSet("TEXLINE_STATUS_FONT_PROBE")&&language=="zh_CN"){
    auto *bar=p->statusBar();bar->showMessage("无法得到 SyncTeX 数据  gjpqy ÅÉ");const auto previous=bar->styleSheet();
    int index=0;for(const QString &family:{QString(),QString("Microsoft YaHei UI"),QString("Segoe UI")}){
     bar->setStyleSheet(family.isEmpty()?previous:QString("QStatusBar {font-family:'%1';font-size:13px;}").arg(family));settle();bar->grab().save(folder+QString("/font-probe-%1.png").arg(index++));
    }bar->setStyleSheet(previous);settle();
   }
   page->setFocus();page->selectAll();key(page,Qt::Key_5,"5");key(page,Qt::Key_4,"4");
   check(language+": typing does not navigate before confirmation",p->widget()->currentPage()==2);key(page,Qt::Key_Return);settle();
   check(language+": Enter navigates without a modal",p->widget()->currentPage()==53&&!QApplication::activeModalWidget());
   zoom->setFocus();zoom->selectAll();for(QChar c:QString("125.50"))key(zoom,c=='.'?Qt::Key_Period:int(c.unicode()),QString(c));key(zoom,Qt::Key_Return);settle();check(language+": precise zoom editing is retained",qAbs(p->widget()->zoomLevel()-1.255)<.0001);
   Research::setFocusMode(true);settle();check(language+": focus mode hides the whole status bar",p->statusBar()->isHidden());
   p->findChild<QAction *>("actionGo_to_Page")->trigger();settle();check(language+": page entry reveals the compact group",p->statusBar()->isVisible()&&page->isVisible()&&total->isVisible()&&inside(total));
   page->selectAll();key(page,Qt::Key_2,"2");key(page,Qt::Key_Escape);settle();check(language+": Escape restores page and hides the focus row",p->statusBar()->isHidden()&&p->widget()->currentPage()==53&&page->value()==54);
   QMetaObject::invokeMethod(p,"doScaleDialog");settle();zoom->selectAll();key(zoom,Qt::Key_2,"2");key(zoom,Qt::Key_Escape);settle();check(language+": Escape preserves the actual scale",p->statusBar()->isHidden()&&qAbs(p->widget()->zoomLevel()-1.255)<.0001&&qAbs(zoom->value()-125.5)<.001);
   const int shortWidth=page->width();Research::setFocusMode(false);settle();delete p;
   auto *longPdf=new PDFDocumentWindow(folder+"/long.pdf");longPdf->resize(800,620);longPdf->show();settle(400);longPdf->widget()->goToPage(1054);settle();
   auto *longPage=longPdf->findChild<QSpinBox *>("researchPdfPageField");auto *longTotal=longPdf->findChild<QLabel *>("researchPdfPageTotal");
   check(language+": a real four-digit PDF grows the field safely",longPage&&longTotal&&longPage->value()==1055&&longPage->width()>shortWidth&&fits(longPage)&&longTotal->text()=="/ 1055");
   longPdf->statusBar()->grab().save(folder+"/long-status-"+language+".png");delete longPdf;
  }
  app.exit(failures?1:0);
 });QTimer::singleShot(90000,&app,[&]{app.exit(92);});return app.exec();
}
