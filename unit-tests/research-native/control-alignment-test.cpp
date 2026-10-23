#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "ResearchSlides.cpp"
#include "PrefsDialog.h"
#include "Engine.h"
#include "ResearchOnboarding.h"
#include "TemplateDialog.h"
#include <QElapsedTimer>
#include <QJsonArray>
#include <QPdfWriter>
#include <QStatusBar>
#include <QScrollArea>
#include <QScrollBar>
#include <QScreen>
#include <QTabWidget>
#include <QThread>
#include <QFontInfo>
#include <QTextLayout>
#include <QtMath>
#include <windows.h>

static wchar_t crashFile[32768]{};
static LONG WINAPI captureCrash(EXCEPTION_POINTERS *error) {
  const auto code=error->ExceptionRecord->ExceptionCode;
  if(code!=0xc0000374&&code!=0xc0000005)return EXCEPTION_CONTINUE_SEARCH;
  static LONG entered=0;if(InterlockedCompareExchange(&entered,1,0))return EXCEPTION_CONTINUE_SEARCH;
  const HANDLE file=CreateFileW(crashFile,GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(file!=INVALID_HANDLE_VALUE){
    CONTEXT frame=*error->ContextRecord;
    for(int i=0;i<40&&frame.Rip;++i){
      DWORD64 base=0;auto *entry=RtlLookupFunctionEntry(frame.Rip,&base,nullptr);char module[MAX_PATH]{};GetModuleFileNameA(reinterpret_cast<HMODULE>(base),module,MAX_PATH);
      char line[1024];const int count=snprintf(line,sizeof line,"code=%08lx pc=%llx relative=%llx module=%s\r\n",code,frame.Rip,frame.Rip-base+0x140000000ULL,module);DWORD written=0;WriteFile(file,line,DWORD(qMax(0,qMin(count,1023))),&written,nullptr);
      if(!entry)break;PVOID data=nullptr;DWORD64 establisher=0;KNONVOLATILE_CONTEXT_POINTERS pointers{};
      RtlVirtualUnwind(UNW_FLAG_NHANDLER,base,frame.Rip,entry,&frame,&data,&establisher,&pointers);
    }
    CloseHandle(file);
  }
  return EXCEPTION_CONTINUE_SEARCH;
}

static bool waitFor(const std::function<bool()> &ready, int ms=20000) {
  QElapsedTimer t; t.start();
  while (!ready() && t.elapsed()<ms) { QCoreApplication::processEvents(); QThread::msleep(3); }
  return ready();
}
static void settle(int ms=180) { QElapsedTimer t; t.start(); waitFor([&]{return t.elapsed()>=ms;},ms+1000); }
static void fitWorkArea(QWidget *window) {
  window->showNormal();settle();
  const auto available=window->screen()->availableGeometry();
  const auto frameExtra=window->frameGeometry().size()-window->size();
  window->resize(QSize(1440,860).boundedTo(available.size()-frameExtra-QSize(16,16)));
  window->move(available.topLeft()+QPoint(8,8));settle();
}
static bool put(const QString &name, const QByteArray &bytes) {
  QFile f(name); return f.open(QIODevice::WriteOnly) && f.write(bytes)==bytes.size();
}
static bool statusChild(QWidget *w) {
  for (auto *p=w->parentWidget();p;p=p->parentWidget()) {
    if (qobject_cast<QStatusBar *>(p)) return true;
    if (p->isWindow()) break;
  }
  return false;
}
int main(int argc,char **argv) {
  const auto report=QString::fromLocal8Bit(argv[1]);(report+".crash.txt").toWCharArray(crashFile);AddVectoredExceptionHandler(1,captureCrash);argc=1; TWApp app(argc,argv);
  app.setQuitOnLastWindowClosed(false);
  QTimer::singleShot(80,&app,[&]{
    QJsonArray checks,inventory,centers,windows; int failures=0; const auto folder=QFileInfo(report).absolutePath();
    auto check=[&](const QString &name,bool passed){
      checks.append(QJsonObject{{"name",name},{"passed",passed}}); failures+=!passed;
      put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures}}).toJson());
    };
    auto audit=[&](QWidget *root,const QString &stage){
      settle(); root->grab().save(folder+"/"+stage+".png");
      if(auto *main=qobject_cast<QMainWindow *>(root);main&&!main->property("researchFocus").toBool()&&main->statusBar()->isVisible())
        if(auto *notice=main->findChild<QLabel *>("researchFocusMessage");notice&&notice->isVisible()) {
          const QRect textRect(notice->mapTo(main,QPoint()),notice->size());
          const QRect row(main->statusBar()->mapTo(main,QPoint()),main->statusBar()->size());
          check(stage+" ordinary notice remains inside the status row",row.contains(textRect));
        }
      if(root->isWindow()) {
        auto array=[](const QRect &rect){return QJsonArray{rect.x(),rect.y(),rect.width(),rect.height()};};
        const auto available=root->screen()->availableGeometry();
        windows.append(QJsonObject{{"stage",stage},{"frame",array(root->frameGeometry())},{"available",array(available)},{"minimumHeight",root->minimumHeight()},{"hintHeight",root->sizeHint().height()},{"minimumHintHeight",root->minimumSizeHint().height()},{"dpr",root->devicePixelRatioF()}});
        put(folder+"/window-geometry.json",QJsonDocument(windows).toJson());
        check(stage+" entire window fits the work area",available.adjusted(-1,-1,1,1).contains(root->frameGeometry()));
      }
      for (auto *w:root->findChildren<QWidget *>()) {
        if (!w->isVisibleTo(root)) continue;
        auto *line=qobject_cast<QLineEdit *>(w); auto *spin=qobject_cast<QAbstractSpinBox *>(w);
        auto *combo=qobject_cast<QComboBox *>(w); auto *button=qobject_cast<QAbstractButton *>(w);
        auto *bar=qobject_cast<QToolBar *>(w);
        auto *view=qobject_cast<QAbstractItemView *>(w);
        if (!line&&!spin&&!combo&&!button&&!bar&&!view) continue;
        const bool embedded=line&&(qobject_cast<QComboBox *>(w->parentWidget())||qobject_cast<QAbstractSpinBox *>(w->parentWidget()));
        const auto pos=w->mapTo(root,QPoint());
        QString text; if (button) text=button->text(); else if(line) text=line->text(); else if(combo) text=combo->currentText();
        inventory.append(QJsonObject{{"stage",stage},{"name",w->objectName()},{"class",w->metaObject()->className()},
          {"text",text},{"x",pos.x()},{"y",pos.y()},{"width",w->width()},{"height",w->height()},
          {"dpr",w->devicePixelRatioF()},{"embedded",embedded},{"status",statusChild(w)},{"fontStrategy",int(w->font().styleStrategy())}});
        if(view){const int flags=int(w->font().styleStrategy());const bool transparent=w->window()->testAttribute(Qt::WA_TranslucentBackground);check(stage+" list view "+w->objectName()+" retains native antialiasing for its surface",(flags&QFont::PreferAntialias)&&!(flags&QFont::NoAntialias)&&(transparent?bool(flags&QFont::NoSubpixelAntialias):!(flags&QFont::NoSubpixelAntialias)));}
        if ((line||spin||combo)&&!embedded&&!statusChild(w))
          check(stage+" field "+w->objectName()+" shares the 32px control row",w->height()==32);
        if (line&&!embedded&&!statusChild(w))
          check(stage+" field "+w->objectName()+" retains left-aligned input",(line->alignment()&Qt::AlignHorizontal_Mask)==Qt::AlignLeft);
        if (button && button->isEnabled() && w->property("researchControl").toBool()) {
          auto *tool=qobject_cast<QToolButton *>(button); if (!tool) continue;
          auto image=root->grab(QRect(tool->mapTo(root,QPoint()),tool->size())).toImage(); const qreal dpr=tool->devicePixelRatioF();
          int menu=tool->menu()&&tool->toolButtonStyle()!=Qt::ToolButtonIconOnly?18:0;
          QRect region(0,0,int((tool->width()-menu)*dpr),image.height());
          QRect ink;
          for(int y=3*int(qCeil(dpr));y<image.height()-3*qCeil(dpr);++y)
            for(int x=3*qCeil(dpr);x<region.right()-3*qCeil(dpr);++x) {
              const auto color=image.pixelColor(x,y);
              if(color.alpha()>180&&qMax(color.red(),qMax(color.green(),color.blue()))<145)
                ink=ink.isNull()?QRect(x,y,1,1):ink.united(QRect(x,y,1,1));
            }
          const qreal offsetX=(ink.left()+ink.right()-region.left()-region.right())/2.0;
          const qreal offsetY=(ink.top()+ink.bottom()-region.top()-region.bottom())/2.0;
          centers.append(QJsonObject{{"stage",stage},{"name",tool->objectName()},{"text",tool->text()},
            {"width",tool->width()},{"menu",menu},{"inkLeft",ink.left()/dpr},{"inkRight",ink.right()/dpr},
            {"hasInk",!ink.isNull()},{"offsetX",offsetX/dpr},{"offsetY",offsetY/dpr}});
          if (!menu&&!ink.isNull()&&QStringList{"researchSourcePreamble","researchSlideNotesButton","researchSlideOutputButton"}.contains(tool->objectName()))
            // Coverage boundaries are quantized to physical pixels. Allow
            // half a device pixel around the existing 3-logical-pixel margin.
            check(stage+" action "+tool->objectName()+" centers its visible content",qAbs(offsetX)<=3*dpr+0.5&&qAbs(offsetY)<=3*dpr+0.5);
        }
      }
      put(folder+"/inventory.json",QJsonDocument(inventory).toJson());
      put(folder+"/content-centers.json",QJsonDocument(centers).toJson());
    };
    Tw::Settings().setValue("openPDFwithTeX",false);
    auto *paper=new TeXDocumentWindow();paper->show();fitWorkArea(paper);
    paper->editor()->setPlainText("alpha beta alpha\n"); audit(paper,"paper");
    const auto sourceFont=paper->editor()->font();
    QJsonArray raster;
    auto textRaster=[&](QWidget *widget,const QString &name,const QRect &area,bool captionOnly=false){
      auto *window=widget->window();
      auto pixmap=window->grab(QRect(widget->mapTo(window,area.topLeft()),area.size()));pixmap.save(folder+"/"+name+".png");
      auto image=pixmap.toImage();int colored=0,gray=0,ink=0,coverage=0;QSet<QRgb> coverageColors;
      // The compile SVG deliberately uses blue-gray ink. Exclude its icon and
      // arrow slots when measuring text fringes; keep the complete PNG evidence.
      const auto dpr=widget->devicePixelRatioF();
      const QRect sample=captionOnly?QRect(qCeil(30*dpr),0,qFloor((widget->width()-54)*dpr),image.height()):image.rect();
      for(int y=sample.top();y<=sample.bottom();++y)for(int x=sample.left();x<=sample.right();++x){
        auto c=image.pixelColor(x,y);const int hi=qMax(c.red(),qMax(c.green(),c.blue())),lo=qMin(c.red(),qMin(c.green(),c.blue()));
        if(hi<245){if((c.red()>40&&c.red()<230)||(c.green()>40&&c.green()<230)||(c.blue()>40&&c.blue()<230)){++coverage;coverageColors.insert(c.rgb());}++ink;if(hi-lo>8)++colored;else if(lo>40&&hi<230)++gray;}
      }
      const auto f=widget->font();const QFontInfo actual(f);
      raster.append(QJsonObject{{"name",name},{"colored",colored},{"gray",gray},{"coverage",coverage},{"ink",ink},{"sampleRect",QJsonArray{sample.x(),sample.y(),sample.width(),sample.height()}},{"dpr",dpr},{"family",actual.family()},{"pixelSize",actual.pixelSize()},{"weight",int(f.weight())},{"strategy",int(f.styleStrategy())}});
      put(folder+"/text-raster.json",QJsonDocument(raster).toJson());
      check(name+" has intermediate coverage rather than binary jagged edges",coverage>30&&coverageColors.size()>8);
      check(name+" follows native text coverage without forcing grayscale on opaque surfaces",window->testAttribute(Qt::WA_TranslucentBackground)?colored==0:!(int(f.styleStrategy())&QFont::NoSubpixelAntialias));
    };
    paper->editor()->setPlainText(QString::fromUtf8("alpha beta alpha\n编辑文档 幻灯片 0123456789\n"));settle();
    QTextLayout mixed(QString::fromUtf8("alpha 编辑文档"),sourceFont);
    mixed.beginLayout();mixed.createLine();mixed.endLayout();QJsonArray fallback;bool glyphsPresent=true;
    for(const auto &run:mixed.glyphRuns()) {
      fallback.append(run.rawFont().familyName());
      for(auto glyph:run.glyphIndexes())glyphsPresent&=glyph!=0;
    }
    put(folder+"/font-fallback.json",QJsonDocument(QJsonObject{{"families",fallback},{"glyphsPresent",glyphsPresent}}).toJson());
    check("Latin and Chinese text both resolve to real glyphs",glyphsPresent&&!fallback.isEmpty());
    textRaster(paper->editor()->viewport(),"source-text-native",QRect(0,0,360,60));
    auto *compileText=paper->findChild<QToolButton *>("researchCompile");
    textRaster(compileText,"compile-text-native",compileText->rect(),true);
    auto borderBrightness=[](QToolButton *button){
      const auto image=button->grab().toImage();int value=765;
      for(int y=0;y<qCeil(button->devicePixelRatioF());++y){const auto color=image.pixelColor(image.width()/2,y);const int a=color.alpha();value=qMin(value,((color.red()+color.green()+color.blue())*a+737*(255-a))/255);}
      return value;
    };
    auto *more=paper->findChild<QToolButton *>("researchMore");
    QApplication::setActiveWindow(paper);
    paper->editor()->setFocus();settle();const int idleBorder=borderBrightness(more);
    more->setFocus(Qt::TabFocusReason);settle();
    check("main toolbar keyboard focus has a visible border",QApplication::focusWidget()==more&&borderBrightness(more)<idleBorder-60);
    audit(paper,"toolbar-keyboard-focus");paper->editor()->setFocus();
    Research::showTransientStatusMessage(paper,"A transient notice after resizing",4000);
    paper->resize(paper->width()-48,paper->height()-48);audit(paper,"notice-after-resize");
    paper->statusBar()->clearMessage();fitWorkArea(paper);
    paper->doReplaceDialog(); paper->findChild<QToolButton *>("researchFindOptionsToggle")->setChecked(true);
    audit(paper,"find-expanded");
    auto *find=paper->findChild<QLineEdit *>("researchFindField");
    auto *scope=paper->findChild<QComboBox *>("researchFindScope");
    auto *results=paper->findChild<QToolButton *>("researchFindAll");
    check("find options field and results share height and center",scope->height()==results->height()&&qAbs(scope->geometry().center().y()-results->geometry().center().y())<=1);
    check("find and replacement input left edges align",find->mapTo(paper,QPoint()).x()==paper->findChild<QLineEdit *>("researchReplaceField")->mapTo(paper,QPoint()).x());
    auto *replace=paper->findChild<QLineEdit *>("researchReplaceField");
    check("find and replacement input right edges align",find->mapTo(paper,QPoint()).x()+find->width()==replace->mapTo(paper,QPoint()).x()+replace->width());
    paper->findChild<QToolButton *>("researchFindClose")->click();
    QTimer::singleShot(100,&app,[&]{
      auto *d=qobject_cast<QDialog *>(QApplication::activeModalWidget()); if(!d){check("preferences opens",false);return;}
      auto *tabs=d->findChild<QTabWidget *>();
      for(int i=0;tabs&&i<tabs->count();++i){tabs->setCurrentIndex(i);audit(d,QString("preferences-%1").arg(i));
        if(auto *area=qobject_cast<QScrollArea *>(tabs->widget(i));area&&area->verticalScrollBar()->maximum()>0){area->verticalScrollBar()->setValue(area->verticalScrollBar()->maximum());audit(d,QString("preferences-%1-bottom").arg(i));}
      }
      d->reject();
    }); PrefsDialog::doPrefsDialog(paper);
    QTimer::singleShot(100,&app,[&]{if(auto *d=paper->findChild<QDialog *>("researchCommandDialog")){audit(d,"commands");d->reject();}});
    paper->findChild<QAction *>("researchCommandPalette")->trigger();
    { TemplateDialog d; QTimer::singleShot(200,&app,[&]{audit(&d,"templates");d.reject();});d.exec(); }
    const auto pdf=folder+"/reading.pdf";{QPdfWriter writer(pdf);QPainter p(&writer);p.drawText(QRect(50,50,2000,500),"Independent PDF");}
    auto *reader=new PDFDocumentWindow(pdf);reader->show();fitWorkArea(reader);audit(reader,"pdf");
    const auto root=folder+"/main.tex";
    const QByteArray source="\\documentclass{beamer}\n\\title{Report}\n\\author{Author}\n\\usetheme{default}\n\\begin{document}\n\\begin{frame}{Evidence}\nAlpha\\pause Beta\n\\end{frame}\n\\begin{frame}{Conclusion}\nDone\n\\end{frame}\n\\end{document}\n";
    put(root,source);auto *slides=new TeXDocumentWindow(root);slides->show();fitWorkArea(slides);
    check("slide directory is ready",waitFor([&]{auto *c=Research::controller(slides);return c&&c->enabled&&!c->dirty&&!c->busy;}));
    auto *c=Research::controller(slides);if(!c){app.exit(1);return;}
    // Open through the existing action if narrow initial placement hid the
    // optional preview before the fixture fitted the window to its work area.
    if(c->previewDock->isHidden())c->previewAction->trigger();
    c->select(0,true);waitFor([&]{return !c->busy&&!c->dirty;});
    audit(slides,"slides");
    const Engine engine("Alignment test","C:/texlive/2023/bin/windows/pdflatex.exe",{"-synctex=1","-interaction=nonstopmode","-file-line-error","$fullname"},true);app.setEngineList({engine});
    auto compile=[&]{slides->typeset();const bool done=waitFor([&]{return !slides->isTypesetting()&&!slides->property("researchBuildPreparing").toBool()&&!slides->property("researchBuildFinishing").toBool()&&!slides->property("researchPublishing").toBool();},45000);settle(900);return done;};
    check("overlay preview compiles with matching navigation",compile()&&compile()&&!Research::previewFile(root).isEmpty());
    c->select(0,true);check("compiled overlay exposes its step controls",waitFor([&]{return !c->dirty&&!c->busy&&c->step->isVisible()&&c->step->maximum()==2;}));
    audit(slides,"slides-compiled");
    auto titleArea=c->list->visualItemRect(c->list->item(0)).adjusted(41,8,-9,0);titleArea.setHeight(20);
    textRaster(c->list->viewport(),"slide-title-native",titleArea);
    Research::showTransientStatusMessage(slides,"Independent status notice",1800);settle();
    auto *notice=slides->findChild<QLabel *>("researchFocusMessage");
    const QRect statusRect(slides->statusBar()->mapTo(slides,QPoint()),slides->statusBar()->size());
    check("ordinary notice stays inside status row instead of covering commands",notice&&notice->isVisible()&&statusRect.contains(QRect(notice->mapTo(slides,QPoint()),notice->size()))&&notice->width()>100&&!notice->text().isEmpty());
    audit(slides,"notice-inline");slides->statusBar()->clearMessage();
    slides->findChild<QToolButton *>("researchSourcePreamble")->click();settle();
    check("preamble navigation exposes its active editing scope",slides->findChild<QToolButton *>("researchSourcePreamble")->property("researchActiveScope").toBool());
    audit(slides,"preamble-active");
    c->select(0,true);settle();
    check("returning to slide clears preamble emphasis",!slides->findChild<QToolButton *>("researchSourcePreamble")->property("researchActiveScope").toBool());
    slides->findChild<QToolButton *>("researchSourceFull")->click();settle();
    check("full-source navigation exposes its active editing scope",slides->findChild<QToolButton *>("researchSourceFull")->property("researchActiveScope").toBool());
    audit(slides,"full-source-active");slides->findChild<QToolButton *>("researchSourceFull")->click();settle();
    check("leaving full source restores scope emphasis",!slides->findChild<QToolButton *>("researchSourceFull")->property("researchActiveScope").toBool());
    auto *notesAction=slides->findChild<QToolButton *>("researchSlideNotesButton");
    check("scope navigation retains the complete source text",slides->textDoc()->editorText()==QString::fromUtf8(source));
    check("scope navigation settles with note actions available",waitFor([&]{return !c->dirty&&!c->busy&&notesAction->isEnabled();},5000));
    put(folder+"/scope-state.json",QJsonDocument(QJsonObject{{"dirty",c->dirty},{"busy",c->busy},{"selected",c->selected},{"noteEnabled",notesAction->isEnabled()},{"source",slides->textDoc()->editorText()},{"parsedSource",c->deck.sources.value(Research::path(root))}}).toJson());
    QApplication::setActiveWindow(slides);
    slides->editor()->setFocus();settle();const int quietBorder=borderBrightness(notesAction);
    notesAction->setFocus(Qt::TabFocusReason);settle();audit(slides,"keyboard-focus");
    check("secondary caption commands have visible keyboard focus",QApplication::focusWidget()==notesAction&&borderBrightness(notesAction)<quietBorder-60);
    slides->editor()->setFocus();
    c->nextStep->click();check("centered next-step action still navigates",c->step->value()==2);c->previousStep->click();check("centered previous-step action still navigates",c->step->value()==1);
    check("directory and preview headers share a height",c->dock->titleBarWidget()->height()==c->previewDock->titleBarWidget()->height());
    check("step field and adjacent buttons share a height",c->step->height()==c->previousStep->height()&&c->step->height()==c->nextStep->height());
    c->showSearch(true);audit(slides,"slide-search");c->showSearch(false);
    QTimer::singleShot(100,&app,[&]{auto *d=slides->findChild<QDialog *>("researchSlideDocumentOptions");if(!d){check("document settings opens",false);return;}auto *tabs=d->findChild<QTabWidget *>("researchDocumentTabs");for(int i=0;i<tabs->count();++i){tabs->setCurrentIndex(i);audit(d,QString("document-%1").arg(i));}d->reject();});c->documentOptions();
    const auto picture=folder+"/picture.png";QImage im(300,180,QImage::Format_RGB32);im.fill(QColor("#497c92"));im.save(picture);
    c->select(0,true);waitFor([&]{return !c->busy&&!c->dirty;});bool imageSeen=false;
    QTimer::singleShot(100,&app,[&]{if(auto *d=slides->findChild<QDialog *>("researchImageOptions")){imageSeen=true;audit(d,"image-options");d->reject();}});c->imageOptions(picture);check("image sizing controls were inspected",imageSeen);
    c->select(0,true);waitFor([&]{return !c->busy&&!c->dirty;});c->editNotes();audit(slides,"notes");check("speaker notes controls were inspected",c->notesPane&&c->notesPane->isVisibleTo(slides)&&c->notesPane->findChild<QPlainTextEdit *>()&&c->notesPane->findChild<QPlainTextEdit *>()->isVisibleTo(slides));
    c->showOverview();settle();if(auto *d=slides->findChild<QDialog *>("researchSlideOverview")){audit(d,"overview");d->reject();}
    auto *menu=slides->findChild<QToolButton *>("researchSlideAddButton")->menu();menu->popup(slides->mapToGlobal(QPoint(24,90)));settle();audit(menu,"layouts");menu->hide();
    Research::showOnboarding(slides,false);settle();if(auto *d=slides->findChild<QDialog *>("researchOnboarding")){audit(d,"onboarding");d->close();}
    Research::setFocusMode(true);audit(slides,"focus-source");audit(reader,"focus-pdf");Research::setFocusMode(false);
    const auto restoredFont=paper->editor()->font();
    check("focus and dialogs retain the user's source family, size and emphasis",restoredFont.families()==sourceFont.families()&&restoredFont.pixelSize()==sourceFont.pixelSize()&&restoredFont.pointSizeF()==sourceFont.pointSizeF()&&restoredFont.weight()==sourceFont.weight()&&restoredFont.italic()==sourceFont.italic());
    check("audit never changes the supplied slide source",Research::read(root)==QString::fromUtf8(source));
    paper->setModified(false);slides->setModified(false);app.exit(failures?1:0);
  });
  QTimer::singleShot(180000,&app,[&]{app.exit(92);});return app.exec();
}
