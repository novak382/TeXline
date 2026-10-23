#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include <QAction>
#include <QToolButton>
#include <ResearchCollaboration.cpp>
#include "Settings.h"
#include "CompletingEdit.h"
#include <QSaveFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QElapsedTimer>
#include <QThread>
#include <QMouseEvent>
#include <functional>
static bool waitFor(const std::function<bool()> &ready,int ms=9000){QElapsedTimer t;t.start();while(t.elapsed()<ms){if(ready())return true;QCoreApplication::processEvents();QThread::msleep(2);}return ready();}
static void settle(int ms=180){QElapsedTimer t;t.start();waitFor([&]{return t.elapsed()>=ms;},ms+1000);}
static bool put(QString p,QByteArray b){QDir().mkpath(QFileInfo(p).absolutePath());QSaveFile f(p);return f.open(QIODevice::WriteOnly)&&f.write(b)==b.size()&&f.commit();}
static void click(QWidget *w){QPoint p=w->rect().center();QMouseEvent press(QEvent::MouseButtonPress,QPointF(p),QPointF(w->mapToGlobal(p)),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(w,&press);QMouseEvent release(QEvent::MouseButtonRelease,QPointF(p),QPointF(w->mapToGlobal(p)),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(w,&release);}
int main(int argc,char **argv){QString report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);QTimer::singleShot(80,&app,[&]{using namespace Research;QJsonArray checks,diagnostics;QPointer<ProjectUpdates> traced;int failures=0;auto check=[&](QString n,bool p){checks.append(QJsonObject{{"name",n},{"passed",p}});failures+=!p;if(traced){QJsonArray known;for(auto it=traced->known.cbegin();it!=traced->known.cend();++it)known.append(QJsonObject{{"path",it.key()},{"contentSha256",QString::fromLatin1(digest(it.value().content.text.toUtf8()).toHex())},{"missing",it.value().missing}});diagnostics.append(QJsonObject{{"check",n},{"epoch",traced->epoch},{"busy",traced->busy},{"candidate",traced->hasCandidate},{"timerActive",traced->timer.isActive()},{"records",QJsonArray::fromStringList(traced->records.keys())},{"known",known},{"watchedFiles",QJsonArray::fromStringList(traced->watcher.files())},{"watchedFolders",QJsonArray::fromStringList(traced->watcher.directories())}});}put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"diagnostics",diagnostics}}).toJson());};
Tw::Settings().setValue("openPDFwithTeX",false);Research::setFocusMode(true);
QString folder=QFileInfo(report).absolutePath()+"/review-flow",root=folder+"/main.tex",chapter=folder+"/chapters/证明.tex";
QByteArray main="\\documentclass{article}\n\\begin{document}\nOriginal body.\n\\input{chapters/证明}\n\\end{document}\n",proof="\\begin{equation}\na=b\\label{eq:one}\n\\end{equation}\n";put(root,main);put(chapter,proof);
auto *w=new TeXDocumentWindow(root);w->resize(920,760);w->show();auto *project=updateProjects.value(canonical(root));traced=project;check("project baseline settles on the independent root and child",project&&waitFor([&]{return project->known.size()==2&&!project->busy&&!project->hasCandidate;}));
main.replace("Original body.","Revised body.");proof.replace("a=b","a=b+c");check("both external atomic writes commit despite concurrent watcher reads",waitFor([&]{return put(root,main);})&&waitFor([&]{return put(chapter,proof);}));check("two external files are summarized without opening the child",waitFor([&]{return project->records.size()==2&&!project->busy&&!project->hasCandidate&&!project->timer.isActive();})&&!TeXDocumentWindow::findDocument(chapter));
w->collaboration()->review();settle();auto *browser=w->findChild<QTextBrowser*>("researchReviewText");auto entries=project->entries();int index=-1;for(int n=0;n<entries.size();++n)if(entries[n].file==canonical(chapter))index=n;check("review provides a precise child entry",browser&&index>=0);if(browser&&index>=0)QMetaObject::invokeMethod(browser,"anchorClicked",Qt::DirectConnection,Q_ARG(QUrl,QUrl("research-change:/"+QString::number(index))));settle();auto *child=TeXDocumentWindow::findDocument(chapter);check("child opens at the actual changed characters",child&&child->editor()->textCursor().selectedText()=="+c");
if(child){auto *panel=child->findChild<QDockWidget*>("researchReviewPanel");auto *next=child->findChild<QToolButton*>("researchReviewNext");child->grab().save(QFileInfo(report).absolutePath()+"/review-child-controls.png");check("the selected file keeps the visible review and continuation controls",panel&&panel->isVisible()&&next&&next->isVisible());check("only the selected file shows a review panel",!w->findChild<QDockWidget*>("researchReviewPanel")->isVisible());if(next&&next->isVisible()){click(next);settle();check("one mouse click continues to the next project change",w->findChild<QDockWidget*>("researchReviewPanel")->isVisible()&&w->editor()->textCursor().selectedText().contains("Revised"));auto *done=w->findChild<QAction*>("researchReviewDone");done->trigger();settle();check("finishing review acknowledges the complete project",project->entries().isEmpty());}else{check("one mouse click continues to the next project change",false);check("finishing review acknowledges the complete project",false);}}
for(auto *doc:TeXDocumentWindow::documentList())if(doc->fileName().startsWith(folder)){doc->setModified(false);delete doc;}settle(300);check("review panels release with their project",updateProjects.isEmpty());app.exit(failures?1:0);});return app.exec();}
