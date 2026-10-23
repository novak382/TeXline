#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "Engine.h"
#include "ResearchBuild.h"
#include "Settings.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTimer>
#include <QElapsedTimer>
#include <QThread>
#include <windows.h>
static QByteArray read(const QString &p){QFile f(p);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray();}
static bool put(const QString &p,const QByteArray &b){QDir().mkpath(QFileInfo(p).absolutePath());QSaveFile f(p);return f.open(QIODevice::WriteOnly)&&f.write(b)==b.size()&&f.commit();}
static bool compile(Engine &engine,const QString &root,TWApp &app){
 const auto job=Research::prepareBuild(root,engine);auto *process=engine.run(QFileInfo(root),&app,job.outputDir);if(!process)return false;
 QElapsedTimer clock;clock.start();while(process->state()!=QProcess::NotRunning&&clock.elapsed()<30000){QCoreApplication::processEvents();QThread::msleep(2);}
 const bool ok=process->state()==QProcess::NotRunning&&process->exitCode()==0&&process->exitStatus()==QProcess::NormalExit;
 if(process->state()!=QProcess::NotRunning){process->kill();process->waitForFinished();}delete process;return ok&&Research::finishBuild(job,true)==1;
}
int main(int argc,char **argv){const QString report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);QTimer::singleShot(100,&app,[&]{
 const QString folder=QFileInfo(report).absolutePath(),project=folder+"/论文 中文",root=project+"/main.tex",pdf=project+"/main.pdf",sync=project+"/main.synctex.gz";
 Research::setBuildCacheForTests(folder+"/cache");Tw::Settings().setValue("binaryPaths","C:/texlive/2023/bin/windows");
 Engine engine("Private metadata test","C:/texlive/2023/bin/windows/pdflatex.exe",{"-synctex=1","-interaction=nonstopmode","-file-line-error","$fullname"},true);
 QJsonArray checks;int failures=0;auto check=[&](const QString &name,bool ok){checks.append(QJsonObject{{"name",name},{"passed",ok}});failures+=!ok;put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures}}).toJson());};
 const QByteArray a="\\documentclass{article}\n\\begin{document}Version A.\\end{document}\n",b="\\documentclass{article}\n\\begin{document}Version B.\\end{document}\n",c="\\documentclass{article}\n\\begin{document}Version C.\\end{document}\n";
 check("independent source and compiler",engine.isAvailable()&&put(root,a));
 const QString before=Research::pdfMetadataFile(pdf);check("new PDF metadata key stays stable after file creation",compile(engine,root,app)&&Research::publishPreview(root)&&Research::pdfMetadataFile(pdf)==before);
 check("private preview and published PDF remain certified",Research::pdfPairValid(Research::previewFile(root))&&Research::pdfPairValid(pdf));
 check("project contains no proprietary metadata",QDir(project).entryList({"*.research.json"},QDir::Files).isEmpty()&&QFileInfo::exists(Research::pdfMetadataFile(pdf))&&!Research::pdfMetadataFile(pdf).startsWith(project+"/"));
 check("Windows case variants use the same local key",Research::pdfMetadataFile(pdf.toUpper())==Research::pdfMetadataFile(pdf));
 const QString other=folder+"/other/main.pdf";check("different folders never share provenance",Research::pdfMetadataFile(other)!=Research::pdfMetadataFile(pdf)&&put(other,read(pdf))&&Research::pdfRevision(other)==Research::PdfRevision::Unknown);
 const auto oldMarker=read(Research::pdfMetadataFile(pdf));const QString legacy=pdf+".research.json";
 check("legacy sidecar fixture can still be read",put(legacy,oldMarker)&&QFile::remove(Research::pdfMetadataFile(pdf))&&Research::pdfPairValid(pdf));
 check("new build migrates legacy publication without leaving a sidecar",put(root,b)&&compile(engine,root,app)&&Research::publishPreview(root)&&Research::pdfPairValid(pdf)&&!QFileInfo::exists(legacy));
 QDir backup(Research::buildCacheDirectory(root)+"/legacy-metadata");bool preserved=false;for(const auto &name:backup.entryList({"*.json"},QDir::Files))preserved|=read(backup.filePath(name))==oldMarker;
 check("legacy metadata is archived before removal",preserved);
 const QByteArray foreign="{\"version\":1,\"custom\":true}";check("unrecognized sidecar fixture is independent",put(legacy,foreign));
 const QByteArray oldPdf=read(pdf),oldSync=read(sync),oldLocal=read(Research::pdfMetadataFile(pdf));
 check("next generation is compiled privately",put(root,c)&&compile(engine,root,app));
 const auto native=QDir::toNativeSeparators(Research::pdfMetadataFile(pdf)).toStdWString();HANDLE lock=CreateFileW(native.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
 check("real metadata replacement lease acquired",lock!=INVALID_HANDLE_VALUE);
 QString error;const bool published=Research::publishPreview(root,&error);
 check("private metadata commit failure rolls back PDF and SyncTeX",!published&&!error.isEmpty()&&read(pdf)==oldPdf&&read(sync)==oldSync&&read(Research::pdfMetadataFile(pdf))==oldLocal);
 if(lock!=INVALID_HANDLE_VALUE)CloseHandle(lock);
 check("retry commits the complete generation after lease release",Research::publishPreview(root,&error)&&Research::pdfPairValid(pdf)&&read(pdf)!=oldPdf);
 check("migration never deletes an unrecognized file",read(legacy)==foreign);
 check("fresh unrelated project emits only standard compiler files",put(folder+"/fresh/main.tex",a)&&compile(engine,folder+"/fresh/main.tex",app)&&Research::publishPreview(folder+"/fresh/main.tex")&&QDir(folder+"/fresh").entryList({"*.research.json"},QDir::Files).isEmpty());
 app.exit(failures?1:0);
 });return app.exec();}
