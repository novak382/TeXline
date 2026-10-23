#include "ResearchSlideExport.h"
#include "ResearchBuild.h"
#include "ResearchProject.h"
#include "ResearchWorkflow.h"
#include "ResearchAppearance.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <vector>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
namespace Research {
namespace {
// Detach engine-created directory aliases before ordinary temporary cleanup.
// If any alias cannot be removed, retain the private temporary directory.
struct ExportAliasCleanup {
 QTemporaryDir &temp;
 ~ExportAliasCleanup() {
#ifdef Q_OS_WIN
  const QDir aliases(temp.path()+QStringLiteral("/search-paths"));
  for(const auto &entry:aliases.entryInfoList(QDir::AllEntries|QDir::NoDotAndDotDot|QDir::Hidden|QDir::System)) {
   const auto native=(QStringLiteral("\\\\?\\")+QDir::toNativeSeparators(entry.absoluteFilePath())).toStdWString();
   const DWORD attributes=GetFileAttributesW(native.c_str());
   if(attributes==INVALID_FILE_ATTRIBUTES) { temp.setAutoRemove(false); continue; }
   if(attributes&FILE_ATTRIBUTE_REPARSE_POINT) {
    const bool removed=(attributes&FILE_ATTRIBUTE_DIRECTORY)?RemoveDirectoryW(native.c_str()):DeleteFileW(native.c_str());
    if(!removed)temp.setAutoRemove(false);
   }
  }
#endif
 }
};
}
QByteArray slideExportHash(const QString &file){QFile f(file);if(!f.exists())return {};if(!f.open(QIODevice::ReadOnly))return QByteArray("unreadable");QCryptographicHash h(QCryptographicHash::Sha256);if(!h.addData(&f))return QByteArray("unreadable");return h.result();}
QString localizedSlideExportError(const QString &error,bool english) {
 if(!english&&!traditionalUi())return error;
 // Translate on the GUI thread, using the currently selected UI language.
 // Keep compiler logs, paths and operating-system error details intact.
 const QList<QPair<QString,QString>> messages{
  {QStringLiteral("请选择可用的 pdfLaTeX、XeLaTeX、LuaLaTeX 或 latexmk 引擎。"),QStringLiteral("Choose an available pdfLaTeX, XeLaTeX, LuaLaTeX or latexmk engine.")},
  {QStringLiteral("导出文件配置不完整。"),QStringLiteral("The export file configuration is incomplete.")},
  {QStringLiteral("无法创建导出副本。"),QStringLiteral("Cannot create the export copy.")},
  {QStringLiteral("依赖文件位于不同磁盘，请将讲稿资源放到同一项目后导出。"),QStringLiteral("Dependencies are on different drives. Keep the presentation resources in one project before exporting.")},
  {QStringLiteral("请选择独立的文件名，避免替换讲稿资源或当前预览。"),QStringLiteral("Choose a separate file name to preserve presentation resources and the current preview.")},
  {QStringLiteral("无法准备导出目录。"),QStringLiteral("Cannot prepare the export folder.")},
  {QStringLiteral("无法复制依赖：%1"),QStringLiteral("Cannot copy dependency: %1")},
  {QStringLiteral("请检查导言中的放映设置块或 document 环境。"),QStringLiteral("Check the presentation settings block in the preamble and the document environment.")},
  {QStringLiteral("无法启动编译引擎。"),QStringLiteral("Cannot start the typesetting engine.")},
  {QStringLiteral("导出编译超时。"),QStringLiteral("Export compilation timed out.")},
  {QStringLiteral("导出编译失败。原稿和已有 PDF 未改动，请查看下方日志。"),QStringLiteral("Export compilation failed. The source and existing PDFs are unchanged. See the log below.")},
  {QStringLiteral("编译未生成有效的 PDF。"),QStringLiteral("Compilation did not produce a valid PDF.")},
  {QStringLiteral("目标文件在导出期间发生变化，未覆盖：%1"),QStringLiteral("The destination changed during export and was not replaced: %1")},
  {QStringLiteral("无法保存：%1"),QStringLiteral("Cannot save: %1")}
 };
 for(const auto &message:messages){
  const auto target=english?message.second:ui(message.first);
  if(error==message.first)return target;
  if(message.first.endsWith(QStringLiteral("%1"))){const auto prefix=message.first.left(message.first.size()-2);if(error.startsWith(prefix))return target.arg(error.mid(prefix.size()));}
 }
 return error;
}
QString slideProfileSource(QString source,int profile){
 if(profile==0)return source;
 const QString first=QStringLiteral("% TeXline presentation profile\n"),last=QStringLiteral("% End TeXline presentation profile\n");
 const auto begin=QRegularExpression(QStringLiteral(R"(\\begin\s*\{document\})")).match(maskProjectText(source));if(!begin.hasMatch())return {};
 if(source.count(first)!=source.count(last)||source.count(first)>1)return {};
 int a=source.indexOf(first),b=a<0?-1:source.indexOf(last,a);if(a>=0){if(b<a||b+last.size()>begin.capturedStart())return {};source.remove(a,b+last.size()-a);}
 const auto at=QRegularExpression(QStringLiteral(R"(\\begin\s*\{document\})")).match(maskProjectText(source)).capturedStart();
 QString settings=profile==2?QStringLiteral("\\usepackage{pgfpages}\n\\setbeameroption{show notes on second screen=right}\n"):QStringLiteral("\\setbeameroption{hide notes}\n\\makeatletter\n\\@ifpackageloaded{pgfpages}{\\pgfpagesuselayout{resize to}[physical paper width=\\paperwidth,physical paper height=\\paperheight]}{}\n\\makeatother\n");
 source.insert(at,first+settings+last);return source;
}
SlideExportResult exportSlideCopies(const SlideExportRequest &r,const std::shared_ptr<std::atomic_bool> &cancel){
 SlideExportResult result;auto stop=[&]{if(!cancel->load())return false;result.cancelled=true;return true;};
 if(!r.engine.supportsLocalBuild()||!r.engine.isAvailable()){result.error=QStringLiteral("请选择可用的 pdfLaTeX、XeLaTeX、LuaLaTeX 或 latexmk 引擎。");return result;}
 if(r.profiles.isEmpty()||r.profiles.size()!=r.destinations.size()){result.error=QStringLiteral("导出文件配置不完整。");return result;}
 QTemporaryDir temp(QDir::tempPath()+QStringLiteral("/texline-export-XXXXXX"));if(!temp.isValid()){result.error=QStringLiteral("无法创建导出副本。");return result;}
 ExportAliasCleanup aliasCleanup{temp};
 auto sources=r.sources;if(!sources.contains(r.root)){QFile f(r.root);if(!f.open(QIODevice::ReadOnly)){result.error=f.errorString();return result;}sources.insert(r.root,QString::fromUtf8(f.readAll()));}
 QStringList files=buildDependencies(r.root,sources);for(const auto &file:r.extraDependencies){const auto path=QFileInfo(file).canonicalFilePath();if(!path.isEmpty()&&!files.contains(path))files.append(path);}for(auto it=sources.cbegin();it!=sources.cend();++it)if(!files.contains(it.key()))files.append(it.key());
 // Literal graphicspath entries are needed before a first recorder file exists.
 QStringList graphicsDirs{QFileInfo(r.root).absolutePath()};
 for(auto it=sources.cbegin();it!=sources.cend();++it){const auto clean=maskProjectText(it.value());auto declarations=QRegularExpression(QStringLiteral(R"(\\graphicspath\s*\{((?:\s*\{[^{}]*\}\s*)+)\})")).globalMatch(clean);while(declarations.hasNext()){auto entries=QRegularExpression(QStringLiteral(R"(\{([^{}]*)\})")).globalMatch(declarations.next().captured(1));while(entries.hasNext()){const auto dir=entries.next().captured(1);if(!dir.contains(QLatin1Char('\\')))graphicsDirs<<QDir(QFileInfo(r.root).absolutePath()).absoluteFilePath(dir);}}}
 for(auto it=sources.cbegin();it!=sources.cend();++it){auto graphics=QRegularExpression(QStringLiteral(R"(\\includegraphics\*?\s*(?:\[[^\]]*\]\s*)?\{([^{}]+)\})")).globalMatch(maskProjectText(it.value()));while(graphics.hasNext()){const auto name=graphics.next().captured(1);if(name.contains(QLatin1Char('\\')))continue;QStringList names{name};if(QFileInfo(name).suffix().isEmpty())for(const auto &ext:{QStringLiteral("pdf"),QStringLiteral("png"),QStringLiteral("jpg"),QStringLiteral("jpeg")})names<<name+QLatin1Char('.')+ext;for(const auto &directory:graphicsDirs)for(const auto &candidate:names){QFileInfo info(QDir(directory).filePath(candidate));if(info.isFile()&&!files.contains(info.canonicalFilePath()))files<<info.canonicalFilePath();}}}
 // Retain directory relationships for relative inputs, including ../shared.
 QString common=QFileInfo(r.root).absolutePath();for(const auto &file:files){while(QDir(common).relativeFilePath(file).startsWith(QStringLiteral("../"))){QDir parent(common);if(!parent.cdUp())break;common=parent.absolutePath();}}
 QHash<QString,QString> copies;
 for(const auto &file:files){if(stop())return result;const auto relative=QDir(common).relativeFilePath(file);if(QDir::isAbsolutePath(relative)||relative.startsWith(QStringLiteral("../"))){result.error=QStringLiteral("依赖文件位于不同磁盘，请将讲稿资源放到同一项目后导出。");return result;}copies.insert(file,temp.path()+QStringLiteral("/source/")+relative);}
 for(const auto &destination:r.destinations){const auto absolute=QFileInfo(destination).absoluteFilePath();if(files.contains(QFileInfo(destination).canonicalFilePath())||absolute==QFileInfo(r.root).absolutePath()+QLatin1Char('/')+QFileInfo(r.root).completeBaseName()+QStringLiteral(".pdf")||absolute==previewFile(r.root)){result.error=QStringLiteral("请选择独立的文件名，避免替换讲稿资源或当前预览。");return result;}}
 for(const auto &file:files){if(stop())return result;const auto dest=copies.value(file);if(!QDir().mkpath(QFileInfo(dest).absolutePath())){result.error=QStringLiteral("无法准备导出目录。");return result;}if(sources.contains(file)){auto text=sources.value(file);for(auto it=copies.cbegin();it!=copies.cend();++it)text.replace(it.key(),it.value());QFile f(dest);if(!f.open(QIODevice::WriteOnly)||f.write(text.toUtf8())<0){result.error=f.errorString();return result;}}else if(!QFile::copy(file,dest)){result.error=QStringLiteral("无法复制依赖：%1").arg(file);return result;}}
 const auto input=copies.value(r.root);QFile base(input);if(!base.open(QIODevice::ReadOnly)){result.error=base.errorString();return result;}const auto original=QString::fromUtf8(base.readAll());base.close();QStringList outputs;
 for(int n=0;n<r.profiles.size();++n){if(stop())return result;const auto source=slideProfileSource(original,r.profiles[n]);if(source.isEmpty()){result.error=QStringLiteral("请检查导言中的放映设置块或 document 环境。");return result;}QFile f(input);if(!f.open(QIODevice::WriteOnly|QIODevice::Truncate)||f.write(source.toUtf8())<0){result.error=f.errorString();return result;}f.close();
  const auto out=temp.path()+QStringLiteral("/output-%1").arg(n);QDir().mkpath(out);for(const auto &file:files){const auto rel=QDir(QFileInfo(r.root).absolutePath()).relativeFilePath(file);if(!rel.startsWith(QStringLiteral("../")))QDir().mkpath(QFileInfo(QDir(out).filePath(rel)).absolutePath());}
  const int passes=QFileInfo(r.engine.program()).completeBaseName().compare(QStringLiteral("latexmk"),Qt::CaseInsensitive)==0?1:2;
  for(int pass=0;pass<passes;++pass){Engine engine=r.engine;std::unique_ptr<QProcess> process(engine.run(QFileInfo(input),nullptr,out));if(!process){result.error=QStringLiteral("无法启动编译引擎。");return result;}QElapsedTimer elapsed;elapsed.start();while(!process->waitForFinished(100)){result.log+=QString::fromLocal8Bit(process->readAll());if(result.log.size()>200000)result.log=result.log.right(200000);if(stop()||elapsed.elapsed()>300000){Engine::stop(process.get());process->waitForFinished(5000);if(!result.cancelled)result.error=QStringLiteral("导出编译超时。");return result;}if(process->state()==QProcess::NotRunning)break;}result.log+=QString::fromLocal8Bit(process->readAll());if(process->exitStatus()!=QProcess::NormalExit||process->exitCode()!=0||process->error()==QProcess::FailedToStart){result.error=QStringLiteral("导出编译失败。原稿和已有 PDF 未改动，请查看下方日志。");return result;}}
  const auto pdf=out+QLatin1Char('/')+QFileInfo(input).completeBaseName()+QStringLiteral(".pdf");QFile output(pdf);if(!output.open(QIODevice::ReadOnly)||output.read(5)!=QByteArray("%PDF-")){result.error=QStringLiteral("编译未生成有效的 PDF。");return result;}outputs.append(pdf);
 }
 if(stop())return result;
 for(const auto &dest:r.destinations)if(slideExportHash(dest)!=r.destinationHashes.value(dest)){result.error=QStringLiteral("目标文件在导出期间发生变化，未覆盖：%1").arg(dest);return result;}
 // Prepare every output before replacing any existing file. An unavailable
 // second destination must not silently replace the first version of a talk.
 std::vector<std::unique_ptr<QSaveFile>> pending;
 for(int n=0;n<outputs.size();++n){
  if(stop())return result;
  QFile inputPdf(outputs[n]);auto target=std::make_unique<QSaveFile>(r.destinations[n]);
  if(!inputPdf.open(QIODevice::ReadOnly)||!target->open(QIODevice::WriteOnly)){result.error=QStringLiteral("无法保存：%1").arg(r.destinations[n]);return result;}
  while(!inputPdf.atEnd()){
   if(stop())return result;
   const auto data=inputPdf.read(256*1024);
   if(data.isEmpty()&&inputPdf.error()!=QFile::NoError){result.error=inputPdf.errorString();return result;}
   if(target->write(data)!=data.size()){result.error=target->errorString();return result;}
  }
  pending.push_back(std::move(target));
 }
 if(stop())return result;
 for(const auto &dest:r.destinations)if(slideExportHash(dest)!=r.destinationHashes.value(dest)){result.error=QStringLiteral("目标文件在导出期间发生变化，未覆盖：%1").arg(dest);return result;}
 // Once publication starts, finish its short sequence rather than returning
 // a cancellation that hides already written files. Commit failures still
 // report the precise subset that succeeded; this is not a multi-file transaction.
 for(int n=0;n<outputs.size();++n){if(!pending[n]->commit()){result.error=QStringLiteral("无法保存：%1\n%2").arg(r.destinations[n],pending[n]->errorString());return result;}result.written.append(r.destinations[n]);}
 return result;
}
}
