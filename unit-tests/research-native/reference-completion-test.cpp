#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "TeXDocumentWindow.h"
#include "CompletingEdit.h"
#include "ResearchAppearance.h"
#include "Settings.h"
#include "ResearchProject.h"
#include <QAction>
#include <QElapsedTimer>
#include <QInputMethodEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QListView>
#include <QSaveFile>
#include <QFile>
#include <QFileInfo>
#include <QThread>
#include <QTimer>
#include <functional>
static bool put(const QString &p,const QByteArray &bytes){QSaveFile f(p);return f.open(QIODevice::WriteOnly)&&f.write(bytes)==bytes.size()&&f.commit();}
static bool waitFor(const std::function<bool()> &ready,int ms=2500){QElapsedTimer t;t.start();while(!ready()&&t.elapsed()<ms){QCoreApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QThread::msleep(3);}return ready();}
static void settle(int ms){QElapsedTimer t;t.start();waitFor([&]{return t.elapsed()>=ms;},ms+500);}
static void key(QWidget *w,int code,QString text={},Qt::KeyboardModifiers mods=Qt::NoModifier){QKeyEvent e(QEvent::KeyPress,code,mods,text);QApplication::sendEvent(w,&e);QKeyEvent r(QEvent::KeyRelease,code,mods);QApplication::sendEvent(w,&r);}
int main(int argc,char **argv){const QString report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
QTimer::singleShot(100,&app,[&]{
 const QString folder=QFileInfo(report).absolutePath(),path=folder+"/main.tex",bib=folder+"/refs.bib";
 const QString base="\\documentclass{article}\n\\label{eq:alpha}\n\\label{eq:beta}\n\\bibliography{refs}\n\\begin{document}Text.\\end{document}\n";
 QJsonArray checks,geometry;int failures=0;auto check=[&](const QString &name,bool ok){checks.append(QJsonObject{{"name",name},{"passed",ok}});failures+=!ok;put(report,QJsonDocument(QJsonObject{{"checks",checks},{"geometry",geometry},{"failures",failures},{"physicalInputVerified",false}}).toJson());};
 check("independent paper and bibliography are atomically written",put(path,base.toUtf8())&&put(bib,"@article{paper:alpha,title={Independent source},author={A},year={2025}}\n"));
 Tw::Settings settings;settings.remove("researchCommandCompletionMode");settings.setValue("openPDFwithTeX",false);CompletingEdit::setAutocompleteEnabled(false);
 auto *w=new TeXDocumentWindow(path);w->resize(850,700);w->show();w->raise();w->activateWindow();auto *e=w->editor();QApplication::setActiveWindow(w);e->setFocus();settle(100);
 check("private editor has Qt focus before testing completion",e->hasFocus());
 auto *popup=w->findChild<QListView*>("researchReferenceCompletion");auto visible=[&]{return popup&&popup->isVisible();};
 auto action=[&](int mode){return w->findChild<QAction*>(QString("researchCommandCompletionMode%1").arg(mode));};
 auto reset=[&](const QString &suffix){key(e,Qt::Key_Escape);e->setPlainText(base+suffix);auto c=e->textCursor();c.movePosition(QTextCursor::End);e->setTextCursor(c);e->document()->clearUndoRedoStacks();w->setModified(true);w->raise();w->activateWindow();QApplication::setActiveWindow(w);e->setFocus();settle(25);};
 auto summon=[&]{key(e,Qt::Key_Slash,{},Qt::AltModifier);return waitFor(visible);};
 auto first=[&]{return visible()?popup->model()->index(0,0).data().toString():QString();};
 bool unfinished=true;
 for(const auto &environment:QStringList{"verbatim","verbatim*","Verbatim","BVerbatim","LVerbatim","lstlisting","minted","comment","filecontents","filecontents*"}){
   const QString raw="\\begin{"+environment+"}\n\\label{fake} \\ref{fake}\n";const auto masked=Research::maskProjectText(raw);unfinished&=masked.size()==raw.size()&&masked.count('\n')==raw.count('\n')&&!masked.contains("fake");
 }
 check("all unfinished literal environments retain offsets and suppress fake anchors",unfinished);
 const QString commented="% \\begin{verbatim}\n\\label{real}\n\\verb|\\end{verbatim}|\n";check("commented literal openings never swallow later real source",Research::maskProjectText(commented).contains("\\label{real}"));
 const QString escaped="\\% \\verb|\\label{fake}%| \\label{real}";check("escaped percent and literal percent preserve the later real label",Research::maskProjectText(escaped).contains("\\label{real}")&&!Research::maskProjectText(escaped).contains("fake"));
 bool inlineSafe=true;for(const auto &literal:QStringList{"\\verb|\\ref{fake}","\\lstinline[language=TeX]|\\ref{fake}","\\mintinline{tex}|\\ref{fake}","\\mintinline{tex}{\\ref{fake}}","\\lstinline{\\ref{fake}}"})inlineSafe&=!Research::maskProjectText(literal).contains("fake");
 check("closed and unfinished inline literal forms suppress fake references",inlineSafe);
 check("a missing inline delimiter never consumes the following source line",Research::maskProjectText("\\verb\n\\label{real}").contains("\\label{real}"));
 check("numeric inline delimiters retain literal reference protection",!Research::maskProjectText("\\verb2\\ref{fake}2").contains("fake"));
 check("default off is shared without writing a user preference",action(0)&&action(0)->isChecked()&&!settings.contains("researchCommandCompletionMode"));
 reset("\\ref{eq:");settle(450);check("off never automatically opens reference completion",!visible());key(e,Qt::Key_Slash,{},Qt::AltModifier);settle(100);check("off manual trigger leaves source and popup untouched",!visible()&&e->toPlainText()==base+"\\ref{eq:");
 action(1)->trigger();check("switching mode immediately retires reference candidates",!visible());reset("\\ref{eq:");settle(450);check("manual mode never starts reference completion on a source change",!visible());check("Alt slash opens the same reference provider in manual mode",summon()&&first().startsWith("eq:"));
 const QString beforeConfirm=e->toPlainText();key(e,Qt::Key_Return,{},Qt::ControlModifier);check("Ctrl Enter replaces only the reference fragment without a newline",e->toPlainText()==base+"\\ref{eq:alpha"&&!visible());e->undo();check("reference confirmation is one native undo step",e->toPlainText()==beforeConfirm);
 reset("\\ref{eq:");summon();const QString beforeEnter=e->toPlainText();key(e,Qt::Key_Return,"\n");settle(350);check("ordinary Enter inserts a newline rather than selecting a reference",e->toPlainText()==beforeEnter+"\n"&&!visible());
 reset("\\ref{eq:");summon();const QString beforeTab=e->toPlainText();key(e,Qt::Key_Tab,"\t");settle(350);check("Tab accepts the current reference without indentation",e->toPlainText()==base+"\\ref{eq:alpha"&&!visible());e->undo();check("Tab reference acceptance is one undo step",e->toPlainText()==beforeTab);
 reset("\\ref{eq:");summon();key(e,Qt::Key_Escape);settle(400);check("Escape retires reference requests until another explicit action",!visible());
 reset("\\ref{eq:");summon();action(0)->trigger();settle(400);check("off retires an already visible reference list",!visible());
 action(2)->trigger();reset("\\ref{eq");QElapsedTimer latency;latency.start();key(e,Qt::Key_Colon,":");settle(245);const bool early=visible();const bool appeared=waitFor(visible);check("automatic reference delay is at least 300 ms",!early&&appeared&&latency.elapsed()>=290);w->grab().save(folder+"/automatic-reference.png");key(e,Qt::Key_Escape);key(e,Qt::Key_Backspace);key(e,Qt::Key_Colon,":");check("an actual edit after Escape can reopen the same automatic reference prefix",waitFor(visible));
 reset("\\ref{eq");key(e,Qt::Key_Colon,":");key(e,Qt::Key_Left);settle(450);check("caret movement cancels a pending automatic reference request",!visible());
 reset("\\ref{eq:");summon();QInputMethodEvent preedit(QStringLiteral("中"),{});QApplication::sendEvent(e,&preedit);check("IME preedit immediately retires reference completion",!visible());const QString composing=e->toPlainText();key(e,Qt::Key_Return,{},Qt::ControlModifier);check("reference confirmation during composition cannot insert a candidate",!e->toPlainText().mid(base.size()).contains("eq:alpha"));QInputMethodEvent commit;commit.setCommitString(QStringLiteral("中文"));QApplication::sendEvent(e,&commit);settle(400);check("synthetic Chinese commit remains source without selecting a reference",e->toPlainText().contains(QStringLiteral("中文"))&&!visible());
 action(1)->trigger();reset("\\cref{eq:beta,\n  eq:");check("wrapped multiple references preserve their earlier key and spacing",summon());key(e,Qt::Key_Return,{},Qt::ControlModifier);check("wrapped reference confirmation changes only the current key",e->toPlainText()==base+"\\cref{eq:beta,\n  eq:alpha");
 reset("\\citep[see][p.~1]{paper:");check("two optional citation notes retain bibliography completion",summon()&&first().startsWith("paper:alpha"));key(e,Qt::Key_Return,{},Qt::ControlModifier);check("citation confirmation retains optional notes and command",e->toPlainText()==base+"\\citep[see][p.~1]{paper:alpha");
 reset("\\parencite{paper:");check("biblatex citation forms use the reference provider",summon()&&first().startsWith("paper:alpha"));
 for(const auto &suffix:QStringList{"% \\ref{eq:","\\begin{verbatim}\n\\ref{eq:"}){reset(suffix);key(e,Qt::Key_Slash,{},Qt::AltModifier);settle(450);check("comments and protected literal contexts remain suppressed: "+suffix,!visible());}
 reset("\\le");key(e,Qt::Key_Slash,{},Qt::AltModifier);check("reference shortcut routing preserves ordinary command completion",!visible()&&waitFor([&]{auto *command=w->findChild<QListView*>("researchCommandCompletion");return command&&command->isVisible();}));key(e,Qt::Key_Escape);
 if(qEnvironmentVariableIsSet("TEXLINE_TEST_REFERENCE_EDGE"))for(bool focus:{false,true}){
  action(2)->trigger();Research::setFocusMode(focus);w->resize(500,270);settle(200);reset(QString(12,QLatin1Char('\n'))+QString(32,QLatin1Char('x'))+" \\ref{eq");key(e,Qt::Key_Colon,":");const bool shown=waitFor(visible);
  const auto caret=e->cursorRect();const QRect inputRect(e->viewport()->mapToGlobal(caret.topLeft()),caret.size()),client(w->mapToGlobal(QPoint()),w->size());
  auto rectangle=[](const QRect &r){return QJsonArray{r.x(),r.y(),r.width(),r.height()};};geometry.append(QJsonObject{{"focus",focus},{"shown",shown},{"client",rectangle(client)},{"caret",rectangle(inputRect)},{"popup",rectangle(popup->geometry())}});
  check(QString("%1 reference popup remains inside the window and clear of the caret").arg(focus?"focus":"normal"),shown&&client.contains(popup->geometry())&&!popup->geometry().intersects(inputRect));
  if(shown)w->grab().save(folder+QString("/reference-edge-%1.png").arg(focus?"focus":"normal"));key(e,Qt::Key_Escape);
 }
 QFile original(path);original.open(QIODevice::ReadOnly);check("completion never overwrites the independent disk manuscript",original.readAll()==base.toUtf8());original.close();w->setModified(false);w->close();app.exit(failures?1:0);
});return app.exec();}
