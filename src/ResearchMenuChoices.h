// Optional menu entries share one preference vocabulary. GPL-2.0-or-later.
#pragma once
#include <QList>
#include <QString>
class QAction;
class QMenu;
class QTabWidget;
class QVBoxLayout;
namespace Research {
enum class MenuSurface { Context, Edit, Insert, Slides };
bool menuEntryEnabled(MenuSurface surface, const QString &action);
void filterMenuEntries(QMenu *menu, MenuSurface surface);
void installMenuChoices(QMenu *menu, MenuSurface surface);
void appendMenuChoiceSettings(QTabWidget *tabs);
void appendMenuChoiceSettings(QVBoxLayout *layout);
}
