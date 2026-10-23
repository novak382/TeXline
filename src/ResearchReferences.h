// On-demand, read-only reference previews. GPL-2.0-or-later.
#pragma once
#include <QHash>
#include <QImage>
#include <QString>
#include <QVector>
#include <QVariantMap>
#include <functional>
class QMenu;
class TeXDocumentWindow;
class PDFDocumentWindow;
namespace QtPDF { class PDFDocumentView; }
namespace Research {
inline constexpr int ReferenceHoverDelay = 1600;
struct ReferenceToken { QString key; int start{-1}, end{-1}; };
struct ReferenceBlock {
  QString file, kind, text, heading;
  int start{0}, end{0}, firstLine{1}, lastLine{1};
  bool equation{false};
};
struct ReferenceDefinition { QString key, number, anchor; ReferenceBlock block; int line{1}; };
struct ReferenceUse { QString key, file, text, heading; int line{1}; };
struct ReferenceIndex {
  QHash<QString,QString> sources; // Retained only for explicit contextual reading.
  QVector<ReferenceDefinition> definitions;
  QVector<ReferenceBlock> blocks;
  QVector<ReferenceUse> uses;
  bool truncated{false};
};
// Pure parsing entry points also used by the native integration fixture.
ReferenceToken referenceAt(const QString &text, int position);
ReferenceIndex referenceIndex(const QString &root, const QHash<QString, QString> &buffers = {},bool retainSources=false,const std::function<bool()> &cancelled={});
void installReferenceSource(TeXDocumentWindow *window, QMenu *more);
void openReferenceContext(TeXDocumentWindow *window,int offset=-1);
QVariantMap referenceReadingState(TeXDocumentWindow *window);
void restoreReferenceReadingState(TeXDocumentWindow *window,const QVariantMap &state,bool passive=false);
void pinSourceReference(TeXDocumentWindow *window, const QString &key = {});
void pinSourceObject(TeXDocumentWindow *window,const QString &file,int offset);
void installReferencePreview(PDFDocumentWindow *window);
void installReferenceSlidePreview(TeXDocumentWindow *window, QtPDF::PDFDocumentView *view);
}
