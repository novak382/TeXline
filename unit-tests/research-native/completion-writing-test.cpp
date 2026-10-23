// Independent completion acceptance scenarios. Run only on an inactive private
// desktop, with private resources/settings and the exact candidate objects.
#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "TeXDocumentWindow.h"
#include "CompletingEdit.h"
#include "ResearchAppearance.h"
#include "ResearchCommandCompletion.h"
#include "ResearchCompletion.h"
#include "ResearchProject.h"
#include "ResearchWorkflow.h"
#include "Settings.h"
#include <QAction>
#include <QComboBox>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QInputMethodEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QScreen>
#include <QSet>
#include <QShortcut>
#include <QThread>
#include <QTimer>
#include <QTabBar>
#include <QToolButton>
#include <functional>
#include <windows.h>
static bool privateDesktop(){wchar_t name[256]{};DWORD needed=0;return GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()),UOI_NAME,name,sizeof(name),&needed)&&QString::fromWCharArray(name)==qEnvironmentVariable("TEXLINE_TEST_DESKTOP")&&QString::fromWCharArray(name).startsWith("TeXlineValidation-");}
static bool waitFor(const std::function<bool()> &ready,int ms=5000){QElapsedTimer t;t.start();while(!ready()&&t.elapsed()<ms){QCoreApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QThread::msleep(2);}return ready();}
static void settle(int ms=50){QElapsedTimer t;t.start();waitFor([&]{return t.elapsed()>=ms;},ms+500);}
static bool put(QString path,QByteArray data){QSaveFile f(path);return f.open(QIODevice::WriteOnly)&&f.write(data)==data.size()&&f.commit();}
static void key(QWidget *w,int code,QString text={},Qt::KeyboardModifiers mods=Qt::NoModifier){QKeyEvent e(QEvent::KeyPress,code,mods,text),r(QEvent::KeyRelease,code,mods);QApplication::sendEvent(w,&e);QApplication::sendEvent(w,&r);}
static void type(QWidget *w,const QString &text){for(auto c:text){key(w,int(c.toUpper().unicode()),QString(c));if(qEnvironmentVariableIsSet("TEXLINE_COMPLETION_ANIMATE"))settle(70);}settle(10);}
int main(int argc,char **argv){
  if(argc<2||!privateDesktop())return 2;
  const auto report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
  QTimer::singleShot(100,&app,[&]{
    QJsonArray checks,frames,timings,diagnostics;int failures=0;const QString folder=QFileInfo(report).absolutePath();
    auto save=[&]{put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"frames",frames},{"timings",timings},{"diagnostics",diagnostics},{"physicalInputVerified",false},{"recordingKind","actual widget rasters; top-level completion composited at measured coordinates"}}).toJson());};
    auto check=[&](QString name,bool pass){checks.append(QJsonObject{{"name",name},{"passed",pass}});failures+=!pass;save();};
    const QString declarations=R"(\newcommand{\norm}[2][2]{\left\lVert #2\right\rVert_{#1}}
\DeclareMathOperator*{\argmax}{arg\,max}
\NewDocumentCommand{\risk}{m O{\lambda}}{#1+#2}
\NewDocumentCommand{\flagged}{s m}{#2}
\newenvironment{assumptionbox}[1][Assumptions]{\paragraph{#1}}{}
\newtheorem{claim}{Claim}
\def\pair#1#2{(#1,#2)}
\def\delimited#1,#2{(#1,#2)}
\newcommand{\outer}{\newcommand{\notglobal}{x}}
% \newcommand{\notcomment}{x}
\begin{verbatim}
\newcommand{\notliteral}{x}
\end{verbatim}
)";
    const auto parsed=Research::completionDeclarations(Research::maskProjectText(declarations),"defs.tex",QByteArray(32,'h'));
    auto declaration=[&](QString name)->const Research::MacroCompletion*{for(const auto &e:parsed)if(e.name==name)return &e;return nullptr;};
    check("literal/comment and nested replacement definitions are not exported",!declaration("notcomment")&&!declaration("notliteral")&&!declaration("notglobal"));
    const auto *norm=declaration("norm");check("newcommand optional default and required arity",norm&&norm->arguments.size()==2&&norm->arguments[0].optional&&norm->arguments[0].value=="2"&&!norm->arguments[1].optional);
    const auto *risk=declaration("risk");check("xparse preserves required/optional argument order",risk&&risk->signatureKnown&&risk->arguments.size()==2&&!risk->arguments[0].optional&&risk->arguments[1].optional&&risk->arguments[1].value=="\\lambda");
    check("unsupported xparse and delimited definitions retain names without guessed arguments",declaration("flagged")&&!declaration("flagged")->signatureKnown&&declaration("delimited")&&!declaration("delimited")->signatureKnown);
    check("simple primitive definition supports two required arguments",declaration("pair")&&declaration("pair")->signatureKnown&&declaration("pair")->arguments.size()==2);
    check("operator and theorem declarations retain source location",declaration("argmax")&&declaration("argmax")->arguments.isEmpty()&&declaration("claim")&&declaration("claim")->environment&&declaration("claim")->line==6);
    const auto snippet=Research::parseCompletionSnippet("\\frac{${2:\\sqrt{x}}}{${1:n}}$0");
    check("snippet defaults preserve nested TeX groups and numeric traversal order",snippet.valid()&&snippet.text=="\\frac{\\sqrt{x}}{n}"&&snippet.stops.size()==2&&snippet.stops[0].number==1&&snippet.stops[0].start>snippet.stops[1].start&&snippet.finalPosition==snippet.text.size());
    check("TeX dollars and escaped TeX braces survive a valid snippet",Research::parseCompletionSnippet("$x$ \\$5 ${1:\\{x\\}} $0").text=="$x$ $5 \\{x\\} ");
    const auto currency=Research::parseCompletionSnippet(QString::fromUtf8(R"(${1:\text{\$}} ${2:\$x} ${3:\\$5} $0)"));
    check("default fields preserve TeX currency escapes and only unescape snippet metadata",currency.valid()&&currency.text==QString::fromUtf8(R"(\text{\$} \$x \$5 )")&&currency.stops.size()==3);
    const auto unicodeDigits=Research::parseCompletionSnippet(QString::fromUtf8("$١$ ${1:١} $0"));
    check("non-ASCII math digits remain literal and cannot become a final cursor marker",unicodeDigits.valid()&&unicodeDigits.text==QString::fromUtf8("$١$ ١ ")&&unicodeDigits.stops.size()==1);
    {
      QMainWindow bindings;QAction chord(&bindings);chord.setShortcut(QKeySequence("Ctrl+Space, Ctrl+K"));
      check("suggestion trigger leaves a registered multi-key chord prefix available",!Research::completionShortcutAvailable(&bindings,QKeySequence("Ctrl+Space")));
      chord.setEnabled(false);QShortcut alternate(&bindings);alternate.setKeys({QKeySequence("Ctrl+L"),QKeySequence("Alt+/")});
      check("secondary QShortcut bindings are included in conflict detection",!Research::completionShortcutAvailable(&bindings,QKeySequence("Alt+/")));
      alternate.setEnabled(false);QAction backtab(&bindings);backtab.setShortcut(QKeySequence("Shift+Backtab"));
      check("Shift+Tab and Backtab conflict consistently",!Research::completionShortcutAvailable(&bindings,QKeySequence("Shift+Tab")));
      check("only the explicitly reused legacy action is exempt from conflict checks",Research::completionShortcutAvailable(&bindings,QKeySequence("Shift+Tab"),&backtab));
      QAction other(&bindings);other.setShortcut(QKeySequence("Shift+Tab"));
      check("reusing a legacy binding never exempts a second conflicting action",!Research::completionShortcutAvailable(&bindings,QKeySequence("Shift+Tab"),&backtab));
    }
    for(const auto &body:QStringList{"${1:missing","${1:x} ${1:y}","${1:${2:n}}","${TM_FILENAME}","${1/a/b/}"})check("invalid/unsupported snippet is rejected: "+body,!Research::parseCompletionSnippet(body).valid());
    const auto linked=Research::parseCompletionSnippet("$1+${1:x}=$2$0");
    check("repeated field defaults initialize every occurrence including forward references",linked.valid()&&linked.text=="x+x="&&linked.stops.size()==3);
    const auto selected=Research::parseCompletionSnippet("\\textbf{${1:${TM_SELECTED_TEXT}}}$0","α + $1$");
    check("selected text is inserted literally rather than parsed as snippet metadata",selected.valid()&&selected.usesSelection&&selected.text=="\\textbf{α + $1$}");
    const auto windowsLines=Research::parseCompletionSnippet("${1:x}\r\n$1$0");check("Windows snippet line endings retain correct mirror offsets",windowsLines.valid()&&windowsLines.text=="x\nx"&&windowsLines.stops.last().start==2);
    check("plain abbreviations are accepted with slash command prefixes",Research::validateUserCompletions({{"ff","Fraction","\\frac{$1}{$2}$0"},{"\\wrap","Wrap","${TM_SELECTED_TEXT}$0"}}).isEmpty());
    const QString main=folder+"/main.tex",defs=folder+"/defs.tex",style=folder+"/localnotation.sty",child=folder+"/section.tex",bib=folder+"/refs.bib";
    const QString head="\\documentclass{article}\n\\usepackage{localnotation}\n\\input{defs}\n\\input{section}\n\\bibliography{refs}\n\\begin{document}\n",tail="\n\\end{document}\n";
    const QByteArray original=(head+"Research note. 研究笔记。"+tail).toUtf8();
    check("independent research fixture written",put(main,original)&&put(defs,declarations.toUtf8())&&put(style,"\\newcommand{\\localrate}[1]{#1/n}\n")&&put(child,"\\section{Concentration}\n\\label{thm:main}\n\\label{eq:bound}\n")&&put(bib,"@article{Jin2025,title={Spectral methods},author={Jin, Example},year={2025}}\n@article{Wang2024,title={Concentration bounds},author={Wang, Example},year={2024}}\n"));
    const auto canonicalMain=QFileInfo(main).canonicalFilePath();const auto canonicalChild=QFileInfo(child).canonicalFilePath();
    check("worker root discovery follows a main file's included child",Research::resolveCompletionRoot(child,"\\section{Concentration}\n",{})==canonicalMain);
    check("worker root discovery recognizes a class beyond the short preamble",Research::resolveCompletionRoot(main,QString("% long preamble\n").repeated(1000)+"\\documentclass{article}",{})==canonicalMain);
    check("cancelled root discovery returns without publishing a guessed root",Research::resolveCompletionRoot(child,"text",{},[]{return true;}).isEmpty());
    const auto alternate=folder+"/alternate-main.tex";put(alternate,"\\documentclass{article}\n\\input{section}\n");check("ambiguous worker roots retain the current file",Research::resolveCompletionRoot(child,"text",{})==canonicalChild);QFile::remove(alternate);
    Research::rememberRoot(child,main);check("a remembered root retains precedence over a local class",Research::resolveCompletionRoot(child,"\\documentclass{article}",{})==canonicalMain);
    const auto index=Research::buildProjectIndex(main,{},true);QSet<QString> names;for(const auto &m:index->macros)names.insert(m.name);
    check("project follows included TeX and local style declarations",names.contains("norm")&&names.contains("localrate")&&index->files.contains(QFileInfo(style).canonicalFilePath()));
    QHash<QString,QString> buffers;buffers.insert(QFileInfo(defs).canonicalFilePath(),declarations+"\\newcommand{\\draftonly}[1]{#1}\n");const auto dirty=Research::buildProjectIndex(main,buffers,false);bool found=false;for(const auto &m:dirty->macros)found|=m.name=="draftonly";check("cross-file unsaved declarations override disk",found);
    const auto refs=Research::searchProject(*index,"eq:",5,1);check("labels and bibliography remain separate and prefix-ranked",!refs.isEmpty()&&index->entries[refs.first()].key=="eq:bound"&&!Research::searchProject(*index,"Wang",5,2).isEmpty());
    Tw::Settings settings;settings.setValue("openPDFwithTeX",false);settings.setValue("researchFocusMode",false);settings.setValue("researchOnboardingSeen",true);settings.setValue("researchCompletionCtrlSpace",false);settings.setValue("researchCompletionSnippetTab",false);settings.remove("researchLocalSnippetsV1");settings.remove("researchCommandCompletionUsage");
    app.applyTranslation(qEnvironmentVariable("TEXLINE_TEST_LOCALE","zh_CN"));
    auto *w=new TeXDocumentWindow(main);w->resize(850,700);w->show();w->activateWindow();auto *e=w->editor();e->setFocus();settle(150);Research::setCommandCompletionMode(1);
    auto popup=[&]{return w->findChild<QListView*>("researchCommandCompletion");};auto visible=[&]{return popup()&&popup()->isVisible();};
    auto row=[&](QString label){if(popup())for(int n=0;n<popup()->model()->rowCount();++n)if(popup()->model()->index(n,0).data().toString()==label)return n;return -1;};
    auto reset=[&](QString text,int pos=-1){key(e,Qt::Key_Escape);e->setPlainText(head+text+tail);auto c=e->textCursor();c.setPosition(head.size()+(pos<0?int(text.size()):pos));e->setTextCursor(c);e->document()->clearUndoRedoStacks();e->setFocus();settle(20);};
    auto summon=[&](QString label){key(e,Qt::Key_Slash,{},Qt::AltModifier);return waitFor([&]{return visible()&&row(label)>=0;});};
    auto choose=[&](QString label){const int n=row(label);if(n<0)return false;popup()->setCurrentIndex(popup()->model()->index(n,0));key(e,Qt::Key_Tab,"\t");settle(30);return true;};
    auto body=[&]{return e->toPlainText().mid(head.size(),e->toPlainText().size()-head.size()-tail.size());};
    QElapsedTimer clock;clock.start();int frame=0;QString recordingStory="project-macro";
    auto capture=[&](QString story){if(!qEnvironmentVariableIsSet("TEXLINE_COMPLETION_RECORD")||!w->isVisible())return;if(!story.isEmpty())recordingStory=story;auto raster=w->grab().toImage();QPainter painter(&raster);if(visible())painter.drawPixmap(popup()->mapToGlobal(QPoint())-w->mapToGlobal(QPoint()),popup()->grab());for(auto *top:QApplication::topLevelWidgets())if(top->isVisible()&&(top->objectName()=="researchCompletionDocumentation"||top->objectName()=="researchCompletionConfig"))painter.drawPixmap(top->mapToGlobal(QPoint())-w->mapToGlobal(QPoint()),top->grab());painter.end();const auto file=QString("frame-%1.png").arg(frame++,4,10,QLatin1Char('0'));raster.save(folder+"/"+file);frames.append(QJsonObject{{"file",file},{"story",recordingStory},{"ms",double(clock.elapsed())},{"dpr",w->devicePixelRatioF()}});save();};
    auto *recorder=new QTimer(w);recorder->setInterval(80);QObject::connect(recorder,&QTimer::timeout,w,[&]{capture(QString());});if(qEnvironmentVariableIsSet("TEXLINE_COMPLETION_ANIMATE"))recorder->start();
    reset("\\frac");key(e,Qt::Key_Slash,{},Qt::AltModifier);waitFor(visible);
    check("an unused fraction defaults to its useful argument template",visible()&&popup()->currentIndex().data().toString()=="\\frac{…}{…}");key(e,Qt::Key_Tab,"\t");
    check("default fraction acceptance immediately enters its first argument",body()=="\\frac{}{}"&&e->property("researchSnippetActive").toBool());
    key(e,Qt::Key_Escape);key(e,Qt::Key_Tab,"\t");check("Escape leaves Tab available for indentation",body()=="\\frac{\t}{}");
    reset("中文 \\nor");check("macro signature and source shown in candidate model",summon("\\norm{…}")&&popup()->model()->index(row("\\norm{…}"),0).data(Qt::UserRole+1).toString().contains("defs.tex"));
    check("suggestions stay compact without default documentation",popup()->height()<=174&&!w->findChild<QWidget*>("researchCompletionDocumentation"));key(e,Qt::Key_Slash,{},Qt::AltModifier);auto *documentation=w->findChild<QWidget*>("researchCompletionDocumentation");check("repeating trigger opens details without inserting text",documentation&&documentation->isVisible()&&body()=="中文 \\nor");if(qEnvironmentVariableIsSet("TEXLINE_COMPLETION_ANIMATE"))settle(500);key(e,Qt::Key_Slash,{},Qt::AltModifier);check("repeating trigger collapses details",documentation&&!documentation->isVisible());
    capture("project-macro");choose("\\norm[2]{…}");check("optional macro template selects its default",body()=="中文 \\norm[2]{}"&&e->textCursor().selectedText()=="2");type(e,"p");key(e,Qt::Key_Return,{},Qt::ControlModifier);type(e,"A");check("successive fields preserve CJK prefix and parameter positions",body()=="中文 \\norm[p]{A}");capture("project-macro");key(e,Qt::Key_Escape);
    reset("\\nor");summon("\\norm{…}");choose("\\norm{…}");e->undo();check("entire macro insertion is one undo",body()=="\\nor");e->redo();check("redo restores exact macro text without metadata",body()=="\\norm{}"&&!e->toPlainText().contains(QChar(0x2022)));
    reset("\\fr");summon("\\frac{…}{…}");choose("\\frac{…}{…}");type(e,"a+b");key(e,Qt::Key_Return,{},Qt::ControlModifier);type(e,"n");key(e,Qt::Key_Return,{},Qt::ControlModifier);check("fraction fields and final cursor work without marker characters",body()=="\\frac{a+b}{n}"&&!e->property("researchSnippetActive").toBool());capture("fraction");
    reset("\\fr");summon("\\frac{…}{…}");choose("\\frac{…}{…}");type(e,"\\sq");summon("\\sqrt{…}");choose("\\sqrt{…}");type(e,"x");key(e,Qt::Key_Return,{},Qt::ControlModifier);type(e,"n");check("nested snippet returns to the outer denominator",body()=="\\frac{\\sqrt{x}}{n}");
    reset("\\fra{old}{n}",4);summon("\\frac{…}{…}");choose("\\frac{…}{…}");check("existing argument braces and contents are reused",body()=="\\frac{old}{n}");
    reset("  \\begin{al");summon("align");choose("align");type(e,"x=y");check("begin-name completion creates an indented balanced environment",body()=="  \\begin{align}\n    x=y\n  \\end{align}");capture("environment");e->undo();e->undo(); // typing is a separate undo transaction
    reset("\\begin{al}\nx=y\n\\end{align}",9);check("existing environment closing is reused",summon("align")&&choose("align")&&body()=="\\begin{align}\nx=y\n\\end{align}");e->undo();check("paired environment completion undoes in one step",body()=="\\begin{al}\nx=y\n\\end{align}");
    reset("\\begin{claim}\nx\n\\begin{align}\na=b\n\\end{align}\n\\end{cl");check("end suggestions rank nearest unclosed environment first",summon("claim")&&popup()->model()->index(0,0).data().toString()=="claim");choose("claim");check("end-name completion adds only a missing closing brace",body().endsWith("\\end{claim}"));
    reset("\\cite{Jin2025, Wang");key(e,Qt::Key_Slash,{},Qt::AltModifier);auto *reference=w->findChild<QListView*>("researchReferenceCompletion");check("citation completion coexists with command/parameter completion",waitFor([&]{return reference&&reference->isVisible();}));key(e,Qt::Key_Return,{},Qt::ControlModifier);check("citation confirmation changes only the current comma-delimited key",body()=="\\cite{Jin2025, Wang2024");
    reset("\\fr");summon("\\frac{…}{…}");key(e,Qt::Key_Return,"\n");check("ordinary Enter inserts a newline while suggestions are visible",body()=="\\fr\n");reset("\\fr");summon("\\frac{…}{…}");popup()->setCurrentIndex(popup()->model()->index(row("\\frac{…}{…}"),0));key(e,Qt::Key_Tab,"\t");check("default Tab accepts the selected snippet",body()=="\\frac{}{}"&&!visible()&&e->property("researchSnippetActive").toBool());
    settings.setValue("researchCompletionSnippetTab",false);reset("\\fr");summon("\\frac{…}{…}");choose("\\frac{…}{…}");type(e,"a");key(e,Qt::Key_Tab,"\t");type(e,"b");key(e,Qt::Key_Backtab,{},Qt::ShiftModifier);
    check("default Tab moves between confirmed fields and Shift+Tab selects the previous field",body()=="\\frac{a}{b}"&&e->textCursor().selectedText()=="a");key(e,Qt::Key_Tab,"\t");check("forward field traversal preserves the second field text",e->textCursor().selectedText()=="b");key(e,Qt::Key_Backtab,{},Qt::ControlModifier|Qt::ShiftModifier);check("default Tab preserves the existing Ctrl+Shift+Backtab placeholder binding",e->textCursor().selectedText()=="a");key(e,Qt::Key_Tab,{},Qt::ControlModifier);check("default Tab preserves the existing Ctrl+Tab placeholder binding",e->textCursor().selectedText()=="b");key(e,Qt::Key_Return,{},Qt::ControlModifier);settings.setValue("researchCompletionSnippetTab",false);
    reset("\\fr");summon("\\frac{…}{…}");choose("\\frac{…}{…}");
    auto compositionState=[&](QString stage){diagnostics.append(QJsonObject{{"stage",stage},{"body",body()},{"cursor",e->textCursor().position()},{"selected",e->textCursor().selectedText()},{"snippetActive",e->property("researchSnippetActive").toBool()}});};
    bool monitoringIme=true;
    const auto editTrace=QObject::connect(e->document(),&QTextDocument::contentsChange,w,[&](int position,int removed,int added){if(monitoringIme)diagnostics.append(QJsonObject{{"stage","ime-contents-change"},{"position",position},{"removed",removed},{"added",added},{"body",body()},{"cursor",e->textCursor().position()},{"active",e->property("researchSnippetActive").toBool()}});});
    const auto cursorTrace=QObject::connect(e,&QTextEdit::cursorPositionChanged,w,[&]{if(monitoringIme)compositionState("ime-cursor-change");});
    compositionState("before-preedit");QInputMethodEvent preedit("中",{});QApplication::sendEvent(e,&preedit);compositionState("after-preedit");QInputMethodEvent commit;commit.setCommitString("中文");QApplication::sendEvent(e,&commit);compositionState("after-commit");key(e,Qt::Key_Return,{},Qt::ControlModifier);type(e,"n");compositionState("after-next-field");monitoringIme=false;QObject::disconnect(editTrace);QObject::disconnect(cursorTrace);check("synthetic composition commit remains within its field",body()=="\\frac{中文}{n}");capture("synthetic-ime");
    reset("\\nor");summon("\\norm[2]{…}");choose("\\norm[2]{…}");QInputMethodEvent defaultPreedit("阶",{});QApplication::sendEvent(e,&defaultPreedit);QInputMethodEvent defaultCommit;defaultCommit.setCommitString("阶数");QApplication::sendEvent(e,&defaultCommit);key(e,Qt::Key_Return,{},Qt::ControlModifier);type(e,"A");check("IME replaces a selected optional default and keeps the required field",body()=="\\norm[阶数]{A}");
    reset("\\fr");summon("\\frac{…}{…}");choose("\\frac{…}{…}");QInputMethodEvent word;word.setCommitString("alpha");QApplication::sendEvent(e,&word);QInputMethodEvent replacement;replacement.setCommitString(QString::fromUtf8("β"),-5,5);QApplication::sendEvent(e,&replacement);key(e,Qt::Key_Return,{},Qt::ControlModifier);type(e,"n");check("IME reconversion replaces committed text without shifting the next field",body()==QString::fromUtf8("\\frac{β}{n}"));
    reset("\\fr");summon("\\frac{…}{…}");choose("\\frac{…}{…}");type(e,"\\sq");summon("\\sqrt{…}");choose("\\sqrt{…}");QInputMethodEvent nestedPreedit("中",{});QApplication::sendEvent(e,&nestedPreedit);QInputMethodEvent nestedCommit;nestedCommit.setCommitString("变量");QApplication::sendEvent(e,&nestedCommit);key(e,Qt::Key_Return,{},Qt::ControlModifier);type(e,"n");check("nested IME fields return to the outer denominator",body()=="\\frac{\\sqrt{变量}}{n}");
    reset("\\nor");summon("\\norm{…}");check("changed definitions reject stale candidate insertion",put(defs,(declarations+"% changed\n").toUtf8())&&choose("\\norm{…}")&&body()=="\\nor");put(defs,declarations.toUtf8());
    reset("\\ZZnotinlocalcatalog");key(e,Qt::Key_Slash,{},Qt::AltModifier);key(e,Qt::Key_Escape);settle(600);check("Esc cancels a project query even before any local candidates exist",!visible()&&body()=="\\ZZnotinlocalcatalog");
    {
      QJsonArray extras;
      auto add=[&](QString prefix,QString body){extras.append(QJsonObject{{"prefix",prefix},{"description",prefix},{"body",body}});};
      add("zz","${1:x}$1$0");add("ff","\\frac{${1:a}}{${2:b}}$0");add("vv","${1:x}+$1=${2:y}$0");
      add("ee","\\begin{${1:align}}\n  $2\n\\end{$1}\n$0");add("bb","\\textbf{${1:${TM_SELECTED_TEXT}}}$0");
      settings.setValue("researchLocalSnippetsV1",QJsonDocument(extras).toJson(QJsonDocument::Compact));
      reset("ff");check("plain abbreviation uses the existing candidate list",summon("ff")&&choose("ff")&&body()=="\\frac{a}{b}");type(e,"q");key(e,Qt::Key_Tab,"\t");type(e,"r");check("plain abbreviation parameters retain standard Tab traversal",body()=="\\frac{q}{r}");
      Research::setCommandCompletionMode(2);reset("");QElapsedTimer abbreviationDelay;abbreviationDelay.start();type(e,"ff");settle(190);const bool earlyAbbreviation=visible();const bool automaticAbbreviation=waitFor(visible);check("ordinary abbreviations keep the automatic 300 ms delay",!earlyAbbreviation&&automaticAbbreviation&&abbreviationDelay.elapsed()>=290);Research::setCommandCompletionMode(1);
      reset("zz");summon("zz");choose("zz");type(e,"ab");compositionState("adjacent-mirrors");check("adjacent mirror occurrences preserve the active field cursor",body()=="abab"&&e->textCursor().position()==head.size()+2&&e->property("researchSnippetActive").toBool());
      reset("vv");summon("vv");choose("vv");type(e,"z");check("typing updates every same-number field",body()=="z+z=y");e->undo();check("one undo reverses the edit and all mirrors",body()=="x+x=y");e->redo();check("redo restores the whole mirrored edit",body()=="z+z=y");
      reset("vv");summon("vv");choose("vv");auto secondary=e->textCursor();secondary.setPosition(head.size()+2);secondary.setPosition(head.size()+3,QTextCursor::KeepAnchor);e->setTextCursor(secondary);type(e,"w");check("editing a secondary occurrence updates its primary",body()=="w+w=y");key(e,Qt::Key_Tab,"\t");check("Tab visits the next distinct field, skipping mirrors",e->textCursor().selectedText()=="y");key(e,Qt::Key_Backtab,{},Qt::ShiftModifier);check("Shift Tab returns to the primary occurrence",e->textCursor().selectedText()=="w"&&e->textCursor().selectionStart()==head.size());
      QInputMethodEvent linkedPreedit("中",{});QApplication::sendEvent(e,&linkedPreedit);QInputMethodEvent linkedCommit;linkedCommit.setCommitString("中文");QApplication::sendEvent(e,&linkedCommit);check("IME commit mirrors without taking composition keys",body()=="中文+中文=y");
      reset("vv");summon("vv");choose("vv");type(e,"\\sq");summon("\\sqrt{…}");choose("\\sqrt{…}");type(e,"t");key(e,Qt::Key_Tab,"\t");type(e,"u");check("nested snippets also update an outer mirrored field",body()=="\\sqrt{t}+\\sqrt{t}=u");
      reset("ee");summon("ee");choose("ee");type(e,"equation");key(e,Qt::Key_Tab,"\t");type(e,"x=y");check("environment mirrors and native paired names do not double-process typing",body()=="\\begin{equation}\n  x=y\n\\end{equation}\n");
      reset("ee");summon("ee");choose("ee");type(e,"equ");summon("equation");choose("equation");key(e,Qt::Key_Tab,"\t");type(e,"x=y");check("environment candidate acceptance keeps linked names and the body field",body()=="\\begin{equation}\n  x=y\n\\end{equation}\n");
      reset("α + $1$");auto selection=e->textCursor();selection.setPosition(head.size());selection.setPosition(head.size()+body().size(),QTextCursor::KeepAnchor);e->setTextCursor(selection);
      check("manual suggestions wrap an actual selection",summon("bb")&&choose("bb")&&body()=="\\textbf{α + $1$}");e->undo();check("selected-text wrapping is a single undo",body()=="α + $1$");
      reset("one\ntwo");selection=e->textCursor();selection.setPosition(head.size());selection.setPosition(head.size()+body().size(),QTextCursor::KeepAnchor);e->setTextCursor(selection);key(e,Qt::Key_Tab,"\t");check("multiline selection still indents every selected line",body()=="\tone\n\ttwo");key(e,Qt::Key_Backtab,{},Qt::ShiftModifier);check("multiline Shift Tab restores indentation without changing text",body()=="one\ntwo");
      for(bool commandFirst:{false,true}) {
        auto *handler=w->findChild<QObject*>(commandFirst?"researchCommandCompletionController":"researchProjectWriting");e->removeEventFilter(handler);e->installEventFilter(handler);
        reset("\\fr");summon("\\frac{…}{…}");choose("\\frac{…}{…}");type(e,"\\ref{eq:");key(e,Qt::Key_Slash,{},Qt::AltModifier);
        auto *refs=w->findChild<QListView*>("researchReferenceCompletion");const bool shown=waitFor([&]{return refs&&refs->isVisible();});
        QKeyEvent reserve(QEvent::ShortcutOverride,Qt::Key_Tab,Qt::NoModifier,"\t");reserve.setAccepted(false);QApplication::sendEvent(e,&reserve);
        key(e,Qt::Key_Tab,"\t");check(QString("reference acceptance wins over snippet navigation in filter order %1").arg(commandFirst),shown&&reserve.isAccepted()&&body().contains("\\ref{eq:bound")&&!body().contains('\t')&&e->property("researchSnippetActive").toBool());
        const auto accepted=body();const auto position=e->textCursor().position();key(e,Qt::Key_Tab,"\t");check("the next Tab traverses the field without another acceptance",body()==accepted&&e->textCursor().position()>position);
      }
      reset("\tone");key(e,Qt::Key_Backtab,{},Qt::ShiftModifier);check("ordinary Shift Tab outdents the current line",body()=="one");
      reset(" one");key(e,Qt::Key_Backtab,{},Qt::ShiftModifier);check("Shift Tab also outdents space-indented lines",body()=="one");
      reset("vv");summon("vv");choose("vv");Research::setCommandCompletionMode(0);const auto off=body();key(e,Qt::Key_Tab,"\t");key(e,Qt::Key_Slash,{},Qt::AltModifier);check("off disables candidates and active snippet navigation together",!visible()&&!e->property("researchSnippetActive").toBool()&&body()!=off);
      reset("ff");key(e,Qt::Key_Slash,{},Qt::AltModifier);key(e,Qt::Key_Tab,"\t");check("off leaves an ordinary abbreviation as text and indentation",body()=="ff\t"&&!visible());
      reset("readonly");auto locked=e->textCursor();locked.setPosition(head.size());locked.setPosition(head.size()+8,QTextCursor::KeepAnchor);e->setTextCursor(locked);e->setReadOnly(true);const auto readOnlyText=e->toPlainText();key(e,Qt::Key_Tab,"\t");key(e,Qt::Key_Backtab,{},Qt::ShiftModifier);check("readonly selection never changes through Tab indentation",e->toPlainText()==readOnlyText);e->setReadOnly(false);
      Research::setCommandCompletionMode(1);settings.remove("researchLocalSnippetsV1");
    }
    auto *config=Research::configureLocalCompletion(w);settle(50);auto *list=config->findChild<QListWidget*>("completionSnippetList");auto *prefix=config->findChild<QLineEdit*>("completionSnippetPrefix");auto *description=config->findChild<QLineEdit*>("completionSnippetDescription");auto *content=config->findChild<QPlainTextEdit*>("completionSnippetBody");
    capture("custom-config");if(qEnvironmentVariableIsSet("TEXLINE_COMPLETION_ANIMATE"))settle(500);config->findChild<QPushButton*>("completionSnippetAdd")->click();
    check("visual snippet editor opens an editable detail",list&&prefix&&prefix->isEnabled());prefix->setText("\\myproof");description->setText("Proof scaffold");content->setPlainText("\\begin{proof}\nArgument.\n\\end{proof}\n");auto field=content->textCursor();field.setPosition(content->toPlainText().indexOf("Argument."));field.setPosition(field.position()+9,QTextCursor::KeepAnchor);content->setTextCursor(field);config->findChild<QPushButton*>("shorthandMakeField")->click();if(qEnvironmentVariableIsSet("TEXLINE_COMPLETION_ANIMATE"))settle(700);auto *buttons=config->findChild<QDialogButtonBox*>();buttons->button(QDialogButtonBox::Save)->click();settle();
    check("visual configuration persists a validated snippet",Research::userCompletions().size()==1&&Research::userCompletions().first().prefix=="\\myproof");reset("\\myp");summon("\\myproof");choose("\\myproof");put(folder+"/custom-snippet-state.json",QJsonDocument(QJsonObject{{"source",body()},{"selection",e->textCursor().selectedText()},{"template",Research::userCompletions().first().body},{"context",Research::userCompletions().first().context}}).toJson());check("custom multiline snippet selects its first default",body()=="\\begin{proof}\nArgument.\n\\end{proof}\n"&&e->textCursor().selectedText()=="Argument.");capture("custom-snippet");
    config=Research::configureLocalCompletion(w);config->findChild<QTabBar *>("shorthandCollection")->setCurrentIndex(1);{auto *items=config->findChild<QListWidget *>("completionSnippetList");for(int n=0;n<items->count();++n)if(items->item(n)->text().contains("\\myproof"))QMetaObject::invokeMethod(items,"itemActivated",Qt::DirectConnection,Q_ARG(QListWidgetItem *,items->item(n)));}config->findChild<QLineEdit*>("completionSnippetPrefix")->setText("\\cancelled");config->reject();settle();check("cancel leaves persisted custom snippets unchanged",Research::userCompletions().first().prefix=="\\myproof");
    for(bool focus:{false,true}){Research::setFocusMode(focus);settle(220);w->resize(qMax(400,w->screen()->availableGeometry().width()/2),qMin(760,w->screen()->availableGeometry().height()-40));reset("\\nor");summon("\\norm{…}");const auto cursor=e->cursorRect();const QRect line(e->viewport()->mapToGlobal(cursor.topLeft()),e->viewport()->mapToGlobal(cursor.bottomRight()));const QRect bounds(w->mapToGlobal(QPoint()),w->size());check(focus?"focus half-screen popup stays inside writing window and avoids active line":"normal half-screen popup stays inside writing window and avoids active line",visible()&&bounds.contains(popup()->geometry())&&!popup()->geometry().intersects(line));capture(focus?"half-focus":"half-normal");}
    Research::setFocusMode(false);
    const QString sections="\\section{Model}\nA symmetric matrix.\n\\section{Results}\n\\nor";reset(sections);
    const bool articleReady=waitFor([&]{return w->property("researchArticleReady").toBool()&&w->property("researchArticleMode").toBool()&&w->findChild<QToolButton*>("researchArticleSection");});
    if(articleReady){auto *section=w->findChild<QToolButton*>("researchArticleSection");section->click();check("section view retains project macros from hidden preamble",e->property("researchSourceSlice").toBool()&&summon("\\norm{…}")&&choose("\\norm{…}")&&e->toPlainText()==head+sections.left(sections.size()-4)+"\\norm{}"+tail);e->undo();check("section-view insertion undo preserves the entire paper",e->toPlainText()==head+sections+tail);w->findChild<QToolButton*>("researchArticleFull")->click();}else check("article precondition for section completion",false);
    const QString notePath=folder+"/short-note.tex";put(notePath,"\\documentclass{article}\n\\begin{document}\nShort note.\n\\end{document}\n");auto *note=new TeXDocumentWindow(notePath);note->show();note->activateWindow();auto *ne=note->editor();ne->setFocus();ne->setPlainText("Short note. \\fr");auto nc=ne->textCursor();nc.movePosition(QTextCursor::End);ne->setTextCursor(nc);key(ne,Qt::Key_Slash,{},Qt::AltModifier);auto *np=note->findChild<QListView*>("researchCommandCompletion");check("short note gets compact completion without article management",waitFor([&]{np=note->findChild<QListView*>("researchCommandCompletion");return np&&np->isVisible();})&&!note->property("researchArticleMode").toBool()&&np->height()<=174);note->setModified(false);note->close();settle();w->activateWindow();e->setFocus();
    auto *draft=new TeXDocumentWindow;draft->show();draft->activateWindow();auto *de=draft->editor();de->setPlainText("\\newcommand{\\shortmacro}[1]{#1}\n\\shortm");auto dc=de->textCursor();dc.movePosition(QTextCursor::End);de->setTextCursor(dc);de->setFocus();check("untitled editor has focus before its first shortcut",waitFor([&]{return de->hasFocus()&&draft->isActiveWindow();}));key(de,Qt::Key_Slash,{},Qt::AltModifier);QListView *dp=nullptr;int dr=-1;
    const bool draftReady=waitFor([&]{dp=draft->findChild<QListView*>("researchCommandCompletion");if(!dp||!dp->isVisible())return false;for(int n=0;n<dp->model()->rowCount();++n)if(dp->model()->index(n,0).data().toString()=="\\shortmacro{…}"){dr=n;return true;}return false;});
    diagnostics.append(QJsonObject{{"stage","untitled-query"},{"focused",de->hasFocus()},{"activeWindow",draft->isActiveWindow()},{"root",Research::completionProjectRoot(draft)},{"popupVisible",dp&&dp->isVisible()},{"rows",dp?dp->model()->rowCount():0},{"text",de->toPlainText()}});
    if(draftReady){dp->setCurrentIndex(dp->model()->index(dr,0));check("untitled macros show a useful source without an internal filename",dp->model()->index(dr,0).data(Qt::UserRole+2).toString().startsWith("Untitled:"));key(de,Qt::Key_Return,{},Qt::ControlModifier);check("unsaved short note can accept its own macro signature",de->toPlainText().endsWith("\\shortmacro{}"));}else check("untitled macro index becomes available",false);
    auto *otherDraft=new TeXDocumentWindow;check("independent unsaved windows have distinct completion identities",Research::completionProjectRoot(draft)!=Research::completionProjectRoot(otherDraft));otherDraft->setModified(false);otherDraft->close();draft->setModified(false);draft->close();settle();w->activateWindow();e->setFocus();
    reset(QString("% long paper\n").repeated(25000)+"\\nor");
    for(int n=0;n<10;++n){key(e,Qt::Key_Escape);QElapsedTimer t;t.start();const bool opened=summon("\\norm{…}");timings.append(QJsonObject{{"query",n},{"readyMs",double(t.nsecsElapsed())/1e6},{"opened",opened}});check(QString("long-paper query %1 resolves cross-file macro").arg(n),opened);}
    QFile originalFile(main);originalFile.open(QIODevice::ReadOnly);check("original independent manuscript remains byte-identical",originalFile.readAll()==original);recorder->stop();w->setModified(false);w->close();settle();save();app.exit(failures?1:0);
  });return app.exec();
}
