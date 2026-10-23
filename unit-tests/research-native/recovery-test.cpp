#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include <QElapsedTimer>
#include "ResearchWorkflow.cpp"
#include "ResearchCollaboration.h"
#include "Settings.h"
#include <QJsonArray>
#include <QThread>
static void settleRecovery(int ms){QElapsedTimer t;t.start();while(t.elapsed()<ms){QCoreApplication::processEvents();QThread::msleep(3);}}
int main(int argc,char **argv){QString report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
QTimer::singleShot(80,&app,[&]{using namespace Research;QJsonArray checks;int failures=0;QString folder=QFileInfo(report).absolutePath()+"/recovery-fixtures";testStorageOverride=folder+"/history";Tw::Settings().setValue("openPDFwithTeX",false);
auto put=[&](QString path,QString text){QDir().mkpath(QFileInfo(path).absolutePath());QSaveFile f(path);auto b=text.toUtf8();return f.open(QIODevice::WriteOnly)&&f.write(b)==b.size()&&f.commit();};
auto check=[&](QString name,bool pass){checks.append(QJsonObject{{"name",name},{"passed",pass}});failures+=!pass;put(report,QString::fromUtf8(QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures}}).toJson()));};
const QString source=folder+"/main.tex";put(source,"Disk source.\n");auto *w=new TeXDocumentWindow(source);w->show();settleRecovery(200);w->setProperty("researchRecoveryReady",true);
const QString id=identity(w),dir=storage()+"/"+id;QDir(dir).removeRecursively();
QSet<QString> expected;for(int n=0;n<200;++n){QString text=QString("Rapid revision %1").arg(n);recordRevision(w,text);if(n>=170)expected.insert(text);}
QSet<QString> actual;for(auto name:QDir(dir).entryList({"*.json"},QDir::Files,QDir::Name))actual.insert(readSnapshot(QDir(dir).filePath(name)).value("text").toString());
check("rapid history writes preserve all of the newest thirty distinct revisions",actual==expected);
check("history retention stays bounded to thirty snapshots",QDir(dir).entryList({"*.json"},QDir::Files).size()<=30);
auto cursor=w->editor()->textCursor();cursor.movePosition(QTextCursor::End);cursor.insertText("Unsaved draft.\n");checkpoint(w);
const QString pending=storage()+"/"+id+".pending.json";check("checkpoint retains the exact dirty text and disk version",readSnapshot(pending).value("text").toString()==w->editor()->toPlainText()&&!readSnapshot(pending).value("diskHash").toString().isEmpty());
auto snap=readSnapshot(pending);QString before=w->editor()->toPlainText();restoreText(w,QJsonObject{{"text","Recovered text.\n"},{"cursor",3}});
check("restoration is a single native undo step",w->editor()->toPlainText()=="Recovered text.\n"&&w->isModified());w->editor()->undo();check("undo restoration recovers the complete prior draft",w->editor()->toPlainText()==before);
checkpoint(w,true);check("intentional close retains history without a stale crash prompt",!QFileInfo::exists(pending));w->setModified(false);delete w;settleRecovery(300);
app.exit(failures?1:0);
});return app.exec();}
