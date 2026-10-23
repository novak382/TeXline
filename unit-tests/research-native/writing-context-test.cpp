// Actual application widgets on an inactive desktop; synthetic events, not a human trial.
#include "ActualAuthoringRecording.h"
#include "ResearchProject.h"
#include "ResearchTextAnchor.h"
#include "ResearchWorkflow.h"
#include "ResearchSettings.h"
#include "PDFDocumentWindow.h"
using namespace Research;
static void whenDialog(QWidget *parent,const QString &name,std::function<void(QDialog*)> fn){
 auto *t=new QTimer(parent);QObject::connect(t,&QTimer::timeout,t,[=]{auto *d=parent->findChild<QDialog*>(name);if(!d||!d->isVisible())return;t->stop();t->deleteLater();QTimer::singleShot(22000,d,&QDialog::reject);fn(d);});t->start(15);
}
int main(int argc,char **argv){
 const auto report=argument(1);if(!qEnvironmentVariable("TEXLINE_TEST_DESKTOP").startsWith("TeXlineValidation-"))return 2;
 argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
 QTimer::singleShot(60,&app,[&]{
  const auto folder=QFileInfo(report).absolutePath();QJsonArray checks,measurements;int failures=0;
  auto check=[&](const QString &name,bool pass){checks.append(QJsonObject{{"name",name},{"passed",pass}});failures+=!pass;put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"measurements",measurements},{"devicePixelRatio",qApp->primaryScreen()->devicePixelRatio()},{"realUserTrial",false},{"nativePhysicalIME",false}}).toJson());};
  const auto main=folder+"/paper/main.tex",child=folder+"/paper/lemma.tex",bib=folder+"/paper/refs.bib";
  const QString passage="First argument establishes the bound. 第一段证明。\n\nThe intermediate calculation stays unchanged.\n\nThird argument completes the proof. 最后一步。";
  QString original="\\documentclass{article}\n\\usepackage{amsmath}\n\\newtheorem{theorem}{Theorem}\n\\newtheorem{lemma}{Lemma}\n\\begin{document}\n\\section{研究现场 Research place}\n\\begin{theorem}\\label{thm:main}\nSee \\ref{lem:child} and \\cite{lem:child}.\n\\end{theorem}\n\\begin{proof}\n"+passage+"\nSee \\eqref{eq:main}.\n\\end{proof}\n\\begin{equation}\\label{eq:main}a=b\\end{equation}\n\\input{lemma}\n\\bibliography{refs}\n\\end{document}\n";
  const QString childText="% !TEX root = main.tex\n\\section{Supporting result}\n\\begin{lemma}\\label{lem:child}A supporting result.\\end{lemma}\nWe apply \\ref{thm:main}.\n";
  const QString bibText="@article{lem:child,title={Research reference 文献},author={Hu, Jieyang},year={2026}}\n";
  put(main,original.toUtf8());put(child,childText.toUtf8());put(bib,bibText.toUtf8());
  Tw::Settings().setValue("openPDFwithTeX",false);Tw::Settings().setValue("researchTypewriterMode",false);
  auto *w=new TeXDocumentWindow(main);w->resize(qEnvironmentVariableIsSet("TEXLINE_TEST_HALF")?620:1040,760);w->show();settle(600);QApplication::setActiveWindow(w);w->editor()->setFocus();
  auto select=[&](int a,int b){QTextCursor c(w->textDoc());c.setPosition(a);c.setPosition(b,QTextCursor::KeepAnchor);w->editor()->setTextCursor(c);};
  auto action=[&](const char *name){auto *a=w->findChild<QAction*>(name);check(QString("command exists: ")+name,a);if(a)a->trigger();};
  int start=int(original.indexOf(passage));select(start+15,start);const auto anchor=w->textCursor().anchor(),position=w->textCursor().position();
  action("researchRelatedAction");QPointer<QDialog> readingDialog=w->findChild<QDialog*>("researchRelatedReading");auto *reading=readingDialog?readingDialog->findChild<QTextBrowser*>("researchRelatedContents"):nullptr;
  check("unlabelled proof context finds outgoing formula",reading&&waitFor([&]{return reading->toHtml().contains("label:eq:main");}));
  if(reading){readingDialog->grab().save(folder+"/context-proof.png");QMetaObject::invokeMethod(reading,"anchorClicked",Q_ARG(QUrl,QUrl("label:thm:main")));
    check("theorem context includes outgoing labels and bibliography",waitFor([&]{const auto html=reading->toHtml();return html.contains("label:lem:child")&&html.contains("cite:lem:child")&&html.contains("lemma.tex");}));
    put(folder+"/context-layout.html",reading->toHtml().toUtf8());readingDialog->grab().save(folder+"/context-theorem.png");QMetaObject::invokeMethod(reading,"anchorClicked",Q_ARG(QUrl,QUrl("cite:lem:child")));
    check("citation and label with same key remain distinct",waitFor([&]{return reading->toPlainText().contains("Research reference");}));
    readingDialog->grab().save(folder+"/context-citation.png");action("researchNavigateBack");settle();check("Back restores previous reading object",reading->toPlainText().contains("thm:main")&&!referenceReadingState(w).value("citation").toBool());
    action("researchNavigateForward");settle();check("Forward restores citation context",referenceReadingState(w).value("citation").toBool());
    key(readingDialog,Qt::Key_Escape);settle();check("Escape preserves reverse source selection",w->textCursor().anchor()==anchor&&w->textCursor().position()==position);
  }
  // Enter the child definition through the contextual chain, then return.
  select(int(original.indexOf("See \\ref")),int(original.indexOf("See \\ref")));action("researchRelatedAction");readingDialog=w->findChild<QDialog*>("researchRelatedReading");reading=readingDialog->findChild<QTextBrowser*>("researchRelatedContents");
  check("reading reloads on demand",waitFor([&]{return reading->toHtml().contains("label:lem:child");}));
  QMetaObject::invokeMethod(reading,"anchorClicked",Q_ARG(QUrl,QUrl("label:lem:child")));check("child definition context loaded",waitFor([&]{return reading->toPlainText().contains("A supporting result");}));
  QMetaObject::invokeMethod(reading,"anchorClicked",Q_ARG(QUrl,QUrl("source:0")));settle();auto *cw=TeXDocumentWindow::findDocument(child);
  check("cross-file contextual source opens exact definition",cw&&cw->textCursor().position()==childText.indexOf("\\begin{lemma}"));
  if(cw)cw->findChild<QAction*>("researchNavigateBack")->trigger();settle();check("cross-file Back restores reading dialog",readingDialog&&readingDialog->isVisible());if(readingDialog)key(readingDialog,Qt::Key_Escape);settle();
  if(cw){QTextCursor childCursor(cw->textDoc());childCursor.setPosition(int(childText.indexOf("\\begin{lemma}")));cw->editor()->setTextCursor(childCursor);rememberReadingPlace(cw);cw->close();settle();const QString prefix="% Later local update before the closed location.\n";put(child,(prefix+childText).toUtf8());action("researchNavigateBack");settle();cw=TeXDocumentWindow::findDocument(child);check("closed-file Back relocates after content change",cw&&cw->textCursor().position()==prefix.size()+childText.indexOf("\\begin{lemma}")&&!cw->textCursor().hasSelection());if(cw)cw->close();settle();put(child,childText.toUtf8());w->selectWindow();}
  select(start+int(passage.size()),start);pinSourceReference(w,"thm:main");settle(500);auto saved=captureWritingPlace(w);QString error;
  whenDialog(w,"researchWritingContextsDialog",[&](QDialog *d){d->findChild<QLineEdit*>("researchSavedWritingName")->setText("证明现场 Proof session");check("saved records ready before saving",waitFor([&]{return d->findChild<QPushButton*>("researchSavedWritingSave")->isEnabled();}));d->findChild<QPushButton*>("researchSavedWritingSave")->click();check("named place saved",waitFor([&]{return d->findChild<QListWidget*>("researchSavedWritingList")->count()==1;}));d->grab().save(folder+"/writing-place.png");key(d,Qt::Key_Escape);});action("researchWritingContexts");
  QTextCursor prefix(w->textDoc());prefix.setPosition(0);prefix.insertText("% Independent prefix inserted before the saved range.\n");const int shift=prefix.position();select(0,0);
  whenDialog(w,"researchWritingContextsDialog",[&](QDialog *d){auto *restore=d->findChild<QPushButton*>("researchSavedWritingRestore");check("place remains available after text edits",waitFor([&]{return restore->isEnabled();}));restore->click();});action("researchWritingContexts");settle(400);
  check("named place maps reverse selection after prefix insertion",w->textCursor().position()==start+shift&&w->textCursor().anchor()==start+shift+passage.size());
  check("named place restores pinned object",referenceReadingState(w).value("pinned").toBool()&&referenceReadingState(w).value("pinKey").toString()=="thm:main");
  const auto pdfFile=folder+"/paper/reading.pdf";{QPdfWriter writer(pdfFile);QPainter painter(&writer);for(int page=0;page<3;++page){if(page)writer.newPage();painter.drawText(100,100,QString("Reading page %1").arg(page+1));}}
  auto pdfState=captureWritingPlace(w);pdfState.insert("pdf",pdfFile);pdfState.insert("pdfVisible",true);pdfState.insert("zoom",1.37);pdfState.insert("pdfX",250.);pdfState.insert("pdfY",900.);
  check("saved PDF view opens",restoreWritingPlace(pdfState,&error));settle(400);auto *savedPdf=PDFDocumentWindow::findDocument(pdfFile);
  check("initial PDF scale does not overwrite saved zoom",savedPdf&&qAbs(savedPdf->widget()->zoomLevel()-1.37)<.001);
  if(savedPdf){const auto center=savedPdf->widget()->mapToScene(savedPdf->widget()->viewport()->rect().center());savedPdf->widget()->setZoomLevel(.8);savedPdf->widget()->centerOn(200,100);restoreWritingPlace(pdfState,&error);settle();const auto after=savedPdf->widget()->mapToScene(savedPdf->widget()->viewport()->rect().center());put(folder+"/pdf-restore-trace.json",QJsonDocument(QJsonObject{{"beforeX",center.x()},{"beforeY",center.y()},{"afterX",after.x()},{"afterY",after.y()},{"viewWidth",savedPdf->widget()->viewport()->width()},{"viewHeight",savedPdf->widget()->viewport()->height()},{"expectedX",pdfState.value("pdfX").toDouble()},{"expectedY",pdfState.value("pdfY").toDouble()}}).toJson());check("PDF reading center restores",QLineF(center,after).length()<3);
    auto next=pdfState;next.insert("pdfY",1400.);restoreWritingPlace(pdfState,&error);restoreWritingPlace(next,&error);settle();check("rapid PDF restores keep latest destination",qAbs(savedPdf->widget()->mapToScene(savedPdf->widget()->viewport()->rect().center()).y()-1400)<3);
    restoreWritingPlace(pdfState,&error);key(savedPdf->widget(),Qt::Key_PageDown);const auto manual=savedPdf->widget()->mapToScene(savedPdf->widget()->viewport()->rect().center());settle();check("PDF input cancels pending position correction",QLineF(manual,savedPdf->widget()->mapToScene(savedPdf->widget()->viewport()->rect().center())).length()<3);savedPdf->close();}
  auto missing=saved;missing.insert("file",folder+"/absent.tex");check("missing scene document fails without guessing",!restoreWritingPlace(missing,&error)&&!error.isEmpty());
  const QString repeated=QString(170,'x')+"body"+QString(170,'y');int a=0,b=0;const auto ambiguous=textAnchor(repeated,170,174);check("ambiguous changed anchor is refused",!locateTextAnchor(repeated+repeated,ambiguous,&a,&b));
  setFocusMode(true);settle(350);const auto focusState=captureWritingPlace(w);setFocusMode(false);settle(350);
  check("focus writing place restores",restoreWritingPlace(focusState,&error));settle(350);check("saved focus layout is restored",w->property("researchFocus").toBool());
  // Create a named alternative, then revise only the selected passage.
  start+=shift;select(start,start+int(passage.size()));
  whenDialog(w,"researchLocalVersionsDialog",[&](QDialog *d){d->findChild<QLineEdit*>("researchSavedWritingName")->setText("原证明 Original proof");check("saved records ready before saving",waitFor([&]{return d->findChild<QPushButton*>("researchSavedWritingSave")->isEnabled();}));d->findChild<QPushButton*>("researchSavedWritingSave")->click();check("local version saved",waitFor([&]{return d->findChild<QListWidget*>("researchSavedWritingList")->count()==1;}));key(d,Qt::Key_Escape);});action("researchLocalVersions");
  const auto beforeRevision=w->textDoc()->editorText();QString changed=passage;changed.replace("First argument","Revised first argument");changed.replace("Third argument","Revised third argument");QTextCursor edit=w->textCursor();edit.insertText(changed);w->editor()->setTextCursor(edit);const auto revised=w->textDoc()->editorText();
  whenDialog(w,"researchLocalVersionsDialog",[&](QDialog *d){auto *restore=d->findChild<QPushButton*>("researchSavedWritingRestore");check("local comparison enables restore",waitFor([&]{return restore->isEnabled();}));auto *text=d->findChild<QTextBrowser*>("researchSavedWritingChanges");check("comparison exposes independent meaningful changes",text->toHtml().contains("restore:1"));d->grab().save(folder+"/local-diff.png");auto *tabs=d->findChild<QTabWidget*>();tabs->setCurrentIndex(1);d->grab().save(folder+"/local-source.png");tabs->setCurrentIndex(0);QMetaObject::invokeMethod(text,"anchorClicked",Q_ARG(QUrl,QUrl("restore:0")));});action("researchLocalVersions");settle();
  check("partial restore leaves the other revision intact",w->textDoc()->editorText().contains("First argument")&&w->textDoc()->editorText().contains("Revised third argument")&&!w->textDoc()->editorText().contains("Revised first argument"));w->editor()->undo();check("partial restore is one undo",w->textDoc()->editorText()==revised);w->editor()->redo();check("partial restore redo",w->textDoc()->editorText()!=revised);w->editor()->undo();
  whenDialog(w,"researchLocalVersionsDialog",[&](QDialog *d){auto *restore=d->findChild<QPushButton*>("researchSavedWritingRestore");check("whole restore ready",waitFor([&]{return restore->isEnabled();}));restore->click();});action("researchLocalVersions");settle();check("whole passage restores without changing surroundings",w->textDoc()->editorText()==beforeRevision);w->editor()->undo();check("whole restore is one undo",w->textDoc()->editorText()==revised);w->editor()->redo();check("whole restore redo",w->textDoc()->editorText()==beforeRevision);
  whenDialog(w,"researchLocalVersionsDialog",[&](QDialog *d){settle();key(d,Qt::Key_Escape);});action("researchLocalVersions");check("Cancel does not modify source",w->textDoc()->editorText()==beforeRevision);
  check("local history retained pre-restore revision",QDir(writingHistoryDirectory()).entryList(QDir::Dirs|QDir::NoDotAndDotDot).size()>1);
  // Typewriter motion is tied to real editor input events, never ordinary navigation.
  setFocusMode(false);settle(300);auto *longWindow=new TeXDocumentWindow();longWindow->resize(w->size());QString longText;for(int n=0;n<6000;++n)longText+=QString("Line %1 研究写作 ").arg(n)+QString(270,'x')+"\n";longWindow->editor()->setPlainText(longText);longWindow->show();QApplication::setActiveWindow(longWindow);auto *editor=longWindow->editor();editor->setFocus();settle(300);
  auto positionAt=[&]{QTextCursor cursor(editor->document()->findBlockByNumber(120));cursor.movePosition(QTextCursor::EndOfBlock);editor->setTextCursor(cursor);editor->ensureCursorVisible();};
  for(bool enabled:{false,true,true,false}){Tw::Settings().setValue("researchTypewriterMode",enabled);editor->refreshTypewriterMode();positionAt();settle();for(int n=0;n<10;++n){key(editor,Qt::Key_A,Qt::NoModifier,"a");settle(1);}QVector<double> samples;
    for(int n=0;n<30;++n){QElapsedTimer timer;timer.start();key(editor,Qt::Key_A,Qt::NoModifier,"a");settle(1);samples.append(timer.nsecsElapsed()/1e6);}std::sort(samples.begin(),samples.end());measurements.append(QJsonObject{{"operation",enabled?"typewriter input 1.7M":"ordinary input 1.7M"},{"medianMs",samples[15]},{"p95Ms",samples[28]},{"maxMs",samples.last()}});
  }
  Tw::Settings().setValue("researchTypewriterMode",true);editor->refreshTypewriterMode();key(editor,Qt::Key_A,Qt::NoModifier,"a");settle();
  check("typewriter current visual line at comfortable height",qAbs(editor->cursorRect().center().y()-qRound(editor->viewport()->height()*.42))<6);
  const int scroll=editor->verticalScrollBar()->value();editor->verticalScrollBar()->setValue(qMax(0,scroll-110));settle();check("manual scrolling is not recentered",editor->verticalScrollBar()->value()==qMax(0,scroll-110));
  auto selection=editor->textCursor();selection.setPosition(selection.position()-10,QTextCursor::KeepAnchor);editor->setTextCursor(selection);const int selectionScroll=editor->verticalScrollBar()->value();settle();check("selection is not recentered",editor->textCursor().hasSelection()&&editor->verticalScrollBar()->value()==selectionScroll);
  selection.clearSelection();editor->setTextCursor(selection);positionAt();settle();QInputMethodEvent preedit(QStringLiteral("中文组合"),{});const auto originalText=editor->toPlainText();QApplication::sendEvent(editor,&preedit);settle();check("IME preedit does not commit content",editor->toPlainText()==originalText);QInputMethodEvent commit;commit.setCommitString(QStringLiteral("中文"));QApplication::sendEvent(editor,&commit);settle();check("IME commit preserved and positioned",editor->toPlainText().contains("中文")&&qAbs(editor->cursorRect().center().y()-qRound(editor->viewport()->height()*.42))<6);
  longWindow->grab().save(folder+"/typewriter.png");
  if(qEnvironmentVariableIsSet("TEXLINE_DESIGN_RECORD")){QDir().mkpath(folder+"/typing-frames");QJsonArray frames;QElapsedTimer recordingClock;recordingClock.start();for(int i=0;i<16;++i){key(editor,Qt::Key_Return,Qt::NoModifier,"\r");settle(40);const auto file=QString("%1.png").arg(i,3,10,QChar('0'));longWindow->grab().save(folder+"/typing-frames/"+file);frames.append(QJsonObject{{"file",file},{"atMs",double(recordingClock.elapsed())}});}put(folder+"/typing-frames/frames.json",QJsonDocument(QJsonObject{{"frames",frames},{"capture","Actual widget rasters, synthetic input, inactive desktop"}}).toJson());}
  Tw::Settings().setValue("researchTypewriterMode",false);editor->refreshTypewriterMode();check("typewriter can be disabled",!settingValue("researchTypewriterMode").toBool());
  check("original project files on disk unchanged",bytes(main)==original.toUtf8()&&bytes(child)==childText.toUtf8()&&bytes(bib)==bibText.toUtf8());
  for(auto *source:TeXDocumentWindow::documentList()){source->textDoc()->setModified(false);source->close();}settle();app.exit(failures?1:0);
 });return app.exec();
}
