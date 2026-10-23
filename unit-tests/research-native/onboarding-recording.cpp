// Record real windows and real workflows; launch only on an inactive private desktop.
// The fixture includes ResearchSlides.cpp; exclude its ordinary object when linking.
#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "ResearchSlides.cpp"
#include "ResearchCollaboration.h"
#include "ResearchOnboarding.h"
#include "ResearchArticle.h"
#include "ResearchAuthoring.h"
#include <QtWidgets>
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
    const bool trace = editor && text == QStringLiteral("Path invariant");
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
  QString modifier;
  Recording() {
    timer.setInterval(100); timer.setTimerType(Qt::PreciseTimer);
    QObject::connect(&timer, &QTimer::timeout, [&] { capture(); });
  }
  void start(const QString &folder, QWidget *window) {
    directory = folder; QDir().mkpath(folder); windows = {window};
    canvas = QRect(window->mapToGlobal(QPoint()), window->size());
    pointer = canvas.topLeft() + QPoint(35, 45);
    failures = 0; frames = {}; actions = {}; images.clear(); fastUntil=0; elapsed.start(); capture(); timer.start();
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
  bool choosePath(QToolButton *button,const QList<QMenu *> &path,QAction *entry) {
    if(!button||!button->menu()||!entry)return false;
    auto *root=button->menu();bool reached=false;QTimer responder,watchdog;
    responder.setInterval(30);watchdog.setSingleShot(true);watchdog.setInterval(18000);
    QObject::connect(&watchdog,&QTimer::timeout,root,[&]{++failures;for(auto *menu:path)if(menu)menu->hide();root->hide();});
    QObject::connect(&responder,&QTimer::timeout,root,[&]{
      if(!root->isVisible())return;responder.stop();auto *current=root;
      for(auto *next:path){if(!next||!current->actions().contains(next->menuAction())){root->hide();return;}
        aim(current,current->actionGeometry(next->menuAction()).center());settle(650);
        if(!waitFor([&]{return next->isVisible();},2000)||!contains(next)){root->hide();return;}current=next;}
      reached=entry->isEnabled()&&current->actions().contains(entry)&&contains(current);
      if(reached){aim(current,current->actionGeometry(entry).center());settle(500);clickAt(current,current->actionGeometry(entry).center());}
      else root->hide();
    });responder.start();watchdog.start();click(button);responder.stop();watchdog.stop();return reached;
  }
  void capture() {
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
      bool owned = false;
      for (auto *owner = popup->parent(); owner; owner = owner->parent())
        if (windows.contains(qobject_cast<QWidget *>(owner))) { owned = true; break; }
      if (owned) painter.drawPixmap(popup->mapToGlobal(QPoint()) - canvas.topLeft(), popup->grab());
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
    frame.setDevicePixelRatio(1);if(frame.width()>1440||frame.height()>900)frame=frame.scaled(QSize(1440,900),Qt::KeepAspectRatio,Qt::SmoothTransformation);
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
      {"devicePixelRatio", windows.first()->devicePixelRatioF()},
      {"logicalWidth", canvas.width()}, {"logicalHeight", canvas.height()}, {"failures", failures}
    }).toJson())) ++failures;
  }
};

static QByteArray article,talk;

#include "OnboardingAuthoring.inc"

int main(int argc, char **argv) {
  if (!qEnvironmentVariable("TEXLINE_TEST_DESKTOP").startsWith("TeXlineValidation-")) return 2;
  const QString output = argument(1), onlyLanguage = argument(2);
  const bool articleOnly = argument(3) == "article", toolsOnly = argument(3) == "tools", slidesOnly = argument(3) == "slides";

  argc = 1; QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
  TWApp app(argc, argv); app.setQuitOnLastWindowClosed(false);
  const QString materialDir=QCoreApplication::applicationDirPath()+"/resources/onboarding-materials";
  article=bytes(materialDir+"/spectral-stability.tex");talk=bytes(materialDir+"/shortest-paths.tex");
  if(article.isEmpty()||talk.isEmpty())return 3;
  Tw::Settings().setValue("researchOnboardingSeen", true);
  Tw::Settings().setValue("researchReduceMotion", false);
  Tw::Settings().setValue("openPDFwithTeX", false);
  Tw::Settings().setValue("researchBuildDestinationPolicy", "local-cache");
  Research::setBuildCacheForTests(output + "/cache");
  app.setBinaryPaths({"C:/texlive/2023/bin/windows"});
  app.setEngineList({Engine("pdfLaTeX", "C:/texlive/2023/bin/windows/pdflatex.exe",
    {"-synctex=1", "-interaction=nonstopmode", "-file-line-error", "$fullname"}, true)});
  QTimer::singleShot(200, &app, [&] {
    QJsonArray checks; int failures = 0;
    auto saveChecks = [&] {
      put(output + "/recording-checks.json", QJsonDocument(QJsonObject{
        {"checks", checks}, {"failures", failures}, {"desktop", qEnvironmentVariable("TEXLINE_TEST_DESKTOP")},
        {"personalDocumentsUsed", false}, {"realAiServiceUsed", false}, {"fileWriteRetries", fileRetries}
      }).toJson());
    };
    auto check = [&](QString name, bool passed) {
      checks.append(QJsonObject{{"name", name}, {"passed", passed}}); failures += !passed; saveChecks();
      if (!passed) throw std::runtime_error(name.toUtf8().constData());
    };
    auto place = [&](QWidget *w, QSize size = QSize(1056, 620)) {
      w->showNormal(); w->resize(size); w->show(); QApplication::setActiveWindow(w); settle(220);
      const QRect area = w->windowHandle()->screen()->availableGeometry();
      // Let the platform keep lower context menus above the work-area edge.
      w->move(area.right() - w->width() + 1, area.bottom() - w->height()); settle(320);
    };
    auto compile = [&](TeXDocumentWindow *w, Recording *record = nullptr) {
      auto *button = w->findChild<QToolButton *>("researchCompile");
      // A fresh article with cross-references needs two actual TeX passes.
      const int passes=w->textDoc()->editorText().contains("\\eqref{")?2:1;
      for(int pass=0;pass<passes;++pass){
        if (record) { record->action(pass?"Run pdfLaTeX again to resolve the actual equation references":"Compile the real document with pdfLaTeX"); check("visible compile button is available", button && button->isVisible()); record->click(button); }
        else w->typeset();
        if(!waitFor([&] {
          return !w->isTypesetting() && !w->property("researchBuildPreparing").toBool()
            && !w->property("researchBuildFinishing").toBool() && !w->property("researchPublishing").toBool()
            && !w->isModified() && Research::pdfPairValid(Research::previewFile(w->fileName()));
        }, 100000))return false;
      }
      return true;
    };
    auto replace = [&](TeXDocumentWindow *w, const QString &oldText, const QString &newText) {
      const int at = w->textDoc()->editorText().indexOf(oldText);
      if (at < 0) return false;
      QTextCursor cursor(w->textDoc()); cursor.setPosition(at);
      cursor.setPosition(at + oldText.size(), QTextCursor::KeepAnchor);
      w->editor()->setTextCursor(cursor); w->editor()->insertPlainText(newText); return true;
    };
    auto articleReady = [&](TeXDocumentWindow *v) { return waitFor([&] {return v->property("researchArticleReady").toBool();}); };
    auto outlineButton = [&](TeXDocumentWindow *v) -> QToolButton * {
      auto *action=v->findChild<QAction *>("researchOutlineAction");
      for(auto *button:v->findChildren<QToolButton *>())if(button->parentWidget()==v->findChild<QToolBar *>("toolBar_run")&&button->accessibleName()==Research::ui(QStringLiteral("结构"))&&action)return button;
      return nullptr;
    };
    auto treeEntry = [&](QTreeWidget *tree,const QString &text) -> QTreeWidgetItem * {
      if(!tree)return nullptr;QTreeWidgetItemIterator it(tree);while(*it){if((*it)->text(0).contains(text))return *it;++it;}return nullptr;
    };
    auto selectEntry = [&](Recording &rec,QTreeWidget *tree,QTreeWidgetItem *item) {
      if(!tree||!item)return false;for(auto *parent=item->parent();parent;parent=parent->parent())parent->setExpanded(true);
      tree->scrollToItem(item);settle(100);rec.clickAt(tree->viewport(),tree->visualItemRect(item).center());return true;
    };
    auto contextPin = [&](Recording &rec,QTreeWidget *tree,QTreeWidgetItem *item) {
      if(!tree||!item)return false;for(auto *parent=item->parent();parent;parent=parent->parent())parent->setExpanded(true);
      tree->scrollToItem(item);settle(150);const auto pos=tree->visualItemRect(item).center();rec.aim(tree->viewport(),pos);
      bool selected=false;
      QTimer::singleShot(650,&app,[&]{auto *menu=qobject_cast<QMenu *>(QApplication::activePopupWidget());
        if(!menu)return;if(!rec.contains(menu)){menu->close();return;}
        for(auto *action:menu->actions())if(action->text()==(Research::englishUi()?"Pin below":"固定到下方")){
          rec.aim(menu,menu->actionGeometry(action).center());settle(550);rec.clickAt(menu,menu->actionGeometry(action).center());selected=true;return;}
        menu->close();});
      QContextMenuEvent context(QContextMenuEvent::Mouse,pos,tree->viewport()->mapToGlobal(pos));QApplication::sendEvent(tree->viewport(),&context);return selected;
    };
    try {
      for (const auto &language : {QString("zh"), QString("en")}) {
        if (!onlyLanguage.isEmpty() && onlyLanguage != language) continue;
        app.applyTranslation(language == "zh" ? "zh_CN" : "en_US"); Research::setFocusMode(false);
        const QString folder = output + "/manuscripts/" + language, root = folder + "/spectral-stability.tex";
        QDir().mkpath(folder);
        for(const auto &entry:QDir(materialDir).entryList(QDir::Files)){check(language+": stage independently sourced material "+entry,QFile::copy(materialDir+"/"+entry,folder+"/"+entry));}
        if(toolsOnly){recordAuthoring(folder,language,check,place);continue;}
        check(language + ": create an independent public example", put(root, article));
        auto *w = new TeXDocumentWindow(root); place(w, QSize(800, 500));
        for (auto *other : TeXDocumentWindow::documentList()) if (other != w && other->untitled()) {
          other->setModified(false); other->hide();
        }
        // Prepare caches outside the clip; the displayed compile retains real time.
        check(language + ": prepare actual article PDF", compile(w)); if (w->pdfDocument()) w->pdfDocument()->hide();
        if(auto *a=w->findChild<QAction *>("researchOutlineAction");a&&a->isChecked())a->trigger();
        w->goToLine(21); settle(450);
        Recording record;
        QTextCursor cursor(w->textDoc());PDFDocumentWindow *pdf=w->pdfDocument();
        if(!slidesOnly){
        const QString notePath=folder+"/operator-norm-notes.tex";
        check(language+": read an independent operator-norm note",!bytes(notePath).isEmpty());
        auto *note=new TeXDocumentWindow(notePath);place(note);
        check(language+": short note stays in the simple writing interface",articleReady(note)&&!note->property("researchArticleMode").toBool()&&!note->findChild<QWidget *>("researchArticleScope"));
        record.start(output + "/research-" + language, note);settle(700);
        record.action("Open the saved paper: section controls appear automatically");note->hide();record.follow(w);
        check(language+": saved paper is identified without choosing a mode",articleReady(w)&&w->property("researchArticleMode").toBool());
        record.action("Use the visible structure button to choose Interpretation");record.click(outlineButton(w));
        auto *paperTree=w->findChild<QTreeWidget *>("researchArticleTree");
        check(language+": actual paper structure is visible",paperTree&&paperTree->isVisible());
        check(language+": open structure retains at least half of the writing width",w->editor()->isVisible()&&w->editor()->width()>=w->width()/2);
        check(language+": choose the interpretation section",selectEntry(record,paperTree,treeEntry(paperTree,"Interpretation")));
        record.click(w->findChild<QToolButton *>("researchArticleSection"));
        check(language+": section view retains the complete underlying paper",w->editor()->property("researchSourceSlice").toBool()&&w->textDoc()->editorText().contains("\\section{Operator norm}"));
        record.action("Hide the structure and write in the selected section");record.click(w->findChild<QToolButton *>("researchArticleClose"));
        record.action("Write a short conclusion in the research note");
        cursor=QTextCursor(w->textDoc()); cursor.setPosition(w->textDoc()->editorText().indexOf("Noise sets the estimation error.")+QString("Noise sets the estimation error.").size());
        w->editor()->setTextCursor(cursor); record.aim(w->editor()->viewport(), w->editor()->cursorRect().center());
        type(w->editor(), "\nThe bound holds for any noise matrix.");
        check(language + ": real editor input appears in the note", w->textDoc()->editorText().contains("The bound holds for any noise matrix."));
        check(language + ": compile the written conclusion", compile(w, &record));
        check(language+": section compilation saves the whole real paper",QString::fromUtf8(bytes(root)).contains("\\documentclass[11pt]{article}")&&QString::fromUtf8(bytes(root)).contains("\\section{Operator norm}")&&QString::fromUtf8(bytes(root)).contains("\\end{document}"));
        pdf = w->pdfDocument(); check(language + ": real paired article PDF exists", pdf && waitFor([&] { return Research::pdfPairValid(pdf->fileName()); }));
        const QString articlePdf = pdf->fileName();
        auto articleDoc = QtPDF::Backend::Document::newDocument(articlePdf);
        put(output + "/article-pdf-diagnostic.json", QJsonDocument(QJsonObject{{"path",articlePdf},{"valid",articleDoc && articleDoc->isValid()},{"pages",articleDoc ? double(articleDoc->numPages()) : -1}}).toJson());
        check(language + ": actual PDF contains the typed conclusion", contains(articleDoc, "The bound holds"));
        record.action("Read the actual compiled conclusion"); record.follow(pdf);
        pdf->widget()->zoomFitWidth(); settle(1000);
        // Center on the actual conclusion via the viewer's ordinary search action.
        pdf->widget()->search("The bound holds", QtPDF::Backend::Search_CaseInsensitive); settle(1200);
        record.finish(); check(language + ": research capture is valid", record.failures == 0);
        check(language+": real structure reveal has multiple captured intermediate paints",record.motionFrameCount("researchPanelMotionVeil")>=3);
        check(language + ": keep independently compiled article PDF", QFile::copy(articlePdf, folder + "/article.pdf"));
        pdf->hide(); w->hide();

        note->setModified(false);delete note;
        w->findChild<QToolButton *>("researchArticleFull")->click();
        // A two-file paper: root inheritance and honest File scope stay visible.
        const QString referenceRoot=folder+"/spectral-proof.tex",referenceChild=folder+"/variational-proof.tex";
        const QByteArray referenceMain=bytes(referenceRoot),referenceResults=bytes(referenceChild);
        check(language+": create the independent two-file reference paper",put(referenceRoot,referenceMain)&&put(referenceChild,referenceResults));
        auto *referenceWindow=new TeXDocumentWindow(referenceRoot);place(referenceWindow);
        check(language+": prepare the actual current theorem PDF",articleReady(referenceWindow)&&compile(referenceWindow));
        if(referenceWindow->pdfDocument())referenceWindow->pdfDocument()->hide();
        if(auto *a=referenceWindow->findChild<QAction *>("researchOutlineAction");a&&a->isChecked())a->trigger();
        record.start(output+"/reference-"+language,referenceWindow);
        record.action("Navigate from the paper outline to the included Results source");
        record.click(outlineButton(referenceWindow));auto *referenceTree=referenceWindow->findChild<QTreeWidget *>("researchArticleTree");
        check(language+": actual multi-file outline opens Results",selectEntry(record,referenceTree,treeEntry(referenceTree,"Variational bound")));
        TeXDocumentWindow *child=nullptr;
        check(language+": included source inherits the root file",waitFor([&]{child=TeXDocumentWindow::findDocument(referenceChild);return child&&child->property("researchArticleReady").toBool();})&&Research::path(child->textDoc()->getRootFilePath())==Research::path(referenceRoot));
        record.follow(child);
        check(language+": multi-file editing is honestly labelled File",child->findChild<QToolButton *>("researchArticleFull")->text()==(Research::englishUi()?"File":"本文件"));
        auto *childTree=child->findChild<QTreeWidget *>("researchArticleTree");
        if(!childTree->isVisible())record.click(outlineButton(child));
        record.action("Browse the actual theorem proof and equation content of this paper");
        record.click(child->findChild<QToolButton *>("researchArticleObjects"));settle(1200);
        check(language+": Browse lists actual mathematical content without a discipline mode",treeEntry(childTree,"thm:bound")&&treeEntry(childTree,"proof"));
        record.action("Search the theorem label and pin its actual compiled statement");
        record.click(child->findChild<QToolButton *>("researchArticleSearchToggle"));auto *search=child->findChild<QLineEdit *>("researchArticleSearch");check(language+": article filtering uses the actual concise Filter label",child->findChild<QToolButton *>("researchArticleSearchToggle")->text()==(Research::englishUi()?"Filter":"筛选"));record.click(search);type(search,"thm:bound");settle(400);
        check(language+": theorem label search finds the actual object",treeEntry(childTree,"thm:bound"));
        check(language+": actual object context menu pins the theorem",contextPin(record,childTree,treeEntry(childTree,"thm:bound")));
        auto *pin=child->findChild<QDockWidget *>("researchPinnedReference");
        check(language+": pin shows the matching compiled theorem",pin&&waitFor([&]{return pin->property("researchPinnedReady").toBool()&&pin->property("researchPinnedVisual").toBool();}));settle(1500);
        record.action("Pin the unlabelled proof using its current source");
        record.click(search);key(search,Qt::Key_A,Qt::ControlModifier);type(search,"proof");settle(400);
        check(language+": actual proof object is available without a label",treeEntry(childTree,"proof"));
        check(language+": actual context menu pins the proof",contextPin(record,childTree,treeEntry(childTree,"proof")));
        check(language+": unlabelled proof is honestly shown as current source",waitFor([&]{auto *source=child->findChild<QTextBrowser *>("researchPinnedSource");return pin->property("researchPinnedReady").toBool()&&!pin->property("researchPinnedVisual").toBool()&&source&&source->toPlainText().contains("Combining the two inequalities");}));
        {auto *snippet=child->findChild<QTextBrowser *>("researchPinnedSource");const auto a=snippet->document()->defaultFont(),b=child->editor()->font();check(language+": pinned source follows the real editor font",a.family()==b.family()&&a.pixelSize()==b.pixelSize()&&qAbs(a.pointSizeF()-b.pointSizeF())<.01);}
        record.click(child->findChild<QToolButton *>("researchArticleClose"));
        const QString childBefore=child->textDoc()->editorText();QTextCursor writing(child->textDoc());writing.setPosition(childBefore.indexOf("Noise controls the error scale.")+QString("Noise controls the error scale.").size());child->editor()->setTextCursor(writing);record.aim(child->editor()->viewport(),child->editor()->cursorRect().center());
        record.action("Revise the interpretation and let the pinned proof refresh");type(child->editor()," The bound is deterministic.");
        check(language+": editing keeps the actual proof reference open",pin->isVisible()&&waitFor([&]{return pin->property("researchPinnedReady").toBool();})&&child->textDoc()->editorText().contains("The bound is deterministic."));settle(1200);
        child->editor()->undo();check(language+": one undo restores the complete child document",child->textDoc()->editorText()==childBefore&&waitFor([&]{auto *go=child->findChild<QToolButton *>("researchPinnedGoToSource");return pin->property("researchPinnedReady").toBool()&&go&&go->isEnabled();}));
        record.action("Locate the pinned proof in source, then close the reference");record.click(child->findChild<QToolButton *>("researchPinnedGoToSource"));
        check(language+": source button locates the proof environment",waitFor([&]{return child->editor()->textCursor().block().text().contains("\\begin{proof}");}));
        record.click(child->findChild<QToolButton *>("researchPinnedClose"));check(language+": close returns unobstructed writing",!pin->isVisible());
        record.finish();check(language+": reference capture is valid",record.failures==0);
        check(language+": actual pinned panel reveal is captured between endpoints",record.motionFrameCount("researchPanelMotionVeil")>=3);
        check(language+": navigation and reference leave both files untouched",bytes(referenceRoot)==referenceMain&&bytes(referenceChild)==referenceResults);
        child->setModified(false);delete child;if(referenceWindow->pdfDocument())delete referenceWindow->pdfDocument();referenceWindow->setModified(false);delete referenceWindow;
        if(articleOnly){delete pdf;w->setModified(false);delete w;continue;}
        // Generic insertion tools are not a separate introduction story.
        }

        const QString deckRoot = folder + "/talk/shortest-paths.tex"; check(language + ": create the Beamer example", put(deckRoot, talk));
        auto *deckWindow = new TeXDocumentWindow(deckRoot); place(deckWindow);
        auto ready = [&] { return waitFor([&] {
          auto *c = Research::controller(deckWindow);
          return c && c->enabled && !c->dirty && !c->busy && c->deck.sources.value(Research::path(deckRoot)) == deckWindow->textDoc()->editorText();
        }); };
        check(language + ": Beamer is detected automatically", ready());
        auto *c = Research::controller(deckWindow);
        deckWindow->resizeDocks({c->dock, c->previewDock}, {200, 360}, Qt::Horizontal); settle(350);
        check(language + ": prepare the genuine slide preview", compile(deckWindow));
        check(language + ": the live slide preview is ready", ready() && waitFor([&] { return c->previewScene && !c->loadedRevision.isEmpty(); }));
        auto select = [&](int row, Recording *rec = nullptr) {
          if (rec) rec->clickAt(c->list->viewport(), c->list->visualItemRect(c->list->item(row)).center());
          else { c->list->setCurrentRow(row); c->select(row, true); }
          settle(450);
        };
        place(deckWindow,QSize(1280,800)); select(3);
        record.start(output + "/slides-" + language, deckWindow);
        auto *slides=dynamic_cast<Research::SlideList *>(c->list);
        check(language+": actual left slide outline is visible",slides&&slides->isVisible());
        select(3,&record);record.action("Continue the proof in the current slide source");
        put(output+"/deck-before-writing.tex",deckWindow->textDoc()->editorText().toUtf8());
        {auto body=deckWindow->textDoc()->find("of an $s$--$v$ walk with at most $k$ edges.");check(language+": find the actual invariant paragraph before editing",!body.isNull());body.movePosition(QTextCursor::EndOfBlock);deckWindow->editor()->setTextCursor(body);deckWindow->editor()->ensureCursorVisible();record.aim(deckWindow->editor()->viewport(),deckWindow->editor()->cursorRect().center());type(deckWindow->editor(),"\nThis follows by induction on $k$.",40);}
        put(output+"/deck-after-writing.tex",deckWindow->textDoc()->editorText().toUtf8());
        check(language+": current-frame editing preserves the complete deck",ready()&&c->deck.frames.size()==4&&deckWindow->textDoc()->editorText().contains("This follows by induction on $k$."));
        record.action("Select Path invariant directly in the left slide outline");select(3,&record);
        check(language+": the fourth outline item is selected",slides->currentRow()==3&&slides->selectedItems().size()==1);
        const auto original = deckWindow->textDoc()->editorText();
        const auto targetRect = slides->visualItemRect(slides->item(2));
        check(language + ": guarded outline destination accepts the move", slides->dropProblem(2).isEmpty());
        record.action("Drag the fourth outline item above Convergence using the actual solid insertion line");
        record.fastFor(1800);
        slides->setProperty("researchSlideDragging", QVariantList{3});
        const QPoint from = slides->visualItemRect(slides->item(3)).center();
        const QPoint to(targetRect.center().x(), targetRect.top() + 4);
        record.aim(slides->viewport(), from); record.clicking = true;
        const QPoint begin = record.pointer, end = slides->viewport()->mapToGlobal(to);
        bool interpolated = false;
        for (int step = 1; step <= 24; ++step) {
          record.pointer = begin + (end - begin) * step / 24;
          if (step == 16) slides->animateGap(2);
          if (step >= 17) interpolated = interpolated || slides->property("researchSlideGaps").toMap().value("2").toReal() > 0;
          settle(35);
        }
        settle(260);
        check(language + ": actual left outline makes room above the third item", interpolated && slides->visualItemRect(slides->item(2)).height() > targetRect.height());
        check(language + ": sidebar animation leaves every source byte unchanged", deckWindow->textDoc()->editorText() == original);
        {const auto image=slides->viewport()->grab().toImage();const qreal dpr=slides->devicePixelRatioF();const int y=qRound((slides->visualItemRect(slides->item(2)).top()+slides->property("researchSlideGaps").toMap().value("2").toReal()/2)*dpr);int longest=0;
          for(int row=qMax(0,y-2);row<=qMin(image.height()-1,y+2);++row){int dark=0;for(int x=qRound(10*dpr);x<qRound((slides->viewport()->width()-10)*dpr);++x){auto p=image.pixelColor(x,row);dark+=qAbs(p.red()-56)+qAbs(p.green()-60)+qAbs(p.blue()-64)<20;}longest=qMax(longest,dark);}
          check(language+": actual drag marker is a continuous dark line",longest>(slides->viewport()->width()-20)*dpr*.85);}

        record.capture();
        record.action("Release in the left outline to commit the complete slide source");
        record.clicking = false;
        const auto releaseAt=record.elapsed.elapsed();record.fastFor(1000);
        const bool committed = slides->dropBefore(2);
        slides->setProperty("researchSlideDragging", QVariant()); slides->animateGap(-1);
        check(language + ": actual guarded sidebar drop commits the move", committed && ready());
        check(language + ": the outline and source place the bound before the takeaway", c->deck.frames[2].title == "Path invariant" && c->deck.frames[3].title == "Convergence");
        check(language + ": the gesture never opens the overview", !deckWindow->findChild<QDialog *>("researchSlideOverview"));
        settle(1400);
        check(language+": release reflow has multiple distinct actual moving frames",record.motionFrameCount("researchSlideReflow",releaseAt)>=3);
        select(2, &record);
        check(language + ": compile the reordered talk", compile(deckWindow, &record));
        check(language + ": the reordered slide has a current compiled preview", ready() && waitFor([&]{return c->previewScene && c->deck.previewCurrent && c->loadedRevision==c->deck.revision;}));
        settle(700);
        check(language+": actual moving cards have captured intermediate paints",record.motionFrameCount("researchSlideReflow")>=3);
        select(1,&record);
        record.action("Open the notes for the selected slide");
        record.click(deckWindow->findChild<QToolButton *>("researchSlideNotesButton"));
        auto *notes=deckWindow->findChild<QPlainTextEdit *>("researchSlideNote0");
        check(language+": real speaker notes are visible",notes&&notes->isVisible());settle(700);
        const auto beforeOutput=deckWindow->textDoc()->editorText();
        bool outputSeen=false,bothChosen=false,outputFits=false;
        record.action("Choose audience and speaker outputs; dismiss the dialog after showing the choice");
        QTimer outputResponder;outputResponder.setInterval(40);
        QObject::connect(&outputResponder,&QTimer::timeout,&app,[&]{
          auto *d=deckWindow->findChild<QDialog *>("researchSlideOutputDialog");if(!d||!d->isVisible())return;
          outputResponder.stop();outputSeen=true;outputFits=record.contains(d);settle(300);
          auto *profile=d->findChild<QComboBox *>("researchSlideOutputProfile");record.click(profile);settle(250);
          auto *options=profile->view();outputFits=outputFits&&record.contains(options->window());
          record.clickAt(options->viewport(),options->visualRect(profile->model()->index(3,0)).center());
          bothChosen=profile->currentIndex()==3;settle(900);d->reject();
        });outputResponder.start();
        check(language+": output is opened from the visible menu",record.choose(deckWindow->findChild<QToolButton *>("researchSlidePreviewMore"),deckWindow->findChild<QAction *>("researchSlideExportMenuAction")));
        outputResponder.stop();
        check(language+": one Beamer story includes both output choices",outputSeen&&bothChosen&&outputFits);
        check(language+": cancelling output preserves the complete deck",deckWindow->textDoc()->editorText()==beforeOutput);
        record.finish();check(language+": combined slide story is valid",record.failures==0);

        if(slidesOnly){deckWindow->setModified(false);delete deckWindow;if(pdf)delete pdf;w->setModified(false);delete w;continue;}

        // Demonstrate ordinary paper writing, independent of the Beamer workspace.
        deckWindow->hide();w->hide();const QString focusPath=folder+"/institutions-and-resources.tex";check(language+": create an independent ordinary article for focus",put(focusPath,bytes(materialDir+"/common-pool-resources.tex")));auto *focusWindow=new TeXDocumentWindow(focusPath);place(focusWindow,QSize(800,500)); focusWindow->goToLine(focusWindow->textDoc()->editorText().left(focusWindow->textDoc()->editorText().indexOf("A citation should identify both the work and the page supporting it.")).count(QLatin1Char('\n'))+1); settle(550);
        check(language + ": focus example is an ordinary article", focusWindow->textDoc()->editorText().contains("\\documentclass[11pt]{article}") && !focusWindow->textDoc()->editorText().contains("\\begin{frame}"));
        record.start(output + "/focus-" + language, focusWindow);
        record.action("Open More in the ordinary article and enter focus mode");
        auto *more = focusWindow->findChild<QToolButton *>("researchMore");
        auto *focus = focusWindow->findChild<QAction *>("researchFocusToggle");
        check(language + ": focus mode has its ordinary menu entry", more && focus);
        check(language + ": actual focus menu stays inside the frame", record.choose(more, focus));
        check(language + ": the article enters actual focus mode", waitFor([&] { return focusWindow->property("researchFocus").toBool() && !focusWindow->statusBar()->isVisible(); }));
        auto *editor = focusWindow->editor();
        const QString beforeFocus = focusWindow->textDoc()->editorText();
        cursor = QTextCursor(focusWindow->textDoc()); cursor.setPosition(beforeFocus.indexOf("A citation should identify both the work and the page supporting it."));
        cursor.movePosition(QTextCursor::EndOfBlock); editor->setTextCursor(cursor);
        record.aim(editor->viewport(), editor->cursorRect().center()); settle(1600);
        record.action("Continue the institutional argument in an ordinary article");
        type(editor, " Clear scope makes these comparisons easier to defend.");
        bool fullSourceVisible=true;for(auto block=focusWindow->textDoc()->begin();block.isValid();block=block.next())fullSourceVisible=fullSourceVisible&&block.isVisible();
        check(language + ": full article remains editable in focus mode", fullSourceVisible && focusWindow->textDoc()->editorText().contains("Clear scope makes these comparisons easier to defend."));
        record.finish(); check(language + ": article focus capture is valid", record.failures == 0);
        check(language+": focus captures real chrome-only transition frames",record.motionFrameCount("researchChromeVeil")>=3);
        editor->undo();
        check(language + ": undo restores every byte of the article", focusWindow->textDoc()->editorText() == beforeFocus);
        Research::setFocusMode(false);focusWindow->setModified(false);delete focusWindow;deckWindow->setModified(false);delete deckWindow;

        // Continue a physics revision while keeping its certified previous PDF.
        delete pdf;w->setModified(false);delete w;
        const QString physicsRoot=folder+"/damped-oscillator.tex";
        w=new TeXDocumentWindow(physicsRoot);place(w,QSize(800,500));
        check(language+": prepare the actual oscillator derivation PDF",compile(w));
        pdf=w->pdfDocument();check(language+": oscillator PDF is paired",pdf&&Research::pdfPairValid(pdf->fileName()));
        const QString physicsPdf=pdf->fileName();
        // Fix both real window geometries before SyncTeX, so following the
        // result does not resize the PDF after the product has centered it.
        place(pdf,QSize(800,500));pdf->hide();place(w,QSize(800,500));w->goToLine(19);settle(450);
        record.start(output + "/preview-" + language, w);
        record.action("Save a revised draft while retaining its previous compiled PDF");
        check(language+": edit the current article before using its previous PDF",replace(w,"Assume $m>0$, $k>0$, and $c\\geq0$.","Assume $m>0$, $k>0$, and $c\\geq0$.\nThe damping force is proportional to velocity."));
        check(language+": save changed source without replacing its paired PDF",w->save()&&!Research::pdfPairValid(physicsPdf)&&Research::pdfCompiledPairValid(physicsPdf));
        settle(500);

        const int reference = w->textDoc()->editorText().indexOf("\\eqref{eq:solution}");
        cursor = QTextCursor(w->textDoc()); cursor.setPosition(reference + 11);
        record.action("Hover over the equation reference to see its real formula");
        record.aim(w->editor()->viewport(), w->editor()->cursorRect(cursor).center());
        QWidget *card = nullptr;
        check(language + ": equation hover displays the actual formula card", waitFor([&] {
          for (auto *item : QApplication::topLevelWidgets()) if (item->objectName() == "researchReferenceCard" && item->isVisible()) { card = item; return true; }
          return false;
        }));
        check(language + ": formula card is fully visible", record.contains(card)); settle(1500);
        check(language+": actual hover explicitly marks the previous PDF",card&&card->findChild<QLabel *>("researchReferenceRevision")&&card->findChild<QLabel *>("researchReferenceRevision")->isVisible());
        const int formula = w->textDoc()->editorText().indexOf("x(t)=A");
        cursor.setPosition(formula + 2); w->editor()->setTextCursor(cursor); w->editor()->ensureCursorVisible();
        const int formulaLine = cursor.blockNumber();
        record.action("Control-click this formula in source to locate its PDF");
        record.modifier = "Ctrl"; record.clickAt(w->editor()->viewport(), w->editor()->cursorRect(cursor).center(), Qt::ControlModifier);
        check(language + ": the real source gesture reaches the paired PDF", waitFor([&] { return pdf->isVisible(); }));
        pdf->grab().save(output+"/synctex-before-record-follow-"+language+".png");
        record.follow(pdf); settle(700); record.modifier.clear();
        QGraphicsPathItem *highlight = nullptr;
        for (auto *item : pdf->widget()->scene()->items()) if (auto *path = dynamic_cast<QGraphicsPathItem *>(item))
          if (path->brush().color() == QColor(255, 255, 0, 63)) { highlight = path; break; }
        check(language + ": SyncTeX highlights the actual formula", highlight);
        const QPoint pdfPoint = pdf->widget()->mapFromScene(highlight->mapToScene(highlight->boundingRect().center()));
        check(language + ": the synchronized equation is inside the PDF viewport", pdf->widget()->viewport()->rect().contains(pdfPoint));
        settle(1000); record.action("Control-click the highlighted formula to return to its source");
        w->goToLine(1); record.modifier = "Ctrl"; record.clickAt(pdf->widget()->viewport(), pdfPoint, Qt::ControlModifier); record.modifier.clear();
        check(language + ": inverse SyncTeX returns to the same equation", qAbs(w->editor()->textCursor().blockNumber() - formulaLine) <= 1);
        record.follow(w); settle(1000); record.finish(); check(language + ": formula capture is valid", record.failures == 0);
        delete pdf; w->setModified(false); delete w; settle(350);

        const QString reviewRoot = folder + "/stability-revision.tex";
        check(language + ": create a separate external-review example", put(reviewRoot, article));
        w = new TeXDocumentWindow(reviewRoot); place(w, QSize(1280, 620));
        check(language + ": draft a local change", replace(w, "Noise sets the estimation error.", "Noise sets the error bound."));
        w->goToLine(w->textDoc()->editorText().left(w->textDoc()->editorText().indexOf("Noise sets the error bound.")).count(QLatin1Char('\n'))+1); settle(400);
        record.start(output + "/collaboration-" + language, w);
        record.action("An external editor saves a disjoint change and a conflicting paragraph");
        QString external = QString::fromUtf8(article);
        external.replace("Let $u$ and $v$ be unit vectors.", "Assume $u$ and $v$ have unit norm.");
        external.replace("Noise sets the estimation error.", "Noise determines the error scale.");
        check(language + ": external revision commits to the private file", put(reviewRoot, external.toUtf8()) && bytes(reviewRoot) == external.toUtf8());
        check(language + ": filesystem watcher merges the safe edit and retains a conflict", waitFor([&] {
          return w->collaboration()->conflictCount() == 1 && w->textDoc()->editorText().contains("Assume $u$ and $v$ have unit norm.");
        }));
        check(language + ": the local conflicting text stays in the editor", w->textDoc()->editorText().contains("Noise sets the error bound."));
        record.action("Review the automatically merged change");
        auto *reviewMenu=w->findChild<QMenu *>("researchFileMenu");
        auto *reviewAction=w->findChild<QAction *>("researchReviewUpdates");
        check(language + ": open the visible collaboration review menu",record.chooseNested(w->findChild<QToolButton *>("researchMore"),reviewMenu,reviewAction));
        auto *panel = w->findChild<QDockWidget *>("researchReviewPanel");
        auto *reviewText = w->findChild<QTextBrowser *>("researchReviewText");
        check(language + ": actual review panel is visible", panel && panel->isVisible() && reviewText);
        w->resizeDocks({panel},{340},Qt::Vertical);settle(250);
        check(language + ": all review controls stay inside the recording",w->size()==record.canvas.size());
        settle(1000);
        for (int step = 0; step < 4 && !reviewText->toPlainText().contains("Noise determines the error scale."); ++step)
          record.click(w->findChild<QToolButton *>("researchReviewNext"));
        check(language + ": conflict view presents both real alternatives", reviewText->toPlainText().contains("Noise sets the error bound.") && reviewText->toPlainText().contains("Noise determines the error scale."));
        const auto localWording=reviewText->document()->find("Noise sets the error bound.");
        const auto externalWording=reviewText->document()->find("Noise determines the error scale.");
        reviewText->setTextCursor(localWording);reviewText->ensureCursorVisible();settle(200);
        check(language + ": both conflicting wordings appear in the actual viewport",reviewText->viewport()->rect().contains(reviewText->cursorRect(localWording).center())&&reviewText->viewport()->rect().contains(reviewText->cursorRect(externalWording).center()));
        record.action("Accept all pending external edits after inspecting the alternatives"); settle(1500);
        record.click(w->findChild<QPushButton *>("researchUseAllRemote"));
        check(language + ": the explicit choice resolves the conflict", w->collaboration()->conflictCount() == 0 && w->textDoc()->editorText().contains("Noise determines the error scale."));
        check(language+": bulk acceptance does not overwrite the external file on disk",bytes(reviewRoot)==external.toUtf8());
        check(language + ": the conflict-free edit is retained too", w->textDoc()->editorText().contains("Assume $u$ and $v$ have unit norm."));
        settle(800); record.finish(); check(language + ": collaboration capture is valid", record.failures == 0);
        w->setModified(false); delete w; settle(350);
      }
    } catch (const std::exception &error) {
      put(output + "/failure.txt", QByteArray(error.what()));
      int failureWindow=0;for (auto *window : QApplication::topLevelWidgets()) if (window->isVisible())
        window->grab().save(output + "/failure-" + QString::number(failureWindow++) + "-" + window->objectName() + ".png");
    }
    saveChecks(); app.exit(failures ? 1 : 0);
  });
  QTimer::singleShot(900000, &app, [&] { app.exit(92); }); return app.exec();
}

