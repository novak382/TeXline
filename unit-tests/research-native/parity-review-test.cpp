// Small preflight review of new workflows; deliberately excludes stress/DPI matrices.
#include "ActualAuthoringRecording.h"
#include "ResearchBibliography.h"
#include "ResearchProjectOperations.h"
#include "ResearchVersionControl.h"
#include "ResearchAuthoring.h"
#include "ResearchTask.h"
#include "ResearchSettings.h"
using namespace Research;
static void dialogWhen(QWidget *parent,const QString &name,std::function<void(QDialog*)> action){
  auto *timer=new QTimer(parent);auto *elapsed=new QElapsedTimer;elapsed->start();
  QObject::connect(timer,&QTimer::timeout,timer,[=]{
    auto *dialog=parent->findChild<QDialog*>(name);
    if(dialog&&dialog->isVisible()){timer->stop();timer->deleteLater();delete elapsed;action(dialog);}
    else if(elapsed->elapsed()>8000){timer->stop();timer->deleteLater();delete elapsed;}
  });timer->start(20);
}
static QAction *action(QWidget *parent,const QString &caption){for(auto *a:parent->findChildren<QAction*>())if(a->text()==caption)return a;return nullptr;}
int main(int argc,char **argv){const auto report=argument(1);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
QTimer::singleShot(60,&app,[&]{
  const auto folder=QFileInfo(report).absolutePath();QJsonArray checks;int failures=0;
  auto check=[&](const QString &name,bool pass){checks.append(QJsonObject{{"name",name},{"passed",pass}});failures+=!pass;put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"devicePixelRatio",qApp->primaryScreen()->devicePixelRatio()}}).toJson());};
  Tw::Settings().setValue("openPDFwithTeX",false);Tw::Settings().setValue("researchFocusMode",false);
  const auto paper=folder+"/paper",main=paper+"/main.tex",child=paper+"/chapter.tex",bib=paper+"/refs.bib";
  const QByteArray source="\\documentclass{article}\n\\begin{document}\nEnglish and 中文 \\cite{old}.\n\\input{chapter}\n\\bibliography{refs}\n\\end{document}\n";
  const QByteArray chapter="% !TEX root = main.tex\nAnother \\cite[3]{old}.\n";
  const QByteArray references="% keep comment\n@article{old,title={A paper},author={Hu, J.},year={2026},eprint={2101.01234v2},archiveprefix={arXiv}}\n";
  put(main,source);put(child,chapter);put(bib,references);
  auto *window=new TeXDocumentWindow(main);window->resize(qEnvironmentVariableIsSet("TEXLINE_TEST_HALF")?620:1040,760);window->show();
  check("article is ready",waitFor([&]{return window->property("researchArticleReady").toBool();}));
  auto cursor=window->textCursor();cursor.setPosition(source.indexOf("English"));cursor.setPosition(cursor.position()+7,QTextCursor::KeepAnchor);window->editor()->setTextCursor(cursor);
  const auto originalCursor=window->textCursor();
  QStringList added;
  dialogWhen(window,"researchBibImportPreview",[&](QDialog *d){
    auto *list=d->findChild<QListWidget*>("researchBibImportEntries");
    check("arXiv URL and eprint duplicate starts unchecked",list&&list->count()==2&&list->item(0)->checkState()==Qt::Unchecked&&list->item(1)->checkState()==Qt::Checked);
    d->grab().save(folder+"/import-review.png");key(d,Qt::Key_Escape);
  });
  const QString incoming="@misc{same,title={A paper},url={https://arxiv.org/abs/2101.01234v3}}\n@book{book,title={A paper},publisher={Press},year={2026}}";
  check("import Esc leaves disk unchanged",!appendBibliography(window,window,bib,incoming,&added)&&bytes(bib)==references);
  dialogWhen(window,"researchBibImportPreview",[](QDialog *d){d->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();});
  check("selected import succeeds",appendBibliography(window,window,bib,incoming,&added)&&added==QStringList{"book"});
  auto *bw=TeXDocumentWindow::findDocument(bib);
  check("import changes buffer only",bw&&bw->isModified()&&bytes(bib)==references);
  if(bw){bw->editor()->undo();check("import is one native undo",bw->textDoc()->editorText()==QString::fromUtf8(references));bw->textDoc()->setModified(false);}
  dialogWhen(window,"researchBibEntryDialog",[&](QDialog *d){
    auto *type=d->findChild<QComboBox*>("researchBibType");type->setCurrentIndex(type->findData("book"));settle(60);
    auto *publisher=d->findChild<QLineEdit*>("researchBibField_publisher");
    check("book fields are available and journal hidden",publisher&&publisher->isVisible()&&!d->findChild<QLineEdit*>("researchBibField_journal")->isVisible());
    auto *missing=d->findChild<QLabel*>("researchBibMissingFields");check("book editor identifies missing publisher",missing&&missing->text().contains(words("出版社","Publisher")));
    publisher->setText("出版社 / Press");check("field completion clears the relevant hint",missing&&!missing->text().contains(words("出版社","Publisher")));d->grab().save(folder+"/book-fields.png");key(d,Qt::Key_Escape);
  });check("field edit cancellation preserves entry",!editBibliographyEntry(window,window,bib,"old")&&bw->textDoc()->editorText()==QString::fromUtf8(references));
  dialogWhen(window,"researchBibAcquire",[&](QDialog *d){
    auto *type=d->findChild<QComboBox*>("researchBibType");check("dataset and uncommon BibLaTeX types are offered",type->findData("dataset")>=0&&type->findData("patent")>=0);
    type->setCurrentIndex(type->findData("dataset"));d->findChild<QLineEdit*>("researchBibNew_title")->setText("Independent dataset");d->findChild<QLineEdit*>("researchBibNew_version")->setText("2.0");
    auto *tabs=d->findChild<QTabWidget*>();check("new entry has source escape hatch",tabs&&tabs->count()==2);
    if(tabs){tabs->setCurrentIndex(1);auto *raw=d->findChild<QPlainTextEdit*>("researchBibRaw");check("field draft transfers losslessly to source",raw&&raw->toPlainText().contains("version = {2.0}"));if(raw){raw->moveCursor(QTextCursor::End);raw->insertPlainText("\n% keep source comment");tabs->setCurrentIndex(0);check("source edit is not overwritten by switching tabs",tabs->currentIndex()==1&&raw->toPlainText().endsWith("% keep source comment"));}}
    d->grab().save(folder+"/new-dataset-source.png");key(d,Qt::Key_Escape);
  });check("new entry cancel returns no draft",acquireBibliography(window,0).isEmpty());
  // Exercise the actual citation manager and its cross-document undo group.
  auto *cite=window->findChild<QAction*>("researchInsertCitation");
  if(!cite)for(auto *a:window->findChildren<QAction*>())if(a->objectName().contains("Citation")&&a->text().contains(words("文献","Citation"))){cite=a;break;}
  check("citation command is discoverable",cite!=nullptr);
  if(cite){cite->trigger();auto *d=window->findChild<QDialog*>("researchCitationDialog");
    auto *list=d?d->findChild<QListWidget*>("researchCitationList"):nullptr;
    check("citation dialog fits its writing window",d&&d->width()<=window->width());
    check("citation index loaded",list&&waitFor([&]{return list->count()==1;},8000));
    if(list&&list->count()){
      list->setCurrentRow(0);settle(80);
      auto *rename=action(d,words("更改引用键…","Rename citation key…"));
      dialogWhen(d,"researchWorkspaceInputDialog",[](QDialog *input){input->findChild<QLineEdit*>("researchWorkspaceInput")->setText("renamed");input->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();});
      dialogWhen(d,"researchWorkspaceConfirmDialog",[&](QDialog *confirm){confirm->grab().save(folder+"/rename-preview.png");confirm->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();});
      if(rename)rename->trigger();
      auto *cw=TeXDocumentWindow::findDocument(child);
      check("rename updates paper chapter and bibliography",rename&&cw&&window->textDoc()->editorText().contains("\\cite{renamed}")&&cw->textDoc()->editorText().contains("{renamed}")&&bw->textDoc()->editorText().contains("@article{renamed,"));
      auto *undo=action(d,words("撤销文献修改","Undo reference edit"));if(undo)undo->trigger();
      check("group undo restores exact three documents",undo&&cw&&window->textDoc()->editorText()==QString::fromUtf8(source)&&cw->textDoc()->editorText()==QString::fromUtf8(chapter)&&bw->textDoc()->editorText()==QString::fromUtf8(references));
      auto *redo=action(d,words("重做文献修改","Redo reference edit"));if(redo)redo->trigger();
      check("group redo restores renamed citation",redo&&window->textDoc()->editorText().contains("\\cite{renamed}"));
      if(undo)undo->trigger();d->grab().save(folder+"/citation-review.png");
    }
    if(d){key(d,Qt::Key_Escape);settle();}
    check("citation Esc restores focus",window->editor()->hasFocus());
    check("citation closes to manuscript selection",window->textCursor().position()==originalCursor.position()&&window->textCursor().anchor()==originalCursor.anchor());
  }
  const auto standalone=folder+"/standalone/library.bib";put(standalone,references);
  auto *library=new TeXDocumentWindow(standalone);library->resize(qEnvironmentVariableIsSet("TEXLINE_TEST_HALF")?620:1040,760);library->show();settle(150);
  auto *manage=library->findChild<QAction*>("researchInsertCitation");check("standalone bibliography has management command",manage&&manage->text()==words("管理文献…","Manage references…"));
  if(manage){manage->trigger();auto *d=library->findChild<QDialog*>("researchCitationDialog");auto *list=d?d->findChild<QListWidget*>("researchCitationList"):nullptr;
    check("standalone bibliography loads without a TeX document",list&&waitFor([&]{return list->count()==1;},8000));
    if(d){auto *buttons=d->findChild<QDialogButtonBox*>("researchCitationButtons");check("management hides citation-only controls",!buttons->button(QDialogButtonBox::Ok)->isVisible()&&!buttons->button(QDialogButtonBox::Ok)->isEnabled()&&!d->findChild<QLineEdit*>("researchCitationLocator")->isVisible());
      QMetaObject::invokeMethod(buttons,"accepted",Qt::DirectConnection);check("management cannot insert citation into bib",library->textDoc()->editorText()==QString::fromUtf8(references));
      dialogWhen(d,"researchBibEntryDialog",[](QDialog *entry){entry->findChild<QLineEdit*>("researchBibField_title")->setText("Updated standalone title");entry->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();});
      d->findChild<QToolButton*>("researchCitationEditSource")->click();check("standalone edit updates its own buffer",library->textDoc()->editorText().contains("Updated standalone title")&&bytes(standalone)==references);
      library->editor()->undo();check("standalone edit has native undo",library->textDoc()->editorText()==QString::fromUtf8(references));
      d->grab().save(folder+"/standalone-bibliography.png");key(d,Qt::Key_Escape);settle();check("standalone manager Esc restores source focus",library->editor()->hasFocus());
    }
  }
  library->textDoc()->setModified(false);library->close();settle();
  QString error;QDir().mkpath(paper+"/target");
  auto batch=planProjectTransfer(paper,{child,bib},paper+"/target",ProjectTransfer::Move,{},main);
  check("batch preview does not mutate",batch.error.isEmpty()&&bytes(child)==chapter&&bytes(bib)==references);
  // Close modified flags for the independent filesystem transaction review.
  for(auto *w:TeXDocumentWindow::documentList())w->textDoc()->setModified(false);
  check("batch move updates all paths together",applyProjectPlan(batch,folder+"/move.json",&error)&&bytes(main).contains("\\input{target/chapter}")&&bytes(main).contains("\\bibliography{target/refs}"));
  check("batch undo restores bytes",applyProjectPlan(batch,folder+"/undo.json",&error,true)&&bytes(main)==source&&bytes(child)==chapter&&bytes(bib)==references);
  check("authentication hint redacts password",!gitFailureMessage("fatal: Authentication failed for 'https://alice:secret@example.invalid/repo'").contains("secret")&&gitFailureMessage("fatal: Authentication failed").contains(words("身份验证","Authentication")));
  // Git UI review uses a private repository and identity, never an account.
  const auto repo=folder+"/git";QDir().mkpath(repo);
  auto git=[&](QStringList args){QProcess p;p.setWorkingDirectory(repo);p.start(systemTool("git"),args);p.waitForFinished(15000);return p.exitCode()==0;};
  check("private repository initialized",git({"init","-b","main"})&&git({"config","user.name","Fixture"})&&git({"config","user.email","fixture@example.invalid"}));
  put(repo+"/main.tex",source);check("private initial commit",git({"add","."})&&git({"commit","-m","First paper"}));
  put(repo+"/main.tex",source+"% reference notes\n");
  auto *gw=new TeXDocumentWindow(repo+"/main.tex");gw->resize(qEnvironmentVariableIsSet("TEXLINE_TEST_HALF")?620:1040,760);gw->show();settle(150);openVersionControl(gw);
  auto *versions=gw->findChild<QDialog*>("researchVersionDialog");
  if(!versions)for(auto *d:gw->findChildren<QDialog*>())if(d->findChild<QListWidget*>("researchGitFiles"))versions=d;
  auto *files=versions?versions->findChild<QListWidget*>("researchGitFiles"):nullptr;
  check("version changes ready",files&&waitFor([&]{return files->isEnabled()&&files->count()>0;}));
  if(versions){settle(180);auto *summary=versions->findChild<QLabel*>("researchGitDiffSummary");if(summary){put(folder+"/diff-summary-geometry.json",QJsonDocument(QJsonObject{{"text",summary->text()},{"width",summary->width()},{"height",summary->height()},{"advance",summary->fontMetrics().horizontalAdvance(summary->text())},{"fontHeight",summary->fontMetrics().height()},{"font",summary->font().toString()},{"hintWidth",summary->sizeHint().width()}}).toJson());check("Git diff summary has complete text geometry",summary->width()>=summary->fontMetrics().horizontalAdvance(summary->text())&&summary->height()>=summary->fontMetrics().height());}}
  if(versions){
    auto ready=[&]{return files->isEnabled();};auto trigger=[&](const char *zh,const char *en){auto *a=action(versions,words(zh,en));if(a)a->trigger();return a!=nullptr;};
    versions->grab().save(folder+"/git-changes.png");auto *tabs=versions->findChild<QTabBar*>();
    if(tabs){tabs->setCurrentIndex(1);check("history loaded",waitFor([&]{return ready()&&files->count()>0;}));settle(100);versions->grab().save(folder+"/git-history.png");tabs->setCurrentIndex(0);waitFor(ready);}
    const auto authRoot=qEnvironmentVariable("TEXLINE_GIT_AUTH_ROOT"),authUrl=qEnvironmentVariable("TEXLINE_GIT_AUTH_URL");
    if(!authRoot.isEmpty()){
      check("private credential remote prepared",git({"clone","--bare",repo,authRoot+"/repo.git"})&&git({"--git-dir="+authRoot+"/repo.git","update-server-info"})&&git({"remote","add","origin",authUrl}));
      auto fetch=[&]{dialogWhen(versions,"researchGitRemote",[](QDialog *d){d->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();});return trigger("获取远程版本","Fetch");};
      fetch();check("credential failure returns to usable UI",waitFor(ready,12000));
      bool hint=false;for(auto *label:versions->findChildren<QLabel*>())hint|=label->text().contains(words("身份验证失败","Authentication failed"));
      check("credential failure shows actionable hint",hint);versions->grab().save(folder+"/git-auth-failure.png");
      check("private helper configured",git({"config","credential.helper","!f() { echo username=review; echo password=fixture-only; }; f"}));
      auto draft=gw->textCursor();draft.movePosition(QTextCursor::End);draft.insertText("% unsaved private draft\n");
      fetch();check("retry uses Git credential helper and fetches",waitFor(ready,15000)&&QFileInfo::exists(repo+"/.git/FETCH_HEAD")&&bytes(repo+"/.git/FETCH_HEAD").contains("branch 'main'"));
      check("fetch preserves unsaved writing",gw->isModified()&&gw->textDoc()->editorText().contains("unsaved private draft")&&!bytes(repo+"/main.tex").contains("unsaved private draft"));gw->editor()->undo();gw->textDoc()->setModified(false);
      versions->grab().save(folder+"/git-auth-recovered.png");
    }
    dialogWhen(versions,"researchWorkspaceInputDialog",[](QDialog *d){d->findChild<QLineEdit*>("researchWorkspaceInput")->setText("Private draft");d->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();});
    check("stash command available",trigger("暂存工作进度…","Stash work…"));check("stash retains work and cleans tree",waitFor(ready)&&bytes(repo+"/main.tex")==source);
    dialogWhen(versions,"researchGitPicker",[](QDialog *d){d->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();});
    dialogWhen(versions,"researchGitStashPreview",[&](QDialog *d){d->grab().save(folder+"/git-stash-preview.png");key(d,Qt::Key_Escape);});
    trigger("恢复暂存进度…","Apply stash…");check("stash preview cancel preserves worktree",waitFor(ready)&&bytes(repo+"/main.tex")==source);
    dialogWhen(versions,"researchGitPicker",[](QDialog *d){d->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();});
    dialogWhen(versions,"researchGitStashPreview",[](QDialog *d){d->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();});
    trigger("恢复暂存进度…","Apply stash…");check("stash restoration preserves notes",waitFor(ready)&&bytes(repo+"/main.tex").contains("reference notes"));
    key(versions,Qt::Key_Escape);settle();check("Git Esc restores focus",gw->editor()->hasFocus());
  }
  for(auto *w:TeXDocumentWindow::documentList())w->textDoc()->setModified(false);
  for(auto *w:TeXDocumentWindow::documentList())w->close();settle(250);
  check("local review finished",true);app.exit(failures?1:0);
});return app.exec();}
