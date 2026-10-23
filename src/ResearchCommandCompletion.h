// Local, opt-in command completion. GPL-2.0-or-later.
#pragma once
class QMainWindow;
class QMenu;
namespace Research {
void installCommandCompletion(QMainWindow *window, QMenu *normal, QMenu *focus);
void setCommandCompletionMode(int mode);
void cancelCommandCompletion(QMainWindow *window);
}

class CompletingEdit;
namespace Research { void installCompletionEditor(QMainWindow *window, CompletingEdit *editor); }
