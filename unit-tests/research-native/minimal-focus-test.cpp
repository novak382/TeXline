#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "ResearchSlides.cpp"
#include <QJsonArray>
#include <QElapsedTimer>
#include <QThread>
#include <QScreen>
#include <QStatusBar>
#include <QToolTip>
#include <QHelpEvent>
#include <windows.h>
static QString crashPath;
static LONG WINAPI recordCrash(EXCEPTION_POINTERS *exception) {
  CONTEXT context=*exception->ContextRecord;QJsonArray frames;
  for(int i=0;i<30&&context.Rip;++i) {
    const auto base=reinterpret_cast<DWORD64>(GetModuleHandleW(nullptr));frames.append(QString::number(context.Rip-base+0x140000000ULL,16));
    DWORD64 image=0;auto *entry=RtlLookupFunctionEntry(context.Rip,&image,nullptr);
    if(!entry)break;PVOID data=nullptr;DWORD64 establisher=0;KNONVOLATILE_CONTEXT_POINTERS pointers{};
    RtlVirtualUnwind(UNW_FLAG_NHANDLER,image,context.Rip,entry,&context,&data,&establisher,&pointers);
  }
  QFile f(crashPath);if(f.open(QIODevice::WriteOnly))f.write(QJsonDocument(QJsonObject{{"stack",frames},{"code",double(exception->ExceptionRecord->ExceptionCode)}}).toJson());
  return EXCEPTION_EXECUTE_HANDLER;
}

static bool waitFor(const std::function<bool()> &f,int ms=20000){QElapsedTimer t;t.start();while(!f()&&t.elapsed()<ms){QCoreApplication::processEvents();QThread::msleep(3);}return f();}
static void settle(int ms=700){QElapsedTimer t;t.start();waitFor([&]{return t.elapsed()>=ms;},ms+1000);}
static void put(const QString &p,const QString &s){QFile f(p);if(f.open(QIODevice::WriteOnly))f.write(s.toUtf8());}
int main(int argc,char **argv){const QString report=QString::fromLocal8Bit(argv[1]);crashPath=report+".crash.json";SetUnhandledExceptionFilter(recordCrash);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);QTimer::singleShot(80,&app,[&]{
 QJsonArray checks;int failures=0;auto check=[&](QString name,bool ok){if(!ok)++failures;checks.append(QJsonObject{{"name",name},{"passed",ok}});put(report,QString::fromUtf8(QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures}}).toJson()));};
 const auto folder=QFileInfo(report).absolutePath(),root=folder+"/main.tex";const QString original="\\documentclass{beamer}\n\\title{Focus}\n\\begin{document}\n\\begin{frame}{First}\nAlpha\n\\end{frame}\n\\appendix\n\\begin{frame}{Backup}\nBeta\n\\end{frame}\n\\end{document}\n";put(root,original);
 Tw::Settings().setValue("researchFocusMode",false);Tw::Settings().setValue("openPDFwithTeX",false);auto *w=new TeXDocumentWindow(root);w->resize(1440,860);w->show();auto ready=[&]{QCoreApplication::processEvents();return waitFor([&]{auto *c=Research::controller(w);return c&&c->enabled&&!c->dirty&&!c->busy;});};check("normal slide mode ready",ready());auto *c=Research::controller(w);if(!c){app.exit(1);return;}
 check("empty preview has one quiet message and a white surface",c->emptyPreview&&c->emptyPreview->isVisible()&&c->status->isHidden()&&c->emptyPreview->grab().toImage().pixelColor(2,2)==QColor(Qt::white));
 auto *add=w->findChild<QToolButton *>("researchSlideAddButton"),*more=w->findChild<QToolButton *>("researchSlideMoreButton"),*previewMore=w->findChild<QToolButton *>("researchSlidePreviewMore");
 check("directory has one header with New and More",add&&more&&add->parentWidget()==c->dock->titleBarWidget()&&more->parentWidget()==c->dock->titleBarWidget());
 check("preview uses the same header height as the directory",previewMore&&c->position->parentWidget()==c->previewDock->titleBarWidget()&&c->dock->titleBarWidget()->height()==c->previewDock->titleBarWidget()->height());
 check("header controls share 32px height and 18px SVGs",add->height()==32&&more->size()==QSize(32,32)&&previewMore->size()==QSize(32,32)&&add->iconSize()==QSize(18,18)&&more->iconSize()==QSize(18,18));
 check("directory has exactly two permanent header buttons",[&]{int n=0;for(auto *b:c->dock->titleBarWidget()->findChildren<QToolButton *>())n+=b->isVisibleTo(c->dock->titleBarWidget());return n==2;}());
 check("secondary controls are accessible in More",more->menu()->findChild<QAction *>("researchSlideOverviewMenuAction")&&more->menu()->findChild<QAction *>("researchSourcePreambleMenuAction")&&more->menu()->findChild<QAction *>("researchSourceFullMenuAction")&&more->menu()->actions().contains(w->findChild<QAction *>("researchDocumentOptionsAction")));
 check("preview has exactly two permanent header buttons for an uncompiled single-step slide",[&]{int n=0;for(auto *b:c->previewDock->titleBarWidget()->findChildren<QToolButton *>())n+=b->isVisibleTo(c->previewDock->titleBarWidget());return n==2;}());
 check("no permanent source controls or footer",c->scopeBar->isHidden()&&c->searchBar->isHidden()&&w->findChild<QToolButton *>("researchSlideSettings")->isHidden());
 const bool wasEnglish=Research::englishUi();
 for(const auto &locale:QStringList{QStringLiteral("en_US"),QStringLiteral("zh_CN")}){app.applyTranslation(locale);settle(100);const bool en=locale.startsWith("en");check(locale+" live slide options and preview options translate",more->accessibleName()==(en?QStringLiteral("Slide options"):QStringLiteral("幻灯片选项"))&&previewMore->accessibleName()==(en?QStringLiteral("Preview options"):QStringLiteral("预览选项")));bool outline=false,preview=false;for(auto *a:more->menu()->actions())outline|=a->text()==(en?QStringLiteral("Hide outline"):QStringLiteral("收起目录"));for(auto *a:previewMore->menu()->actions())preview|=a->text()==(en?QStringLiteral("Hide preview"):QStringLiteral("收起预览"));check(locale+" live panel menu captions translate",outline&&preview);}
 app.applyTranslation(wasEnglish?QStringLiteral("en_US"):QStringLiteral("zh_CN"));settle(100);
 auto *search=w->findChild<QToolButton *>("researchSlideSearchButton");search->click();QCoreApplication::processEvents();check("one click opens inline search",c->searchBar->isVisible()&&c->scopeBar->isHidden()&&c->filter->hasFocus());
 {auto *closeSearch=w->findChild<QToolButton *>("researchCloseSlideSearch");QToolTip::hideText();QHelpEvent help(QEvent::ToolTip,closeSearch->rect().center(),closeSearch->mapToGlobal(closeSearch->rect().center()));QApplication::sendEvent(closeSearch,&help);settle(40);check("obvious search-close control does not open a redundant tooltip",!QToolTip::isVisible());}
 check("search and filter share a row without the old combo row",c->filterButton->isVisible()&&c->groups->isHidden()&&qAbs(c->filterButton->geometry().center().y()-c->filter->geometry().center().y())<3);
 c->filterMenu->aboutToShow();int backup=c->groups->findData("backup");c->filterMenu->findChild<QAction *>(QString("researchSlideGroup%1").arg(backup))->trigger();check("compact filter still selects backup slides",c->list->item(0)->isHidden()&&!c->list->item(1)->isHidden());c->filter->setText("Backup");w->grab().save(folder+"/search.png");c->filterMenu->aboutToHide();QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(c->filter,&escape);QCoreApplication::processEvents();check("escape restores direct editing controls and clears hidden filters",c->scopeBar->isHidden()&&c->searchBar->isHidden()&&c->filter->text().isEmpty()&&c->groups->currentIndex()==0&&!c->list->item(0)->isHidden());
 auto compactEditor=[&](const QString &mode){auto *e=w->editor();const QRect view=e->viewport()->geometry();check(mode+" editor starts without a blank top band",view.top()<=4);check(mode+" scrollbar has no wide outer gutter",e->width()-view.right()-1<=20);};
 compactEditor("normal");check("normal mode shows line and position status",w->statusBar()->isVisible());
 check("preamble and full source stay out of the permanent toolbar",!w->findChild<QToolButton *>("researchSourcePreamble")->isVisible()&&!c->fullSourceButton->isVisible());w->grab().save(folder+"/normal.png");ready();
 auto visible=[&]{int n=0;for(auto b=w->textDoc()->begin();b.isValid();b=b.next())n+=b.isVisible();return n;};const auto normalGeometry=w->geometry();Research::setFocusMode(true);settle();
 compactEditor("focus");check("focus hides the entire status bar",w->statusBar()->isHidden());
 check("focus editor reaches the bottom without a reserved status strip",w->height()-w->editor()->mapTo(w,QPoint(0,w->editor()->height())).y()<=3);
 {auto *e=w->editor();const bool numbers=e->getLineNumbersVisible();for(bool enabled:{false,true}){e->setLineNumberDisplay(enabled);auto cursor=w->textCursor();cursor.movePosition(QTextCursor::Start);e->setTextCursor(cursor);e->ensureCursorVisible();settle();compactEditor(enabled?"focus with line numbers":"focus without line numbers");check(enabled?"numbered first source line remains fully visible":"first source line remains fully visible",e->viewport()->rect().contains(e->cursorRect()));w->grab().save(folder+(enabled?"/focus-numbers.png":"/focus-no-numbers.png"));}e->setLineNumberDisplay(numbers);}
 check("focus keeps the existing window size",w->geometry()==normalGeometry&&!w->isMaximized());
 check("focus suspends Beamer rather than only hiding it",w->property("researchFocus").toBool()&&!c->enabled&&!w->property("researchSlideMode").toBool());
 check("focus exposes every source line",visible()==w->textDoc()->blockCount()&&!w->editor()->property("researchSourceSlice").toBool());
 check("focus hides all slide panels and mode actions",c->dock->isHidden()&&c->previewDock->isHidden()&&!c->toggle->isVisible()&&!c->toggle->isEnabled()&&!w->findChild<QAction *>("researchDetectBeamerAction"));
 check("focus disables slide-specific shortcut",!w->findChild<QAction *>("researchSlideQuickInsert")->isEnabled());
 const int parses=w->property("researchSlideParses").toInt();auto cursor=w->textCursor();cursor.setPosition(0);w->editor()->setTextCursor(cursor);w->editor()->insertPlainText("% Focus edit\n");Research::detectSlideMode(w);settle();check("focus typing never starts slide parsing",!c->enabled&&w->property("researchSlideParses").toInt()==parses);w->editor()->undo();check("focus editing has exact native undo",w->textDoc()->editorText()==original);w->grab().save(folder+"/focus.png");
 const auto coldRoot=folder+"/opened-in-focus.tex";put(coldRoot,original);auto *cold=new TeXDocumentWindow(coldRoot);cold->show();settle();check("opening Beamer while focused does not activate a controller",cold->property("researchFocus").toBool()&&!Research::controller(cold));check("cold focus mode action cannot enable Beamer",!cold->findChild<QAction *>("researchSlideModeAction")->isEnabled());
 const auto coldGeometry=cold->geometry();check("new focused source opens on the left half",!cold->isMaximized()&&qAbs(cold->width()-cold->screen()->availableGeometry().width()/2)<30&&cold->x()<cold->screen()->availableGeometry().center().x());Research::setFocusMode(false);settle();check("exit focus restores the status bar",w->statusBar()->isVisible()&&cold->statusBar()->isVisible());check("exit focus preserves the half-screen window",cold->geometry()==coldGeometry&&!cold->isMaximized());check("exit focus restores slide workflow",ready()&&c->dock->isVisible()&&c->previewDock->isVisible()&&c->toggle->isEnabled());check("cold document detects Beamer only after leaving focus",waitFor([&]{auto *cc=Research::controller(cold);return cc&&cc->enabled&&!cc->busy&&!cc->dirty;}));cold->setModified(false);cold->close();
 c->previewDock->hide();Research::setFocusMode(true);settle();Research::setFocusMode(false);settle();check("manually collapsed preview stays collapsed after focus",ready()&&c->previewDock->isHidden());
 c->previewDock->show();c->select(0,true);c->editNotes();c->notesPane->findChild<QPlainTextEdit *>("researchSlideNote0")->setPlainText("Invalid {");Research::setFocusMode(true);settle();check("invalid note prevents transition without losing draft",!w->property("researchFocus").toBool()&&c->notesChanged&&c->notesPane->isVisible());c->notesPane->findChild<QAction *>("researchDiscardNotes")->trigger();
 check("navigation and focus never change source on disk",Research::read(root)==original);w->setModified(false);app.exit(failures?1:0);
 });QTimer::singleShot(120000,&app,[&]{app.exit(92);});return app.exec();}
