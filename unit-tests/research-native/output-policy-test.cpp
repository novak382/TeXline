#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "TeXDocumentWindow.h"
#include "CompletingEdit.h"
#include "Engine.h"
#include "ResearchBuild.h"
#include "ResearchOutputPolicy.h"
#include "ResearchAppearance.h"
#include "Settings.h"
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMenu>
#include <QListWidget>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QProcess>
#include <QSaveFile>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <windows.h>
static bool put(const QString &p,const QByteArray &d){QDir().mkpath(QFileInfo(p).absolutePath());QSaveFile f(p);return f.open(QIODevice::WriteOnly)&&f.write(d)==d.size()&&f.commit();}
static QByteArray read(const QString &p){QFile f(p);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray();}
static QString hash(const QByteArray &b){return QString::fromLatin1(QCryptographicHash::hash(b,QCryptographicHash::Sha256).toHex());}
static bool waitFor(const std::function<bool()> &ready,int ms=90000){QElapsedTimer t;t.start();while(!ready()&&t.elapsed()<ms){QCoreApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QThread::msleep(2);}return ready();}
int main(int argc,char **argv){const QString report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);QTimer::singleShot(100,&app,[&]{
 const QString folder=QFileInfo(report).absolutePath(),sync=folder+"/实际同步目录/中文论文",root=sync+"/main.tex",dir=QFileInfo(root).absolutePath(),local=folder+"/Dropbox-name-only/main.tex",sibling=folder+"/实际同步目录-other/main.tex",manual=folder+"/手动同步/main.tex",child=folder+"/外部章节/child.tex",info=folder+"/private-info.json";
 Research::setBuildCacheForTests(folder+"/cache");Research::setSyncInfoFilesForTests({info});Tw::Settings().setValue("researchBuildOutputModes",QVariantMap());Tw::Settings().setValue("researchManualSyncRoots",QStringList());Tw::Settings().setValue("binaryPaths","C:/texlive/2023/bin/windows");Tw::Settings().setValue("openPDFwithTeX",false);Tw::Settings().setValue("researchCommandCompletionMode",0);
 const Engine engine("Policy test","C:/texlive/2023/bin/windows/pdflatex.exe",{"-synctex=1","-interaction=nonstopmode","-file-line-error","$fullname"},false);app.setEngineList({engine});
 QJsonArray checks,samples;int failures=0;auto save=[&]{put(report,QJsonDocument(QJsonObject{{"checks",checks},{"samples",samples},{"failures",failures},{"physicalInputVerified",false},{"cloudUploadVerified",false}}).toJson());};auto check=[&](const QString &name,bool ok){checks.append(QJsonObject{{"name",name},{"passed",ok}});failures+=!ok;save();};
 const QByteArray simple="\\documentclass{article}\n\\begin{document}Local paper.\\end{document}\n";
 QByteArray source="\\documentclass{article}\n\\usepackage{hyperref}\n\\begin{document}\n\\input{../../外部章节/child}\n\\section{Result}First version.\\end{document}\n";
 check("independent Chinese sync/local/manual/external fixtures",engine.isAvailable()&&put(root,source)&&put(local,simple)&&put(sibling,simple)&&put(manual,simple)&&put(child,"External chapter.\n")&&put(info,QJsonDocument(QJsonObject{{"personal",QJsonObject{{"path",QFileInfo(sync).dir().absolutePath()},{"is_team",false}}}}).toJson()));
 const auto roots=Research::discoverSyncRoots({info},{});const auto nroot=Research::normalizedOutputPath(root);
 check("Dropbox personal path is sync without assuming multi-user sharing",roots.size()==1&&Research::insideSyncRoot(nroot,roots));
 check("name-only and sibling-prefix directories are not sync",!Research::insideSyncRoot(Research::normalizedOutputPath(local),roots)&&!Research::insideSyncRoot(Research::normalizedOutputPath(sibling),roots));
 check("dot traversal is normalized before boundary check",!Research::insideSyncRoot(Research::normalizedOutputPath(sync+"/../../Dropbox-name-only/main.tex"),roots));
 check("Windows path case is normalized consistently",Research::insideSyncRoot(Research::normalizedOutputPath(root.toLower()),roots));
 check("relative metadata paths are rejected",put(folder+"/malformed.json","{\"personal\":{\"path\":\"Dropbox-relative\"}}")&&Research::discoverSyncRoots({folder+"/malformed.json"},{}).isEmpty());
 // These are isolated client-record fixtures, never the user's registry or
 // DriveFS settings. Registration is not proof of remote synchronization.
 const QString clientInfo=folder+"/private-client-records.json",personal=folder+"/个人写作",business=folder+"/研究组文档",school=folder+"/学校材料",google=folder+"/挂载甲",googleSecond=folder+"/挂载乙",stale=folder+"/尚未就绪",mirror=folder+"/未确认镜像";
 bool clientFolders=true;for(const auto &path:QStringList{personal,business,school,google,googleSecond,mirror})clientFolders=put(path+"/main.tex",simple)&&clientFolders;
 auto googleAccount=[](const QString &key,const QJsonObject &value){return QJsonObject{{"key",key},{"value",value}};};
 const QJsonObject googlePreferences{{"per_account_preferences",QJsonArray{
   googleAccount("account-one",{{"mount_point_path",google}}),googleAccount("account-two",{{"mount_point_path",googleSecond}}),
   googleAccount("duplicate",{{"mount_point_path",google}}),googleAccount("not-ready",{{"mount_point_path",stale}}),
   googleAccount("relative",{{"mount_point_path","not/a/root"}}),googleAccount("remote",{{"mount_point_path","//server/share"}}),
   googleAccount("system-volume",{{"mount_point_path",qEnvironmentVariable("SystemDrive")+"/"}}),
   googleAccount("unconfirmed-mirror",{{"mirrored_my_drive_path",mirror}}),googleAccount("",{{"mount_point_path",mirror}})
 }}};
 const QJsonObject clientRecords{{"OneDriveAccounts",QJsonObject{
   {"Personal",QJsonObject{{"UserFolder",personal}}},{"Business1",QJsonObject{{"UserFolder",business}}},{"Business2",QJsonObject{{"UserFolder",school}}},
   {"Business3",QJsonObject{{"UserFolder",business}}},{"Business4",QJsonObject{{"UserFolder",stale}}},{"Business5",QJsonObject{{"UserFolder","relative/path"}}},
   {"Business6",QJsonObject{{"UserFolder",qEnvironmentVariable("SystemRoot")}}},{"Business7",QJsonObject{{"UserFolder","//server/share"}}},
   {"Business8",QJsonObject{{"UserFolder",qEnvironmentVariable("SystemDrive")+"/"}}}
 }},{"GoogleDrivePreferences",QString::fromUtf8(QJsonDocument(googlePreferences).toJson(QJsonDocument::Compact))},{"DefaultMountPoint",mirror}};
 check("private OneDrive and Google per-account records created",clientFolders&&put(clientInfo,QJsonDocument(clientRecords).toJson()));
 const auto registered=Research::discoverSyncRoots({info},{business},clientInfo);
 check("all personal work school and Google registrations merge without duplicates",registered.size()==6&&Research::insideSyncRoot(nroot,registered)&&Research::insideSyncRoot(Research::normalizedOutputPath(personal+"/main.tex"),registered)&&Research::insideSyncRoot(Research::normalizedOutputPath(business+"/main.tex"),registered)&&Research::insideSyncRoot(Research::normalizedOutputPath(school+"/main.tex"),registered)&&Research::insideSyncRoot(Research::normalizedOutputPath(google+"/main.tex"),registered)&&Research::insideSyncRoot(Research::normalizedOutputPath(googleSecond+"/main.tex"),registered));
 check("registered roots reject missing remote system and adjacent paths",!Research::insideSyncRoot(stale+"/main.tex",registered)&&!Research::insideSyncRoot(qEnvironmentVariable("SystemRoot")+"/main.tex",registered)&&!Research::insideSyncRoot(google+"-other/main.tex",registered)&&!Research::insideSyncRoot(personal+"-other/main.tex",registered));
 check("names preferences and unconfirmed mirror paths are not detected",!Research::insideSyncRoot(Research::normalizedOutputPath(local),registered)&&!Research::insideSyncRoot(Research::normalizedOutputPath(mirror+"/main.tex"),registered));
 auto brokenRecords=clientRecords;brokenRecords.insert("GoogleDrivePreferences","not json");
 check("invalid Google record does not suppress valid OneDrive and Dropbox",put(folder+"/invalid-client-records.json",QJsonDocument(brokenRecords).toJson())&&Research::discoverSyncRoots({info},{},folder+"/invalid-client-records.json").size()==4);
 const QString junction=folder+"/registered-system-junction";
 QProcess junctionTask;junctionTask.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *a){a->flags|=0x08000000;});
 junctionTask.start(QDir(qEnvironmentVariable("SystemRoot")).filePath("System32/cmd.exe"),{"/D","/C","mklink","/J",QDir::toNativeSeparators(junction),qEnvironmentVariable("SystemRoot")});
 const bool linked=junctionTask.waitForFinished(5000)&&junctionTask.exitCode()==0;
 auto junctionRecords=clientRecords;junctionRecords.insert("OneDriveAccounts",QJsonObject{{"Personal",QJsonObject{{"UserFolder",junction}}}});junctionRecords.remove("GoogleDrivePreferences");
 check("registered junction cannot bypass system-directory exclusions",linked&&put(folder+"/junction-client-records.json",QJsonDocument(junctionRecords).toJson())&&Research::discoverSyncRoots({}, {},folder+"/junction-client-records.json").isEmpty());
 if(linked)check("private junction removed without touching target",RemoveDirectoryW(QDir::toNativeSeparators(junction).toStdWString().c_str())!=0);
 Research::setSyncClientRecordsForTests(clientInfo);
 for(const auto &project:QStringList{personal+"/main.tex",googleSecond+"/main.tex"}) {
   QObject policyContext;bool ready=false;Research::OutputPolicy policy;Research::requestOutputPolicy(&policyContext,project,[&](Research::OutputPolicy result){policy=result;ready=true;});
   check("registered project uses existing asynchronous output policy: "+QFileInfo(project).dir().dirName(),waitFor([&]{return ready;},10000)&&policy.synchronized&&!policy.cacheOnly);
   Research::rememberOutputPolicy(policy,true);ready=false;Research::requestOutputPolicy(&policyContext,project,[&](Research::OutputPolicy result){policy=result;ready=true;});
   check("registered project remembers local preview without writing sync folder: "+QFileInfo(project).dir().dirName(),waitFor([&]{return ready;},10000)&&policy.cacheOnly&&policy.directory==policy.cacheDirectory&&!QFileInfo::exists(QFileInfo(project).absolutePath()+"/main.pdf"));
   Research::rememberOutputPolicy(policy,false);ready=false;Research::requestOutputPolicy(&policyContext,project,[&](Research::OutputPolicy result){policy=result;ready=true;});
   check("registered project returns to local writeback: "+QFileInfo(project).dir().dirName(),waitFor([&]{return ready;},10000)&&!policy.cacheOnly&&policy.directory==QFileInfo(project).absolutePath());
 }
 check("client records remain unchanged",QJsonDocument::fromJson(read(clientInfo)).object()==clientRecords);
 Research::setSyncInfoFilesForTests({info});Tw::Settings().setValue("researchBuildOutputModes",QVariantMap());
 bool discovered=false;QElapsedTimer discoveryRequest;discoveryRequest.start();Research::requestOutputPolicy(&app,root,[&](Research::OutputPolicy policy){discovered=policy.synchronized;});const double discoveryRequestMs=double(discoveryRequest.nsecsElapsed())/1e6;
 check("first discovery returns before filesystem callback without blocking editor",!discovered&&discoveryRequestMs<50);samples.append(QJsonObject{{"name","first-discovery-request"},{"requestMs",discoveryRequestMs}});check("background discovery completes",waitFor([&]{return discovered;}));
 auto *w=new TeXDocumentWindow(root);w->resize(1120,800);w->show();
 Research::setSyncInfoFilesForTests({info});
 auto *early=w->findChild<QToolButton *>("researchCompile");w->populateResearchEngineMenu(early->menu());
 auto *earlyCache=early->menu()->findChild<QAction *>("researchOutputCacheOnly");
 check("first-open menu asynchronously exposes local preview without reopening",earlyCache&&waitFor([&]{return earlyCache->isVisible()&&earlyCache->isEnabled();}));
 check("asynchronous discovery settles on current project",waitFor([&]{return w->property("researchOutputPolicyReady").toBool();}));
 auto *button=w->findChild<QToolButton *>("researchCompile");auto state=[&]{return w->property("researchPublishStatus").toString();};
 check("first sync default keeps automatic publication and clear caption",w->property("researchSynchronizedProject").toBool()&&!w->property("researchCacheOnly").toBool()&&button&&button->text()==Research::ui("编译")&&button->toolTip().contains("同步目录"));
 check("first discovery does not write per-project preferences",Tw::Settings().value("researchBuildOutputModes").toMap().isEmpty());
 auto menuAction=[&](const QString &name){w->populateResearchEngineMenu(button->menu());return button->menu()->findChild<QAction *>(name);};
 check("split button retains engines plus both output choices",button->popupMode()==QToolButton::MenuButtonPopup&&menuAction("researchOutputCacheOnly")&&menuAction("researchOutputSynchronize")&&button->menu()->actions().size()>4);
 auto compile=[&](const QString &name){QElapsedTimer t;t.start();w->typeset();const bool done=waitFor([&]{return !w->isTypesetting()&&!w->property("researchPublishing").toBool();});samples.append(QJsonObject{{"name",name},{"completeMs",t.nsecsElapsed()/1e6},{"status",state()},{"consoleTail",w->property("consoleOutput").toString().right(1800)}});save();return done;};
 auto snapshot=[&]{QJsonObject o;for(const auto &name:QStringList{"main.pdf","main.synctex.gz","main.aux","main.out","main.log"})o.insert(name,hash(read(dir+"/"+name)));o.insert("private-metadata",hash(read(Research::pdfMetadataFile(dir+"/main.pdf"))));return o;};
 check("sync success publishes certified PDF/SyncTeX and actual side products",compile("sync-initial")&&state()=="synchronized"&&Research::pdfPairValid(dir+"/main.pdf")&&QFileInfo::exists(dir+"/main.aux")&&QFileInfo::exists(dir+"/main.out")&&QFileInfo::exists(dir+"/main.log"));
 check("success message confirms local writeback not Dropbox upload",w->findChild<QLabel *>("researchBuildOutputStatus")->text().contains("上传状态由同步软件决定"));
 const auto valid=snapshot();auto *cacheAction=menuAction("researchOutputCacheOnly");if(cacheAction)cacheAction->trigger();
 samples.append(QJsonObject{{"name","immediate-cache-selection"},{"cacheOnly",w->property("researchCacheOnly").toBool()},{"caption",button->text()},{"buttonBusy",button->property("researchCompileBusy").toBool()},{"typesetting",w->isTypesetting()},{"directory",w->property("researchOutputDirectory").toString()},{"expectedDirectory",QFileInfo(Research::previewFile(root)).absolutePath()}});save();
 check("cache choice immediately refreshes caption hint and output directory",w->property("researchCacheOnly").toBool()&&button->text()==Research::ui("预览")&&w->property("researchOutputDirectory").toString()==QFileInfo(Research::previewFile(root)).absolutePath());
 Research::setUiLocale("en");Research::refreshCompileButton(button);check("destination caption and hint support English without reopening",button->text()=="Preview"&&button->toolTip().contains("local preview")&&button->toolTip().contains("sync folder"));Research::setUiLocale("zh_CN");Research::refreshCompileButton(button);check("destination caption can return to Chinese",button->text()=="预览");
 samples.append(QJsonObject{{"name","compact-compile-control"},{"chineseCaption",button->text()},{"width",button->width()},{"height",button->height()},{"devicePixelRatio",button->devicePixelRatioF()}});save();
 source.replace("First version.","Cache-only revision.");w->editor()->setPlainText(QString::fromUtf8(source));check("cache-only selection still saves ordinary TeX",w->save()&&read(root)==source);
 check("cache-only success retains every prior project output",compile("cache-only")&&state()=="cached"&&snapshot()==valid&&Research::pdfPairValid(Research::previewFile(root))&&read(Research::previewFile(root))!=read(dir+"/main.pdf"));
 check("repeated cache-only success never writes sync outputs",compile("cache-repeat")&&state()=="cached"&&snapshot()==valid);
 auto *other=new TeXDocumentWindow(local);other->show();check("switching to local project refreshes without stale cloud",waitFor([&]{return other->property("researchOutputPolicyReady").toBool();})&&!other->property("researchSynchronizedProject").toBool()&&other->findChild<QToolButton *>("researchCompile")->text()==Research::ui("编译")&&other->property("researchOutputDirectory").toString()==QFileInfo(local).absolutePath());
 other->typeset();check("ordinary local compilation still publishes to project directory",waitFor([&]{return !other->isTypesetting()&&!other->property("researchPublishing").toBool();})&&other->property("researchPublishStatus").toString()=="updated"&&Research::pdfPairValid(QFileInfo(local).absolutePath()+"/main.pdf"));delete other;
 auto *reopened=new TeXDocumentWindow(root);reopened->show();check("reopening same project remembers cache-only choice",waitFor([&]{return reopened->property("researchOutputPolicyReady").toBool();})&&reopened->property("researchCacheOnly").toBool());delete reopened;
 Tw::Settings().setValue("researchManualSyncRoots",QStringList{QFileInfo(manual).absolutePath()});auto *manualWindow=new TeXDocumentWindow(manual);manualWindow->show();check("manual supplement is recognized independently of folder name",waitFor([&]{return manualWindow->property("researchOutputPolicyReady").toBool();})&&manualWindow->property("researchSynchronizedProject").toBool());
 auto *manualButton=manualWindow->findChild<QToolButton *>("researchCompile");manualWindow->populateResearchEngineMenu(manualButton->menu());if(auto *action=manualButton->menu()->findChild<QAction *>("researchOutputCacheOnly"))action->trigger();check("cache destination is known before first PDF exists",manualWindow->property("researchCacheOnly").toBool()&&manualWindow->property("researchOutputDirectory").toString()==Research::buildCacheDirectory(manual)+"/preview"&&!QFileInfo::exists(QFileInfo(manual).absolutePath()+"/main.pdf"));delete manualWindow;
 auto *manage=menuAction("researchSyncDirectories");if(manage)manage->trigger();check("manual sync directory dialog lists private configured roots",waitFor([&]{return w->findChild<QListWidget *>("researchManualSyncRoots");})&&w->findChild<QListWidget *>("researchManualSyncRoots")->count()==1);if(auto *dialog=w->findChild<QWidget *>("researchSyncDirectoriesDialog"))dialog->close();
 Research::setFocusMode(true);check("focus mode keeps destination dropdown and full caption",button->menu()&&button->popupMode()==QToolButton::MenuButtonPopup&&button->fontMetrics().horizontalAdvance(button->text())+38<button->width());w->grab().save(folder+"/focus-cache.png");Research::setFocusMode(false);
 auto *syncAction=menuAction("researchOutputSynchronize");if(syncAction)syncAction->trigger();check("sync choice restores project directory and remembered mode",!w->property("researchCacheOnly").toBool()&&button->text()==Research::ui("编译")&&w->property("researchOutputDirectory").toString()==dir);
 check("new success publishes cached generation as matching project outputs",compile("sync-new")&&state()=="synchronized"&&snapshot()!=valid&&Research::pdfPairValid(dir+"/main.pdf"));const auto finalValid=snapshot();const auto good=source;
 source.replace("Cache-only revision.","\\UndefinedPolicyCommand");w->editor()->setPlainText(QString::fromUtf8(source));check("private failure source saved",w->save());check("sync failure preserves complete published generation",compile("sync-failure")&&state()=="failed"&&snapshot()==finalValid);
 w->findChild<QPushButton *>("researchViewLatestLog")->click();check("failure log remains immediately available",waitFor([&]{return w->findChild<QPlainTextEdit *>("researchLatestLogText");})&&w->findChild<QPlainTextEdit *>("researchLatestLogText")->toPlainText().contains("UndefinedPolicyCommand"));if(auto *dialog=w->findChild<QWidget *>("researchLatestLogDialog"))dialog->close();
 source=good;source.replace("Cache-only revision.","\\newcount\\slowcount\\loop\\advance\\slowcount1\\ifnum\\slowcount<5000000\\repeat Cancellation.");w->editor()->setPlainText(QString::fromUtf8(source));check("private cancellation fixture saved",w->save());w->typeset();check("cancel reaches real owned compiler and mode cannot change mid-build",waitFor([&]{return state()=="building";})&&menuAction("researchOutputCacheOnly")&&!menuAction("researchOutputCacheOnly")->isEnabled());w->interrupt();check("sync cancellation preserves previous generation",waitFor([&]{return !w->isTypesetting()&&!w->property("researchPublishing").toBool();})&&state()=="cancelled"&&snapshot()==finalValid);
 source=good;source.replace("Cache-only revision.","\\newcount\\slowcount\\loop\\advance\\slowcount1\\ifnum\\slowcount<500000\\repeat Race.");w->editor()->setPlainText(QString::fromUtf8(source));w->save();w->typeset();check("external chapter update during compilation recorded",waitFor([&]{return state()=="building";})&&put(child,"Externally changed during build.\n"));check("source version change cannot publish mismatched outputs",waitFor([&]{return !w->isTypesetting()&&!w->property("researchPublishing").toBool();})&&state()=="not-published"&&snapshot()==finalValid);
 put(child,"External chapter.\n");source=good;source.replace("Cache-only revision.","Final locked revision.");w->editor()->setPlainText(QString::fromUtf8(source));w->save();const auto target=QDir::toNativeSeparators(dir+"/main.pdf").toStdWString();HANDLE locked=CreateFileW(target.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);check("real Windows output lock acquired",locked!=INVALID_HANDLE_VALUE);
 check("sync write failure is distinct from successful cached preview",compile("locked")&&state()=="write-failed"&&snapshot()==finalValid&&Research::pdfPairValid(Research::previewFile(root)));if(locked!=INVALID_HANDLE_VALUE)CloseHandle(locked);
 if(auto *action=menuAction("researchOutputCacheOnly"))action->trigger();check("cache-only mode hides old writeback retry",w->findChild<QPushButton *>("researchRetryPublication")->isHidden());
 if(auto *action=menuAction("researchOutputSynchronize"))action->trigger();check("returning to sync mode restores failed-write retry",!w->findChild<QPushButton *>("researchRetryPublication")->isHidden());
 w->findChild<QPushButton *>("researchRetryPublication")->click();check("visible retry writes certified cache without recompiling",waitFor([&]{return !w->property("researchPublishing").toBool();})&&state()=="synchronized"&&Research::pdfPairValid(dir+"/main.pdf"));
 check("private info.json remains unchanged by detection",QJsonDocument::fromJson(read(info)).object().value("personal").toObject().value("path").toString()==QFileInfo(sync).dir().absolutePath());
 w->grab().save(folder+"/sync-output.png");w->setModified(false);delete w;save();app.exit(failures?1:0);
 });return app.exec();}
