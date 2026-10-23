#include "ActualAuthoringRecording.h"
#include "PrefsDialog.h"
#include <QTextBlock>
#include <QTextLayout>
#include <QGlyphRun>
#include <QFontInfo>
using namespace Research;
static QStringList renderedFaces(const QTextBlock &block) {
 // A document-backed QTextLayout can have empty text(); use the block's explicit range.
 block.document()->documentLayout()->blockBoundingRect(block);
 QStringList faces;
 for(const auto &run:block.layout()->glyphRuns(0,block.length()-1))faces.append(run.rawFont().familyName());
 return faces;
}
int main(int argc,char **argv){
 const auto report=argument(1);if(!qEnvironmentVariable("TEXLINE_TEST_DESKTOP").startsWith("TeXlineValidation-"))return 2;
 argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
 QTimer::singleShot(80,&app,[&]{
  const auto folder=QFileInfo(report).absolutePath();QJsonArray checks,details;int failures=0;
  auto check=[&](const QString &name,bool ok){checks.append(QJsonObject{{"name",name},{"passed",ok}});failures+=!ok;put(report,QJsonDocument(QJsonObject{{"checks",checks},{"details",details},{"failures",failures},{"devicePixelRatio",qApp->primaryScreen()->devicePixelRatio()},{"physicalInput",false}}).toJson());};
  QTimer watchdog;watchdog.setSingleShot(true);QObject::connect(&watchdog,&QTimer::timeout,&app,[&]{check("no unexpected modal",false);if(auto *d=qobject_cast<QDialog*>(QApplication::activeModalWidget()))d->reject();app.exit(1);});watchdog.start(120000);
  Tw::Settings settings;settings.setValue("researchFocusMode",false);settings.setValue("openPDFwithTeX",false);
  QString source="\\documentclass{article}\n\\begin{document}\n\\section{Proof 證明}\n";for(int i=0;i<600;++i)source+=QString("Paragraph %1. 中文與 English：完整显示上下笔画。 The argument remains intact after a change of leading.\n").arg(i);source+="\\end{document}\n";
  const auto file=folder+"/paper/main.tex";put(file,source.toUtf8());auto *w=new TeXDocumentWindow(file);w->resize(qEnvironmentVariableIsSet("TEXLINE_TEST_HALF")?620:1120,760);w->show();QApplication::setActiveWindow(w);auto *e=w->editor();e->setFocus();settle(200);
  auto *other=new TeXDocumentWindow();other->hide();settle(80);const auto otherFont=other->editor()->font();const auto otherSpacing=other->lineSpacing();
  QFont initial=e->font();initial.setPointSizeF(15.5);w->setEditorAppearance(initial,132.5);settle(120);const auto initialConsole=w->findChild<QTextEdit*>("textEdit_console")->font();
  auto c=e->textCursor();c.movePosition(QTextCursor::End);c.beginEditBlock();c.insertText("First edit");c.endEditBlock();c.beginEditBlock();c.insertText("Second edit");c.endEditBlock();e->undo();
  const bool initiallyModified=e->document()->isModified();const auto withFirst=e->toPlainText();auto select=e->textCursor();select.setPosition(source.indexOf("Paragraph 300")+30);select.setPosition(source.indexOf("Paragraph 300"),QTextCursor::KeepAnchor);e->setTextCursor(select);e->verticalScrollBar()->setValue(e->verticalScrollBar()->value()+80);settle(60);
  const auto anchor=e->cursorForPosition(QPoint(0,0));const int anchorY=e->cursorRect(anchor).top();const auto savedFont=settings.value("font");const auto savedSpacing=settings.value("lineSpacing");
  const bool baseline=qEnvironmentVariableIsSet("TEXLINE_MENU_BASELINE");
  auto run=[&](const QString &name,const std::function<void(QDialog*)> &action){
   bool opened=false;QTimer::singleShot(120,&app,[&]{auto *d=qobject_cast<PrefsDialog*>(QApplication::activeModalWidget());opened=d;if(!d){check(name+": settings opened",false);return;}d->resize(qEnvironmentVariableIsSet("TEXLINE_TEST_HALF")?620:860,660);QApplication::setActiveWindow(d);settle(60);action(d);});
   PrefsDialog::doPrefsDialog(w,1);check(name+": dialog closes",opened);settle(80);
  };
  w->grab().save(folder+"/appearance-before.png");
  details.append(QJsonObject{{"otherFontBefore",otherFont.toString()},{"otherSpacingBefore",otherSpacing},{"settingsFontBefore",savedFont.toString()},{"settingsSpacingBefore",savedSpacing.toString()}});
  run("untouched",[&](QDialog *d){d->grab().save(folder+"/settings-before.png");d->accept();});
  details.append(QJsonObject{{"otherFontAfter",other->editor()->font().toString()},{"otherSpacingAfter",other->lineSpacing()},{"settingsFontAfter",settings.value("font").toString()},{"settingsSpacingAfter",settings.value("lineSpacing").toString()}});check("unrelated Save preserves fractional font and spacing",e->font()==initial&&qFuzzyCompare(w->lineSpacing(),132.5));check("unrelated Save preserves defaults and other window",settings.value("font")==savedFont&&settings.value("lineSpacing")==savedSpacing&&other->editor()->font()==otherFont&&other->lineSpacing()==otherSpacing);
  if(baseline){check("baseline recorded",true);watchdog.stop();for(auto *x:TeXDocumentWindow::documentList()){x->textDoc()->setModified(false);x->close();}app.exit(failures?1:0);return;}
  Recording record;const bool recording=qEnvironmentVariableIsSet("TEXLINE_DESIGN_RECORD");
  run("cancel preview",[&](QDialog *d){
   if(recording){record.start(folder+"/appearance-recording",d);record.action("Change font size and leading");}
   auto *size=d->findChild<QSpinBox*>("fontSize"),*spacing=d->findChild<QSpinBox*>("lineSpacing");auto *sample=d->findChild<QTextEdit*>("researchEditorAppearanceSample");check("visible live sample",sample&&sample->isVisible());
   size->setValue(19);spacing->setValue(170);settle(90);
   check("controls immediately update the current editor",e->font().pointSize()==19&&w->lineSpacing()==170);check("preview keeps source reverse selection and redo",e->toPlainText()==withFirst&&e->textCursor()==select&&e->document()->isRedoAvailable()&&e->document()->isModified()==initiallyModified);check("preview does not persist preferences",settings.value("font")==savedFont&&settings.value("lineSpacing")==savedSpacing);
   check("sample has enough height for all lines",sample&&sample->viewport()->height()+1>=sample->document()->documentLayout()->documentSize().height());
   if(sample){const auto faces=renderedFaces(sample->document()->lastBlock()),sourceFaces=renderedFaces(e->document()->firstBlock());details.append(QJsonObject{{"sampleFaces",QJsonArray::fromStringList(faces)},{"sourceFaces",QJsonArray::fromStringList(sourceFaces)},{"chosenFace",QFontInfo(e->font()).family()}});check("sample actually paints the selected editor typeface",faces.contains(QFontInfo(e->font()).family()));check("source actually paints the selected editor typeface",sourceFaces.contains(QFontInfo(e->font()).family()));}
   d->grab().save(folder+"/settings-live.png");w->grab().save(folder+"/appearance-live.png");if(recording){settle(350);record.action("Escape restores the original appearance");}key(d,Qt::Key_Escape);if(recording)record.finish();
  });
  check("Cancel preserves modified state",e->document()->isModified()==initiallyModified);check("Escape restores exact appearance and console",e->font()==initial&&qFuzzyCompare(w->lineSpacing(),132.5)&&w->findChild<QTextEdit*>("textEdit_console")->font()==initialConsole);check("Escape restores cursor selection reading anchor and focus",e->textCursor()==select&&qAbs(e->cursorRect(anchor).top()-anchorY)<=1&&e->hasFocus());
  e->redo();check("redo after Cancel still replays second text edit",e->toPlainText()==source+"First editSecond edit");e->undo();e->undo();check("undo after preview restores only the actual text edits",e->toPlainText()==source);e->redo();e->redo();e->undo();
  run("cancel font family",[&](QDialog *d){auto *family=d->findChild<QComboBox*>("editorFont");const int row=family?family->findText("Consolas"):-1;check("alternate font exists for actual-face check",row>=0);if(row>=0){family->setCurrentIndex(row);settle(90);const auto faces=renderedFaces(e->document()->firstBlock());details.append(QJsonObject{{"changedSourceFaces",QJsonArray::fromStringList(faces)}});check("font-family change reaches the real source glyphs",faces.contains("Consolas"));}d->reject();});check("font-family Cancel restores exact original face and redo",e->font()==initial&&e->document()->isRedoAvailable());
  run("close preview",[&](QDialog *d){d->findChild<QSpinBox*>("lineSpacing")->setValue(160);settle(60);d->close();});check("window close restores fractional spacing",qFuzzyCompare(w->lineSpacing(),132.5)&&e->document()->isRedoAvailable());
  run("pending cancel",[&](QDialog *d){d->findChild<QSpinBox*>("fontSize")->setValue(21);d->reject();});check("cancel discards pending preview",e->font()==initial&&qFuzzyCompare(w->lineSpacing(),132.5));
  run("save spacing only",[&](QDialog *d){d->findChild<QSpinBox*>("lineSpacing")->setValue(145);d->accept();});check("Save flushes pending spacing and preserves exact font",w->lineSpacing()==145&&e->font()==initial&&settings.value("lineSpacing").toInt()==145&&settings.value("font")==savedFont&&e->document()->isRedoAvailable());
  run("save font",[&](QDialog *d){d->findChild<QSpinBox*>("fontSize")->setValue(17);d->accept();});check("Save updates current font and new-document default",e->font().pointSize()==17&&settings.value("font").toString()==e->font().toString());
  auto *next=new TeXDocumentWindow();check("new document uses saved display preferences",next->editor()->font().pointSize()==17&&next->lineSpacing()==145);next->close();check("Save preserves modified state",e->document()->isModified()==initiallyModified);check("other existing window remains unchanged",other->editor()->font()==otherFont&&other->lineSpacing()==otherSpacing);
  e->redo();check("redo survives accepted appearance changes",e->toPlainText()==source+"First editSecond edit");e->undo();e->undo();check("accepted preview adds no undo step",e->toPlainText()==source);check("original sample file unchanged",bytes(file)==source.toUtf8());
  watchdog.stop();for(auto *x:TeXDocumentWindow::documentList()){x->textDoc()->setModified(false);x->close();}app.exit(failures?1:0);
 });return app.exec();
}
