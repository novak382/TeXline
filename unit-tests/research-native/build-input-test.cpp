#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
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
#include <QSaveFile>
#include <QThread>
#include <QTimer>
#include <functional>
static bool put(const QString &path,const QByteArray &bytes){QDir().mkpath(QFileInfo(path).absolutePath());QSaveFile f(path);return f.open(QIODevice::WriteOnly)&&f.write(bytes)==bytes.size()&&f.commit();}
static bool waitFor(const std::function<bool()> &ready,int ms=30000){QElapsedTimer t;t.start();while(!ready()&&t.elapsed()<ms){QCoreApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QThread::msleep(2);}return ready();}
class InputPaint : public QObject {
public:
 CompletingEdit *editor;QElapsedTimer clock;bool entered=false,painted=false,restored=false,preparing=false;double delivered=0,completed=0;int changed=0;QString original;QImage before;
 explicit InputPaint(CompletingEdit *e):editor(e){editor->viewport()->installEventFilter(this);}
 bool eventFilter(QObject *object,QEvent *event)override{
  if(object!=editor->viewport()||event->type()!=QEvent::Paint||!entered||painted)return false;painted=true;
  QTimer::singleShot(0,this,[this]{completed=clock.nsecsElapsed()/1e6;const auto after=editor->viewport()->grab(QRect(0,0,qMin(650,editor->viewport()->width()),110)).toImage().convertToFormat(QImage::Format_RGB32);if(after.size()==before.size())for(int y=0;y<qMin(90,after.height());++y)for(int x=0;x<qMin(600,after.width());++x)changed+=before.pixel(x,y)!=after.pixel(x,y);const bool correct=editor->toPlainText()=="Z"+original;editor->undo();restored=correct&&editor->toPlainText()==original;});return false;
 }
};
int main(int argc,char **argv){const QString report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);QTimer::singleShot(100,&app,[&]{
 const QString folder=QFileInfo(report).absolutePath(),root=folder+"/project/main.tex";Research::setBuildCacheForTests(folder+"/cache");Tw::Settings().setValue("binaryPaths","C:/texlive/2023/bin/windows");Tw::Settings().setValue("openPDFwithTeX",false);Tw::Settings().setValue("researchCommandCompletionMode",0);
 const Engine engine("Input during preparation","C:/texlive/2023/bin/windows/pdflatex.exe",{"-synctex=1","-interaction=nonstopmode","-file-line-error","$fullname"},false);app.setEngineList({engine});
 QByteArray source="\\documentclass{article}\n\\usepackage{graphicx}\n";bool written=true;for(int n=0;n<8;++n){const QByteArray name="asset"+QByteArray::number(n)+".jpg";source+="\\newcommand{\\unused"+QByteArray(1,char('a'+n))+"}{\\includegraphics{"+name+"}}\n";written&=put(QFileInfo(root).absolutePath()+"/"+QString::fromLatin1(name),QByteArray(5*1024*1024,char('a'+n)));}source+="\\begin{document}Independent source.\\end{document}\n";
 QJsonArray checks;int failures=0;auto check=[&](const QString &name,bool ok){checks.append(QJsonObject{{"name",name},{"passed",ok}});failures+=!ok;};check("independent heavy project and compiler are available",written&&put(root,source)&&engine.isAvailable());
 auto *w=new TeXDocumentWindow(root);w->resize(850,700);w->show();QApplication::setActiveWindow(w);w->editor()->setFocus();QElapsedTimer settle;settle.start();waitFor([&]{return settle.elapsed()>180;});InputPaint probe(w->editor());probe.original=w->editor()->toPlainText();auto cursor=w->editor()->textCursor();cursor.setPosition(0);w->editor()->setTextCursor(cursor);probe.before=w->editor()->viewport()->grab(QRect(0,0,qMin(650,w->editor()->viewport()->width()),110)).toImage().convertToFormat(QImage::Format_RGB32);probe.clock.start();
 QTimer::singleShot(20,&app,[&]{probe.delivered=probe.clock.nsecsElapsed()/1e6;probe.preparing=w->property("researchBuildPreparing").toBool();probe.entered=true;QKeyEvent key(QEvent::KeyPress,Qt::Key_Z,Qt::NoModifier,"Z");QApplication::sendEvent(w->editor(),&key);w->editor()->viewport()->update();});w->typeset();
 check("queued input changes actual glyphs and remains one undo",waitFor([&]{return probe.restored;})&&probe.changed>100&&!w->isModified());check("input is delivered during background preparation",probe.preparing);check("planned input completes painting within 150 ms",probe.completed>0&&probe.completed<150);
 check("temporary input and undo do not invalidate the saved source build",waitFor([&]{return !w->isTypesetting();})&&!Research::previewFile(root).isEmpty()&&Research::pdfPairValid(Research::previewFile(root)));QFile original(root);check("the source file is never changed by the input experiment",original.open(QIODevice::ReadOnly)&&original.readAll()==source);original.close();w->setModified(false);delete w;Research::setBuildCacheForTests(QString());put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"plannedInputMs",20},{"deliveredMs",probe.delivered},{"paintCompletedMs",probe.completed},{"inputToPaintMs",probe.completed-probe.delivered},{"changedRasterPixels",probe.changed},{"physicalInputVerified",false}}).toJson());app.exit(failures?1:0);
});return app.exec();}
