#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include <QAction>
#include <QToolButton>
#include "TeXDocumentWindow.h"
#include "ResearchCollaboration.h"
#include "ResearchAppearance.h"
#include "Settings.h"
#include "CompletingEdit.h"
#include <QDialog>
#include <QDialogButtonBox>
#include <QPlainTextEdit>
#include <QTextBrowser>
#include <QPushButton>
#include <QPointer>
#include <QSaveFile>
#include <QDirIterator>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QElapsedTimer>
#include <QThread>
#include <QTimer>
#include <QScreen>

static bool put(const QString &path,const QString &text){QDir().mkpath(QFileInfo(path).absolutePath());const auto bytes=text.toUtf8();QElapsedTimer t;t.start();do{QSaveFile f(path);if(f.open(QIODevice::WriteOnly)&&f.write(bytes)==bytes.size()&&f.commit())return true;QThread::msleep(4);}while(t.elapsed()<500);return false;}
static void settle(int ms){QElapsedTimer t;t.start();while(t.elapsed()<ms){QCoreApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QThread::msleep(2);}}
static bool stored(const QString &path,const QString &text){QDirIterator files(QCoreApplication::applicationDirPath()+"/settings/research-history",{"*.json"},QDir::Files,QDirIterator::Subdirectories);while(files.hasNext()){QFile f(files.next());if(f.open(QIODevice::ReadOnly)){const auto object=QJsonDocument::fromJson(f.readAll()).object();if(QFileInfo(object.value("path").toString()).absoluteFilePath()==QFileInfo(path).absoluteFilePath()&&object.value("text").toString()==text)return true;}}return false;}
int main(int argc,char **argv){const QString report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);QTimer::singleShot(100,&app,[&]{
    Tw::Settings().setValue("openPDFwithTeX",false);if(qEnvironmentVariableIntValue("TEXLINE_REVIEW_FOCUS"))Research::setFocusMode(true);
    QJsonArray checks;int failures=0;auto check=[&](const QString &name,bool ok){checks.append(QJsonObject{{"name",name},{"passed",ok}});failures+=!ok;};
    const QString folder=QFileInfo(report).absolutePath();
    for(int scenario=0;scenario<4;++scenario){const QString root=folder+QString("/合并 中文𐐀/%1/main.tex").arg(scenario),base="\\documentclass{article}\n\\begin{document}\nContext before.\nAlpha original paragraph.\nContext after.\n\\end{document}\n";
        check(QString("scenario %1 writes a private source").arg(scenario),put(root,base));QPointer<TeXDocumentWindow> w=new TeXDocumentWindow(root);w->show();settle(150);
        QString local=base,remote=base;local.replace("original","local");remote.replace("original","external");QTextCursor edit(w->textDoc());edit.select(QTextCursor::Document);edit.insertText(local);
        check(QString("scenario %1 commits the actual peer version").arg(scenario),put(root,remote));w->collaboration()->receive(remote);w->collaboration()->review();settle(80);
        const auto selected=w->editor()->textCursor();QString expected=local;expected.replace(selected.selectionStart(),selected.selectionEnd()-selected.selectionStart(),"joined");
        auto *merge=w->findChild<QAction*>("researchEditMerge");check(QString("scenario %1 exposes a custom merge action").arg(scenario),merge&&merge->isVisible());
        bool seen=false;QTimer automation,watchdog;automation.setInterval(10);watchdog.setSingleShot(true);
        QObject::connect(&watchdog,&QTimer::timeout,&app,[&]{for(auto *top:QApplication::topLevelWidgets())if(auto *d=qobject_cast<QDialog*>(top);d&&d->objectName()=="researchMergeDialog")d->reject();});
        QObject::connect(&automation,&QTimer::timeout,&app,[&]{for(auto *top:QApplication::topLevelWidgets())if(auto *dialog=qobject_cast<QDialog*>(top);dialog&&dialog->objectName()=="researchMergeDialog"){
            automation.stop();seen=true;auto *draft=dialog->findChild<QPlainTextEdit*>("researchMergeDraft");
            auto *context=dialog->findChild<QTextBrowser*>("researchMergeContext");auto *buttons=dialog->findChild<QDialogButtonBox*>("researchMergeButtons");
            check(QString("scenario %1 shows both alternatives with paragraph context").arg(scenario),context&&context->toPlainText().contains("local")&&context->toPlainText().contains("external")&&context->toPlainText().contains("Context before."));
            check(QString("scenario %1 modal and action buttons fit the work area").arg(scenario),dialog->screen()->availableGeometry().contains(dialog->frameGeometry())&&buttons&&buttons->button(QDialogButtonBox::Ok)->isVisible()&&dialog->rect().contains(buttons->geometry()));
            const bool english=Tw::Settings().value("locale").toString().startsWith("en");
            check(QString("scenario %1 localized merge captions match the private language").arg(scenario),dialog->windowTitle()==(english?"Merge Editor":"合并编辑器")&&buttons&&buttons->button(QDialogButtonBox::Ok)->text()==(english?"Complete Merge":"完成合并")&&context&&context->toPlainText().contains(english?"Current edits":"当前编辑"));
            dialog->grab().save(folder+QString("/merge-%1.png").arg(scenario));
            if(!draft||!buttons){dialog->reject();return;}draft->setPlainText("joined");
            if(scenario==0)buttons->button(QDialogButtonBox::Ok)->click();
            else if(scenario==1)buttons->button(QDialogButtonBox::Cancel)->click();
            else if(scenario==2){QString newer=remote;newer.replace("external","newest");check("new peer commits while the merge is open",put(root,newer));w->collaboration()->receive(newer);buttons->button(QDialogButtonBox::Ok)->click();}
            else {w->setModified(false);w->close();}
            return;
        }});
        if(merge){automation.start();watchdog.start(8000);merge->trigger();automation.stop();watchdog.stop();}
        check(QString("scenario %1 opens the real merge modal").arg(scenario),seen);settle(80);
        if(scenario==0){check("custom merge changes only the selected fragment and retires its conflict",w&&w->editor()->toPlainText()==expected&&w->collaboration()->conflictCount()==0&&w->collaboration()->reviewCount()==0);check("both complete alternatives are retained in private local history",stored(root,local)&&stored(root,remote));if(w){w->editor()->undo();check("one undo restores the complete original local buffer",w->editor()->toPlainText()==local);}}
        else if(scenario==1)check("cancel preserves local text and the pending conflict",w&&w->editor()->toPlainText()==local&&w->collaboration()->conflictCount()==1);
        else if(scenario==2){check("stale custom draft never overwrites a newer received version",w&&w->editor()->toPlainText()==local&&w->collaboration()->conflictCount()==1);check("stale complete custom draft remains recoverable in local history",stored(root,expected));}
        else check("closing the source during its modal releases it safely",w.isNull());
        if(w){w->setModified(false);delete w;}settle(80);
    }
    {
        const QString root=folder+"/many/main.tex";QString original="\\documentclass{article}\n\\begin{document}\n";
        for(int n=0;n<40;++n)original+=QString("Paragraph %1: original.\nStable separator %1.\n").arg(n);
        original+="\\end{document}\n";QString peer=original;peer.replace("original","external");
        check("large review starts with a private baseline",put(root,original));auto *w=new TeXDocumentWindow(root);w->show();settle(120);check("large peer version is committed",put(root,peer));auto *c=w->collaboration();c->receive(peer);
        check("forty disjoint changes are individually represented",c->reviewCount()==40);
        for(int n=0;n<40&&c->reviewCount();++n)c->resolveCurrent(true);
        check("forty individual accepts preserve the exact peer buffer",c->reviewCount()==0&&w->editor()->toPlainText()==peer);
        check("complete pre-review and peer versions survive rolling history retention",stored(root,original)&&stored(root,peer));
        w->setModified(false);delete w;settle(80);
    }
    {
        const QString root=folder+"/unavailable-history/main.tex",base="\\documentclass{article}\n\\begin{document}Original.\\end{document}\n";
        check("history-failure private baseline is written",put(root,base));auto *w=new TeXDocumentWindow(root);w->show();settle(120);
        QString local=base,peer=base;local.replace("Original","Local");peer.replace("Original","Peer");QTextCursor edit(w->textDoc());edit.select(QTextCursor::Document);edit.insertText(local);check("history-failure peer is committed",put(root,peer));auto *c=w->collaboration();c->receive(peer);
        const QString history=QCoreApplication::applicationDirPath()+"/settings/research-history";QString leaf;
        QDirIterator files(history,{"*.json"},QDir::Files,QDirIterator::Subdirectories);while(files.hasNext()){QFile f(files.next());if(QFileInfo(f).absolutePath()==history)continue;if(f.open(QIODevice::ReadOnly)){const auto o=QJsonDocument::fromJson(f.readAll()).object();if(QFileInfo(o.value("path").toString()).absoluteFilePath()==QFileInfo(root).absoluteFilePath()){leaf=QFileInfo(f).absolutePath();break;}}}
        const bool owned=!leaf.isEmpty()&&QFileInfo(leaf).canonicalFilePath().startsWith(QFileInfo(history).canonicalFilePath()+"/",Qt::CaseInsensitive);const QString name=QFileInfo(leaf).fileName(),backup=name+"-private-block-test";QDir parent(history);
        const bool moved=owned&&parent.rename(name,backup);QFile blocker(leaf);const bool blocked=moved&&blocker.open(QIODevice::WriteOnly)&&blocker.write("private-block")==13;blocker.close();check("only the private history leaf is blocked",blocked);
        if(blocked)c->resolveCurrent(true);
        check("unavailable history refuses a decision without losing either buffer alternative",blocked&&c->conflictCount()==1&&w->editor()->toPlainText()==local);
        const bool restored=moved&&QFile::remove(leaf)&&parent.rename(backup,name);check("the private history leaf is restored",restored);
        if(restored)c->resolveCurrent(true);check("the same decision succeeds after history storage recovers",restored&&c->conflictCount()==0&&w->editor()->toPlainText()==peer&&stored(root,local)&&stored(root,peer));
        w->setModified(false);delete w;settle(80);
    }
    QSaveFile f(report);if(f.open(QIODevice::WriteOnly)){f.write(QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"physicalInputVerified",false}}).toJson());f.commit();}app.exit(failures?1:0);
});return app.exec();}
