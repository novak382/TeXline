#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "TeXDocumentWindow.h"
#include "PDFDocumentWindow.h"
#include "CompletingEdit.h"
#include "FindDialog.h"
#include "ResearchAppearance.h"
#include "ResearchCollaboration.h"
#include "PDFDocumentScene.h"
#include "Settings.h"
#include <QAction>
#include <QAbstractNativeEventFilter>
#include <QDateTime>
#include <QDir>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QGraphicsOpacityEffect>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QMenuBar>
#include <QPainter>
#include <QSaveFile>
#include <QScrollBar>
#include <QStatusBar>
#include <QTextBrowser>
#include <QThread>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <functional>
#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#endif

static bool waitFor(const std::function<bool()> &ready,int limit=5000) {
  QElapsedTimer clock;clock.start();
  while(!ready()&&clock.elapsed()<limit){QCoreApplication::processEvents();QThread::msleep(5);}return ready();
}
static void settle(int ms=150){QElapsedTimer clock;clock.start();waitFor([&]{return clock.elapsed()>=ms;},ms+500);}
static bool put(const QString &path,const QByteArray &data){QDir().mkpath(QFileInfo(path).absolutePath());QSaveFile f(path);return f.open(QIODevice::WriteOnly)&&f.write(data)==data.size()&&f.commit();}
static QJsonArray pointerReveals;
static void reveal(QToolButton *button){const bool enabled=button->isEnabled(),visible=button->isVisible();const QPoint center=button->rect().center();for(const auto point:{center-QPoint(6,0),center}){QMouseEvent move(QEvent::MouseMove,QPointF(point),QPointF(button->mapToGlobal(point)),Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(button,&move);}settle(140);pointerReveals.append(QJsonObject{{"button",button->objectName()},{"enabledBefore",enabled},{"visibleBefore",visible},{"enabledAfter",button->isEnabled()},{"visibleAfter",button->isVisible()},{"explicitMovementPixels",6}});}
static void press(QToolButton *button){reveal(button);button->click();}
static bool openMenu(QToolButton *button,const QString &capture=QString()){reveal(button);bool opened=false;auto c=QObject::connect(button->menu(),&QMenu::aboutToShow,[&]{opened=true;QTimer::singleShot(150,button->menu(),[button,capture]{if(!capture.isEmpty())button->menu()->grab().save(capture);button->menu()->hide();});});button->click();settle();QObject::disconnect(c);return opened;}
static int entries(QMenu *menu){int count=0;for(auto *a:menu->actions())if(!a->isSeparator()&&a->isVisible())++count;return count;}
static bool reachable(QMenu *menu,QAction *action){if(!action)return false;for(auto *a:menu->actions())if(a==action||(a->menu()&&reachable(a->menu(),action)))return true;return false;}
static int visibleButtons(QMainWindow *window){int count=0;for(auto *b:window->findChildren<QToolButton*>())if(b->window()==window&&b->isVisible())++count;return count;}
static bool lightPixel(const QColor &color){return color.alpha()==255&&qMin(color.red(),qMin(color.green(),color.blue()))>=176;}
static bool lightImage(const QImage &image){if(image.isNull())return false;for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x)if(!lightPixel(image.pixelColor(x,y)))return false;return true;}
static bool lightEdges(const QImage &image){if(image.isNull())return false;for(int x=0;x<image.width();++x)if(!lightPixel(image.pixelColor(x,image.height()-1)))return false;for(int y=0;y<image.height();++y)if(!lightPixel(image.pixelColor(0,y)))return false;return true;}
static bool nativeBorder(QMainWindow *window,COLORREF expected){
#ifdef Q_OS_WIN
  COLORREF color=0;const HRESULT result=DwmGetWindowAttribute(reinterpret_cast<HWND>(window->winId()),static_cast<DWMWINDOWATTRIBUTE>(34),&color,sizeof(color));return FAILED(result)||color==expected;
#else
  Q_UNUSED(window);Q_UNUSED(expected);return true;
#endif
}
static void key(QWidget *target,int code,Qt::KeyboardModifiers modifiers){QKeyEvent press(QEvent::KeyPress,code,modifiers);QApplication::sendEvent(target,&press);QKeyEvent release(QEvent::KeyRelease,code,modifiers);QApplication::sendEvent(target,&release);settle();}
// Raised fixture windows must not turn unrelated human input into edits.
// The fixture's explicitly delivered Qt mouse/key events are still exercised.
class FixtureInput final : public QAbstractNativeEventFilter {
public:
  int ignored=0;
  bool nativeEventFilter(const QByteArray &type,void *message,qintptr *result) override {
    if(!message||(type!="windows_generic_MSG"&&type!="windows_dispatcher_MSG"))return false;
    const auto *m=static_cast<MSG*>(message);
    if(!QWidget::find(reinterpret_cast<WId>(m->hwnd)))return false;
    if((m->message>=WM_KEYFIRST&&m->message<=WM_KEYLAST)||(m->message>=WM_LBUTTONDOWN&&m->message<=WM_MBUTTONDBLCLK)){
      ++ignored;if(result)*result=0;return true;
    }
    return false;
  }
};
int main(int argc,char **argv){
  const QString report=QString::fromLocal8Bit(argv[1]),reference=QString::fromLocal8Bit(argv[2]);argc=1;
  TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);app.applyTranslation("zh_CN");
  FixtureInput input;app.installNativeEventFilter(&input);
  QTimer::singleShot(100,&app,[&]{
    QJsonArray checks;int failures=0;const QString folder=QFileInfo(report).absolutePath(),fixture=folder+"/fixture-"+QDateTime::currentDateTimeUtc().toString("yyyyMMddHHmmsszzz"),path=fixture+"/main.tex";
    auto check=[&](const QString &name,bool pass){checks.append(QJsonObject{{"name",name},{"passed",pass}});if(!pass)++failures;put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures}}).toJson());};
    Tw::Settings().setValue("openPDFwithTeX",false);Tw::Settings().setValue("researchFocusMode",false);
    put(path,"\\documentclass{article}\n\\begin{document}\n\\section{Introduction}\nConsider a symmetric network with $n$ nodes\nand adjacency matrix $A$. We write\n\\begin{equation}\n A = \\Omega + W.\n\\label{eq:network-model}\n\\end{equation}\n% A clear model before its assumptions.\n\\end{document}\n");
    QFile::copy(reference,fixture+"/main.pdf");auto *source=new TeXDocumentWindow(path);source->resize(1000,760);source->show();source->goToPreview();settle(400);auto *pdf=source->pdfDocument();
    check("PDF remains a separate linked window",pdf&&pdf->isWindow());
    if(!pdf){source->setModified(false);delete source;app.exit(1);return;}
    pdf->resize(1000,760);pdf->show();settle();
    auto *bar=source->findChild<QToolBar*>("toolBar_run"),*pdfBar=pdf->findChild<QToolBar*>("toolBar");
    auto *more=source->findChild<QToolButton*>("researchFocusMore"),*pdfMore=pdf->findChild<QToolButton*>("researchFocusMore");
    auto *normalMore=source->findChild<QToolButton*>("researchMore");const int normalTop=source->editor()->mapToGlobal(QPoint()).y()-source->frameGeometry().top();
    auto *view=pdf->widget();const int ordinaryFrame=view->frameStyle();const auto ordinaryBackground=view->backgroundRole();
    auto *play=source->findChild<QToolButton*>("researchFocusCompile"),*pdfZoom=pdf->findChild<QToolButton*>("researchFocusZoom");
    check("ordinary menus have eleven task entries including wrap in source and six in PDF",entries(normalMore->menu())==11&&normalMore->menu()->actions().contains(source->findChild<QAction*>("actionWrap_Lines"))&&entries(pdf->findChild<QMenu*>("researchMoreMenu"))==6);
    check("file editing and collaboration commands remain reachable through groups",reachable(normalMore->menu(),source->findChild<QAction*>("actionSave"))&&reachable(normalMore->menu(),source->findChild<QAction*>("actionReplace"))&&source->findChild<QMenu*>("researchCollaborationMenu")->actions().size()==2);
    check("default menus open with grouped entries",openMenu(normalMore,folder+"/source-menu-default.png")&&openMenu(pdf->findChild<QToolButton*>("researchMore"),folder+"/pdf-menu-default.png"));
    check("ordinary mode keeps both toolbars and hides floating controls",bar->isVisible()&&pdfBar->isVisible()&&more->isHidden()&&pdfMore->isHidden());
    Research::setFocusMode(true);settle();
    check("PDF focus removes the sunken frame and dark palette role",view->frameWidth()==0&&view->frameShape()==QFrame::NoFrame&&view->backgroundRole()==QPalette::Base);
    check("focus PDF uses the complete panel without a reserved control row",view->geometry().top()==0&&pdf->contentsMargins().top()==0);
    check("both mode transitions reach their final layout before checking the paper surface",waitFor([&]{auto *sourceVeil=source->findChild<QWidget*>("researchChromeVeil"),*pdfVeil=pdf->findChild<QWidget*>("researchChromeVeil");return sourceVeil&&pdfVeil&&sourceVeil->isHidden()&&pdfVeil->isHidden();},600));
    // Hidden PDF chrome deliberately exposes page content (including colored
    // covers). Reveal it before testing the opaque white control surface.
    reveal(pdfZoom);
    const auto focusChrome = pdf->grab().toImage();
    check("focus PDF control row is opaque white without a black backing-store band",focusChrome.pixelColor(3,int(24*focusChrome.devicePixelRatio()))==QColor(Qt::white));
    check("paper suppresses the native border without recreating frameless Qt windows",nativeBorder(source,0xfffffffe)&&nativeBorder(pdf,0xfffffffe)&&!(source->windowFlags()&Qt::FramelessWindowHint)&&!(pdf->windowFlags()&Qt::FramelessWindowHint));
    view->zoomFitWindow();settle();auto canvas=view->viewport()->grab().toImage();
    check("empty PDF canvas is opaque white beside the fitted page",canvas.pixelColor(1,canvas.height()/2)==QColor(Qt::white));
    pdf->statusBar()->clearMessage();pdf->grab().save(folder+"/pdf-focus-fit.png");
    view->zoomFitWidth();view->zoomBy(1.5);settle();auto *horizontal=view->horizontalScrollBar(),*vertical=view->verticalScrollBar();horizontal->setValue(horizontal->minimum());vertical->setValue(vertical->minimum());settle();
    check("both scroll directions remain available at enlarged reading scale",horizontal->isVisible()&&vertical->isVisible()&&horizontal->maximum()>0&&vertical->maximum()>0);
    check("horizontal track and handle have no black frame",lightImage(horizontal->grab().toImage()));
    check("vertical track and handle have no black frame",lightImage(vertical->grab().toImage()));
    check("PDF left and bottom edges have no surrounding grey or black frame",lightEdges(view->grab().toImage()));
    horizontal->grab().save(folder+"/horizontal-clean.png");vertical->grab().save(folder+"/vertical-clean.png");pdf->grab().save(folder+"/pdf-focus-large.png");
    bool scrollClean=true;const qreal readingScale=view->zoomLevel();QElapsedTimer scrollTime;scrollTime.start();
    for(int n=0;n<16;++n){horizontal->setValue((n*193)%qMax(1,horizontal->maximum()));vertical->setValue((n*7693)%qMax(1,vertical->maximum()));settle(10);scrollClean&=lightImage(horizontal->grab().toImage())&&lightImage(vertical->grab().toImage());}
    const qint64 scrollMs=scrollTime.elapsed();
    check("rapid two-axis scrolling retains clean tracks and exact zoom",scrollClean&&view->zoomLevel()==readingScale);
    put(folder+"/scroll-observation.json",QJsonDocument(QJsonObject{{"samples",16},{"elapsedMs",scrollMs},{"devicePixelRatio",view->devicePixelRatioF()},{"zoomPreserved",view->zoomLevel()==readingScale},{"tracksRemainLight",scrollClean}}).toJson());
    view->setPageMode(QtPDF::PDFDocumentView::PageMode_Presentation);settle();
    check("actual PDF presentation still uses its dedicated black background",view->backgroundBrush().color()==QColor(Qt::black));
    view->setPageMode(QtPDF::PDFDocumentView::PageMode_OneColumnContinuous);view->goToPage(QtPDF::PDFDocumentView::size_type(0));view->zoomFitContentWidth();settle();
    check("minimal mode folds entire editor and PDF toolbar rows",bar->isHidden()&&pdfBar->isHidden());
    reveal(play);reveal(pdfZoom);check("movement reveals exactly five controls in source and PDF",visibleButtons(source)==5&&visibleButtons(pdf)==5);
    check("source play keeps its original action while PDF has a direct zoom button",play&&pdfZoom&&play->defaultAction()==source->findChild<QAction*>("actionTypeset")&&!pdf->findChild<QToolButton*>("researchFocusCompile")&&play->toolButtonStyle()==Qt::ToolButtonIconOnly&&pdfZoom->toolButtonStyle()==Qt::ToolButtonIconOnly&&!play->menu()&&!pdfZoom->menu());
    const QList<QAction*> fitModes{pdf->findChild<QAction*>("actionFit_to_Window"),pdf->findChild<QAction*>("actionFit_to_Width"),pdf->findChild<QAction*>("actionFit_to_Content_Width")};
    const QList<QIcon> fitIcons{QIcon(":/research/icons/fit-page.svg"),QIcon(":/research/icons/fit-width.svg"),QIcon(":/research/icons/fit-content.svg")};
    auto fitIconMatches=[&](int index){return pdfZoom->icon().pixmap(pdfZoom->iconSize()).toImage()==fitIcons[index].pixmap(pdfZoom->iconSize()).toImage();};
    bool distinctIcons=true;
    for(int size:{18,24,36,48}){const QSize pixels(size,size);const auto search=QIcon(":/research/icons/find.svg").pixmap(pixels).toImage();for(int index=0;index<3;++index){const auto rendered=fitIcons[index].pixmap(pixels).toImage();distinctIcons&=!rendered.isNull()&&rendered!=search;for(int other=0;other<index;++other)distinctIcons&=rendered!=fitIcons[other].pixmap(pixels).toImage();}}
    check("three vector fit icons stay distinct from search at all toolbar pixel sizes",distinctIcons&&pdfZoom->iconSize()==QSize(18,18));
    QImage iconProof(QSize(360,112),QImage::Format_ARGB32_Premultiplied);iconProof.fill(Qt::white);
    {QPainter painter(&iconProof);painter.setFont(QFont("Microsoft YaHei UI",10));painter.setPen(QColor("#596168"));const QStringList names{QStringLiteral("整页"),QStringLiteral("页面宽度"),QStringLiteral("正文宽度")};for(int index=0;index<3;++index){fitIcons[index].paint(&painter,QRect(index*120+42,12,36,36));fitIcons[index].paint(&painter,QRect(index*120+51,60,18,18));painter.drawText(QRect(index*120,86,120,20),Qt::AlignCenter,names[index]);}}iconProof.save(folder+"/fit-icons-proof.png");
    check("all original fit modes and advanced zoom commands remain available",reachable(pdfMore->menu(),fitModes[0])&&reachable(pdfMore->menu(),fitModes[1])&&reachable(pdfMore->menu(),fitModes[2])&&reachable(pdfMore->menu(),pdf->findChild<QAction*>("actionZoom_In"))&&reachable(pdfMore->menu(),pdf->findChild<QAction*>("actionZoom_Out"))&&reachable(pdfMore->menu(),pdf->findChild<QAction*>("actionActual_Size")));
    QList<qreal> expectedZoom;
    for(auto *action:fitModes){action->trigger();settle();expectedZoom.append(view->zoomLevel());}
    bool defaultsMatch=true;const int defaultOptions[]{3,2,5};
    for(int index=0;index<3;++index){Tw::Settings().setValue("scaleOption",defaultOptions[index]);defaultsMatch&=QMetaObject::invokeMethod(pdf,"setDefaultScale",Qt::DirectConnection);settle();defaultsMatch&=fitIconMatches(index)&&qAbs(view->zoomLevel()-expectedZoom[index])<0.00001;}
    check("all three saved opening fits select the matching icon and real scale",defaultsMatch);
    press(pdfZoom);settle();
    check("first zoom click selects whole-page fit and its page-in-frame icon",qAbs(view->zoomLevel()-expectedZoom[0])<0.00001&&fitIconMatches(0));
    pdfZoom->grab().save(folder+"/fit-control-whole.png");
    pdf->grab().save(folder+"/pdf-mode-whole.png");
    press(pdfZoom);settle();auto *page=static_cast<QtPDF::PDFDocumentScene*>(view->scene())->pageAt(view->currentPage());const auto pageWidth=view->mapFromScene(page->sceneBoundingRect()).boundingRect().width();
    check("second zoom click fits page width and shows its width-marker icon",qAbs(view->zoomLevel()-expectedZoom[1])<0.00001&&qAbs(pageWidth-view->viewport()->width())<20&&fitIconMatches(1));
    pdfZoom->grab().save(folder+"/fit-control-width.png");
    pdf->grab().save(folder+"/pdf-mode-width.png");
    press(pdfZoom);settle();
    check("third zoom click fills text width and shows its text-area icon",qAbs(view->zoomLevel()-expectedZoom[2])<0.00001&&view->frameWidth()==0&&lightImage(horizontal->grab().toImage())&&fitIconMatches(2));
    pdfZoom->grab().save(folder+"/fit-control-content.png");
    pdf->grab().save(folder+"/pdf-mode-content.png");
    int triggered[3]{0,0,0};QList<QMetaObject::Connection> modeConnections;
    for(int index=0;index<fitModes.size();++index)modeConnections.append(QObject::connect(fitModes[index],&QAction::triggered,[&,index]{++triggered[index];}));
    bool cycleCorrect=true;
    for(int step=0;step<6;++step){press(pdfZoom);settle();cycleCorrect&=qAbs(view->zoomLevel()-expectedZoom[step%3])<0.00001&&fitIconMatches(step%3);for(auto *window:QApplication::topLevelWidgets())if(qobject_cast<QMenu*>(window)&&window->isVisible())cycleCorrect=false;}
    for(const auto &connection:modeConnections)QObject::disconnect(connection);
    check("successive clicks repeat exactly three fit modes without opening a menu",cycleCorrect&&triggered[0]==2&&triggered[1]==2&&triggered[2]==2);
    fitModes[0]->trigger();settle();press(pdfZoom);settle();
    check("selecting an original fit action synchronizes the next click and icon",qAbs(view->zoomLevel()-expectedZoom[1])<0.00001&&fitIconMatches(1));
    check("zoom confirmation uses shared fading and leaves both toolbar rows hidden",waitFor([&]{return pdf->statusBar()->currentMessage().isEmpty();},3000)&&bar->isHidden()&&pdfBar->isHidden()&&pdf->contentsMargins().top()==0);
    fitModes[2]->trigger();settle();
    Research::setFocusMode(false);settle();
    QToolButton *normalSearch=nullptr;for(auto *button:pdfBar->findChildren<QToolButton*>())if(button->accessibleName()==QStringLiteral("查找"))normalSearch=button;
    const qreal zoomBeforeSearch=view->zoomLevel();
    bool searchDialogOpened=false;int findTriggered=0;auto findConnection=QObject::connect(pdf->findChild<QAction*>("actionFind"),&QAction::triggered,[&]{++findTriggered;});
    if(normalSearch){QTimer::singleShot(150,&app,[&]{for(auto *window:QApplication::topLevelWidgets())if(auto *dialog=qobject_cast<PDFFindDialog*>(window);dialog&&dialog->isVisible()){searchDialogOpened=true;dialog->reject();}});normalSearch->click();settle();}
    QObject::disconnect(findConnection);
    put(folder+"/search-observation.json",QJsonDocument(QJsonObject{{"buttonFound",normalSearch!=nullptr},{"buttonEnabled",normalSearch&&normalSearch->isEnabled()},{"buttonVisible",normalSearch&&normalSearch->isVisible()},{"dialogOpened",searchDialogOpened},{"triggered",findTriggered},{"magnifierPreserved",normalSearch&&normalSearch->icon().pixmap(QSize(18,18)).toImage()==QIcon(":/research/icons/find.svg").pixmap(QSize(18,18)).toImage()},{"iconsDistinct",normalSearch&&normalSearch->icon().pixmap(QSize(18,18)).toImage()!=pdfZoom->icon().pixmap(QSize(18,18)).toImage()},{"fitIconPreserved",fitIconMatches(2)},{"zoomBefore",zoomBeforeSearch},{"zoomAfter",view->zoomLevel()}}).toJson());
    check("ordinary PDF search retains its magnifier and opens the real find dialog",normalSearch&&searchDialogOpened&&findTriggered==1&&normalSearch->icon().pixmap(QSize(18,18)).toImage()==QIcon(":/research/icons/find.svg").pixmap(QSize(18,18)).toImage()&&normalSearch->icon().pixmap(QSize(18,18)).toImage()!=pdfZoom->icon().pixmap(QSize(18,18)).toImage()&&fitIconMatches(2)&&view->zoomLevel()==zoomBeforeSearch);
    Research::setFocusMode(true);settle();fitModes[2]->trigger();settle();
    check("menu and status bars remain hidden by default",source->menuBar()->isHidden()&&pdf->menuBar()->isHidden()&&source->statusBar()->isHidden()&&pdf->statusBar()->isHidden());
    check("paper gains vertical writing space by removing the system caption",normalTop-(source->editor()->mapToGlobal(QPoint()).y()-source->frameGeometry().top())>=24);
    check("both menu entries have the same 32-pixel size and fixed left placement",more->size()==QSize(32,32)&&pdfMore->size()==more->size()&&more->x()==56&&pdfMore->x()==56&&more->y()==8&&pdfMore->y()==8);
    check("two controls share geometry an eight-pixel gap and sixteen-pixel inset",play->size()==more->size()&&pdfZoom->size()==more->size()&&play->x()==16&&pdfZoom->x()==16&&more->x()-play->x()-play->width()==8&&pdfMore->x()-pdfZoom->x()-pdfZoom->width()==8&&play->y()==more->y()&&pdfZoom->y()==pdfMore->y());
    check("source focus keeps wrap and removes duplicate Build while PDF retains it",entries(more->menu())==10&&more->menu()->actions().contains(source->findChild<QAction*>("actionWrap_Lines"))&&entries(pdfMore->menu())==7&&more->menu()!=normalMore->menu());
    check("focus menu preserves primary commands and direct file access",!more->menu()->actions().contains(source->findChild<QAction*>("actionTypeset"))&&more->menu()->actions().contains(source->findChild<QAction*>("actionGo_to_Preview"))&&more->menu()->actions().contains(source->findChild<QAction*>("actionFind"))&&!more->menu()->actions().contains(normalMore->menu()->menuAction())&&reachable(more->menu(),source->findChild<QAction*>("actionSave")));
    check("PDF menu retains source and view commands",pdfMore->menu()->actions().contains(pdf->findChild<QAction*>("actionGo_to_Source"))&&reachable(pdfMore->menu(),pdf->findChild<QAction*>("actionFit_to_Content_Width"))&&reachable(pdfMore->menu(),pdf->findChild<QAction*>("actionSelect_Text")));
    auto *engines=source->findChild<QMenu*>("researchEngineMenu");
    check("engine language and recovery remain available below the focus menu",engines&&reachable(more->menu(),engines->menuAction())&&reachable(more->menu(),source->findChild<QMenu*>("researchLanguageMenu")->menuAction())&&source->findChild<QMenu*>("researchFileMenu")->actions().last()->menu()->actions().size()==2);
    check("both floating entries open their small menus",openMenu(more,folder+"/source-menu-focus.png")&&openMenu(pdfMore,folder+"/pdf-menu-focus.png"));
    source->statusBar()->clearMessage();
    source->grab().save(folder+"/source-minimal.png");pdf->grab().save(folder+"/pdf-minimal.png");
    const QString text=source->editor()->toPlainText();const int cursor=source->cursorPosition();
    QJsonArray cursorStages;auto recordCursor=[&](const QString &stage){cursorStages.append(QJsonObject{{"stage",stage},{"cursor",source->cursorPosition()},{"ignoredPhysicalInput",input.ignored}});put(folder+"/cursor-stages.json",QJsonDocument(cursorStages).toJson());};recordCursor("before language");
    app.applyTranslation("en_US");settle();
    recordCursor("after English");
    check("language switch preserves the clean reading surface",view->frameWidth()==0&&view->backgroundRole()==QPalette::Base&&lightImage(horizontal->grab().toImage()));
    check("language switching preserves fit meaning and its selected icon",more->accessibleName()=="More"&&pdfZoom->accessibleName()=="Fit view"&&pdfZoom->toolTip()=="Fit view · Click to cycle"&&fitIconMatches(2)&&bar->isHidden()&&pdfBar->isHidden()&&!more->menu()->actions().contains(source->findChild<QAction*>("actionTypeset")));
    check("group and focus labels switch to English",source->findChild<QMenu*>("researchFileMenu")->title()=="File"&&source->findChild<QAction*>("researchFocusExit")->text()=="Exit focus"&&normalMore->menu()->title()=="Other commands");
    openMenu(more,folder+"/source-menu-focus-en.png");
    app.applyTranslation("zh_CN");
    recordCursor("after Chinese");
    source->resize(500,650);pdf->resize(500,650);settle();
    recordCursor("after narrow resize");
    check("narrow windows keep clean overflow tracks",lightImage(horizontal->grab().toImage())&&lightImage(vertical->grab().toImage()));
    check("narrow windows retain fixed left entries without clipping",more->geometry().right()<source->width()&&pdfMore->geometry().right()<pdf->width()&&more->x()==56&&play->x()==16&&play->y()==more->y());
    source->resize(1000,760);pdf->resize(1000,760);settle();
    source->raise();source->activateWindow();source->editor()->setFocus();settle();key(source->editor(),Qt::Key_F,Qt::ControlModifier);
    recordCursor("after Ctrl F");
    auto *find=source->findChild<QToolBar*>("researchFindBar");
    check("Ctrl F opens find on demand with the main toolbar still hidden",find&&find->isVisible()&&bar->isHidden());
    settle();reveal(more);check("temporary controls stay clear of the on-demand find row",find&&more->mapTo(source,QPoint()).y()>=find->mapTo(source,QPoint()).y()+find->height()&&more->y()==8);
    find->hide();settle();
    recordCursor("after closing find");
    check("closing find returns the lone entry to its usual corner",more->y()==8);
    view->goToPage(QtPDF::PDFDocumentView::size_type(0));settle(400);
    const double zoom=pdf->widget()->zoomLevel();const int x=pdf->widget()->horizontalScrollBar()->value(),y=pdf->widget()->verticalScrollBar()->value();
    source->collaboration()->review();settle(180);
    auto *message=source->findChild<QLabel*>("researchFocusMessage");
    check("external review feedback remains visible without a status bar",message&&message->isVisible()&&source->statusBar()->currentMessage().contains(QStringLiteral("没有待审阅")));
    check("review feedback still dismisses after its shared fade",waitFor([&]{return source->statusBar()->currentMessage().isEmpty();},5000));
    recordCursor("after feedback fade");
    put(folder+"/reading-state-observation.json",QJsonDocument(QJsonObject{{"textPreserved",text==source->editor()->toPlainText()},{"cursorBefore",cursor},{"cursorAfter",source->cursorPosition()},{"zoomBefore",zoom},{"zoomAfter",view->zoomLevel()},{"xBefore",x},{"xAfter",horizontal->value()},{"yBefore",y},{"yAfter",vertical->value()}}).toJson());
    check("folding chrome and dismissing feedback preserve text cursor and PDF reading state",text==source->editor()->toPlainText()&&cursor==source->cursorPosition()&&zoom==pdf->widget()->zoomLevel()&&x==pdf->widget()->horizontalScrollBar()->value()&&y==pdf->widget()->verticalScrollBar()->value());
    source->findChild<QAction*>("researchFocusExit")->trigger();settle();
    check("ordinary mode restores original frame palette and keeps the shared native border suppressed",view->frameStyle()==ordinaryFrame&&view->backgroundRole()==ordinaryBackground&&nativeBorder(pdf,0xfffffffe));
    check("ordinary mode restores both toolbars and removes floating entries",bar->isVisible()&&pdfBar->isVisible()&&more->isHidden()&&pdfMore->isHidden());
    source->findChild<QAction*>("actionFind")->trigger();settle();auto *console=source->findChild<QWidget*>("consoleTabs");if(console->isHidden())source->toggleConsoleVisibility();
    auto *dock=new QDockWidget("Fixture navigation",source);dock->setWidget(new QWidget(dock));source->addDockWidget(Qt::LeftDockWidgetArea,dock);dock->show();settle();
    Research::setFocusMode(true);settle();check("entering minimal mode folds navigation search and console",dock->isHidden()&&find->isHidden()&&console->isHidden());
    Research::setFocusMode(false);settle();check("leaving restores only panels folded by minimal mode",dock->isVisible()&&find->isVisible()&&console->isVisible());dock->hide();find->hide();console->hide();Research::setFocusMode(true);settle();
    source->raise();source->activateWindow();source->editor()->setFocus();settle();
    key(source->editor(),Qt::Key_T,Qt::ControlModifier);
    check("Ctrl T still starts real compilation without the toolbar",source->isTypesetting());
    check("hidden compile action and menu stay synchronized during compilation",source->findChild<QToolButton*>("researchCompile")->text()==QStringLiteral("停止")&&source->findChild<QAction*>("actionTypeset")->isEnabled()&&!more->menu()->actions().contains(source->findChild<QAction*>("actionTypeset"))&&bar->isHidden());
    reveal(play);reveal(pdfZoom);check("source shows its live stop icon while PDF retains direct zoom",play->icon().cacheKey()==source->findChild<QToolButton*>("researchCompile")->icon().cacheKey()&&pdfZoom->isVisible()&&!pdfZoom->menu()&&!pdf->findChild<QToolButton*>("researchFocusCompile"));
    check("real compilation completes and refreshes the separate preview",waitFor([&]{return !source->isTypesetting();},90000)&&!source->statusBar()->currentMessage().contains(QStringLiteral("未完成"))&&source->pdfDocument());
    check("completion keeps toolbars folded and the first source line below floating controls",bar->isHidden()&&pdfBar->isHidden()&&source->contentsMargins().top()==24&&pdf->contentsMargins().top()==0);
    press(play);check("textless play starts a real build",source->isTypesetting());press(play);
    check("the same control aborts compilation without opening a menu",waitFor([&]{return !source->isTypesetting();},10000)&&bar->isHidden()&&play->isVisible());
    // Reopen while focus is selected: saved ordinary-mode window state must
    // not reintroduce a toolbar on startup.
    source->setModified(false);delete source;for(auto *viewer:PDFDocumentWindow::documentList())delete viewer;
    auto *reopened=new TeXDocumentWindow(path);reopened->show();settle();key(reopened->editor(),Qt::Key_Shift,Qt::NoModifier);settle(250);
    check(QStringLiteral("opening a file starts with the selected minimal chrome (mode=%1, toolbarHidden=%2, menuVisible=%3)").arg(reopened->property("researchFocus").toBool()).arg(reopened->findChild<QToolBar*>("toolBar_run")->isHidden()).arg(reopened->findChild<QToolButton*>("researchFocusMore")->isVisible()),reopened->findChild<QToolBar*>("toolBar_run")->isHidden()&&reopened->findChild<QWidget*>("researchFocusChrome")->isHidden()&&!reopened->findChild<QToolButton*>("researchFocusMore")->isVisible()&&!reopened->findChild<QToolButton*>("researchFocusCompile")->isVisible());
    put(folder+"/pointer-reveals.json",QJsonDocument(pointerReveals).toJson());reopened->setModified(false);delete reopened;app.exit(failures?1:0);
  });return app.exec();
}

