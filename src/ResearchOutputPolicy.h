// Local sync-directory discovery and per-project output choices. GPL-2.0-or-later.
#pragma once
#include <QStringList>
#include <functional>
class QObject;
class QMenu;
class TeXDocumentWindow;
namespace Research {
struct OutputPolicy {
  QString root, key, directory, cacheDirectory;
  bool synchronized = false, cacheOnly = false;
};
// Filesystem-only helpers; call on a worker, never during initial editor paint.
QString normalizedOutputPath(const QString &path);
QStringList discoverSyncRoots(const QStringList &infoFiles, const QStringList &manual, const QString &clientRecordsFile = QString());
bool insideSyncRoot(const QString &normalizedFile, const QStringList &normalizedRoots);
void requestOutputPolicy(QObject *context, const QString &root, const std::function<void(OutputPolicy)> &ready);
void rememberOutputPolicy(const OutputPolicy &policy, bool cacheOnly);
void refreshBuildDestination(TeXDocumentWindow *window);
void addBuildDestinationMenu(TeXDocumentWindow *window, QMenu *menu);
void addBuildOutputMenu(TeXDocumentWindow *window, QMenu *menu);
// Private-fixture injection disables all real client/registry discovery.
void setSyncInfoFilesForTests(const QStringList &files);
void setSyncClientRecordsForTests(const QString &file);
}
