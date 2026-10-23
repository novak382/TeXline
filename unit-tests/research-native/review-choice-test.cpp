#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include <QAction>
#include <QToolButton>
#include "ResearchCollaboration.h"
#include "TeXDocumentWindow.h"
#include "ResearchAppearance.h"
#include "Settings.h"
#include "CompletingEdit.h"
#include <QSaveFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QElapsedTimer>
#include <QThread>
#include <QDir>
#include <QPushButton>
#include <QTextBrowser>

static bool put(const QString &path,const QString &text){QDir().mkpath(QFileInfo(path).absolutePath());const auto data=text.toUtf8();QElapsedTimer timer;timer.start();do{QSaveFile f(path);if(f.open(QIODevice::WriteOnly)&&f.write(data)==data.size()&&f.commit()){QFile verify(path);return verify.open(QIODevice::ReadOnly)&&verify.readAll()==data;}QThread::msleep(4);}while(timer.elapsed()<500);return false;}
static void settle(int ms){QElapsedTimer t;t.start();while(t.elapsed()<ms){QCoreApplication::processEvents();QThread::msleep(2);}}
int main(int argc,char **argv){const QString report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);QTimer::singleShot(100,&app,[&]{using namespace Research;
    QJsonArray checks;int failures=0;auto check=[&](const QString &name,bool ok){checks.append(QJsonObject{{"name",name},{"passed",ok}});failures+=!ok;};Tw::Settings().setValue("openPDFwithTeX",false);if(qEnvironmentVariableIntValue("TEXLINE_REVIEW_FOCUS"))Research::setFocusMode(true);
    const QString root=QFileInfo(report).absolutePath()+"/逐处审核 中文/main.tex";
    const QString base="\\documentclass{article}\n\\begin{document}\nAlpha original paragraph.\nStable paragraph context.\n\\begin{equation}\nx=1\n\\end{equation}\n\\end{document}\n";
    QString remote=base;remote.replace("original","external");remote.replace("x=1","x=2");check("private base is written",put(root,base));
    auto *w=new TeXDocumentWindow(root);w->resize(1000,800);w->show();settle(100);auto *c=w->collaboration();check("private remote is written",put(root,remote));c->receive(remote);c->review();settle(100);
    auto *mine=w->findChild<QAction*>("researchKeepLocal");auto *theirs=w->findChild<QPushButton*>("researchUseRemote");
    check("ordinary received changes offer individual keep and accept",mine&&theirs&&mine->isVisible()&&theirs->isVisible());
    auto *preview=w->findChild<QTextBrowser*>("researchReviewText");check("review shows paragraph context surrounding the precise change",preview&&preview->toPlainText().contains("Alpha")&&preview->toPlainText().contains("Stable paragraph context."));
    if(mine)mine->trigger();
    check("keeping one ordinary change restores only its original fragment",w->editor()->toPlainText().contains("Alpha original")&&w->editor()->toPlainText().contains("x=2")&&c->reviewCount()==1);
    if(theirs)theirs->click();check("accepting the next ordinary change retires only that item",c->reviewCount()==0&&w->editor()->toPlainText().contains("Alpha original")&&w->editor()->toPlainText().contains("x=2"));
    w->grab().save(QFileInfo(report).absolutePath()+"/review-choices.png");w->setModified(false);delete w;settle(100);

    const QString path=QFileInfo(report).absolutePath()+"/conflict/main.tex",original="\\documentclass{article}\n\\begin{document}\nAlpha initial.\nSeparator unchanged.\nBeta initial.\n\\end{document}\n";
    check("second private base is written",put(path,original));w=new TeXDocumentWindow(path);w->show();c=w->collaboration();QString local=original;local.replace("Alpha initial","Alpha local");local.replace("Beta initial","Beta local");QTextCursor edit(w->textDoc());edit.select(QTextCursor::Document);edit.insertText(local);
    remote=original;remote.replace("Alpha initial","Alpha peer");remote.replace("Beta initial","Beta peer");check("overlapping remote is committed",put(path,remote));c->receive(remote);check("two overlaps remain pending",c->conflictCount()==2);c->resolveCurrent(true);
    QString newer=remote;newer.replace("Alpha peer","Alpha newer");check("new peer version is committed",put(path,newer));c->receive(newer);
    check("accepted overlap follows newer remote while the other conflict remains",w->editor()->toPlainText().contains("Alpha newer")&&w->editor()->toPlainText().contains("Beta local")&&c->conflictCount()==1);
    c->review();settle(100);auto *merge=w->findChild<QAction*>("researchEditMerge");check("conflict exposes an explicit custom merge entry",merge&&merge->isVisible());
    w->setModified(false);delete w;settle(100);QSaveFile f(report);if(f.open(QIODevice::WriteOnly)){f.write(QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures}}).toJson());f.commit();}app.exit(failures?1:0);
});return app.exec();}
