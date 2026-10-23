#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "Engine.h"
#include "ResearchBuild.h"
#include "Settings.h"
#include <QtWidgets>
#include <QProcess>
#include <QCryptographicHash>
int main(int argc,char **argv){
 if(argc<2||!qEnvironmentVariable("TEXLINE_TEST_DESKTOP").startsWith("TeXlineValidation-"))return 2;
 const QString report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
 QTimer::singleShot(50,&app,[&]{
  const auto base=qEnvironmentVariable("TEXLINE_MATURITY_PERF_ROOT")+"/compile-certification";QDir().mkpath(base);const auto root=base+"/main.tex";QJsonArray checks,samples;int failures=0;
  auto check=[&](QString name,bool ok){checks.append(QJsonObject{{"name",name},{"passed",ok}});failures+=!ok;};
  auto write=[&](QString path,QByteArray text){QFile f(path);return f.open(QIODevice::WriteOnly)&&f.write(text)==text.size();};
  const QByteArray source="\\documentclass{article}\n\\usepackage{graphicx}\n\\begin{document}\n\\section{Image-heavy paper}\nAn independent compilation benchmark.\\includegraphics[width=.9\\linewidth]{sample.png}\n\\end{document}\n";
  check("private source prepared",write(root,source));
  if(!QFileInfo::exists(base+"/sample.png")){QImage image(1600,1200,QImage::Format_RGB888);quint32 rng=1729;for(int y=0;y<image.height();++y)for(int x=0;x<image.width()*3;++x){rng=rng*1664525u+1013904223u;image.scanLine(y)[x]=uchar(rng>>24);}check("deterministic private image created",image.save(base+"/sample.png"));}
  Research::setBuildCacheForTests(base+"/cache");Engine engine("Performance","C:/texlive/2023/bin/windows/pdflatex.exe",{"-synctex=1","-recorder","-interaction=nonstopmode","$fullname"},false);
  QJsonArray total;for(int round=0;round<5;++round){QElapsedTimer whole;whole.start();auto job=Research::prepareBuild(root,engine);check("managed build available "+QString::number(round),job.managed&&!job.outputDir.isEmpty());
   auto *process=engine.run(QFileInfo(root),nullptr,job.outputDir);const bool done=process&&process->waitForFinished(60000)&&process->exitCode()==0;
   if(process){write(QFileInfo(report).absolutePath()+"/compiler-"+QString::number(round)+".log",process->readAll());delete process;}
   check("real TeX compiler succeeds "+QString::number(round),done);if(!done)break;
   QElapsedTimer promotion;promotion.start();const int result=Research::finishBuildInBackground(job,true,[](const QStringList&){return false;});const double ms=promotion.nsecsElapsed()/1e6;total.append(whole.nsecsElapsed()/1e6);samples.append(ms);check("complete PDF and SyncTeX certified "+QString::number(round),result==1&&Research::pdfCompiledPairValid(job.previewFile));
   if(round==4){const auto before=Research::pdfMetadata(job.previewFile);check("external same-size edit rejects certification",write(root,QByteArray(source).replace("independent","Independent"))&&Research::finishBuildInBackground(job,true,[](const QStringList&){return false;})==-1&&Research::pdfMetadata(job.previewFile)==before);write(root,source);}
  }
  QFile image(base+"/sample.png");image.open(QIODevice::ReadOnly);const auto imageHash=QString::fromLatin1(QCryptographicHash::hash(image.readAll(),QCryptographicHash::Sha256).toHex());
  QFile out(report);out.open(QIODevice::WriteOnly);out.write(QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"certificationMs",samples},{"endToEndMs",total},{"sameProjectRoot",base},{"imageBytes",double(QFileInfo(base+"/sample.png").size())},{"imageSha256",imageHash},{"seed",1729},{"captureDuringTiming",false}}).toJson());out.close();app.exit(failures?1:0);
 });return app.exec();
}
