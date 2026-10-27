// Same private sample and fixture before/after; no capture during timing.
#include "ActualAuthoringRecording.h"
#include "ResearchSlideExport.h"
using namespace Research;
// The frozen baseline predates session reuse. Keep the measured fixture identical.
template<class R> auto enableReuse(R &r,int) -> decltype(r.previewSession=std::make_shared<typename decltype(r.previewSession)::element_type>(),void()) {
  r.previewSession=std::make_shared<typename decltype(r.previewSession)::element_type>();
}
template<class R> void enableReuse(R &,long) {}
int main(int argc,char **argv) {
 const auto report=argument(1);
 if(!qEnvironmentVariable("TEXLINE_TEST_DESKTOP").startsWith("TeXlineValidation-"))return 2;
 argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
 QTimer::singleShot(60,&app,[&]{
  const auto base=qEnvironmentVariable("TEXLINE_COMPILE_PERF_ROOT");if(base.isEmpty()){app.exit(2);return;}
  QDir().mkpath(base);QJsonArray checks,samples;int failures=0;
  auto save=[&]{put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"samples",samples},{"project",base},{"captureDuringTiming",false},{"physicalInput",false}}).toJson());};
  auto check=[&](QString name,bool ok){checks.append(QJsonObject{{"name",name},{"passed",ok}});failures+=!ok;save();};
  const QString bin="C:/texlive/2023/bin/windows/";
  const QStringList names{"pdf-table","pdf-beamer","xe-chinese"};
  for(const auto &name:names){
   const auto dir=base+"/"+name,root=dir+"/main.tex",dest=dir+"/draft.pdf";QDir().mkpath(dir);
   const bool beamer=name.contains("beamer"),chinese=name.startsWith("xe-");
   const QString preamble=chinese?"\\documentclass{ctexart}\n\\usepackage{amsmath,booktabs}\n":(beamer?"\\documentclass{beamer}\n\\usepackage{amsmath,booktabs}\n":"\\documentclass{article}\n\\usepackage{amsmath,booktabs,graphicx,tikz}\n");
   SlideExportRequest request;request.root=root;request.engine=Engine(name,bin+(chinese?"xelatex.exe":"pdflatex.exe"),{"-no-shell-escape","-synctex=1","-interaction=nonstopmode","-halt-on-error","$fullname"},false);request.profiles={0};request.destinations={dest};enableReuse(request,0);
   for(int round=0;round<6;++round){
    const auto body=QString("\\begin{tabular}{lr}\\toprule\nObservation & %1\\\\\nEstimate & $\\frac{a+b}{c}$\\\\\\bottomrule\\end{tabular}\n").arg(round);
    const auto text=preamble+"\\begin{document}\n"+(beamer?"\\begin{frame}{Observation}\n":"")+(chinese?QString::fromUtf8("独立中文预览样稿。\n"):QString())+body+(beamer?"\\end{frame}\n":"")+"\\end{document}\n";
    put(root,text.toUtf8());request.root=QFileInfo(root).canonicalFilePath();request.sources[request.root]=text;request.destinationHashes[dest]=slideExportHash(dest);
    const auto token=std::make_shared<std::atomic_bool>(false);QElapsedTimer t;t.start();const auto result=exportSlideCopies(request,token);const double total=t.nsecsElapsed()/1e6;
    samples.append(QJsonObject{{"kind",name},{"round",round},{"cold",round==0},{"totalMs",total},{"preparationMs",result.preparationMs},{"compilerMs",result.compilationMs},{"publicationMs",result.publicationMs},{"passes",result.enginePasses}});
    check(name+" actual compiler "+QString::number(round),result.error.isEmpty()&&result.written.size()==1);if(!result.error.isEmpty()){put(QFileInfo(report).absolutePath()+"/"+name+"-failure.log",(result.error+result.log).toUtf8());break;}
    auto pdf=QtPDF::Backend::Document::newDocument(dest);check(name+" current PDF content "+QString::number(round),pdf&&pdf->numPages()==1&&pdf->page(0).toStrongRef()->boxes().size()>0);
   }
  }
  // User-visible automatic table preview, including unchanged debounce/rendering.
  const auto root=base+"/ui/main.tex";const QString text="\\documentclass{article}\n\\usepackage{amsmath,booktabs,graphicx,tikz}\n\\begin{document}\nIndependent source.\n\\end{document}\n";put(root,text.toUtf8());
  app.setEngineList({Engine("pdfLaTeX",bin+"pdflatex.exe",{"-synctex=1","-interaction=nonstopmode","-file-line-error","$fullname"},true)});
  Tw::Settings().setValue("openPDFwithTeX",false);setBuildCacheForTests(base+"/ui/cache");auto *window=new TeXDocumentWindow(root);window->resize(1120,740);window->show();QApplication::setActiveWindow(window);window->editor()->setFocus();settle(160);
  window->findChild<QAction*>("researchInsertTable")->trigger();QPointer<QDialog> dialog=window->findChild<QDialog*>("researchObjectDialog");check("actual table editor opens",dialog);
  if(dialog){dialog->resize(1080,700);settle(60);auto *grid=dialog->findChild<QTableWidget*>("researchTableGrid");auto *image=dialog->findChild<QLabel*>("researchObjectPreview");
   auto ready=[&]{return image&&image->property("researchDraftReady").toBool();};
   grid->item(0,0)->setText("Initial observation");check("initial automatic preview succeeds",waitFor(ready,60000));
   for(int round=0;round<6;++round){QElapsedTimer t;t.start();grid->item(0,0)->setText("Observation "+QString::number(round));const bool invalidated=!ready();const bool ok=waitFor(ready,60000);samples.append(QJsonObject{{"kind","visible-table"},{"round",round},{"totalMs",t.nsecsElapsed()/1e6}});check("edited preview is invalidated and becomes visible "+QString::number(round),invalidated&&ok);if(!ok)break;}
   // Captures are deliberately outside the performance samples.
   dialog->grab().save(QFileInfo(report).absolutePath()+"/table-preview.png");dialog->reject();settle(100);
   check("Cancel keeps source and undo history",window->textDoc()->editorText()==text&&!window->textDoc()->isModified());
  }
  window->textDoc()->setModified(false);window->close();save();app.exit(failures?1:0);
 });return app.exec();
}
