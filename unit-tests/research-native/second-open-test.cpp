#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "TeXDocumentWindow.h"
#include "CompletingEdit.h"
#include "Settings.h"
#include "InterProcessCommunicator.h"
#include <QDir>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QSaveFile>
#include <QThread>
#include <QTimer>
#include <cstdio>

static void settle(int ms) {
  QElapsedTimer timer; timer.start();
  while (timer.elapsed() < ms) { QCoreApplication::processEvents(); QThread::msleep(3); }
}
static bool put(const QString &path, const QByteArray &bytes) {
  QSaveFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
int main(int argc, char **argv) {
  if (qEnvironmentVariableIsSet("TEXLINE_IPC_SENDER")) {
    std::fprintf(stderr,"sender before application\n");std::fflush(stderr);
    TWApp app(argc, argv);
    std::fprintf(stderr,"sender after application\n");std::fflush(stderr);
    return app.exec();
  }
  const QString report = QString::fromLocal8Bit(argv[1]); argc = 1;
  TWApp app(argc, argv); app.setQuitOnLastWindowClosed(false);
  QTimer::singleShot(100, &app, [&] {
    QDir().mkpath(QFileInfo(report).absolutePath());
    const QString folder = QFileInfo(report).absolutePath();
    const QString first = folder + "/first 中文.tex", second = folder + "/second 中文.tex";
    put(first, "first document\n"); put(second, "second document\n");
    Tw::Settings().setValue("openPDFwithTeX", false);
    app.openFile(first); settle(200);
    QJsonArray checks; int failures = 0;
    auto check = [&](const QString &name, bool passed) {
      checks.append(QJsonObject{{"name", name}, {"passed", passed}}); failures += !passed;
      put(report, QJsonDocument(QJsonObject{{"checks", checks}, {"failures", failures}}).toJson());
    };
    auto send = [&](const QString &path) {
      QProcess process;
      auto env = QProcessEnvironment::systemEnvironment(); env.insert("TEXLINE_IPC_SENDER", "1");
      process.setProcessEnvironment(env); process.start(QCoreApplication::applicationFilePath(), {path});
      QElapsedTimer timer; timer.start();
      while (process.state() != QProcess::NotRunning && timer.elapsed() < 30000) settle(10);
      const bool ended = process.state() == QProcess::NotRunning;
      if (!ended) { process.kill(); process.waitForFinished(2000); }
      put(folder+"/sender-"+QFileInfo(path).completeBaseName()+".txt",process.readAllStandardError());
      settle(300); check("sender exits after delivering " + QFileInfo(path).fileName(), ended && process.exitCode() == 0);
    };
    send(second);
    auto *window = TeXDocumentWindow::findDocument(second);
    check("second launch opens a second document in the first process", window && window->editor()->toPlainText() == "second document\n");
    if (window) { window->showMinimized(); settle(80); send(second); check("reopening restores a minimized document", !window->isMinimized()); }
    send(first);
    check("first document remains intact", TeXDocumentWindow::findDocument(first) && TeXDocumentWindow::findDocument(first)->editor()->toPlainText() == "first document\n");
    for (auto *doc : TeXDocumentWindow::documentList()) { doc->setModified(false); doc->close(); }
    app.exit(failures ? 1 : 0);
  }); return app.exec();
}
