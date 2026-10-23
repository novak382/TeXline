// TeXworks Research appearance extensions. GPL-2.0-or-later.
#pragma once
#include <QDialog>
#include <QListView>
#include <QFrame>
class QApplication;
class QAction;
class QMainWindow;
class QMenu;
class QToolBar;
class QToolButton;
class QPushButton;
class QLineEdit;
class QTextBrowser;
namespace Research {
// Logical pixels: Qt applies the display's DPI scale to both windows equally.
namespace ControlMetrics {
inline constexpr int Height = 32;
inline constexpr int Icon = 18;
inline constexpr int Gap = 8;
inline constexpr int Margin = 12;
inline constexpr int TextWidth = 56;
inline constexpr int MenuWidth = 72;
inline constexpr int CompileWidth = 112;
inline constexpr int FontPixels = 15;
}
enum class ButtonRole { Icon, Text, Menu, Compile, Status };
QFont smoothTextFont(QFont font);
QFont interfaceFont();
bool motionEnabled();
void installAppearance(QApplication *app);
// Call after choosing popup flags/style and before its native window is created.
void preparePopupSurface(QWidget *popup);
class PopupFrame final : public QFrame {
public:
  using QFrame::QFrame;
protected:
  void paintEvent(QPaintEvent *event) override;
};
class PopupListView final : public QListView {
public:
  using QListView::QListView;
protected:
  bool event(QEvent *event) override;
};
void configureToolbar(QToolBar *bar);
void configureButton(QToolButton *button, ButtonRole role);
void refreshCompileButton(QToolButton *button);
void refreshFocusMessage(QMainWindow *window);
enum class NoticePriority { Routine, Attention };
void showTransientStatusMessage(QMainWindow *window, const QString &text, int milliseconds = 4000, NoticePriority priority = NoticePriority::Routine);
void configureButton(QPushButton *button, int minimumWidth = 0);
QString ui(const QString &source);
QString ui(const QString &source, bool english);
bool englishUi();
bool traditionalUi();
QString uiLocale();
QString uiSearchText(const QString &text);
QString scriptUi(const QString &title, const QString &text);
QString supportedUiLocale(const QString &locale);
void setUiLocale(const QString &locale);
void setControlStatus(QToolButton *button, const QString &hint, bool attention = false);
void setBuildIssueCounts(QMainWindow *window, int errors, int warnings);
void retranslateWorkspace(QWidget *root);
void setLocalizedHtml(QTextBrowser *browser, const QString &chinese, const QString &english);
void configureField(QLineEdit *field);
QToolButton *workspaceButton(QToolBar *bar, const QString &label, const QString &icon = QString(), QAction *action = nullptr);
void installWorkspace(QMainWindow *window, QToolBar *bar, QMenu *more);
void restoreWorkspace(QMainWindow *window);
void setFocusMode(bool focused);
class MaterialDialog : public QDialog {
public:
  explicit MaterialDialog(QWidget *parent = nullptr) : QDialog(parent) {setFont(interfaceFont());}
protected:
  void paintEvent(QPaintEvent *) override;
  void showEvent(QShowEvent *) override;
};
}
