// On-demand setup diagnostics; no polling or automatic installation. GPL-2.0-or-later.
#include "ResearchVersionControl.h"
#include "ResearchWorkspaceUi.h"
#include "ResearchTask.h"
#include "TeXDocumentWindow.h"
#include "CompletingEdit.h"
#include "TWApp.h"
#include <QDesktopServices>
#include <QAction>
#include <QFileDialog>
#include <QListWidget>
#include <QMenu>
#include <QPointer>
#include <QScrollBar>
#include <QToolButton>
#include <QUrl>
namespace Research {
namespace {
class EnvironmentDialog final : public MaterialDialog {
  QPointer<TeXDocumentWindow> owner;
  QString missingTool;
  QListWidget *list;
  QLabel *notice;
  QPushButton *retry{nullptr};
  QList<QPointer<ExternalTask>> tasks;
  int epoch{0},running{0};
  bool away{false},pending{false},scheduled{false},closed{false};
  bool event(QEvent *event) override {
    if(event->type()==QEvent::WindowDeactivate)away=true;
    else if(event->type()==QEvent::WindowActivate&&away){away=false;requestCheck();}
    return MaterialDialog::event(event);
  }
  void scheduleCheck(){
    if(!pending||scheduled||running||closed)return;
    scheduled=true;
    QTimer::singleShot(0,this,[this]{scheduled=false;if(pending&&!running&&!closed&&isVisible())check();});
  }
  void showDetails(){if(auto *item=list->currentItem())notice->setText(item->data(Qt::UserRole).toString());}
  void check(){
    if(closed)return;
    pending=false;const int ticket=++epoch;tasks.clear();
    setProperty("researchEnvironmentCheckCount",property("researchEnvironmentCheckCount").toInt()+1);
    const auto selected=list->currentItem()?list->currentItem()->data(Qt::UserRole+1).toString():QString();
    const int scroll=list->verticalScrollBar()->value();
    list->clear();if(retry)retry->setEnabled(false);
    const auto paths=TWApp::instance()->getBinaryPaths();
    notice->setText(words("写作需至少一个 TeX 编译器；Git 与 Zotero 用于可选工作流。","Writing needs a TeX engine; Git and Zotero support optional workflows."));
    const auto tools=missingTool.isEmpty()?QStringList{QStringLiteral("xelatex"),QStringLiteral("lualatex"),QStringLiteral("pdflatex"),QStringLiteral("bibtex"),QStringLiteral("biber"),QStringLiteral("latexmk"),QStringLiteral("git"),QStringLiteral("curl")}:QStringList{QFileInfo(missingTool).completeBaseName()};
    for(const auto &tool:tools){
      QString executable=systemTool(tool);
      if(tool!=QStringLiteral("git")&&tool!=QStringLiteral("curl"))for(const auto &path:paths){const auto candidate=QDir(path).filePath(tool+QStringLiteral(".exe"));if(QFileInfo(candidate).isFile()){executable=candidate;break;}}
      auto *item=new QListWidgetItem(tool+QStringLiteral(" · ")+words("检查中…","Checking…"),list);
      item->setData(Qt::UserRole+1,tool);
      if(executable.isEmpty()||!QFileInfo(executable).isFile()){
        item->setText(tool+QStringLiteral(" · ")+words("未找到","Not found"));
        item->setData(Qt::UserRole,words("选择已有编译器文件夹，或从安装帮助打开官方页面。","Locate existing tools or open the official download page in Installation help."));
        continue;
      }
      ++running;
      tasks.append(new ExternalTask(this,executable,{QStringLiteral("--version")},{},[this,item,tool,executable,ticket](TaskResult result){
        --running;
        if(!closed&&ticket==epoch){
          if(retry)retry->setEnabled(result.ok);
          item->setText(tool+QStringLiteral(" · ")+(result.ok?words("可用","Ready"):words("无法运行","Unavailable")));
          item->setData(Qt::UserRole,executable+QStringLiteral("\n")+QString::fromUtf8(result.ok?result.output:result.error).left(800));
          if(list->currentItem()==item)showDetails();
        }
        scheduleCheck();
      },10000));
    }
    for(int n=0;n<list->count();++n)if(list->item(n)->data(Qt::UserRole+1).toString()==selected){list->setCurrentRow(n);break;}
    list->verticalScrollBar()->setValue(scroll);
  }
public:
  void requestCheck(){
    if(closed)return;
    // Invalidate once and finish bounded probes. Repeated returns coalesce
    // into one new pass instead of repeatedly launching processes.
    if(!pending){pending=true;++epoch;if(retry)retry->setEnabled(false);}
    scheduleCheck();
  }
  explicit EnvironmentDialog(TeXDocumentWindow *window,const QString &missing):MaterialDialog(window),owner(window),missingTool(missing){
    setProperty("missingTool",missingTool);setAttribute(Qt::WA_DeleteOnClose);setObjectName(QStringLiteral("researchEnvironmentDialog"));configureSettingsSurface(this);setWindowTitle(words("写作环境","Writing environment"));
    auto *layout=new QVBoxLayout(this);layout->addWidget(new QLabel(missingTool.isEmpty()?words("编译工具","Typesetting tools"):words("未找到编译工具：","Typesetting tool not found: ")+missingTool,this));
    list=new QListWidget(this);list->setObjectName(QStringLiteral("researchEnvironmentTools"));configureSettingsList(list);layout->addWidget(list,1);
    notice=new QLabel(this);notice->setObjectName(QStringLiteral("researchEnvironmentDetails"));notice->setWordWrap(true);layout->addWidget(notice);
    auto *row=new QHBoxLayout;
    auto *locate=new QPushButton(words("选择编译器文件夹…","Locate TeX tools…"),this);configureButton(locate);row->addWidget(locate);
    auto *install=new QToolButton(this);install->setText(words("安装帮助…","Installation help…"));configureButton(install,ButtonRole::Text);install->setPopupMode(QToolButton::InstantPopup);auto *menu=new QMenu(install);install->setMenu(menu);row->addWidget(install);row->addStretch();
    auto *refresh=new QPushButton(words("重新检查","Check again"),this);configureButton(refresh);row->addWidget(refresh);layout->addLayout(row);
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Close,this);configureButton(buttons->button(QDialogButtonBox::Close));buttons->button(QDialogButtonBox::Close)->setText(words("关闭","Close"));layout->addWidget(buttons);connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
    if(!missingTool.isEmpty()){
      retry=buttons->addButton(words("重新编译","Retry compilation"),QDialogButtonBox::AcceptRole);retry->setObjectName(QStringLiteral("researchEnvironmentRetry"));configureButton(retry);retry->setEnabled(false);
      connect(retry,&QPushButton::clicked,this,[this]{accept();if(owner)QTimer::singleShot(0,owner,[window=owner]{if(window)if(auto *action=window->findChild<QAction*>(QStringLiteral("actionTypeset")))action->trigger();});});
    }
    for(const auto &pair:QList<QPair<QString,QString>>{{QStringLiteral("TeX Live"),QStringLiteral("https://tug.org/texlive/")},{QStringLiteral("MiKTeX"),QStringLiteral("https://miktex.org/download")},{QStringLiteral("Git for Windows"),QStringLiteral("https://git-scm.com/downloads/win")},{QStringLiteral("Zotero / Better BibTeX"),QStringLiteral("https://retorque.re/zotero-better-bibtex/installation/")}})connect(menu->addAction(pair.first),&QAction::triggered,this,[url=pair.second]{QDesktopServices::openUrl(QUrl(url));});
    menu->addSeparator();connect(menu->addAction(words("从示例开始…","Start from an example…")),&QAction::triggered,this,[this]{accept();if(owner)if(auto *action=owner->findChild<QAction*>(QStringLiteral("actionNew_from_Template")))action->trigger();});
    connect(refresh,&QPushButton::clicked,this,[this]{requestCheck();});
    connect(list,&QListWidget::currentItemChanged,this,[this]{showDetails();});
    connect(locate,&QPushButton::clicked,this,[this]{
      const auto path=QFileDialog::getExistingDirectory(this,words("编译器文件夹","TeX tool folder"));if(path.isEmpty())return;
      bool valid=false;for(const auto &tool:(missingTool.isEmpty()?QStringList{QStringLiteral("xelatex.exe"),QStringLiteral("lualatex.exe"),QStringLiteral("pdflatex.exe")}:QStringList{QFileInfo(missingTool).completeBaseName()+QStringLiteral(".exe")}))valid|=QFileInfo(QDir(path).filePath(tool)).isFile();
      if(!valid){notice->setText(missingTool.isEmpty()?words("此文件夹没有找到 TeX 编译器。","No TeX engine was found in this folder."):words("此文件夹没有找到所需工具：","Required tool not found in this folder: ")+missingTool);return;}
      auto paths=TWApp::instance()->getBinaryPaths();paths.removeAll(path);paths.prepend(path);TWApp::instance()->setBinaryPaths(paths);requestCheck();
    });
    connect(this,&QDialog::finished,this,[this]{closed=true;++epoch;pending=false;for(auto &task:tasks)if(task)task->cancel();if(owner)owner->editor()->setFocus();});
    fitWorkspaceDialog(this,660,460);
  }
  void open(){show();check();}
};
}
void openEnvironmentCheck(TeXDocumentWindow *window,const QString &missingTool){
  if(auto *existing=window->findChild<QDialog*>(QStringLiteral("researchEnvironmentDialog"))){
    if(missingTool.isEmpty()||existing->property("missingTool").toString()==missingTool){existing->show();existing->raise();static_cast<EnvironmentDialog*>(existing)->requestCheck();return;}
    existing->setObjectName(QString());existing->reject();
  }
  (new EnvironmentDialog(window,missingTool))->open();
}
}
