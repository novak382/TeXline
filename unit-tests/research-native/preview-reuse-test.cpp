// Real native engines, exact raster comparison and failure/cancel recovery.
#include "ActualAuthoringRecording.h"
#include "ResearchSlideExport.h"
#include <thread>
using namespace Research;
static QVector<QImage> pages(const QString &path){
 QVector<QImage> result;auto pdf=QtPDF::Backend::Document::newDocument(path);if(!pdf)return result;
 for(int i=0;i<pdf->numPages();++i)if(auto p=pdf->page(i).toStrongRef())result.append(p->renderToImage(96,96,QRect(),false));return result;
}
int main(int argc,char **argv){
 const auto report=argument(1);if(!qEnvironmentVariable("TEXLINE_TEST_DESKTOP").startsWith("TeXlineValidation-"))return 2;
 argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
 QTimer::singleShot(60,&app,[&]{
  const auto base=QFileInfo(report).absolutePath()+"/paper",root=base+"/preview-cache-proof.tex",dest=base+"/preview.pdf";
  QDir().mkpath(base);QJsonArray checks,details;int failures=0;
  auto save=[&]{put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"details",details}}).toJson());};
  auto check=[&](const QString &name,bool ok){checks.append(QJsonObject{{"name",name},{"passed",ok}});failures+=!ok;save();};
  const QString preamble="\\documentclass{article}\n\\usepackage{amsmath,graphicx}\n\\usepackage{local-preview}\n";
  const QString body="\\begin{document}\n\\previewword\n\\begin{equation}a=b\\label{eq:main}\\end{equation}\nReference \\ref{eq:main}.\n\\end{document}\n";
  const auto style=base+"/local-preview.sty";put(style,"\\newcommand{\\previewword}{Alpha}\n");
  SlideExportRequest request;request.root=root;request.engine=Engine("pdfLaTeX","C:/texlive/2023/bin/windows/pdflatex.exe",{"-no-shell-escape","-synctex=1","-interaction=nonstopmode","-halt-on-error","$fullname"},false);request.profiles={0};request.destinations={dest};request.previewSession=std::make_shared<SlidePreviewSession>();
  auto compile=[&](const QString &text,const QString &name){
   put(root,text.toUtf8());request.root=QFileInfo(root).canonicalFilePath();request.sources[request.root]=text;request.destinationHashes[dest]=slideExportHash(dest);
   const auto result=exportSlideCopies(request,std::make_shared<std::atomic_bool>(false));
   details.append(QJsonObject{{"name",name},{"passes",result.enginePasses},{"reused",result.reusedWorkspace},{"error",result.error}});
   if(!result.error.isEmpty())put(QFileInfo(report).absolutePath()+"/"+name+".log",(result.error+result.log).toUtf8());return result;
  };
  auto result=compile(preamble+body,"cold");check("cold independent preview succeeds",result.error.isEmpty()&&!result.reusedWorkspace&&result.enginePasses>=2);
  result=compile(preamble+body,"warm");check("stable preview reuses workspace and one engine pass",result.error.isEmpty()&&result.reusedWorkspace&&result.enginePasses==1);
  const auto changed=QString(body).replace("\\begin{equation}","\\setcounter{equation}{4}\n\\begin{equation}");
  result=compile(preamble+changed,"number-change");check("changed equation numbers rerun until auxiliary state settles",result.error.isEmpty()&&result.reusedWorkspace&&result.enginePasses>=2);
  const auto optimized=pages(dest);auto independent=request;independent.previewSession.reset();independent.destinations={base+"/independent.pdf"};independent.destinationHashes.clear();
  const auto cold=exportSlideCopies(independent,std::make_shared<std::atomic_bool>(false));check("warm PDF pixels exactly match fresh ordinary compilation",cold.error.isEmpty()&&!optimized.isEmpty()&&optimized==pages(independent.destinations.first()));
  auto doc=QtPDF::Backend::Document::newDocument(dest);check("the updated reference is visible in the real PDF",doc&&contains(doc,"Reference 5"));doc.clear();
  const auto styleTime=QFileInfo(style).lastModified();put(style,"\\newcommand{\\previewword}{Bravo}\n");{QFile f(style);f.open(QIODevice::ReadWrite);f.setFileTime(styleTime,QFileDevice::FileModificationTime);}
  result=compile(preamble+changed,"style-change");check("same-size same-time style edits invalidate the private state",result.error.isEmpty()&&!result.reusedWorkspace);doc=QtPDF::Backend::Document::newDocument(dest);check("changed style text replaces the old content",doc&&contains(doc,"Bravo")&&!contains(doc,"Alpha"));doc.clear();
  result=compile(preamble+"\\newcommand{\\newmacro}{New}\n"+changed,"preamble-change");check("preamble changes create a fresh workspace",result.error.isEmpty()&&!result.reusedWorkspace);
  const auto lastGood=slideExportHash(dest);result=compile(preamble+"\\newcommand{\\newmacro}{New}\n"+QString(changed).replace("a=b","\\DefinitelyUnknownPreviewCommand"),"syntax-error");check("a real TeX error preserves the last good PDF",!result.error.isEmpty()&&slideExportHash(dest)==lastGood&&result.written.isEmpty());
  result=compile(preamble+changed,"recovery");check("repairing source recovers with fresh state",result.error.isEmpty()&&!result.reusedWorkspace);
  auto token=std::make_shared<std::atomic_bool>(false);request.sources[request.root]=preamble+QString(body).replace("a=b","a=c");request.destinationHashes[dest]=slideExportHash(dest);const auto beforeCancel=slideExportHash(dest);
  std::thread cancel([token]{std::this_thread::sleep_for(std::chrono::milliseconds(100));token->store(true);});result=exportSlideCopies(request,token);cancel.join();check("in-flight cancellation keeps the previous output",result.cancelled&&result.written.isEmpty()&&slideExportHash(dest)==beforeCancel);
  result=compile(preamble+body,"after-cancel");check("cancelled state is retired before the next request",result.error.isEmpty()&&!result.reusedWorkspace);
  // Remove a required dependency while the old private copy still exists.
  const auto good=slideExportHash(dest);QFile::remove(style);result=compile(preamble+body,"missing-style");check("a missing original dependency is not served from stale private files",!result.error.isEmpty()&&slideExportHash(dest)==good);put(style,"\\newcommand{\\previewword}{Alpha}\n");result=compile(preamble+body,"dependency-recovery");check("restoring the dependency recovers",result.error.isEmpty());
  // Deliberately damage this test's own auxiliary file; no other session is touched.
  QString ownWorkspace;for(const auto &name:QDir(QDir::tempPath()).entryList({"texline-export-*"},QDir::Dirs|QDir::NoDotAndDotDot)){
   const auto dir=QDir::tempPath()+"/"+name;const auto copy=dir+"/source/preview-cache-proof.tex";
   if(QFileInfo::exists(copy)&&bytes(copy).contains("previewword")){ownWorkspace=dir;break;}
  }
  check("the private owned workspace can be identified",!ownWorkspace.isEmpty());
  if(!ownWorkspace.isEmpty()){put(ownWorkspace+"/output-0/preview-cache-proof.aux","\\InvalidAuxiliaryCommand\n");result=compile(preamble+body,"auxiliary-recovery");check("damaged retained auxiliaries fall back to an independent full compile",result.error.isEmpty()&&result.enginePasses>=3);}
  put(base+"/references.bib","@book{demo,title={Independent source},author={A. Author},year={2024}}\n");
  const QString bibliography="\\documentclass{article}\n\\usepackage[backend=biber]{biblatex}\n\\addbibresource{references.bib}\n\\begin{document}\nA draft \\cite{demo}.\\end{document}\n";
  // TeX Live 2023 writes nohash on pass 1 and nobblfile on pass 2;
  // an independent four-pass probe confirms that pass 3 settles this state.
  result=compile(bibliography,"biblatex-cold");check("bibliography state converges without futile tool-request retries",result.error.isEmpty()&&result.enginePasses>=2&&result.enginePasses<=3);
  result=compile(QString(bibliography).replace("A draft","Another draft"),"biblatex-warm");check("a bibliography preamble also supports stable warm preview",result.error.isEmpty()&&result.reusedWorkspace&&result.enginePasses==1);
  // Exercise actual Unicode engines and Beamer's multi-page state.
  for(const auto &engine:QStringList{"xelatex","lualatex","latexmk"}){
   request.engine=Engine(engine,"C:/texlive/2023/bin/windows/"+engine+".exe",engine=="latexmk"?QStringList{"-norc","-pdf","-interaction=nonstopmode","$fullname"}:QStringList{"-no-shell-escape","-synctex=1","-interaction=nonstopmode","-halt-on-error","$fullname"},false);
   const QString text=engine=="xelatex"?QString::fromUtf8("\\documentclass{ctexart}\n\\begin{document}中文公式 $a=b$。\\end{document}\n"):"\\documentclass{article}\n\\begin{document}Local engine $a=b$.\\end{document}\n";
   result=compile(text,engine+"-cold");check(engine+" cold real compilation",result.error.isEmpty()&&!result.reusedWorkspace);
   result=compile(QString(text).replace("a=b","a=c"),engine+"-warm");check(engine+" warm actual compilation",result.error.isEmpty()&&result.reusedWorkspace&&result.enginePasses==1);
  }
  request.engine=Engine("pdfLaTeX","C:/texlive/2023/bin/windows/pdflatex.exe",{"-no-shell-escape","-synctex=1","-interaction=nonstopmode","-halt-on-error","$fullname"},false);
  const QString slides="\\documentclass{beamer}\n\\begin{document}\n\\begin{frame}{Stages}\\begin{itemize}\\item<1-> First\\item<2-> Second\\item<3-> Third\\end{itemize}\\end{frame}\n\\end{document}\n";
  result=compile(slides,"beamer-cold");check("Beamer overlays compile from fresh state",result.error.isEmpty()&&!result.reusedWorkspace);
  result=compile(QString(slides).replace("Third","Final"),"beamer-warm");doc=QtPDF::Backend::Document::newDocument(dest);check("Beamer warm compile retains all overlays and the latest content",result.error.isEmpty()&&result.reusedWorkspace&&result.enginePasses==1&&doc&&doc->numPages()==3&&contains(doc,"Final"));doc.clear();
  check("private preview never writes the original PDF or auxiliary files",!QFileInfo::exists(base+"/preview-cache-proof.pdf")&&!QFileInfo::exists(base+"/preview-cache-proof.aux")&&bytes(root)==request.sources.value(request.root).toUtf8());
  request.previewSession.reset();check("closing the editor releases its private workspace",ownWorkspace.isEmpty()||!QFileInfo::exists(ownWorkspace));save();app.exit(failures?1:0);
 });return app.exec();
}
