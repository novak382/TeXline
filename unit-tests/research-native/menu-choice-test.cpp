#include "ActualAuthoringRecording.h"
#include "PrefsDialog.h"
#include "ResearchCommands.h"
using namespace Research;
int main(int argc,char **argv){
 const auto report=argument(1);if(!qEnvironmentVariable("TEXLINE_TEST_DESKTOP").startsWith("TeXlineValidation-"))return 2;
 argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
 QTimer::singleShot(80,&app,[&]{
  const auto folder=QFileInfo(report).absolutePath();const bool baseline=qEnvironmentVariableIsSet("TEXLINE_MENU_BASELINE");QJsonArray checks,menus;int failures=0;
  auto check=[&](QString name,bool ok){checks.append(QJsonObject{{"name",name},{"passed",ok}});failures+=!ok;};
  auto snapshot=[&](QMenu *m,const QString &name){QJsonArray entries;for(auto *a:m->actions())if(a->isVisible()&&!a->isSeparator())entries.append(a->text());menus.append(QJsonObject{{"name",name},{"entries",entries},{"count",entries.size()}});m->grab().save(folder+"/"+name+".png");};
  const QString text="\\documentclass{article}\n\\usepackage{amsmath}\n\\begin{document}\n\\section{Results 结果}\nA selected proof. 证明的一段文字。\n\\[\\begin{pmatrix}a&b\\\\c&d\\end{pmatrix}\\]\n\\cite{sample}\n\\end{document}\n";
  const auto file=folder+"/paper/main.tex";put(file,text.toUtf8());Tw::Settings().setValue("openPDFwithTeX",false);
  auto *w=new TeXDocumentWindow(file);w->resize(qEnvironmentVariableIsSet("TEXLINE_TEST_HALF")?640:1180,760);w->show();settle(600);QApplication::setActiveWindow(w);auto *editor=w->editor();editor->setFocus();
  QTextCursor selection(w->textDoc());selection.setPosition(text.indexOf("A selected"));selection.setPosition(selection.position()+17,QTextCursor::KeepAnchor);editor->setTextCursor(selection);
  auto *edit=w->findChild<QMenu*>("researchEditMenu"),*insert=w->findChild<QMenu*>("researchInsertMenu");
  auto open=[&](QMenu *m,const QString &name){check(name+" exists",m);if(!m)return;m->popup(w->mapToGlobal(QPoint(100,70)));settle(80);snapshot(m,name);m->close();};
  open(edit,"edit-default");open(insert,"insert-default");
  auto *line=w->findChild<QAction*>("researchLineEdit2"),*macro=w->findChild<QAction*>("researchCreateCommand");
  if(!baseline){check("optional line menu entry omitted",line&&!edit->actions().contains(line));check("macro remains visible command",macro&&macro->isVisible()&&!insert->actions().contains(macro));check("hidden entry retains shortcut host",line&&line->isVisible()&&w->actions().contains(line)&&!line->shortcut().isEmpty());}
  auto context=[&](const QString &name,bool extras){
   const auto before=editor->textCursor();const auto point=editor->cursorRect().center();
   QTimer::singleShot(90,w,[&]{auto *m=qobject_cast<QMenu*>(QApplication::activePopupWidget());check(name+" opens",m);if(!m)return;snapshot(m,name);
    if(!baseline){auto *snippet=w->findChild<QAction*>("researchSaveSnippet");check(name+" snippet follows preference",m->actions().contains(snippet)==extras);check(name+" insert follows preference",m->actions().contains(insert->menuAction())==extras);check(name+" selection version retained",m->findChild<QAction*>("researchContextLocalVersion"));}
    key(m,Qt::Key_Escape);});
   QContextMenuEvent event(QContextMenuEvent::Mouse,point,editor->viewport()->mapToGlobal(point));QApplication::sendEvent(editor->viewport(),&event);settle(50);
   check(name+" keeps selection",editor->textCursor()==before);
  };context("context-default",false);
  const auto deck=folder+"/slides/main.tex";put(deck,"\\documentclass{beamer}\n\\begin{document}\n\\begin{frame}{Results 结果}First\\end{frame}\n\\begin{frame}{Proof 证明}Second\\end{frame}\n\\end{document}\n");
  auto *s=new TeXDocumentWindow(deck);s->resize(w->size());s->show();settle(650);check("slides ready",waitFor([&]{auto *l=s->findChild<QListWidget*>("researchSlideList");return l&&l->count()==2;}));
  auto *list=s->findChild<QListWidget*>("researchSlideList");auto *panel=s->findChild<QMenu*>("researchSlidePanelMenu");
  if(list){list->setCurrentRow(0);list->setFocus();QTimer::singleShot(90,s,[&]{auto *m=qobject_cast<QMenu*>(QApplication::activePopupWidget());check("slide context opens",m);if(m){snapshot(m,"slide-context-default");if(!baseline)check("slide notes omitted only from context",!m->actions().contains(list->findChild<QAction*>("researchSlideNotes")));key(m,Qt::Key_Escape);}});auto point=list->visualItemRect(list->item(0)).center();QContextMenuEvent event(QContextMenuEvent::Mouse,point,list->viewport()->mapToGlobal(point));QApplication::sendEvent(list->viewport(),&event);}
  open(panel,"slide-panel-default");
  if(!baseline){
   check("slide shortcut registrations untouched",list&&list->findChild<QAction*>("researchSlideCopy")&&list->actions().contains(list->findChild<QAction*>("researchSlideCopy")));
   QApplication::setActiveWindow(w);editor->setFocus();editor->setTextCursor(selection);
   auto settings=[&](bool save,bool enabled,bool reset=false){QTimer::singleShot(170,w,[&]{auto *d=w->findChild<QDialog*>("researchSettingsDialog");check("settings opens",d);if(!d)return;
     auto *tabs=d->findChild<QTabWidget*>("tabWidget");auto *page=d->findChild<QWidget*>("settingsShortcuts");auto *inputTabs=d->findChild<QTabBar*>("researchInputTabs");auto *area=d->findChild<QComboBox*>("researchMenuSurface");check("legacy menu destination opens the combined input page",tabs&&page&&tabs->currentWidget()==page&&inputTabs&&inputTabs->currentIndex()==0);check("one shared customization page",tabs&&tabs->count()==6&&area&&area->count()==4);if(!tabs||!area||!page||!inputTabs){d->reject();return;}tabs->setCurrentWidget(page);inputTabs->setCurrentIndex(0);
     for(int i=0;i<4;++i){area->setCurrentIndex(i);settle(30);d->grab().save(folder+QString("/settings-%1-%2.png").arg(save?"save":"cancel").arg(i));}
     for(auto *b:d->findChildren<QCheckBox*>())if(b->property("settingKey").toString().startsWith("researchMenu/"))b->setChecked(enabled);
     auto *buttons=d->findChild<QDialogButtonBox*>();if(reset)buttons->button(QDialogButtonBox::RestoreDefaults)->click();
     if(save)buttons->button(QDialogButtonBox::Ok)->click();else key(d,Qt::Key_Escape);
    });PrefsDialog::doPrefsDialog(w,8);settle(80);};
   settings(false,true);check("Escape does not save preferences",!Tw::Settings().value("researchMenu/contextSnippet",false).toBool());check("settings Escape restores editor focus",editor->hasFocus());
   settings(true,true);check("save persists preference",Tw::Settings().value("researchMenu/contextSnippet",false).toBool());
   open(edit,"edit-custom");open(insert,"insert-custom");context("context-custom",true);open(panel,"slide-panel-custom");
   check("same live menu restores line command",edit->actions().contains(line));check("same live menu restores macro command",insert->actions().contains(macro));check("quotation option maps to insert menu",insert->actions().contains(w->findChild<QAction*>("researchWrapQuotation"))&&!edit->actions().contains(w->findChild<QAction*>("researchWrapQuotation")));
   settings(true,false,true);open(edit,"edit-reset");open(insert,"insert-reset");check("reset restores minimal defaults",!edit->actions().contains(line)&&!insert->actions().contains(macro));
   for(int i=0;i<20;++i){Tw::Settings().setValue("researchMenu/editLines",i%2==0);QMetaObject::invokeMethod(edit,"aboutToShow");check("repeat toggle keeps one or zero entry",edit->actions().count(line)==(i%2==0?1:0));}
   Tw::Settings().setValue("researchMenu/editLines",false);QMetaObject::invokeMethod(edit,"aboutToShow");
   auto *late=new QAction("Late action",w);late->setObjectName("lateMenuChoiceTest");edit->addAction(late);QMetaObject::invokeMethod(edit,"aboutToShow");check("late commands survive filtering",edit->actions().contains(late));delete late;QMetaObject::invokeMethod(edit,"aboutToShow");check("destroyed command does not leave entry",!edit->findChild<QAction*>("lateMenuChoiceTest"));
   const auto original=editor->toPlainText();editor->setTextCursor(selection);editor->setFocus();QApplication::setActiveWindow(w);
   const auto sequence=line->shortcut();key(editor,sequence[0].key(),sequence[0].keyboardModifiers());settle(30);check("hidden line command still executes",editor->toPlainText()!=original);editor->undo();check("line command one-step undo",editor->toPlainText()==original);
   QTimer::singleShot(150,w,[&]{auto *d=w->findChild<QDialog*>("researchCommandDialog");check("palette opens",d);if(!d)return;auto *query=d->findChild<QLineEdit*>("researchCommandQuery");auto *results=d->findChild<QListWidget*>("researchCommandResults");query->setText("macro");settle(30);bool found=false;for(int i=0;i<results->count();++i)if(!results->item(i)->isHidden()&&results->item(i)->text().contains(macro->text().remove('&')))found=true;check("hidden macro remains searchable",found);key(d,Qt::Key_Escape);});w->findChild<QAction*>("researchCommandPalette")->trigger();
   QTimer::singleShot(150,w,[&]{auto *d=w->findChild<QDialog*>("researchSettingsDialog");if(!d)return;auto *tabs=d->findChild<QTabWidget*>("tabWidget");auto *inputTabs=d->findChild<QTabBar*>("researchInputTabs");auto *inputPages=d->findChild<QStackedWidget*>("researchInputPages");auto *page=d->findChild<QWidget*>("settingsShortcuts");check("combined page has synchronized menu and shortcut sections",page&&inputTabs&&inputPages);if(!tabs||!page||!inputTabs||!inputPages){d->reject();return;}tabs->setCurrentWidget(page);inputTabs->setCurrentIndex(1);auto *search=d->findChild<QLineEdit*>("researchSettingsSearch");search->setText(englishUi()?"speaker notes":traditionalUi()?"講者備註":"讲者备注");key(search,Qt::Key_Return);settle(50);auto *area=d->findChild<QComboBox*>("researchMenuSurface");auto *box=d->findChild<QCheckBox*>("slidesNotes");check("settings search reveals correct nested area",tabs->currentWidget()==page&&inputTabs->currentIndex()==0&&inputPages->currentIndex()==0&&area&&area->currentIndex()==3&&box&&box->isVisible());d->reject();});PrefsDialog::doPrefsDialog(w,0);
   setFocusMode(true);settle(250);open(edit,"edit-focus");check("focus uses same filtered actions",!edit->actions().contains(line));setFocusMode(false);settle(250);
  }
  put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"menus",menus},{"baseline",baseline},{"devicePixelRatio",qApp->primaryScreen()->devicePixelRatio()}}).toJson());
  s->textDoc()->setModified(false);w->textDoc()->setModified(false);s->close();w->close();settle();app.exit(failures?1:0);
 });return app.exec();
}
