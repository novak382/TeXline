#include "ResearchSourceHistory.h"
#include "ResearchAppearance.h"
#include "ResearchSettings.h"
#include "TeXDocumentWindow.h"
#include "CompletingEdit.h"
#include <QApplication>
#include <QCryptographicHash>
#include <QScopedValueRollback>
#include <QScrollBar>
#include <QSet>
#include <algorithm>

namespace Research {
namespace {
QByteArray fingerprint(TeXDocumentWindow *w){return QCryptographicHash::hash(w->textDoc()->editorText().toUtf8(),QCryptographicHash::Sha256);}
struct Group {QVector<SourceHistoryFile> files;bool undone{false},valid{true};};
class SourceHistory final:public QObject {
public:
 SourceHistory():QObject(qApp){}
 QVector<Group> groups;QSet<QObject*> documents;bool applying{false};
 void add(QVector<SourceHistoryFile> files){
  if(files.size()<2)return;
  for(auto &file:files){if(!file.window)return;file.after=fingerprint(file.window);file.undoSteps=file.window->textDoc()->availableUndoSteps();if(file.before==file.after)return;}
  for(const auto &file:files){auto *document=file.window->textDoc();if(documents.contains(document))continue;documents.insert(document);
   connect(document,&QTextDocument::undoCommandAdded,this,[this,document]{if(applying)return;for(auto &group:groups)if(group.undone)for(const auto &item:group.files)if(item.window&&item.window->textDoc()==document)group.valid=false;});
   connect(document,&QObject::destroyed,this,[this,document]{documents.remove(document);for(auto &group:groups)for(const auto &item:group.files)if(!item.window||item.window->textDoc()==document)group.valid=false;});
  }
  groups.append({files,false,true});while(groups.size()>64)groups.removeFirst();
 }
 bool apply(TeXDocumentWindow *caller,bool redo){
  if(applying||!caller)return false;
  for(auto it=groups.rbegin();it!=groups.rend();++it){auto &group=*it;if(!group.valid||group.undone!=redo)continue;
   auto belongs=std::find_if(group.files.cbegin(),group.files.cend(),[&](const auto &file){return file.window==caller&&file.file==caller->fileName()&&(redo?caller->textDoc()->availableRedoSteps()==file.redoSteps:caller->textDoc()->availableUndoSteps()==file.undoSteps);});
   if(belongs==group.files.cend()||fingerprint(caller)!=(redo?belongs->before:belongs->after))continue;
   for(const auto &file:group.files){auto *w=file.window.data();if(!w||w->fileName()!=file.file||w->editor()->isReadOnly()||(redo?w->textDoc()->availableRedoSteps()!=file.redoSteps:w->textDoc()->availableUndoSteps()!=file.undoSteps)||fingerprint(w)!=(redo?file.before:file.after)){
     showTransientStatusMessage(caller,words("相关文件已有后续修改。请先撤销相关修改，再重试。","A related file has later edits. Undo those edits before retrying."));return true;
   }}
   QScopedValueRollback<bool> busy(applying,true);
   for(auto &file:group.files){auto *w=file.window.data();const auto cursor=w->textCursor();const int scroll=w->editor()->verticalScrollBar()->value();
    if(redo)w->editor()->QTextEdit::redo();else w->editor()->QTextEdit::undo();
    if(w!=caller){w->editor()->setTextCursor(cursor);w->editor()->verticalScrollBar()->setValue(scroll);}
    if(redo)file.undoSteps=w->textDoc()->availableUndoSteps();else file.redoSteps=w->textDoc()->availableRedoSteps();
   }
   group.undone=!redo;return true;
  }
  return false;
 }
};
SourceHistory *history(){static QPointer<SourceHistory> value;if(!value)value=new SourceHistory;return value;}
}
SourceHistoryFile sourceHistoryBefore(TeXDocumentWindow *window){return {window,window->fileName(),fingerprint(window),{},0,0};}
void registerSourceHistory(QVector<SourceHistoryFile> files){history()->add(std::move(files));}
bool sourceHistory(QTextEdit *editor,bool redo){auto *window=qobject_cast<TeXDocumentWindow*>(editor->window());return window&&window->editor()==editor&&history()->apply(window,redo);}
}
