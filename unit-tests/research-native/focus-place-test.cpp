#include "ActualAuthoringRecording.h"
using namespace Research;
int main(int argc,char **argv){
 const auto report=argument(1);if(!qEnvironmentVariable("TEXLINE_TEST_DESKTOP").startsWith("TeXlineValidation-"))return 2;
 argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
 QTimer::singleShot(80,&app,[&]{
  const auto folder=QFileInfo(report).absolutePath();QJsonArray checks;int failures=0;
  auto check=[&](const QString &name,bool ok){checks.append(QJsonObject{{"name",name},{"passed",ok}});failures+=!ok;put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"devicePixelRatio",qApp->primaryScreen()->devicePixelRatio()},{"physicalInput",false}}).toJson());};
  Tw::Settings().setValue("openPDFwithTeX",false);Tw::Settings().setValue("researchFocusMode",false);
  QString source="\\documentclass{article}\n\\begin{document}\n\\section{Introduction}\nOpening paragraph.\n\\section{Proof 證明}\n";for(int i=0;i<300;++i)source+=QString("Proof line %1 中文：The estimate is uniform and the writing position must stay stable.\n").arg(i);source+="\\section{Discussion}\nA different section.\n\\end{document}\n";
  const auto path=folder+"/paper/main.tex";put(path,source.toUtf8());auto *w=new TeXDocumentWindow(path);w->resize(qEnvironmentVariableIsSet("TEXLINE_TEST_HALF")?620:1120,760);w->show();QApplication::setActiveWindow(w);auto *e=w->editor();e->setFocus();
  auto ready=[&]{return waitFor([&]{return w->property("researchArticleReady").toBool()&&w->property("researchArticleMode").toBool();});};check("article index ready",ready());
  auto *section=w->findChild<QAction*>("researchArticleSectionChoice");auto *outline=w->findChild<QAction*>("researchOutlineAction");auto *dock=w->findChild<QDockWidget*>("researchArticleDock");
  auto c=e->textCursor();c.setPosition(source.indexOf("Proof line 150")+25);c.setPosition(source.indexOf("Proof line 150"),QTextCursor::KeepAnchor);e->setTextCursor(c);if(section)section->trigger();settle(100);check("section range selected",e->property("researchSourceSlice").toBool());
  if(dock&&!dock->isHidden())outline->trigger();settle(200);e->verticalScrollBar()->setValue(e->verticalScrollBar()->value()+60);const auto anchor=e->cursorForPosition(QPoint(0,0));const auto cursor=e->textCursor();const int y=e->viewport()->mapTo(w,e->cursorRect(anchor).topLeft()).y();w->grab().save(folder+"/focus-before.png");
  Recording record;const bool recording=qEnvironmentVariableIsSet("TEXLINE_DESIGN_RECORD");if(recording){record.start(folder+"/focus-recording",w);record.action("Enter focus while editing the current proof");}
  setFocusMode(true);settle(recording?650:230);check("focus retains section and reverse selection",e->property("researchSourceSlice").toBool()&&e->textCursor()==cursor);check("focus retains reading anchor",qAbs(e->viewport()->mapTo(w,e->cursorRect(anchor).topLeft()).y()-y)<=2);w->grab().save(folder+"/focus-entered.png");
  const auto place=articleReadingState(w);e->insertPlainText("Revised proof 改寫");check("focused edit retains current section",e->property("researchSourceSlice").toBool());e->undo();check("focused edit is one exact undo",e->toPlainText()==source);check("focus reindex completes",ready());
  if(recording)record.action("Reverse a transition and continue writing");setFocusMode(false);setFocusMode(true);setFocusMode(false);settle(recording?650:250);check("rapid reversals end in requested mode with live scope",!w->property("researchFocus").toBool()&&e->property("researchSourceSlice").toBool()&&e->toPlainText()==source);
  restoreArticleReadingState(w,place);settle(80);c=e->textCursor();c.clearSelection();e->setTextCursor(c);setFocusMode(true);settle(160);e->insertPlainText("\\section{New boundary}\n");check("focused structural change invalidates stale range",ready()&&!e->property("researchSourceSlice").toBool());e->undo();ready();setFocusMode(false);settle(180);check("exit does not revive a stale pre-focus section",!e->property("researchSourceSlice").toBool());
  c=e->textCursor();c.setPosition(source.indexOf("Proof line 80"));e->setTextCursor(c);section->trigger();setFocusMode(true);settle(140);c.setPosition(source.indexOf("A different section"));e->setTextCursor(c);check("intentional navigation outside section reveals requested text",!e->property("researchSourceSlice").toBool());setFocusMode(false);settle(180);check("exit retains deliberate new location and scope",e->textCursor()==c&&!e->property("researchSourceSlice").toBool());
  setFocusMode(true);outline->trigger();settle(180);const bool opened=dock&&dock->isVisible();setFocusMode(false);settle(200);check("outline choice during focus survives exit",opened&&dock->isVisible());
  if(recording){record.finish();check("real-window recording has frames",record.failures==0&&!record.frames.isEmpty());}
  check("source file unchanged",bytes(path)==source.toUtf8());w->textDoc()->setModified(false);w->close();app.exit(failures?1:0);
 });return app.exec();
}
