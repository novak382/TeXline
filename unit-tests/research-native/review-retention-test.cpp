#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include <ResearchCollaboration.cpp>
#include "Settings.h"
#include "CompletingEdit.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QElapsedTimer>
#include <QThread>

// Windows readers can briefly prevent replacement of an existing file. Model
// a cooperating external writer without delivering a version that was never
// committed. Retain every failed attempt rather than hiding the contention.
static QJsonArray writes;
static bool put(const QString &path,const QString &text){
 QDir().mkpath(QFileInfo(path).absolutePath());const auto bytes=text.toUtf8();
 QElapsedTimer elapsed;elapsed.start();QJsonArray errors;int attempts=0;bool committed=false;
 do {
  QSaveFile file(path);++attempts;
  committed=file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size()&&file.commit();
  if(committed)break;
  errors.append(file.errorString());QThread::msleep(5);
 }while(elapsed.elapsed()<500);
 QFile disk(path);const bool exact=committed&&disk.open(QIODevice::ReadOnly)&&disk.readAll()==bytes;
 writes.append(QJsonObject{{"attempts",attempts},{"milliseconds",elapsed.elapsed()},{"errors",errors},{"committed",committed},{"exactDiskBytes",exact}});
 return exact;
}
int main(int argc,char **argv){const QString report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
 QTimer::singleShot(80,&app,[&]{using namespace Research;QJsonArray checks;int failures=0;
 auto check=[&](QString name,bool pass){checks.append(QJsonObject{{"name",name},{"passed",pass}});failures+=!pass;};
 const QString path=QFileInfo(report).absolutePath()+"/retention/main.tex";
 const QString initial="\\documentclass{article}\n\\begin{document}\nAlpha original sentence.\nStable separator.\nBeta original sentence.\n\\end{document}\n";
 check("independent initial source is committed",put(path,initial));Tw::Settings().setValue("openPDFwithTeX",false);auto *w=new TeXDocumentWindow(path);w->show();
 auto *receiver=w->collaboration();auto *project=updateProjects.value(canonical(path));
 QString remote=initial;remote.replace("Alpha original","Alpha revised");check("first external version is committed",put(path,remote));receiver->receive(remote);check("first received change is reviewable",receiver->reviewCount()==1&&w->editor()->toPlainText()==remote);
 remote.replace("Beta original","Beta revised");check("second external version is committed",put(path,remote));receiver->receive(remote);
 auto retained=[&]{const auto entries=project->entries();return entries.size()==2&&entries[0].before=="original"&&entries[1].before=="original";};
 check("a disjoint second update preserves both unreviewed original alternatives",receiver->reviewCount()==2&&retained());
 remote.replace("Alpha revised","Alpha finalized");check("overlapping external version is committed",put(path,remote));receiver->receive(remote);
 check("updating the same range retains the first original alternative",receiver->reviewCount()==2&&retained());
 QTextCursor edit(w->editor()->document());edit.setPosition(int(w->editor()->toPlainText().indexOf("Stable separator.")));edit.insertText("Local draft outside changes.\n");
 remote.replace("Alpha finalized","Alpha newest");check("latest remote version is committed",put(path,remote));receiver->receive(remote);
 check("a local draft outside received ranges survives further updates",w->editor()->toPlainText().contains("Local draft outside changes.")&&w->isModified()&&receiver->conflictCount()==0);
 check("local draft text is not mislabeled as a received review change",receiver->reviewCount()==2&&retained()&&[&]{for(const auto &entry:project->entries())if(entry.before.contains("Local draft")||entry.after.contains("Local draft"))return false;return true;}());
 receiver->review();project->acknowledge();check("finishing review clears the accumulated changes",receiver->reviewCount()==0&&project->entries().isEmpty());
 check("remote reversal is committed",put(path,initial));receiver->receive(initial);
 check("a reversal after acknowledgement starts a fresh review",receiver->reviewCount()==2&&w->editor()->toPlainText().contains("Local draft outside changes."));
 check("remote roundtrip is committed",put(path,remote));receiver->receive(remote);
 check("reverting all unreviewed changes retires them without removing the local draft",receiver->reviewCount()==0&&project->entries().isEmpty()&&w->editor()->toPlainText().contains("Local draft outside changes.")&&w->isModified());
 w->setModified(false);delete w;QSaveFile file(report);if(file.open(QIODevice::WriteOnly)){file.write(QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"externalWrites",writes}}).toJson());file.commit();}app.exit(failures?1:0);
 });return app.exec();}
