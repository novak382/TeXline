// Local multi-file template library. GPL-2.0-or-later.
#include "ResearchTemplates.h"
#include "ResearchProject.h"
#include "ResearchWorkflow.h"
#include "ResearchSettings.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUuid>
#include <QtEndian>
#include <QSet>
#include <functional>
#include <zlib.h>
namespace Research {
namespace {
constexpr qint64 maxBytes=128*1024*1024;
const QString manifestName=QStringLiteral(".texline-template.json");
bool safeName(const QString &name){
 if(name.isEmpty()||QDir::isAbsolutePath(name)||name.contains(QLatin1Char('\\'))||name.contains(QLatin1Char(':')))return false;
 for(const auto &part:name.split(QLatin1Char('/'))){if(part.isEmpty()||part==QStringLiteral(".")||part==QStringLiteral("..")||part.endsWith(QLatin1Char('.'))||part.endsWith(QLatin1Char(' ')))return false;const auto stem=part.section(QLatin1Char('.'),0,0).toUpper();if(QRegularExpression(QStringLiteral("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])$")).match(stem).hasMatch())return false;}
 return true;
}
bool omit(const QString &name,bool project=false){
 if(project){for(const auto &part:name.split(QLatin1Char('/')))if(part==QStringLiteral(".git")||part==QStringLiteral("__MACOSX"))return true;return false;}
 for(const auto &part:name.split(QLatin1Char('/')))if(part.startsWith(QLatin1Char('.'))||part==QStringLiteral("__MACOSX"))return true;
 const auto suffix=QFileInfo(name).suffix().toLower();
 return QStringList{QStringLiteral("exe"),QStringLiteral("dll"),QStringLiteral("bat"),QStringLiteral("cmd"),QStringLiteral("ps1"),QStringLiteral("lnk"),QStringLiteral("aux"),QStringLiteral("log"),QStringLiteral("nav"),QStringLiteral("snm"),QStringLiteral("toc"),QStringLiteral("out"),QStringLiteral("fls"),QStringLiteral("fdb_latexmk")}.contains(suffix)||name.endsWith(QStringLiteral(".synctex.gz"));
}
bool write(const QString &path,const QByteArray &bytes){QSaveFile f(path);return QDir().mkpath(QFileInfo(path).absolutePath())&&f.open(QIODevice::WriteOnly)&&f.write(bytes)==bytes.size()&&f.commit();}
QString read(const QString &file){QFile f(file);return f.open(QIODevice::ReadOnly)?QString::fromUtf8(f.readAll()):QString();}
}
TemplateBundle stageTemplate(const QString &input,const QString &destination,const std::shared_ptr<std::atomic_bool> &cancel,bool project){
 TemplateBundle result;const QString dest=QFileInfo(destination).absoluteFilePath();
 if(QFileInfo::exists(dest)){result.error=words("请选择尚不存在的文件夹。","Choose a new folder.");return result;}
 if(!QDir(QFileInfo(dest).absolutePath()).mkdir(QFileInfo(dest).fileName())){result.error=words("无法创建项目副本。","Cannot create the project copy.");return result;}
 auto fail=[&](const QString &message){result.error=message;QDir(dest).removeRecursively();return result;};
 const QString invalid=words("项目文件无法读取，或包含不支持的路径。","The project cannot be read or contains unsupported paths.");
 auto add=[&](const QString &name,const QByteArray &bytes){if(cancel->load()||!safeName(name)||++result.files>5000||(result.bytes+=bytes.size())>maxBytes)return false;if(omit(name,project))return true;return write(QDir(dest).filePath(name),bytes);};
 const QFileInfo source(input);
 if(source.isDir()){
  const QString root=source.canonicalFilePath();if(dest.startsWith(root+QLatin1Char('/'),Qt::CaseInsensitive)||source.isSymLink())return fail(invalid);
  QSet<QString> visited;int directories=0;std::function<bool(const QString &,const QString &)> copy=[&](const QString &folder,const QString &prefix){
   const auto canonical=QFileInfo(folder).canonicalFilePath();if(++directories>5000||prefix.count(QLatin1Char('/'))>32||visited.contains(canonical)||(!canonical.startsWith(root+QLatin1Char('/'),Qt::CaseInsensitive)&&canonical!=root))return false;visited.insert(canonical);
   for(const auto &entry:QDir(folder).entryInfoList(QDir::AllEntries|QDir::NoDotAndDotDot|QDir::Hidden,QDir::Name)){
    if(cancel->load())return false;const auto name=prefix+entry.fileName();if(omit(name,project))continue;if(entry.isSymLink()||!safeName(name))return false;
    if(entry.isDir()){if(!copy(entry.absoluteFilePath(),name+QLatin1Char('/')))return false;}
    else {QFile f(entry.absoluteFilePath());if(entry.size()>maxBytes||!f.open(QIODevice::ReadOnly)||!add(name,f.readAll()))return false;}
   }return true;};
  if(!copy(root,{}))return fail(invalid);
 }else{
  QFile file(input);if(source.size()>maxBytes||!file.open(QIODevice::ReadOnly))return fail(invalid);const QByteArray zip=file.readAll();
  auto u16=[&](qsizetype p)->quint16{return p>=0&&p+2<=zip.size()?qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(zip.constData()+p)):0;};
  auto u32=[&](qsizetype p)->quint32{return p>=0&&p+4<=zip.size()?qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(zip.constData()+p)):0;};
  qsizetype end=zip.size()-22;while(end>=qMax(qsizetype(0),zip.size()-65557)&&u32(end)!=0x06054b50)--end;
  if(end<0||u32(end)!=0x06054b50||u16(end+4)||u16(end+6)||u16(end+8)!=u16(end+10)||end+22+u16(end+20)!=zip.size())return fail(invalid);
  qsizetype at=u32(end+16);const int count=u16(end+10);if(count>5000||count==65535||at+u32(end+12)!=end)return fail(invalid);
  QSet<QString> names;
  for(int n=0;n<count;++n){
   if(cancel->load()||at+46>end||u32(at)!=0x02014b50)return fail(invalid);
   const auto flags=u16(at+8),method=u16(at+10),length=u16(at+28);const quint32 packed=u32(at+20),unpacked=u32(at+24),crc=u32(at+16),offset=u32(at+42),attrs=u32(at+38);
   if(at+46+length+u16(at+30)+u16(at+32)>end||flags&1||(method!=0&&method!=8)||(attrs>>16&0170000)==0120000||unpacked>maxBytes)return fail(invalid);
   QString name=QString::fromUtf8(zip.mid(at+46,length));at+=46+length+u16(at+30)+u16(at+32);
   const bool directory=name.endsWith(QLatin1Char('/'));if(directory)name.chop(1);
   if(!safeName(name)||names.contains(name.toCaseFolded()))return fail(invalid);names.insert(name.toCaseFolded());if(directory||omit(name,project))continue;
   if(u32(offset)!=0x04034b50||u16(offset+8)!=method)return fail(invalid);const qsizetype start=qsizetype(offset)+30+u16(offset+26)+u16(offset+28);if(start+packed>u32(end+16)||start<0)return fail(invalid);
   QByteArray bytes;if(method==0){if(packed!=unpacked)return fail(invalid);bytes=zip.mid(start,packed);}else{
    bytes.resize(unpacked?unpacked:1);z_stream stream{};stream.next_in=reinterpret_cast<Bytef*>(const_cast<char*>(zip.constData()+start));stream.avail_in=packed;stream.next_out=reinterpret_cast<Bytef*>(bytes.data());stream.avail_out=quint32(bytes.size());if(inflateInit2(&stream,-MAX_WBITS)!=Z_OK)return fail(invalid);const int status=inflate(&stream,Z_FINISH);inflateEnd(&stream);if(status!=Z_STREAM_END||stream.total_out!=unpacked||stream.total_in!=packed)return fail(invalid);bytes.resize(unpacked);
   }
   if(crc32(0,reinterpret_cast<const Bytef*>(bytes.constData()),uInt(bytes.size()))!=crc||!add(name,bytes))return fail(invalid);
  }
  if(at!=end)return fail(invalid);
 }
 QString root=dest;
 // GitHub ZIPs wrap the project in one directory.
 while(true){const auto entries=QDir(root).entryInfoList(QDir::AllEntries|QDir::NoDotAndDotDot|QDir::Hidden);if(entries.size()!=1||!entries.first().isDir())break;root=entries.first().absoluteFilePath();}
 std::function<void(const QString &)> scan=[&](const QString &folder){for(const auto &entry:QDir(folder).entryInfoList(QDir::AllEntries|QDir::NoDotAndDotDot)){if(entry.isDir())scan(entry.absoluteFilePath());else if(entry.suffix().compare(QStringLiteral("tex"),Qt::CaseInsensitive)==0){const auto text=maskProjectText(read(entry.absoluteFilePath()));if(QRegularExpression(QStringLiteral(R"(\\documentclass\s*(?:\[[^\]]*\]\s*)?\{[^{}\\]+\})")).match(text).hasMatch()&&QRegularExpression(QStringLiteral(R"(\\begin\s*\{document\})")).match(text).hasMatch())result.mains.append(QDir(root).relativeFilePath(entry.absoluteFilePath()));}}};scan(root);
 if(result.mains.isEmpty())return fail(words("未找到 LaTeX 主文件，请检查项目是否完整。","No LaTeX main file found. Check that the project is complete."));
 result.root=root;return result;
}
TemplateInfo templateInfo(const QString &main){
 if(QFileInfo(main).isDir()){QFile manifest(QDir(main).filePath(manifestName));if(!manifest.open(QIODevice::ReadOnly))return {};const auto relative=QJsonDocument::fromJson(manifest.readAll()).object().value(QStringLiteral("main")).toString();const QFileInfo target(QDir(main).filePath(relative));return safeName(relative)&&target.isFile()&&target.canonicalFilePath().startsWith(QFileInfo(main).canonicalFilePath()+QLatin1Char('/'),Qt::CaseInsensitive)?templateInfo(target.absoluteFilePath()):TemplateInfo{};}
 const auto canonical=QFileInfo(main).canonicalFilePath();QString folder=QFileInfo(canonical).absolutePath();
 for(int i=0;i<16&&!folder.isEmpty();++i){QFile f(QDir(folder).filePath(manifestName));if(f.open(QIODevice::ReadOnly)){const auto data=QJsonDocument::fromJson(f.readAll()).object();const auto relative=data.value(QStringLiteral("main")).toString();if(data.value(QStringLiteral("version")).toInt()!=1||!safeName(relative)||!canonical.startsWith(folder+QLatin1Char('/'),Qt::CaseInsensitive)||QFileInfo(QDir(folder).filePath(relative)).canonicalFilePath()!=canonical)return {};return {folder,relative,data.value(QStringLiteral("name")).toString(),data.value(QStringLiteral("engine")).toString(),QDir(folder).filePath(QStringLiteral(".texline-preview.png"))};}QDir parent(folder);if(!parent.cdUp()||parent.absolutePath()==folder)break;folder=parent.absolutePath();}return {};
}
QString saveTemplate(const TemplateBundle &bundle,const QString &main,const QString &name,const QString &engine,const QImage &preview,const QString &library,QString *error){
 if(!bundle.error.isEmpty()||!bundle.mains.contains(main)||name.trimmed().isEmpty()){*error=words("请选择主文件并填写模板名称。","Choose a main file and name the template.");return {};}
 QDir().mkpath(library);QTemporaryDir staging(QDir(library).filePath(QStringLiteral(".import-XXXXXX")));if(!staging.isValid()){*error=words("无法保存模板。","Cannot save the template.");return {};}
 const auto copy=stageTemplate(bundle.root,staging.filePath(QStringLiteral("project")),std::make_shared<std::atomic_bool>(false));if(!copy.error.isEmpty()){*error=copy.error;return {};}
 const QJsonObject meta{{QStringLiteral("version"),1},{QStringLiteral("main"),main},{QStringLiteral("name"),name.trimmed()},{QStringLiteral("engine"),engine}};
 if(!write(QDir(copy.root).filePath(manifestName),QJsonDocument(meta).toJson())||(!preview.isNull()&&!preview.save(QDir(copy.root).filePath(QStringLiteral(".texline-preview.png"))))){*error=words("无法保存模板。","Cannot save the template.");return {};}
 QString label=name.trimmed();label.replace(QRegularExpression(QStringLiteral(R"([<>:"/\\|?*\x00-\x1f])")),QStringLiteral("-"));label=label.left(60);while(label.endsWith(QLatin1Char('.'))||label.endsWith(QLatin1Char(' ')))label.chop(1);
 const QString target=QDir(library).filePath(label+QLatin1Char('-')+QUuid::createUuid().toString(QUuid::Id128).left(6));if(!QDir().rename(copy.root,target)){*error=words("无法保存模板。","Cannot save the template.");return {};}return QDir(target).filePath(main);
}
QString instantiateTemplate(const QString &main,const QString &destination,QString *error){
 const auto info=templateInfo(main);if(!info.valid()){*error=words("模板记录无效，请重新导入。","Template entry is invalid. Import it again.");return {};}
 const auto copied=stageTemplate(info.root,destination,std::make_shared<std::atomic_bool>(false));if(!copied.error.isEmpty()){*error=copied.error;return {};}
 const QString result=QDir(copied.root).filePath(info.main);if(!QFileInfo::exists(result)){QDir(destination).removeRecursively();*error=words("主文件不存在。","Main file is missing.");return {};}
 const QString text=QStringLiteral("% !TEX TS-program = ")+info.engine.toLower()+QLatin1Char('\n')+read(result);if(!write(result,text.toUtf8())){QDir(destination).removeRecursively();*error=words("无法创建文稿。","Cannot create project.");return {};}return result;
}
}

#include "ResearchTemplateImport.inc"
