#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "Engine.h"
#include "TeXDocumentWindow.h"
#include "CompletingEdit.h"
#include "ResearchBuild.h"
#include "Settings.h"
#include "PDFBackend.h"
#include <QDir>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QSaveFile>
#include <QStatusBar>
#include <QThread>
#include <QTimer>
#include <functional>

static bool waitFor(const std::function<bool()> &ready,int ms=30000){QElapsedTimer t;t.start();while(!ready()&&t.elapsed()<ms){QCoreApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QThread::msleep(2);}return ready();}
static bool put(const QString &path,const QByteArray &bytes){QDir().mkpath(QFileInfo(path).absolutePath());QSaveFile f(path);return f.open(QIODevice::WriteOnly)&&f.write(bytes)==bytes.size()&&f.commit();}
int main(int argc,char **argv){const QString report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
 QTimer::singleShot(80,&app,[&]{QJsonArray checks,rounds;int failures=0;
 auto save=[&]{QSaveFile file(report);if(file.open(QIODevice::WriteOnly)){file.write(QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"rounds",rounds}}).toJson());file.commit();}};
 auto check=[&](QString name,bool pass){checks.append(QJsonObject{{"name",name},{"passed",pass}});failures+=!pass;save();};
 const QString folder=QFileInfo(report).absolutePath(),cache=folder+"/cache";Research::setBuildCacheForTests(cache);Tw::Settings().setValue("openPDFwithTeX",false);
 const Engine engine("Direct no-edit test","C:/texlive/2023/bin/windows/pdflatex.exe",{"-synctex=1","-interaction=nonstopmode","-file-line-error","$fullname"},true);app.setEngineList({engine});
 for(int round=0;round<5;++round){const QString dir=folder+QString("/source-%1").arg(round),root=dir+"/main.tex",child=dir+"/chapter.tex",gate=folder+QString("/ready-%1.gate").arg(round);
  const QString document="\\documentclass{article}\n\\newcount\\ready\\newcount\\stress\\newread\\readgate\\newwrite\\checkpoint\n\\begin{document}\n\\immediate\\openout\\checkpoint=no-edit-ready.txt\\immediate\\write\\checkpoint{ready}\\immediate\\closeout\\checkpoint\n\\ready=0\\stress=0\\loop\\openin\\readgate=\\detokenize{"+gate+"}\\relax\\ifeof\\readgate\\else\\ready=1\\fi\\closein\\readgate\\advance\\stress by1\\ifnum\\stress>5000000\\ready=1\\fi\\ifnum\\ready=0\\repeat\n\\input{chapter}\n\\end{document}\n";
  check("independent sources are written",put(root,document.toUtf8())&&put(child,"Unchanged body.\n"));
  const auto initial=Research::prepareBuild(root,engine);auto *window=new TeXDocumentWindow(root);window->show();QStringList notices;int starts=0;
  QPointer<TeXDocumentWindow> guard(window);
  QTimer processes;processes.setInterval(1);
  QObject::connect(&processes,&QTimer::timeout,window,[&]{
    for(auto *process:window->findChildren<QProcess *>())if(process->state()!=QProcess::NotRunning&&!process->property("auditCounted").toBool()){
      process->setProperty("auditCounted",true);++starts;
      if(round==3&&starts==2){QFile file(child);if(file.open(QIODevice::ReadWrite))file.setFileTime(QFileInfo(child).lastModified().addSecs(5),QFileDevice::FileModificationTime);}
    }
  });processes.start();
  QObject::connect(window->statusBar(),&QStatusBar::messageChanged,window,[&](const QString &text){
    notices.append(text);
    if(round==4&&text=="正在重新核对编译；当前编辑内容已保留。")QTimer::singleShot(0,window,[window]{window->close();});
  });
  window->typeset();check("actual editor starts a real TeX process",waitFor([&]{return QFileInfo::exists(initial.outputDir+"/no-edit-ready.txt")&&window->isTypesetting();}));
  if(round==1||round>=3){QFile file(child);const auto before=QFileInfo(child).lastModified();const bool touched=file.open(QIODevice::ReadWrite)&&file.setFileTime(before.addSecs(5),QFileDevice::FileModificationTime);file.close();check("timestamp-only fixture changes metadata while retaining bytes",touched&&QFileInfo(child).lastModified()!=before);}
  if(round==2)check("real edit fixture changes source content",put(child,"Changed body.\n"));
  check("controlled compile gate opens",put(gate,"go\n"));
  if(round==4){check("closing before automatic recheck cancels the queued build",waitFor([&]{return !guard;})&&starts==1);continue;}
  const bool stopped=waitFor([&]{return !window->isTypesetting()&&(round!=3||notices.contains("编译期间输入版本无法确认；保留上一版 PDF，请重新编译。"));});QElapsedTimer settle;settle.start();waitFor([&]{return settle.elapsed()>=300;},1000);
  const QString preview=Research::previewFile(root);const bool current=!preview.isEmpty()&&Research::pdfPairValid(preview);
  if(round<2){check(round==0?"unedited source compiles once without a source-change warning":"timestamp-only change is rechecked once and produces a valid preview",stopped&&waitFor([&]{return !window->isTypesetting()&&!Research::previewFile(root).isEmpty()&&Research::pdfPairValid(Research::previewFile(root));})&&starts==(round==0?1:2)&&!notices.contains("编译期间源码已更新；保留上一版 PDF，请重新编译。"));}
  else if(round==2)check("actual changed content remains rejected without an automatic retry",stopped&&!current&&starts==1&&notices.contains("编译期间源码已更新；保留上一版 PDF，请重新编译。"));
  else check("repeated metadata changes stop after one recheck without claiming a source edit",stopped&&!current&&starts==2&&notices.contains("编译期间输入版本无法确认；保留上一版 PDF，请重新编译。")&&!notices.contains("编译期间源码已更新；保留上一版 PDF，请重新编译。"));
  QFile original(child);check("automatic handling never rewrites source bytes",original.open(QIODevice::ReadOnly)&&original.readAll()==(round==2?"Changed body.\n":"Unchanged body.\n"));original.close();
  const QString finalPreview=Research::previewFile(root);
  rounds.append(QJsonObject{{"round",round},{"buildStarts",starts},{"notices",QJsonArray::fromStringList(notices)},{"console",window->property("consoleOutput").toString()},{"current",!finalPreview.isEmpty()&&Research::pdfPairValid(finalPreview)}});
  if(window->isTypesetting()){window->interrupt();check("fixture waits for interrupted process cleanup",waitFor([&]{return !window->isTypesetting();}));}
  window->setModified(false);delete window;
 }
 // Certification checks bytes even for binary dependencies whose frequent
 // UI revision checks use the cheaper size/timestamp path.
 const QString largeRoot=folder+"/large-input/main.tex",largeAsset=folder+"/large-input/figure.jpg";
 check("independent large binary dependency is written",put(largeRoot,"\\documentclass{article}\n\\usepackage{graphicx}\n\\newcommand{\\unused}{\\includegraphics{figure.jpg}}\n\\begin{document}Body.\\end{document}\n")&&put(largeAsset,QByteArray(5*1024*1024,'x')));
 const auto largeJob=Research::prepareBuild(largeRoot,engine);
 check("pre-build snapshot records the large dependency",largeJob.inputs.contains(QFileInfo(largeAsset).canonicalFilePath()));
 QFile largeFile(largeAsset);bool touched=largeFile.open(QIODevice::ReadWrite)&&largeFile.setFileTime(QFileInfo(largeAsset).lastModified().addSecs(5),QFileDevice::FileModificationTime);largeFile.close();
 check("large dependency metadata alone does not imply changed content",touched&&Research::buildContentUnchanged(largeJob));
 check("large dependency bytes can still invalidate rechecking",put(largeAsset,QByteArray(5*1024*1024,'y'))&&!Research::buildContentUnchanged(largeJob));
 Research::setBuildCacheForTests(QString());QSaveFile file(report);if(file.open(QIODevice::WriteOnly)){file.write(QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"rounds",rounds}}).toJson());file.commit();}app.exit(failures?1:0);
 });return app.exec();}

