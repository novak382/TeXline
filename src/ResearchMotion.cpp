// TeXline motion: no per-frame document layout, no input interception.
// GPL-2.0-or-later.
#include "ResearchMotion.h"
#include "ResearchAppearance.h"
#include <QApplication>
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QComboBox>
#include <QPlainTextEdit>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QLayout>
#include <QMainWindow>
#include <QPainter>
#include <QPointer>
#include <QScrollBar>
#include <QTextEdit>
#include <QTextBlock>
#include <QTextLayout>
#include <QTimer>
#include <QEasingCurve>
#include <QScreen>
#include <cmath>

namespace Research {
void installSourceLayoutCache(QTextDocument *document) {
  if(!document||qEnvironmentVariableIsSet("TEXLINE_DISABLE_WIDTH_CACHE")||document->property("researchWidthCacheInstalled").toBool())return;
  document->setProperty("researchWidthCacheInstalled",true);
  auto refresh=[document](int at,int removed,int added){
    if(!removed&&!added)return;
    const bool enabled=document->characterCount()<=2*1024*1024;
    const bool changed=enabled!=document->property("researchWidthCacheEnabled").toBool();
    document->setProperty("researchWidthCacheEnabled",enabled);
    auto block=changed?document->firstBlock():document->findBlock(at);
    const int end=changed?document->characterCount():at+added;
    for(;block.isValid()&&block.position()<=end;block=block.next())block.layout()->setCacheEnabled(enabled);
  };
  QObject::connect(document,&QTextDocument::contentsChange,document,refresh);
  refresh(0,0,document->characterCount());
}
int motionFrameInterval(const QWidget *surface) {
  const auto *screen=surface?surface->screen():nullptr;const qreal rate=screen?screen->refreshRate():60.;
  return qBound(4,qRound(1000./(std::isfinite(rate)&&rate>=30.?qMin(rate,240.):60.)),16);
}
namespace {
constexpr int duration = MotionMetrics::TransitionMs;
QRect area(QWidget *widget, QWidget *root) { return {widget->mapTo(root,QPoint()),widget->size()}; }
qint64 bytes(const QPixmap &p) {return qint64(p.width())*p.height()*4;}
// A timer exists only while a reveal is running. Use elapsed wall time so an
// inactive/slow surface cannot retain a stale bitmap after the deadline.
class Reveal final : public QWidget {
public:
  explicit Reveal(QWidget *owner) : QWidget(owner), tick(this) {
    setObjectName(QStringLiteral("researchArrivalVeil"));
    setAttribute(Qt::WA_TransparentForMouseEvents);setAttribute(Qt::WA_NoSystemBackground);
    // The deadline may hide this child during an offscreen/native paint.
    // Keep the live owner beneath it valid for that same frame.
    tick.setInterval(16);tick.setTimerType(Qt::PreciseTimer);
    connect(&tick,&QTimer::timeout,this,[this]{if(clock.elapsed()>=duration)finish();else update();});hide();
    owner->installEventFilter(this);qApp->installEventFilter(this);
    for(auto *field:owner->findChildren<QLineEdit*>())connect(field,&QLineEdit::textChanged,this,[this]{finish();});
    for(auto *text:owner->findChildren<QTextEdit*>()){
      connect(text,&QTextEdit::textChanged,this,[this]{finish();});
      connect(text,&QTextEdit::cursorPositionChanged,this,[this]{finish();});
    }
    for(auto *text:owner->findChildren<QPlainTextEdit*>()){
      connect(text,&QPlainTextEdit::textChanged,this,[this]{finish();});
      connect(text,&QPlainTextEdit::cursorPositionChanged,this,[this]{finish();});
    }
    for(auto *button:owner->findChildren<QAbstractButton*>())connect(button,&QAbstractButton::toggled,this,[this]{finish();});
    for(auto *choice:owner->findChildren<QComboBox*>())connect(choice,&QComboBox::currentIndexChanged,this,[this]{finish();});
    for(auto *view:owner->findChildren<QAbstractItemView*>())if(auto *model=view->model()){
      connect(model,&QAbstractItemModel::dataChanged,this,[this]{finish();});
      connect(model,&QAbstractItemModel::modelReset,this,[this]{finish();});
      connect(model,&QAbstractItemModel::rowsInserted,this,[this]{finish();});
      connect(model,&QAbstractItemModel::rowsRemoved,this,[this]{finish();});
      connect(model,&QAbstractItemModel::layoutChanged,this,[this]{finish();});
    }
    for(auto *bar:owner->findChildren<QScrollBar*>())connect(bar,&QScrollBar::valueChanged,this,[this]{finish();});
  }
  ~Reveal() override {qApp->removeEventFilter(this);}
  void finish() {tick.stop();hide();frame=QPixmap();setProperty("researchSnapshotBytes",qint64(0));}
  void start(int offset) {
    finish();auto *owner=parentWidget();if(!owner||!owner->isVisible()||!motionEnabled())return;
    QElapsedTimer capture;capture.start();setGeometry(owner->rect());frame=owner->grab();distance=offset;
    if(qEnvironmentVariableIsSet("TEXLINE_MOTION_PROFILE"))owner->setProperty("arrival-capture",double(capture.nsecsElapsed())/1000000.);
    if(capture.elapsed()>2*motionFrameInterval(owner)){finish();return;}
    if(frame.isNull())return;setProperty("researchSnapshotBytes",bytes(frame));clock.start();show();raise();tick.start(motionFrameInterval(owner));
  }
protected:
  bool eventFilter(QObject *object,QEvent *event) override {
    if(!isVisible())return false;
    const auto type=event->type();auto *widget=qobject_cast<QWidget*>(object);
    if((object==parentWidget()&&(type==QEvent::Resize||type==QEvent::Hide||type==QEvent::DevicePixelRatioChange))||
       (widget&&(widget==parentWidget()||parentWidget()->isAncestorOf(widget))&&
        (type==QEvent::KeyPress||type==QEvent::InputMethod||type==QEvent::MouseButtonPress||type==QEvent::Wheel||type==QEvent::FontChange||type==QEvent::StyleChange)))finish();
    return false;
  }
  void paintEvent(QPaintEvent *) override {
    if(clock.elapsed()>=duration){finish();return;}
    const qreal t=QEasingCurve(QEasingCurve::OutCubic).valueForProgress(qBound(0.,double(clock.elapsed())/duration,1.));
    QPainter p(this);p.fillRect(rect(),parentWidget()->palette().color(QPalette::Base));
    // Text-first tools keep full contrast during their short translation.
    // Fading a complete form over a tinted base makes it look disabled.
    p.setOpacity(parentWidget()->property("researchArrivalOpaque").toBool()?1.:t);p.drawPixmap(QPoint(0,qRound(distance*(1-t))),frame);
  }
private:
  QTimer tick;QElapsedTimer clock;QPixmap frame;int distance{6};
};

class PanelReveal final : public QWidget {
public:
  explicit PanelReveal(QMainWindow *owner) : QWidget(owner),window(owner),tick(this) {
    setObjectName(QStringLiteral("researchPanelMotionVeil"));setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_OpaquePaintEvent);
    tick.setInterval(16);tick.setTimerType(Qt::PreciseTimer);
    connect(&tick,&QTimer::timeout,this,[this]{if(clock.elapsed()>=duration)finish();else update();});hide();
    owner->installEventFilter(this);qApp->installEventFilter(this);
    if(auto *editor=owner->findChild<QTextEdit*>(QStringLiteral("textEdit"))){sourceEditor=editor;connect(editor->document(),&QTextDocument::contentsChange,this,[this](int,int removed,int added){if(removed||added)finish();});auto release=[this]{if(isVisible())finish();};connect(editor,&QTextEdit::cursorPositionChanged,this,release);connect(editor->verticalScrollBar(),&QScrollBar::valueChanged,this,release);connect(editor->horizontalScrollBar(),&QScrollBar::valueChanged,this,release);}
  }
  ~PanelReveal() override {qApp->removeEventFilter(this);}
  void finish() {tick.stop();hide();panelFrame=QPixmap();sourceFrame=QPixmap();panel=nullptr;setProperty("researchSnapshotBytes",qint64(0));}
  qreal extentNow() const {
    const qreal t=QEasingCurve(QEasingCurve::OutCubic).valueForProgress(qBound(0.,double(clock.elapsed())/duration,1.));
    return from+(to-from)*t;
  }
  bool refresh(QDockWidget *value) {if(!isVisible()||panel!=value||value->isHidden())return false;if(clock.elapsed()>=duration){finish();return false;}if(value->layout())value->layout()->activate();panelFrame=value->grab();setProperty("researchSnapshotBytes",bytes(panelFrame)+bytes(sourceFrame));update();return true;}
  void change(QDockWidget *value,bool on,int extent,bool animate) {
    const bool profiling=qEnvironmentVariableIsSet("TEXLINE_PANEL_PROFILE");
    QElapsedTimer elapsed;if(profiling)elapsed.start();
    auto phase=[&](const char *name){if(profiling)window->setProperty(name,double(elapsed.nsecsElapsed())/1000000.);};
    const bool reversing=isVisible()&&panel==value;
    const qreal visibleExtent=reversing?extentNow():0;
    const bool wasVisible=!value->isHidden();
    if(wasVisible==on&&!reversing){if(on&&extent>0)window->resizeDocks({value},{extent},orientation(value));return;}
    auto *center=window->centralWidget();if(!center)return;
    const auto oldSource=area(center,window);const auto oldPanel=area(value,window);
    const bool run=animate&&motionEnabled()&&window->updatesEnabled()&&window->isVisible()&&!window->isMinimized();
    QPixmap outgoing;if(run&&!on)outgoing=reversing?panelFrame:value->grab();
    QPixmap previousSource;if(run&&on&&window->dockWidgetArea(value)==Qt::BottomDockWidgetArea)previousSource=center->grab();
    phase("captures-before");
    finish();panel=value;edge=window->dockWidgetArea(value);
    auto *editor=window->findChild<QTextEdit*>(QStringLiteral("textEdit"));
    sourceBackground=editor?editor->viewport()->palette().color(QPalette::Base):center->palette().color(QPalette::Base);sourceBackground.setAlpha(255);
    const auto anchor=editor?editor->cursorForPosition(QPoint(0,0)):QTextCursor();
    const int y=editor?editor->cursorRect(anchor).top():0;
    const int x=editor?editor->horizontalScrollBar()->value():0;
    const bool atTop=editor&&editor->verticalScrollBar()->value()==editor->verticalScrollBar()->minimum();
    phase("anchor-before");
    const bool updates=window->updatesEnabled();window->setUpdatesEnabled(false);
    const auto priorWidthDocument=qApp->property("_texlineWidthRelayoutDocument");
    if(editor&&editor->document()->property("researchWidthCacheEnabled").toBool())qApp->setProperty("_texlineWidthRelayoutDocument",qulonglong(reinterpret_cast<quintptr>(editor->document())));
    value->setVisible(on);if(on&&extent>0)window->resizeDocks({value},{extent},orientation(value));
    phase("visibility-and-resize");
    if(window->layout())window->layout()->activate();
    for(auto *layout:center->findChildren<QLayout*>())layout->activate();
    phase("layouts");
    if(editor){editor->verticalScrollBar()->setValue(atTop?editor->verticalScrollBar()->minimum():editor->verticalScrollBar()->value()+editor->cursorRect(anchor).top()-y);editor->horizontalScrollBar()->setValue(x);}
    sourceRect=area(center,window);panelRect=on?area(value,window):oldPanel;
    phase("anchor-after");
    if(run){
      // Qt skips disabled child surfaces in QWidget::grab. Re-enable updates
      // before rendering the final surfaces; queued paints cannot run until
      // this call returns, after the reveal is already in place.
      window->setUpdatesEnabled(updates);
      panelFrame=on?value->grab():outgoing;sourceFrame=previousSource.isNull()?center->grab():previousSource;
      phase("captures-after");
      const int full=orientation(value)==Qt::Horizontal?panelRect.width():panelRect.height();
      from=reversing?visibleExtent:(wasVisible?full:0);to=on?full:0;
      // Left source moves as the sidebar reveals. Text is laid out only once,
      // at its final width, then clipped instead of rewrapping every frame.
      sourceDelta=oldSource.left()-sourceRect.left();
      if(reversing&&edge==Qt::LeftDockWidgetArea)sourceDelta=qRound(from-to);
      setGeometry(window->rect());clip=sourceRect.united(oldSource).united(panelRect);
      // A bottom panel can span a sidebar. A bounding rectangle alone would
      // temporarily erase that unrelated sidebar above the new panel.
      movingRegion=QRegion(sourceRect)+QRegion(oldSource)+QRegion(panelRect);setMask(movingRegion);
      setProperty("researchSnapshotBytes",bytes(panelFrame)+bytes(sourceFrame));clock.start();show();raise();tick.start(motionFrameInterval(window));
      setProperty("researchMotionFrom",from);setProperty("researchMotionTo",to);
    }else finish();
    window->setUpdatesEnabled(updates);window->update();
    qApp->setProperty("_texlineWidthRelayoutDocument",priorWidthDocument);
    phase("done");
  }
protected:
  bool eventFilter(QObject *object,QEvent *event) override {
    if(!isVisible())return false;const auto type=event->type();
    if(object==sourceEditor&&(type==QEvent::FontChange||type==QEvent::StyleChange))finish();
    if(object==window&&(type==QEvent::Resize||type==QEvent::Hide||type==QEvent::WindowStateChange||type==QEvent::DevicePixelRatioChange))finish();
    auto *widget=qobject_cast<QWidget*>(object);
    if(widget&&widget->window()==window){
      // Command buttons and Escape can reverse a panel. Other interactions
      // expose live values before dispatch, including line, numeric, choice
      // and slider inputs. A field's own clear button belongs to that field.
      const bool interaction=type==QEvent::KeyPress||type==QEvent::InputMethod||type==QEvent::MouseButtonPress||type==QEvent::Wheel;
      const bool escape=type==QEvent::KeyPress&&static_cast<QKeyEvent*>(event)->key()==Qt::Key_Escape;
      if(interaction&&!escape){bool button=false,field=false;for(auto *p=widget;p&&p!=window;p=p->parentWidget()){button|=bool(qobject_cast<QAbstractButton*>(p));field|=bool(qobject_cast<QLineEdit*>(p))||bool(qobject_cast<QAbstractScrollArea*>(p));}if(!button||field)finish();}
    }
    return false;
  }
  void paintEvent(QPaintEvent *) override {
    if(clock.elapsed()>=duration||!panel){finish();return;}
    if(!window->centralWidget()||area(window->centralWidget(),window)!=sourceRect){finish();return;}
    const qreal current=extentNow(),t=from==to?1:(current-from)/(to-from);
    QPainter p(this);p.setClipRegion(movingRegion);p.fillRect(clip,sourceBackground);
    QRect visible=panelRect;
    if(edge==Qt::LeftDockWidgetArea)visible.setWidth(qRound(current));
    else if(edge==Qt::RightDockWidgetArea)visible.setLeft(panelRect.right()-qRound(current)+1);
    else if(edge==Qt::BottomDockWidgetArea)visible.setTop(panelRect.bottom()-qRound(current)+1);
    else visible.setHeight(qRound(current));
    p.save();p.setClipRect(visible,Qt::IntersectClip);p.drawPixmap(panelRect.topLeft(),panelFrame);p.restore();
    // Keep the right edge fixed; only left-dock displacement needs translation.
    const int dx=edge==Qt::LeftDockWidgetArea?qRound(sourceDelta*(1-t)):0;
    QRect sourceClip=sourceRect.united(clip);if(edge==Qt::LeftDockWidgetArea)sourceClip.setLeft(panelRect.left()+qRound(current));
    if(edge==Qt::RightDockWidgetArea)sourceClip.setRight(panelRect.right()-qRound(current));
    if(edge==Qt::BottomDockWidgetArea)sourceClip.setBottom(panelRect.bottom()-qRound(current));
    p.setClipRect(sourceClip,Qt::IntersectClip);
    if(dx){p.save();p.setClipRect(QRect(sourceRect.left()+dx,sourceRect.top(),qMax(0,sourceRect.width()-20),sourceRect.height()),Qt::IntersectClip);p.drawPixmap(sourceRect.topLeft()+QPoint(dx,0),sourceFrame);p.restore();
      const QRect edgeStrip(sourceRect.right()-19,sourceRect.top(),20,sourceRect.height());p.setClipRect(edgeStrip,Qt::IntersectClip);p.drawPixmap(sourceRect.topLeft(),sourceFrame);
    }else p.drawPixmap(sourceRect.topLeft(),sourceFrame);
    setProperty("researchMotionExtent",current);
  }
private:
  Qt::Orientation orientation(QDockWidget *p)const {const auto a=window->dockWidgetArea(p);return a==Qt::LeftDockWidgetArea||a==Qt::RightDockWidgetArea?Qt::Horizontal:Qt::Vertical;}
  QMainWindow *window;QPointer<QDockWidget> panel;QPointer<QTextEdit> sourceEditor;QTimer tick;QElapsedTimer clock;QColor sourceBackground;
  Qt::DockWidgetArea edge{Qt::LeftDockWidgetArea};QPixmap panelFrame,sourceFrame;
  QRect panelRect,sourceRect,clip;QRegion movingRegion;qreal from{0},to{0};int sourceDelta{0};
};
}
void setPanelVisible(QDockWidget *panel,bool visible,int extent,bool animate) {
  if(!panel)return;auto *window=qobject_cast<QMainWindow*>(panel->parentWidget());
  if(!window){panel->setVisible(visible);return;}
  auto *motion=dynamic_cast<PanelReveal*>(window->findChild<QWidget*>(QStringLiteral("researchPanelMotionVeil"),Qt::FindDirectChildrenOnly));
  if(!motion)motion=new PanelReveal(window);motion->change(panel,visible,extent,animate);
}
bool refreshPanelMotion(QDockWidget *panel) {if(panel&&panel->parentWidget())if(auto *motion=dynamic_cast<PanelReveal*>(panel->parentWidget()->findChild<QWidget*>(QStringLiteral("researchPanelMotionVeil"),Qt::FindDirectChildrenOnly)))return motion->refresh(panel);return false;}
void cancelArrival(QWidget *content) {if(content)if(auto *veil=dynamic_cast<Reveal*>(content->findChild<QWidget*>(QStringLiteral("researchArrivalVeil"),Qt::FindDirectChildrenOnly)))veil->finish();}
void animateArrival(QWidget *content,int distance) {
  if(!content||!content->isVisible()||!motionEnabled())return;
  auto *veil=dynamic_cast<Reveal*>(content->findChild<QWidget*>(QStringLiteral("researchArrivalVeil"),Qt::FindDirectChildrenOnly));if(!veil)veil=new Reveal(content);veil->start(distance);
}
}
