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
#include <QContextMenuEvent>
#include <QMenu>
#include <QMessageBox>
#include <QAbstractButton>
#include <QPointer>
#include <QLabel>
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
 QTimer::singleShot(50,&app,[&]{QJsonArray checks;int failures=0;auto check=[&](const QString &name,bool good){checks.append(QJsonObject{{"name",name},{"passed",good}});failures+=!good;QFile f(report);if(f.open(QIODevice::WriteOnly))f.write(QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures}}).toJson());};
 if(!qEnvironmentVariable("TEXLINE_TEST_DESKTOP").startsWith("TeXlineValidation-")){check("requires private desktop",false);app.exit(1);return;}
 const auto folder=QFileInfo(report).absolutePath(),root=folder+"/controls.tex";const QString source="\\documentclass{article}\n\\usepackage{amsthm}\n\\newtheorem{claim}{Claim}\n\\begin{document}\n\\section{First}\n\\begin{claim}\\label{thm:one}\nA statement to read.\n\\end{claim}\n\\section{Second}\nWriting here.\n\\end{document}\n";write(root,source);Tw::Settings().setValue("openPDFwithTeX",false);
 auto *draft=new TeXDocumentWindow;draft->show();draft->editor()->insertPlainText(source);check("unsaved manuscript stays in pure writing until a real file exists",waitFor([&]{return draft->property("researchArticleReady").toBool();})&&!draft->property("researchArticleMode").toBool()&&!draft->findChild<QWidget*>("researchArticleScope"));draft->grab().save(folder+"/unsaved-manuscript.png");Research::pinSourceReference(draft,"thm:one");check("unsaved reference command creates no invalid file-based pin",!draft->findChild<QDockWidget*>("researchPinnedReference"));draft->setModified(false);delete draft;
 auto *w=new TeXDocumentWindow(root);const auto work=w->screen()->availableGeometry();w->resize(work.width(),qMin(850,work.height()-20));w->show();check("paper ready",waitFor([&]{return w->property("researchArticleReady").toBool();}));auto *tree=w->findChild<QTreeWidget*>("researchArticleTree");auto *outline=w->findChild<QAction*>("researchOutlineAction");if(!tree||!outline){app.exit(2);return;}
 if(!outline->isChecked())outline->trigger();w->findChild<QToolButton*>("researchArticleObjects")->click();auto *entry=find(tree,"claim · thm:one");check("object available through visible list control",entry);if(!entry){app.exit(2);return;}entry->parent()->setExpanded(true);tree->scrollToItem(entry);settle(70);
 bool menuSeen=false,actionChosen=false;QTimer popup;popup.setInterval(15);QObject::connect(&popup,&QTimer::timeout,&app,[&]{auto *m=qobject_cast<QMenu*>(QApplication::activePopupWidget());if(!m)return;menuSeen=true;m->grab().save(folder+"/object-context-menu.png");const auto actions=m->actions();for(auto *a:actions)if(a->text()==(Research::englishUi()?"Pin below":"固定到下方")){actionChosen=true;a->trigger();break;}m->close();});popup.start();QTimer::singleShot(1500,&app,[]{if(auto *m=qobject_cast<QMenu*>(QApplication::activePopupWidget()))m->close();});
 const auto pos=tree->visualItemRect(entry).center();QContextMenuEvent context(QContextMenuEvent::Mouse,pos,tree->viewport()->mapToGlobal(pos));QApplication::sendEvent(tree->viewport(),&context);popup.stop();check("real object context menu exposes and executes pin",menuSeen&&actionChosen);
 auto *pin=w->findChild<QDockWidget*>("researchPinnedReference");check("context command pins the clicked theorem",pin&&waitFor([&]{return pin->property("researchPinnedReady").toBool();})&&w->findChild<QTextBrowser*>("researchPinnedSource")&&w->findChild<QTextBrowser*>("researchPinnedSource")->toPlainText().contains("A statement to read."));if(!pin){app.exit(2);return;}
 auto *go=w->findChild<QToolButton*>("researchPinnedGoToSource"),*close=w->findChild<QToolButton*>("researchPinnedClose");check("pin header controls have accessible names",go&&close&&(!go->accessibleName().isEmpty()||!go->text().isEmpty())&&!close->accessibleName().isEmpty());
 QTextCursor cursor(w->textDoc());cursor.setPosition(source.indexOf("Writing here"));w->editor()->setTextCursor(cursor);go->click();check("actual source button locates the pinned environment",waitFor([&]{return w->editor()->textCursor().block().text().contains("\\begin{claim}");}));
 close->click();check("actual close button returns focus to unobstructed writing",pin->isHidden()&&w->editor()->isVisible());cursor=w->textCursor();cursor.setPosition(source.indexOf("Writing here"));cursor.insertText("Edited ");settle(650);check("editing cannot reopen a closed pin",pin->isHidden());w->editor()->undo();
 Research::pinSourceReference(w,"thm:one");check("closed pin can reopen with the current source",waitFor([&]{return pin->isVisible()&&pin->property("researchPinnedReady").toBool();}));
 auto *referenceText=w->findChild<QTextBrowser*>("researchPinnedSource");w->editor()->zoomIn(2);QCoreApplication::processEvents();check("reference source follows live editor zoom",referenceText&&qAbs(referenceText->fontMetrics().height()-w->editor()->fontMetrics().height())<=1);w->editor()->zoomOut(2);
 cursor=w->textCursor();cursor.setPosition(source.indexOf("Writing here"));cursor.beginEditBlock();cursor.insertText("Z");cursor.endEditBlock();QPointer<QWidget> loading=w->findChild<QWidget*>("researchPinnedLoading");cursor.beginEditBlock();cursor.insertText("Y");cursor.endEditBlock();check("consecutive edits reuse the loading view and disable old navigation",loading&&loading==w->findChild<QWidget*>("researchPinnedLoading")&&!go->isEnabled());w->editor()->undo();w->editor()->undo();waitFor([&]{return pin->property("researchPinnedReady").toBool();});
 Tw::Settings().setValue("researchReduceMotion",true);Research::setFocusMode(true);check("reduced-motion focus avoids the snapshot overlay",[&]{auto *veil=w->findChild<QWidget*>("researchChromeVeil");return !veil||!veil->isVisible();}());check("focus hides pinned reference and article scope",pin->isHidden()&&!w->findChild<QWidget*>("researchArticleScope")->isVisible());Research::setFocusMode(false);Tw::Settings().setValue("researchReduceMotion",false);check("leaving focus restores requested reference",waitFor([&]{return pin->isVisible()&&pin->property("researchPinnedReady").toBool();}));
 cursor=w->textCursor();cursor.setPosition(source.indexOf("\\section{Second}"));cursor.insertText("\\begin{claim}\\label{thm:one}Duplicate statement.\\end{claim}\n");check("duplicate labels never enable a guessed source destination",waitFor([&]{return pin->property("researchPinnedReady").toBool();})&&!go->isEnabled()&&!pin->property("researchPinnedVisual").toBool());w->editor()->undo();settle(650);close->click();
 if(!outline->isChecked())outline->trigger();w->findChild<QToolButton*>("researchArticleClose")->click();check("actual directory close preserves writing and synchronizes toggle",!outline->isChecked()&&w->editor()->isVisible());outline->trigger();w->findChild<QToolButton*>("researchArticleSearchToggle")->click();auto *search=w->findChild<QLineEdit*>("researchArticleSearch");search->setText("Second");QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(search,&enter);check("Enter in structure search navigates to its result",w->cursorPosition()>=source.indexOf("\\section{Second}")&&w->cursorPosition()<source.indexOf("\\end{document}"));
 w->grab().save(folder+"/control-states.png");
 const auto mainFile=folder+"/project.tex",childFile=folder+"/child.tex";const QString childSource="\\section{Child}\nChild text.\n";write(mainFile,"\\documentclass{article}\n\\begin{document}\n\\section{Opening}\nBody\n\\input{child}\n\\section{Ending}\nBody\n\\end{document}\n");write(childFile,childSource);auto *m=new TeXDocumentWindow(mainFile);m->show();waitFor([&]{return m->property("researchArticleReady").toBool();});auto *mt=m->findChild<QTreeWidget*>("researchArticleTree");check("multi-file status does not repeat the same filename",m->findChild<QLabel*>("researchArticleLocation")->text()=="project.tex");QPointer<TeXDocumentWindow> childWindow=new TeXDocumentWindow(childFile);childWindow->show();QTextCursor cc(childWindow->textDoc());cc.insertText("\\section{Unsaved child}\n");check("parent sees the unsaved child version",waitFor([&]{return m->property("researchArticleReady").toBool()&&find(mt,"Unsaved child");}));
 bool ignored=false;QTimer saveDialog;saveDialog.setInterval(10);QObject::connect(&saveDialog,&QTimer::timeout,&app,[&]{auto *box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());if(!box)return;if(auto *ignore=box->button(QMessageBox::Discard)){ignored=ignore->text()==(Research::englishUi()?"Ignore":"忽略");ignore->click();}else box->reject();});saveDialog.start();childWindow->close();saveDialog.stop();check("actual child close uses the Ignore choice",ignored);check("ignoring child edits restores parent directory and leaves disk intact",waitFor([&]{QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);return m->property("researchArticleReady").toBool()&&!find(mt,"Unsaved child");})&&read(childFile)==childSource);m->setModified(false);
check("control operations leave disk source untouched",read(root)==source);w->setModified(false);app.exit(failures?1:0);
 });QTimer::singleShot(60000,&app,[&]{app.exit(92);});return app.exec();}
