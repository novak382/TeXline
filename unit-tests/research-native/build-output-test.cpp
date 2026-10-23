#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "TeXDocumentWindow.h"
#include "CompletingEdit.h"
#include "Engine.h"
#include "ResearchBuild.h"
#include "ResearchPublication.h"
#include "Settings.h"
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPlainTextEdit>
#include <QListWidget>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QSaveFile>
#include <QThread>
#include <QTimer>
#include <functional>
#include <windows.h>
// Model a cooperating external atomic writer. Never proceed with a source
// change that Windows refused to commit because of a concurrent read lease.
static QJsonArray fixtureWriteRetries;
static bool put(const QString &p,const QByteArray &d){
 QDir().mkpath(QFileInfo(p).absolutePath());QElapsedTimer elapsed;elapsed.start();
 for(int attempt=1;;++attempt){
  QSaveFile f(p);
  if(f.open(QIODevice::WriteOnly)&&f.write(d)==d.size()&&f.commit()){
   QFile disk(p);return disk.open(QIODevice::ReadOnly)&&disk.readAll()==d;
  }
  fixtureWriteRetries.append(QJsonObject{{"path",p},{"attempt",attempt},{"error",f.errorString()},{"milliseconds",elapsed.elapsed()}});
  if(f.error()!=QFileDevice::RenameError||elapsed.elapsed()>=500)return false;
  QThread::msleep(5);
 }
}
static QByteArray read(const QString &p){QFile f(p);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray();}
static QString hash(const QByteArray &b){return QString::fromLatin1(QCryptographicHash::hash(b,QCryptographicHash::Sha256).toHex());}
static bool waitFor(const std::function<bool()> &ready,int ms=90000){QElapsedTimer t;t.start();while(!ready()&&t.elapsed()<ms){QCoreApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QThread::msleep(2);}return ready();}
int main(int argc,char **argv){const QString report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);QTimer::singleShot(100,&app,[&]{
 const QString folder=QFileInfo(report).absolutePath(),root=folder+"/论文目录/main.tex",dir=QFileInfo(root).absolutePath(),child=folder+"/外部章节/child.tex",cache=folder+"/cache";
 Research::setBuildCacheForTests(cache);Tw::Settings().setValue("binaryPaths","C:/texlive/2023/bin/windows");Tw::Settings().setValue("openPDFwithTeX",false);Tw::Settings().setValue("researchCommandCompletionMode",0);
 const Engine engine("Output test","C:/texlive/2023/bin/windows/pdflatex.exe",{"-synctex=1","-interaction=nonstopmode","-file-line-error","$fullname"},false);app.setEngineList({engine});
 QJsonArray checks,samples;int failures=0;auto save=[&]{put(report,QJsonDocument(QJsonObject{{"checks",checks},{"samples",samples},{"fixtureWriteRetries",fixtureWriteRetries},{"failures",failures},{"physicalInputVerified",false},{"interruptionTest","durable partial-publication journal recovery, not forced process termination"}}).toJson());};auto check=[&](const QString &name,bool ok){checks.append(QJsonObject{{"name",name},{"passed",ok}});failures+=!ok;save();};
 QByteArray source="\\documentclass{article}\n\\usepackage{hyperref}\n\\begin{document}\n\\input{manual.aux}\n\\input{../外部章节/child}\n\\include{oldchapter}\n\\section{Result}First version.\\end{document}\n";
 const QByteArray manual="% handwritten dependency\n\\newcommand{\\privatevalue}{Safe}\n";
 check("compiler and independent Unicode/external fixtures available",engine.isAvailable()&&put(root,source)&&put(child,"External chapter.\n")&&put(dir+"/oldchapter.tex","Old chapter.\n")&&put(dir+"/manual.aux",manual)&&put(dir+"/handwritten.out","Keep me."));
 auto *w=new TeXDocumentWindow(root);w->resize(1024,760);w->show();
 auto state=[&]{return w->property("researchPublishStatus").toString();};
 auto compile=[&](const QString &name){QElapsedTimer t;t.start();w->typeset();const bool done=waitFor([&]{return !w->isTypesetting()&&!w->property("researchPublishing").toBool();});samples.append(QJsonObject{{"name",name},{"totalMs",t.nsecsElapsed()/1e6},{"status",state()},{"consoleTail",w->property("consoleOutput").toString().right(2200)}});save();return done;};
 const QStringList names={"main.pdf","main.synctex.gz","main.aux","main.out","main.log"};
 auto snapshot=[&]{QJsonObject o;for(const auto &name:names)o.insert(name,hash(read(dir+"/"+name)));o.insert("private-metadata",hash(read(Research::pdfMetadataFile(dir+"/main.pdf"))));return o;};
 check("first successful compile automatically publishes",compile("initial")&&state()=="updated");
 check("all actually generated PDF/SyncTeX/aux/out/log reach project",QFileInfo::exists(dir+"/main.pdf")&&QFileInfo::exists(dir+"/main.synctex.gz")&&QFileInfo::exists(dir+"/main.aux")&&QFileInfo::exists(dir+"/main.out")&&QFileInfo::exists(dir+"/main.log"));
 check("shared PDF and SyncTeX are certified including external chapter",Research::pdfPairValid(dir+"/main.pdf")&&Research::pdfRevision(dir+"/main.pdf")==Research::PdfRevision::Current);
 check("handwritten and authored generated-extension dependencies retained",read(dir+"/manual.aux")==manual&&read(dir+"/handwritten.out")=="Keep me.");
 check("visible folder and latest-log controls exist",w->findChild<QPushButton *>("researchOpenOutputDirectory")&&w->findChild<QPushButton *>("researchViewLatestLog")&&w->property("researchOutputDirectory").toString()==dir);
 const auto initial=snapshot();check("continuous unchanged compilation has no false version rejection",compile("repeat")&&state()=="updated"&&Research::pdfPairValid(dir+"/main.pdf"));
 source.replace("First version.","Changed final version.");w->editor()->setPlainText(QString::fromUtf8(source));check("private edited source is saved",w->save());
 check("changed successful compile updates matching pair and side products",compile("changed")&&state()=="updated"&&snapshot().value("main.pdf")!=initial.value("main.pdf")&&Research::pdfPairValid(dir+"/main.pdf"));
 const auto valid=snapshot();const QByteArray correct=source;
 source.replace("Changed final version.","\\UndefinedBrokenCommand");w->editor()->setPlainText(QString::fromUtf8(source));check("failure fixture saved privately",w->save());
 check("failed compilation preserves complete published generation",compile("failure")&&state()=="failed"&&snapshot()==valid);
 w->findChild<QPushButton *>("researchViewLatestLog")->click();
 check("failed cached log is directly viewable",waitFor([&]{return w->findChild<QPlainTextEdit *>("researchLatestLogText");})&&w->findChild<QPlainTextEdit *>("researchLatestLogText")->toPlainText().contains("UndefinedBrokenCommand"));
 if(auto *text=w->findChild<QPlainTextEdit *>("researchLatestLogText"))put(folder+"/failed-log-view.txt",text->toPlainText().toUtf8());
 if(auto *dialog=w->findChild<QWidget *>("researchLatestLogDialog"))dialog->close();
 source=correct;source.replace("Changed final version.","\\newcount\\slowcount\\loop\\advance\\slowcount1\\ifnum\\slowcount<5000000\\repeat Cancel test.");w->editor()->setPlainText(QString::fromUtf8(source));check("cancel fixture saved privately",w->save());
 w->typeset();check("cancel reaches an owned compiler process",waitFor([&]{return state()=="building";}));w->interrupt();
 check("cancelled compilation preserves complete published generation",waitFor([&]{return !w->isTypesetting()&&!w->property("researchPublishing").toBool();})&&state()=="cancelled"&&snapshot()==valid);
 w->editor()->setPlainText(QString::fromUtf8(correct));check("valid fixture restored privately",w->save());
 source=correct;source.replace("Changed final version.","\\newcount\\slowcount\\loop\\advance\\slowcount1\\ifnum\\slowcount<500000\\repeat Source race.");w->editor()->setPlainText(QString::fromUtf8(source));check("source-race fixture saved privately",w->save());
 w->typeset();check("external update occurs after preparation during compilation",waitFor([&]{return state()=="building";})&&put(child,"External during compile.\n"));
 check("source change during actual compilation refuses publication",waitFor([&]{return !w->isTypesetting()&&!w->property("researchPublishing").toBool();})&&state()=="not-published"&&snapshot()==valid);
 check("external source-race fixture is restored on disk",put(child,"External chapter.\n"));source=correct;w->editor()->setPlainText(QString::fromUtf8(source));check("source-race fixture restored privately",w->save());
 const std::wstring target=QDir::toNativeSeparators(dir+"/main.pdf").toStdWString();HANDLE locked=CreateFileW(target.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
 check("real Windows lease denies target replacement",locked!=INVALID_HANDLE_VALUE);
 source=correct;source.replace("Changed final version.","Locked next version.");w->editor()->setPlainText(QString::fromUtf8(source));check("locked fixture saved privately",w->save());
 check("writeback failure distinct from cache success and no mixed generation",compile("locked")&&state()=="write-failed"&&snapshot()==valid&&Research::pdfPairValid(Research::previewFile(root)));
 if(locked!=INVALID_HANDLE_VALUE)CloseHandle(locked);
 w->findChild<QPushButton *>("researchRetryPublication")->click();check("visible retry publishes already certified cache",waitFor([&]{return !w->property("researchPublishing").toBool();})&&state()=="updated"&&Research::pdfPairValid(dir+"/main.pdf"));
 const auto published=snapshot();QString error;
 const bool externalWritten=put(child,"External changed chapter.\n");const auto externalRevision=Research::pdfRevision(dir+"/main.pdf");
 samples.append(QJsonObject{{"name","external-change-evidence"},{"written",externalWritten},{"actualBytes",QString::fromUtf8(read(child))},{"revision",int(externalRevision)},{"publishedMetadata",QString::fromUtf8(read(Research::pdfMetadataFile(dir+"/main.pdf")))}});save();
 check("external edit invalidates shared freshness",externalWritten&&externalRevision==Research::PdfRevision::Stale);
 check("stale source refuses publish and preserves all outputs",!Research::publishGeneratedPreview(root,&error)&&snapshot()==published);
 check("external change recompiles and certifies new shared pair",compile("external-change")&&state()=="updated"&&Research::pdfPairValid(dir+"/main.pdf"));
 const auto beforeUnsaved=snapshot();w->editor()->insertPlainText("% unsaved change\n");
 check("manual publication refuses dirty source buffer",!Research::publishPreview(root,&error)&&snapshot()==beforeUnsaved);w->editor()->undo();w->setModified(false);
 const QString base=Research::buildCacheDirectory(root),transaction=base+"/publication/simulated-interruption";
 const QByteArray oldPdf=read(dir+"/main.pdf"),oldSync=read(dir+"/main.synctex.gz"),newPdf="Interrupted newer PDF";QJsonArray rows;
 for(const auto &entry:QList<QPair<QString,QByteArray>>{{"main.pdf",oldPdf},{"main.synctex.gz",oldSync}}){const QString key=entry.first=="main.pdf"?"0":"1";put(transaction+"/"+key+".old",entry.second);rows.append(QJsonObject{{"name",entry.first},{"oldHash",hash(entry.second)},{"newHash",hash(entry.first=="main.pdf"?newPdf:QByteArray("new sync"))},{"existed",true},{"backup",key+".old"}});}
 put(base+"/publish-pending.json",QJsonDocument(QJsonObject{{"root",QFileInfo(root).canonicalFilePath()},{"transaction",transaction},{"files",rows}}).toJson());put(dir+"/main.pdf",newPdf);
 check("interrupted partial generation recovers before refusing cancelled publish",!Research::publishGeneratedPreview(root,&error,[]{return true;})&&read(dir+"/main.pdf")==oldPdf&&read(dir+"/main.synctex.gz")==oldSync&&!QFileInfo::exists(base+"/publish-pending.json"));
 source.replace("\\include{oldchapter}\n","");w->editor()->setPlainText(QString::fromUtf8(source));check("obsolete include removed only in private fixture",w->save());
 check("compile after include removal succeeds",compile("remove-include")&&state()=="updated");check("second compile stops consuming obsolete aux",compile("remove-include-repeat")&&state()=="updated");
 const auto plan=Research::legacyArtifactPlan(root);bool found=false,safe=true;for(const auto &value:plan){const auto name=value.toObject().value("name").toString();found|=name=="oldchapter.aux";safe&=name!="manual.aux"&&name!="handwritten.out"&&name!="main.aux";}
 check("cleanup plan includes proven obsolete aux and protects current/dependency files",found&&safe);
 w->findChild<QPushButton *>("researchCleanupArtifacts")->click();
 check("visible cleanup shows review list before removing anything",waitFor([&]{return w->findChild<QWidget *>("researchArtifactCleanupDialog");})&&QFileInfo::exists(dir+"/oldchapter.aux")&&w->findChild<QListWidget *>()->count()==plan.size());
 if(auto *dialog=w->findChild<QWidget *>("researchArtifactCleanupDialog"))dialog->close();
 const auto oldAux=read(dir+"/oldchapter.aux");QString backup;
 check("cleanup makes verified durable backup before removal",Research::cleanLegacyArtifacts(root,plan,&backup,&error)&&!QFileInfo::exists(dir+"/oldchapter.aux")&&read(backup+"/0")==oldAux&&QFileInfo::exists(backup+"/manifest.json"));
 check("cleanup preserves handwritten files and existing source dependencies",read(dir+"/manual.aux")==manual&&read(dir+"/handwritten.out")=="Keep me."&&read(dir+"/oldchapter.tex")=="Old chapter.\n");
 // Empty/no-op and cancelled operations never remove an unconfirmed file.
 check("empty cleanup cannot delete arbitrary files",!Research::cleanLegacyArtifacts(root,{},&backup,&error));
 check("cached and shared preview hashes match after final successful run",read(Research::previewFile(root))==read(dir+"/main.pdf")&&Research::pdfPairValid(dir+"/main.pdf"));
 w->grab().save(folder+"/output-ui.png");w->setModified(false);delete w;Research::setBuildCacheForTests(QString());save();app.exit(failures?1:0);
 });return app.exec();}
