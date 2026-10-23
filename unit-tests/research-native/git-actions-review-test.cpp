#include "ActualAuthoringRecording.h"
#include "ResearchVersionControl.h"
#include "ResearchTask.h"
#include "ResearchSettings.h"
using namespace Research;
static void dialog(const QString &name,std::function<void(QDialog*)> fn){auto *t=new QTimer(qApp);QObject::connect(t,&QTimer::timeout,t,[=]{for(auto *top:QApplication::topLevelWidgets())if(auto *d=qobject_cast<QDialog*>(top);d&&d->isVisible()&&d->objectName()==name){t->stop();t->deleteLater();fn(d);return;}});t->start(20);}
static void input(const QString &value){dialog("researchWorkspaceInputDialog",[=](QDialog *d){d->findChild<QLineEdit*>("researchWorkspaceInput")->setText(value);d->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();});}
int main(int argc,char **argv){const auto report=argument(1);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);QTimer::singleShot(60,&app,[&]{
 const auto folder=QFileInfo(report).absolutePath(),repo=folder+"/paper";QDir().mkpath(repo);QJsonArray checks;int failures=0;
 auto check=[&](const QString &name,bool pass){checks.append(QJsonObject{{"name",name},{"passed",pass}});failures+=!pass;put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures}}).toJson());};
 auto git=[&](QStringList args){QProcess p;p.setWorkingDirectory(repo);p.start(systemTool("git"),args);p.waitForFinished(15000);return p.readAllStandardOutput();};
 git({"init","-b","main"});git({"config","user.name","Fixture"});git({"config","user.email","fixture@example.invalid"});put(repo+"/main.tex","\\documentclass{article}\n\\begin{document}\nBase\n\\end{document}\n");put(repo+"/notes.tex","First\nLast\n");git({"add","."});git({"commit","-m","Start"});
 put(repo+"/notes.tex","First\nAlpha addition\nBeta addition\nLast\n");
 Tw::Settings().setValue("openPDFwithTeX",false);auto *w=new TeXDocumentWindow(repo+"/main.tex");w->resize(1040,760);w->show();settle();openVersionControl(w);auto *d=w->findChild<QDialog*>("researchVersionDialog");auto *files=d->findChild<QListWidget*>("researchGitFiles");auto *preview=d->findChild<QPlainTextEdit*>("researchGitDiff");auto ready=[&]{return files->isEnabled();};
 Recording recording;const bool record=qEnvironmentVariableIsSet("TEXLINE_INTUITIVE_RECORD");if(record)recording.start(folder+"/recording",d);
 auto trigger=[&](const char *zh,const char *en){for(auto *a:d->findChildren<QAction*>())if(a->text()==words(zh,en)){if(record){recording.action(a->text());settle(500);}a->trigger();if(record)settle(600);return true;}return false;};
 auto selectText=[&](const QString &text){auto cursor=preview->document()->find(text);if(cursor.isNull())return false;cursor.movePosition(QTextCursor::StartOfBlock);cursor.movePosition(QTextCursor::EndOfBlock,QTextCursor::KeepAnchor);preview->setTextCursor(cursor);return true;};
 check("added lines appear in actual diff",waitFor([&]{return ready()&&preview->toPlainText().contains("Alpha addition");}));
 check("selection maps to added diff line",selectText("Alpha addition"));trigger("加入选中行","Stage selected lines");
 check("UI stages only selected addition",waitFor(ready)&&git({"show",":notes.tex"}).contains("Alpha addition")&&!git({"show",":notes.tex"}).contains("Beta addition"));
 for(int i=0;i<files->count();++i)if(files->item(i)->data(Qt::UserRole+1).toBool())files->setCurrentRow(i);waitFor(ready);selectText("Alpha addition");
 // The same shared action selects the operation from the currently shown side.
 trigger("加入选中行","Stage selected lines");check("UI unstage retains both working additions",waitFor(ready)&&!git({"show",":notes.tex"}).contains("Alpha addition")&&bytes(repo+"/notes.tex").contains("Beta addition"));
 git({"add","notes.tex"});git({"commit","-m","Second"});const auto original=git({"rev-parse","HEAD"}).trimmed();trigger("刷新","Refresh");waitFor(ready);
 dialog("researchWorkspaceInputDialog",[](QDialog *draft){key(draft,Qt::Key_Escape);});trigger("修订上次版本…","Amend last commit…");settle(350);check("amend Esc leaves HEAD unchanged",git({"rev-parse","HEAD"}).trimmed()==original);
 input("Revised second");trigger("修订上次版本…","Amend last commit…");check("amend updates message and retains old commit",waitFor([&]{return ready()&&git({"log","-1","--format=%s"}).trimmed()=="Revised second";})&&git({"for-each-ref","--format=%(objectname)","refs/texline/recovery"}).contains(original));
 dialog("researchWorkspaceConfirmDialog",[](QDialog *draft){draft->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();});trigger("撤销上次提交…","Undo last commit…");check("undo commit preserves staged and working contents",waitFor([&]{return ready()&&git({"log","-1","--format=%s"}).trimmed()=="Start";})&&git({"diff","--cached"}).contains("Alpha addition")&&bytes(repo+"/notes.tex").contains("Beta addition"));
 dialog("researchGitPicker",[](QDialog *picker){picker->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();});input("retained");trigger("从保留版本恢复…","Recover retained version…");check("retained version opens as recoverable branch",waitFor([&]{return ready()&&!git({"show-ref","--verify","refs/heads/retained"}).isEmpty();}));
 d->grab().save(folder+"/git-partial-review.png");if(record)recording.finish();key(d,Qt::Key_Escape);settle();check("advanced Git Esc restores manuscript focus",w->editor()->hasFocus());for(auto *window:TeXDocumentWindow::documentList())window->textDoc()->setModified(false);for(auto *window:TeXDocumentWindow::documentList())window->close();settle();app.exit(failures?1:0);
 });return app.exec();}
