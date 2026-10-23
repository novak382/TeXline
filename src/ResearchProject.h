// Bounded, on-demand research navigation and reference assistance. GPL-2.0-or-later.
#pragma once
#include <QHash>
#include <QSharedPointer>
#include <QString>
#include <QVector>
#include <QVariantMap>
#include <functional>
#include "ResearchCompletion.h"
class TeXDocumentWindow;
class QMainWindow;
class PDFDocumentWindow;
class QTextCursor;
class QObject;
namespace Research {
struct ProjectEntry {
  QString file, key, title, detail, kind, number, search;
  int line{1}, offset{0};
};
struct ProjectIndex {
  QVector<MacroCompletion> macros;
  QVector<ProjectEntry> entries;
  // Literal uses and read-only source diagnostics; never compiler verdicts.
  QVector<ProjectEntry> uses, issues;
  QHash<QString, QVector<QPair<int, int>>> literalRanges;
  QHash<QString, QByteArray> parsedHashes;
  QHash<QString, QByteArray> sourceBufferHashes;
  QHash<QString, QByteArray> bibliographyBufferHashes;
  QStringList files, dependencies;
  QStringList bibliographies;
  qsizetype indexedBytes{0};
  bool cancelled{false};
  bool dynamicLabels{false}, issuesTruncated{false}, citationCoveragePartial{false};
  bool truncated{false}, numbersCurrent{false}, citationsReady{false};
  qint64 buildMilliseconds{0};
};
struct CitationContext {
  bool valid{false}, citation{false};
  int start{0}, end{0};
  QString query;
};
// Pure worker entry points, also exercised by native integration tests.
QSharedPointer<const ProjectIndex> buildProjectIndex(const QString &root, const QHash<QString, QString> &buffers = {}, bool citations = true, const std::function<bool()> &cancelled = {});
CitationContext projectContext(const QString &block, int cursor);
CitationContext projectContext(const QTextCursor &cursor);
QVector<int> searchProject(const ProjectIndex &index, const QString &query, int limit = 120, int completion = 0);
void rememberReadingPlace(TeXDocumentWindow *window);
void rememberReadingPlace(PDFDocumentWindow *window);
QVariantMap captureWritingPlace(TeXDocumentWindow *window);
bool restoreWritingPlace(const QVariantMap &state,QString *error);
// Same-document, passive restoration: never opens/activates another source
// window or changes the application's focus-mode preference.
bool restoreRecentWritingPlace(TeXDocumentWindow *window,const QVariantMap &state,QString *error);
void installReadingNavigation(QMainWindow *window);
void installProjectWriting(TeXDocumentWindow *window);
void cancelProjectWriting(QMainWindow *window);
bool showProjectCompletion(TeXDocumentWindow *window);
quint64 completionProjectEpoch(); // GUI thread only
QString completionProjectRoot(TeXDocumentWindow *window);
// Shared expiring worker/cache; the receiver and cancellation predicate bound
// asynchronous completion to the requesting document/session.
void requestCompletionProject(TeXDocumentWindow *window, QObject *receiver,
  std::function<void(QSharedPointer<const ProjectIndex>)> done,
  const std::function<bool()> &cancelled = {});
void openProjectNavigation(TeXDocumentWindow *window);
QString maskProjectText(QString text);
}
