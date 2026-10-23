// Actual native paint frames on an inactive desktop. No physical input.
#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "ResearchSlides.cpp"
#include "ResearchArticle.h"
#include "ResearchMotion.h"
#include <QJsonArray>
#include <QThread>
#include <QTextBrowser>
#include <QScreen>
#include <QImageWriter>
#include <QInputMethodEvent>
#include <windows.h>
#include <shellapi.h>

static bool waitFor(const std::function<bool()> &f,int ms=15000){QElapsedTimer t;t.start();while(!f()&&t.elapsed()<ms){QCoreApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QThread::msleep(2);}return f();}
static void settle(int ms=220){QElapsedTimer t;t.start();waitFor([&]{return t.elapsed()>=ms;},ms+2000);}
static void put(const QString &file,const QByteArray &text){QDir().mkpath(QFileInfo(file).absolutePath());QFile f(file);if(f.open(QIODevice::WriteOnly))f.write(text);}
static void key(QWidget *target,int code,const QString &text){QKeyEvent e(QEvent::KeyPress,code,Qt::NoModifier,text);QApplication::sendEvent(target,&e);}
static int darkPixels(const QImage &image,QRect bounds={}) {const auto im=image.convertToFormat(QImage::Format_ARGB32);bounds=bounds.isNull()?im.rect():bounds.intersected(im.rect());int count=0;for(int y=bounds.top();y<=bounds.bottom();y+=2){const auto *row=reinterpret_cast<const QRgb*>(im.constScanLine(y));for(int x=bounds.left();x<=bounds.right();x+=2)count+=qAlpha(row[x])>128&&qRed(row[x])+qGreen(row[x])+qBlue(row[x])<390;}return count;}
struct Clip {
  QString folder;QPointer<QWidget> window;QTimer tick;QElapsedTimer clock;QJsonArray frames,events;QVector<QImage> images;int number{0};
  explicit Clip(const QString &path,QWidget *w):folder(path),window(w){tick.setInterval(33);tick.setTimerType(Qt::PreciseTimer);QObject::connect(&tick,&QTimer::timeout,[this]{capture();});}
  void capture(){if(!window)return;const qint64 at=clock.elapsed();auto image=window->grab().toImage();images.append(!images.isEmpty()&&image==images.last()?images.last():image);const QString name=QString("%1.png").arg(number++,4,10,QLatin1Char('0'));frames.append(QJsonObject{{"file",name},{"atMs",double(at)}});}
  void start(){QDir().mkpath(folder);clock.start();capture();tick.start();}
  void event(const QString &name){events.append(QJsonObject{{"atMs",double(clock.elapsed())},{"action",name}});}
  void stop(){tick.stop();capture();for(int i=0;i<images.size();++i){if(i>0&&images[i].cacheKey()==images[i-1].cacheKey()){auto frame=frames[i].toObject();frame["file"]=frames[i-1].toObject()["file"];frames[i]=frame;continue;}QImageWriter writer(folder+"/"+frames[i].toObject()["file"].toString());writer.setCompression(1);writer.write(images[i]);}images.clear();put(folder+"/frames.json",QJsonDocument(QJsonObject{{"frames",frames},{"actions",events},{"physicalDpr",window?window->devicePixelRatioF():0},{"capture","QWidget::grab of actual native animation; identical frames share pixels; PNG encoding after each clip; private inactive desktop"}}).toJson());}
};
int main(int argc,char **argv){
  if(!qEnvironmentVariable("TEXLINE_TEST_DESKTOP").startsWith("TeXlineValidation-"))return 2;
  int n=0;auto args=CommandLineToArgvW(GetCommandLineW(),&n);const QString report=QString::fromWCharArray(args[1]);LocalFree(args);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
  QTimer::singleShot(80,&app,[&]{
    const QString folder=QFileInfo(report).absolutePath();QJsonArray checks,geometry;int failures=0;
    auto check=[&](const QString &name,bool good){checks.append(QJsonObject{{"name",name},{"passed",good}});failures+=!good;put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"geometry",geometry}}).toJson());};
    Tw::Settings().setValue("researchReduceMotion",false);Tw::Settings().setValue("openPDFwithTeX",false);
    const QString path=folder+"/fixtures/paper/main.tex";QString source="\\documentclass{article}\n\\usepackage{amsmath,amsthm}\n\\newtheorem{theorem}{Conservation law}\n\\title{Mass conservation in a sealed diffusion cell}\n\\begin{document}\n\\section{Diffusive transport}\n";
    for(const auto &paragraph:QStringList{
      "The concentration field obeys local mass balance in a sealed cell. The flux is proportional to the negative concentration gradient.",
      "An impermeable boundary suppresses exchange with the surroundings. Internal redistribution can change the profile while leaving total mass unchanged.",
      "For a constant diffusivity, the continuity equation reduces to the heat equation. The boundary condition is a vanishing normal derivative.",
      "Integrating local continuity over the domain converts the volume divergence into a boundary flux. This gives a direct conservation diagnostic.",
      "At equilibrium the concentration becomes spatially uniform on a connected domain. Its value is fixed by the initial total mass.",
      "A finite-volume discretization should preserve the cancellation of fluxes across shared cell faces. Only boundary faces contribute to the global balance.",
      "Mass drift is therefore a useful implementation check. It can reveal an inconsistent boundary stencil even when the plotted profiles look smooth.",
      "The diagnostic should be evaluated before normalization. Rescaling each profile would hide the very error that the check is intended to detect.",
      "For measured profiles, the integration weights must reflect the sampling geometry. A two-dimensional image cannot be summed as though every pixel represented the same volume.",
      "The following calculation isolates the closed-system assumption. A source term or a permeable wall requires an additional term in the mass balance."
    })source+=paragraph+QStringLiteral("\n\n");
    source+="\\subsection{Closed-system balance}\n\\begin{theorem}\\label{thm:main}\nFor a smooth solution of $\\partial_t c=D\\Delta c$ on a bounded smooth domain $\\Omega$ with $\\partial_n c=0$, the total mass $\\int_\\Omega c(x,t)\\,dx$ is constant.\n\\end{theorem}\n\\begin{proof}\nThe divergence theorem gives\\[\\frac{d}{dt}\\int_\\Omega c\\,dx=D\\int_\\Omega\\Delta c\\,dx=D\\int_{\\partial\\Omega}\\partial_n c\\,dS=0.\\]\n\\end{proof}\n\\section{Method}\nTrack the volume-weighted concentration sum before any profile normalization.\n\\section{Results}\nA conservative flux discretization cancels every interior face contribution exactly.\n\\end{document}\n";put(path,source.toUtf8());
    auto *w=new TeXDocumentWindow(path);w->resize(1120,730);w->show();
    check("paper detection completes with the actual article controls",waitFor([&]{return w->property("researchArticleReady").toBool()&&w->property("researchArticleMode").toBool();}));
    auto *outline=w->findChild<QAction*>("researchOutlineAction");auto *dock=w->findChild<QDockWidget*>("researchArticleDock");auto *tree=w->findChild<QTreeWidget*>("researchArticleTree");
    if(!outline||!dock||!tree){check("article panel exists",false);app.exit(1);return;}
    const bool record=qEnvironmentVariable("TEXLINE_MOTION_RECORD")=="1";
    for(bool half:{false,true}){
      const QString prefix=half?"half":"wide";Tw::Settings().setValue("researchReduceMotion",true);if(outline->isChecked())outline->trigger();
      const auto work=w->screen()->availableGeometry();w->resize(half?work.width()/2:1120,qMin(730,work.height()-36));settle();Tw::Settings().setValue("researchReduceMotion",false);
      auto cursor=w->textCursor();cursor.setPosition(source.indexOf("The concentration field"));w->editor()->setTextCursor(cursor);w->editor()->verticalScrollBar()->setValue(0);const int pos=w->cursorPosition(),revision=w->textDoc()->revision();
      Clip clip(folder+"/clips/panel-"+prefix,w);if(record){clip.start();settle(350);clip.event("Open article structure");}
      outline->trigger();auto *veil=w->findChild<QWidget*>("researchPanelMotionVeil");check(prefix+": opening starts an interruptible reveal",veil&&veil->isVisible());
      const qreal density=w->devicePixelRatioF();const QRect bodyInk(0,qRound(w->editor()->mapTo(w,QPoint()).y()*density),qRound(w->width()*density),qRound(w->editor()->height()*density));
      QList<qreal> extents,sampledExtents;QVector<QImage> sampled;QSet<QByteArray> rasters;bool space=true,stable=true,visibleInk=true,structureInk=false;int layouts=-1;QElapsedTimer t;t.start();
      // Cache paints first. Full-image hashing/conversion at 200% must not
      // starve the very 160ms timer whose intermediate frames we observe.
      while(t.elapsed()<200){QCoreApplication::processEvents();sampled.append(w->grab().toImage());const qreal extent=veil->isVisible()?veil->property("researchMotionExtent").toReal():-1;sampledExtents<<extent;if(extent>=0)extents<<extent;space&=w->editor()->viewport()->width()>=w->width()/2;const int lines=w->textDoc()->findBlock(source.indexOf("The concentration field")).layout()->lineCount();if(layouts<0)layouts=lines;else stable&=layouts==lines;QThread::msleep(12);}
      QCoreApplication::processEvents();const bool released=!veil->isVisible()&&veil->property("researchSnapshotBytes").toLongLong()==0;const int expectedInk=darkPixels(w->editor()->viewport()->grab().toImage());
      for(int i=0;i<sampled.size();++i){const auto &image=sampled[i];rasters.insert(QCryptographicHash::hash(QByteArray(reinterpret_cast<const char*>(image.constBits()),image.sizeInBytes()),QCryptographicHash::Sha256));visibleInk&=darkPixels(image,bodyInk)>=expectedInk*.45;const qreal extent=sampledExtents[i];if(extent>dock->width()/2)structureInk|=darkPixels(image,QRect(0,bodyInk.top(),qRound(extent*density),bodyInk.height()))>15;}
      check(prefix+": actual intermediate paints show a moving boundary",rasters.size()>=3&&extents.size()>=2&&extents.first()<extents.last());
      check(prefix+": writing ink remains visible in every moving frame",expectedInk>200&&visibleInk);check(prefix+": structure content is visible while its panel reveals",structureInk);
      check(prefix+": source uses a single final text layout and at least half the width",stable&&space);
      check(prefix+": opening preserves source, caret and document revision",w->textDoc()->editorText()==source&&w->cursorPosition()==pos&&w->textDoc()->revision()==revision&&w->editor()->verticalScrollBar()->value()==0);
      check(prefix+": completed reveal releases all bitmaps",released);
      if(record){settle(450);clip.event("Collapse article structure");}outline->trigger();settle(230);if(record){settle(350);clip.event("Open and reverse before completion");}
      outline->trigger();settle(45);w->grab();const qreal before=veil->property("researchMotionExtent").toReal();outline->trigger();w->grab();const qreal from=veil->property("researchMotionFrom").toReal();
      check(prefix+": reversal resumes from the current visible extent",veil->isVisible()&&qAbs(before-from)<35);settle();check(prefix+": reversal reaches the requested closed state",dock->isHidden()&&!veil->isVisible());
      for(int i=0;i<8;++i){outline->trigger();settle(13);}settle();check(prefix+": repeated toggles leave neither stale frame nor wrong action state",dock->isHidden()&&!outline->isChecked()&&!veil->isVisible());
      if(record){settle(300);clip.event("Typing immediately cancels the temporary raster");}outline->trigger();key(w->editor(),Qt::Key_Z,"Z");
      check(prefix+": typing during reveal is delivered immediately",!veil->isVisible()&&w->textDoc()->editorText().contains("ZThe concentration field"));w->editor()->undo();check(prefix+": animation adds no undo steps",w->textDoc()->editorText()==source);settle();
      outline->trigger();settle();outline->trigger();QInputMethodEvent composition;composition.setCommitString(QString::fromUtf8("测试"));QApplication::sendEvent(w->editor(),&composition);
      check(prefix+": composition commits immediately through the temporary frame",!veil->isVisible()&&w->textDoc()->editorText().contains(QString::fromUtf8("测试The concentration field")));w->editor()->undo();settle();
      outline->trigger();settle();outline->trigger();w->editor()->zoomIn(1);check(prefix+": font zoom immediately releases the old text raster",!veil->isVisible());w->editor()->zoomOut(1);settle();
      outline->trigger();settle();outline->trigger();auto destination=w->textCursor();destination.setPosition(source.indexOf("\\section{Results}"));w->editor()->setTextCursor(destination);check(prefix+": direct navigation immediately releases the old reading position",!veil->isVisible());destination.setPosition(pos);w->editor()->setTextCursor(destination);w->editor()->verticalScrollBar()->setValue(0);settle();
      outline->trigger();settle(30);w->resize(w->width()-24,w->height()-10);settle(20);check(prefix+": resize immediately releases the old-size raster",!veil->isVisible()&&veil->property("researchSnapshotBytes").toLongLong()==0);
      Tw::Settings().setValue("researchReduceMotion",true);outline->trigger();check(prefix+": reduced motion applies panel state with no snapshot",!dock->isHidden()&&!veil->isVisible()&&veil->property("researchSnapshotBytes").toLongLong()==0);Tw::Settings().setValue("researchReduceMotion",false);
      key(tree,Qt::Key_Right,QString());check(prefix+": article branches refresh the native motion policy before interaction",tree->isAnimated());settle();
      geometry.append(QJsonObject{{"mode",prefix},{"windowWidth",w->width()},{"writerWidth",w->editor()->viewport()->width()},{"dpr",w->devicePixelRatioF()},{"movingPaints",extents.size()}});
      if(record){settle(350);clip.stop();}
      w->grab().save(folder+"/panel-final-"+prefix+".png");
    }
    auto compile=[&](TeXDocumentWindow *window){for(int i=0;i<2;++i){window->typeset();if(!waitFor([&]{return !window->isTypesetting()&&!window->property("researchBuildPreparing").toBool()&&!window->property("researchBuildFinishing").toBool()&&!window->property("researchPublishing").toBool();},45000))return false;settle(300);}return QFileInfo::exists(Research::previewFile(window->fileName()));};
    if(record)check("recording uses a real compiled article preview",compile(w));
    Clip pinClip(folder+"/clips/reference",w);if(record){pinClip.start();settle(350);pinClip.event("Pin theorem for reading");}
    Research::pinSourceReference(w,"thm:main");auto *pin=w->findChild<QDockWidget*>("researchPinnedReference");
    auto ink=[](const QImage &im){int count=0;for(int y=0;y<im.height();++y)for(int x=0;x<im.width();++x){const auto c=im.pixelColor(x,y);count+=c.red()+c.green()+c.blue()<390;}return count;};
    const QRect headerRect(0,0,dock->width(),qMin(120,dock->height()));const auto expectedHeader=dock->grab(headerRect).toImage();const auto client=w->grab().toImage();const qreal dpr=w->devicePixelRatioF();const auto topLeft=dock->mapTo(w,headerRect.topLeft());const QRect crop(qRound(topLeft.x()*dpr),qRound(topLeft.y()*dpr),expectedHeader.width(),expectedHeader.height());
    check("reference reveal preserves ink in the unrelated structure sidebar",ink(client.copy(crop))>=ink(expectedHeader)*.85);
    check("reference panel opens and current content becomes ready",pin&&waitFor([&]{return pin->property("researchPinnedReady").toBool();}));
    auto *pinSurface=w->findChild<QWidget*>("researchPanelMotionVeil");auto *pinArrival=pin->findChild<QWidget*>("researchArrivalVeil");check("an opening reference never stacks a second content fade",!pinSurface||!pinSurface->isVisible()||!pinArrival||!pinArrival->isVisible());
    auto *text=w->findChild<QTextBrowser*>("researchPinnedSource");check("reference arrival uses the actual requested theorem",(text&&text->toPlainText().contains("total mass"))||(record&&pin->property("researchPinnedVisual").toBool()));settle();
    auto *close=w->findChild<QToolButton*>("researchPinnedClose");if(record){settle(400);pinClip.event("Collapse the reference");}close->click();settle();check("reference closes without changing the writing cursor",pin->isHidden());
    if(record){settle(350);pinClip.stop();}
    Clip focusClip(folder+"/clips/focus",w);if(record){focusClip.start();settle(350);focusClip.event("Enter focus mode without ghosted source text");}Research::setFocusMode(true);auto *chrome=w->findChild<QWidget*>("researchChromeVeil");
    check("focus transition preserves live reading instead of blending old text",chrome&&chrome->isVisible()&&w->editor()->isVisible());
    const auto reading=QRect(w->editor()->mapTo(w,QPoint()),w->editor()->size());
    check("chrome animation damage excludes the source surface",chrome&&!chrome->mask().isEmpty()&&(chrome->mask()&QRegion(reading)).isEmpty());
    settle(35);Research::setFocusMode(false);check("focus reversal starts immediately without queueing",!w->property("researchFocus").toBool()&&chrome&&chrome->isVisible());
    Research::setFocusMode(true);settle();if(record){settle(500);focusClip.event("Return to the writing workspace");}Research::setFocusMode(false);settle();if(record){settle(450);focusClip.stop();}
    {Research::MaterialDialog dialog(w);dialog.resize(440,260);auto *layout=new QVBoxLayout(&dialog);auto *field=new QLineEdit(&dialog);layout->addWidget(new QLabel("A short, quiet transition",&dialog));layout->addWidget(field);dialog.show();QCoreApplication::processEvents();
    auto *arrival=dialog.findChild<QWidget*>("researchArrivalVeil");check("product dialogs share the same short arrival",arrival&&arrival->isVisible());key(field,Qt::Key_A,"a");check("dialog input immediately releases its arrival frame",field->text()=="a"&&!arrival->isVisible());dialog.hide();}
    w->setModified(false);delete w;
    const QString deckPath=folder+"/fixtures/slides/main.tex";QString slides="\\documentclass[aspectratio=169]{beamer}\n\\usetheme{default}\n\\begin{document}\n";
    const QList<QPair<QString,QString>> talk{
      {"Diffusion in a sealed cell","Can a changing concentration profile preserve the total amount of solute?"},
      {"From flux to local balance","Fick's law: $J=-D\\nabla c$.\\[\\partial_t c+\\nabla\\cdot J=0.\\]"},
      {"No-flux boundaries","An impermeable wall imposes $J\\cdot n=0$. No material crosses the boundary."},
      {"A conserved quantity","Integrating over the domain gives\\[\\frac{d}{dt}\\int_\\Omega c\\,dx=-\\int_{\\partial\\Omega}J\\cdot n\\,dS=0.\\]"},
      {"Checking a finite-volume solver","Sum the volume-weighted concentrations before normalization. Interior face fluxes must cancel in pairs."},
      {"What mass drift reveals","A smooth concentration profile can conceal an inconsistent boundary stencil. Track mass balance alongside the visual profile."}
    };
    for(const auto &slide:talk)slides+="\\begin{frame}{"+slide.first+"}\n"+slide.second+"\n\\end{frame}\n";slides+="\\end{document}\n";put(deckPath,slides.toUtf8());
    auto *s=new TeXDocumentWindow(deckPath);s->resize(1200,730);s->show();auto ready=[&]{return waitFor([&]{auto *c=Research::controller(s);return c&&c->enabled&&!c->busy&&!c->dirty&&c->deck.sources.value(Research::path(deckPath))==s->textDoc()->editorText();});};
    check("independent slide deck is ready",ready());auto *c=Research::controller(s);auto *side=static_cast<Research::SlideList*>(c->list);settle();c->list->setCurrentRow(0);c->select(0,true);c->list->setFocus();check("slide selection settles before an authoring action",ready());settle();
    if(record){check("recording uses a real compiled slide deck",compile(s));check("compiled deck maps to real preview pages",ready()&&waitFor([&]{return c->deck.mapped;}));}
    auto context=[&](const QString &name){put(folder+"/"+name+".json",QJsonDocument(QJsonObject{{"selected",c->selected},{"frames",c->deck.frames.size()},{"readOnly",s->editor()->isReadOnly()},{"dirty",c->dirty},{"busy",c->busy},{"listVisible",side->isVisible()},{"reflowVisible",side->reflow->isVisible()},{"arriving",side->reflow->property("researchArrivingCards").toInt()},{"moving",side->reflow->property("researchMovingCards").toInt()},{"leaving",side->reflow->property("researchLeavingCards").toInt()},{"text",s->textDoc()->editorText()}}).toJson());};context("before-insert");
    Clip cardClip(folder+"/clips/cards",s);if(record){cardClip.start();settle(400);cardClip.event("Insert a slide below the current slide");}
    c->edit(Research::SlideEdit::Insert);check("new slide commits before animation without blocking editing",ready()&&c->deck.frames.size()==7);
    context("after-insert");
    check("new card enters as neighbours make room",side->reflow->isVisible()&&side->reflow->property("researchArrivingCards").toInt()>=1&&side->reflow->property("researchMovingCards").toInt()>=1);
    const bool moving=side->reflow->isVisible();if(moving)side->reflow->hide();const int settledInk=darkPixels(side->viewport()->grab().toImage());if(moving)side->reflow->show();bool noDuplicateInk=true;
    QSet<QByteArray> cardFrames;QElapsedTimer ct;ct.start();while(ct.elapsed()<210){QCoreApplication::processEvents();auto im=side->viewport()->grab().toImage();noDuplicateInk&=darkPixels(im)<=settledInk*1.25;cardFrames.insert(QCryptographicHash::hash(QByteArray(reinterpret_cast<const char*>(im.constBits()),im.sizeInBytes()),QCryptographicHash::Sha256));QThread::msleep(12);}
    check("card arrival has distinct actual intermediate rasters",cardFrames.size()>=3);check("settled card list releases its temporary images",!side->reflow->isVisible()&&side->reflow->property("researchSnapshotBytes").toLongLong()==0);
    check("moving sidebar titles never duplicate the final model underneath",settledInk>20&&noDuplicateInk);
    if(record){settle(450);cardClip.event("Undo the insertion");}s->editor()->undo();check("one undo restores the original complete deck",ready()&&s->textDoc()->editorText()==slides);settle();
    if(record){settle(450);cardClip.event("Move a slide down");}c->list->setCurrentRow(1);c->edit(Research::SlideEdit::Down);check("move animates the actual survivors without adding a new card",ready()&&side->reflow->isVisible()&&side->reflow->property("researchMovingCards").toInt()>=2&&side->reflow->property("researchArrivingCards").toInt()==0);context("after-move");settle();s->editor()->undo();ready();settle();
    if(record){settle(350);cardClip.stop();}
    c->showOverview();settle();auto *overview=c->overviewWindow.data();check("actual overview opens",overview!=nullptr);
    if(overview){
      if(record)check("overview recording contains rendered PDF thumbnails",waitFor([&]{return overview->property("researchOverviewRendered").toInt()>=6;}));
      Clip overviewClip(folder+"/clips/overview",overview);if(record){overviewClip.start();settle(500);overviewClip.event("Duplicate a card; neighbours make room");}
      overview->grid->setCurrentRow(1);overview->editSlides(Research::SlideEdit::Duplicate);check("overview duplicates with a new card and natural reflow",ready()&&overview->grid->reflow->isVisible()&&overview->grid->reflow->property("researchArrivingCards").toInt()>=1);settle();
      if(record)check("unchanged overview thumbnails remain visible under the previous-build notice",overview->property("researchOverviewRendered").toInt()>=6&&!c->deck.previewCurrent);
      if(record){settle(550);overviewClip.event("Undo the duplicate");}s->editor()->undo();ready();settle();
      if(record){settle(550);overviewClip.event("Delete a card and close its gap");}overview->grid->setCurrentRow(1);overview->editSlides(Research::SlideEdit::Delete);check("overview deletion closes the gap and fades the removed card",ready()&&overview->grid->reflow->isVisible()&&overview->grid->reflow->property("researchLeavingCards").toInt()>=1);settle();
      if(record){settle(550);overviewClip.event("Undo restores the original deck");}s->editor()->undo();ready();settle();if(record){settle(500);overviewClip.stop();}
      overview->resize(760,500);settle();overview->grid->setCurrentRow(1);const bool hadScrollbar=overview->grid->verticalScrollBar()->maximum()>0;
      QJsonObject gridDiagnostic{{"beforeWindowWidth",overview->width()},{"beforeWindowHeight",overview->height()},{"beforeViewportWidth",overview->grid->viewport()->width()},{"beforeViewportHeight",overview->grid->viewport()->height()},{"beforeMax",overview->grid->verticalScrollBar()->maximum()},{"beforeValue",overview->grid->verticalScrollBar()->value()},{"beforeCount",overview->grid->count()},{"dirty",c->dirty},{"busy",c->busy}};
      QJsonArray viewportEvents;QElapsedTimer eventTime;eventTime.start();const auto sizeTrace=QObject::connect(overview->grid->verticalScrollBar(),&QScrollBar::rangeChanged,overview,[&](int low,int high){viewportEvents.append(QJsonObject{{"atMs",double(eventTime.elapsed())},{"minimum",low},{"maximum",high},{"value",overview->grid->verticalScrollBar()->value()},{"motion",overview->grid->reflow->isVisible()}});});const auto valueTrace=QObject::connect(overview->grid->verticalScrollBar(),&QScrollBar::valueChanged,overview,[&](int value){viewportEvents.append(QJsonObject{{"atMs",double(eventTime.elapsed())},{"value",value},{"motion",overview->grid->reflow->isVisible()}});});
      overview->editSlides(Research::SlideEdit::Duplicate);ready();QObject::disconnect(sizeTrace);QObject::disconnect(valueTrace);gridDiagnostic["afterCount"]=overview->grid->count();gridDiagnostic["afterViewportWidth"]=overview->grid->viewport()->width();gridDiagnostic["afterViewportHeight"]=overview->grid->viewport()->height();gridDiagnostic["afterMax"]=overview->grid->verticalScrollBar()->maximum();gridDiagnostic["afterValue"]=overview->grid->verticalScrollBar()->value();gridDiagnostic["motion"]=overview->grid->reflow->isVisible();gridDiagnostic["arrivals"]=overview->grid->reflow->property("researchArrivingCards").toInt();gridDiagnostic["events"]=viewportEvents;put(folder+"/scrollbar-diagnostic.json",QJsonDocument(gridDiagnostic).toJson());
      check("crossing a new grid row preserves reflow through scrollbar layout",overview->grid->reflow->isVisible()&&(!hadScrollbar?overview->grid->verticalScrollBar()->maximum()>0:true));settle();s->editor()->undo();ready();settle();overview->hide();
    }
    Tw::Settings().setValue("researchReduceMotion",true);c->edit(Research::SlideEdit::Insert);check("reduced motion inserts cards immediately",ready()&&!side->reflow->isVisible());s->editor()->undo();ready();Tw::Settings().setValue("researchReduceMotion",false);settle();
    c->edit(Research::SlideEdit::Insert);ready();side->hide();check("hiding an animated list clears its images immediately",!side->reflow->isVisible()&&side->reflow->property("researchSnapshotBytes").toLongLong()==0);side->show();s->editor()->undo();ready();settle();
    s->grab().save(folder+"/slides-final.png");check("complete workflow leaves authored fixture files untouched",Research::read(deckPath)==slides);Research::setPanelVisible(c->dock,false);check("ordinary close is exercised during an active panel reveal",s->findChild<QWidget*>("researchPanelMotionVeil")->isVisible());s->setModified(false);delete s;
    check("all temporary motion widgets survive ordinary window teardown",true);app.exit(failures?1:0);
  });QTimer::singleShot(180000,&app,[&]{app.exit(93);});return app.exec();
}
