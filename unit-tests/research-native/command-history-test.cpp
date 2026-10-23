#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "TeXDocumentWindow.h"
#include "CompletingEdit.h"
#include "Settings.h"
#include <QAction>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QListView>
#include <QSaveFile>
#include <QTimer>
#include <QThread>
// Run in a fresh process after command-completion-test. No internal cache or
// document from the process that learned the choice can influence this check.
int main(int argc,char **argv) {
  QString report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
  QTimer::singleShot(100,&app,[&] {
    QJsonArray checks;int failures=0;
    auto check=[&](QString name,bool passed){checks.append(QJsonObject{{"name",name},{"passed",passed}});failures+=!passed;};
    const auto stored=Tw::Settings().value("researchCommandCompletionUsage").toStringList();
    check("fresh process restores bounded confirmed choices",stored.size()==64&&!stored.isEmpty()&&stored.first().endsWith("\t\\geq"));
    Tw::Settings().setValue("openPDFwithTeX",false);auto *w=new TeXDocumentWindow();w->show();w->activateWindow();w->editor()->setFocus();
    w->findChild<QAction*>("researchCommandCompletionMode1")->trigger();w->editor()->setPlainText("\\ge");auto cursor=w->editor()->textCursor();cursor.movePosition(QTextCursor::End);w->editor()->setTextCursor(cursor);
    // Keep assertion storage alive throughout the check. A delayed lambda
    // must not capture the outer timer callback's stack after it returns.
    QElapsedTimer settling;settling.start();while(settling.elapsed()<150){QCoreApplication::processEvents();QThread::msleep(3);}
      QKeyEvent key(QEvent::KeyPress,Qt::Key_Slash,Qt::AltModifier);QApplication::sendEvent(w->editor(),&key);
      auto *popup=w->findChild<QListView*>("researchCommandCompletion");check("fresh process ranks the learned geq before the exact ge prefix",popup&&popup->isVisible()&&popup->model()->index(0,0).data().toString()=="\\geq");
      check("ranking alone never edits the source",w->editor()->toPlainText()=="\\ge");
      w->setModified(false);w->close();Tw::Settings().setValue("researchCommandCompletionMode",0);
      QSaveFile file(report);if(file.open(QIODevice::WriteOnly)){file.write(QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures}}).toJson());file.commit();}app.exit(failures?1:0);
  });return app.exec();
}
