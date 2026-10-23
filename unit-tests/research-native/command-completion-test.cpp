#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "TeXDocumentWindow.h"
#include "CompletingEdit.h"
#include "ResearchAppearance.h"
#include "ResearchProject.h"
#include "Settings.h"
#include <QAction>
#include <QAbstractNativeEventFilter>
#include <QElapsedTimer>
#include <QInputMethodEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QListView>
#include <QMenu>
#include <QMouseEvent>
#include <QPointer>
#include <QSaveFile>
#include <QThread>
#include <QTimer>
#include <windows.h>
#include <functional>
static bool waitFor(const std::function<bool()> &ready,int ms=5000){QElapsedTimer t;t.start();while(!ready()&&t.elapsed()<ms){QCoreApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QThread::msleep(3);}return ready();}
static void settle(int ms=100){QElapsedTimer t;t.start();waitFor([&]{return t.elapsed()>=ms;},ms+500);}
static bool put(const QString &path,const QByteArray &data){QSaveFile f(path);return f.open(QIODevice::WriteOnly)&&f.write(data)==data.size()&&f.commit();}
static void key(QWidget *w,int code,QString text={},Qt::KeyboardModifiers mods=Qt::NoModifier){QKeyEvent e(QEvent::KeyPress,code,mods,text);QApplication::sendEvent(w,&e);QKeyEvent release(QEvent::KeyRelease,code,mods);QApplication::sendEvent(w,&release);}
static void type(QWidget *w,const QString &text){for(const auto c:text)key(w,c==QLatin1Char('\\')?int(Qt::Key_Backslash):int(c.toUpper().unicode()),QString(c));settle(10);}
class FixtureInput final:public QAbstractNativeEventFilter{public:int ignored=0;bool nativeEventFilter(const QByteArray &type,void *message,qintptr *result)override{if(!message||(type!="windows_generic_MSG"&&type!="windows_dispatcher_MSG"))return false;auto *m=static_cast<MSG*>(message);if(!QWidget::find(reinterpret_cast<WId>(m->hwnd)))return false;if((m->message>=WM_KEYFIRST&&m->message<=WM_KEYLAST)||(m->message>=WM_LBUTTONDOWN&&m->message<=WM_MBUTTONDBLCLK)){++ignored;if(result)*result=0;return true;}return false;}};
int main(int argc,char **argv){const QString report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);FixtureInput input;app.installNativeEventFilter(&input);
QTimer::singleShot(100,&app,[&]{QJsonArray checks;int failures=0;const QString folder=QFileInfo(report).absolutePath(),path=folder+"/completion.tex";const QByteArray original="\\documentclass{article}\n\\label{eq:alpha}\n\\label{eq:beta}\n\\begin{document}\nText.\n\\end{document}\n";put(path,original);
auto check=[&](QString name,bool pass){checks.append(QJsonObject{{"name",name},{"passed",pass}});failures+=!pass;put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures}}).toJson());};
Tw::Settings settings;settings.remove("researchCommandCompletionMode");settings.remove("researchCommandCompletionUsage");settings.setValue("openPDFwithTeX",false);settings.setValue("researchFocusMode",false);app.applyTranslation("zh_CN");auto *w=new TeXDocumentWindow(path);w->resize(850,700);w->show();w->raise();w->activateWindow();auto *e=w->editor();e->setFocus();settle(150);
auto action=[&](int n){return w->findChild<QAction*>(QString("researchCommandCompletionMode%1").arg(n));};
auto popup=[&]{return w->findChild<QListView*>("researchCommandCompletion");};auto visible=[&]{return popup()&&popup()->isVisible();};
auto reset=[&](QString text){key(e,Qt::Key_Escape);e->setPlainText(text);auto c=e->textCursor();c.movePosition(QTextCursor::End);e->setTextCursor(c);e->document()->clearUndoRedoStacks();w->raise();w->activateWindow();e->setFocus();settle(30);};
auto summon=[&]{key(e,Qt::Key_Slash,{},Qt::AltModifier);settle(30);};
auto row=[&](QString label){if(!popup())return -1;for(int n=0;n<popup()->model()->rowCount();++n)if(popup()->model()->index(n,0).data().toString()==label)return n;return -1;};
auto choose=[&](int index){if(popup()&&index>=0)popup()->setCurrentIndex(popup()->model()->index(index,0));key(e,Qt::Key_Return,{},Qt::ControlModifier);settle(20);};
auto mouseChoose=[&](int index){if(!visible()||index<0)return;const auto modelIndex=popup()->model()->index(index,0);popup()->scrollTo(modelIndex);const QPoint position=popup()->visualRect(modelIndex).center();auto *target=popup()->viewport();for(const auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}){QMouseEvent mouse(type,QPointF(position),QPointF(target->mapToGlobal(position)),Qt::LeftButton,type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(target,&mouse);}settle(20);};
check("first use defaults off without writing a preference",action(0)&&action(0)->isChecked()&&!settings.contains("researchCommandCompletionMode"));
bool managed=w->findChild<QAction *>("researchConfigureCompletion");for(int mode=0;mode<3;++mode)managed&=action(mode)&&!w->findChild<QMenu*>("researchMoreMenu")->actions().contains(action(mode))&&!w->findChild<QMenu*>("researchFocusMenu")->actions().contains(action(mode));check("three exclusive completion modes remain managed through settings",managed);
bool free=true;for(auto *a:w->findChildren<QAction*>())for(const auto &s:a->shortcuts())free&=s!=QKeySequence("Alt+/")&&s!=QKeySequence("Ctrl+Return")&&s!=QKeySequence("Ctrl+Enter");check("trigger and confirmation do not collide with registered shortcuts",free&&action(1)->text().contains("Alt+/"));
reset("\\le");summon();settle(350);check("off creates no popup and inserts no command",!popup()&&e->toPlainText()=="\\le");CompletingEdit::setAutocompleteEnabled(true);key(e,Qt::Key_Tab,"\t");check("off command prefix Tab still indents rather than expanding",e->toPlainText()=="\\le\t");
action(1)->trigger();check("manual selection persists and remains exclusive",settings.value("researchCommandCompletionMode").toInt()==1&&action(1)->isChecked()&&!action(0)->isChecked()&&!action(2)->isChecked());
reset("");type(e,"\\le");settle(450);check("manual typing alone never opens candidates",!visible());summon();check("manual shortcut opens a local one-line list without editing",visible()&&e->toPlainText()=="\\le"&&row("\\le")>=0&&row("\\leq")>=0&&row("\\leq")<6&&row("\\left(…\\right)")>=0&&row("\\left[…\\right]")>=0);
check("popup uses at most six compact single-line rows with on-demand descriptions",popup()&&popup()->font().pixelSize()==14&&popup()->height()<=174&&!popup()->model()->index(0,0).data(Qt::UserRole+1).toString().isEmpty()&&popup()->horizontalScrollBarPolicy()==Qt::ScrollBarAlwaysOff&&!popup()->wordWrap());
popup()->grab().save(folder+"/command-manual.png");choose(row("\\le"));check("a legal le command stays le instead of becoming leq",e->toPlainText()=="\\le");
reset("a+\\le");summon();check("candidate window receives pointer input without a transparent target",!popup()->testAttribute(Qt::WA_TransparentForMouseEvents));mouseChoose(row("\\left(…\\right)"));check("mouse confirmation closes the list and records the confirmed expansion",!visible()&&settings.value("researchCommandCompletionUsage").toStringList().first().endsWith("\t\\left(\\right)"));check("round pair replaces only the prefix and places caret inside",e->toPlainText()=="a+\\left(\\right)"&&e->textCursor().position()==8);e->undo();check("one undo restores the entire prefix replacement",e->toPlainText()=="a+\\le");e->redo();check("redo restores the paired snippet",e->toPlainText()=="a+\\left(\\right)");
reset("\\le");summon();choose(row("\\left[…\\right]"));check("square pair is balanced with an interior caret",e->toPlainText()=="\\left[\\right]"&&e->textCursor().position()==6);e->undo();
summon();const int before=popup()->currentIndex().row();key(e,Qt::Key_Down);check("arrows select without moving the source caret",popup()->currentIndex().row()==before+1&&e->textCursor().position()==3);key(e,Qt::Key_Up);key(e,Qt::Key_Escape);settle(350);check("Esc cancels without edits or delayed reopening",!visible()&&e->toPlainText()=="\\le");
reset("\\l");summon();type(e,"eq");check("manual session updates the same list as typing continues",waitFor(visible)&&row("\\leq")>=0&&popup()->model()->index(0,0).data().toString()=="\\leq");choose(0);check("exact leq remains a legal unmodified command",e->toPlainText()=="\\leq");
reset("\\le");e->setReadOnly(true);summon();check("read-only sources never offer mutating candidates",!visible()&&e->toPlainText()=="\\le");e->setReadOnly(false);
reset("\\le");summon();const auto oldIndex=popup()->model()->index(0,0);action(0)->trigger();QMetaObject::invokeMethod(popup(),"clicked",Qt::DirectConnection,Q_ARG(QModelIndex,oldIndex));check("an obsolete click cannot insert after disabling completion",e->toPlainText()=="\\le");action(1)->trigger();
reset("\\left");auto c=e->textCursor();c.setPosition(3);e->setTextCursor(c);summon();check("completion inside an existing command cannot corrupt its suffix",!visible()&&e->toPlainText()=="\\left");
action(2)->trigger();reset("");QElapsedTimer pause;pause.start();type(e,"\\le");settle(190);check("automatic candidates do not appear immediately",!visible());const bool opened=waitFor(visible,900);check("automatic trigger appears after approximately 300ms",opened&&pause.elapsed()>=280&&pause.elapsed()<1000);put(folder+"/command-delay.json",QJsonDocument(QJsonObject{{"elapsedMs",double(pause.elapsed())},{"appeared",opened}}).toJson());
const QString beforeEnter=e->toPlainText();key(e,Qt::Key_Return,"\n");settle(350);check("ordinary Enter inserts a newline and never accepts automatic candidates",!visible()&&e->toPlainText()==beforeEnter+"\n");
reset("");type(e,"\\le");waitFor(visible);popup()->setCurrentIndex(popup()->model()->index(row("\\leq"),0));const auto tabChoice=popup()->currentIndex().data().toString();key(e,Qt::Key_Tab,"\t");check("automatic popup Tab accepts its selected command once",!visible()&&e->toPlainText()==tabChoice);
for(const auto &sample:QStringList{"ordinary English text",QStringLiteral("普通中文正文"),"% \\le","\\begin{verbatim}\n\\le","\\begin{lstlisting}\n\\le","\\begin{minted}{tex}\n\\le","\\verb|\\le","\\lstinline|\\le","\\mintinline{tex}|\\le"}){reset(sample.left(sample.size()-1));type(e,sample.right(1));settle(350);check("automatic suppression: "+sample,!visible());}
for(const auto &sample:QStringList{"\\% \\le","% \\begin{verbatim}\n\\le","\\begin{verbatim}\nraw\n\\end{verbatim}\n\\le","\\verb|raw| \\le"}){reset(sample.left(sample.size()-1));type(e,sample.right(1));check("escaped or closed literal context allows a real command: "+sample,waitFor(visible,1000));}
reset("");type(e,"\\leftarrow");check("fast typing cannot show a stale shorter prefix",waitFor(visible)&&popup()->model()->index(0,0).data().toString()=="\\leftarrow");key(e,Qt::Key_Escape);settle(350);check("automatic Esc remains cancelled until a new edit",!visible());
reset("");type(e,"\\le");key(e,Qt::Key_Left);settle(450);check("moving the caret cancels pending automatic completion",!visible());
reset("\\le");summon();QInputMethodEvent preedit(QStringLiteral("中"),{});QApplication::sendEvent(e,&preedit);check("IME preedit immediately hides command candidates",!visible());key(e,Qt::Key_Return,{},Qt::ControlModifier);check("confirmation during composition cannot insert a candidate",!e->toPlainText().contains("\\left"));QInputMethodEvent commit;commit.setCommitString(QStringLiteral("中文"));QApplication::sendEvent(e,&commit);settle(350);check("Chinese IME commit remains text without an unwanted completion",e->toPlainText().contains(QStringLiteral("中文"))&&!visible());
for(bool focus:{false,true}){Research::setFocusMode(focus);w->resize(500,270);settle(180);reset(QString(12,QLatin1Char('\n'))+QString(32,QLatin1Char('x'))+" \\l");type(e,"e");const bool shown=waitFor(visible,1200);const auto caret=e->cursorRect();const QRect inputRect(e->viewport()->mapToGlobal(caret.topLeft()),caret.size()),client(w->mapToGlobal(QPoint()),w->size());check(QString("%1 edge popup stays within the window and clear of the caret").arg(focus?"focus":"normal"),shown&&client.contains(popup()->geometry())&&!popup()->geometry().intersects(inputRect)&&popup()->height()<=174);if(shown)w->grab().save(folder+QString("/command-edge-%1.png").arg(focus?"focus":"normal"));key(e,Qt::Key_Escape);}
w->resize(850,700);settle();app.applyTranslation("en_US");check("completion settings localize without a new menu level",action(0)->text()=="Completion: off"&&action(1)->text().startsWith("Completion: manual")&&action(2)->text()=="Completion: automatic");
action(1)->trigger();
auto first=[&]{return visible()?popup()->model()->index(0,0).data().toString():QString();};
reset("\\le");summon();choose(row("\\leq"));reset("\\le");summon();
check("confirmed leq choice outranks the prefix but never removes legal le",first()=="\\leq"&&row("\\le")>=0&&e->toPlainText()=="\\le");
const auto beforeCancel=settings.value("researchCommandCompletionUsage").toStringList();key(e,Qt::Key_Down);key(e,Qt::Key_Escape);
check("selection movement and cancellation do not train preferences",settings.value("researchCommandCompletionUsage").toStringList()==beforeCancel);
reset("\\le");summon();choose(row("\\left(…\\right)"));reset("\\le");summon();check("a recent round pair is learned independently of bare left",first()=="\\left(…\\right)"&&row("\\left")>=0);
choose(row("\\left[…\\right]"));reset("\\le");summon();check("a changed bracket habit can replace the old round preference",first()=="\\left[…\\right]");popup()->grab().save(folder+"/command-learned.png");
const QStringList manualOrder=[&]{QStringList a;for(int i=0;i<popup()->model()->rowCount();++i)a.append(popup()->model()->index(i,0).data().toString());return a;}();
action(2)->trigger();reset("");type(e,"\\le");waitFor(visible,1000);QStringList automaticOrder;if(popup())for(int i=0;i<popup()->model()->rowCount();++i)automaticOrder.append(popup()->model()->index(i,0).data().toString());check("manual and automatic sessions share learned candidate order",manualOrder==automaticOrder&&e->toPlainText()=="\\le");
action(0)->trigger();const auto disabledHistory=settings.value("researchCommandCompletionUsage").toStringList();reset("");type(e,"\\le");summon();settle(350);check("off performs no learning and offers no automatic insertion",!visible()&&disabledHistory==settings.value("researchCommandCompletionUsage").toStringList());action(1)->trigger();
for(const auto &sample:QStringList{QString("a \\geq b\n").repeated(8),QString("% \\geq\n").repeated(8),"\\begin{verbatim}\n"+QString("\\geq\n").repeated(8)+"\\end{verbatim}\n",QString("\\verb|\\geq| ").repeated(8)+"\n",QString("\\lstinline|\\geq| ").repeated(8)+"\n",QString("\\mintinline{tex}|\\geq| ").repeated(8)+"\n"}){
  reset(sample+"\\ge");summon();const bool live=sample.startsWith("a ");check("nearby ranking excludes comments and literal code: "+sample.left(30),first()==(live?"\\geq":"\\ge")&&row("\\ge")>=0&&row("\\geq")>=0);
}
reset("\\newcommand{\\myoperator}{x}\n\\myop");summon();check("a locally defined command is offered without a package server",row("\\myoperator")>=0);choose(row("\\myoperator"));check("a document command replaces only its unfinished name",e->toPlainText().endsWith("\n\\myoperator"));e->undo();check("a document command insertion is one undo block",e->toPlainText().endsWith("\n\\myop"));
for(const auto &shape:QStringList{"[","("}){
  const QString sample="x+\\left"+shape+"a\\ri",expected=shape=="["?"\\right]":"\\right)";reset(sample);summon();check("unclosed left delimiter prioritizes its corresponding closing command: "+shape,first()==expected);choose(row(expected));check("closing delimiter keeps existing text and places caret at its end: "+shape,e->toPlainText()==sample.left(sample.size()-3)+expected&&e->textCursor().atEnd());e->undo();check("closing delimiter is exactly reversible: "+shape,e->toPlainText()==sample);
}
reset("\\left[ a+\\left( b\\ri");summon();check("nearest nested delimiter wins over an outer bracket or old habit",first()=="\\right)");
for(const auto &shape:QStringList{"[","("}){
  const QString closing=shape=="["?"]":")",expected="\\right"+closing,sample="\\left"+shape+" x\\ri"+closing;
  reset(sample);auto caret=e->textCursor();caret.movePosition(QTextCursor::Left);e->setTextCursor(caret);summon();choose(row(expected));
  check("closing completion consumes one identical existing delimiter: "+shape,e->toPlainText()==sample.left(sample.size()-4)+expected&&e->textCursor().atEnd());
  e->undo();check("existing closing delimiter and prefix are restored in one undo: "+shape,e->toPlainText()==sample);
}
reset("\\begin{verbatim}\n\\geq\n\\end{verbatim}\n\\ge");summon();check("protected-state checkpoints allow commands after a literal environment closes",first()=="\\ge");
bool bounded=true;QJsonArray training;
for(int i=0;i<70;++i){
  QString name="\\zzLocal"+QString(QChar('a'+i/26))+QString(QChar('a'+i%26));reset("\\newcommand{"+name+"}{x}\n\\zzLocal");summon();
  const int index=row(name),revision=e->document()->revision();const bool shown=visible(),focused=e->hasFocus(),active=w->isActiveWindow();choose(index);
  const bool inserted=e->toPlainText().endsWith(name);bounded&=index>=0&&inserted;
  training.append(QJsonObject{{"name",name},{"row",index},{"visibleBeforeConfirm",shown},{"editorFocused",focused},{"windowActive",active},{"revisionBefore",revision},{"revisionAfter",e->document()->revision()},{"inserted",inserted},{"textAfter",e->toPlainText()},{"historyEntries",settings.value("researchCommandCompletionUsage").toStringList().size()}});
}
put(folder+"/command-choice-training.json",QJsonDocument(training).toJson());
const auto history=settings.value("researchCommandCompletionUsage").toStringList();check("confirmed-choice storage remains bounded after 70 distinct document commands",bounded&&history.size()==64);bool payload=true;for(const auto &value:history){int p=value.indexOf('\t');payload&=p>0&&value.mid(p+1).size()<=512&&value.left(p).toInt()>=1&&value.left(p).toInt()<=1024;}check("persisted preferences contain bounded expansions and counts only",payload);
reset(QString("% long document\n").repeated(25000)+"\\ge");QJsonArray timings;bool accurate=true;for(int i=0;i<12;++i){key(e,Qt::Key_Escape);QElapsedTimer elapsed;elapsed.start();key(e,Qt::Key_Slash,{},Qt::AltModifier);accurate&=visible()&&row("\\ge")>=0;timings.append(double(elapsed.nsecsElapsed())/1000000.0);}check("repeated assistance on a 25000-line document stays correct",accurate);put(folder+"/command-ranking-measures.json",QJsonDocument(QJsonObject{{"manualPopupMs",timings},{"blocks",e->document()->blockCount()},{"historyEntries",history.size()},{"nearbyBlockLimit",128},{"nearbyCharacterLimit",16384}}).toJson());
reset("\\ge");summon();choose(row("\\geq"));reset("\\ge");summon();check("preferences are persisted after confirmed use and shared with a reopened file",first()=="\\geq"&&settings.value("researchCommandCompletionUsage").toStringList().first().endsWith("\t\\geq"));key(e,Qt::Key_Escape);
// Menu actions are shared with a controller, not owned by either menu. A
// late locale refresh must therefore also be honored when either menu opens.
app.applyTranslation("zh_CN");Research::setUiLocale("en_US");
for(const auto &name:{QString("researchMoreMenu"),QString("researchFocusMenu")}){
  auto *menu=w->findChild<QMenu*>(name);menu->popup(w->mapToGlobal(QPoint(20,60)));settle(60);
  menu->grab().save(folder+"/late-english-"+name+".png");
  check("late English locale refresh updates shared first-level completion entries: "+name,action(0)->text()=="Completion: off"&&action(1)->text().startsWith("Completion: manual")&&action(2)->text()=="Completion: automatic");
  QStringList untranslated;static const QRegularExpression chinese("[\\x{4e00}-\\x{9fff}]");for(auto *a:menu->actions())if(a->isVisible()&&!a->isSeparator()&&(!a->menu()||a->menu()->objectName()!="researchLanguageMenu")&&chinese.match(a->text()).hasMatch())untranslated.append(a->text());check("first-level entries use English except the bilingual language entry: "+name+(untranslated.isEmpty()?QString():" / "+untranslated.join(", ")),untranslated.isEmpty());
  menu->hide();
}
app.applyTranslation("en_US");
action(2)->trigger();
reset("\\le");summon();auto *other=new TeXDocumentWindow();other->show();other->activateWindow();other->editor()->setFocus();settle(150);check("switching files hides the former command list",!visible()&&other->findChild<QAction*>("researchCommandCompletionMode2")->isChecked());other->setModified(false);delete other;w->activateWindow();e->setFocus();
action(0)->trigger();QApplication::setActiveWindow(w);e->setFocus();reset(QString::fromUtf8(original)+"\\ref{eq:a");settle(350);auto referenceVisible=[&]{auto *list=w->findChild<QListView*>("researchReferenceCompletion");return list&&list->isVisible()&&list->model()->rowCount()>0;};check("shared off suppresses both command and reference candidates",!visible()&&!referenceVisible());
action(1)->trigger();summon();check("manual mode preserves reference completion beside command completion",waitFor(referenceVisible));key(e,Qt::Key_Return,{},Qt::ControlModifier);check("shared confirmation still inserts only the reference label",e->toPlainText().endsWith("\\ref{eq:alpha"));e->undo();check("reference acceptance remains one undo step",e->toPlainText().endsWith("\\ref{eq:a"));summon();waitFor(referenceVisible);key(e,Qt::Key_Tab,"\t");check("reference Tab accepts once with legacy abbreviations enabled",e->toPlainText().endsWith("\\ref{eq:alpha")&&!referenceVisible());
action(1)->trigger();w->setModified(false);QPointer<TeXDocumentWindow> guard(w);w->close();check("normal close removes fixture recovery state",waitFor([&]{return !guard;},1000));auto *reopened=new TeXDocumentWindow(path);reopened->show();settle(150);check("reopening restores the chosen completion mode",reopened->findChild<QAction*>("researchCommandCompletionMode1")->isChecked());reopened->findChild<QAction*>("researchCommandCompletionMode2")->trigger();reopened->activateWindow();reopened->editor()->setFocus();auto last=reopened->editor()->textCursor();last.movePosition(QTextCursor::End);reopened->editor()->setTextCursor(last);type(reopened->editor(),"\\le");QPointer<TeXDocumentWindow> closing(reopened);reopened->setModified(false);reopened->close();settle(450);check("close with pending completion cancels timers and removes the window",!closing);settings.setValue("researchCommandCompletionMode",0);
put(folder+"/command-input-isolation.json",QJsonDocument(QJsonObject{{"ignoredPhysicalFixtureInput",input.ignored},{"syntheticImePreeditAndCommit",true},{"physicalWindowsImeNotTested",true}}).toJson());app.exit(failures?1:0);
});return app.exec();}
