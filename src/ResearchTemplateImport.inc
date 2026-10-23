// Template import and project creation reuse the existing material dialog.
#include "ResearchAppearance.h"
#include "ResearchSlideExport.h"
#include "ResearchWorkspaceUi.h"
#include "TWApp.h"
#include "PDFBackend.h"
#include <QApplication>
#include <QComboBox>
#include <QDirIterator>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPointer>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRunnable>
#include <QScreen>
#include <QSignalBlocker>
#include <QThreadPool>
#include <QVBoxLayout>
namespace Research {
QString importProjectDialog(QWidget *parent,const QString &archive){
 MaterialDialog dialog(parent);configureSettingsSurface(&dialog);dialog.setObjectName(QStringLiteral("researchOpenProject"));dialog.setWindowTitle(words("打开项目","Open project"));
 auto *layout=new QVBoxLayout(&dialog);auto *form=new QFormLayout;
 auto *name=new QLineEdit(QFileInfo(archive).completeBaseName(),&dialog);name->setObjectName(QStringLiteral("researchProjectName"));configureField(name);form->addRow(words("项目名称","Project name"),name);
 auto *folder=new QLineEdit(QFileInfo(archive).absolutePath(),&dialog);folder->setObjectName(QStringLiteral("researchProjectDestination"));configureField(folder);
 auto *row=new QHBoxLayout;row->addWidget(folder,1);auto *browse=new QPushButton(words("选择…","Choose…"),&dialog);configureButton(browse);row->addWidget(browse);form->addRow(words("保存位置","Save in"),row);
 auto *main=new QComboBox(&dialog);main->setObjectName(QStringLiteral("researchProjectMain"));main->setMinimumHeight(32);form->addRow(words("主文件","Main file"),main);layout->addLayout(form);
 auto *notice=new QLabel(words("正在读取项目…","Reading project…"),&dialog);notice->setTextFormat(Qt::PlainText);notice->setWordWrap(true);layout->addWidget(notice);layout->addStretch();
 auto *buttons=new QDialogButtonBox(QDialogButtonBox::Open|QDialogButtonBox::Cancel,&dialog);layout->addWidget(buttons);auto *open=buttons->button(QDialogButtonBox::Open);open->setText(words("打开项目","Open project"));open->setEnabled(false);configureButton(open);buttons->button(QDialogButtonBox::Cancel)->setText(words("取消","Cancel"));configureButton(buttons->button(QDialogButtonBox::Cancel));
 auto temp=std::make_shared<QTemporaryDir>(QDir::tempPath()+QStringLiteral("/texline-project-XXXXXX"));auto token=std::make_shared<std::atomic_bool>(false);auto bundle=std::make_shared<TemplateBundle>();QPointer<QDialog> guard(&dialog);QString result;
 QObject::connect(browse,&QPushButton::clicked,&dialog,[&]{const auto path=QFileDialog::getExistingDirectory(&dialog,words("保存位置","Save in"),folder->text());if(!path.isEmpty())folder->setText(path);});
 QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
 QObject::connect(&dialog,&QDialog::finished,&dialog,[token]{token->store(true);});
 QObject::connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{
   const auto label=name->text().trimmed();const auto destination=QDir(folder->text()).filePath(label);
   if(!safeName(label)||label.contains(QLatin1Char('/'))||!QFileInfo(folder->text()).isDir()){notice->setText(words("请填写项目名称并选择已有文件夹。","Name the project and choose an existing folder."));return;}
   if(!bundle->mains.contains(main->currentText())||QFileInfo::exists(destination)){notice->setText(words("目标已存在，请换一个项目名称。","The destination exists. Choose another project name."));return;}
   open->setEnabled(false);name->setEnabled(false);folder->setEnabled(false);browse->setEnabled(false);main->setEnabled(false);
   notice->setText(words("正在创建项目…","Creating project…"));const auto chosen=main->currentText();
   auto output=std::make_shared<QTemporaryDir>(QDir(folder->text()).filePath(QStringLiteral(".texline-import-XXXXXX")));
   QThreadPool::globalInstance()->start(QRunnable::create([=,&result]{
     const auto copied=stageTemplate(bundle->root,output->filePath(QStringLiteral("project")),token,true);
     if(qApp)QMetaObject::invokeMethod(qApp,[=,&result,retainedOutput=output]{
       if(!guard||token->load())return;
       if(copied.error.isEmpty()&&QDir().rename(copied.root,destination)){
         result=QDir(destination).filePath(chosen);guard->accept();return;
       }
       notice->setText(copied.error.isEmpty()?words("无法创建项目，请检查保存位置。","Cannot create the project. Check its destination."):copied.error);
       open->setEnabled(true);name->setEnabled(true);folder->setEnabled(true);browse->setEnabled(true);main->setEnabled(true);
     },Qt::QueuedConnection);
   }));
 });
 QThreadPool::globalInstance()->start(QRunnable::create([=]{
   const auto data=stageTemplate(archive,temp->filePath(QStringLiteral("project")),token,true);
   if(qApp)QMetaObject::invokeMethod(qApp,[=]{if(!guard||token->load())return;*bundle=data;main->addItems(data.mains);if(data.mains.size()>1)main->setCurrentIndex(-1);open->setEnabled(data.error.isEmpty()&&main->currentIndex()>=0);notice->setText(data.error.isEmpty()?words("%1 个文件 · 保留项目原有结构","%1 files · Original project structure retained").arg(data.files):data.error);},Qt::QueuedConnection);
 }));
 QObject::connect(main,qOverload<int>(&QComboBox::currentIndexChanged),&dialog,[=](int index){open->setEnabled(index>=0&&bundle->error.isEmpty());});
 fitWorkspaceDialog(&dialog,580,300);dialog.exec();token->store(true);return result;
}
namespace {
class TemplateImage final:public QLabel {
public:QImage image;using QLabel::QLabel;
 void paintEvent(QPaintEvent *e)override{if(image.isNull()){QLabel::paintEvent(e);return;}QSizeF fit=image.size();fit.scale(contentsRect().size(),Qt::KeepAspectRatio);QPainter p(this);p.setRenderHint(QPainter::SmoothPixmapTransform);p.drawImage(QRectF(contentsRect().center()-QPointF(fit.width()/2,fit.height()/2),fit),image);}
 QSize minimumSizeHint()const override{return {80,80};}
 QSize sizeHint()const override{return {560,300};}
};
class TemplateImport final:public MaterialDialog {
 std::shared_ptr<std::atomic_bool> cancel=std::make_shared<std::atomic_bool>(false);
 std::shared_ptr<QTemporaryDir> temporary;
 TemplateBundle bundle;QString library,result,loadedSource,previewLog;quint64 generation{0};bool saving{false},engineChosen{false};
 QLineEdit *source,*name;QComboBox *main,*engine;QFormLayout *form;TemplateImage *preview;QLabel *notice{nullptr};QPushButton *test,*save;
 void invalidate(){previewLog.clear();if(notice)notice->clear();cancel->store(true);cancel=std::make_shared<std::atomic_bool>(false);++generation;preview->image={};preview->clear();preview->update();setProperty("researchTemplatePreviewReady",false);}
 void load(){
  if(saving||(!bundle.root.isEmpty()&&loadedSource==source->text().trimmed()))return;loadedSource=source->text().trimmed();
  invalidate();bundle={};engineChosen=false;main->clear();form->setRowVisible(main,false);save->setEnabled(false);test->setEnabled(false);if(source->text().trimmed().isEmpty())return;
  temporary=std::make_shared<QTemporaryDir>(QDir::tempPath()+QStringLiteral("/texline-template-XXXXXX"));const auto temp=temporary;const auto token=cancel;const auto ticket=generation;const auto input=source->text().trimmed();QPointer<TemplateImport> guard(this);notice->setText(words("正在读取模板…","Reading template…"));
  QThreadPool::globalInstance()->start(QRunnable::create([guard,temp,token,ticket,input]{const auto data=stageTemplate(input,temp->filePath(QStringLiteral("project")),token);if(qApp)QMetaObject::invokeMethod(qApp,[guard,temp,token,ticket,data,input]{if(!guard||token->load()||guard->generation!=ticket)return;guard->bundle=data;guard->notice->setText(data.error);{QSignalBlocker block(guard->main);guard->main->addItems(data.mains);guard->main->setCurrentIndex(data.mains.size()==1?0:-1);}guard->form->setRowVisible(guard->main,data.mains.size()>1);guard->name->setText(QFileInfo(input).completeBaseName());guard->selectMain();},Qt::QueuedConnection);}));
 }
 void selectMain(){const bool ready=!bundle.root.isEmpty()&&bundle.error.isEmpty()&&!main->currentText().isEmpty();save->setEnabled(ready);test->setEnabled(ready);if(!ready)return;invalidate();if(!engineChosen){const auto text=read(QDir(bundle.root).filePath(main->currentText()));QString selected=suggestedEngine(text);if(text.contains(QStringLiteral("ctexbeamer")))selected=QStringLiteral("XeLaTeX");const int at=engine->findText(selected,Qt::MatchFixedString);if(at>=0)engine->setCurrentIndex(at);}}
 void render(){
  if(bundle.root.isEmpty()||main->currentText().isEmpty())return;
  invalidate();const auto token=cancel;const auto temp=temporary;const auto ticket=generation;QPointer<TemplateImport> guard(this);SlideExportRequest request;request.root=QDir(bundle.root).filePath(main->currentText());request.engine=TWApp::instance()->getNamedEngine(engine->currentText());auto args=request.engine.arguments();for(int i=args.size()-1;i>=0;--i)if(args[i].contains(QStringLiteral("shell-escape"))||args[i].contains(QStringLiteral("enable-write18")))args.removeAt(i);args.prepend(QStringLiteral("-no-shell-escape"));request.engine.setArguments(args);request.profiles={0};QDirIterator resources(bundle.root,QDir::Files,QDirIterator::Subdirectories);while(resources.hasNext())request.extraDependencies.append(resources.next());test->setEnabled(false);notice->setText(words("正在排版预览…","Typesetting preview…"));
  QThreadPool::globalInstance()->start(QRunnable::create([guard,token,temp,ticket,request]()mutable{QTemporaryDir output(QDir::tempPath()+QStringLiteral("/texline-template-preview-XXXXXX"));const auto pdf=output.filePath(QStringLiteral("preview.pdf"));request.destinations={pdf};request.destinationHashes.insert(pdf,{});const auto compiled=exportSlideCopies(request,token);QImage image;if(!token->load()&&compiled.error.isEmpty()){auto doc=QtPDF::Backend::Document::newDocument(pdf);if(doc&&doc->numPages())if(auto page=doc->page(0).toStrongRef()){const qreal dpi=72*1200/qMax(qreal(1),page->pageSizeF().width());image=page->renderToImage(dpi,dpi,QRect(),false);}}
   if(qApp)QMetaObject::invokeMethod(qApp,[guard,token,temp,ticket,image,compiled]{if(!guard||token->load()||guard->generation!=ticket)return;guard->test->setEnabled(true);guard->preview->image=image;guard->preview->update();guard->setProperty("researchTemplatePreviewReady",!image.isNull());guard->previewLog=localizedSlideExportError(compiled.error,englishUi())+QLatin1Char('\n')+compiled.log;guard->notice->setText(image.isNull()?QStringLiteral("<a href=\"log\" style=\"color:#536573\">%1</a>").arg(words("预览未完成 · 查看日志","Preview incomplete · View log")):QString());guard->notice->setToolTip(QString());},Qt::QueuedConnection);
  }));
 }
 void store(){
  if(bundle.root.isEmpty()||main->currentText().isEmpty())return;
  if(name->text().trimmed().isEmpty()){notice->setText(words("填写模板名称。","Enter a template name."));name->setFocus();return;}
  saving=true;cancel->store(true);cancel=std::make_shared<std::atomic_bool>(false);const auto token=cancel;const auto temp=temporary;const auto data=bundle;const QString chosen=main->currentText(),label=name->text(),compiler=engine->currentText(),destination=library;const QImage image=preview->image;QPointer<TemplateImport> guard(this);save->setEnabled(false);test->setEnabled(false);source->setEnabled(false);name->setEnabled(false);main->setEnabled(false);engine->setEnabled(false);notice->setText(words("正在保存模板…","Saving template…"));
  QThreadPool::globalInstance()->start(QRunnable::create([guard,token,temp,data,chosen,label,compiler,destination,image]{QString error;QString path;if(!token->load())path=saveTemplate(data,chosen,label,compiler,image,destination,&error);if(token->load()&&!path.isEmpty()){QDir(templateInfo(path).root).removeRecursively();return;}if(qApp)QMetaObject::invokeMethod(qApp,[guard,token,path,error]{if(!guard||token->load()){if(!path.isEmpty())QDir(templateInfo(path).root).removeRecursively();return;}if(!path.isEmpty()){guard->result=path;guard->accept();}else{guard->saving=false;guard->notice->setText(error);guard->save->setEnabled(true);guard->test->setEnabled(true);guard->source->setEnabled(true);guard->name->setEnabled(true);guard->main->setEnabled(true);guard->engine->setEnabled(true);}},Qt::QueuedConnection);}));
 }
public:
 TemplateImport(QWidget *parent,const QString &directory):MaterialDialog(parent),library(directory){
  setObjectName(QStringLiteral("researchImportTemplateDialog"));setWindowTitle(words("导入模板","Import template"));resize(780,610);auto *layout=new QVBoxLayout(this);layout->setContentsMargins(24,20,24,20);layout->setSpacing(12);
  auto *row=new QHBoxLayout;source=new QLineEdit(this);source->setObjectName(QStringLiteral("templateImportSource"));source->setPlaceholderText(words("模板文件夹或 ZIP","Template folder or ZIP"));configureField(source);row->addWidget(source,1);auto *browse=new QPushButton(words("选择…","Browse…"),this);configureButton(browse);row->addWidget(browse);layout->addLayout(row);
  auto *choices=new QMenu(browse);choices->addAction(words("ZIP 文件…","ZIP file…"),this,[this]{const auto path=QFileDialog::getOpenFileName(this,windowTitle(),{},QStringLiteral("ZIP (*.zip)"));if(!path.isEmpty()){source->setText(path);load();}});choices->addAction(words("文件夹…","Folder…"),this,[this]{const auto path=QFileDialog::getExistingDirectory(this,windowTitle());if(!path.isEmpty()){source->setText(path);load();}});browse->setMenu(choices);QObject::connect(source,&QLineEdit::editingFinished,this,[this]{load();});
  form=new QFormLayout;form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);form->setRowWrapPolicy(QFormLayout::WrapLongRows);form->setVerticalSpacing(10);name=new QLineEdit(this);name->setObjectName(QStringLiteral("templateImportName"));configureField(name);main=new QComboBox(this);main->setObjectName(QStringLiteral("templateImportMain"));main->setMinimumHeight(32);engine=new QComboBox(this);engine->setObjectName(QStringLiteral("templateImportEngine"));engine->addItems({QStringLiteral("XeLaTeX"),QStringLiteral("pdfLaTeX"),QStringLiteral("LuaLaTeX")});engine->setMinimumHeight(32);form->addRow(words("名称","Name"),name);main->setPlaceholderText(words("选择主文件…","Choose the main file…"));form->addRow(words("主文件","Main file"),main);form->setRowVisible(main,false);form->addRow(words("排版工具","Typesetter"),engine);layout->addLayout(form);
  preview=new TemplateImage(this);preview->setObjectName(QStringLiteral("templateImportPreview"));preview->setAlignment(Qt::AlignCenter);layout->addWidget(preview,1);notice=new QLabel(this);notice->setObjectName(QStringLiteral("templateImportNotice"));notice->setWordWrap(true);notice->setTextInteractionFlags(Qt::TextBrowserInteraction);notice->setOpenExternalLinks(false);layout->addWidget(notice);QObject::connect(notice,&QLabel::linkActivated,this,[this]{MaterialDialog log(this);log.setObjectName(QStringLiteral("researchTemplateLogDialog"));log.setWindowTitle(words("排版日志","Typesetting log"));log.resize(720,460);auto *body=new QVBoxLayout(&log);auto *text=new QPlainTextEdit(&log);text->setObjectName(QStringLiteral("researchTemplateLog"));text->setReadOnly(true);text->setPlainText(previewLog);body->addWidget(text);auto *close=new QDialogButtonBox(QDialogButtonBox::Close,&log);for(auto *button:close->findChildren<QPushButton*>())configureButton(button);body->addWidget(close);QObject::connect(close,&QDialogButtonBox::rejected,&log,&QDialog::reject);log.exec();});
  auto *buttons=new QDialogButtonBox(QDialogButtonBox::Cancel,this);test=new QPushButton(words("预览","Preview"),this);configureButton(test);test->setObjectName(QStringLiteral("templateImportRender"));save=buttons->addButton(words("导入","Import"),QDialogButtonBox::AcceptRole);save->setObjectName(QStringLiteral("templateImportSave"));for(auto *b:buttons->findChildren<QPushButton*>())configureButton(b);test->setEnabled(false);save->setEnabled(false);auto *footer=new QHBoxLayout;footer->addWidget(test);footer->addStretch(1);footer->addWidget(buttons);layout->addLayout(footer);QObject::connect(test,&QPushButton::clicked,this,[this]{render();});QObject::connect(save,&QPushButton::clicked,this,[this]{store();});QObject::connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);QObject::connect(main,&QComboBox::currentIndexChanged,this,[this]{selectMain();});QObject::connect(engine,&QComboBox::activated,this,[this]{engineChosen=true;invalidate();test->setEnabled(!bundle.root.isEmpty()&&!main->currentText().isEmpty());});
 }
 ~TemplateImport()override{cancel->store(true);}
 QString run(){exec();return result;}
};
}
QString importTemplateDialog(QWidget *parent,const QString &library){TemplateImport dialog(parent,library);return dialog.run();}
QString createTemplateProjectDialog(QWidget *parent,const QString &main){
 const auto info=templateInfo(main);if(!info.valid())return {};MaterialDialog dialog(parent);dialog.setObjectName(QStringLiteral("researchCreateTemplateProject"));dialog.setWindowTitle(words("使用模板新建","Create from template"));dialog.resize(580,250);auto *layout=new QVBoxLayout(&dialog);auto *form=new QFormLayout;auto *name=new QLineEdit(info.name,&dialog);name->setObjectName(QStringLiteral("templateProjectName"));configureField(name);auto *folder=new QLineEdit(QDir::homePath(),&dialog);folder->setObjectName(QStringLiteral("templateProjectParent"));configureField(folder);auto *row=new QHBoxLayout;row->addWidget(folder,1);auto *browse=new QPushButton(words("选择…","Browse…"),&dialog);configureButton(browse);row->addWidget(browse);form->addRow(words("文稿名称","Project name"),name);form->addRow(words("保存到","Save in"),row);layout->addLayout(form);auto *notice=new QLabel(&dialog);notice->setWordWrap(true);layout->addWidget(notice);auto *buttons=new QDialogButtonBox(QDialogButtonBox::Cancel,&dialog);auto *create=buttons->addButton(words("创建","Create"),QDialogButtonBox::AcceptRole);create->setObjectName(QStringLiteral("templateProjectCreate"));for(auto *b:buttons->findChildren<QPushButton*>())configureButton(b);layout->addWidget(buttons);QObject::connect(browse,&QPushButton::clicked,&dialog,[&]{const auto path=QFileDialog::getExistingDirectory(&dialog,dialog.windowTitle(),folder->text());if(!path.isEmpty())folder->setText(path);});QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
 auto result=std::make_shared<QString>();auto token=std::make_shared<std::atomic_bool>(false);QObject::connect(create,&QPushButton::clicked,&dialog,[&]{const QString label=name->text().trimmed();if(!safeName(label)||label.contains(QLatin1Char('/'))||!QFileInfo(folder->text()).isDir()){notice->setText(words("填写文稿名称并选择保存位置。","Enter a project name and choose its folder."));return;}const QString destination=QDir(folder->text()).filePath(label);if(QFileInfo::exists(destination)){notice->setText(words("同名文件夹已存在，请换一个名称。","That folder already exists. Choose another name."));return;}create->setEnabled(false);name->setEnabled(false);folder->setEnabled(false);browse->setEnabled(false);QPointer<QDialog> guard(&dialog);
  QThreadPool::globalInstance()->start(QRunnable::create([guard,token,main,destination,result,notice,create,name,folder,browse]{QString error;const QString path=instantiateTemplate(main,destination,&error);if(token->load()){if(!path.isEmpty())QDir(destination).removeRecursively();return;}if(qApp)QMetaObject::invokeMethod(qApp,[guard,token,path,error,result,notice,create,name,folder,browse,destination]{if(!guard||token->load()){if(!path.isEmpty())QDir(destination).removeRecursively();return;}if(!path.isEmpty()){*result=path;guard->accept();}else{notice->setText(error);create->setEnabled(true);name->setEnabled(true);folder->setEnabled(true);browse->setEnabled(true);}},Qt::QueuedConnection);}));
 });dialog.exec();token->store(true);return *result;
}
}
