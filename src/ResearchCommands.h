// Shared command identities, discovery and shortcut persistence. GPL-2.0-or-later.
#pragma once
#include <QList>
#include <QString>
class QAction;
class QMainWindow;
class QWidget;
namespace Research {
struct CommandDefinition {
  const char *id; const char *group; const char *keywords;
  const char *purposeZh{nullptr};const char *purposeEn{nullptr};const char *intents{nullptr};
};
const QList<CommandDefinition> &commandDefinitions();
QList<QAction *> workspaceCommands(QMainWindow *window);
QString commandSearchText(QAction *action);
QString commandPurpose(QAction *action);
int commandMatchScore(QAction *action,const QString &query);
void applyCommandShortcuts(QMainWindow *window);
void showCommandPalette(QMainWindow *window, QAction *invoker);
QWidget *shortcutSettings(QWidget *parent, QMainWindow *window);
bool saveShortcutSettings(QWidget *page);
void resetShortcutSettings(QWidget *page);
}
