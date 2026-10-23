#include "ActualAuthoringRecording.h"
#include "ResearchFind.h"
#include "ResearchSlides.h"
#include "Engine.h"
using namespace Research;
int main(int argc,char **argv){
 const auto report=argument(1);if(!qEnvironmentVariable("TEXLINE_TEST_DESKTOP").startsWith("TeXlineValidation-"))return 2;
 argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
 QTimer::singleShot(80,&app,[&]{
  const auto folder=QFileInfo(report).absolutePath();const bool baseline=qEnvironmentVariableIsSet("TEXLINE_PREVIEW_BASELINE");QJsonArray checks,timings;int failures=0;
  auto check=[&](QString n,bool ok){checks.append(QJsonObject{{"name",n},{"passed",ok}});put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures+=!ok},{"timingsMs",timings},{"devicePixelRatio",qApp->primaryScreen()->devicePixelRatio()}}).toJson());};
  const auto file=folder+"/paper/main.tex";const QString source="\\documentclass[aspectratio=169]{beamer}\n\\usetheme{default}\n\\title{From observation to proof}\n\\begin{document}\n\\section{Results}\n\\begin{frame}{A bound worth understanding}\n\\large\nFor independent observations $X_1,\\ldots,X_n$,\\[\\Pr\\!\\left(\\left|\\bar X-\\mu\\right|>t\\right)\\leq 2\\exp\\!\\left(-\\frac{nt^2}{2\\sigma^2}\\right).\\]\n\\medskip\nThe argument separates concentration from approximation.\n\\end{frame}\n\\begin{frame}{Proof outline}\n\\begin{enumerate}\\item Center the observations.\\item Control the exponential moment.\\item Optimize the bound.\\end{enumerate}\n\\end{frame}\n\\end{document}\n";
  put(file,source.toUtf8());Tw::Settings().setValue("openPDFwithTeX",false);
  auto *w=new TeXDocumentWindow(file);w->resize(1180,760);w->show();settle(500);QApplication::setActiveWindow(w);
  auto *list=w->findChild<QListWidget*>("researchSlideList");check("slide list loads",waitFor([&]{list=w->findChild<QListWidget*>("researchSlideList");return list&&list->count()==2;}));
  app.setEngineList({Engine("pdfLaTeX","C:/texlive/2023/bin/windows/pdflatex.exe",{"-synctex=1","-interaction=nonstopmode","-file-line-error","$fullname"},true)});
  w->typeset();check("actual sample compiles",waitFor([&]{return !w->isTypesetting()&&!w->property("researchBuildPreparing").toBool()&&!w->property("researchBuildFinishing").toBool()&&!w->property("researchPublishing").toBool();},90000)&&pdfPairValid(previewFile(file)));settle(800);
  auto *dock=w->findChild<QDockWidget*>("researchSlideDock"),*pdf=w->findChild<QDockWidget*>("researchSlidePreviewDock");
  if(!dock||!pdf){check("both panes exist",false);app.exit(1);return;}
  auto *editor=w->editor();editor->setFocus();QTextCursor cursor(w->textDoc());cursor.setPosition(source.indexOf("For independent"));cursor.setPosition(cursor.position()+21,QTextCursor::KeepAnchor);editor->setTextCursor(cursor);
  const int pdfTop=pdf->mapTo(w,QPoint()).y();showFind(w);settle();auto *bar=w->findChild<QToolBar*>("researchFindBar");auto *query=w->findChild<QLineEdit*>("researchFindField");
  w->grab().save(folder+"/source-find.png");
  if(!baseline){
   check("search ends at source right edge",bar&&qAbs(bar->mapTo(w,QPoint(bar->width(),0)).x()-w->centralWidget()->mapTo(w,QPoint(w->centralWidget()->width(),0)).x())<=1);
   check("search does not move preview top",pdf->mapTo(w,QPoint()).y()==pdfTop);
   check("search controls remain inside source",query&&query->isVisible()&&bar->rect().contains(query->mapTo(bar,QPoint(query->width()-1,query->height()-1))));
   check("search close control remains visible",w->findChild<QToolButton*>("researchFindClose")->visibleRegion().boundingRect().width()>10);
  }
  key(query,Qt::Key_Escape);settle();editor->setTextCursor(cursor);const int scroll=editor->verticalScrollBar()->value();const int width=pdf->width();
  auto *expand=w->findChild<QToolButton*>("researchSlideExpandPreviewButton");
  if(!baseline){
   check("SVG expansion control available",expand&&!expand->icon().isNull());
   if(expand){
    QElapsedTimer timer;timer.start();expand->click();settle(30);timings.append(timer.elapsed());settle(200);
    check("expanded view hides only writing area",w->centralWidget()->isHidden()&&dock->isVisible()&&pdf->isVisible());
    check("preview fills remaining width",pdf->width()>w->width()-dock->width()-32);
    w->grab().save(folder+"/expanded.png");
    expand->click();settle(250);check("collapse restores pane width",qAbs(pdf->width()-width)<=3);
    check("collapse preserves source selection and scroll",editor->textCursor()==cursor&&editor->verticalScrollBar()->value()==scroll);
    check("collapse restores editing focus",editor->hasFocus());
    for(int i=0;i<12;++i){timer.restart();expand->click();QCoreApplication::processEvents();expand->click();QCoreApplication::processEvents();timings.append(timer.elapsed());}
    settle(200);check("rapid toggles do not queue or strand source",!w->centralWidget()->isHidden()&&!w->property("researchSlidePreviewExpanded").toBool());
    expand->click();settle();w->resize(760,700);settle(300);w->grab().save(folder+"/expanded-half.png");
    check("half width keeps list and expanded preview",dock->isVisible()&&pdf->isVisible()&&w->centralWidget()->isHidden()&&w->width()==760);
    list->setCurrentRow(1);settle(250);check("slide navigation stays expanded",w->property("researchSlidePreviewExpanded").toBool()&&list->currentRow()==1);
    check("preview browsing preserves hidden writing selection",editor->textCursor()==cursor);
    auto *view=w->findChild<QtPDF::PDFDocumentView*>("researchSlidePreview");view->setFocus();key(view,Qt::Key_Escape);settle(300);
    check("Escape restores source",!w->centralWidget()->isHidden()&&!w->property("researchSlidePreviewExpanded").toBool());
    w->resize(1180,760);settle(300);pdf->show();expand->click();settle();showFind(w);settle();
    check("source search exits expanded preview",!w->centralWidget()->isHidden()&&query->hasFocus());key(query,Qt::Key_Escape);settle();
    expand->click();settle();setFocusMode(true);settle(350);check("focus mode restores source before entering",!w->centralWidget()->isHidden()&&w->property("researchFocus").toBool());setFocusMode(false);settle(350);
   }
  }
  showFind(w);settle();w->resize(640,700);settle(300);w->grab().save(folder+"/source-find-half.png");
  if(!baseline){check("half window not widened by search",w->width()==640);check("half source query visible",query->isVisible()&&query->visibleRegion().boundingRect().width()>=75);}
  check("source content unchanged",w->textDoc()->editorText()==source);
  if(!baseline){
   key(query,Qt::Key_Escape);settle();
   const auto multi=folder+"/cross-file/main.tex",child=folder+"/cross-file/second.tex";
   put(multi,"\\documentclass{beamer}\n\\begin{document}\n\\begin{frame}{First}First frame\\end{frame}\n\\input{second}\n\\end{document}\n");put(child,"\\begin{frame}{Second}Second frame\\end{frame}\n");
   auto *cross=new TeXDocumentWindow(multi);cross->resize(1180,760);cross->show();QApplication::setActiveWindow(cross);settle(500);
   auto *rows=cross->findChild<QListWidget*>("researchSlideList");check("cross-file deck loads",waitFor([&]{rows=cross->findChild<QListWidget*>("researchSlideList");return rows&&rows->count()==2;}));
   auto *button=cross->findChild<QToolButton*>("researchSlideExpandPreviewButton");
   if(button&&rows&&rows->count()==2){button->click();settle();const auto old=cross->textCursor();rows->setCurrentRow(1);settle(300);
    check("cross-file browsing keeps expanded window",cross->property("researchSlidePreviewExpanded").toBool()&&QApplication::activeWindow()==cross);
    check("cross-file browsing leaves source cursor untouched",cross->textCursor()==old);
    button->click();settle(700);auto *target=TeXDocumentWindow::findDocument(child);
    check("return opens selected child source",target&&target->isVisible()&&!target->centralWidget()->isHidden());
   }
   for(auto *other:TeXDocumentWindow::documentList())if(other!=w){other->textDoc()->setModified(false);other->close();}settle();
  }
  put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"timingsMs",timings},{"baseline",baseline},{"devicePixelRatio",qApp->primaryScreen()->devicePixelRatio()}}).toJson());
  w->textDoc()->setModified(false);w->close();settle();app.exit(failures?1:0);
 });return app.exec();
}
