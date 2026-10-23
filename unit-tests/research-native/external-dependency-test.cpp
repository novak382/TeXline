#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "Engine.h"
#include "TWSynchronizer.h"
#include "ResearchProject.h"
#include <ResearchCollaboration.cpp>
#include "Settings.h"
#include "CompletingEdit.h"
#include <QSaveFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QElapsedTimer>
#include <QThread>
#include <functional>
#include <cstdlib>
static bool waitFor(const std::function<bool()> &ready,int ms=10000){QElapsedTimer t;t.start();while(t.elapsed()<ms){if(ready())return true;QCoreApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QThread::msleep(3);}return ready();}
static bool put(const QString &p,const QByteArray &b){QDir().mkpath(QFileInfo(p).absolutePath());QSaveFile f(p);return f.open(QIODevice::WriteOnly)&&f.write(b)==b.size()&&f.commit();}
int main(int argc,char **argv){const QString report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);QTimer::singleShot(80,&app,[&]{using namespace Research;
QJsonArray checks;int failures=0;auto check=[&](QString n,bool p){checks.append(QJsonObject{{"name",n},{"passed",p}});failures+=!p;put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures}}).toJson());};
const QString base=QFileInfo(report).absolutePath()+"/outside-dependencies",folder=base+"/paper 中文",library=base+"/"+qEnvironmentVariable("RESEARCH_TEST_LIBRARY_NAME",QString::fromUtf8("共享库 中文 😀")),root=folder+"/main.tex",bib=library+"/library.bib",chapter=library+"/chapter.tex";
const QByteArray source="\\documentclass{article}\n\\begin{document}\nPaper \\cite{external}.\\input{chapter}\n\\bibliographystyle{plain}\\bibliography{library}\n\\end{document}\n";
check("isolated author inputs are created",put(root,source)&&put(bib,"@book{external,author={Example, Alice},title={Original external book},year={2024},publisher={Test}}\n")&&put(chapter,"\\par External chapter.\\label{outside:chapter}\\par\n"));

#ifdef Q_OS_WIN
const std::wstring nativeSearch=(library+";").toStdWString();_wputenv_s(L"BIBINPUTS",nativeSearch.c_str());_wputenv_s(L"TEXINPUTS",nativeSearch.c_str());
#else
qputenv("BIBINPUTS",library.toLocal8Bit()+";");qputenv("TEXINPUTS",library.toLocal8Bit()+";");
#endif
check("native search environment preserves the original Unicode directory",qEnvironmentVariable("TEXINPUTS")==library+";"&&qEnvironmentVariable("BIBINPUTS")==library+";");setBuildCacheForTests(base+"/cache");Tw::Settings().setValue("openPDFwithTeX",false);
put(base+"/environment.json",QJsonDocument(QJsonObject{{"root",root},{"library",library},{"bib",bib},{"BIBINPUTS",qEnvironmentVariable("BIBINPUTS")},{"libraryExists",QFileInfo::exists(library)},{"bibExists",QFileInfo::exists(bib)},{"separator",QString(QDir::listSeparator())}}).toJson());
const auto dependencies=buildDependencies(root);check("external BIBINPUTS bibliography is tracked before the first build",dependencies.contains(canonical(bib)));check("external TEXINPUTS chapter is tracked before the first build",dependencies.contains(canonical(chapter)));
auto index=buildProjectIndex(root);check("navigation and citations use the same outside dependencies",index->files.contains(canonical(chapter))&&index->bibliographies.contains(canonical(bib))&&std::any_of(index->entries.begin(),index->entries.end(),[](const auto &e){return e.key=="external";}));
auto *w=new TeXDocumentWindow(root);w->resize(980,740);w->show();auto *project=updateProjects.value(canonical(root));
check("event-driven project watcher establishes an outside-file baseline",project&&waitFor([&]{return !project->busy&&!project->hasCandidate&&!project->timer.isActive();})&&project->known.contains(canonical(bib))&&project->known.contains(canonical(chapter)));
check("outside-file parents are watched for atomic replacement",project&&project->watcher.directories().contains(canonical(library)));
check("both outside atomic replacements commit despite concurrent watcher reads",waitFor([&]{return put(bib,"@book{external,author={Example, Alice},title={Updated external book},year={2025},publisher={Test}}\n");})&&waitFor([&]{return put(chapter,"\\par Changed external chapter.\\label{outside:chapter}\\par\n");}));
check("atomic replacements of two outside files appear in one project review",project&&waitFor([&]{return project->records.contains(canonical(bib))&&project->records.contains(canonical(chapter));}));
if(project)project->acknowledge();
Engine engine("pdfLaTeX",qEnvironmentVariable("RESEARCH_TEST_LATEXMK","latexmk.exe"),{"-pdf","-synctex=1","-interaction=nonstopmode","-file-line-error","$fullname"},true);
auto job=prepareBuild(root,engine);QElapsedTimer elapsed;elapsed.start();QProcess *process=engine.run(QFileInfo(root),&app,job.outputDir);QByteArray console;
const bool started=process&&(process->state()==QProcess::Running||process->waitForStarted(3000));const bool done=started&&waitFor([&]{console+=process->readAllStandardOutput();return process->state()==QProcess::NotRunning;},90000);if(process){console+=process->readAllStandardOutput();put(base+"/process.json",QJsonDocument(QJsonObject{{"program",process->program()},{"arguments",QJsonArray::fromStringList(process->arguments())},{"error",process->errorString()},{"started",started},{"TEXINPUTS",process->processEnvironment().value("TEXINPUTS")},{"BIBINPUTS",process->processEnvironment().value("BIBINPUTS")}}).toJson());}put(base+"/console.txt",console);
check("real latexmk builds the shared bibliography and external chapter",done&&process->exitCode()==0);const int result=finishBuild(job,done&&process->exitCode()==0);put(base+"/promotion.json",QJsonDocument(QJsonObject{{"result",result},{"inputs",job.inputs},{"pdf",job.previewFile},{"revision",int(pdfRevision(job.previewFile,root))},{"modified",w->isModified()}}).toJson());check("the first external-dependency PDF is safely promoted",result==1&&pdfPairValid(job.previewFile));if(process)delete process;
auto known=prepareBuild(root,engine);check("both outside dependencies are in the next build snapshot",known.inputs.contains(canonical(bib))&&known.inputs.contains(canonical(chapter)));
TWSyncTeXSynchronizer synchronizer(job.previewFile,{}, {},folder);
const auto location=synchronizer.syncFromTeX({canonical(chapter),1,0,0},TWSynchronizer::LineResolution);
check("external source SyncTeX works through a cache directory alias",synchronizer.isValid()&&location.page>0&&!location.rects.isEmpty());
auto clicked=location;if(!clicked.rects.isEmpty())clicked.rects={QRectF(clicked.rects.first().center(),QSizeF())};
const auto reverse=synchronizer.syncFromPDF(clicked,TWSynchronizer::LineResolution);
put(base+"/sync.json",QJsonDocument(QJsonObject{{"forwardPage",location.page},{"reverseFile",reverse.filename},{"reverseLine",reverse.line},{"expected",canonical(chapter)}}).toJson());
check("external PDF SyncTeX resolves back to the original directory",reverse.line==1&&canonical(QDir(folder).filePath(reverse.filename))==canonical(chapter));
QString publishError;check("a current PDF with outside dependencies can be published to its project",publishPreview(root,&publishError));
const QString shared=folder+"/main.pdf";const auto exported=pdfMetadata(shared);
const auto exportedInputs=exported.value("inputs").toObject().keys();
check("project publication binds verified outside dependencies to its original root",exported.value("externalInputs").toBool()&&exported.value("boundRoot").toString()==canonical(root)&&exported.value("externalDependencies").toObject().contains(canonical(bib))&&exported.value("externalDependencies").toObject().contains(canonical(chapter))&&pdfRevision(shared,root)==PdfRevision::Current&&std::all_of(exportedInputs.begin(),exportedInputs.end(),[](const QString &s){return !QDir::isAbsolutePath(s)&&!s.startsWith("../");}));
const QString relocated=base+"/relocated/main.pdf";QDir().mkpath(QFileInfo(relocated).absolutePath());
check("relocated publication never claims verification against another project's outside files",QFile::copy(shared,relocated)&&QFile::copy(pdfMetadataFile(shared),pdfMetadataFile(relocated))&&QFile::copy(root,base+"/relocated/main.tex")&&pdfRevision(relocated,base+"/relocated/main.tex")==PdfRevision::Unknown);
const QByteArray original=QByteArray("@book{external,author={Example, Alice},title={Updated external book},year={2025},publisher={Test}}\n");QThread::msleep(25);const bool transientCommitted=waitFor([&]{return put(bib,"@book{external,title={Transient revision}}\n");});QThread::msleep(25);check("outside bibliography change and restoration both commit",transientCommitted&&waitFor([&]{return put(bib,original);}));
check("a changed-and-restored outside bibliography rejects an in-flight build",finishBuild(known,true)==-1);
check("latest outside bibliography replacement commits",waitFor([&]{return put(bib,"@book{external,title={Latest library revision}}\n");}));check("an outside bibliography edit marks the existing PDF stale",pdfRevision(job.previewFile,root)==PdfRevision::Stale);
check("outside dependency changes never rewrite the manuscript",[&]{QFile f(root);return f.open(QIODevice::ReadOnly)&&f.readAll()==source;}());
const QByteArray macroSource="\\documentclass{article}\n\\newcommand{\\chaptername}{chapter}\n\\begin{document}\\input{\\chaptername}\\end{document}\n";
check("macro-only manuscript replacement commits",waitFor([&]{return put(root,macroSource);}));auto macroJob=prepareBuild(root,engine);QProcess *macroProcess=engine.run(QFileInfo(root),&app,macroJob.outputDir);
const bool macroStarted=macroProcess&&(macroProcess->state()==QProcess::Running||macroProcess->waitForStarted(3000));
const bool macroDone=macroStarted&&waitFor([&]{macroProcess->readAllStandardOutput();return macroProcess->state()==QProcess::NotRunning;},90000);
check("compiler records safely learn a macro-based external include",macroDone&&macroProcess->exitCode()==0&&finishBuild(macroJob,true)==1&&recordedDependencies(root).contains(canonical(chapter)));
if(macroProcess)delete macroProcess;
auto pruned=prepareBuild(root,engine);check("removing a bibliography discards old dependencies without losing macro inputs",!pruned.inputs.contains(canonical(bib))&&pruned.inputs.contains(canonical(chapter)));
index=buildProjectIndex(root);check("navigation uses learned macro inputs and removes the unused bibliography",index->files.contains(canonical(chapter))&&index->bibliographies.isEmpty());
if(project){w->collaboration()->buildFinished();waitFor([&]{return !project->busy&&!project->hasCandidate&&!project->timer.isActive();});project->acknowledge();}
check("watchers release the unused external bibliography",project&&waitFor([&]{return !project->busy&&!project->hasCandidate&&!project->timer.isActive()&&!project->watcher.files().contains(canonical(bib))&&project->watcher.files().contains(canonical(chapter));}));
w->grab().save(QFileInfo(report).absolutePath()+"/outside-review.png");w->setModified(false);delete w;check("closing the project releases all watchers",waitFor([&]{return updateProjects.isEmpty();}));app.exit(failures?1:0);
});return app.exec();}

