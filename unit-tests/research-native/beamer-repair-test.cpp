// Real private-copy Beamer compile, frame mapping, navigation and preview size.
#include "ActualAuthoringRecording.h"
#include "ResearchSlides.h"
#include "Engine.h"
#include "PDFDocumentWindow.h"
#include <PDFDocumentScene.h>
using namespace Research;
int main(int argc,char **argv){
  const auto report=argument(1);wchar_t desktop[256]{};DWORD needed=0;
  if(!GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()),UOI_NAME,desktop,sizeof(desktop),&needed)||QString::fromWCharArray(desktop)!=qEnvironmentVariable("TEXLINE_TEST_DESKTOP")||!QString::fromWCharArray(desktop).startsWith("TeXlineValidation-"))return 2;
  argc=1;QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
  QTimer::singleShot(50,&app,[&]{
    const auto folder=QFileInfo(report).absolutePath(),root=qEnvironmentVariable("TEXLINE_BEAMER_FIXTURE");QJsonArray checks,geometry;QJsonObject details;int failures=0;
    auto check=[&](const QString &name,bool passed){checks.append(QJsonObject{{"name",name},{"passed",passed}});failures+=!passed;put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"geometry",geometry},{"details",details},{"devicePixelRatio",qApp->primaryScreen()->devicePixelRatio()},{"physicalInputVerified",false}}).toJson());};
    const auto original=bytes(root);check("private real lecture exists",!root.isEmpty()&&!original.isEmpty());
    const auto parsed=parseSlideDeck(root);details["parserReason"]=parsed.reason;details["parsedFrames"]=parsed.frames.size();check("fifteen logical frames include macro cover",parsed.beamer&&!parsed.uncertain&&parsed.frames.size()==15);
    check("inline table is tracked as a source",parsed.sources.contains(QFileInfo(QFileInfo(root).dir().filePath("tables/full_results.tex")).canonicalFilePath()));

    const auto negative=folder+"/negative/main.tex",child=folder+"/negative/child.tex";put(child,"\\cover\n");
    auto parseText=[&](const QString &text){return parseSlideDeck(negative,{{QFileInfo(negative).absoluteFilePath(),text}});};
    const QString prefix="\\documentclass{beamer}\n\\newcommand{\\cover}{\\begin{frame}\\titlepage\\end{frame}}\n",tail="\\begin{frame}{Body}Text.\\end{frame}\n\\end{document}\n";
    auto d=parseText(prefix+"\\begin{document}\n\\input{child}\n"+tail);check("wrapper at first character of child has valid range",!d.uncertain&&d.frames.size()==2&&d.frames[0].start==0&&d.frames[0].end>0&&d.frames[0].sliceable&&!d.frames[0].editable&&d.frames[0].line==1);
    d=parseText(prefix+"\\begin{document}\n"+tail);check("unused frame macro does not become an executed frame",!d.uncertain&&d.frames.size()==1);
    d=parseText(prefix+"\\renewcommand{\\cover}{\\begin{frame}A\\end{frame}\\begin{frame}B\\end{frame}}\n\\begin{document}\n\\cover\n"+tail);check("two frame redefinition revokes wrapper proof",d.uncertain);
    d=parseText(prefix+"\\iftrue\\renewcommand{\\cover}{}\\fi\n\\begin{document}\n\\cover\n"+tail);check("conditional redefinition revokes wrapper proof",d.uncertain);
    d=parseText(prefix+"\\renewcommand{\\cover}[1]{\\begin{frame}#1\\end{frame}}\n\\begin{document}\n\\cover{A}\n"+tail);check("parameter macro is not guessed as a single frame",d.uncertain);
    d=parseText("\\documentclass{beamer}\\begin{document}\n\\begin{frame}\n\\input{missing}\n\\end{frame}\n\\end{document}");check("missing inline input remains uncertain",d.uncertain);
    const auto tablePath=QFileInfo(QFileInfo(root).dir().filePath("tables/full_results.tex")).canonicalFilePath();auto tableBuffers=QHash<QString,QString>{{tablePath,"\\begin{frame}Nested\\end{frame}"}};d=parseSlideDeck(root,tableBuffers);check("structural inline input cannot certify mapping",d.uncertain);
    tableBuffers[tablePath]="\\newcommand{\\later}{\\input{missing}}\n"+QString::fromUtf8(bytes(tablePath));d=parseSlideDeck(root,tableBuffers);check("unused inline definition does not read its missing input",!d.uncertain&&d.frames.size()==15);
    const QString boundary="\\documentclass{beamer}\n\\begin{document}\n\\begin{frame}{Before}First.\\end{frame}\n",groupBody="\\setbeamertemplate{footline}[footlineoff]\n\\usebackgroundtemplate{}\n\\begin{frame}[plain]\nClosing.\n\\end{frame}\n",ending="\\end{document}\n";
    const auto grouped=boundary+"{\n"+groupBody+"}\n"+ending;d=parseText(grouped);
    const bool groupedOk=!d.uncertain&&d.frames.size()==2;
    check("standalone local style group is one readonly slice",groupedOk&&d.frames.last().kind=="groupframe"&&d.frames.last().sliceable&&!d.frames.last().editable&&grouped.mid(d.frames.last().start,d.frames.last().end-d.frames.last().start).trimmed()==("{\n"+groupBody+"}").trimmed());
    check("grouped frame certifies literal close rather than closing brace",groupedOk&&d.frames.last().endLine==int(QStringView(grouped).left(grouped.lastIndexOf("\\end{frame}")).count('\n'))+1);
    d=parseText(boundary+"\\unknown\n{\n"+groupBody+"}\n"+ending);check("unknown command argument group is not executed",d.uncertain);
    d=parseText(boundary+"\\iftrue\n{\n"+groupBody+"}\n\\fi\n"+ending);check("conditional local style group remains uncertain",d.uncertain);
    d=parseText(boundary+"{\n"+groupBody+"\\begin{frame}Second.\\end{frame}\n}\n"+ending);check("multiple frames in a local group remain uncertain",d.uncertain);
    d=parseText(boundary+"{\n"+groupBody+ending);check("unclosed local style group remains uncertain",d.uncertain);
    d=parseText(boundary+"{\n"+QString(groupBody).replace("[plain]","[allowframebreaks]")+"}\n"+ending);check("unsupported grouped continuation is not misclassified as overlays",d.uncertain);
    Tw::Settings().setValue("openPDFwithTeX",false);Tw::Settings().setValue("researchBuildDestinationPolicy","local-cache");setBuildCacheForTests(folder+"/cache");
    app.setEngineList({Engine("XeLaTeX","C:/texlive/2023/bin/windows/xelatex.exe",{"-synctex=1","-interaction=nonstopmode","-file-line-error","$fullname"},true)});
    auto *w=new TeXDocumentWindow(root);w->resize(qEnvironmentVariableIsSet("TEXLINE_TEST_HALF")?740:1460,900);w->show();settle();
    check("slide mode recognized",waitFor([&]{return w->property("researchSlideMode").toBool();}));auto *list=w->findChild<QListWidget*>("researchSlideList");auto *preview=w->findChild<QtPDF::PDFDocumentView*>("researchSlidePreview");
    if(qEnvironmentVariableIsSet("TEXLINE_TEST_FOCUS")){
      const auto beforeFocus=w->textDoc()->editorText();setFocusMode(true);settle();check("focus mode keeps source editable and unchanged",Tw::Settings().value("researchFocusMode").toBool()&&w->editor()->viewport()->isVisible()&&w->textDoc()->editorText()==beforeFocus);w->grab().save(folder+"/focus-source.png");
      setFocusMode(false);check("leaving focus restores slide mode",waitFor([&]{auto *directory=w->findChild<QListWidget*>("researchSlideList");return w->property("researchSlideMode").toBool()&&w->findChild<QtPDF::PDFDocumentView*>("researchSlidePreview")&&directory&&directory->count()==15;}));
      list=w->findChild<QListWidget*>("researchSlideList");preview=w->findChild<QtPDF::PDFDocumentView*>("researchSlidePreview");
    }
    check("slide directory and preview exist",list&&preview);if(!list||!preview){app.exit(1);return;}
    check("real source rows arrive",waitFor([&]{return list->count()==parsed.frames.size();}));
    auto visible=[&]{QString text;for(auto b=w->editor()->document()->begin();b.isValid();b=b.next())if(b.isVisible())text+=b.text()+"\n";return text;};
    auto choose=[&](int row){if(row<0||row>=list->count())return;list->setCurrentRow(row);list->itemClicked(list->item(row));settle(80);};
    for(int i=0;i<list->count();++i){choose(i);check(QString("navigation keeps one source slice %1").arg(i),w->editor()->property("researchSourceSlice").toBool()&&!slideReadingState(w).value("full").toBool()&&!visible().contains("\\documentclass"));}
    choose(qMin(7,list->count()-1));auto *expand=w->findChild<QAction*>("researchSlideExpandPreview");if(expand)expand->trigger();settle(300);w->grab().save(folder+"/before-compile.png");
    auto compile=[&]{w->typeset();const bool stopped=waitFor([&]{return !w->isTypesetting()&&!w->property("researchBuildPreparing").toBool()&&!w->property("researchBuildFinishing").toBool()&&!w->property("researchPublishing").toBool();},60000);settle(1200);return stopped;};
    check("real XeLaTeX first build completes",compile()&&!previewFile(root).isEmpty());check("real XeLaTeX second pass completes",compile());
    auto *dock=w->findChild<QDockWidget*>("researchSlidePreviewDock");if(dock)dock->show();settle();
    auto fitCheck=[&](const QString &tag){auto *scene=dynamic_cast<QtPDF::PDFDocumentScene*>(preview->scene());if(!scene){check(tag+" PDF scene exists",false);return;}auto *page=scene->pageAt(preview->currentPage());if(!page){check(tag+" PDF page exists",false);return;}const auto r=preview->mapFromScene(page->sceneBoundingRect()).boundingRect();const auto view=preview->viewport()->rect();geometry.append(QJsonObject{{"tag",tag},{"pageWidth",r.width()},{"pageHeight",r.height()},{"viewportWidth",view.width()},{"viewportHeight",view.height()},{"zoom",preview->zoomLevel()}});check(tag+" page fits and fills preview",r.width()<=view.width()+3&&r.height()<=view.height()+3&&qMax(double(r.width())/view.width(),double(r.height())/view.height())>.82);};
    fitCheck("compiled expanded");w->grab().save(folder+"/compiled-preview.png");
    auto *step=w->findChild<QSpinBox*>("researchSlideStep");check("literal and macro frames map to PDF pages",list->count()==15&&step&&step->isEnabled());
    for(int i=0;i<list->count();++i){choose(i);check(QString("preview follows frame %1").arg(i),int(preview->currentPage())==i);}
    choose(qMin(7,list->count()-1));if(expand)expand->trigger();settle();check("table frame stays in single source view",w->editor()->property("researchSourceSlice").toBool()&&visible().contains("tables/full_results.tex")&&!visible().contains("\\documentclass"));w->grab().save(folder+"/table-slide-source.png");
    if(list->count()>3){choose(2);const auto prior=w->textDoc()->editorText();auto d=parseSlideDeck(root,{{QFileInfo(root).canonicalFilePath(),prior}});QString reason;const bool moved=editSlide(w,d,2,SlideEdit::Down,&reason);check("adjacent slide move succeeds",moved);if(moved){check("structural move never flashes full source",w->editor()->property("researchSourceSlice").toBool());settle(1000);check("move preserves single source scope",w->editor()->property("researchSourceSlice").toBool()&&!slideReadingState(w).value("full").toBool());w->editor()->undo();check("undo never flashes full source",w->editor()->property("researchSourceSlice").toBool());settle(1000);check("one undo restores source and scope",w->textDoc()->editorText()==prior&&w->editor()->property("researchSourceSlice").toBool());}}

    check("navigation did not alter manuscript bytes",w->textDoc()->editorText()==QString::fromUtf8(original));
    const auto state=slideReadingState(w);auto *inputWindow=new TeXDocumentWindow(tablePath);inputWindow->show();settle();inputWindow->editor()->insertPlainText("% private unsaved table edit\n");settle(1400);auto *status=w->findChild<QLabel*>("researchSlideStatus");check("unsaved inline dependency marks preview previous",status&&(status->text().contains("Previous")||status->text().contains(QString::fromUtf8("上一版"))));inputWindow->editor()->undo();settle(1400);check("undo restores current certified preview",status&&status->isHidden());inputWindow->textDoc()->setModified(false);inputWindow->close();
    const auto background=QFileInfo(root).dir().filePath("theme/ustc_cover.tex");auto *backgroundWindow=new TeXDocumentWindow(background);backgroundWindow->show();settle();backgroundWindow->editor()->insertPlainText("% private unsaved background edit\n");settle(1400);check("unsaved compiler-tracked background marks preview previous",status&&(status->text().contains("Previous")||status->text().contains(QString::fromUtf8("上一版"))));backgroundWindow->editor()->undo();settle(1400);check("background undo restores current certified preview",status&&status->isHidden());backgroundWindow->textDoc()->setModified(false);backgroundWindow->close();restoreSlideReadingState(w,state);w->textDoc()->setModified(false);w->close();
    const auto dynamic=folder+"/unmapped/main.tex";put(dynamic,"\\documentclass[aspectratio=169]{beamer}\n\\begin{document}\n\\iftrue\n\\begin{frame}{One}First marker.\\end{frame}\n\\begin{frame}{Two}Second marker.\\end{frame}\n\\begin{frame}{Three}Third marker.\\end{frame}\n\\fi\n\\end{document}\n");
    w=new TeXDocumentWindow(dynamic);w->resize(1460,900);w->show();check("unmapped fallback mode starts",waitFor([&]{return w->findChild<QtPDF::PDFDocumentView*>("researchSlidePreview")!=nullptr;}));preview=w->findChild<QtPDF::PDFDocumentView*>("researchSlidePreview");if(!preview){app.exit(1);return;}expand=w->findChild<QAction*>("researchSlideExpandPreview");if(expand)expand->trigger();settle(300);check("unmapped valid PDF compiles",compile()&&!previewFile(dynamic).isEmpty());fitCheck("unmapped first load");w->grab().save(folder+"/unmapped-fitted.png");
    if(preview&&preview->scene()){preview->goToPage(2,Qt::AlignCenter);preview->setZoomLevel(preview->zoomLevel()*.8);settle(200);auto *position=w->findChild<QLabel*>("researchSlidePosition");check("unmapped position states the physical PDF page",position&&position->text().contains("3 / 3")&&position->text().contains("PDF")&&!position->text().contains("preview slide"));const auto manualZoom=preview->zoomLevel();const auto priorPdf=bytes(previewFile(dynamic));auto edit=w->editor()->document()->find("Third marker.");edit.insertText("Third marker revised.");check("unmapped replacement PDF compiles",compile()&&bytes(previewFile(dynamic))!=priorPdf&&contains(preview->document().toStrongRef(),"Third marker revised."));check("unmapped rebuild keeps physical page and manual zoom",int(preview->currentPage())==2&&qAbs(preview->zoomLevel()-manualZoom)<.001);check("unmapped replacement retains an honest physical page label",position&&position->text().contains("3 / 3")&&position->text().contains("PDF"));w->grab().save(folder+"/unmapped-recompiled.png");}
    w->textDoc()->setModified(false);w->close();app.exit(failures?1:0);

  });return app.exec();
}
