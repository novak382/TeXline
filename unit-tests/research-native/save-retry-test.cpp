#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "TeXDocumentWindow.h"
#include "CompletingEdit.h"
#include "Settings.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QThread>
#include <QTimer>
#include <windows.h>
#include <atomic>

static bool put(const QString &path,const QByteArray &bytes) {
 QDir().mkpath(QFileInfo(path).absolutePath());QSaveFile file(path);
 return file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size()&&file.commit();
}
static QByteArray disk(const QString &path) {QFile f(path);if(!f.open(QIODevice::ReadOnly))return {};return f.readAll();}
static void settle(int milliseconds) {QElapsedTimer t;t.start();while(t.elapsed()<milliseconds){QCoreApplication::processEvents();QThread::msleep(5);}}
int main(int argc,char **argv) {
 const QString report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
 QTimer::singleShot(80,&app,[&]{
  QJsonArray checks,runs,messages;int failures=0;
  auto check=[&](QString name,bool passed){checks.append(QJsonObject{{"name",name},{"passed",passed}});failures+=!passed;};
  Tw::Settings().setValue("openPDFwithTeX",false);
  const QByteArray original=QString("\\documentclass{article}\n\\begin{document}\nOriginal. 中文 α\n\\end{document}\n").toUtf8();
  for(int scenario=0;scenario<3;++scenario) {
   const QString path=QFileInfo(report).absolutePath()+QString("/保存 中文𐐀/%1.tex").arg(scenario);
   check("independent source is atomically committed",put(path,original));
   auto *w=new TeXDocumentWindow(path);w->show();settle(1100);
   QTextCursor cursor(w->editor()->document());cursor.movePosition(QTextCursor::End);cursor.insertText("Local draft.\n");const QString draft=w->editor()->toPlainText();
   const QString native=QString("\\\\?\\")+QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
   HANDLE handle=CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
   check("an ordinary Windows reader holds the target",handle!=INVALID_HANDLE_VALUE);
   QByteArray external=original;external.replace("Original.","External.");std::atomic<bool> externalWritten{scenario!=2};
   QThread *release=QThread::create([&,handle,scenario]{
    QThread::msleep(scenario==1?600:60);
    if(scenario==2) {QFile f(path);externalWritten=f.open(QIODevice::WriteOnly|QIODevice::Truncate)&&f.write(external)==external.size()&&f.flush();f.close();}
    if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);
   });release->start();
   const int previousMessages=messages.size();QTimer dismiss;dismiss.setInterval(20);
   QObject::connect(&dismiss,&QTimer::timeout,&app,[&]{for(auto *top:QApplication::topLevelWidgets())if(auto *box=qobject_cast<QMessageBox*>(top);box&&box->isVisible()){messages.append(QJsonObject{{"scenario",scenario},{"title",box->windowTitle()},{"text",box->text()}});box->accept();}});dismiss.start();
   QElapsedTimer elapsed;elapsed.start();const bool saved=w->save();const auto duration=elapsed.elapsed();dismiss.stop();release->wait();delete release;
   const QByteArray result=disk(path);
   if(scenario==0) {
    check("short reader contention is saved without an error dialog",saved&&!w->isModified()&&messages.size()==previousMessages);
    check("short contention saves the exact draft bytes",result==draft.toUtf8());
   } else if(scenario==1) {
    check("persistent contention stops with the complete unsaved draft",!saved&&w->isModified()&&w->editor()->toPlainText()==draft);
    check("persistent contention preserves the complete original on disk",result==original);
    check("persistent contention has a bounded save wait",duration<400);
    check("explicit retry succeeds after the reader is released",w->save()&&!w->isModified()&&disk(path)==draft.toUtf8());
   } else {
    check("the independent external writer finishes its actual disk modification",externalWritten);
    check("save retry does not overwrite the external version",!saved&&result==external);
    check("save retry preserves the local draft",w->editor()->toPlainText().contains("Local draft."));
    settle(500);check("external observation still preserves the local draft",w->editor()->toPlainText().contains("Local draft."));
    check("the rejected save still receives the disjoint external update",w->editor()->toPlainText().contains("External."));
   }
   runs.append(QJsonObject{{"scenario",scenario},{"saved",saved},{"milliseconds",duration},{"dialogs",messages.size()-previousMessages},{"externalWritten",externalWritten.load()}});
   w->setModified(false);delete w;settle(200);
  }
  put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"runs",runs},{"messages",messages}}).toJson());app.exit(failures?1:0);
 });return app.exec();
}
