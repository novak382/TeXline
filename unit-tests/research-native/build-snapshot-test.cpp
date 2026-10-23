#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "Engine.h"
#include "ResearchBuild.h"
#include "ResearchWorkflow.h"
#include "ResearchDependencies.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSemaphore>
#include <QThreadPool>
#include <QThread>
#include <QRunnable>
#include <QTimer>
static bool put(const QString &p,const QByteArray &data){QDir().mkpath(QFileInfo(p).absolutePath());QSaveFile f(p);return f.open(QIODevice::WriteOnly)&&f.write(data)==data.size()&&f.commit();}
int main(int argc,char **argv){const QString report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);QTimer::singleShot(100,&app,[&]{
 const QString folder=QFileInfo(report).absolutePath(),root=folder+"/project/main.tex",disk=folder+"/project/disk.tex",snapshot=folder+"/project/快照章节.tex";
 Research::setBuildCacheForTests(folder+"/cache");Research::setDependencyBinaryPaths({"C:/texlive/2023/bin/windows"});
 QJsonArray checks;int failures=0;auto check=[&](const QString &name,bool ok){checks.append(QJsonObject{{"name",name},{"passed",ok}});failures+=!ok;};
 check("independent disk and Unicode snapshot chapters are written",put(root,"\\documentclass{article}\n\\input{disk}\n")&&put(disk,"Disk graph.\n")&&put(snapshot,"Snapshot graph.\n"));
 const QString canonicalRoot=QFileInfo(root).canonicalFilePath(),canonicalDisk=QFileInfo(disk).canonicalFilePath(),canonicalSnapshot=QFileInfo(snapshot).canonicalFilePath();
 QHash<QString,QString> buffers{{canonicalRoot,"\\documentclass{article}\n\\input{快照章节}\n"}};
 const Engine engine("Snapshot preparation","C:/texlive/2023/bin/windows/pdflatex.exe",{"-synctex=1","-interaction=nonstopmode","$fullname"},false);
 QThreadPool pool;pool.setMaxThreadCount(1);QSemaphore done;Research::BuildJob built,cancelled,partial;QStringList graph;bool worker=false;int polls=0;
 pool.start(QRunnable::create([&]{worker=QThread::currentThread()!=app.thread();graph=Research::projectFiles(root,buffers);built=Research::prepareBuild(root,engine,buffers);cancelled=Research::prepareBuild(root,engine,buffers,[]{return true;});partial=Research::prepareBuild(root,engine,buffers,[&]{return ++polls>=3;});done.release();}));
 const bool finished=done.tryAcquire(1,15000);if(!finished)pool.waitForDone();
 check("preparation executes on a worker with no document widgets",finished&&worker);
 check("the worker graph respects captured Unicode source text",graph.contains(canonicalSnapshot)&&!graph.contains(canonicalDisk));
 check("the build fingerprints exactly the captured source graph",built.managed&&!built.cancelled&&built.inputs.contains(canonicalRoot)&&built.inputs.contains(canonicalSnapshot)&&!built.inputs.contains(canonicalDisk));
 check("an initially retired request never creates a runnable build",cancelled.cancelled&&cancelled.outputDir.isEmpty()&&cancelled.inputs.isEmpty());
 check("cancellation at a file boundary retires a partial scan",partial.cancelled&&partial.outputDir.isEmpty()&&partial.inputs.isEmpty());
 QFile source(root);check("worker preparation never substitutes source on disk",source.open(QIODevice::ReadOnly)&&source.readAll()=="\\documentclass{article}\n\\input{disk}\n");source.close();pool.waitForDone();Research::setBuildCacheForTests(QString());put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures}}).toJson());app.exit(failures?1:0);
});return app.exec();}
