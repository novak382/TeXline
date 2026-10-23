// TeXworks Research workflow extensions. GPL-2.0-or-later.
#pragma once
#include <QString>
#include <QStringList>
#include <QList>
#include <QHash>
#include <functional>

class TeXDocumentWindow;
class QWidget;

namespace Research {
QString compiledTexText(QString text);
// Positive preamble proof only; an empty result requires complete parsing.
// Remembered roots retain precedence over a plain document class.
QString mainRootFromPreamble(const QString &file, const QString &preamble);
QString resolveRoot(const QString &file, const QString &text);
// Immutable snapshots only: completion root discovery must not inspect GUI documents.
QString resolveCompletionRoot(const QString &file, const QString &text,
  const QHash<QString, QString> &buffers, const std::function<bool()> &cancelled = {});
QStringList rootCandidates(const QString &file, const QString &text);
QStringList projectFiles(const QString &root);
// Immutable GUI snapshots or disk reads only; safe on a preparation worker.
QStringList projectFiles(const QString &root, const QHash<QString,QString> &sources, const std::function<bool()> &cancelled = {});
void rememberRoot(const QString &file, const QString &root);
QString suggestedEngine(const QString &text);
QString canonicalEngineName(const QString &name);
QString preferredEngine(const QString &file, const QString &text);
void rememberEngine(const QString &file, const QString &engine);
void forgetEngine(const QString &file);
void installWorkflow(TeXDocumentWindow *window);
bool hasPendingRecovery(const QString &file);
void startRecovery(TeXDocumentWindow *window);
void checkpoint(TeXDocumentWindow *window, bool closing = false);
void saved(TeXDocumentWindow *window, const QString &previousPath);
void saveFailure(TeXDocumentWindow *window, const QString &reason);
void beforeSave(TeXDocumentWindow *window, const QString &path);
bool recordRevision(TeXDocumentWindow *window, const QString &text);
QString writingHistoryDirectory();
// Keep the last review's full alternatives outside the rolling thirty entries.
bool retainReviewVersions(const QString &path, const QString &before, const QString &external, bool keepExisting = false);
struct Issue {
  QString file; int line; QString message; bool error;
  // TeX's reported line remains authoritative. A separate, explicitly tentative
  // source hint can point to an earlier unmatched math delimiter.
  int suspectLine = 0, suspectColumn = 0;
};
QList<Issue> compileIssues(const QString &root, const QString &output, bool failed, const QString &logFile = QString());
QString issueExplanation(const Issue &issue, bool english);
int selfTest(const QString &reportPath);
}
