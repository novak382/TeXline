#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "ResearchArticle.h"
#include "ResearchAppearance.h"
#include "ResearchBuild.h"
#include "ResearchFind.h"
#include "ResearchReferences.h"
#include "PDFBackend.h"
#include "TWApp.h"
#include "TeXDocumentWindow.h"
#include "CompletingEdit.h"
#include "Settings.h"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLineEdit>
#include <QScreen>
#include <QTextBlock>
#include <QTextBrowser>
#include <QLabel>
#include <QScrollBar>
#include <QMouseEvent>
#include <QStackedWidget>
#include <QFileSystemWatcher>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QToolBar>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <algorithm>
#include <functional>

static bool waitFor(const std::function<bool()> &f,int ms=15000){QElapsedTimer t;t.start();while(!f()&&t.elapsed()<ms){QCoreApplication::processEvents();QThread::msleep(3);}return f();}
static void settle(int ms=650){QElapsedTimer t;t.start();while(t.elapsed()<ms){QCoreApplication::processEvents();QThread::msleep(3);}}
static QString read(const QString &p){QFile f(p);return f.open(QIODevice::ReadOnly)?QString::fromUtf8(f.readAll()).replace("\r\n","\n"):QString();}
static bool write(const QString &p,const QString &s){QFile f(p);return f.open(QIODevice::WriteOnly)&&f.write(s.toUtf8())==s.toUtf8().size();}
static QString visible(TeXDocumentWindow *w){QString s;for(auto b=w->textDoc()->begin();b.isValid();b=b.next())if(b.isVisible())s+=b.text()+"\n";return s;}
static QTreeWidgetItem *find(QTreeWidget *tree,const QString &text){QTreeWidgetItemIterator it(tree);while(*it){if((*it)->text(0)==text)return *it;++it;}return nullptr;}
static void key(CompletingEdit *editor,int code,Qt::KeyboardModifiers modifiers=Qt::NoModifier,const QString &text={}){QKeyEvent override(QEvent::ShortcutOverride,code,modifiers,text);QApplication::sendEvent(editor,&override);QKeyEvent event(QEvent::KeyPress,code,modifiers,text);QApplication::sendEvent(editor,&event);}
static void sendKey(QWidget *widget,int code){QKeyEvent e(QEvent::KeyPress,code,Qt::NoModifier);QApplication::sendEvent(widget,&e);}
static void clickItem(QTreeWidget *tree,QTreeWidgetItem *item){if(!item)return;tree->scrollToItem(item);QCoreApplication::processEvents();const QPointF pos=tree->visualItemRect(item).center();QMouseEvent press(QEvent::MouseButtonPress,pos,pos,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier),release(QEvent::MouseButtonRelease,pos,pos,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(tree->viewport(),&press);QApplication::sendEvent(tree->viewport(),&release);}
int main(int argc,char **argv){const QString report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
 QTimer::singleShot(50,&app,[&]{QJsonArray checks;int failures=0;auto check=[&](const QString &name,bool passed){checks.append(QJsonObject{{"name",name},{"passed",passed}});failures+=!passed;QFile f(report);if(f.open(QIODevice::WriteOnly))f.write(QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"physicalInput",false},{"realIme",false}}).toJson());};
 if(!qEnvironmentVariable("TEXLINE_TEST_DESKTOP").startsWith("TeXlineValidation-")){check("requires inactive private desktop",false);app.exit(1);return;}
 const auto folder=QFileInfo(report).absolutePath(),root=folder+"/研究论文.tex";
 const QString text="\\documentclass{article}\n\\usepackage{amsthm}\n\\newtheorem{claim}{Claim}\n\\begin{document}\n\\section{Introduction}\nOpening paragraph.\n\\subsection{Motivation}\nIdea.\n\\section{Results}\n\\begin{claim}\\label{thm:one}\nFirst claim.\n\\end{claim}\n\\begin{proof}\nFirst proof.\n\\end{proof}\n\\begin{claim}\\label{thm:two}\nSecond claim.\n\\end{claim}\nLast result line.\n\\section{Discussion}\nClosing paragraph.\n\\end{document}\n";
 write(root,text);const auto book=folder+"/book.tex";write(book,"\\documentclass{book}\n\\begin{document}\n\\part{Research}\n\\chapter{First}\nBody\n\\chapter{Second}\nBody\n\\end{document}\n");
 check("one part with several chapters is a structured manuscript",Research::parseArticleDocument(book).article);
 const auto main=folder+"/main.tex",child=folder+"/fragment.tex";
 const QString mainText="\\documentclass{article}\n\\usepackage{amsthm}\n\\newtheorem{claim}{Claim}\n\\begin{document}\n\\section{Opening}\nIntroduction.\n\\input{fragment}\n\\section{Ending}\nConclusion.\n\\end{document}\n";
 const QString childText="\\begin{claim}\\label{thm:fragment}Fragment claim.\\end{claim}\n";write(main,mainText);write(child,childText);
 Tw::Settings().setValue("openPDFwithTeX",false);
 auto ready=[&](TeXDocumentWindow *v){return waitFor([&]{return v->property("researchArticleReady").toBool();});};
 auto *w=new TeXDocumentWindow(root);const auto work=w->screen()->availableGeometry();w->resize(work.width(),qMin(850,work.height()-20));w->show();check("automatically detects an ordinary research paper",ready(w)&&w->property("researchArticleMode").toBool());
 auto *tree=w->findChild<QTreeWidget*>("researchArticleTree");auto *full=w->findChild<QToolButton*>("researchArticleFull"),*section=w->findChild<QToolButton*>("researchArticleSection"),*objects=w->findChild<QToolButton*>("researchArticleObjects");auto *outline=w->findChild<QAction*>("researchOutlineAction");
 if(!tree||!full||!section||!objects||!outline){check("controls available",false);app.exit(1);return;}
 auto open=[&]{if(!outline->isChecked())outline->trigger();settle(40);};
 auto pick=[&](const QString &name){open();clickItem(tree,find(tree,name));settle(40);};
 pick("Results");section->click();check("actual tree click enters the selected section",w->editor()->property("researchSourceSlice").toBool()&&visible(w).contains("First claim")&&!visible(w).contains("Closing paragraph"));
 for(int operation:{-1,1,2,3,4}){
   pick("Results");section->click();
   const int at=w->textDoc()->editorText().indexOf("First claim.");QTextCursor c(w->textDoc());c.setPosition(at);w->editor()->setTextCursor(c);const auto before=w->textDoc()->editorText();w->editor()->editLines(operation);ready(w);
   const auto after=w->textDoc()->editorText();write(folder+"/operation-"+QString::number(operation)+"-after.txt",visible(w));check("line operation preserves hidden source "+QString::number(operation),after.startsWith(before.left(before.indexOf("\\section{Results}")))&&after.endsWith(before.mid(before.indexOf("\\section{Discussion}"))));
   w->editor()->undo();ready(w);check("line operation has one exact undo "+QString::number(operation),w->textDoc()->editorText()==before);w->editor()->redo();ready(w);check("line operation redo restores exact edit "+QString::number(operation),w->textDoc()->editorText()==after);w->editor()->undo();ready(w);write(folder+"/operation-"+QString::number(operation)+"-undo.txt",visible(w));check("in-body undo keeps section view "+QString::number(operation),w->editor()->property("researchSourceSlice").toBool());
 }
 key(w->editor(),Qt::Key_Home,Qt::ControlModifier);w->editor()->editLines(-1);check("move-up cannot cross a hidden section boundary",w->textDoc()->editorText()==text);
 key(w->editor(),Qt::Key_End,Qt::ControlModifier);w->editor()->editLines(1);check("move-down cannot cross a hidden section boundary",w->textDoc()->editorText()==text);
 full->click();Research::showFind(w,false);w->findChild<QToolButton*>("researchFindClose")->click();pick("Results");section->click();Research::showFind(w,true);check("entering a section resets an old file-wide find scope",w->findChild<QComboBox*>("researchFindScope")->currentData()=="slide");w->findChild<QLineEdit*>("researchFindField")->setText("paragraph");w->findChild<QLineEdit*>("researchReplaceField")->setText("CHANGED");settle(180);w->findChild<QToolButton*>("researchReplaceAll")->click();write(folder+"/replace-debug.txt",w->textDoc()->editorText()+"\nVISIBLE:\n"+visible(w)+"\nSCOPE:"+w->findChild<QComboBox*>("researchFindScope")->currentData().toString());check("section replace-all never touches hidden paragraphs",w->textDoc()->editorText()==text);w->findChild<QToolButton*>("researchFindClose")->click();
 auto c=w->textCursor();c.setPosition(text.indexOf("First claim."));w->editor()->setTextCursor(c);Research::pinSourceReference(w,"thm:one");auto *pin=w->findChild<QDockWidget*>("researchPinnedReference");auto pinnedText=[&]{auto *t=w->findChild<QTextBrowser*>("researchPinnedSource");return t?t->toPlainText():QString();};auto pinReady=[&]{return pin&&pin->property("researchPinnedReady").toBool();};
 check("first theorem pin resolves",waitFor(pinReady)&&pinnedText().contains("First claim."));
 auto *go=w->findChild<QToolButton*>("researchPinnedGoToSource");check("resolved pin can locate source",go&&go->isEnabled());
 Research::pinSourceReference(w,"thm:two");check("switching pins clears the previous theorem immediately",!pinnedText().contains("First claim."));check("source navigation disabled while target is unresolved",go&&!go->isEnabled());
 check("second theorem pin resolves",waitFor(pinReady)&&pinnedText().contains("Second claim."));
 Research::pinSourceReference(w,"missing:label");check("missing target produces no misleading compiled/current location",waitFor(pinReady)&&go&&!go->isEnabled());
 Research::pinSourceReference(w,"thm:one");waitFor(pinReady);c=w->textCursor();c.setPosition(text.indexOf("\\begin{claim}"));c.setPosition(text.indexOf("\\end{claim}")+QString("\\end{claim}").size(),QTextCursor::KeepAnchor);w->editor()->setTextCursor(c);c.removeSelectedText();
 check("deleted pinned target clears content and location immediately",!pinReady()&&!go->isEnabled()&&!pinnedText().contains("First claim."));
 check("deleted target stays unavailable after refresh",waitFor(pinReady)&&!go->isEnabled()&&!pinnedText().contains("First claim."));w->editor()->undo();ready(w);waitFor(pinReady);pin->hide();full->click();
 open();auto *intro=find(tree,"Introduction");intro->setExpanded(true);c=w->textCursor();c.setPosition(0);w->editor()->setTextCursor(c);w->editor()->insertPlainText("% new note\n");ready(w);check("expanded branches survive ordinary source offset changes",find(tree,"Introduction")->isExpanded());w->editor()->undo();ready(w);
 intro=find(tree,"Introduction");intro->setExpanded(false);w->findChild<QToolButton*>("researchArticleSearchToggle")->click();auto *search=w->findChild<QLineEdit*>("researchArticleSearch");search->setText("Motivation");check("search reveals matching descendants",!find(tree,"Motivation")->isHidden()&&find(tree,"Introduction")->isExpanded());search->clear();check("clearing search restores prior collapsed branches",!find(tree,"Introduction")->isExpanded());
 search->setText("no such heading");auto *empty=w->findChild<QLabel*>("researchArticleEmpty");check("empty search offers an explicit no-results state",empty&&empty->isVisible());w->grab().save(folder+"/search-empty.png");sendKey(search,Qt::Key_Escape);check("Escape closes and clears structure search",search->isHidden()&&search->text().isEmpty());
 w->resize(720,qMin(740,work.height()-20));settle(100);check("shrinking a docked directory keeps half the window writable",w->editor()->isVisible()&&outline->isChecked()&&w->editor()->viewport()->width()>=w->width()/2);open();sendKey(tree,Qt::Key_Escape);check("Escape returns from narrow navigation to writing",w->editor()->isVisible()&&!outline->isChecked());
 open();intro=find(tree,"Introduction");intro->setExpanded(true);tree->setCurrentItem(find(tree,"Motivation"));sendKey(tree,Qt::Key_Return);check("keyboard activation returns narrow navigation to source",w->editor()->isVisible()&&w->cursorPosition()==w->textDoc()->editorText().indexOf("\\subsection{Motivation}"));
 w->grab().save(folder+"/narrow-writing.png");
 w->resize(work.width(),qMin(850,work.height()-20));settle(100);open();pick("Results");section->click();Research::pinSourceReference(w,"thm:one");waitFor(pinReady);settle(100);check("source occupies at least half of the window width",w->editor()->isVisible()&&w->editor()->width()>=w->width()/2);check("source occupies at least half of window height with reference open",w->editor()->height()>=w->height()/2);w->grab().save(folder+"/article-writing.png");
 pin->hide();auto *m=new TeXDocumentWindow(main);m->resize(work.width(),qMin(850,work.height()-20));m->show();check("multi-file manuscript detects automatically",ready(m)&&m->property("researchArticleMode").toBool());auto *mt=m->findChild<QTreeWidget*>("researchArticleTree");auto *mo=m->findChild<QToolButton*>("researchArticleObjects");mo->click();auto *fragment=find(mt,"claim · thm:fragment");check("theorem in a heading-free included file is listed under its logical section",fragment&&fragment->parent()&&fragment->parent()->text(0)=="Opening");
 auto *cw=new TeXDocumentWindow(child);cw->show();auto cc=cw->textCursor();cc.setPosition(0);cw->editor()->setTextCursor(cc);cw->editor()->insertPlainText("\\section{Unsaved section}\n");check("main outline follows unsaved child edits",ready(m)&&find(mt,"Unsaved section"));cw->setModified(false);delete cw;
 check("discarding and closing child returns outline to disk source",waitFor([&]{return m->property("researchArticleReady").toBool()&&!find(mt,"Unsaved section");},4500));
 
 QFile::remove(child);check("removing an included file makes section bounds conservative",waitFor([&]{return m->property("researchArticleReady").toBool()&&!find(mt,"claim · thm:fragment")&&!m->findChild<QToolButton*>("researchArticleSection")->isEnabled();},4500));write(child,childText);check("recreating an included file refreshes automatically",waitFor([&]{return m->property("researchArticleReady").toBool()&&find(mt,"claim · thm:fragment");},4500));
 if(auto *a=m->findChild<QAction*>("researchOutlineAction");a&&!a->isChecked())a->trigger();m->grab().save(folder+"/multi-file-objects.png");
 
 // Search must discover labelled objects without a separate manual mode choice.
 pick("Results");if(objects->isChecked())objects->click();w->findChild<QToolButton*>("researchArticleSearchToggle")->click();search->setText("thm:two");check("structure search discovers objects even when their listing is off",find(tree,"claim · thm:two")&&!find(tree,"claim · thm:two")->isHidden());sendKey(search,Qt::Key_Escape);
 const bool wasEnglish=Research::englishUi();app.applyTranslation(wasEnglish?"zh_CN":"en_US");check("live language updates structure icon accessibility",w->findChild<QToolButton*>("researchArticleClose")->accessibleName()==(Research::englishUi()?"Hide structure":"收起结构"));app.applyTranslation(wasEnglish?"en_US":"zh_CN");
 const auto note=folder+"/draft.tex";const QString noteText="\\documentclass{article}\n\\begin{document}\n\\section{Note}\nIdea.\n\\end{document}\n";write(note,noteText);auto *n=new TeXDocumentWindow(note);n->show();check("short note never constructs structure controls or file watchers",ready(n)&&!n->findChild<QWidget*>("researchArticleScope")&&!n->findChild<QObject*>("researchArticle")->findChild<QFileSystemWatcher*>());
 auto replace=[&](const QString &value){QTextCursor all(n->textDoc());all.select(QTextCursor::Document);all.insertText(value);ready(n);};replace(text);check("growing note automatically gains paper navigation",n->property("researchArticleMode").toBool());replace(noteText);write(folder+"/note-transition.txt",n->textDoc()->editorText()+"\nROOT:"+n->textDoc()->getRootFilePath()+"\nMODE:"+QString::number(n->property("researchArticleMode").toBool())+"\nEDITOR:"+QString::number(n->editor()->isVisible())+"\nSCOPE:"+QString::number(n->findChild<QWidget*>("researchArticleScope")->isVisible()));n->grab().save(folder+"/note-transition.png");check("shortening paper automatically returns to simple note",!n->property("researchArticleMode").toBool()&&n->editor()->isVisible()&&!n->findChild<QWidget*>("researchArticleScope")->isVisible());n->editor()->undo();ready(n);check("undo restores automatically recognized paper without changing text",n->property("researchArticleMode").toBool()&&n->textDoc()->editorText()==text);
 
 const QString longTitle(400,QLatin1Char('W'));replace(text+"\n% tail");QTextCursor lc(n->textDoc());lc.setPosition(n->textDoc()->editorText().indexOf("Introduction"));lc.setPosition(lc.position()+12,QTextCursor::KeepAnchor);lc.insertText(longTitle);ready(n);auto *nt=n->findChild<QTreeWidget*>("researchArticleTree");n->resize(work.width()/2,qMin(850,work.height()-20));if(!n->findChild<QAction*>("researchOutlineAction")->isChecked())n->findChild<QAction*>("researchOutlineAction")->trigger();settle(100);auto *longItem=find(nt,longTitle);check("long headings stay compact with complete tooltip",longItem&&nt->visualItemRect(longItem).height()<=40&&longItem->toolTip(0).contains(longTitle));check("long headings preserve half-screen writing space",n->editor()->isVisible()&&n->editor()->viewport()->width()>=n->width()/2);n->grab().save(folder+"/long-heading.png");
 
 QString longText="\\documentclass{article}\n\\begin{document}\n";for(int j=0;j<80;++j){longText+="\\section{Section "+QString::number(j)+"}\n";for(int k=0;k<60;++k)longText+="Probability and statistics paragraph "+QString::number(k)+".\n";}longText+="\\end{document}\n";
 QElapsedTimer perf;perf.start();replace(longText);const auto parseMs=perf.elapsed();check("large article reaches a current structure",n->property("researchArticleMode").toBool()&&n->findChild<QTreeWidget*>("researchArticleTree")->topLevelItemCount()==81);
 QTextCursor endCursor(n->textDoc());endCursor.setPosition(longText.indexOf("paragraph")+9);n->editor()->setTextCursor(endCursor);QJsonArray latencies;
 for(int j=0;j<60;++j){perf.restart();key(n->editor(),Qt::Key_X,Qt::NoModifier,"x");QCoreApplication::processEvents();latencies.append(double(perf.nsecsElapsed())/1000000.0);}ready(n);check("continuous typing preserves the complete large source",n->textDoc()->editorText().size()==longText.size()+60&&n->textDoc()->editorText().endsWith("\\end{document}\n"));QFile timings(folder+"/typing-timings.json");if(timings.open(QIODevice::WriteOnly))timings.write(QJsonDocument(QJsonObject{{"initialStructureMs",double(parseMs)},{"lines",longText.count('\n')},{"eventAndProcessMs",latencies},{"physicalInput",false}}).toJson());
 check("all tests leave original files intact",read(root)==text&&read(main)==mainText&&read(child)==childText);
 for(auto *doc:TeXDocumentWindow::documentList())doc->setModified(false);app.exit(failures?1:0);
 });QTimer::singleShot(180000,&app,[&]{app.exit(92);});return app.exec();}
