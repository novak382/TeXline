#include "utils/FilePath.h"
#include "ResearchBuild.h"
#include "ResearchPublication.h"
#include "ResearchWorkflow.h"
#include "ResearchDependencies.h"
#include "Engine.h"
#include "TeXDocumentWindow.h"
#include "TWApp.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QHash>
#include <QTemporaryDir>
#include <QThread>
#include <QTextCodec>
#include <QMutex>
#include <QCache>

namespace Research {
namespace {
QString canonical(const QString &p) {return Tw::Utils::canonicalSourcePath(p);}
QString digest(const QByteArray &bytes) {return QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());}
QString hashFile(const QString &path) {
  QFile f(path);if(!f.open(QIODevice::ReadOnly))return {};
  QCryptographicHash h(QCryptographicHash::Sha256);if(!h.addData(&f))return {};return QString::fromLatin1(h.result().toHex());
}
QString testBuildCache;
QString cacheBase() {return testBuildCache.isEmpty()?QCoreApplication::applicationDirPath()+QStringLiteral("/settings/research-build"):testBuildCache;}
QString rootCache(const QString &root) {return cacheBase()+QLatin1Char('/')+digest(canonical(root).toUtf8()).left(32);}
QString metaFile(const QString &pdf) {return pdfMetadataFile(pdf);}
QJsonObject metadata(const QString &pdf) {return pdfMetadata(pdf);}
bool jsonWrite(const QString &path,const QJsonObject &object,const std::function<bool()> &retryAllowed = {}) {
  const QByteArray data=QJsonDocument(object).toJson();
  for(int attempt=0;;++attempt) {
    {
      QSaveFile f(path);
      if(!f.open(QIODevice::WriteOnly)||f.write(data)!=data.size())return false;
      if(f.commit())return true;
#ifdef Q_OS_WIN
      // A reader briefly lacking FILE_SHARE_DELETE can block only the atomic
      // rename. Keep the old commit marker; never fall back to direct writing.
      if(f.error()!=QFileDevice::RenameError||attempt>=4)return false;
#else
      return false;
#endif
    }
    QThread::msleep(20);
    if(retryAllowed&&!retryAllowed())return false;
  }
}
bool copyAtomic(const QString &from,const QString &to) {
  QFile input(from);if(!input.open(QIODevice::ReadOnly))return false;
  QSaveFile output(to);if(!output.open(QIODevice::WriteOnly))return false;
  while(!input.atEnd()) {const auto bytes=input.read(1024*1024);if(bytes.isEmpty()&&input.error()!=QFileDevice::NoError)return false;if(output.write(bytes)!=bytes.size())return false;}
  return output.commit();
}
QJsonObject fingerprints(const QStringList &files,const std::function<bool()> &cancelled = {}) {
  QJsonObject result;
  for(const auto &path:files) {
    if(cancelled&&cancelled())return {};
    const QFileInfo info(path);const auto hash=hashFile(path);
    if(hash.isEmpty())result.insert(canonical(path),QJsonObject{{QStringLiteral("unreadable"),true}});
    else result.insert(canonical(path),QJsonObject{{QStringLiteral("hash"),hash},{QStringLiteral("size"),double(info.size())},{QStringLiteral("mtime"),double(info.lastModified().toMSecsSinceEpoch())}});
  }
  return result;
}
bool sameInputs(const QJsonObject &snapshot,bool consultEditors,bool duringBuild=false) {
  for(auto it=snapshot.begin();it!=snapshot.end();++it) {
    const QFileInfo info(it.key());const auto version=it.value().toObject();
    if(!info.isFile()||version.contains(QStringLiteral("unreadable")))return false;
    // A file can change while TeX is reading it and then be restored byte for
    // byte. Content equality alone cannot certify that build's output.
    if(duringBuild&&info.lastModified().toMSecsSinceEpoch()!=qint64(version.value(QStringLiteral("mtime")).toDouble()))return false;
    if(consultEditors) if(auto *w=TeXDocumentWindow::findDocument(it.key()))if(w->isModified())return false;
    // Text dependencies are always hashed; larger images use their recorded
    // size and time for the frequent, UI-thread status check.
    if(info.size()>4*1024*1024 && consultEditors && !duringBuild && !textDependency(it.key())) {
      if(info.size()!=qint64(version.value(QStringLiteral("size")).toDouble())||info.lastModified().toMSecsSinceEpoch()!=qint64(version.value(QStringLiteral("mtime")).toDouble()))return false;
    } else if(hashFile(it.key())!=version.value(QStringLiteral("hash")).toString())return false;
  }
  return !snapshot.isEmpty();
}
QJsonObject localInputs(const QString &pdf,const QJsonObject &meta) {
  QJsonObject result;
  if(managedPreview(pdf))return meta.value(QStringLiteral("inputs")).toObject();
  const QDir dir(QFileInfo(pdf).absolutePath());
  const auto relative=meta.value(QStringLiteral("inputs")).toObject();
  for(auto it=relative.begin();it!=relative.end();++it) {
    if(QDir::isAbsolutePath(it.key()))return {};
    const QString clean=QDir::cleanPath(it.key());
    if(clean==QStringLiteral("..")||clean.startsWith(QStringLiteral("../")))return {};
    result.insert(dir.absoluteFilePath(clean),it.value());
  }
  if(meta.value(QStringLiteral("externalInputs")).toBool()) {
    const QString source=dir.filePath(meta.value(QStringLiteral("source")).toString());
    if(meta.value(QStringLiteral("boundRoot")).toString()!=canonical(source))return {};
    const auto external=meta.value(QStringLiteral("externalDependencies")).toObject();
    if(external.isEmpty())return {};
    for(auto it=external.begin();it!=external.end();++it){if(!QDir::isAbsolutePath(it.key()))return {};result.insert(it.key(),it.value());}
  }
  return result;
}
bool pdfHashMatches(const QString &pdf,const QJsonObject &meta) {
  static QMutex mutex;QMutexLocker lock(&mutex);
  const QString expected=meta.value(QStringLiteral("pdfHash")).toString();if(expected.size()!=64)return false;
  // Published PDFs are immutable between atomic replacements. Reuse the hash
  // for status updates while the file's timestamp and size remain unchanged.
  struct Cached {qint64 size,time;QString hash;};static QHash<QString,Cached> cache;
  QFileInfo info(pdf);if(!info.isFile())return false;
  const auto found=cache.constFind(pdf);const qint64 time=info.lastModified().toMSecsSinceEpoch();
  if(found!=cache.cend()&&found->size==info.size()&&found->time==time)return found->hash==expected;
  const QString hash=hashFile(pdf);if(cache.size()>64)cache.clear();cache.insert(pdf,{info.size(),time,hash});return hash==expected;
}
}

static QStringList buildDependenciesWithSource(const QString &root,bool includeRecorded,const QHash<QString,QString> *sources=nullptr,const std::function<bool()> &cancelled={}) {
  QStringList files=sources?projectFiles(root,*sources,cancelled):projectFiles(root);if(files.isEmpty()&&QFileInfo(root).isFile())files.append(canonical(root));
  const QDir rootDir(QFileInfo(root).absolutePath());
  const QStringList textFiles=files;
  static const QRegularExpression resource(QStringLiteral(R"(\\(?:bibliography|bibliographystyle|addbibresource|includegraphics|documentclass|usepackage)\s*(?:\[[^\]]*\]\s*)?\{([^{}]+)\})"));
  for(const auto &file:textFiles) {
    if(cancelled&&cancelled())return {};
    QFile f(file);QString content;
    if(sources&&sources->contains(file))content=sources->value(file);
    else {if(!f.open(QIODevice::ReadOnly)||f.size()>4*1024*1024)continue;content=QString::fromUtf8(f.readAll());}
    const QString text=compiledTexText(content);
    auto matches=resource.globalMatch(text);
    while(matches.hasNext()) {
      const auto match=matches.next();const QString command=match.captured(0);
      for(QString name:match.captured(1).split(QLatin1Char(','))) {
        name=name.trimmed();if(name.contains(QLatin1Char('\\'))||name.isEmpty())continue;
        QStringList names{name};
        if(QFileInfo(name).suffix().isEmpty()) {
          QStringList extensions;
          if(command.startsWith(QStringLiteral("\\bibliographystyle")))extensions={QStringLiteral("bst")};
          else if(command.startsWith(QStringLiteral("\\bibliography"))||command.startsWith(QStringLiteral("\\addbibresource")))extensions={QStringLiteral("bib")};
          else if(command.startsWith(QStringLiteral("\\documentclass")))extensions={QStringLiteral("cls")};
          else if(command.startsWith(QStringLiteral("\\usepackage")))extensions={QStringLiteral("sty")};
          else extensions={QStringLiteral("pdf"),QStringLiteral("png"),QStringLiteral("jpg"),QStringLiteral("jpeg"),QStringLiteral("eps")};
          for(const auto &extension:extensions)names.append(name+QLatin1Char('.')+extension);
        }
        for(const auto &candidate:names) {const QString path=canonical(rootDir.filePath(candidate));if(QFileInfo(path).isFile()&&!files.contains(path))files.append(path);}
        if(command.startsWith(QStringLiteral("\\bibliography"))||command.startsWith(QStringLiteral("\\addbibresource"))) {
          const auto path=resolveDependency(name,file,root,QStringLiteral(".bib"));
          if(!path.isEmpty()&&!files.contains(path))files.append(path);
        }
      }
    }
  }
  // Retain dependencies learned from the last successful recorder file.
  if(includeRecorded)for(const auto &path:recordedDependencies(root))if(QFileInfo(path).isFile()&&!files.contains(path))files.append(path);
  return files;
}

QStringList buildDependencies(const QString &root,bool includeRecorded){return buildDependenciesWithSource(root,includeRecorded);}
QStringList buildDependencies(const QString &root,const QHash<QString,QString> &sources){return buildDependenciesWithSource(root,true,&sources);}

QStringList recordedDependencies(const QString &root) {
  const QString pdf=previewFile(root);if(pdf.isEmpty())return {};
  return localInputs(pdf,metadata(pdf)).keys();
}

namespace {
QStringList compilerDependencies(const BuildJob &job) {
  QStringList result;QSet<QString> consumed,visited;const QDir sourceDir(QFileInfo(job.root).absolutePath());
  const QString stem=job.outputDir+QLatin1Char('/')+QFileInfo(job.root).completeBaseName();
  // Recorder logs repeat fonts and distribution files hundreds of times.
  // Deduplicate before filesystem queries; only author inputs need tracking.
  const QString rootPrefix=QDir::fromNativeSeparators(sourceDir.absolutePath()).toLower()+QLatin1Char('/');
  auto append=[&](const QString &name){
    const QString absolute=QDir::cleanPath(sourceDir.filePath(name.trimmed()));
    QString key=QDir::fromNativeSeparators(absolute);
#ifdef Q_OS_WIN
    key=key.toCaseFolded();
#endif
    if(visited.contains(key))return;
    visited.insert(key);
    const QString lower=key.toLower();
    if((lower.contains(QStringLiteral("/texmf-dist/"))||lower.contains(QStringLiteral("/texmf-var/")))&&!lower.startsWith(rootPrefix))return;
    const auto path=canonical(absolute);if(QFileInfo(path).isFile()&&authoredDependency(path,job.root,job.outputDir)&&!result.contains(path))result.append(path);
  };
  QFile recorder(stem+QStringLiteral(".fls"));
  if(recorder.open(QIODevice::ReadOnly)&&recorder.size()<=4*1024*1024)for(const auto &line:QString::fromUtf8(recorder.readAll()).split(QLatin1Char('\n')))if(line.startsWith(QStringLiteral("INPUT "))) {
    const QString name=line.mid(6).trimmed();
    // Only bibliography outputs determine whether an old bibliography rule
    // remains active; other recorder entries never participate in this gate.
    if(name.endsWith(QStringLiteral(".bbl"),Qt::CaseInsensitive))consumed.insert(canonical(sourceDir.filePath(name)).toCaseFolded());
    append(name);
  }
  // TeX's recorder does not include files read by BibTeX/Biber. latexmk's
  // versioned database records those inputs separately from generated files.
  QFile database(stem+QStringLiteral(".fdb_latexmk"));
  if(database.open(QIODevice::ReadOnly)&&database.size()<=4*1024*1024) {
    const QString text=QString::fromUtf8(database.readAll());
    static const QRegularExpression input(QStringLiteral(R"fdb(^  "([^"]+)"\s+[0-9]+\s+[0-9]+\s+[^\s]+\s+")fdb"),QRegularExpression::MultilineOption);
    const QString version=text.section(QLatin1Char('\n'),0,0).trimmed();
    if(version==QStringLiteral("# Fdb version 3")||version==QStringLiteral("# Fdb version 4")) {
      static const QRegularExpression rule(QStringLiteral(R"fdb(^\["([^"]+)"\]\s+[0-9]+\s+"[^"]*"\s+"([^"]*)")fdb"));
      bool active=true;
      for(const auto &line:text.split(QLatin1Char('\n'))) {
        const auto header=rule.match(line);
        if(header.hasMatch()) {
          const auto name=header.captured(1);
          active=!(name.startsWith(QStringLiteral("bibtex "))||name.startsWith(QStringLiteral("biber ")))||
            consumed.contains(canonical(sourceDir.filePath(header.captured(2))).toCaseFolded());
          continue;
        }
        const auto entry=input.match(line);if(active&&entry.hasMatch())append(entry.captured(1));
      }
    }
  }
  return result;
}
}

bool usesBibliographySource(const QStringList &dependencies) {
  static const QRegularExpression declaration(QStringLiteral(R"(\\(?:bibliography|addbibresource)\b)"));
  for(const auto &path:dependencies) {
    const auto suffix=QFileInfo(path).suffix();
    if(suffix.compare(QStringLiteral("bib"),Qt::CaseInsensitive)==0)return true;
    if(suffix.compare(QStringLiteral("tex"),Qt::CaseInsensitive)!=0)continue;
    QFile file(path);if(!file.open(QIODevice::ReadOnly)||file.size()>4*1024*1024)return true;
    // A declared bibliography can live in BIBINPUTS or use a macro path.
    // Not finding a local .bib does not prove that the author supplied only .bbl.
    if(declaration.match(compiledTexText(QString::fromUtf8(file.readAll()))).hasMatch())return true;
  }
  return false;
}

static BuildJob prepareBuildWithSource(const QString &root,const Engine &engine,const QHash<QString,QString> *sources,const std::function<bool()> &cancelled) {
  BuildJob job;job.root=canonical(root);job.managed=engine.supportsLocalBuild();job.startedAt=QDateTime::currentMSecsSinceEpoch();
  if(sources)job.sourceTexts=*sources;
  auto stop=[&]{if(!cancelled||!cancelled())return false;job.cancelled=true;job.outputDir.clear();return true;};if(stop())return job;
  if(!job.managed) {
    const QString tool=QFileInfo(engine.program()).completeBaseName().toLower();
    if(tool==QStringLiteral("bibtex")||tool==QStringLiteral("biber")) {
      const QString cached=metadata(previewFile(root)).value(QStringLiteral("outputDir")).toString();
      if(cached.startsWith(rootCache(root)+QLatin1Char('/'))&&QDir(cached).exists()) {
        job.outputDir=cached;job.logFile=cached+QLatin1Char('/')+QFileInfo(root).completeBaseName()+QStringLiteral(".blg");
      }
    }
    return job;
  }
  const QString base=rootCache(root),engineKey=digest((engine.program()+engine.arguments().join(QLatin1Char('\n'))).toUtf8()).left(16);
  job.outputDir=base+QStringLiteral("/work-")+engineKey;
  job.previewFile=base+QStringLiteral("/preview/")+QFileInfo(root).completeBaseName()+QStringLiteral(".pdf");
  job.logFile=job.outputDir+QLatin1Char('/')+QFileInfo(root).completeBaseName()+QStringLiteral(".log");
  if(!QDir().mkpath(job.outputDir)||!QDir().mkpath(QFileInfo(job.previewFile).absolutePath())) {job.outputDir.clear();return job;}
  const QDir sourceDir(QFileInfo(root).absolutePath());
  const auto parts=sources?projectFiles(root,*sources,cancelled):projectFiles(root);if(stop())return job;
  for(const auto &file:parts) {
    if(stop())return job;
    const QString rel=sourceDir.relativeFilePath(QFileInfo(file).absolutePath());
    if(rel!=QStringLiteral("..")&&!rel.startsWith(QStringLiteral("../")))QDir().mkpath(QDir(job.outputDir).filePath(rel));
  }
  // Reuse the authored graph at completion; reparsing it there would block editing.
  job.authoredInputs=buildDependenciesWithSource(root,false,sources,cancelled);if(stop())return job;
  QStringList dependencies=job.authoredInputs;
  for(const auto &path:recordedDependencies(root))if(QFileInfo(path).isFile()&&!dependencies.contains(path))dependencies.append(path);
  for(const auto &path:compilerDependencies(job))if(!dependencies.contains(path))dependencies.append(path);
  const QString sourceBbl=canonical(sourceDir.filePath(QFileInfo(root).completeBaseName()+QStringLiteral(".bbl")));
  const QString cachedBbl=QDir(job.outputDir).filePath(QFileInfo(root).completeBaseName()+QStringLiteral(".bbl"));
  const bool hasBibSource=usesBibliographySource(dependencies);
  // An existing source-side .bbl otherwise makes latexmk choose and rewrite
  // that source-side target, even with -outdir. Bootstrap the cache first.
  // Without a .bib source, the .bbl is authored input and must follow edits.
  const bool seedBbl=QFileInfo(sourceBbl).isFile()&&(!QFileInfo(cachedBbl).isFile()||(!hasBibSource&&hashFile(cachedBbl)!=hashFile(sourceBbl)));
  if(seedBbl&&!dependencies.contains(sourceBbl))dependencies.append(sourceBbl);
  job.inputs=fingerprints(dependencies,cancelled);if(stop())return job;
  if(seedBbl&&!copyAtomic(sourceBbl,cachedBbl))job.outputDir.clear();
  return job;
}

BuildJob prepareBuild(const QString &root,const Engine &engine){
  if(auto *app=qobject_cast<TWApp*>(QCoreApplication::instance()))setDependencyBinaryPaths(app->getBinaryPaths());
  return prepareBuildWithSource(root,engine,nullptr,{});
}
BuildJob prepareBuild(const QString &root,const Engine &engine,const QHash<QString,QString> &sources,const std::function<bool()> &cancelled){return prepareBuildWithSource(root,engine,&sources,cancelled);}

static int finishBuildImpl(const BuildJob &job,bool success,bool consultEditors,const std::function<bool(const QStringList &)> &inputsChanged) {
  if(!job.managed||!success)return 0;
  auto matches=[&](const QJsonObject &inputs){
    return !(inputsChanged&&inputsChanged(inputs.keys())) &&
      sameInputs(inputs,consultEditors,true) &&
      !(inputsChanged&&inputsChanged(inputs.keys()));
  };
  if(!matches(job.inputs))return -1;
  const QString stem=QFileInfo(job.root).completeBaseName(),output=job.outputDir+QLatin1Char('/')+stem;
  QFile pdf(output+QStringLiteral(".pdf"));if(!pdf.open(QIODevice::ReadOnly)||!pdf.read(5).startsWith("%PDF-"))return -2;pdf.close();
  const auto actual=compilerDependencies(job);
  QStringList files=job.inputs.keys();
  for(const auto &path:actual)if(!files.contains(path)) {
    if(QFileInfo(path).lastModified().toMSecsSinceEpoch()>=job.startedAt)return -1;
    files.append(path);
  }
  // A complete recorder lets a new successful revision forget dependencies
  // removed from the project. Keep the pre-build snapshot for safety above,
  // but do not accumulate stale files forever in the published revision.
  if(actual.contains(canonical(job.root))) {
    files=job.authoredInputs;
    for(const auto &path:actual)if(!files.contains(path))files.append(path);
  }
  auto inputs=fingerprints(files);
  if(!matches(inputs))return -1;
  // Keep the source that produced this PDF. It is read-only provenance, never
  // written back to authored files. Bound the optional snapshot independently
  // of binary dependencies; missing snapshots leave only that file unmapped.
  qint64 snapshotBytes=0;
  for(const auto &file:inputs.keys()) {
    if(!textDependency(file))continue;
    QFile f(file);if(!f.open(QIODevice::ReadOnly)||f.size()>4*1024*1024)continue;
    const auto bytes=f.readAll();auto entry=inputs.value(file).toObject();
    if(digest(bytes)!=entry.value(QStringLiteral("hash")).toString())return -1;
    QString text;
    if(job.sourceTexts.contains(file))text=job.sourceTexts.value(file);
    else {QTextCodec::ConverterState state;text=QTextCodec::codecForName("UTF-8")->toUnicode(bytes.constData(),bytes.size(),&state);if(state.invalidChars)continue;}
    text.replace(QStringLiteral("\r\n"),QStringLiteral("\n"));text.replace(QLatin1Char('\r'),QLatin1Char('\n'));
    if(text.startsWith(QChar(0xfeff)))text.remove(0,1);
    const auto utf8=text.toUtf8();snapshotBytes+=utf8.size();if(snapshotBytes>16*1024*1024)break;
    entry.insert(QStringLiteral("sourceText"),QString::fromLatin1(qCompress(utf8).toBase64()));
    entry.insert(QStringLiteral("sourceTextHash"),digest(utf8));inputs.insert(file,entry);
  }
  auto generated=generatedBuildArtifacts(job);
  const auto &authored=job.authoredInputs;
  for(const auto &name:generated.keys()) {
    const QString target=canonical(QFileInfo(job.root).absoluteDir().filePath(name));
    if(authored.contains(target))generated.remove(name);
    else inputs.remove(target); // An ambient generated aux is not authored input.
  }
  // Certify the union at each transaction boundary. Shared dependencies must
  // agree with both snapshots, then need only one physical read per boundary.
  // No cross-boundary cache: external edits and same-size replacements remain
  // observable before and after publishing the PDF/SyncTeX pair.
  QJsonObject certification=job.inputs;
  for(auto it=inputs.begin();it!=inputs.end();++it){
    if(certification.contains(it.key())){
      const auto old=certification.value(it.key()).toObject(),now=it.value().toObject();
      for(const auto &field:{QStringLiteral("hash"),QStringLiteral("size"),QStringLiteral("mtime")})
        if(old.value(field)!=now.value(field))return -1;
    }
    certification.insert(it.key(),it.value());
  }
  const QString sync=output+QStringLiteral(".synctex.gz"),plainSync=output+QStringLiteral(".synctex");
  const QString sourceSync=QFileInfo(sync).isFile()?sync:plainSync;
  const QString syncSuffix=QFileInfo(sync).isFile()?QStringLiteral(".synctex.gz"):QStringLiteral(".synctex");
  const QString previewStem=job.previewFile.left(job.previewFile.size()-4);
  // Private metadata is the commit marker. A half-updated pair fails verification and
  // cannot be used for source jumps; the viewer keeps its last loaded PDF.
  const QString syncHash=QFileInfo(sourceSync).isFile()?hashFile(sourceSync):QString();
  QTemporaryDir backup(QFileInfo(job.previewFile).absolutePath()+QStringLiteral("/transaction-XXXXXX"));
  if(!backup.isValid())return -2;
  if(!QDir().mkpath(QFileInfo(metaFile(job.previewFile)).absolutePath()))return -2;
  const QStringList targets{job.previewFile,previewStem+QStringLiteral(".synctex.gz"),previewStem+QStringLiteral(".synctex"),metaFile(job.previewFile)};
  QSet<QString> existing;
  for(const auto &target:targets)if(QFileInfo::exists(target)) {
    existing.insert(target);if(!QFile::copy(target,backup.filePath(QFileInfo(target).fileName())))return -2;
  }
  auto rollback=[&](){for(const auto &target:targets) {
    if(existing.contains(target))copyAtomic(backup.filePath(QFileInfo(target).fileName()),target);
    else QFile::remove(target);
  }return -2;};
  if(!matches(certification))return -1;
  if(!copyAtomic(output+QStringLiteral(".pdf"),job.previewFile))return rollback();
  if(!syncHash.isEmpty()&&!copyAtomic(sourceSync,previewStem+syncSuffix))return rollback();
  QJsonObject meta{{QStringLiteral("version"),1},{QStringLiteral("source"),job.root},{QStringLiteral("outputDir"),job.outputDir},{QStringLiteral("pdfHash"),hashFile(job.previewFile)},{QStringLiteral("syncHash"),syncHash},{QStringLiteral("syncSuffix"),syncSuffix},{QStringLiteral("inputs"),inputs},{QStringLiteral("builtAt"),QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}};
  meta.insert(QStringLiteral("generated"),generated);
  QJsonObject auxiliary;qint64 auxiliaryBytes=0;
  for(const auto &name:generated.keys())if(name.endsWith(QStringLiteral(".aux"))) {
    QFile f(generated.value(name).toObject().value(QStringLiteral("file")).toString());
    if(!f.open(QIODevice::ReadOnly)||f.size()>1024*1024)continue;const auto bytes=f.readAll();auxiliaryBytes+=bytes.size();if(auxiliaryBytes>4*1024*1024)break;
    if(digest(bytes)==generated.value(name).toObject().value(QStringLiteral("hash")).toString())auxiliary.insert(name,QString::fromLatin1(qCompress(bytes).toBase64()));
  }
  meta.insert(QStringLiteral("compiledAux"),auxiliary);
  // Freeze optional Beamer navigation in the same commit marker as the full
  // PDF/SyncTeX pair. Never read a later work-directory .nav for an old preview.
  QFile navigation(output+QStringLiteral(".nav"));
  if(navigation.open(QIODevice::ReadOnly)&&navigation.size()<=256*1024) {
    const auto bytes=navigation.readAll();
    if(bytes.contains("\\beamer@documentpages"))meta.insert(QStringLiteral("beamerNavigation"),QString::fromUtf8(bytes));
  }
  if(!matches(certification)){rollback();return -1;}
  bool retryInputsUnchanged=true;
  if(!jsonWrite(metaFile(job.previewFile),meta,[&]{
    // Source can change while the Windows reader is releasing its lease.
    // Recheck the original build snapshot and all certified dependencies.
    retryInputsUnchanged=matches(certification);
    return retryInputsUnchanged;
  })) {rollback();return retryInputsUnchanged?-2:-1;}
  if(!matches(certification)){rollback();return -1;}
  QFile::remove(previewStem+(syncSuffix==QStringLiteral(".synctex.gz")?QStringLiteral(".synctex"):QStringLiteral(".synctex.gz")));
  return 1;
}
int finishBuild(const BuildJob &job,bool success){return finishBuildImpl(job,success,true,{});}
int finishBuildInBackground(const BuildJob &job,bool success,const std::function<bool(const QStringList &)> &inputsChanged){return finishBuildImpl(job,success,false,inputsChanged);}

bool buildContentUnchanged(const BuildJob &job) {
  if (!job.managed) return false;
  for (auto it = job.inputs.begin(); it != job.inputs.end(); ++it)
    if (auto *window = TeXDocumentWindow::findDocument(it.key()))
      if (window->isModified()) return false;
  // Recheck certification hashes every dependency, including large images.
  // The cheap metadata path is only suitable for frequent UI status checks.
  return sameInputs(job.inputs, false);
}

void setBuildCacheForTests(const QString &directory) {testBuildCache=directory;}
bool managedPreview(const QString &pdf) {return canonical(pdf).startsWith(canonical(cacheBase())+QLatin1Char('/'));}
QString previewFile(const QString &root) {
  const QString local=rootCache(root)+QStringLiteral("/preview/")+QFileInfo(root).completeBaseName()+QStringLiteral(".pdf");
  if(QFileInfo(local).isFile()&&metadata(local).value(QStringLiteral("source")).toString()==canonical(root))return local;
  const QString shared=QFileInfo(root).absoluteDir().filePath(QFileInfo(root).completeBaseName()+QStringLiteral(".pdf"));
  return QFileInfo(shared).isFile()?shared:QString();
}
QString pdfSourceRoot(const QString &pdf) {
  const auto meta=metadata(pdf);if(!pdfHashMatches(pdf,meta))return {};
  const QString root=meta.value(QStringLiteral("source")).toString();
  if(managedPreview(pdf))return QFileInfo(root).isFile()?canonical(root):QString();
  if(root!=QFileInfo(root).fileName())return {};
  const QString resolved=QFileInfo(pdf).absoluteDir().filePath(root);return QFileInfo(resolved).isFile()?canonical(resolved):QString();
}
PdfRevision pdfRevision(const QString &pdf,const QString &root) {
  const auto meta=metadata(pdf);if(meta.isEmpty())return PdfRevision::Unknown;
  if(!pdfHashMatches(pdf,meta))return PdfRevision::Unpaired;
  if(meta.value(QStringLiteral("externalInputs")).toBool()&&!managedPreview(pdf)) {
    const QString bound=canonical(QFileInfo(pdf).absoluteDir().filePath(meta.value(QStringLiteral("source")).toString()));
    if(meta.value(QStringLiteral("boundRoot")).toString()!=bound||meta.value(QStringLiteral("externalDependencies")).toObject().isEmpty())return PdfRevision::Unknown;
  }
  const auto inputs=localInputs(pdf,meta);
  Q_UNUSED(root);
  return sameInputs(inputs,true)?PdfRevision::Current:PdfRevision::Stale;
}
bool pdfPairValid(const QString &pdf) {
  if(pdfRevision(pdf)!=PdfRevision::Current)return false;
  return pdfCompiledPairValid(pdf);
}
bool pdfCompiledPairValid(const QString &pdf) {
  const auto meta=metadata(pdf);const QString suffix=meta.value(QStringLiteral("syncSuffix")).toString();
  if(suffix!=QStringLiteral(".synctex.gz")&&suffix!=QStringLiteral(".synctex"))return false;
  const QString expected=meta.value(QStringLiteral("syncHash")).toString();
  return !localInputs(pdf,meta).isEmpty()&&meta.value(QStringLiteral("pdfHash")).toString().size()==64&&pdfHashMatches(pdf,meta)&&expected.size()==64&&hashFile(pdf.left(pdf.size()-4)+suffix)==expected;
}
QHash<QString,QString> pdfCompiledSources(const QString &pdf) {
  const auto meta=metadata(pdf);const auto inputs=localInputs(pdf,meta);
  const QString identity=canonical(pdf)+meta.value(QStringLiteral("pdfHash")).toString()+meta.value(QStringLiteral("syncHash")).toString()+meta.value(QStringLiteral("builtAt")).toString();
  static QMutex mutex;static QCache<QString,QHash<QString,QString>> cache(32768);
  {QMutexLocker lock(&mutex);if(auto *snapshot=cache.object(identity))return *snapshot;}
  QHash<QString,QString> result;qint64 total=0;
  for(auto it=inputs.begin();it!=inputs.end();++it) {
    if(!textDependency(it.key()))continue;const auto entry=it.value().toObject();QByteArray bytes;
    const auto encoded=entry.value(QStringLiteral("sourceText")).toString().toLatin1();
    if(!encoded.isEmpty()) {
      const auto packed=QByteArray::fromBase64(encoded);if(packed.size()<4)continue;
      quint32 size=0;for(int i=0;i<4;++i)size=(size<<8)|quint8(packed[i]);if(size>4*1024*1024||total+size>16*1024*1024)continue;
      bytes=qUncompress(packed);if(digest(bytes)!=entry.value(QStringLiteral("sourceTextHash")).toString())continue;
    } else {
      QFile file(it.key());if(!file.open(QIODevice::ReadOnly)||file.size()>4*1024*1024||total+file.size()>16*1024*1024)continue;
      bytes=file.readAll();if(digest(bytes)!=entry.value(QStringLiteral("hash")).toString())continue;
    }
    total+=bytes.size();QTextCodec::ConverterState state;QString text=QTextCodec::codecForName("UTF-8")->toUnicode(bytes.constData(),bytes.size(),&state);if(state.invalidChars)continue;
    text.replace(QStringLiteral("\r\n"),QStringLiteral("\n"));text.replace(QLatin1Char('\r'),QLatin1Char('\n'));if(text.startsWith(QChar(0xfeff)))text.remove(0,1);result.insert(canonical(it.key()),text);
  }
  // Also retains a verified legacy revision loaded before an edit. New build
  // markers persist snapshots, so reopening a saved draft works as well.
  if(!result.isEmpty()){int cost=0;for(const auto &text:result)cost+=int(text.size()*2/1024)+1;QMutexLocker lock(&mutex);cache.insert(identity,new QHash<QString,QString>(result),qMax(1,cost));}
  return result;
}
QHash<QString,QString> pdfCompiledAux(const QString &pdf) {
  QHash<QString,QString> result;const auto meta=metadata(pdf);const auto rows=meta.value(QStringLiteral("compiledAux")).toObject();
  const QString root=pdfSourceRoot(pdf),output=meta.value(QStringLiteral("outputDir")).toString();qint64 total=0;
  for(auto it=rows.begin();it!=rows.end();++it) {
    if(QDir::isAbsolutePath(it.key())||QDir::cleanPath(it.key()).startsWith(QStringLiteral("..")))continue;
    const auto packed=QByteArray::fromBase64(it.value().toString().toLatin1());if(packed.size()<4)continue;quint32 size=0;for(int i=0;i<4;++i)size=(size<<8)|quint8(packed[i]);if(size>1024*1024||total+size>4*1024*1024)continue;
    const auto bytes=qUncompress(packed);total+=bytes.size();const QString text=QString::fromUtf8(bytes);
    if(!output.isEmpty())result.insert(canonical(QDir(output).filePath(it.key())),text);
    if(!root.isEmpty())result.insert(canonical(QFileInfo(root).absoluteDir().filePath(it.key())),text);
  }
  return result;
}
QString buildCacheDirectory(const QString &root) { return rootCache(root); }
QString pdfMetadataFile(const QString &pdf) {
  if(pdf.isEmpty())return {};
  const QFileInfo file(pdf);const QString directory=QFileInfo(file.absolutePath()).canonicalFilePath();
  QString identity=file.exists()?canonical(pdf):(directory.isEmpty()?file.absoluteFilePath():QDir(directory).filePath(file.fileName()));
#ifdef Q_OS_WIN
  identity=identity.toCaseFolded();
#endif
  return cacheBase()+QStringLiteral("/pdf-metadata/")+digest(identity.toUtf8())+QStringLiteral(".json");
}
QString pdfMetadataReadFile(const QString &pdf) {
  if(pdf.isEmpty())return {};
  const QString local=pdfMetadataFile(pdf),legacy=pdf+QStringLiteral(".research.json");
  return !QFileInfo::exists(local)&&QFileInfo(legacy).isFile()?legacy:local;
}
QJsonObject pdfMetadata(const QString &pdf) {
  // Read old sidecars until successful publication archives and retires them.
  // An unreadable private marker never falls back to older certification.
  QFile file(pdfMetadataReadFile(pdf));
  if(!file.open(QIODevice::ReadOnly)||file.size()>32*1024*1024)return {};
  return QJsonDocument::fromJson(file.readAll()).object();
}
QJsonObject currentBuildMetadata(const QString &root) {
  return metadata(rootCache(root)+QStringLiteral("/preview/")+QFileInfo(root).completeBaseName()+QStringLiteral(".pdf"));
}
bool publishPreview(const QString &root,QString *error) {
  if(QCoreApplication::instance()&&QThread::currentThread()==QCoreApplication::instance()->thread()) {
    const auto inputs=currentBuildMetadata(root).value(QStringLiteral("inputs")).toObject();
    if(!sameInputs(inputs,true)) {if(error)*error=QStringLiteral("当前源码尚未保存或已变化，未更新论文目录。");return false;}
  }
  return publishGeneratedPreview(root,error);
}
}
