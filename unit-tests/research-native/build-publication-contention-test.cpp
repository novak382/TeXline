#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "Engine.h"
#include "ResearchBuild.h"
#include "Settings.h"
#include <QDir>
#include <QFile>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QThread>
#include <QTimer>
#include <QDateTime>
#include <windows.h>
#include <atomic>

static bool put(const QString &path,const QByteArray &bytes){QDir().mkpath(QFileInfo(path).absolutePath());QSaveFile f(path);return f.open(QIODevice::WriteOnly)&&f.write(bytes)==bytes.size()&&f.commit();}
static QByteArray read(const QString &path){QFile f(path);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray();}
static bool compile(Engine &engine,const QString &root,const Research::BuildJob &job,TWApp &app){
    QProcess *p=engine.run(QFileInfo(root),&app,job.outputDir);if(!p)return false;QElapsedTimer timer;timer.start();
    while(p->state()!=QProcess::NotRunning&&timer.elapsed()<30000){QCoreApplication::processEvents();QThread::msleep(2);}
    const bool ok=p->state()==QProcess::NotRunning&&p->exitStatus()==QProcess::NormalExit&&p->exitCode()==0;
    if(p->state()!=QProcess::NotRunning){p->kill();p->waitForFinished();}delete p;return ok;
}
int main(int argc,char **argv){const QString report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);QTimer::singleShot(100,&app,[&]{
    const QString folder=QFileInfo(report).absolutePath();Research::setBuildCacheForTests(folder+"/cache");Tw::Settings().setValue("binaryPaths","C:/texlive/2023/bin/windows");
    Engine engine("Private publication experiment","C:/texlive/2023/bin/windows/pdflatex.exe",{"-synctex=1","-interaction=nonstopmode","-file-line-error","$fullname"},true);
    QJsonArray checks,runs;int failures=0;auto check=[&](const QString &name,bool ok){checks.append(QJsonObject{{"name",name},{"passed",ok}});failures+=!ok;};
    const QByteArray a="\\documentclass{article}\n\\begin{document}Version A.\\end{document}\n",b="\\documentclass{article}\n\\begin{document}Version B.\\end{document}\n",c="\\documentclass{article}\n\\begin{document}External version C.\\end{document}\n";
    for(int scenario=0;scenario<3;++scenario){const QString root=folder+QString("/版本发布 中文𐐀/%1/main.tex").arg(scenario);
        check(QString("scenario %1 writes only a private source").arg(scenario),put(root,a));auto initial=Research::prepareBuild(root,engine);
        check(QString("scenario %1 stable real build publishes complete pair").arg(scenario),compile(engine,root,initial,app)&&Research::finishBuild(initial,true)==1&&Research::pdfPairValid(initial.previewFile));
        const QString marker=Research::pdfMetadataFile(initial.previewFile),sync=initial.previewFile.left(initial.previewFile.size()-4)+".synctex.gz";
        const QByteArray oldPdf=read(initial.previewFile),oldSync=read(sync),oldMarker=read(marker);
        check(QString("scenario %1 captures the entire previous revision").arg(scenario),!oldPdf.isEmpty()&&!oldSync.isEmpty()&&!oldMarker.isEmpty());
        check(QString("scenario %1 writes the next independent revision").arg(scenario),put(root,b));const auto job=Research::prepareBuild(root,engine);
        check(QString("scenario %1 real compiler produces version B").arg(scenario),compile(engine,root,job,app));
        const QString native="\\\\?\\"+QDir::toNativeSeparators(QFileInfo(marker).absoluteFilePath());
        const HANDLE lease=CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        check(QString("scenario %1 holds an ordinary Windows metadata reader").arg(scenario),lease!=INVALID_HANDLE_VALUE);
        const qint64 previousPdfTime=QFileInfo(job.previewFile).lastModified().toMSecsSinceEpoch();
        std::atomic<bool> externalWritten{scenario!=2},publicationReached{false};
        QThread *release=QThread::create([&,lease,scenario]{
            // Observe replacement through file attributes, without adding a
            // second read lease on the PDF. Start contention timing only after
            // the publication reaches its commit-marker step.
            QElapsedTimer gate;gate.start();while(gate.elapsed()<5000&&QFileInfo(job.previewFile).lastModified().toMSecsSinceEpoch()==previousPdfTime)QThread::msleep(2);
            publicationReached=gate.elapsed()<5000;
            QThread::msleep(scenario==1?600:60);
            if(scenario==2){QElapsedTimer writer;writer.start();while(!(externalWritten=put(root,c))&&writer.elapsed()<500)QThread::msleep(2);}
            if(lease!=INVALID_HANDLE_VALUE)CloseHandle(lease);
        });release->start();
        QElapsedTimer elapsed;elapsed.start();const int result=Research::finishBuild(job,true);const qint64 ms=elapsed.elapsed();release->wait();delete release;
        check(QString("scenario %1 reaches the controlled marker contention").arg(scenario),publicationReached);
        if(scenario==0){check("short metadata lease publishes without losing atomicity",result==1&&Research::pdfPairValid(job.previewFile));check("short lease leaves source bytes unchanged",read(root)==b);}
        else {check(QString("scenario %1 retains all previous PDF, SyncTeX and marker bytes").arg(scenario),read(job.previewFile)==oldPdf&&read(sync)==oldSync&&read(marker)==oldMarker);
            check(QString("scenario %1 previous preview correctly reports stale").arg(scenario),Research::pdfRevision(job.previewFile,root)==Research::PdfRevision::Stale);
            if(scenario==1){check("persistent metadata lease returns bounded I/O failure",result==-2&&ms<400);check("explicit publication retry succeeds after lease release",Research::finishBuild(job,true)==1&&Research::pdfPairValid(job.previewFile));}
            else {check("external edit during metadata retry is detected and retained",externalWritten&&read(root)==c&&result==-1);check("external edit never certifies the new PDF as current",!Research::pdfPairValid(job.previewFile));}
        }
        runs.append(QJsonObject{{"scenario",scenario},{"result",result},{"milliseconds",double(ms)},{"externalWritten",externalWritten.load()}});
    }
    Research::setBuildCacheForTests(QString());QSaveFile out(report);if(out.open(QIODevice::WriteOnly)){out.write(QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"runs",runs}}).toJson());out.commit();}app.exit(failures?1:0);
});return app.exec();}
