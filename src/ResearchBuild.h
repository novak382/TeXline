// TeXworks Research local build and PDF revision tracking. GPL-2.0-or-later.
#pragma once
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QHash>
#include <functional>
class Engine;
namespace Research {
struct BuildJob {
  QString root, outputDir, previewFile, logFile;
  QJsonObject inputs;
  QHash<QString,QString> sourceTexts;
  QStringList authoredInputs;
  bool managed{false}, cancelled{false};
  qint64 startedAt{0};
};
enum class PdfRevision { Current, Stale, Unknown, Unpaired };
BuildJob prepareBuild(const QString &root, const Engine &engine);
// Source snapshots are captured on the GUI; this overload never reads widgets.
BuildJob prepareBuild(const QString &root, const Engine &engine, const QHash<QString,QString> &sources, const std::function<bool()> &cancelled = {});
// 1: promoted; 0: unsuccessful/custom build; -1: inputs changed; -2: I/O failure.
int finishBuild(const BuildJob &job, bool success);
// The callback consumes only immutable paths/shared cancellation state; no widgets.
int finishBuildInBackground(const BuildJob &job, bool success, const std::function<bool(const QStringList &)> &inputsChanged);
// A rejected build may be retried, but never promoted, when current content
// still matches its snapshot. Timestamp changes alone do not prove an edit.
bool buildContentUnchanged(const BuildJob &job);
QString previewFile(const QString &root);
QString pdfSourceRoot(const QString &pdf);
bool managedPreview(const QString &pdf);
PdfRevision pdfRevision(const QString &pdf, const QString &root = QString());
bool pdfPairValid(const QString &pdf);
// Integrity of the compiled revision, independent of subsequent source edits.
bool pdfCompiledPairValid(const QString &pdf);
QHash<QString,QString> pdfCompiledSources(const QString &pdf);
QHash<QString,QString> pdfCompiledAux(const QString &pdf);
bool publishPreview(const QString &root, QString *error = nullptr);
// Immutable filesystem-only publication inputs, suitable for a worker.
QString buildCacheDirectory(const QString &root);
QJsonObject currentBuildMetadata(const QString &root);
// Local provenance stays in the application cache, never beside authored files.
QString pdfMetadataFile(const QString &pdf);
QString pdfMetadataReadFile(const QString &pdf);
QJsonObject pdfMetadata(const QString &pdf);
QStringList buildDependencies(const QString &root, bool includeRecorded = true);
QStringList buildDependencies(const QString &root, const QHash<QString,QString> &sources);
// Successful compiler records, shared by navigation and external-edit review.
QStringList recordedDependencies(const QString &root);
bool usesBibliographySource(const QStringList &dependencies);
void setBuildCacheForTests(const QString &directory);
}
