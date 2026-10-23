// Independent BibTeX grammar controls, also checked by the installed compiler.
// GPL-2.0-or-later.
#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "BibTeXFile.h"
#include "Engine.h"
#include "TWApp.h"
#include "Settings.h"
#include <QtCore>

static bool put(const QString &path,const QByteArray &text){QFile f(path);return f.open(QIODevice::WriteOnly)&&f.write(text)==text.size();}
int main(int argc,char **argv){
 const QString report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);
 if(!qEnvironmentVariable("TEXLINE_TEST_DESKTOP").startsWith("TeXlineValidation-"))return 2;
 QJsonArray checks;int failures=0;auto check=[&](QString name,bool passed){checks.append(QJsonObject{{"name",name},{"passed",passed}});failures+=!passed;};
 const QString folder=QFileInfo(report).absolutePath();
 const QByteArray data="@string{series = {Independent fixtures}}\n@comment{A literal @book{fake,title={Do not index}}}\n@misc_data{archive,author={Doe, Jane},title={Fictional data archive},year={2026}}\n@dataset2(dataset,author={Roe, Alex},title=\"Fictional (field) dataset\",year={2025})\n@data.archive-v2{punctuated,title={Fictional archive inventory},year={2026}}\n@book{regular,author={Smith, Jordan},title={Nested {Archive} with an email test@example.org},year={2024}}\n";
 check("private bibliography and compiler input written",put(folder+"/refs.bib",data)&&put(folder+"/check.aux","\\relax\n\\citation{archive,dataset,regular,punctuated}\n\\bibstyle{plain}\n\\bibdata{refs}\n"));
 QProcess compiler;compiler.setWorkingDirectory(folder);compiler.setProgram(TWApp::instance()->findProgram("bibtex",Engine::binPaths()));compiler.setArguments({"check"});
#ifdef Q_OS_WIN
 compiler.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args){args->flags|=0x08000000;});
#endif
 compiler.start();const bool completed=compiler.waitForFinished(15000);QFile bbl(folder+"/check.bbl");const QByteArray rendered=bbl.open(QIODevice::ReadOnly)?bbl.readAll():QByteArray();
 put(folder+"/compiler-output.txt",compiler.readAllStandardOutput()+compiler.readAllStandardError());
 check("actual BibTeX accepts and emits custom type identifiers",completed&&compiler.exitStatus()==QProcess::NormalExit&&rendered.contains("\\bibitem{archive}")&&rendered.contains("\\bibitem{dataset}")&&rendered.contains("\\bibitem{regular}")&&rendered.contains("\\bibitem{punctuated}"));
 BibTeXFile bib;check("metadata parser loads the same bibliography",bib.load(folder+"/refs.bib"));QStringList keys;for(unsigned i=0;i<bib.numEntries();++i)if(bib.entry(i).type()==BibTeXFile::Entry::NORMAL)keys.append(bib.entry(i).key());
 check("underscore and numbered custom types remain discoverable",keys.contains("archive")&&keys.contains("dataset"));
 check("nested entries, comments and email addresses have correct boundaries",keys.contains("regular")&&keys.contains("punctuated")&&!keys.contains("fake")&&keys.size()==4);
 check("custom entry source preserves its original delimiter and type",BibTeXFile::entrySource(QString::fromUtf8(data),"archive").startsWith("@misc_data{")&&BibTeXFile::entrySource(QString::fromUtf8(data),"dataset").startsWith("@dataset2("));
 const bool buffered=bib.loadData(data);QStringList bufferedKeys;for(unsigned i=0;i<bib.numEntries();++i)bufferedKeys.append(bib.entry(i).key());
 check("buffer and file metadata use identical record boundaries",buffered&&bufferedKeys==keys);
 put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"physicalInput",false}}).toJson());return failures?1:0;
}
