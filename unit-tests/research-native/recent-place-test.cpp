// Actual recent-file persistence and passive restoration, on a private desktop.
#include "ActualAuthoringRecording.h"
#include "ResearchWritingContext.h"
#include "ResearchProject.h"
#include "ResearchTextAnchor.h"
#include "ResearchWorkflow.h"
#include "PDFDocumentWindow.h"
#include <QPdfWriter>
using namespace Research;
int main(int argc,char **argv){
 const auto report=argument(1);wchar_t desktop[256]{};DWORD needed=0;
 if(!GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()),UOI_NAME,desktop,sizeof(desktop),&needed)||QString::fromWCharArray(desktop)!=qEnvironmentVariable("TEXLINE_TEST_DESKTOP")||!QString::fromWCharArray(desktop).startsWith("TeXlineValidation-"))return 2;
 argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
 QTimer::singleShot(60,&app,[&]{
  const auto folder=QFileInfo(report).absolutePath(),file=folder+"/paper/main.tex",child=folder+"/paper/child.tex",other=folder+"/paper/other.tex";QJsonArray checks;int failures=0;
  auto check=[&](const QString &name,bool pass){checks.append(QJsonObject{{"name",name},{"passed",pass}});failures+=!pass;put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"privateNativeWindows",true},{"physicalInputMethod",false}}).toJson());};
  QTimer watchdog;watchdog.setSingleShot(true);QObject::connect(&watchdog,&QTimer::timeout,&app,[&]{check("recent-place fixture completed within bound",false);app.exit(2);});watchdog.start(120000);
  Tw::Settings().setValue("openPDFwithTeX",true);Tw::Settings().setValue("researchFocusMode",false);
  // Keep the editable section inside this file; a section containing \input
  // deliberately remains in full-file mode because its range crosses files.
  QString text="\\documentclass{article}\n\\newtheorem{theorem}{Theorem}\n\\begin{document}\n\\input{child}\n\\section{First section}\n";
  for(int n=0;n<1200;++n){if(n==800)text+="\\section{Second section}\n";text+=QString("Paragraph %1: 研究中的独立内容 and a distinct writing anchor.\n").arg(n);}text+="\\end{document}\n";
  const QString childText="% !TEX root = main.tex\n\\begin{theorem}\\label{thm:child}A closed-file theorem to keep nearby.\\end{theorem}\nA separate selected passage for later reference.\n";
  check("independent manuscript and cross-file pin samples created",put(file,text.toUtf8())&&put(child,childText.toUtf8())&&put(other,"\\documentclass{article}\n\\begin{document}Other task\\end{document}\n"));
  {QPdfWriter writer(folder+"/paper/main.pdf");writer.setResolution(72);QPainter p(&writer);for(int page=0;page<3;++page){if(page)writer.newPage();p.drawText(QRect(40,80,400,600),Qt::AlignTop,QString("Private reading-position page %1").arg(page+1));}}
  auto open=[&](const QString &name){auto *w=new TeXDocumentWindow(name);w->resize(qEnvironmentVariableIsSet("TEXLINE_TEST_HALF")?620:1040,760);w->show();return w;};
  auto *w=open(file);settle(600);check("article structure is ready",waitFor([&]{return w->property("researchArticleReady").toBool();}));
  const int at=text.indexOf("Paragraph 600:");QTextCursor cursor(w->textDoc());cursor.setPosition(at+28);cursor.setPosition(at,QTextCursor::KeepAnchor);w->editor()->setTextCursor(cursor);
  auto article=articleReadingState(w);article.insert("partial",true);restoreArticleReadingState(w,article);settle(250);
  check("fixture enters a valid single-file section before saving",articleReadingState(w).value("partial").toBool()&&w->editor()->property("researchSourceSlice").toBool());
  const int a=childText.indexOf("A separate selected passage"),b=childText.indexOf('\n',a);QVariantMap references{{"dialog",false},{"pinned",true},{"pinFile",child},{"pinOffset",a},{"pinKey",QString()},{"pinRange",true},{"pinSelection",true},{"pinTextRange",textAnchor(childText,a,b)}};
  restoreReferenceReadingState(w,references,true);auto *pin=w->findChild<QDockWidget*>("researchPinnedReference");
  check("passive closed-file selection restores without opening its source",pin&&waitFor([&]{return pin->property("researchPinnedReady").toBool();})&&!TeXDocumentWindow::findDocument(child)&&pin->findChild<QTextBrowser*>("researchPinnedSource")&&pin->findChild<QTextBrowser*>("researchPinnedSource")->toPlainText().contains("A separate selected passage"));
  w->editor()->setTextCursor(cursor);w->editor()->ensureCursorVisible();w->editor()->verticalScrollBar()->setValue(w->editor()->verticalScrollBar()->value()+w->editor()->cursorRect().top()-150);settle(250);
  QPointer<PDFDocumentWindow> pdf=w->pdfDocument();check("private real PDF opens for position restoration",pdf);if(pdf){pdf->widget()->setZoomLevel(1.1);pdf->widget()->centerOn(QPointF(250,1000));settle(150);}
  const auto expected=captureWritingPlace(w);const auto recent=captureRecentWritingPlace(w);const auto topText=w->editor()->cursorForPosition(QPoint(0,0)).block().text();const int topY=w->editor()->cursorRect(w->editor()->cursorForPosition(QPoint(0,0))).top();
  const auto namedPath=writingHistoryDirectory()+"/named/"+QString::fromLatin1(QCryptographicHash::hash(completionProjectRoot(w).toUtf8(),QCryptographicHash::Sha256).toHex())+"/places/private-preserved.json";
  const auto namedBytes=QJsonDocument(QJsonObject{{"version",1},{"name","Explicitly named place"},{"file",file},{"state",QJsonObject::fromVariantMap(expected)}}).toJson();check("private named record prepared",put(namedPath,namedBytes));
  check("recent record captures selection scope pin and reading anchor within bound",recent.value("version").toInt()==1&&recent.value("reverse").toBool()&&recent.value("article").toMap().value("partial").toBool()&&recent.value("references").toMap().value("pinned").toBool()&&QJsonDocument(QJsonObject::fromVariantMap(recent)).toJson().size()<64*1024);
  w->grab().save(folder+"/recent-before-close.png");const auto source=w->editor()->toPlainText();QPointer<TeXDocumentWindow> old=w;w->close();if(pdf)pdf->close();settle(200);
  check("successful close stores the recent writing place",old.isNull()&&!app.getFileProperties(file).value("researchRecentPlace").toMap().isEmpty());
  w=open(file);check("reopening restores once through the real load path",waitFor([&]{return w->property("researchRecentPlaceStatus").toString()=="restored"&&!w->property("researchRestoreArticlePlace").isValid()&&w->property("researchArticleReady").toBool();}));settle(300);
  check("reopen restores reverse selection and section scope",w->textCursor().position()==at&&w->textCursor().anchor()==at+28&&articleReadingState(w).value("partial").toBool()&&w->editor()->toPlainText()==source);
  auto top=w->editor()->cursorForPosition(QPoint(0,0));check("reopen restores independent reading position",top.block().text()==topText&&qAbs(w->editor()->cursorRect(top).top()-topY)<=2);
  pin=w->findChild<QDockWidget*>("researchPinnedReference");check("reopen restores fixed cross-file content without a second source window",pin&&waitFor([&]{return pin->property("researchPinnedReady").toBool();})&&!TeXDocumentWindow::findDocument(child)&&pin->findChild<QTextBrowser*>("researchPinnedSource")&&pin->findChild<QTextBrowser*>("researchPinnedSource")->toPlainText().contains("A separate selected passage"));
  pdf=w->pdfDocument();check("reopen restores PDF zoom and reading center",pdf&&qAbs(pdf->widget()->zoomLevel()-expected.value("zoom").toDouble())<.001&&QLineF(pdf->widget()->mapToScene(pdf->widget()->viewport()->rect().center()),QPointF(expected.value("pdfX").toDouble(),expected.value("pdfY").toDouble())).length()<3);w->grab().save(folder+"/recent-after-reopen.png");
  // A manual save updates the bounded record; ordinary cursor movement does not.
  const auto beforeMove=app.getFileProperties(file).value("researchRecentPlace").toMap();cursor=w->textCursor();cursor.clearSelection();cursor.setPosition(text.indexOf("Paragraph 650:"));w->editor()->setTextCursor(cursor);settle(100);
  check("moving and reading does not write a new recent record",app.getFileProperties(file).value("researchRecentPlace").toMap()==beforeMove);
  check("manual save updates current context even without new text",w->save()&&app.getFileProperties(file).value("researchRecentPlace").toMap().value("range").toMap().value("start").toInt()==cursor.position());
  check("automatic recent state leaves the named record intact",bytes(namedPath)==namedBytes);
  const auto saved=app.getFileProperties(file).value("researchRecentPlace").toMap();
  // Resolve an old anchor after an independent prefix edit; no raw offsets.
  cursor=QTextCursor(w->textDoc());cursor.setPosition(0);cursor.insertText("% A new prefix before the saved scene.\n");QString error;check("content-aware recent restore accepts a unique relocated anchor",restoreRecentWritingPlace(w,saved,&error));settle(350);
  check("changed document restores the saved content rather than old offset",w->textCursor().block().text().startsWith("Paragraph 650:"));
  const auto discard=captureRecentWritingPlace(w);check("discardable unsaved changes do not replace the last saved recent anchor",discard==saved);w->editor()->undo();w->textDoc()->setModified(false);settle(150);
  // Input/selection/scroll before the one queued restore must take precedence.
  for(int mode=0;mode<3;++mode){cancelRecentWritingPlace(w);auto full=articleReadingState(w);full.insert("partial",false);restoreArticleReadingState(w,full);cursor=QTextCursor(w->textDoc());cursor.setPosition(0);w->editor()->setTextCursor(cursor);settle(100);scheduleRecentWritingPlace(w,saved);
    if(mode==0){key(w->editor(),Qt::Key_Z,Qt::NoModifier,"z");}
    else if(mode==1){cursor.setPosition(15);cursor.setPosition(5,QTextCursor::KeepAnchor);w->editor()->setTextCursor(cursor);}
    else w->editor()->verticalScrollBar()->setValue(100);
    const auto chosen=w->textCursor();const int scroll=w->editor()->verticalScrollBar()->value();settle(250);
    check(QString("new user state cancels queued restore %1").arg(mode),w->property("researchRecentPlaceStatus").toString()=="cancelled"&&w->textCursor().position()==chosen.position()&&w->textCursor().anchor()==chosen.anchor()&&w->editor()->verticalScrollBar()->value()==scroll);
    if(mode==0){w->editor()->undo();w->textDoc()->setModified(false);}cancelRecentWritingPlace(w);
  }
  // Explicit navigation and a different window cannot be redirected by restore.
  scheduleRecentWritingPlace(w,saved);TeXDocumentWindow::openDocument(file,true,true,7);settle(200);check("explicit line navigation wins over recent restoration",w->textCursor().blockNumber()==6&&w->property("researchRecentPlaceStatus").toString()=="cancelled");
  auto *ow=open(other);settle(250);const auto otherCursor=ow->textCursor();const auto active=QApplication::activeWindow();const auto count=TeXDocumentWindow::documentList().size();
  check("passive restore stays in its own existing window",restoreRecentWritingPlace(w,saved,&error));settle(250);check("passive restore does not select another document or alter it",QApplication::activeWindow()==active&&ow->textCursor().position()==otherCursor.position()&&TeXDocumentWindow::documentList().size()==count&&!TeXDocumentWindow::findDocument(child));
  scheduleRecentWritingPlace(ow,saved);settle(100);check("another file rejects an unrelated saved scene",!ow->findChild<QObject*>("researchPendingRecentPlace")&&ow->textCursor().position()==otherCursor.position());
  auto ambiguous=saved;auto range=ambiguous.value("range").toMap();range.insert("hash","different");range.insert("before","missing anchor");ambiguous.insert("range",range);const auto unchanged=w->textCursor();check("ambiguous saved position reports failure without moving cursor",!restoreRecentWritingPlace(w,ambiguous,&error)&&!error.isEmpty()&&w->textCursor().position()==unchanged.position());
  auto named=captureWritingPlace(w);auto namedArticle=named.value("article").toMap();namedArticle.insert("partial",false);named.insert("article",namedArticle);auto namedReferences=named.value("references").toMap();namedReferences.insert("pinned",false);namedReferences.insert("pinRestore",false);named.insert("references",namedReferences);named.remove("layout");
  scheduleRecentWritingPlace(w,saved);check("explicit named restore accepts the same cursor with a new scope",restoreWritingPlace(named,&error));settle(300);
  check("explicit restoration supersedes queued recent layout and pin",!articleReadingState(w).value("partial").toBool()&&!w->findChild<QObject*>("researchPendingRecentPlace")&&(!w->findChild<QDockWidget*>("researchPinnedReference")||w->findChild<QDockWidget*>("researchPinnedReference")->isHidden()));
  watchdog.stop();for(auto *window:TeXDocumentWindow::documentList())window->textDoc()->setModified(false);for(auto *window:TeXDocumentWindow::documentList())window->close();for(auto *widget:QApplication::topLevelWidgets())if(auto *window=qobject_cast<PDFDocumentWindow*>(widget))window->close();settle(200);app.exit(failures?1:0);
 });return app.exec();
}
