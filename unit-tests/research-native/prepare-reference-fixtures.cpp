#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "TeXDocumentWindow.h"
#include "PDFDocumentWindow.h"
#include "ResearchBuild.h"
#include "Settings.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QSaveFile>
#include <QTimer>
#include <QThread>
#include <QElapsedTimer>
int main(int argc,char **argv){QString report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);QTimer::singleShot(100,&app,[&]{
  QJsonArray checks;int failed=0;Tw::Settings().setValue("openPDFwithTeX",false);const QString base=QCoreApplication::applicationDirPath();
  for(const QString &relative:{QString("/papers/main 研究 论文/main.tex"),QString("/papers/supp 研究 补充/supp-1008.tex"),QString("/fixtures/matrix/main.tex")}){
    auto *w=new TeXDocumentWindow(base+relative);w->show();w->typeset();QElapsedTimer t;t.start();while(w->isTypesetting()&&t.elapsed()<120000){QCoreApplication::processEvents();QThread::msleep(4);}const bool valid=!w->isTypesetting()&&Research::pdfPairValid(Research::previewFile(base+relative));
    checks.append(QJsonObject{{"name",relative+": real PDF and SyncTeX match the copied source"},{"passed",valid}});failed+=!valid;
    if(w->pdfDocument())w->pdfDocument()->close();w->setModified(false);delete w;
    QSaveFile f(report);if(f.open(QIODevice::WriteOnly)){f.write(QJsonDocument(QJsonObject{{"checks",checks},{"failures",failed}}).toJson());f.commit();}
  }app.exit(failed?1:0);
});return app.exec();}
