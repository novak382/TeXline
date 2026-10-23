// Source windows keep their own native undo stacks. A multi-file slide cut
// couples only the verified top commands when undo comes from the slide list.
  struct ClipboardFileChange {QPointer<TeXDocumentWindow> window;QByteArray before,after;int undoSteps{0},redoSteps{0};};
  struct ClipboardGroup {QVector<ClipboardFileChange> files;bool undone{false},valid{true};};
  QVector<ClipboardGroup> clipboardGroups;
  QSet<QObject*> clipboardDocuments;
  bool applyingClipboardGroup{false};
  QByteArray clipboardFingerprint(TeXDocumentWindow *w)const{return QCryptographicHash::hash(w->textDoc()->editorText().toUtf8(),QCryptographicHash::Sha256);}
  bool cutAcrossFiles(const QVector<int> &rows,bool apply) {
    QHash<QString,QVector<int>> byFile;QStringList order;
    for(int row:rows){const auto file=deck.frames[row].file;if(!byFile.contains(file))order.append(file);byFile[file].append(row);}
    if(order.size()<2)return false;
    for(const auto &file:order){auto *target=sourceWindow(file);auto local=deck;for(int row:rows)if(!byFile[file].contains(row))local.frames[row].kind.clear();QString why;if(!editSlideSelection(target,local,byFile[file],SlideEdit::Delete,-1,&why,false)){showTransientStatusMessage(window,words(why,QStringLiteral("The selected source files changed or contain a live slide dependency.")));return false;}}
    if(!apply)return true;
    ClipboardGroup group;
    for(const auto &file:order){auto *target=sourceWindow(file);group.files.append({target,clipboardFingerprint(target),{},0,0});
      auto *document=target->textDoc();if(!clipboardDocuments.contains(document)){clipboardDocuments.insert(document);connect(document,&QTextDocument::undoCommandAdded,this,[this]{if(!applyingClipboardGroup)for(auto &group:clipboardGroups)if(group.undone)group.valid=false;});connect(document,&QObject::destroyed,this,[this,document]{clipboardDocuments.remove(document);});}
    }
    QScopedValueRollback<bool> applying(applyingClipboardGroup,true);
    for(int n=0;n<order.size();++n){const auto file=order[n];auto local=deck;for(int row:rows)if(!byFile[file].contains(row))local.frames[row].kind.clear();
      if(!editSlideSelection(group.files[n].window,local,byFile[file],SlideEdit::Delete)){for(int done=n-1;done>=0;--done)group.files[done].window->editor()->undo();return false;}
      auto &change=group.files[n];change.after=clipboardFingerprint(change.window);change.undoSteps=change.window->textDoc()->availableUndoSteps();
    }
    for(auto &previous:clipboardGroups)if(previous.undone)previous.valid=false;clipboardGroups.append(group);while(clipboardGroups.size()>64)clipboardGroups.removeFirst();editTarget=group.files.first().window;preferredFile=path(editTarget->fileName());preferredStart=editTarget->cursorPosition();parseTimer.start(0);return true;
  }
  bool clipboardGroupHistory(bool redo) {
    QHash<TeXDocumentWindow*,QByteArray> fingerprints;auto fingerprint=[&](TeXDocumentWindow *w){if(!fingerprints.contains(w))fingerprints.insert(w,clipboardFingerprint(w));return fingerprints.value(w);};
    for(int n=int(clipboardGroups.size())-1;n>=0;--n){auto &group=clipboardGroups[n];if(!group.valid||group.undone!=redo)continue;bool matches=true;
      for(const auto &change:group.files){auto *target=change.window.data();if(!target||target->editor()->isReadOnly()||(redo?target->textDoc()->availableRedoSteps()!=change.redoSteps:target->textDoc()->availableUndoSteps()!=change.undoSteps)||fingerprint(target)!=(redo?change.before:change.after)){matches=false;break;}}
      if(!matches)continue;
      QScopedValueRollback<bool> applying(applyingClipboardGroup,true);
      if(redo){for(auto &change:group.files){change.window->editor()->redo();change.undoSteps=change.window->textDoc()->availableUndoSteps();}}
      else {for(int i=int(group.files.size())-1;i>=0;--i){auto &change=group.files[i];change.window->editor()->undo();change.redoSteps=change.window->textDoc()->availableRedoSteps();}}
      group.undone=!redo;editTarget=group.files.first().window;preferredFile=path(editTarget->fileName());preferredStart=editTarget->cursorPosition();parseTimer.start(0);return true;
    }return false;
  }
