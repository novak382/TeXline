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
#include <QScrollArea>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextBrowser>
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
int main(int argc,char **argv){const QString report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
 QTimer::singleShot(50,&app,[&]{QJsonArray checks,geometry;int failures=0;auto check=[&](const QString &name,bool passed){checks.append(QJsonObject{{"name",name},{"passed",passed}});failures+=!passed;QFile f(report);if(f.open(QIODevice::WriteOnly))f.write(QJsonDocument(QJsonObject{{"checks",checks},{"geometry",geometry},{"failures",failures}}).toJson());};
 if(!qEnvironmentVariable("TEXLINE_TEST_DESKTOP").startsWith("TeXlineValidation-")){check("requires inactive private desktop",false);app.exit(1);return;}
 const auto folder=QFileInfo(report).absolutePath(),root=folder+"/论文.tex";
 const QString original="\\documentclass{article}\n\\usepackage{amsthm}\n\\newtheorem{claim}{Claim}\n\\begin{document}\n% \\section{Fake comment}\n\\newcommand{\\fake}{\\section{Fake macro}}\n\\section{Introduction}\nIntro text.\n\\subsection{Motivation}\nMotivation text.\n\\section{Results}\n\\begin{claim}\\label{thm:main}\nOriginal statement.\n\\end{claim}\n\\begin{proof}[Proof of Claim~\\ref{thm:main}]\nProof text.\n\\end{proof}\n\\section{Discussion}\nFinal text.\n\\end{document}\n% Tail\n";
 check("writes own Unicode-path article",write(root,original));
 const auto parsed=Research::parseArticleDocument(root);check("automatic article classification",parsed.article&&!parsed.beamer&&!parsed.uncertain);check("comments and macro-generated headings ignored",parsed.sections.size()==4&&parsed.sections[1].title=="Motivation");check("hierarchy belongs to parent section",parsed.sections[1].parent==0&&parsed.sections[2].parent==-1);check("custom theorem and proof indexed",parsed.objects.size()==2&&parsed.objects[0].key=="thm:main");
 const auto note=folder+"/draft.tex";const QString noteText="\\documentclass{article}\n\\begin{document}\n\\section{Working note}\nText\n\\subsection{Idea}\nMore text\n\\end{document}\n";write(note,noteText);check("one-section draft stays simple despite article class",!Research::parseArticleDocument(note).article);
 const auto dynamic=Research::parseArticleDocument(root,{{root,QString(original).replace("Intro text.","\\ifdefined\\unknown Intro text.\\fi")}});check("uncertain conditional source never becomes an editable slice",dynamic.uncertain&&std::none_of(dynamic.sections.begin(),dynamic.sections.end(),[](const auto &s){return s.editable;}));
 const auto cancelled=Research::parseArticleDocument(root,{},[]{return true;});check("scan cancellation is observed",cancelled.cancelled);
 const auto main=folder+"/main.tex",child=folder+"/results.tex",fragment=folder+"/body.tex";
 const QString mainText="\\documentclass{article}\n\\usepackage{amsthm}\n\\newtheorem{claim}{Claim}\n\\begin{document}\n\\section{Opening}\nStart\n\\input{results}\n\\section{Ending}\nEnd\n\\end{document}\n";
 const QString childText="\\section{Child results}\n\\begin{claim}\\label{thm:child}Child statement.\\end{claim}\n\\begin{proof}[Proof of Claim~\\ref{thm:child}]Child proof.\\end{proof}\n";
 write(main,mainText);write(child,childText);write(fragment,"Continuation text.\n");
 const auto multi=Research::parseArticleDocument(main);check("multi-file outline follows include order",multi.article&&multi.sections.size()==3&&multi.sections[1].file.endsWith("results.tex")&&multi.sections[2].title=="Ending");check("whole child section has safe local range",multi.sections.size()==3&&multi.sections[1].editable);
 const auto spanning=Research::parseArticleDocument(main,{{main,QString(mainText).replace("\\input{results}","\\input{body}")}});check("cross-file section is navigable but not silently truncated",spanning.article&&!spanning.sections.first().editable);
 const auto repeated=Research::parseArticleDocument(main,{{main,QString(mainText).replace("\\input{results}","\\input{results}\n\\input{results}")}});check("repeated input disables uncertain slice edits",repeated.uncertain);
 const auto beamer=Research::parseArticleDocument(root,{{root,QString(original).replace("{article}","{beamer}")}});check("Beamer never activates article management",beamer.beamer&&!beamer.article);
 Tw::Settings().setValue("openPDFwithTeX",false);auto *w=new TeXDocumentWindow(root);const auto work=w->screen()->availableGeometry();w->resize(qMin(1400,work.width()),qMin(850,work.height()-20));w->show();
 auto ready=[&](TeXDocumentWindow *v){return waitFor([&]{return v->property("researchArticleReady").toBool();});};
 check("real source window classifies automatically",ready(w)&&w->property("researchArticleMode").toBool());
 auto *tree=w->findChild<QTreeWidget*>("researchArticleTree");auto *full=w->findChild<QToolButton*>("researchArticleFull");auto *section=w->findChild<QToolButton*>("researchArticleSection");auto *outline=w->findChild<QAction*>("researchOutlineAction");
 if(!tree||!full||!section||!outline){check("article controls exist",false);app.exit(1);return;}
 auto *toolbar=w->findChild<QToolBar*>("toolBar_run");auto *scopeWidget=w->findChild<QWidget*>("researchArticleScope");
 check("source scope shares the existing toolbar without a third row",toolbar&&toolbar->isAncestorOf(scopeWidget)&&w->editor()->mapTo(w,QPoint()).y()<=toolbar->mapTo(w,QPoint()).y()+toolbar->height()+2);
 check("opens in continuous full source",visible(w).contains("Tail")&&!w->editor()->property("researchSourceSlice").toBool());
 auto *preamble=find(tree,Research::englishUi()?"Preamble":"导言");check("preamble has a direct source entry",preamble!=nullptr);if(preamble)tree->itemClicked(preamble,0);check("preamble navigation shows complete current source",w->cursorPosition()==0&&!w->editor()->property("researchSourceSlice").toBool());
 auto select=[&](const QString &title){auto *item=find(tree,title);check("finds chapter "+title,item!=nullptr);if(item)tree->itemClicked(item,0);QCoreApplication::processEvents();};
 select("Results");section->click();check("explicit section hides only surrounding source",w->editor()->property("researchSourceSlice").toBool()&&visible(w).contains("Original statement")&&!visible(w).contains("Intro text")&&!visible(w).contains("Tail"));
 check("view changes preserve full buffer and disk",w->textDoc()->editorText()==original&&read(root)==original&&!w->isModified());
 key(w->editor(),Qt::Key_A,Qt::ControlModifier);check("select-all respects visible section",w->textCursor().selectedText().contains("Original statement")&&!w->textCursor().selectedText().contains("Final text"));
 key(w->editor(),Qt::Key_Home,Qt::ControlModifier);const int begin=w->cursorPosition();key(w->editor(),Qt::Key_Backspace);check("backspace cannot erase hidden previous section",w->cursorPosition()==begin&&w->textDoc()->editorText()==original);
 key(w->editor(),Qt::Key_End,Qt::ControlModifier);const int end=w->cursorPosition();key(w->editor(),Qt::Key_Delete);check("delete cannot erase hidden next section",w->cursorPosition()==end&&w->textDoc()->editorText()==original);
 auto c=w->textCursor();c.setPosition(original.indexOf("Original statement"));w->editor()->setTextCursor(c);w->editor()->insertPlainText("Edited ");check("typing changes actual original buffer",ready(w)&&w->textDoc()->editorText().contains("Edited Original statement")&&w->editor()->property("researchSourceSlice").toBool());w->editor()->undo();check("one undo restores exact article",ready(w)&&w->textDoc()->editorText()==original);w->editor()->redo();check("redo preserves hidden source",ready(w)&&w->textDoc()->editorText()==QString(original).replace("Original statement","Edited Original statement"));check("saving a section writes full document with hidden preamble and tail",w->save()&&read(root).replace("\r\n","\n")==QString(original).replace("Original statement","Edited Original statement"));w->editor()->undo();ready(w);check("undo and save restore the original fixture",w->save()&&read(root).replace("\r\n","\n")==original);ready(w);
 const int caret=w->cursorPosition();full->click();check("return to full preserves caret",w->cursorPosition()==caret&&visible(w).contains("Tail"));
 select("Introduction");section->click();check("parent section includes its subsections",visible(w).contains("Motivation text")&&!visible(w).contains("Original statement"));
 c=w->textCursor();c.setPosition(original.indexOf("Intro text."));w->editor()->setTextCursor(c);w->editor()->insertPlainText("\\ifdefined\\unknown\\fi ");check("new uncertain boundaries reveal the complete source",ready(w)&&!w->editor()->property("researchSourceSlice").toBool()&&visible(w).contains("Tail"));w->editor()->undo();ready(w);section->click();
 c=w->textCursor();c.setPosition(original.indexOf("Motivation text."));w->editor()->setTextCursor(c);w->editor()->insertPlainText("\\section{New boundary}\n");check("new headings leave a stale slice before hiding any changed range",ready(w)&&!w->editor()->property("researchSourceSlice").toBool()&&visible(w).contains("Tail"));w->editor()->undo();ready(w);select("Introduction");section->click();
 Research::showFind(w,false);auto *scope=w->findChild<QComboBox*>("researchFindScope");check("find defaults to explicit section range",scope&&scope->currentData()=="slide"&&scope->currentText()==(Research::englishUi()?"Section source":"本节源码"));if(auto *bar=w->findChild<QWidget*>("researchFindBar"))bar->hide();
 c=w->textCursor();c.setPosition(original.indexOf("Final text"));w->editor()->setTextCursor(c);check("external navigation expands hidden destination",!w->editor()->property("researchSourceSlice").toBool()&&visible(w).contains("Final text"));
 select("Results");section->click();Research::setFocusMode(true);check("focus retains the current section",w->editor()->property("researchSourceSlice").toBool()&&w->findChild<QWidget*>("researchArticleScope")->isHidden());Research::setFocusMode(false);check("exit focus restores section choice",w->editor()->property("researchSourceSlice").toBool());
 full->click();w->resize(720,qMin(760,work.height()-20));settle(100);auto *dock=w->findChild<QDockWidget*>("researchArticleDock");if(outline->isChecked())outline->trigger();outline->trigger();settle(100);check("narrow structure leaves at least half the window available for writing",w->editor()->isVisible()&&tree->isVisible()&&w->editor()->viewport()->width()>=w->width()/2);select("Discussion");check("selecting section restores full-width writing",waitFor([&]{return w->editor()->isVisible()&&w->editor()->width()>=w->centralWidget()->width()-20&&dock->isHidden();}));
 w->resize(work.width()/2,work.height()-20);if(!outline->isChecked())outline->trigger();settle(150);check("actual half-screen structure never replaces the editor",w->editor()->isVisible()&&tree->isVisible()&&w->editor()->viewport()->width()>=w->width()/2&&w->findChild<QWidget*>("researchArticleScope")->isVisible());auto *filter=w->findChild<QToolButton*>("researchArticleSearchToggle");check("directory filtering is distinct from source search",filter&&filter->icon().isNull()&&filter->text()==(Research::englishUi()?"Filter":"筛选"));w->grab().save(folder+"/article-half-open.png");w->resizeDocks({dock},{w->width()},Qt::Horizontal);settle(100);check("manual structure resizing cannot consume the writing half",dock->width()<=w->width()/3&&w->editor()->viewport()->width()>=w->width()/2);outline->trigger();
 geometry.append(QJsonObject{{"devicePixelRatio",w->devicePixelRatioF()},{"sourceWidth",w->editor()->width()},{"windowWidth",w->width()}});w->grab().save(folder+"/article-narrow.png");
 auto *n=new TeXDocumentWindow(note);n->resize(720,qMin(700,work.height()-20));n->show();check("short note has no article management controls",ready(n)&&!n->property("researchArticleMode").toBool()&&!n->findChild<QWidget*>("researchArticleScope"));n->grab().save(folder+"/short-note.png");
 w->resize(qMin(1400,work.width()),qMin(850,work.height()-20));if(dock->isHidden())outline->trigger();select("Results");section->click();w->grab().save(folder+"/article-section.png");
 c=w->textCursor();c.setPosition(original.indexOf("Original statement"));w->editor()->setTextCursor(c);Research::pinSourceReference(w,"thm:main");auto *lookup=w->findChild<QDockWidget*>("researchPinnedReference");check("pin opens read-only source without a compiled PDF",lookup&&waitFor([&]{return lookup->property("researchPinnedReady").toBool();})&&!lookup->property("researchPinnedVisual").toBool());check("pinned theorem is the requested current statement",w->findChild<QTextBrowser*>("researchPinnedSource")&&w->findChild<QTextBrowser*>("researchPinnedSource")->toPlainText().contains("Original statement"));
 w->editor()->insertPlainText("Updated ");check("typing invalidates old pinned rendering immediately",!lookup->property("researchPinnedReady").toBool());check("pinned source refreshes after edits",waitFor([&]{auto *text=w->findChild<QTextBrowser*>("researchPinnedSource");return lookup->property("researchPinnedReady").toBool()&&text&&text->toPlainText().contains("Updated Original statement");}));w->editor()->undo();ready(w);lookup->hide();
 c=w->textCursor();c.setPosition(original.indexOf("Proof text."));w->editor()->setTextCursor(c);Research::pinSourceReference(w);check("unlabelled proof can be pinned directly",waitFor([&]{auto *text=w->findChild<QTextBrowser*>("researchPinnedSource");return lookup->property("researchPinnedReady").toBool()&&text&&text->toPlainText().contains("Proof text.")&&!text->toPlainText().contains("Original statement.");}));
 full->click();c=w->textCursor();c.setPosition(0);w->editor()->setTextCursor(c);w->editor()->insertPlainText(QString(300,QLatin1Char(' ')));check("unlabelled proof pin follows source edits before its position",ready(w)&&waitFor([&]{auto *text=w->findChild<QTextBrowser*>("researchPinnedSource");return lookup->property("researchPinnedReady").toBool()&&text&&text->toPlainText().contains("Proof text.")&&!text->toPlainText().contains("Original statement.");}));w->editor()->undo();ready(w);lookup->hide();select("Results");section->click();
 if(qEnvironmentVariableIsSet("TEXLINE_ARTICLE_COMPILE")){
   Research::pinSourceReference(w,"thm:main");waitFor([&]{return lookup->property("researchPinnedReady").toBool();});
   for(int pass=0;pass<2;++pass){w->typeset();check("actual article compile "+QString::number(pass),waitFor([&]{return !w->isTypesetting()&&!w->property("researchBuildPreparing").toBool()&&!w->property("researchBuildFinishing").toBool()&&!w->property("researchPublishing").toBool();},45000));settle(350);}
   check("whole document build is certified from a local slice",!Research::previewFile(root).isEmpty()&&Research::pdfPairValid(Research::previewFile(root))&&read(root)==original);
   check("successful compile automatically refreshes an existing source pin",waitFor([&]{return lookup->property("researchPinnedReady").toBool()&&lookup->property("researchPinnedVisual").toBool();}));
   Research::pinSourceReference(w,"thm:main");check("pinned theorem uses matching compiled excerpt",waitFor([&]{return lookup->property("researchPinnedReady").toBool();})&&lookup->property("researchPinnedVisual").toBool());
   auto pdf=QtPDF::Backend::Document::newDocument(Research::previewFile(root));const int page=lookup->property("researchPinnedPage").toInt();auto p=pdf&&page>=0?pdf->page(page).toStrongRef():QSharedPointer<QtPDF::Backend::Page>();const auto crop=lookup->property("researchPinnedCrop").toRectF();const auto excerpt=p?p->selectedText({QPolygonF(crop)}):QString();check("theorem crop contains the statement and excludes the adjacent proof",excerpt.contains("Original statement")&&!excerpt.contains("Proof text"));w->grab().save(folder+"/compiled-theorem.png");w->resize(720,qMin(760,work.height()-20));settle(150);auto *pinArea=w->findChild<QScrollArea*>("researchPinnedContent");check("compiled vector pin fits a resized writing window without horizontal scrolling",pinArea&&pinArea->horizontalScrollBar()->maximum()==0&&w->editor()->isVisible());w->resize(qMin(1400,work.width()),qMin(850,work.height()-20));settle(150);
   QFile goodPdf(Research::previewFile(root));goodPdf.open(QIODevice::ReadOnly);const auto goodBytes=goodPdf.readAll();goodPdf.close();
   c=w->textCursor();c.setPosition(w->textDoc()->editorText().indexOf("Original statement"));w->editor()->setTextCursor(c);w->editor()->insertPlainText("\\UnknownArticleAuditCommand ");w->typeset();check("section compile failure finishes and retains the previous PDF",waitFor([&]{return !w->isTypesetting()&&!w->property("researchBuildPreparing").toBool()&&!w->property("researchBuildFinishing").toBool();},45000)&&[&]{QFile f(Research::previewFile(root));return f.open(QIODevice::ReadOnly)&&f.readAll()==goodBytes;}());
   check("failed compile retains explicitly marked previous theorem without certifying current source",!Research::pdfPairValid(Research::previewFile(root))&&waitFor([&]{return lookup->property("researchPinnedReady").toBool();})&&lookup->property("researchPinnedVisual").toBool()&&lookup->property("researchPinnedPrevious").toBool());
   w->editor()->undo();ready(w);w->typeset();check("undo and recompile restore the current article and theorem preview",waitFor([&]{return !w->isTypesetting()&&!w->property("researchBuildPreparing").toBool()&&!w->property("researchBuildFinishing").toBool();},45000)&&Research::pdfPairValid(Research::previewFile(root))&&waitFor([&]{return lookup->property("researchPinnedReady").toBool()&&lookup->property("researchPinnedVisual").toBool();}));lookup->hide();
 }
 auto *m=new TeXDocumentWindow(main);m->resize(qMin(1400,work.width()),qMin(850,work.height()-20));m->show();check("multi-file main automatically recognized",ready(m)&&m->property("researchArticleMode").toBool());auto *mt=m->findChild<QTreeWidget*>("researchArticleTree");auto *childItem=mt?find(mt,"Child results"):nullptr;check("child chapter appears in article directory",childItem!=nullptr);if(childItem)mt->itemClicked(childItem,0);
 auto *cw=TeXDocumentWindow::findDocument(child);check("child window inherits article root and directory",cw&&ready(cw)&&cw->textDoc()->getRootFilePath()==QFileInfo(main).canonicalFilePath()&&cw->findChild<QTreeWidget*>("researchArticleTree")->topLevelItemCount()==4);
 if(cw){check("multi-file full scope is honestly labelled",cw->findChild<QToolButton*>("researchArticleFull")->text()==(Research::englishUi()?"File":"本文件"));cw->findChild<QToolButton*>("researchArticleSection")->click();check("child section edits retain own source document",cw->editor()->property("researchSourceSlice").toBool()&&cw->textDoc()->editorText()==childText&&read(main)==mainText);cw->grab().save(folder+"/multi-file.png");}
 check("source navigation never rewrites manuscripts",read(root)==original&&read(main)==mainText&&read(child)==childText&&read(note)==noteText);
 for(auto *doc:TeXDocumentWindow::documentList())doc->setModified(false);app.exit(failures?1:0);
 });QTimer::singleShot(180000,&app,[&]{app.exit(92);});return app.exec();}
