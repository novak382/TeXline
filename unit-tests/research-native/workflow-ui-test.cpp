#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "TeXDocumentWindow.h"
#include "CompletingEdit.h"
#include "ResearchWorkflow.h"
#include "ResearchCollaboration.h"
#include "ResearchAppearance.h"
#include "Settings.h"
#include <QAction>
#include <QDialog>
#include <QDockWidget>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QListWidget>
#include <QPushButton>
#include <QTextBrowser>
#include <QToolButton>
#include <QTimer>
#include <QStatusBar>
#include <QThread>
#include <QElapsedTimer>
#include <QScreen>
#include <QDialogButtonBox>
static void settle(int ms=250){QElapsedTimer t;t.start();while(t.elapsed()<ms){QCoreApplication::processEvents();QThread::msleep(2);}}
int main(int argc,char **argv){const QString report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
 QTimer::singleShot(80,&app,[&]{
  QJsonArray checks;int failures=0;auto check=[&](QString name,bool passed){checks.append(QJsonObject{{"name",name},{"passed",passed}});failures+=!passed;QFile f(report);if(f.open(QIODevice::WriteOnly))f.write(QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures}}).toJson());};
  const QString root=QFileInfo(report).absolutePath()+"/main.tex";const QString original=QStringLiteral("\\documentclass{article}\n\\begin{document}\nA\u00a0B\nOriginal paragraph.\n\\end{document}\n");
  QFile file(root);if(file.open(QIODevice::WriteOnly))file.write(original.toUtf8());file.close();Tw::Settings().setValue("openPDFwithTeX",false);
  auto *w=new TeXDocumentWindow(root);w->show();settle();check("ordinary paper has no slide controller",!w->findChild<QObject *>("researchSlides"));
  auto put=[&](QString text){QFile f(root);if(f.open(QIODevice::WriteOnly))f.write(text.toUtf8());};
  auto *c=w->collaboration();c->acceptedDisk(original);const QString remote=QString(original).replace("Original paragraph.","Remote paragraph.");put(remote);c->receive(remote);settle();
  check("external merge preserves NBSP and changes only requested paragraph",w->textDoc()->editorText()==remote&&c->reviewCount()==1&&c->conflictCount()==0);
  c->review();settle();auto *collapse=w->findChild<QAction *>("researchReviewCollapse");auto *done=w->findChild<QAction *>("researchReviewDone");
  check("review has separate collapse and confirmation",collapse&&done&&done->isEnabled());if(collapse)collapse->trigger();check("collapsing retains pending review",c->reviewCount()==1&&!w->findChild<QDockWidget *>("researchReviewPanel")->isVisible());
  c->review();if(done)done->trigger();check("confirm acknowledges ordinary review",c->reviewCount()==0);
  auto local=remote;local.replace("Remote paragraph.","Local paragraph.");w->editor()->setPlainText(local);const auto peer=QString(remote).replace("Remote paragraph.","Other paragraph.");put(peer);c->receive(peer);c->review();settle();
  check("conflict disables confirmation while keeping collapse available",c->conflictCount()>0&&!done->isEnabled()&&collapse->isEnabled());collapse->trigger();check("collapse retains conflict",c->conflictCount()>0);
  c->resolveCurrent(false);settle();const auto current=w->textDoc()->editorText();
  Research::saveFailure(w,QStringLiteral("Private save failure"));auto *issue=w->findChild<QToolButton *>("researchSaveFailureButton");check("save failure remains reachable",issue&&issue->isVisible()&&issue->toolTip()=="Private save failure");settle();check("save failure action fits the shared compact status row",issue&&issue->height()==22&&w->statusBar()->height()==28&&w->statusBar()->rect().contains(issue->geometry()));
  Research::showTransientStatusMessage(w,QStringLiteral("Routine update"));check("routine feedback does not replace a save failure",w->statusBar()->currentMessage()=="Private save failure");
  Research::saved(w,root);check("successful save clears failure indicator",issue->isHidden()&&!w->property("researchSaveFailure").isValid());
  const QString historical=QString(current).replace("Local paragraph.","Historical paragraph.");check("history record retained",Research::recordRevision(w,historical));
  QAction *history=w->findChild<QAction *>("researchHistoryAction");check("history action reachable",history);
  bool diffShown=false,copyClicked=false;const auto windows=TeXDocumentWindow::documentList();
  QTimer::singleShot(150,&app,[&]{auto *dialog=w->findChild<QDialog *>("researchPalette");check("history dialog opened",dialog);if(!dialog)return;auto *list=dialog->findChild<QListWidget *>();auto *preview=dialog->findChild<QTextBrowser *>("researchHistoryPreview");auto *copy=dialog->findChild<QPushButton *>("researchHistoryOpenCopy");
    if(list)for(int i=0;i<list->count();++i){list->setCurrentRow(i);if(preview&&preview->toPlainText().contains("Historical")){diffShown=preview->toPlainText().contains("Local");break;}}
    dialog->grab().save(QFileInfo(report).absolutePath()+"/history.png");
    check("history and its confirmation fit the work area",dialog->screen()->availableGeometry().contains(dialog->frameGeometry())&&dialog->rect().contains(QRect(copy->parentWidget()->mapTo(dialog,QPoint()),copy->parentWidget()->size())));
    if(copy){copyClicked=true;copy->click();}else dialog->reject();
  });if(history)history->trigger();settle();
  TeXDocumentWindow *newCopy=nullptr;for(auto *other:TeXDocumentWindow::documentList())if(!windows.contains(other))newCopy=other;
  check("history shows changed current and historical text",diffShown);check("open copy leaves current text intact",copyClicked&&newCopy&&newCopy->untitled()&&newCopy->textDoc()->editorText()==historical&&w->textDoc()->editorText()==current);
  w->grab().save(QFileInfo(report).absolutePath()+"/workflow.png");for(auto *other:TeXDocumentWindow::documentList())other->setModified(false);app.exit(failures?1:0);
 });return app.exec();}
