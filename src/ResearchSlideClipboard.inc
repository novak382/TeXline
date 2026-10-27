// Included inside SlideController. Both outline and overview use these actions.
#include "ResearchSlideClipboardHistory.inc"
  QPointer<TeXDocumentWindow> lastPasteTarget;
  QPointer<SlideList> lastPasteView;
  QTextCursor lastPasteRange;
  int lastPasteRevision{-1},lastPasteRow{-1};
  void restorePastedSelection() {
    if(!lastPasteTarget||lastPasteTarget->textDoc()->revision()!=lastPasteRevision||lastPasteRange.isNull())return;
    auto restore=[&](SlideList *view){if(!view)return;QSignalBlocker block(view);view->clearSelection();for(int i=0;i<deck.frames.size();++i){const auto &frame=deck.frames[i];if(frame.file==path(lastPasteTarget->fileName())&&frame.start>=lastPasteRange.selectionStart()&&frame.end<=lastPasteRange.selectionEnd())view->item(i)->setSelected(true);}};
    restore(static_cast<SlideList*>(list));if(overviewWindow)restore(overviewWindow->grid);updateActions();
  }
  void slideClipboard(SlideList *view,int operation,int before=-1) {
    if(!enabled||!view)return;
    struct RestoreFocus {QPointer<SlideList> view;~RestoreFocus(){if(view&&view->isVisible()){view->window()->activateWindow();view->setFocus();}}} restoreFocus{view};
    const bool repeat=operation==2&&before<0&&lastPasteView==view&&lastPasteTarget&&lastPasteTarget->textDoc()->revision()==lastPasteRevision&&view->currentRow()==lastPasteRow&&!lastPasteRange.isNull();
    if((dirty||busy)&&!repeat)return;
    QVector<int> rows;for(auto *item:view->selectedItems())if(!item->isHidden())rows.append(view->row(item));std::sort(rows.begin(),rows.end());
    if(operation<2) {
      if(rows.isEmpty())return;QJsonArray frames;
      for(int row:rows){if(row<0||row>=deck.frames.size())return;const auto &f=deck.frames[row];auto *owner=sourceWindow(f.file);if(!owner&&operation==1){owner=TeXDocumentWindow::openDocument(f.file,false,false);if(owner)carryContext(owner);}if(!f.editable||(owner?owner->textDoc()->editorText():read(f.file))!=deck.sources.value(f.file))return;frames.append(deck.sources.value(f.file).mid(f.start,f.end-f.start));}
      auto *target=sourceWindow(deck.frames[rows.first()].file);QString why;QSet<QString> files;for(int row:rows)files.insert(deck.frames[row].file);const bool acrossFiles=files.size()>1;
      if(operation==1&&!(acrossFiles?cutAcrossFiles(rows,false):editSlideSelection(target,deck,rows,SlideEdit::Delete,-1,&why,false))){if(!why.isEmpty())showTransientStatusMessage(window,words(why,QStringLiteral("A source boundary or live reference prevents this cut.")));return;}
      auto *mime=new QMimeData;mime->setData(QStringLiteral("application/x-texline-slides"),QJsonDocument(QJsonObject{{QStringLiteral("version"),1},{QStringLiteral("frames"),frames}}).toJson(QJsonDocument::Compact));QStringList plain;for(const auto &frame:frames)plain.append(frame.toString());mime->setText(plain.join(QLatin1Char('\n')));view->writeClipboard(mime);
      if(operation==1){if(acrossFiles)cutAcrossFiles(rows,true);else if(editSlideSelection(target,deck,rows,SlideEdit::Delete,-1,&why)){editTarget=target;preferredFile=path(target->fileName());preferredStart=target->cursorPosition();parseTimer.start(0);}}return;
    }
    const auto *mime=view->readClipboard();if(!mime||!mime->hasFormat(QStringLiteral("application/x-texline-slides")))return;
    const auto data=mime->data(QStringLiteral("application/x-texline-slides"));if(data.size()>8*1024*1024)return;const auto object=QJsonDocument::fromJson(data).object();if(object.value(QStringLiteral("version")).toInt()!=1)return;
    const auto frames=object.value(QStringLiteral("frames")).toArray();if(frames.isEmpty()||frames.size()>2048)return;
    if(before<0)before=view->currentRow()<0?int(deck.frames.size()):view->currentRow()+1;
    if(before<0||before>deck.frames.size())return;
    QString file=repeat?path(lastPasteTarget->fileName()):root;int at=repeat?lastPasteRange.selectionEnd():-1;
    if(!repeat&&!deck.frames.isEmpty()){const auto &f=deck.frames[qMin(before,int(deck.frames.size())-1)];if(!f.editable)return;file=f.file;at=before==deck.frames.size()?f.end:f.start;}
    auto *target=sourceWindow(file);if(!target){target=TeXDocumentWindow::openDocument(file,false,false);if(target)carryContext(target);}if(!target||target->editor()->isReadOnly()||!repeat&&target->textDoc()->editorText()!=deck.sources.value(file))return;
    const auto original=target->textDoc()->editorText();if(at<0){const auto end=QRegularExpression(QStringLiteral(R"(\\end\s*\{document\})")).match(maskProjectText(original));if(!end.hasMatch())return;at=int(end.capturedStart());}
    QString content;for(const auto &frame:frames){if(!frame.isString())return;content+=frame.toString();if(!content.endsWith(QLatin1Char('\n')))content+=QLatin1Char('\n');}
    // Remap literal anchors as a group, including internal references. Dynamic
    // names are kept in source rather than guessed by the clipboard operation.
    const QRegularExpression anchors(QStringLiteral(R"(\\(?:label|hypertarget)\s*\{([^{}\\]+)\}|\blabel\s*=\s*([^,\]\s{}]+))"));
    QHash<QString,QString> renamed;auto matches=anchors.globalMatch(maskProjectText(content));QSet<QString> used;
    auto collect=[&](const QString &text){auto existing=anchors.globalMatch(maskProjectText(text));while(existing.hasNext()){const auto m=existing.next();used.insert(m.captured(1).isEmpty()?m.captured(2):m.captured(1));}};collect(original);for(auto it=deck.sources.cbegin();it!=deck.sources.cend();++it)if(it.key()!=file){auto *owner=sourceWindow(it.key());collect(owner?owner->textDoc()->editorText():it.value());}
    while(matches.hasNext()){const auto m=matches.next();const auto key=m.captured(1).isEmpty()?m.captured(2):m.captured(1);if(renamed.contains(key))continue;auto replacement=key;for(int n=2;used.contains(replacement);++n)replacement=key+QStringLiteral("-%1").arg(n);used.insert(replacement);renamed.insert(key,replacement);}
    const QRegularExpression references(QStringLiteral(R"((\\(?:label|hypertarget|hyperlink|ref|pageref|eqref|autoref|cref|Cref|againframe)\s*(?:\[[^\]]*\]\s*)?\{)([^{}\\]+)(\})|(\blabel\s*=\s*)([^,\]\s{}]+))"));
    auto refs=references.globalMatch(content);struct Replacement{int at,length;QString value;};QVector<Replacement> changes;
    while(refs.hasNext()){const auto m=refs.next();const int group=m.captured(4).isEmpty()?2:5;auto keys=m.captured(group).split(QLatin1Char(','));for(auto &key:keys)if(renamed.contains(key.trimmed()))key=renamed.value(key.trimmed());changes.append({int(m.capturedStart(group)),int(m.capturedLength(group)),keys.join(QLatin1Char(','))});}
    for(int n=int(changes.size())-1;n>=0;--n)content.replace(changes[n].at,changes[n].length,changes[n].value);
    if(at>0&&original[at-1]!=QLatin1Char('\n'))content.prepend(QLatin1Char('\n'));
    SlideSourceEdit sourceEdit(target);
    auto cursor=target->textCursor();cursor.beginEditBlock();cursor.setPosition(at);cursor.insertText(content);cursor.endEditBlock();cursor.setPosition(at);target->editor()->setTextCursor(cursor);editTarget=target;preferredFile=file;preferredStart=at+qMax(0,int(content.size())-2);
    lastPasteTarget=target;lastPasteView=view;lastPasteRevision=target->textDoc()->revision();lastPasteRow=view->currentRow();lastPasteRange=QTextCursor(target->textDoc());lastPasteRange.setPosition(at);lastPasteRange.setPosition(at+int(content.size()),QTextCursor::KeepAnchor);parseTimer.start(0);
  }
  void installSlideClipboard(SlideList *view) {
    auto add=[&](const char *id,const QString &caption,int operation,const QKeySequence &key,int before=-1){auto *action=new QAction(caption,view);action->setObjectName(QString::fromLatin1(id));action->setShortcut(key);action->setShortcutContext(Qt::WidgetWithChildrenShortcut);view->addAction(action);connect(action,&QAction::triggered,this,[this,view,operation,before]{slideClipboard(view,operation,before==-2?view->currentRow():before);});};
    add("researchSlideCopy",words(QStringLiteral("复制"),QStringLiteral("Copy")),0,QKeySequence::Copy);
    add("researchSlideCut",words(QStringLiteral("剪切"),QStringLiteral("Cut")),1,QKeySequence::Cut);
    add("researchSlidePaste",words(QStringLiteral("粘贴到此张之后"),QStringLiteral("Paste after this slide")),2,QKeySequence::Paste);
    add("researchSlidePasteBefore",words(QStringLiteral("粘贴到此张之前"),QStringLiteral("Paste before this slide")),2,{},-2);
    connect(TWApp::instance(),&TWApp::updatedTranslators,view,[this,view]{const QList<QPair<QString,QString>> captions{{QStringLiteral("researchSlideCopy"),words(QStringLiteral("复制"),QStringLiteral("Copy"))},{QStringLiteral("researchSlideCut"),words(QStringLiteral("剪切"),QStringLiteral("Cut"))},{QStringLiteral("researchSlidePaste"),words(QStringLiteral("粘贴到此张之后"),QStringLiteral("Paste after this slide"))},{QStringLiteral("researchSlidePasteBefore"),words(QStringLiteral("粘贴到此张之前"),QStringLiteral("Paste before this slide"))}};for(const auto &caption:captions)if(auto *action=view->findChild<QAction*>(caption.first))action->setText(caption.second);});
    view->clipboardKey=[this,view](QKeyEvent *key){if(key->matches(QKeySequence::Copy)){slideClipboard(view,0);return true;}if(key->matches(QKeySequence::Cut)){slideClipboard(view,1);return true;}if(key->matches(QKeySequence::Paste)){slideClipboard(view,2);return true;}if(key->matches(QKeySequence::Undo)||key->matches(QKeySequence::Redo)){if(clipboardGroupHistory(key->matches(QKeySequence::Redo)))return true;auto *target=editTarget?editTarget.data():window;if(target&&!target->editor()->isReadOnly()){if(key->matches(QKeySequence::Undo))target->editor()->undo();else target->editor()->redo();preferredFile=path(target->fileName());preferredStart=target->cursorPosition();parseTimer.start(0);}return true;}return false;};
  }
