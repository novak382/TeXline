// Native return-to-dialog refresh checks. Run only on an inactive private desktop.
#include "ActualAuthoringRecording.h"
#include "ResearchVersionControl.h"
#include "ResearchTask.h"
#include "ResearchSettings.h"
using namespace Research;
static void returned(QWidget *dialog,int count=1){for(int i=0;i<count;++i){QEvent left(QEvent::WindowDeactivate),back(QEvent::WindowActivate);QApplication::sendEvent(dialog,&left);QApplication::sendEvent(dialog,&back);}}
int main(int argc,char **argv){
  if(argument(1)=="--version"){
    QCoreApplication helper(argc,argv);const auto folder=QFileInfo(argument(0)).absolutePath();
    const auto state=QJsonDocument::fromJson(bytes(folder+"/probe.json")).object();
    QFile log(folder+"/probe.log");if(log.open(QIODevice::WriteOnly|QIODevice::Append))log.write(state.value("marker").toString().toUtf8()+"\n");log.close();
    QThread::msleep(state.value("delay").toInt());QFile output;output.open(stdout,QIODevice::WriteOnly);output.write(state.value("marker").toString().toUtf8());output.close();return state.value("exit").toInt();
  }
  const auto report=argument(1);wchar_t desktop[256]{};DWORD needed=0;
  if(!GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()),UOI_NAME,desktop,sizeof(desktop),&needed)||QString::fromWCharArray(desktop)!=qEnvironmentVariable("TEXLINE_TEST_DESKTOP")||!QString::fromWCharArray(desktop).startsWith("TeXlineValidation-"))return 2;
  argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
  QTimer::singleShot(60,&app,[&]{
    const auto folder=QFileInfo(report).absolutePath(),repo=folder+"/private-repo";QDir().mkpath(repo);
    QJsonArray checks;int failures=0;
    auto check=[&](const QString &name,bool pass){checks.append(QJsonObject{{"name",name},{"passed",pass}});failures+=!pass;put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"syntheticWindowReturn",true},{"realInstallerOrPhysicalActivation",false}}).toJson());};
    QTimer watchdog;watchdog.setSingleShot(true);QObject::connect(&watchdog,&QTimer::timeout,&app,[&]{check("bounded native fixture completed",false);app.exit(2);});watchdog.start(120000);
    qputenv("GIT_CONFIG_GLOBAL",QFile::encodeName(folder+"/private-git-config"));qputenv("GIT_CONFIG_NOSYSTEM","1");qputenv("GIT_CEILING_DIRECTORIES",QFile::encodeName(folder));
    auto git=[&](const QStringList &args){QProcess p;p.setWorkingDirectory(repo);p.setProgram(systemTool("git"));p.setArguments(args);p.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *a){a->flags|=0x08000000;});p.start();const bool finished=p.waitForFinished(15000);return qMakePair(finished?p.exitCode():-1,p.readAllStandardOutput());};
    check("private Git repository initialized",git({"init","-b","main"}).first==0);git({"config","user.name","Private test"});git({"config","user.email","test@example.invalid"});
    put(repo+"/main.tex","\\documentclass{article}\n\\begin{document}Research\\end{document}\n");put(repo+"/a.tex","Initial staged note\n");put(repo+"/z.tex","Original paragraph\n");git({"add","."});check("initial local commit",git({"commit","-m","Start"}).first==0);
    put(repo+"/a.tex","Staged note\n");git({"add","a.tex"});QByteArray draft;for(int n=0;n<180;++n)draft+="Unique paragraph "+QByteArray::number(n)+".\n";put(repo+"/z.tex",draft);
    Tw::Settings().setValue("openPDFwithTeX",false);auto *w=new TeXDocumentWindow(repo+"/main.tex");w->resize(qEnvironmentVariableIsSet("TEXLINE_TEST_HALF")?620:1040,760);w->show();settle();
    const auto source=w->editor()->toPlainText(),engine=w->researchEngineName();int compiles=0;QObject::connect(w->findChild<QAction*>("actionTypeset"),&QAction::triggered,w,[&]{++compiles;});
    openVersionControl(w);auto *d=w->findChild<QDialog*>("researchVersionDialog");auto *list=d->findChild<QListWidget*>("researchGitFiles");auto *diff=d->findChild<QPlainTextEdit*>("researchGitDiff");auto *message=d->findChild<QLineEdit*>("researchGitMessage");
    if(qEnvironmentVariableIsSet("TEXLINE_TEST_HALF"))d->resize(620,d->height());
    auto ready=[&]{return list->isEnabled()&&list->count()>0;};
    check("initial Git status and diff finish",waitFor(ready));
    auto findFile=[&](const QString &path,bool staged)->QListWidgetItem*{for(int i=0;i<list->count();++i){auto *item=list->item(i);if(item->data(Qt::UserRole+3).toString()==path&&item->data(Qt::UserRole+1).toBool()==staged)return item;}return nullptr;};
    auto *selected=findFile("z.tex",false);check("nonfirst working file available",selected!=nullptr);if(selected)list->setCurrentItem(selected);check("selected diff is loaded",waitFor([&]{return ready()&&diff->toPlainText().contains("Unique paragraph 179.");}));
    auto cursor=diff->document()->find("Unique paragraph 70.");diff->setTextCursor(cursor);diff->verticalScrollBar()->setValue(60);message->setText("Continue the proof tomorrow");message->setFocus();settle(100);
    const auto oldText=diff->toPlainText();const int oldPosition=diff->textCursor().position(),oldAnchor=diff->textCursor().anchor(),oldScroll=diff->verticalScrollBar()->value();
    const auto oldIndex=git({"diff","--cached","--binary"}).second;const int refreshCount=d->property("researchGitRefreshCount").toInt();d->grab().save(folder+"/git-before-return.png");
    put(repo+"/b.tex","External local note\n");returned(d,5);
    check("return coalesces into one local Git refresh",waitFor([&]{return ready()&&findFile("b.tex",false)&&d->property("researchGitRefreshCount").toInt()==refreshCount+1;}));
    check("return retains selected file and commit draft",list->currentItem()&&list->currentItem()->data(Qt::UserRole+3).toString()=="z.tex"&&message->text()=="Continue the proof tomorrow");
    check("unchanged diff retains reverse-independent selection and reading position",diff->toPlainText()==oldText&&diff->textCursor().position()==oldPosition&&diff->textCursor().anchor()==oldAnchor&&diff->verticalScrollBar()->value()==oldScroll);
    check("refresh leaves index and typing focus intact",git({"diff","--cached","--binary"}).second==oldIndex&&message->hasFocus());d->grab().save(folder+"/git-after-return.png");
    const int readingCount=d->property("researchGitRefreshCount").toInt();returned(d);check("return starts a background local read",waitFor([&]{return !list->isEnabled();}));
    cursor=diff->document()->find("Unique paragraph 92.");diff->setTextCursor(cursor);diff->verticalScrollBar()->setValue(80);const int newPosition=cursor.position(),newAnchor=cursor.anchor(),newScroll=diff->verticalScrollBar()->value();
    check("reading during refresh wins over earlier viewport state",waitFor([&]{return ready()&&d->property("researchGitRefreshCount").toInt()==readingCount+1;})&&diff->textCursor().position()==newPosition&&diff->textCursor().anchor()==newAnchor&&diff->verticalScrollBar()->value()==newScroll);
    draft="Inserted before existing lines\n"+draft;put(repo+"/z.tex",draft);const int changedCount=d->property("researchGitRefreshCount").toInt();returned(d);
    check("changed diff refreshes using current file",waitFor([&]{return ready()&&d->property("researchGitRefreshCount").toInt()==changedCount+1&&diff->toPlainText().contains("Inserted before existing lines");}));
    check("changed diff relocates same content without stale staging selection",diff->textCursor().block().text().contains("Unique paragraph 92.")&&!diff->textCursor().hasSelection());
    const auto priorTop=diff->cursorForPosition(QPoint(0,0));const auto topText=priorTop.block().text();const int topY=diff->cursorRect(priorTop).top(),wrapCount=d->property("researchGitRefreshCount").toInt();
    draft=QByteArray("A long newly inserted note ").repeated(45)+"\n"+draft;put(repo+"/z.tex",draft);returned(d);
    check("wrapped insertion refresh finishes",waitFor([&]{return ready()&&d->property("researchGitRefreshCount").toInt()==wrapCount+1&&diff->toPlainText().contains("A long newly inserted note");}));
    const auto afterTop=diff->cursorForPosition(QPoint(0,0));check("wrapped diff lines retain the same top content and visual offset",afterTop.block().text()==topText&&qAbs(diff->cursorRect(afterTop).top()-topY)<=2);
    selected=findFile("z.tex",false);const int writeCount=d->property("researchGitRefreshCount").toInt();if(selected)selected->setCheckState(Qt::Checked);
    check("checkbox starts an asynchronous Git write",!list->isEnabled());returned(d,6);message->setText("Draft typed during the operation");
    check("return during write is absorbed by its single refresh",waitFor([&]{return ready()&&findFile("z.tex",true)&&d->property("researchGitRefreshCount").toInt()==writeCount+1;}));
    check("staging intent and newer input survive deferred refresh",list->currentItem()&&list->currentItem()->data(Qt::UserRole+3).toString()=="z.tex"&&list->currentItem()->data(Qt::UserRole+1).toBool()&&message->text()=="Draft typed during the operation"&&git({"diff","--cached","--name-only"}).second.contains("z.tex"));
    git({"reset","-q","HEAD","--","z.tex"});const int unstageCount=d->property("researchGitRefreshCount").toInt();returned(d);
    check("external unstage is reflected without restoring old checkboxes",waitFor([&]{return ready()&&d->property("researchGitRefreshCount").toInt()==unstageCount+1&&findFile("z.tex",false);})&&!findFile("z.tex",true)&&!git({"diff","--cached","--name-only"}).second.contains("z.tex"));
    auto *tabs=d->findChild<QTabBar*>();tabs->setCurrentIndex(1);check("history view loads",waitFor(ready));const auto selectedCommit=list->currentItem()?list->currentItem()->data(Qt::UserRole).toString():QString();git({"commit","-m","External local commit"});const int historyCount=d->property("researchGitRefreshCount").toInt();returned(d);
    check("history return preserves selected commit while including new commit",waitFor([&]{return ready()&&list->count()>=2&&d->property("researchGitRefreshCount").toInt()==historyCount+1;})&&list->currentItem()&&list->currentItem()->data(Qt::UserRole).toString()==selectedCommit);
    check("private repository metadata can be temporarily moved",QDir().rename(repo+"/.git",folder+"/private-git-metadata"));tabs->setCurrentIndex(0);
    check("failed tab change cannot reinterpret old commit rows as files",waitFor([&]{return list->isEnabled()&&list->count()==0;})&&diff->toPlainText().isEmpty());
    check("private metadata is restored after failure probe",QDir().rename(folder+"/private-git-metadata",repo+"/.git"));
    QPointer<QDialog> gitGuard=d;returned(d);key(d,Qt::Key_Escape);settle();check("closing queued Git refresh restores editor focus",gitGuard.isNull()&&w->editor()->hasFocus());

    const auto originalPaths=app.getBinaryPaths();const auto pathEnv=qgetenv("PATH");const auto bin1=folder+"/private-tools-1",bin2=folder+"/private-tools-2";QDir().mkpath(bin1);QDir().mkpath(bin2);app.setBinaryPaths({bin1});qputenv("PATH",QFile::encodeName(QCoreApplication::applicationDirPath())+";"+pathEnv);
    const QString tool="texline-private-return-probe",exe1=bin1+"/"+tool+".exe",exe2=bin2+"/"+tool+".exe";
    openEnvironmentCheck(w,tool);auto *environment=w->findChild<QDialog*>("researchEnvironmentDialog");auto *tools=environment->findChild<QListWidget*>("researchEnvironmentTools");auto *retry=environment->findChild<QPushButton*>("researchEnvironmentRetry");auto *details=environment->findChild<QLabel*>("researchEnvironmentDetails");
    check("private missing tool is reported and cannot retry",tools->count()==1&&tools->item(0)->text().contains(words("未找到","Not found"))&&!retry->isEnabled());tools->setCurrentRow(0);
    auto linkHelper=[&](const QString &path){const auto source=QDir::toNativeSeparators(QCoreApplication::applicationFilePath()),destination=QDir::toNativeSeparators(path);return CreateHardLinkW(reinterpret_cast<LPCWSTR>(destination.utf16()),reinterpret_cast<LPCWSTR>(source.utf16()),nullptr)||QFile::copy(source,path);};
    const bool helpers=linkHelper(exe1)&&linkHelper(exe2);check("private helper tools are provisioned without user installation",helpers);
    if(helpers){
      put(bin1+"/probe.json",QJsonDocument(QJsonObject{{"marker","first-ready"},{"delay",0},{"exit",0}}).toJson());const int initialCount=environment->property("researchEnvironmentCheckCount").toInt();returned(environment,5);
      check("return detects tool provision once without automatic compilation",waitFor([&]{return retry->isEnabled()&&environment->property("researchEnvironmentCheckCount").toInt()==initialCount+1;})&&compiles==0&&w->researchEngineName()==engine);
      check("tool selection and details refresh in place",tools->currentRow()==0&&details->text().contains("first-ready"));
      put(bin1+"/probe.json",QJsonDocument(QJsonObject{{"marker","obsolete-slow-result"},{"delay",650},{"exit",0}}).toJson());returned(environment);
      check("slow old environment probe actually started",waitFor([&]{return bytes(bin1+"/probe.log").contains("obsolete-slow-result");}));
      const int slowCount=environment->property("researchEnvironmentCheckCount").toInt();put(bin2+"/probe.json",QJsonDocument(QJsonObject{{"marker","current-config-result"},{"delay",250},{"exit",0}}).toJson());app.setBinaryPaths({bin2});returned(environment,7);
      check("pending environment return immediately invalidates old readiness",!retry->isEnabled());bool staleShown=false;
      const bool fresh=waitFor([&]{staleShown|=details->text().contains("obsolete-slow-result");return retry->isEnabled()&&details->text().contains("current-config-result");});
      check("changed configuration rejects old result and runs one new pass",fresh&&!staleShown&&environment->property("researchEnvironmentCheckCount").toInt()==slowCount+1&&bytes(bin2+"/probe.log").count('\n')==1);
      check("environment refresh does not change engine source or compile",compiles==0&&w->researchEngineName()==engine&&w->editor()->toPlainText()==source);environment->grab().save(folder+"/environment-return.png");
      put(bin2+"/probe.json",QJsonDocument(QJsonObject{{"marker","close-during-check"},{"delay",1000},{"exit",0}}).toJson());returned(environment);waitFor([&]{return bytes(bin2+"/probe.log").contains("close-during-check");});
    }
    QPointer<QDialog> environmentGuard=environment;key(environment,Qt::Key_Escape);settle(200);check("environment Esc cancels pending probes and returns focus",environmentGuard.isNull()&&w->editor()->hasFocus());
    app.setBinaryPaths(originalPaths);qputenv("PATH",pathEnv);QFile::remove(exe1);QFile::remove(exe2);watchdog.stop();
    w->textDoc()->setModified(false);w->close();settle();app.exit(failures?1:0);
  });return app.exec();
}
