#include "ResearchOutputPolicy.h"
#include "ResearchBuild.h"
#include "ResearchAppearance.h"
#include "ResearchPublication.h"
#include "Settings.h"
#include "TeXDocumentWindow.h"
#include "TWApp.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSettings>
#include <QMenu>
#include <QActionGroup>
#include <QPointer>
#include <QThreadPool>
#include <QToolButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Research {
namespace {
QJsonObject privateClientRecords(const QString &path) {
  QFile file(path);
  if(path.isEmpty()||!file.open(QIODevice::ReadOnly)||file.size()>1024*1024)return {};
  return QJsonDocument::fromJson(file.readAll()).object();
}
QJsonObject nativeClientRecords() {
  QJsonObject records;
#ifdef Q_OS_WIN
  // Microsoft uses Accounts/*/UserFolder for personal and work/school accounts:
  // OneDrive/onedrive-admin-scripts, Scripts/Sync-KFM-Deployment/KFM_Deployment.ps1.
  QSettings oneDrive(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\OneDrive\\Accounts"),QSettings::NativeFormat);
  QJsonObject accounts;
  for(const auto &account:oneDrive.childGroups().mid(0,128)) {
    const auto path=oneDrive.value(account+QStringLiteral("/UserFolder"));
    if(path.metaType().id()==QMetaType::QString)accounts.insert(account,QJsonObject{{QStringLiteral("UserFolder"),path.toString()}});
  }
  records.insert(QStringLiteral("OneDriveAccounts"),accounts);
  // Files Community's GoogleDriveCloudDetector documents this per-account JSON.
  // DefaultMountPoint is a preference, not an account registration; do not use it.
  QSettings drive(QStringLiteral("HKEY_CURRENT_USER\\Software\\Google\\DriveFS"),QSettings::NativeFormat);
  const auto preferences=drive.value(QStringLiteral("PerAccountPreferences"));
  if(preferences.metaType().id()==QMetaType::QString&&preferences.toString().size()<=1024*1024)
    records.insert(QStringLiteral("GoogleDrivePreferences"),preferences.toString());
#endif
  return records;
}
QString registeredLocalRoot(QString path,bool googleMount,bool resolved=false) {
  path=QDir::fromNativeSeparators(path.trimmed());
  if(path.startsWith(QStringLiteral("//?/")))path.remove(0,4);
  if(googleMount&&path.size()==1&&path.at(0).isLetter()&&path.at(0).unicode()<128)path+=QStringLiteral(":/");
  if(path.startsWith(QStringLiteral("//"))||!QDir::isAbsolutePath(path))return {};
  path=QDir::cleanPath(path);
#ifdef Q_OS_WIN
  if(path.size()<3||path.at(1)!=QLatin1Char(':')||path.at(2)!=QLatin1Char('/')||GetDriveTypeW((path.left(3).toStdWString()).c_str())==DRIVE_REMOTE)return {};
  // A stale or malformed registration must not classify the system volume or
  // application/cache directories as synchronized. User-selected roots retain
  // their existing rules and remain the fallback for unsupported mirror modes.
  if(path.size()==3) {
    if(!googleMount||path.left(2).compare(qEnvironmentVariable("SystemDrive"),Qt::CaseInsensitive)==0)return {};
  }
  for(const auto *variable:{"SystemRoot","ProgramFiles","ProgramFiles(x86)","ProgramData","LOCALAPPDATA","APPDATA"}) {
    const auto base=QDir::cleanPath(QDir::fromNativeSeparators(qEnvironmentVariable(variable)));
    if(!base.isEmpty()&&base!=QStringLiteral(".")&&(path.compare(base,Qt::CaseInsensitive)==0||path.startsWith(base+QLatin1Char('/'),Qt::CaseInsensitive)))return {};
  }
#else
  if(path==QStringLiteral("/"))return {};
#endif
  if(path.compare(QDir::cleanPath(QDir::homePath()),Qt::CaseInsensitive)==0||!QFileInfo(path).isDir())return {};
  // Recheck the resolved destination too: a registered local junction can
  // otherwise turn into the system directory, home, or a remote share.
  if(!resolved)return registeredLocalRoot(normalizedOutputPath(path),googleMount,true);
  return path;
}
QStringList registeredClientRoots(const QJsonObject &records) {
  QStringList roots;
  const auto accounts=records.value(QStringLiteral("OneDriveAccounts")).toObject();
  int count=0;
  for(auto it=accounts.begin();it!=accounts.end()&&count++<128;++it) {
    const auto path=it.value().toObject().value(QStringLiteral("UserFolder"));
    if(path.isString()) {const auto root=registeredLocalRoot(path.toString(),false);if(!root.isEmpty())roots.append(root);}
  }
  const auto bytes=records.value(QStringLiteral("GoogleDrivePreferences")).toString().toUtf8();
  if(bytes.size()<=1024*1024) {
    const auto accounts=QJsonDocument::fromJson(bytes).object().value(QStringLiteral("per_account_preferences")).toArray();
    for(int i=0;i<qMin(accounts.size(),qsizetype(128));++i) {
      const auto account=accounts.at(i).toObject();
      if(account.value(QStringLiteral("key")).toString().isEmpty())continue;
      const auto path=account.value(QStringLiteral("value")).toObject().value(QStringLiteral("mount_point_path"));
      if(path.isString()) {const auto root=registeredLocalRoot(path.toString(),true);if(!root.isEmpty())roots.append(root);}
    }
  }
  return roots;
}
}
QString normalizedOutputPath(const QString &path) {
  if(path.isEmpty() || !QDir::isAbsolutePath(path))return {};
  QString result=QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath()));
#ifdef Q_OS_WIN
  QString native=QDir::toNativeSeparators(result);
  if(!native.startsWith(QStringLiteral("\\\\?\\")))native=native.startsWith(QStringLiteral("\\\\"))?QStringLiteral("\\\\?\\UNC\\")+native.mid(2):QStringLiteral("\\\\?\\")+native;
  HANDLE handle=CreateFileW(native.toStdWString().c_str(),0,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr);
  if(handle!=INVALID_HANDLE_VALUE){
    const DWORD size=GetFinalPathNameByHandleW(handle,nullptr,0,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
    if(size && size<32768){std::wstring buffer(size+1,L'\0');const DWORD length=GetFinalPathNameByHandleW(handle,buffer.data(),DWORD(buffer.size()),FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
      if(length && length<buffer.size()){
        QString final=QString::fromWCharArray(buffer.data(),int(length));
        if(final.startsWith(QStringLiteral("\\\\?\\UNC\\")))final=QStringLiteral("\\\\")+final.mid(8);
        else if(final.startsWith(QStringLiteral("\\\\?\\")))final.remove(0,4);
        result=QDir::cleanPath(QDir::fromNativeSeparators(final));
      }
    }CloseHandle(handle);
  }
#else
  const auto canonical=QFileInfo(path).canonicalFilePath();if(!canonical.isEmpty())result=canonical;
#endif
  return result;
}
QStringList discoverSyncRoots(const QStringList &infoFiles,const QStringList &manual,const QString &clientRecordsFile) {
  QStringList paths=manual+registeredClientRoots(privateClientRecords(clientRecordsFile)),result;
  for(const auto &name:infoFiles){QFile file(name);if(!file.open(QIODevice::ReadOnly)||file.size()>1024*1024)continue;
    const auto accounts=QJsonDocument::fromJson(file.readAll()).object();
    for(auto it=accounts.begin();it!=accounts.end();++it){const auto path=it.value().toObject().value(QStringLiteral("path"));if(path.isString())paths.append(path.toString());}
  }
  for(const auto &path:paths){if(!QDir::isAbsolutePath(path)||!QFileInfo(path).isDir())continue;const auto root=normalizedOutputPath(path);if(!root.isEmpty()&&!result.contains(root,Qt::CaseInsensitive))result.append(root);}
  return result;
}
bool insideSyncRoot(const QString &file,const QStringList &roots) {
  for(const auto &root:roots){const auto prefix=root.endsWith(QLatin1Char('/'))?root:root+QLatin1Char('/');if(file.startsWith(prefix,Qt::CaseInsensitive))return true;}
  return false;
}
namespace {
struct Pending { QPointer<QObject> context;std::function<void(OutputPolicy)> ready; };
class Catalog final : public QObject {
public:
  explicit Catalog():QObject(qApp){}
  bool loaded=false,loading=false;quint64 revision=0;
  QStringList roots,manual,infoFiles;
  QHash<QString,OutputPolicy> projects;
  QHash<QString,QList<Pending>> pending;
};
Catalog *catalog(){static QPointer<Catalog> value;if(!value)value=new Catalog;return value;}
QStringList privateInfoFiles;QString privateClientFile;bool privateDiscovery=false;
QStringList infoFiles(){
  if(privateDiscovery)return privateInfoFiles;
  QStringList result;
  for(const auto *variable:{"LOCALAPPDATA","APPDATA"}){const auto base=qEnvironmentVariable(variable);if(!base.isEmpty())result.append(QDir(base).filePath(QStringLiteral("Dropbox/info.json")));}
  return result;
}
OutputPolicy chosen(OutputPolicy policy){
  const auto choices=Tw::Settings().value(QStringLiteral("researchBuildOutputModes")).toMap();
  policy.cacheOnly=policy.synchronized&&choices.value(policy.key).toString()==QStringLiteral("cache");
  policy.directory=policy.cacheOnly?policy.cacheDirectory:QFileInfo(policy.root).absolutePath();
  return policy;
}
void resolvePending(Catalog *state,const QString &raw){
  const auto roots=state->roots;const auto revision=state->revision;QPointer<Catalog> guard(state);
  QThreadPool::globalInstance()->start([guard,raw,roots,revision]{
    OutputPolicy policy;policy.root=normalizedOutputPath(raw);policy.key=QString::fromLatin1(QCryptographicHash::hash(policy.root.toCaseFolded().toUtf8(),QCryptographicHash::Sha256).toHex());policy.synchronized=insideSyncRoot(policy.root,roots);policy.cacheDirectory=buildCacheDirectory(raw)+QStringLiteral("/preview");
    QMetaObject::invokeMethod(qApp,[guard,raw,revision,policy]{if(!guard||revision!=guard->revision)return;
      guard->projects.insert(raw,policy);const auto callbacks=guard->pending.take(raw);for(const auto &callback:callbacks)if(callback.context)callback.ready(chosen(policy));
    },Qt::QueuedConnection);
  });
}
void refreshWindows(){for(auto *window:TeXDocumentWindow::documentList())refreshBuildDestination(window);}
void manualDirectories(TeXDocumentWindow *window){
  auto *dialog=new QDialog(window);dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setObjectName(QStringLiteral("researchSyncDirectoriesDialog"));dialog->setWindowTitle(ui(QStringLiteral("同步目录…")));dialog->resize(650,360);
  auto *layout=new QVBoxLayout(dialog);auto *caption=new QLabel(ui(QStringLiteral("自动识别的同步目录，可手动添加。")),dialog);caption->setWordWrap(true);layout->addWidget(caption);
  auto *detected=new QLabel(catalog()->roots.join(QLatin1Char('\n')),dialog);detected->setWordWrap(true);detected->setTextInteractionFlags(Qt::TextSelectableByMouse);layout->addWidget(detected);
  layout->addWidget(new QLabel(ui(QStringLiteral("手动补充：")),dialog));auto *list=new QListWidget(dialog);list->setObjectName(QStringLiteral("researchManualSyncRoots"));list->addItems(Tw::Settings().value(QStringLiteral("researchManualSyncRoots")).toStringList());layout->addWidget(list);
  auto *add=new QPushButton(ui(QStringLiteral("添加同步目录…")),dialog);layout->addWidget(add);
  auto *remove=new QPushButton(ui(QStringLiteral("移除选中目录")),dialog);layout->addWidget(remove);
  QObject::connect(add,&QPushButton::clicked,dialog,[dialog,list]{const auto path=QFileDialog::getExistingDirectory(dialog,ui(QStringLiteral("添加同步目录…")));if(!path.isEmpty()&&list->findItems(path,Qt::MatchFixedString).isEmpty())list->addItem(path);});
  QObject::connect(remove,&QPushButton::clicked,dialog,[list]{delete list->takeItem(list->currentRow());});
  auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,dialog);layout->addWidget(buttons);
  QObject::connect(buttons,&QDialogButtonBox::rejected,dialog,&QDialog::close);
  QObject::connect(buttons,&QDialogButtonBox::accepted,dialog,[dialog,list]{QStringList paths;for(int i=0;i<list->count();++i)paths.append(list->item(i)->text());Tw::Settings().setValue(QStringLiteral("researchManualSyncRoots"),paths);dialog->close();refreshWindows();});dialog->open();
}
}
void setSyncInfoFilesForTests(const QStringList &files){privateDiscovery=true;privateInfoFiles=files;privateClientFile.clear();auto *state=catalog();++state->revision;state->loaded=state->loading=false;state->projects.clear();state->pending.clear();}
void setSyncClientRecordsForTests(const QString &file){privateDiscovery=true;privateClientFile=file;auto *state=catalog();++state->revision;state->loaded=state->loading=false;state->projects.clear();state->pending.clear();}
void requestOutputPolicy(QObject *context,const QString &raw,const std::function<void(OutputPolicy)> &ready){
  if(raw.isEmpty()){ready({});return;}
  auto *state=catalog();const auto manual=Tw::Settings().value(QStringLiteral("researchManualSyncRoots")).toStringList();
  if(state->manual!=manual){state->manual=manual;++state->revision;state->loaded=state->loading=false;state->projects.clear();}
  if(state->loaded&&state->projects.contains(raw)){ready(chosen(state->projects.value(raw)));return;}
  const bool first=!state->pending.contains(raw);state->pending[raw].append({context,ready});
  if(state->loaded){if(first)resolvePending(state,raw);return;}
  if(state->loading)return;state->loading=true;const auto revision=state->revision;const auto files=infoFiles();const auto clientFile=privateClientFile;const bool nativeClients=!privateDiscovery;QPointer<Catalog> guard(state);
  QThreadPool::globalInstance()->start([guard,files,manual,revision,clientFile,nativeClients]{const auto roots=discoverSyncRoots(files,manual+(nativeClients?registeredClientRoots(nativeClientRecords()):QStringList()),clientFile);
    QMetaObject::invokeMethod(qApp,[guard,revision,roots]{if(!guard||revision!=guard->revision)return;guard->roots=roots;guard->loaded=true;guard->loading=false;for(const auto &raw:guard->pending.keys())resolvePending(guard,raw);},Qt::QueuedConnection);
  });
}
void rememberOutputPolicy(const OutputPolicy &policy,bool cacheOnly){
  if(!policy.synchronized||policy.root.isEmpty())return;
  auto choices=Tw::Settings().value(QStringLiteral("researchBuildOutputModes")).toMap();choices.insert(policy.key,cacheOnly?QStringLiteral("cache"):QStringLiteral("publish"));Tw::Settings().setValue(QStringLiteral("researchBuildOutputModes"),choices);refreshWindows();
}
void refreshBuildDestination(TeXDocumentWindow *window){
  if(!window)return;const auto raw=window->textDoc()->getRootFilePath();
  const bool changed=window->property("researchOutputPolicyRequest").toString()!=raw;
  window->setProperty("researchOutputPolicyRequest",raw);
  if(changed){window->setProperty("researchOutputPolicyReady",false);window->setProperty("researchSynchronizedProject",false);window->setProperty("researchCacheOnly",false);window->setProperty("researchCompileIdleCaption",QStringLiteral("编译"));window->setProperty("researchCompileDestinationHint",QStringLiteral("编译后更新项目目录。"));}
  QPointer<TeXDocumentWindow> guard(window);requestOutputPolicy(window,raw,[guard,raw](OutputPolicy policy){if(!guard||guard->property("researchOutputPolicyRequest").toString()!=raw)return;
    guard->setProperty("researchOutputPolicyReady",true);guard->setProperty("researchSynchronizedProject",policy.synchronized);guard->setProperty("researchCacheOnly",policy.cacheOnly);guard->setProperty("researchCompileIdleCaption",policy.synchronized?(policy.cacheOnly?QStringLiteral("预览"):QStringLiteral("编译")):QStringLiteral("编译"));
    guard->setProperty("researchCompileDestinationHint",policy.cacheOnly?QStringLiteral("仅在本机生成预览，不更新同步目录。"):policy.synchronized?QStringLiteral("编译后更新同步目录。上传进度请查看同步软件。"):QStringLiteral("编译后更新项目目录。"));
    guard->setProperty("researchSelectedOutputDirectory",policy.directory);refreshBuildOutputDestination(guard,policy.directory,policy.root);
    if(auto *button=guard->findChild<QToolButton *>(QStringLiteral("researchCompile")))refreshCompileButton(button);
  });
  if(changed)if(auto *button=window->findChild<QToolButton *>(QStringLiteral("researchCompile")))refreshCompileButton(button);
}
void addBuildDestinationMenu(TeXDocumentWindow *window,QMenu *menu){
  refreshBuildDestination(window);const auto raw=window->textDoc()->getRootFilePath();
  auto *cache=menu->addAction(englishUi()?QStringLiteral("Local preview"):Research::ui(QStringLiteral("本机预览")));
  auto *publish=menu->addAction(englishUi()?QStringLiteral("Write back to sync folder"):Research::ui(QStringLiteral("写回同步目录")));
  cache->setObjectName(QStringLiteral("researchOutputCacheOnly"));publish->setObjectName(QStringLiteral("researchOutputSynchronize"));
  auto *group=new QActionGroup(cache);group->addAction(cache);group->addAction(publish);
  for(auto *action:{cache,publish}){action->setCheckable(true);action->setVisible(false);}
  auto *separator=menu->addSeparator();separator->setVisible(false);
  QPointer<QAction> cacheGuard(cache),publishGuard(publish),separatorGuard(separator);QPointer<TeXDocumentWindow> guard(window);
  requestOutputPolicy(cache,raw,[guard,raw,cacheGuard,publishGuard,separatorGuard](OutputPolicy policy){
    if(!guard||!cacheGuard||!publishGuard||!separatorGuard||guard->textDoc()->getRootFilePath()!=raw)return;
    const bool enabled=!guard->isTypesetting()&&!guard->property("researchPublishing").toBool();
    cacheGuard->setVisible(policy.synchronized);publishGuard->setVisible(policy.synchronized);separatorGuard->setVisible(policy.synchronized);
    cacheGuard->setChecked(policy.cacheOnly);publishGuard->setChecked(!policy.cacheOnly);
    cacheGuard->setEnabled(enabled);publishGuard->setEnabled(enabled);
    cacheGuard->setToolTip(ui(QStringLiteral("仅在本机生成预览，不更新同步目录。")));
    publishGuard->setToolTip(ui(QStringLiteral("编译后更新同步目录。上传进度请查看同步软件。")));
    for(bool local:{true,false})QObject::connect(local?cacheGuard.data():publishGuard.data(),&QAction::triggered,guard,[guard,raw,policy,local]{
      if(guard&&guard->textDoc()->getRootFilePath()==raw&&!guard->isTypesetting()&&!guard->property("researchPublishing").toBool())rememberOutputPolicy(policy,local);
    });
  });
  auto *manage=menu->addAction(ui(QStringLiteral("同步目录…")));manage->setObjectName(QStringLiteral("researchSyncDirectories"));QObject::connect(manage,&QAction::triggered,window,[window]{manualDirectories(window);});menu->addSeparator();
}
}
