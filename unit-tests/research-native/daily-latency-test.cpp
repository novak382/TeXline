#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "TeXDocumentWindow.h"
#include "CompletingEdit.h"
#include "Settings.h"
#include <QElapsedTimer>
#include <QDir>
#include <QCryptographicHash>
#include <QAbstractTextDocumentLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QMessageBox>
#include <QPointer>
#include <QSaveFile>
#include <QThread>
#include <QTextBlock>
#include <QTimer>
#include <windows.h>
#include <psapi.h>
#include <algorithm>
#include <functional>

static void drain(int ms=0) {
 QElapsedTimer t;t.start();do {QCoreApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);if(ms)QThread::msleep(2);}while(t.elapsed()<ms);
}
static void key(CompletingEdit *e,int code,const QString &text={}) {
 QKeyEvent down(QEvent::KeyPress,code,Qt::NoModifier,text),up(QEvent::KeyRelease,code,Qt::NoModifier);
 QApplication::sendEvent(e,&down);QApplication::sendEvent(e,&up);
}
static QJsonObject distribution(QList<double> values) {
 if(values.isEmpty())return {};
 std::sort(values.begin(),values.end());QJsonArray raw;for(double n:values)raw.append(n);
 return {{"count",values.size()},{"medianMs",values[values.size()/2]},{"p95Ms",values[values.size()*95/100]},{"maxMs",values.last()},{"values",raw}};
}
static QJsonObject memoryUsage() {
 using Query=BOOL(WINAPI *)(HANDLE,PPROCESS_MEMORY_COUNTERS,DWORD);
 auto query=reinterpret_cast<Query>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"K32GetProcessMemoryInfo"));
 PROCESS_MEMORY_COUNTERS_EX counters{};
 if(!query||!query(GetCurrentProcess(),reinterpret_cast<PPROCESS_MEMORY_COUNTERS>(&counters),sizeof(counters)))return {{"available",false}};
 return {{"available",true},{"workingSetMiB",double(counters.WorkingSetSize)/(1024*1024)},{"privateCommitMiB",double(counters.PrivateUsage)/(1024*1024)},{"peakCommitMiB",double(counters.PeakPagefileUsage)/(1024*1024)}};
}
int main(int argc,char **argv) {
 const QString report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
 QTimer::singleShot(80,&app,[&] {
  QJsonArray checks,runs,unexpectedMessages;int failures=0;QString stage;QList<double> gaps;QElapsedTimer heartbeatTime;heartbeatTime.start();
  QTimer heartbeat;QObject::connect(&heartbeat,&QTimer::timeout,&app,[&]{gaps.append(double(heartbeatTime.nsecsElapsed())/1e6);heartbeatTime.restart();});heartbeat.start(16);
  auto save=[&]{QSaveFile f(report);if(f.open(QIODevice::WriteOnly)){f.write(QJsonDocument(QJsonObject{{"checks",checks},{"runs",runs},{"failures",failures},{"unexpectedMessages",unexpectedMessages},{"physicalInputVerified",false}}).toJson());f.commit();}};
  auto check=[&](QString name,bool passed){checks.append(QJsonObject{{"name",name},{"passed",passed}});failures+=!passed;save();};
  QTimer modalObserver;QObject::connect(&modalObserver,&QTimer::timeout,&app,[&]{
   for(auto *widget:QApplication::topLevelWidgets())if(auto *message=qobject_cast<QMessageBox*>(widget);message&&message->isVisible()) {
    unexpectedMessages.append(QJsonObject{{"stage",stage},{"title",message->windowTitle()},{"text",message->text()}});save();message->reject();
   }
  });modalObserver.start(20);
  Tw::Settings settings;settings.setValue("openPDFwithTeX",false);settings.setValue("researchCommandCompletionMode",0);settings.setValue("highlightCurrentLine",true);
  const int samples=qBound(12,qEnvironmentVariableIntValue("TEXLINE_LATENCY_SAMPLES")>0?qEnvironmentVariableIntValue("TEXLINE_LATENCY_SAMPLES"):120,240);
  for(int lines:{2500,100000}) {
   const QString original="\\documentclass{article}\n\\begin{document}\n"+QString("Paragraph mathematical source 中文 $x_i$ alpha beta gamma.\n").repeated(lines)+"UniqueTailSentinel\\end{document}\n";
   const QString path=QFileInfo(report).absolutePath()+QString("/source-%1/main.tex").arg(lines);QDir().mkpath(QFileInfo(path).absolutePath());QSaveFile source(path);
   check("independent source is written",source.open(QIODevice::WriteOnly)&&source.write(original.toUtf8())==original.toUtf8().size()&&source.commit());
   QElapsedTimer load;load.start();auto *w=new TeXDocumentWindow(path);auto *e=w->editor();w->resize(850,700);w->show();drain(600);
   runs.append(QJsonObject{{"lines",lines},{"stage","open-and-600ms-idle"},{"ms",double(load.nsecsElapsed())/1e6},{"sourceSha256",QString::fromLatin1(QCryptographicHash::hash(original.toUtf8(),QCryptographicHash::Sha256).toHex())},{"memory",memoryUsage()},{"profile",QJsonObject{{"font",e->font().toString()},{"lineHeight",e->document()->firstBlock().blockFormat().lineHeight()},{"lineWrapMode",int(e->lineWrapMode())},{"dpr",e->devicePixelRatioF()},{"currentLine",true},{"lazySourceEdits",e->document()->property("_texline_lazy_edits").toBool()},{"physicalInput",false}}}});save();
   if(qEnvironmentVariableIsSet("TEXLINE_TEST_LAZY_EDITS"))e->document()->setProperty("_texline_lazy_edits",true);
   auto measure=[&](QString name,const std::function<void(int)> &action,int count,int pace=0){
    stage=name;gaps.clear();heartbeatTime.restart();QList<double> values,dispatch,eventDrain;
    for(int n=0;n<count;++n){QElapsedTimer timer;timer.start();action(n);const double direct=double(timer.nsecsElapsed())/1e6;drain();const double total=double(timer.nsecsElapsed())/1e6;dispatch.append(direct);eventDrain.append(total-direct);values.append(total);if(pace)drain(pace);}
    runs.append(QJsonObject{{"lines",lines},{"stage",name},{"operationAndEvents",distribution(values)},{"dispatch",distribution(dispatch)},{"eventDrain",distribution(eventDrain)},{"heartbeatGaps",distribution(gaps)}});save();
   };
   measure("first-end-jump",[&](int){auto c=e->textCursor();c.movePosition(QTextCursor::End);e->setTextCursor(c);},1);
   check("first end jump leaves the requested cursor visible",e->viewport()->rect().intersects(e->cursorRect()));
   for(QString location:{"start","middle","end"}) {
    const int position=location=="start"?40:location=="middle"?int(original.size()/2):int(original.size()-1);
    measure("jump-"+location,[&](int){auto c=e->textCursor();c.setPosition(position);e->setTextCursor(c);},1);
    check("requested cursor remains visible at "+location,e->viewport()->rect().intersects(e->cursorRect()));
    measure("typing-"+location,[&](int){key(e,Qt::Key_A,"a");},samples,12);
    const QString typed=original.left(position)+QString("a").repeated(samples)+original.mid(position);
    check("typing preserves every untouched character at "+location,e->text()==typed);
    measure("undo-typing-"+location,[&](int){e->undo();},1);check("one typing undo restores source",e->text()==original);
    measure("redo-typing-"+location,[&](int){e->redo();},1);check("typing redo restores source",e->text()==typed);
    measure("delete-"+location,[&](int){key(e,Qt::Key_Backspace);},samples,12);check("deletion restores original source",e->text()==original);
    auto c=e->textCursor();c.setPosition(position);e->setTextCursor(c);
    const QString paste=QString("Pasted independent paragraph 中文 $a$ and math source.\n").repeated(2000);
    measure("paste-primitive-"+location,[&](int){auto cursor=e->textCursor();cursor.insertText(paste);e->setTextCursor(cursor);},1);
    check("paste primitive inserts the exact independent content",e->text()==original.left(position)+paste+original.mid(position));
    measure("undo-paste-"+location,[&](int){e->undo();},1);check("paste undo restores source",e->text()==original);
    e->document()->clearUndoRedoStacks();
    if(qEnvironmentVariableIsSet("TEXLINE_TEST_SAME_SIZE")) {
     measure("complete-layout-before-same-size-"+location,[&](int){e->document()->documentLayout()->documentSize();},1);drain(100);
     const int at=qMin(position+1,int(original.size()-2));const QString previous=original.mid(at,1),changed=previous=="x"?"y":"x";
     measure("same-size-"+location,[&](int n){auto cursor=e->textCursor();cursor.setPosition(at);cursor.setPosition(at+1,QTextCursor::KeepAnchor);cursor.insertText(n%2?previous:changed);e->setTextCursor(cursor);},samples,12);
     check("same-size editor writes preserve all other source characters",e->text()==original);
     e->undo();check("same-size native undo restores the previous character",e->text()==original.left(at)+changed+original.mid(at+1));
     e->redo();check("same-size native redo restores the original source",e->text()==original);e->document()->clearUndoRedoStacks();
    }
   }
   settings.setValue("searchText","UniqueTailSentinel");settings.setValue("searchRegex",false);settings.setValue("searchFlags",0);settings.setValue("searchWrap",true);settings.setValue("searchSelection",false);settings.setValue("searchAllFiles",false);
   measure("find-literal-tail",[&](int){auto c=e->textCursor();c.setPosition(0);e->setTextCursor(c);QMetaObject::invokeMethod(w,"doFindAgain",Qt::DirectConnection);},12);
   check("actual find operation selects only the unique tail",e->textCursor().selectedText()=="UniqueTailSentinel");
   auto c=e->textCursor();c.setPosition(int(original.size()-1));c.insertText("% recovery sample\n");e->setTextCursor(c);
   measure("modified-7s-idle",[&](int){drain(7000);},1);
   e->undo();check("idle recovery leaves document and undo intact",e->text()==original);
   bool saved=false;measure("save-private-source",[&](int){saved=w->save();},1);
   check("save succeeds and clears the independent document's modified state",saved&&!w->isModified());
   QFile disk(path);check("final private source equals initial bytes",disk.open(QIODevice::ReadOnly)&&disk.readAll()==original.toUtf8());disk.close();
   const QSizeF size=e->document()->documentLayout()->documentSize();bool complete=size.height()>0;double prior=-1;
   for(auto block=e->document()->firstBlock();block.isValid();block=block.next()){const QRectF bounds=e->document()->documentLayout()->blockBoundingRect(block);complete&=bounds.height()>0&&bounds.top()>=prior;prior=bounds.top();}
   check("all final paragraphs retain positive ordered geometry",complete);
   runs.append(QJsonObject{{"lines",lines},{"stage","final-source-and-layout"},{"memory",memoryUsage()}});save();
   const QFont originalFont=e->font();
   const double originalTail=e->document()->documentLayout()->blockBoundingRect(e->document()->lastBlock()).top();
   auto changing=e->textCursor();changing.setPosition(40);changing.insertText("a");e->setTextCursor(changing);
   const bool narrow=qEnvironmentVariableIsSet("TEXLINE_TEST_NARROW_REFLOW");
   QFont enlarged=originalFont;enlarged.setPointSizeF(originalFont.pointSizeF()*(narrow?1.2:1.1));
   auto milestone=[&](QString name){runs.append(QJsonObject{{"lines",lines},{"stage",name}});save();};
   milestone("reflow-before-font");e->setFont(enlarged);milestone("reflow-before-resize");w->resize(narrow?460:650,700);milestone("reflow-before-events");drain(80);milestone("reflow-before-full-layout");
   e->document()->documentLayout()->documentSize();bool wrapped=true;
   milestone("reflow-before-line-widths");
   const auto body=e->document()->findBlockByNumber(2);
   for(auto block=body;block.isValid()&&block!=e->document()->lastBlock();block=block.next()) {
    wrapped&=block.layout()->lineCount()>0;
    for(int i=0;i<block.layout()->lineCount();++i)wrapped&=block.layout()->lineAt(i).naturalTextWidth()<=e->viewport()->width()+1;
   }
   runs.append(QJsonObject{{"lines",lines},{"stage","reflow-geometry"},{"viewportWidth",e->viewport()->width()},{"textWidth",e->document()->textWidth()},{"bodyLines",body.layout()->lineCount()},{"bodyWidth",body.layout()->lineAt(0).naturalTextWidth()},{"bodyHeight",body.layout()->boundingRect().height()},{"font",e->font().toString()},{"narrow",narrow}});save();
   check("font and width changes invalidate all cached line geometry during an edit",wrapped&&body.layout()->lineCount()>1);
   e->undo();check("reflow during an edit preserves the complete native undo",e->text()==original);
   e->setFont(originalFont);w->resize(850,700);drain(80);e->document()->documentLayout()->documentSize();
   check("restoring font and width restores the complete tail position",qAbs(e->document()->documentLayout()->blockBoundingRect(e->document()->lastBlock()).top()-originalTail)<=2);
   QPointer<TeXDocumentWindow> guard(w);w->setModified(false);w->close();drain(50);check("closing releases the independent window",guard.isNull());
  }
  check("complete daily operations have no unexpected modal messages",unexpectedMessages.isEmpty());save();app.exit(failures?1:0);
 });return app.exec();
}
