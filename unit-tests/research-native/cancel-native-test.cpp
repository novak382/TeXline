#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "Engine.h"
#include "TeXDocumentWindow.h"
#include "ResearchAppearance.h"
#include "Settings.h"
#include <QSaveFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QElapsedTimer>
#include <QThread>
#include <QTimer>
#include <QMessageBox>
#include <QPointer>
#include <windows.h>
#include <functional>
static bool put(QString p,QByteArray b){QSaveFile f(p);return f.open(QIODevice::WriteOnly)&&f.write(b)==b.size()&&f.commit();}
static bool wait(const std::function<bool()> &condition,int ms=7000){QElapsedTimer t;t.start();while(!condition()&&t.elapsed()<ms){QCoreApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QThread::msleep(3);}return condition();}
int main(int argc,char **argv){QString report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);QTimer::singleShot(80,&app,[&]{QJsonArray checks;int failures=0;QJsonObject measures;auto check=[&](QString n,bool b){checks.append(QJsonObject{{"name",n},{"passed",b}});failures+=!b;put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"measures",measures}}).toJson());};auto old=app.getEngineList();QString folder=QFileInfo(report).absolutePath();Tw::Settings().setValue("openPDFwithTeX",false);
for(int n=0;n<4;++n){QString prefix=folder+QString("/cancel-%1").arg(n),source=prefix+".tex";QFile::remove(prefix+".worker");QFile::remove(prefix+".late");put(source,"% !TEX program = Maturity cancellation\n\\documentclass{article}\n\\begin{document}Safe source.\\end{document}\n");app.setEngineList({Engine("Maturity cancellation",QCoreApplication::applicationDirPath()+"/../cancel-helper.exe",{"--parent",prefix},false)});auto *w=new TeXDocumentWindow(source);w->show();w->typeset();bool ready=wait([&]{return QFileInfo::exists(prefix+".worker");});check(QString("round %1: actual editor starts a child engine").arg(n),ready&&w->isTypesetting());QFile f(prefix+".worker");f.open(QIODevice::ReadOnly);DWORD pid=f.readAll().toUInt();f.close();HANDLE worker=OpenProcess(SYNCHRONIZE|PROCESS_TERMINATE,FALSE,pid);if(n%2){QTimer::singleShot(20,&app,[]{for(auto *widget:QApplication::topLevelWidgets())if(auto *box=qobject_cast<QMessageBox*>(widget))box->done(QMessageBox::Yes);});QPointer<TeXDocumentWindow> guard(w);w->close();check(QString("round %1: closing during build releases the editor").arg(n),wait([&]{return guard.isNull();}));w=nullptr;}else{w->interrupt();check(QString("round %1: abort promptly returns the editor to idle").arg(n),wait([&]{return !w->isTypesetting();}));}
bool stopped=worker&&WaitForSingleObject(worker,1200)==WAIT_OBJECT_0;check(QString("round %1: abort also stops the engine descendant").arg(n),stopped);check(QString("round %1: abort cannot generate a late output").arg(n),!QFileInfo::exists(prefix+".late"));if(worker){if(!stopped)TerminateProcess(worker,99);CloseHandle(worker);}if(w){w->setModified(false);delete w;}}
QString detached=folder+"/intentional-detached";QFile::remove(detached+".worker");QFile::remove(detached+".late");Engine custom("Detached custom tool",QCoreApplication::applicationDirPath()+"/../cancel-helper.exe",{"--detach",detached},false);QProcess *normal=custom.run(QFileInfo(folder+"/cancel-0.tex"),&app);check("a custom tool can finish normally",normal&&wait([&]{return normal->state()==QProcess::NotRunning;})&&normal->exitCode()==0);delete normal;check("a successful custom tool retains its intentionally detached child",wait([&]{return QFileInfo::exists(detached+".late");},7000));
app.setEngineList(old);app.exit(failures?1:0);});return app.exec();}