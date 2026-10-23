// VersionDialog's on-demand workflows. Included inside the dialog class.
// GPL-2.0-or-later.
  void refreshHistory() {
    run({QStringLiteral("log"),QStringLiteral("--exclude=refs/texline/*"),QStringLiteral("--all"),QStringLiteral("--graph"),QStringLiteral("-150"),QStringLiteral("--date=short"),QStringLiteral("--format=%H%x00%h%x00%s%x00%an%x00%ad%x00%D")},[this](TaskResult result){
      if(!result.ok)return;
      const QRegularExpression commitHash(QStringLiteral("([0-9a-f]{40,64})$"));int commits=0;
      for(const auto &bytes:result.output.split('\n')){
        if(bytes.isEmpty())continue;const auto parts=QString::fromUtf8(bytes).split(QChar(0));
        const auto match=commitHash.match(parts[0]);
        auto *item=new QListWidgetItem(files);
        if(parts.size()!=6||!match.hasMatch()){
          item->setData(Qt::UserRole+7,parts[0]);item->setData(Qt::UserRole+8,QString());item->setFlags(Qt::NoItemFlags);continue;
        }
        const auto detail=parts[4]+QStringLiteral(" · ")+parts[3]+(parts[5].isEmpty()?QString():QStringLiteral(" · ")+parts[5]);
        item->setText(parts[2]);item->setData(Qt::UserRole,match.captured(1));
        item->setData(Qt::UserRole+7,parts[0].left(match.capturedStart()));item->setData(Qt::UserRole+8,parts[2]);item->setData(Qt::UserRole+9,detail);
        item->setToolTip(parts[1]+QStringLiteral(" · ")+parts[2]+QStringLiteral("\n")+detail);++commits;
      }
      status->setText(commits>=150?words("最近 150 个版本 · 包含本地与远程分支","Latest 150 commits · Local and remote branches"):words("%1 个版本 · 本地与远程分支","%1 commits · Local and remote branches").arg(commits));
      for(int row=0;row<files->count();++row)if(!files->item(row)->data(Qt::UserRole).toString().isEmpty()){files->setCurrentRow(row);break;}
    });
  }

  void showTrackingStatus() {
    if(branch.isEmpty()){if(files->count())files->setCurrentRow(0);return;}
    run({QStringLiteral("for-each-ref"),QStringLiteral("--format=%(upstream:short)%00%(upstream:track)"),QStringLiteral("refs/heads/")+branch},[this](TaskResult result){
      if(result.ok&&operationError.isEmpty()){
        const auto parts=QString::fromUtf8(result.output).trimmed().split(QChar(0));const auto tracking=parts.value(1);
        const auto ahead=QRegularExpression(QStringLiteral("ahead (\\d+)")).match(tracking),behind=QRegularExpression(QStringLiteral("behind (\\d+)")).match(tracking);
        QStringList details;if(ahead.hasMatch())details.append(words("待推送 %1","%1 outgoing").arg(ahead.captured(1)));if(behind.hasMatch())details.append(words("待拉取 %1","%1 incoming").arg(behind.captured(1)));
        if(!details.isEmpty())status->setText(status->text()+QStringLiteral(" · ")+details.join(QStringLiteral(" · ")));
        sync->setToolTip(parts.value(0));
      }
      if(files->count())files->setCurrentRow(0);
    });
  }

  void recoverCommit() {
    run({QStringLiteral("for-each-ref"),QStringLiteral("--sort=-creatordate"),QStringLiteral("--format=%(objectname)%00%(creatordate:short)  %(subject)"),QStringLiteral("refs/texline/recovery")},[this](TaskResult result){
      if(!result.ok)return;QList<QPair<QString,QString>> choices;
      for(const auto &line:result.output.split('\n')){const auto parts=line.split('\0');if(parts.size()==2)choices.append({QString::fromUtf8(parts[1]),QString::fromUtf8(parts[0])});}
      const auto selected=pickVersionItem(words("修订前保留的版本","Versions retained before revision"),choices);if(selected.isEmpty())return;
      const auto name=workspaceInput(this,words("从保留版本新建分支","Create branch from retained version"),words("新分支名称","New branch name"));
      if(!name.isEmpty()&&!name.startsWith(QLatin1Char('-')))change({QStringLiteral("branch"),name,selected});
    });
  }

  void refreshBranches() {
    run({QStringLiteral("for-each-ref"),QStringLiteral("--format=%(refname)%00%(refname:short)%00%(HEAD)%00%(upstream:short)%00%(upstream:track)"),QStringLiteral("refs/heads"),QStringLiteral("refs/remotes")},[this](TaskResult result){
      if(!result.ok)return;
      for(const auto &row:result.output.split('\n')){
        const auto parts=row.split('\0');if(parts.size()!=5||parts[0].endsWith("/HEAD"))continue;
        const auto reference=QString::fromUtf8(parts[0]),name=QString::fromUtf8(parts[1]);
        const bool current=parts[2]=="*",remote=reference.startsWith(QStringLiteral("refs/remotes/"));
        auto *item=new QListWidgetItem(name+(current?words(" · 当前"," · Current"):remote?words(" · 远程"," · Remote"):QString()),files);
        item->setData(Qt::UserRole,reference);item->setData(Qt::UserRole+5,remote);
        item->setToolTip(name+QStringLiteral("\n")+QString::fromUtf8(parts[3])+QStringLiteral(" ")+QString::fromUtf8(parts[4]));
        if(current)branch=name;
      }
      status->clear();
    });
  }

  void switchReference(const QString &reference) {
    if(reference.startsWith(QStringLiteral("refs/remotes/"))){
      const auto shortName=reference.mid(QStringLiteral("refs/remotes/").size());
      const auto proposed=shortName.mid(shortName.indexOf(QLatin1Char('/'))+1);
      const auto name=workspaceInput(this,words("跟踪远程分支","Track remote branch"),words("本地分支名称","Local branch name"),proposed);
      if(!name.isEmpty()&&!name.startsWith(QLatin1Char('-')))change({QStringLiteral("switch"),QStringLiteral("--track"),QStringLiteral("-c"),name,reference});
    }else if(reference.startsWith(QStringLiteral("refs/heads/")))change({QStringLiteral("switch"),QStringLiteral("--"),reference.mid(QStringLiteral("refs/heads/").size())});
  }

  void branchContext(QListWidgetItem *item,const QPoint &point) {
    const auto reference=item->data(Qt::UserRole).toString();const bool remote=item->data(Qt::UserRole+5).toBool();
    QMenu menu(this);auto *checkout=menu.addAction(remote?words("跟踪此分支…","Track this branch…"):words("切换到此分支","Switch branch"));
    auto *compare=menu.addAction(words("与当前版本比较","Compare with current version"));
    auto *merge=menu.addAction(words("合并到当前分支…","Merge into current branch…"));
    auto *remove=remote?nullptr:menu.addAction(words("删除已合并分支…","Delete merged branch…"));
    const auto selected=menu.exec(files->viewport()->mapToGlobal(point));
    if(selected==checkout)switchReference(reference);
    else if(selected==compare){run({QStringLiteral("diff"),QStringLiteral("--no-ext-diff"),QStringLiteral("--no-textconv"),QStringLiteral("HEAD"),reference,QStringLiteral("--")},[this,reference](TaskResult result){
      if(!result.ok)return;MaterialDialog draft(this);configureSettingsSurface(&draft);draft.setWindowTitle(reference);
      auto *layout=new QVBoxLayout(&draft);auto *text=new QPlainTextEdit(&draft);configureWorkspaceText(text);text->setReadOnly(true);text->setPlainText(QString::fromUtf8(result.output));new VersionDiffHighlighter(text->document());layout->addWidget(text,1);
      auto *buttons=new QDialogButtonBox(QDialogButtonBox::Close,&draft);configureButton(buttons->button(QDialogButtonBox::Close));buttons->button(QDialogButtonBox::Close)->setText(words("关闭","Close"));layout->addWidget(buttons);connect(buttons,&QDialogButtonBox::rejected,&draft,&QDialog::reject);fitWorkspaceDialog(&draft,800,560);draft.exec();
    });}
    else if(selected==merge&&workspaceConfirm(this,words("合并分支","Merge branch"),item->text(),words("合并","Merge")))change({QStringLiteral("merge"),QStringLiteral("--no-edit"),QStringLiteral("--"),reference});
    else if(remove&&selected==remove&&workspaceConfirm(this,words("删除分支","Delete branch"),item->text(),words("删除","Delete")))change({QStringLiteral("branch"),QStringLiteral("-d"),QStringLiteral("--"),reference.mid(QStringLiteral("refs/heads/").size())});
  }

  void manageRemotes() {
    run({QStringLiteral("remote")},[this](TaskResult result){
      if(!result.ok)return;QList<QPair<QString,QString>> choices;
      for(const auto &name:QString::fromUtf8(result.output).split(QLatin1Char('\n'),Qt::SkipEmptyParts))choices.append({name,name});
      const auto name=pickVersionItem(words("远程仓库","Remote repositories"),choices);if(name.isEmpty())return;
      run({QStringLiteral("remote"),QStringLiteral("get-url"),name},[this,name](TaskResult address){
        if(!address.ok)return;MaterialDialog draft(this);configureSettingsSurface(&draft);draft.setWindowTitle(name);
        auto *layout=new QVBoxLayout(&draft);auto *url=new QLineEdit(QString::fromUtf8(address.output).trimmed(),&draft);configureField(url);layout->addWidget(url);
        auto *operation=new QComboBox(&draft);operation->addItem(words("更新仓库地址","Update repository URL"));operation->addItem(words("移除此远程配置","Remove this remote"));layout->addWidget(operation);
        auto *buttons=workspaceFooter(&draft,layout,words("应用","Apply"));connect(buttons,&QDialogButtonBox::accepted,&draft,&QDialog::accept);fitWorkspaceDialog(&draft,620,230);
        if(draft.exec()!=QDialog::Accepted)return;
        if(operation->currentIndex()==1){if(workspaceConfirm(this,words("移除远程配置","Remove remote"),name,words("移除","Remove")))change({QStringLiteral("remote"),QStringLiteral("remove"),name});}
        else if(!url->text().trimmed().isEmpty()&&!url->text().startsWith(QLatin1Char('-')))change({QStringLiteral("remote"),QStringLiteral("set-url"),name,url->text().trimmed()});
      });
    });
  }

  void worktrees(int mode) {
    if(mode==1){chooseReference(words("工作副本的起始版本","Starting version for working copy"),[this](QString reference){
      const auto parent=QFileDialog::getExistingDirectory(this,words("工作副本的存放文件夹","Choose working copy parent folder"));if(parent.isEmpty())return;
      const auto name=workspaceInput(this,words("新建工作副本","New working copy"),words("新文件夹名称","New folder name"));
      if(name.isEmpty()||name==QStringLiteral(".")||name==QStringLiteral("..")||name.contains(QLatin1Char('/'))||name.contains(QLatin1Char('\\')))return;
      const auto target=QDir(parent).filePath(name);if(QFileInfo::exists(target)){status->setText(words("目标文件夹已存在。","Destination folder already exists."));return;}
      const auto branchName=workspaceInput(this,words("新建工作副本","New working copy"),words("新分支名称","New branch name"),name);
      if(branchName.isEmpty()||branchName.startsWith(QLatin1Char('-')))return;
      change({QStringLiteral("worktree"),QStringLiteral("add"),QStringLiteral("-b"),branchName,QStringLiteral("--"),target,reference});
    });return;}
    run({QStringLiteral("worktree"),QStringLiteral("list"),QStringLiteral("--porcelain"),QStringLiteral("-z")},[this,mode](TaskResult result){
      if(!result.ok)return;QList<QPair<QString,QString>> choices;QString path,label;
      for(const auto &row:result.output.split('\0')){
        if(row.startsWith("worktree ")){path=QString::fromUtf8(row.mid(9));label=path;}
        else if(row.startsWith("branch "))label+=QStringLiteral(" · ")+QString::fromUtf8(row.mid(7));
        else if(row.isEmpty()&&!path.isEmpty()){choices.append({label,path});path.clear();}
      }
      const auto selected=pickVersionItem(mode==2?words("移除工作副本","Remove working copy"):words("打开工作副本","Open working copy"),choices);if(selected.isEmpty())return;
      if(mode==2){
        if(QFileInfo(selected)==QFileInfo(root)){status->setText(words("当前工作副本正在使用。","The current working copy is in use."));return;}
        for(auto *window:TeXDocumentWindow::documentList())if(!window->untitled()){
          const auto relative=QDir(selected).relativeFilePath(window->fileName());
          if(!QDir::isAbsolutePath(relative)&&!relative.startsWith(QStringLiteral("../"))){status->setText(words("请先关闭此工作副本中打开的文稿。","Close documents in this working copy first."));return;}
        }
        if(workspaceConfirm(this,words("移除工作副本","Remove working copy"),selected+QStringLiteral("\n")+words("分支与提交记录保留。","The branch and commits are retained."),words("移除","Remove")))change({QStringLiteral("worktree"),QStringLiteral("remove"),QStringLiteral("--"),selected});
        return;
      }
      const auto document=QFileDialog::getOpenFileName(this,words("打开文稿","Open document"),selected,QStringLiteral("LaTeX (*.tex);;All files (*)"));
      if(!document.isEmpty())TeXDocumentWindow::openDocument(document,true,true);
    });
  }

  void stageSelectedLines() {
    if(busy||tabs->currentIndex()!=0||shownPath.isEmpty())return;
    const auto cursor=preview->textCursor();if(!cursor.hasSelection())return;
    const int first=preview->document()->findBlock(cursor.selectionStart()).blockNumber();
    const int last=preview->document()->findBlock(cursor.selectionEnd()-1).blockNumber();
    QSet<int> selected;
    for(int line=first;line<=last;++line)if(diffLines.value(line,-1)>=0)selected.insert(diffLines[line]);
    const auto patch=gitPatchForSelectedLines(shownDiff,selected,shownStaged);
    if(patch.isEmpty()){
      status->setText(words("请选择新增或删除的文字行；重命名和二进制文件请按整个文件操作。","Select added or removed lines. Stage renamed or binary files as a whole."));return;
    }
    change({QStringLiteral("apply"),QStringLiteral("--cached"),QStringLiteral("--recount"),QStringLiteral("-")},patch.toUtf8());
  }

  QString pickVersionItem(const QString &title,const QList<QPair<QString,QString>> &items) {
    if(items.isEmpty()){status->setText(words("没有可选择的项目。","Nothing to select."));return {};}
    MaterialDialog dialog(this);configureSettingsSurface(&dialog);
    dialog.setObjectName(QStringLiteral("researchGitPicker"));dialog.setWindowTitle(title);
    auto *layout=new QVBoxLayout(&dialog);
    auto *query=new QLineEdit(&dialog);configureField(query);query->setPlaceholderText(words("筛选","Filter"));
    query->setObjectName(QStringLiteral("researchGitPickerQuery"));layout->addWidget(query);
    auto *list=new QListWidget(&dialog);configureSettingsList(list);
    list->setObjectName(QStringLiteral("researchGitPickerItems"));layout->addWidget(list,1);
    for(const auto &pair:items){auto *item=new QListWidgetItem(pair.first,list);item->setData(Qt::UserRole,pair.second);item->setToolTip(pair.first);}
    list->setCurrentRow(0);
    connect(query,&QLineEdit::textChanged,&dialog,[list](const QString &text){
      for(int i=0;i<list->count();++i)list->item(i)->setHidden(!list->item(i)->text().contains(text,Qt::CaseInsensitive));
    });
    auto *buttons=workspaceFooter(&dialog,layout,words("选择","Choose"));
    auto accept=[&]{if(list->currentItem()&&!list->currentItem()->isHidden())dialog.accept();};
    connect(buttons,&QDialogButtonBox::accepted,&dialog,accept);
    connect(list,&QListWidget::itemActivated,&dialog,[&](QListWidgetItem*){accept();});
    fitWorkspaceDialog(&dialog,600,430);
    return dialog.exec()==QDialog::Accepted?list->currentItem()->data(Qt::UserRole).toString():QString();
  }

  void chooseReference(const QString &title,std::function<void(QString)> selected) {
    run({QStringLiteral("for-each-ref"),QStringLiteral("--format=%(refname)%00%(refname:short)"),QStringLiteral("refs/heads"),QStringLiteral("refs/remotes"),QStringLiteral("refs/tags")},
      [this,title,selected=std::move(selected)](TaskResult result){
        if(!result.ok)return;QList<QPair<QString,QString>> choices;
        for(const auto &line:result.output.split('\n')) {
          const auto parts=line.split('\0');if(parts.size()!=2||parts[0].endsWith("/HEAD"))continue;
          choices.append({QString::fromUtf8(parts[1]),QString::fromUtf8(parts[0])});
        }
        const auto reference=pickVersionItem(title,choices);if(!reference.isEmpty())selected(reference);
      });
  }

  void manageStashes() {
    run({QStringLiteral("stash"),QStringLiteral("list"),QStringLiteral("--format=%gd%x00%gs%x00%cr")},[this](TaskResult result){
      if(!result.ok)return;QList<QPair<QString,QString>> choices;
      for(const auto &line:result.output.split('\n')) {
        const auto parts=line.split('\0');if(parts.size()!=3)continue;
        choices.append({QString::fromUtf8(parts[1])+QStringLiteral(" · ")+QString::fromUtf8(parts[2]),QString::fromUtf8(parts[0])});
      }
      const auto selected=pickVersionItem(words("暂存的工作进度","Stashed work"),choices);if(selected.isEmpty())return;
      run({QStringLiteral("stash"),QStringLiteral("show"),QStringLiteral("--patch"),QStringLiteral("--include-untracked"),selected},[this,selected](TaskResult diff){
        if(!diff.ok)return;MaterialDialog draft(this);configureSettingsSurface(&draft);
        draft.setObjectName(QStringLiteral("researchGitStashPreview"));draft.setWindowTitle(words("恢复工作进度","Restore stashed work"));
        auto *layout=new QVBoxLayout(&draft);auto *text=new QPlainTextEdit(&draft);configureWorkspaceText(text);
        text->setReadOnly(true);text->setPlainText(QString::fromUtf8(diff.output));new VersionDiffHighlighter(text->document());layout->addWidget(text,1);
        auto *mode=new QComboBox(&draft);mode->addItem(words("恢复并保留此进度","Apply and keep stash"),QStringLiteral("apply"));
        mode->addItem(words("恢复成功后移除此进度","Apply and remove on success"),QStringLiteral("pop"));
        mode->addItem(words("只移除此进度","Delete this stash"),QStringLiteral("drop"));layout->addWidget(mode);
        auto *buttons=workspaceFooter(&draft,layout,words("继续","Continue"));connect(buttons,&QDialogButtonBox::accepted,&draft,&QDialog::accept);
        fitWorkspaceDialog(&draft,760,500);if(draft.exec()!=QDialog::Accepted)return;
        const auto operation=mode->currentData().toString();
        if(operation==QStringLiteral("drop")&&!workspaceConfirm(this,words("移除暂存进度","Delete stash"),selected,words("移除","Delete")))return;
        QStringList args{QStringLiteral("stash"),operation};if(operation!=QStringLiteral("drop"))args<<QStringLiteral("--index");args<<selected;change(args);
      });
    });
  }

  void createBranchFrom() {
    chooseReference(words("从哪个版本创建分支","Choose branch starting point"),[this](const QString &reference){
      const auto name=workspaceInput(this,words("新建分支","New branch"),words("分支名称","Branch name"));
      if(!name.isEmpty()&&!name.startsWith(QLatin1Char('-')))change({QStringLiteral("switch"),QStringLiteral("-c"),name,reference});
    });
  }

  void revisionOperation(const QString &operation,const QString &revision) {
    const auto title=operation==QStringLiteral("cherry-pick")?words("应用这个版本的修改","Apply this commit's changes"):
                     operation==QStringLiteral("rebase")?words("将当前分支接到另一分支之后","Rebase current branch"):
                     words("查看指定版本","Check out revision");
    if(workspaceConfirm(this,title,revision,words("继续","Continue")))change({operation,revision});
  }

  void manageOperation(const QString &operation,const QString &step) {
    QStringList args{operation,QStringLiteral("--")+step};
    const auto title=step==QStringLiteral("abort")?words("取消进行中的操作","Abort current operation"):
                     step==QStringLiteral("skip")?words("跳过当前版本","Skip current commit"):words("继续完成操作","Continue operation");
    if(step==QStringLiteral("continue")||workspaceConfirm(this,title,operation,words("继续","Continue")))change(args);
  }

  void reviseLastCommit(bool amend) {
    run({QStringLiteral("log"),QStringLiteral("-1"),QStringLiteral("--format=%H%x00%B")},[this,amend](TaskResult result){
      if(!result.ok)return;const auto split=result.output.indexOf('\0');if(split<0)return;
      const auto head=QString::fromUtf8(result.output.left(split)).trimmed();
      const auto description=QString::fromUtf8(result.output.mid(split+1)).trimmed();
      QString replacement;
      if(amend){bool accepted=false;replacement=workspaceInput(this,words("修订上次版本","Amend last commit"),words("版本说明","Commit message"),description,&accepted,false);if(!accepted||replacement.trimmed().isEmpty())return;}
      else if(!workspaceConfirm(this,words("撤销上次提交","Undo last commit"),description+QStringLiteral("\n\n")+words("修改保留在本次待提交内容中。","Changes remain staged."),words("撤销提交","Undo commit")))return;
      const auto backup=QStringLiteral("refs/texline/recovery/")+QUuid::createUuid().toString(QUuid::WithoutBraces);
      run({QStringLiteral("update-ref"),backup,head},[this,amend,replacement,head](TaskResult saved){
        if(!saved.ok)return;
        run({QStringLiteral("rev-parse"),QStringLiteral("HEAD")},[this,amend,replacement,head](TaskResult current){
          if(!current.ok||QString::fromUtf8(current.output).trimmed()!=head){status->setText(words("版本已变化，请重新核对。","HEAD changed. Review it again."));return;}
          if(amend)change({QStringLiteral("commit"),QStringLiteral("--amend"),QStringLiteral("-F"),QStringLiteral("-")},replacement.toUtf8());
          else run({QStringLiteral("rev-list"),QStringLiteral("--parents"),QStringLiteral("-1"),head},[this,head](TaskResult parents){
            if(!parents.ok)return;const auto values=QString::fromUtf8(parents.output).trimmed().split(QLatin1Char(' '),Qt::SkipEmptyParts);
            if(values.size()==1)change({QStringLiteral("update-ref"),QStringLiteral("-d"),QStringLiteral("HEAD"),head});
            else if(values.size()>1)change({QStringLiteral("reset"),QStringLiteral("--soft"),values[1]});
          });
        });
      },true);
    });
  }

  void compareReference(const QString &revision) {
    chooseReference(words("比较版本","Compare revisions"),[this,revision](const QString &reference){
      run({QStringLiteral("diff"),QStringLiteral("--no-ext-diff"),QStringLiteral("--no-textconv"),reference,revision,QStringLiteral("--")},[this,revision,reference](TaskResult result){
        if(result.ok){shownPath.clear();setFileTitle(reference+QStringLiteral(" → ")+revision.left(12));displayDiff(QString::fromUtf8(result.output));}
      });
    });
  }

  void currentFileHistory() {
    if(!owner||owner->untitled())return;
    const auto relative=QDir(root).relativeFilePath(owner->fileName());
    if(relative.startsWith(QStringLiteral("../"))||QDir::isAbsolutePath(relative))return;
    run({QStringLiteral("log"),QStringLiteral("--follow"),QStringLiteral("-200"),QStringLiteral("--format=%H%x00%ad  %s  ·  %an"),QStringLiteral("--date=short"),QStringLiteral("--"),relative},[this,relative](TaskResult result){
      if(!result.ok)return;QList<QPair<QString,QString>> choices;
      for(const auto &line:result.output.split('\n')){const auto values=line.split('\0');if(values.size()==2)choices.append({QString::fromUtf8(values[1]),QString::fromUtf8(values[0])});}
      const auto revision=pickVersionItem(words("文稿历史 · ","File history · ")+relative,choices);
      if(revision.isEmpty())return;
      run({QStringLiteral("show"),QStringLiteral("--format=short"),QStringLiteral("--no-ext-diff"),QStringLiteral("--no-textconv"),revision,QStringLiteral("--"),relative},[this,relative](TaskResult diff){if(diff.ok){shownPath.clear();setFileTitle(relative);displayDiff(QString::fromUtf8(diff.output));}});
    });
  }

  void currentLineAuthor() {
    if(!owner||owner->untitled())return;
    if(owner->isModified()){status->setText(words("保存文稿后即可查看这一行的版本记录。","Save the document to see the history of this line."));return;}
    const auto relative=QDir(root).relativeFilePath(owner->fileName());
    if(relative.startsWith(QStringLiteral("../"))||QDir::isAbsolutePath(relative))return;
    const int line=owner->textCursor().blockNumber()+1;
    run({QStringLiteral("blame"),QStringLiteral("--line-porcelain"),QStringLiteral("-L"),QString::number(line)+QStringLiteral(",")+QString::number(line),QStringLiteral("--"),relative},[this,relative,line](TaskResult result){
      if(!result.ok)return;QMap<QByteArray,QByteArray> values;
      const auto rows=result.output.split('\n');for(const auto &row:rows){const auto at=row.indexOf(' ');if(at>0)values.insert(row.left(at),row.mid(at+1));}
      QString text=words("作者：","Author: ")+QString::fromUtf8(values.value("author"))+QStringLiteral("\n")+
          words("版本：","Commit: ")+QString::fromUtf8(rows.value(0).split(' ').value(0)).left(12)+QStringLiteral("\n")+
          QString::fromUtf8(values.value("summary"));
      shownPath.clear();shownDiff.clear();diffLines.clear();preview->setExtraSelections({});setFileTitle(relative+QStringLiteral(":%1").arg(line));preview->setPlainText(text);diffSummary->clear();
    });
  }
