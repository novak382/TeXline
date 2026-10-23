// Run only with private resources/settings on an inactive validation desktop.
#include "ActualAuthoringRecording.h"
#include "ResearchAnnotations.cpp"
#include <QGraphicsSceneMouseEvent>
#include <cstdio>
using namespace Research;
static LONG WINAPI reportAnnotationFault(EXCEPTION_POINTERS *exception) {
  std::fprintf(stderr,"Native fault at %p\n",exception->ExceptionRecord->ExceptionAddress);
  void *stack[40];const auto count=CaptureStackBackTrace(0,40,stack,nullptr);
  for(unsigned i=0;i<count;++i)std::fprintf(stderr,"%p\n",stack[i]);
  std::fflush(stderr);return EXCEPTION_CONTINUE_SEARCH;
}
static QPoint viewportPoint(PDFDocumentWindow *window,int number,const QPointF &top) {
  auto *view=window->widget();
  for(auto *item:view->scene()->items())if(auto *page=qgraphicsitem_cast<QtPDF::PDFPageGraphicsItem*>(item)) {
    if(page->pageNum()!=number)continue;
    const auto backend=page->page().toStrongRef();if(!backend)return {};
    return view->mapFromScene(page->mapToScene(page->mapFromPage(QPointF(top.x(),backend->pageSizeF().height()-top.y()))));
  }
  return {};
}
static void dragSelection(QWidget *viewport,const QPoint &first,const QPoint &last,Recording *recording=nullptr) {
  auto send=[&](QEvent::Type type,const QPoint &point,Qt::MouseButton button,Qt::MouseButtons buttons) {
    QMouseEvent event(type,QPointF(point),QPointF(viewport->mapToGlobal(point)),button,buttons,Qt::NoModifier);
    QApplication::sendEvent(viewport,&event);
  };
  if(recording){recording->aim(viewport,first);recording->action("Select actual rendered text");recording->fastFor(750);}
  send(QEvent::MouseMove,first,Qt::NoButton,Qt::NoButton);
  send(QEvent::MouseButtonPress,first,Qt::LeftButton,Qt::LeftButton);
  for(int n=1;n<=8;++n){const auto p=first+(last-first)*n/8;send(QEvent::MouseMove,p,Qt::NoButton,Qt::LeftButton);if(recording)recording->pointer=viewport->mapToGlobal(p);settle(recording?35:5);}
  send(QEvent::MouseButtonRelease,last,Qt::LeftButton,Qt::NoButton);settle(70);
}
int main(int argc,char **argv) {
  SetUnhandledExceptionFilter(reportAnnotationFault);
  const QString report=argument(1);argc=1;TWApp app(argc,argv);
  QTimer::singleShot(0,&app,[&] {
    QJsonArray checks;int failures=0;
    auto check=[&](const QString &name,bool passed){checks.append(QJsonObject{{"name",name},{"passed",passed}});if(!passed)++failures;std::fprintf(stderr,"%s: %s\n",passed?"PASS":"FAIL",name.toUtf8().constData());std::fflush(stderr);};
    auto saveReport=[&] {
      put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"physicalInputVerified",false},
        {"realImeVerified",false},{"foregroundDwmVerified",false},{"devicePixelRatio",qApp->primaryScreen()->devicePixelRatio()}}).toJson());
    };
    auto active=OpenInputDesktop(0,FALSE,DESKTOP_READOBJECTS);wchar_t name[256]={};DWORD length=0;
    if(active){GetUserObjectInformationW(active,UOI_NAME,name,sizeof(name),&length);CloseDesktop(active);}
    check("private inactive desktop",qEnvironmentVariable("TEXLINE_TEST_DESKTOP").startsWith("TeXlineValidation-")&&QString::fromWCharArray(name)!=qEnvironmentVariable("TEXLINE_TEST_DESKTOP"));
    if(failures){saveReport();app.exit(1);return;}
    const QString paragraph="A measured decay is fitted with a single exponential.\nIts uncertainty is estimated independently.\n";
    check("paragraph stops at a real blank line",annotationSpan(paragraph+"\nNext paragraph.",8).end==paragraph.size()-1);
    const QString nested="Before.\n\n\\begin{proof}\nA {nested group} and $a_{i}^2$.\n\\begin{align}x&=y\\end{align}\n\\end{proof}\n\nAfter.";
    const auto proof=annotationSpan(nested,nested.indexOf("nested"));
    check("a proof paragraph is annotated before its next display",proof.valid()&&proof.end<=nested.indexOf("\\begin{align}"));
    const auto alignment=annotationSpan(nested,nested.indexOf("x&=y"));
    check("a nested display remains an atomic block",alignment.valid()&&alignment.environment=="align"&&nested.mid(alignment.insertion).startsWith("\n\\end{proof}"));
    check("unbalanced group is rejected",!annotationSpan("Before { unfinished",1).valid());
    const QString coloured="{\\color{blue}\nFirst coloured paragraph.\n\nSecond coloured paragraph.\n}\n";
    const auto colouredSpan=annotationSpan(coloured,coloured.indexOf("First"));
    check("a long colour group keeps the annotation beside its paragraph",colouredSpan.valid()&&colouredSpan.insertion<coloured.indexOf("Second"));
    const auto macroSpan=annotationSpan("A \\textbf{grouped word} remains one paragraph.\n\nAfter.",14);
    check("macro arguments are not split by annotation helpers",macroSpan.valid()&&macroSpan.insertion>35);
    const QString selectedParagraphs="\\documentclass{article}\n\\begin{document}\nFirst selected paragraph.\n\nSecond selected paragraph.\n\nUnselected paragraph.\n\\end{document}";
    auto range=makeAnnotation("range.tex",selectedParagraphs,selectedParagraphs.indexOf("First"),"First…Second",0,{},selectedParagraphs.indexOf("Second"));
    range.id="A01";range.opinion="Compare these two paragraphs.";
    const auto rangeCopy=annotationSources(range.file,{{range.file,selectedParagraphs}},{range});
    const auto rangeText=rangeCopy.texts.value(range.file);
    check("a multi-paragraph annotation follows the complete selected range",rangeCopy.error.isEmpty()&&rangeText.indexOf("\\TeXlineReviewNote{A01}")>rangeText.indexOf("Second selected")&&rangeText.indexOf("\\TeXlineReviewNote{A01}")<rangeText.indexOf("Unselected"));
    check("commented environment cannot swallow a paragraph",annotationSpan("% \\begin{proof}\nA real paragraph.\n\nNext.",22).valid());
    check("display math is not split at blank lines",annotationSpan("Before.\n\n\\[\nx^2\n\n+y^2\n\\]\n\nAfter.",14).end>=24);
    auto anchor=makeAnnotation("note.tex",paragraph,8,"measured decay",0,{80,90,120,12});
    anchor.id="A01";anchor.opinion="Give a 95% interval for $D$; preserve a_b & {labels}.";
    check("original source resolves exactly",resolveAnnotation(anchor,paragraph)==0);
    check("anchor follows inserted paragraphs",resolveAnnotation(anchor,"New introduction.\n\n"+paragraph)==19);
    check("changed original block requires verification",resolveAnnotation(anchor,QString(paragraph).replace("single","double"))<0);
    QString unsafe;annotationTeX("$\\input{private}$",&unsafe);check("document commands inside opinion math are rejected",!unsafe.isEmpty());
    const auto tex=annotationTeX(anchor.opinion);
    check("prose escapes special characters while preserving explicit math",tex.contains("95\\%")&&tex.contains("a\\_")&&tex.contains("\\& \\{labels\\}")&&tex.contains("$D$"));
    check("record round trip preserves quote and position",annotationJson(annotationFromJson(annotationJson(anchor)))==annotationJson(anchor));
    const QString folder=QFileInfo(report).absolutePath()+"/paper";QDir().mkpath(folder+"/sections");
    const QString root=canonical(folder+"/diffusion.tex"),child=canonical(folder+"/sections/measurement.tex");
    const QString main="% !TeX program = pdfLaTeX\n\\documentclass[a4paper,11pt]{article}\n"
      "\\usepackage[margin=24mm]{geometry}\n\\usepackage{amsmath,graphicx,hyperref}\n"
      "\\title{Estimating diffusion from single-particle tracks}\n\\author{Research notebook}\n"
      "\\begin{document}\n\\maketitle\n\\section{Measurement}\n"
      "The measured correlation decays over the acquisition window. We report uncertainty separately from the point estimate. A measured correlation is inspected again.\n\n"
      "\\input{sections/measurement}\n\\section{Interpretation}\n"
      "A change in exposure time can alter the measured displacement distribution. The analysis therefore retains the acquisition settings.\n\n"
      "\\end{document}\n";
    const QString body="Each trajectory is centred before calculating the lagged displacement.\n\n"
      "\\begin{equation}\\label{eq:diffusion}\nD=\\frac{1}{2d}\\frac{\\mathrm{d}}{\\mathrm{d}t}\\mathbb{E}\\|X(t)-X(0)\\|^2.\n\\end{equation}\n\n"
      "\\begin{figure}[tbp]\n\\centering\\includegraphics[width=.55\\linewidth]{figures/displacement.png}\n"
      "\\caption{Displacement estimated at successive time lags.}\\label{fig:track}\n\\end{figure}\n\n"
      "\\begin{table}[htbp]\\centering\\begin{tabular}{lrr}Lag & Estimate & SE\\\\1 & 0.34 & 0.05\\\\2 & 0.61 & 0.08\\end{tabular}"
      "\\caption{Lagged displacement estimates.}\\end{table}\n\n";
    // amssymb supplies the expectation glyph in this independent fixture.
    const QString mainText=QString(main).replace("amsmath,graphicx","amsmath,amssymb,graphicx");
    check("independent multi-file manuscript",put(root,mainText.toUtf8())&&put(child,body.toUtf8()));
    QDir().mkpath(folder+"/figures");const QString graphic=folder+"/figures/displacement.png";
    QImage plot(420,220,QImage::Format_RGB32);plot.fill(Qt::white);{
      QPainter p(&plot);p.setRenderHint(QPainter::Antialiasing);p.setPen(QPen(QColor("#545e66"),1.5));
      p.drawLine(50,180,390,180);p.drawLine(50,180,50,20);p.setFont(QFont("Segoe UI",10));p.drawText(160,212,"Time lag");p.drawText(12,18,"MSD");
      p.setPen(QPen(QColor("#596f7c"),2));QPolygonF points{{75,157},{135,133},{195,104},{255,79},{315,50},{375,28}};p.drawPolyline(points);
      for(const auto &point:points){p.drawEllipse(point,3,3);p.drawLine(point-QPointF(0,8),point+QPointF(0,8));}
    }check("independent displacement graphic",plot.save(graphic));
    Tw::Settings().setValue("openPDFwithTeX",true);Tw::Settings().setValue("researchFocusMode",false);
    const auto available=qApp->primaryScreen()->availableGeometry();
    const int height=qMin(730,available.height()-24);const bool sideBySide=available.width()>=1240;
    auto *source=new TeXDocumentWindow(root);source->setGeometry(available.left()+20,available.top()+12,sideBySide?600:qMin(640,available.width()-40),height);source->show();
    source->typeset();
    check("normal compilation finishes",waitFor([&]{return !source->isTypesetting()&&!source->property("researchBuildPreparing").toBool()&&!source->property("researchBuildFinishing").toBool()&&!source->property("researchPublishing").toBool();},60000));
    const QString pdf=previewFile(root);check("normal PDF is certified",pdfCompiledPairValid(pdf));settle(200);
    auto *viewer=PDFDocumentWindow::findDocument(pdf);if(!viewer)viewer=new PDFDocumentWindow(pdf,source);
    viewer->setGeometry(available.left()+(sideBySide?625:20),available.top()+12,sideBySide?600:qMin(640,available.width()-40),height);viewer->show();viewer->widget()->zoomFitWidth();settle(250);
    const auto requested=qEnvironmentVariable("TEXLINE_ANNOTATION_DPR_REQUEST").toDouble();
    check("requested display scale applies to the actual PDF widget",requested<=0||qAbs(viewer->widget()->devicePixelRatioF()-requested)<.015);
    auto *switcher=viewer->findChild<QWidget*>("researchAnnotationSwitch");
    auto *pill=viewer->findChild<QToolButton*>("researchAnnotationPill");
    check("no review controls before actual annotations",switcher&&!switcher->isVisible()&&pill&&!pill->isVisible());
    const auto originalBytes=bytes(root),childBytes=bytes(child),pdfBytes=bytes(pdf);
    const auto originalEditor=source->textDoc()->editorText();const int cursor=source->textCursor().position(),undo=source->textDoc()->availableUndoSteps();
    const auto snapshots=pdfCompiledSources(pdf);QVector<Annotation> annotations;
    auto note=[&](const QString &file,const QString &needle,const QString &opinion) {
      auto n=makeAnnotation(file,snapshots.value(file),snapshots.value(file).indexOf(needle),needle,0,{});
      n.id=QString("A%1").arg(annotations.size()+1,2,10,QChar('0'));n.opinion=opinion;annotations.append(n);return n;
    };
    note(root,"The measured","请补充 $D$ 的置信区间，并说明曝光时间对估计的影响。");
    annotations.first().quote="The measured correlation decays over the acquisition window. We report uncertainty separately from the point estimate.";
    note(child,"D=\\frac","State the units of $D$ and distinguish localisation noise from diffusion.");
    note(child,"\\begin{figure}","Explain the error bars beneath this figure. "+QString("Retain the original track identifiers. ").repeated(30));
    note(child,"\\begin{table}","Define SE and report the number of trajectories used at each lag.");
    const auto prepared=annotationSources(root,snapshots,annotations);
    check("temporary preparation leaves caller snapshots unchanged",snapshots.value(root)==mainText&&snapshots.value(child)==body);
    check("multi-file opinions injected in the right files",prepared.error.isEmpty()&&prepared.texts.value(root).contains("{A01}")&&prepared.texts.value(child).contains("{A02}")&&prepared.texts.value(child).contains("{A04}"));
    check("figure opinion follows an in-place pinned float",prepared.texts.value(child).contains("\\begin{figure}[H]")&&prepared.texts.value(child).indexOf("{A03}")>prepared.texts.value(child).indexOf("\\end{figure}"));
    check("original paper dimensions are not overridden",!prepared.texts.value(root).contains("paperwidth=")&&!prepared.texts.value(root).contains("paperheight="));
    AnnotationRequest request{root,pdf,snapshots,pdfCompiledAux(pdf),annotations,TWApp::instance()->getNamedEngine("pdfLaTeX")};
    const auto result=compileAnnotations(request,std::make_shared<std::atomic_bool>(false));
    put(QFileInfo(report).absolutePath()+"/direct-compiler.txt",(result.error+"\n"+result.log).toUtf8());
    check("actual pdfLaTeX typesets Chinese and math opinions",result.error.isEmpty()&&!result.pdf.isEmpty());
    if(!result.pdf.isEmpty()) {
      const auto compiled=QtPDF::Backend::Document::newDocument(result.pdf);
      check("review opinions are visible searchable PDF text",contains(compiled,"A01")&&contains(compiled,"confidence")==false&&contains(compiled,"State the units")&&contains(compiled,"Define SE"));
      check("Chinese annotation survives PDF extraction",contains(compiled,"置信区间"));
      const auto locations=result.underlines.value("A01");int lines=0;for(const auto &at:locations)lines+=at.rects.size();
      check("an actual multi-line quote receives separate underlines in the compiled review",lines>=2&&result.modified.value(root).contains("shipout/foreground"));
      if(!locations.isEmpty()) {
        const auto &at=locations.first();auto target=annotations.first();target.quote="measured correlation";
        const auto exact=annotationLocations(target,compiled,at.page,at.rects);
        bool narrow=!exact.isEmpty();for(const auto &loc:exact)for(const auto &box:loc.rects)narrow&=box.width()<140&&box.width()>30;
        check("underlines stop at the selected phrase without marking its full paragraph",narrow);
        const auto repeated=compiled->page(at.page).toStrongRef()->search("measured correlation",QtPDF::Backend::Search_CaseInsensitive);
        bool correctOccurrence=false;
        if(repeated.size()>=2) {
          target.selectionRects={repeated.last().bbox};target.referenceBox=annotationBounds(at.rects);
          const auto second=annotationLocations(target,compiled,at.page,at.rects,{},true,true);
          correctOccurrence=!second.isEmpty()&&QLineF(annotationBounds(second.first().rects).center(),repeated.last().bbox.center()).length()<2;
        }
        check("a repeated phrase keeps the occurrence actually annotated",correctOccurrence);
      }
      check("full paper is retained alongside opinions",contains(compiled,"Estimating diffusion")&&contains(compiled,"Interpretation")&&contains(compiled,"Lagged displacement"));
      bool a4=compiled&&compiled->numPages()>0;
      for(int i=0;compiled&&i<compiled->numPages();++i){auto p=compiled->page(i).toStrongRef();a4&=p&&qAbs(p->pageSizeF().width()-595.28)<.6&&qAbs(p->pageSizeF().height()-841.89)<.6;}
      check("all review pages remain A4",a4);
      put(QFileInfo(report).absolutePath()+"/annotated.pdf",bytes(result.pdf));
      check("static exported PDF can be reopened independently",contains(QtPDF::Backend::Document::newDocument(QFileInfo(report).absolutePath()+"/annotated.pdf"),"A04"));
      AnnotationSync sync(result,pdf,root);
      const int line=body.left(body.indexOf("D=\\frac")).count('\n')+1;
      const auto located=sync.syncFromTeX({child,line,0,-1},TWSynchronizer::LineResolution);
      check("review copy supports forward source positioning",located.page>0&&!located.rects.isEmpty());
      if(located.page>0&&!located.rects.isEmpty()) {
        const auto inverse=sync.syncFromPDF({pdf,located.page,{QRectF(located.rects.first().center(),QSizeF())}},TWSynchronizer::LineResolution);
        check("review inverse positioning returns original source rather than a temporary file",canonical(inverse.filename)==child&&inverse.line>=line-1&&inverse.line<=line+1);
      }
      auto same=annotations;same.append(annotations.first());same.last().id="A05";same.last().opinion="A second independent opinion.";
      const auto multiple=annotationSources(root,snapshots,same).texts.value(root);
      check("multiple opinions on one paragraph retain creation order",multiple.indexOf("{A01}")<multiple.indexOf("{A05}"));
      auto changed=snapshots;changed[child].replace("D=\\frac","D=2\\frac");
      check("ambiguous or changed anchors do not silently export",!annotationSources(root,changed,annotations).error.isEmpty());
      auto cancelled=std::make_shared<std::atomic_bool>(true);
      check("cancelled background request publishes nothing",compileAnnotations(request,cancelled).cancelled);
      if(qEnvironmentVariableIsSet("TEXLINE_ANNOTATION_DEEP")) {
        QTemporaryDir crossVolume(QStringLiteral("D:/TeXworks-Research/validation/intuitive-cross-XXXXXX"));
        check("independent second-volume fixture exists",crossVolume.isValid());
        if(crossVolume.isValid()) {
          const auto supplement=canonical(crossVolume.filePath(QStringLiteral("supplement.tex")));const QByteArray external("Cross-volume supplementary evidence.\n");put(supplement,external);
          auto cross=request;auto rootText=cross.sources.value(root);rootText.insert(rootText.indexOf("\\end{document}"),"\\input{"+supplement+"}\n");cross.sources[root]=rootText;cross.sources[supplement]=QString::fromUtf8(external);
          const auto reviewed=compileAnnotations(cross,std::make_shared<std::atomic_bool>(false));
          put(QFileInfo(report).absolutePath()+"/cross-volume-compiler.txt",(reviewed.error+"\n"+reviewed.log).toUtf8());
          check("cross-volume supplement typesets through isolated copies",reviewed.error.isEmpty()&&!reviewed.pdf.isEmpty()&&contains(reviewed.document,"Cross-volume supplementary"));
          check("cross-volume mapping retains source ownership",reviewed.copyToOriginal.values().contains(supplement)&&bytes(supplement)==external&&bytes(root)==originalBytes);
        }

        const auto imageBefore=bytes(graphic);put(graphic,QByteArray("changed independent resource"));
        const auto changedResource=compileAnnotations(request,std::make_shared<std::atomic_bool>(false));
        check("changed binary resources cannot silently alter an old PDF review",!changedResource.error.isEmpty()&&changedResource.pdf.isEmpty());
        put(graphic,imageBefore);
        for(const auto &engineName:{QString("XeLaTeX"),QString("LuaLaTeX")}) {
          auto alternate=request;alternate.engine=TWApp::instance()->getNamedEngine(engineName);
          const auto alternative=compileAnnotations(alternate,std::make_shared<std::atomic_bool>(false));
          put(QFileInfo(report).absolutePath()+"/"+engineName+"-compiler.txt",(alternative.error+"\n"+alternative.log).toUtf8());
          check(engineName+" preserves its engine and typesets Chinese annotations",alternative.error.isEmpty()&&contains(alternative.document,"置信区间"));
        }
        const QString letter=canonical(folder+"/letter-note.tex");
        const QString letterText="\\documentclass[letterpaper]{article}\n\\usepackage[letterpaper]{geometry}\n\\begin{document}\nA short note records the instrumental uncertainty separately from the fitted residual error.\n\\end{document}\n";
        put(letter,letterText.toUtf8());auto *shortWindow=new TeXDocumentWindow(letter);shortWindow->typeset();
        check("short Letter note compiles normally",waitFor([&]{return !shortWindow->isTypesetting()&&!shortWindow->property("researchBuildPreparing").toBool()&&!shortWindow->property("researchBuildFinishing").toBool()&&!shortWindow->property("researchPublishing").toBool();},60000));
        waitFor([&]{return shortWindow->property("researchArticleReady").toBool();},3000);
        check("short note keeps its simple source interface",!shortWindow->property("researchArticleMode").toBool());
        AnnotationRequest shortRequest;shortRequest.root=letter;shortRequest.pdf=previewFile(letter);shortRequest.sources=pdfCompiledSources(shortRequest.pdf);shortRequest.engine=request.engine;
        auto shortNote=makeAnnotation(letter,shortRequest.sources.value(letter),letterText.indexOf("A short"),"instrumental uncertainty",0,{});
        shortNote.id="A01";shortNote.opinion="Explain how the instrumental uncertainty was calibrated.";shortRequest.notes={shortNote};
        const auto shortReview=compileAnnotations(shortRequest,std::make_shared<std::atomic_bool>(false));
        put(QFileInfo(report).absolutePath()+"/letter-review-compiler.txt",(shortReview.error+"\n"+shortReview.log).toUtf8());
        auto shortPage=shortReview.document?shortReview.document->page(0).toStrongRef():QSharedPointer<QtPDF::Backend::Page>();
        check("Letter paper remains Letter after annotation",shortReview.error.isEmpty()&&shortPage&&qAbs(shortPage->pageSizeF().width()-612)<.5&&qAbs(shortPage->pageSizeF().height()-792)<.5);
        auto plainRequest=shortRequest;plainRequest.pdf.clear();plainRequest.sources[letter].remove("\\usepackage[letterpaper]{geometry}\n");
        auto plainOriginal=plainRequest;plainOriginal.notes.clear();
        const auto baselineNote=compileAnnotations(plainOriginal,std::make_shared<std::atomic_bool>(false));
        check("plain note baseline compiles independently",baselineNote.error.isEmpty()&&baselineNote.document);
        const auto annotatedNote=compileAnnotations(plainRequest,std::make_shared<std::atomic_bool>(false));
        const auto beforePage=baselineNote.document?baselineNote.document->page(0).toStrongRef():QSharedPointer<QtPDF::Backend::Page>();
        const auto afterPage=annotatedNote.document?annotatedNote.document->page(0).toStrongRef():QSharedPointer<QtPDF::Backend::Page>();
        check("helper packages do not reset the physical paper of a plain note",baselineNote.error.isEmpty()&&annotatedNote.error.isEmpty()&&beforePage&&afterPage&&beforePage->pageSizeF()==afterPage->pageSizeF());
        shortWindow->setModified(false);shortWindow->close();settle(80);
      }
    }
    const bool record=qEnvironmentVariableIsSet("TEXLINE_ANNOTATION_RECORD");
    Recording recording;Recording *rec=nullptr;
    if(record){recording.directory=QFileInfo(report).absolutePath()+"/actual-flow";QDir().mkpath(recording.directory);recording.windows=sideBySide?QList<QWidget*>{source,viewer}:QList<QWidget*>{viewer};recording.canvas=QRect((sideBySide?source:static_cast<QWidget*>(viewer))->mapToGlobal(QPoint()),QSize(sideBySide?1205:viewer->width(),height));recording.pointer=recording.canvas.topLeft()+QPoint(50,80);recording.elapsed.start();recording.capture();recording.timer.start();rec=&recording;}
    auto search=viewer->widget()->document().toStrongRef()->search("measured correlation",QtPDF::Backend::Search_CaseInsensitive);
    check("actual rendered selection target exists",!search.isEmpty());
    if(!search.isEmpty()) {
      const auto hit=search.first();viewer->widget()->goToPage(hit.pageNum);settle(100);
      const auto first=viewportPoint(viewer,hit.pageNum,QPointF(hit.bbox.left()+1,hit.bbox.center().y()));
      const auto last=viewportPoint(viewer,hit.pageNum,QPointF(hit.bbox.right()-1,hit.bbox.center().y()));
      dragSelection(viewer->widget()->viewport(),first,last,rec);
      check("ordinary selection retains selected text and shows only the pill",!viewer->widget()->selectedText().trimmed().isEmpty()&&pill->isVisible()&&!viewer->findChild<QFrame*>("researchAnnotationEditor"));
      if(rec){recording.action("Open Annotation beside the selection");recording.click(pill);}else pill->click();
      settle(90);auto *input=viewer->findChild<QPlainTextEdit*>("researchAnnotationText");
      auto *save=viewer->findChild<QToolButton*>("researchAnnotationSave");
      check("one compact editor opens only on request",input&&input->isVisible()&&save&&!save->isEnabled());
      check("new annotation has no empty More button",viewer->findChild<QToolButton*>("researchAnnotationMore")&&!viewer->findChild<QToolButton*>("researchAnnotationMore")->isVisible());
      if(input&&save) {
        input->setPlainText("请补充 $D$ 的置信区间，并说明曝光时间对估计的影响。");settle(120);
        QSet<int> widths;QTimer monitor;monitor.setInterval(10);bool separated=true;
        QObject::connect(&monitor,&QTimer::timeout,&app,[&]{if(switcher->isVisible()) {
          widths.insert(switcher->width());QList<QRect> boxes;
          for(auto *b:switcher->findChildren<QToolButton*>())if(b->isVisible()) {
            const QRect box(b->mapTo(switcher,QPoint()),b->size());for(const auto &other:boxes)separated&=!box.intersects(other);boxes.append(box);
          }
        }});monitor.start();
        if(rec){recording.action("Save the author-specified opinion");recording.fastFor(500);recording.click(save);}else save->click();
        settle(220);monitor.stop();check("review switch appears after the first saved annotation",switcher->isVisible()&&switcher->width()>50);
        check("the review control really paints through intermediate widths",!motionEnabled()||widths.size()>2);
        check("button captions never overlap during the actual reveal",separated);
        bool contained=true;
        for(auto *button:switcher->findChildren<QToolButton*>())if(button->isVisible())contained&=switcher->rect().contains(button->geometry());
        check("review captions fit inside their reserved toolbar space",contained);
        QJsonArray controls;QList<QRect> occupied;bool disjoint=true;
        for(auto *button:viewer->findChildren<QToolButton*>())if(button->isVisible()) {
          const auto at=button->mapTo(viewer,QPoint());if(at.y()>80||at.y()<25)continue;
          const QRect box(at,button->size());
          for(const auto &other:occupied)if(box.intersects(other))disjoint=false;
          occupied.append(box);
          controls.append(QJsonObject{{"object",button->objectName()},{"text",button->text()},{"x",box.x()},{"y",box.y()},{"w",box.width()},{"h",box.height()},{"icon",!button->icon().isNull()}});
        }
        put(QFileInfo(report).absolutePath()+"/toolbar-controls.json",QJsonDocument(controls).toJson());
        check("annotation toolbar controls do not overlap their neighbours",disjoint);
      }
    }
    const auto store=annotationStorePath(root);
    auto saved=QJsonDocument::fromJson(bytes(store)).object().value("notes").toArray();
    check("confirmed opinion is durably separate from the source",saved.size()==1&&saved.first().toObject().value("opinion").toString().contains("置信"));
    const auto savedNote=saved.isEmpty()?Annotation():annotationFromJson(saved.first().toObject());
    auto storedNotes=[&]{return QJsonDocument::fromJson(bytes(store)).object().value("notes").toArray();};
    auto *undoOpinion=viewer->findChild<QAction*>("researchAnnotationUndo"),*redoOpinion=viewer->findChild<QAction*>("researchAnnotationRedo");
    if(undoOpinion&&redoOpinion){{QLockFile held(store+QStringLiteral(".lock"));held.setStaleLockTime(10000);if(held.tryLock(0)){undoOpinion->trigger();check("failed undo keeps persisted notes and history position",storedNotes()==saved&&!redoOpinion->isEnabled());}}undoOpinion->trigger();settle(90);check("new annotation has a complete undo",storedNotes().isEmpty());redoOpinion->trigger();settle(90);check("new annotation redo restores its exact record",storedNotes()==saved);}

    check("actual text selection stores exact line boxes outside the source",!savedNote.geometryHash.isEmpty()&&!savedNote.selectionRects.isEmpty()&&!savedNote.quote.isEmpty());
    AnnotationMark *marker=nullptr;for(auto *item:viewer->widget()->scene()->items())if(auto *mark=dynamic_cast<AnnotationMark*>(item)){marker=mark;break;}
    if(marker) {
      const auto point=viewer->widget()->mapFromScene(marker->mapToScene(marker->boundingRect().center()));
      if(rec){recording.action("Reopen the opinion by its dashed underline");recording.aim(viewer->widget()->viewport(),point);}
      dragSelection(viewer->widget()->viewport(),point,point);settle(60);
    }
    auto *existingEditor=viewer->findChild<QPlainTextEdit*>("researchAnnotationText");
    check("the original PDF annotation marker opens its opinion",marker&&existingEditor&&existingEditor->isVisible());
    if(existingEditor){
      const auto originalOpinion=existingEditor->toPlainText();existingEditor->moveCursor(QTextCursor::End);type(existingEditor," Extra evidence.",1);key(existingEditor,Qt::Key_Z,Qt::ControlModifier);check("opinion input owns text undo without removing its annotation",storedNotes().size()==1&&existingEditor->toPlainText()!=originalOpinion+" Extra evidence.");
      existingEditor->setPlainText(originalOpinion+" Revised evidence.");viewer->findChild<QToolButton*>("researchAnnotationSave")->click();settle(90);
      check("annotation opinion modification is committed",storedNotes().first().toObject().value("opinion").toString().endsWith("Revised evidence."));undoOpinion->trigger();settle(90);check("modification undo restores original opinion",storedNotes()==saved);redoOpinion->trigger();settle(90);check("modification redo restores revised opinion",storedNotes().first().toObject().value("opinion").toString().endsWith("Revised evidence."));undoOpinion->trigger();settle(90);
    }
    auto *reviewButton=viewer->findChild<QToolButton*>("researchAnnotationReview");
    auto *originalButton=viewer->findChild<QToolButton*>("researchAnnotationOriginal");
    check("Original and Annotations are equal-sized plain buttons",reviewButton&&originalButton&&reviewButton->size()==originalButton->size()&&!reviewButton->menu()&&reviewButton->text()==(englishUi()?QString("Annotations"):QString("批注")));
    auto *management=viewer->findChild<QMenu*>("researchAnnotationMenu");QMetaObject::invokeMethod(management,"aboutToShow");
    check("annotation management has no redundant refresh command",management&&!management->findChild<QAction*>("researchAnnotationUpdate"));
    check("annotation management is reachable in focus mode",viewer->findChild<QMenu*>("researchFocusMenu")->actions().contains(management->menuAction()));
    auto returnOriginal=[&]{auto *original=viewer->findChild<QToolButton*>("researchAnnotationOriginal");(original->isVisible()?original:viewer->findChild<QToolButton*>("researchAnnotationReview"))->click();};
    if(saved.size()==1&&reviewButton) {
      const auto reading=viewer->widget()->mapToScene(viewer->widget()->viewport()->rect().center());
      const auto readingScroll=QPoint(viewer->widget()->horizontalScrollBar()->value(),viewer->widget()->verticalScrollBar()->value());
      const auto readingViewport=viewer->widget()->viewport()->size();
      const auto readingZoom=viewer->widget()->zoomLevel();
      if(rec){recording.action("Typeset and view the annotated A4 document");recording.click(reviewButton);}else reviewButton->click();
      const auto stableSize=reviewButton->size();
      check("typesetting does not change the button caption or size",reviewButton->text()==(englishUi()?QString("Annotations"):QString("批注"))&&stableSize==originalButton->size());
      check("review compiles asynchronously and becomes visible",waitFor([&]{return viewer->property("researchAnnotationReview").toBool()||!viewer->property("researchAnnotationBusy").toBool();},60000)&&viewer->property("researchAnnotationReview").toBool());
      check("review rendering contains a real LaTeX annotation",contains(viewer->widget()->document().toStrongRef(),"A01")&&contains(viewer->widget()->document().toStrongRef(),"置信区间"));
      const auto firstRaster=viewer->widget()->viewport()->grab().toImage().convertToFormat(QImage::Format_RGB32);
      int ink=0;for(int y=0;y<firstRaster.height();++y)for(int x=0;x<firstRaster.width();++x){const auto p=firstRaster.pixel(x,y);if(qRed(p)<180&&qGreen(p)<180&&qBlue(p)<180)++ink;}
      check("the first displayed review raster is not a blank loading page",ink>500);
      int reviewMarks=0;for(auto *item:viewer->widget()->scene()->items())reviewMarks+=dynamic_cast<AnnotationMark*>(item)!=nullptr;
      check("the typeset review retains clickable annotation locations",reviewMarks>0);
      check("printing/export identity follows the displayed review",annotationDisplayedFile(viewer)!=pdf&&QFileInfo::exists(annotationDisplayedFile(viewer)));
      check("normal file identity stays bound to the original",viewer->fileName()==pdf);
      viewer->grab().save(QFileInfo(report).absolutePath()+"/actual-review.png");
      if(rec)settle(1300);
      if(rec){recording.action("Return to the original at its previous reading position");recording.click(originalButton->isVisible()?originalButton:reviewButton);}else returnOriginal();
      settle(150);
      const auto returned=viewer->widget()->mapToScene(viewer->widget()->viewport()->rect().center());
      put(QFileInfo(report).absolutePath()+"/reading-position.json",QJsonDocument(QJsonObject{
        {"before",QJsonArray{reading.x(),reading.y()}},{"after",QJsonArray{returned.x(),returned.y()}},
        {"beforeScroll",QJsonArray{readingScroll.x(),readingScroll.y()}},
        {"afterScroll",QJsonArray{viewer->widget()->horizontalScrollBar()->value(),viewer->widget()->verticalScrollBar()->value()}},
        {"beforeViewport",QJsonArray{readingViewport.width(),readingViewport.height()}},
        {"afterViewport",QJsonArray{viewer->widget()->viewport()->width(),viewer->widget()->viewport()->height()}},
        {"beforeZoom",readingZoom},{"afterZoom",viewer->widget()->zoomLevel()}}).toJson());
      check("returning to original restores reading position",!viewer->property("researchAnnotationReview").toBool()&&(viewer->widget()->mapToScene(viewer->widget()->viewport()->rect().center())-reading).manhattanLength()<3);
      check("original PDF still has no review helper text",!contains(viewer->widget()->document().toStrongRef(),"A01"));
      for(int i=0;i<8;++i){reviewButton->click();returnOriginal();}
      settle(100);check("rapid reverse switching finishes on original",!viewer->property("researchAnnotationReview").toBool()&&contains(viewer->widget()->document().toStrongRef(),"Interpretation"));
      check("review operations preserve source cursor and undo history",source->textCursor().position()==cursor&&source->textDoc()->availableUndoSteps()==undo);
      // Keep an intentionally unfinished source edit while annotating the last
      // successful PDF. Neither source cursor nor undo history may be replaced.
      QTextCursor edit(source->textDoc());edit.movePosition(QTextCursor::End);edit.insertText("\n{unfinished draft\n");
      const auto dirty=source->textDoc()->editorText();
      reviewButton->click();check("dirty draft does not block the previous PDF review",waitFor([&]{return viewer->property("researchAnnotationReview").toBool();},1500));
      returnOriginal();check("reviewing does not modify dirty source",source->textDoc()->editorText()==dirty);
      source->textDoc()->undo();
      viewer->resize(440,qMin(690,height));settle(120);reviewButton->click();viewer->widget()->zoomFitWidth();settle(100);
      check("half-width PDF remains visible with one toolbar row",viewer->widget()->viewport()->width()>300&&switcher->isVisible()&&reviewButton->geometry().height()<=40);
      check("the compact review caption stays within its toolbar space",switcher->rect().contains(reviewButton->geometry()));
      viewer->grab().save(QFileInfo(report).absolutePath()+"/half-review.png");reviewButton->click();
    }
    if(rec){recording.finish();check("actual widget recording completes",!recording.failures);}
    viewer->resize(qMin(640,available.width()-40),height);viewer->widget()->zoomFitWidth();settle(100);
    setFocusMode(true);settle(180);
    auto focusToggle=[&] {
      auto *menu=viewer->findChild<QMenu*>("researchAnnotationMenu");QMetaObject::invokeMethod(menu,"aboutToShow");
      for(auto *action:menu->actions())if(action->text()==words("查看批注","View annotations")||action->text()==words("返回原稿","Return to original")){action->trigger();return true;}
      return false;
    };
    const bool focusedReview=focusToggle()&&waitFor([&]{return viewer->property("researchAnnotationReview").toBool();},3000);
    check("focus mode can view annotations without the hidden toolbar",viewer->property("researchFocus").toBool()&&focusedReview);
    check("focus mode returns to the original through its one menu entry",focusToggle()&&!viewer->property("researchAnnotationReview").toBool());
    setFocusMode(false);settle(180);
    check("leaving focus retains equal annotation button metrics",reviewButton->size()==viewer->findChild<QToolButton*>("researchAnnotationOriginal")->size());
    // Blank clicks and Escape never create a confirmed annotation.
    const auto countNotes=[&]{return QJsonDocument::fromJson(bytes(store)).object().value("notes").toArray();};
    const int beforeEmpty=countNotes().size();
    dragSelection(viewer->widget()->viewport(),QPoint(24,50),QPoint(24,50));
    viewer->findChild<QAction*>("researchAnnotateSelection")->trigger();settle(70);
    check("empty selection does not open an annotation editor",!viewer->findChild<QFrame*>("researchAnnotationEditor")&&countNotes().size()==beforeEmpty);
    // A region over a real rendered equation is mapped into its included source.
    TWSyncTeXSynchronizer originalSync(pdf,{}, {},QFileInfo(root).absolutePath());
    const int equationLine=body.left(body.indexOf("D=\\frac")).count('\n')+1;
    const auto equation=originalSync.syncFromTeX({child,equationLine,0,-1},TWSynchronizer::LineResolution);
    check("included equation has a real PDF target",equation.page>0&&!equation.rects.isEmpty());
    if(equation.page>0&&!equation.rects.isEmpty()) {
      viewer->widget()->goToPage(equation.page-1);settle(70);
      const auto box=equation.rects.first();
      const auto first=viewportPoint(viewer,int(equation.page-1),box.topLeft()-QPointF(2,3));
      const auto last=viewportPoint(viewer,int(equation.page-1),box.bottomRight()+QPointF(2,3));
      auto *region=viewer->findChild<QAction*>("researchAnnotateRegion");
      region->setChecked(true);key(viewer->widget()->viewport(),Qt::Key_Escape);
      check("Escape cancels region annotation",!region->isChecked()&&countNotes().size()==beforeEmpty);
      region->setChecked(true);dragSelection(viewer->widget()->viewport(),first,last);
      auto *input=viewer->findChild<QPlainTextEdit*>("researchAnnotationText");
      auto *save=viewer->findChild<QToolButton*>("researchAnnotationSave");
      check("region opens a compact single editor",input&&input->isVisible()&&input->window()->height()<=240);
      if(input&&save) {
        input->setPlainText("State the exposure time used in the $D$ estimate.");save->click();settle(220);
        const auto rows=countNotes();
        check("equation opinion retains the included file anchor",rows.size()==beforeEmpty+1&&rows.last().toObject().value("file").toString()==child&&rows.last().toObject().value("anchor").toString().contains("\\begin{equation}"));
        reviewButton->click();returnOriginal();reviewButton->click();
        const auto during=source->textDoc()->editorText();const bool pendingBuild=viewer->property("researchAnnotationBusy").toBool();
        key(source->editor(),Qt::Key_X,Qt::NoModifier,QString("x"));
        check("source input remains live during annotation typesetting",pendingBuild&&viewer->property("researchAnnotationBusy").toBool()&&source->textDoc()->editorText().size()==during.size()+1);
        source->textDoc()->undo();check("typing during review compilation keeps ordinary source undo",source->textDoc()->editorText()==during);
        check("reversing during background compilation keeps the final requested view",waitFor([&]{return viewer->property("researchAnnotationReview").toBool()||!viewer->property("researchAnnotationBusy").toBool();},60000)&&viewer->property("researchAnnotationReview").toBool());
      }
    }
    // Automate only this process's Qt file dialog, not the user's native dialog.
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs,true);
    auto saveThroughDialog=[&](const char *actionName,const QString &destination) {
      auto *menu=viewer->findChild<QMenu*>("researchAnnotationMenu");QMetaObject::invokeMethod(menu,"aboutToShow");
      auto *action=menu->findChild<QAction*>(actionName);if(!action)return false;
      bool seen=false;QTimer timer;timer.setInterval(20);
      QObject::connect(&timer,&QTimer::timeout,&app,[&] {
        for(auto *widget:QApplication::topLevelWidgets())if(auto *dialog=qobject_cast<QFileDialog*>(widget))if(dialog->isVisible()) {
          seen=true;timer.stop();dialog->selectFile(destination);QMetaObject::invokeMethod(dialog,"accept",Qt::QueuedConnection);return;
        }
      });
      timer.start();action->trigger();const bool saved=waitFor([&]{return QFileInfo::exists(destination);},60000);timer.stop();return saved&&seen;
    };
    check("actual export action saves the annotated PDF",saveThroughDialog("researchAnnotationExportPdf",QFileInfo(report).absolutePath()+"/export-through-ui.pdf"));
    check("exported review contains all confirmed opinions",contains(QtPDF::Backend::Document::newDocument(QFileInfo(report).absolutePath()+"/export-through-ui.pdf"),"A02"));
    check("author opinions can be exported for AI collaboration",saveThroughDialog("researchAnnotationExportOpinions",QFileInfo(report).absolutePath()+"/author-opinions.md")&&QString::fromUtf8(bytes(QFileInfo(report).absolutePath()+"/author-opinions.md")).contains("sections/measurement.tex"));
    AnnotationMark *reviewMark=nullptr;
    for(auto *item:viewer->widget()->scene()->items())if(auto *mark=dynamic_cast<AnnotationMark*>(item)){reviewMark=mark;break;}
    if(reviewMark) {
      viewer->widget()->ensureVisible(reviewMark);settle(50);
      const auto point=viewer->widget()->mapFromScene(reviewMark->mapToScene(reviewMark->boundingRect().center()));
      dragSelection(viewer->widget()->viewport(),point,point);settle(200);
    }
    auto *reviewEditor=viewer->findChild<QPlainTextEdit*>("researchAnnotationText");
    put(QFileInfo(report).absolutePath()+"/review-click.json",QJsonDocument(QJsonObject{{"markExisted",reviewMark!=nullptr},{"editorExists",reviewEditor!=nullptr},{"editorVisible",reviewEditor&&reviewEditor->isVisible()},{"review",viewer->property("researchAnnotationReview").toBool()}}).toJson());
    check("clicking a typeset underline safely returns to its original opinion",reviewMark&&reviewEditor&&reviewEditor->isVisible()&&!viewer->property("researchAnnotationReview").toBool());
    if(reviewEditor){key(reviewEditor,Qt::Key_Escape);settle(60);}
    if(viewer->property("researchAnnotationReview").toBool())returnOriginal();
    // Reopening restores opinions, never helper source or a review-only page.
    QPointer<PDFDocumentWindow> closed=viewer;viewer->close();check("closing the review window releases it safely",waitFor([&]{return closed.isNull();},3000));
    viewer=new PDFDocumentWindow(pdf,source);viewer->setGeometry(available.left()+20,available.top()+12,qMin(640,available.width()-40),height);viewer->show();settle(120);
    reviewButton=viewer->findChild<QToolButton*>("researchAnnotationReview");
    switcher=viewer->findChild<QWidget*>("researchAnnotationSwitch");
    check("annotations survive reopening without changing the original view",switcher->isVisible()&&!viewer->property("researchAnnotationReview").toBool()&&countNotes().size()>=2);
    // Delete every opinion through the real editor menu, then undo the last.
    while(!countNotes().isEmpty()) {
      auto *menu=viewer->findChild<QMenu*>("researchAnnotationMenu");QMetaObject::invokeMethod(menu,"aboutToShow");
      QAction *entry=nullptr;for(auto *a:menu->actions())if(a->text().startsWith("[A")){entry=a;break;}
      if(!entry)break;entry->trigger();settle(80);
      auto *erase=viewer->findChild<QAction*>("researchAnnotationDelete");
      if(!erase)break;erase->trigger();settle(180);
    }
    const QJsonObject deletionBefore{{"notes",countNotes().size()},{"visible",switcher->isVisible()},{"width",switcher->width()}};
    QElapsedTimer deletionClock;deletionClock.start();
    const bool deletionHidden=waitFor([&]{return !switcher->isVisible();},1000);
    put(QFileInfo(report).absolutePath()+"/delete-animation.json",QJsonDocument(QJsonObject{{"afterFixedWait",deletionBefore},{"additionalWaitMs",deletionClock.elapsed()},{"hidden",deletionHidden},{"width",switcher->width()}}).toJson());
    check("deleting the final opinion removes the review controls",countNotes().isEmpty()&&deletionHidden);
    undoOpinion=viewer->findChild<QAction*>("researchAnnotationUndo");redoOpinion=viewer->findChild<QAction*>("researchAnnotationRedo");
    undoOpinion->trigger();settle(150);check("annotation deletion undo restores the last exact note",countNotes().size()==1&&switcher->isVisible());redoOpinion->trigger();settle(180);check("annotation deletion redo removes the same note",countNotes().isEmpty());

    if(equation.page>0&&!equation.rects.isEmpty()) {
      viewer->widget()->zoomFitWidth();viewer->widget()->goToPage(equation.page-1);settle(60);
      auto *region=viewer->findChild<QAction*>("researchAnnotateRegion");region->setChecked(true);
      dragSelection(viewer->widget()->viewport(),viewportPoint(viewer,int(equation.page-1),equation.rects.first().topLeft()-QPointF(2,3)),viewportPoint(viewer,int(equation.page-1),equation.rects.first().bottomRight()+QPointF(2,3)));
      auto *input=viewer->findChild<QPlainTextEdit*>("researchAnnotationText");auto *save=viewer->findChild<QToolButton*>("researchAnnotationSave");
      if(input&&save){input->setPlainText("Explain the measurement resolution.");save->click();settle(180);}
    }
    check("deleted annotation identifiers are never reused",countNotes().size()==1&&countNotes().first().toObject().value("id").toString()=="A03");
    auto *more=viewer->findChild<QMenu*>("researchAnnotationMenu");QMetaObject::invokeMethod(more,"aboutToShow");
    undoOpinion->trigger();settle(120);check("undo after a new annotation reverses the newest creation",countNotes().isEmpty());redoOpinion->trigger();settle(120);check("redo restores the new identifier without reusing a deletion",countNotes().size()==1&&countNotes().first().toObject().value("id").toString()=="A03");
    {
      auto *menu=viewer->findChild<QMenu*>("researchAnnotationMenu");QMetaObject::invokeMethod(menu,"aboutToShow");
      for(auto *action:menu->actions())if(action->text().startsWith("[A03]")){action->trigger();break;}
      settle(60);auto *move=viewer->findChild<QAction*>("researchAnnotationRelocate");if(move)move->trigger();settle(60);
      const auto targets=viewer->widget()->document().toStrongRef()->search("measured correlation",QtPDF::Backend::Search_CaseInsensitive);
      if(!targets.isEmpty()) {
        const auto target=targets.first();viewer->widget()->goToPage(target.pageNum);settle(60);
        dragSelection(viewer->widget()->viewport(),viewportPoint(viewer,int(target.pageNum),target.bbox.topLeft()-QPointF(2,3)),viewportPoint(viewer,int(target.pageNum),target.bbox.bottomRight()+QPointF(2,3)));
        if(auto *save=viewer->findChild<QToolButton*>("researchAnnotationSave"))save->click();settle(180);
      }
      bool relocated=false;for(const auto &row:countNotes()){const auto n=row.toObject();if(n.value("id").toString()=="A03")relocated=n.value("file").toString()==root&&n.value("anchor").toString().contains("The measured");}
      check("reselecting a location preserves the opinion identifier",relocated&&countNotes().size()==1);
      const auto relocatedNotes=countNotes();undoOpinion->trigger();settle(100);check("relocation undo restores the earlier source anchor",countNotes().size()==1&&countNotes().first().toObject().value("file").toString()!=root);redoOpinion->trigger();settle(100);check("relocation redo restores the exact new geometry",countNotes()==relocatedNotes);

      reviewButton->click();
      const bool backgroundPending=viewer->property("researchAnnotationBusy").toBool();
      dragSelection(viewer->widget()->viewport(),QPoint(24,40),QPoint(24,40));
      waitFor([&]{return !viewer->property("researchAnnotationBusy").toBool();},60000);
      check("resuming PDF interaction prevents a late automatic view switch",backgroundPending&&!viewer->property("researchAnnotationReview").toBool());
      reviewButton->click();check("a prepared review remains available on explicit request",viewer->property("researchAnnotationReview").toBool());
    }
    if(!viewer->findChild<QToolButton*>("researchAnnotationOriginal")->isVisible()) {
      returnOriginal();auto *menu=viewer->findChild<QMenu*>("researchAnnotationMenu");QMetaObject::invokeMethod(menu,"aboutToShow");
      for(auto *action:menu->actions())if(action->text().startsWith("[A03]")){action->trigger();break;}
      settle(60);if(auto *input=viewer->findChild<QPlainTextEdit*>("researchAnnotationText")){input->appendPlainText("Check the fitted interval.");viewer->findChild<QToolButton*>("researchAnnotationSave")->click();}
      settle(60);reviewButton->click();
      const bool background=viewer->property("researchAnnotationBusy").toBool();
      reviewButton->click();waitFor([&]{return !viewer->property("researchAnnotationBusy").toBool();},60000);
      check("the visible compact control reverses a pending view request",background&&!viewer->property("researchAnnotationReview").toBool());
      reviewButton->click();
    }
    bool cleanEditors=true;for(auto *w:TeXDocumentWindow::documentList())cleanEditors&=!w->textDoc()->editorText().contains("\\TeXlineReviewNote");
    check("temporary helpers never open in any source editor",cleanEditors);
    check("disk source and compiled original remain byte-identical",bytes(root)==originalBytes&&bytes(child)==childBytes&&bytes(pdf)==pdfBytes);
    check("source contents and undo count remain unchanged",source->textDoc()->editorText()==originalEditor&&source->textDoc()->availableUndoSteps()==undo);
    if(viewer->property("researchAnnotationReview").toBool())returnOriginal();
    viewer->grab().save(QFileInfo(report).absolutePath()+"/actual-original.png");
    // Historical metadata with an obsolete anchor must not advertise an empty
    // review document. Retain its opinion for deliberate relocation/deletion.
    auto reopen=[&]{QPointer<PDFDocumentWindow> old=viewer;viewer->close();check("private PDF lifecycle closes",waitFor([&]{return old.isNull();},3000));viewer=new PDFDocumentWindow(pdf,source);viewer->resize(714,760);viewer->show();settle(260);};
    auto orphan=savedNote;orphan.id="A01";orphan.anchor="An old paragraph that was deleted years ago.";orphan.sourceHash="historic";orphan.opinion="Preserve this old opinion until deliberately deleted.";
    auto whitespace=orphan;whitespace.id="A02";whitespace.opinion="  \n ";
    auto writeNotes=[&](const QJsonArray &notes){auto object=QJsonDocument::fromJson(bytes(store)).object();object["notes"]=notes;object["draft"]=QJsonObject();return put(store,QJsonDocument(object).toJson());};
    check("independent historical annotation fixture written",writeNotes({annotationJson(orphan),annotationJson(whitespace)}));reopen();
    switcher=viewer->findChild<QWidget *>("researchAnnotationSwitch");reviewButton=viewer->findChild<QToolButton *>("researchAnnotationReview");
    check("orphan and blank opinions show no review control on reopening",!switcher->isVisible());
    viewer->grab().save(QFileInfo(report).absolutePath()+"/orphan-hidden.png");
    reviewButton->click();settle(180);check("an empty review request neither compiles nor opens a stale warning",!viewer->property("researchAnnotationBusy").toBool()&&!viewer->property("researchAnnotationReview").toBool()&&!viewer->statusBar()->currentMessage().contains("A01"));
    auto *orphanMenu=viewer->findChild<QMenu *>("researchAnnotationMenu");QMetaObject::invokeMethod(orphanMenu,"aboutToShow");QAction *orphanEntry=nullptr;for(auto *action:orphanMenu->actions())if(action->text().startsWith("[A01]"))orphanEntry=action;
    check("unlocated opinion remains manually recoverable",orphanEntry&&orphanEntry->text().contains(words("待重新定位","Relocate")));
    if(orphanEntry){orphanEntry->trigger();settle(100);auto *erase=viewer->findChild<QAction *>("researchAnnotationDelete");if(erase)erase->trigger();settle(220);}
    check("deleting an unlocated opinion clears its persistent record",countNotes().isEmpty());reopen();check("deleted historical opinions stay absent after a second reopening",!viewer->findChild<QWidget *>("researchAnnotationSwitch")->isVisible()&&countNotes().isEmpty());
    check("mixed valid and obsolete fixture written",writeNotes({annotationJson(orphan),[&]{auto valid=savedNote;valid.id="A03";return annotationJson(valid);}()}));reopen();
    reviewButton=viewer->findChild<QToolButton *>("researchAnnotationReview");check("a valid opinion still reveals review controls",viewer->findChild<QWidget *>("researchAnnotationSwitch")->isVisible());reviewButton->click();
    check("obsolete history cannot block typesetting a valid opinion",waitFor([&]{return !viewer->property("researchAnnotationBusy").toBool();},60000)&&viewer->property("researchAnnotationReview").toBool());
    if(viewer->property("researchAnnotationReview").toBool())returnOriginal();
    check("peer deletion is written",writeNotes({}));QMetaObject::invokeMethod(viewer,"reloaded");settle(240);
    check("same-window reload observes external deletion without resurrection",!viewer->findChild<QWidget *>("researchAnnotationSwitch")->isVisible()&&!viewer->findChild<QAction *>("researchAnnotationUndo")->isEnabled()&&countNotes().isEmpty());
    for(auto *w:TeXDocumentWindow::documentList())w->setModified(false);
    saveReport();app.exit(failures?1:0);
  });
  return app.exec();
}
