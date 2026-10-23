// Automatic previews and coupled source edits through real public UI paths.
#include "ActualAuthoringRecording.h"
#include "ResearchSlideExport.h"
#include "ResearchProject.h"
#include "ResearchWorkflow.h"
#include "utils/FilePath.h"
#include "Engine.h"
#include <QDirIterator>
using namespace Research;

int main(int argc,char **argv){
  const auto report=argument(1);wchar_t desktop[256]{};DWORD needed=0;
  if(!GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()),UOI_NAME,desktop,sizeof(desktop),&needed)||QString::fromWCharArray(desktop)!=qEnvironmentVariable("TEXLINE_TEST_DESKTOP")||!QString::fromWCharArray(desktop).startsWith("TeXlineValidation-"))return 2;
  argc=1;QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
  QTimer::singleShot(80,&app,[&]{
    const auto folder=QFileInfo(report).absolutePath(),paper=folder+"/paper",root=paper+"/main.tex",child=paper+"/sections/part.tex",asset=folder+"/external/plot{one}%#.png";
    const QString rootSource="\\documentclass{article}\n\\begin{document}\n\\input{sections/part}\n\\end{document}\n",childSource="% !TEX root = ../main.tex\nA stable opening.\n";
    put(root,rootSource.toUtf8());put(child,childSource.toUtf8());QDir().mkpath(QFileInfo(asset).absolutePath());
    QImage picture(360,180,QImage::Format_RGB32);picture.fill(Qt::white);{QPainter p(&picture);p.setPen(QPen(QColor("#486d81"),3));p.drawLine(20,150,330,20);p.drawLine(20,150,330,150);p.drawText(QRect(50,65,240,40),Qt::AlignCenter,"Independent image");}picture.save(asset);const auto assetHash=slideExportHash(asset);
    QJsonArray checks,frames;int failures=0;auto save=[&]{put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"frames",frames},{"devicePixelRatio",qApp->primaryScreen()->devicePixelRatio()},{"physicalInput",false},{"systemIme",false},{"nativeFileDialog",false}}).toJson());};
    auto check=[&](const QString &name,bool pass){checks.append(QJsonObject{{"name",name},{"passed",pass}});failures+=!pass;save();};
    auto capture=[&](QWidget *widget,const QString &name){QCoreApplication::sendPostedEvents(nullptr,QEvent::LayoutRequest);settle(80);const auto image=widget->grab();const bool saved=image.save(folder+"/"+name+".png");frames.append(QJsonObject{{"file",name+".png"},{"width",widget->width()},{"height",widget->height()},{"devicePixelRatio",image.devicePixelRatio()},{"saved",saved}});check(name+": real widget captured",saved);};
    QTimer watchdog;watchdog.setSingleShot(true);watchdog.setInterval(240000);QObject::connect(&watchdog,&QTimer::timeout,&app,[&]{check("fixture completes without unexpected modal",false);if(auto *d=qobject_cast<QDialog*>(QApplication::activeModalWidget()))d->reject();app.exit(1);});watchdog.start();
    Tw::Settings().setValue("openPDFwithTeX",false);Tw::Settings().setValue("researchFocusMode",false);setBuildCacheForTests(folder+"/cache");
    app.setEngineList({Engine("pdfLaTeX","C:/texlive/2023/bin/windows/pdflatex.exe",{"-synctex=1","-interaction=nonstopmode","-file-line-error","$fullname"},true)});
    const QSize size(qEnvironmentVariableIsSet("TEXLINE_TEST_HALF")?620:1120,740);
    auto *main=new TeXDocumentWindow(root);main->resize(size);main->show();rememberRoot(child,root);auto *w=new TeXDocumentWindow(child);w->resize(size);w->show();QApplication::setActiveWindow(w);w->editor()->setFocus();settle(180);
    check("fixture is a real child without a document environment and a known root",!w->textDoc()->editorText().contains("\\begin{document}")&&QFileInfo(w->textDoc()->getRootFilePath())==QFileInfo(root));
    auto place=[&]{auto c=w->textCursor();c.setPosition(w->textDoc()->editorText().size());w->editor()->setTextCursor(c);};place();
    auto open=[&](bool table){w->findChild<QAction*>(table?"researchInsertTable":"researchInsertFigure")->trigger();QPointer<QDialog> d=w->findChild<QDialog*>("researchObjectDialog");if(d){d->resize(size.width()-20,700);settle(70);}check(table?"real table dialog opens":"real figure dialog opens",d&&d->isVisible());return d;};
    auto ready=[](QDialog *d){auto *image=d->findChild<QLabel*>("researchObjectPreview");return image&&image->property("researchDraftReady").toBool();};
    auto awaitPreview=[&](QDialog *d,const QString &name){auto *retry=d->findChild<QPushButton*>("researchObjectPreviewButton");const bool completed=waitFor([&]{return ready(d)||(retry->isVisible()&&retry->isEnabled());},60000);const bool ok=completed&&ready(d);check(name,ok);if(!ok)put(folder+"/"+name+"-log.txt",d->findChild<QLabel*>("researchObjectNotice")->toolTip().toUtf8());return ok;};
    auto dismiss=[&](QPointer<QDialog> d){if(d)key(d,Qt::Key_Escape);check("Escape closes the temporary editor and restores writing focus",waitFor([&]{return (!d||!d->isVisible())&&w->editor()->hasFocus();},3000));settle(60);};
    auto choose=[&](QDialog *owner){bool selected=false;QTimer response,timeout;response.setInterval(15);timeout.setSingleShot(true);timeout.setInterval(6000);QObject::connect(&response,&QTimer::timeout,&app,[&]{auto *picker=qobject_cast<QFileDialog*>(QApplication::activeModalWidget());if(!picker)return;response.stop();selected=true;picker->selectFile(asset);QMetaObject::invokeMethod(picker,"accept",Qt::QueuedConnection);});QObject::connect(&timeout,&QTimer::timeout,&app,[&]{if(auto *picker=qobject_cast<QFileDialog*>(QApplication::activeModalWidget()))picker->reject();});response.start();timeout.start();owner->findChild<QToolButton*>("researchFigureChoose")->click();response.stop();timeout.stop();check("original chooser selects a private image with TeX special characters",selected);};
    auto aliases=[] {return QDir(QDir::tempPath()).entryList({"texline-assets-*"},QDir::Dirs|QDir::NoDotAndDotDot);};const auto beforeAliases=aliases();
    auto d=open(false);if(!d){watchdog.stop();app.exit(1);return;}
    check("normal figure editing does not require Update preview",!d->findChild<QPushButton*>("researchObjectPreviewButton")->isVisible());choose(d);
    auto *caption=d->findChild<QPlainTextEdit*>("researchObjectCaption");auto *width=d->findChild<QDoubleSpinBox*>("researchFigureWidth");
    caption->setPlainText("First draft");width->setValue(65);awaitPreview(d,"figure previews automatically without Update");
    auto *draft=d->findChild<QPlainTextEdit*>("researchObjectSource");caption->setPlainText("Superseded draft");width->setValue(70);settle(480);caption->setPlainText("Final compact figure");width->setValue(45);
    check("a new figure edit invalidates the previous preview immediately",!ready(d));awaitPreview(d,"continuous edits finish with a current preview");
    check("latest figure draft preserves the final caption and width",draft->toPlainText().contains("\\caption{Final compact figure}")&&draft->toPlainText().contains("width=0.45\\linewidth")&&!draft->toPlainText().contains("Superseded"));capture(d,"figure-auto-final");
    bool privateAlias=false;for(const auto &name:aliases())if(!beforeAliases.contains(name)){QDirIterator it(QDir::tempPath()+"/"+name,{"*.png"},QDir::Files,QDirIterator::Subdirectories);while(it.hasNext()){it.next();if(slideExportHash(it.filePath())==assetHash)privateAlias=true;}}
    check("special-character preview uses a private image alias without importing into the project",privateAlias&&!QFileInfo::exists(paper+"/figures")&&bytes(root)==rootSource.toUtf8()&&bytes(child)==childSource.toUtf8());
    auto *toggle=d->findChild<QToolButton*>("researchObjectPreviewToggle");toggle->click();caption->setPlainText("Hidden pending draft");settle(650);
    check("hidden preview cancels pending work and never publishes a current image",!toggle->isChecked()&&!ready(d)&&!d->findChild<QWidget*>("researchObjectPreviewPane")->isVisible());dismiss(d);
    check("Cancel imports nothing and leaves both manuscripts and the source image exact",!QFileInfo::exists(paper+"/figures")&&w->textDoc()->editorText()==childSource&&main->textDoc()->editorText()==rootSource&&slideExportHash(asset)==assetHash);

    d=open(false);if(d){choose(d);d->findChild<QPlainTextEdit*>("researchObjectCaption")->setPlainText("Applied figure");d->findChild<QDialogButtonBox*>("researchObjectButtons")->button(QDialogButtonBox::Apply)->click();check("Apply closes the editor and returns focus to the requesting child",waitFor([&]{return (!d||!d->isVisible())&&w->editor()->hasFocus();},3000));}
    const auto childAfter=w->textDoc()->editorText(),rootAfter=main->textDoc()->editorText();const auto relative="figures/figure-"+QString::fromLatin1(assetHash.toHex().left(16))+".png";
    check("Apply imports one safe resource and leaves the external original untouched",childAfter.contains(relative)&&slideExportHash(paper+"/"+relative)==assetHash&&slideExportHash(asset)==assetHash);
    check("required package is inserted only into the existing main preamble",rootAfter.count("\\usepackage{graphicx}")==1&&rootAfter.indexOf("\\usepackage{graphicx}")<rootAfter.indexOf("\\begin{document}")&&!childAfter.contains("\\usepackage")&&childAfter.contains("Applied figure"));
    w->editor()->undo();check("one ordinary child Undo restores the object and root package together",w->textDoc()->editorText()==childSource&&main->textDoc()->editorText()==rootSource);w->editor()->redo();check("one ordinary child Redo restores both native source edits",w->textDoc()->editorText()==childAfter&&main->textDoc()->editorText()==rootAfter);
    QTextCursor later(main->textDoc());later.movePosition(QTextCursor::End);later.beginEditBlock();later.insertText("% Later independent main edit\n");later.endEditBlock();const auto rootLater=main->textDoc()->editorText();
    w->editor()->undo();check("coupled Undo refuses to erase a later edit in the related main file",w->textDoc()->editorText()==childAfter&&main->textDoc()->editorText()==rootLater);
    main->editor()->undo();w->editor()->undo();check("undoing the later main edit makes the original group safely undoable",w->textDoc()->editorText()==childSource&&main->textDoc()->editorText()==rootSource);
    check("coupled history leaves unsaved source changes off disk",bytes(root)==rootSource.toUtf8()&&bytes(child)==childSource.toUtf8());place();

    d=open(true);if(d){auto *grid=d->findChild<QTableWidget*>("researchTableGrid");auto *preview=d->findChild<QToolButton*>("researchObjectPreviewToggle");auto *source=d->findChild<QPlainTextEdit*>("researchObjectSource");
      if(preview->isChecked())preview->click();grid->item(0,0)->setText("Alpha");grid->item(1,0)->setText("Beta");preview->click();awaitPreview(d,"table previews automatically when shown");
      preview->click();grid->item(1,0)->setText("Superseded cell");grid->item(1,0)->setText("Final table cell");preview->click();awaitPreview(d,"table publishes its latest edited cells without Update");
      check("table draft contains the final cells and no superseded cell",source->toPlainText().contains("Final table cell")&&!source->toPlainText().contains("Superseded cell")&&!d->findChild<QPushButton*>("researchObjectPreviewButton")->isVisible());capture(d,"table-auto-final");
      d->findChild<QToolButton*>("researchObjectSourceTab")->click();source->setPlainText("\\begin{table}\n\\UnknownAutoPreviewCommand\n\\end{table}");if(!preview->isChecked())preview->click();
      auto *retry=d->findChild<QPushButton*>("researchObjectPreviewButton");auto *notice=d->findChild<QLabel*>("researchObjectNotice");
      check("real draft compilation failure reveals the retry action and its cause",waitFor([&]{return retry->isVisible()&&retry->isEnabled()&&!ready(d);},60000)&&!notice->text().isEmpty());put(folder+"/table-failure-log.txt",notice->toolTip().toUtf8());capture(d,"table-auto-failure");
      const auto broken=source->toPlainText();retry->click();check("explicit Retry reruns the retained failing draft without applying it",waitFor([&]{return retry->isVisible()&&retry->isEnabled();},60000)&&source->toPlainText()==broken&&w->textDoc()->editorText()==childSource);
      source->setPlainText("\\begin{table}[htbp]\n\\centering\\begin{tabular}{ll}\nRecovered & Final \\\\\n\\end{tabular}\n\\end{table}");awaitPreview(d,"correcting the draft automatically recovers from a preview error");check("successful recovery hides Retry again",!retry->isVisible());capture(d,"table-auto-recovered");
      source->appendPlainText("% Cancel before the next automatic job");dismiss(d);
    }
    check("all table cancellation and failures preserve both real sources",w->textDoc()->editorText()==childSource&&main->textDoc()->editorText()==rootSource&&bytes(root)==rootSource.toUtf8()&&bytes(child)==childSource.toUtf8());
    QTextCursor package(main->textDoc());package.setPosition(rootSource.indexOf("\\begin{document}"));package.beginEditBlock();package.insertText("\\usepackage{graphicx}\n");package.endEditBlock();
    place();d=open(false);if(d){choose(d);d->findChild<QPlainTextEdit*>("researchObjectCaption")->setPlainText("Keep this draft after root edit");const auto expectedDraft=d->findChild<QPlainTextEdit*>("researchObjectSource")->toPlainText();
      QTextCursor remove(main->textDoc());const auto at=main->textDoc()->editorText().indexOf("\\usepackage{graphicx}\n");remove.setPosition(at);remove.setPosition(at+QString("\\usepackage{graphicx}\n").size(),QTextCursor::KeepAnchor);remove.beginEditBlock();remove.removeSelectedText();remove.endEditBlock();
      d->findChild<QDialogButtonBox*>("researchObjectButtons")->button(QDialogButtonBox::Apply)->click();
      check("removing an already-present required package blocks a stale Apply",d&&d->isVisible()&&w->textDoc()->editorText()==childSource&&main->textDoc()->editorText()==rootSource&&d->findChild<QPlainTextEdit*>("researchObjectSource")->toPlainText()==expectedDraft&&!d->findChild<QLabel*>("researchObjectNotice")->text().isEmpty());if(d&&d->isVisible())capture(d,"figure-root-changed-retained");dismiss(d);
    }
    for(auto *window:TeXDocumentWindow::documentList())window->textDoc()->setModified(false);for(auto *window:TeXDocumentWindow::documentList())window->close();settle(120);

    const auto hiddenRoot=folder+"/background/main.tex",hiddenChild=folder+"/background/sections/part.tex";put(hiddenRoot,rootSource.toUtf8());put(hiddenChild,childSource.toUtf8());rememberRoot(hiddenChild,hiddenRoot);
    w=new TeXDocumentWindow(hiddenChild);w->resize(size);w->show();QApplication::setActiveWindow(w);w->editor()->setFocus();settle(100);place();const auto scroll=w->editor()->verticalScrollBar()->value();
    check("background-root sample opens only its child",TeXDocumentWindow::findDocument(hiddenRoot)==nullptr&&QFileInfo(w->textDoc()->getRootFilePath())==QFileInfo(hiddenRoot));
    check("existing pasted-image entry opens the original figure editor",pasteResearchImage(w,picture));d=w->findChild<QDialog*>("researchObjectDialog");
    if(d){d->findChild<QPlainTextEdit*>("researchObjectCaption")->setPlainText("Background preamble");d->findChild<QDialogButtonBox*>("researchObjectButtons")->button(QDialogButtonBox::Apply)->click();check("background-root Apply returns to the same child editor",waitFor([&]{return (!d||!d->isVisible())&&w->editor()->hasFocus();},3000));}
    QPointer<TeXDocumentWindow> background=TeXDocumentWindow::findDocument(hiddenRoot);const auto backgroundChildAfter=w->textDoc()->editorText();
    check("missing main buffer is opened invisibly without replacing the current writing view",background&&!background->isVisible()&&QApplication::activeWindow()==w&&w->editor()->hasFocus()&&w->fileName()==hiddenChild&&w->editor()->verticalScrollBar()->value()==scroll);
    check("background Apply inserts the package in main and the figure only in child",background&&background->textDoc()->editorText().contains("\\usepackage{graphicx}")&&!backgroundChildAfter.contains("\\usepackage")&&backgroundChildAfter.contains("Background preamble"));
    w->editor()->undo();check("ordinary Undo includes the main document opened in the background",background&&background->textDoc()->editorText()==rootSource&&w->textDoc()->editorText()==childSource);w->editor()->redo();check("ordinary Redo restores the background source group without showing main",background&&!background->isVisible()&&background->textDoc()->editorText().contains("\\usepackage{graphicx}")&&w->textDoc()->editorText()==backgroundChildAfter);
    for(auto *window:TeXDocumentWindow::documentList())window->textDoc()->setModified(false);for(auto *window:TeXDocumentWindow::documentList())window->close();settle(120);

    // A genuine recovery record belongs to an unopened, independent main file.
    // Applying an object must not consume it or open a recovery prompt in secret.
    const auto pendingRoot=folder+"/pending/main.tex",pendingChild=folder+"/pending/sections/part.tex";put(pendingRoot,rootSource.toUtf8());put(pendingChild,childSource.toUtf8());rememberRoot(pendingChild,pendingRoot);
    const auto pendingPath=writingHistoryDirectory()+"/"+QString::fromLatin1(QCryptographicHash::hash(Tw::Utils::canonicalSourcePath(pendingRoot).toUtf8(),QCryptographicHash::Sha256).toHex())+".pending.json";
    const auto recovered=QString(rootSource).replace("\\begin{document}","% Recovered independent preamble\n\\begin{document}");
    const auto pendingBytes=QJsonDocument(QJsonObject{{"path",pendingRoot},{"text",recovered},{"cursor",4},{"time",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},{"diskHash",QString::fromLatin1(QCryptographicHash::hash(bytes(pendingRoot),QCryptographicHash::Sha256).toHex())},{"version",1}}).toJson();
    check("a legal private recovery record is created for the unopened main",put(pendingPath,pendingBytes)&&hasPendingRecovery(pendingRoot));
    w=new TeXDocumentWindow(pendingChild);w->resize(size);w->show();QApplication::setActiveWindow(w);w->editor()->setFocus();settle(100);place();
    check("pending recovery sample still opens only its child",TeXDocumentWindow::findDocument(pendingRoot)==nullptr);
    check("pending protection uses the existing pasted-image entry",pasteResearchImage(w,picture));d=w->findChild<QDialog*>("researchObjectDialog");check("pending protection reaches its real visible figure editor",d&&d->isVisible());
    bool unexpectedRecovery=false;QTimer unexpectedPrompt;unexpectedPrompt.setInterval(15);QObject::connect(&unexpectedPrompt,&QTimer::timeout,&app,[&]{auto *box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());auto *owner=box?qobject_cast<TeXDocumentWindow*>(box->parentWidget()):nullptr;if(owner&&QFileInfo(owner->fileName())==QFileInfo(pendingRoot)){unexpectedRecovery=true;box->reject();}});
    if(d){d->findChild<QPlainTextEdit*>("researchObjectCaption")->setPlainText("Retain until recovery is reviewed");const auto retained=d->findChild<QPlainTextEdit*>("researchObjectSource")->toPlainText();unexpectedPrompt.start();d->findChild<QDialogButtonBox*>("researchObjectButtons")->button(QDialogButtonBox::Apply)->click();settle(180);unexpectedPrompt.stop();
      check("unopened main with pending recovery blocks Apply and retains its draft",d&&d->isVisible()&&d->findChild<QPlainTextEdit*>("researchObjectSource")->toPlainText()==retained&&!d->findChild<QLabel*>("researchObjectNotice")->text().isEmpty()&&w->textDoc()->editorText()==childSource);
      check("blocked Apply creates no main window, resource import or background recovery prompt",!TeXDocumentWindow::findDocument(pendingRoot)&&!unexpectedRecovery&&!QFileInfo::exists(folder+"/pending/figures"));
      check("blocked Apply leaves recovery record and both source files byte-identical",bytes(pendingPath)==pendingBytes&&bytes(pendingRoot)==rootSource.toUtf8()&&bytes(pendingChild)==childSource.toUtf8());if(d&&d->isVisible())capture(d,"figure-pending-recovery-retained");dismiss(d);
    }
    bool recoveryShown=false;QTimer recoveryResponse;recoveryResponse.setInterval(15);QObject::connect(&recoveryResponse,&QTimer::timeout,&app,[&]{auto *box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());auto *owner=box?qobject_cast<TeXDocumentWindow*>(box->parentWidget()):nullptr;if(!owner||QFileInfo(owner->fileName())!=QFileInfo(pendingRoot))return;recoveryResponse.stop();recoveryShown=true;for(auto *button:box->buttons())if(box->buttonRole(button)==QMessageBox::AcceptRole){button->click();return;}box->reject();});recoveryResponse.start();
    QPointer<TeXDocumentWindow> explicitMain=TeXDocumentWindow::openDocument(pendingRoot,true,true);
    check("explicitly opening main still offers and restores its exact pending content",waitFor([&]{return recoveryShown&&explicitMain&&explicitMain->textDoc()->editorText()==recovered;},8000)&&explicitMain->isVisible()&&explicitMain->isModified());recoveryResponse.stop();
    check("explicit recovery preserves original disk files and the cancelled child",bytes(pendingRoot)==rootSource.toUtf8()&&bytes(pendingChild)==childSource.toUtf8()&&w->textDoc()->editorText()==childSource);
    if(explicitMain){explicitMain->editor()->undo();check("explicit recovery remains one normal undo",explicitMain->textDoc()->editorText()==rootSource);}
    for(auto *window:TeXDocumentWindow::documentList())window->textDoc()->setModified(false);for(auto *window:TeXDocumentWindow::documentList())window->close();settle(120);watchdog.stop();save();app.exit(failures?1:0);
  });return app.exec();
}
