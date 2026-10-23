// Speaker-note boundary commits through actual controls and public product APIs.
#include "ActualAuthoringRecording.h"
#include "ResearchSlides.h"
#include "Engine.h"
using namespace Research;

int main(int argc,char **argv) {
  const auto report=argument(1);
  if(!qEnvironmentVariable("TEXLINE_TEST_DESKTOP").startsWith("TeXlineValidation-"))return 2;
  argc=1;QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
  QTimer::singleShot(80,&app,[&]{
    const auto folder=QFileInfo(report).absolutePath();QJsonArray checks,frames;int failures=0;
    auto saveReport=[&]{put(report,QJsonDocument(QJsonObject{{"checks",checks},{"frames",frames},{"failures",failures},{"devicePixelRatio",qApp->primaryScreen()->devicePixelRatio()},{"physicalInput",false}}).toJson());};
    auto check=[&](const QString &name,bool passed){checks.append(QJsonObject{{"name",name},{"passed",passed}});failures+=!passed;saveReport();};
    auto capture=[&](QWidget *widget,const QString &name){QCoreApplication::sendPostedEvents(nullptr,QEvent::LayoutRequest);settle(60);const auto image=widget->grab();const bool saved=image.save(folder+"/"+name+".png");frames.append(QJsonObject{{"file",name+".png"},{"width",widget->width()},{"height",widget->height()},{"devicePixelRatio",image.devicePixelRatio()}});check(name+": real widget captured",saved);};
    QTimer watchdog;watchdog.setSingleShot(true);watchdog.setInterval(180000);QObject::connect(&watchdog,&QTimer::timeout,&app,[&]{check("fixture completes without a blocked workflow",false);if(auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget()))dialog->reject();for(auto *w:TeXDocumentWindow::documentList())if(w->isTypesetting())w->interrupt();app.exit(1);});watchdog.start();
    Tw::Settings().setValue("openPDFwithTeX",false);Tw::Settings().setValue("researchFocusMode",false);Tw::Settings().setValue("researchBuildDestinationPolicy","local-cache");setBuildCacheForTests(folder+"/compile-cache");
    app.setEngineList({Engine("pdfLaTeX","C:/texlive/2023/bin/windows/pdflatex.exe",{"-synctex=1","-interaction=nonstopmode","-file-line-error","$fullname"},true)});
    const QSize windowSize(qEnvironmentVariableIsSet("TEXLINE_TEST_HALF")?620:1120,760);
    const QString source="\\documentclass{beamer}\n\\begin{document}\n\\begin{frame}{Main result}\nThe bound is uniform.\n\\end{frame}\n\\begin{frame}{Proof}\nCenter and truncate.\n\\end{frame}\n\\end{document}\n";
    auto pane=[](TeXDocumentWindow *w){for(auto *p:w->findChildren<QWidget*>("researchSlideNotesPane"))if(p->isVisible())return p;return static_cast<QWidget*>(nullptr);};
    auto ready=[&](TeXDocumentWindow *w){return waitFor([&]{auto *a=w->findChild<QAction*>("researchSlideNotes");auto *list=w->findChild<QListWidget*>("researchSlideList");return w->property("researchSlideMode").toBool()&&a&&a->isEnabled()&&list&&list->count()>0;},10000);};
    auto make=[&](const QString &name,const QString &text){const auto file=folder+"/"+name+"/main.tex";check(name+": independent sample written",put(file,text.toUtf8()));auto *w=new TeXDocumentWindow(file);w->resize(windowSize);w->show();QApplication::setActiveWindow(w);check(name+": slide actions ready",ready(w));return w;};
    auto openNotes=[&](TeXDocumentWindow *w){if(!pane(w)){ready(w);if(auto *action=w->findChild<QAction*>("researchSlideNotes"))action->trigger();}check("notes open as a modeless writing pane",waitFor([&]{return pane(w);},3000)&&!QApplication::activeModalWidget());auto *p=pane(w);return p?p->findChild<QPlainTextEdit*>("researchSlideNote0"):nullptr;};
    auto discard=[&](TeXDocumentWindow *w){if(auto *p=pane(w))if(auto *action=p->findChild<QAction*>("researchDiscardNotes"))action->trigger();settle(100);};
    auto close=[&](TeXDocumentWindow *w){if(hasPendingSlideNotes(w))discard(w);w->textDoc()->setModified(false);w->close();settle(80);};
    auto retainedError=[&](TeXDocumentWindow *w,QPlainTextEdit *editor,const QString &value,const QString &name){auto *p=pane(w);auto *notice=p?p->findChild<QLabel*>("researchSlideNotesNotice"):nullptr;QCoreApplication::sendPostedEvents(nullptr,QEvent::LayoutRequest);settle(40);check(name,hasPendingSlideNotes(w)&&p&&editor&&editor->toPlainText()==value&&editor->hasFocus()&&notice&&notice->isVisible()&&!notice->text().isEmpty()&&notice->contentsRect().height()>=notice->fontMetrics().height());};

    auto *w=make("boundaries",source);auto *note=openNotes(w);
    if(!note){close(w);watchdog.stop();app.exit(1);return;}
    auto *p=pane(w);auto *apply=p->findChild<QPushButton*>("researchApplyNotes");auto *notice=p->findChild<QLabel*>("researchSlideNotesNotice");
    check("normal note editing has no Apply button or redundant explanation",apply&&apply->isHidden()&&notice&&notice->isHidden()&&notice->text().isEmpty());
    note->setFocus();type(note,"First draft. ",0);type(note,"A useful second sentence.",0);
    check("continuous note typing remains a local draft until a boundary",hasPendingSlideNotes(w)&&w->textDoc()->editorText()==source&&w->isWindowModified());capture(w,"notes-draft");
    p->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click();
    const auto committed=w->textDoc()->editorText();check("Hide applies the note and returns focus to source",committed.contains("\\note{First draft. A useful second sentence.}")&&!hasPendingSlideNotes(w)&&!pane(w)&&w->editor()->hasFocus());
    w->editor()->undo();check("one source undo restores the whole continuous note edit",w->textDoc()->editorText()==source);w->editor()->redo();check("one redo restores the complete committed note",w->textDoc()->editorText()==committed);w->editor()->undo();ready(w);
    note=openNotes(w);if(note){note->setPlainText("Esc commits this draft");key(note,Qt::Key_Escape);check("Escape applies notes and collapses the pane",!pane(w)&&!hasPendingSlideNotes(w)&&w->textDoc()->editorText().contains("\\note{Esc commits this draft}")&&w->editor()->hasFocus());w->editor()->undo();ready(w);}
    note=openNotes(w);if(note){note->setPlainText("Switching to Proof");auto *list=w->findChild<QListWidget*>("researchSlideList");list->setCurrentRow(1);check("switching slides commits the previous note and follows the destination",ready(w)&&slideReadingState(w).value("selected").toInt()==1&&w->textDoc()->editorText().contains("\\note{Switching to Proof}")&&pane(w)&&pane(w)->findChild<QPlainTextEdit*>("researchSlideNote0")->toPlainText().isEmpty());w->editor()->undo();ready(w);list->setCurrentRow(0);ready(w);}
    note=openNotes(w);if(note){note->setPlainText("Hide the preview");w->findChild<QAction*>("researchSlidePreviewAction")->trigger();check("hiding the whole preview commits its note draft",w->textDoc()->editorText().contains("\\note{Hide the preview}")&&!hasPendingSlideNotes(w)&&!w->findChild<QDockWidget*>("researchSlidePreviewDock")->isVisible());w->editor()->undo();ready(w);}
    note=openNotes(w);if(note){note->setPlainText("Normal source mode");w->findChild<QAction*>("researchSlideModeAction")->setChecked(false);check("leaving slide mode commits the draft",!w->property("researchSlideMode").toBool()&&!hasPendingSlideNotes(w)&&w->textDoc()->editorText().contains("\\note{Normal source mode}"));w->editor()->undo();check("mode-exit commit is one exact undo",w->textDoc()->editorText()==source);w->findChild<QAction*>("researchSlideModeAction")->setChecked(true);ready(w);}
    note=openNotes(w);if(note){note->setPlainText("Go to preamble");auto *menu=w->findChild<QMenu*>("researchSlidePanelMenu");auto *entry=menu?menu->findChild<QAction*>("researchSourcePreambleMenuAction"):nullptr;if(entry)entry->trigger();check("source scope navigation commits note edits",entry&&!hasPendingSlideNotes(w)&&w->textDoc()->editorText().contains("\\note{Go to preamble}"));w->editor()->undo();ready(w);}
    close(w);

    const QString rapid=QString(source).replace("\\end{document}","\\begin{frame}{Conclusion}\nThe last destination.\n\\end{frame}\n\\end{document}");
    w=make("latest-navigation",rapid);note=openNotes(w);
    if(note){const QString draft=QStringLiteral("An extended note. ").repeated(40);note->setPlainText(draft);auto *list=w->findChild<QListWidget*>("researchSlideList");list->setCurrentRow(1);list->setCurrentRow(2);check("latest slide choice supersedes pending note navigation",ready(w)&&slideReadingState(w).value("selected").toInt()==2&&list->currentRow()==2&&w->textDoc()->editorText().count("\\note{")==1&&w->textDoc()->editorText().contains(draft));w->editor()->undo();check("rapid navigation still creates exactly one note edit",w->textDoc()->editorText()==rapid);}
    close(w);

    w=make("quick-save",source);note=openNotes(w);
    if(note){note->setPlainText("First saved note");check("Save commits a note-only draft",w->save()&&!hasPendingSlideNotes(w));QPointer<QPlainTextEdit> sameEditor(note);note->insertPlainText(" continued immediately");check("Save again before parsing uses the new source snapshot",w->save()&&!hasPendingSlideNotes(w)&&w->textDoc()->editorText().contains("continued immediately"));const auto after=w->textDoc()->editorText();const auto cursor=note->textCursor();check("background refresh preserves the note editor and its caret",ready(w)&&sameEditor&&pane(w)&&pane(w)->findChild<QPlainTextEdit*>("researchSlideNote0")==sameEditor&&sameEditor->textCursor()==cursor);check("normal save persists the latest note",bytes(w->fileName())==after.toUtf8());w->editor()->undo();check("second save is a distinct single-step note edit",w->textDoc()->editorText().contains("\\note{First saved note}")&&!w->textDoc()->editorText().contains("continued immediately"));}
    close(w);

    const QString multiple=QString(source).replace("The bound is uniform.\n","The bound is uniform.\n\\note[item]{First original}\n\\note{Second original}\n");
    w=make("multiple-notes",multiple);note=openNotes(w);
    if(note){p=pane(w);auto *second=p->findChild<QPlainTextEdit*>("researchSlideNote1");check("multiple existing note blocks remain separately editable",second);if(second){note->setPlainText("First revised");second->setPlainText("Second revised");p->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click();check("both notes commit while preserving the note option",w->textDoc()->editorText().contains("\\note[item]{First revised}")&&w->textDoc()->editorText().contains("\\note{Second revised}"));w->editor()->undo();check("one undo restores both original note blocks exactly",w->textDoc()->editorText()==multiple);ready(w);note=openNotes(w);if(note){note->moveCursor(QTextCursor::End);note->insertPlainText(" transient");note->undo();const int revision=w->textDoc()->revision();check("undoing a draft back to its baseline is no pending edit",!hasPendingSlideNotes(w)&&commitSlideNotes(w)&&w->textDoc()->revision()==revision);}}}
    close(w);

    w=make("failure-recovery",source);note=openNotes(w);
    if(note){
      note->setPlainText("Unbalanced {");auto selection=note->textCursor();selection.setPosition(10);selection.setPosition(3,QTextCursor::KeepAnchor);note->setTextCursor(selection);key(note,Qt::Key_Escape);
      retainedError(w,note,"Unbalanced {","invalid braces retain draft, notice and focus");check("invalid-boundary failure preserves reverse selection and source",note->textCursor()==selection&&w->textDoc()->editorText()==source);
      auto *list=w->findChild<QListWidget*>("researchSlideList");list->setCurrentRow(1);check("invalid notes cannot silently switch to another slide",list->currentRow()==0&&slideReadingState(w).value("selected").toInt()==0);
      auto *previewAction=w->findChild<QAction*>("researchSlidePreviewAction");previewAction->trigger();check("invalid notes cannot be hidden with their whole preview",w->findChild<QDockWidget*>("researchSlidePreviewDock")->isVisible()&&previewAction->isChecked());
      w->findChild<QAction*>("researchSlideModeAction")->setChecked(false);check("invalid notes keep slide mode and its draft editor",w->property("researchSlideMode").toBool()&&note->isVisible());
      check("invalid note blocks closing before any save dialog",!w->close()&&w->isVisible()&&!QApplication::activeModalWidget());capture(w,"notes-invalid");
      discard(w);check("Discard explicitly abandons only the note draft",!hasPendingSlideNotes(w)&&w->textDoc()->editorText()==source&&pane(w)&&pane(w)->findChild<QPlainTextEdit*>("researchSlideNote0")->toPlainText().isEmpty());
      note=pane(w)->findChild<QPlainTextEdit*>("researchSlideNote0");note->setPlainText("Read-only draft");w->editor()->setReadOnly(true);key(note,Qt::Key_Escape);retainedError(w,note,"Read-only draft","read-only source leaves an actionable note draft");w->editor()->setReadOnly(false);key(note,Qt::Key_Escape);check("retry after making the source writable commits the same draft",!hasPendingSlideNotes(w)&&w->textDoc()->editorText().contains("\\note{Read-only draft}"));w->editor()->undo();ready(w);
      note=openNotes(w);if(note){note->setPlainText("Keep this changed-source draft");auto cursor=w->textCursor();cursor.movePosition(QTextCursor::End);cursor.insertText("\n% Concurrent source edit\n");const auto changed=w->textDoc()->editorText();check("source change is detected before writing any note",!commitSlideNotes(w)&&w->textDoc()->editorText()==changed);retainedError(w,note,"Keep this changed-source draft","source change retains the exact draft and focus");capture(w,"notes-source-changed");discard(w);check("discard after a source change preserves the concurrent edit",ready(w)&&!hasPendingSlideNotes(w)&&w->textDoc()->editorText()==changed&&pane(w)&&pane(w)->findChild<QPlainTextEdit*>("researchSlideNote0")->toPlainText().isEmpty());}
    }close(w);

    w=make("compile",source);note=openNotes(w);
    if(note){
      note->setPlainText("Compile-ready note");w->findChild<QAction*>("actionTypeset")->trigger();const bool idle=waitFor([&]{return !w->isTypesetting()&&!w->property("researchBuildPreparing").toBool()&&!w->property("researchBuildFinishing").toBool()&&!w->property("researchPublishing").toBool();},90000);put(folder+"/notes-compile.txt",w->property("consoleOutput").toString().toUtf8());const auto pdf=previewFile(w->fileName());auto document=QtPDF::Backend::Document::newDocument(pdf);
      check("normal Compile includes notes and publishes the current real PDF",idle&&!hasPendingSlideNotes(w)&&bytes(w->fileName()).contains("\\note{Compile-ready note}")&&pdfPairValid(pdf)&&pdfRevision(pdf,w->fileName())==PdfRevision::Current&&document&&document->isValid()&&contains(document,"The bound is uniform."));capture(w,"notes-compiled");
      note=openNotes(w);auto *preview=w->findChild<QtPDF::PDFDocumentView*>("researchSlidePreview");
      check("actual compiled PDF is mapped for page navigation",preview&&waitFor([&]{return preview->document().toStrongRef()&&w->findChild<QSpinBox*>("researchSlideStep")->isEnabled();},5000));
      if(note&&preview){note->setPlainText("A final note before reading the next slide");preview->setFocus();preview->goToPage(1);check("PDF page navigation commits the previous note and follows the viewed slide",waitFor([&]{return !hasPendingSlideNotes(w)&&slideReadingState(w).value("selected").toInt()==1;},10000)&&w->textDoc()->editorText().contains("\\note{A final note before reading the next slide}")&&pane(w)&&pane(w)->findChild<QPlainTextEdit*>("researchSlideNote0")->toPlainText().isEmpty());}
    }
    close(w);

    auto closeAnswer=[&](TeXDocumentWindow *target,QMessageBox::StandardButton answer){bool prompted=false;QTimer responder;responder.setInterval(20);QObject::connect(&responder,&QTimer::timeout,&app,[&]{if(auto *dialog=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())){prompted=true;responder.stop();if(auto *button=dialog->button(answer))button->click();else dialog->reject();}});responder.start();const bool accepted=target->close();responder.stop();settle(80);check("normal close uses the existing source-save decision",prompted);return accepted;};
    w=make("close",source);note=openNotes(w);
    if(note){note->setPlainText("Close-boundary note");check("cancelling close retains the committed note and source window",!closeAnswer(w,QMessageBox::Cancel)&&w->isVisible()&&w->textDoc()->editorText().contains("\\note{Close-boundary note}")&&!hasPendingSlideNotes(w));const auto file=w->fileName();QPointer<TeXDocumentWindow> guard(w);check("Save on close persists notes and closes the same window",closeAnswer(w,QMessageBox::Save)&&!guard&&bytes(file).contains("\\note{Close-boundary note}"));}else close(w);

    const auto root=folder+"/multi/main.tex",part=folder+"/multi/part.tex";const QString rootSource="\\documentclass{beamer}\n\\begin{document}\n\\input{part}\n\\end{document}\n",partSource="% !TeX root = main.tex\n\\begin{frame}{Child slide}\nChild source content.\n\\end{frame}\n";check("cross-file samples written",put(root,rootSource.toUtf8())&&put(part,partSource.toUtf8()));
    auto *child=new TeXDocumentWindow(part);child->resize(windowSize);child->show();auto *owner=new TeXDocumentWindow(root);owner->resize(windowSize);owner->show();QApplication::setActiveWindow(owner);check("cross-file deck is ready",ready(owner));note=openNotes(owner);
    if(note){note->setPlainText("Child-file speaker note");QPointer<TeXDocumentWindow> childGuard(child);check("closing a child commits notes held by its presentation owner",closeAnswer(child,QMessageBox::Save)&&!childGuard&&bytes(part).contains("\\note{Child-file speaker note}")&&bytes(root)==rootSource.toUtf8()&&!hasPendingSlideNotes(owner));}else close(child);close(owner);
    watchdog.stop();saveReport();app.exit(failures?1:0);
  });return app.exec();
}
