#include "ResearchOnboarding.h"
#include "ResearchAppearance.h"
#include "Settings.h"
#include "TWApp.h"
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QImageReader>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QKeyEvent>
#include <QMainWindow>
#include <QMenu>
#include <QMouseEvent>
#include <QMovie>
#include <QPainter>
#include <QPointer>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScreen>
#include <QShortcut>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>
#if defined(Q_OS_WIN)
#include <windows.h>
#endif

namespace Research {
namespace {
const QString seenKey = QStringLiteral("researchOnboardingSeen");
QPointer<QDialog> introduction;

QByteArray readLimited(const QString &path, qint64 limit) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly) && file.size() <= limit ? file.readAll() : QByteArray();
}
QString localized(const QJsonObject &values) {
  const QString preferred = values.value(englishUi() ? QStringLiteral("en") : QStringLiteral("zh_CN")).toString();
  return preferred.isEmpty() ? values.value(QStringLiteral("zh_CN")).toString() : preferred;
}
bool validContent(const QJsonObject &object) {
  if (object.value(QStringLiteral("schemaVersion")).toInt() != 1) return false;
  const auto pages = object.value(QStringLiteral("pages")).toArray();
  if (pages.isEmpty() || pages.size() > 12) return false;
  for (const auto &value : pages) {
    const auto page = value.toObject();
    for (const auto &key : {QStringLiteral("title"), QStringLiteral("body")}) {
      const auto text = page.value(key).toObject();
      for (const auto &language : {QStringLiteral("zh_CN"), QStringLiteral("en")}) {
        const auto entry = text.value(language).toString();
        if (entry.trimmed().isEmpty() || entry.size() > 512) return false;
      }
    }
  }
  return true;
}
struct Pack {
  QJsonObject content;
  QString style, root;
  bool external{false};
  Pack() {
    root = QCoreApplication::applicationDirPath() + QStringLiteral("/resources/onboarding");
    content = QJsonDocument::fromJson(readLimited(root + QStringLiteral("/content.json"), 128 * 1024)).object();
    external = validContent(content);
    if (!external) content = QJsonDocument::fromJson(readLimited(QStringLiteral(":/research/onboarding/content.json"), 128 * 1024)).object();
    const auto overrideStyle = readLimited(root + QStringLiteral("/style.qss"), 64 * 1024);
    style = QString::fromUtf8(overrideStyle.isEmpty() ? readLimited(QStringLiteral(":/research/onboarding/style.qss"), 64 * 1024) : overrideStyle);
  }
  QString string(const QString &key, const QString &language = QString()) const {
    const QString locale = language.isEmpty() ? (englishUi() ? QStringLiteral("en") : QStringLiteral("zh_CN")) : language;
    const auto strings = content.value(QStringLiteral("strings")).toObject();
    auto value = strings.value(locale).toObject().value(key).toString();
    if (value.isEmpty()) {
      const auto defaults = QJsonDocument::fromJson(readLimited(QStringLiteral(":/research/onboarding/content.json"), 128 * 1024)).object();
      value = defaults.value(QStringLiteral("strings")).toObject().value(locale).toObject().value(key).toString();
    }
    return value;
  }
  int metric(const QString &key, int fallback, int low, int high) const {
    return qBound(low, content.value(QStringLiteral("layout")).toObject().value(key).toInt(fallback), high);
  }
  QString media(const QJsonObject &page) const {
    const auto relative = localized(page.value(QStringLiteral("media")).toObject());
    if (relative.isEmpty() || QDir::isAbsolutePath(relative)) return {};
    const auto base = QFileInfo(root).canonicalFilePath();
    const auto file = QFileInfo(QDir(root).filePath(relative));
    const auto resolved = file.canonicalFilePath();
    if (base.isEmpty() || !resolved.startsWith(base + QLatin1Char('/'), Qt::CaseInsensitive) || file.size() > 32 * 1024 * 1024) return {};
    return resolved;
  }
};
bool animationEnabled() {
#if defined(Q_OS_WIN)
  BOOL enabled = TRUE;
  if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0)) return enabled;
#endif
  return true;
}

// Decode only the active clip. Closing or hiding the dialog releases/pauses it.
class Demo final : public QFrame {
  QMovie *movie{nullptr};
  QSize original;
  QPixmap still;
public:
  explicit Demo(QWidget *parent) : QFrame(parent) { setObjectName(QStringLiteral("onboardingMedia")); }
  void release() {
    if (movie) { movie->stop(); delete movie; movie = nullptr; }
    original = {};still=QPixmap(); update();
  }
  bool load(const QString &path) {
    release();
    if (path.isEmpty()) return false;
    QImageReader reader(path); original = reader.size();
    if (!original.isValid() || original.width() > 1920 || original.height() > 1200) return false;
    if(!reader.supportsAnimation()){still=QPixmap::fromImage(reader.read());update();return !still.isNull();}
    movie = new QMovie(path, QByteArray(), this);
    movie->setObjectName(QStringLiteral("onboardingMovie")); movie->setCacheMode(QMovie::CacheNone);
    if (!movie->isValid()) { release(); return false; }
    connect(movie, &QMovie::frameChanged, this, [this] { update(); });
    connect(movie, &QMovie::error, this, [this] { if (movie) movie->stop(); update(); });
    resizeMovie(); movie->jumpToFrame(0); update(); return true;
  }
  void pause(bool paused) {
    if (!movie) return;
    if (paused) { if (movie->state() == QMovie::Running) movie->setPaused(true); }
    else movie->start();
  }
  bool animated()const{return movie!=nullptr;}
private:
  void resizeMovie() {
    if (movie && original.isValid()) movie->setScaledSize(original.scaled((size() - QSize(12, 12)) * devicePixelRatioF(), Qt::KeepAspectRatio));
  }
  void resizeEvent(QResizeEvent *event) override { QFrame::resizeEvent(event); resizeMovie(); }
  void paintEvent(QPaintEvent *event) override {
    QFrame::paintEvent(event);
    if (!movie&&still.isNull()) return;
    auto frame = movie?movie->currentPixmap():still.scaled((size()-QSize(12,12))*devicePixelRatioF(),Qt::KeepAspectRatio,Qt::SmoothTransformation); if (frame.isNull()) return;
    frame.setDevicePixelRatio(devicePixelRatioF());
    QPainter painter(this);
    const auto logical = frame.deviceIndependentSize();
    painter.drawPixmap(QPointF((width() - logical.width()) / 2, (height() - logical.height()) / 2), frame);
  }
};

class Introduction final : public QDialog {
  Pack pack;
  QJsonArray pages;
  QWidget *pageBody;
  QWidget *languageChoices;
  QLabel *title, *body, *brand;
  Demo *demo;
  QPushButton *skip, *back, *next, *play, *closeButton;
  QList<QToolButton *> dots;
  QGraphicsOpacityEffect *opacity;
  QPropertyAnimation *fade;
  int current{0};
  bool paused{!animationEnabled()};
  bool choosingLanguage{true};
  bool contextual{false};
  QPoint dragOffset;
  bool dragging{false};
public:
  explicit Introduction(QWidget *parent,const QString &topic={}) : QDialog(parent), pages(pack.content.value(QStringLiteral("pages")).toArray()) {
    if(!topic.isEmpty())for(const auto &page:pages)if(page.toObject().value(QStringLiteral("id")).toString()==topic){pages=QJsonArray{page};contextual=true;break;}
    setObjectName(QStringLiteral("researchOnboarding"));
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setWindowModality(Qt::WindowModal); setAttribute(Qt::WA_DeleteOnClose);
    setProperty("onboardingExternal", pack.external); setProperty("onboardingPageCount", pages.size());
    setStyleSheet(pack.style);
    auto *layout = new QVBoxLayout(this);
    const int margin = pack.metric(QStringLiteral("margin"), 32, 16, 48);
    layout->setContentsMargins(margin, 20, margin, 24);
    layout->setSpacing(pack.metric(QStringLiteral("spacing"), 16, 8, 28));
    auto *header = new QHBoxLayout;
    brand = new QLabel(QStringLiteral("TeXline"), this); brand->setObjectName(QStringLiteral("onboardingBrand"));
    closeButton = new QPushButton(this); closeButton->setObjectName(QStringLiteral("onboardingClose")); closeButton->setAutoDefault(false);
    closeButton->setIcon(QIcon(QStringLiteral(":/research/icons/codicons/chrome-close.svg"))); closeButton->setIconSize(QSize(16, 16));
    header->addWidget(brand); header->addStretch(); header->addWidget(closeButton); layout->addLayout(header);
    pageBody = new QWidget(this); auto *content = new QVBoxLayout(pageBody); content->setContentsMargins(0, 10, 0, 0); content->setSpacing(10);
    title = new QLabel(pageBody); title->setObjectName(QStringLiteral("onboardingTitle")); title->setWordWrap(true); title->setTextFormat(Qt::PlainText);
    body = new QLabel(pageBody); body->setObjectName(QStringLiteral("onboardingBody")); body->setWordWrap(true); body->setTextFormat(Qt::PlainText);
    content->addWidget(title); content->addWidget(body); content->addSpacing(14);
    languageChoices = new QWidget(pageBody); auto *languages = new QHBoxLayout(languageChoices); languages->setContentsMargins(0, 12, 0, 16); languages->setSpacing(12);
    const QStringList locales{QStringLiteral("zh_CN"),QStringLiteral("zh_TW"),QStringLiteral("en_US")};
    const QStringList names{QStringLiteral("简体中文"),QStringLiteral("繁體中文"),QStringLiteral("English")};
    const QStringList objects{QStringLiteral("onboardingChinese"),QStringLiteral("onboardingTraditional"),QStringLiteral("onboardingEnglish")};
    for (int i=0;i<locales.size();++i) {
      const auto locale=locales[i];
      auto *button = new QPushButton(names[i], languageChoices);
      button->setObjectName(objects[i]); button->setProperty("onboardingLanguage", true); button->setAutoDefault(false); button->installEventFilter(this);
      button->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
      languages->addWidget(button,1);
      connect(button, &QPushButton::clicked, this, [this, locale] {
        choosingLanguage = false; current = 0;
        Tw::Settings().setValue(QStringLiteral("locale"), locale); TWApp::instance()->applyTranslation(locale); next->setFocus();
      });
    }
    content->addWidget(languageChoices);
    demo = new Demo(pageBody); demo->setFixedHeight(pack.metric(QStringLiteral("mediaHeight"), 306, 160, 360)); content->addWidget(demo);
    opacity = new QGraphicsOpacityEffect(pageBody); pageBody->setGraphicsEffect(opacity);
    fade = new QPropertyAnimation(opacity, "opacity", this); fade->setDuration(paused ? 0 : pack.metric(QStringLiteral("fadeDuration"), 140, 0, 300)); fade->setEasingCurve(QEasingCurve::OutCubic);
    layout->addWidget(pageBody);
    auto *progress = new QHBoxLayout; progress->setSpacing(6);
    play = new QPushButton(this); play->setObjectName(QStringLiteral("onboardingPlayback")); play->setAutoDefault(false);
    progress->addWidget(play); progress->addStretch();
    for (int i = 0; i < pages.size(); ++i) {
      auto *dot = new QToolButton(this); dot->setObjectName(QStringLiteral("onboardingDot%1").arg(i)); dot->setProperty("onboardingDot", true); dot->setCheckable(true);
      dots.append(dot); progress->addWidget(dot); connect(dot, &QToolButton::clicked, this, [this, i] { showPage(i); });
    }
    layout->addLayout(progress);
    auto *footer = new QHBoxLayout; footer->setSpacing(8);
    skip = new QPushButton(this); skip->setObjectName(QStringLiteral("onboardingSkip")); skip->setAutoDefault(false);
    back = new QPushButton(this); back->setObjectName(QStringLiteral("onboardingBack")); back->setAutoDefault(false);
    next = new QPushButton(this); next->setObjectName(QStringLiteral("onboardingNext")); next->setDefault(true);
    footer->addWidget(skip); footer->addStretch(); footer->addWidget(back); footer->addWidget(next); layout->addLayout(footer);
    connect(skip, &QPushButton::clicked, this, &QDialog::reject);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(back, &QPushButton::clicked, this, [this] { if (current > 0) showPage(current - 1); else showLanguages(); });
    connect(next, &QPushButton::clicked, this, [this] { if (current + 1 == pages.size()) accept(); else showPage(current + 1); });
    connect(play, &QPushButton::clicked, this, [this] { paused = !paused; demo->pause(paused); updatePlayback(); });
    QPointer<QWidget> returnFocus=QApplication::focusWidget();
    connect(this, &QDialog::finished, this, [this,returnFocus] { demo->release(); introduction = nullptr; if(!contextual)Tw::Settings().setValue(seenKey, true);if(returnFocus)QTimer::singleShot(0,returnFocus,[returnFocus]{if(returnFocus&&returnFocus->isVisible())returnFocus->setFocus();}); });
    connect(TWApp::instance(), &TWApp::updatedTranslators, this, [this] { if (choosingLanguage) showLanguages(); else showPage(current, false); });
    auto *left = new QShortcut(QKeySequence(Qt::Key_Left), this); connect(left, &QShortcut::activated, this, [this] { if (choosingLanguage) moveLanguageFocus(-1); else { if (current > 0) showPage(current - 1); else showLanguages(); } });
    auto *right = new QShortcut(QKeySequence(Qt::Key_Right), this); connect(right, &QShortcut::activated, this, [this] { if (choosingLanguage) moveLanguageFocus(1); else if (current + 1 < pages.size()) showPage(current + 1); });
    brand->installEventFilter(this);
    if(contextual)showPage(0,false);else showLanguages();
    fitWithinScreen();
    if(choosingLanguage)languageChoices->findChild<QPushButton *>(englishUi() ? QStringLiteral("onboardingEnglish") : traditionalUi()?QStringLiteral("onboardingTraditional"):QStringLiteral("onboardingChinese"))->setFocus();
  }
private:
  void moveLanguageFocus(int direction) {
    const auto buttons=languageChoices->findChildren<QPushButton *>(QString(),Qt::FindDirectChildrenOnly);
    if(buttons.isEmpty())return;
    int index=buttons.indexOf(qobject_cast<QPushButton *>(focusWidget()));
    if(index<0)index=direction>0?-1:0;
    buttons[(index+direction+buttons.size())%buttons.size()]->setFocus(Qt::TabFocusReason);
  }
  void fitWithinScreen() {
    QWidget *parent = parentWidget();
    QScreen *screen = parent && parent->windowHandle() ? parent->windowHandle()->screen() : QGuiApplication::primaryScreen();
    const QRect available = screen->availableGeometry().adjusted(16, 16, -16, -16);
    int desiredWidth=pack.metric(QStringLiteral("width"), 660, 480, 820);
    if(choosingLanguage&&parent)desiredWidth=qMin(desiredWidth,qMax(360,parent->width()));
    setFixedWidth(qMin(desiredWidth, available.width()));
    adjustSize();
    // Keep the complete navigation visible on small or high-DPI work areas.
    if (height() > available.height() && demo->isVisibleTo(this)) {
      demo->setFixedHeight(qMax(100, demo->height() - (height() - available.height())));
      // The child's height change queues a layout request. Recompute both
      // layouts now so adjustSize does not retain the old minimum height.
      pageBody->layout()->activate(); layout()->activate(); adjustSize();
    }
    const QPoint center = parent ? parent->frameGeometry().center() : available.center();
    move(qBound(available.left(), center.x() - width() / 2, available.right() - width() + 1), qBound(available.top(), center.y() - height() / 2, available.bottom() - height() + 1));
  }
  void updatePlayback() { play->setText(pack.string(paused ? QStringLiteral("play") : QStringLiteral("pause"))); }
  void showLanguages() {
    if(contextual){showPage(0,false);return;}
    choosingLanguage = true; fade->stop(); opacity->setOpacity(1.0); demo->release(); demo->hide(); play->hide(); languageChoices->show();
    setProperty("onboardingPageIndex", -1); setProperty("onboardingMediaPath", QString()); setWindowTitle(QStringLiteral("TeXline"));
    title->setText(QStringLiteral("语言 / Language")); body->hide();
    for (auto *dot : dots) dot->hide();
    back->hide(); next->hide(); skip->setText(pack.string(QStringLiteral("languageSkip")));
    closeButton->setAccessibleName(pack.string(QStringLiteral("close"))); closeButton->setToolTip(QString());
    fitWithinScreen();
    languageChoices->findChild<QPushButton *>(englishUi()?QStringLiteral("onboardingEnglish"):traditionalUi()?QStringLiteral("onboardingTraditional"):QStringLiteral("onboardingChinese"))->setFocus();
  }
  void showPage(int index, bool animate = true) {
    if (index < 0 || index >= pages.size()) return;
    fade->stop(); current = index; const auto page = pages[index].toObject();
    choosingLanguage = false; languageChoices->hide(); body->show(); back->show(); next->show();
    setProperty("onboardingPageIndex", current);
    setWindowTitle(pack.string(QStringLiteral("windowTitle")));
    title->setText(localized(page.value(QStringLiteral("title")).toObject())); body->setText(localized(page.value(QStringLiteral("body")).toObject()));
    skip->setText(pack.string(QStringLiteral("skip"))); back->setText(pack.string(QStringLiteral("back")));
    back->setEnabled(true); next->setText(pack.string(current + 1 == pages.size() ? QStringLiteral("finish") : QStringLiteral("next")));
    if(contextual){back->hide();skip->hide();next->setText(englishUi()?QStringLiteral("Return"):QStringLiteral("返回"));}
    closeButton->setAccessibleName(pack.string(QStringLiteral("close"))); closeButton->setToolTip(QString());
    for (int i = 0; i < dots.size(); ++i) { dots[i]->show(); dots[i]->setChecked(i == current); dots[i]->setAccessibleName(pack.string(QStringLiteral("page")).arg(i + 1).arg(pages.size())); dots[i]->setToolTip(dots[i]->accessibleName()); }
    const auto path = pack.media(page); const bool loaded = demo->load(path);
    demo->setVisible(loaded); play->setVisible(loaded&&demo->animated()); setProperty("onboardingMediaPath", loaded ? path : QString());
    demo->setAccessibleName(title->text()); demo->pause(paused || !isVisible()); updatePlayback();
    fitWithinScreen();
    if (animate && fade->duration()) { fade->setStartValue(0.75); fade->setEndValue(1.0); fade->start(); } else opacity->setOpacity(1.0);
  }
  void showEvent(QShowEvent *event) override { QDialog::showEvent(event); demo->pause(paused); }
  void hideEvent(QHideEvent *event) override { demo->pause(true); QDialog::hideEvent(event); }
  void changeEvent(QEvent *event) override {
    QDialog::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange) demo->pause(paused || isMinimized());
  }
  bool eventFilter(QObject *object, QEvent *event) override {
    if(choosingLanguage&&object->property("onboardingLanguage").toBool()&&event->type()==QEvent::KeyPress){
      const auto *key=static_cast<QKeyEvent *>(event);
      if((key->key()==Qt::Key_Return||key->key()==Qt::Key_Enter)&&(key->modifiers()&~Qt::KeypadModifier)==Qt::NoModifier){
        if(!key->isAutoRepeat())static_cast<QPushButton *>(object)->click();
        return true;
      }
    }
    if (object == brand) {
      if (event->type() == QEvent::MouseButtonPress) { const auto *mouse = static_cast<QMouseEvent *>(event); if (mouse->button() == Qt::LeftButton) { dragging = true; dragOffset = mouse->globalPosition().toPoint() - frameGeometry().topLeft(); } }
      else if (event->type() == QEvent::MouseMove && dragging) move(static_cast<QMouseEvent *>(event)->globalPosition().toPoint() - dragOffset);
      else if (event->type() == QEvent::MouseButtonRelease) dragging = false;
    }
    return QDialog::eventFilter(object, event);
  }
};
}

void showOnboarding(QWidget *parent, bool firstUse) {
  if (firstUse && Tw::Settings().value(seenKey, false).toBool()) return;
  if (introduction) { introduction->show(); introduction->raise(); introduction->activateWindow(); return; }
  if (!parent && TWApp::instance()) parent = TWApp::instance()->topWindow();
  auto *dialog = new Introduction(parent); introduction = dialog; dialog->show();
}
void scheduleOnboarding() {
  QTimer::singleShot(0, qApp, [] { showOnboarding(nullptr, true); });
}
void showOnboardingTopic(QWidget *parent,const QString &topic) {
  if(introduction)introduction->close();
  auto *dialog=new Introduction(parent,topic);introduction=dialog;dialog->show();
}
void installOnboardingMenu(QMainWindow *window, QMenu *normal, QMenu *focused) {
  auto *action = new QAction(window); action->setObjectName(QStringLiteral("researchOnboardingAction"));
  auto update = [action] { action->setText(Pack().string(QStringLiteral("windowTitle"))); };
  update(); QObject::connect(TWApp::instance(), &TWApp::updatedTranslators, action, update);
  QObject::connect(action, &QAction::triggered, window, [window] { showOnboarding(window); });
  normal->addSeparator(); normal->addAction(action); focused->addAction(action);
  QObject::connect(normal, &QMenu::aboutToShow, action, update);
  QObject::connect(focused, &QMenu::aboutToShow, action, update);
  if (auto *help = window->findChild<QMenu *>(QStringLiteral("menuHelp"))) help->addAction(action);
}
}
