#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "Engine.h"
#include "TeXDocumentWindow.h"
#include "ResearchBuild.h"
#include "Settings.h"
#include "ResearchAppearance.h"
#include "utils/TypesetManager.h"
#include <QAction>
#include <QDir>
#include <QDynamicPropertyChangeEvent>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QSaveFile>
#include <QStatusBar>
#include <QThread>
#include <QTimer>
#include <functional>
static bool put(const QString &p,const QByteArray &data){QDir().mkpath(QFileInfo(p).absolutePath());QSaveFile f(p);return f.open(QIODevice::WriteOnly)&&f.write(data)==data.size()&&f.commit();}
static bool waitFor(const std::function<bool()> &ready,int ms=30000){QElapsedTimer t;t.start();while(!ready()&&t.elapsed()<ms){QCoreApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QThread::msleep(2);}return ready();}
static void settle(int ms){QElapsedTimer t;t.start();waitFor([&]{return t.elapsed()>=ms;},ms+1000);}
// Managed builds now render progress in the output summary, so observe its
// lifecycle state instead of counting obsolete status-bar captions.
class BuildStartCounter final : public QObject {
public:
 int starts=0;
 QString previous;
 bool eventFilter(QObject *object,QEvent *event) override {
  if(event->type()==QEvent::DynamicPropertyChange && static_cast<QDynamicPropertyChangeEvent *>(event)->propertyName()=="researchPublishStatus") {
   const QString state=object->property("researchPublishStatus").toString();
   if(state=="building" && previous!=state)++starts;
   previous=state;
  }
  return false;
 }
};
int main(int argc,char **argv){const QString report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);QTimer::singleShot(100,&app,[&]{
 const QString folder=QFileInfo(report).absolutePath(),root=folder+"/project/main.tex";Research::setBuildCacheForTests(folder+"/cache");Tw::Settings().setValue("binaryPaths","C:/texlive/2023/bin/windows");Tw::Settings().setValue("openPDFwithTeX",false);
 const Engine engine("Preparation lifecycle","C:/texlive/2023/bin/windows/pdflatex.exe",{"-synctex=1","-interaction=nonstopmode","-file-line-error","$fullname"},false);app.setEngineList({engine});
 QJsonArray checks,timings;int failures=0;auto save=[&]{put(report,QJsonDocument(QJsonObject{{"checks",checks},{"timings",timings},{"failures",failures}}).toJson());};auto check=[&](const QString &name,bool ok){checks.append(QJsonObject{{"name",name},{"passed",ok}});failures+=!ok;save();};
 QByteArray text="\\documentclass{article}\n\\usepackage{graphicx}\n";bool assets=true;for(int n=0;n<8;++n){const QByteArray name="asset"+QByteArray::number(n)+".jpg";text+="\\newcommand{\\unused"+QByteArray(1,char('a'+n))+"}{\\includegraphics{"+name+"}}\n";assets&=put(QFileInfo(root).absolutePath()+"/"+QString::fromLatin1(name),QByteArray(5*1024*1024,char('a'+n)));}text+="\\begin{document}Independent source.\\end{document}\n";
 check("compiler and independent heavy fixture are available",engine.isAvailable()&&assets&&put(root,text));
 auto *window=new TeXDocumentWindow(root);window->show();settle(100);BuildStartCounter counter;window->installEventFilter(&counter);const int &starts=counter.starts;
 QElapsedTimer timer;timer.start();window->typeset();const double request=timer.nsecsElapsed()/1e6;timings.append(QJsonObject{{"requestReturnMs",request}});
 check("preparation returns within 80 ms while reserving the project",request<80&&window->isTypesetting());check("the preparing state is explicit before launching the compiler",window->property("researchBuildPreparing").toBool()&&starts==0);
 timer.restart();window->interrupt();const double stopped=timer.nsecsElapsed()/1e6;check("interrupting preparation releases ownership without waiting",stopped<80&&!window->isTypesetting()&&!window->property("researchBuildPreparing").toBool());settle(500);check("a retired preparation cannot start a later compiler",starts==0&&!window->isTypesetting());
 window->typeset();window->typeset();check("a duplicate request shares one project reservation",window->isTypesetting());check("a subsequent full build succeeds after cancellation",waitFor([&]{return !window->isTypesetting();})&&starts==1&&!Research::previewFile(root).isEmpty()&&Research::pdfPairValid(Research::previewFile(root)));
 const QString preview=Research::previewFile(root);QFile previous(preview);previous.open(QIODevice::ReadOnly);const QByteArray before=previous.readAll();previous.close();
 window->typeset();window->interrupt();settle(500);QFile unchanged(preview);check("cancelling another preparation retains the certified PDF",unchanged.open(QIODevice::ReadOnly)&&unchanged.readAll()==before);unchanged.close();
 window->setModified(false);delete window;settle(100);
 auto *closing=new TeXDocumentWindow(root);closing->show();closing->typeset();const bool wasPreparing=closing->property("researchBuildPreparing").toBool();if(!wasPreparing){closing->interrupt();waitFor([&]{return !closing->isTypesetting();});}QPointer<TeXDocumentWindow> owner(closing);closing->close();check("closing during preparation removes the owner promptly",wasPreparing&&waitFor([&]{return !owner;},1000));settle(500);
 auto *synchronous=new TeXDocumentWindow(root);synchronous->show();bool cancelled=false;auto connection=QObject::connect(&app.typesetManager(),&Tw::Utils::TypesetManager::typesettingStarted,synchronous,[&](const QString &file){if(app.typesetManager().getOwnerForRootFile(file)==synchronous){cancelled=true;synchronous->interrupt();}});synchronous->typeset();settle(200);QObject::disconnect(connection);check("synchronous start notification can cancel before worker dispatch",cancelled&&!synchronous->isTypesetting()&&!synchronous->property("researchBuildPreparing").toBool());synchronous->setModified(false);delete synchronous;
 QFile source(root);check("preparation cancellation never rewrites the source",source.open(QIODevice::ReadOnly)&&source.readAll()==text);source.close();Research::setBuildCacheForTests(QString());save();app.exit(failures?1:0);
});return app.exec();}
