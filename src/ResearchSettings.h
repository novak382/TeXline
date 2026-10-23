// Shared settings vocabulary, scope and presentation. GPL-2.0-or-later.
#pragma once
#include <QVariant>
#include <QString>
class QAbstractItemView;
class QComboBox;
class QDialog;
class QDialogButtonBox;
class QTabWidget;
class QVBoxLayout;
class QWidget;
namespace Research {
QString words(const char *chinese,const char *english);
void configureSettingsSurface(QWidget *widget);
void configureSettingsList(QAbstractItemView *list);
void localizeEditorModeChoices(QComboBox *indent,QComboBox *quotes);
QVariant defaultPreference(const char *key,const QVariant &fallback = {});
struct SettingDefinition {
  const char *key; const char *chinese; const char *english; QVariant initial;
  const char *scope; const char *keywords;
};
const QList<SettingDefinition> &settingDefinitions();
const SettingDefinition &settingDefinition(const char *key);
QVariant settingValue(const char *key);
QWidget *settingControl(const char *key,QWidget *parent);
void saveSettingControls(QWidget *root);
void resetSettingControls(QWidget *page);
QVBoxLayout *settingsPage(QTabWidget *tabs,const char *id,const char *chinese,const char *english);
void settingsHeading(QVBoxLayout *layout,const QString &title);
QVBoxLayout *settingsDetails(QVBoxLayout *layout,const char *id,const QString &title);
QWidget *revealSetting(QWidget *target);
void settingsRow(QVBoxLayout *layout,const QString &title,QWidget *control,const QString &keywords={});
void installSettingsShell(QDialog *dialog,QTabWidget *tabs,QDialogButtonBox *buttons);
}
