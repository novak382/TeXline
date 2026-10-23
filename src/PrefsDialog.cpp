#include "ResearchMenuChoices.h"
/*
	This is part of TeXworks, an environment for working with TeX documents
	Copyright (C) 2007-2025  Jonathan Kew, Stefan Löffler, Charlie Sharpsteen

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

#include "PrefsDialog.h"
#include "TeXDocumentWindow.h"
#include "CompletingEdit.h"

#include "CompletingEdit.h"
#include "DefaultPrefs.h"
#include "PDFDocumentWindow.h"
#include "Settings.h"
#include "TWApp.h"
#include "TWUtils.h"
#include "ResearchAppearance.h"
#include "ResearchCommandCompletion.h"
#include "ResearchCompletion.h"
#include "ResearchSettings.h"
#include "ResearchStart.h"
#include "ResearchCommands.h"
#include "ResearchShorthand.h"
#include <QButtonGroup>
#include <QTabBar>
#include <QTimer>
#include <QStackedWidget>
#include <QTextEdit>
#include <QGroupBox>
#include "TeXHighlighter.h"
#include "document/SpellCheckManager.h"

#include <QFileDialog>
#include <QFontDatabase>
#include <QMainWindow>
#include <QPointer>
#include <QMessageBox>
#include <QSet>
#include <QTextCodec>
#include <QToolBar>
#include <QScrollArea>
#include <QScrollBar>
#include <QScreen>
#include <QAbstractTextDocumentLayout>
#include <functional>

namespace {
// A small live sample stays visible even when Settings covers a narrow editor.
class EditorAppearanceSample final : public QTextEdit {
public:
	explicit EditorAppearanceSample(QWidget *parent) : QTextEdit(parent) {
		setObjectName(QStringLiteral("researchEditorAppearanceSample"));
		setReadOnly(true); setFocusPolicy(Qt::NoFocus); setFrameShape(QFrame::NoFrame);
		setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff); setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
		setPlainText(Research::words("% 记录想法\n\\section{Notes}", "% A short note\n\\section{Notes}"));
		document()->setUndoRedoEnabled(false);
		document()->setDocumentMargin(4);
	}
	void preview(const QFont &font, qreal spacing) {
		setFont(Research::smoothTextFont(font)); document()->setDefaultFont(font);
		QTextCursor cursor(document()); cursor.select(QTextCursor::Document);
		// The application UI stylesheet must not substitute its font for the chosen editor face.
		QTextCharFormat characters; characters.setFont(Research::smoothTextFont(font)); cursor.setCharFormat(characters);
		QTextBlockFormat format; format.setTopMargin(0); format.setBottomMargin(0); format.setLineHeight(spacing, QTextBlockFormat::ProportionalHeight); cursor.mergeBlockFormat(format);
		fit();
	}
protected:
	void resizeEvent(QResizeEvent *event) override { QTextEdit::resizeEvent(event); fit(); }
private:
	void fit() { const int height=qCeil(document()->documentLayout()->documentSize().height())+2; if(minimumHeight()!=height)setFixedHeight(height); }
};
}

PrefsDialog::PrefsDialog(QWidget *parent)
	: QDialog(parent)
	, pathsChanged(false)
	, toolsChanged(false)
{
	init();
}

void PrefsDialog::init()
{
	setupUi(this);
	for(auto *button:{pathUp,pathDown,pathAdd,pathRemove,toolUp,toolDown,toolAdd,toolRemove})Research::configureButton(button,Research::ButtonRole::Icon);
	Research::configureButton(toolEdit,Research::ButtonRole::Text);
	connect(buttonBox, &QDialogButtonBox::clicked, this, &PrefsDialog::buttonClicked);

	connect(binPathList, &QListWidget::itemSelectionChanged, this, &PrefsDialog::updatePathButtons);
	connect(pathUp, &QToolButton::clicked, this, &PrefsDialog::movePathUp);
	connect(pathDown, &QToolButton::clicked, this, &PrefsDialog::movePathDown);
	connect(pathAdd, &QToolButton::clicked, this, &PrefsDialog::addPath);
	connect(pathRemove, &QToolButton::clicked, this, &PrefsDialog::removePath);

	connect(toolList, &QListWidget::itemSelectionChanged, this, &PrefsDialog::updateToolButtons);
	connect(toolList, &QListWidget::itemDoubleClicked, this, &PrefsDialog::editTool);
	connect(toolUp, &QToolButton::clicked, this, &PrefsDialog::moveToolUp);
	connect(toolDown, &QToolButton::clicked, this, &PrefsDialog::moveToolDown);
	connect(toolAdd, &QToolButton::clicked, this, &PrefsDialog::addTool);
	connect(toolRemove, &QToolButton::clicked, this, &PrefsDialog::removeTool);
	connect(toolEdit, &QToolButton::clicked, this, [=]() { this->editTool(); });

	connect(tabWidget, &QTabWidget::currentChanged, this, &PrefsDialog::changedTabPanel);

	pathsChanged = toolsChanged = false;
}

void PrefsDialog::changedTabPanel(int index)
{
  if(auto *page=tabWidget->widget(index))page->clearFocus();
}

void PrefsDialog::updatePathButtons()
{
	int selRow = -1;
	if (binPathList->selectedItems().count() > 0)
		selRow = binPathList->currentRow();
	pathRemove->setEnabled(selRow != -1);
	pathUp->setEnabled(selRow > 0);
	pathDown->setEnabled(selRow != -1 && selRow < binPathList->count() - 1);
}

void PrefsDialog::movePathUp()
{
	int selRow = -1;
	if (binPathList->selectedItems().count() > 0)
		selRow = binPathList->currentRow();
	if (selRow > 0) {
		QListWidgetItem *item = binPathList->takeItem(selRow);
		binPathList->insertItem(selRow - 1, item);
		binPathList->setCurrentItem(binPathList->item(selRow - 1));
		pathsChanged = true;
	}
}

void PrefsDialog::movePathDown()
{
	int selRow = -1;
	if (binPathList->selectedItems().count() > 0)
		selRow = binPathList->currentRow();
	if (selRow != -1 &&  selRow < binPathList->count() - 1) {
		QListWidgetItem *item = binPathList->takeItem(selRow);
		binPathList->insertItem(selRow + 1, item);
		binPathList->setCurrentItem(binPathList->item(selRow + 1));
		pathsChanged = true;
	}
}

void PrefsDialog::addPath()
{
	QString dir = QFileDialog::getExistingDirectory(this, tr("Choose Directory"),
	                 QString::fromLatin1("/usr"), /*0*/ /*QFileDialog::DontUseNativeDialog*/
								QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
	// remove the trailing / (if any)
	dir = QDir::fromNativeSeparators(dir);
#if defined(Q_OS_WIN)
	if (dir.length() > 2)
#else
	if (dir.length() > 1)
#endif
		if (dir.at(dir.length() - 1) == QChar::fromLatin1('/'))
			dir.chop(1);
	if (!dir.isEmpty()) {
		binPathList->addItem(dir);
		binPathList->setCurrentItem(binPathList->item(binPathList->count() - 1));
		pathsChanged = true;
	}
}

void PrefsDialog::removePath()
{
	if (binPathList->currentRow() > -1)
		if (binPathList->currentItem()->isSelected()) {
			binPathList->takeItem(binPathList->currentRow());
			pathsChanged = true;
		}
}

void PrefsDialog::updateToolButtons()
{
	int selRow = -1;
	if (toolList->selectedItems().count() > 0)
		selRow = toolList->currentRow();
	toolEdit->setEnabled(selRow != -1);
	toolRemove->setEnabled(selRow != -1);
	toolUp->setEnabled(selRow > 0);
	toolDown->setEnabled(selRow != -1 && selRow < toolList->count() - 1);
}

void PrefsDialog::moveToolUp()
{
	int selRow = -1;
	if (toolList->selectedItems().count() > 0)
		selRow = toolList->currentRow();
	if (selRow > 0) {
		QListWidgetItem *item = toolList->takeItem(selRow);
		toolList->insertItem(selRow - 1, item);
		toolList->setCurrentItem(toolList->item(selRow - 1));
		Engine e = engineList.takeAt(selRow);
		engineList.insert(selRow - 1, e);
		refreshDefaultTool();
		toolsChanged = true;
	}
}

void PrefsDialog::moveToolDown()
{
	int selRow = -1;
	if (toolList->selectedItems().count() > 0)
		selRow = toolList->currentRow();
	if (selRow != -1 &&  selRow < toolList->count() - 1) {
		QListWidgetItem *item = toolList->takeItem(selRow);
		toolList->insertItem(selRow + 1, item);
		toolList->setCurrentItem(toolList->item(selRow + 1));
		Engine e = engineList.takeAt(selRow);
		engineList.insert(selRow + 1, e);
		refreshDefaultTool();
		toolsChanged = true;
	}
}

void PrefsDialog::addTool()
{
	Engine e;
	e.setName(tr("New Tool"));
	e.setShowPdf(true);
	if (ToolConfig::doToolConfig(this, e) == QDialog::Accepted) {
		engineList.append(e);
		toolList->addItem(e.name());
		refreshDefaultTool();
		toolsChanged = true;
	}
}

void PrefsDialog::removeTool()
{
	if (toolList->currentRow() > -1)
		if (toolList->currentItem()->isSelected()) {
			engineList.removeAt(toolList->currentRow());
			toolList->takeItem(toolList->currentRow());
			refreshDefaultTool();
			toolsChanged = true;
		}
}

void PrefsDialog::editTool(QListWidgetItem* item)
{
	int row = -1;
	if (item)
		row = toolList->row(item);
	else if (toolList->currentRow() > -1 && toolList->currentItem()->isSelected())
		row = toolList->currentRow();
	if (row > -1) {
		Engine e = engineList[toolList->currentRow()];
		if (ToolConfig::doToolConfig(this, e) == QDialog::Accepted) {
			engineList[toolList->currentRow()] = e;
			toolList->currentItem()->setText(e.name());
			toolsChanged = true;
		}
	}
}

void PrefsDialog::refreshDefaultTool()
{
	QString val;
	int toolIndex = -1, defaultIndex = -1;

	if (defaultTool->count() > 0)
		val = defaultTool->currentText();
	defaultTool->clear();
	foreach (Engine e, engineList) {
		defaultTool->addItem(e.name());
		if (e.name() == val)
			toolIndex = defaultTool->count() - 1;
		if (e.name() == QString::fromUtf8(DEFAULT_ENGINE_NAME))
			defaultIndex = defaultTool->count() - 1;
	}

	if (toolIndex >= 0)
		defaultTool->setCurrentIndex(toolIndex);
	else if (defaultIndex >= 0)
		defaultTool->setCurrentIndex(defaultIndex);
}

void PrefsDialog::buttonClicked(QAbstractButton *whichButton)
{
	if (buttonBox->buttonRole(whichButton) == QDialogButtonBox::ResetRole)
		restoreDefaults();
}

void PrefsDialog::restoreDefaults()
{
  using Research::defaultPreference;
  Research::resetSettingControls(tabWidget->currentWidget());
  const auto page=tabWidget->currentWidget()->objectName();
  const QStringList ids{QStringLiteral("settingsGeneral"),QStringLiteral("settingsEditor"),QStringLiteral("settingsPdf"),QStringLiteral("settingsShortcuts"),QStringLiteral("settingsBuild"),QStringLiteral("settingsAdvanced")};
  switch(ids.indexOf(page)){
    case 0: {
      const int launch=defaultPreference("launchOption",kDefault_LaunchOption).toInt();(launch==2?templateDialog:launch==3?openDialog:blankDocument)->setChecked(true);
      int locale=localePopup->findData(defaultPreference("locale",QStringLiteral("zh_CN")));localePopup->setCurrentIndex(locale>=0?locale:kDefault_Locale);
      openPDFwithTeX->setChecked(defaultPreference("openPDFwithTeX",kDefault_OpenPDFwithTeX).toBool());break;
    }
    case 1: {
      QFont font;font.fromString(defaultPreference("font",QString()).toString());editorFont->setEditText(font.family());fontSize->setValue(font.pointSize());
      tabWidth->setValue(defaultPreference("tabWidth",kDefault_TabWidth).toInt());lineSpacing->setValue(defaultPreference("lineSpacing",kDefault_LineSpacing).toInt());
      lineNumbers->setChecked(defaultPreference("lineNumbers",kDefault_LineNumbers).toBool());wrapLines->setChecked(defaultPreference("wrapLines",kDefault_WrapLines).toBool());
      autoIndent->setCurrentIndex(1+kDefault_IndentMode);smartQuotes->setCurrentIndex(1+kDefault_QuotesMode);language->setCurrentIndex(kDefault_SpellcheckLanguage);
      highlightCurrentLine->setChecked(defaultPreference("highlightCurrentLine",kDefault_HighlightCurrentLine).toBool());
      if(auto *page=findChild<QWidget *>(QStringLiteral("researchCompletionPage")))if(auto *reset=page->findChild<QPushButton *>(QStringLiteral("completionResetPresets")))reset->click();break;
    }
    case 2: {
      const int fit=defaultPreference("scaleOption",kDefault_PreviewScaleOption).toInt();(fit==2?fitWidth:fit==3?fitWindow:fit==4?fixedScale:fit==5?fitContentWidth:actualSize)->setChecked(true);
      scale->setValue(defaultPreference("previewScale",kDefault_PreviewScale).toInt());pdfPageMode->setCurrentIndex(kDefault_PDFPageMode==QtPDF::PDFDocumentView::PageMode_SinglePage?0:kDefault_PDFPageMode==QtPDF::PDFDocumentView::PageMode_TwoColumnContinuous?2:1);
      autoFollowFocusEnabled->setChecked(defaultPreference("autoFollowFocusEnabled",kDefault_AutoFollowFocusEnabled).toBool());pdfPaperColor->setColor(kDefault_PaperColor);cbSyncToTeX->setCurrentIndex(TWSynchronizer::kDefault_Resolution_ToTeX);cbSyncToPDF->setCurrentIndex(TWSynchronizer::kDefault_Resolution_ToPDF);break;
    }
    case 3: Research::resetShortcutSettings(tabWidget->currentWidget());break;
    case 4: {
      engineList=TWApp::defaultEngines();binPathList->clear();binPathList->addItems(TWApp::instance()->getDefaultBinaryPaths());toolList->clear();defaultTool->clear();for(const auto &engine:engineList){toolList->addItem(engine.name());defaultTool->addItem(engine.name());}defaultTool->setCurrentText(QStringLiteral("pdfLaTeX"));autoHideOutput->setCurrentIndex(defaultPreference("autoHideConsole",kDefault_HideConsole).toInt());pathsChanged=toolsChanged=true;updatePathButtons();updateToolButtons();break;
    }
    case 5: {
      autocompleteEnabled->setChecked(defaultPreference("autocompleteEnabled",kDefault_AutocompleteEnabled).toBool());
      const auto syntax=defaultPreference("syntaxColoring",QStringLiteral("LaTeX")).toString();syntaxColoring->setCurrentIndex(qMax(0,syntaxColoring->findText(syntax)));encoding->setCurrentIndex(encoding->findText(defaultPreference("defaultEncoding",QStringLiteral("UTF-8")).toString()));cursorWidth->setValue(kDefault_CursorWidth);
      pdfRulerUnits->setCurrentIndex(kDefault_PreviewRulerUnits);pdfRulerShow->setChecked(kDefault_PreviewRulerShow);resolution->setDpi(QApplication::screens().first()->physicalDotsPerInch());pdfPageCacheSizeMiB->setValue(kDefault_PDFPageCacheSizeMiB);
      (kDefault_MagnifierSize==1?smallMag:kDefault_MagnifierSize==3?largeMag:mediumMag)->setChecked(true);circularMag->setChecked(kDefault_CircularMagnifier);
      const int icons=defaultPreference("toolBarIconSize",kDefault_ToolBarIcons).toInt();(icons==1?smallIcons:icons==3?largeIcons:mediumIcons)->setChecked(true);showText->setChecked(defaultPreference("toolBarShowText",kDefault_ToolBarText).toBool());
      allowScriptFileReading->setChecked(kDefault_AllowScriptFileReading);allowScriptFileWriting->setChecked(kDefault_AllowScriptFileWriting);allowSystemCommands->setChecked(kDefault_AllowSystemCommands);enableScriptingPlugins->setChecked(kDefault_EnableScriptingPlugins);scriptDebugger->setChecked(kDefault_ScriptDebugger);break;
    }
  }
}

void PrefsDialog::initPathAndToolLists()
{
	binPathList->clear();
	toolList->clear();
	defaultTool->clear();
	binPathList->addItems(TWApp::instance()->getPrefsBinaryPaths());
	engineList = TWApp::instance()->getEngineList();
	foreach (Engine e, engineList) {
		toolList->addItem(e.name());
		defaultTool->addItem(e.name());
		if (e.name() == TWApp::instance()->getDefaultEngine().name())
			defaultTool->setCurrentIndex(defaultTool->count() - 1);
	}
	if (binPathList->count() > 0)
		binPathList->setCurrentItem(binPathList->item(0));
	if (toolList->count() > 0)
		toolList->setCurrentItem(toolList->item(0));
	updatePathButtons();
	updateToolButtons();
}


typedef QPair<QString, QString> DictPair;

static bool dictPairLessThan(const DictPair& d1, const DictPair& d2)
{
	return d1.first.toLower() < d2.first.toLower();
}

QDialog::DialogCode PrefsDialog::doPrefsDialog(QWidget *parent, int page, const QString &query)
{
	QPointer<QWidget> previousFocus = QApplication::focusWidget();
	if (parent && (!previousFocus || previousFocus->window() != parent->window())) previousFocus = parent->focusWidget();
	PrefsDialog dlg(parent);
	using index_type = decltype(QComboBox().currentIndex());

	QStringList nameList;
	foreach (QTextCodec *codec, *TWUtils::findCodecs())
		nameList.append(QString::fromUtf8(codec->name().constData()));
	dlg.encoding->addItems(nameList);

	QStringList syntaxOptions = TeXHighlighter::syntaxOptions();
	dlg.syntaxColoring->addItems(syntaxOptions);

	QStringList indentModes = CompletingEdit::autoIndentModes();
	dlg.autoIndent->addItems(indentModes);

	QStringList quotesModes = CompletingEdit::smartQuotesModes();
	dlg.smartQuotes->addItems(quotesModes);

	QList< DictPair > dictList;
	foreach (const QString& dictKey, Tw::Document::SpellCheckManager::getDictionaryList()->uniqueKeys()) {
		foreach (QString dict, Tw::Document::SpellCheckManager::getDictionaryList()->values(dictKey)) {
			const QString label{Tw::Document::SpellCheckManager::labelForDict(dict)};
			dictList << qMakePair(label, dict);
		}
	}
	std::sort(dictList.begin(), dictList.end(), dictPairLessThan);
	foreach (const DictPair& dict, dictList)
		dlg.language->addItem(dict.first, dict.second);

	Tw::Settings settings;
	// initialize controls based on the current settings

	// General
	int oldIconSize = settings.value(QString::fromLatin1("toolBarIconSize"), kDefault_ToolBarIcons).toInt();
	switch (oldIconSize) {
		case 1:
			dlg.smallIcons->setChecked(true);
			break;
		case 3:
			dlg.largeIcons->setChecked(true);
			break;
		default:
			dlg.mediumIcons->setChecked(true);
			break;
	}
	bool oldShowText = settings.value(QString::fromLatin1("toolBarShowText"), kDefault_ToolBarText).toBool();
	dlg.showText->setChecked(oldShowText);

	switch (settings.value(QString::fromLatin1("launchOption"), kDefault_LaunchOption).toInt()) {
		default:
			dlg.blankDocument->setChecked(true);
			break;
		case 2:
			dlg.templateDialog->setChecked(true);
			break;
		case 3:
			dlg.openDialog->setChecked(true);
			break;
	}
	dlg.openPDFwithTeX->setChecked(settings.value(QString::fromLatin1("openPDFwithTeX"), kDefault_OpenPDFwithTeX).toBool());

	QString oldLocale = settings.value(QString::fromLatin1("locale")).toString();
	const QString effectiveLocale = Research::supportedUiLocale(oldLocale.isEmpty() ? QLocale::system().name() : oldLocale);
	dlg.localePopup->addItem(QStringLiteral("简体中文"), QStringLiteral("zh_CN"));
	dlg.localePopup->addItem(QStringLiteral("繁體中文"), QStringLiteral("zh_TW"));
	dlg.localePopup->addItem(QStringLiteral("English"), QStringLiteral("en_US"));
	const int oldLocaleIndex = dlg.localePopup->findData(effectiveLocale);
	dlg.localePopup->setCurrentIndex(oldLocaleIndex);

	// Editor
	dlg.syntaxColoring->setCurrentIndex(static_cast<index_type>(settings.contains(QString::fromLatin1("syntaxColoring"))
	                        ? 1 + syntaxOptions.indexOf(settings.value(QString::fromLatin1("syntaxColoring")).toString())
							: 1 + kDefault_SyntaxColoring));
	dlg.autoIndent->setCurrentIndex(static_cast<index_type>(settings.contains(QString::fromLatin1("autoIndent"))
	                        ? 1 + indentModes.indexOf(settings.value(QString::fromLatin1("autoIndent")).toString())
							: 1 + kDefault_IndentMode));
	dlg.smartQuotes->setCurrentIndex(static_cast<index_type>(settings.contains(QString::fromLatin1("smartQuotes"))
	                        ? 1 + quotesModes.indexOf(settings.value(QString::fromLatin1("smartQuotes")).toString())
							: 1 + kDefault_QuotesMode));
	dlg.lineNumbers->setChecked(settings.value(QString::fromLatin1("lineNumbers"), kDefault_LineNumbers).toBool());
	dlg.wrapLines->setChecked(settings.value(QString::fromLatin1("wrapLines"), kDefault_WrapLines).toBool());
	dlg.tabWidth->setValue(settings.value(QString::fromLatin1("tabWidth"), kDefault_TabWidth).toInt());
	dlg.lineSpacing->setValue(settings.value(QStringLiteral("lineSpacing"), kDefault_LineSpacing).toInt());
	const QStringList fontFamilies = []() {
		#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
		return QFontDatabase().families();
		#else
		return QFontDatabase::families();
		#endif
	}();
	dlg.editorFont->addItems(fontFamilies);
	QString fontString = settings.value(QString::fromLatin1("font")).toString();
	QFont font;
	if (!fontString.isEmpty())
		font.fromString(fontString);
	QPointer<TeXDocumentWindow> appearanceWindow = parent ? qobject_cast<TeXDocumentWindow *>(parent->window()) : nullptr;
	if (appearanceWindow) {
		font = appearanceWindow->editor()->font();
		dlg.lineSpacing->setValue(qRound(appearanceWindow->lineSpacing()));
	}
	const QFont initialFont = font;
	const qreal initialSpacing = appearanceWindow ? appearanceWindow->lineSpacing() : dlg.lineSpacing->value();
	const QTextCursor initialCursor = appearanceWindow ? appearanceWindow->textCursor() : QTextCursor();
	const QTextCursor readingAnchor = appearanceWindow ? appearanceWindow->editor()->cursorForPosition(QPoint(0,0)) : QTextCursor();
	const int readingY = appearanceWindow ? appearanceWindow->editor()->cursorRect(readingAnchor).top() : 0;
	const int readingX = appearanceWindow ? appearanceWindow->editor()->horizontalScrollBar()->value() : 0;
	const bool readingAtTop = appearanceWindow && appearanceWindow->editor()->verticalScrollBar()->value()==appearanceWindow->editor()->verticalScrollBar()->minimum();
	dlg.editorFont->setCurrentIndex(static_cast<index_type>(fontFamilies.indexOf(font.family())));
	if(dlg.editorFont->currentIndex()<0)dlg.editorFont->setEditText(font.family());
	dlg.fontSize->setValue(font.pointSize());
	dlg.encoding->setCurrentIndex(static_cast<index_type>(nameList.indexOf(QString::fromUtf8(TWApp::instance()->getDefaultCodec()->name().constData()))));
	dlg.highlightCurrentLine->setChecked(settings.value(QString::fromLatin1("highlightCurrentLine"), kDefault_HighlightCurrentLine).toBool());
	dlg.cursorWidth->setValue(settings.value(QStringLiteral("cursorWidth"), kDefault_CursorWidth).toInt());
	dlg.autocompleteEnabled->setChecked(settings.value(QString::fromLatin1("autocompleteEnabled"), kDefault_AutocompleteEnabled).toBool());
	dlg.autoFollowFocusEnabled->setChecked(settings.value(QStringLiteral("autoFollowFocusEnabled"), kDefault_AutoFollowFocusEnabled).toBool());

	QString defDict = settings.value(QString::fromLatin1("language"), QString::fromLatin1("None")).toString();
	int i = dlg.language->findData(defDict);
	if (i >= 0)
		dlg.language->setCurrentIndex(i);

	// Preview
	switch (settings.value(QString::fromLatin1("scaleOption"), kDefault_PreviewScaleOption).toInt()) {
		default:
			dlg.actualSize->setChecked(true);
			break;
		case 2:
			dlg.fitWidth->setChecked(true);
			break;
		case 3:
			dlg.fitWindow->setChecked(true);
			break;
		case 4:
			dlg.fixedScale->setChecked(true);
			break;
		case 5:
			dlg.fitContentWidth->setChecked(true);
			break;
	}
	dlg.scale->setValue(settings.value(QString::fromLatin1("previewScale"), kDefault_PreviewScale).toInt());
	switch (settings.value(QString::fromLatin1("pdfPageMode"), kDefault_PDFPageMode).toInt()) {
		case QtPDF::PDFDocumentView::PageMode_SinglePage:
			dlg.pdfPageMode->setCurrentIndex(0);
			break;
		case QtPDF::PDFDocumentView::PageMode_OneColumnContinuous:
		default:
			dlg.pdfPageMode->setCurrentIndex(1);
			break;
		case QtPDF::PDFDocumentView::PageMode_TwoColumnContinuous:
			dlg.pdfPageMode->setCurrentIndex(2);
			break;
	}

	dlg.pdfPaperColor->setColor(settings.value(QStringLiteral("pdfPaperColor"), QVariant::fromValue<QColor>(kDefault_PaperColor)).value<QColor>());

	dlg.pdfRulerUnits->setCurrentIndex(settings.value(QStringLiteral("pdfRulerUnits"), kDefault_PreviewRulerUnits).toInt());
	dlg.pdfRulerShow->setChecked(settings.value(QStringLiteral("pdfRulerShow"), kDefault_PreviewRulerShow).toBool());

	double oldResolution = settings.value(QString::fromLatin1("previewResolution"), QApplication::screens().first()->physicalDotsPerInch()).toDouble();
	dlg.resolution->setDpi(oldResolution);

	const int oldPDFPageCacheSize = settings.value(QStringLiteral("pdfPageCacheSizeMiB"), kDefault_PDFPageCacheSizeMiB).toInt();
	dlg.pdfPageCacheSizeMiB->setValue(oldPDFPageCacheSize);

	int oldSyncToTeX = settings.value(QString::fromLatin1("syncResolutionToTeX"), TWSynchronizer::kDefault_Resolution_ToTeX).toInt();
	dlg.cbSyncToTeX->setCurrentIndex(oldSyncToTeX);

	int oldSyncToPDF = settings.value(QString::fromLatin1("syncResolutionToPDF"), TWSynchronizer::kDefault_Resolution_ToPDF).toInt();
	dlg.cbSyncToPDF->setCurrentIndex(oldSyncToPDF);

	int oldMagSize = settings.value(QString::fromLatin1("magnifierSize"), kDefault_MagnifierSize).toInt();
	switch (oldMagSize) {
		case 1:
			dlg.smallMag->setChecked(true);
			break;
		default:
			dlg.mediumMag->setChecked(true);
			break;
		case 3:
			dlg.largeMag->setChecked(true);
			break;
	}
	bool oldCircular = settings.value(QString::fromLatin1("circularMagnifier"), kDefault_CircularMagnifier).toBool();
	dlg.circularMag->setChecked(oldCircular);

	// Typesetting
	dlg.initPathAndToolLists();
	QVariant hideConsoleSetting = settings.value(QString::fromLatin1("autoHideConsole"), kDefault_HideConsole);
	// Backwards compatibility to Tw 0.4.0 and before
	if (hideConsoleSetting.toString() == QLatin1String("true") || hideConsoleSetting.toString() == QLatin1String("false"))
		hideConsoleSetting = (hideConsoleSetting.toBool() ? kDefault_HideConsole : 0);
	dlg.autoHideOutput->setCurrentIndex(hideConsoleSetting.toInt());

	// Scripts
	dlg.allowScriptFileReading->setChecked(settings.value(QString::fromLatin1("allowScriptFileReading"), kDefault_AllowScriptFileReading).toBool());
	dlg.allowScriptFileWriting->setChecked(settings.value(QString::fromLatin1("allowScriptFileWriting"), kDefault_AllowScriptFileWriting).toBool());
	dlg.allowSystemCommands->setChecked(settings.value(QString::fromLatin1("allowSystemCommands"), kDefault_AllowSystemCommands).toBool());
	dlg.enableScriptingPlugins->setChecked(settings.value(QString::fromLatin1("enableScriptingPlugins"), kDefault_EnableScriptingPlugins).toBool());
	// there is always at least one built-in ScriptInterface
	if (TWApp::instance()->getScriptManager()->languages().size() <= 1)
		dlg.enableScriptingPlugins->setEnabled(false);
	dlg.scriptDebugger->setChecked(settings.value(QString::fromLatin1("scriptDebugger"), kDefault_ScriptDebugger).toBool());

	// Decide which tab to select initially
	if (sCurrentTab == -1) {
		if (parent && parent->inherits("TeXDocument"))
			sCurrentTab = 1;
		else if (parent && parent->inherits("PDFDocument"))
			sCurrentTab = 2;
		else
			sCurrentTab = 0;
	}
	if (sCurrentTab != dlg.tabWidget->currentIndex())
		dlg.tabWidget->setCurrentIndex(sCurrentTab);

	dlg.initResearchPages(parent);
	auto *sample = static_cast<EditorAppearanceSample *>(dlg.findChild<QTextEdit *>(QStringLiteral("researchEditorAppearanceSample")));
	QTimer appearanceTimer; appearanceTimer.setSingleShot(true); appearanceTimer.setInterval(0);
	QFont previewFont=initialFont;
	qreal previewSpacing=initialSpacing;
	QString lastFamily=initialFont.family();
	bool previewApplied=false;
	bool familyChanged=false,sizeChanged=false,spacingChanged=false;
	auto previewAppearance = [&] {
		const auto family=dlg.editorFont->currentText();
		if(fontFamilies.contains(family,Qt::CaseInsensitive)||family==initialFont.family())lastFamily=family;
		previewFont=initialFont;
		if(familyChanged)previewFont.setFamily(lastFamily);
		if(sizeChanged)previewFont.setPointSize(dlg.fontSize->value());
		previewSpacing=spacingChanged ? dlg.lineSpacing->value() : initialSpacing;
		if(sample)sample->preview(previewFont,previewSpacing);
		if(appearanceWindow) {
			previewApplied|=appearanceWindow->editor()->font()!=previewFont||!qFuzzyCompare(appearanceWindow->lineSpacing(),qreal(previewSpacing));
			appearanceWindow->setEditorAppearance(previewFont,previewSpacing);
		}
	};
	// Initializing the form must not turn the caller's temporary zoom into a preference.
	if(sample)sample->preview(initialFont,initialSpacing);
	QObject::connect(&appearanceTimer,&QTimer::timeout,&dlg,previewAppearance);
	QObject::connect(dlg.editorFont,&QComboBox::currentTextChanged,&dlg,[&]{familyChanged=true;appearanceTimer.start();});
	QObject::connect(dlg.fontSize,qOverload<int>(&QSpinBox::valueChanged),&dlg,[&]{sizeChanged=true;appearanceTimer.start();});
	QObject::connect(dlg.lineSpacing,qOverload<int>(&QSpinBox::valueChanged),&dlg,[&]{spacingChanged=true;appearanceTimer.start();});
	if(auto *edit=dlg.editorFont->lineEdit())QObject::connect(edit,&QLineEdit::editingFinished,&dlg,[&,edit]{
		if(!fontFamilies.contains(edit->text(),Qt::CaseInsensitive)&&edit->text()!=initialFont.family())edit->setText(lastFamily);
	});
	// Existing command destinations retain their meaning when categories merge.
	if(page>=0){
	  const QList<int> destinations{0,1,1,1,2,3,4,5,3};
	  dlg.tabWidget->setCurrentIndex(destinations.value(page,0));
	  const QHash<int,QString> targets{{2,QStringLiteral("researchFocusLineNumbersSetting")},{3,QStringLiteral("shorthandSearch")},{5,QStringLiteral("researchShortcutSearch")},{8,QStringLiteral("researchMenuSurface")}};
	  if(auto *target=dlg.findChild<QWidget *>(targets.value(page));targets.contains(page)&&target)Research::revealSetting(target);
	  if(page==5)if(auto *stack=dlg.findChild<QStackedWidget *>(QStringLiteral("researchInputPages")))stack->setCurrentIndex(1);
	}
	if(!query.isEmpty())if(auto *search=dlg.findChild<QLineEdit *>(QStringLiteral("researchSettingsSearch"))){search->setText(query);QMetaObject::invokeMethod(search,"returnPressed");}
	dlg.show();

	DialogCode result = static_cast<DialogCode>(dlg.exec());
	appearanceTimer.stop();
	if(result==Accepted)previewAppearance();
	else if(appearanceWindow&&previewApplied) {
		appearanceWindow->setEditorAppearance(initialFont,initialSpacing);
		auto *editor=appearanceWindow->editor();
		editor->setTextCursor(initialCursor);
		editor->verticalScrollBar()->setValue(readingAtTop ? editor->verticalScrollBar()->minimum() : editor->verticalScrollBar()->value()+editor->cursorRect(readingAnchor).top()-readingY);
		editor->horizontalScrollBar()->setValue(readingX);
	}

	if (result == Accepted) {
		sCurrentTab = dlg.tabWidget->currentIndex();
		Research::saveSettingControls(&dlg);
		Research::saveCompletionSettings(dlg.findChild<QWidget *>(QStringLiteral("researchCompletionPage")));
		Research::saveShortcutSettings(&dlg);
		for(auto *widget:QApplication::topLevelWidgets())if(auto *window=qobject_cast<QMainWindow *>(widget)){
		  const bool show=Research::settingValue("researchFocusLineNumbers").toBool();
		  window->setProperty("researchFocusLineNumbers",show);
		  if(auto *source=qobject_cast<TeXDocumentWindow*>(window)){source->editor()->refreshTypewriterMode();Research::refreshWritingStart(source);}
		  if(window->property("researchFocus").toBool())if(auto *a=window->findChild<QAction *>(QStringLiteral("actionLine_Numbers")))a->setChecked(show);
		}


		// General
		int iconSize = 2;
		if (dlg.smallIcons->isChecked())
			iconSize = 1;
		else if (dlg.largeIcons->isChecked())
			iconSize = 3;
		bool showText = dlg.showText->isChecked();
		if (iconSize != oldIconSize || showText != oldShowText) {
			settings.setValue(QString::fromLatin1("toolBarIconSize"), iconSize);
			settings.setValue(QString::fromLatin1("toolBarShowText"), showText);
			foreach (QWidget *widget, qApp->topLevelWidgets()) {
				QMainWindow *theWindow = qobject_cast<QMainWindow*>(widget);
				if (theWindow)
					TWUtils::applyToolbarOptions(theWindow, iconSize, showText);
			}
		}

		int launchOption = 1;
		if (dlg.templateDialog->isChecked())
			launchOption = 2;
		else if (dlg.openDialog->isChecked())
			launchOption = 3;
		settings.setValue(QString::fromLatin1("launchOption"), launchOption);

		settings.setValue(QString::fromLatin1("openPDFwithTeX"), dlg.openPDFwithTeX->isChecked());

		const QString selectedLocale = dlg.localePopup->currentData().toString();
		if (selectedLocale != oldLocale) {
			TWApp::instance()->applyTranslation(selectedLocale);
			settings.setValue(QStringLiteral("locale"), selectedLocale);
		}

		// Editor
		settings.setValue(QString::fromLatin1("syntaxColoring"), dlg.syntaxColoring->currentText());
		settings.setValue(QString::fromLatin1("autoIndent"), dlg.autoIndent->currentData().toString());
		settings.setValue(QString::fromLatin1("smartQuotes"), dlg.smartQuotes->currentData().toString());
		settings.setValue(QString::fromLatin1("lineNumbers"), dlg.lineNumbers->isChecked());
		settings.setValue(QString::fromLatin1("wrapLines"), dlg.wrapLines->isChecked());
		settings.setValue(QString::fromLatin1("tabWidth"), dlg.tabWidth->value());
		if(previewSpacing!=initialSpacing)settings.setValue(QStringLiteral("lineSpacing"), previewSpacing);
		if(previewFont!=initialFont)settings.setValue(QStringLiteral("font"), previewFont.toString());
		TWApp::instance()->setDefaultCodec(QTextCodec::codecForName(dlg.encoding->currentText().toLatin1()));
		if (dlg.language->currentIndex() >= 0) {
			QVariant data = dlg.language->itemData(dlg.language->currentIndex());
			if (data.isValid())
				settings.setValue(QString::fromLatin1("language"), data);
			else
				settings.setValue(QString::fromLatin1("language"), QString::fromLatin1("None"));
		}
		else
			settings.setValue(QString::fromLatin1("language"), QString::fromLatin1("None"));
		bool highlightLine = dlg.highlightCurrentLine->isChecked();
		settings.setValue(QString::fromLatin1("highlightCurrentLine"), highlightLine);
		CompletingEdit::setHighlightCurrentLine(highlightLine);

		settings.setValue(QStringLiteral("cursorWidth"), dlg.cursorWidth->value());

		bool autocompleteEnabled = dlg.autocompleteEnabled->isChecked();
		settings.setValue(QString::fromLatin1("autocompleteEnabled"), autocompleteEnabled);
		CompletingEdit::setAutocompleteEnabled(autocompleteEnabled);

		settings.setValue(QStringLiteral("autoFollowFocusEnabled"), dlg.autoFollowFocusEnabled->isChecked());

		// Since the tab width can't be set by any other means, forcibly update
		// all windows now
		foreach (QWidget* widget, TWApp::instance()->allWidgets()) {
			QTextEdit* editor = qobject_cast<QTextEdit*>(widget);
			if (editor)
#if QT_VERSION < QT_VERSION_CHECK(5, 10, 0)
				editor->setTabStopWidth(dlg.tabWidth->value());
#else
				editor->setTabStopDistance(dlg.tabWidth->value());
#endif
		}

		// Preview
		int scaleOption = 1;
		if (dlg.fitWidth->isChecked())
			scaleOption = 2;
		else if (dlg.fitWindow->isChecked())
			scaleOption = 3;
		else if (dlg.fixedScale->isChecked())
			scaleOption = 4;
		else if (dlg.fitContentWidth->isChecked())
			scaleOption = 5;
		int scale = dlg.scale->value();
		settings.setValue(QString::fromLatin1("scaleOption"), scaleOption);
		settings.setValue(QString::fromLatin1("previewScale"), scale);
		switch (dlg.pdfPageMode->currentIndex()) {
			case 0:
			    settings.setValue(QString::fromLatin1("pdfPageMode"), QtPDF::PDFDocumentView::PageMode_SinglePage);
				break;
			case 1:
			    settings.setValue(QString::fromLatin1("pdfPageMode"), QtPDF::PDFDocumentView::PageMode_OneColumnContinuous);
				break;
			case 2:
			    settings.setValue(QString::fromLatin1("pdfPageMode"), QtPDF::PDFDocumentView::PageMode_TwoColumnContinuous);
				break;
		}

		settings.setValue(QStringLiteral("pdfPaperColor"), dlg.pdfPaperColor->color());

		settings.setValue(QStringLiteral("pdfRulerUnits"), dlg.pdfRulerUnits->currentIndex());
		settings.setValue(QStringLiteral("pdfRulerShow"), dlg.pdfRulerShow->isChecked());

		double resolution = dlg.resolution->dpi();
		if (resolution != oldResolution) {
			settings.setValue(QString::fromLatin1("previewResolution"), resolution);
			foreach (QWidget *widget, qApp->topLevelWidgets()) {
				PDFDocumentWindow *thePdfDoc = qobject_cast<PDFDocumentWindow*>(widget);
				if (thePdfDoc)
					thePdfDoc->setResolution(resolution);
			}
		}

		const int pdfPageCacheSize = dlg.pdfPageCacheSizeMiB->value();
		settings.setValue(QStringLiteral("pdfPageCacheSizeMiB"), pdfPageCacheSize);
		QtPDF::Backend::Document::pageCache().setMaxCost(pdfPageCacheSize * 1024 * 1024);

		int syncToTeX = dlg.cbSyncToTeX->currentIndex();
		if (syncToTeX != oldSyncToTeX)
			settings.setValue(QString::fromLatin1("syncResolutionToTeX"), syncToTeX);

		int syncToPDF = dlg.cbSyncToPDF->currentIndex();
		if (syncToPDF != oldSyncToPDF)
			settings.setValue(QString::fromLatin1("syncResolutionToPDF"), syncToPDF);

		int magSize = 2;
		if (dlg.smallMag->isChecked())
			magSize = 1;
		else if (dlg.largeMag->isChecked())
			magSize = 3;
		settings.setValue(QString::fromLatin1("magnifierSize"), magSize);
		bool circular = dlg.circularMag->isChecked();
		settings.setValue(QString::fromLatin1("circularMagnifier"), circular);
		if (oldMagSize != magSize || oldCircular != circular) {
			foreach (QWidget *widget, qApp->topLevelWidgets()) {
				PDFDocumentWindow *thePdfDoc = qobject_cast<PDFDocumentWindow*>(widget);
				if (thePdfDoc)
					thePdfDoc->resetMagnifier();
			}
		}

		// Typesetting
		if (dlg.pathsChanged) {
			QStringList paths;
			for (int i = 0; i < dlg.binPathList->count(); ++i)
				paths << dlg.binPathList->item(i)->text();
			TWApp::instance()->setBinaryPaths(paths);
		}
		if (dlg.toolsChanged)
			TWApp::instance()->setEngineList(dlg.engineList);
		TWApp::instance()->setDefaultEngine(dlg.defaultTool->currentText());
		settings.setValue(QString::fromLatin1("autoHideConsole"), dlg.autoHideOutput->currentIndex());

		// Scripts
		settings.setValue(QString::fromLatin1("allowScriptFileReading"), dlg.allowScriptFileReading->isChecked());
		settings.setValue(QString::fromLatin1("allowScriptFileWriting"), dlg.allowScriptFileWriting->isChecked());
		settings.setValue(QString::fromLatin1("allowSystemCommands"), dlg.allowSystemCommands->isChecked());
		settings.setValue(QString::fromLatin1("enableScriptingPlugins"), dlg.enableScriptingPlugins->isChecked());
		settings.setValue(QString::fromLatin1("scriptDebugger"), dlg.scriptDebugger->isChecked());
		// with changed settings, the availability of scripts may have changed
		// (e.g., because of enabling/disabling the use of scripting plugins)
		TWApp::instance()->updateScriptsList();
	}

	if (previousFocus && previousFocus->isVisible() && previousFocus->isEnabled())
		previousFocus->setFocus(Qt::PopupFocusReason);
	return result;
}

void PrefsDialog::initResearchPages(QWidget *workspace)
{
  using namespace Research;
  localizeEditorModeChoices(autoIndent,smartQuotes);
  auto *legacy=new QWidget(this);legacy->hide();
  while(tabWidget->count()){auto *page=tabWidget->widget(0);tabWidget->removeTab(0);page->setParent(legacy);page->hide();}
  auto general=settingsPage(tabWidget,"settingsGeneral","通用","General");
  auto editor=settingsPage(tabWidget,"settingsEditor","写作","Writing");
  auto pdf=settingsPage(tabWidget,"settingsPdf","PDF 阅读","PDF reading");
  auto shortcuts=settingsPage(tabWidget,"settingsShortcuts","菜单与快捷键","Menus & shortcuts");
  auto tools=settingsPage(tabWidget,"settingsBuild","编译","Build");
  auto advanced=settingsPage(tabWidget,"settingsAdvanced","高级","Advanced");
  auto radioGroup=[this](const QList<QAbstractButton *> &buttons){auto *group=new QButtonGroup(this);for(auto *button:buttons)group->addButton(button);};
  radioGroup({blankDocument,templateDialog,openDialog});radioGroup({smallIcons,mediumIcons,largeIcons});radioGroup({actualSize,fitWidth,fitWindow,fitContentWidth,fixedScale});radioGroup({smallMag,mediumMag,largeMag});
  auto row=[](QVBoxLayout *page,const char *zh,const char *en,QWidget *control,const QString &keys=QString()){settingsRow(page,words(zh,en),control,keys);control->show();};
  auto check=[&](QVBoxLayout *page,QCheckBox *control,const char *zh,const char *en){control->setText(words(zh,en));row(page,"","",control);};
  auto radio=[&](QVBoxLayout *page,QRadioButton *control,const char *zh,const char *en){control->setText(words(zh,en));row(page,"","",control);};
  settingsHeading(general,words("界面","Interface"));row(general,"语言 / Language","语言 / Language",localePopup,QStringLiteral("改语言 language locale"));
  row(general,"","",settingControl("researchReduceMotion",this));row(general,"","",settingControl("researchDetectBeamer",this));
  settingsHeading(general,words("启动时","On launch"));radio(general,blankDocument,"空白文稿","Blank document");radio(general,templateDialog,"选择模板","Choose a template");radio(general,openDialog,"打开文件","Open a file");check(general,openPDFwithTeX,"同时打开对应 PDF","Open the corresponding PDF");
  row(general,"","",settingControl("researchStartRecentFiles",this));

  auto *writingBasics=new QWidget;writingBasics->setObjectName(QStringLiteral("researchWritingBasics"));auto *writingLayout=new QVBoxLayout(writingBasics);writingLayout->setContentsMargins(0,0,0,0);writingLayout->setSpacing(6);editor->addWidget(writingBasics);
  settingsHeading(writingLayout,qobject_cast<TeXDocumentWindow *>(workspace?workspace->window():nullptr)?words("当前文稿与新文稿","Current and new documents"):words("新文稿默认设置","New document defaults"));
  auto *fontRow=new QWidget(this);auto *fontLayout=new QHBoxLayout(fontRow);fontLayout->setContentsMargins(0,0,0,0);fontLayout->setSpacing(8);fontLayout->addWidget(editorFont,1);fontRow->setFocusProxy(editorFont);fontLayout->addWidget(fontSize);editorFont->show();fontSize->show();fontSize->setAccessibleName(words("字号","Font size"));fontSize->setToolTip(words("字号","Font size"));fontSize->setMaximumWidth(80);row(writingLayout,"字体","Font",fontRow,QStringLiteral("字号 size"));
  row(writingLayout,"行距","Line spacing",lineSpacing);
  row(writingLayout,"预览","Preview",new EditorAppearanceSample(this));
  settingsHeading(writingLayout,words("新文稿默认设置","New document defaults"));
  check(writingLayout,lineNumbers,"显示行号","Show line numbers");check(writingLayout,wrapLines,"自动换行","Wrap lines");
  auto *input=settingsDetails(writingLayout,"researchInputDetails",words("缩进、引号与拼写…","Indentation, quotes and spelling…"));row(input,"自动缩进","Auto indent",autoIndent);row(input,"引号","Quotes",smartQuotes);row(input,"拼写检查","Spelling",language);settingsHeading(input,words("所有编辑窗口","All editor windows"));row(input,"缩进宽度","Indent width",tabWidth);
  settingsHeading(writingLayout,words("写作时","While writing"));check(writingLayout,highlightCurrentLine,"突出当前行","Highlight current line");row(writingLayout,"","",settingControl("researchFocusLineNumbers",this));row(writingLayout,"","",settingControl("researchTypewriterMode",this));
  auto *completionPage=completionSettings(this,qobject_cast<QMainWindow *>(workspace),{},false,true);editor->addWidget(completionPage);

  settingsHeading(pdf,words("新 PDF 默认设置","New PDF defaults"));radio(pdf,actualSize,"实际大小","Actual size");radio(pdf,fitWidth,"适合页面宽度","Fit page width");radio(pdf,fitWindow,"适合整页","Fit page");radio(pdf,fitContentWidth,"适合正文宽度","Fit content width");radio(pdf,fixedScale,"固定缩放","Fixed zoom");row(pdf,"缩放比例","Zoom",scale);auto *zoomRow=scale->parentWidget();auto showZoom=[this,zoomRow]{const bool visible=fixedScale->isChecked();zoomRow->setVisible(visible);scale->setProperty("settingsRevealFallback",visible?QVariant():QVariant::fromValue<QObject*>(fixedScale));};connect(fixedScale,&QRadioButton::toggled,this,showZoom);showZoom();row(pdf,"页面排列","Pages",pdfPageMode);row(pdf,"纸张底色","Paper color",pdfPaperColor);
  auto *sync=settingsDetails(pdf,"researchSyncDetails",words("双向定位…","Source and PDF navigation…"));row(sync,"源码 → PDF","Source → PDF",cbSyncToPDF);row(sync,"PDF → 源码","PDF → source",cbSyncToTeX);check(sync,autoFollowFocusEnabled,"源码与 PDF 跟随焦点","Follow focus between source and PDF");
  pdfPaperColor->setText(words("选择颜色…","Choose color…"));pdfPaperColor->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);configureButton(pdfPaperColor,ButtonRole::Text);

  auto *inputTabs=new QTabBar(this);inputTabs->setObjectName(QStringLiteral("researchInputTabs"));inputTabs->setExpanding(false);inputTabs->addTab(words("菜单","Menus"));inputTabs->addTab(words("快捷键","Shortcuts"));shortcuts->addWidget(inputTabs);
  auto *inputPages=new QStackedWidget(this);inputPages->setObjectName(QStringLiteral("researchInputPages"));shortcuts->addWidget(inputPages,1);
  auto *menuPage=new QWidget;auto *menuLayout=new QVBoxLayout(menuPage);menuLayout->setContentsMargins(0,12,0,0);appendMenuChoiceSettings(menuLayout);inputPages->addWidget(menuPage);
  inputPages->addWidget(shortcutSettings(this,qobject_cast<QMainWindow *>(workspace)));
  connect(inputTabs,&QTabBar::currentChanged,inputPages,&QStackedWidget::setCurrentIndex);connect(inputPages,&QStackedWidget::currentChanged,inputTabs,&QTabBar::setCurrentIndex);

  settingsHeading(tools,words("编译","Build"));row(tools,"默认引擎","Default engine",defaultTool);row(tools,"编译后收起输出","Hide output after build",autoHideOutput);
  auto *toolDetails=settingsDetails(tools,"researchBuildDetails",words("管理编译工具…","Manage build tools…"));
  configureSettingsList(toolList);toolList->setMinimumHeight(120);toolList->setMaximumHeight(160);row(toolDetails,"","",toolList);auto *toolRow=new QWidget(this);auto *toolLayout=new QHBoxLayout(toolRow);toolLayout->setContentsMargins(0,0,0,0);for(auto *button:{toolUp,toolDown,toolAdd,toolRemove,toolEdit}){toolLayout->addWidget(button);button->show();}toolLayout->addStretch();toolDetails->addWidget(toolRow);
  configureSettingsList(binPathList);binPathList->setMinimumHeight(60);binPathList->setMaximumHeight(80);row(toolDetails,"程序位置","Program locations",binPathList,QStringLiteral("路径 path TeX Live MiKTeX"));auto *paths=new QWidget(this);auto *pathLayout=new QHBoxLayout(paths);pathLayout->setContentsMargins(0,0,0,0);for(auto *button:{pathUp,pathDown,pathAdd,pathRemove}){pathLayout->addWidget(button);button->show();}pathLayout->addStretch();toolDetails->addWidget(paths);

  settingsHeading(advanced,words("新文稿格式","New document format"));row(advanced,"语法着色","Syntax coloring",syntaxColoring);row(advanced,"编码","Encoding",encoding);row(advanced,"光标宽度","Cursor width",cursorWidth);
  auto *pdfTools=settingsDetails(advanced,"researchPdfToolsDetails",words("PDF 工具…","PDF tools…"));row(pdfTools,"标尺单位","Ruler units",pdfRulerUnits);row(pdfTools,"显示标尺","Show ruler",pdfRulerShow);row(pdfTools,"显示校准","Display calibration",resolution);row(pdfTools,"渲染缓存","Render cache",pdfPageCacheSizeMiB);radio(pdfTools,smallMag,"小放大镜","Small magnifier");radio(pdfTools,mediumMag,"中放大镜","Medium magnifier");radio(pdfTools,largeMag,"大放大镜","Large magnifier");check(pdfTools,circularMag,"圆形放大镜","Circular magnifier");
  auto *scripts=settingsDetails(advanced,"researchScriptDetails",words("脚本权限…","Script permissions…"));check(scripts,allowScriptFileReading,"读取文件","Read files");check(scripts,allowScriptFileWriting,"写入文件","Write files");check(scripts,allowSystemCommands,"运行系统命令","Run system commands");check(scripts,enableScriptingPlugins,"启用脚本插件","Enable scripting plugins");check(scripts,scriptDebugger,"脚本调试","Script debugging");
  auto *compat=settingsDetails(advanced,"researchCompatibilityDetails",words("兼容选项…","Compatibility…"));check(compat,autocompleteEnabled,"使用传统缩写表","Use the legacy abbreviation table");settingsHeading(compat,words("传统工具栏","Legacy toolbars"));radio(compat,smallIcons,"小图标","Small icons");radio(compat,mediumIcons,"中图标","Medium icons");radio(compat,largeIcons,"大图标","Large icons");check(compat,showText,"同时显示文字","Show text with icons");
  for(auto *page:{general,editor,pdf,tools,advanced})page->addStretch();
  auto *snippetToggle=completionPage->findChild<QToolButton *>(QStringLiteral("researchSnippetDetailsToggle"));
  auto *snippetBody=completionPage->findChild<QWidget *>(QStringLiteral("researchSnippetDetails"));
  writingBasics->setProperty("settingsDisclosureClosedBy",QVariant::fromValue(static_cast<QObject *>(snippetToggle)));
  // Keep a single draft and one visible scroll surface. Managing snippets takes
  // the writing page; returning restores the existing writing controls.
  connect(snippetToggle,&QToolButton::toggled,this,[=](bool open){
    auto *area=findChild<QScrollArea *>(QStringLiteral("settingsEditor"));
    if(open)completionPage->setProperty("writingScroll",area->verticalScrollBar()->value());
    writingBasics->setVisible(!open);
    editor->setStretch(editor->indexOf(completionPage),open?1:0);editor->setStretch(editor->count()-1,open?0:1);
    if(auto *rows=qobject_cast<QVBoxLayout *>(completionPage->layout())){rows->removeWidget(snippetToggle);rows->insertWidget(open?0:1,snippetToggle,0,Qt::AlignLeft);rows->setStretch(rows->indexOf(snippetBody),open?1:0);}
    snippetToggle->setText(open?words("返回写作设置","Back to writing settings"):words("管理片段…","Manage snippets…"));
    snippetToggle->setIcon(QIcon(open?QStringLiteral(":/research/icons/chevron-left.svg"):QStringLiteral(":/research/icons/chevron-right.svg")));
    configureButton(snippetToggle,ButtonRole::Text);
    if(!open&&!snippetToggle->property("settingsSearchReveal").toBool()){const auto revision=property("settingsRevealRevision");QTimer::singleShot(0,this,[=]{if(!snippetToggle->isChecked()&&property("settingsRevealRevision")==revision)area->verticalScrollBar()->setValue(completionPage->property("writingScroll").toInt());});}
  });

  tabWidget->setCurrentIndex(qBound(0,sCurrentTab,tabWidget->count()-1));installSettingsShell(this,tabWidget,buttonBox);if(workspace&&workspace->width()>=400)resize(qMin(width(),workspace->width()-24),height());
  disconnect(buttonBox,&QDialogButtonBox::accepted,this,&QDialog::accept);
  connect(buttonBox,&QDialogButtonBox::accepted,this,[this,completionPage]{if(Research::validateCompletionSettings(completionPage))accept();else {tabWidget->setCurrentIndex(1);Research::revealSetting(completionPage->findChild<QWidget *>(QStringLiteral("shorthandValidation")));}});
}

int PrefsDialog::sCurrentTab = -1;

ToolConfig::ToolConfig(QWidget *parent)
	: QDialog(parent)
{
	init();
}

void ToolConfig::init()
{
	setupUi(this);
	Research::configureSettingsSurface(this);

	connect(arguments, &QListWidget::itemSelectionChanged, this, &ToolConfig::updateArgButtons);
	connect(argUp, &QToolButton::clicked, this, &ToolConfig::moveArgUp);
	connect(argDown, &QToolButton::clicked, this, &ToolConfig::moveArgDown);
	connect(argAdd, &QToolButton::clicked, this, &ToolConfig::addArg);
	connect(argRemove, &QToolButton::clicked, this, &ToolConfig::removeArg);
	connect(btnBrowseForProgram, &QPushButton::clicked, this, &ToolConfig::browseForProgram);
}

void ToolConfig::browseForProgram()
{
	QFileInfo info(program->text());
	QString str = QFileDialog::getOpenFileName(this, tr("Select program file"),
											   info.exists() ? info.canonicalFilePath() : QString());
	if (!str.isNull()) {
		info.setFile(str);
		if (!info.isExecutable()) {
			QMessageBox::warning(this, tr("Invalid program"),
								 tr("The file '%1' is not executable!").arg(info.fileName()));
			return;
		}
		program->setText(info.canonicalFilePath());
	}
}

void ToolConfig::updateArgButtons()
{
	int selRow = -1;
	if (arguments->selectedItems().count() > 0)
		selRow = arguments->currentRow();
	argRemove->setEnabled(selRow != -1);
	argUp->setEnabled(selRow > 0);
	argDown->setEnabled(selRow != -1 && selRow < arguments->count() - 1);
}

void ToolConfig::moveArgUp()
{
	int selRow = -1;
	if (arguments->selectedItems().count() > 0)
		selRow = arguments->currentRow();
	if (selRow > 0) {
		QListWidgetItem *item = arguments->takeItem(selRow);
		arguments->insertItem(selRow - 1, item);
		arguments->setCurrentItem(arguments->item(selRow - 1));
	}
}

void ToolConfig::moveArgDown()
{
	int selRow = -1;
	if (arguments->selectedItems().count() > 0)
		selRow = arguments->currentRow();
	if (selRow != -1 &&  selRow < arguments->count() - 1) {
		QListWidgetItem *item = arguments->takeItem(selRow);
		arguments->insertItem(selRow + 1, item);
		arguments->setCurrentItem(arguments->item(selRow + 1));
	}
}

void ToolConfig::addArg()
{
	arguments->addItem(tr("NewArgument"));
	QListWidgetItem* item = arguments->item(arguments->count() - 1);
	item->setFlags(item->flags() | Qt::ItemIsEditable);
	arguments->setCurrentItem(item);
	arguments->editItem(item);
}

void ToolConfig::removeArg()
{
	if (arguments->currentRow() > -1)
		if (arguments->currentItem()->isSelected())
			arguments->takeItem(arguments->currentRow());
}

QDialog::DialogCode ToolConfig::doToolConfig(QWidget *parent, Engine &engine)
{
	ToolConfig dlg(parent);

	dlg.toolName->setText(engine.name());
	dlg.program->setText(engine.program());
	dlg.arguments->addItems(engine.arguments());
	for (int i = 0; i < dlg.arguments->count(); ++i) {
		QListWidgetItem *item = dlg.arguments->item(i);
		item->setFlags(item->flags() | Qt::ItemIsEditable);
	}
	dlg.viewPdf->setChecked(engine.showPdf());

	dlg.show();

	DialogCode result = static_cast<DialogCode>(dlg.exec());
	if (result == Accepted) {
		dlg.arguments->setCurrentItem(nullptr); // ensure editing is terminated
		engine.setName(dlg.toolName->text());
		engine.setProgram(dlg.program->text());
		QStringList args;
		for (int i = 0; i < dlg.arguments->count(); ++i)
			args << dlg.arguments->item(i)->text();
		engine.setArguments(args);
		engine.setShowPdf(dlg.viewPdf->isChecked());
	}

	return result;
}
