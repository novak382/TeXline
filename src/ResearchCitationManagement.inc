// Included inside CitationDialog. Native document undo remains authoritative.
// GPL-2.0-or-later.
  struct BibliographyUndoFile {QPointer<TeXDocumentWindow> window;QByteArray hash;int steps{0};};
  QVector<BibliographyUndoFile> bibliographyUndoFiles;bool bibliographyUndone{false};
  QString bibliographyOldKey,bibliographyNewKey;
  int bibliographyBeforeStart{0},bibliographyBeforeEnd{0},bibliographyAfterStart{0},bibliographyAfterEnd{0};
  void bibliographyAnchor(int start,int end){
    if(!edit.target)return;edit.range=QTextCursor(edit.target->textDoc());edit.range.setPosition(start);edit.range.setPosition(end,QTextCursor::KeepAnchor);edit.rebase();
    if(existing.valid())existing=citationAt(edit.original,edit.start);
  }
  void rememberBibliographyUndo(const QString &file){
    bibliographyUndoFiles.clear();bibliographyUndone=false;bibliographyOldKey.clear();bibliographyNewKey.clear();
    if(auto *target=TeXDocumentWindow::findDocument(file))bibliographyUndoFiles.append({target,QCryptographicHash::hash(target->textDoc()->editorText().toUtf8(),QCryptographicHash::Sha256),target->textDoc()->availableUndoSteps()});
  }
  void bibliographyHistory(bool redo){
    if(bibliographyUndoFiles.isEmpty()||bibliographyUndone!=redo)return;
    for(const auto &file:bibliographyUndoFiles){auto *target=file.window.data();
      if(!target||target->editor()->isReadOnly()||(redo?target->textDoc()->availableRedoSteps():target->textDoc()->availableUndoSteps())!=file.steps||QCryptographicHash::hash(target->textDoc()->editorText().toUtf8(),QCryptographicHash::Sha256)!=file.hash){notice->setText(words("相关文件已有后续修改，请在文件中撤销。","Related files have later edits. Undo in those files."));return;}
    }
    for(auto &file:bibliographyUndoFiles){
      auto *target=file.window.data();const auto before=target->textDoc()->editorText();
      const auto cursor=target->textCursor();const int position=cursor.position(),anchor=cursor.anchor();
      const int vertical=target->editor()->verticalScrollBar()->value(),horizontal=target->editor()->horizontalScrollBar()->value();
      if(redo)target->editor()->redo();else target->editor()->undo();
      const auto after=target->textDoc()->editorText();auto restored=target->textCursor();
      restored.setPosition(mapSourcePosition(before,after,anchor));
      restored.setPosition(mapSourcePosition(before,after,position),QTextCursor::KeepAnchor);
      target->editor()->setTextCursor(restored);target->editor()->verticalScrollBar()->setValue(vertical);target->editor()->horizontalScrollBar()->setValue(horizontal);
      file.steps=redo?target->textDoc()->availableUndoSteps():target->textDoc()->availableRedoSteps();file.hash=QCryptographicHash::hash(after.toUtf8(),QCryptographicHash::Sha256);
    }
    bibliographyUndone=!redo;
    if(!bibliographyOldKey.isEmpty()){
      chosenKeys.replaceInStrings(QRegularExpression(QStringLiteral("^")+QRegularExpression::escape(redo?bibliographyOldKey:bibliographyNewKey)+QStringLiteral("$")),redo?bibliographyNewKey:bibliographyOldKey);
      bibliographyAnchor(redo?bibliographyAfterStart:bibliographyBeforeStart,redo?bibliographyAfterEnd:bibliographyBeforeEnd);
    }
    load();
  }
  void renameEntry(){
    if(!edit.target||!index||index->truncated||index->citationCoveragePartial){notice->setText(words("文献范围尚未完整读取，请先处理来源问题。","Resolve incomplete bibliography sources before renaming a key."));return;}
    const auto oldKey=key();if(keyCounts.value(oldKey)!=1)return;
    QString bib;for(const auto &entry:entries)if(entry.key==oldKey&&QFileInfo(entry.file).suffix().compare(QStringLiteral("bib"),Qt::CaseInsensitive)==0)bib=entry.file;
    if(bib.isEmpty())return;
    const auto newKey=workspaceInput(this,words("更改引用键","Rename citation key"),words("新的引用键","New citation key"),oldKey);
    if(newKey.isEmpty()||newKey==oldKey)return;
    if(!literalKey(newKey)||newKey.contains(QRegularExpression(QStringLiteral("[()\"=]")))){notice->setText(words("引用键含有不支持的字符。","The citation key contains unsupported characters."));return;}
    if(keyCounts.contains(newKey)){notice->setText(words("这个引用键已存在。","This citation key already exists."));return;}
    const auto snapshot=index;const auto buffers=sourceBuffers();
    QMap<QString,QString> original,next;QMap<QString,QByteArray> disk;
    QStringList files=snapshot->files+snapshot->bibliographies;files.removeDuplicates();
    for(const auto &file:files){
      const auto text=sourceOf(file,buffers);const auto hash=QCryptographicHash::hash(text.toUtf8(),QCryptographicHash::Sha256);
      const auto expected=snapshot->sourceBufferHashes.value(file,snapshot->bibliographyBufferHashes.value(file));
      const auto actualDisk=slideExportHash(file);
      if((!expected.isEmpty()&&expected!=hash)||(expected.isEmpty()&&(actualDisk!=snapshot->parsedHashes.value(file)||(buffers.contains(file)&&text!=sourceOf(file,{}))))){notice->setText(words("项目已变化，正在重新读取；请重新更改引用键。","The project changed. Reloading; rename the key again."));load();return;}
      original.insert(file,text);next.insert(file,text);disk.insert(file,actualDisk);
    }
    struct Replacement{int start,length;QString text;};QMap<QString,QVector<Replacement>> replacements;
    auto renameList=[&](QString text){const QRegularExpression token(QStringLiteral("(^|,)\\s*(")+QRegularExpression::escape(oldKey)+QStringLiteral(")(?=\\s*(?:,|$))"));auto matches=token.globalMatch(text);QVector<QPair<int,int>> ranges;while(matches.hasNext()){const auto match=matches.next();ranges.append({int(match.capturedStart(2)),int(match.capturedLength(2))});}for(auto it=ranges.crbegin();it!=ranges.crend();++it)text.replace(it->first,it->second,newKey);return text;};
    int citationCount=0;QSet<QString> commands;
    const QRegularExpression arguments(QStringLiteral(R"(\\[A-Za-z]+\*?\s*(?:\[[^\]]*\]\s*){0,2}\{([^{}]*)\})"));
    for(const auto &use:snapshot->uses)if(use.kind==QStringLiteral("citation-use")&&use.key==oldKey){
      const auto identity=use.file+QStringLiteral(":%1").arg(use.offset);if(commands.contains(identity))continue;commands.insert(identity);
      const auto match=arguments.match(maskProjectText(original.value(use.file)),use.offset);
      if(!match.hasMatch()||match.capturedStart()!=use.offset){notice->setText(words("引用结构已变化，请重新读取文献。","The citation structure changed. Reload references."));return;}
      const auto value=original.value(use.file).mid(match.capturedStart(1),match.capturedLength(1)),replacement=renameList(value);
      if(value==replacement){notice->setText(words("无法确认引用位置，请从源码核对。","Cannot confirm the citation location. Check its source."));return;}
      replacements[use.file].append({int(match.capturedStart(1)),int(match.capturedLength(1)),replacement});++citationCount;
    }
    for(const auto &file:snapshot->bibliographies){
      const auto parsed=parseBibliography(original.value(file));if(!parsed.error.isEmpty()){notice->setText(parsed.error);return;}
      for(const auto &record:parsed.records){
        if(file==bib&&record.key==oldKey)replacements[file].append({record.keyStart,record.keyEnd-record.keyStart,newKey});
        for(const auto &fieldName:QStringList{QStringLiteral("crossref"),QStringLiteral("xref"),QStringLiteral("xdata"),QStringLiteral("entryset"),QStringLiteral("related")}){
          if(!record.fields.contains(fieldName))continue;const auto field=record.fields.value(fieldName);const auto changed=renameList(field.value);if(changed==field.value)continue;
          if(!field.literal){notice->setText(words("文献间引用包含宏，请先在源码中展开该字段。","An inter-entry reference uses a macro. Edit that field in source first."));return;}
          const auto value=field.expression.startsWith(QLatin1Char('"'))?QLatin1Char('"')+changed+QLatin1Char('"'):QLatin1Char('{')+changed+QLatin1Char('}');
          replacements[file].append({field.start,field.end-field.start,value});
        }
      }
    }
    if(replacements.value(bib).isEmpty())return;
    QString description=oldKey+QStringLiteral(" → ")+newKey+QStringLiteral("\n")+words("更新 %1 处引用","Update %1 citations").arg(citationCount);
    for(auto it=replacements.begin();it!=replacements.end();++it){
      std::sort(it.value().begin(),it.value().end(),[](const Replacement &a,const Replacement &b){return a.start>b.start;});
      for(const auto &change:it.value())next[it.key()].replace(change.start,change.length,change.text);
      description+=QStringLiteral("\n")+QFileInfo(it.key()).fileName();
    }
    if(!workspaceConfirm(this,words("更改引用键","Rename citation key"),description,words("应用","Apply")))return;
    // Recheck every indexed input, including files whose include declarations
    // did not change, before applying any command to a native document stack.
    const auto current=sourceBuffers();
    for(const auto &file:files)if(sourceOf(file,current)!=original.value(file)||slideExportHash(file)!=disk.value(file)){notice->setText(words("项目已变化，请重新核对。","The project changed. Review it again."));return;}
    QVector<std::shared_ptr<SourceEdit>> drafts;QStringList values;
    for(auto it=replacements.begin();it!=replacements.end();++it){
      auto *target=TeXDocumentWindow::findDocument(it.key());if(!target)target=TeXDocumentWindow::openDocument(it.key(),false,false);
      if(!target||target->editor()->isReadOnly()||target->textDoc()->editorText()!=original.value(it.key())){notice->setText(words("无法编辑相关文件：","Cannot edit related file: ")+it.key());return;}
      auto draft=std::make_shared<SourceEdit>(target,0,0);const auto &after=next[it.key()];
      int first=0,oldEnd=draft->original.size(),newEnd=after.size();while(first<oldEnd&&first<newEnd&&draft->original[first]==after[first])++first;
      while(oldEnd>first&&newEnd>first&&draft->original[oldEnd-1]==after[newEnd-1]){--oldEnd;--newEnd;}
      draft->start=first;draft->end=oldEnd;drafts.append(draft);values.append(after.mid(first,newEnd-first));
    }
    for(const auto &draft:drafts)if(!draft->current()){notice->setText(words("文件已变化，请重新核对。","A file changed. Review it again."));return;}
    bibliographyBeforeStart=edit.start;bibliographyBeforeEnd=edit.end;
    auto mapAnchor=[&](int position){int delta=0;for(const auto &change:replacements.value(QFileInfo(edit.target->fileName()).canonicalFilePath())){if(position>=change.start+change.length)delta+=change.text.size()-change.length;else if(position>change.start)delta+=qMin(position-change.start,int(change.text.size()))-(position-change.start);}return position+delta;};
    bibliographyAfterStart=mapAnchor(edit.start);bibliographyAfterEnd=mapAnchor(edit.end);
    for(int i=0;i<drafts.size();++i)if(!drafts[i]->apply(values[i],{},notice)){for(int j=i-1;j>=0;--j)drafts[j]->target->editor()->undo();bibliographyAnchor(bibliographyBeforeStart,bibliographyBeforeEnd);return;}
    bibliographyUndoFiles.clear();bibliographyUndone=false;bibliographyOldKey=oldKey;bibliographyNewKey=newKey;
    for(const auto &draft:drafts){auto *target=draft->target.data();bibliographyUndoFiles.append({target,QCryptographicHash::hash(target->textDoc()->editorText().toUtf8(),QCryptographicHash::Sha256),target->textDoc()->availableUndoSteps()});}
    chosenKeys.replaceInStrings(QRegularExpression(QStringLiteral("^")+QRegularExpression::escape(oldKey)+QStringLiteral("$")),newKey);
    bibliographyAnchor(bibliographyAfterStart,bibliographyAfterEnd);load();
  }
