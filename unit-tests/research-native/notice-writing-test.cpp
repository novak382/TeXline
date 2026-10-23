// A focus-mode notification must leave the active source selection readable.
#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "TeXDocumentWindow.h"
#include "CompletingEdit.h"
#include "ResearchAppearance.h"
#include "Settings.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QLabel>
#include <QStatusBar>
#include <QThread>
#include <QTimer>
#include <QKeyEvent>
#include <windows.h>
#include <shellapi.h>
static void settle(int ms){QElapsedTimer t;t.start();while(t.elapsed()<ms){QCoreApplication::processEvents();QThread::msleep(2);}}
static bool put(const QString &path,const QByteArray &data){QSaveFile file(path);return file.open(QIODevice::WriteOnly)&&file.write(data)==data.size()&&file.commit();}
int main(int argc,char **argv){
 if(!qEnvironmentVariable("TEXLINE_TEST_DESKTOP").startsWith("TeXlineValidation-"))return 2;
 int n=0;auto **args=CommandLineToArgvW(GetCommandLineW(),&n);const QString report=QString::fromWCharArray(args[1]);LocalFree(args);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
 QTimer::singleShot(80,&app,[&]{
  const auto folder=QFileInfo(report).absolutePath(),path=folder+"/transport.tex";QString source="\\documentclass{article}\n\\begin{document}\n";
  for(int i=0;i<120;++i)source+="Mass in each finite-volume cell is tracked before normalization.\n";source+="\\end{document}\n";put(path,source.toUtf8());
  QJsonArray checks;int failures=0;auto check=[&](QString name,bool good){failures+=!good;checks.append(QJsonObject{{"name",name},{"passed",good}});};
  Tw::Settings().setValue("openPDFwithTeX",false);Tw::Settings().setValue("researchFocusMode",false);auto *w=new TeXDocumentWindow(path);w->resize(1050,720);w->show();settle(400);Research::setFocusMode(true);settle(220);
  auto c=w->textCursor();c.movePosition(QTextCursor::End);c.movePosition(QTextCursor::PreviousBlock,QTextCursor::KeepAnchor,2);w->editor()->setTextCursor(c);w->editor()->ensureCursorVisible();settle(80);
  const auto selected=c.selectedText();auto a=c,b=c;a.setPosition(c.selectionStart());b.setPosition(c.selectionEnd());const auto first=w->editor()->cursorRect(a),last=w->editor()->cursorRect(b);const auto top=w->editor()->viewport()->mapTo(w,QPoint(0,qMin(first.top(),last.top())));const QRect selectedArea(top,QSize(w->editor()->viewport()->width(),qAbs(last.bottom()-first.top())+1));
  Research::showTransientStatusMessage(w,"Recovered the previous source revision. Undo remains available.",800,Research::NoticePriority::Attention);settle(220);auto *label=w->findChild<QLabel*>("researchFocusMessage");const QRect notice(label?label->mapTo(w,QPoint()):QPoint(),label?label->size():QSize());
  check("actual attention notice is available",label&&!w->statusBar()->currentMessage().isEmpty());
  check("active source selection remains visible under a notice",!label||!label->isVisible()||!selectedArea.intersects(notice));
  check("notice leaves source selection and source bytes intact",w->textCursor().selectedText()==selected&&w->textDoc()->editorText()==source);
  w->grab().save(folder+"/selected-bottom.png");put(folder+"/geometry.json",QJsonDocument(QJsonObject{{"selection",QJsonArray{selectedArea.x(),selectedArea.y(),selectedArea.width(),selectedArea.height()}},{"notice",QJsonArray{notice.x(),notice.y(),notice.width(),notice.height()}},{"visible",label&&label->isVisible()}}).toJson());
  const auto originalSelection=w->textCursor();c.select(QTextCursor::Document);w->editor()->setTextCursor(c);settle(30);
  check("a full-source selection is not covered by a floating notice",label->isHidden());
  check("temporary avoidance retains the attention state",!w->statusBar()->currentMessage().isEmpty());
  c.clearSelection();w->editor()->setTextCursor(c);settle(30);check("notice returns when a safe writing area is available",label->isVisible());
  w->editor()->setTextCursor(originalSelection);
  QKeyEvent key(QEvent::KeyPress,Qt::Key_X,Qt::NoModifier,"x");QApplication::sendEvent(w->editor(),&key);check("typing reaches the selected source immediately",w->textDoc()->editorText()!=source&&!w->textCursor().hasSelection());w->editor()->undo();check("notification adds no undo transaction",w->textDoc()->editorText()==source);settle(1100);check("attention notice expires normally",w->statusBar()->currentMessage().isEmpty()&&label->isHidden());
  w->setModified(false);delete w;put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures}}).toJson());app.exit(failures?1:0);
 });return app.exec();
}
