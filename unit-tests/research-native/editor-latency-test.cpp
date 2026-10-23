#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "TeXDocumentWindow.h"
#include "CompletingEdit.h"
#include "ResearchAppearance.h"
#include "Settings.h"
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QPointer>
#include <QSaveFile>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <functional>

static void settle(int ms=0) {
  QElapsedTimer t; t.start();
  do { QCoreApplication::processEvents(); QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete); if(ms)QThread::msleep(2); } while(t.elapsed()<ms);
}
static void key(CompletingEdit *e,int code,const QString &text={}) {
  QKeyEvent down(QEvent::KeyPress,code,Qt::NoModifier,text),up(QEvent::KeyRelease,code,Qt::NoModifier);
  QApplication::sendEvent(e,&down); QApplication::sendEvent(e,&up);
}
static QJsonObject measure(const std::function<void(int)> &action,int count) {
  QList<double> times; QElapsedTimer all; all.start();
  for(int n=0;n<count;++n) { QElapsedTimer t;t.start();action(n);times.append(double(t.nsecsElapsed())/1e6); }
  const double elapsed=double(all.nsecsElapsed())/1e6; std::sort(times.begin(),times.end());
  return {{"count",count},{"totalMs",elapsed},{"medianMs",times[times.size()/2]},{"p95Ms",times[times.size()*95/100]},{"maxMs",times.last()}};
}
int main(int argc,char **argv) {
  const QString report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
  QTimer::singleShot(100,&app,[&] {
    QJsonArray checks,runs;int failures=0;
    auto save=[&]{QSaveFile f(report);if(f.open(QIODevice::WriteOnly)){f.write(QJsonDocument(QJsonObject{{"checks",checks},{"runs",runs},{"failures",failures}}).toJson());f.commit();}};
    auto check=[&](QString name,bool pass){checks.append(QJsonObject{{"name",name},{"passed",pass}});failures+=!pass;save();};
    Tw::Settings().setValue("openPDFwithTeX",false);Tw::Settings().setValue("researchCommandCompletionMode",0);Research::setFocusMode(false);
    for(int lines:{1000,25000,100000}) {
      auto *w=new TeXDocumentWindow();auto *e=w->editor();w->resize(850,700);w->show();
      const QString original=QString("% Example text, mathematical source and multilingual 注释.\n").repeated(lines)+"tail ";
      e->setPlainText(original);e->setSmartQuotesMode(-1);auto cursor=e->textCursor();cursor.movePosition(QTextCursor::End);e->setTextCursor(cursor);settle(180);
      const auto typing=measure([&](int){key(e,Qt::Key_A,"a");},240);
      check(QString("%1 lines: ordinary typing changes only the intended suffix").arg(lines),e->toPlainText()==original+QString("a").repeated(240));
      const auto arrows=measure([&](int n){key(e,n%2?Qt::Key_Right:Qt::Key_Left);},240);
      check(QString("%1 lines: cursor movement preserves text").arg(lines),e->toPlainText()==original+QString("a").repeated(240));
      while(e->document()->isUndoAvailable())e->undo();
      check(QString("%1 lines: undo restores the whole source").arg(lines),e->toPlainText()==original);
      e->setPlainText("{\n[\n(x)\n]\n}");cursor=e->textCursor();cursor.movePosition(QTextCursor::End);e->setTextCursor(cursor);key(e,Qt::Key_Left);
      bool match=false;for(const auto &selection:e->extraSelections())if(selection.cursor.selectionStart()==0&&selection.cursor.selectedText()=="{")match=true;
      check(QString("%1 lines: cross-block brace highlighting works").arg(lines),match);
      e->setPlainText(original);e->setSmartQuotesMode(0);cursor=e->textCursor();cursor.movePosition(QTextCursor::End);e->setTextCursor(cursor);settle(180);
      const auto smartTyping=measure([&](int){key(e,Qt::Key_B,"b");},240);
      check(QString("%1 lines: smart-quote mode preserves ordinary characters").arg(lines),e->toPlainText()==original+QString("b").repeated(240));
      for(int mode=0;mode<CompletingEdit::smartQuotesModes().size();++mode) {
        e->setSmartQuotesMode(mode);e->setPlainText("\\");cursor=e->textCursor();cursor.movePosition(QTextCursor::End);e->setTextCursor(cursor);key(e,Qt::Key_QuoteDbl,"\"");
        check(QString("%1 lines mode %2: escaped quotes remain literal").arg(lines).arg(mode),e->toPlainText()=="\\\"");
        e->setPlainText("\n");cursor=e->textCursor();cursor.movePosition(QTextCursor::End);e->setTextCursor(cursor);key(e,Qt::Key_QuoteDbl,"\"");const QString afterNewline=e->toPlainText().mid(1);
        e->setPlainText("");key(e,Qt::Key_QuoteDbl,"\"");check(QString("%1 lines mode %2: paragraph boundary uses opening quote").arg(lines).arg(mode),e->toPlainText()==afterNewline);
      }
      runs.append(QJsonObject{{"lines",lines},{"characters",original.size()},{"typing",typing},{"arrows",arrows},{"smartTyping",smartTyping}});save();
      QPointer<TeXDocumentWindow> guard(w);w->setModified(false);w->close();settle(100);check(QString("%1 lines: closing releases window").arg(lines),guard.isNull());
    }
    save();app.exit(failures?1:0);
  });return app.exec();
}
