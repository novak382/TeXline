#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "ResearchSlideExport.h"
#include <QDir>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTimer>
static void put(const QString &p,const QByteArray &b){QSaveFile f(p);if(f.open(QIODevice::WriteOnly)){f.write(b);f.commit();}}
int main(int argc,char **argv){const auto report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
 QTimer::singleShot(100,&app,[&]{const auto folder=QFileInfo(report).absolutePath();QJsonArray checks;int failures=0;auto check=[&](QString name,bool ok){checks.append(QJsonObject{{"name",name},{"passed",ok}});failures+=!ok;put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures}}).toJson());};
  const auto root=folder+"/main.tex",first=folder+"/projection.pdf",second=folder+"/speaker.pdf";const QByteArray source="\\documentclass{beamer}\n\\begin{document}\n\\begin{frame}{Safety}Content\\note{Private note}\\end{frame}\n\\end{document}\n";put(root,source);put(first,"Existing projection must survive failed batch");
  Research::SlideExportRequest r;r.root=QFileInfo(root).canonicalFilePath();r.sources[r.root]=QString::fromUtf8(source);r.engine=Engine("pdfLaTeX","C:/texlive/2023/bin/windows/pdflatex.exe",{"-interaction=nonstopmode","-file-line-error","$fullname"},true);r.profiles={1,2};r.destinations={first,second};r.destinationHashes[first]=Research::slideExportHash(first);
  // A directory at the second output path models an unavailable destination.
  QDir().mkpath(second);const auto before=Research::slideExportHash(first);r.destinationHashes[second]=Research::slideExportHash(second);auto cancel=std::make_shared<std::atomic_bool>(false);auto failed=Research::exportSlideCopies(r,cancel);put(folder+"/failure.txt",(failed.error+failed.log).toUtf8());check("unwritable second output is reported as failure",!failed.error.isEmpty());const auto english=Research::localizedSlideExportError(failed.error,true);check("English export errors retain the exact destination",english.startsWith("Cannot save:")&&english.contains(second)&&Research::localizedSlideExportError(failed.error,false)==failed.error);check("failed batch leaves earlier destination unchanged",Research::slideExportHash(first)==before&&failed.written.isEmpty());
  QDir().rmdir(second);r.destinationHashes[first]=Research::slideExportHash(first);r.destinationHashes[second]=QByteArray();auto result=Research::exportSlideCopies(r,cancel);check("retry exports both outputs",result.error.isEmpty()&&result.written.size()==2);const auto good=Research::slideExportHash(first);cancel->store(true);result=Research::exportSlideCopies(r,cancel);check("cancel retains completed outputs",result.cancelled&&Research::slideExportHash(first)==good);check("source is unchanged",Research::slideExportHash(root)==QCryptographicHash::hash(source,QCryptographicHash::Sha256));app.exit(failures?1:0);
 });return app.exec();}
