// Shared, bounded dependency lookup. GPL-2.0-or-later.
#pragma once
#include <QString>
#include <QStringList>
namespace Research {
void setDependencyBinaryPaths(const QStringList &paths);
QString resolveDependency(const QString &name, const QString &file,
                          const QString &root, const QString &suffix);
bool authoredDependency(const QString &path, const QString &root,
                        const QString &outputDirectory = QString());
bool textDependency(const QString &path);
}
