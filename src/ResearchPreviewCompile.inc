// Session-scoped native preview reuse. Inspired by WasmTex's state reuse;
// no upstream implementation or runtime is embedded.
namespace {
bool insidePreview(const QString &file,const QString &directory) {
 return file.startsWith(directory+QLatin1Char('/'),Qt::CaseInsensitive);
}
void hashField(QCryptographicHash &hash,const QByteArray &value) {
 hash.addData(QByteArray::number(value.size()));hash.addData(QByteArray(1,':'));hash.addData(value);
}
// Include all generated state, including custom auxiliary files. PDF, recorder,
// log and SyncTeX describe this pass; they are not inputs to the next one.
QByteArray previewAuxiliaryState(const QString &directory) {
 QStringList files;QStringList folders{directory};int count=0;qint64 bytes=0;
 while(!folders.isEmpty()){
  const QDir folder(folders.takeLast());
  for(const auto &file:folder.entryInfoList(QDir::AllEntries|QDir::NoDotAndDotDot|QDir::Hidden|QDir::System,QDir::Name)){
   if(file.isSymLink()||file.isJunction())return {};
   if(file.isDir()){if(++count>1024)return {};folders.append(file.absoluteFilePath());continue;}
   const auto suffix=file.suffix().toLower(),name=file.fileName().toLower();
   if(QStringList{QStringLiteral("pdf"),QStringLiteral("log"),QStringLiteral("fls"),QStringLiteral("xdv"),QStringLiteral("dvi"),QStringLiteral("synctex")}.contains(suffix)||name.endsWith(QStringLiteral(".synctex.gz")))continue;
   bytes+=file.size();if(++count>1024||bytes>32*1024*1024)return {};files.append(file.absoluteFilePath());
  }
 }
 files.sort();QCryptographicHash hash(QCryptographicHash::Sha256);
 for(const auto &file:files){const auto digest=slideExportHash(file);if(digest.size()!=32)return {};hashField(hash,QDir(directory).relativeFilePath(file).toUtf8());hashField(hash,digest);}
 return hash.result();
}
bool previewNeedsRerun(const QString &log) {
 // Repeating TeX cannot satisfy a request for Biber/BibTeX or an index tool.
 static const QRegularExpression external(QStringLiteral(R"(please\s+(?:\(re\))?run\s+(?:biber|bibtex|makeindex|xindy|makeglossaries)\b)"),QRegularExpression::CaseInsensitiveOption);
 static const QRegularExpression package(QStringLiteral(R"(^Package\s+(\S+)\s+Warning:)"));
 QString actionable,continuation;bool waitingForTool=false;
 for(const auto &line:log.split(QLatin1Char('\n'))){
  if(external.match(line).hasMatch()){
   waitingForTool=true;const auto name=package.match(line).captured(1);continuation=name.isEmpty()?QString():QLatin1Char('(')+name+QLatin1Char(')');continue;
  }
  if(waitingForTool&&(line.trimmed().isEmpty()||(!continuation.isEmpty()&&line.startsWith(continuation))))continue;
  waitingForTool=false;actionable+=line+QLatin1Char('\n');
 }
 static const QRegularExpression rerun(QStringLiteral(R"((?:rerun\s+(?:to get|LaTeX|the)|please\s+(?:\(re\))?run\s+(?:(?:pdf|xe|lua)?latex|tex)\b|label\(s\) may have changed))"),QRegularExpression::CaseInsensitiveOption);
 return rerun.match(actionable).hasMatch();
}
}
struct SlidePreviewSession::State {
 std::mutex mutex;
 std::unique_ptr<QTemporaryDir> folder;
 QByteArray identity;
 QHash<QString,QByteArray> copies,externalInputs;
 bool valid{false};
 void clear(){valid=false;identity.clear();copies.clear();externalInputs.clear();if(folder){{ExportAliasCleanup cleanup{*folder};}folder.reset();}}
 ~State(){clear();}
 bool inputsUnchanged()const {
  for(auto it=copies.cbegin();it!=copies.cend();++it)if(slideExportHash(it.key())!=it.value())return false;
  for(auto it=externalInputs.cbegin();it!=externalInputs.cend();++it)if(slideExportHash(it.key())!=it.value())return false;
  return true;
 }
 bool recordInputs(const QString &recorder,const QString &workingDirectory) {
  externalInputs.clear();QFile file(recorder);if(!file.open(QIODevice::ReadOnly)||file.size()>4*1024*1024)return false;
  const auto lines=QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));bool any=false;QStringList external;QSet<QString> seenPaths,seenFiles;qint64 bytes=0;
  for(const auto &line:lines){
   if(!line.startsWith(QStringLiteral("INPUT ")))continue;any=true;
   // TeX's recorder repeats many inputs. Deduplicate before canonicalization:
   // resolving every repeated Windows path can cost more than hashing it.
   const auto name=line.mid(6).trimmed();if(seenPaths.contains(name))continue;seenPaths.insert(name);
   const QFileInfo input(QDir(workingDirectory).absoluteFilePath(name));const auto path=input.canonicalFilePath();
   if(path.isEmpty())return false;
   if(insidePreview(path,folder->path())||seenFiles.contains(path))continue;seenFiles.insert(path);
   bytes+=input.size();if(external.size()>=1024||bytes>128*1024*1024)return false;external.append(path);
  }
  for(const auto &path:external){const auto digest=slideExportHash(path);if(digest.size()!=32)return false;externalInputs.insert(path,digest);}
  return any;
 }
};
SlidePreviewSession::SlidePreviewSession():state(std::make_unique<State>()){}
SlidePreviewSession::~SlidePreviewSession()=default;
