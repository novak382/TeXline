#include "ActualAuthoringRecording.h"
#include "ResearchFind.h"
#include "ResearchSlides.h"
#include "ResearchWritingContext.h"
using namespace Research;
int main(int argc,char **argv){
 const auto report=argument(1);if(!qEnvironmentVariable("TEXLINE_TEST_DESKTOP").startsWith("TeXlineValidation-"))return 2;
 argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
 QTimer::singleShot(80,&app,[&]{
  const auto folder=QFileInfo(report).absolutePath();const bool baseline=qEnvironmentVariableIsSet("TEXLINE_ENTRY_BASELINE");QJsonArray checks;int failures=0;
  auto check=[&](QString n,bool ok){checks.append(QJsonObject{{"name",n},{"passed",ok}});failures+=!ok;};
  const QString text="\\documentclass{article}\n\\newtheorem{theorem}{Theorem}\n\\begin{document}\n\\section{Main result 主结果}\n\\begin{theorem}\\label{thm:main}The estimation error is bounded. 估计误差有界。\\end{theorem}\n\\begin{proof}Apply \\ref{thm:main}. First control the remainder. 首先控制余项。\\end{proof}\n\\end{document}\n";
  const auto file=folder+"/paper/main.tex";put(file,text.toUtf8());Tw::Settings().setValue("openPDFwithTeX",false);
  auto *w=new TeXDocumentWindow(file);w->resize(qEnvironmentVariableIsSet("TEXLINE_TEST_HALF")?640:1180,760);w->show();settle(600);QApplication::setActiveWindow(w);w->editor()->setFocus();
  auto *editor=w->editor();QTextCursor selected(w->textDoc());selected.setPosition(text.indexOf("The estimation"));selected.setPosition(selected.position()+45,QTextCursor::KeepAnchor);editor->setTextCursor(selected);
  auto captureMenu=[&](const QString &name){auto *m=w->findChild<QMenu*>(name);if(!m)return false;QMetaObject::invokeMethod(m,"aboutToShow");m->popup(w->mapToGlobal(QPoint(100,60)));settle(150);m->grab().save(folder+"/"+name+".png");m->close();return true;};
  check("main menu available",captureMenu("researchMoreMenu")||captureMenu("researchFocusMenu"));
  auto *files=w->findChild<QMenu*>("researchFileMenu"),*readingMenu=w->findChild<QMenu*>("researchReadingMenu");
  auto *places=w->findChild<QAction*>("researchWritingContexts"),*related=w->findChild<QAction*>("researchRelatedAction");
  if(!baseline){check("writing places next to file save",files&&files->actions().contains(places));check("reading promoted from view",readingMenu&&!readingMenu->actions().contains(related));}
  captureMenu("researchFileMenu");
  auto context=[&](bool withSelection){
   if(!withSelection){QTextCursor c=selected;c.clearSelection();editor->setTextCursor(c);}else editor->setTextCursor(selected);
   const auto before=editor->textCursor();const auto point=editor->cursorRect().center();
   QTimer::singleShot(90,w,[&]{auto *m=qobject_cast<QMenu*>(QApplication::activePopupWidget());check("native editor context opened",m);if(!m)return;
    m->grab().save(folder+(withSelection?"/selection-menu.png":"/cursor-menu.png"));
    if(!baseline){check("context offers read here",m->findChild<QAction*>("researchContextRead"));check("context excludes global writing place action",!m->actions().contains(places));check("selection version only with selection",bool(m->findChild<QAction*>("researchContextLocalVersion"))==withSelection);}
    m->close();});
   QContextMenuEvent event(QContextMenuEvent::Mouse,point,editor->viewport()->mapToGlobal(point));QApplication::sendEvent(editor->viewport(),&event);settle();
   check("context cancel retains cursor and selection",editor->textCursor().anchor()==before.anchor()&&editor->textCursor().position()==before.position());
  };context(true);context(false);
  if(!baseline){
   editor->setTextCursor(selected);QMenu contextMenu;addWritingContextActions(&contextMenu,w,text.indexOf("Apply"));
   contextMenu.findChild<QAction*>("researchContextRead")->trigger();settle(300);QPointer<QDialog> reader=w->findChild<QDialog*>("researchRelatedReading");
   auto *content=reader?reader->findChild<QTextBrowser*>("researchRelatedContents"):nullptr;
   check("right click reads clicked proof without moving selection",content&&waitFor([&]{return content->toPlainText().contains("First control");})&&editor->textCursor().anchor()==selected.anchor()&&editor->textCursor().position()==selected.position());
   if(reader){key(reader,Qt::Key_Escape);settle();}check("read Escape restores editor focus",editor->hasFocus());
   QTimer::singleShot(180,w,[&]{auto *dialog=w->findChild<QDialog*>("researchLocalVersionsDialog");check("selection context opens existing local version dialog",dialog&&dialog->isVisible());if(dialog)dialog->reject();});
   contextMenu.findChild<QAction*>("researchContextLocalVersion")->trigger();settle();check("version cancel preserves reverse selection",editor->textCursor()==selected);
   QTimer::singleShot(180,w,[&]{auto *dialog=w->findChild<QDialog*>("researchWritingContextsDialog");check("context opens existing places dialog",dialog&&dialog->isVisible());if(dialog)dialog->reject();});places->trigger();settle();check("place cancel preserves writing selection",editor->textCursor()==selected);
  }
  const auto deck=folder+"/slides/main.tex";const QString slides="\\documentclass{beamer}\n\\begin{document}\n\\section{Results}\n\\begin{frame}{Main result 主结果}\nneedle alpha\\end{frame}\n\\begin{frame}{Proof 证明}\nneedle beta\\end{frame}\n\\end{document}\n";put(deck,slides.toUtf8());
  auto *s=new TeXDocumentWindow(deck);s->resize(w->size());s->show();settle(600);check("beamer mode ready",waitFor([&]{auto *l=s->findChild<QListWidget*>("researchSlideList");return l&&l->count()==2;}));
  auto *dock=s->findChild<QDockWidget*>("researchSlideDock");auto *list=s->findChild<QListWidget*>("researchSlideList");auto *outline=s->findChild<QAction*>("researchOutlineAction");
  if(dock&&list){s->grab().save(folder+"/slides.png");dock->hide();settle();
   if(!baseline){outline->trigger();settle(300);check("existing outline entry reopens slides",!dock->isHidden());outline->trigger();settle(300);check("outline reversibly closes slides",dock->isHidden());outline->trigger();settle(300);}
   else dock->show();
   if(auto *button=s->findChild<QToolButton*>("researchSlideSearchButton"))button->click();
   auto *filter=s->findChild<QLineEdit*>("researchSlideFilter");if(filter){filter->setText("Proof");settle();check("list search filters titles",list->item(0)->isHidden()&&!list->item(1)->isHidden());s->grab().save(folder+"/slide-filter.png");key(filter,Qt::Key_Escape);settle();check("filter Escape clears query",filter->text().isEmpty());}
   list->setCurrentRow(0);settle(150);
  }
  showFind(s);settle();auto *query=s->findChild<QLineEdit*>("researchFindField");auto *scope=s->findChild<QComboBox*>("researchFindScope");
  if(query){query->setText("needle");settle(200);s->grab().save(folder+"/source-find.png");
   if(!baseline){check("source search is bounded",query->width()<=360);check("scope visible without options",scope&&scope->isVisible());check("placeholder explicitly says source",query->placeholderText().contains(englishUi()?"source":"源码"));check("typed query still has explicit source scope",scope&&scope->currentText().contains(englishUi()?"source":"源码"));check("scope text fits at effective scale",scope&&scope->width()>=scope->fontMetrics().horizontalAdvance(scope->currentText())+25);}
   findNext(s);check("source search selects text",s->textCursor().selectedText()=="needle");key(query,Qt::Key_Escape);settle();check("search Escape returns editor focus",s->editor()->hasFocus());
  }
  setFocusMode(true);settle(350);captureMenu("researchFocusMenu");check("focus remains operational",w->property("researchFocus").toBool());setFocusMode(false);settle(350);
  put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"baseline",baseline},{"devicePixelRatio",qApp->primaryScreen()->devicePixelRatio()}}).toJson());
  s->textDoc()->setModified(false);w->textDoc()->setModified(false);s->close();w->close();settle();app.exit(failures?1:0);
 });return app.exec();
}
