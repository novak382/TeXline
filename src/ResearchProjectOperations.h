// Recoverable file operations scoped to an explicitly opened project. GPL-2.0-or-later.
#pragma once
#include <QString>
#include <QVector>
#include <functional>
#include <QMap>
#include <QStringList>
class QDialog; class QTreeView; class QFileSystemModel; class QToolButton; class TeXDocumentWindow;
namespace Research {
struct ProjectFileChange {QString path;QByteArray before,after;bool existed{false},existsAfter{false};};
struct ProjectFilePlan {QString root,error;QVector<ProjectFileChange> files;QStringList warnings;QMap<QString,QString> moves;QStringList createDirectories,removeDirectories;};
enum class ProjectTransfer { Copy, Move, Remove };
ProjectFilePlan planProjectTransfer(const QString &root,const QStringList &sources,const QString &destination,ProjectTransfer operation,const std::function<bool()> &cancelled={},const QString &mainFile={});
ProjectFilePlan planProjectMove(const QString &root,const QString &from,const QString &to,const std::function<bool()> &cancelled={},const QString &mainFile={});
ProjectFilePlan planProjectReplace(const QString &root,const QString &query,const QString &replacement,const std::function<bool()> &cancelled={});
bool applyProjectPlan(const ProjectFilePlan &plan,const QString &journal,QString *error,bool reverse=false);
void installProjectFileOperations(TeXDocumentWindow *window,QDialog *dialog,QTreeView *tree,QFileSystemModel *model,QToolButton *operations,const QString &folder,const QString &root);
}
