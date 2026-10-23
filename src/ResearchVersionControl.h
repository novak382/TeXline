// On-demand Git workflow. GPL-2.0-or-later.
#pragma once
#include <QString>
#include <QVector>
#include <QSet>
class TeXDocumentWindow;
namespace Research {
struct GitFile {QString path,previous;char index{' '},worktree{' '};bool conflict{false};};
QVector<GitFile> parseGitStatus(const QByteArray &bytes,QString *error);
QString gitPatchAtLine(const QString &diff,int line);
QString gitPatchForSelectedLines(const QString &diff,const QSet<int> &lines,bool reverse=false);
QString gitFailureMessage(const QByteArray &error);
void openVersionControl(TeXDocumentWindow *window);
void installProjectTools(TeXDocumentWindow *window);
void openEnvironmentCheck(TeXDocumentWindow *window,const QString &missingTool={});
}
