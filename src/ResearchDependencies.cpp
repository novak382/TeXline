#include "utils/FilePath.h"
#include "ResearchDependencies.h"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QProcess>
#include <QStandardPaths>

namespace Research {
namespace {
QMutex lookupMutex;
QStringList binaryPaths;
struct Lookup {QString path; qint64 time;};
QHash<QString, Lookup> lookups;
QString canonicalDependency(const QString &path) {
  return Tw::Utils::canonicalSourcePath(path);
}
bool within(const QString &path,const QString &directory) {
  if(directory.isEmpty())return false;
  const auto prefix=canonicalDependency(directory)+QLatin1Char('/');
#ifdef Q_OS_WIN
  return canonicalDependency(path).startsWith(prefix,Qt::CaseInsensitive);
#else
  return canonicalDependency(path).startsWith(prefix);
#endif
}
}
void setDependencyBinaryPaths(const QStringList &paths) {
  QMutexLocker lock(&lookupMutex);if(binaryPaths!=paths){binaryPaths=paths;lookups.clear();}
}
bool authoredDependency(const QString &path,const QString &root,const QString &outputDirectory) {
  if(path.isEmpty())return false;
  if(within(path,outputDirectory))return false;
  if(within(path,QFileInfo(root).absolutePath()))return true;
  const QString normalized=QDir::fromNativeSeparators(path).toLower();
  // Installed distribution inputs are immutable for a writing session. Do
  // not turn a paper watcher into a watcher for the entire TeX distribution.
  if(normalized.contains(QStringLiteral("/texmf-dist/"))||normalized.contains(QStringLiteral("/texmf-var/")))return false;
  static const QStringList suffixes{QStringLiteral("tex"),QStringLiteral("bib"),QStringLiteral("bbl"),QStringLiteral("sty"),QStringLiteral("cls"),QStringLiteral("bst"),QStringLiteral("cfg"),QStringLiteral("def"),QStringLiteral("txt"),QStringLiteral("csv"),QStringLiteral("dat"),QStringLiteral("pdf"),QStringLiteral("png"),QStringLiteral("jpg"),QStringLiteral("jpeg"),QStringLiteral("eps")};
  return suffixes.contains(QFileInfo(path).suffix().toLower());
}
bool textDependency(const QString &path) {
  static const QStringList suffixes{QStringLiteral("tex"),QStringLiteral("bib"),QStringLiteral("bbl"),QStringLiteral("sty"),QStringLiteral("cls"),QStringLiteral("bst"),QStringLiteral("cfg"),QStringLiteral("def"),QStringLiteral("txt"),QStringLiteral("csv"),QStringLiteral("dat")};
  return suffixes.contains(QFileInfo(path).suffix().toLower());
}
QString resolveDependency(const QString &value,const QString &file,const QString &root,const QString &suffix) {
  const QString name=value.trimmed();
  if(name.isEmpty()||name.contains(QLatin1Char('\\'))||name.contains(QLatin1Char('#'))||name.contains(QLatin1Char('\n')))return {};
  QStringList names{name};if(!name.endsWith(suffix,Qt::CaseInsensitive))names.append(name+suffix);
  QStringList folders{QFileInfo(root).absolutePath(),QFileInfo(file).absolutePath()};
  for(const auto &folder:folders)for(const auto &candidate:names){const auto path=QDir(folder).filePath(candidate);if(QFileInfo(path).isFile())return canonicalDependency(path);}
  const QString variable=suffix==QStringLiteral(".bib")?QStringLiteral("BIBINPUTS"):QStringLiteral("TEXINPUTS");
  const QString search=qEnvironmentVariable(variable.toLatin1().constData());
  // Ordinary explicit paths need neither a process nor a recursive scan.
  for(QString folder:search.split(QDir::listSeparator(),Qt::SkipEmptyParts)) {
    if(folder.startsWith(QStringLiteral("!!")))folder.remove(0,2);
    if(folder.contains(QStringLiteral("//"))||folder.contains(QLatin1Char('$'))||folder.contains(QLatin1Char('{')))continue;
    if(folder.startsWith(QLatin1Char('~')))folder.replace(0,1,QDir::homePath());
    if(QDir::isRelativePath(folder))folder=QFileInfo(root).absoluteDir().filePath(folder);
    for(const auto &candidate:names){const auto path=QDir(folder).filePath(candidate);if(QFileInfo(path).isFile())return canonicalDependency(path);}
  }
  QStringList paths;{QMutexLocker lock(&lookupMutex);paths=binaryPaths;}
  const QString cacheKey=QFileInfo(root).absolutePath()+QLatin1Char('\n')+name+suffix+QLatin1Char('\n')+search+QLatin1Char('\n')+paths.join(QLatin1Char(';'));
  const qint64 now=QDateTime::currentMSecsSinceEpoch();
  {QMutexLocker lock(&lookupMutex);const auto found=lookups.constFind(cacheKey);if(found!=lookups.cend()&&now-found->time<2000&&(found->path.isEmpty()||QFileInfo(found->path).isFile()))return found->path;}
  QString tool=QStandardPaths::findExecutable(QStringLiteral("kpsewhich"),paths);
  if(tool.isEmpty())tool=QStandardPaths::findExecutable(QStringLiteral("kpsewhich"));
  QString result;
  if(!tool.isEmpty()) {
    QProcess process;process.setWorkingDirectory(QFileInfo(root).absolutePath());
    process.start(tool,{QStringLiteral("-must-exist"),QStringLiteral("-format=")+(suffix==QStringLiteral(".bib")?QStringLiteral("bib"):QStringLiteral("tex")),names.last()});
    if(process.waitForStarted(500)&&process.waitForFinished(750)&&process.exitCode()==0) {
      const QByteArray output=process.readAllStandardOutput();
      QString path=QString::fromUtf8(output).trimmed().section(QLatin1Char('\n'),0,0).trimmed();
      if(!QFileInfo(path).isFile())path=QString::fromLocal8Bit(output).trimmed().section(QLatin1Char('\n'),0,0).trimmed();
      if(QFileInfo(path).isFile())result=canonicalDependency(path);
    }
    if(process.state()!=QProcess::NotRunning){process.kill();process.waitForFinished(250);}
  }
  {QMutexLocker lock(&lookupMutex);if(lookups.size()>=128)lookups.clear();lookups.insert(cacheKey,{result,now});}
  return result;
}
}
