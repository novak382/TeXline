#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "ResearchOnboarding.h"
#include "ResearchAppearance.h"
#include "Settings.h"
#include "TeXDocumentWindow.h"
#include "PDFDocumentWindow.h"
#include "CompletingEdit.h"
#include <QDir>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QMovie>
#include <QPushButton>
#include <QSaveFile>
#include <QScreen>
#include <QThread>
#include <QToolButton>
#include <QWindow>
#include <functional>
#include <windows.h>
#include <shellapi.h>

static bool waitFor(const std::function<bool()> &ready, int ms = 4000) {
  QElapsedTimer time; time.start();
  while (!ready() && time.elapsed() < ms) { QCoreApplication::processEvents(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); QThread::msleep(4); }
  return ready();
}
static void settle(int ms = 180) { QElapsedTimer time; time.start(); waitFor([&] { return time.elapsed() >= ms; }, ms + 1000); }
static QByteArray read(const QString &path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray(); }
static bool put(const QString &path, const QByteArray &data) { QSaveFile file(path); return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit(); }
static QDialog *dialog() { for (auto *w : QApplication::topLevelWidgets()) if (w->objectName() == "researchOnboarding" && w->isVisible()) return qobject_cast<QDialog *>(w); return nullptr; }
static int introCount() { int count = 0; for (auto *w : QApplication::topLevelWidgets()) count += w->objectName() == "researchOnboarding"; return count; }
static int decoderCount() { int count = 0; for (auto *w : QApplication::topLevelWidgets()) count += w->findChildren<QMovie *>("onboardingMovie").size(); return count; }
static void click(QDialog *d, const char *name) { if (auto *button = d ? d->findChild<QPushButton *>(name) : nullptr) button->click(); settle(); }
static void closeIntro() { if (auto *d = dialog()) d->reject(); settle(); }
static QString argument(int index) { int count = 0; auto args = CommandLineToArgvW(GetCommandLineW(), &count); QString value = index < count ? QString::fromWCharArray(args[index]) : QString(); LocalFree(args); return value; }

int main(int argc, char **argv) {
  if (!qEnvironmentVariable("TEXLINE_TEST_DESKTOP").startsWith("TeXlineValidation-")) return 2;
  const QString report = argument(1);
  const QString pdf = argument(2);
  argc = 1; TWApp app(argc, argv); app.setQuitOnLastWindowClosed(false);
  Tw::Settings().setValue("researchOnboardingSeen", false);
  app.applyTranslation("zh_CN");
  auto runChecks = [&] {
    QJsonArray checks, geometry; int failures = 0;
    auto check = [&](QString name, bool passed) { checks.append(QJsonObject{{"name", name}, {"passed", passed}}); failures += !passed; };
    const QString packDir = QCoreApplication::applicationDirPath() + "/resources/onboarding";
    const QByteArray content = read(packDir + "/content.json"), style = read(packDir + "/style.qss");
    const auto executableHash = QCryptographicHash::hash(read(QCoreApplication::applicationFilePath()), QCryptographicHash::Sha256);
    auto *source = TeXDocumentWindow::documentList().value(0);
    check("fresh launch opens exactly one optional introduction", source && dialog() && introCount() == 1);
    auto *d = dialog();
    if (d) {
      check("language choice precedes the six introduction pages", d->property("onboardingPageIndex").toInt() == -1 && d->findChild<QPushButton *>("onboardingChinese")->isVisible() && d->findChild<QPushButton *>("onboardingEnglish")->isVisible());
      d->grab().save(QFileInfo(report).absolutePath() + "/onboarding-language.png");
      auto *traditional=d->findChild<QPushButton *>("onboardingTraditional");
      auto *chinese=d->findChild<QPushButton *>("onboardingChinese");
      auto *english=d->findChild<QPushButton *>("onboardingEnglish");
      check("three equal language choices fit in one row",traditional&&traditional->isVisible()&&traditional->text()==QString::fromUtf8("繁體中文")&&qAbs(traditional->width()-chinese->width())<=1&&qAbs(traditional->width()-english->width())<=1&&traditional->y()==chinese->y()&&traditional->y()==english->y()&&traditional->parentWidget()->rect().contains(traditional->geometry())&&traditional->parentWidget()->rect().contains(english->geometry()));
      if(traditional){
        traditional->click();
        check("Traditional selection persists and opens the existing first page",Tw::Settings().value("locale").toString()=="zh_TW"&&d->property("onboardingPageIndex").toInt()==0);
        click(d,"onboardingBack");
        check("back keeps all three language options available",d->property("onboardingPageIndex").toInt()==-1&&traditional->isVisible()&&chinese->isVisible()&&english->isVisible());
      }
      click(d, "onboardingChinese");
      check("Chinese selection is remembered and opens page one", Tw::Settings().value("locale").toString() == "zh_CN" && d->property("onboardingPageIndex").toInt() == 0);
      check("external pack is loaded on the first launch", d->property("onboardingExternal").toBool());
      const auto *close = d->findChild<QPushButton *>("onboardingClose");
      check("close uses the existing icon and an accessible name", close && close->text().isEmpty() && !close->icon().isNull() && !close->accessibleName().isEmpty() && close->toolTip().isEmpty());
      check("all six external introduction pages are available", d->property("onboardingPageCount").toInt() == 6);
      const QStringList titles{"打开论文，看到 PDF", "插入第一条文献引用", "找到定理在哪里用到", "改写前，保留这一段", "明天，从这里接着写", "整理讲稿，再展开预览"};
      const QStringList bodies{"打开项目中的主文稿，编辑后编译；在源码与 PDF 间定位。", "插入 → 文献引用，关联文献文件，选择条目后插入。", "在定理处右键查阅上下文，查看引用位置，再返回原处。", "选中证明，右键保留选区版本；改写后比较并取回。", "文件 → 保存写作现场；下次搜索“接着写”，恢复原位置。", "在左侧调整幻灯片顺序，点击预览右上角展开图标。"};
      const QRect area = d->windowHandle()->screen()->availableGeometry();
      const auto frame = d->frameGeometry();
      geometry.append(QJsonObject{{"work", QJsonArray{area.x(), area.y(), area.width(), area.height()}}, {"frame", QJsonArray{frame.x(), frame.y(), frame.width(), frame.height()}}, {"minimumHeight", d->minimumHeight()}});
      check("complete dialog fits the monitor work area", area.contains(d->frameGeometry()));
      for (int page = 0; page < 6; ++page) {
        check(QString("page %1 preserves exact punctuation-free copy").arg(page + 1), d->findChild<QLabel *>("onboardingTitle")->text() == titles[page] && d->findChild<QLabel *>("onboardingBody")->text() == bodies[page]);
        auto *movie = d->findChild<QMovie *>("onboardingMovie");
        check(QString("page %1 loads the expected external visual").arg(page + 1), movie && movie->isValid() && movie->frameCount() > 2);
        if (movie) {
          auto *play = d->findChild<QPushButton *>("onboardingPlayback");
          if (movie->state() != QMovie::Running) play->click();
          check(QString("page %1 plays without caching all frames").arg(page + 1), movie->cacheMode() == QMovie::CacheNone && waitFor([&] { return movie->currentFrameNumber() > 0; }));
          if (movie->state() == QMovie::Running) play->click();
          const int frame = movie->currentFrameNumber(); settle(230);
          check(QString("page %1 pauses on the current frame").arg(page + 1), movie->state() == QMovie::Paused && movie->currentFrameNumber() == frame);
          play->click(); check(QString("page %1 resumes on demand").arg(page + 1), waitFor([&] { return movie->currentFrameNumber() != frame; }));
        }
        check(QString("page %1 controls fit without clipping").arg(page + 1), d->rect().contains(d->findChild<QPushButton *>("onboardingNext")->geometry()) && d->findChild<QPushButton *>("onboardingSkip")->isVisible());
        check(QString("Chinese page %1 fits the work area").arg(page + 1), area.contains(d->frameGeometry()));
        d->grab().save(QFileInfo(report).absolutePath() + QString("/onboarding-zh-%1.png").arg(page + 1));
        QPointer<QMovie> oldMovie(movie); click(d, "onboardingNext");
        check(QString("leaving page %1 releases its decoder").arg(page + 1), !oldMovie);
      }
      check("completion remembers seen state and removes the introduction", !dialog() && Tw::Settings().value("researchOnboardingSeen").toBool());
    }
    Research::showOnboarding(source, true); settle(); check("later startup does not repeat completed onboarding", !dialog());
    if (source) {
      const QString before = source->editor()->toPlainText();
      auto *action = source->findChild<QAction *>("researchOnboardingAction");
      auto *normalButton=source->findChild<QToolButton *>("researchMore"),*focusButton=source->findChild<QToolButton *>("researchFocusMore");
      auto *normal=normalButton?normalButton->menu():nullptr,*focused=focusButton?focusButton->menu():nullptr;
      std::function<bool(QMenu *,int)> includes=[&](QMenu *menu,int depth){if(!menu||depth>3)return false;for(auto *entry:menu->actions())if(entry->isVisible()&&entry->isEnabled()&&(entry==action||(entry->menu()&&includes(entry->menu(),depth+1))))return true;return false;};
      if(normal){normal->popup(source->mapToGlobal(QPoint(16,50)));settle();}
      const bool normalEntry=action&&normal&&normal->isVisible()&&includes(normal,1);if(normal)normal->close();
      Research::setFocusMode(true);settle();if(focused){focused->popup(source->mapToGlobal(QPoint(16,50)));settle();}
      const bool focusEntry=action&&focused&&focused->isVisible()&&includes(focused,1);if(focused)focused->close();Research::setFocusMode(false);settle();
      check("both normal and focus menus expose the same reopening action",normalEntry&&focusEntry);
      if (action) action->trigger();
      settle(); check("menu reopens after completion", dialog());
      Research::showOnboarding(source); settle(); check("repeat menu requests retain one introduction", introCount() == 1);
      if (dialog()) {
        click(dialog(), "onboardingEnglish");
        check("English selection is remembered", Tw::Settings().value("locale").toString() == "en_US");
        auto *dot = dialog()->findChild<QToolButton *>("onboardingDot4"); dot->click(); settle();
        check("progress dots navigate directly", dialog()->property("onboardingPageIndex").toInt() == 4);
        click(dialog(), "onboardingBack"); check("Back returns to the preceding page", dialog()->property("onboardingPageIndex").toInt() == 3);
        app.applyTranslation("en_US"); settle();
        check("open introduction follows the UI language", dialog()->findChild<QLabel *>("onboardingTitle")->text() == "Keep a passage before rewriting");
        check("English uses an English recording", dialog()->property("onboardingMediaPath").toString().endsWith("versions-en.gif"));
        for (int i = 0; i < 6; ++i) {
          dialog()->findChild<QToolButton *>(QString("onboardingDot%1").arg(i))->click(); settle();
          check(QString("English page %1 loads a valid visual").arg(i + 1), dialog()->findChild<QMovie *>("onboardingMovie") && dialog()->findChild<QMovie *>("onboardingMovie")->isValid());
          check(QString("English page %1 fits the work area").arg(i + 1), dialog()->windowHandle()->screen()->availableGeometry().contains(dialog()->frameGeometry()));
          dialog()->grab().save(QFileInfo(report).absolutePath() + QString("/onboarding-en-%1.png").arg(i + 1));
        }
      }
      click(dialog(), "onboardingSkip");
      check("Skip releases the introduction and preserves document text", !dialog() && source->editor()->toPlainText() == before);
      Research::setFocusMode(true); app.applyTranslation("zh_CN"); Research::showOnboarding(source); settle();
      check("introduction opens on the focus workspace", dialog() && source->property("researchFocus").toBool());
      closeIntro(); check("window close also remembers dismissal", !dialog() && Tw::Settings().value("researchOnboardingSeen").toBool());
    }
    auto object = QJsonDocument::fromJson(content).object();
    auto pages = object["pages"].toArray(); auto first = pages[0].toObject();
    first["title"] = QJsonObject{{"zh_CN", "<b>外置更新</b>"}, {"en", "External update"}}; pages[0] = first; pages.append(first);
    object["pages"] = pages; object["revision"] = "future-content-only-update";
    check("updated pack can be saved independently", put(packDir + "/content.json", QJsonDocument(object).toJson()));
    put(packDir + "/style.qss", style + "\nQDialog#researchOnboarding { background: #f9f5ed; }\n");
    Research::showOnboarding(source, true); settle(); check("content revisions do not reset seen state", !dialog());
    Research::showOnboarding(source); settle();
    click(dialog(), "onboardingChinese");
    d = dialog();
    check("reopening picks up text and page-count changes without rebuilding", d && d->property("onboardingPageCount").toInt() == 7 && d->findChild<QLabel *>("onboardingTitle")->text() == "<b>外置更新</b>");
    check("external text remains plain text", d && d->findChild<QLabel *>("onboardingTitle")->textFormat() == Qt::PlainText);
    check("reopening picks up external style changes", d && d->grab().toImage().pixelColor(16, 16) == QColor("#f9f5ed")); closeIntro();
    check("content and style updates leave the executable unchanged", executableHash == QCryptographicHash::hash(read(QCoreApplication::applicationFilePath()), QCryptographicHash::Sha256));
    put(packDir + "/content.json", "{broken json"); Research::showOnboarding(source); settle();
    click(dialog(), "onboardingChinese");
    const int embeddedPages=QJsonDocument::fromJson(read(":/research/onboarding/content.json")).object()["pages"].toArray().size();
    check("malformed content falls back to the embedded page set", embeddedPages==6 && dialog() && !dialog()->property("onboardingExternal").toBool() && dialog()->property("onboardingPageCount").toInt() == embeddedPages); closeIntro();
    object = QJsonDocument::fromJson(content).object(); pages = object["pages"].toArray(); first = pages[0].toObject();
    first["media"] = QJsonObject{{"zh_CN", "../outside.gif"}, {"en", "../outside.gif"}}; pages[0] = first; object["pages"] = pages;
    put(packDir + "/content.json", QJsonDocument(object).toJson()); Research::showOnboarding(source); settle();
    click(dialog(), "onboardingChinese");
    check("media paths cannot escape the content pack", dialog() && dialog()->property("onboardingMediaPath").toString().isEmpty() && !dialog()->findChild<QMovie *>("onboardingMovie")); closeIntro();
    put(packDir + "/content.json", content); put(packDir + "/style.qss", style);
    if (!pdf.isEmpty()) {
      auto *viewer = new PDFDocumentWindow(pdf); viewer->show(); settle();
      auto *action = viewer->findChild<QAction *>("researchOnboardingAction");
      check("PDF workspace also exposes the reopening action", action);
      if (action) action->trigger();
      settle(); check("PDF menu opens the same introduction", dialog() && dialog()->parentWidget() == viewer); closeIntro();
      delete viewer;
    }
    check("all GIF decoders release after dismissal", decoderCount() == 0);
    const auto screens = QGuiApplication::screens();
    if (screens.size() > 1 && source) {
      source->windowHandle()->setScreen(screens[1]); source->move(screens[1]->availableGeometry().topLeft() + QPoint(20, 20));
      Research::showOnboarding(source); settle(); check("reopening centers within the second monitor", dialog() && screens[1]->availableGeometry().contains(dialog()->frameGeometry())); closeIntro();
    }
    const QJsonObject result{{"checks", checks}, {"geometry", geometry}, {"failures", failures}, {"devicePixelRatio", source ? source->devicePixelRatioF() : 0}, {"physicalScreens", screens.size()}, {"secondMonitorCovered", screens.size() > 1}, {"desktop", qEnvironmentVariable("TEXLINE_TEST_DESKTOP")}, {"physicalMouseAndImeCovered", false}};
    put(report, QJsonDocument(result).toJson());
    app.exit(failures ? 1 : 0);
  };
  // Wait in the outer event loop: document creation may process events before
  // launchAction has finished scheduling the introduction.
  QPointer<QTimer> startup = new QTimer(&app); startup->setInterval(50);
  QObject::connect(startup, &QTimer::timeout, &app, [&] {
    if (!dialog() || TeXDocumentWindow::documentList().isEmpty()) return;
    startup->stop(); startup->deleteLater(); QTimer::singleShot(0, &app, runChecks);
  });
  startup->start();
  QTimer::singleShot(8000, &app, [&] {
    if (!startup || !startup->isActive()) return;
    put(report, QJsonDocument(QJsonObject{{"checks", QJsonArray{QJsonObject{{"name", "startup introduction appears"}, {"passed", false}}}}, {"failures", 1}}).toJson());
    app.exit(1);
  });
  return app.exec();
}
