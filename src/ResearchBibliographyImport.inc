// Included in Research namespace, sharing the lossless bibliography editor.
// GPL-2.0-or-later.
bool appendBibliography(QWidget *parent,TeXDocumentWindow *owner,const QString &file,const QString &incoming,QStringList *added) {
  MaterialDialog dialog(parent);configureSettingsSurface(&dialog);
  dialog.setObjectName(QStringLiteral("researchBibImportPreview"));dialog.setWindowTitle(words("导入文献","Import references"));
  auto *layout=new QVBoxLayout(&dialog);
  auto *refresh=new QCheckBox(words("更新已有条目，保留项目引用键","Update existing entries, keeping project citation keys"),&dialog);
  refresh->setObjectName(QStringLiteral("researchBibRefreshExisting"));layout->addWidget(refresh);
  auto *list=new QListWidget(&dialog);configureSettingsList(list);
  list->setObjectName(QStringLiteral("researchBibImportEntries"));layout->addWidget(list,1);
  auto *preview=new QPlainTextEdit(&dialog);configureWorkspaceText(preview);preview->setReadOnly(true);
  preview->setMaximumHeight(180);layout->addWidget(preview);
  auto *notice=new QLabel(&dialog);notice->setWordWrap(true);layout->addWidget(notice);
  auto *buttons=footer(&dialog,layout,words("导入","Import"));
  const bool exists=QFileInfo::exists(file);
  std::unique_ptr<BibEdit> edit;if(exists)edit=std::make_unique<BibEdit>(owner,file);
  const auto original=edit?edit->original:QString();
  const auto parsed=parseBibliography(incoming);
  const bool dependencies=QRegularExpression(QStringLiteral("@(?:string|preamble)\\s*[({]"),QRegularExpression::CaseInsensitiveOption).match(incoming).hasMatch();
  QStringList drafts,keys;QString next,error;int updated=0;
  QSet<QString> knownKeys,knownWorks;const auto originalParsed=parseBibliography(original);
  QMap<QString,QString> originalEntries;QMap<QString,QStringList> workKeys;
  for(const auto &record:originalParsed.records){knownKeys.insert(record.key);originalEntries[record.key]+=original.mid(record.start,record.end-record.start)+QLatin1Char('\n');for(const auto &id:bibliographyWorkIds(record)){knownWorks.insert(id);workKeys[id].append(record.key);}}
  for(const auto &record:parsed.records) {
    const auto ids=bibliographyWorkIds(record);
    bool duplicate=knownKeys.contains(record.key);for(const auto &id:ids)duplicate|=knownWorks.contains(id);
    bool existing=originalEntries.contains(record.key);for(const auto &id:ids)existing|=workKeys.contains(id);
    drafts.append(incoming.mid(record.start,record.end-record.start));
    const auto title=fieldValue(record,QStringLiteral("title"));
    auto *item=new QListWidgetItem(record.key+QStringLiteral(" · ")+title,list);
    item->setToolTip(item->text());item->setData(Qt::UserRole,record.key);item->setData(Qt::UserRole+1,existing);
    item->setFlags(item->flags()|Qt::ItemIsUserCheckable);item->setCheckState(duplicate?Qt::Unchecked:Qt::Checked);
    if(duplicate)item->setText(item->text()+words(" · 已存在"," · Existing"));
    knownKeys.insert(record.key);for(const auto &id:ids)knownWorks.insert(id);
  }
  // Macro dependencies are indivisible: selecting only entries could silently
  // discard their definitions. Preserve the existing standalone-file path.
  refresh->setVisible(!original.isEmpty()&&!dependencies);
  if(dependencies){list->hide();preview->setMaximumHeight(QWIDGETSIZE_MAX);preview->setPlainText(incoming);}
  auto rebuild=[&] {
    error.clear();keys.clear();next=original;updated=0;QSet<QString> refreshed;
    if(!originalParsed.error.isEmpty())error=originalParsed.error;
    else if(originalEntries.size()!=originalParsed.records.size())error=words("请先处理重复引用键。","Resolve duplicate citation keys first.");
    else if(!parsed.error.isEmpty())error=parsed.error;
    else if(parsed.records.isEmpty())error=words("未找到可导入的 BibTeX 条目。","No BibTeX entries to import.");
    else if(dependencies)next=mergeBibliography(original,incoming,&keys,&error);
    else {
    QString additions;QMap<QString,QString> replacements;
    for(int i=0;i<list->count();++i)if(list->item(i)->checkState()==Qt::Checked) {
      if(refresh->isChecked()&&list->item(i)->data(Qt::UserRole+1).toBool()){
        const auto record=parseBibliography(drafts[i]).records.value(0);QSet<QString> candidates;
        if(originalEntries.contains(record.key))candidates.insert(record.key);
        for(const auto &id:bibliographyWorkIds(record))for(const auto &key:workKeys.value(id))candidates.insert(key);
        QString before;for(const auto &key:candidates)before+=originalEntries.value(key);
        QString key;const auto changed=updateBibliographyEntry(before,drafts[i],&key,&error);
        if(!error.isEmpty())break;
        if(refreshed.contains(key)){error=words("多个更新对应同一条文献，请只选择一个版本：","Multiple updates match one reference. Select one version: ")+key;break;}refreshed.insert(key);
        if(changed!=before){replacements.insert(key,changed.trimmed());++updated;keys.append(key);}continue;
      }
      additions+=drafts[i]+QLatin1Char('\n');
    }
    if(error.isEmpty()){
      for(auto it=originalParsed.records.crbegin();it!=originalParsed.records.crend();++it)if(replacements.contains(it->key))next.replace(it->start,it->end-it->start,replacements.value(it->key));
      if(!additions.isEmpty()){QStringList fresh;next=mergeBibliography(next,additions,&fresh,&error);keys.append(fresh);}
    }
    }
    notice->setText(error.isEmpty()?words("新增 %1 项 · 更新 %2 项 · %3","%1 new · %2 updated · %3").arg(keys.size()-updated).arg(updated).arg(QFileInfo(file).fileName()):error);
    buttons->button(QDialogButtonBox::Ok)->setEnabled(error.isEmpty()&&!keys.isEmpty()&&(!exists||edit->target));
  };
  auto showPreview=[&](int row){
    if(refresh->isChecked()&&list->item(row)&&list->item(row)->data(Qt::UserRole+1).toBool()){
      QString key,failure;const auto changed=updateBibliographyEntry(original,drafts.value(row),&key,&failure);
      if(failure.isEmpty()){
        QString before,after;for(const auto &r:parseBibliography(original).records)if(r.key==key)before=original.mid(r.start,r.end-r.start);
        for(const auto &r:parseBibliography(changed).records)if(r.key==key)after=changed.mid(r.start,r.end-r.start);
        preview->setPlainText(words("原条目\n","Current\n")+before+words("\n\n更新后\n","\n\nAfter update\n")+after);return;
      }
    }
    preview->setPlainText(drafts.value(row));
  };
  QObject::connect(refresh,&QCheckBox::toggled,&dialog,[&](bool enabled){
    {QSignalBlocker blocker(list);for(int i=0;i<list->count();++i)if(list->item(i)->data(Qt::UserRole+1).toBool())list->item(i)->setCheckState(enabled?Qt::Checked:Qt::Unchecked);}
    rebuild();showPreview(list->currentRow());
  });
  QObject::connect(list,&QListWidget::itemChanged,&dialog,[&](QListWidgetItem*){rebuild();});
  QObject::connect(list,&QListWidget::currentRowChanged,&dialog,showPreview);
  list->setContextMenuPolicy(Qt::CustomContextMenu);
  QObject::connect(list,&QListWidget::customContextMenuRequested,&dialog,[&](QPoint point) {
    auto *item=list->itemAt(point);if(!item)return;const int row=list->row(item);
    QMenu menu(&dialog);auto *rename=menu.addAction(words("更换引用键…","Change citation key…"));
    auto *all=menu.addAction(words("全选","Select all"));auto *none=menu.addAction(words("全不选","Select none"));
    const auto chosen=menu.exec(list->viewport()->mapToGlobal(point));
    if(chosen==rename){
      const auto key=workspaceInput(&dialog,words("引用键","Citation key"),words("新的引用键","New citation key"),item->data(Qt::UserRole).toString());
      if(key.isEmpty())return;
      if(!keyValid(key)){notice->setText(words("引用键不能包含空格或 BibTeX 分隔符。","Citation keys cannot contain spaces or BibTeX delimiters."));return;}
      const auto record=parseBibliography(drafts[row]).records.value(0);
      drafts[row].replace(record.keyStart,record.keyEnd-record.keyStart,key);
      QSignalBlocker changeGuard(list);bool existing=originalEntries.contains(key);for(const auto &id:bibliographyWorkIds(record))existing|=workKeys.contains(id);item->setData(Qt::UserRole+1,existing);
      item->setData(Qt::UserRole,key);item->setText(key+QStringLiteral(" · ")+fieldValue(record,QStringLiteral("title")));
      item->setToolTip(item->text());item->setCheckState(Qt::Checked);list->setCurrentItem(item);preview->setPlainText(drafts[row]);
    }else if(chosen==all||chosen==none){
      QSignalBlocker blocker(list);for(int i=0;i<list->count();++i)list->item(i)->setCheckState(chosen==all?Qt::Checked:Qt::Unchecked);
    }
    rebuild();
  });
  QObject::connect(buttons,&QDialogButtonBox::accepted,&dialog,[&] {
    rebuild();if(!error.isEmpty()||keys.isEmpty())return;
    if(!exists){
      if(QFileInfo::exists(file)){notice->setText(words("目标文件已出现，请重新导入。","The destination now exists. Retry the import."));return;}
      QString failure;
      if(!writeNewFile(file,next.toUtf8(),&failure)){notice->setText(failure);return;}
      rememberRoot(file,completionProjectRoot(owner));
    }else if(!edit->apply(next,notice))return;
    *added=keys;dialog.accept();
  });
  list->setCurrentRow(0);rebuild();dialogSize(&dialog);return dialog.exec()==QDialog::Accepted;
}
