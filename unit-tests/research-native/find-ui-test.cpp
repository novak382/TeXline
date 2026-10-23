#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "TeXDocumentWindow.h"
#include "CompletingEdit.h"
#include "ResearchAppearance.h"
#include <QCheckBox>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QTimer>
#include <QThread>
#include <QToolBar>
#include <QToolButton>
#include <QListWidget>
static void settle(int ms=200){QElapsedTimer t;t.start();while(t.elapsed()<ms){QCoreApplication::processEvents();QThread::msleep(2);}}
int main(int argc,char **argv){const QString report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);QTimer::singleShot(80,&app,[&]{
 QJsonArray checks;int failures=0;auto check=[&](QString name,bool passed){checks.append(QJsonObject{{"name",name},{"passed",passed}});failures+=!passed;QFile f(report);if(f.open(QIODevice::WriteOnly))f.write(QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures}}).toJson());};
 auto *w=new TeXDocumentWindow();w->resize(1100,760);w->show();auto *e=w->editor();const QString original=QStringLiteral("alpha ALPHA alphabet\nA\u00a0B alpha\n");e->setPlainText(original);e->moveCursor(QTextCursor::Start);w->doReplaceDialog();settle();
 auto *query=w->findChild<QLineEdit *>("researchFindField"),*replacement=w->findChild<QLineEdit *>("researchReplaceField");auto *count=w->findChild<QLabel *>("researchFindCount");auto *next=w->findChild<QToolButton *>("researchFindNext");auto *previous=w->findChild<QToolButton *>("researchFindPrevious");auto *one=w->findChild<QToolButton *>("researchReplaceOne"),*all=w->findChild<QToolButton *>("researchReplaceAll");auto *words=w->findChild<QCheckBox *>("researchFindWords");auto *regex=w->findChild<QCheckBox *>("researchFindRegex");auto *scope=w->findChild<QComboBox *>("researchFindScope");
 check("replace opens the same nonmodal search panel",query&&replacement&&replacement->isVisible()&&!QApplication::activeModalWidget());if(!query){app.exit(1);return;}
 query->setText("alpha");words->setChecked(true);settle();check("whole-word count excludes alphabet",count->text().contains("0 / 3"));next->click();check("next selects first result",e->textCursor().selectionStart()==0&&e->textCursor().selectedText()=="alpha"&&count->text().contains("1 / 3"));next->click();previous->click();check("previous returns to first result",e->textCursor().selectionStart()==0);
 replacement->setText("x");one->click();
 check("single replacement preserves other text and NBSP",e->toPlainText().startsWith("x ALPHA alphabet")&&w->textDoc()->editorText().contains(QChar::Nbsp));e->undo();check("single replacement has one-step undo",w->textDoc()->editorText()==original);
 all->click();check("replace all respects whole words and NBSP",w->textDoc()->editorText()==QStringLiteral("x x alphabet\nA\u00a0B x\n"));e->undo();check("replace all has one-step undo",w->textDoc()->editorText()==original);
 words->setChecked(false);regex->setChecked(true);query->setText("(alpha)(bet)?");replacement->setText("\\1!");all->click();check("capture replacement retains full-source context",w->textDoc()->editorText()==QStringLiteral("alpha! ALPHA! alpha!\nA\u00a0B alpha!\n"));e->undo();
 query->setText("(?=alpha)");replacement->setText("/");all->click();check("zero-length matches terminate and replace once per position",w->textDoc()->editorText()==QStringLiteral("/alpha /ALPHA /alphabet\nA\u00a0B /alpha\n"));e->undo();
 regex->setChecked(false);auto selection=e->textCursor();selection.setPosition(0);selection.setPosition(5,QTextCursor::KeepAnchor);e->setTextCursor(selection);w->doReplaceDialog();scope->setCurrentIndex(scope->findData("selection"));query->setText("alpha");replacement->setText("selected");all->click();check("selection scope never changes text outside the selection",w->textDoc()->editorText()==QString(original).replace(0,5,"selected"));e->undo();
 const auto evidence=QFileInfo(report).absolutePath();auto *findBar=w->findChild<QToolBar *>("researchFindBar");findBar->grab().save(evidence+"/find-controls.png");
 const int navigationGap=next->geometry().left()-previous->geometry().right()-1;check("find navigation arrows stay together above wide replace actions",navigationGap>=0&&navigationGap<=10);
 QJsonArray controlGeometry;for(auto *button:findBar->findChildren<QToolButton *>())if(button->isVisible()&&!button->text().isEmpty())controlGeometry.append(QJsonObject{{"name",button->objectName()},{"text",button->text()},{"width",button->width()},{"textWidth",button->fontMetrics().horizontalAdvance(button->text())},{"height",button->height()},{"devicePixelRatio",button->devicePixelRatioF()}});
 QFile layout(evidence+"/find-controls.json");if(layout.open(QIODevice::WriteOnly))layout.write(QJsonDocument(controlGeometry).toJson());
 w->findChild<QToolButton *>("researchFindClose")->click();check("close hides panel and preserves editor content",w->findChild<QToolBar *>("researchFindBar")->isHidden()&&w->textDoc()->editorText()==original);
 w->grab().save(QFileInfo(report).absolutePath()+"/find.png");w->setModified(false);
 const auto file=QFileInfo(report).absolutePath()+"/scopes.tex";const QString slides="\\documentclass{beamer}\n\\begin{document}\n\\begin{frame}{First}\nfirst token\n\\end{frame}\n\\begin{frame}{Second}\nsecond token\n\\end{frame}\n\\end{document}\n";{QFile f(file);if(f.open(QIODevice::WriteOnly))f.write(slides.toUtf8());}
 auto *s=new TeXDocumentWindow(file);s->show();settle(900);auto *list=s->findChild<QListWidget *>("researchSlideList");check("scope fixture opens in slide mode",list&&list->count()==2);if(list){list->setCurrentRow(0);s->doReplaceDialog();auto *q=s->findChild<QLineEdit *>("researchFindField");q->setText("token");list->setCurrentRow(1);settle();s->findChild<QLineEdit *>("researchReplaceField")->setText("revised");s->findChild<QToolButton *>("researchReplaceAll")->click();check("switching slides refreshes current-slide replacement scope",s->textDoc()->editorText()==QString(slides).replace("second token","second revised"));s->editor()->undo();check("slide-scoped replacement has one undo",s->textDoc()->editorText()==slides);}s->setModified(false);app.exit(failures?1:0);
 });return app.exec();}
