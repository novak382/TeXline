// Read the exact recovery format produced by the frozen x86 application code.
#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "ResearchWorkflow.cpp"
#include "Settings.h"

int main(int argc, char **argv) {
  if (argc < 2) return 2;
  const QString report = QString::fromLocal8Bit(argv[1]);
  argc = 1;
  TWApp app(argc, argv);
  app.setQuitOnLastWindowClosed(false);
  const QString base = QCoreApplication::applicationDirPath() + "/migration-compat";
  QDir().mkpath(base);
  Research::testStorageOverride = base + "/history";
  Tw::Settings().setValue("openPDFwithTeX", false);
  const QString text = QString::fromUtf8(
      "研究草稿 — αβ\n\\begin{equation}\n A=\\Omega+W.\\label{eq:model}\n\\end{equation}\n");
  const QString source = base + "/main.tex", saved = base + "/x86.snapshot.json";
  QJsonArray checks;
  int failures = 0;
  auto check = [&](const QString &name, bool pass) {
    checks.append(QJsonObject{{"name", name}, {"passed", pass}});
    failures += !pass;
  };
  if (sizeof(void *) == 4) {
    QSaveFile f(source);
    const QByteArray disk("Original disk source.\n");
    bool written = f.open(QIODevice::WriteOnly) && f.write(disk) == disk.size() && f.commit();
    check("x86 independent source is written atomically", written);
    auto *window = new TeXDocumentWindow(source);
    window->editor()->setPlainText(text);
    QTextCursor cursor(window->textDoc());
    cursor.setPosition(13);
    window->editor()->setTextCursor(cursor);
    auto data = Research::snapshot(window);
    data.insert("writerPointerBits", 32);
    check("x86 production recovery format retains mathematical Unicode source",
          data.value("text").toString() == text && data.value("cursor").toInt() == 13);
    check("x86 production writer persists the compatibility snapshot",
          Research::writeSnapshot(saved, data));
    window->setModified(false);
    delete window;
  } else {
    const auto data = Research::readSnapshot(saved);
    check("x64 production reader accepts a real x86 snapshot",
          data.value("writerPointerBits").toInt() == 32 && data.value("version").toInt() == 1 &&
          data.value("text").toString() == text && data.value("path").toString() == source);
    auto *window = new TeXDocumentWindow(source);
    const QString previous = window->editor()->toPlainText();
    Research::restoreText(window, data);
    check("x64 restoration preserves source and cursor exactly",
          window->editor()->toPlainText() == text && window->cursorPosition() == 13 && window->isModified());
    window->editor()->undo();
    check("undo returns to the complete pre-migration disk content",
          window->editor()->toPlainText() == previous);
    window->setModified(false);
    delete window;
  }
  QSaveFile result(report);
  if (!result.open(QIODevice::WriteOnly)) return 2;
  result.write(QJsonDocument(QJsonObject{{"checks", checks}, {"failures", failures},
       {"pointerBits", int(sizeof(void *) * 8)}}).toJson());
  if (!result.commit()) return 2;
  return failures ? 1 : 0;
}
