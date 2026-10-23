#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "TeXDocumentWindow.h"
#include "CompletingEdit.h"
#include "Settings.h"
#include <QAbstractTextDocumentLayout>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTextBlock>
#include <QThread>
#include <QTimer>

static void settle(int ms = 0) { QElapsedTimer clock; clock.start(); do { QCoreApplication::processEvents(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); if (ms) QThread::msleep(2); } while (clock.elapsed() < ms); }
int main(int argc, char **argv) {
  const QString report = QString::fromLocal8Bit(argv[1]), input = QString::fromLocal8Bit(argv[2]); argc = 1; TWApp app(argc, argv); app.setQuitOnLastWindowClosed(false);
  QTimer::singleShot(100, &app, [&] {
    Tw::Settings settings; settings.setValue("openPDFwithTeX", false); settings.setValue("researchFocusMode", false);
    QJsonArray checks, runs; int failures = 0;
    auto save = [&] { QSaveFile out(report); if (out.open(QIODevice::WriteOnly)) { out.write(QJsonDocument(QJsonObject{{"checks", checks}, {"runs", runs}, {"failures", failures}, {"physicalInputVerified", false}}).toJson()); out.commit(); } };
    auto check = [&](const QString &name, bool ok) { failures += !ok; checks.append(QJsonObject{{"name", name}, {"passed", ok}}); save(); };
    const QString header = "\\documentclass{article}\n\\begin{document}\n", tail = "\\end{document}\n";
    QString unique = header; for (int i = 0; i < 100000; ++i) unique += QString("Paragraph %1 mathematical source 中文 $x_i$ alpha beta gamma.\n").arg(i, 6, 10, QChar('0')); unique += tail;
    QFile paper(input); check("actual extended project input is an independent readable copy", paper.open(QIODevice::ReadOnly));
    QString actual = QString::fromUtf8(paper.readAll()); actual.replace("\r\n", "\n"); actual.replace(QChar('\r'), QChar('\n')); check("actual project contains substantial source", actual.size() > 1000000);
    const QList<QPair<QString, QString>> documents{{"repeated", header + QString("Paragraph mathematical source 中文 $x_i$ alpha beta gamma.\n").repeated(100000) + tail}, {"unique", unique}, {"actual-extended-project", actual}};
    for (const auto &entry : documents) {
      const QString path = QFileInfo(report).absolutePath() + "/" + entry.first + "/main.tex"; QDir().mkpath(QFileInfo(path).absolutePath()); QSaveFile file(path);
      check(entry.first + " source is independently written", file.open(QIODevice::WriteOnly) && file.write(entry.second.toUtf8()) == entry.second.toUtf8().size() && file.commit());
      QElapsedTimer load; load.start(); auto *window = new TeXDocumentWindow(path); auto *editor = window->editor(); window->resize(850, 700); window->show(); settle(600); const double loadMs = double(load.nsecsElapsed()) / 1e6;
      QElapsedTimer first; first.start(); auto cursor = editor->textCursor(); cursor.movePosition(QTextCursor::End); editor->setTextCursor(cursor); settle();
      editor->document()->documentLayout()->documentSize(); const double firstMs = double(first.nsecsElapsed()) / 1e6;
      bool ordered = true; qreal previous = -1;
      for (auto block = editor->document()->firstBlock(); block.isValid(); block = block.next()) { const QRectF rect = editor->document()->documentLayout()->blockBoundingRect(block); ordered &= rect.height() > 0 && rect.top() >= previous; previous = rect.top(); }
      check(entry.first + " first pass lays out every block with ordered positive geometry", ordered);
      check(entry.first + " end cursor is actually visible", editor->viewport()->rect().intersects(editor->cursorRect()));
      check(entry.first + " layout preserves source and modified state", editor->text() == entry.second && !window->isModified());
      runs.append(QJsonObject{{"case", entry.first}, {"chars", entry.second.size()}, {"lines", editor->document()->blockCount()}, {"sourceSha256", QString::fromLatin1(QCryptographicHash::hash(entry.second.toUtf8(), QCryptographicHash::Sha256).toHex())}, {"loadAnd600MsIdleMs", loadMs}, {"firstCompleteLayoutMs", firstMs}, {"documentHeight", editor->document()->size().height()}, {"font", editor->font().toString()}, {"dpr", editor->devicePixelRatioF()}, {"lineHeight", editor->document()->firstBlock().blockFormat().lineHeight()}}); save();
      window->setModified(false); window->close(); settle(60);
    }
    app.exit(failures ? 1 : 0);
  }); return app.exec();
}
