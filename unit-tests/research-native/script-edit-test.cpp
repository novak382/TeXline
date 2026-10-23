#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "scripting/ScriptAPI.h"
#include "TeXDocumentWindow.h"
#include "CompletingEdit.h"
#include "ResearchAppearance.h"
#include "ResearchWorkflow.h"
#include "TeXHighlighter.h"
#include "Settings.h"
#include "TWScriptManager.h"
#include "scripting/ECMAScriptInterface.h"
#include "scripting/ScriptObject.h"
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTextBlock>
#include <QTimer>
#include <QThread>
#include <QJSEngine>
#include <QFileInfo>
#include <QAbstractTextDocumentLayout>
#include <QPixmap>
#include <QMessageBox>

static void settle(int ms=200) {
  QElapsedTimer t;t.start();do{QCoreApplication::processEvents();QThread::msleep(2);}while(t.elapsed()<ms);
}
class RangeLayout : public QAbstractTextDocumentLayout {
public:
  int from=-1,removed=-1,added=-1,notifications=0;
  explicit RangeLayout(QTextDocument *doc):QAbstractTextDocumentLayout(doc){}
  void draw(QPainter *,const PaintContext &)override{}
  int hitTest(const QPointF &,Qt::HitTestAccuracy)const override{return 0;}
  int pageCount()const override{return 1;}
  QSizeF documentSize()const override{return {100,100};}
  QRectF frameBoundingRect(QTextFrame *)const override{return {0,0,100,100};}
  QRectF blockBoundingRect(const QTextBlock &)const override{return {0,0,100,20};}
protected:
  void documentChanged(int position,int oldLength,int length)override{from=position;removed=oldLength;added=length;++notifications;}
};
class RangeHighlighter : public NonblockingSyntaxHighlighter {
public:
  bool bold=false;
  explicit RangeHighlighter(QTextDocument *doc):NonblockingSyntaxHighlighter(doc){}
  void dirty(const QTextBlock &b){pushDirtyRange(b);markDirtyContent();}
protected:
  void highlightBlock(const QString &text)override{if(bold){QTextCharFormat format;format.setFontWeight(QFont::Bold);setFormat(0,text.size(),format);}}
};
int main(int argc,char **argv) {
  const QString report=QString::fromLocal8Bit(argv[1]);argc=1;
  TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
  QTimer::singleShot(100,&app,[&] {
    QJsonArray checks,runs,unexpectedDialogs;int failures=0;
    auto save=[&]{QSaveFile f(report);if(f.open(QIODevice::WriteOnly)){f.write(QJsonDocument(QJsonObject{{"checks",checks},{"runs",runs},{"failures",failures},{"unexpectedDialogs",unexpectedDialogs}}).toJson());f.commit();}};
    auto check=[&](const QString &name,bool passed){checks.append(QJsonObject{{"name",name},{"passed",passed}});failures+=!passed;save();};
    QTimer dialogGuard;
    QObject::connect(&dialogGuard,&QTimer::timeout,&app,[&] {
      for(auto *widget:QApplication::topLevelWidgets()) {
        auto *box=qobject_cast<QMessageBox*>(widget);
        if(box&&box->isVisible()) {
          unexpectedDialogs.append(QJsonObject{{"title",box->windowTitle()},{"text",box->text()},{"details",box->informativeText()}});
          save();box->reject();
        }
      }
    });dialogGuard.start(200);
    Tw::Settings().setValue("openPDFwithTeX",false);Research::setFocusMode(false);
    QVariant retiredResult;auto *retiredTarget=new QObject;
    Tw::Scripting::ScriptAPI retiredApi(nullptr,&app,retiredTarget,retiredResult);
    retiredTarget->deleteLater();retiredApi.yield();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);retiredApi.yield();
    check("yield safely retires a destroyed script target",retiredApi.GetTarget()==nullptr);
    const QString childPath=QFileInfo(report).absolutePath()+"/large-child.tex",parentPath=QFileInfo(report).absolutePath()+"/parent.tex";
    const QString childText=QString("% Independent long subfile 注释.\n").repeated(50000)+"body\n";
    auto sourceFile=[](const QString &path,const QString &text){QSaveFile f(path);return f.open(QIODevice::WriteOnly)&&f.write(text.toUtf8())>0&&f.commit();};
    check("large child and its parent are independently created",sourceFile(childPath,childText)&&sourceFile(parentPath,"\\documentclass{article}\n\\input{large-child}\n"));
    bool parentCorrect=Research::resolveRoot(childPath,childText)==QFileInfo(parentPath).canonicalFilePath();QElapsedTimer roots;roots.start();
    for(int i=0;i<20;++i)parentCorrect&=Research::resolveRoot(childPath,childText)==QFileInfo(parentPath).canonicalFilePath();
    runs.append(QJsonObject{{"variant","large-subfile-cached-root"},{"queries",20},{"milliseconds",double(roots.nsecsElapsed())/1e6}});
    check("large subfile root queries consistently reuse its parent",parentCorrect);
    check("a tail class declaration invalidates the cached parent beyond the preamble",Research::resolveRoot(childPath,childText+"\\documentclass{article}\n")==QFileInfo(childPath).canonicalFilePath());
    QTextDocument rangeDoc("prefix\n"+QString("other source\n").repeated(1000)+"last");
    auto *rangeLayout=new RangeLayout(&rangeDoc);rangeDoc.setDocumentLayout(rangeLayout);
    RangeHighlighter rangeHighlighter(&rangeDoc);const auto last=rangeDoc.lastBlock();rangeHighlighter.dirty(last);
    check("highlight invalidation uses a length, not an end position",rangeLayout->from==last.position()&&rangeLayout->removed==last.length()&&rangeLayout->added==last.length());
    settle(120);rangeLayout->notifications=0;
    rangeHighlighter.rehighlight();settle(120);
    check("unchanged highlight formats avoid redundant layout notifications",rangeLayout->notifications==0);
    runs.append(QJsonObject{{"variant","unchanged-highlighting"},{"layoutNotifications",rangeLayout->notifications}});
    const int sourceRevision=rangeDoc.revision();rangeHighlighter.bold=true;rangeHighlighter.rehighlight();settle(120);
    check("changed font formats still notify the layout",rangeLayout->notifications>0&&rangeDoc.firstBlock().layout()->formats().first().format.fontWeight()==QFont::Bold);
    rangeLayout->notifications=0;rangeHighlighter.rehighlight();settle(120);
    check("identical font formats skip relayout after the first application",rangeLayout->notifications==0);
    check("highlight format updates preserve the source revision",rangeDoc.revision()==sourceRevision);
    if(qEnvironmentVariableIsSet("TEXLINE_TEST_HIGHLIGHT_ONLY")) {
      save();app.exit(failures?1:0);return;
    }
    const QString original="\\documentclass{article}\n"+QString("% Reproducible source and multilingual 注释.\n").repeated(100000)+"tail\n";
    const QString path=QFileInfo(report).absolutePath()+"/long-source.tex";
    QSaveFile input(path);check("independent source created",input.open(QIODevice::WriteOnly)&&input.write(original.toUtf8())>0&&input.commit());
    QElapsedTimer loading;loading.start();
    auto *w=new TeXDocumentWindow(path);auto *e=w->editor();
    runs.append(QJsonObject{{"variant","load-without-full-layout-demand"},{"milliseconds",double(loading.nsecsElapsed())/1e6}});save();
    // Let idle syntax updates interrupt the initial lazy layout. Demanding
    // documentSize()/lastBlock bounds first would mask the Qt failure.
    settle(600);
    auto *doc=w->textDoc();
    check("loaded snapshot exactly matches disk text",doc->sourceText()==original);
    const QString cachedEditor=doc->editorText(),cachedSource=doc->sourceText();const int revisionBeforeModelines=doc->revision();
    doc->parseModeLines();
    check("reading modelines preserves revision and shared text snapshots",doc->revision()==revisionBeforeModelines&&doc->editorText().constData()==cachedEditor.constData()&&doc->sourceText().constData()==cachedSource.constData());
    Tw::Document::TeXDocument unicodeDoc(QString::fromUtf8("A\xc2\xa0 B\xe2\x80\xa8 C 😀\n"));
    QString expectedEditor=unicodeDoc.toRawText();expectedEditor.replace(QChar::ParagraphSeparator,QLatin1Char('\n'));expectedEditor.replace(QChar::LineSeparator,QLatin1Char('\n'));
    check("editor snapshot preserves NBSP and supplementary text while normalizing separators",unicodeDoc.editorText()==expectedEditor&&unicodeDoc.editorText().contains(QChar::Nbsp));
    check("analysis snapshot retains Qt plain text semantics",unicodeDoc.sourceText()==unicodeDoc.toPlainText());
    const QString raw=unicodeDoc.editorText();
    check("unchanged script text reads share their editor snapshot",unicodeDoc.editorText().constData()==raw.constData());
    bool visibleLayout=true;QJsonArray layoutSamples;
    for(int blockNumber:{0,1,10000,20000,50000,99990}) {
      QTextCursor visible(doc->findBlockByNumber(blockNumber));e->setTextCursor(visible);settle();
      const auto bounds=doc->documentLayout()->blockBoundingRect(visible.block());
      layoutSamples.append(QJsonObject{{"block",blockNumber},{"height",bounds.height()},{"y",bounds.y()},{"width",bounds.width()},{"visible",visible.block().isVisible()},{"lines",visible.block().layout()->lineCount()},{"lineHeight",visible.block().blockFormat().lineHeight()},{"cursorHeight",e->cursorRect(visible).height()}});
      visibleLayout&=bounds.height()>0&&bounds.width()>0&&e->cursorRect(visible).height()>0;
    }
    check("long source remains laid out when jumping across distant paragraphs",visibleLayout);
    doc->documentLayout()->documentSize();int missing=0;
    for(auto b=doc->firstBlock();b.isValid();b=b.next())missing+=doc->documentLayout()->blockBoundingRect(b).height()<=0;
    check("a full layout demand gives every long-source paragraph positive height",missing==0);
    runs.append(QJsonObject{{"variant","layout"},{"samples",layoutSamples},{"missingLines",missing},{"documentHeight",doc->documentLayout()->documentSize().height()},{"pageHeight",doc->pageSize().height()},{"spacing",w->lineSpacing()}});save();
    for(const QString &variant:{QString("normal"),QString("edit-block"),QString("layout-suspended")}) {
      QTextCursor batch(doc);QElapsedTimer timer;timer.start();
      if(variant=="edit-block")batch.beginEditBlock();
      if(variant=="layout-suspended")doc->setLayoutEnabled(false);
      for(int n=0;n<10;++n){w->selectRange(0,0);w->insertText("x");w->selectRange(0,1);w->insertText("");}
      if(variant=="edit-block")batch.endEditBlock();
      if(variant=="layout-suspended")doc->setLayoutEnabled(true);
      runs.append(QJsonObject{{"variant",variant},{"operations",20},{"milliseconds",double(timer.nsecsElapsed())/1e6}});
      check(variant+": repeated script API edits preserve all source",doc->sourceText()==original&&e->text()==original);
      settle();
    }
    QTextCursor cursor(doc);cursor.movePosition(QTextCursor::End);cursor.insertText("中文");
    check("tail edit invalidates source and engine facts",doc->sourceText()==original+"中文"&&Research::suggestedEngine(doc->sourceText())=="XeLaTeX");
    doc->undo();check("undo invalidates source and engine facts",doc->sourceText()==original&&Research::suggestedEngine(doc->sourceText())=="pdfLaTeX");
    doc->redo();check("redo restores the changed source snapshot",doc->sourceText()==original+"中文");doc->undo();
    doc->setPlainText("same size A");check("replacement updates snapshot",doc->sourceText()=="same size A");
    doc->setPlainText("same size B");check("same-size replacement cannot reuse an old snapshot",doc->sourceText()=="same size B");
    CompletingEdit::setHighlightCurrentLine(true);
    doc->setPlainText(original);settle();
    auto script=[&](const QString &body) {
      const QString filename=QFileInfo(report).absolutePath()+"/edits.js";
      QSaveFile f(filename);if(!f.open(QIODevice::WriteOnly))return false;
      f.write(("// TeXworksScript\n// Title: independent script regression\n// Script-Type: standalone\n"+body).toUtf8());if(!f.commit())return false;
      Tw::Scripting::ECMAScriptInterface language(nullptr);
      Tw::Scripting::ScriptObject object(std::unique_ptr<Tw::Scripting::Script>(language.newScript(filename)));
      if(!object.getScript()->parseHeader())return false;
      QVariant result;return app.getScriptManager()->runScript(&object,w,result,Tw::Scripting::Script::ScriptStandalone);
    };
    const int undoBeforeSelection=doc->availableUndoSteps();const bool dirtyBeforeSelection=doc->isModified();
    const QString selectionSnapshot=doc->sourceText();
    const int selectionRevision=doc->revision();
    check("selection-only scripts add no undo entry or modified state",script("TW.target.selectRange(0,0); undefined;")&&doc->availableUndoSteps()==undoBeforeSelection&&doc->isModified()==dirtyBeforeSelection);
    runs.append(QJsonObject{{"variant","selection-only"},{"revisionBefore",selectionRevision},{"revisionAfter",doc->revision()}});
    check("selection-only scripts retain the shared source allocation",doc->sourceText().constData()==selectionSnapshot.constData());
    QElapsedTimer scriptTimer;scriptTimer.start();
    check("real JavaScript batches 1000 writes without changing source",script("for(var i=0;i<500;i++){TW.target.selectRange(0,0);TW.target.insertText('x');TW.target.selectRange(0,1);TW.target.insertText('');} undefined;")&&doc->sourceText()==original);
    check("script completion restores current-line selections after valid layout",!e->property("researchScriptEditPending").toBool()&&!e->extraSelections().isEmpty());
    runs.append(QJsonObject{{"variant","real-JavaScript"},{"operations",1000},{"milliseconds",double(scriptTimer.nsecsElapsed())/1e6}});
    doc->firstBlock().layout()->clearLayout();
    QPixmap frame(e->viewport()->size());e->viewport()->render(&frame);
    check("painting a temporarily invalid highlighted line safely preserves source",!frame.isNull()&&doc->sourceText()==original);
    doc->markContentsDirty(0,doc->firstBlock().length());settle();
    check("current-line highlight returns after deferred layout completes",doc->firstBlock().layout()->lineCount()>0&&!e->extraSelections().isEmpty());
    doc->setPlainText("base");
    check("script reads see pending edits and modified status",script("TW.target.selectRange(0,0);TW.target.insertText('A');if(TW.target.text!=='Abase'||!TW.target.modified)throw Error('stale read');undefined;")&&doc->sourceText()=="Abase");
    doc->undo();check("script edit undo restores source",doc->sourceText()=="base");
    doc->setPlainText("base");
    check("yield flushes edits before the next batch",script("TW.target.selectRange(0,0);TW.target.insertText('A');TW.yield();TW.target.selectRange(0,0);TW.target.insertText('B');undefined;")&&doc->sourceText()=="BAbase");
    doc->undo();check("undo respects the yield boundary",doc->sourceText()=="Abase");doc->undo();check("both batches undo cleanly",doc->sourceText()=="base");
    check("script exceptions retire pending edit blocks",!script("TW.target.selectRange(0,0);TW.target.insertText('E');throw Error('expected');")&&doc->sourceText()=="Ebase");
    doc->undo();check("undo remains available after a script exception",doc->sourceText()=="base");
    check("save flushes pending script changes",script("TW.target.selectRange(0,0);TW.target.insertText('S');TW.target.save();undefined;")&&!w->isModified());
    QFile saved(path);check("script save commits exactly the current source",saved.open(QIODevice::ReadOnly)&&QString::fromUtf8(saved.readAll())=="Sbase");saved.close();
    const QString prefix=QString("% padding\n").repeated(300);
    check("first engine analysis ignores comments",Research::suggestedEngine(prefix+"% 中文")=="pdfLaTeX");
    check("changes beyond preamble invalidate cached engine facts",Research::suggestedEngine(prefix+"中文")=="XeLaTeX");
    check("scripts and saving complete without unexpected modal dialogs",unexpectedDialogs.isEmpty());
    w->setModified(false);delete w;save();app.exit(failures?1:0);
  });return app.exec();
}
