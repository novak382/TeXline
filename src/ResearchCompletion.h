// Local completion data and bounded snippet grammar. GPL-2.0-or-later.
#pragma once
#include <QByteArray>
#include <QString>
#include <QVector>
class QWidget;
class QDialog;
class QMainWindow;
class QKeySequence;
class QAction;
namespace Research {
struct CompletionArgument { bool optional{false}; QString value; };
struct MacroCompletion {
  QString name, file, definition;
  QVector<CompletionArgument> arguments;
  QByteArray sourceHash;
  int line{1};
  bool environment{false}, signatureKnown{true};
};
// Input is already masked by maskProjectText; offsets/newlines are preserved.
QVector<MacroCompletion> completionDeclarations(const QString &masked, const QString &file, const QByteArray &hash);
struct SnippetStop { int number{0}, start{0}, length{0}; };
struct CompletionSnippet {
  QString text, error;
  QVector<SnippetStop> stops;
  int finalPosition{-1};
  bool usesSelection{false};
  bool valid() const { return error.isEmpty(); }
};
// Numeric stops may repeat; they share a value and one navigation group.
// TM_SELECTED_TEXT is inserted literally, including inside a field default.
// General nested fields, transforms and unknown variables are not evaluated.
CompletionSnippet parseCompletionSnippet(const QString &body,const QString &selectedText = {});
struct UserCompletion { QString prefix, description, body, scope; int context{0}, trigger{1}; };
QVector<UserCompletion> userCompletions();
QString validateUserCompletions(const QVector<UserCompletion> &values);
QDialog *configureLocalCompletion(QWidget *parent);
bool completionShortcutAvailable(QMainWindow *window,const QKeySequence &key,const QAction *ignored = nullptr);
}
