#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include <QAbstractTextDocumentLayout>
#include <QDir>
#include <QFileInfo>
#include <QGlyphRun>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QRawFont>
#include <QSaveFile>
#include <QScreen>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLayout>
#include <QTimer>

static bool sameLayout(const QTextDocument &a, const QTextDocument &b) {
  if (a.size() != b.size()) return false;
  auto x = a.begin(), y = b.begin();
  while (x.isValid() && y.isValid()) {
    auto *l = x.layout(), *r = y.layout();
    if (l->lineCount() != r->lineCount() || l->position() != r->position()) return false;
    for (int i = 0; i < l->lineCount(); ++i) {
      auto p = l->lineAt(i), q = r->lineAt(i);
      if (p.rect() != q.rect() || p.naturalTextWidth() != q.naturalTextWidth() || p.textStart() != q.textStart() || p.textLength() != q.textLength()) return false;
      for (int offset = p.textStart(); offset <= p.textStart() + p.textLength(); ++offset)
        if (p.cursorToX(offset) != q.cursorToX(offset)) return false;
    }
    const auto p = l->glyphRuns(), q = r->glyphRuns();
    if (p.size() != q.size()) return false;
    for (int i = 0; i < p.size(); ++i)
      if (p[i].glyphIndexes() != q[i].glyphIndexes() || p[i].positions() != q[i].positions() || p[i].rawFont().familyName() != q[i].rawFont().familyName() || p[i].flags() != q[i].flags()) return false;
    x = x.next(); y = y.next();
  }
  return !x.isValid() && !y.isValid();
}
int main(int argc, char **argv) {
  const QString report = QString::fromLocal8Bit(argv[1]); argc = 1; TWApp app(argc, argv); app.setQuitOnLastWindowClosed(false);
  QTimer::singleShot(100, &app, [&] {
    QJsonArray checks; int failures = 0;
    auto check = [&](const QString &name, bool ok) {
      failures += !ok; checks.append(QJsonObject{{"name", name}, {"passed", ok}});
      QSaveFile out(report); if (out.open(QIODevice::WriteOnly)) { out.write(QJsonDocument(QJsonObject{{"checks", checks}, {"failures", failures}, {"physicalInputVerified", false}}).toJson()); out.commit(); }
    };
    QString text = QString::fromUtf8("AV To fi ffi != -> => <= \\alpha{x_i}\n中文 日本語 Ω α é ä عربى עברית हिन्दी ไทย 👩‍🔬 ❤️\n");
    for (int offset=0; offset<20; ++offset) {
      text += QString(offset,QChar(0x03b1)) + QString::fromUtf8("中中中中中中中中中中AV fi != \\alpha{x_i}中中中中中中中中中中") + QString::number(offset) + QChar('\n');
      text += QString(offset,QChar(0x03b2)) + QString::fromUtf8("😀😀😀😀😀AV fi != \\alpha{x_i}😀😀😀😀😀") + QString::number(offset) + QChar('\n');
    }
    for (int i = 32; i <= 126; ++i) for (int j = 32; j <= 126; ++j) text += (QString(QChar(i)) + QChar(j) + QChar('\n')).repeated(3);
    text += QString("long ASCII $x_{ij}$ \\section{Words} ").repeated(200);
    const qreal dpr = app.primaryScreen()->devicePixelRatio();
    for (const QString family : {QString("JetBrains Mono NL"), QString("Consolas")}) {
      for (int style = 0; style < 4; ++style) for (int size : {11, 15, 21}) {
        QFont font(family, size); font.setBold(style & 1); font.setItalic(style & 2);
        QImage oldImage(QSize(800 * dpr, 240 * dpr), QImage::Format_ARGB32_Premultiplied), newImage = oldImage;
        oldImage.setDevicePixelRatio(dpr); newImage.setDevicePixelRatio(dpr); oldImage.fill(Qt::white); newImage.fill(Qt::white);
        QTextDocument oldDocument, newDocument;
        auto prepare = [&](QTextDocument &doc, QImage &image, bool fast) {
          doc.setProperty("_texline_lazy_edits", fast); doc.documentLayout()->setPaintDevice(&image); doc.setDefaultFont(font);
          doc.setPlainText(text); doc.setTextWidth(550); QTextCursor cursor(&doc); cursor.select(QTextCursor::Document);
          QTextBlockFormat spacing; spacing.setLineHeight(128, QTextBlockFormat::ProportionalHeight); cursor.setBlockFormat(spacing); doc.size();
        };
        prepare(oldDocument, oldImage, false); prepare(newDocument, newImage, true);
        const QString label = QString("%1 style %2 size %3 DPR %4").arg(family).arg(style).arg(size).arg(dpr);
        check(label + " preserves glyphs, wrapping and every cursor position", sameLayout(oldDocument, newDocument));
        { QPainter painter(&oldImage); oldDocument.drawContents(&painter, QRectF(0, 0, 800, 240)); }
        { QPainter painter(&newImage); newDocument.drawContents(&painter, QRectF(0, 0, 800, 240)); }
        check(label + " preserves visible mixed-script pixels", oldImage == newImage);
      }
    }
    app.exit(failures ? 1 : 0);
  }); return app.exec();
}
