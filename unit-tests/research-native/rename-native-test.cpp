// Public-name migration without losing the established configuration identity.
// GPL-2.0-or-later.
#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "TeXDocumentWindow.h"
#include "PDFDocumentWindow.h"
#include "ResearchAppearance.h"
#include "Settings.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTimer>

int main(int argc,char **argv) {
  const QString report=QString::fromLocal8Bit(argv[1]); argc=1;
  TWApp app(argc,argv); app.setQuitOnLastWindowClosed(false);
  QTimer::singleShot(80,&app,[&]{
    QJsonArray checks; int failures=0;
    auto check=[&](const QString &name,bool pass){checks.append(QJsonObject{{"name",name},{"passed",pass}});failures+=!pass;};
    const QString base=QCoreApplication::applicationDirPath();
    check("public application name is TeXline",QApplication::applicationDisplayName()=="TeXline");
    check("established settings and library identity remains compatible",QCoreApplication::applicationName()=="TeXworks Research" && QCoreApplication::organizationName()=="TUG");
    check("existing portable configuration is used directly",QFileInfo(Tw::Settings().fileName())==QFileInfo(base+"/settings/TUG/TeXworks Research.ini") && QFileInfo::exists(Tw::Settings().fileName()));
    check("no parallel configuration is created for the new name",!QFileInfo::exists(base+"/settings/TUG/TeXline.ini"));
    Tw::Settings().setValue("openPDFwithTeX",false); Research::setFocusMode(false);
    auto *source=new TeXDocumentWindow(base+"/papers/main 研究 论文/main.tex");source->show();
    auto *pdf=new PDFDocumentWindow(base+"/fixtures/lshort.pdf");pdf->show();app.processEvents();
    check("default editor title shows TeXline",source->windowTitle().endsWith(" - TeXline"));
    check("default PDF title shows TeXline",pdf->windowTitle().endsWith(" - TeXline"));
    Research::setUiLocale("en");app.processEvents();
    check("English interface retains TeXline and the same settings file",source->windowTitle().endsWith(" - TeXline") && Tw::Settings().fileName().endsWith("TeXworks Research.ini"));
    source->grab().save(QFileInfo(report).absolutePath()+"/texline-editor-default.png");
    pdf->grab().save(QFileInfo(report).absolutePath()+"/texline-pdf-default.png");
    source->setModified(false);delete source;delete pdf;
    QSaveFile output(report);if(!output.open(QIODevice::WriteOnly)){app.exit(2);return;}
    output.write(QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures}}).toJson());
    app.exit(output.commit()?(failures?1:0):2);
  });return app.exec();
}
