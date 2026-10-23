#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "TeXDocumentWindow.h"
#include "CompletingEdit.h"
#include "ResearchAppearance.h"
#include "Settings.h"
#include <QElapsedTimer>
#include <QInputMethodEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QListView>
#include <QSaveFile>
#include <QScreen>
#include <QThread>
#include <QTimer>
#include <QAction>
#include <QRegularExpression>
#include <QSet>
#include <functional>
#include <windows.h>
#include <QAbstractNativeEventFilter>
static void settle(int ms=0){QElapsedTimer t;t.start();do{QCoreApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);if(ms)QThread::msleep(2);}while(t.elapsed()<ms);}
static bool waitFor(const std::function<bool()> &f,int ms=1000){QElapsedTimer t;t.start();while(!f()&&t.elapsed()<ms)settle(3);return f();}
static bool put(QString path,QByteArray data){QSaveFile f(path);return f.open(QIODevice::WriteOnly)&&f.write(data)==data.size()&&f.commit();}
static void key(QWidget *w,int code,QString text={},Qt::KeyboardModifiers mods=Qt::NoModifier){QKeyEvent a(QEvent::KeyPress,code,mods,text),b(QEvent::KeyRelease,code,mods);QApplication::sendEvent(w,&a);QApplication::sendEvent(w,&b);}
class FixtureInput:public QAbstractNativeEventFilter{public:bool nativeEventFilter(const QByteArray &,void *p,qintptr *r)override{auto *m=static_cast<MSG*>(p);if(!m||!QWidget::find(reinterpret_cast<WId>(m->hwnd)))return false;if((m->message>=WM_KEYFIRST&&m->message<=WM_KEYLAST)||(m->message>=WM_LBUTTONDOWN&&m->message<=WM_MBUTTONDBLCLK)){if(r)*r=0;return true;}return false;}};
int main(int argc,char **argv){QString report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);FixtureInput guard;app.installNativeEventFilter(&guard);
QTimer::singleShot(100,&app,[&]{
 const QString folder=QFileInfo(report).absolutePath(),path=folder+"/catalog.tex";put(path,"\\documentclass{article}\n\\begin{document}\nText\n\\end{document}\n");
 Tw::Settings settings;settings.remove("researchCommandCompletionUsage");settings.setValue("researchCommandCompletionMode",1);settings.setValue("researchFocusMode",false);settings.setValue("openPDFwithTeX",false);settings.setValue("launchOption",0);
 auto *w=new TeXDocumentWindow(path);w->showNormal();w->resize(900,720);w->raise();w->activateWindow();auto *e=w->editor();e->setFocus();settle(160);
 QJsonArray checks,commands,snippets,contexts,familyCases,habits,timing;int failures=0;auto save=[&]{put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"commands",commands},{"snippets",snippets},{"contexts",contexts},{"families",familyCases},{"habits",habits},{"popupMs",timing}}).toJson());};auto check=[&](QString name,bool pass){checks.append(QJsonObject{{"name",name},{"passed",pass}});failures+=!pass;save();};
 auto popup=[&]{return w->findChild<QListView*>("researchCommandCompletion");};auto visible=[&]{return popup()&&popup()->isVisible();};
 auto row=[&](QString label){if(!popup())return -1;for(int i=0;i<popup()->model()->rowCount();++i)if(popup()->model()->index(i,0).data().toString()==label)return i;return -1;};
 auto reset=[&](QString text,int pos=-1){key(e,Qt::Key_Escape);e->setPlainText(text);auto c=e->textCursor();c.setPosition(pos<0?int(text.size()):pos);e->setTextCursor(c);e->document()->clearUndoRedoStacks();e->setFocus();settle();};
 auto summon=[&]{QElapsedTimer t;t.start();key(e,Qt::Key_Slash,{},Qt::AltModifier);timing.append(double(t.nsecsElapsed())/1e6);};
 auto choose=[&](int n){if(n>=0&&popup()){popup()->setCurrentIndex(popup()->model()->index(n,0));key(e,Qt::Key_Return,{},Qt::ControlModifier);settle();}};
 QSet<QString> names,raw,offeredCaptions,coveredCaptions;const QRegularExpression canonical(R"(^\\[A-Za-z@]+)");
 for(const auto &v:CompletingEdit::localCompletionEntries()){auto m=canonical.match(v.second);if(m.hasMatch()){names.insert(m.captured());if(v.second.contains("#INS#"))raw.insert(v.second);}}
 // Required standard mathematical vocabulary, independently specified rather
 // than inferred from an abbreviation catalog that may omit real commands.
 for(const auto &name:QString("alpha beta gamma delta epsilon varepsilon theta vartheta lambda pi varpi rho varrho sigma varsigma phi varphi psi omega Gamma Delta Theta Lambda Xi Pi Sigma Phi Psi Omega sum prod int oint iint iiint lim liminf limsup inf sup partial nabla in notin subset subseteq supset supseteq approx equiv sim simeq cong propto perp parallel forall exists nexists emptyset varnothing pm mp times div cdot cap cup bigcap bigcup oplus otimes aleph ell hbar Re Im sin cos tan cot sec csc sinh cosh tanh arcsin arccos arctan exp log ln det dim max min gcd Pr frac dfrac tfrac binom dbinom tbinom sqrt mathbb mathcal mathfrak mathsf mathtt mathrm mathit mathnormal mathbf boldsymbol operatorname overline underline overbrace underbrace overset underset displaystyle textstyle scriptstyle scriptscriptstyle ldots cdots vdots ddots lVert rVert langle rangle rightarrow leftarrow Longrightarrow implies iff le leq ge geq neq infty").split(' '))names.insert("\\"+name);
 // Black-box inventory of the actual offered model, including vocabulary
 // not present in the abbreviation files. No private provider hook is used.
 const QRegularExpression bare(R"(^\\[A-Za-z@]+$)");
 for(QChar letter:QString("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ@")){reset(QString("\\")+letter);summon();if(visible())for(int i=0;i<popup()->model()->rowCount();++i){QString caption=popup()->model()->index(i,0).data().toString();if(bare.match(caption).hasMatch())names.insert(caption);else offeredCaptions.insert(caption);}}
 QStringList ordered=names.values();ordered.sort();int commandBad=0;QElapsedTimer total;total.start();
 for(const auto &name:ordered){const QString prefix=name.size()>2?name.left(name.size()-1):name,source="x+"+prefix+" suffix";reset(source,2+prefix.size());summon();int n=row(name);const bool present=visible()&&n>=0;choose(n);const bool inserted=e->toPlainText()=="x+"+name+" suffix",caret=e->textCursor().position()==2+name.size();e->undo();const bool undo=e->toPlainText()==source;e->redo();const bool redo=e->toPlainText()=="x+"+name+" suffix";bool pass=present&&inserted&&caret&&undo&&redo;commandBad+=!pass;commands.append(QJsonObject{{"command",name},{"present",present},{"inserted",inserted},{"caret",caret},{"undo",undo},{"redo",redo}});if(commands.size()%200==0)save();}
 check(QString("all %1 canonical catalog commands preserve suffix, caret, one-step undo and redo; %2 failures").arg(commands.size()).arg(commandBad),commandBad==0);const qint64 commandMs=total.elapsed();
 QStringList templates=raw.values();templates.sort();QSet<QString> normalized;int snippetBad=0;
 for(const auto &expansion:templates){QString expected=expansion;int marker=expected.indexOf("#INS#");QString head=expected.left(marker);head.remove(QChar(0x2022));int caret=head.size();expected.remove("#INS#");expected.remove(QChar(0x2022));if(normalized.contains(expected))continue;normalized.insert(expected);
  QString label=expansion;label.replace("#INS#",QString(QChar(0x2026)));label.replace(QChar(0x2022),QChar(0x2026));label=label.simplified();QString cleaned=expansion;cleaned.remove(QRegularExpression(QString(QChar(0x2022))+"(?=\\s*$)"));cleaned.replace("#INS#",QString(QChar(0x2026)));cleaned.replace(QChar(0x2022),QChar(0x2026));cleaned.replace('\n',QChar(0x21b5));cleaned=cleaned.simplified();coveredCaptions.insert(cleaned);const QString prefix=canonical.match(expansion).captured(),source="x+"+prefix+" suffix";reset(source,2+prefix.size());summon();int n=row(cleaned);if(n<0)n=row(label);const bool present=visible()&&n>=0;choose(n);const QString actual=e->toPlainText();const bool inserted=actual=="x+"+expected+" suffix",placed=e->textCursor().position()==2+caret,clean=!actual.contains("#INS#")&&!actual.contains(QChar(0x2022));e->undo();const bool undo=e->toPlainText()==source;e->redo();const bool redo=e->toPlainText()=="x+"+expected+" suffix";bool pass=present&&inserted&&placed&&clean&&undo&&redo;snippetBad+=!pass;snippets.append(QJsonObject{{"template",expansion},{"expected",expected},{"actual",actual},{"present",present},{"inserted",inserted},{"caret",placed},{"clean",clean},{"undo",undo},{"redo",redo}});if(!pass&&snippetBad<=3){w->grab().save(folder+QString("/snippet-failure-%1.png").arg(snippetBad));}if(snippets.size()%25==0)save();
 }
 check(QString("all %1 distinct snippets insert clean TeX and support interior caret, one-step undo and redo; %2 failures").arg(snippets.size()).arg(snippetBad),snippetBad==0);
 // Explicit user-facing examples do not derive expectations from catalog implementation.
 for(const auto &item:QList<QPair<QString,QString>>{{"\\frac{…}{…}","\\frac{}{}"},{"\\sqrt{…}","\\sqrt{}"},{"\\left(…\\right)","\\left(\\right)"},{"\\left[…\\right]","\\left[\\right]"},{"\\textbf{…}","\\textbf{}"}}){QString prefix=canonical.match(item.second).captured();reset(prefix);summon();choose(row(item.first));check("explicit snippet contract: "+item.first,e->toPlainText()==item.second&&!e->toPlainText().contains(QChar(0x2022)));}
 for(const auto &name:QString("operatorname mathbb mathcal mathfrak mathsf mathtt mathrm mathit mathnormal mathbf boldsymbol text textbf emph overline underline sqrt frac dfrac tfrac binom dbinom tbinom overset underset").split(' ')){
   const bool two=QStringList{"frac","dfrac","tfrac","binom","dbinom","tbinom","overset","underset"}.contains(name);const QString command="\\"+name,caption=command+"{…}"+(two?"{…}":""),expected=command+"{}"+(two?"{}":""),prefix=command.left(command.size()-1),before=QStringLiteral("中文 😀 ")+prefix+" suffix";coveredCaptions.insert(caption);reset(before,before.size()-7);summon();choose(row(caption));bool inserted=e->toPlainText()==QStringLiteral("中文 😀 ")+expected+" suffix"&&e->textCursor().position()==QStringLiteral("中文 😀 ").size()+command.size()+1;e->undo();bool undo=e->toPlainText()==before;e->redo();check("standard argument snippet preserves Unicode, suffix, interior caret and atomic undo/redo: "+command,inserted&&undo&&e->toPlainText()==QStringLiteral("中文 😀 ")+expected+" suffix");
 }
 for(const auto &caption:QStringList{"\\left(…\\right)","\\left[…\\right]","\\right)","\\right]"})coveredCaptions.insert(caption);
 // New local templates have a readable signature and a fixed paired-body
 // caption. Independently translate that public caption into the promised TeX,
 // then require exact suffix preservation and atomic Undo/Redo for every item.
 QStringList extraTemplates=(offeredCaptions-coveredCaptions).values();extraTemplates.sort();
 for(const auto &caption:extraTemplates){
   QString expected=caption;const bool environment=expected.contains(" ↵ … \\end{");
   if(environment)expected.replace(" ↵ … \\end{","\n  \n\\end{");
   expected.replace(QChar(0x2026),QString());const QString prefix=canonical.match(caption).captured();
   if(prefix.isEmpty()||(!caption.contains(QChar(0x2026))&&!environment))continue;
   const QString before="x+"+prefix+" suffix";reset(before,2+prefix.size());summon();int n=row(caption);const bool offered=visible()&&n>=0;choose(n);
   const bool insertion=e->toPlainText()=="x+"+expected+" suffix";e->undo();const bool undo=e->toPlainText()==before;e->redo();
   check("additional argument/environment template matches its public signature and atomic undo/redo: "+caption,offered&&insertion&&undo&&e->toPlainText()=="x+"+expected+" suffix");coveredCaptions.insert(caption);
 }
 QStringList untested=(offeredCaptions-coveredCaptions).values();untested.sort();check("every offered non-canonical candidate is covered by a snippet or delimiter insertion test",untested.isEmpty());put(folder+"/catalog-model-inventory.json",QJsonDocument(QJsonObject{{"commands",names.size()},{"offeredSnippetCaptions",offeredCaptions.size()},{"untestedCaptions",QJsonArray::fromStringList(untested)}}).toJson());
 // Common argument snippets must be reachable in the first six rows, even
 // when thousands of unrelated commands share the same prefix.
 for(const auto &item:QList<QPair<QString,QString>>{{"\\fr","\\frac{…}{…}"},{"\\sq","\\sqrt{…}"},{"\\textb","\\textbf{…}"},{"\\oper","\\operatorname{…}"}}){reset(item.first);summon();check("common snippet is among the first six: "+item.second,row(item.second)>=0&&row(item.second)<6);}
 // Context matrix covers every protected environment, inline variant and escape parity.
 settings.setValue("researchCommandCompletionMode",2);
 QList<QPair<QString,bool>> samples{{"plain English",false},{QStringLiteral("普通中文"),false},{"% \\fr",false},{"\\% \\fr",true},{"\\\\% \\fr",false},{"\\\\\\% \\fr",true},{"\\\\fr",false},{"\\\\\\fr",true},{"\\verb|\\fr",false},{"\\verb*|\\fr",false},{"\\verb|raw| \\fr",true},{"\\lstinline[language=TeX]|\\fr",false},{"\\lstinline{\\fr",false},{"\\lstinline{raw} \\fr",true},{"\\mintinline[breaklines]{tex}|\\fr",false},{"\\mintinline{tex}{\\fr",false},{"\\mintinline{tex}{raw} \\fr",true}};
 for(const auto &env:QStringList{"verbatim","verbatim*","Verbatim","BVerbatim","LVerbatim","lstlisting","minted","comment","filecontents","filecontents*"}){samples.append({"\\begin{"+env+"}\n\\fr",false});samples.append({"\\begin{"+env+"}\nraw\n\\end{"+env+"}\n\\fr",true});}
 bool contextOk=true;for(const auto &sample:samples){reset(sample.first.left(sample.first.size()-1));key(e,int(sample.first.back().toUpper().unicode()),sample.first.right(1));settle(360);bool pass=visible()==sample.second;contextOk&=pass;contexts.append(QJsonObject{{"text",sample.first},{"expectedPopup",sample.second},{"actualPopup",visible()},{"passed",pass}});}check("automatic suppression and escape parity across the full literal-context matrix",contextOk);
 QStringList families{"\\al","\\be","\\ga","\\de","\\vare","\\si","\\om","\\sum","\\prod","\\int","\\oint","\\lim","\\ma","\\text","\\sec","\\sub","\\incl","\\beg","\\end","\\frac","\\sq","\\over","\\under","\\left","\\ri","\\cite","\\eqr","\\newc","\\def"};
 for(bool focus:{false,true})for(const auto &lang:QStringList{"zh_CN","en_US"}){Research::setFocusMode(focus);app.applyTranslation(lang);settle(160);bool familyOk=true;for(const auto &prefix:families){reset("");for(QChar c:prefix)key(e,c=='\\'?int(Qt::Key_Backslash):int(c.toUpper().unicode()),QString(c));bool opened=waitFor(visible,800),newline=false;QStringList labels;if(opened){for(int i=0;i<qMin(6,popup()->model()->rowCount());++i)labels.append(popup()->model()->index(i,0).data().toString());auto old=e->toPlainText();key(e,Qt::Key_Return,"\n");newline=e->toPlainText()==old+"\n"&&!visible();}else key(e,Qt::Key_Escape);familyOk&=opened&&newline;familyCases.append(QJsonObject{{"prefix",prefix},{"focus",focus},{"locale",lang},{"opened",opened},{"newline",newline},{"firstSix",QJsonArray::fromStringList(labels)}});}check(QString("%1 command families in %2 %3: auto trigger, ordinary newline does not accept").arg(families.size()).arg(focus?"focus":"normal",lang),familyOk);}
 settings.setValue("researchCommandCompletionMode",1);
 bool learned=true;for(const auto &pair:QList<QPair<QString,QString>>{{"\\al","\\alpha"},{"\\be","\\beta"},{"\\ga","\\gamma"},{"\\de","\\delta"},{"\\va","\\varepsilon"},{"\\si","\\sigma"},{"\\om","\\omega"},{"\\ma","\\mathcal"},{"\\ma","\\mathbb"},{"\\fr","\\frac"},{"\\sq","\\sqrt"},{"\\in","\\infty"},{"\\in","\\int"},{"\\lo","\\log"},{"\\te","\\textbf"},{"\\se","\\section"},{"\\su","\\sum"},{"\\su","\\subseteq"},{"\\ov","\\overline"},{"\\un","\\underbrace"}}){reset(pair.first);summon();int selected=row(pair.second);choose(selected);bool inserted=e->toPlainText()==pair.second;reset(pair.first);summon();QString first=visible()?popup()->model()->index(0,0).data().toString():QString();bool pass=selected>=0&&inserted&&first==pair.second;learned&=pass;habits.append(QJsonObject{{"prefix",pair.first},{"choice",pair.second},{"inserted",inserted},{"firstAfterLearning",first},{"passed",pass}});}check("20 choices across Greek, operators, fonts, structure and snippets learn independently",learned);
 reset("\\fr");summon();if(popup())popup()->grab().save(folder+"/catalog-frac.png");key(e,Qt::Key_Escape);
 reset("\\begin");summon();if(popup())popup()->grab().save(folder+"/catalog-environments.png");key(e,Qt::Key_Escape);
 settings.setValue("researchCommandCompletionMode",1);reset(QString("% long document\n").repeated(25000)+"\\frac");QJsonArray longMs;bool accurate=true;for(int i=0;i<30;++i){key(e,Qt::Key_Escape);QElapsedTimer t;t.start();key(e,Qt::Key_Slash,{},Qt::AltModifier);longMs.append(double(t.nsecsElapsed())/1e6);accurate&=visible()&&row("\\frac")>=0;}check("30 repeated full-catalog queries in a 25000-line file remain correct",accurate);
 put(folder+"/catalog-performance.json",QJsonDocument(QJsonObject{{"commands",commands.size()},{"snippets",snippets.size()},{"commandSweepMs",double(commandMs)},{"allPopupMs",timing},{"longFilePopupMs",longMs},{"workingSetNotComparableDuringBuild",true}}).toJson());
 QFile original(path);original.open(QIODevice::ReadOnly);const bool untouched=original.readAll()=="\\documentclass{article}\n\\begin{document}\nText\n\\end{document}\n";original.close();check("on-disk source bytes remain unchanged",untouched);
 w->setModified(false);w->close();settle(60);save();app.exit(failures?1:0);
});return app.exec();}
