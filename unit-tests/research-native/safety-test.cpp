#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "TeXDocumentWindow.h"
#include "CompletingEdit.h"
#include "ResearchCollaboration.h"
#include "ResearchAppearance.h"
#include "Settings.h"
#include <QDir>
#include <QFileSystemWatcher>
#include <QSaveFile>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMessageBox>
#include <QThread>
#include <QTimer>
#include <windows.h>
#include <functional>
static bool waitFor(const std::function<bool()> &ready,int ms=8000){QElapsedTimer t;t.start();while(!ready()&&t.elapsed()<ms){QCoreApplication::processEvents();QThread::msleep(5);}return ready();}
static void settle(int ms=250){QElapsedTimer t;t.start();waitFor([&]{return t.elapsed()>=ms;},ms+1000);}
// Ordinary Windows readers may briefly block atomic replacement. Never test
// receipt of an external version before verifying that it reached the disk.
static QJsonArray writes;
static bool put(const QString &path,const QString &s){
 QDir().mkpath(QFileInfo(path).absolutePath());const auto bytes=s.toUtf8();
 QElapsedTimer elapsed;elapsed.start();QJsonArray errors;int attempts=0;bool committed=false;
 do {QSaveFile file(path);++attempts;committed=file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size()&&file.commit();
  if(committed)break;
  errors.append(file.errorString());QThread::msleep(5);
 }while(elapsed.elapsed()<500);
 QFile disk(path);const bool exact=committed&&disk.open(QIODevice::ReadOnly)&&disk.readAll()==bytes;
 if(QFileInfo(path).fileName()!=QStringLiteral("report.json"))
  writes.append(QJsonObject{{"path",path},{"attempts",attempts},{"milliseconds",elapsed.elapsed()},{"errors",errors},{"committed",committed},{"exactDiskBytes",exact}});
 return exact;
}
int main(int argc,char **argv){QString report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
QTimer::singleShot(100,&app,[&]{QJsonArray checks;int failures=0;QString folder=QFileInfo(report).absolutePath()+"/safety-fixtures";Tw::Settings().setValue("openPDFwithTeX",false);
auto check=[&](QString name,bool good){checks.append(QJsonObject{{"name",name},{"passed",good}});failures+=!good;put(report,QString::fromUtf8(QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"externalWrites",writes}}).toJson()));};
QString path=folder+"/commit failure.tex";QString original="\\documentclass{article}\n\\begin{document}\nOriginal.\n\\end{document}\n";put(path,original);
auto *w=new TeXDocumentWindow(path);w->show();settle(1100);auto *watcher=w->findChild<QFileSystemWatcher*>();
check("normal loaded file is watched",watcher&&watcher->files().contains(QFileInfo(path).canonicalFilePath()));
auto cursor=w->editor()->textCursor();cursor.movePosition(QTextCursor::End);cursor.insertText("\nLocal draft\n");const auto draft=w->editor()->toPlainText();
HANDLE lock=CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
check("fixture holds an ordinary reader without delete sharing",lock!=INVALID_HANDLE_VALUE);
QTimer dismiss;dismiss.setInterval(25);QObject::connect(&dismiss,&QTimer::timeout,&app,[&]{for(auto *top:QApplication::topLevelWidgets())if(auto *box=qobject_cast<QMessageBox*>(top);box&&box->isVisible()){box->grab().save(QFileInfo(report).absolutePath()+"/save-failure-dialog.png");box->accept();}});dismiss.start();
bool saved=w->save();dismiss.stop();if(lock!=INVALID_HANDLE_VALUE)CloseHandle(lock);
check("failed atomic replacement does not report successful save",!saved&&w->isModified());
QFile disk(path);disk.open(QIODevice::ReadOnly);check("failed save preserves the complete disk version",QString::fromUtf8(disk.readAll())==original);disk.close();
check("failed save preserves the complete unsaved draft",w->editor()->toPlainText()==draft);
check("file observation is rearmed after atomic commit failure",watcher&&watcher->files().contains(QFileInfo(path).canonicalFilePath()));
check("retry succeeds after the reader releases its handle",w->save()&&!w->isModified());settle();
QString external=w->editor()->toPlainText();external.replace("Original.","External after failed save.");
check("the independent external update is committed with exact disk bytes",put(path,external));
check("external updates still arrive after failed save and retry",waitFor([&]{return w->editor()->toPlainText()==external;}));
w->setModified(false);delete w;settle();app.exit(failures?1:0);
});return app.exec();}
