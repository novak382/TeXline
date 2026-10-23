// Opt-in shorthand and example-based snippet authoring. GPL-2.0-or-later.
#pragma once
#include "ResearchCompletion.h"
#include <functional>
class QTextEdit;
class TeXDocumentWindow;
class QVBoxLayout;
namespace Research {
struct ShorthandMatch { int start{-1},end{-1}; QString prefix,body; bool valid()const{return start>=0;} };
QVector<UserCompletion> effectiveCompletions(QMainWindow *window);
QVector<UserCompletion> completionsAtCursor(QTextEdit *editor,QMainWindow *window);
ShorthandMatch matchShorthand(QTextEdit *editor,QMainWindow *window,bool automatic,const QString &pending={});
QWidget *completionSettings(QWidget *parent,QMainWindow *window,const QString &selection={},bool create=false,bool compact=false);
bool saveCompletionSettings(QWidget *page);
bool validateCompletionSettings(QWidget *page);
void installSnippetAuthoring(TeXDocumentWindow *window);
}
