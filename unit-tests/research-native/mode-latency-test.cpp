#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "TeXDocumentWindow.h"
#include "PDFDocumentWindow.h"
#include "CompletingEdit.h"
#include "ResearchAppearance.h"
#include "Settings.h"
#include <QElapsedTimer>
#include <QGraphicsView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QSaveFile>
#include <QScrollBar>
#include <QThread>
#include <QTimer>
#include <algorithm>

static void settle(int ms=220) { QElapsedTimer timer;timer.start();while(timer.elapsed()<ms){QCoreApplication::processEvents();QThread::msleep(2);} }
int main(int argc,char **argv) {
  const QString report=QString::fromLocal8Bit(argv[1]),pdfPath=QString::fromLocal8Bit(argv[2]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
  QTimer::singleShot(80,&app,[&] {
    QJsonArray checks,runs;int failures=0;
    auto check=[&](QString name,bool pass){checks.append(QJsonObject{{"name",name},{"passed",pass}});failures+=!pass;};
    Tw::Settings().setValue("openPDFwithTeX",false);Tw::Settings().setValue("researchOnboardingSeen",true);Research::setFocusMode(false);
    auto *source=new TeXDocumentWindow();const QString text=QString("English and 中文 $a=b+c$ with braces {value}.\n").repeated(10000);
    source->editor()->setPlainText(text);source->setModified(false);source->resize(1050,820);source->show();
    auto *pdf=new PDFDocumentWindow(pdfPath);pdf->resize(1050,820);pdf->show();settle(800);
    auto document=pdf->widget()->document().toStrongRef();check("real PDF is available",document&&document->numPages()>0);
    if(document)for(int round=0;round<16;++round) {
      const int page=(round*47)%document->numPages();pdf->widget()->goToPage(page);settle(300);
      source->editor()->verticalScrollBar()->setValue(source->editor()->verticalScrollBar()->maximum()/2);
      const auto cursor=source->editor()->textCursor();const qreal zoom=pdf->widget()->zoomLevel();
      for(bool focused:{true,false}) {
        QElapsedTimer timer;timer.start();Research::setFocusMode(focused);const double elapsed=timer.nsecsElapsed()/1.e6;
        runs.append(QJsonObject{{"round",round},{"focus",focused},{"modeCallMs",elapsed},{"page",page},{"pdfPages",document->numPages()},{"dpr",pdf->devicePixelRatioF()}});
        settle();
        check(QString("round %1 %2 retains source selection and PDF state").arg(round).arg(focused),source->editor()->textCursor()==cursor&&pdf->widget()->currentPage()==page&&qFuzzyCompare(pdf->widget()->zoomLevel(),zoom));
      }
    }
    check("source bytes are preserved",source->editor()->toPlainText()==text);
    // A busy renderer can delay the animation timer's first delivery. Wait
    // for retirement with a bound rather than equating 220 ms with completion.
    QElapsedTimer retirement;retirement.start();
    while(retirement.elapsed()<2000&&(source->findChild<QWidget*>("researchChromeVeil")->property("researchSnapshotBytes").toLongLong()!=0||pdf->findChild<QWidget*>("researchChromeVeil")->property("researchSnapshotBytes").toLongLong()!=0))settle(20);
    check("transition snapshots retire",source->findChild<QWidget*>("researchChromeVeil")->property("researchSnapshotBytes").toLongLong()==0&&pdf->findChild<QWidget*>("researchChromeVeil")->property("researchSnapshotBytes").toLongLong()==0);
    source->setModified(false);QPointer<TeXDocumentWindow> sg(source);QPointer<PDFDocumentWindow> pg(pdf);source->close();pdf->close();settle();
    check("mode changes retain ordinary close",!sg&&!pg);
    QSaveFile file(report);if(file.open(QIODevice::WriteOnly)){file.write(QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"runs",runs}}).toJson());file.commit();}
    app.exit(failures?1:0);
  });return app.exec();
}
