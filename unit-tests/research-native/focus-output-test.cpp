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
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMenu>
#include <QPlainTextEdit>
#include <QSaveFile>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QLayout>
#include <QLabel>
#include <QWidgetAction>
#include <windows.h>
static bool put(const QString &path,const QByteArray &bytes){QDir().mkpath(QFileInfo(path).absolutePath());QSaveFile file(path);return file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size()&&file.commit();}
static bool waitFor(const std::function<bool()> &ready,int ms=60000){QElapsedTimer clock;clock.start();while(!ready()&&clock.elapsed()<ms){QCoreApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QThread::msleep(2);}return ready();}
static void settle(int ms=600){QElapsedTimer clock;clock.start();waitFor([&]{return clock.elapsed()>=ms;},ms+1000);}
int main(int argc,char **argv){const QString report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);QTimer::singleShot(100,&app,[&]{
 const QString folder=QFileInfo(report).absolutePath(),sync=folder+"/private-sync",info=folder+"/private-info.json";
 Research::setBuildCacheForTests(folder+"/cache");Research::setSyncInfoFilesForTests({info});Tw::Settings().setValue("researchBuildOutputModes",QVariantMap());Tw::Settings().setValue("researchManualSyncRoots",QStringList());Tw::Settings().setValue("openPDFwithTeX",false);Tw::Settings().setValue("researchCommandCompletionMode",0);Tw::Settings().setValue("binaryPaths","C:/texlive/2023/bin/windows");
 const Engine engine("Focus output","C:/texlive/2023/bin/windows/pdflatex.exe",{"-synctex=1","-interaction=nonstopmode","-file-line-error","$fullname"},false);app.setEngineList({engine});
 QJsonArray checks,geometry;int failures=0;auto save=[&]{put(report,QJsonDocument(QJsonObject{{"checks",checks},{"geometry",geometry},{"failures",failures},{"physicalInputVerified",false},{"actualDropboxDirectoryTouched",false}}).toJson());};auto check=[&](const QString &name,bool ok){checks.append(QJsonObject{{"name",name},{"passed",ok}});failures+=!ok;save();};
 const QByteArray source="\\documentclass{article}\n\\begin{document}Private focus test.\\end{document}\n";
 QDir().mkpath(sync);check("private compiler and simulated sync configuration",engine.isAvailable()&&put(info,QJsonDocument(QJsonObject{{"personal",QJsonObject{{"path",sync}}}}).toJson()));
 for(const auto &mode:QStringList{"cache","sync","local"}){
  Research::setFocusMode(false);const QString root=(mode=="local"?folder+"/local":sync+"/"+mode)+"/main.tex";check(mode+" fixture",put(root,source));
  auto *window=new TeXDocumentWindow(root);window->resize(1120,800);window->show();check(mode+" policy ready",waitFor([&]{return window->property("researchOutputPolicyReady").toBool();}));
  auto *bar=window->findChild<QWidget *>("researchBuildOutput");auto *button=window->findChild<QToolButton *>("researchCompile");
  auto action=[&](const QString &name){window->populateResearchEngineMenu(button->menu());return button->menu()->findChild<QAction *>(name);};
  if(mode=="cache")if(auto *choice=action("researchOutputCacheOnly"))choice->trigger();
  auto focus=[&](bool enabled){Research::setFocusMode(enabled);const bool changed=waitFor([&]{return window->property("researchFocus").toBool()==enabled;});settle();return changed;};
  auto state=[&]{return window->property("researchPublishStatus").toString();};
  bool visibleDuringFocus=false;QTimer monitor;monitor.setInterval(5);QObject::connect(&monitor,&QTimer::timeout,window,[&]{if(window->property("researchFocus").toBool()&&bar->isVisible())visibleDuringFocus=true;});monitor.start();
  auto compile=[&]{window->typeset();return waitFor([&]{return !window->isTypesetting()&&!window->property("researchPublishing").toBool()&&!window->property("researchBuildPreparing").toBool()&&!window->property("researchBuildFinishing").toBool();});};
  const QString expected=mode=="cache"?"cached":mode=="sync"?"synchronized":"updated";
  check(mode+" initial footer hidden",bar&&bar->isHidden());check(mode+" enter focus before compile",focus(true));
  check(mode+" actual focused compile succeeds",compile()&&state()==expected&&Research::pdfPairValid(Research::previewFile(root)));
  settle(220);check(mode+" focused compile never reveals footer",bar->isHidden()&&!visibleDuringFocus);
  auto *split=window->findChild<QWidget *>("splitter");const int bottom=split->mapTo(window->centralWidget(),QPoint(0,split->height())).y();geometry.append(QJsonObject{{"mode",mode},{"centralHeight",window->centralWidget()->height()},{"splitterBottom",bottom},{"barHidden",bar->isHidden()}});save();
  check(mode+" footer consumes no focus layout space",bottom>=window->centralWidget()->height()-2);window->grab().save(folder+"/"+mode+"-focused.png");
  check(mode+" exit focus restores normal status without a duplicate success capsule",focus(false)&&window->statusBar()->isVisible()&&bar->isHidden()&&state()==expected);
  check(mode+" normal-mode repeat compile succeeds",compile()&&bar->isHidden()&&state()==expected&&Research::pdfPairValid(Research::previewFile(root)));
  settle(100); // Measure the committed UI layout, not the worker callback's frame.
  auto *summary=window->findChild<QToolButton *>("researchBuildSummary");
  check(mode+" output is integrated in existing status row",summary&&window->statusBar()->isAncestorOf(bar)&&window->centralWidget()->layout()->indexOf(bar)==-1&&window->statusBar()->height()<40);
  check(mode+" summary is short and source position unframed",summary&&summary->text().size()<24&&window->statusBar()->findChildren<QLabel *>().size()>0);
  check(mode+" complete summary text fits rendered width",summary&&(bar->isHidden()||summary->width()>=summary->fontMetrics().horizontalAdvance(summary->text())+20));
  if(summary){geometry.append(QJsonObject{{"mode",mode},{"summary",summary->text()},{"summaryWidth",summary->width()},{"sizeHintWidth",summary->sizeHint().width()},{"textWidth",summary->fontMetrics().horizontalAdvance(summary->text())},{"minimumWidth",summary->minimumWidth()},{"maximumWidth",summary->maximumWidth()},{"localStyle",summary->styleSheet()}});save();}
  check(mode+" managed compile has no duplicated legacy message",bar->isHidden()&&!window->statusBar()->currentMessage().isEmpty());
  if(summary){window->statusBar()->grab().save(folder+"/"+mode+"-status.png");
   auto *popup=summary->menu();QMetaObject::invokeMethod(popup,"aboutToShow",Qt::DirectConnection);
   auto *details=popup->findChild<QLabel *>("researchBuildDetails");check(mode+" full output path is available on demand",details&&details->text().contains(QDir::toNativeSeparators(window->property("researchOutputDirectory").toString()))&&popup->findChild<QAction *>("researchViewLatestLogAction"));
   window->statusBar()->showMessage("A separate save/recovery notice",100);check(mode+" unrelated status notice remains visible",window->statusBar()->currentMessage()=="A separate save/recovery notice");settle(140);check(mode+" summary returns after unrelated notice",window->statusBar()->currentMessage().isEmpty()&&bar->isHidden());
   window->statusBar()->showMessage("Important recovery notice",4000);focus(true);focus(false);check(mode+" focus switching does not cover an important notice",window->statusBar()->currentMessage()=="Important recovery notice"&&bar->isHidden());window->statusBar()->clearMessage();check(mode+" status returns after clearing an important notice",bar->isHidden());
   if(mode=="cache"){popup->popup(summary->mapToGlobal(QPoint(0,0)));settle(100);popup->grab().save(folder+"/status-details.png");popup->hide();}
   window->resize(560,650);settle(140);check(mode+" compact half-width status does not overflow",summary->geometry().right()<=window->statusBar()->width()&&bar->height()<40);window->resize(1120,800);
  }
  visibleDuringFocus=false;check(mode+" entering focus after compile removes footer",focus(true)&&bar->isHidden());
  auto *log=action("researchViewLatestLogAction");auto *directory=button->menu()->findChild<QAction *>("researchOpenOutputDirectoryAction");check(mode+" focus dropdown retains log and directory access",log&&log->isEnabled()&&directory&&directory->isEnabled()&&!directory->toolTip().isEmpty());
  if(log)log->trigger();check(mode+" focus log opens without restoring footer",waitFor([&]{return window->findChild<QPlainTextEdit *>("researchLatestLogText");},1000)&&bar->isHidden());if(auto *dialog=window->findChild<QWidget *>("researchLatestLogDialog"))dialog->close();
  check(mode+" focus keeps the bottom edge free while source editing remains available",window->statusBar()->isHidden()&&window->editor()->isVisible()&&window->cursorPosition()>=0);
  if(mode=="sync"){
   window->editor()->setPlainText("\\documentclass{article}\n\\begin{document}\\UndefinedFocusCommand\\end{document}\n");check("private failure source saved",window->save());
   check("failed focus compilation keeps footer hidden",compile()&&state()=="failed"&&!bar->isVisible());
   if(auto *failedLog=action("researchViewLatestLogAction"))failedLog->trigger();check("failed latest log remains available in focus",waitFor([&]{return window->findChild<QPlainTextEdit *>("researchLatestLogText");},1000)&&window->findChild<QPlainTextEdit *>("researchLatestLogText")->toPlainText().contains("UndefinedFocusCommand"));if(auto *dialog=window->findChild<QWidget *>("researchLatestLogDialog"))dialog->close();
   window->editor()->setPlainText(QString::fromUtf8(source).replace("Private focus test.","Locked next private version."));window->save();const auto path=QDir::toNativeSeparators(QFileInfo(root).absolutePath()+"/main.pdf").toStdWString();HANDLE handle=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);check("owned PDF lock acquired",handle!=INVALID_HANDLE_VALUE);
   check("writeback failure does not unhide focus footer",compile()&&state()=="write-failed"&&!bar->isVisible());if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);
   auto *retry=action("researchRetryPublicationAction");check("writeback retry remains accessible in focus dropdown",retry&&retry->isEnabled());if(retry)retry->trigger();
   check("retry updates private sync output without showing footer",waitFor([&]{return !window->property("researchPublishing").toBool();})&&state()=="synchronized"&&bar->isHidden());
  }
  monitor.stop();check(mode+" final exit retains last output state",focus(false)&&window->statusBar()->isVisible()&&bar->isHidden());window->setModified(false);delete window;
 }
 save();app.exit(failures?1:0);
 });return app.exec();}
