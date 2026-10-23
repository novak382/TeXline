#include "ResearchPublication.h"
#include "ResearchBuild.h"
#include "utils/FilePath.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLockFile>
#include <QSaveFile>
#include <QUuid>
#include <QSet>

namespace Research {
namespace {
QString canonical(const QString &p){return Tw::Utils::canonicalSourcePath(p);}
QString hash(const QString &p){QFile f(p);if(!f.open(QIODevice::ReadOnly))return {};QCryptographicHash h(QCryptographicHash::Sha256);return h.addData(&f)?QString::fromLatin1(h.result().toHex()):QString();}
QJsonObject readJson(const QString &p){QFile f(p);return f.open(QIODevice::ReadOnly)&&f.size()<4*1024*1024?QJsonDocument::fromJson(f.readAll()).object():QJsonObject();}
bool writeJson(const QString &p,const QJsonObject &o){QSaveFile f(p);const auto bytes=QJsonDocument(o).toJson();return f.open(QIODevice::WriteOnly)&&f.write(bytes)==bytes.size()&&f.commit();}
bool copy(const QString &a,const QString &b){QFile in(a);QSaveFile out(b);if(!in.open(QIODevice::ReadOnly)||!out.open(QIODevice::WriteOnly))return false;while(!in.atEnd()){const auto bytes=in.read(1024*1024);if(bytes.isEmpty()&&in.error()!=QFileDevice::NoError)return false;if(out.write(bytes)!=bytes.size())return false;}return out.commit();}
bool within(const QString &p,const QString &dir){return canonical(p).startsWith(canonical(dir)+QLatin1Char('/'),Qt::CaseInsensitive);}
QString destination(const QString &root,const QString &relative){
 const QString clean=QDir::cleanPath(relative);if(clean.isEmpty()||clean==QStringLiteral(".")||clean==QStringLiteral("..")||clean.startsWith(QStringLiteral("../"))||QDir::isAbsolutePath(clean))return {};
 const QString base=QFileInfo(canonical(root)).absolutePath();QString path=QDir(base).filePath(clean);
 for(QString check=path;check.size()>base.size();check=QFileInfo(check).absolutePath()) {const QFileInfo info(check);if(info.isSymLink()||info.isJunction())return {};}
 return within(path,base)?path:QString();
}
QString journalTarget(const QString &root,const QJsonObject &row){
 if(!row.value(QStringLiteral("metadata")).toBool())return destination(root,row.value(QStringLiteral("name")).toString());
 const QString marker=QFileInfo(root).completeBaseName()+QStringLiteral(".pdf.research.json");
 if(row.value(QStringLiteral("name")).toString()!=marker)return {};
 return pdfMetadataFile(QFileInfo(root).absoluteDir().filePath(QFileInfo(root).completeBaseName()+QStringLiteral(".pdf")));
}
void retireLegacyMetadata(const QString &root){
 const QString pdf=QFileInfo(root).absoluteDir().filePath(QFileInfo(root).completeBaseName()+QStringLiteral(".pdf"));
 const QString legacy=pdf+QStringLiteral(".research.json");const QFileInfo info(legacy);
 if(!info.isFile()||info.isSymLink()||info.size()>2*1024*1024)return;
 const auto object=readJson(legacy);
 if(object.value(QStringLiteral("version")).toInt()!=1||object.value(QStringLiteral("boundRoot")).toString()!=canonical(root)||object.value(QStringLiteral("pdfHash")).toString().size()!=64||!object.value(QStringLiteral("inputs")).isObject())return;
 const QString before=hash(legacy),directory=buildCacheDirectory(root)+QStringLiteral("/legacy-metadata");
 if(before.isEmpty()||!QDir().mkpath(directory))return;
 const QString backup=directory+QLatin1Char('/')+before+QStringLiteral(".json");
 if((hash(backup)==before||copy(legacy,backup))&&hash(backup)==before&&hash(legacy)==before)QFile::remove(legacy);
}
bool current(const QJsonObject &inputs){if(inputs.isEmpty())return false;for(auto i=inputs.begin();i!=inputs.end();++i)if(hash(i.key())!=i.value().toObject().value(QStringLiteral("hash")).toString())return false;return true;}
QStringList recorded(const QString &file,const QString &prefix,const QString &cwd){QFile f(file);QStringList result;if(!f.open(QIODevice::ReadOnly)||f.size()>4*1024*1024)return result;for(const auto &line:QString::fromUtf8(f.readAll()).split(QLatin1Char('\n')))if(line.startsWith(prefix)){const QString name=line.mid(prefix.size()).trimmed();result.append(canonical(QDir(cwd).absoluteFilePath(name)));}return result;}
// A durable journal records only safe, relative destinations and private backups.
bool restore(const QString &root,const QJsonObject &journal,QString *error){
 const QString transaction=journal.value(QStringLiteral("transaction")).toString();
 if(journal.value(QStringLiteral("root")).toString()!=canonical(root)||journal.value(QStringLiteral("files")).toArray().isEmpty()||!within(transaction,buildCacheDirectory(root)+QStringLiteral("/publication"))){if(error)*error=QStringLiteral("发布回退记录无法确认，备份与现有文件均保留。");return false;}
 bool ok=true;const auto rows=journal.value(QStringLiteral("files")).toArray();
 for(const auto &value:rows){const auto row=value.toObject();const QString target=journalTarget(root,row),old=row.value(QStringLiteral("oldHash")).toString(),now=hash(target),fresh=row.value(QStringLiteral("newHash")).toString();
  if(target.isEmpty()||(QFileInfo::exists(target)&&now.isEmpty())){ok=false;continue;}if(now==old)continue;
  if(!now.isEmpty()&&now!=fresh){ok=false;continue;} // Never undo another writer's edit.
  if(row.value(QStringLiteral("existed")).toBool()){const QString from=QDir(transaction).filePath(row.value(QStringLiteral("backup")).toString());if(!within(from,transaction)||hash(from)!=old||!copy(from,target))ok=false;}
  else if(QFileInfo::exists(target)&&!QFile::remove(target))ok=false;
 }
 if(!ok&&error)*error=QStringLiteral("目录更新中断，回退尚未完成；缓存与备份保留，请释放文件占用后重试。");return ok;
}
}

QJsonObject generatedBuildArtifacts(const BuildJob &job){
 QJsonObject result;const QString stem=QFileInfo(job.root).completeBaseName();
 auto files=recorded(QDir(job.outputDir).filePath(stem+QStringLiteral(".fls")),QStringLiteral("OUTPUT "),QFileInfo(job.root).absolutePath());
 files.append(job.logFile);
 for(const auto &file:files){const QFileInfo info(file);const auto suffix=info.suffix().toLower();if((suffix!=QStringLiteral("aux")&&suffix!=QStringLiteral("out")&&suffix!=QStringLiteral("log"))||!info.isFile()||info.isSymLink()||!within(file,job.outputDir)||info.lastModified().toMSecsSinceEpoch()<job.startedAt)continue;
  const QString relative=QDir(job.outputDir).relativeFilePath(file);if(destination(job.root,relative).isEmpty())continue;const QString value=hash(file);if(!value.isEmpty())result.insert(relative,QJsonObject{{QStringLiteral("hash"),value},{QStringLiteral("file"),file}});
 }return result;
}

bool publishGeneratedPreview(const QString &root,QString *error,const std::function<bool()> &cancelled){
 auto fail=[&](const QString &s){if(error)*error=s;return false;};
 const QString base=buildCacheDirectory(root),pending=base+QStringLiteral("/publish-pending.json");QDir().mkpath(base);
 QLockFile lock(base+QStringLiteral("/publish.lock"));lock.setStaleLockTime(0);if(!lock.tryLock(0))return fail(QStringLiteral("另一项目录更新正在进行，缓存已保留，请稍后重试。"));
 if(QFileInfo::exists(pending)){
  const auto previous=readJson(pending);const auto files=previous.value(QStringLiteral("files")).toArray();bool complete=previous.value(QStringLiteral("root")).toString()==canonical(root)&&!files.isEmpty()&&within(previous.value(QStringLiteral("transaction")).toString(),base+QStringLiteral("/publication"));
  for(const auto &value:files){const auto row=value.toObject();const QString target=journalTarget(root,row),expected=row.value(QStringLiteral("newHash")).toString();if(target.isEmpty()||(expected.isEmpty()?QFileInfo::exists(target):hash(target)!=expected))complete=false;}
  if(!complete&&!restore(root,previous,error))return false;if(!QFile::remove(pending))return fail(QStringLiteral("回退记录无法更新，未写入论文目录。"));
 }
 auto meta=currentBuildMetadata(root);const auto inputs=meta.value(QStringLiteral("inputs")).toObject();
 const QString certifiedRoot=meta.value(QStringLiteral("source")).toString();
 auto valid=[&]{return !(cancelled&&cancelled())&&certifiedRoot==canonical(root)&&current(inputs);};
 if(!valid())return fail(QStringLiteral("当前源码尚未保存或已变化，未更新论文目录。"));
 const QString stem=QFileInfo(root).completeBaseName(),pdf=base+QStringLiteral("/preview/")+stem+QStringLiteral(".pdf");
 QJsonObject sources{{stem+QStringLiteral(".pdf"),QJsonObject{{QStringLiteral("file"),pdf},{QStringLiteral("hash"),meta.value(QStringLiteral("pdfHash"))}}}};
 const QString sync=meta.value(QStringLiteral("syncSuffix")).toString(),syncHash=meta.value(QStringLiteral("syncHash")).toString();
 if(!syncHash.isEmpty()){if(sync!=QStringLiteral(".synctex.gz")&&sync!=QStringLiteral(".synctex"))return fail(QStringLiteral("SyncTeX 配对无法确认，未更新论文目录。"));sources.insert(stem+sync,QJsonObject{{QStringLiteral("file"),pdf.left(pdf.size()-4)+sync},{QStringLiteral("hash"),syncHash}});}
 const auto extras=meta.value(QStringLiteral("generated")).toObject();for(auto it=extras.begin();it!=extras.end();++it)if(!inputs.contains(canonical(destination(root,it.key()))))sources.insert(it.key(),it.value());
 const QString transaction=base+QStringLiteral("/publication/")+QUuid::createUuid().toString(QUuid::Id128);if(!QDir().mkpath(transaction))return fail(QStringLiteral("发布备份无法创建，未更新论文目录。"));
 QJsonArray rows;int index=0;
 auto stage=[&](const QString &name,const QString &from,const QString &expected,bool internal=false){
  const QString target=internal?journalTarget(root,QJsonObject{{QStringLiteral("name"),name},{QStringLiteral("metadata"),true}}):destination(root,name);const QString part=QString::number(index++);if(target.isEmpty()||!within(from,base)||expected.size()!=64||!QDir().mkpath(QFileInfo(target).absolutePath()))return false;
  const QString old=hash(target);if(QFileInfo::exists(target)&&old.isEmpty())return false;
  if(old==expected)return true;
  if(!copy(from,QDir(transaction).filePath(part+QStringLiteral(".new")))||hash(QDir(transaction).filePath(part+QStringLiteral(".new")))!=expected)return false;
  const QString backup=part+QStringLiteral(".old");if(!old.isEmpty()&&(!copy(target,QDir(transaction).filePath(backup))||hash(QDir(transaction).filePath(backup))!=old))return false;
  rows.append(QJsonObject{{QStringLiteral("name"),name},{QStringLiteral("oldHash"),old},{QStringLiteral("newHash"),expected},{QStringLiteral("existed"),!old.isEmpty()},{QStringLiteral("backup"),backup},{QStringLiteral("staged"),part+QStringLiteral(".new")},{QStringLiteral("metadata"),internal}});return true;
 };
 for(auto it=sources.begin();it!=sources.end();++it){const auto file=it.value().toObject();if(!valid()||!stage(it.key(),file.value(QStringLiteral("file")).toString(),file.value(QStringLiteral("hash")).toString()))return fail(QStringLiteral("产物暂时被占用或已变化，编译成功但未更新论文目录；缓存保留。"));}
 for(const auto &suffix:{QStringLiteral(".synctex"),QStringLiteral(".synctex.gz")}) {
  if(!syncHash.isEmpty()&&suffix==sync)continue;
  const QString alternate=stem+suffix,target=destination(root,alternate);
  if(target.isEmpty()||inputs.contains(canonical(target)))return fail(QStringLiteral("SyncTeX 目标是编译输入，未覆盖论文目录。"));
  if(QFileInfo::exists(target)){const QString old=hash(target),part=QString::number(index++);if(old.isEmpty()||!copy(target,QDir(transaction).filePath(part+QStringLiteral(".old")))||hash(QDir(transaction).filePath(part+QStringLiteral(".old")))!=old)return fail(QStringLiteral("旧 SyncTeX 暂时被占用，缓存保留。"));rows.append(QJsonObject{{QStringLiteral("name"),alternate},{QStringLiteral("oldHash"),old},{QStringLiteral("newHash"),QString()},{QStringLiteral("existed"),true},{QStringLiteral("backup"),part+QStringLiteral(".old")},{QStringLiteral("remove"),true}});}
 }
 // Keep the pair and provenance together; the marker is committed last.
 QJsonObject relative,external;const QDir dir(QFileInfo(root).absolutePath());
 for(auto it=inputs.begin();it!=inputs.end();++it){const auto name=dir.relativeFilePath(it.key());if(name.startsWith(QStringLiteral("../"))||QDir::isAbsolutePath(name))external.insert(it.key(),it.value());else relative.insert(name,it.value());}
 meta.insert(QStringLiteral("source"),QFileInfo(root).fileName());meta.insert(QStringLiteral("inputs"),relative);meta.insert(QStringLiteral("boundRoot"),canonical(root));meta.insert(QStringLiteral("externalDependencies"),external);meta.insert(QStringLiteral("externalInputs"),!external.isEmpty());meta.remove(QStringLiteral("outputDir"));meta.remove(QStringLiteral("generated"));
 auto previousShared=pdfMetadata(dir.filePath(stem+QStringLiteral(".pdf")));QJsonObject owned;
 if(previousShared.value(QStringLiteral("boundRoot")).toString()==canonical(root))owned=previousShared.value(QStringLiteral("publishedFiles")).toObject();
 for(auto it=sources.begin();it!=sources.end();++it)owned.insert(it.key(),it.value().toObject().value(QStringLiteral("hash")));meta.insert(QStringLiteral("publishedFiles"),owned);
 const QString marker=stem+QStringLiteral(".pdf.research.json"),markerSource=transaction+QStringLiteral("/marker");
 if(!writeJson(markerSource,meta)||!stage(marker,markerSource,hash(markerSource),true))return fail(QStringLiteral("发布记录无法保存，编译成功但未更新论文目录；缓存保留。"));
 const QJsonObject journal{{QStringLiteral("version"),2},{QStringLiteral("root"),canonical(root)},{QStringLiteral("transaction"),transaction},{QStringLiteral("files"),rows}};
 if(!writeJson(transaction+QStringLiteral("/manifest.json"),journal)||!valid()||!writeJson(pending,journal))return fail(QStringLiteral("发布记录无法保存或源码已变化，未更新论文目录。"));
 for(const auto &value:rows){const auto row=value.toObject();const QString target=journalTarget(root,row);
  if(!valid()||hash(target)!=row.value(QStringLiteral("oldHash")).toString()||(row.value(QStringLiteral("remove")).toBool()?!QFile::remove(target):!copy(QDir(transaction).filePath(row.value(QStringLiteral("staged")).toString()),target))){
   if(restore(root,journal,error)){QFile::remove(pending);return fail(QStringLiteral("编译成功但目录写回失败；上一份产物已回退，缓存保留。"));}return false;
  }
 }
 if(!valid()){if(!restore(root,journal,error))return false;QFile::remove(pending);return fail(QStringLiteral("发布期间源码已变化，未采用本次产物；缓存及回退记录保留。"));}
 if(!QFile::remove(pending))return fail(QStringLiteral("产物已写入，但发布记录尚未完成；请重试确认目录状态。"));
 auto committed=journal;committed.insert(QStringLiteral("committed"),true);writeJson(transaction+QStringLiteral("/manifest.json"),committed);
 retireLegacyMetadata(root);
 const QDir archive(base+QStringLiteral("/publication"));int retained=0;
 for(const auto &entry:archive.entryInfoList(QDir::Dirs|QDir::NoDotAndDotDot,QDir::Time)) {
  if(entry.isSymLink()||entry.isJunction())continue;const auto record=readJson(entry.filePath()+QStringLiteral("/manifest.json"));
  if(record.value(QStringLiteral("root")).toString()!=canonical(root)||!record.value(QStringLiteral("committed")).toBool())continue;
  if(++retained>2&&within(entry.filePath(),archive.absolutePath()))QDir(entry.filePath()).removeRecursively();
 }
 return true;
}

QJsonArray legacyArtifactPlan(const QString &root){
 const auto meta=currentBuildMetadata(root);const auto inputs=meta.value(QStringLiteral("inputs")).toObject();QJsonArray result;
 if(meta.value(QStringLiteral("source")).toString()!=canonical(root)||!current(inputs))return result;
 const QString folder=QFileInfo(root).absolutePath(),stem=QFileInfo(root).completeBaseName(),work=meta.value(QStringLiteral("outputDir")).toString();
 QSet<QString> protectedPaths;for(auto i=inputs.begin();i!=inputs.end();++i)protectedPaths.insert(canonical(i.key()).toCaseFolded());
 const auto recorder=QDir(work).filePath(stem+QStringLiteral(".fls"));
 for(const auto &prefix:{QStringLiteral("INPUT "),QStringLiteral("OUTPUT ")})for(const auto &file:recorded(recorder,prefix,folder)){
  QString mapped=file;if(within(file,work))mapped=destination(root,QDir(work).relativeFilePath(file));if(!mapped.isEmpty())protectedPaths.insert(canonical(mapped).toCaseFolded());
 }
 const auto shared=pdfMetadata(QDir(folder).filePath(stem+QStringLiteral(".pdf")));const auto owned=shared.value(QStringLiteral("boundRoot")).toString()==canonical(root)?shared.value(QStringLiteral("publishedFiles")).toObject():QJsonObject();
 QJsonObject evidence=owned;const QString legacy=QDir(folder).filePath(stem+QStringLiteral(".fls"));QFile oldRecorder(legacy);
 if(!QFileInfo(legacy).isSymLink()&&oldRecorder.open(QIODevice::ReadOnly)&&oldRecorder.size()<4*1024*1024){
  const auto text=QString::fromUtf8(oldRecorder.readAll());bool matchingDirectory=false;
  for(const auto &line:text.split(QLatin1Char('\n')))if(line.startsWith(QStringLiteral("PWD "))&&canonical(line.mid(4).trimmed())==canonical(folder))matchingDirectory=true;
  if(matchingDirectory)for(const auto &file:recorded(legacy,QStringLiteral("OUTPUT "),folder)){
   if(!within(file,folder)||QFileInfo(file).lastModified()>QFileInfo(legacy).lastModified())continue;
   QFile f(file);if(!f.open(QIODevice::ReadOnly))continue;const auto head=f.read(512);const QString suffix=QFileInfo(file).suffix().toLower();
   if((suffix==QStringLiteral("aux")&&(head.startsWith("\\relax")||head.startsWith("\\providecommand")))||(suffix==QStringLiteral("out")&&(head.isEmpty()||head.startsWith("\\BOOKMARK")))||(suffix==QStringLiteral("log")&&head.startsWith("This is ")&&head.contains("TeX")))evidence.insert(QDir(folder).relativeFilePath(file),hash(file));
  }
 }
 for(auto it=evidence.begin();it!=evidence.end();++it){const QString file=destination(root,it.key());const QString suffix=QFileInfo(file).suffix().toLower();
  if(file.isEmpty()||(suffix!=QStringLiteral("aux")&&suffix!=QStringLiteral("out")&&suffix!=QStringLiteral("log"))||protectedPaths.contains(canonical(file).toCaseFolded())||it.value().toString().size()!=64||hash(file)!=it.value().toString())continue;
  result.append(QJsonObject{{QStringLiteral("name"),it.key()},{QStringLiteral("hash"),it.value()},{QStringLiteral("bytes"),double(QFileInfo(file).size())}});
 }return result;
}

bool cleanLegacyArtifacts(const QString &root,const QJsonArray &plan,QString *backup,QString *error,const std::function<bool()> &cancelled){
 auto fail=[&](const QString &s){if(error)*error=s;return false;};if(plan.isEmpty())return fail(QStringLiteral("没有选中可整理的旧产物。"));
 QLockFile lock(buildCacheDirectory(root)+QStringLiteral("/publish.lock"));lock.setStaleLockTime(0);if(!lock.tryLock(0))return fail(QStringLiteral("目录正在更新，稍后再整理。"));
 if(QFileInfo::exists(buildCacheDirectory(root)+QStringLiteral("/publish-pending.json")))return fail(QStringLiteral("请先完成发布回退，再整理旧产物。"));
 const auto latest=legacyArtifactPlan(root);QJsonObject eligible;for(const auto &value:latest){const auto row=value.toObject();eligible.insert(row.value(QStringLiteral("name")).toString(),row.value(QStringLiteral("hash")));}
 for(const auto &value:plan){const auto row=value.toObject();if(row.value(QStringLiteral("hash"))!=eligible.value(row.value(QStringLiteral("name")).toString()))return fail(QStringLiteral("整理清单已变化，请重新查看。"));}
 const auto inputs=currentBuildMetadata(root).value(QStringLiteral("inputs")).toObject();
 const QString directory=buildCacheDirectory(root)+QStringLiteral("/cleanup/")+QUuid::createUuid().toString(QUuid::Id128);if(!QDir().mkpath(directory))return fail(QStringLiteral("无法创建备份，未整理任何文件。"));
 if(backup)*backup=directory;int index=0;QJsonArray rows;
 for(const auto &value:plan){auto row=value.toObject();const QString name=row.value(QStringLiteral("name")).toString(),file=destination(root,name),saved=QString::number(index++);
  if((cancelled&&cancelled())||!current(inputs)||file.isEmpty()||hash(file)!=row.value(QStringLiteral("hash")).toString()||!copy(file,QDir(directory).filePath(saved))||hash(QDir(directory).filePath(saved))!=row.value(QStringLiteral("hash")).toString())return fail(QStringLiteral("备份未完成或文件已变化，未整理任何文件。"));
  row.insert(QStringLiteral("backup"),saved);rows.append(row);
 }
 if(!writeJson(directory+QStringLiteral("/manifest.json"),QJsonObject{{QStringLiteral("root"),canonical(root)},{QStringLiteral("files"),rows}}))return fail(QStringLiteral("备份清单无法保存，未整理任何文件。"));
 QJsonArray removed;
 for(const auto &value:rows){const auto row=value.toObject();const QString file=journalTarget(root,row);
  if((cancelled&&cancelled())||!current(inputs)||file.isEmpty()||hash(file)!=row.value(QStringLiteral("hash")).toString()||!QFile::remove(file)){
   bool restored=true;for(const auto &old:removed){const auto r=old.toObject();const QString target=destination(root,r.value(QStringLiteral("name")).toString());if(QFileInfo::exists(target)||!copy(QDir(directory).filePath(r.value(QStringLiteral("backup")).toString()),target))restored=false;}
   return fail(restored?QStringLiteral("整理未完成；已处理文件已恢复，备份保留。"):QStringLiteral("整理中断；完整备份保留，请从备份恢复被占用的文件。"));
  }removed.append(row);
 }return true;
}
}
