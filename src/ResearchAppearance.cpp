#include "ResearchMenuChoices.h"
// TeXworks Research appearance extensions. GPL-2.0-or-later.
#include "ResearchAppearance.h"
#include "ResearchSettings.h"
#include "ResearchCommands.h"
#include "ResearchCompletion.h"
#include "PrefsDialog.h"
#include "ResearchMotion.h"
#include "ResearchSlides.h"
#include "ResearchArticle.h"
#include "ResearchOnboarding.h"
#include "ResearchCommandCompletion.h"
#include "PDFDocumentWindow.h"
#include "TeXDocumentWindow.h"
#include "CompletingEdit.h"
#include "Settings.h"
#include "TWApp.h"
#include "utils/WindowManager.h"
#include <QApplication>
#include <QCache>
#include <QAction>
#include <QActionGroup>
#include <QAbstractButton>
#include <QAbstractNativeEventFilter>
#include <QDockWidget>
#include <QComboBox>
#include <QIcon>
#include <QEvent>
#include <QElapsedTimer>
#include <QMainWindow>
#include <QMenuBar>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QCursor>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>
#include <QShortcut>
#include <QLabel>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QScrollBar>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTextEdit>
#include <algorithm>
#include <functional>
#include <cmath>
#include <QFontMetrics>
#include <QLayout>
#include <QPainter>
#include <QtMath>
#include <QPointer>
#include <QPaintEvent>
#include <QPushButton>
#include <QMenu>
#include <QMessageBox>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QScreen>
#include <QStatusBar>
#include <QGraphicsOpacityEffect>
#include <QGraphicsView>
#include <QPropertyAnimation>
#include <QVariantAnimation>
#include <QTimer>
#include <QSet>
#include <QToolBar>
#include <QToolButton>
#include <QWidget>
#ifdef Q_OS_WIN
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#endif

namespace Research {
QFont interfaceFont() {
  QFont font;
  font.setFamilies({QStringLiteral("Segoe UI"),QStringLiteral("Microsoft YaHei UI")});
  font.setPixelSize(ControlMetrics::FontPixels);
  return smoothTextFont(font);
}
bool motionEnabled() {
  if(Tw::Settings().value(QStringLiteral("researchReduceMotion"),false).toBool())return false;
#ifdef Q_OS_WIN
  BOOL enabled=TRUE;
  if(SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&enabled,0))return enabled;
#endif
  return true;
}
namespace {
bool uiEnglish = false;
bool uiTraditional = false;
#include "ResearchLocaleCatalog.inc"
QFont controlFont(bool primary = false) {
  QFont font=interfaceFont();
  font.setWeight(primary ? QFont::Medium : QFont::Normal);
  return smoothTextFont(font);
}
const QHash<QString, QString> &englishStrings() {
  static const auto strings = [] {
    QHash<QString, QString> result;
    QFile file(QStringLiteral(":/research/ui-en.json"));
    if (file.open(QIODevice::ReadOnly)) {
      const auto object = QJsonDocument::fromJson(file.readAll()).object();
      for (auto it = object.begin(); it != object.end(); ++it) result.insert(it.key(), it.value().toString());
    }
    return result;
  }();
  return strings;
}
struct DisplayPattern {
  QString chinese, english, traditional;
  QRegularExpression cnPattern, enPattern, twPattern;
  QList<int> cnArguments, enArguments, twArguments;
};
QRegularExpression displayPattern(const QString &text, QList<int> &arguments) {
  static const QRegularExpression marker(QStringLiteral("%([1-9][0-9]*)"));
  QString pattern = QStringLiteral("\\A"); int position = 0;
  auto matches = marker.globalMatch(text);
  while (matches.hasNext()) {
    const auto match = matches.next();
    pattern += QRegularExpression::escape(text.mid(position, match.capturedStart() - position)) + QStringLiteral("(.*?)");
    arguments.append(match.captured(1).toInt()); position = match.capturedEnd();
  }
  pattern += QRegularExpression::escape(text.mid(position)) + QStringLiteral("\\z");
  return QRegularExpression(pattern, QRegularExpression::DotMatchesEverythingOption);
}
QString displayText(const QString &text, bool english);
QString displayTextUncached(const QString &text, bool english) {
  const auto &strings = englishStrings();
  if (strings.contains(text)) return english ? strings.value(text) : ui(text);
  static const auto reverse=[] {QHash<QString,QString> out;auto keys=englishStrings().keys();std::sort(keys.begin(),keys.end());for(const auto &key:keys)if(!out.contains(englishStrings().value(key)))out.insert(englishStrings().value(key),key);return out;}();
  if(reverse.contains(text))return english?text:ui(reverse.value(text));
  if(traditionalSources().contains(text))return english?ui(traditionalSources().value(text),true):ui(traditionalSources().value(text));
  if(uiTraditional&&traditionalStrings().contains(text))return traditionalStrings().value(text);
  static const auto patterns = [] {
    QList<DisplayPattern> result;
    for (auto it = englishStrings().begin(); it != englishStrings().end(); ++it) if (it.key().contains(QStringLiteral("%1"))) {
      DisplayPattern entry; entry.chinese = it.key(); entry.english = it.value();
      entry.cnPattern = displayPattern(entry.chinese, entry.cnArguments);
      entry.enPattern = displayPattern(entry.english, entry.enArguments); entry.traditional=traditionalStrings().value(entry.chinese,entry.chinese);entry.twPattern=displayPattern(entry.traditional,entry.twArguments);result.append(entry);
    }
    std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) { return a.chinese.size() > b.chinese.size(); });
    return result;
  }();
  for (const auto &entry : patterns) {
    auto match = entry.cnPattern.match(text); const QList<int> *order = &entry.cnArguments;
    if (!match.hasMatch()) { match = entry.enPattern.match(text); order = &entry.enArguments; }
    if (!match.hasMatch()) { match=entry.twPattern.match(text);order=&entry.twArguments; }
    if (!match.hasMatch()) continue;
    QHash<int, QString> arguments;
    for (int i = 0; i < order->size(); ++i) arguments.insert((*order)[i], match.captured(i + 1));
    // Only this argument is a generated build state. Preserve filenames and
    // all other arguments verbatim, including user-authored Chinese text.
    if (entry.chinese.startsWith(QStringLiteral("%1 · %2"))) arguments[1] = displayText(arguments.value(1), english);
    const QString target = english ? entry.english : ui(entry.chinese);
    static const QRegularExpression marker(QStringLiteral("%([1-9][0-9]*)"));
    QString result; int position = 0; auto markers = marker.globalMatch(target);
    while (markers.hasNext()) {
      const auto m = markers.next(); result += target.mid(position, m.capturedStart() - position) + arguments.value(m.captured(1).toInt()); position = m.capturedEnd();
    }
    return result + target.mid(position);
  }
  return text;
}
QString displayText(const QString &text, bool english) {
  // Repeated Show events should not scan every translated label and pattern.
  // The bound keeps transient filenames/messages from accumulating indefinitely.
  static QCache<QString,QString> cache(1024);
  const auto key=(english?QStringLiteral("en:"):uiLocale()+QLatin1Char(':'))+text;
  if(auto *found=cache.object(key))return *found;
  const auto result=displayTextUncached(text,english);
  if(text.size()<2048)cache.insert(key,new QString(result));
  return result;
}
bool eligible(QWidget *w) {
  return w && w->isWindow() && (qobject_cast<QMainWindow *>(w) || w->objectName() == QStringLiteral("researchPalette"));
}
void applyToolbarMetrics(QToolBar *bar) {
  if (auto *layout = bar->layout()) {
    // QToolBar transposes some nonuniform margin coordinates internally.
    // Keep its margin uniform; small layout items add horizontal breathing room.
    const int margin = bar->property("researchCompactFind").toBool() ? 2 : bar->property("researchWorkspace").toBool() ? ControlMetrics::Gap / 2 : ControlMetrics::Gap;
    layout->setContentsMargins(margin, margin, margin, margin);
    layout->setSpacing(ControlMetrics::Gap);
  }
}
void applyMaterial(QWidget *w);
void refreshStyle(QWidget *w);
void refreshWorkspaceStyle(QMainWindow *window);
int paperHeaderHeight(const QWidget *window) {
#ifdef Q_OS_WIN
  if (!window->property("researchFocus").toBool()) return 32;
  // Keep a compact draggable window edge. The overlay hides when the pointer
  // enters the source, without reserving extra padding inside the editor.
  return window->findChild<QTextEdit *>(QStringLiteral("textEdit")) ? 24 : 0;
#else
  Q_UNUSED(window); return 48;
#endif
}
#ifdef Q_OS_WIN
// A temporary overlay, never part of the document layout. One animation owns
// the white surface and all five controls, so revealing it cannot change fit
// scale, scroll position or manuscript wrapping.
// Keep command names and shortcuts legible at narrow widths and high DPI.
class CommandDelegate final : public QStyledItemDelegate {
public:
  using QStyledItemDelegate::QStyledItemDelegate;
  QSize sizeHint(const QStyleOptionViewItem &,const QModelIndex &) const override { return QSize(0,36); }
  void paint(QPainter *p,const QStyleOptionViewItem &option,const QModelIndex &index) const override {
    auto o=option;initStyleOption(&o,index);const QString name=index.data(Qt::UserRole+1).toString(),keys=index.data(Qt::UserRole+2).toString();o.text.clear();
    const auto *widget=o.widget;const auto *style=widget?widget->style():QApplication::style();style->drawControl(QStyle::CE_ItemViewItem,&o,p,widget);
    p->save();const QRect area=o.rect.adjusted(10,0,-10,0);QFont small=o.font;small.setPixelSize(13);const int keyWidth=keys.isEmpty()?0:qMin(area.width()/2,QFontMetrics(small).horizontalAdvance(keys)+4);
    const bool enabled=o.state.testFlag(QStyle::State_Enabled);p->setFont(o.font);p->setPen(enabled?QColor(QStringLiteral("#202629")):QColor(QStringLiteral("#929b9f")));
    p->drawText(area.adjusted(0,0,-keyWidth-(keyWidth?16:0),0),Qt::AlignVCenter|Qt::AlignLeft,QFontMetrics(o.font).elidedText(name,Qt::ElideRight,qMax(0,area.width()-keyWidth-(keyWidth?16:0))));
    p->setFont(small);p->setPen(enabled?QColor(QStringLiteral("#6e6e73")):QColor(QStringLiteral("#929b9f")));p->drawText(area,Qt::AlignVCenter|Qt::AlignRight,QFontMetrics(small).elidedText(keys,Qt::ElideLeft,keyWidth));p->restore();
  }
};
class FocusChrome final : public QWidget {
public:
  explicit FocusChrome(QWidget *window) : QWidget(window) {
    setObjectName(QStringLiteral("researchFocusChrome"));
    setMouseTracking(true); hide();
  }
protected:
  void paintEvent(QPaintEvent *event) override {
    QPainter painter(this); painter.setClipRegion(event->region());
    painter.fillRect(rect(), Qt::white);
  }
};
// Use the original Microsoft Codicons SVGs; only the button surface is painted here.
class CaptionButton final : public QToolButton {
public:
  CaptionButton(QWidget *parent,int kind):QToolButton(parent),close(kind==2){setFixedSize(46,40);setIconSize(QSize(16,16));}
protected:
  void paintEvent(QPaintEvent *) override {
    QPainter painter(this);const bool hover=underMouse();
    if(isDown()||hover)painter.fillRect(rect(),close?QColor(QString::fromLatin1(isDown()?"#a8231b":"#c42b1c")):QColor(QString::fromLatin1(isDown()?"#cbd9df":"#d9e4e9")));
    if(hasFocus()){painter.setPen(QPen(QColor(QStringLiteral("#7b8c95")),1));painter.drawRect(rect().adjusted(2,2,-3,-3));}
    const QRect target(QPoint((width()-16)/2,(height()-16)/2),QSize(16,16));
    if(close&&(hover||isDown())) {
      QPixmap glyph=icon().pixmap(QSize(16,16),devicePixelRatioF());
      QPainter tint(&glyph);tint.setCompositionMode(QPainter::CompositionMode_SourceIn);tint.fillRect(glyph.rect(),Qt::white);tint.end();
      painter.drawPixmap(target.topLeft(),glyph);
    }else icon().paint(&painter,target,Qt::AlignCenter,isEnabled()?QIcon::Normal:QIcon::Disabled);
  }
private:bool close;
};
// The paper surface keeps its HWND, native resize/Snap behavior and normal
// close controller. Only its non-client drawing and hit regions change.
class PaperWindowControls final : public QObject {
public:
  explicit PaperWindowControls(QMainWindow *window)
      : QObject(window), window(window), idle(this), fade(this) {
    setObjectName(QStringLiteral("researchPaperController"));
    // Hidden controls leave bare client pixels at the top/right edge. Those
    // pixels belong to QMainWindow, not the editor's tracked viewport.
    window->setMouseTracking(true);
    chrome = new FocusChrome(window);
    chrome->installEventFilter(this);
    for (const auto &name : {QStringLiteral("researchFocusCompile"), QStringLiteral("researchFocusZoom"), QStringLiteral("researchFocusMore")})
      if (auto *control = window->findChild<QToolButton *>(name)) {
        control->setParent(chrome); control->setMouseTracking(true); control->installEventFilter(this);
        if (auto *menu = control->menu()) {
          connect(menu, &QMenu::aboutToShow, this, [this] { idle.stop(); });
          connect(menu, &QMenu::aboutToHide, this, [this] {
            if (this->window->property("researchFocus").toBool()) { lastPosition = QCursor::pos(); idle.start(); }
          });
        }
      }
    strip = new QWidget(window);
    strip->setObjectName(QStringLiteral("researchPaperControls"));
    strip->setFixedSize(46 * 3, 40);
    title = new QLabel(window); title->setObjectName(QStringLiteral("researchPaperTitle"));
    title->setFont(controlFont()); title->setTextFormat(Qt::PlainText);
    title->setStyleSheet(QStringLiteral("QLabel { color: #40464d; background: transparent; }"));
    position();
    strip->setMouseTracking(true);
    strip->installEventFilter(this);
    opacity = new QGraphicsOpacityEffect(chrome); opacity->setOpacity(0);
    chrome->setGraphicsEffect(opacity); strip->hide();
    fade.setTargetObject(opacity); fade.setPropertyName("opacity");
    fade.setEasingCurve(QEasingCurve::InOutCubic);
    idle.setSingleShot(true); idle.setInterval(1800);
    auto button = [this](const QString &name, const QString &label, const QString &icon, int index) {
      auto *control = new CaptionButton(strip,index); control->setObjectName(name);
      control->setIcon(QIcon(icon)); control->setToolButtonStyle(Qt::ToolButtonIconOnly);
      control->setAccessibleName(Research::ui(label));
      control->setCursor(Qt::ArrowCursor);
      // Caption actions must not steal the editor's typing focus on a click.
      // They remain reachable by keyboard navigation when requested.
      control->setFocusPolicy(Qt::TabFocus);
      control->move(index * 46, 0);
      control->setMouseTracking(true); control->installEventFilter(this);
      return control;
    };
    auto *close = button(QStringLiteral("researchWindowClose"), QStringLiteral("关闭窗口"), QStringLiteral(":/research/icons/codicons/chrome-close.svg"), 2);
    maximize = button(QStringLiteral("researchWindowMaximize"), QStringLiteral("最大化"), QStringLiteral(":/research/icons/codicons/chrome-maximize.svg"), 1);
    auto *minimize = button(QStringLiteral("researchWindowMinimize"), QStringLiteral("最小化"), QStringLiteral(":/research/icons/codicons/chrome-minimize.svg"), 0);
    // Queue close until the mouse handler has returned: closing can destroy
    // the entire strip, and must still run the existing unsaved-file dialog.
    connect(close, &QToolButton::clicked, window, [window] { QTimer::singleShot(0, window, [window] { window->close(); }); });
    connect(minimize, &QToolButton::clicked, window, [window] { window->showMinimized(); });
    connect(maximize, &QToolButton::clicked, window, [window] {
      if (window->isFullScreen()) {
        // Restore the PDF's menus and Escape shortcuts through their owner.
        if (auto *pdf = qobject_cast<PDFDocumentWindow *>(window)) pdf->toggleFullScreen();
        else window->showNormal();
      } else {
        // Dispatch the native caption command so Windows owns the transition.
        PostMessageW(reinterpret_cast<HWND>(window->winId()), WM_SYSCOMMAND,
                     window->isMaximized() ? SC_RESTORE : SC_MAXIMIZE, 0);
      }
    });
    connect(&idle, &QTimer::timeout, this, [this] {
      // A stationary pointer is idle even on the white strip. Only an
      // active keyboard operation or open menu keeps the overlay visible.
      if (!this->window->property("researchFocus").toBool()) return;
      if (!chrome->isAncestorOf(QApplication::focusWidget()) && !ownedPopup()) hideSoftly();
    });
    connect(&fade, &QPropertyAnimation::finished, this, [this] {
      if (opacity->opacity() <= 0) { chrome->hide(); strip->hide(); }
    });
    reset();
  }
  void reset() {
    idle.stop(); fade.stop(); hasPosition = false;
    const bool focused = window->property("researchFocus").toBool();
    chrome->hide(); chrome->setEnabled(true);
    strip->setParent(focused ? chrome : static_cast<QWidget *>(window));
    const bool compact=focused&&window->findChild<QTextEdit *>(QStringLiteral("textEdit"));
    strip->setFixedHeight(focused ? (compact?24:40) : 32);
    for (auto *button : strip->findChildren<QToolButton *>()) button->setFixedHeight(strip->height());
    strip->setEnabled(true); strip->setVisible(!focused); opacity->setOpacity(focused ? 0 : 1);
    position();
    title->setVisible(!focused); updateTitle();
  }
  void position() {
    int overlayTop = 0;
    const bool compact=window->property("researchFocus").toBool()&&window->findChild<QTextEdit *>(QStringLiteral("textEdit"));
    if (window->property("researchFocus").toBool()&&!compact) {
      if (window->menuBar()->isVisible()) overlayTop = window->menuBar()->height();
      if (auto *find = window->findChild<QWidget *>(QStringLiteral("researchFindBar")); find && find->isVisible())
        overlayTop = qMax(overlayTop, find->mapTo(window, QPoint()).y() + find->height());
    }
    // Source controls fit inside the existing 24px draggable edge. A taller
    // overlay used to cover the first line, including its mouse hit targets.
    chrome->setGeometry(0, overlayTop, window->width(), compact?24:48);
    const int top = window->property("researchFocus").toBool()&&!compact ? 4 : 0;
    strip->move(qMax(0, window->width() - strip->width()), top);
    if (title) { title->setGeometry(16, 0, qMax(0, strip->x() - 32), 32); updateTitle(); }
  }
  void updateTitle() {
    QString text = window->windowTitle();
    text.remove(QStringLiteral(" - TeXline")).replace(QStringLiteral("[*]"), window->isWindowModified() ? QStringLiteral(" •") : QString());
    title->setToolTip(text); title->setText(title->fontMetrics().elidedText(text, Qt::ElideRight, title->width()));
  }
  void motion(const QPoint &position) {
    if (!window->property("researchFocus").toBool() || window->isMinimized()) return;
    // Reveal from the top edge, not the first source line. The editor no
    // longer reserves a blank writing margin for the overlay.
    const QPoint local = window->mapFromGlobal(position);
    bool pressingControl = false, overOverlayControl = false;
    for (auto *child = window->childAt(local); child && child != window; child = child->parentWidget())
      if (qobject_cast<QAbstractButton *>(child)) {
        pressingControl = true; overOverlayControl = chrome->isAncestorOf(child); break;
      }
    const bool topEdge = local.y() >= 0 && local.y() < paperHeaderHeight(window);
    const QRect controlArea(strip->mapTo(window, QPoint()), strip->size());
    // Keep a pointer path to the buttons when a search/menu row moves the
    // overlay down. The writing area to their left still dismisses it.
    const QRect controlApproach(controlArea.left(), 0, window->width() - controlArea.left(), controlArea.bottom() + 1);
    // The complete visible compile/more buttons belong to the overlay too,
    // including their lower half below the narrow source reveal edge.
    const bool overControls = chrome->isVisible() && (controlApproach.contains(local) || overOverlayControl);
    if (window->findChild<QTextEdit *>(QStringLiteral("textEdit")) &&
        ((QApplication::mouseButtons() != Qt::NoButton && !pressingControl) ||
         (!topEdge && !overControls))) {
      hasPosition = false;
      if (!ownedPopup()) hideSoftly();
      return;
    }
    const QPoint delta = position - lastPosition;
    if (hasPosition && chrome->isVisible() && chrome->isEnabled() &&
        delta.x() * delta.x() + delta.y() * delta.y() < 9) return;
    hasPosition = true; lastPosition = position;
    updateMaximize();
    chrome->setEnabled(true); strip->setEnabled(true);
    strip->show(); chrome->show(); chrome->raise();
    animate(1, 120); idle.start();
  }
  void hideSoftly() { idle.stop(); chrome->setEnabled(false); strip->setEnabled(false); animate(0, 200); }
  void keystroke(QWidget *target) {
    if (target != chrome && !chrome->isAncestorOf(target) && !ownedPopup()) hideSoftly();
  }
  void updateMaximize() {
    const bool restore = window->isMaximized() || window->isFullScreen();
    const QString label = Research::ui(restore ? QStringLiteral("还原窗口") : QStringLiteral("最大化"));
    if (hasMaximizeState && restoring == restore && maximize->accessibleName() == label) return;
    hasMaximizeState = true; restoring = restore;
    maximize->setIcon(QIcon(restore ? QStringLiteral(":/research/icons/codicons/chrome-restore.svg") : QStringLiteral(":/research/icons/codicons/chrome-maximize.svg")));
    maximize->setAccessibleName(label);
    maximize->update();
  }
protected:
  bool eventFilter(QObject *, QEvent *event) override {
    if (window->property("researchFocus").toBool()) {
      if (event->type() == QEvent::MouseButtonPress) idle.stop();
      else if (event->type() == QEvent::MouseButtonRelease) idle.start();
    }
    if (event->type() == QEvent::Leave && chrome->isVisible() && window->property("researchFocus").toBool()) { lastPosition = QCursor::pos(); idle.start(); }
    return false;
  }
private:
  bool ownedPopup() const {
    auto *popup = QApplication::activePopupWidget();
    return popup && popup->parentWidget() && popup->parentWidget()->window() == window;
  }
  void animate(qreal end, int duration) {
    if (fade.state() == QAbstractAnimation::Running && fade.endValue().toReal() == end) return;
    fade.stop();
    // Input can reverse a reveal before its first frame. Cancel that reveal
    // even when opacity already equals zero, rather than letting it flash in.
    if (opacity->opacity() == end) {
      if (end <= 0) { chrome->hide(); strip->hide(); }
      return;
    }
    if(!motionEnabled()){opacity->setOpacity(end);if(end<=0){chrome->hide();strip->hide();}return;}
    fade.setDuration(qBound(120,duration,180)); fade.setStartValue(opacity->opacity()); fade.setEndValue(end); fade.start();
  }
  QMainWindow *window;
  FocusChrome *chrome;
  QWidget *strip;
  QToolButton *maximize;
  QLabel *title = nullptr;
  QGraphicsOpacityEffect *opacity;
  QTimer idle;
  QPropertyAnimation fade;
  QPoint lastPosition;
  bool hasPosition = false;
  bool hasMaximizeState = false, restoring = false;
};
PaperWindowControls *paperControls(QMainWindow *window) {
  return static_cast<PaperWindowControls *>(window->findChild<QObject *>(QStringLiteral("researchPaperController"), Qt::FindDirectChildrenOnly));
}
#endif
// All timed notices share one small-label animation. Persistent progress and
// unresolved PDF/source warnings retain their existing lifetime.
class StatusMessageController final : public QObject {
public:
  StatusMessageController(QMainWindow *window, QLabel *label)
      : QObject(label), window(window), label(label), statusBar(window->statusBar()), timer(this), fade(this) {
    setObjectName(QStringLiteral("researchStatusController"));
    opacity = new QGraphicsOpacityEffect(label);
    opacity->setOpacity(1);
    label->setGraphicsEffect(opacity);
    label->installEventFilter(this);
    statusBar->installEventFilter(this);
    timer.setSingleShot(true);
    fade.setTargetObject(opacity);
    fade.setPropertyName("opacity");
    fade.setEasingCurve(QEasingCurve::InOutCubic);
    connect(&timer, &QTimer::timeout, this, [this] {
      if (!transient) return;
      if (this->label->isVisible() && motionEnabled()) {
        fadingOut = true;
        fade.stop(); fade.setDuration(160);
        fade.setStartValue(opacity->opacity()); fade.setEndValue(0.0); fade.start();
      }
      else if (statusBar) statusBar->clearMessage();
    });
    connect(&fade, &QPropertyAnimation::finished, this, [this] {
      if (transient && fadingOut && statusBar) statusBar->clearMessage();
    });
    connect(statusBar.data(), &QStatusBar::messageChanged, this, [this](const QString &text) {
      this->window->setProperty("researchFocusStatus", text);
      reset();
    });
  }
  void reset() {
    transient = false;
    priority=NoticePriority::Routine;
    fadingOut = false;
    timer.stop(); fade.stop(); opacity->setOpacity(1);
    setAnimated(false);
  }
  void arm(int milliseconds) {
    reset(); transient = true; duration = qMax(1, milliseconds);
    setAnimated(true);
    refreshFocusMessage(window);
    if (label->isVisible() && !label->underMouse() && motionEnabled()) {
      opacity->setOpacity(0);
      fade.setDuration(120); fade.setStartValue(0.0); fade.setEndValue(1.0); fade.start();
    }
    if (!label->isVisible() || !label->underMouse()) timer.start(duration);
  }
  bool isTransient() const { return transient; }
  NoticePriority priority{NoticePriority::Routine};
  int remainingTime() const {
    return fadingOut && fade.state() == QAbstractAnimation::Running ? 1 :
      (timer.isActive() ? qMax(1, timer.remainingTime()) : duration);
  }
protected:
  bool eventFilter(QObject *object, QEvent *event) override {
    if (statusBar && object == statusBar.data() &&
        (event->type() == QEvent::Resize || event->type() == QEvent::LayoutRequest || event->type() == QEvent::Show) && !positionQueued) {
      positionQueued = true;
      QTimer::singleShot(0, this, [this] { positionQueued = false; refreshFocusMessage(window); });
    }
    if (object == label && transient) {
      if (event->type() == QEvent::Enter) {
        timer.stop(); fade.stop(); fadingOut = false; opacity->setOpacity(1);
      } else if (event->type() == QEvent::Leave) timer.start(duration);
    }
    return QObject::eventFilter(object, event);
  }
private:
  void setAnimated(bool active) {
    auto *status = statusBar.data();
    if (!status) return;
    if (status->property("researchAnimatedNotice").toBool() == active) return;
    status->setProperty("researchAnimatedNotice", active);
    status->style()->unpolish(status); status->style()->polish(status); status->update();
  }
  QMainWindow *window;
  QLabel *label;
  // The controller dies with its label. In ordinary mode both belong to the
  // status bar, which may be destroyed before the rest of the main window.
  QPointer<QStatusBar> statusBar;
  QGraphicsOpacityEffect *opacity;
  QTimer timer;
  QPropertyAnimation fade;
  int duration = 4000;
  bool transient = false;
  bool fadingOut = false;
  bool positionQueued = false;
};
StatusMessageController *statusController(QMainWindow *window) {
  return window ? static_cast<StatusMessageController *>(window->findChild<QObject *>(QStringLiteral("researchStatusController"))) : nullptr;
}
void compactStatusBar(QStatusBar *status) {
  // QStatusBar's platform layout adds vertical spacers independently of QSS.
  // Remove those insets before choosing a row height from the real font.
  std::function<void(QLayout *)> compact=[&](QLayout *layout){
    if(!layout)return;const auto margins=layout->contentsMargins();
    if(margins.top()||margins.bottom())layout->setContentsMargins(margins.left(),0,margins.right(),0);
    auto *box=dynamic_cast<QBoxLayout *>(layout);const bool vertical=box&&(box->direction()==QBoxLayout::TopToBottom||box->direction()==QBoxLayout::BottomToTop);
    for(int n=0;n<layout->count();++n){auto *item=layout->itemAt(n);if(item->layout())compact(item->layout());else if(vertical&&item->spacerItem()&&item->spacerItem()->sizeHint().height()>0)item->spacerItem()->changeSize(item->spacerItem()->sizeHint().width(),0,QSizePolicy::Minimum,QSizePolicy::Fixed);}
  };
  compact(status->layout());
  int height=qMax(24,int(std::ceil(QFontMetricsF(status->font()).height()))+4);
  for(auto *label:status->findChildren<QLabel *>())if(label->objectName()!=QStringLiteral("researchFocusMessage")){
    if(!label->isHidden())height=qMax(height,int(std::ceil(QFontMetricsF(label->font()).height()))+4);
    label->setAlignment((label->alignment()&Qt::AlignHorizontal_Mask)|Qt::AlignVCenter);
  }
  if(status->minimumHeight()!=height||status->maximumHeight()!=height)status->setFixedHeight(height);
}
void positionFocusMessage(QMainWindow *window) {
  auto *label = window->findChild<QLabel *>(QStringLiteral("researchFocusMessage"));
  if (!label || label->isHidden()) return;
  // Ordinary notifications belong to the status row. Floating them above it
  // covered the outline's document settings and looked like another button.
  auto *status = window->statusBar();
  const bool inlineNotice = !window->property("researchFocus").toBool() && status->isVisible();
  QWidget *owner = inlineNotice ? static_cast<QWidget *>(status) : static_cast<QWidget *>(window);
  if (label->parentWidget() != owner) { label->setParent(owner); label->show(); }
  if (label->property("researchInlineNotice").toBool() != inlineNotice) {
    label->setProperty("researchInlineNotice", inlineNotice);
    refreshStyle(label);
  }
  // QLabel/QFrame can retain the floating rule's vertical padding after a
  // stylesheet repolish. Set the actual content margins for both placements;
  // the inline label must not inherit the floating inset.
  label->setContentsMargins(inlineNotice?QMargins(4,0,4,0):QMargins(11,7,11,7));
  if (inlineNotice) {
    int right = status->width() - 8;
    for (auto *widget : status->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly))
      if (widget != label && widget->isVisible() && widget->width() > 0) right = qMin(right, widget->x() - 6);
    const int available = qMax(0, right - 8);
    label->setText(label->fontMetrics().elidedText(label->toolTip(), Qt::ElideRight, qMax(0, available - 8)));
    label->setGeometry(8, 1, available, qMax(0, status->height() - 2));
    label->raise(); return;
  }
  const int available = qMax(40, window->width() - 64);
  label->setText(label->fontMetrics().elidedText(label->toolTip(), Qt::ElideRight, available));
  label->adjustSize();
  const int statusHeight = window->statusBar()->isVisible() ? window->statusBar()->height() : 0;
  label->move(16, window->height() - label->height() - statusHeight - 16);
  if(auto *editor=window->findChild<QTextEdit *>(QStringLiteral("textEdit"));editor&&editor->isVisible()) {
    const auto cursor=editor->textCursor();auto first=cursor,last=cursor;
    first.setPosition(cursor.selectionStart());last.setPosition(cursor.selectionEnd());
    const auto a=editor->cursorRect(first),b=editor->cursorRect(last);
    QRect writing=cursor.hasSelection()?a.united(b):a.adjusted(-12,-3,12,3);
    if(cursor.hasSelection()&&first.block()!=last.block()){writing.setLeft(0);writing.setRight(editor->viewport()->width()-1);}
    writing.translate(editor->viewport()->mapTo(window,QPoint()));
    writing=writing.intersected(QRect(editor->viewport()->mapTo(window,QPoint()),editor->viewport()->size()));
    if(label->geometry().intersects(writing)) {
      // Keep the source, caret and selected ink stationary. A short notice can
      // use the nearest space above them; a full-page selection has no spare
      // surface, so its status remains available without covering that text.
      const int top=writing.top()-label->height()-8;
      const int firstLine=editor->viewport()->mapTo(window,QPoint()).y()+editor->fontMetrics().height()+8;
      if(top>=firstLine)label->move(label->x(),top);else label->hide();
    }
  }
  label->raise();
}
void positionFocusControls(QMainWindow *window) {
#ifdef Q_OS_WIN
  // The shared caption must track normal-mode resizing too, even when the
  // floating focus controls below are hidden.
  if (auto *controls = paperControls(window)) controls->position();
#endif
  auto *button = window->findChild<QToolButton *>(QStringLiteral("researchFocusMore"));
  if (!button || button->isHidden()) return;
  const int menuHeight = window->menuBar()->isVisible() ? window->menuBar()->height() : 0;
  int top = menuHeight;
#ifdef Q_OS_WIN
  if (!window->property("researchFocus").toBool())
#endif
  for (auto *bar : window->findChildren<QToolBar *>())
    if (bar->isVisible() && window->toolBarArea(bar) == Qt::TopToolBarArea)
      top = qMax(top, bar->mapTo(window, QPoint()).y() + bar->height());
#ifdef Q_OS_WIN
  if (window->property("researchFocus").toBool()) top = 0;
#endif
  const bool leftEntries =
#ifdef Q_OS_WIN
    window->property("researchFocus").toBool();
#else
    false;
#endif
  const int inset=button->property("researchCompactFocusControl").toBool()?0:8;
  button->move(leftEntries ? 16 + ControlMetrics::Height + ControlMetrics::Gap : qMax(8, window->width() - button->width() - 16), top + inset);
  button->raise();
  for (const auto &name : {QStringLiteral("researchFocusCompile"), QStringLiteral("researchFocusZoom")})
    if (auto *primary = window->findChild<QToolButton *>(name)) {
      primary->move(leftEntries ? 16 : qMax(8, button->x() - primary->width() - ControlMetrics::Gap), top + inset);
      primary->raise();
    }
}
void syncWorkspaceChrome(QMainWindow *window) {
  const bool focused = window->property("researchFocus").toBool();
  for (auto *bar : window->findChildren<QToolBar *>())
    if (bar->property("researchWorkspace").toBool()) bar->setVisible(!focused);
  if (auto *button = window->findChild<QToolButton *>(QStringLiteral("researchFocusMore"))) button->setVisible(focused);
  if (auto *button = window->findChild<QToolButton *>(QStringLiteral("researchFocusCompile"))) button->setVisible(focused);
  if (auto *button = window->findChild<QToolButton *>(QStringLiteral("researchFocusZoom"))) button->setVisible(focused);
  positionFocusControls(window);
}
void queueFocusPosition(QWidget *window) {
  if (!window->property("researchFocus").toBool() || window->property("researchFocusPositionQueued").toBool()) return;
  window->setProperty("researchFocusPositionQueued", true);
  QTimer::singleShot(0, window, [window] {
    window->setProperty("researchFocusPositionQueued", false);
    if (auto *main = qobject_cast<QMainWindow *>(window)) positionFocusControls(main);
  });
}
void paperContentsMargins(QMainWindow *window, bool focused) {
  const auto m = window->property("researchNormalContentsMargins").toList();
  if (m.size() != 4) return;
#ifdef Q_OS_WIN
  Q_UNUSED(focused);
  const int header = paperHeaderHeight(window);
#else
  const int header = focused && qobject_cast<QGraphicsView *>(window->centralWidget()) ? 48 : 0;
#endif
  window->setContentsMargins(m[0].toInt(), m[1].toInt() + header, m[2].toInt(), m[3].toInt());
}
void applyWorkspace(QMainWindow *window, bool focused, bool restored = false) {
  if(focused&&window->property("researchSlidePreviewExpanded").toBool())
    if(auto *expand=window->findChild<QAction*>(QStringLiteral("researchSlideExpandPreview")))expand->trigger();
  const bool modeChanged = window->property("researchFocus").toBool() != focused;
  const bool changed = restored || modeChanged;
  window->setProperty("researchFocus", focused);
  if (!window->property("researchNormalContentsMargins").isValid()) {
    const auto m = window->contentsMargins();
    window->setProperty("researchNormalContentsMargins", QVariantList{m.left(), m.top(), m.right(), m.bottom()});
  }
#ifdef Q_OS_WIN
  if (changed) if (auto *controls = paperControls(window)) controls->reset();
#endif
  paperContentsMargins(window, focused);
  if (auto *view = qobject_cast<QGraphicsView *>(window->centralWidget())) {
    if (!view->property("researchNormalFrameStyle").isValid()) {
      view->setProperty("researchNormalFrameStyle", view->frameStyle());
      view->setProperty("researchNormalBackgroundRole", int(view->backgroundRole()));
    }
    // The scroll-area frame and Dark palette role otherwise leave grey edges
    // and black tracks around the white PDF when the toolbar is removed.
    view->setFrameStyle(QFrame::NoFrame);
    view->setBackgroundRole(QPalette::Base);
  }
  window->menuBar()->hide();
  // Focus leaves the complete bottom edge to the document.
  window->statusBar()->setVisible(!focused);
  if (modeChanged) if (auto *source = qobject_cast<TeXDocumentWindow *>(window)) {
    const bool current = source->editor()->getLineNumbersVisible();
    window->setProperty(focused ? "researchNormalLineNumbers" : "researchFocusLineNumbers", current);
    const char *desired = focused ? "researchFocusLineNumbers" : "researchNormalLineNumbers";
    const bool visible = window->property(desired).isValid() ? window->property(desired).toBool() : focused ? Tw::Settings().value(QStringLiteral("researchFocusLineNumbers"),false).toBool() : current;
    source->editor()->setLineNumberDisplay(visible);
    if (auto *action = source->findChild<QAction *>(QStringLiteral("actionLine_Numbers"))) action->setChecked(visible);
  }
  window->setProperty("researchFocusStatus", window->statusBar()->currentMessage());
  if (auto *toggle = window->findChild<QAction *>(QStringLiteral("researchFocusToggle"))) toggle->setChecked(focused);
  if (changed) {
    // Fold open panels on entry, and restore only those folded by this mode.
    // Commands and error reporting can still open a panel while focusing.
    QList<QWidget *> panels;
    for (auto *dock : window->findChildren<QDockWidget *>())
      panels.append(dock);
    for (const auto &name : {QStringLiteral("consoleTabs"), QStringLiteral("inputLine"), QStringLiteral("researchFindBar")})
      if (auto *panel = window->findChild<QWidget *>(name)) panels.append(panel);
    for (auto *panel : panels) {
      if (focused) {
        panel->setProperty("researchFocusRestore", !panel->isHidden());
        panel->hide();
      } else if (panel->property("researchFocusRestore").toBool()) {
        panel->show();
        panel->setProperty("researchFocusRestore", false);
      }
    }
  }
  for (auto *bar : window->findChildren<QToolBar *>()) if (bar->property("researchWorkspace").toBool()) {
    auto *compile = bar->findChild<QToolButton *>(QStringLiteral("researchCompile"));
    for (auto *action : bar->actions()) {
      if (action->property("researchFocusHidden").toBool()) action->setVisible(!focused);
      if (compile && action->property("researchFocusRight").toBool()) {
        bar->insertAction(compile->defaultAction(), action);
      }
    }
    if (compile) refreshCompileButton(compile);
    applyToolbarMetrics(bar);
  }
  if(changed)if(auto *source=qobject_cast<TeXDocumentWindow *>(window)){setSlideFocusMode(source,focused);setArticleFocusMode(source,focused);}
  QMetaObject::invokeMethod(window, "updateWindowMenu", Qt::DirectConnection);
  syncWorkspaceChrome(window);
  if (window->isVisible()) applyMaterial(window);
  refreshWorkspaceStyle(window);
  refreshFocusMessage(window);
}
// Retain one visible frame only during a mode change. The real document has
// its final layout throughout the fade; PDF pages are not re-rendered on each
// frame, and no window opacity or native surface is changed by the animation.
static constexpr int workspaceTransitionDuration = MotionMetrics::TransitionMs;
class ChromeVeil final : public QWidget {
public:
  explicit ChromeVeil(QMainWindow *window) : QWidget(window) {
    setObjectName(QStringLiteral("researchChromeVeil"));
    setAttribute(Qt::WA_TransparentForMouseEvents); hide();
  }
  void setFrame(QPixmap value) {
    frame = std::move(value); opacity = 1; frameAge.start();
    setProperty("researchSnapshotBytes", qint64(frame.width()) * frame.height() * 4);
  }
  void clear() { hide(); frame = QPixmap(); frameAge.invalidate(); setProperty("researchSnapshotBytes", qint64(0)); }
  std::function<void()> expire;
  bool compatible(const QWidget *window) const {
    return frame.isNull() || (QSizeF(frame.size()) / frame.devicePixelRatio() == QSizeF(window->size()) && qFuzzyCompare(frame.devicePixelRatio(), window->devicePixelRatioF()));
  }
  void setProgress(qreal value) { opacity = 1.0 - value; update(); }
  void protectReading(const QRegion &region) { reading=region;setMask(QRegion(parentWidget()->rect()).subtracted(reading)); }
protected:
  void paintEvent(QPaintEvent *event) override {
    // A late paint can precede an overdue timer event. Never draw an expired
    // snapshot over the live document merely because that event is pending.
    if (frameAge.isValid() && frameAge.hasExpired(workspaceTransitionDuration)) {
      if (expire) expire(); else clear();
      return;
    }
    QPainter painter(this); painter.setClipRegion(event->region().subtracted(reading));
    painter.setOpacity(opacity); painter.drawPixmap(QPoint(), frame);
  }
private:
  qreal opacity = 1;
  QPixmap frame;
  QElapsedTimer frameAge;
  QRegion reading;
};
class WorkspaceTransition final : public QObject {
public:
  explicit WorkspaceTransition(QMainWindow *window) : QObject(window), window(window), veil(new ChromeVeil(window)), fade(this), deadline(this), frames(this) {
    setObjectName(QStringLiteral("researchWorkspaceTransition"));
    window->installEventFilter(this);
    veil->expire = [this] { cancel(); };
    if (auto *editor = window->findChild<QTextEdit *>(QStringLiteral("textEdit"))) {
      editor->installEventFilter(this); editor->viewport()->installEventFilter(this);
      connect(editor->document(), &QTextDocument::contentsChange, this,
              [this](int, int removed, int added) { if (removed || added) cancel(); });
    }
    if (auto *view = qobject_cast<QGraphicsView *>(window->centralWidget())) {
      view->installEventFilter(this); view->viewport()->installEventFilter(this);
    }
    fade.setEasingCurve(QEasingCurve::OutCubic);
    connect(&fade, &QVariantAnimation::valueChanged, veil.data(), [this](const QVariant &value) { if (veil) veil->setProgress(value.toReal()); });
    deadline.setSingleShot(true); deadline.setTimerType(Qt::PreciseTimer);
    frames.setTimerType(Qt::PreciseTimer);connect(&frames,&QTimer::timeout,this,[this]{fade.setCurrentTime(qMin(int(frameClock.elapsed()),workspaceTransitionDuration));});
    connect(&deadline, &QTimer::timeout, this, [this] { cancel(); });
    connect(&fade, &QVariantAnimation::finished, this, [this] { frames.stop();deadline.stop(); if (veil) veil->clear(); });
  }
  ~WorkspaceTransition() override { if (veil) veil->expire = {}; cancel(); }
  void cancel() { frames.stop();deadline.stop(); fade.stop(); if (veil) veil->clear(); }
  void position() {
    if (!veil) return;
    if (!veil->compatible(window)) cancel();
    veil->setGeometry(window->rect());
  }
  void start(bool focused) {
    const bool profiling=qEnvironmentVariableIsSet("TEXLINE_MOTION_PROFILE");
    QElapsedTimer elapsed;if(profiling)elapsed.start();
    auto phase=[&](const char *name){if(profiling)window->setProperty(name,double(elapsed.nsecsElapsed())/1000000.);};
    if (window->property("researchFocus").toBool() == focused) return;
    if (!window->isVisible() || window->isMinimized()) {
      cancel();
      applyWorkspace(window, focused); return;
    }
    frames.stop();deadline.stop(); fade.stop();
    const bool animate=motionEnabled();
    auto *editor = window->findChild<QTextEdit *>(QStringLiteral("textEdit"));
    QRegion reading;if(editor)reading+=QRect(editor->mapTo(window,QPoint()),editor->size());
    auto *view = qobject_cast<QGraphicsView *>(window->centralWidget());
    if(view)reading+=QRect(view->viewport()->mapTo(window,QPoint()),view->viewport()->size());
    QPixmap previous;
    if(animate){
      previous=QPixmap(window->size()*window->devicePixelRatioF());previous.setDevicePixelRatio(window->devicePixelRatioF());previous.fill(Qt::transparent);
      QPainter painter(&previous);
      // Only chrome participates in this fade. Do not rasterize the paper or
      // source just to discard their pixels, or repaint them at every tick.
      window->render(&painter,QPoint(),QRegion(window->rect()).subtracted(reading));
    }
    // A reversal starts from the currently composed chrome, not from an old
    // endpoint. Its previous timer is stopped and no transition is queued.
    veil->clear();
    phase("focus-capture");
    const QTextCursor anchor = editor ? editor->cursorForPosition(QPoint(0, 0)) : QTextCursor();
    const int anchorY = editor ? editor->viewport()->mapTo(window, editor->cursorRect(anchor).topLeft()).y() : 0;
    const int scrollX = editor ? editor->horizontalScrollBar()->value() : 0;
    const bool atTop = editor && editor->verticalScrollBar()->value() == editor->verticalScrollBar()->minimum();
    const QPointF center = view ? view->mapToScene(view->viewport()->mapFrom(window, window->rect().center())) : QPointF();
    // Folding/restoring docks also changes source width. Reuse the same bounded
    // shaping cache as an explicit sidebar toggle during this layout transaction.
    const auto priorWidthDocument=qApp->property("_texlineWidthRelayoutDocument");
    if(editor&&editor->document()->property("researchWidthCacheEnabled").toBool())
      qApp->setProperty("_texlineWidthRelayoutDocument",qulonglong(reinterpret_cast<quintptr>(editor->document())));
    window->setUpdatesEnabled(false);
    auto *layout = window->layout();
    const bool layoutEnabled = layout && layout->isEnabled();
    if (layoutEnabled) layout->setEnabled(false);
    applyWorkspace(window, focused);
    phase("focus-apply");
    if (layoutEnabled) { layout->setEnabled(true); layout->activate(); }
    for (auto *layout : window->findChildren<QLayout *>()) layout->activate();
    phase("focus-layout");
    if (editor) {
      // At the beginning of the document, the taller normal toolbar must not
      // scroll the first line underneath its viewport just to keep a screen Y.
      editor->verticalScrollBar()->setValue(atTop ? editor->verticalScrollBar()->minimum() :
        editor->verticalScrollBar()->value() + editor->viewport()->mapTo(window, editor->cursorRect(anchor).topLeft()).y() - anchorY);
      editor->horizontalScrollBar()->setValue(scrollX);
    }
    if (view) {
      const QPoint delta = view->mapFromScene(center) - view->viewport()->mapFrom(window, window->rect().center());
      view->horizontalScrollBar()->setValue(view->horizontalScrollBar()->value() + delta.x());
      view->verticalScrollBar()->setValue(view->verticalScrollBar()->value() + delta.y());
    }
    if(editor)reading+=QRect(editor->mapTo(window,QPoint()),editor->size());
    if(view)reading+=QRect(view->viewport()->mapTo(window,QPoint()),view->viewport()->size());
    veil->protectReading(reading);
    positionFocusControls(window); position();
    qApp->setProperty("_texlineWidthRelayoutDocument",priorWidthDocument);
    phase("focus-anchor");
    if (!animate) { window->setUpdatesEnabled(true); window->update(); return; }
    veil->setFrame(std::move(previous)); veil->show(); veil->raise();
    window->setUpdatesEnabled(true);
    fade.setDuration(workspaceTransitionDuration); fade.setStartValue(0.0); fade.setEndValue(1.0); fade.start();fade.pause();
    frameClock.start();frames.start(motionFrameInterval(window));
    // An inactive surface's animation clock may stop or lag behind real time.
    // The final layout is already applied; release its old frame independently
    // rather than leaving stale text visible until another animation tick.
    deadline.start(fade.duration());
    window->update();
    phase("focus-done");
  }
protected:
  bool eventFilter(QObject *object, QEvent *event) override {
    switch (event->type()) {
      case QEvent::KeyPress: case QEvent::InputMethod:
      case QEvent::MouseButtonPress: case QEvent::Wheel:
      case QEvent::Hide: case QEvent::DevicePixelRatioChange:
        cancel(); break;
      default: break;
    }
    return QObject::eventFilter(object, event);
  }
private:
  QMainWindow *window;
  QPointer<ChromeVeil> veil;
  QVariantAnimation fade;
  QTimer deadline;
  QTimer frames;QElapsedTimer frameClock;
};
void refreshStyle(QWidget *w) {
  w->style()->unpolish(w); w->style()->polish(w); w->update();
  for (auto *child : w->findChildren<QWidget *>()) {
    if (child->window() == w) {
      // Stylesheet qproperty-icon rules must not undo the compact toolbar's icon choices.
      auto *button = qobject_cast<QAbstractButton *>(child);
      const QIcon icon = button ? button->icon() : QIcon();
      child->style()->unpolish(child); child->style()->polish(child);
      if (button) button->setIcon(icon);
      child->update();
    }
  }
}
void refreshWorkspaceStyle(QMainWindow *window) {
  // Only these surfaces have focus-dependent stylesheet rules. Repolishing
  // every child also asks the source editor to refresh an unchanged style and
  // can invalidate a long document's layout before the transition even starts.
  QList<QWidget *> surfaces{window,window->centralWidget(),window->menuBar()};
  for(auto *bar:window->findChildren<QToolBar *>()) {
    surfaces.append(bar);
    if(auto *compile=bar->findChild<QToolButton *>(QStringLiteral("researchCompile")))surfaces.append(compile);
  }
  for(auto *view:window->findChildren<QGraphicsView *>()) {
    surfaces.append(view);surfaces.append(view->viewport());
    surfaces.append(view->horizontalScrollBar());surfaces.append(view->verticalScrollBar());
    if(view->cornerWidget())surfaces.append(view->cornerWidget());
  }
  QSet<QWidget *> refreshed;
  for(auto *widget:surfaces)if(widget&&widget->window()==window&&!refreshed.contains(widget)) {
    refreshed.insert(widget);
    auto *button=qobject_cast<QAbstractButton *>(widget);const QIcon icon=button?button->icon():QIcon();
    widget->style()->unpolish(widget);widget->style()->polish(widget);
    if(button)button->setIcon(icon);widget->update();
  }
}
void applyMaterial(QWidget *w) {
#ifdef Q_OS_WIN
  if (!w->isWindow()) return;
  const bool translucent = w->testAttribute(Qt::WA_TranslucentBackground);
  const HWND hwnd = reinterpret_cast<HWND>(w->winId());
  const bool configure = !w->property("researchDwmConfigured").toBool() ||
    w->property("researchDwmHandle").toULongLong() != reinterpret_cast<quintptr>(hwnd);
  // These calls synchronously cross into the compositor. Do not repeatedly
  // submit unchanged colors/corners when switching modes or restoring a window.
  auto attribute = [w, hwnd, configure](int number, int value) {
    const QByteArray key = QByteArray("researchDwmAttribute") + QByteArray::number(number);
    const QByteArray success = key + "Succeeded";
    if (!configure && w->property(key.constData()).isValid() && w->property(key.constData()).toInt() == value)
      return w->property(success.constData()).toBool();
    const bool ok = SUCCEEDED(DwmSetWindowAttribute(hwnd, static_cast<DWMWINDOWATTRIBUTE>(number), &value, sizeof(value)));
    w->setProperty(key.constData(), value); w->setProperty(success.constData(), ok); return ok;
  };
  attribute(20, FALSE);
  // Use one opaque paper surface in both workspaces. Ignore the retired
  // appearance preference without rewriting the user's existing settings.
  attribute(38, 1); // DWMWA_SYSTEMBACKDROP_TYPE: DWMSBT_NONE (Windows 11).
  if (qobject_cast<QMainWindow *>(w)) {
    attribute(34, RGB(220,230,234));
    // The shared title row is drawn by Qt; no native caption colors needed.
  }
  if (configure || !w->property("researchDwmExtended").isValid()) {
    const int border = translucent ? 0 : 1;
    const MARGINS margins{border, border, border, border};
    DwmExtendFrameIntoClientArea(hwnd, &margins); w->setProperty("researchDwmExtended", false);
    w->setProperty("researchDwmFrameBorder", border);
  }
  if (translucent) {
    // Popup rounding belongs to its alpha surface; a DWM glass edge or native
    // shadow paints a rectangular backing outside that surface on Windows.
    attribute(DWMWA_NCRENDERING_POLICY, DWMNCRP_DISABLED);
  }
  if (w->property("researchWorkspaceReady").toBool() && !w->isFullScreen()) {
    attribute(DWMWA_NCRENDERING_POLICY, DWMNCRP_ENABLED);
    attribute(DWMWA_TRANSITIONS_FORCEDISABLED, FALSE);
    // Keep the native caption/resize styles for system animations and Snap.
    // WM_NCCALCSIZE supplies the custom client and the one-pixel normal edge.
    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    const LONG_PTR framed = style | WS_CAPTION | WS_THICKFRAME;
    if (framed != style) SetWindowLongPtrW(hwnd, GWL_STYLE, framed);
    if (configure || framed != style || !w->property("researchPaperFrame").toBool()) {
      w->setProperty("researchPaperFrame", true);
      SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER);
    }
  }
  attribute(33, translucent ? 1 : 2);
  w->setProperty("researchDwmConfigured", true);
  w->setProperty("researchDwmHandle", QVariant::fromValue<qulonglong>(reinterpret_cast<quintptr>(hwnd)));
#endif
  if (!w->property("researchMaterialReady").toBool()) {
    w->setProperty("researchMaterialReady", true);
    refreshStyle(w);
  }
  w->setProperty("researchMaterial", QStringLiteral("Solid"));
}
class Appearance final : public QObject, public QAbstractNativeEventFilter {
  bool altPressed{false};
public:
  explicit Appearance(QApplication *app) : QObject(app) { app->installEventFilter(this); app->installNativeEventFilter(this); }
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  bool nativeEventFilter(const QByteArray &, void *message, qintptr *result) override {
#else
  bool nativeEventFilter(const QByteArray &, void *message, long *result) override {
#endif
#ifdef Q_OS_WIN
    auto *msg = static_cast<MSG *>(message);
    if (msg->message == WM_MOUSEMOVE || msg->message == WM_NCMOUSEMOVE) {
      auto *receiver = QWidget::find(reinterpret_cast<WId>(msg->hwnd));
      auto *window = receiver ? qobject_cast<QMainWindow *>(receiver->window()) : nullptr;
      if (window && window->property("researchWorkspaceReady").toBool()) {
        // The hidden header includes bare HWND client pixels. Receive their
        // native motion even when Qt routes no tracked child mouse event.
        // Translate through the native client before applying Qt's DPR: a
        // global coordinate cannot be divided uniformly across monitors.
        POINT point{GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam)};
        if (msg->message == WM_MOUSEMOVE) ClientToScreen(msg->hwnd, &point);
        ScreenToClient(reinterpret_cast<HWND>(window->winId()), &point);
        const QPoint local(qRound(point.x / window->devicePixelRatioF()), qRound(point.y / window->devicePixelRatioF()));
        if (auto *controls = paperControls(window))
          controls->motion(window->mapToGlobal(local));
      }
      return false;
    }
    if (msg->message != WM_GETMINMAXINFO && msg->message != WM_NCCALCSIZE && msg->message != WM_NCHITTEST && msg->message != WM_NCPAINT && msg->message != WM_NCACTIVATE) return false;
    auto *window = qobject_cast<QMainWindow *>(QWidget::find(reinterpret_cast<WId>(msg->hwnd)));
    if (!window || !window->property("researchWorkspaceReady").toBool()) return false;
    if (msg->message == WM_GETMINMAXINFO) {
      if (!window->isFullScreen()) {
        MONITORINFO monitor{}; monitor.cbSize = sizeof(monitor);
        if (GetMonitorInfoW(MonitorFromWindow(msg->hwnd, MONITOR_DEFAULTTONEAREST), &monitor)) {
          // Supply the work area; Windows may add invisible resize borders to
          // GetWindowRect. The visible DWM frame and client must exclude the taskbar.
          auto *limits = reinterpret_cast<MINMAXINFO *>(msg->lParam);
          limits->ptMaxPosition = {monitor.rcWork.left - monitor.rcMonitor.left,
                                   monitor.rcWork.top - monitor.rcMonitor.top};
          limits->ptMaxSize = {monitor.rcWork.right - monitor.rcWork.left,
                              monitor.rcWork.bottom - monitor.rcWork.top};
        }
      }
      // Qt still applies the window's minimum and maximum tracking sizes.
      return false;
    }
    // DWM owns the real outer edge, including transparent rounded corners.
    if (msg->message == WM_NCPAINT || msg->message == WM_NCACTIVATE) return false;
    if (msg->message == WM_NCCALCSIZE) {
      RECT *client = msg->wParam ? &reinterpret_cast<NCCALCSIZE_PARAMS *>(msg->lParam)->rgrc[0] : reinterpret_cast<RECT *>(msg->lParam);
      if (IsZoomed(msg->hwnd) && !window->isFullScreen()) {
        MONITORINFO monitor{}; monitor.cbSize = sizeof(monitor);
        if (GetMonitorInfoW(MonitorFromWindow(msg->hwnd, MONITOR_DEFAULTTONEAREST), &monitor)) *client = monitor.rcWork;
      } else if (!window->isFullScreen()) {
        // Keep the native side/bottom resize margins. Qt 6.10 accounts for
        // these when calculating client width; stripping them causes clipping.
        // Only the caption is replaced, with a one-pixel top edge for DWM.
        const LONG top = client->top;
        DefWindowProcW(msg->hwnd, WM_NCCALCSIZE, msg->wParam, msg->lParam);
        client->top = top + 1;
      }
      *result = 0; return true;
    }
    const POINT screen{GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam)};
    RECT rect{}; GetClientRect(msg->hwnd, &rect);
    POINT client = screen; ScreenToClient(msg->hwnd, &client);
    const QPoint point(qRound(client.x / window->devicePixelRatioF()), qRound(client.y / window->devicePixelRatioF()));
    bool interactive = false;
    for (auto *child = window->childAt(point); child && child != window; child = child->parentWidget())
      if (qobject_cast<QAbstractButton *>(child) || qobject_cast<QMenuBar *>(child)) { interactive = true; break; }
    // Caption controls occupy the top edge. Their full
    // click area takes precedence over the invisible resize band.
    if (interactive) { *result = HTCLIENT; return true; }
    const int edge = qMax(1, qRound(ControlMetrics::Gap * window->devicePixelRatioF()));
    if (!IsZoomed(msg->hwnd) && !window->isFullScreen()) {
      const bool left = client.x < edge, right = client.x >= rect.right - edge;
      const bool top = client.y < edge, bottom = client.y >= rect.bottom - edge;
      if (left || right || top || bottom) {
        *result = top ? (left ? HTTOPLEFT : right ? HTTOPRIGHT : HTTOP) : bottom ? (left ? HTBOTTOMLEFT : right ? HTBOTTOMRIGHT : HTBOTTOM) : left ? HTLEFT : HTRIGHT;
        return true;
      }
    }
    *result = HTCLIENT;
    if (!window->isFullScreen() && point.y() >= 0) {
      if (!window->property("researchFocus").toBool()) {
        if (point.y() < paperHeaderHeight(window)) *result = HTCAPTION;
      } else {
        auto *chrome = window->findChild<QWidget *>(QStringLiteral("researchFocusChrome"));
        // Source text must never become a native window-drag region, even
        // while the overlay is fading out above the compact editor.
        auto *find = window->findChild<QWidget *>(QStringLiteral("researchFindBar"));
        const bool sourceWindow = window->findChild<QTextEdit *>(QStringLiteral("textEdit"));
        if ((chrome && chrome->isVisible() && chrome->isEnabled() && chrome->geometry().contains(point) &&
             (!sourceWindow || point.y() < paperHeaderHeight(window))) ||
            (!qobject_cast<QGraphicsView *>(window->centralWidget()) && (!find || !find->isVisible()) && !window->menuBar()->isVisible() && point.y() < 24)) *result = HTCAPTION;
      }
    }
    return true;
#else
    Q_UNUSED(message); Q_UNUSED(result); return false;
#endif
  }
  bool eventFilter(QObject *o, QEvent *e) override {
#ifdef Q_OS_WIN
    if (e->type() == QEvent::MouseMove || e->type() == QEvent::NonClientAreaMouseMove || e->type() == QEvent::KeyPress || e->type() == QEvent::MouseButtonPress || e->type() == QEvent::Wheel || e->type() == QEvent::InputMethod || e->type() == QEvent::WindowStateChange || e->type() == QEvent::WindowTitleChange || e->type() == QEvent::ModifiedChange) {
      auto *widget = qobject_cast<QWidget *>(o);
      auto *window = widget ? qobject_cast<QMainWindow *>(widget->window()) : nullptr;
      if (window && (e->type() == QEvent::KeyPress || e->type() == QEvent::MouseButtonPress || e->type() == QEvent::Wheel || e->type() == QEvent::InputMethod))
        if (auto *transition = static_cast<WorkspaceTransition *>(window->findChild<QObject *>(QStringLiteral("researchWorkspaceTransition"), Qt::FindDirectChildrenOnly))) transition->cancel();
      if (window) if (auto *controls = paperControls(window)) {
        if (e->type() == QEvent::MouseMove || e->type() == QEvent::NonClientAreaMouseMove) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
          controls->motion(static_cast<QMouseEvent *>(e)->globalPosition().toPoint());
#else
          controls->motion(static_cast<QMouseEvent *>(e)->globalPos());
#endif
        }
        else if (e->type() == QEvent::KeyPress) { if (window->property("researchFocus").toBool()) controls->keystroke(widget); }
        else if (e->type() == QEvent::WindowStateChange) { paperContentsMargins(window, window->property("researchFocus").toBool()); controls->updateMaximize(); }
        else controls->updateTitle();
      }
    }
#endif
    if (e->type() == QEvent::KeyPress || e->type() == QEvent::KeyRelease) {
      auto *key = static_cast<QKeyEvent *>(e);
      if (e->type() == QEvent::KeyPress) {
        altPressed = key->key() == Qt::Key_Alt && !key->isAutoRepeat();
      } else if (key->key() == Qt::Key_Alt && altPressed) {
        altPressed = false;
        auto *widget = qobject_cast<QWidget *>(o);
        auto *window = widget ? qobject_cast<QMainWindow *>(widget->window()) : nullptr;
        if (window && window->property("researchWorkspaceReady").toBool()) {
          const bool show = !window->menuBar()->isVisible();
          window->menuBar()->setVisible(show);
          positionFocusControls(window);
          if (show) window->menuBar()->setFocus();
          return true;
        }
      }
      return false;
    }
    // Most traffic is input, timers and PDF render callbacks. Do not inspect
    // widget types or window properties for events the appearance never uses.
    switch (e->type()) {
      case QEvent::ToolTip:
      case QEvent::LayoutRequest:
      case QEvent::Paint:
      case QEvent::Polish:
      case QEvent::FontChange:
      case QEvent::Show:
      case QEvent::Hide:
      case QEvent::ThemeChange:
      case QEvent::StyleChange:
      case QEvent::Resize:
        break;
      default:
        return false;
    }
    auto *w = qobject_cast<QWidget *>(o);
    if(auto *status=qobject_cast<QStatusBar *>(w);status&&(e->type()==QEvent::Show||e->type()==QEvent::LayoutRequest||e->type()==QEvent::FontChange||e->type()==QEvent::StyleChange))compactStatusBar(status);
    if(e->type()==QEvent::Show)if(auto *box=qobject_cast<QMessageBox*>(w);box&&box->icon()==QMessageBox::Warning){
      // The native warning is 48px and dominates a two-line writing prompt.
      // Keep the shared SVG geometry square at every device scale.
      auto icon=QIcon(QStringLiteral(":/research/icons/codicons/warning.svg")).pixmap(QSize(24,24),box->devicePixelRatioF());
      {QPainter tint(&icon);tint.setCompositionMode(QPainter::CompositionMode_SourceIn);tint.fillRect(icon.rect(),QColor(QStringLiteral("#98702e")));}
      box->setIconPixmap(icon);
      if(auto *label=box->findChild<QLabel*>(QStringLiteral("qt_msgboxex_icon_label"))){label->setScaledContents(false);label->setFixedSize(24,24);}
    }
    if(e->type()==QEvent::Show&&qobject_cast<QDialog *>(w)&&dynamic_cast<MaterialDialog *>(w)) {
      // Flexible list/preview areas shrink before the footer leaves the screen.
      const QSize available=w->screen()->availableGeometry().size()-QSize(24,64);
      w->setMinimumSize(w->minimumSize().boundedTo(available));w->resize(w->size().boundedTo(available));
    }

    if(w && w->isWindow() && (e->type()==QEvent::Polish||e->type()==QEvent::Show)) {
      const bool popup=qobject_cast<QMenu *>(w)||w->windowType()==Qt::ToolTip||w->windowType()==Qt::Popup;
      if (popup && e->type() == QEvent::Polish) preparePopupSurface(w);
      if(popup) {
        w->setAttribute(Qt::WA_TranslucentBackground);
        w->setAutoFillBackground(false);
        if(qobject_cast<QComboBox *>(w->parentWidget())) {
          // The native combo container paints its own square panel behind the
          // rounded item view. Keep that container clear as well.
          w->setObjectName(QStringLiteral("researchComboPopup"));
          w->setStyleSheet(QStringLiteral("QFrame#researchComboPopup { background: transparent; border: none; }"));
        }
      }
    }
    if (w && w->isWindow() && w->testAttribute(Qt::WA_TranslucentBackground) && e->type() == QEvent::Show) applyMaterial(w);
    if (w && (e->type() == QEvent::Polish || e->type() == QEvent::FontChange || e->type()==QEvent::Show)) {
      // A stylesheet or an item view can resolve a fresh platform font after
      // application-font inheritance. Keep the same rendering policy without
      // changing its family, size or emphasis. The second FontChange is a no-op.
      const auto current = w->font();
      auto smooth = smoothTextFont(current);
      if(w->window()->testAttribute(Qt::WA_TranslucentBackground))
        smooth.setStyleStrategy(QFont::StyleStrategy(int(smooth.styleStrategy())|QFont::NoSubpixelAntialias));
      if (smooth.styleStrategy() != current.styleStrategy() || smooth.hintingPreference() != current.hintingPreference())
        w->setFont(smooth);
    }
    if (e->type() == QEvent::Show && w && (eligible(w) || w->objectName().startsWith(QStringLiteral("research")))) retranslateWorkspace(w);
    if (e->type() == QEvent::Polish || e->type() == QEvent::Show || e->type() == QEvent::StyleChange) {
      if (auto *bar = qobject_cast<QToolBar *>(w); bar && bar->property("researchControls").toBool() && !bar->property("researchMetricsQueued").toBool()) {
        bar->setProperty("researchMetricsQueued", true);
        QTimer::singleShot(0, bar, [bar] {
          bar->setProperty("researchMetricsQueued", false);
          // QToolBar refreshes layout margins/spacing after style events.
          // Apply our shared layout metrics after that refresh has finished.
          applyToolbarMetrics(bar);
        });
      }
    }
    if (e->type() == QEvent::ToolTip) {
      auto *button = qobject_cast<QToolButton *>(w);
      if (button && (button->objectName().startsWith(QStringLiteral("researchWindow"))
          || (button->objectName().startsWith(QStringLiteral("research")) && button->objectName().contains(QStringLiteral("Close")))
          )) {
        e->accept();
        return true;
      }
      if (button && button->toolButtonStyle() != Qt::ToolButtonIconOnly) {
        auto plain = [](QString s) { return s.remove(QLatin1Char('&')).remove(QChar(0x2026)).simplified(); };
        if (plain(button->toolTip()) == plain(button->text())) {
          // Text buttons already explain themselves; keep informative and icon-only tips.
          e->accept();
          return true;
        }
      }
    }
    if (w && qobject_cast<QToolBar *>(w) && e->type() == QEvent::LayoutRequest) {
      auto *window = w->window();
      queueFocusPosition(window);
    }
    if (!eligible(w)) return false;
    if (e->type() == QEvent::Hide)
      if (auto *window = qobject_cast<QMainWindow *>(w))
        if (auto *transition = static_cast<WorkspaceTransition *>(window->findChild<QObject *>(QStringLiteral("researchWorkspaceTransition"), Qt::FindDirectChildrenOnly))) transition->cancel();
    if (e->type() == QEvent::LayoutRequest) {
      if (qobject_cast<QMainWindow *>(w)) queueFocusPosition(w);
      return false;
    }
    if (e->type() == QEvent::Resize) {
      if (auto *window = qobject_cast<QMainWindow *>(w)) {
        positionFocusMessage(window);
        positionFocusControls(window);
        if (auto *transition = static_cast<WorkspaceTransition *>(window->findChild<QObject *>(QStringLiteral("researchWorkspaceTransition"), Qt::FindDirectChildrenOnly))) transition->position();
      }
      return false;
    }
    if (e->type() == QEvent::Paint && qobject_cast<QMainWindow *>(w)) {
      // Clear the complete paper frame, including temporary layout gaps.
      QPainter painter(w);
      painter.setClipRegion(static_cast<QPaintEvent *>(e)->region());
      painter.setCompositionMode(QPainter::CompositionMode_Source);
      painter.fillRect(static_cast<QPaintEvent *>(e)->rect(), w->property("researchFocus").toBool() ? Qt::white : QColor(235, 245, 249));
    } else if (e->type() == QEvent::Polish) {
      w->setAttribute(Qt::WA_StyledBackground);
    } else if (e->type() == QEvent::Show) {
      applyMaterial(w);
      // A saved window state can restore old toolbar visibility before show.
      // The current workspace mode takes precedence over that saved chrome.
      if (auto *window = qobject_cast<QMainWindow *>(w); window && window->property("researchWorkspaceReady").toBool()) {
        syncWorkspaceChrome(window);
        QTimer::singleShot(0, window, [window] {
          syncWorkspaceChrome(window);
          const QVariant pending = window->property("researchPendingHalfScreen");
          if (pending.isValid()) {
            window->setProperty("researchPendingHalfScreen", QVariant());
            Tw::Utils::WindowManager::zoomToHalfScreen(window, pending.toBool());
          } else if (window->property("researchRestoreHalfScreen").toBool())
            Tw::Utils::WindowManager::correctPaperHalfScreen(window);
        });
      }
    } else if (e->type() == QEvent::ThemeChange) {
      w->setProperty("researchDwmConfigured", false);
      QTimer::singleShot(0, w, [w] { applyMaterial(w); });
    }
    return false;
  }
};
}
QFont smoothTextFont(QFont font) {
#ifdef Q_OS_WIN
  // Opaque writing surfaces use the Windows rasterizer's full subpixel
  // coverage. Only translucent popups request grayscale coverage at polish.
  const int strategy = (int(font.styleStrategy()) & ~(int(QFont::NoAntialias)|int(QFont::NoSubpixelAntialias)))
      | QFont::PreferAntialias | QFont::PreferOutline;
  font.setStyleStrategy(QFont::StyleStrategy(strategy));
  font.setHintingPreference(QFont::PreferVerticalHinting);
#endif
  return font;
}
namespace {
void paintPopupPanel(QWidget *surface) {
  QPainter painter(surface);painter.setRenderHint(QPainter::Antialiasing);
  painter.setPen(QPen(QColor(QStringLiteral("#d7e1e6")),1));painter.setBrush(Qt::white);
  painter.drawRoundedRect(QRectF(surface->rect()).adjusted(.5,.5,-.5,-.5),8,8);
}
}
void PopupFrame::paintEvent(QPaintEvent *) { paintPopupPanel(this); }
bool PopupListView::event(QEvent *event) {
  if(event->type()==QEvent::Paint&&isWindow()&&testAttribute(Qt::WA_TranslucentBackground)) {
    // QAbstractScrollArea handles the frame separately from its viewport.
    // On a translucent HWND, a stylesheet background alone can leave the
    // unselected rows transparent. Paint the entire rounded frame explicitly.
    paintPopupPanel(this);return true;
  }
  return QListView::event(event);
}
void preparePopupSurface(QWidget *popup) {
  // A child view may already be polished before being changed into a tooltip.
  // Set all surface flags explicitly at that conversion, before the HWND exists.
  const auto flags=popup->windowFlags()|Qt::FramelessWindowHint|Qt::NoDropShadowWindowHint;
  if(popup->windowFlags()!=flags)popup->setWindowFlags(flags);
  popup->setAttribute(Qt::WA_TranslucentBackground);
  popup->setAttribute(Qt::WA_StyledBackground);
  popup->setAutoFillBackground(false);
}
void installAppearance(QApplication *app) { new Appearance(app); }
bool englishUi() { return uiEnglish; }
bool traditionalUi() { return uiTraditional; }
QString uiLocale() { return uiEnglish ? QStringLiteral("en_US") : uiTraditional ? QStringLiteral("zh_TW") : QStringLiteral("zh_CN"); }
QString uiSearchText(const QString &text) { return foldUiSearch(text); }
QString scriptUi(const QString &title, const QString &text) {
  // Only localize our bundled research dialogs. Third-party script content,
  // document titles and choice values retain their original meaning.
  for (const auto &source : {QStringLiteral("跳转到定义"), QStringLiteral("选取当前证明或环境"), QStringLiteral("章节与定理导航")})
    if (title == source || title == ui(source, true)) return displayText(text, uiEnglish);
  return text;
}
QString supportedUiLocale(const QString &locale) { const auto tag=locale.toLower().replace(QLatin1Char('_'),QLatin1Char('-')); return tag.startsWith(QStringLiteral("zh")) ? (tag.contains(QStringLiteral("hant"))||tag==QStringLiteral("zh-tw")||tag==QStringLiteral("zh-hk")||tag==QStringLiteral("zh-mo") ? QStringLiteral("zh_TW") : QStringLiteral("zh_CN")) : QStringLiteral("en_US"); }
void setUiLocale(const QString &locale) { const auto supported=supportedUiLocale(locale);uiEnglish=supported==QStringLiteral("en_US");uiTraditional=supported==QStringLiteral("zh_TW"); }
QString ui(const QString &source, bool english) { return english ? englishStrings().value(source, source) : source; }
QString ui(const QString &source) { return uiTraditional ? traditionalStrings().value(source,source) : ui(source, uiEnglish); }
void setLocalizedHtml(QTextBrowser *browser, const QString &chinese, const QString &english) {
  browser->setProperty("researchHtmlChinese", chinese); browser->setProperty("researchHtmlEnglish", english);
  browser->setProperty("researchHtmlLocale", uiLocale()); browser->setHtml(uiEnglish ? english : ui(chinese));
}
void retranslateWorkspace(QWidget *root) {
  // Designer's LanguageChange resets this borrowed action to the upstream
  // label. Restore our shared first-level label in both workspace menus.
  if (auto *wrap = root->findChild<QAction *>(QStringLiteral("actionWrap_Lines")))
    wrap->setText(ui(QStringLiteral("自动换行")));
  auto property = [](QObject *object, const char *name) {
    const QString current = object->property(name).toString(); if (current.isEmpty()) return;
    const QByteArray key = QByteArray("researchLocaleSource_") + name;
    QString source = object->property(key.constData()).toString();
    if (!source.isEmpty() && current != source && current != ui(source, true) && current != traditionalStrings().value(source,source)) source.clear();
    if (source.isEmpty() && englishStrings().contains(current) && !current.contains(QStringLiteral("%1"))) source = current;
    if (!source.isEmpty()) object->setProperty(key.constData(), source);
    const QString translated = source.isEmpty() ? displayText(current, uiEnglish) : ui(source);
    if (translated != current) object->setProperty(name, translated);
  };
  QList<QPointer<QObject>> objects; objects.append(root);
  // Updating rich text or repolishing a toolbar may delete internal children.
  // Keep guarded UI objects only, rather than stale document/layout pointers.
  for (auto *object : root->findChildren<QObject *>())
    if (qobject_cast<QWidget *>(object) || qobject_cast<QAction *>(object)) objects.append(object);
  // Shared actions belong to the document window or an action group, not
  // necessarily the menu opening now. Include that menu's borrowed entries.
  if (auto *menu = qobject_cast<QMenu *>(root))
    for (auto *action : menu->actions()) if (!objects.contains(QPointer<QObject>(action))) objects.append(action);
  for (const auto &guard : objects) {
    auto *object = guard.data(); if (!object) continue;
    if (auto *action=qobject_cast<QAction *>(object)) { if(action->objectName()==QStringLiteral("actionPreferences"))action->setText(ui(QStringLiteral("设置…"))); property(object, "text"); property(object, "toolTip"); property(object, "statusTip"); }
    if (qobject_cast<QMenu *>(object)) property(object, "title");
    if (auto *button = qobject_cast<QAbstractButton *>(object)) {
      property(button, "text"); property(button, "toolTip"); property(button, "accessibleName");
      if (auto *tool = qobject_cast<QToolButton *>(button); tool && tool->property("researchControl").toBool()) {
        const auto role = tool->property("researchButtonRole").toString();
        configureButton(tool, role == QStringLiteral("icon") ? ButtonRole::Icon : role == QStringLiteral("menu") ? ButtonRole::Menu : role == QStringLiteral("compile") ? ButtonRole::Compile : role == QStringLiteral("status") ? ButtonRole::Status : ButtonRole::Text);
      } else if (auto *push = qobject_cast<QPushButton *>(button); push && push->window()->objectName() == QStringLiteral("researchPalette")) configureButton(push);
    }
    if (auto *label = qobject_cast<QLabel *>(object); label && label->textFormat() != Qt::RichText) property(label, "text");
    if (qobject_cast<QLineEdit *>(object)) property(object, "placeholderText");
    if (auto *widget = qobject_cast<QWidget *>(object); widget && (widget->isWindow() || qobject_cast<QDockWidget *>(widget))) property(widget, "windowTitle");
    if (auto *tabs = qobject_cast<QTabWidget *>(object)) for (int i = 0; i < tabs->count(); ++i) tabs->setTabText(i, displayText(tabs->tabText(i), uiEnglish));
    if (auto *browser = qobject_cast<QTextBrowser *>(object); browser && browser->property("researchHtmlChinese").isValid() && browser->property("researchHtmlLocale").toString() != uiLocale()) {
      const int scroll = browser->verticalScrollBar()->value();
      browser->setHtml(uiEnglish?browser->property("researchHtmlEnglish").toString():ui(browser->property("researchHtmlChinese").toString()));
      browser->setProperty("researchHtmlLocale", uiLocale()); browser->verticalScrollBar()->setValue(scroll);
    }
    if (auto *status = qobject_cast<QStatusBar *>(object)) {
      const QString message = displayText(status->currentMessage(), uiEnglish);
      if (message != status->currentMessage()) {
        if (auto *window = qobject_cast<QMainWindow *>(status->parentWidget());
            statusController(window) && statusController(window)->isTransient()) {
          showTransientStatusMessage(window, message, statusController(window)->remainingTime());
          continue;
        }
        int timeout = 0;
        for (auto *timer : status->findChildren<QTimer *>(QString(), Qt::FindDirectChildrenOnly)) if (timer->isActive()) timeout = qMax(1, timer->remainingTime());
        status->showMessage(message, timeout);
      }
    }
  }
  if (auto *body = root->findChild<QWidget *>(QStringLiteral("researchReviewBody"))) {
    const auto buttons = body->findChildren<QPushButton *>(); int width = 0;
    for (auto *button : buttons) { configureButton(button); width = qMax(width, button->width()); }
    for (auto *button : buttons) configureButton(button, width);
  }
}
void refreshFocusMessage(QMainWindow *window) {
  auto *message = window->findChild<QLabel *>(QStringLiteral("researchFocusMessage"));
  if (!message) return;
  if (!window->property("researchFocus").toBool() && !(statusController(window) && statusController(window)->isTransient())) { message->hide(); return; }
  QString text = window->property("researchFocusStatus").toString();
  // The PDF version warning must survive hiding its usual status-bar row.
  if (text.isEmpty()) if (auto *revision = window->findChild<QLabel *>(QStringLiteral("researchPdfRevision")); revision && !revision->isHidden() && !revision->property("researchRoutineRevision").toBool()) text = revision->text();
  if (text.isEmpty()) { message->hide(); return; }
  message->setToolTip(text); message->show(); positionFocusMessage(window);
}
void showTransientStatusMessage(QMainWindow *window, const QString &text, int milliseconds, NoticePriority priority) {
  if (!window) return;
  if (auto *controller = statusController(window)) {
    if(controller->isTransient()&&controller->remainingTime()>1&&(controller->priority>priority||window->statusBar()->currentMessage()==text))return;
    // Reset explicitly too: QStatusBar does not emit when the text is unchanged.
    controller->reset();
    window->setProperty("researchFocusStatus", text);
    window->statusBar()->showMessage(text);
    controller->arm(milliseconds);
    controller->priority=priority;
  } else window->statusBar()->showMessage(text, qMax(1, milliseconds));
}
void configureButton(QToolButton *button, ButtonRole role) {
  // Status text sizes naturally with its state; toolbar fixed metrics would
  // clip it after a compile or a focus transition.
  if (button->objectName() == QStringLiteral("researchBuildSummary") || button->property("researchLayoutTile").toBool()) return;
  button->setProperty("researchControl", true);
  const char *name = role == ButtonRole::Icon ? "icon" : role == ButtonRole::Menu ? "menu" : role == ButtonRole::Compile ? "compile" : role == ButtonRole::Status ? "status" : "text";
  button->setProperty("researchButtonRole", QString::fromLatin1(name));
  // Status actions share the compact row with page/zoom fields, including
  // save-failure recovery. Other toolbars keep their existing 32px controls.
  bool inStatusBar = false;
  for (auto *parent = button->parentWidget(); parent; parent = parent->parentWidget())
    if (qobject_cast<QStatusBar *>(parent)) { inStatusBar = true; break; }
  const int height = inStatusBar ? 22 : button->property("researchCompactFocusControl").toBool()?24:ControlMetrics::Height;
  const int fontPixels = inStatusBar ? 13 : ControlMetrics::FontPixels;
  auto font = controlFont(role == ButtonRole::Compile); font.setPixelSize(fontPixels);
  button->setFont(font);
  button->setIconSize(QSize(ControlMetrics::Icon, ControlMetrics::Icon));
  const bool instantMenu = button->menu() && button->popupMode() == QToolButton::InstantPopup && (role == ButtonRole::Text || role == ButtonRole::Status);
  const int disclosureWidth = instantMenu ? 18 : 0;
  const int captionPadding = 16;
  const int captionIcon = role != ButtonRole::Compile && !button->icon().isNull() ? ControlMetrics::Icon + 4 : 0;
  // Qt tests the raw caption for elision before drawing mnemonic markers.
  // Reserve that same width even when an ampersand is not painted.
  const int captionWidth = qMax(button->fontMetrics().horizontalAdvance(button->text()),
    button->fontMetrics().horizontalAdvance(button->property("researchEqualWidthCaption").toString()));
  const int contentWidth = captionWidth + captionPadding + captionIcon + disclosureWidth + 2;
  const int textWidth = qMax(ControlMetrics::TextWidth, (contentWidth + 7) / 8 * 8);
  const bool compactMenu=role==ButtonRole::Menu&&button->property("researchCompactMenu").toBool();
  const int left = role == ButtonRole::Icon ? 0 : compactMenu ? 4 : 8;
  // Keep the split-menu hit target in focus mode too.
  const int right = role == ButtonRole::Icon ? 0 : compactMenu ? 18 : role == ButtonRole::Compile || role == ButtonRole::Menu || instantMenu ? 26 : 8;
  // Reserve only the icon, caption and split-menu target. Use the longest
  // caption in the current language so starting/stopping never shifts peers.
  const int compileTextWidth = std::max({button->fontMetrics().horizontalAdvance(button->window()->property("researchSynchronizedProject").toBool()?ui(QStringLiteral("预览")):ui(QStringLiteral("编译"))),
    button->fontMetrics().horizontalAdvance(button->window()->property("researchSynchronizedProject").toBool()?ui(QStringLiteral("编译")):ui(QStringLiteral("编译"))),
    button->fontMetrics().horizontalAdvance(ui(QStringLiteral("停止"))),
    button->fontMetrics().horizontalAdvance(ui(QStringLiteral("转到")))});
  const int compileWidth = (compileTextWidth + ControlMetrics::Icon + 4 + left + right + 2 + 7) / 8 * 8;
  const int width = compactMenu ? ((button->fontMetrics().horizontalAdvance(button->text())+captionIcon+left+right+2+7)/8*8) : role == ButtonRole::Icon ? ControlMetrics::Height : role == ButtonRole::Compile ? compileWidth : role == ButtonRole::Menu ? qMax(ControlMetrics::MenuWidth, textWidth + 24) : textWidth;
  // QStyleSheetStyle resets QWidget's size limits whenever it is polished.
  // Put the same metrics in the rendering rule so backdrop/focus changes
  // cannot silently replace fixed sizes with label-dependent size hints.
  button->setStyleSheet(QStringLiteral("QToolButton { font-family: 'Segoe UI', 'Microsoft YaHei UI'; font-size: %6px; font-weight: %5; min-width: %1px; max-width: %1px; min-height: %2px; max-height: %2px; padding: 0px %3px 0px %4px; }")
    .arg(width - left - right - 2).arg(height - 2).arg(right).arg(left).arg(role == ButtonRole::Compile ? 500 : 400).arg(fontPixels));
  button->setFixedSize(width, height);
  if(button->property("researchArticleScopeControl").toBool())
    button->setStyleSheet(button->styleSheet()+QStringLiteral("QToolButton {background:transparent;border:0;border-bottom:2px solid transparent;border-radius:0;} QToolButton:checked {border-bottom-color:#51565b;color:#202629;} QToolButton:hover {background:#f0f1f2;}"));
  const auto slideEmphasis=button->property("researchSlideEmphasis").toString();
  if(!slideEmphasis.isEmpty()) {
    const bool primary=slideEmphasis==QStringLiteral("primary");
    button->setStyleSheet(button->styleSheet()+QStringLiteral("QToolButton {color:%1;background:transparent;border:1px solid transparent;border-radius:5px;font-weight:%2;} QToolButton:hover {background:#e9e9ed;} QToolButton:pressed {background:#dedee3;} QToolButton:disabled {color:#98989d;background:transparent;} QToolButton:focus {border-color:#8d98a8;}").arg(primary?QStringLiteral("#1d1d1f"):QStringLiteral("#30363c"),primary?QStringLiteral("500"):QStringLiteral("400")));
  }
}
// State belongs to the existing control: no extra hit target or toolbar slot.
class ControlStatusDot final : public QWidget {
  QToolButton *button;
public:
  explicit ControlStatusDot(QToolButton *owner):QWidget(owner),button(owner) {
    setObjectName(QStringLiteral("researchControlStatusDot"));setAttribute(Qt::WA_TransparentForMouseEvents);setAttribute(Qt::WA_NoSystemBackground);setFixedSize(6,6);button->installEventFilter(this);place();
  }
  void place(){move(button->width()-(button->popupMode()==QToolButton::MenuButtonPopup?23:9),4);raise();}
  bool eventFilter(QObject *,QEvent *event)override{if(event->type()==QEvent::Resize||event->type()==QEvent::StyleChange)place();return false;}
  void paintEvent(QPaintEvent *)override{QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.setPen(Qt::NoPen);p.setBrush(button->property("researchStatusAttention").toBool()?QColor(QStringLiteral("#a97942")):QColor(QStringLiteral("#7b8992")));p.drawEllipse(QRectF(1,1,4,4));}
};
void setControlStatus(QToolButton *button,const QString &hint,bool attention) {
  if(!button)return;
  button->setProperty("researchStatusHint",hint);button->setProperty("researchStatusAttention",attention);button->setAccessibleDescription(hint);
  auto *dot=static_cast<ControlStatusDot *>(button->findChild<QWidget *>(QStringLiteral("researchControlStatusDot"),Qt::FindDirectChildrenOnly));
  if(!dot&&!hint.isEmpty())dot=new ControlStatusDot(button);
  if(dot){dot->setVisible(!hint.isEmpty());dot->place();dot->update();}
}
void setBuildIssueCounts(QMainWindow *window,int errors,int warnings) {
  if(!window)return;
  window->setProperty("researchBuildErrors",errors);window->setProperty("researchBuildWarnings",warnings);
  if(auto *button=window->findChild<QToolButton *>(QStringLiteral("researchCompile")))refreshCompileButton(button);
}
void refreshCompileButton(QToolButton *button) {
  auto *engines = button->findChild<QMenu *>(QStringLiteral("researchEngineMenu"));
  button->setMenu(engines);
  button->setPopupMode(QToolButton::MenuButtonPopup);
  if(!button->property("researchCompileBusy").toBool()){
    const auto caption=button->window()->property("researchCompileIdleCaption").toString();if(!caption.isEmpty())button->setText(ui(caption));
    button->setToolTip(ui(button->window()->property("researchCompileDestinationHint").toString()));
  }
  configureButton(button, ButtonRole::Compile);
  const auto label = button->text();
  button->setIcon(QIcon(label == Research::ui(QStringLiteral("停止")) ? QStringLiteral(":/research/icons/focus-stop.svg") : label == Research::ui(QStringLiteral("转到")) ? QStringLiteral(":/research/icons/focus-jump.svg") : button->window()->property("researchSynchronizedProject").toBool()&&!button->window()->property("researchCacheOnly").toBool()?QStringLiteral(":/research/icons/compile-sync.svg"):QStringLiteral(":/research/icons/focus-play.svg")));
  const int warnings=button->window()->property("researchBuildWarnings").toInt();
  const bool warningOnly=warnings>0&&!button->window()->property("researchBuildErrors").toInt()&&!button->property("researchCompileBusy").toBool()&&label!=ui(QStringLiteral("停止"));
  const QString hint=warningOnly?words("%1 条警告 · 在编译菜单查看","%1 warnings · Open the build menu for details").arg(warnings):QString();
  setControlStatus(button,hint,true);if(warningOnly)button->setToolTip(button->toolTip()+QLatin1Char('\n')+hint);
  if (auto *focus = button->window()->findChild<QToolButton *>(QStringLiteral("researchFocusCompile"))) {focus->setIcon(button->icon());setControlStatus(focus,hint,true);focus->setToolTip(button->toolTip());}
}
void configureButton(QPushButton *button, int minimumWidth) {
  button->setFont(controlFont());
  const int width = button->fontMetrics().horizontalAdvance(button->text().remove(QLatin1Char('&'))) + 26;
  const int overallWidth = qMax(qMax(88, minimumWidth), (width + 7) / 8 * 8);
  button->setStyleSheet(QStringLiteral("QPushButton { font-family: 'Segoe UI', 'Microsoft YaHei UI'; font-size: %3px; min-width: %1px; max-width: %1px; min-height: %2px; max-height: %2px; padding: 0px 12px; }")
    .arg(overallWidth - 26).arg(ControlMetrics::Height - 2).arg(ControlMetrics::FontPixels));
  button->setFixedSize(overallWidth, ControlMetrics::Height);
}
void configureField(QLineEdit *field) {
  field->setFont(controlFont());
  field->setStyleSheet(QStringLiteral("QLineEdit { font-family: 'Segoe UI', 'Microsoft YaHei UI'; font-size: %2px; min-height: %1px; max-height: %1px; padding: 0px 10px; }")
    .arg(ControlMetrics::Height - 2).arg(ControlMetrics::FontPixels));
  field->setFixedHeight(ControlMetrics::Height);
}
void configureToolbar(QToolBar *bar) {
  // A Designer toolbar stylesheet overrides the application stylesheet,
  // even when its selector is less specific. Replace that legacy override.
  const bool workspace = bar->property("researchWorkspace").toBool();
  const int margin = bar->property("researchCompactFind").toBool() ? 2 : workspace ? ControlMetrics::Gap / 2 : ControlMetrics::Gap;
  bar->setStyleSheet(QStringLiteral("QToolBar { padding: %1px; spacing: %2px; }").arg(margin).arg(ControlMetrics::Gap));
  if (workspace && !bar->findChild<QWidget *>(QStringLiteral("researchEdgeInsetLeading"))) {
    auto inset = [bar](const QString &name) {
      auto *space = new QWidget(bar); space->setObjectName(name);
      space->setFixedSize(ControlMetrics::Gap / 2, ControlMetrics::Height);
      space->setAttribute(Qt::WA_TransparentForMouseEvents); return space;
    };
    bar->insertWidget(bar->actions().value(0), inset(QStringLiteral("researchEdgeInsetLeading")));
    bar->addWidget(inset(QStringLiteral("researchEdgeInsetTrailing")));
  }
  bar->setProperty("researchControls", true);
  bar->setFont(controlFont());
  bar->setIconSize(QSize(ControlMetrics::Icon, ControlMetrics::Icon));
  bar->setMovable(false); bar->setFloatable(false);
  for (auto *button : bar->findChildren<QToolButton *>())
    if (!button->property("researchControl").toBool())
      configureButton(button, button->toolButtonStyle() == Qt::ToolButtonIconOnly ? ButtonRole::Icon : ButtonRole::Text);
  applyToolbarMetrics(bar);
}
QToolButton *workspaceButton(QToolBar *bar, const QString &label, const QString &icon, QAction *source) {
  auto *button = new QToolButton(bar);
  button->setText(label);
  if (source && !source->shortcut().isEmpty()) button->setToolTip(label + QStringLiteral(" · ") + source->shortcut().toString(QKeySequence::NativeText));
  if (!icon.isEmpty()) button->setIcon(QIcon(icon));
  button->setToolButtonStyle(label.isEmpty() ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextOnly);
  configureButton(button, label.isEmpty() ? ButtonRole::Icon : ButtonRole::Text);
  if (source) {
    button->setEnabled(source->isEnabled());
    QObject::connect(source, &QAction::changed, button, [source, button]() { button->setEnabled(source->isEnabled()); });
    QObject::connect(button, &QToolButton::clicked, source, &QAction::trigger);
  }
  QAction *before = nullptr;
  if (auto *edge = bar->findChild<QWidget *>(QStringLiteral("researchEdgeInsetTrailing")))
    for (auto *action : bar->actions())
      if (bar->widgetForAction(action) == edge) { before = action; break; }
  bar->insertWidget(before, button);
  return button;
}
void setFocusMode(bool focused) {
  if(focused)for(auto *window:TeXDocumentWindow::documentList())if(!commitSlideNotes(window))return;
  Tw::Settings().setValue(QStringLiteral("researchFocusMode"), focused);
  // Applying one workspace can dispatch events and retire another window.
  // Keep guarded references throughout this multi-window transition.
  QList<QPointer<QMainWindow>> windows;
  for (auto *widget : QApplication::topLevelWidgets())
    if (auto *window = qobject_cast<QMainWindow *>(widget)) windows.append(window);
  for (const auto &guard : windows) {
    auto *window = guard.data();
    if (window && window->property("researchWorkspaceReady").toBool()) {
      auto *transition = static_cast<WorkspaceTransition *>(window->findChild<QObject *>(QStringLiteral("researchWorkspaceTransition"), Qt::FindDirectChildrenOnly));
      if (!transition) transition = new WorkspaceTransition(window);
      transition->start(focused);
    }
  }
}
void restoreWorkspace(QMainWindow *window) {
  if (window && window->property("researchWorkspaceReady").toBool())
    applyWorkspace(window, window->property("researchFocus").toBool(), true);
}
void installWorkspace(QMainWindow *window, QToolBar *bar, QMenu *more) {
  window->setProperty("researchWorkspaceReady", true);
  bar->setProperty("researchWorkspace", true);
  configureToolbar(bar);
  const auto previous = more->actions();
  for (auto *action : previous) more->removeAction(action);
  const bool source = window->findChild<QAction *>(QStringLiteral("actionSave"));
  auto group = [more](const QString &title, const QString &name) {
    auto *menu = more->addMenu(Research::ui(title)); menu->setObjectName(name); return menu;
  };
  auto named = [window](QMenu *menu, const char *name) { if(auto *a=window->findChild<QAction *>(QString::fromLatin1(name))) menu->addAction(a); };
  named(more,"actionFind");
  if(source)named(more,"researchRelatedAction");
  auto *files = group(QStringLiteral("文件"), QStringLiteral("researchFileMenu"));
  auto *edit = group(QStringLiteral("编辑"), QStringLiteral("researchEditMenu"));
  QMenu *insert = source ? group(QStringLiteral("插入"), QStringLiteral("researchInsertMenu")) : nullptr;
  auto *reading = group(QStringLiteral("查看"), QStringLiteral("researchReadingMenu"));
  auto *build = group(QStringLiteral("编译"), QStringLiteral("researchBuildMenu"));
  const QHash<QString,QMenu *> groups{{QStringLiteral("file"),files},{QStringLiteral("edit"),edit},{QStringLiteral("insert"),insert},{QStringLiteral("view"),reading},{QStringLiteral("build"),build}};
  for(const auto &definition:commandDefinitions()) if(auto *menu=groups.value(QString::fromLatin1(definition.group))) named(menu,definition.id);
  if(auto *recent=window->findChild<QMenu *>(QStringLiteral("menuOpen_Recent")))files->insertMenu(files->actions().value(3),recent);
  if(!source)named(files,"actionPrintPdf");
  if(auto *engines=window->findChild<QMenu *>(QStringLiteral("researchEngineMenu"))){engines->setTitle(ui(QStringLiteral("编译器")));build->addMenu(engines);}
  auto *compile = bar->findChild<QToolButton *>(QStringLiteral("researchCompile"));
  if(!source) {
    if(auto *view=bar->findChild<QToolButton *>(QStringLiteral("researchViewMenu"));view&&view->menu())for(auto *a:view->menu()->actions())reading->addAction(a);
    for(auto *a:previous)if(a->menu())reading->addMenu(a->menu());
  }
  auto *settings = new QMenu(ui(QStringLiteral("帮助")),window);settings->setObjectName(QStringLiteral("researchSettingsMenu"));
  if(auto *help=window->findChild<QMenu *>(QStringLiteral("menuHelp")))for(auto *a:help->actions())settings->addAction(a);
  auto *message = new QLabel(window);
  message->setObjectName(QStringLiteral("researchFocusMessage"));
  message->hide();
  new StatusMessageController(window, message);
  QObject::connect(window->statusBar(), &QStatusBar::messageChanged, message, [window](const QString &text) {
    window->setProperty("researchFocusStatus", text);
    refreshFocusMessage(window);
  });
  if(auto *editor=window->findChild<QTextEdit *>(QStringLiteral("textEdit"))) {
    const auto place=[window]{if(window->property("researchFocus").toBool()&&!window->property("researchFocusStatus").toString().isEmpty())refreshFocusMessage(window);};
    QObject::connect(editor,&QTextEdit::cursorPositionChanged,message,place);
    QObject::connect(editor,&QTextEdit::selectionChanged,message,place);
    QObject::connect(editor->verticalScrollBar(),&QScrollBar::valueChanged,message,place);
    QObject::connect(editor->horizontalScrollBar(),&QScrollBar::valueChanged,message,place);
  }
  // Register existing shortcuts on the window even when the menu bar is hidden.
  for (auto *action : window->findChildren<QAction *>()) if (!action->shortcut().isEmpty()) window->addAction(action);
  auto *languages = new QMenu(QStringLiteral("语言 / Language"), window);
  languages->setObjectName(QStringLiteral("researchLanguageMenu"));
  auto *languageGroup = new QActionGroup(languages); languageGroup->setExclusive(true);
  auto *chinese = languages->addAction(QStringLiteral("简体中文"));
  auto *traditional = languages->addAction(QStringLiteral("繁體中文"));
  traditional->setObjectName(QStringLiteral("researchLanguageTraditional"));
  auto *english = languages->addAction(QStringLiteral("English"));
  chinese->setObjectName(QStringLiteral("researchLanguageChinese")); english->setObjectName(QStringLiteral("researchLanguageEnglish"));
  for (auto *action : {chinese, traditional, english}) { action->setCheckable(true); languageGroup->addAction(action); }
  auto syncLanguage = [chinese, traditional, english] { chinese->setChecked(!englishUi()&&!traditionalUi());traditional->setChecked(traditionalUi());english->setChecked(englishUi()); };
  syncLanguage();
  QObject::connect(languages, &QMenu::aboutToShow, window, syncLanguage);
  QObject::connect(TWApp::instance(), &TWApp::updatedTranslators, window, [window, syncLanguage] { syncLanguage(); retranslateWorkspace(window); });
  auto switchLanguage = [](const QString &locale) {
    { Tw::Settings settings; settings.setValue(QStringLiteral("locale"), locale); }
    TWApp::instance()->applyTranslation(locale);
  };
  QObject::connect(chinese, &QAction::triggered, window, [switchLanguage] { switchLanguage(QStringLiteral("zh_CN")); });
  QObject::connect(english, &QAction::triggered, window, [switchLanguage] { switchLanguage(QStringLiteral("en_US")); });
  QObject::connect(traditional, &QAction::triggered, window, [switchLanguage] { switchLanguage(QStringLiteral("zh_TW")); });
  auto *focus = new QAction(Research::ui(QStringLiteral("专注模式")),window);
  focus->setObjectName(QStringLiteral("researchFocusToggle"));focus->setCheckable(true);
  focus->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+F11")));window->addAction(focus);
  QObject::connect(focus,&QAction::triggered,window,[](bool on){setFocusMode(on);});
  more->addAction(focus); named(more,"actionPreferences"); more->addMenu(languages); more->addMenu(settings);
  auto *menuToggle=reading->addAction(Research::ui(QStringLiteral("显示菜单栏")));menuToggle->setObjectName(QStringLiteral("researchMenuBarToggle"));menuToggle->setCheckable(true);
  QObject::connect(menuToggle,&QAction::triggered,window,[window](bool show){window->menuBar()->setVisible(show);positionFocusControls(window);});
  QObject::connect(reading,&QMenu::aboutToShow,window,[window,menuToggle]{menuToggle->setChecked(window->menuBar()->isVisible());});
  auto *focusMenu=new QMenu(window);focusMenu->setObjectName(QStringLiteral("researchFocusMenu"));
  auto *onboardingFocus=new QMenu(window);installOnboardingMenu(window,settings,onboardingFocus);
  if(auto *about=window->findChild<QAction *>(QStringLiteral("actionAbout_TW"))) {
    settings->removeAction(about);settings->addAction(about);
    for(auto *action:settings->actions())if(action->isSeparator()){settings->removeAction(action);action->deleteLater();}
    QObject::connect(settings,&QMenu::aboutToShow,settings,[about]{about->setText(words("关于 TeXline…","About TeXline…"));});
  }
  auto *exitFocus=new QAction(Research::ui(QStringLiteral("退出专注")),window);exitFocus->setObjectName(QStringLiteral("researchFocusExit"));exitFocus->setVisible(false);
  QObject::connect(exitFocus,&QAction::triggered,window,[]{setFocusMode(false);});
  if(source)installCommandCompletion(window,edit,edit);
  installMenuChoices(edit,MenuSurface::Edit);
  installMenuChoices(insert,MenuSurface::Insert);
  auto *commands=new QAction(words("查找操作…","Find an action…"),window);commands->setObjectName(QStringLiteral("researchCommandPalette"));
  if(completionShortcutAvailable(window,QKeySequence(QStringLiteral("Ctrl+Shift+P"))))commands->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+P")));
  window->addAction(commands);more->insertAction(files->menuAction(),commands);
  QObject::connect(TWApp::instance(),&TWApp::updatedTranslators,commands,[commands]{commands->setText(words("查找操作…","Find an action…"));});
  if(insert){more->removeAction(insert->menuAction());more->insertMenu(more->actions().value(0),insert);}
  more->insertSeparator(focus);

  QObject::connect(commands,&QAction::triggered,window,[window,commands]{showCommandPalette(window,commands);});
  // Both modes borrow the very same QAction and QMenu instances. Late feature
  // registration is reflected when the menu opens, without caption matching.
  auto syncMenus=[window,more,focusMenu,focus,settings,languages,commands]{
    commands->setText(words("查找操作…","Find an action…"));
    focus->setText(ui(window->property("researchFocus").toBool()?QStringLiteral("退出专注"):QStringLiteral("专注模式")));
    if(auto *annotations=window->findChild<QMenu *>(QStringLiteral("researchAnnotationMenu"))){more->removeAction(annotations->menuAction());more->insertMenu(window->findChild<QMenu *>(QStringLiteral("researchBuildMenu"))->menuAction(),annotations);}
    focusMenu->clear();for(auto *a:more->actions())focusMenu->addAction(a);
    applyCommandShortcuts(window);
  };
  QObject::connect(more,&QMenu::aboutToShow,window,syncMenus);QObject::connect(focusMenu,&QMenu::aboutToShow,window,syncMenus);
  QTimer::singleShot(0,window,syncMenus);
  struct SettingsCommand { const char *id,*zh,*en;int page; };
  for(const auto &entry:QList<SettingsCommand>{{"researchSettingsMenus","自定义菜单…","Customize menus…",8},{"researchSettingsLanguage","语言 / Language…","Language / 语言…",0},{"researchSettingsEditor","编辑设置…","Editor settings…",1},{"researchSettingsFocus","专注设置…","Focus settings…",2},{"researchSettingsSnippets","补全与片段设置…","Completion & snippets settings…",3},{"researchSettingsShortcuts","配置快捷键…","Keyboard shortcuts…",5}}){
    auto *action=new QAction((englishUi()?QString::fromUtf8(entry.en):ui(QString::fromUtf8(entry.zh))),window);action->setObjectName(QString::fromLatin1(entry.id));
    QObject::connect(TWApp::instance(),&TWApp::updatedTranslators,action,[action,entry]{action->setText((englishUi()?QString::fromUtf8(entry.en):ui(QString::fromUtf8(entry.zh))));});
    QObject::connect(action,&QAction::triggered,window,[window,entry]{PrefsDialog::doPrefsDialog(window,entry.page,entry.page==0?(englishUi()?QStringLiteral("Language"):Research::ui(QStringLiteral("语言"))):QString());});
  }
  auto *moreButton = workspaceButton(bar, QString(), QStringLiteral(":/research/icons/more.svg"));
  moreButton->setObjectName(QStringLiteral("researchMore")); moreButton->setAccessibleName(Research::ui(QStringLiteral("更多")));
  moreButton->setPopupMode(QToolButton::InstantPopup); moreButton->setMenu(more);
  auto menuCaption=[](QToolButton *button){button->setIcon(QIcon());button->setText(words("菜单","Menu"));button->setAccessibleName(button->text());button->setToolTip(words("菜单 · 插入、文件与设置","Menu · Insert, file and settings"));button->setToolButtonStyle(Qt::ToolButtonTextOnly);button->setProperty("researchCompactMenu",true);configureButton(button,ButtonRole::Menu);};
  menuCaption(moreButton);QObject::connect(TWApp::instance(),&TWApp::updatedTranslators,moreButton,[=]{menuCaption(moreButton);});
  // The writing and reading entries retain their fixed place on the paper.
  if (!source) {
    if (auto *view = bar->findChild<QToolButton *>(QStringLiteral("researchViewMenu")); view && view->menu()) {
      auto *zoom = new QToolButton(window); zoom->setObjectName(QStringLiteral("researchFocusZoom"));
      zoom->setIcon(QIcon(QStringLiteral(":/research/icons/fit-page.svg")));
      zoom->setToolButtonStyle(Qt::ToolButtonIconOnly);
      zoom->setToolTip(Research::ui(QStringLiteral("切换页面缩放方式"))); zoom->setAccessibleName(Research::ui(QStringLiteral("页面缩放")));
      configureButton(zoom, ButtonRole::Icon); zoom->hide();
    }
  } else if (compile && compile->defaultAction()) {
    auto *focusCompile = new QToolButton(window);
    focusCompile->setObjectName(QStringLiteral("researchFocusCompile"));
    focusCompile->setProperty("researchCompactFocusControl",true);
    focusCompile->setDefaultAction(compile->defaultAction());
    focusCompile->setToolButtonStyle(Qt::ToolButtonIconOnly);
    configureButton(focusCompile, ButtonRole::Icon);
    QObject::connect(compile->defaultAction(), &QAction::changed, focusCompile, [focusCompile] {
      configureButton(focusCompile, ButtonRole::Icon);
    });
    focusCompile->hide();
  }
  auto *focusMore = new QToolButton(window);
  focusMore->setObjectName(QStringLiteral("researchFocusMore"));
  focusMore->setProperty("researchCompactFocusControl",bool(window->findChild<QTextEdit *>(QStringLiteral("textEdit"))));
  focusMore->setIcon(QIcon(QStringLiteral(":/research/icons/more.svg")));
  focusMore->setToolButtonStyle(Qt::ToolButtonIconOnly);
  focusMore->setAccessibleName(Research::ui(QStringLiteral("更多")));
  focusMore->setPopupMode(QToolButton::InstantPopup); focusMore->setMenu(focusMenu);
  menuCaption(focusMore);QObject::connect(TWApp::instance(),&TWApp::updatedTranslators,focusMore,[=]{menuCaption(focusMore);});
  focusMore->hide();
#ifdef Q_OS_WIN
  new PaperWindowControls(window);
#endif
  applyWorkspace(window, Tw::Settings().value(QStringLiteral("researchFocusMode"), false).toBool());
}
void MaterialDialog::paintEvent(QPaintEvent *) {
  // QDialog does not paint a stylesheet background on every native translucent surface.
  // The command palette uses the same opaque surface on every desktop.
  QPainter painter(this);
  painter.fillRect(rect(), property("researchWhiteSurface").toBool()?QColor(Qt::white):QColor(245, 245, 247));
}
void MaterialDialog::showEvent(QShowEvent *event) {
  QDialog::showEvent(event);
  QTimer::singleShot(0,this,[this]{animateArrival(this);});
}
}
