#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "TeXDocumentWindow.h"
#include "PDFDocumentWindow.h"
#include "CompletingEdit.h"
#include "ResearchAppearance.h"
#include "Settings.h"
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QScrollBar>
#include <QThread>
#include <QTimer>

static void settle(int ms) {QElapsedTimer t;t.start();while(t.elapsed()<ms){QCoreApplication::processEvents();QThread::msleep(2);}}
int main(int argc,char **argv) {
  const QString report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
  QTimer::singleShot(100,&app,[&]{
    QJsonArray checks;int failures=0;auto save=[&]{QSaveFile f(report);if(f.open(QIODevice::WriteOnly)){f.write(QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures}}).toJson());f.commit();}};
    auto check=[&](QString n,bool pass){checks.append(QJsonObject{{"name",n},{"passed",pass}});failures+=!pass;save();};
    Tw::Settings().setValue("openPDFwithTeX",false);Research::setFocusMode(false);
    auto *w=new TeXDocumentWindow();w->resize(850,700);w->show();auto *e=w->editor();
    const QString text=QString("\\documentclass{article}\n% mathematical source 注释\n").repeated(300);e->setPlainText(text);settle(100);
    for(const QString &locale:QStringList{"zh_CN","en_US"}) {
      app.applyTranslation(locale);
      for(bool focused:{true,false,true,false}) {
        e->moveCursor(QTextCursor::Start);e->verticalScrollBar()->setValue(e->verticalScrollBar()->minimum());Research::setFocusMode(focused);settle(250);
        QTextCursor first(e->document());first.movePosition(QTextCursor::Start);const QRect line=e->cursorRect(first);
        check(locale+(focused?" focus":" default")+" top-of-document transition keeps the complete first line visible",line.top()>=0&&line.bottom()<e->viewport()->height()&&e->verticalScrollBar()->value()==e->verticalScrollBar()->minimum());
      }
      Research::setFocusMode(true);auto *veil=w->findChild<QWidget *>("researchChromeVeil");
      check(locale+" rapid reversal exposes live content without a stale snapshot",veil&&veil->property("researchSnapshotBytes").toLongLong()==0);
      Research::setFocusMode(false);settle(550);
      Research::setFocusMode(true);
      check(locale+" visible transition retains a snapshot",veil&&veil->property("researchSnapshotBytes").toLongLong()>0);
      w->hide();check(locale+" hiding a window cancels its snapshot immediately",veil&&veil->property("researchSnapshotBytes").toLongLong()==0&&veil->isHidden());
      w->show();settle(250);check(locale+" reopening a hidden window preserves source",e->toPlainText()==text);
    }
    w->setModified(false);w->close();settle(100);save();app.exit(failures?1:0);
  });return app.exec();
}
