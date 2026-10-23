// Visible build destinations; filesystem publication stays off the editor thread.
#include "ResearchPublication.h"
#include "ResearchBuild.h"
#include "ResearchWorkflow.h"
#include "ResearchAppearance.h"
#include "ResearchOutputPolicy.h"
#include "TeXDocumentWindow.h"
#include "TWApp.h"
#include "utils/FilePath.h"
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDynamicPropertyChangeEvent>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QResizeEvent>
#include <QSet>
#include <QThreadPool>
#include <QStatusBar>
#include <QStyle>
#include <QToolButton>
#include <QUrl>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <atomic>
#include <memory>

namespace Research {
namespace {
class OutputPath final : public QLabel {
public:
 using QLabel::QLabel;
 void setPath(const QString &value){full=value;setToolTip(full);refresh();}
protected:
 void resizeEvent(QResizeEvent *event) override {QLabel::resizeEvent(event);refresh();}
private:
 QString full;
 void refresh(){setText(fontMetrics().elidedText(full,Qt::ElideMiddle,qMax(0,contentsRect().width())));}
};
class BuildOutput final : public QObject {
public:
 explicit BuildOutput(TeXDocumentWindow *owner) : QObject(owner), window(owner) {
  setObjectName(QStringLiteral("researchBuildOutputController"));
  auto *statusBar=owner->statusBar();statusBar->setProperty("researchSourceStatus",true);
  bar=new QWidget(statusBar);bar->setObjectName(QStringLiteral("researchBuildOutput"));
  auto *layout=new QHBoxLayout(bar);layout->setContentsMargins(0,0,0,0);layout->setSpacing(0);
  summary=new QToolButton(bar);summary->setObjectName(QStringLiteral("researchBuildSummary"));
  // The application can style a newly parented button before it is named.
  // Drop those toolbar limits as well as excluding later toolbar refreshes.
  summary->setStyleSheet(QString());summary->setMinimumSize(0,0);summary->setMaximumSize(QWIDGETSIZE_MAX,QWIDGETSIZE_MAX);summary->setFont(statusBar->font());summary->setSizePolicy(QSizePolicy::Maximum,QSizePolicy::Fixed);
  summary->setToolButtonStyle(Qt::ToolButtonTextOnly);summary->setPopupMode(QToolButton::InstantPopup);summary->setFocusPolicy(Qt::StrongFocus);summary->setFixedHeight(22);layout->addWidget(summary);layout->addStretch();
  auto *menu=new QMenu(summary);menu->setObjectName(QStringLiteral("researchBuildSummaryMenu"));summary->setMenu(menu);
  connect(menu,&QMenu::aboutToShow,this,[this,menu]{populateDetails(menu);});
  // Reuse command handlers and enabled state; only the summary is rendered.
  // Full paths and commands are available on demand in its menu.
  auto *commands=new QWidget(bar);commands->hide();
  status=new QLabel(commands);status->setTextFormat(Qt::PlainText);status->setObjectName(QStringLiteral("researchBuildOutputStatus"));
  location=new OutputPath(commands);
  auto button=[&](const QString &name,const QString &caption){auto *b=new QPushButton(ui(caption),commands);b->setObjectName(name);return b;};
  auto *folder=button(QStringLiteral("researchOpenOutputDirectory"),QStringLiteral("打开输出目录"));
  auto *log=button(QStringLiteral("researchViewLatestLog"),QStringLiteral("查看日志"));
  retry=button(QStringLiteral("researchRetryPublication"),QStringLiteral("重试写回"));
  cleanup=button(QStringLiteral("researchCleanupArtifacts"),QStringLiteral("清理辅助文件…"));
  retry->hide();cleanup->setEnabled(false);
  statusBar->insertWidget(0,bar,1);
  bar->hide();
  connect(folder,&QPushButton::clicked,this,[this]{if(!directory.isEmpty())QDesktopServices::openUrl(QUrl::fromLocalFile(directory));});
  connect(log,&QPushButton::clicked,this,[this]{showLog();});
  connect(retry,&QPushButton::clicked,this,[this]{publish();});
  connect(cleanup,&QPushButton::clicked,this,[this]{planCleanup();});
  // Newly opened dependencies must also cancel a pending publication on editing.
  connect(TWApp::instance(),&TWApp::windowListChanged,this,[this]{watchInputs();});
  connect(owner,&QWidget::windowTitleChanged,this,[this]{scheduleDestination();});
  connect(owner->textDoc(),&Tw::Document::TeXDocument::modelinesChanged,this,[this](const QStringList &changed,const QStringList &removed){if(changed.contains(QStringLiteral("root"))||removed.contains(QStringLiteral("root")))scheduleDestination();});
  owner->installEventFilter(this);
  connect(statusBar,&QStatusBar::messageChanged,this,[this]{refreshVisibility();});
  scheduleDestination();
 }
 ~BuildOutput() override { cancel(); }
 void cancel(){if(token)token->store(true);pending=false;}
 void started(const BuildJob &job){
  cancel();root=job.root;latestLog=job.logFile;logStartedAt=job.startedAt;fallbackLog.clear();++generation;
  policyReady=false;completionPending=false;buildPolicy={};
  setDirectory(QFileInfo(root).absolutePath());
  window->setProperty("researchLatestLog",latestLog);window->setProperty("researchOutputCancelled",false);
  hasOutput=true;refreshVisibility();retry->hide();cleanup->setEnabled(false);setState(QStringLiteral("building"),QStringLiteral("正在编译 · 本机缓存"));
  const auto serial=generation;requestOutputPolicy(this,root,[this,serial](OutputPolicy policy){if(serial!=generation)return;buildPolicy=policy;policyReady=true;setDirectory(policy.directory);if(completionPending){completionPending=false;publishOrPreview();}else if(window->isTypesetting())setState(QStringLiteral("building"),policy.cacheOnly?QStringLiteral("正在编译 · 仅本机预览"):policy.synchronized?QStringLiteral("正在编译 · 成功后写回同步目录"):QStringLiteral("正在编译 · 成功后更新项目目录"));});
 }
 void finished(const BuildJob &job,bool failed,int promoted){
  root=job.root;latestLog=job.logFile;fallbackLog=window->property("consoleOutput").toString();
  window->setProperty("researchLatestLog",latestLog);
  if(failed){setState(window->property("researchOutputCancelled").toBool()?QStringLiteral("cancelled"):QStringLiteral("failed"),QStringLiteral("编译未完成 · 上一份目录产物保留；可查看本次日志"));return;}
  if(job.managed&&promoted==1){if(policyReady)publishOrPreview();else{completionPending=true;window->setProperty("researchPublishing",true);}return;}
  if(job.managed){setState(QStringLiteral("not-published"),promoted==-1?QStringLiteral("源码版本已变化 · 上一份目录产物保留，请重新编译"):QStringLiteral("缓存预览更新失败 · 上一份目录产物保留"));return;}
  setState(QStringLiteral("custom"),QStringLiteral("编译完成 · 自定义工具使用自身的输出位置"));
 }
 void destination(const QString &path,const QString &selectedRoot){
  if(busy||window->isTypesetting())return;
  if(!root.isEmpty()&&root.compare(selectedRoot,Qt::CaseInsensitive)!=0&&buildPolicy.root.compare(selectedRoot,Qt::CaseInsensitive)!=0){root=selectedRoot;latestLog.clear();fallbackLog.clear();retry->hide();cleanup->setEnabled(false);setState(QStringLiteral("ready"),QStringLiteral("等待编译"));}
  setDirectory(path);retry->setVisible(!window->property("researchCacheOnly").toBool()&&window->property("researchPublishStatus").toString()==QStringLiteral("write-failed"));
 }
 void addMenu(QMenu *menu){
  if(!hasOutput)return;
  menu->addSection(ui(QStringLiteral("编译输出")));
  auto add=[&](const QString &name,const QString &caption,QPushButton *button,bool enabled){
   auto *action=menu->addAction(ui(caption));action->setObjectName(name+QStringLiteral("Action"));action->setEnabled(enabled&&button->isEnabled());
   connect(action,&QAction::triggered,button,&QPushButton::click);return action;
  };
  auto *folder=window->findChild<QPushButton *>(QStringLiteral("researchOpenOutputDirectory"));
  auto *log=window->findChild<QPushButton *>(QStringLiteral("researchViewLatestLog"));
  add(folder->objectName(),QStringLiteral("打开输出目录"),folder,!directory.isEmpty())->setToolTip(QDir::toNativeSeparators(directory));
  add(log->objectName(),QStringLiteral("查看日志"),log,!latestLog.isEmpty()||!fallbackLog.isEmpty());
  if(!retry->isHidden())add(retry->objectName(),QStringLiteral("重试写回"),retry,true);
  add(cleanup->objectName(),QStringLiteral("清理辅助文件…"),cleanup,true);menu->addSeparator();
 }
protected:
 bool eventFilter(QObject *object,QEvent *event) override {
  if(object==window&&event->type()==QEvent::DynamicPropertyChange&&static_cast<QDynamicPropertyChangeEvent *>(event)->propertyName()==QByteArrayLiteral("researchFocus"))refreshVisibility();
  return QObject::eventFilter(object,event);
 }
private:
 TeXDocumentWindow *window;QWidget *bar;QToolButton *summary;QLabel *status;OutputPath *location;QPushButton *retry,*cleanup;
 QString root,directory,latestLog,fallbackLog;bool busy=false,pending=false,policyReady=false,completionPending=false,destinationQueued=false,hasOutput=false;OutputPolicy buildPolicy;quint64 generation=0;qint64 logStartedAt=0;
 std::shared_ptr<std::atomic_bool> token;QJsonObject inputs;
 QList<QMetaObject::Connection> watchers;QSet<QTextDocument *> watched;
 void refreshVisibility(){const auto state=window->property("researchPublishStatus").toString();const bool attention=state==QStringLiteral("write-failed")||state==QStringLiteral("failed")||state==QStringLiteral("not-published");bar->setVisible(hasOutput&&attention&&window->statusBar()->currentMessage().isEmpty());}
 void scheduleDestination(){if(destinationQueued)return;destinationQueued=true;QTimer::singleShot(0,this,[this]{destinationQueued=false;refreshBuildDestination(window);});}
 void setDirectory(const QString &path){directory=path;window->setProperty("researchOutputDirectory",directory);location->setPath(ui(QStringLiteral("输出目录：%1")).arg(QDir::toNativeSeparators(directory)));refreshSummaryTip();}
 void refreshSummaryTip(){summary->setToolTip(status->text()+QStringLiteral("\n")+ui(QStringLiteral("输出目录：%1")).arg(QDir::toNativeSeparators(directory)));summary->setAccessibleDescription(summary->toolTip());}
 void populateDetails(QMenu *menu){
  menu->clear();auto *information=new QWidgetAction(menu);auto *label=new QLabel(status->text()+QStringLiteral("\n\n")+ui(QStringLiteral("输出目录：%1")).arg(QDir::toNativeSeparators(directory)),menu);
  label->setObjectName(QStringLiteral("researchBuildDetails"));label->setTextFormat(Qt::PlainText);label->setTextInteractionFlags(Qt::TextSelectableByMouse);label->setWordWrap(true);label->setFixedWidth(qBound(240,window->width()-64,440));label->setContentsMargins(12,8,12,8);label->setStyleSheet(QStringLiteral("QLabel { color: #6e6e73; font-size: 12px; background: transparent; }"));information->setDefaultWidget(label);menu->addAction(information);addMenu(menu);
 }
 QString briefState(const QString &state)const{
  if(state==QStringLiteral("building"))return QStringLiteral("编译中…");
  if(state==QStringLiteral("publishing"))return QStringLiteral("正在写回…");
  if(state==QStringLiteral("cached"))return QStringLiteral("已编译 · 仅本机");
  if(state==QStringLiteral("synchronized"))return QStringLiteral("已编译 · 已写回");
  if(state==QStringLiteral("updated"))return QStringLiteral("已编译 · 目录已更新");
  if(state==QStringLiteral("write-failed"))return QStringLiteral("写回失败 · 可重试");
  if(state==QStringLiteral("cancelled"))return QStringLiteral("编译已取消");
  if(state==QStringLiteral("failed"))return QStringLiteral("编译失败 · 查看日志");
  if(state==QStringLiteral("not-published"))return QStringLiteral("产物未更新 · 查看详情");
  if(state==QStringLiteral("ready"))return QStringLiteral("等待编译");
  if(state==QStringLiteral("custom"))return QStringLiteral("编译完成");
  return QStringLiteral("编译输出");
 }
 void publishOrPreview(){
  window->setProperty("researchPublishing",false);
  if(buildPolicy.cacheOnly){setDirectory(buildPolicy.directory);setState(QStringLiteral("cached"),QStringLiteral("编译成功 · 仅本机缓存，未写回同步目录"));retry->hide();cleanup->setEnabled(true);return;}
  publish();
 }
 void setState(const QString &state,const QString &caption,const QString &detail={}){
  const QString previousState=window->property("researchPublishStatus").toString();
  window->setProperty("researchPublishStatus",state);status->setText(ui(caption)+(detail.isEmpty()?QString():QStringLiteral("\n")+detail));
  status->setToolTip(status->text());
  const QString source=briefState(state);summary->setText(ui(source));summary->setAccessibleName(ui(source));summary->setProperty("researchBuildAttention",state==QStringLiteral("write-failed")||state==QStringLiteral("failed")||state==QStringLiteral("not-published"));summary->style()->unpolish(summary);summary->style()->polish(summary);refreshSummaryTip();
  refreshVisibility();
  if(state!=previousState&&(state==QStringLiteral("updated")||state==QStringLiteral("cached")||state==QStringLiteral("synchronized")||state==QStringLiteral("custom"))&&!window->property("researchSlideMode").toBool())showTransientStatusMessage(window,ui(QStringLiteral("编译完成")));
 }
 void watchInputs(){
  if(!busy||!token)return;
  for(auto *document:TeXDocumentWindow::documentList()){
   if(!inputs.contains(Tw::Utils::canonicalSourcePath(document->fileName())))continue;
   if(document->textDoc()->isModified())token->store(true);
   if(watched.contains(document->textDoc()))continue;
   watched.insert(document->textDoc());const auto cancellation=token;
   watchers.append(connect(document->textDoc(),&QTextDocument::contentsChange,this,[cancellation](int,int removed,int added){if(removed||added)cancellation->store(true);}));
  }
 }
 void done(){busy=false;for(const auto &connection:watchers)disconnect(connection);watchers.clear();watched.clear();inputs={};window->setProperty("researchPublishing",false);}
 void publish(){
  if(root.isEmpty())return;if(busy){pending=true;return;}
  busy=true;pending=false;token=std::make_shared<std::atomic_bool>(false);inputs=currentBuildMetadata(root).value(QStringLiteral("inputs")).toObject();watchInputs();
  const auto cancellation=token;const auto serial=generation;const QString source=root;QPointer<BuildOutput> guard(this);
  window->setProperty("researchPublishing",true);retry->setEnabled(false);retry->hide();cleanup->setEnabled(false);
  setDirectory(QFileInfo(root).absolutePath());setState(QStringLiteral("publishing"),buildPolicy.synchronized?QStringLiteral("编译成功 · 正在写回同步目录…"):QStringLiteral("编译成功 · 正在更新论文目录…"));
  QThreadPool::globalInstance()->start([guard,cancellation,serial,source]{
   QString error;const bool ok=publishGeneratedPreview(source,&error,[cancellation]{return cancellation->load();});
   QMetaObject::invokeMethod(qApp,[guard,serial,ok,error]{if(!guard)return;const bool latest=serial==guard->generation;guard->done();
    if(latest){guard->setState(ok?(guard->buildPolicy.synchronized?QStringLiteral("synchronized"):QStringLiteral("updated")):QStringLiteral("write-failed"),ok?(guard->buildPolicy.synchronized?QStringLiteral("编译成功 · 已写回同步目录；上传状态由同步软件决定"):QStringLiteral("编译成功 · 论文目录已更新")):QStringLiteral("编译成功 · 目录写回失败，缓存预览保留"),error);guard->retry->setVisible(!ok);guard->retry->setEnabled(true);guard->cleanup->setEnabled(ok);}
    if(guard->pending){guard->pending=false;guard->publish();}
   },Qt::QueuedConnection);
  });
 }
 void showLog(){
  auto *dialog=new QDialog(window);dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setObjectName(QStringLiteral("researchLatestLogDialog"));dialog->setWindowTitle(ui(QStringLiteral("查看日志")));dialog->resize(820,580);
  auto *layout=new QVBoxLayout(dialog);auto *caption=new QLabel(latestLog,dialog);caption->setWordWrap(true);caption->setTextInteractionFlags(Qt::TextSelectableByMouse);layout->addWidget(caption);
  auto *text=new QPlainTextEdit(dialog);text->setReadOnly(true);text->setObjectName(QStringLiteral("researchLatestLogText"));QFile file(latestLog);
  text->setPlainText(QFileInfo(latestLog).lastModified().toMSecsSinceEpoch()>=logStartedAt&&file.open(QIODevice::ReadOnly)?QString::fromUtf8(file.read(8*1024*1024)):fallbackLog);layout->addWidget(text);
  auto *buttons=new QDialogButtonBox(QDialogButtonBox::Close,dialog);connect(buttons,&QDialogButtonBox::rejected,dialog,&QDialog::close);layout->addWidget(buttons);dialog->open();
 }
 void planCleanup(){
  if(busy||root.isEmpty())return;cleanup->setEnabled(false);const QString source=root;const auto serial=generation;QPointer<BuildOutput> guard(this);
  QThreadPool::globalInstance()->start([guard,source,serial]{const auto plan=legacyArtifactPlan(source);QMetaObject::invokeMethod(qApp,[guard,source,serial,plan]{
   if(!guard||serial!=guard->generation)return;guard->cleanup->setEnabled(true);
   auto *dialog=new QDialog(guard->window);dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setObjectName(QStringLiteral("researchArtifactCleanupDialog"));dialog->setWindowTitle(ui(QStringLiteral("清理辅助文件…")));dialog->resize(650,420);
   auto *layout=new QVBoxLayout(dialog);auto *caption=new QLabel(ui(QStringLiteral("选择要清理的辅助文件。清理前会自动备份。")),dialog);caption->setWordWrap(true);layout->addWidget(caption);
   auto *list=new QListWidget(dialog);layout->addWidget(list);for(const auto &value:plan){auto *item=new QListWidgetItem(value.toObject().value(QStringLiteral("name")).toString(),list);item->setData(Qt::UserRole,value.toObject());item->setCheckState(Qt::Checked);}
   if(plan.isEmpty())layout->addWidget(new QLabel(ui(QStringLiteral("没有可清理的辅助文件。")),dialog));
   auto *buttons=new QDialogButtonBox(QDialogButtonBox::Cancel,dialog);auto *accept=buttons->addButton(ui(QStringLiteral("备份并整理")),QDialogButtonBox::AcceptRole);accept->setEnabled(!plan.isEmpty());layout->addWidget(buttons);
   connect(buttons,&QDialogButtonBox::rejected,dialog,&QDialog::close);
   connect(accept,&QPushButton::clicked,guard,[guard,dialog,list,source,serial]{if(!guard||guard->busy||guard->generation!=serial){dialog->close();return;}QJsonArray chosen;for(int i=0;i<list->count();++i)if(list->item(i)->checkState()==Qt::Checked)chosen.append(list->item(i)->data(Qt::UserRole).value<QJsonObject>());if(chosen.isEmpty())return;
    dialog->close();guard->busy=true;guard->token=std::make_shared<std::atomic_bool>(false);guard->inputs=currentBuildMetadata(source).value(QStringLiteral("inputs")).toObject();guard->watchInputs();const auto cancellation=guard->token;guard->cleanup->setEnabled(false);
    QThreadPool::globalInstance()->start([guard,source,serial,chosen,cancellation]{QString backup,error;const bool ok=cleanLegacyArtifacts(source,chosen,&backup,&error,[cancellation]{return cancellation->load();});QMetaObject::invokeMethod(qApp,[guard,serial,ok,backup,error]{if(!guard)return;guard->done();guard->window->setProperty("researchCleanupBackup",backup);if(serial==guard->generation){guard->setState(QStringLiteral("updated"),ok?QStringLiteral("辅助文件已清理。"):QStringLiteral("清理未完成，请查看备份。"),backup+QStringLiteral("\n")+error);guard->cleanup->setEnabled(true);}if(guard->pending){guard->pending=false;guard->publish();}},Qt::QueuedConnection);});
   });dialog->open();
  },Qt::QueuedConnection);});
 }
};
BuildOutput *controller(TeXDocumentWindow *window){return static_cast<BuildOutput *>(window->findChild<QObject *>(QStringLiteral("researchBuildOutputController")));}
}
void installBuildOutput(TeXDocumentWindow *window){if(!controller(window))new BuildOutput(window);}
void buildOutputStarted(TeXDocumentWindow *window,const BuildJob &job){if(auto *output=controller(window))output->started(job);}
void buildOutputFinished(TeXDocumentWindow *window,const BuildJob &job,bool failed,int promoted){if(auto *output=controller(window))output->finished(job,failed,promoted);}
void cancelBuildOutput(TeXDocumentWindow *window){if(auto *output=controller(window))output->cancel();}
void refreshBuildOutputDestination(TeXDocumentWindow *window,const QString &directory,const QString &root){if(auto *output=controller(window))output->destination(directory,root);}
void addBuildOutputMenu(TeXDocumentWindow *window,QMenu *menu){if(auto *output=controller(window))output->addMenu(menu);}
}
