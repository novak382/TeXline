#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "Engine.h"
#include "TeXDocumentWindow.h"
#include "ResearchBuild.h"
#include "Settings.h"
#include <QElapsedTimer>
#include <QDirIterator>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QSaveFile>
#include <QThread>
#include <QTimer>
#include <functional>
static bool put(const QString &p,const QByteArray &d){QSaveFile f(p);return f.open(QIODevice::WriteOnly)&&f.write(d)==d.size()&&f.commit();}
static bool waitFor(const std::function<bool()> &f){QElapsedTimer t;t.start();while(!f()&&t.elapsed()<180000){QCoreApplication::processEvents();QThread::msleep(2);}return f();}
int main(int argc,char **argv){const QString report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);QTimer::singleShot(100,&app,[&]{
 const QString root=qEnvironmentVariable("TEXLINE_BUILD_PAPER_ROOT");Research::setBuildCacheForTests(QFileInfo(report).absolutePath()+"/cache");Tw::Settings().setValue("binaryPaths","C:/texlive/2023/bin/windows");Tw::Settings().setValue("openPDFwithTeX",false);Tw::Settings().setValue("researchCommandCompletionMode",0);
 Engine engine("Output timing","C:/texlive/2023/bin/windows/pdflatex.exe",{"-synctex=1","-interaction=nonstopmode","-file-line-error","$fullname"},false);app.setEngineList({engine});
 QJsonArray checks,samples;int failures=0;auto check=[&](const QString &name,bool passed){checks.append(QJsonObject{{"name",name},{"passed",passed}});failures+=!passed;};check("independent typical paper and compiler available",QFileInfo::exists(root)&&engine.isAvailable());
 auto *w=new TeXDocumentWindow(root);w->show();
 QFile source(root);source.open(QIODevice::ReadOnly);const QString sourceHash=QString::fromLatin1(QCryptographicHash::hash(source.readAll(),QCryptographicHash::Sha256).toHex());source.close();
 for(int round=0;round<3;++round){QElapsedTimer clock;clock.start();double previous=0;QJsonArray gaps;QTimer heartbeat;heartbeat.setInterval(10);heartbeat.setTimerType(Qt::PreciseTimer);QObject::connect(&heartbeat,&QTimer::timeout,w,[&]{const double now=clock.nsecsElapsed()/1e6;gaps.append(now-previous);previous=now;});heartbeat.start();
  QElapsedTimer request;request.start();w->typeset();const double call=request.nsecsElapsed()/1e6;const bool done=waitFor([&]{return !w->isTypesetting()&&!w->property("researchPublishing").toBool();});heartbeat.stop();double maximum=0;for(const auto &v:gaps)maximum=qMax(maximum,v.toDouble());
  const QString preview=Research::previewFile(root);check(QString("actual typical paper compilation %1").arg(round),done&&Research::pdfPairValid(preview));QFile cached(preview),shared(QFileInfo(root).absolutePath()+"/"+QFileInfo(root).completeBaseName()+".pdf");const bool sharedMatches=cached.open(QIODevice::ReadOnly)&&shared.open(QIODevice::ReadOnly)&&cached.readAll()==shared.readAll();samples.append(QJsonObject{{"round",round},{"typesetCallMs",call},{"completeMs",request.nsecsElapsed()/1e6},{"heartbeatMaximumMs",maximum},{"heartbeatGapsMs",gaps},{"publishStatus",w->property("researchPublishStatus").toString()},{"sharedPdfMatchesCache",sharedMatches},{"consoleTail",w->property("consoleOutput").toString().right(1200)}});
 }
 check("experiment never modifies private paper source",source.open(QIODevice::ReadOnly)&&QString::fromLatin1(QCryptographicHash::hash(source.readAll(),QCryptographicHash::Sha256).toHex())==sourceHash&&!w->isModified());source.close();w->setModified(false);delete w;Research::setBuildCacheForTests(QString());put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"sourceSha256",sourceHash},{"samples",samples}}).toJson());app.exit(failures?1:0);
 });return app.exec();}
