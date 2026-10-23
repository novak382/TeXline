#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "Engine.h"
#include "TeXDocumentWindow.h"
#include "PDFDocumentWindow.h"
#include "PDFBackend.h"
#include "CompletingEdit.h"
#include "ResearchBuild.h"
#include "ResearchAppearance.h"
#include "Settings.h"
#include <QAction>
#include <QAbstractButton>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QPointer>
#include <QSaveFile>
#include <QScreen>
#include <QThread>
#include <QTextBrowser>
#include <QTimer>
#include <functional>
static bool waitFor(const std::function<bool()> &f,int ms=90000){QElapsedTimer t;t.start();while(!f()&&t.elapsed()<ms){QCoreApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QThread::msleep(3);}return f();}
static void settle(int ms=300){QElapsedTimer t;t.start();waitFor([&]{return t.elapsed()>=ms;},ms+1000);}
static QByteArray read(const QString &p){QFile f(p);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray();}
static void put(const QString &p,const QByteArray &b){QSaveFile f(p);if(f.open(QIODevice::WriteOnly)){f.write(b);f.commit();}}
int main(int argc,char **argv){const QString report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
 QTimer::singleShot(100,&app,[&]{
  const auto folder=QFileInfo(report).absolutePath(),root=qEnvironmentVariable("TEXLINE_BUILD_PAPER_ROOT");QJsonArray checks;int failures=0;
  auto check=[&](QString name,bool good){checks.append(QJsonObject{{"name",name},{"passed",good}});failures+=!good;put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures}}).toJson());};
  check("paper input is a private copy under this report",!root.isEmpty()&&QFileInfo(root).absoluteFilePath().startsWith(folder+"/")&&!read(root).isEmpty());if(failures){app.exit(1);return;}
  Research::setBuildCacheForTests(folder+"/cache");Tw::Settings().setValue("openPDFwithTeX",false);Tw::Settings().setValue("researchFocusMode",false);
  app.setEngineList({Engine("pdfLaTeX","C:/texlive/2023/bin/windows/pdflatex.exe",{"-synctex=1","-interaction=nonstopmode","-file-line-error","$fullname"},true)});
  auto *w=new TeXDocumentWindow(root);w->resize(1280,820);w->show();settle();const auto original=w->editor()->toPlainText();
  auto compile=[&]{w->typeset();return waitFor([&]{return !w->isTypesetting()&&!w->property("researchBuildPreparing").toBool()&&!w->property("researchBuildFinishing").toBool()&&!w->property("researchPublishing").toBool();});};
  check("real paper compiles with certified source and PDF",compile()&&Research::pdfPairValid(Research::previewFile(root)));const auto preview=Research::previewFile(root);const auto goodPdf=read(preview);
  auto cursor=w->editor()->textCursor();const auto body=original.indexOf("\\begin{document}");check("real paper body is found",body>=0);if(body<0){app.exit(2);return;}cursor.setPosition(body+QString("\\begin{document}").size());cursor.insertText("\n\\noindent Independent workflow validation paragraph.\\par\n");const auto edited=w->editor()->toPlainText();
  w->editor()->undo();check("paper edit undoes to complete original",w->editor()->toPlainText()==original);w->editor()->redo();check("paper edit redoes exactly",w->editor()->toPlainText()==edited);
  check("save stores exact edited paper",w->save()&&QString::fromUtf8(read(root))==edited&&!w->isModified());
  cursor=w->editor()->textCursor();cursor.movePosition(QTextCursor::Start);cursor.insertText("\\UndefinedMaturityAuditCommand\n");const auto broken=w->editor()->toPlainText();
  check("failed compilation finishes without destroying last good PDF",compile()&&read(preview)==goodPdf);check("failed source is retained for repair",w->editor()->toPlainText()==broken);check("failed build cannot certify the stale preview",!Research::pdfPairValid(preview));
  put(folder+"/failed-console.txt",w->property("consoleOutput").toString().toUtf8());
  auto *diagnostic=w->findChild<QTextBrowser *>("researchProblems");check("first visible diagnostic identifies the actual error",diagnostic&&diagnostic->toPlainText().contains("Undefined control sequence"));
  w->grab().save(folder+"/paper-compile-error.png");w->editor()->undo();check("undo repairs only the deliberate error",w->editor()->toPlainText()==edited);
  check("repaired paper compiles and certifies again",compile()&&Research::pdfPairValid(Research::previewFile(root)));auto rendered=QtPDF::Backend::Document::newDocument(Research::previewFile(root));bool paragraphVisible=false;if(rendered)for(int page=0;page<int(rendered->numPages());++page)paragraphVisible|=!rendered->page(page).toStrongRef()->search("Independent workflow validation paragraph",{}).isEmpty();check("PDF includes the newly written visible paragraph",paragraphVisible);rendered.reset();QString publishError;check("export publishes the current certified paper PDF",Research::publishPreview(root,&publishError)&&read(QFileInfo(root).absolutePath()+"/"+QFileInfo(root).completeBaseName()+".pdf")==read(Research::previewFile(root)));cursor=w->editor()->textCursor();cursor.setPosition(w->editor()->toPlainText().indexOf("Independent workflow"));w->editor()->setTextCursor(cursor);w->editor()->ensureCursorVisible();settle();w->grab().save(folder+"/paper-writing.png");
  QJsonArray inventory;for(auto *action:w->findChildren<QAction *>())if(!action->isSeparator()&&!action->text().isEmpty())inventory.append(QJsonObject{{"kind","action"},{"id",action->objectName()},{"text",action->text()},{"hint",action->toolTip()},{"shortcut",action->shortcut().toString()},{"enabled",action->isEnabled()},{"visible",action->isVisible()}});
  for(auto *button:w->findChildren<QAbstractButton *>())inventory.append(QJsonObject{{"kind","button"},{"id",button->objectName()},{"text",button->text()},{"hint",button->toolTip()},{"enabled",button->isEnabled()},{"visible",button->isVisible()}});put(folder+"/ui-inventory.json",QJsonDocument(inventory).toJson());
  Research::setFocusMode(true);settle();auto *pdf=new PDFDocumentWindow(Research::previewFile(root),w);pdf->show();const auto area=w->screen()->availableGeometry();w->setGeometry(area.x(),area.y(),area.width()/2,area.height());pdf->setGeometry(area.x()+area.width()/2,area.y(),area.width()-area.width()/2,area.height());settle(900);
  check("focus source and PDF are independent top-level windows",w->isWindow()&&pdf->isWindow()&&w->geometry().right()<pdf->geometry().left()+4);w->grab().save(folder+"/focus-source.png");pdf->grab().save(folder+"/focus-pdf.png");
  check("focus transitions retain exact paper",w->editor()->toPlainText()==edited);pdf->close();Research::setFocusMode(false);settle();
  cursor=w->editor()->textCursor();cursor.movePosition(QTextCursor::Start);cursor.insertText("% Unsaved close test\n");const auto draft=w->editor()->toPlainText();bool prompted=false;QTimer dismiss;dismiss.setInterval(20);QObject::connect(&dismiss,&QTimer::timeout,&app,[&]{for(auto *top:QApplication::topLevelWidgets())if(auto *box=qobject_cast<QMessageBox *>(top);box&&box->isVisible()){prompted=true;box->done(QMessageBox::Cancel);}});dismiss.start();const bool closed=w->close();dismiss.stop();
  check("cancel close protects unsaved paper",prompted&&!closed&&w->isVisible()&&w->editor()->toPlainText()==draft&&w->isModified());check("final save preserves draft",w->save()&&QString::fromUtf8(read(root))==draft);
  QPointer<TeXDocumentWindow> guard=w;w->close();settle();auto *reopened=new TeXDocumentWindow(root);reopened->show();settle();check("close and reopen restores complete saved content",reopened->editor()->toPlainText()==draft&&!reopened->isModified());reopened->grab().save(folder+"/paper-reopened.png");reopened->setModified(false);reopened->close();app.exit(failures?1:0);
 });QTimer::singleShot(300000,&app,[&]{app.exit(92);});return app.exec();}
