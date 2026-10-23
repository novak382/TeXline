#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "TeXDocumentWindow.h"
#include "PDFDocumentWindow.h"
#include "CompletingEdit.h"
#include "ResearchAppearance.h"
#include "Settings.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGraphicsView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QSaveFile>
#include <QThread>
#include <QTimer>
#include <QVariantAnimation>

static void settle(int ms = 0) {
  QElapsedTimer clock; clock.start();
  do { QCoreApplication::processEvents(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); if (ms) QThread::msleep(2); } while (clock.elapsed() < ms);
}
int main(int argc, char **argv) {
  const QString report = QString::fromLocal8Bit(argv[1]), pdf = QString::fromLocal8Bit(argv[2]); argc = 1;
  TWApp app(argc, argv); app.setQuitOnLastWindowClosed(false);
  QTimer::singleShot(100, &app, [&] {
    Tw::Settings().setValue("openPDFwithTeX", false);
    Tw::Settings().setValue("researchFocusMode", false);
    const QString path = QFileInfo(report).absolutePath() + "/transition 中文.tex";
    const QString content = QString("Independent source 中文 $x$.\n").repeated(400);
    QDir().mkpath(QFileInfo(path).absolutePath()); QSaveFile file(path);
    QJsonArray checks; QJsonObject measurements; int failures = 0;
    auto check = [&](const QString &name, bool ok) {
      failures += !ok; checks.append(QJsonObject{{"name", name}, {"passed", ok}});
      QSaveFile output(report); if (output.open(QIODevice::WriteOnly)) { output.write(QJsonDocument(QJsonObject{{"checks", checks}, {"failures", failures}, {"measurements", measurements}, {"physicalInputVerified", false}}).toJson()); output.commit(); }
    };
    check("independent source is written", file.open(QIODevice::WriteOnly) && file.write(content.toUtf8()) == content.toUtf8().size() && file.commit());
    auto *source = new TeXDocumentWindow(path); source->resize(850, 600); source->show(); settle(650);
    auto *preview = new PDFDocumentWindow(pdf); preview->resize(850, 600); preview->show(); settle(650);
    auto interrupted = [&](QMainWindow *window, const QString &name) {
      Research::setFocusMode(false); settle(650); Research::setFocusMode(true);
      auto *veil = window->findChild<QWidget *>("researchChromeVeil");
      auto *transition = window->findChild<QObject *>("researchWorkspaceTransition");
      auto *animation = transition ? transition->findChild<QVariantAnimation *>() : nullptr;
      auto *deadline = transition ? transition->findChild<QTimer *>() : nullptr;
      check(name + " starts with a live transition", veil && veil->isVisible() && animation && deadline && deadline->isActive());
      if (!veil || !animation || !deadline) return;
      animation->pause(); QThread::msleep(220);
      measurements.insert(name + " overdueTimerMs", deadline->remainingTime());
      check(name + " timer expiry is deliberately awaiting event dispatch", deadline->isActive() && deadline->remainingTime() == 0 && veil->isVisible());
      QPixmap paint(veil->size()); paint.fill(Qt::transparent); veil->render(&paint);
      check(name + " first late paint releases stale frame without timer dispatch", veil->isHidden() && veil->property("researchSnapshotBytes").toLongLong() == 0 && animation->state() == QAbstractAnimation::Stopped);
      settle();
    };
    interrupted(source, "source"); interrupted(preview, "PDF");
    for (auto *window : QList<QMainWindow *>{source, preview}) {
      Research::setFocusMode(false); settle(650); Research::setFocusMode(true);
      Research::setFocusMode(false); // No timer, animation tick or paint dispatch.
      auto *frame = window->findChild<QWidget *>("researchChromeVeil");
      auto *owner = window->findChild<QObject *>("researchWorkspaceTransition");
      auto *motion = owner ? owner->findChild<QVariantAnimation *>() : nullptr;
      check(QString(window == source ? "source" : "PDF") + " rapid reversal exposes final layout before event dispatch",
            !window->property("researchFocus").toBool() && frame && frame->isHidden()
            && frame->property("researchSnapshotBytes").toLongLong() == 0 && motion && motion->state() == QAbstractAnimation::Stopped);
    }
    Research::setFocusMode(false); settle(650); Research::setFocusMode(true);
    auto *veil = source->findChild<QWidget *>("researchChromeVeil");
    source->editor()->insertPlainText("changed");
    check("text edits immediately expose the live document", veil && veil->isHidden() && veil->property("researchSnapshotBytes").toLongLong() == 0);
    source->editor()->undo(); check("interruption preserves native undo and all source", source->editor()->text() == content);
    Research::setFocusMode(false); settle(650); Research::setFocusMode(true);
    QKeyEvent key(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier); QApplication::sendEvent(source->editor(), &key);
    check("cursor input exposes the live layout before handling the key", veil->isHidden() && source->editor()->text() == content);
    Research::setFocusMode(false); settle(650);
    check("focus state and document modified state remain correct", !source->property("researchFocus").toBool() && !source->isModified());
    QFile disk(path); check("view and input operations preserve disk bytes", disk.open(QIODevice::ReadOnly) && disk.readAll() == content.toUtf8());
    source->setModified(false); source->close(); preview->close(); settle(80); app.exit(failures ? 1 : 0);
  }); return app.exec();
}
