// Compare the opt-in display leading against Qt's ordinary block formatting.
#include "ActualAuthoringRecording.h"
#include <QAbstractTextDocumentLayout>
int main(int argc,char **argv) {
  const auto report=argument(1);if(!qEnvironmentVariable("TEXLINE_TEST_DESKTOP").startsWith("TeXlineValidation-"))return 2;
  QApplication app(argc,argv);QJsonArray checks;int failures=0;
  auto check=[&](const QString &name,bool ok){checks.append(QJsonObject{{"name",name},{"passed",ok}});failures+=!ok;};
  QTextEdit source,reference;source.resize(470,420);reference.resize(source.size());
  const QString text="中文與 English，完整显示上下笔画。\nA long wrapped proof: "+QString("The estimate holds uniformly. ").repeated(12)+"\n\\[ \\sum_{i=1}^n x_i^2 \\leq n \\]\n最后一行。";
  QFont font("Consolas",14);source.setFont(font);reference.setFont(font);source.setPlainText(text);reference.setPlainText(text);
  source.document()->setProperty("_texline_lazy_edits",true);source.show();reference.show();settle(30);
  auto display=[&](qreal spacing){source.document()->setProperty("_texline_display_line_spacing",spacing);source.document()->markContentsDirty(0,source.document()->characterCount());};
  auto format=[&](qreal spacing){QTextCursor c(reference.document());c.select(QTextCursor::Document);QTextBlockFormat f;f.setLineHeight(spacing,QTextBlockFormat::ProportionalHeight);c.mergeBlockFormat(f);};
  auto same=[&]{source.document()->documentLayout()->documentSize();reference.document()->documentLayout()->documentSize();bool ok=true;auto a=source.document()->begin(),b=reference.document()->begin();for(;a.isValid()&&b.isValid();a=a.next(),b=b.next()){ok&=source.document()->documentLayout()->blockBoundingRect(a)==reference.document()->documentLayout()->blockBoundingRect(b);ok&=a.layout()->lineCount()==b.layout()->lineCount();for(int i=0;i<qMin(a.layout()->lineCount(),b.layout()->lineCount());++i)ok&=a.layout()->lineAt(i).position()==b.layout()->lineAt(i).position();}return ok;};
  const int revision=source.document()->revision();source.document()->setModified(false);
  for(int spacing:{100,120,150,200,100}){display(spacing);format(spacing);check(QString("%1 percent matches native Qt geometry").arg(spacing),same());check("display does not edit block formats",source.document()->begin().blockFormat().lineHeightType()==QTextBlockFormat::SingleHeight);}
  check("display preserves revision and modified state",source.document()->revision()==revision&&!source.document()->isModified()&&!source.document()->isUndoAvailable());
  source.setFocus();auto c=source.textCursor();c.movePosition(QTextCursor::End);source.setTextCursor(c);source.insertPlainText(" first");c=source.textCursor();c.beginEditBlock();c.insertText(" second");c.endEditBlock();source.undo();
  const auto one=source.toPlainText();check("redo exists before display adjustment",source.document()->isRedoAvailable());
  display(180);source.setFont(QFont("Consolas",17));display(120);source.setFont(font);check("preview leaves redo and source intact",source.document()->isRedoAvailable()&&source.toPlainText()==one);
  source.redo();check("redo restores the next text edit",source.toPlainText()==text+" first second");source.undo();source.undo();check("two undos restore exact source",source.toPlainText()==text);
  source.setPlainText(text);reference.setPlainText(text);display(150);format(150);c=source.textCursor();c.movePosition(QTextCursor::End);c.insertText("\n新段落 new paragraph");c=reference.textCursor();c.movePosition(QTextCursor::End);c.insertText("\n新段落 new paragraph");check("new paragraph inherits display leading",same());
  source.document()->setProperty("_texline_lazy_edits",false);source.document()->markContentsDirty(0,source.document()->characterCount());check("ordinary documents ignore source-only property",source.document()->documentLayout()->documentSize().height()<reference.document()->documentLayout()->documentSize().height());
  put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"devicePixelRatio",source.devicePixelRatioF()},{"physicalInput",false}}).toJson());return failures?1:0;
}
