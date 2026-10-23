// TeXworks Research collaboration extensions. GPL-2.0-or-later.
#pragma once
#include <QString>
#include <QVector>
namespace Research {
struct TextEdit { int start{0}, end{0}; QString text; };
struct MergeConflict { int start{0}, end{0}; QString base, local, remote; };
struct MergeResult { QString text; QVector<MergeConflict> conflicts; };
QVector<TextEdit> textEdits(const QString &before, const QString &after);
MergeResult mergeText(const QString &base, const QString &local, const QString &remote);
QString changeCategory(const QString &text, int position);
}
