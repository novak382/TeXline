#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "Engine.h"
#include "TeXDocumentWindow.h"
#include "ResearchBuild.h"
#include "Settings.h"
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStatusBar>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <functional>
static bool put(const QString &p,const QByteArray &data){QDir().mkpath(QFileInfo(p).absolutePath());QSaveFile f(p);return f.open(QIODevice::WriteOnly)&&f.write(data)==data.size()&&f.commit();}
static bool waitFor(const std::function<bool()> &ready,int ms=90000){QElapsedTimer t;t.start();while(!ready()&&t.elapsed()<ms){QCoreApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QThread::msleep(2);}return ready();}
static void settle(int ms){QElapsedTimer t;t.start();waitFor([&]{return t.elapsed()>=ms;},ms+1000);}
static bool copyProject(const QString &from,const QString &to){QDir().mkpath(to);QDirIterator it(from,QDir::Files|QDir::NoDotAndDotDot,QDirIterator::Subdirectories);bool ok=true;while(it.hasNext()){const QString file=it.next(),target=QDir(to).filePath(QDir(from).relativeFilePath(file));QDir().mkpath(QFileInfo(target).absolutePath());ok&=QFile::copy(file,target);}return ok;}
int main(int argc,char **argv){const QString report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
QTimer::singleShot(100,&app,[&]{
 const QString folder=QFileInfo(report).absolutePath();Research::setBuildCacheForTests(folder+"/cache");Tw::Settings().setValue("openPDFwithTeX",false);Tw::Settings().setValue("binaryPaths","C:/texlive/2023/bin/windows");Tw::Settings().setValue("researchCommandCompletionMode",0);
 const Engine engine("Build responsiveness","C:/texlive/2023/bin/windows/pdflatex.exe",{"-synctex=1","-interaction=nonstopmode","-file-line-error","$fullname"},true);app.setEngineList({engine});
 QJsonArray checks,samples;int failures=0;auto save=[&]{put(report,QJsonDocument(QJsonObject{{"checks",checks},{"samples",samples},{"failures",failures},{"physicalInputVerified",false}}).toJson());};auto check=[&](const QString &name,bool ok){checks.append(QJsonObject{{"name",name},{"passed",ok}});failures+=!ok;save();};
 check("the private fixture compiler is available",engine.isAvailable());if(!engine.isAvailable()){Research::setBuildCacheForTests(QString());app.exit(2);return;}
 QStringList roots,names;
 const QString paper=qEnvironmentVariable("TEXLINE_BUILD_PAPER_ROOT"),paperCopy=folder+"/paper";
 if(!paper.isEmpty()){check("typical paper is copied into an independent fixture",copyProject(QFileInfo(paper).absolutePath(),paperCopy));roots.append(paperCopy+"/"+QFileInfo(paper).fileName());names.append("typical-paper");}
 const QString heavy=folder+"/heavy/main.tex";QByteArray source="\\documentclass{article}\n\\usepackage{graphicx}\n";bool written=true;
 for(int n=0;n<8;++n){const QByteArray name="asset"+QByteArray::number(n)+".jpg";source+="\\newcommand{\\unused"+QByteArray(1,char('a'+n))+"}{\\includegraphics{"+name+"}}\n";written&=put(QFileInfo(heavy).absolutePath()+"/"+QString::fromLatin1(name),QByteArray(5*1024*1024,char('a'+n)));}
 source+="\\begin{document}Independent measurement.\\end{document}\n";check("40 MiB synthetic dependency project is written",written&&put(heavy,source));roots.append(heavy);names.append("synthetic-40MiB");
 for(int project=0;project<roots.size();++project){const QString root=roots[project];QFile input(root);input.open(QIODevice::ReadOnly);const QByteArray original=input.readAll();input.close();auto *window=new TeXDocumentWindow(root);window->show();settle(250);QStringList notices;
  QObject::connect(window->statusBar(),&QStatusBar::messageChanged,window,[&](const QString &text){notices.append(text);});
  for(int round=0;round<3;++round){QJsonArray gaps;QElapsedTimer elapsed;elapsed.start();double previous=0;QTimer heartbeat;heartbeat.setTimerType(Qt::PreciseTimer);heartbeat.setInterval(10);QObject::connect(&heartbeat,&QTimer::timeout,window,[&]{const double now=elapsed.nsecsElapsed()/1e6;gaps.append(now-previous);previous=now;});heartbeat.start();settle(20);previous=elapsed.nsecsElapsed()/1e6;
   QElapsedTimer call;call.start();window->typeset();const double returned=call.nsecsElapsed()/1e6;const bool stopped=waitFor([&]{return !window->isTypesetting();});settle(30);heartbeat.stop();
   const QString preview=Research::previewFile(root);const bool valid=!preview.isEmpty()&&Research::pdfPairValid(preview);double maximum=0;for(const auto &gap:gaps)maximum=qMax(maximum,gap.toDouble());
   samples.append(QJsonObject{{"project",names[project]},{"round",round},{"stage","actual-typeset"},{"typesetCallMs",returned},{"requestToStoppedMs",call.nsecsElapsed()/1e6},{"heartbeatMaximumMs",maximum},{"heartbeatGapsMs",gaps},{"previewValid",valid},{"notices",QJsonArray::fromStringList(notices)},{"consoleTail",window->property("consoleOutput").toString().right(4000)}});
   check(names[project]+QString(" actual compilation and preview pair %1").arg(round),stopped&&valid);check(names[project]+QString(" unedited build has no false content-edit warning %1").arg(round),!notices.contains("编译期间源码已更新；保留上一版 PDF，请重新编译。"));
   if(window->isTypesetting()){window->interrupt();waitFor([&]{return !window->isTypesetting();});}
  }
  for(int round=0;round<3;++round){QElapsedTimer t;t.start();const auto job=Research::prepareBuild(root,engine);const double prep=t.nsecsElapsed()/1e6;t.restart();const int promoted=Research::finishBuild(job,true);const double finish=t.nsecsElapsed()/1e6;samples.append(QJsonObject{{"project",names[project]},{"round",round},{"stage","direct-api"},{"prepareMs",prep},{"finishMs",finish},{"inputCount",job.inputs.size()},{"promoted",promoted}});check(names[project]+QString(" direct preparation and publication %1").arg(round),!job.outputDir.isEmpty()&&promoted==1);}
  QFile verify(root);check(names[project]+" editor and disk source remain unchanged",verify.open(QIODevice::ReadOnly)&&verify.readAll()==original&&!window->isModified());verify.close();window->setModified(false);delete window;
 }
 Research::setBuildCacheForTests(QString());save();app.exit(failures?1:0);
});return app.exec();}

