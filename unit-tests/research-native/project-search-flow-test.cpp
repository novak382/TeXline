// Real project-search dialog on an inactive private desktop. IME composition
// events are synthetic; this fixture does not claim system-IME screen coverage.
#include "ActualAuthoringRecording.h"
#include "ResearchSettings.h"
#include "ResearchProjectOperations.h"
#include <QInputMethodEvent>
using namespace Research;

int main(int argc,char **argv){
  const auto report=argument(1);
  if(!qEnvironmentVariable("TEXLINE_TEST_DESKTOP").startsWith("TeXlineValidation-"))return 2;
  argc=1;QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
  QTimer::singleShot(80,&app,[&]{
    const auto folder=QFileInfo(report).absolutePath(),paper=folder+"/paper",main=paper+"/main.tex";
    const auto child=paper+"/sections/one.tex",bib=paper+"/refs.bib",style=paper+"/school.cls",plain=paper+"/notes.txt";
    const QByteArray source="\\documentclass{article}\n\\begin{document}\n\\input{sections/one}\n\\end{document}\n";
    const QByteArray childSource=QByteArray("\xef\xbb\xbf")+"% !TEX root = ../main.tex\nneedle alpha\n中文 术语\n";
    const QByteArray bibSource="@book{k,title={needle bibliography}}\n",styleSource="% needle class\n",plainSource="needle note\n";
    put(main,source);put(child,childSource);put(bib,bibSource);put(style,styleSource);put(plain,plainSource);
    put(paper+"/build/ignored.tex","needle ignored build\n");put(paper+"/.private/ignored.tex","needle ignored hidden folder\n");
    put(paper+"/picture.svg","needle ignored image\n");put(paper+"/legacy.tex",QByteArray("\xff\xfe")+"needle non UTF8\n");
    QJsonArray checks,frames,observations;int failures=0;
    auto save=[&]{put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"frames",frames},{"observations",observations},{"devicePixelRatio",qApp->primaryScreen()->devicePixelRatio()},{"physicalInput",false},{"systemIme",false}}).toJson());};
    auto check=[&](const QString &name,bool passed){checks.append(QJsonObject{{"name",name},{"passed",passed}});failures+=!passed;save();};
    auto capture=[&](QWidget *widget,const QString &name){QCoreApplication::sendPostedEvents(nullptr,QEvent::LayoutRequest);settle(70);const auto image=widget->grab();const bool saved=image.save(folder+"/"+name+".png");frames.append(QJsonObject{{"file",name+".png"},{"width",widget->width()},{"height",widget->height()},{"devicePixelRatio",image.devicePixelRatio()},{"saved",saved}});check(name+": actual widget captured",saved);};
    QTimer watchdog;watchdog.setSingleShot(true);watchdog.setInterval(90000);
    QObject::connect(&watchdog,&QTimer::timeout,&app,[&]{check("fixture finishes without unexpected modal",false);if(auto *d=qobject_cast<QDialog*>(QApplication::activeModalWidget()))d->reject();app.exit(1);});watchdog.start();
    Tw::Settings().setValue("openPDFwithTeX",false);Tw::Settings().setValue("researchFocusMode",false);
    auto *w=new TeXDocumentWindow(main);w->resize(qEnvironmentVariableIsSet("TEXLINE_TEST_HALF")?620:1120,740);w->show();QApplication::setActiveWindow(w);settle(180);
    auto selection=w->textCursor();selection.setPosition(26);selection.setPosition(8,QTextCursor::KeepAnchor);w->editor()->setTextCursor(selection);
    auto *filesAction=w->findChild<QAction*>("researchProjectFilesAction");check("existing project command is available",filesAction!=nullptr);
    if(!filesAction){w->textDoc()->setModified(false);w->close();watchdog.stop();app.exit(1);return;}
    filesAction->trigger();settle(100);QPointer<QDialog> files=w->findChild<QDialog*>("researchProjectFiles");
    check("project files opens through the existing command",files&&files->isVisible());
    if(!files){w->close();watchdog.stop();app.exit(1);return;}
    auto operation=[&](const char *zh,const char *en){for(auto *action:files->findChildren<QAction*>())if(action->text()==words(zh,en)){action->trigger();return true;}return false;};
    auto search=[&](const std::function<void(QDialog*)> &body){
      bool reached=false;QTimer responder,timeout;responder.setInterval(15);timeout.setSingleShot(true);timeout.setInterval(20000);
      QObject::connect(&responder,&QTimer::timeout,&app,[&]{auto *d=files?files->findChild<QDialog*>("researchProjectSearch"):nullptr;if(!d||!d->isVisible())return;responder.stop();reached=true;QPointer<QDialog> guard(d);body(d);if(guard&&guard->isVisible())key(guard,Qt::Key_Escape);});
      QObject::connect(&timeout,&QTimer::timeout,&app,[&]{check("search interaction completes within its watchdog",false);if(auto *d=qobject_cast<QDialog*>(QApplication::activeModalWidget()))d->reject();});
      responder.start();timeout.start();const bool invoked=operation("项目内查找与替换…","Find and replace in project…");responder.stop();timeout.stop();check("existing project search action opens its real dialog",invoked&&reached);settle(80);
    };
    auto unchanged=[&]{return bytes(main)==source&&bytes(child)==childSource&&bytes(bib)==bibSource&&bytes(style)==styleSource&&bytes(plain)==plainSource;};
    auto controls=[&](QDialog *d){return d->findChild<QLineEdit*>("researchProjectTextQuery")&&d->findChild<QListWidget*>("researchProjectTextResults")&&d->findChild<QLabel*>("researchProjectSearchNotice")&&d->findChild<QPushButton*>("researchProjectSearchRetry")&&d->findChild<QDialogButtonBox*>();};
    search([&](QDialog *d){
      check("search exposes stable existing-purpose controls",controls(d));if(!controls(d))return;
      auto *query=d->findChild<QLineEdit*>("researchProjectTextQuery");auto *rows=d->findChild<QListWidget*>("researchProjectTextResults");auto *notice=d->findChild<QLabel*>("researchProjectSearchNotice");auto *find=d->findChild<QPushButton*>("researchProjectSearchRetry");auto *preview=d->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);
      check("first use is ready to type without an extra Find button",query->hasFocus()&&query->text().isEmpty()&&rows->count()==0&&!find->isVisible()&&!notice->isVisible()&&!preview->isEnabled());
      QElapsedTimer elapsed;elapsed.start();type(query,"needle",15);
      check("a typing burst waits for the pause instead of exposing stale results",rows->count()==0&&!preview->isEnabled());
      const bool found=waitFor([&]{return rows->count()==4&&preview->isEnabled();},5000);
      observations.append(QJsonObject{{"name","first automatic search including typing and debounce"},{"elapsedMs",elapsed.elapsed()}});
      check("typing pause automatically returns source, bibliography, class and text matches",found);
      QSet<QString> paths;for(int i=0;i<rows->count();++i)paths.insert(QFileInfo(rows->item(i)->data(Qt::UserRole).toString()).absoluteFilePath());
      check("search preserves existing file scope and skips generated, hidden and non-UTF8 content",paths==QSet<QString>{child,bib,style,plain});
      check("normal result notice is readable and Find stays hidden",notice->isVisible()&&!notice->text().isEmpty()&&notice->height()>=notice->fontMetrics().height()&&!find->isVisible());capture(d,"project-search-results");
      query->setText("not present anywhere");check("changing the query immediately removes old actionable rows",rows->count()==0&&!preview->isEnabled());key(query,Qt::Key_Return);
      check("Enter starts the pending query without opening replacement",waitFor([&]{return notice->text()==words("%1 处匹配","%1 matches").arg(0);},5000)&&d->isVisible()&&rows->count()==0&&!preview->isEnabled());
      for(int i=0;i<60;++i){query->setText(i%2?"needle":"not present anywhere");key(query,Qt::Key_Return);}query->setText(QString::fromUtf8("术语"));key(query,Qt::Key_Return);
      check("rapid requests finish with only the latest Chinese query",waitFor([&]{return rows->count()==1&&preview->isEnabled()&&rows->item(0)->text().contains(QString::fromUtf8("术语"));},5000));settle(350);
      check("superseded results never replace the final result later",rows->count()==1&&rows->item(0)->text().contains(QString::fromUtf8("术语")));
      query->clear();check("clearing search cancels immediately and leaves no stale notice",rows->count()==0&&!preview->isEnabled()&&notice->text().isEmpty()&&!notice->isVisible());
      QInputMethodEvent partial("yu",{});partial.setCommitString(QString::fromUtf8("术"));QApplication::sendEvent(query,&partial);settle(400);
      check("synthetic IME partial commit does not scan while preedit remains",query->text()==QString::fromUtf8("术")&&rows->count()==0&&notice->text().isEmpty()&&!preview->isEnabled());
      QInputMethodEvent commit;commit.setCommitString(QString::fromUtf8("语"));QApplication::sendEvent(query,&commit);
      check("synthetic IME completion schedules one current-text search",waitFor([&]{return query->text()==QString::fromUtf8("术语")&&rows->count()==1&&preview->isEnabled();},5000));capture(d,"project-search-ime-committed");
      query->setText("needle");key(query,Qt::Key_Return);key(d,Qt::Key_Escape);
    });
    check("Escape during search keeps project dialog and source selection intact",files&&files->isVisible()&&w->textCursor().position()==8&&w->textCursor().anchor()==26&&unchanged());
    search([&](QDialog *d){
      if(!controls(d)){check("search controls survive reopening",false);return;}
      auto *query=d->findChild<QLineEdit*>("researchProjectTextQuery");auto *rows=d->findChild<QListWidget*>("researchProjectTextResults");auto *notice=d->findChild<QLabel*>("researchProjectSearchNotice");auto *find=d->findChild<QPushButton*>("researchProjectSearchRetry");auto *preview=d->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);
      check("reopened search has no result from the cancelled instance",query->text().isEmpty()&&rows->count()==0&&!preview->isEnabled());
      const auto large=paper+"/oversize.txt";QFile sparse(large);const bool created=sparse.open(QIODevice::WriteOnly)&&sparse.resize(64*1024*1024+1);sparse.close();check("private oversized-file failure input created",created);
      query->setText("needle");check("bounded search surfaces a real recoverable error",waitFor([&]{return find->isVisible()&&notice->text()==words("查找范围过大或文件不可读。","Search scope is too large or unreadable.");},5000)&&rows->count()==0&&!preview->isEnabled());capture(d,"project-search-error");
      check("only this fixture's oversized file is removed for retry",QFile::remove(large));find->click();
      check("explicit retry reuses the same query and returns usable results",waitFor([&]{return rows->count()==4&&preview->isEnabled()&&!find->isVisible();},5000));
      query->clear();settle(350);check("empty input stays quiet after pending work completes",rows->count()==0&&!notice->isVisible()&&!preview->isEnabled());
    });
    auto replace=[&](bool accept){
      bool reviewed=false;QTimer confirmation;confirmation.setInterval(15);
      QObject::connect(&confirmation,&QTimer::timeout,&app,[&]{auto *d=files?files->findChild<QDialog*>("researchWorkspaceConfirmDialog"):nullptr;if(!d||!d->isVisible())return;confirmation.stop();reviewed=true;const auto text=d->findChild<QPlainTextEdit*>()->toPlainText();check("replacement still requires an old/new source review",text.contains("needle")&&text.contains("proof")&&text.contains("sections/one.tex"));if(accept)d->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();else key(d,Qt::Key_Escape);});
      search([&](QDialog *d){if(!controls(d))return;auto *query=d->findChild<QLineEdit*>("researchProjectTextQuery");auto *preview=d->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);query->setText("needle");check("automatic results enable the existing replacement preview",waitFor([&]{return preview->isEnabled();},5000));d->findChild<QLineEdit*>("researchProjectReplacement")->setText("proof");confirmation.start();preview->click();});
      confirmation.stop();check(accept?"replacement confirmation was accepted":"replacement preview was cancelled with Escape",reviewed);
    };
    replace(false);check("cancelled automatic-search replacement changes no bytes",unchanged());replace(true);
    check("confirmed replacement updates every matching file and preserves UTF-8 BOM",bytes(main)==source&&bytes(child)==QByteArray(childSource).replace("needle","proof")&&bytes(bib)==QByteArray(bibSource).replace("needle","proof")&&bytes(style)==QByteArray(styleSource).replace("needle","proof")&&bytes(plain)==QByteArray(plainSource).replace("needle","proof"));
    check("one existing project Undo restores the complete replacement",operation("撤销上次文件操作","Undo last file operation")&&unchanged());
    bool redoSeen=false;QTimer redoResponder;redoResponder.setInterval(15);QObject::connect(&redoResponder,&QTimer::timeout,&app,[&]{auto *d=files?files->findChild<QDialog*>("researchWorkspaceConfirmDialog"):nullptr;if(d&&d->isVisible()){redoResponder.stop();redoSeen=true;d->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();}});redoResponder.start();operation("重做文件操作","Redo file operation");redoResponder.stop();check("existing redo still reviews and restores the batch",redoSeen&&bytes(child)==QByteArray(childSource).replace("needle","proof"));operation("撤销上次文件操作","Undo last file operation");check("second Undo remains byte-exact",unchanged());
    // No product delay hook: direct scanner cancellation uses an isolated input
    // and checks the callback frequently enough to interrupt a large file read.
    const auto cancelFolder=folder+"/cancel-scan";put(cancelFolder+"/large.txt",QByteArray(4*1024*1024,'x')+"needle\n");int probes=0;
    const auto cancelledPlan=planProjectReplace(cancelFolder,"needle","proof",[&]{return ++probes>=8;});
    check("scanner cancels inside a bounded file read without a partial replacement plan",cancelledPlan.error=="Cancelled"&&cancelledPlan.files.isEmpty()&&probes<=10);
    search([&](QDialog *d){if(!controls(d))return;auto *query=d->findChild<QLineEdit*>("researchProjectTextQuery");auto *rows=d->findChild<QListWidget*>("researchProjectTextResults");query->setText(QString::fromUtf8("术语"));check("cross-file result is available without Find",waitFor([&]{return rows->count()==1;},5000));if(rows->count()!=1)return;rows->setCurrentRow(0);rows->setFocus();key(rows,Qt::Key_Return);});
    auto *target=TeXDocumentWindow::findDocument(child);check("activating a result opens its actual source line and retains main-file association",target&&target->textCursor().blockNumber()==2&&QFileInfo(target->textDoc()->getRootFilePath())==QFileInfo(main));
    if(target){target->findChild<QAction*>("researchNavigateBack")->trigger();settle(120);}
    check("return from search restores original inverse source selection",w->editor()->hasFocus()&&w->textCursor().position()==8&&w->textCursor().anchor()==26);
    if(files&&files->isVisible())key(files,Qt::Key_Escape);
    for(auto *window:TeXDocumentWindow::documentList())window->textDoc()->setModified(false);for(auto *window:TeXDocumentWindow::documentList())window->close();settle(80);watchdog.stop();save();app.exit(failures?1:0);
  });return app.exec();
}
