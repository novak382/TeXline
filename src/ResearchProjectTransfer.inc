// Batch file operations use the same preview, journal and content checks as
// single-file moves. Included in Research namespace. GPL-2.0-or-later.
ProjectFilePlan planProjectTransfer(const QString &root,const QStringList &input,const QString &destination,ProjectTransfer operation,const std::function<bool()> &cancelled,const QString &mainFile) {
  ProjectFilePlan plan;plan.root=QFileInfo(root).canonicalFilePath();
  auto fail=[&](const QString &message){plan.error=message;return plan;};
  if(input.size()>10000)return fail(words("选中文件过多，请分批操作。","Select fewer files per operation."));
  auto stop=[&]{return cancelled&&cancelled();};
  QStringList sources;
  for(const auto &source:input){
    const auto path=QDir::cleanPath(QFileInfo(source).absoluteFilePath());
    const QFileInfo info(path);
    if(!info.exists()||info.isSymLink()||info.isJunction()||(operation!=ProjectTransfer::Copy&&!withinProject(root,path)))return fail(words("请选择普通文件或文件夹；移动和删除仅限当前项目。","Select regular files or folders. Move and delete operate within the current project."));
    if(!sources.contains(path))sources.append(path);
  }
  std::sort(sources.begin(),sources.end(),[](const QString &a,const QString &b){return a.size()<b.size();});
  QStringList top;
  for(const auto &path:sources){bool covered=false;for(const auto &parent:top)if(path.startsWith(parent+QLatin1Char('/'),Qt::CaseInsensitive)){covered=true;break;}if(!covered)top.append(path);}
  if(operation!=ProjectTransfer::Remove&&QFileInfo(destination)!=QFileInfo(root)&&!withinProject(root,destination))return fail(words("请选择项目内的目标文件夹。","Choose a destination folder inside the project."));
  if(operation!=ProjectTransfer::Remove&&!QFileInfo(destination).isDir())return fail(words("目标文件夹不存在。","The destination folder does not exist."));
  QMap<QString,QString> mapping;QSet<QString> destinations;qint64 bytes=0;int entries=0;
  for(const auto &source:top){
    if(stop())return fail(QStringLiteral("Cancelled"));
    const bool directory=QFileInfo(source).isDir();
    const auto target=operation==ProjectTransfer::Remove?QString():QDir(destination).filePath(QFileInfo(source).fileName());
    if(operation!=ProjectTransfer::Remove){
      if(QFileInfo::exists(target)||destinations.contains(target.toCaseFolded()))return fail(words("目标已存在：","Destination already exists: ")+target);
      if(directory&&(QFileInfo(destination)==QFileInfo(source)||destination.startsWith(source+QLatin1Char('/'),Qt::CaseInsensitive)))return fail(words("不能将文件夹放入自身。","A folder cannot be placed inside itself."));
      destinations.insert(target.toCaseFolded());
    }
    if(!directory){mapping.insert(source,target);continue;}
    if(operation!=ProjectTransfer::Copy)plan.removeDirectories.append(source);
    if(operation!=ProjectTransfer::Remove)plan.createDirectories.append(target);
    QDirIterator iterator(source,QDir::Files|QDir::Dirs|QDir::Hidden|QDir::System|QDir::NoDotAndDotDot,QDirIterator::Subdirectories);
    while(iterator.hasNext()){
      iterator.next();if(stop())return fail(QStringLiteral("Cancelled"));
      if(++entries>10000)return fail(words("文件夹过大，请分批操作。","Use smaller batches for this large folder."));
      const auto path=iterator.filePath();const auto info=iterator.fileInfo();
      if(info.isSymLink()||info.isJunction()||(operation!=ProjectTransfer::Copy&&!withinProject(root,path)))return fail(words("请单独处理文件夹中的链接：","Handle linked files separately: ")+path);
      const auto dest=operation==ProjectTransfer::Remove?QString():QDir(target).filePath(QDir(source).relativeFilePath(path));
      if(info.isDir()){
        if(operation!=ProjectTransfer::Copy)plan.removeDirectories.append(path);
        if(operation!=ProjectTransfer::Remove)plan.createDirectories.append(dest);
      }else if(info.isFile())mapping.insert(path,dest);
    }
  }
  if(operation==ProjectTransfer::Move)plan.moves=mapping;
  QStringList texts;
  if(operation==ProjectTransfer::Move){texts=projectTextFiles(root,cancelled,&plan.error);if(!plan.error.isEmpty())return plan;}
  QStringList all=mapping.keys();for(const auto &file:texts)if(!all.contains(file))all.append(file);
  for(const auto &file:all){
    if(stop())return fail(QStringLiteral("Cancelled"));
    bool ok=false;const auto before=bytesOf(file,&ok);bytes+=before.size();
    if(!ok||bytes>128*1024*1024)return fail(words("此次操作超过 128 MB 或文件不可读，请分批处理。","The operation exceeds 128 MB or a file is unreadable. Use smaller batches."));
    auto after=before;const bool selected=mapping.contains(file);const auto dest=mapping.value(file,file);
    if(operation==ProjectTransfer::Move&&texts.contains(file)){
      const bool bom=before.startsWith("\xef\xbb\xbf");const auto body=bom?before.mid(3):before;const auto text=QString::fromUtf8(body);
      if(text.toUtf8()!=body)return fail(words("请先将文件保存为 UTF-8：","Save this file as UTF-8 first: ")+file);
      after=updatePaths(text,file,dest,root,mainFile.isEmpty()?root:QFileInfo(mapping.value(mainFile,mainFile)).absolutePath(),mapping).toUtf8();if(bom)after.prepend("\xef\xbb\xbf");
    }
    if(selected){
      if(operation!=ProjectTransfer::Copy)plan.files.append({file,before,{},true,false});
      if(operation!=ProjectTransfer::Remove)plan.files.append({dest,{},after,false,true});
    }else if(before!=after)plan.files.append({file,before,after,true,true});
  }
  if(operation==ProjectTransfer::Remove)plan.warnings.append(words("文稿中的文件引用保留；删除后可从“操作”恢复。","References in documents are preserved. Restore removed files from Actions."));
  includeCreatedParents(plan);return plan;
}
