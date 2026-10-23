#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "TeXDocumentWindow.h"
#include "PDFDocumentWindow.h"
#include "ResearchBuild.h"
#include "Settings.h"
#include "Engine.h"
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSaveFile>
#include <QDir>
#include <QDateTime>
#include <QFileInfo>
#include <QProcess>
#include <QTimer>
#include <QThread>
#include <QPointer>
#include <functional>
static bool waitFor(const std::function<bool()> &ready,int limit=120000){QElapsedTimer t;t.start();while(!ready()&&t.elapsed()<limit){QCoreApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QThread::msleep(5);}return ready();}
static QByteArray read(const QString &p){QFile f(p);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray();}
static bool put(const QString &p,const QByteArray &v){QDir().mkpath(QFileInfo(p).absolutePath());QSaveFile f(p);return f.open(QIODevice::WriteOnly)&&f.write(v)==v.size()&&f.commit();}
static QJsonObject snapshot(const QString &folder){QJsonObject out;for(const auto &name:QDir(folder).entryList(QDir::Files)){if(name.endsWith(".tex")||name.endsWith(".bib"))continue;const QString p=QDir(folder).filePath(name);out.insert(name,QJsonObject{{"hash",QString::fromLatin1(QCryptographicHash::hash(read(p),QCryptographicHash::Sha256).toHex())},{"mtime",double(QFileInfo(p).lastModified().toMSecsSinceEpoch())}});}return out;}
static QString pdfText(PDFDocumentWindow *v){QString out;if(v)if(auto d=v->widget()->document().toStrongRef())for(int i=0;i<d->numPages();i++)if(auto p=d->page(i).toStrongRef())out+=p->selectedText({QPolygonF(QRectF(QPointF(),p->pageSizeF()))})+"\n";return out.simplified();}
int main(int argc,char **argv){const QString report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);QTimer::singleShot(100,&app,[&]{
 QJsonArray checks;QJsonObject evidence;int failures=0;const QString base=QFileInfo(report).absolutePath()+QStringLiteral("/existing 中文 artifacts");
 auto save=[&]{put(report,QJsonDocument(QJsonObject{{"checks",checks},{"evidence",evidence},{"failures",failures}}).toJson());};auto check=[&](const QString &name,bool pass){checks.append(QJsonObject{{"name",name},{"passed",pass}});failures+=!pass;save();};
 Research::setBuildCacheForTests(base+"/cache");Tw::Settings().setValue("openPDFwithTeX",false);
 const QString folder=base+"/paper",root=folder+"/main.tex",bib=folder+"/refs.bib";
 const QByteArray source="\\documentclass{article}\n\\usepackage{hyperref}\n\\begin{document}\nAn existing project with citation \\cite{demo}.\n\\bibliographystyle{plain}\n\\bibliography{refs}\n\\end{document}\n";
 auto reference=[](bool updated){return QByteArray("@book{demo,author={Example, Alice},title={")+(updated?"Updated reference title":"First reference title")+"},year={2024},publisher={Test Press}}\n";};
 check("isolated existing project is created",put(root,source)&&put(bib,reference(false)));
 Engine engine=TWApp::instance()->getNamedEngine("pdfLaTeX");QProcess *pre=engine.run(QFileInfo(root),&app);QByteArray console;
 const bool compiled=pre&&waitFor([&]{console+=pre->readAllStandardOutput();return pre->state()==QProcess::NotRunning;});if(pre)console+=pre->readAllStandardOutput();put(base+"/precompiled-console.txt",console);
 check("ordinary source-directory compilation creates the pre-existing PDF, aux and bibliography",compiled&&pre->exitCode()==0&&QFileInfo::exists(folder+"/main.pdf")&&QFileInfo::exists(folder+"/main.bbl"));if(pre)delete pre;
 const auto before=snapshot(folder);evidence.insert("originalGeneratedFiles",before);
 auto *w=new TeXDocumentWindow(root);w->show();PDFDocumentWindow *v=nullptr;
 for(int round=0;round<3;round++){
  if(round==1)check("a real bibliography update is written to the test copy",put(bib,reference(true)));
  w->typeset();const bool done=waitFor([&]{return !w->isTypesetting();});if(!done){w->interrupt();waitFor([&]{return !w->isTypesetting();},10000);}
  const QString pdf=Research::previewFile(root);v=w->pdfDocument();check(QString("existing artifacts: round %1 publishes a valid local PDF/SyncTeX pair").arg(round),done&&Research::managedPreview(pdf)&&Research::pdfPairValid(pdf)&&v);
  const auto after=snapshot(folder);evidence.insert(QString("generatedFilesAfter%1").arg(round),after);check(QString("existing artifacts: round %1 does not rewrite source-side generated files").arg(round),after==before);
  const QString text=pdfText(v);evidence.insert(QString("renderedText%1").arg(round),text);check(QString("existing artifacts: round %1 shows the correct bibliography").arg(round),text.contains(round==0?"First reference title":"Updated reference title"));
  if(v){evidence.insert(QString("zoomAfter%1").arg(round),v->widget()->zoomLevel());evidence.insert(QString("pdfFileAfter%1").arg(round),v->fileName());if(round==0){v->resize(1100,800);v->show();QElapsedTimer layout;layout.start();waitFor([&]{return layout.elapsed()>=350;},1000);v->widget()->setZoomLevel(1.15);}else check(QString("existing artifacts: round %1 retains reading zoom").arg(round),qAbs(v->widget()->zoomLevel()-1.15)<.001);}
 }
 check("real manuscript content is not modified by compilation",read(root)==source);if(v)v->close();QPointer<TeXDocumentWindow> guard(w);w->setModified(false);w->close();waitFor([&]{return !guard;});check("existing project closes without retained source windows",!guard);
 const QString manualFolder=base+"/manual",manual=manualFolder+"/manual.tex",bbl=manualFolder+"/manual.bbl";
 const QByteArray manualSource="\\documentclass{article}\n% Disabled database: \\bibliography{not-a-real-input}\n\\begin{document}\n\\verb|\\bibliography{not-a-real-input}|\n\\begin{verbatim}\n\\addbibresource{not-a-real-input.bib}\n\\end{verbatim}\nA manual reference \\cite{demo}.\n\\input{manual.bbl}\n\\end{document}\n";
 auto manualReference=[](bool updated){return QByteArray("\\begin{thebibliography}{1}\n\\bibitem{demo}")+(updated?"Manual second reference.":"Manual first reference.")+"\n\\end{thebibliography}\n";};
 check("bibliography-only authored input is created without a bib source",put(manual,manualSource)&&put(bbl,manualReference(false)));
 w=new TeXDocumentWindow(manual);w->show();v=nullptr;
 for(int round=0;round<2;round++){
  if(round==1)check("authored bbl external edit is accepted as new input",put(bbl,manualReference(true)));
  const QByteArray authored=read(bbl);const auto time=QFileInfo(bbl).lastModified();w->typeset();const bool done=waitFor([&]{return !w->isTypesetting();});if(!done){w->interrupt();waitFor([&]{return !w->isTypesetting();},10000);}v=w->pdfDocument();evidence.insert(QString("manualConsole%1").arg(round),w->property("consoleOutput").toString());evidence.insert(QString("manualBuildRoot%1").arg(round),w->property("researchBuildRoot").toString());
  check(QString("authored bbl: round %1 has a valid pair").arg(round),done&&Research::pdfPairValid(Research::previewFile(manual))&&v);
  check(QString("authored bbl: round %1 renders the current supplied reference").arg(round),pdfText(v).contains(round==0?"Manual first reference":"Manual second reference"));
  check(QString("authored bbl: round %1 never overwrites the authored bibliography").arg(round),read(bbl)==authored&&QFileInfo(bbl).lastModified()==time);
 }
 if(v)v->close();guard=w;w->setModified(false);w->close();waitFor([&]{return !guard;});check("bibliography-only project closes without retained windows",!guard);
 const QString externalFolder=base+"/external project",externalRoot=externalFolder+"/external.tex",externalBibFolder=base+"/outside bibliography",externalBib=externalBibFolder+"/outside.bib";
 const QByteArray externalSource=QByteArray(source).replace("\\bibliography{refs}","\\bibliography{outside}");
 check("bibliography outside the project is created as a real TeX search-path input",put(externalRoot,externalSource)&&put(externalBib,reference(false)));
 const QByteArray oldBibInputs=qgetenv("BIBINPUTS");qputenv("BIBINPUTS",externalBibFolder.toLocal8Bit()+";"+oldBibInputs);
 pre=engine.run(QFileInfo(externalRoot),&app);console.clear();const bool externalCompiled=pre&&waitFor([&]{console+=pre->readAllStandardOutput();return pre->state()==QProcess::NotRunning;});if(pre)console+=pre->readAllStandardOutput();put(base+"/external-precompiled-console.txt",console);
 check("normal TeX search-path compilation produces the supplied bibliography",externalCompiled&&pre->exitCode()==0&&QFileInfo::exists(externalFolder+"/external.bbl"));if(pre)delete pre;
 const auto externalArtifacts=snapshot(externalFolder);w=new TeXDocumentWindow(externalRoot);w->show();v=nullptr;
 for(int round=0;round<3;round++){
  if(round==1)check("external bibliography update is written only to the isolated input",put(externalBib,reference(true)));
  w->typeset();const bool done=waitFor([&]{return !w->isTypesetting();});if(!done){w->interrupt();waitFor([&]{return !w->isTypesetting();},10000);}v=w->pdfDocument();const QString text=pdfText(v);
  evidence.insert(QString("externalRenderedText%1").arg(round),text);evidence.insert(QString("externalConsole%1").arg(round),w->property("consoleOutput").toString());
  check(QString("search-path bibliography: round %1 publishes a valid pair").arg(round),done&&Research::pdfPairValid(Research::previewFile(externalRoot))&&v);
  check(QString("search-path bibliography: round %1 renders the current reference").arg(round),text.contains(round==0?"First reference title":"Updated reference title"));
  check(QString("search-path bibliography: round %1 never rewrites source-side products").arg(round),snapshot(externalFolder)==externalArtifacts);
 }
 check("search-path compilation preserves the manuscript and external bibliography",read(externalRoot)==externalSource&&read(externalBib)==reference(true));
 if(v)v->close();guard=w;w->setModified(false);w->close();waitFor([&]{return !guard;});qputenv("BIBINPUTS",oldBibInputs);check("search-path bibliography project releases its windows",!guard);
 save();app.exit(failures?1:0);
 });return app.exec();}

