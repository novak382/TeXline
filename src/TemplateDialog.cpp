/*
	This is part of TeXworks, an environment for working with TeX documents
	Copyright (C) 2008-2025  Jonathan Kew, Stefan Löffler, Charlie Sharpsteen

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <https://www.gnu.org/licenses/>.

	For links to further information, or to contact the authors,
	see <https://tug.org/texworks/>.
*/

#include "Settings.h"
#include "TeXHighlighter.h"
#include "TemplateDialog.h"
#include "ResearchAppearance.h"
#include "ResearchTemplates.h"
#include "ResearchSettings.h"
#include <QTimer>
#include <QResizeEvent>
#include <QRegularExpression>
#include "document/TeXDocument.h"
#include "utils/ResourcesLibrary.h"

#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTabWidget>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QPushButton>
#include <QPainter>
#include <QScreen>

namespace {
QHash<QString,QString> templateLabels(){return {{QStringLiteral("chinese-article.tex"),Research::words("中文文章","Chinese article")},{QStringLiteral("minimal-research.tex"),Research::words("英文研究笔记","Research note (English)")},{QStringLiteral("Basic LaTeX documents"),Research::words("基础文稿","Basic documents")},{QStringLiteral("Beamer presentations"),Research::words("幻灯片","Slides")},{QStringLiteral("Imported"),Research::words("我的模板","My templates")},{QStringLiteral("More LaTeX classes"),Research::words("其他文稿","Other documents")},{QStringLiteral("XeLaTeX documents"),Research::words("字体与排版","Fonts and typesetting")},{QStringLiteral("classic-blank.tex"),Research::words("经典空白","Classic blank")},{QStringLiteral("minimal-seminar.tex"),Research::words("简洁学术报告","Academic seminar")}};}
QString templateSource(const QString &path){const auto info=Research::templateInfo(path);return info.valid()?QDir(info.root).filePath(info.main):path;}
class TemplateFiles final:public QFileSystemModel {
public:using QFileSystemModel::QFileSystemModel;
 int rowCount(const QModelIndex &parent={})const override{if(parent.isValid()&&isDir(parent)&&Research::templateInfo(filePath(parent)).valid())return 0;return QFileSystemModel::rowCount(parent);}
 bool hasChildren(const QModelIndex &parent={})const override{if(parent.isValid()&&isDir(parent)&&Research::templateInfo(filePath(parent)).valid())return false;return QFileSystemModel::hasChildren(parent);}
 QVariant data(const QModelIndex &index,int role=Qt::DisplayRole)const override{if(index.column()==0&&isDir(index)&&(role==Qt::DisplayRole||role==Qt::ToolTipRole)){const auto info=Research::templateInfo(filePath(index));if(info.valid())return info.name;}if(index.column()==0&&role==Qt::DisplayRole){const auto label=templateLabels().value(fileName(index));if(!label.isEmpty())return label;}return QFileSystemModel::data(index,role);}
};
class TemplatePreview final : public QLabel {
public:
 using QLabel::QLabel;
 QSize sizeHint() const override { return QSize(520,330); }
 QSize minimumSizeHint() const override { return QSize(80,80); }
protected:
 void paintEvent(QPaintEvent *event) override {
   const auto image=pixmap();if(image.isNull()){QLabel::paintEvent(event);return;}
   const QSize fitted=image.size().scaled(contentsRect().size(),Qt::KeepAspectRatio);
   const QRect target(contentsRect().center()-QPoint(fitted.width()/2,fitted.height()/2),fitted);
   QPainter painter(this);painter.setRenderHint(QPainter::SmoothPixmapTransform);painter.drawPixmap(target,image);
 }
};
}
TemplateDialog::TemplateDialog(QWidget *parent)
	: QDialog(parent)
{
	init();
}

void TemplateDialog::init()
{
	setupUi(this);
	Research::configureSettingsSurface(this);
	setStyleSheet(QStringLiteral("QDialog#researchTemplateDialog {background:white;} QTreeView {background:white;border:0;} QTreeView::item {padding:6px 4px;} QTreeView::item:selected {background:#e6e9ec;color:#20252a;} QTabWidget::pane {border:0;}"));
	treeView->setUniformRowHeights(true);treeView->setFrameShape(QFrame::NoFrame);treeView->setTextElideMode(Qt::ElideRight);
	label->hide();
	setObjectName(QStringLiteral("researchTemplateDialog"));setWindowTitle(Research::words("从模板新建","New from template"));buttonBox->button(QDialogButtonBox::Open)->setText(Research::words("使用模板","Use template"));resize(940,650);splitter->setOrientation(Qt::Horizontal);
	auto *tabs=new QTabWidget(splitter);tabs->setObjectName(QStringLiteral("researchTemplateTabs"));auto *page=new QWidget(tabs);auto *previewLayout=new QVBoxLayout(page);
	auto *description=new QLabel(page);description->setObjectName(QStringLiteral("researchTemplateDescription"));description->setWordWrap(true);previewLayout->addWidget(description);
	auto *image=new TemplatePreview(page);image->setObjectName(QStringLiteral("researchTemplateImage"));image->setAlignment(Qt::AlignCenter);image->setWordWrap(true);previewLayout->addWidget(image,1);tabs->addTab(page,Research::ui(QStringLiteral("预览")));tabs->addTab(textEdit,Research::ui(QStringLiteral("源码")));splitter->addWidget(tabs);splitter->setSizes({280,620});
	auto *search=new QLineEdit(this);search->setObjectName(QStringLiteral("researchTemplateSearch"));search->setPlaceholderText(Research::ui(QStringLiteral("搜索模板")));search->setClearButtonEnabled(true);Research::configureField(search);auto *grid=qobject_cast<QGridLayout *>(layout());grid->setContentsMargins(16,16,16,12);grid->setSpacing(12);grid->addWidget(search,0,0,1,2);grid->removeWidget(splitter);grid->addWidget(splitter,1,0,1,2);grid->removeWidget(buttonBox);grid->addWidget(buttonBox,2,0,1,2);
	Tw::Document::TeXDocument * texDoc = new Tw::Document::TeXDocument(textEdit);
	textEdit->setDocument(texDoc);

	QString templatePath = Tw::Utils::ResourcesLibrary::getLibraryPath(QStringLiteral("templates"));
		// do this before creating the model, as getLibraryPath might initialize a new dir

	model = new TemplateFiles(this);
	model->setRootPath(templatePath);
	model->setNameFilters({QStringLiteral("*.tex")});model->setNameFilterDisables(false);
	connect(search,&QLineEdit::textChanged,this,[this](const QString &query){QStringList filters{QLatin1Char('*')+query+QStringLiteral("*.tex")};const auto labels=templateLabels();for(auto it=labels.begin();it!=labels.end();++it)if(it.key().endsWith(QStringLiteral(".tex"))&&it.value().contains(query,Qt::CaseInsensitive))filters<<it.key();model->setNameFilters(filters);if(!query.isEmpty())treeView->expandAll();});
	treeView->setModel(model);
	treeView->setRootIndex(model->index(templatePath));
    QTimer::singleShot(250,this,[this,templatePath]{treeView->expand(model->index(QDir(templatePath).filePath(QStringLiteral("Imported"))));if(treeView->currentIndex().isValid())return;const auto first=model->index(QDir(templatePath).filePath(QStringLiteral("Beamer presentations/classic-blank.tex")));if(first.isValid())treeView->setCurrentIndex(first);});
	treeView->hideColumn(2);
	treeView->hideColumn(1);treeView->hideColumn(3);treeView->setHeaderHidden(true);
	buttonBox->button(QDialogButtonBox::Open)->setEnabled(false);
    auto *import=new QPushButton(Research::words("导入模板…","Import template…"),this);import->setObjectName(QStringLiteral("researchTemplateImport"));Research::configureButton(import);auto *footer=new QWidget(this);auto *footerLayout=new QHBoxLayout(footer);footerLayout->setContentsMargins(0,0,0,0);grid->removeWidget(buttonBox);footerLayout->addWidget(import);footerLayout->addStretch();footerLayout->addWidget(buttonBox);grid->addWidget(footer,2,0,1,2);
    connect(import,&QPushButton::clicked,this,[this,templatePath,search]{const auto file=Research::importTemplateDialog(this,QDir(templatePath).filePath(QStringLiteral("Imported")));if(file.isEmpty())return;search->clear();auto select=[this,file]{const auto index=model->index(Research::templateInfo(file).root);if(index.isValid()){for(auto parent=index.parent();parent.isValid()&&parent!=treeView->rootIndex();parent=parent.parent())treeView->expand(parent);treeView->setCurrentIndex(index);treeView->scrollTo(index);}};select();QTimer::singleShot(300,this,select);});

	connect(treeView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &TemplateDialog::selectionChanged);

	connect(treeView, &QTreeView::activated, this, &TemplateDialog::itemActivated);

	Tw::Settings settings;
	if (settings.value(QString::fromLatin1("syntaxColoring"), true).toBool()) {
		TeXHighlighter * highlighter = new TeXHighlighter(texDoc);
		// For now, we use "LaTeX" highlighting for all files (which is probably
		// reasonable in most/typical cases)
		int idx = static_cast<int>(TeXHighlighter::syntaxOptions().indexOf(QStringLiteral("LaTeX")));
		if (idx >= 0)
			highlighter->setActiveIndex(idx);
	}
}

void TemplateDialog::itemActivated(const QModelIndex & index)
{
	QFileSystemModel * model = qobject_cast<QFileSystemModel*>(treeView->model());
	if (model && (!model->isDir(index)||Research::templateInfo(model->filePath(index)).valid()))
		accept();
}

void TemplateDialog::selectionChanged(const QItemSelection &selected, const QItemSelection &/*deselected*/)
{
	textEdit->clear();
	auto *preview=findChild<QLabel *>(QStringLiteral("researchTemplateImage"));auto *description=findChild<QLabel *>(QStringLiteral("researchTemplateDescription"));preview->clear();description->clear();buttonBox->button(QDialogButtonBox::Open)->setEnabled(false);
	if (selected.indexes().count() > 0) {
		QString filePath(templateSource(model->filePath(selected.indexes()[0])));
		QFileInfo fileInfo(filePath);
		if (fileInfo.isFile() && fileInfo.isReadable()) {
			QFile file(filePath);
			if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
				QTextStream in(&file);
				const QString source=in.readAll();textEdit->setPlainText(source);buttonBox->button(QDialogButtonBox::Open)->setEnabled(true);
				const bool slides=source.contains(QStringLiteral("{beamer}"))||source.contains(QStringLiteral("{ctexbeamer}"));const bool simple=fileInfo.baseName().startsWith(QStringLiteral("minimal-"));const auto engine=(source.contains(QStringLiteral("fontspec"))||source.contains(QStringLiteral("ctexbeamer")))?QStringLiteral("XeLaTeX"):QStringLiteral("pdfLaTeX");
				description->setText(fileInfo.fileName()+QLatin1Char('\n')+Research::ui(slides?(simple?QStringLiteral("简洁学术报告 · 16:9 · 含讲者备注和备份页"):QStringLiteral("Beamer 演示文稿 · 传统主题示例")):(simple?QStringLiteral("研究论文 · 定理与证明 · 标准字体"):QStringLiteral("LaTeX 文档模板")))+QStringLiteral("\n")+Research::ui(QStringLiteral("编译引擎：%1")).arg(engine));
                if(fileInfo.baseName()==QStringLiteral("classic-blank"))description->setText(Research::words("经典空白 · Beamer 默认样式 · 16:9","Classic blank · Default Beamer style · 16:9"));
                const auto imported=Research::templateInfo(filePath);if(imported.valid()){description->setText(imported.name+QStringLiteral("\n")+imported.engine);QPixmap saved(imported.preview);if(!saved.isNull())preview->setPixmap(saved);else preview->setText(Research::words("导入的文稿模板","Imported document template"));return;}
				QFile original(filePath),manifest(QStringLiteral(":/research/template-previews/manifest.json"));if(original.open(QIODevice::ReadOnly)&&manifest.open(QIODevice::ReadOnly)){const auto hash=QString::fromLatin1(QCryptographicHash::hash(original.readAll(),QCryptographicHash::Sha256).toHex());const auto asset=QJsonDocument::fromJson(manifest.readAll()).object().value(hash).toString();if(!asset.isEmpty()){QPixmap image(QStringLiteral(":/research/template-previews/")+asset);preview->setPixmap(image);}else preview->setText(Research::ui(QStringLiteral("暂无预览，请查看“源码”。")));}
			}
		}
	}
}

QString TemplateDialog::doTemplateDialog(bool *project, QWidget *parent)
{
 if(project)*project=false;
 TemplateDialog dlg(parent);
 while(dlg.exec()==Accepted){
  const auto selection=dlg.treeView->selectionModel()->selectedRows();
  if(selection.isEmpty())continue;
  const auto file=templateSource(dlg.model->filePath(selection.first()));
  if(!QFileInfo(file).isFile()||!QFileInfo(file).isReadable())continue;
  if(!Research::templateInfo(file).valid())return file;
  const auto created=Research::createTemplateProjectDialog(&dlg,file);
  // Cancelling the destination step returns to the selected template.
  if(created.isEmpty())continue;
  if(project)*project=true;
  return created;
 }
 return {};
}

void TemplateDialog::showEvent(QShowEvent * event)
{
	QDialog::showEvent(event);
	const QSize available=screen()->availableGeometry().size()-QSize(24,64);
	setMinimumSize(minimumSize().boundedTo(available));resize(size().boundedTo(available));

	// Resize the first column to take all available screen space. This can only
	// be done once the dialog is fully layouted to be shown - hence here.

	// Only resize columns the first time the dialog is shown. After that, keep
	// using what the user are used to (or have even configured themselves)
	if (!_shouldResizeColumns) {
		return;
	}
	_shouldResizeColumns = false;

	QHeaderView * h = treeView->header();
	if (!h) {
		return;
	}
	// Do not use the real section sizes as reference, as the last section might
	// be expanding and is hence larger than necessary. The default section size
	// seems to work well here.
	h->setSectionResizeMode(0,QHeaderView::Stretch);
}

void TemplateDialog::resizeEvent(QResizeEvent *event)
{
 QDialog::resizeEvent(event);
 const auto orientation=width()<680?Qt::Vertical:Qt::Horizontal;
 if(splitter->orientation()!=orientation){splitter->setOrientation(orientation);splitter->setSizes(orientation==Qt::Vertical?QList<int>{160,360}:QList<int>{260,620});}
}
