#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "TeXDocumentWindow.h"
#include "CompletingEdit.h"
#include "ResearchWorkflow.h"
#include "Settings.h"
#include "document/TeXDocument.h"
#include "utils/FilePath.h"
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QRunnable>
#include <QSaveFile>
#include <QSemaphore>
#include <QStatusBar>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <QUuid>
#include <memory>

static void settle(int ms = 0) {
  QElapsedTimer clock; clock.start();
  do { QCoreApplication::processEvents(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); if (ms) QThread::msleep(2); } while (clock.elapsed() < ms);
}
static QJsonObject read(const QString &path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject(); }
struct PoolHold {
  struct Barrier { QSemaphore entered, release; };
  std::shared_ptr<Barrier> barrier = std::make_shared<Barrier>();
  QThreadPool *pool = QThreadPool::globalInstance(); int previous = pool->maxThreadCount(); bool valid = false, finished = false;
  PoolHold() {
    pool->waitForDone(5000); pool->setMaxThreadCount(1); const auto state = barrier;
    pool->start(QRunnable::create([state] { state->entered.release(); state->release.acquire(); }));
    valid = barrier->entered.tryAcquire(1, 5000);
  }
  bool finish() { if (finished) return true; finished = true; barrier->release.release(); const bool done = pool->waitForDone(5000); pool->setMaxThreadCount(previous); return done; }
  ~PoolHold() { finish(); }
};
int main(int argc, char **argv) {
  const QString report = QString::fromLocal8Bit(argv[1]); argc = 1;
  QApplication::setAttribute(Qt::AA_DontUseNativeDialogs); TWApp app(argc, argv); app.setQuitOnLastWindowClosed(false);
  QTimer::singleShot(100, &app, [&] {
    Tw::Settings().setValue("openPDFwithTeX", false);
    const QString root = QFileInfo(report).absolutePath() + "/async-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QDir().mkpath(root); const QString path = root + "/恢复 中文.tex";
    const QString original = "\\documentclass{article}\n\\begin{document}\n" + QString("Paragraph mathematical source 中文 $x_i$ alpha beta gamma.\n").repeated(100000) + "\\end{document}\n";
    QJsonArray checks; QJsonObject measurements; int failures = 0;
    auto check = [&](const QString &name, bool ok) {
      failures += !ok; checks.append(QJsonObject{{"name", name}, {"passed", ok}}); QSaveFile output(report);
      if (output.open(QIODevice::WriteOnly)) { output.write(QJsonDocument(QJsonObject{{"checks", checks}, {"failures", failures}, {"measurements", measurements}, {"physicalInputVerified", false}}).toJson()); output.commit(); }
    };
    QSaveFile file(path); check("independent long source is written", file.open(QIODevice::WriteOnly) && file.write(original.toUtf8()) == original.toUtf8().size() && file.commit());
    auto *window = new TeXDocumentWindow(path); window->show(); settle(600);
    QTimer *timer = nullptr; for (auto *candidate : window->findChildren<QTimer *>(QString(), Qt::FindDirectChildrenOnly)) if (candidate->interval() == 5000) timer = candidate;
    check("existing periodic recovery timer is ready", timer && window->property("researchRecoveryReady").toBool()); if (!timer) { window->setModified(false); window->close(); app.exit(1); return; } timer->stop();
    auto pending = [&] {
      const QString id = QString::fromLatin1(QCryptographicHash::hash(Tw::Utils::canonicalSourcePath(window->textDoc()->absoluteFilePath()).toUtf8(), QCryptographicHash::Sha256).toHex());
      return app.applicationDirPath() + "/settings/research-history/" + id + ".pending.json";
    };
    auto trigger = [&] { return QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection); };
    window->editor()->insertPlainText("first queued "); window->setProperty("researchHistoryTime", qint64(0));
    {
      PoolHold hold; check("controlled worker barrier is active", hold.valid);
      QElapsedTimer elapsed; elapsed.start(); const bool invoked = trigger(); measurements.insert("periodicCallbackMs", double(elapsed.nsecsElapsed()) / 1e6);
      check("periodic snapshot capture stays below 100ms with its worker blocked", invoked && elapsed.elapsed() < 100);
      const QString oldPending = pending(); const QString savedText = window->editor()->text();
      check("ordinary save succeeds while an older recovery job waits", window->save());
      check("owned queued workers finish after save", hold.finish()); settle(80);
      check("an older job cannot recreate pending recovery after save", !QFileInfo::exists(oldPending) && !window->isModified());
      QFile disk(path); check("saved source retains the exact newer text", disk.open(QIODevice::ReadOnly) && QString::fromUtf8(disk.readAll()) == savedText);
    }
    window->editor()->insertPlainText("old queued ");
    {
      PoolHold hold; check("newer-checkpoint barrier is active", hold.valid); trigger(); window->editor()->insertPlainText("newer checkpoint ");
      const QString latest = window->editor()->text(); Research::checkpoint(window);
      check("newer synchronous checkpoint writes exact text", read(pending()).value("text").toString() == latest);
      check("owned queued workers finish after checkpoint", hold.finish()); settle(80);
      check("older job cannot overwrite a newer synchronous checkpoint", read(pending()).value("text").toString() == latest);
    }
    {
      PoolHold hold; check("save-as barrier is active", hold.valid); window->editor()->insertPlainText("save-as "); trigger(); const QString oldPending = pending();
      const QString newPath = root + "/另存 新路径.tex"; QTimer choose; choose.setInterval(10);
      QObject::connect(&choose, &QTimer::timeout, &app, [&] { for (auto *widget : QApplication::topLevelWidgets()) if (auto *dialog = qobject_cast<QFileDialog *>(widget); dialog && dialog->isVisible()) { dialog->selectFile(newPath); QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection); } }); choose.start();
      const bool saved = window->saveAs(); choose.stop(); const QString newPending = pending();
      check("save-as changes the independent document path", saved && Tw::Utils::canonicalSourcePath(newPath) == window->textDoc()->absoluteFilePath());
      check("owned queued workers finish after save-as", hold.finish()); settle(80);
      check("save-as prevents late recovery at either old or new identity", !QFileInfo::exists(oldPending) && !QFileInfo::exists(newPending));
    }
    window->editor()->insertPlainText("retry after failure "); const QString broken = pending(); QFile::remove(broken);
    check("private failing destination is an empty directory", QDir().mkpath(broken)); trigger();
    check("failing recovery worker completes", QThreadPool::globalInstance()->waitForDone(5000)); settle(80);
    check("failed recovery preserves the modified draft and reports the real failure", window->isModified() && window->statusBar()->currentMessage().contains(QStringLiteral("恢复副本保存失败")));
    check("private failing directory is removed without recursion", QDir().rmdir(broken)); trigger();
    check("retry worker completes", QThreadPool::globalInstance()->waitForDone(5000)); settle(80);
    check("a later recovery retry writes the exact draft", read(pending()).value("text").toString() == window->editor()->text());
    {
      PoolHold hold; check("discard barrier is active", hold.valid); window->editor()->insertPlainText("discarded queued "); trigger(); const QString oldPending = pending();
      Research::checkpoint(window, true); window->setModified(false); QPointer<TeXDocumentWindow> guard(window); window->close(); settle(60);
      check("intentional close releases its window", guard.isNull()); check("owned queued workers finish after close", hold.finish()); settle(80);
      check("closed/discarded document cannot acquire a late crash prompt", !QFileInfo::exists(oldPending));
    }
    app.exit(failures ? 1 : 0);
  }); return app.exec();
}
