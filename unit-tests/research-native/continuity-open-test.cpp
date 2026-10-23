#include "ActualAuthoringRecording.h"
#include "ResearchWorkflow.h"
#include "PDFDocumentWindow.h"
#include "document/TeXDocument.h"

int main(int argc,char **argv) {
  if(!qEnvironmentVariable("TEXLINE_TEST_DESKTOP").startsWith("TeXlineValidation-"))return 2;
  const auto report=argument(1);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
  QTimer::singleShot(100,&app,[&] {
    QJsonArray checks,runs;int failures=0;const auto folder=QFileInfo(report).absolutePath();
    auto check=[&](const QString &name,bool pass){checks.append(QJsonObject{{"name",name},{"passed",pass}});failures+=!pass;};
    Tw::Settings().setValue("openPDFwithTeX",false);
    Research::setBuildCacheForTests(folder+"/build-cache");
    for(bool chinese:{false,true}) {
      const QString name=chinese?"chinese":"english",dir=folder+"/"+name;
      const QString root=dir+"/main.tex",child=dir+"/sections/results.tex";
      const QString body=chinese?QString::fromUtf8("\\section{主要结果}\n研究结论与证明。\n"):QString("\\section{Results}\nThe transport bound is stable.\n");
      const QString source=(chinese?QString("\\documentclass{ctexart}\n"):QString("\\documentclass{article}\n"))+"\\begin{document}\n\\input{sections/results}\n\\end{document}\n";
      check(name+": private multi-file sample written",put(root,source.toUtf8())&&put(child,body.toUtf8()));
      auto *w=new TeXDocumentWindow(child);w->resize(qEnvironmentVariableIsSet("TEXLINE_TEST_HALF")?520:1100,760);w->show();settle(350);
      check(name+": opening the child detects its main file",QFileInfo(w->textDoc()->getRootFilePath())==QFileInfo(root));
      QMenu menu;w->populateResearchEngineMenu(&menu);const QString expected=chinese?"XeLaTeX":"pdfLaTeX";bool matched=false;
      for(auto *action:menu.actions())if(action->isChecked()&&Research::canonicalEngineName(action->text())==expected)matched=true;
      check(name+": automatic engine follows the main document",matched);
      auto *compile=w->findChild<QAction*>("actionTypeset");check(name+": normal Compile is available",compile&&compile->isEnabled());
      QElapsedTimer elapsed;elapsed.start();if(compile)compile->trigger();
      const bool finished=waitFor([&]{return !w->isTypesetting()&&!w->property("researchBuildPreparing").toBool()&&!w->property("researchBuildFinishing").toBool()&&!w->property("researchPublishing").toBool();},120000);
      const auto pdf=Research::previewFile(root);auto document=QtPDF::Backend::Document::newDocument(pdf);
      check(name+": compiling from the child produces its main PDF",finished&&Research::pdfPairValid(pdf)&&document&&document->isValid());
      check(name+": PDF contains the child content",contains(document,chinese?QString::fromUtf8("研究结论"):QString("The transport bound")));
      put(dir+"/compile.txt",w->property("consoleOutput").toString().toUtf8());
      runs.append(QJsonObject{{"case",name},{"milliseconds",double(elapsed.elapsed())},{"engine",expected},{"root",root},{"pdf",pdf}});
      if(finished&&Research::pdfPairValid(pdf)) {
        w->findChild<QAction*>("actionGo_to_Preview")->trigger();settle(250);
        check(name+": PDF is visible through normal preview",w->pdfDocument()&&w->pdfDocument()->isVisible());
        if(auto *viewer=w->pdfDocument()){viewer->resize(w->size());viewer->grab().save(dir+"/pdf.png");viewer->close();}
      }
      check(name+": independent source files unchanged",bytes(root)==source.toUtf8()&&bytes(child)==body.toUtf8());
      w->grab().save(dir+"/source.png");QPointer<TeXDocumentWindow> guard=w;w->setModified(false);w->close();settle();check(name+": source window closes cleanly",!guard);
      auto *again=new TeXDocumentWindow(child);again->show();settle(150);check(name+": reopening preserves root and engine",QFileInfo(again->textDoc()->getRootFilePath())==QFileInfo(root)&&Research::preferredEngine(child,again->textDoc()->editorText())==expected);again->setModified(false);again->close();settle();
    }
    put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"runs",runs},{"physicalInputVerified",false}}).toJson());app.exit(failures?1:0);
  });QTimer::singleShot(270000,&app,[&]{app.exit(93);});return app.exec();
}
