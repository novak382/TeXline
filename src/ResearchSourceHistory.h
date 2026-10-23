// Couple verified native source edits without replacing document undo stacks.
#pragma once
#include <QByteArray>
#include <QPointer>
#include <QString>
#include <QVector>
class TeXDocumentWindow;
class QTextEdit;
namespace Research {
struct SourceHistoryFile {QPointer<TeXDocumentWindow> window;QString file;QByteArray before,after;int undoSteps{0},redoSteps{0};};
SourceHistoryFile sourceHistoryBefore(TeXDocumentWindow *window);
void registerSourceHistory(QVector<SourceHistoryFile> files);
// True means a verified group was applied, or a conflicting group was blocked.
bool sourceHistory(QTextEdit *editor,bool redo);
}
