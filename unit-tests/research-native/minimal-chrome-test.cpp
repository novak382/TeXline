// Exercise the visible replacement controls, not their hidden compatibility buttons.
#include "ActualAuthoringRecording.h"
#include "ResearchSlides.h"
#include "Engine.h"
using namespace Research;

int main(int argc,char **argv) {
  const auto report=argument(1);
  if(!qEnvironmentVariable("TEXLINE_TEST_DESKTOP").startsWith("TeXlineValidation-"))return 2;
  argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
  QTimer::singleShot(80,&app,[&]{
    const auto folder=QFileInfo(report).absolutePath();const bool recording=qEnvironmentVariableIsSet("TEXLINE_DESIGN_RECORD");QJsonArray checks;int failures=0;
    auto check=[&](const QString &name,bool passed){checks.append(QJsonObject{{"name",name},{"passed",passed}});failures+=!passed;put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"devicePixelRatio",qApp->primaryScreen()->devicePixelRatio()},{"physicalInput",false}}).toJson());};
    QTimer watchdog;watchdog.setSingleShot(true);watchdog.setInterval(recording?180000:90000);
    QObject::connect(&watchdog,&QTimer::timeout,&app,[&]{check("fixture completes without an unexpected modal",false);if(auto *popup=QApplication::activePopupWidget())popup->close();if(auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget()))dialog->reject();app.exit(1);});watchdog.start();
    Tw::Settings().setValue("openPDFwithTeX",false);Tw::Settings().setValue("researchFocusMode",false);
    const auto main=folder+"/paper/main.tex",child=folder+"/paper/child.tex";
    const QString source="\\documentclass{article}\n\\newtheorem{theorem}{Theorem}\n\\begin{document}\n\\section{Main result 主要结果}\n\\begin{theorem}\\label{thm:main}A selected statement.\\end{theorem}\n\\section{Discussion 讨论}\nDiscussion text.\n\\input{child}\n\\end{document}\n";
    const QString childSource="% !TEX root = main.tex\n\\section{Child result 子文件结果}\nChild text. See \\ref{thm:main}.\n";
    check("independent multi-file sample written",put(main,source.toUtf8())&&put(child,childSource.toUtf8()));
    auto *w=new TeXDocumentWindow(main);w->resize(qEnvironmentVariableIsSet("TEXLINE_TEST_HALF")?620:1120,760);w->show();QApplication::setActiveWindow(w);w->editor()->setFocus();
    auto ready=[](TeXDocumentWindow *window){return waitFor([&]{return window->property("researchArticleReady").toBool()&&window->property("researchArticleMode").toBool();});};
    check("article and project structure ready",ready(w));
    auto *scope=w->findChild<QToolButton*>("researchArticleScopeButton");auto *browse=w->findChild<QToolButton*>("researchArticleBrowseButton");
    auto *full=w->findChild<QAction*>("researchArticleFullChoice"),*section=w->findChild<QAction*>("researchArticleSectionChoice");
    auto *chapters=w->findChild<QAction*>("researchArticleChaptersChoice"),*objects=w->findChild<QAction*>("researchArticleObjectsChoice");
    auto *outline=w->findChild<QAction*>("researchOutlineAction");auto *dock=w->findChild<QDockWidget*>("researchArticleDock");auto *tree=w->findChild<QTreeWidget*>("researchArticleTree");
    if(!scope||!browse||!full||!section||!chapters||!objects||!outline||!dock||!tree){check("replacement controls exist",false);watchdog.stop();w->close();app.exit(1);return;}
    auto choose=[&](QToolButton *button,QAction *entry,const QString &name){
      if(!button||!button->isVisible()||!button->isEnabled()||!button->menu()){check(name+": visible menu button",false);return;}
      auto *menu=button->menu();bool reached=false;QTimer responder,timeout;responder.setInterval(20);timeout.setSingleShot(true);timeout.setInterval(5000);
      QObject::connect(&timeout,&QTimer::timeout,menu,[&]{menu->close();});
      QObject::connect(&responder,&QTimer::timeout,menu,[&]{if(!menu->isVisible())return;responder.stop();settle(30);menu->grab().save(folder+"/"+name+".png");reached=!entry||(menu->actions().contains(entry)&&entry->isVisible()&&entry->isEnabled());if(entry&&reached){menu->setActiveAction(entry);key(menu,Qt::Key_Return);}else key(menu,Qt::Key_Escape);});
      responder.start();timeout.start();button->showMenu();responder.stop();timeout.stop();settle(100);check(name+": actual menu opens and requested entry is usable",reached);
    };
    auto openOutline=[&]{if(dock->isHidden())outline->trigger();settle(200);};
    auto findSection=[&](const QString &title){QTreeWidgetItemIterator it(tree);while(*it){if((*it)->data(0,Qt::UserRole+1).toInt()==0&&(*it)->text(0).startsWith(title))return *it;++it;}return static_cast<QTreeWidgetItem*>(nullptr);};
    auto objectCount=[&]{int count=0;QTreeWidgetItemIterator it(tree);while(*it){count+=(*it)->data(0,Qt::UserRole+1).toInt()==1;++it;}return count;};
    openOutline();
    check("one visible scope control replaces full and section buttons",scope->isVisible()&&w->findChild<QToolButton*>("researchArticleFull")->isHidden()&&w->findChild<QToolButton*>("researchArticleSection")->isHidden());
    const auto fileCaption=englishUi()?QStringLiteral("File"):traditionalUi()?QStringLiteral("本檔案"):QStringLiteral("本文件");
    const auto fullCaption=englishUi()?QStringLiteral("Edit this file"):traditionalUi()?QStringLiteral("編輯此檔案"):QStringLiteral("编辑本文件");
    check("project full scope is this file, never the whole project",scope->text()==fileCaption&&full->text()==fullCaption);
    check("duplicate filename close and bottom browse controls removed",w->findChild<QLabel*>("researchArticleRoot")->isHidden()&&w->findChild<QToolButton*>("researchArticleClose")->isHidden()&&w->findChild<QToolButton*>("researchArticleObjects")->isHidden());
    check("sidebar header and filter stay inside the allotted third",dock->width()<=w->width()/3&&browse->geometry().right()<dock->width()&&w->findChild<QToolButton*>("researchArticleSearchToggle")->geometry().right()<dock->width()&&w->editor()->viewport()->width()>=w->width()/2);
    w->grab().save(folder+"/article-full.png");
    auto cursor=w->textCursor();const int at=source.indexOf("A selected statement");cursor.setPosition(at+10);cursor.setPosition(at,QTextCursor::KeepAnchor);w->editor()->setTextCursor(cursor);w->editor()->setFocus();settle(50);
    choose(scope,nullptr,"scope-cancel");check("range menu Escape preserves reverse selection and editor focus",w->textCursor()==cursor&&w->editor()->hasFocus()&&!w->editor()->property("researchSourceSlice").toBool());
    choose(scope,section,"scope-section");check("section choice keeps selection in the original document",w->editor()->property("researchSourceSlice").toBool()&&w->textCursor()==cursor&&w->textDoc()->editorText()==source&&scope->toolTip().contains("Main result"));
    check("current section title remains readable in the scope control",scope->text().contains("Main")&&section->isChecked());
    const auto place=articleReadingState(w);w->editor()->insertPlainText("Replacement");w->editor()->undo();check("section edit remains one exact undo",w->textDoc()->editorText()==source);check("article reindex completes",ready(w));
    choose(scope,full,"scope-full");check("full menu reveals source without changing content",!w->editor()->property("researchSourceSlice").toBool()&&w->textDoc()->editorText()==source&&full->isChecked());
    restoreArticleReadingState(w,place);check("saved scope still restores through the shared state",waitFor([&]{return w->editor()->property("researchSourceSlice").toBool()&&section->isChecked();}));
    openOutline();choose(browse,objects,"outline-browse");check("same sidebar entry reveals document objects",objects->isChecked()&&!chapters->isChecked()&&objectCount()>0&&browse->text()==objects->text());
    choose(browse,chapters,"outline-chapters");check("return to chapters removes object rows",chapters->isChecked()&&!objects->isChecked()&&objectCount()==0);
    auto *filter=w->findChild<QToolButton*>("researchArticleSearchToggle");filter->click();auto *query=w->findChild<QLineEdit*>("researchArticleSearch");query->setText("Main");key(query,Qt::Key_Escape);check("filter Escape clears only filter and keeps outline",query->isHidden()&&query->text().isEmpty()&&dock->isVisible());
    tree->setFocus();key(tree,Qt::Key_Escape);check("outline Escape returns to editor",waitFor([&]{return dock->isHidden()&&w->editor()->hasFocus();}));outline->trigger();check("existing top outline command reopens sidebar",waitFor([&]{return dock->isVisible()&&browse->isVisible();}));
    auto *childItem=findSection("Child result");check("child remains reachable from the same outline",childItem);if(childItem){tree->itemClicked(childItem,0);settle(100);}
    auto *cw=TeXDocumentWindow::findDocument(child);check("navigation opens actual child source",cw&&ready(cw)&&cw->textDoc()->editorText()==childSource);
    if(cw){auto *childScope=cw->findChild<QToolButton*>("researchArticleScopeButton");auto *location=cw->findChild<QLabel*>("researchArticleLocation");check("child scope keeps current-file identity and root context",childScope&&location&&childScope->toolTip().contains("child.tex")&&location->toolTip().contains("main.tex")&&cw->findChild<QToolButton*>("researchArticleFull")->text()==fileCaption);cw->grab().save(folder+"/article-child.png");cw->findChild<QAction*>("researchNavigateBack")->trigger();settle(100);check("cross-file Back restores original section editing",w->editor()->hasFocus()&&w->editor()->property("researchSourceSlice").toBool());}
    setFocusMode(true);settle(200);check("focus mode retains hidden scope behavior",!scope->isVisible()&&w->editor()->property("researchSourceSlice").toBool());setFocusMode(false);settle(250);check("leaving focus restores the same section",scope->isVisible()&&w->editor()->property("researchSourceSlice").toBool());w->grab().save(folder+"/article-section.png");
    const auto originalLocale=uiLocale();const auto beforeLocale=w->textCursor();
    for(const auto &locale:QStringList{"en","zh_TW","zh_CN"}){
      TWApp::instance()->applyTranslation(locale);settle(100);
      const auto expectedFull=locale=="en"?QStringLiteral("Edit this file"):locale=="zh_TW"?QStringLiteral("編輯此檔案"):QStringLiteral("编辑本文件");
      const auto expectedOutline=locale=="en"?QStringLiteral("Outline"):locale=="zh_TW"?QStringLiteral("章節"):QStringLiteral("章节");
      const auto expectedBrowse=locale=="en"?QStringLiteral("Browse"):locale=="zh_TW"?QStringLiteral("查閱"):QStringLiteral("查阅");
      check(locale+": live menus and visible sidebar label update together",full->text()==expectedFull&&chapters->text()==expectedOutline&&objects->text()==expectedBrowse&&browse->text()==expectedOutline&&w->textCursor()==beforeLocale);
    }TWApp::instance()->applyTranslation(originalLocale);settle(100);

    const auto deck=folder+"/slides/main.tex";const QString slides="\\documentclass{beamer}\n\\begin{document}\n\\begin{frame}{Main result}\nAn estimator with a controlled error.\\pause\nThe bound holds uniformly.\n\\end{frame}\n\\begin{frame}{Proof outline}\nCenter, truncate, and control the remainder.\n\\end{frame}\n\\end{document}\n";check("private slide sample written",put(deck,slides.toUtf8()));
    if(recording){setBuildCacheForTests(folder+"/compile-cache");Tw::Settings().setValue("researchBuildDestinationPolicy","local-cache");app.setEngineList({Engine("pdfLaTeX","C:/texlive/2023/bin/windows/pdflatex.exe",{"-synctex=1","-interaction=nonstopmode","-file-line-error","$fullname"},true)});}
    auto *s=new TeXDocumentWindow(deck);s->resize(w->size());s->show();QApplication::setActiveWindow(s);
    check("slide list becomes ready",waitFor([&]{auto *list=s->findChild<QListWidget*>("researchSlideList");return list&&list->count()==2;}));
    auto *list=s->findChild<QListWidget*>("researchSlideList");auto *directory=s->findChild<QDockWidget*>("researchSlideDock");auto *preview=s->findChild<QDockWidget*>("researchSlidePreviewDock");auto *expand=s->findChild<QToolButton*>("researchSlideExpandPreviewButton");auto *arrange=s->findChild<QToolButton*>("researchSlideOverviewButton");auto *more=s->findChild<QToolButton*>("researchSlidePreviewMore");
    if(list&&directory&&preview&&expand&&arrange&&more){
      bool compiledPreview=false;auto *pdf=s->findChild<QtPDF::PDFDocumentView*>("researchSlidePreview");
      if(recording){
        s->findChild<QAction*>("actionTypeset")->trigger();const bool idle=waitFor([&]{return !s->isTypesetting()&&!s->property("researchBuildPreparing").toBool()&&!s->property("researchBuildFinishing").toBool()&&!s->property("researchPublishing").toBool();},90000);
        put(folder+"/slides-compile.txt",s->property("consoleOutput").toString().toUtf8());auto document=QtPDF::Backend::Document::newDocument(previewFile(deck));
        check("recording compiles its own current TeX PDF",idle&&pdfPairValid(previewFile(deck))&&pdfRevision(previewFile(deck),deck)==PdfRevision::Current&&document&&document->isValid()&&contains(document,"An estimator with a controlled error.")&&contains(document,"Center, truncate, and control the remainder."));
        compiledPreview=pdf&&waitFor([&]{auto current=pdf->document().toStrongRef();auto *step=s->findChild<QSpinBox*>("researchSlideStep");return current&&current->isValid()&&contains(current,"An estimator with a controlled error.")&&step&&step->maximum()==2&&step->isEnabled();});
        check("recording uses the current mapped two-step slide preview",compiledPreview);QApplication::setActiveWindow(s);
      }
      auto panelsSettled=[&]{auto *veil=s->findChild<QWidget*>("researchPanelMotionVeil");return !veil||!veil->isVisible();};
      if(directory->isHidden())s->findChild<QAction*>("researchOutlineAction")->trigger();list->setCurrentRow(0);waitFor(panelsSettled);QCoreApplication::sendPostedEvents(nullptr,QEvent::LayoutRequest);settle(80);
      auto *add=s->findChild<QToolButton*>("researchSlideAddButton");
      auto buttonFits=[&](QToolButton *button){return button&&button->isVisible()&&directory->rect().contains(QRect(button->mapTo(directory,QPoint()),button->size()));};
      const bool headerReady=waitFor([&]{return panelsSettled()&&buttonFits(add)&&buttonFits(arrange)&&arrange->text()==(englishUi()?"Arrange":"排序");},2000);
      auto rect=[](const QRect &value){return QJsonObject{{"x",value.x()},{"y",value.y()},{"width",value.width()},{"height",value.height()}};};
      put(folder+"/slide-header-geometry.json",QJsonDocument(QJsonObject{{"directory",rect(directory->rect())},{"addVisible",add&&add->isVisible()},{"add",add?rect(QRect(add->mapTo(directory,QPoint()),add->size())):QJsonObject()},{"arrangeVisible",arrange->isVisible()},{"arrange",rect(QRect(arrange->mapTo(directory,QPoint()),arrange->size()))},{"arrangeText",arrange->text()},{"motionSettled",panelsSettled()}}).toJson());s->grab().save(folder+"/slides-before-arrange.png");
      check("new and arrange are directly available",headerReady);
      arrange->click();settle(180);auto *overview=s->findChild<QDialog*>("researchSlideOverview");check("arrange opens the existing overview",overview&&overview->isVisible());if(overview){overview->grab().save(folder+"/slides-arrange.png");key(overview,Qt::Key_Escape);settle(100);}check("arrange cancellation preserves original source",s->textDoc()->editorText()==slides);
      Recording record;const bool capture=recording&&compiledPreview;if(capture){record.start(folder+"/slide-preview-recording",s);record.action("Expand the real compiled slide preview");record.fastFor(500);}
      auto *expandCommand=s->findChild<QAction*>("researchSlideExpandPreview");if(expandCommand)expandCommand->trigger();settle(capture?900:250);
      check("expanded preview retains the slide list and hides only source",s->property("researchSlidePreviewExpanded").toBool()&&directory->isVisible()&&preview->isVisible()&&s->centralWidget()->isHidden());
      check("notes and export are absent from the preview header",s->findChild<QToolButton*>("researchSlideNotesButton")->isHidden()&&s->findChild<QToolButton*>("researchSlideOutputButton")->isHidden()&&expand->isVisible());
      auto *notes=list->findChild<QAction*>("researchSlideNotes");auto *exportAction=s->findChild<QAction*>("researchSlideExportMenuAction");check("preview menu retains original notes and export commands",notes&&more->menu()->actions().contains(notes)&&exportAction&&more->menu()->actions().contains(exportAction));s->grab().save(folder+"/slides-expanded.png");
      if(capture)record.action("Open speaker notes from preview options");choose(more,notes,"slides-notes-menu");auto *pane=s->findChild<QWidget*>("researchSlideNotesPane");check("notes menu opens the existing contextual pane",pane&&pane->isVisible()&&s->findChild<QPlainTextEdit*>("researchSlideNote0"));if(pane){auto *buttons=pane->findChild<QDialogButtonBox*>();if(buttons)buttons->button(QDialogButtonBox::Cancel)->click();settle(100);}check("closing unchanged notes does not edit source",s->textDoc()->editorText()==slides);
      if(capture){record.action("Return to editing with Escape");record.fastFor(500);}if(pdf){pdf->setFocus();key(pdf,Qt::Key_Escape);}check("Escape restores source after expanded preview",waitFor([&]{return !s->property("researchSlidePreviewExpanded").toBool()&&s->centralWidget()->isVisible();}));if(capture){record.finish();check("actual preview recording completes without capture errors",record.failures==0&&!record.frames.isEmpty());}
      list->setSelectionMode(QAbstractItemView::ExtendedSelection);list->item(0)->setSelected(true);list->item(1)->setSelected(true);check("notes disabled for multiple slides",notes&&!notes->isEnabled());
      check("existing slide shortcuts remain registered",list->findChild<QAction*>("researchSlideUp")&&list->findChild<QAction*>("researchSlideCopy")&&list->findChild<QAction*>("researchSlideUp")->shortcut()==QKeySequence("Alt+Up")&&list->findChild<QAction*>("researchSlideCopy")->shortcut()==QKeySequence("Ctrl+C"));
      s->findChild<QAction*>("researchSlideDirectoryAction")->trigger();waitFor(panelsSettled);s->findChild<QAction*>("researchOutlineAction")->trigger();check("hidden slide list reopens from the existing top command",waitFor([&]{return panelsSettled()&&directory->isVisible()&&buttonFits(arrange);}));settle(50);s->grab().save(folder+"/slides-writing.png");
    }else check("all slide controls exist",false);
    check("all private files are unchanged",bytes(main)==source.toUtf8()&&bytes(child)==childSource.toUtf8()&&bytes(deck)==slides.toUtf8());
    for(auto *window:TeXDocumentWindow::documentList()){window->textDoc()->setModified(false);window->close();}settle();watchdog.stop();app.exit(failures?1:0);
  });return app.exec();
}
