// Certified local-cache publication and reversible artifact cleanup. GPL-2.0-or-later.
#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <functional>
class TeXDocumentWindow;
namespace Research {
struct BuildJob;
QJsonObject generatedBuildArtifacts(const BuildJob &job);
bool publishGeneratedPreview(const QString &root, QString *error, const std::function<bool()> &cancelled = {});
QJsonArray legacyArtifactPlan(const QString &root);
bool cleanLegacyArtifacts(const QString &root, const QJsonArray &plan, QString *backup, QString *error, const std::function<bool()> &cancelled = {});
void installBuildOutput(TeXDocumentWindow *window);
void buildOutputStarted(TeXDocumentWindow *window, const BuildJob &job);
void buildOutputFinished(TeXDocumentWindow *window, const BuildJob &job, bool failed, int promoted);
void cancelBuildOutput(TeXDocumentWindow *window);
void refreshBuildOutputDestination(TeXDocumentWindow *window, const QString &directory, const QString &root);
}
