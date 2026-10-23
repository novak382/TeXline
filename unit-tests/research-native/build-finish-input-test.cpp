#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "TWScriptManager.h"
#include "Engine.h"
#include "TeXDocumentWindow.h"
#include "CompletingEdit.h"
#include "ResearchBuild.h"
#include "Settings.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QMessageBox>
#include <QPointer>
#include <QSaveFile>
#include <QTabWidget>
#include <QThread>
#include <QTimer>
#include <functional>
static bool put(const QString &p,const QByteArray &b){QDir().mkpath(QFileInfo(p).absolutePath());QSaveFile f(p);return f.open(QIODevice::WriteOnly)&&f.write(b)==b.size()&&f.commit();}
static QByteArray read(const QString &p){QFile f(p);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray();}
static bool waitFor(const std::function<bool()> &ready,int ms=90000){QElapsedTimer t;t.start();while(!ready()&&t.elapsed()<ms){QCoreApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QThread::msleep(2);}return ready();}
class PaintedInput : public QObject {
public:
 CompletingEdit *editor;QElapsedTimer clock;QImage before;QString original;bool entered=false,painted=false,restored=false;int different=0;double delivered=0,completed=0;
 explicit PaintedInput(CompletingEdit *e):editor(e){editor->viewport()->installEventFilter(this);}
 bool eventFilter(QObject *object,QEvent *event) override {
  if(object!=editor->viewport()||event->type()!=QEvent::Paint||!entered||painted)return false;painted=true;
  QTimer::singleShot(0,this,[this]{completed=clock.nsecsElapsed()/1e6;const auto after=editor->viewport()->grab(QRect(0,0,qMin(650,editor->viewport()->width()),100)).toImage().convertToFormat(QImage::Format_RGB32);if(after.size()==before.size())for(int y=0;y<after.height();++y)for(int x=0;x<after.width();++x)different+=after.pixel(x,y)!=before.pixel(x,y);const bool changed=editor->toPlainText()=="Z"+original;editor->undo();restored=changed&&editor->toPlainText()==original;});return false;
 }
};
int main(int argc,char **argv){const QString report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);QTimer::singleShot(100,&app,[&]{
 const QString folder=QFileInfo(report).absolutePath(),root=folder+"/project/main.tex";Research::setBuildCacheForTests(folder+"/cache");Tw::Settings().setValue("binaryPaths","C:/texlive/2023/bin/windows");Tw::Settings().setValue("openPDFwithTeX",false);Tw::Settings().setValue("researchCommandCompletionMode",0);
 Engine engine("Finish lifecycle","C:/texlive/2023/bin/windows/pdflatex.exe",{"-synctex=1","-interaction=nonstopmode","-file-line-error","$fullname"},false);app.setEngineList({engine});
 QByteArray source="\\documentclass{article}\n\\usepackage{graphicx}\n";bool assets=true;for(int n=0;n<8;++n){const QByteArray name="asset"+QByteArray::number(n)+".jpg";source+="\\newcommand{\\unused"+QByteArray(1,char('a'+n))+"}{\\includegraphics{"+name+"}}\n";assets&=put(QFileInfo(root).absolutePath()+"/"+QString::fromLatin1(name),QByteArray(5*1024*1024,char('a'+n)));}source+="\\begin{document}Independent finishing source.\\end{document}\n";
 QJsonArray checks;int failures=0;auto save=[&]{put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"physicalInputVerified",false}}).toJson());};auto check=[&](const QString &name,bool passed){checks.append(QJsonObject{{"name",name},{"passed",passed}});failures+=!passed;save();};
 check("private 40 MiB fixture and compiler available",assets&&put(root,source)&&engine.isAvailable());
 const QString hookDir=QCoreApplication::applicationDirPath()+"/resources/scripts/Hooks",hook=hookDir+"/private-finish-probe.js";
 const QByteArray custom="// TeXworksScript\n// Title: Private custom finish hook\n// Script-Type: hook\n// Hook: AfterTypeset\nTW.target.windowTitle='Custom finish hook ran';TW.result='<html><body>Private retained hook result</body></html>';undefined;\n";
 check("custom hook created only in private resources",put(hook,custom));app.getScriptManager()->reloadScripts();
 auto *w=new TeXDocumentWindow(root);w->resize(850,700);w->show();QApplication::setActiveWindow(w);w->editor()->setFocus();
 auto done=[&]{return !w->isTypesetting()&&!w->property("researchPublishing").toBool();};w->typeset();check("baseline successful build and published pair",waitFor(done)&&w->property("researchPublishStatus").toString()=="updated"&&Research::pdfPairValid(Research::previewFile(root)));
 bool customTab=false;for(auto *tabs:w->findChildren<QTabWidget *>())for(int i=0;i<tabs->count();++i)customTab|=tabs->tabText(i)=="Private custom finish hook";
 check("custom after-typeset hook still runs with live target and output",w->windowTitle().contains("Custom finish hook ran")&&customTab);
 const QString unrelatedFile=folder+"/other/main.tex";put(unrelatedFile,"\\documentclass{article}\n\\begin{document}Other work.\\end{document}\n");auto *unrelated=new TeXDocumentWindow(unrelatedFile);unrelated->editor()->insertPlainText("% private unrelated change\n");unrelated->show();QApplication::setActiveWindow(w);w->editor()->setFocus();
 PaintedInput probe(w->editor());probe.original=w->editor()->toPlainText();auto cursor=w->editor()->textCursor();cursor.setPosition(0);w->editor()->setTextCursor(cursor);probe.before=w->editor()->viewport()->grab(QRect(0,0,qMin(650,w->editor()->viewport()->width()),100)).toImage().convertToFormat(QImage::Format_RGB32);
 w->typeset();check("completion has an explicit background state",waitFor([&]{return w->property("researchBuildFinishing").toBool();}));probe.clock.start();bool enteredDuringFinish=false;
 QTimer::singleShot(20,&app,[&]{enteredDuringFinish=w->property("researchBuildFinishing").toBool();probe.delivered=probe.clock.nsecsElapsed()/1e6;probe.entered=true;QKeyEvent key(QEvent::KeyPress,Qt::Key_Z,Qt::NoModifier,"Z");QApplication::sendEvent(w->editor(),&key);w->editor()->viewport()->update();});
 check("first input is actually painted and one undo restores it during completion",waitFor([&]{return probe.restored;})&&probe.different>100&&enteredDuringFinish);check("input-to-paint delay stays below 150 ms",probe.completed-probe.delivered<150);w->editor()->viewport()->removeEventFilter(&probe);
 check("temporary input and undo do not cause a false saved-source rejection",waitFor(done)&&w->property("researchPublishStatus").toString()=="updated"&&Research::pdfPairValid(Research::previewFile(root))&&!w->isModified());
 check("editing another project does not cancel or save unrelated work",unrelated->isModified()&&!read(unrelatedFile).contains("private unrelated change"));unrelated->setModified(false);delete unrelated;
 const QString preview=Research::previewFile(root),shared=QFileInfo(root).absolutePath()+"/main.pdf";const auto cachedBefore=read(preview),sharedBefore=read(shared);
 w->typeset();check("dirty-buffer scenario reaches background completion",waitFor([&]{return w->property("researchBuildFinishing").toBool();}));w->editor()->insertPlainText("% unsaved during completion\n");
 check("dirty buffer during completion cannot replace either valid PDF",waitFor(done)&&w->property("researchPublishStatus").toString()=="not-published"&&read(preview)==cachedBefore&&read(shared)==sharedBefore);w->editor()->undo();w->setModified(false);
 w->typeset();check("cancel scenario reaches background completion",waitFor([&]{return w->property("researchBuildFinishing").toBool();}));QElapsedTimer stop;stop.start();w->interrupt();const double stopped=stop.nsecsElapsed()/1e6;
 check("completion cancellation returns promptly and preserves pair",stopped<100&&waitFor(done)&&w->property("researchPublishStatus").toString()=="cancelled"&&read(preview)==cachedBefore&&read(shared)==sharedBefore);
 QElapsedTimer cancelledSettle;cancelledSettle.start();waitFor([&]{return cancelledSettle.elapsed()>400;},1000);check("cancelled completion never schedules a silent automatic retry",done()&&w->property("researchPublishStatus").toString()=="cancelled"&&read(preview)==cachedBefore&&read(shared)==sharedBefore);
 w->typeset();check("closing scenario reaches background completion",waitFor([&]{return w->property("researchBuildFinishing").toBool();}));QPointer<TeXDocumentWindow> owner(w);QTimer::singleShot(10,&app,[]{for(auto *widget:QApplication::topLevelWidgets())if(auto *box=qobject_cast<QMessageBox *>(widget))box->done(QMessageBox::Yes);});w->close();
 check("closing owned window retires pending completion safely",waitFor([&]{return !owner;},1500));QElapsedTimer settle;settle.start();waitFor([&]{return settle.elapsed()>600;},1500);
 check("closed callback cannot publish or modify original source",read(preview)==cachedBefore&&read(shared)==sharedBefore&&read(root)==source);
 check("private customized hook remains byte-identical",read(hook)==custom);QFile::remove(hook);app.getScriptManager()->reloadScripts();
 Research::setBuildCacheForTests(QString());put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"physicalInputVerified",false},{"inputToPaintMs",probe.completed-probe.delivered},{"changedPixels",probe.different},{"interruptReturnMs",stopped}}).toJson());app.exit(failures?1:0);
 });return app.exec();}
