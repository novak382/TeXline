// Record real windows and real workflows; launch only on an inactive private desktop.
// Shared real-widget recorder; does not move the physical pointer.
#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include <QtWidgets>
#include "ResearchAuthoring.h"
#include "ResearchAppearance.h"
#include "ResearchReferences.h"
#include "ResearchBuild.h"
#include "TeXDocumentWindow.h"
#include "CompletingEdit.h"
#include "TWApp.h"
#include "Settings.h"
#include "PDFBackend.h"
#include "ResearchCollaboration.h"
#include "ResearchOnboarding.h"
#include "ResearchArticle.h"
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <PDFDocumentWidget.h>
#include <QGraphicsPathItem>
#include <QContextMenuEvent>
#include <QJsonArray>
#include <QMouseEvent>
#include <QTextBrowser>
#include <QElapsedTimer>
#include <QScreen>
#include <QImageWriter>
#include <QShortcutEvent>
#include <QThread>
#include <stdexcept>
#include <windows.h>
#include <shellapi.h>

static QString argument(int index) {
  int count = 0; auto args = CommandLineToArgvW(GetCommandLineW(), &count);
  const QString value = index < count ? QString::fromWCharArray(args[index]) : QString();
  LocalFree(args); return value;
}
static bool waitFor(const std::function<bool()> &ready, int ms = 12000) {
  QElapsedTimer t; t.start();
  while (!ready() && t.elapsed() < ms) {
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QThread::msleep(3);
  }
  return ready();
}
static void settle(int ms = 180) {
  QElapsedTimer t; t.start(); waitFor([&] { return t.elapsed() >= ms; }, ms + 2000);
}
static QJsonArray fileRetries;
static QJsonArray titleInputTrace;
static bool put(const QString &path, const QByteArray &bytes) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  for (int attempt = 0; attempt < 8; ++attempt) {
    QString error;
    {
      QSaveFile f(path);
      if (f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size() && f.commit()) return true;
      error = f.errorString();
    }
    fileRetries.append(QJsonObject{{"file", path}, {"attempt", attempt + 1}, {"error", error}});
    settle(120);
  }
  return false;
}
static QByteArray bytes(const QString &path) {
  QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}
static void key(QWidget *target, int code, Qt::KeyboardModifiers modifiers = Qt::NoModifier, const QString &text = {}) {
  QKeyEvent down(QEvent::KeyPress, code, modifiers, text), up(QEvent::KeyRelease, code, modifiers, text);
  QApplication::sendEvent(target, &down); QApplication::sendEvent(target, &up);
}
static void type(QWidget *target, const QString &text, int delay = 45) {
  for (const auto &ch : text) {
    auto *editor = qobject_cast<CompletingEdit *>(target);
    const bool trace = editor && text == QStringLiteral("Error bound");
    const auto before = trace ? editor->toPlainText() : QString();
    if (ch == QLatin1Char('\n')) key(target, Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r"));
    else key(target, int(ch.toUpper().unicode()), ch.isUpper() ? Qt::ShiftModifier : Qt::NoModifier, QString(ch));
    const auto after = trace ? editor->toPlainText() : QString();
    settle(delay);
    if (trace) titleInputTrace.append(QJsonObject{{"character",QString(ch)},{"before",before},{"afterKey",after},{"afterEvents",editor->toPlainText()},{"enabled",editor->isEnabled()},{"readOnly",editor->isReadOnly()},{"cursor",editor->textCursor().position()},{"popup",QApplication::activePopupWidget()?QApplication::activePopupWidget()->metaObject()->className():"none"}});
  }
}
static bool contains(const QSharedPointer<QtPDF::Backend::Document> &doc, const QString &text) {
  if (!doc || !doc->isValid() || doc->numPages() <= 0) return false;
  for (int i = 0; i < doc->numPages(); ++i) {
    const auto page = doc->page(i).toStrongRef();
    if (page && !page->search(text, QtPDF::Backend::Search_CaseInsensitive).isEmpty()) return true;
  }
  return false;
}

struct Recording {
  QString directory;
  QRect canvas;
  QList<QWidget *> windows;
  QPoint pointer;
  QTimer timer;
  QElapsedTimer elapsed;
  QJsonArray frames, actions;
  QVector<QImage> images;
  qint64 fastUntil{0};
  int failures{0};
  bool clicking{false};
  // Recording-only opt-in. Move a whole owned dialog into the fixed frame;
  // never resize controls, crop content or change normal application behavior.
  bool frameOwnedDialogs{false};
  int dialogFrameChecks{0}, clippedDialogFrames{0};
  QJsonArray dialogFraming;
  QSet<QWidget *> failedDialogFraming;
  QString modifier;
  Recording() {
    timer.setInterval(100); timer.setTimerType(Qt::PreciseTimer);
    QObject::connect(&timer, &QTimer::timeout, [&] { capture(); });
  }
  void start(const QString &folder, QWidget *window) {
    directory = folder; QDir().mkpath(folder); windows = {window};
    canvas = QRect(window->mapToGlobal(QPoint()), window->size());
    pointer = canvas.topLeft() + QPoint(35, 45);
    failures = 0; frames = {}; actions = {}; images.clear(); fastUntil=0;
    dialogFrameChecks=clippedDialogFrames=0;dialogFraming={};failedDialogFraming.clear();
    elapsed.start(); capture(); timer.start();
    action("Begin with the actual current application window");
    settle(650);
  }
  void fastFor(int ms) { fastUntil=qMax(fastUntil,elapsed.elapsed()+ms);timer.setInterval(16); }
  int motionFrameCount(const QString &name,qint64 since=0) const {QSet<QString> found;for(const auto &v:frames){auto f=v.toObject();if(f["atMs"].toDouble()>=since&&f["motions"].toArray().contains(name))found.insert(f["file"].toString());}return found.size();}
  void shortcut(QAction *entry,QWidget *target) {
    if(!entry||!entry->isEnabled()||entry->shortcut().isEmpty()||!target||!target->isVisible()){++failures;return;}
    aim(target,target->rect().center());modifier=entry->shortcut().toString(QKeySequence::NativeText);fastFor(800);capture();QShortcutEvent event(entry->shortcut(),0);QApplication::sendEvent(entry,&event);settle(250);modifier.clear();
  }
  void action(const QString &name) {
    actions.append(QJsonObject{{"atMs", double(elapsed.elapsed())}, {"action", name}});
  }
  bool contains(QWidget *target) const {
    return target && target->isVisible() && canvas.contains(QRect(target->mapToGlobal(QPoint()), target->size()));
  }
  bool ownsPopup(QWidget *popup) const {
    for(auto *owner=popup->parent();owner;owner=owner->parent())
      if(windows.contains(qobject_cast<QWidget *>(owner)))return true;
    return false;
  }
  void placeOwnedDialogs(bool moveIntoFrame=true) {
    if(!frameOwnedDialogs)return;
    auto rect=[](const QRect &r){return QJsonObject{{"x",r.x()},{"y",r.y()},{"width",r.width()},{"height",r.height()}};};
    for(auto *popup:QApplication::topLevelWidgets()) {
      auto *dialog=qobject_cast<QDialog *>(popup);
      if(!dialog||!dialog->isVisible()||windows.contains(dialog)||!ownsPopup(dialog))continue;
      const auto before=dialog->frameGeometry();const auto size=dialog->size();
      const bool canFit=before.isValid()&&before.width()<=canvas.width()&&before.height()<=canvas.height();
      if(moveIntoFrame&&canFit&&!canvas.contains(before)) {
        const QPoint topLeft(qBound(canvas.left(),before.left(),canvas.right()-before.width()+1),
          qBound(canvas.top(),before.top(),canvas.bottom()-before.height()+1));
        dialog->move(dialog->pos()+topLeft-before.topLeft());
      }
      const auto after=dialog->frameGeometry();const QRect client(dialog->mapToGlobal(QPoint()),dialog->size());
      const bool complete=canFit&&canvas.contains(after)&&canvas.contains(client)&&dialog->size()==size;
      ++dialogFrameChecks;
      if(!complete)++clippedDialogFrames;
      if(before!=after||(!complete&&!failedDialogFraming.contains(dialog)))
        dialogFraming.append(QJsonObject{{"atMs",double(elapsed.elapsed())},{"dialog",dialog->objectName()},
          {"title",dialog->windowTitle()},{"before",rect(before)},{"after",rect(after)},{"client",rect(client)},
          {"canvas",rect(canvas)},{"moved",before.topLeft()!=after.topLeft()},{"complete",complete}});
      if(!complete&&!failedDialogFraming.contains(dialog)){failedDialogFraming.insert(dialog);++failures;}
    }
  }
  void anchorForNativeContext(QWidget *window) {
    // QToolButton menus use the work area; the native actions context menu on
    // this inactive desktop uses the screen edge. Track the real window while
    // placing it at that edge, without altering any control or menu geometry.
    timer.stop();const auto localPointer=pointer-canvas.topLeft();
    const auto screen=window->windowHandle()->screen()->geometry();
    window->move(window->x(),screen.bottom()-window->height());settle(180);
    canvas.moveTopLeft(window->mapToGlobal(QPoint()));pointer=canvas.topLeft()+localPointer;
    timer.start();capture();
  }
  void follow(QWidget *window) {
    timer.stop();
    window->showNormal(); window->setWindowState(Qt::WindowNoState); settle(180);
    window->setGeometry(canvas.topLeft().x(), canvas.topLeft().y(), canvas.width(), canvas.height());
    window->show(); settle(500);
    if(window->size()!=canvas.size()||window->isMaximized())++failures;
    windows = {window}; canvas.moveTopLeft(window->mapToGlobal(QPoint()));
    pointer = canvas.topLeft() + QPoint(35, 45); timer.start(); capture();
  }
  void aim(QWidget *target, QPoint local) {
    if (!target) { ++failures; return; }
    placeOwnedDialogs();
    const QPoint end = target->mapToGlobal(local), begin = pointer;
    for (int step = 1; step <= 7; ++step) { pointer = begin + (end - begin) * step / 7; settle(32); }
    QMouseEvent move(QEvent::MouseMove, QPointF(local), QPointF(end), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(target, &move); settle(220);
  }
  void clickAt(QWidget *target, QPoint local, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    if (!target) { ++failures; return; }
    aim(target, local); fastFor(600); clicking = true; capture();
    QMouseEvent down(QEvent::MouseButtonPress, QPointF(local), QPointF(target->mapToGlobal(local)), Qt::LeftButton, Qt::LeftButton, modifiers);
    QMouseEvent up(QEvent::MouseButtonRelease, QPointF(local), QPointF(target->mapToGlobal(local)), Qt::LeftButton, Qt::NoButton, modifiers);
    QApplication::sendEvent(target, &down); settle(90); QApplication::sendEvent(target, &up);
    clicking = false; settle(260);
  }
  void click(QWidget *target) {
    if (!target || !target->isVisible() || !target->isEnabled()) { ++failures; return; }
    clickAt(target, target->rect().center());
  }
  bool choose(QToolButton *button, QAction *entry) {
    if (!button || !button->isVisible() || !button->menu() || !entry) return false;
    auto *menu=button->menu();bool visible=false,fits=false;QTimer responder,watchdog;
    responder.setInterval(30);watchdog.setSingleShot(true);watchdog.setInterval(15000);
    QObject::connect(&watchdog,&QTimer::timeout,menu,[&]{if(menu->isVisible()){++failures;menu->hide();}});
    QObject::connect(&responder,&QTimer::timeout,menu,[&]{
      if(!menu->isVisible())return;responder.stop();visible=entry->isEnabled()&&entry->isVisible();fits=contains(menu);
      if(!visible){menu->hide();return;}aim(menu,menu->actionGeometry(entry).center());settle(650);clickAt(menu,menu->actionGeometry(entry).center());
    });
    responder.start();watchdog.start();click(button);responder.stop();watchdog.stop();return visible&&fits;
  }
  bool contextChoose(QWidget *viewport, QPoint local, QAction *entry) {
    if (!viewport || !entry || !entry->isEnabled()) return false;
    bool visible=false,fits=false;
    aim(viewport,local);
    QTimer::singleShot(650, viewport, [&] {
      auto *menu=qobject_cast<QMenu *>(QApplication::activePopupWidget());
      visible=menu&&menu->actions().contains(entry);fits=visible&&contains(menu);
      if(menu)put(directory+"/context-menu.json",QJsonDocument(QJsonObject{{"visible",visible},{"fits",fits},{"x",menu->mapToGlobal(QPoint()).x()},{"y",menu->mapToGlobal(QPoint()).y()},{"width",menu->width()},{"height",menu->height()},{"canvasX",canvas.x()},{"canvasY",canvas.y()},{"canvasWidth",canvas.width()},{"canvasHeight",canvas.height()}}).toJson());
      if(!visible)return;
      aim(menu,menu->actionGeometry(entry).center());settle(650);
      clickAt(menu,menu->actionGeometry(entry).center());
    });
    QContextMenuEvent context(QContextMenuEvent::Mouse,local,viewport->mapToGlobal(local));
    QApplication::sendEvent(viewport,&context);
    return visible&&fits;
  }
  bool chooseNested(QToolButton *button,QMenu *submenu,QAction *entry) {
    if(!button||!button->isVisible()||!button->menu()||!submenu||!entry)return false;
    auto *menu=button->menu();bool reached=false,fits=false;QTimer responder,watchdog;
    responder.setInterval(30);watchdog.setSingleShot(true);watchdog.setInterval(15000);
    QObject::connect(&watchdog,&QTimer::timeout,menu,[&]{if(menu->isVisible()){++failures;menu->hide();}});
    QObject::connect(&responder,&QTimer::timeout,menu,[&]{
      if(!menu->isVisible())return;responder.stop();
      if(!menu->actions().contains(submenu->menuAction())){menu->hide();return;}
      aim(menu,menu->actionGeometry(submenu->menuAction()).center());settle(650);
      reached=waitFor([&]{return submenu->isVisible();},2000);fits=contains(menu)&&contains(submenu);
      if(!reached){menu->hide();return;}aim(submenu,submenu->actionGeometry(entry).center());settle(650);clickAt(submenu,submenu->actionGeometry(entry).center());
    });
    responder.start();watchdog.start();click(button);responder.stop();watchdog.stop();return reached&&fits;
  }
  void capture() {
    placeOwnedDialogs();
    const qint64 timestamp=elapsed.elapsed();
    if(timer.interval()!=100&&timestamp>=fastUntil)timer.setInterval(100);
    const qreal ratio = windows.isEmpty() ? 1 : windows.first()->devicePixelRatioF();
    QImage frame(canvas.size() * ratio, QImage::Format_RGB32); frame.fill(Qt::white); frame.setDevicePixelRatio(ratio);
    QPainter painter(&frame);
    for (auto *window : windows) if (window && window->isVisible())
      painter.drawPixmap(window->mapToGlobal(QPoint()) - canvas.topLeft(), window->grab());
    // Composite actual owned native popups, never drawings of an interface.
    for (auto *popup : QApplication::topLevelWidgets()) {
      if (!popup->isVisible() || windows.contains(popup)) continue;
      if (!qobject_cast<QMenu *>(popup) && !qobject_cast<QDialog *>(popup) && popup->windowType() != Qt::Popup && popup->objectName() != "researchReferenceCard") continue;
      if (ownsPopup(popup)) painter.drawPixmap(popup->mapToGlobal(QPoint()) - canvas.topLeft(), popup->grab());
    }
    const QPoint point = pointer - canvas.topLeft();
    // This indicator shows the scripted target without moving the physical pointer.
    if (clicking) { painter.setPen(Qt::NoPen); painter.setBrush(QColor(65, 85, 105, 50)); painter.drawEllipse(point, 13, 13); }
    QPolygon arrow; arrow << point << point + QPoint(0, 17) << point + QPoint(4, 13) << point + QPoint(8, 21) << point + QPoint(11, 19) << point + QPoint(7, 11) << point + QPoint(13, 11);
    painter.setPen(QPen(Qt::white, 1.5)); painter.setBrush(QColor("#303b45")); painter.drawPolygon(arrow);
    if (!modifier.isEmpty()) {
      const QRect badge(point + QPoint(18, 12), QSize(qMax(48, QFontMetrics(QFont("Segoe UI", 10)).horizontalAdvance(modifier) + 16), 25));
      painter.setPen(QColor("#d5dae0")); painter.setBrush(QColor("#f5f5f7")); painter.drawRoundedRect(badge, 5, 5);
      QFont font("Segoe UI", 10); font.setWeight(QFont::Medium); painter.setFont(font);
      painter.setPen(QColor("#343a40")); painter.drawText(badge, Qt::AlignCenter, modifier);
    }
    painter.end();
    // Verify the final capture geometry without making a second positioning
    // adjustment; a layout/paint change must not pass as a complete frame.
    placeOwnedDialogs(false);
    frame.setDevicePixelRatio(1);frame=frame.scaled(QSize(1100,660),Qt::KeepAspectRatio,Qt::SmoothTransformation);
    if(images.isEmpty()||frame!=images.last())images.append(frame);
    const QString name=QString("frame-%1.png").arg(images.size()-1,5,10,QLatin1Char('0'));
    QJsonArray motions;for(auto *window:windows)for(const auto &tag:{QString("researchPanelMotionVeil"),QString("researchSlideReflow"),QString("researchArrivalVeil"),QString("researchChromeVeil")})
      for(auto *widget:window->findChildren<QWidget *>(tag))if(widget->isVisible()&&!motions.contains(tag))motions.append(tag);
    frames.append(QJsonObject{{"file",name},{"atMs",double(timestamp)},{"motions",motions}});
  }

  void finish() {
    settle(1300); capture(); timer.stop();
    const auto duration=elapsed.elapsed();
    for(int i=0;i<images.size();++i){QImageWriter writer(directory+QString("/frame-%1.png").arg(i,5,10,QLatin1Char('0')));if(!writer.write(images[i]))++failures;}images.clear();
    if (!put(directory + "/recording.json", QJsonDocument(QJsonObject{
      {"frames", frames}, {"actions", actions}, {"durationMs", double(duration)},
      {"desktop", qEnvironmentVariable("TEXLINE_TEST_DESKTOP")},
      {"capture", "Actual Qt Windows paints, uniformly resized; cached during capture, PNG compression after the clip"},
      {"inputs", "Scripted Qt mouse, menu and editor events on actual visible widgets"},
      {"pointer", "Input-target indicator added to actual window captures"},
      {"frameOwnedDialogs",frameOwnedDialogs},{"dialogFrameChecks",dialogFrameChecks},
      {"clippedDialogFrames",clippedDialogFrames},{"dialogFraming",dialogFraming},
      {"devicePixelRatio", windows.first()->devicePixelRatioF()},
      {"logicalWidth", canvas.width()}, {"logicalHeight", canvas.height()}, {"failures", failures}
    }).toJson())) ++failures;
  }
};

