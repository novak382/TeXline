// Optional Beamer source navigation. GPL-2.0-or-later.
#pragma once
#include <QHash>
#include <QSet>
#include <QString>
#include <QVariantMap>
#include <QStringList>
#include <QVector>
#include <functional>
class TeXDocumentWindow;
namespace Research {
struct SlideFrame {
  QString file, title, label, kind;
  int start{0}, end{0}, line{1}, endLine{1};
  int firstPage{0}, lastPage{0}; // Physical PDF pages, one based; zero = unverified.
  bool editable{false}, labelled{false}, breaks{false};
  QString section;
  bool backup{false};
  bool titlePage{false};
};
struct SlideDeck {
  struct Landmark { QString title,file; int line{1}; };
  QVector<SlideFrame> frames;
  QVector<Landmark> document;
  QHash<QString,int> preambleEnd;
  QHash<QString, QString> sources;
  QStringList dependencies;
  bool beamer{false}, uncertain{false}, cancelled{false}, mapped{false}, previewCurrent{false};
  QString reason, pdf, revision;
};
// Filesystem/snapshot-only worker APIs. Never parse on a key-event path.
SlideDeck parseSlideDeck(const QString &root, const QHash<QString, QString> &buffers = {},
                         const std::function<bool()> &cancelled = {});
bool mapSlidePages(SlideDeck &deck, const QString &navigation, int pages,
                   const QVector<QSet<int>> &sourcePages);
enum class SlideEdit { Insert, Duplicate, Up, Down, Delete };
// A complete replacement of only the relevant contiguous range, in one undo block.
bool editSlide(TeXDocumentWindow *window, const SlideDeck &deck, int index,
               SlideEdit operation, QString *reason = nullptr);
bool editSlideSelection(TeXDocumentWindow *window, const SlideDeck &deck, QVector<int> indices,
                        SlideEdit operation, int before = -1, QString *reason = nullptr, bool apply = true);
bool commitSlideNotes(TeXDocumentWindow *window);
bool hasPendingSlideNotes(TeXDocumentWindow *window);
void setSlideFocusMode(TeXDocumentWindow *window, bool focused);
QVariantMap slideReadingState(TeXDocumentWindow *window);
void restoreSlideReadingState(TeXDocumentWindow *window,const QVariantMap &state);
void installSlideMode(TeXDocumentWindow *window);
void detectSlideMode(TeXDocumentWindow *window);
bool toggleSlideDirectory(TeXDocumentWindow *window);
void slideBuildStarted(TeXDocumentWindow *window);
void slideBuildFinished(TeXDocumentWindow *window, bool accepted, bool failed);
void slidePreviewAvailable(TeXDocumentWindow *window);
}
