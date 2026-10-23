#include "ResearchFileWrite.h"
#include "ResearchProject.h"
#include "ResearchVersionControl.h"
#include "ResearchFind.h"
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

#include "TeXDocumentWindow.h"
#include "ResearchPublication.h"
#include "ResearchOutputPolicy.h"
#include "ResearchWorkflow.h"
#include "ResearchReferences.h"
#include "ResearchAuthoring.h"
#include "ResearchCollaboration.h"
#include "ResearchAppearance.h"
#include "ResearchMotion.h"
#include "ResearchShorthand.h"
#include "ResearchDependencies.h"
#include "ResearchSlides.h"
#include "ResearchArticle.h"
#include "ResearchStart.h"
#include "ResearchWritingContext.h"

#include "CitationSelectDialog.h"
#include "DefaultPrefs.h"
#include "Engine.h"
#include "FindDialog.h"
#include "HardWrapDialog.h"
#include "PDFDocumentWindow.h"
#include "Settings.h"
#include "TWApp.h"
#include "TWUtils.h"
#include "TeXDocks.h"
#include "TeXHighlighter.h"
#include "TemplateDialog.h"
#include "scripting/ScriptAPI.h"
#include "document/SpellChecker.h"
#include "document/SpellCheckManager.h"
#include "ui/ClickableLabel.h"
#include "ui/RemoveAuxFilesDialog.h"
#include "utils/CmdKeyFilter.h"
#include "utils/WindowManager.h"
#include "utils/FilePath.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractTextDocumentLayout>
#include <QActionGroup>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QThread>
#include <QThreadPool>
#include <QRunnable>
#include <QFileDialog>
#include <QFileSystemWatcher>
#include <QFontDialog>
#include <QInputDialog>
#include <QLineEdit>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QShortcut>
#include <QToolBar>
#include <QToolButton>
#include <QWidgetAction>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalMapper>
#include <QStatusBar>
#include <QStringList>
#include <QTextBrowser>
#include <QTextCodec>
#include <QUrl>
#include <QUrlQuery>
#include <QTimer>
#include <QPointer>
#include <QMutex>
#include <QMutexLocker>

#if defined(Q_OS_WIN)
#include <windows.h>
#endif

namespace {
QThreadPool &buildPreparationPool(){static QThreadPool pool;static const bool ready=[](){pool.setMaxThreadCount(1);pool.setExpiryTimeout(10000);return true;}();Q_UNUSED(ready);return pool;}
struct FinishSourceState {
  QMutex mutex;
  QHash<quintptr,QPair<QString,bool>> buffers;
  void update(quintptr id,const QString &path,bool modified){QMutexLocker lock(&mutex);buffers.insert(id,qMakePair(path,modified));}
  void retire(quintptr id){QMutexLocker lock(&mutex);buffers.remove(id);}
  bool changed(const QStringList &paths){QMutexLocker lock(&mutex);for(const auto &buffer:buffers)if(buffer.second&&paths.contains(buffer.first))return true;return false;}
};
// The stock parser never changes its reading context while parsing. Keep its
// permission checks live, but avoid repeatedly resolving the same GUI root.
class LogSnapshotAPI final : public Tw::Scripting::ScriptAPI {
  QPointer<QObject> readingContext;
public:
  LogSnapshotAPI(Tw::Scripting::ScriptObject *script,QObject *app,QObject *target,QVariant &result,QObject *snapshot)
    :ScriptAPI(script,app,target,result),readingContext(snapshot){}
  QObject *clone()const override{return new LogSnapshotAPI(m_script,m_app,m_target,m_result,readingContext);}
  int fileExists(const QString &filename)const override{
    if(!readingContext)return SystemAccess_PermissionDenied;
    const QString path=QFileInfo(m_script->getFilename()).dir().absoluteFilePath(filename);
    if(!mayReadFile(path,readingContext))return SystemAccess_PermissionDenied;
    return QFileInfo::exists(path)?SystemAccess_OK:SystemAccess_Failed;
  }
};
}

QList<TeXDocumentWindow*> TeXDocumentWindow::docList;

TeXDocumentWindow::TeXDocumentWindow()
	: _texDoc(new Tw::Document::TeXDocument(this))
{
	init();
	Research::showTransientStatusMessage(this, tr("New document"), kStatusMessageDuration);
	if (researchCollaboration) researchCollaboration->acceptedDisk(textEdit->toPlainText());
	Research::startRecovery(this);
}

TeXDocumentWindow::TeXDocumentWindow(const QString &fileName, bool asTemplate, bool inBackground)
	: _texDoc(new Tw::Document::TeXDocument(this))
{
	setProperty("researchBackgroundSource",inBackground);
	init(true);
	loadFile(QFileInfo(fileName), asTemplate, inBackground);
	Research::startRecovery(this);
}

TeXDocumentWindow::~TeXDocumentWindow()
{
	researchPrepareEpoch->fetch_add(1);
	if(researchFinishCancelled)researchFinishCancelled->store(true);
	if(researchBuildPreparing||researchBuildFinishing)TWApp::instance()->typesetManager().stopTypesetting(this);
	flushScriptEdits();
	// Disconnect from notifications to avoid getting signals after our
	// TeXDocumentWindow is destroyed (but before the base QObject is destroyed,
	// in which case the system wouldn't know what to do with the signal)
	disconnect(TWApp::instance(), &TWApp::windowListChanged, this, nullptr);
	disconnect(TWApp::instance(), &TWApp::recentFileActionsChanged, this, nullptr);
	docList.removeAll(this);
	updateWindowMenu();
	// Because _texDoc->parent() == this, _texDoc will be destroyed
	// automatically by ~QObject()
#if defined(Q_OS_DARWIN)
	// Work around QTBUG-17941
	QTimer::singleShot(0, TWApp::instance(), &TWApp::recreateSpecialMenuItems);
#endif // defined(Q_OS_DARWIN)
}

static bool dictActionLessThan(const QAction * a1, const QAction * a2) {
	return a1->text().toLower() < a2->text().toLower();
}

void TeXDocumentWindow::init(bool openingFile)
{
	codec = TWApp::instance()->getDefaultCodec();
	pdfDoc = nullptr;
	process = nullptr;
	utf8BOM = false;
#if defined(Q_OS_WIN)
	lineEndings = kLineEnd_CRLF;
#else
	lineEndings = kLineEnd_LF;
#endif

	setupUi(this);
	editor()->setDocument(textDoc());
	Research::installSourceLayoutCache(textDoc());
	Research::installWorkflow(this);

	setAttribute(Qt::WA_DeleteOnClose, true);

	setContextMenuPolicy(Qt::NoContextMenu);

	makeUntitled();
	hideConsole();
	keepConsoleOpen = false;
	connect(consoleTabs, &Tw::UI::ClosableTabWidget::requestClose, actionShow_Hide_Console, &QAction::trigger);

	statusBar()->addPermanentWidget(lineEndingLabel = new Tw::UI::ClickableLabel());
	lineEndingLabel->setFrameStyle(QFrame::NoFrame);
	lineEndingLabel->setFont(statusBar()->font());
	connect(lineEndingLabel, &Tw::UI::ClickableLabel::mouseLeftClick, this, &TeXDocumentWindow::lineEndingLabelClick);
	showLineEndingSetting();

	statusBar()->addPermanentWidget(encodingLabel = new Tw::UI::ClickableLabel());
	encodingLabel->setFrameStyle(QFrame::NoFrame);
	encodingLabel->setFont(statusBar()->font());
	connect(encodingLabel, &Tw::UI::ClickableLabel::mouseLeftClick, this, &TeXDocumentWindow::encodingLabelClick);
	showEncodingSetting();

	statusBar()->addPermanentWidget(lineNumberLabel = new Tw::UI::ClickableLabel());
	lineNumberLabel->setFrameStyle(QFrame::NoFrame);
	lineNumberLabel->setFont(statusBar()->font());
	connect(lineNumberLabel, &Tw::UI::ClickableLabel::mouseLeftClick, this, &TeXDocumentWindow::doLineDialog);
	showCursorPosition();

	engineActions = new QActionGroup(this);
	connect(engineActions, &QActionGroup::triggered, this, static_cast<void (TeXDocumentWindow::*)(QAction*)>(&TeXDocumentWindow::selectedEngine));

	codec = TWApp::instance()->getDefaultCodec();
	engineName = TWApp::instance()->getDefaultEngine().name();
	engine = new QComboBox(this);
	engine->setEditable(false);
	engine->setFocusPolicy(Qt::NoFocus);
	engine->setSizeAdjustPolicy(QComboBox::AdjustToContents);
#if defined(Q_OS_DARWIN)
	engine->setStyleSheet(QString::fromLatin1("padding:4px;"));
	engine->setMinimumWidth(150);
#endif
	toolBar_run->addWidget(engine);
	updateEngineList();
	connect(engine, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, static_cast<void (TeXDocumentWindow::*)(int)>(&TeXDocumentWindow::selectedEngine));

	connect(TWApp::instance(), &TWApp::engineListChanged, this, &TeXDocumentWindow::updateEngineList);
	connect(engine, static_cast<void (QComboBox::*)(int)>(&QComboBox::activated), this, [this](int index) {
		Research::rememberEngine(textDoc()->absoluteFilePath(), engine->itemText(index));
	});

	connect(actionNew, &QAction::triggered, this, &TeXDocumentWindow::newFile);
	connect(actionNew_from_Template, &QAction::triggered, this, &TeXDocumentWindow::newFromTemplate);
	connect(actionOpen, &QAction::triggered, this, static_cast<void (TeXDocumentWindow::*)()>(&TeXDocumentWindow::open));
	connect(actionAbout_TW, &QAction::triggered, TWApp::instance(), &TWApp::about);
	connect(actionSettings_and_Resources, &QAction::triggered, TWApp::instance(), &TWApp::doResourcesDialog);
	connect(actionGoToHomePage, &QAction::triggered, TWApp::instance(), &TWApp::goToHomePage);
	connect(actionWriteToMailingList, &QAction::triggered, TWApp::instance(), &TWApp::writeToMailingList);

	connect(actionSave, &QAction::triggered, this, &TeXDocumentWindow::save);
	connect(actionSave_As, &QAction::triggered, this, &TeXDocumentWindow::saveAs);
	connect(actionSave_All, &QAction::triggered, this, &TeXDocumentWindow::saveAll);
	connect(actionRevert_to_Saved, &QAction::triggered, this, &TeXDocumentWindow::revert);
	connect(actionClose, &QAction::triggered, this, &TeXDocumentWindow::close);

	connect(actionRemove_Aux_Files, &QAction::triggered, this, &TeXDocumentWindow::removeAuxFiles);

	connect(actionQuit_TeXworks, &QAction::triggered, TWApp::instance(), &TWApp::maybeQuit);

	connect(actionClear, &QAction::triggered, this, &TeXDocumentWindow::clear);

	connect(actionFont, &QAction::triggered, this, &TeXDocumentWindow::doFontDialog);
	connect(actionGo_to_Line, &QAction::triggered, this, &TeXDocumentWindow::doLineDialog);
	connect(actionFind, &QAction::triggered, this, &TeXDocumentWindow::doFindDialog);
	connect(actionFind_Again, &QAction::triggered, this, &TeXDocumentWindow::doFindAgain);
	connect(actionReplace, &QAction::triggered, this, &TeXDocumentWindow::doReplaceDialog);
	connect(actionReplace_Again, &QAction::triggered, this, &TeXDocumentWindow::doReplaceAgain);

	connect(actionCopy_to_Find, &QAction::triggered, this, &TeXDocumentWindow::copyToFind);
	connect(actionCopy_to_Replace, &QAction::triggered, this, &TeXDocumentWindow::copyToReplace);
	connect(actionFind_Selection, &QAction::triggered, this, &TeXDocumentWindow::findSelection);

	connect(actionShow_Selection, &QAction::triggered, this, &TeXDocumentWindow::showSelection);

	connect(actionIndent, &QAction::triggered, this, &TeXDocumentWindow::doIndent);
	connect(actionUnindent, &QAction::triggered, this, &TeXDocumentWindow::doUnindent);

	connect(actionComment, &QAction::triggered, this, &TeXDocumentWindow::doComment);
	connect(actionUncomment, &QAction::triggered, this, &TeXDocumentWindow::doUncomment);
	const QList<QPair<QString, QString>> lineCommands{
		{QStringLiteral("上移行"), QStringLiteral("Alt+Up")},
		{QStringLiteral("下移行"), QStringLiteral("Alt+Down")},
		{QStringLiteral("复制行"), QStringLiteral("Ctrl+Shift+D")},
		{QStringLiteral("删除行"), QStringLiteral("Ctrl+Shift+K")},
		{QStringLiteral("注释或取消注释"), QStringLiteral("Ctrl+/")}};
	const QList<int> operations{-1, 1, 2, 3, 4};
	for (int i = 0; i < lineCommands.size(); ++i) {
		auto *action = new QAction(Research::ui(lineCommands[i].first), this);
		action->setObjectName(QStringLiteral("researchLineEdit%1").arg(i));
		action->setShortcut(QKeySequence(lineCommands[i].second));
		action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
		textEdit->addAction(action); menuEdit->addAction(action);
		connect(action, &QAction::triggered, textEdit, [this, operation = operations[i]] { textEdit->editLines(operation); });
		// Do not let the shortcut map consume candidate-navigation keys during IME.
		connect(textEdit, &CompletingEdit::compositionChanged, action, &QAction::setDisabled);
	}

	connect(actionHard_Wrap, &QAction::triggered, this, &TeXDocumentWindow::doHardWrapDialog);
	connect(actionInsert_Citations, &QAction::triggered, this, &TeXDocumentWindow::doInsertCitationsDialog);

	connect(actionTo_Uppercase, &QAction::triggered, this, &TeXDocumentWindow::toUppercase);
	connect(actionTo_Lowercase, &QAction::triggered, this, &TeXDocumentWindow::toLowercase);
	connect(actionToggle_Case, &QAction::triggered, this, &TeXDocumentWindow::toggleCase);

	connect(actionBalance_Delimiters, &QAction::triggered, this, &TeXDocumentWindow::balanceDelimiters);

	connect(textDoc(), &Tw::Document::TeXDocument::modificationChanged, this, &TeXDocumentWindow::setWindowModified);
	connect(textDoc(), &Tw::Document::TeXDocument::modificationChanged, this, &TeXDocumentWindow::maybeEnableSaveAndRevert);
	connect(textDoc(), &Tw::Document::TeXDocument::modelinesChanged, this, &TeXDocumentWindow::handleModelineChange);
	connect(textEdit, &CompletingEdit::cursorPositionChanged, this, &TeXDocumentWindow::showCursorPosition);
	connect(textEdit, &CompletingEdit::selectionChanged, this, &TeXDocumentWindow::showCursorPosition);
	connect(textEdit->document(), &QTextDocument::blockCountChanged, this, &TeXDocumentWindow::showCursorPosition);
	connect(textEdit, &CompletingEdit::syncClick, this, &TeXDocumentWindow::syncClick);
	connect(this, &TeXDocumentWindow::syncFromSource, TWApp::instance(), &TWApp::syncPdf);

	connect(QApplication::clipboard(), &QClipboard::dataChanged, this, &TeXDocumentWindow::clipboardChanged);
	clipboardChanged();

	connect(actionTypeset, &QAction::triggered, this, &TeXDocumentWindow::typeset);

	updateRecentFileActions();
	connect(TWApp::instance(), &TWApp::recentFileActionsChanged, this, &TeXDocumentWindow::updateRecentFileActions);
	connect(TWApp::instance(), &TWApp::windowListChanged, this, &TeXDocumentWindow::updateWindowMenu);
	connect(actionClear_Recent_Files, &QAction::triggered, TWApp::instance(), &TWApp::clearRecentFiles);

	connect(TWApp::instance(), &TWApp::hideFloatersExcept, this, &TeXDocumentWindow::hideFloatersUnlessThis);
	connect(this, &TeXDocumentWindow::activatedWindow, TWApp::instance(), &TWApp::activatedWindow);

	connect(&(TWApp::instance()->typesetManager()), &Tw::Utils::TypesetManager::typesettingStarted, this, &TeXDocumentWindow::updateTypesettingAction);
	connect(&(TWApp::instance()->typesetManager()), &Tw::Utils::TypesetManager::typesettingStopped, this, &TeXDocumentWindow::updateTypesettingAction);
	connect(&(TWApp::instance()->typesetManager()), &Tw::Utils::TypesetManager::typesettingStarted, this, &TeXDocumentWindow::conditionallyEnableRemoveAuxFiles);
	connect(&(TWApp::instance()->typesetManager()), &Tw::Utils::TypesetManager::typesettingStopped, this, &TeXDocumentWindow::conditionallyEnableRemoveAuxFiles);

	connect(actionStack, &QAction::triggered, TWApp::instance(), &TWApp::stackWindows);
	connect(actionTile, &QAction::triggered, TWApp::instance(), &TWApp::tileWindows);
	connect(actionSide_by_Side, &QAction::triggered, this, &TeXDocumentWindow::sideBySide);
	connect(actionPlace_on_Left, &QAction::triggered, this, &TeXDocumentWindow::placeOnLeft);
	connect(actionPlace_on_Right, &QAction::triggered, this, &TeXDocumentWindow::placeOnRight);
	connect(actionShow_Hide_Console, &QAction::triggered, this, &TeXDocumentWindow::toggleConsoleVisibility);
	connect(actionGo_to_Preview, &QAction::triggered, this, &TeXDocumentWindow::goToPreview);

	connect(this, &TeXDocumentWindow::destroyed, TWApp::instance(), &TWApp::updateWindowMenus);

	connect(actionPreferences, &QAction::triggered, TWApp::instance(), &TWApp::preferences);

	connect(menuEdit, &QMenu::aboutToShow, this, &TeXDocumentWindow::editMenuAboutToShow);

#if QT_VERSION < QT_VERSION_CHECK(5, 4, 0)
	// Work around QTBUG-36281
	textEdit->installEventFilter(Tw::Utils::CmdKeyFilter::filter());
#endif

	connect(inputLine, &QLineEdit::returnPressed, this, &TeXDocumentWindow::acceptInputLine);

	Tw::Settings settings;
	TWUtils::applyToolbarOptions(this, settings.value(QString::fromLatin1("toolBarIconSize"), 2).toInt(), settings.value(QString::fromLatin1("toolBarShowText"), false).toBool());

	QFont font = textEdit->font();
	if (settings.contains(QString::fromLatin1("font"))) {
		QString fontString = settings.value(QString::fromLatin1("font")).toString();
		if (!fontString.isEmpty()) {
			font.fromString(fontString);
			textEdit->setFont(font);
		}
	}
	font.setPointSize(font.pointSize() - 1);
	inputLine->setFont(font);
	inputLine->setLayoutDirection(Qt::LeftToRight);
	textEdit_console->setFont(font);
	textEdit_console->setLayoutDirection(Qt::LeftToRight);

	setLineSpacing(settings.value(QStringLiteral("lineSpacing"), kDefault_LineSpacing).toReal());

	bool b = settings.value(QString::fromLatin1("wrapLines"), true).toBool();
	actionWrap_Lines->setChecked(b);
	setWrapLines(b);

	b = settings.value(QString::fromLatin1("lineNumbers"), false).toBool();
	actionLine_Numbers->setChecked(b);
	setLineNumbers(b);

	actionAuto_Follow_Focus->setChecked(settings.value(QStringLiteral("autoFollowFocusEnabled"), kDefault_AutoFollowFocusEnabled).toBool());

	QStringList options = TeXHighlighter::syntaxOptions();

	QSignalMapper *syntaxMapper = new QSignalMapper(this);
#if QT_VERSION < QT_VERSION_CHECK(5, 15, 0)
	connect(syntaxMapper, static_cast<void (QSignalMapper::*)(int)>(&QSignalMapper::mapped), this, &TeXDocumentWindow::setSyntaxColoring);
#else
	connect(syntaxMapper, &QSignalMapper::mappedInt, this, &TeXDocumentWindow::setSyntaxColoring);
#endif
	syntaxMapper->setMapping(actionSyntaxColoring_None, -1);
	connect(actionSyntaxColoring_None, &QAction::triggered, syntaxMapper, static_cast<void (QSignalMapper::*)()>(&QSignalMapper::map));

	QActionGroup *syntaxGroup = new QActionGroup(this);
	syntaxGroup->addAction(actionSyntaxColoring_None);

	int index = 0;
	foreach (const QString& opt, options) {
		QAction *action = menuSyntax_Coloring->addAction(opt, syntaxMapper, SLOT(map()));
		action->setCheckable(true);
		syntaxGroup->addAction(action);
		syntaxMapper->setMapping(action, index);
		++index;
	}

	// kDefault_TabWidth is defined in DefaultPrefs.h
#if QT_VERSION < QT_VERSION_CHECK(5, 10, 0)
	textEdit->setTabStopWidth(settings.value(QString::fromLatin1("tabWidth"), kDefault_TabWidth).toInt());
	textEdit_console->setTabStopWidth(settings.value(QString::fromLatin1("tabWidth"), kDefault_TabWidth).toInt());
#else
	textEdit->setTabStopDistance(settings.value(QString::fromLatin1("tabWidth"), kDefault_TabWidth).toReal());
	textEdit_console->setTabStopDistance(settings.value(QString::fromLatin1("tabWidth"), kDefault_TabWidth).toReal());
#endif

	// It is VITAL that this connection is queued! Calling showMessage directly
	// from TeXDocument::contentsChanged would otherwise result in a seg fault
	// (for whatever reason)
	connect(this, &TeXDocumentWindow::asyncFlashStatusBarMessage, statusBar(), &QStatusBar::showMessage, Qt::QueuedConnection);

	QString indentOption = settings.value(QString::fromLatin1("autoIndent")).toString();
	options = CompletingEdit::autoIndentModes();

	QSignalMapper *indentMapper = new QSignalMapper(this);
#if QT_VERSION < QT_VERSION_CHECK(5, 15, 0)
	connect(indentMapper, static_cast<void (QSignalMapper::*)(int)>(&QSignalMapper::mapped), textEdit, &CompletingEdit::setAutoIndentMode);
#else
	connect(indentMapper, &QSignalMapper::mappedInt, textEdit, &CompletingEdit::setAutoIndentMode);
#endif
	indentMapper->setMapping(actionAutoIndent_None, -1);
	connect(actionAutoIndent_None, &QAction::triggered, indentMapper, static_cast<void (QSignalMapper::*)()>(&QSignalMapper::map));

	QActionGroup *indentGroup = new QActionGroup(this);
	indentGroup->addAction(actionAutoIndent_None);

	index = 0;
	foreach (const QString& opt, options) {
		QAction *action = menuAuto_indent_Mode->addAction(opt, indentMapper, SLOT(map()));
		action->setCheckable(true);
		indentGroup->addAction(action);
		indentMapper->setMapping(action, index);
		if (opt == indentOption) {
			action->setChecked(true);
			textEdit->setAutoIndentMode(index);
		}
		++index;
	}

	QString quotesOption = settings.value(QString::fromLatin1("smartQuotes")).toString();
	options = CompletingEdit::smartQuotesModes();

	QSignalMapper *quotesMapper = new QSignalMapper(this);
#if QT_VERSION < QT_VERSION_CHECK(5, 15, 0)
	connect(quotesMapper, static_cast<void (QSignalMapper::*)(int)>(&QSignalMapper::mapped), textEdit, &CompletingEdit::setSmartQuotesMode);
#else
	connect(quotesMapper, &QSignalMapper::mappedInt, textEdit, &CompletingEdit::setSmartQuotesMode);
#endif
	quotesMapper->setMapping(actionSmartQuotes_None, -1);
	connect(actionSmartQuotes_None, &QAction::triggered, quotesMapper, static_cast<void (QSignalMapper::*)()>(&QSignalMapper::map));

	QActionGroup *quotesGroup = new QActionGroup(this);
	quotesGroup->addAction(actionSmartQuotes_None);

	menuSmart_Quotes_Mode->removeAction(actionApply_to_Selection);
	index = 0;
	foreach (const QString& opt, options) {
		QAction *action = menuSmart_Quotes_Mode->addAction(opt, quotesMapper, SLOT(map()));
		action->setCheckable(true);
		quotesGroup->addAction(action);
		quotesMapper->setMapping(action, index);
		if (opt == quotesOption) {
			action->setChecked(true);
			textEdit->setSmartQuotesMode(index);
		}
		++index;
	}
	if (!options.empty())
		menuSmart_Quotes_Mode->addSeparator();
	menuSmart_Quotes_Mode->addAction(actionApply_to_Selection);
	connect(actionApply_to_Selection, &QAction::triggered, textEdit, &CompletingEdit::smartenQuotes);

	connect(actionLine_Numbers, &QAction::triggered, this, &TeXDocumentWindow::setLineNumbers);
	connect(actionWrap_Lines, &QAction::triggered, this, &TeXDocumentWindow::setWrapLines);

	connect(actionNone, &QAction::triggered, &dictSignalMapper, static_cast<void (QSignalMapper::*)()>(&QSignalMapper::map));
	dictSignalMapper.setMapping(actionNone, QString());
#if QT_VERSION < QT_VERSION_CHECK(5, 15, 0)
	connect(&dictSignalMapper, static_cast<void (QSignalMapper::*)(const QString&)>(&QSignalMapper::mapped), this, &TeXDocumentWindow::setLangInternal);
#else
	connect(&dictSignalMapper, &QSignalMapper::mappedString, this, &TeXDocumentWindow::setLangInternal);
#endif

	QActionGroup *group = new QActionGroup(this);
	group->addAction(actionNone);

	reloadSpellcheckerMenu();
	connect(Tw::Document::SpellCheckManager::instance(), &Tw::Document::SpellCheckManager::dictionaryListChanged, this, &TeXDocumentWindow::reloadSpellcheckerMenu);

	menuShow->addAction(toolBar_run->toggleViewAction());

	menuShow->addSeparator();

#ifdef Q_OS_WIN
	// loadFile restores saved placement or the paper half-screen geometry.
	// Do not create a temporary maximized native surface before that decision.
	if (!openingFile) setWindowState(windowState() | Qt::WindowMaximized);
#else
	Q_UNUSED(openingFile)
	setWindowState(windowState() | Qt::WindowMaximized);
#endif

	QDockWidget *dw = new TagsDock(this);
	dw->hide();
	addDockWidget(Qt::LeftDockWidgetArea, dw);
	menuShow->addAction(dw->toggleViewAction());

	watcher = new QFileSystemWatcher(this);
	connect(watcher, &QFileSystemWatcher::fileChanged, this, &TeXDocumentWindow::reloadIfChangedOnDisk, Qt::QueuedConnection);
	connect(watcher, &QFileSystemWatcher::directoryChanged, this, &TeXDocumentWindow::reloadIfChangedOnDisk, Qt::QueuedConnection);

	docList.append(this);

	TWApp::instance()->updateWindowMenus();

	initScriptable(menuScripts, actionAbout_Scripts, actionManage_Scripts,
				   actionUpdate_Scripts, actionShow_Scripts_Folder);

	TWUtils::insertHelpMenuItems(menuHelp);
	TWUtils::installCustomShortcuts(this);
	// Add familiar Windows aliases only when no existing command owns them.
	for(const auto &alias:QList<QPair<QAction *,QKeySequence>>{{actionReplace,QKeySequence(QStringLiteral("Ctrl+H"))},{actionRedo,QKeySequence(QStringLiteral("Ctrl+Y"))}}){
		bool occupied=false;for(auto *other:findChildren<QAction *>())if(other!=alias.first)for(const auto &key:other->shortcuts())if(key.matches(alias.second)!=QKeySequence::NoMatch)occupied=true;
		for(auto *other:findChildren<QShortcut *>())if(other->key().matches(alias.second)!=QKeySequence::NoMatch)occupied=true;
		if(!occupied&&!alias.first->shortcuts().contains(alias.second)){auto keys=alias.first->shortcuts();keys.append(alias.second);alias.first->setShortcuts(keys);}
	}
	delayedInit();
	setupResearchWorkspace();
	if (!openingFile) Research::installWritingStart(this);
	// Refresh only when the empty/untitled boundary changes, not on every edit.
	auto refreshEmptyCompile = [this] {
		const bool empty = untitled() && textDoc()->isEmpty();
		if (property("researchEmptyCompile").isValid() && property("researchEmptyCompile").toBool() == empty) return;
		setProperty("researchEmptyCompile", empty);
		updateTypesettingAction();
	};
	connect(textDoc(), &QTextDocument::contentsChanged, this, refreshEmptyCompile);
	connect(TWApp::instance(), &TWApp::windowListChanged, this, refreshEmptyCompile);
	refreshEmptyCompile();
	researchCollaboration = new Research::Collaboration(this);
	Research::installBuildOutput(this);
	// Respect custom bindings: retain the menu command but disable ambiguous keys.
	for (int i = 0; i < 5; ++i) {
		auto *action = findChild<QAction *>(QStringLiteral("researchLineEdit%1").arg(i));
		for (auto *other : findChildren<QAction *>()) {
			bool conflict = false;
			for (const auto &key : other->shortcuts()) if (!key.isEmpty() && !action->shortcut().isEmpty() &&
				(action->shortcut().matches(key) != QKeySequence::NoMatch || key.matches(action->shortcut()) != QKeySequence::NoMatch)) conflict = true;
			if (other != action && conflict) {
				action->setProperty("researchShortcutConflict", other->objectName());
				action->setShortcut(QKeySequence()); break;
			}
		}
		for (auto *other : findChildren<QShortcut *>()) if (!action->shortcut().isEmpty() && !other->key().isEmpty() &&
			(action->shortcut().matches(other->key()) != QKeySequence::NoMatch || other->key().matches(action->shortcut()) != QKeySequence::NoMatch)) {
			action->setProperty("researchShortcutConflict", other->objectName()); action->setShortcut(QKeySequence()); break;
		}
	}
}

void TeXDocumentWindow::changeEvent(QEvent *event)
{
	if (event->type() == QEvent::LanguageChange) {
		QString title = windowTitle();
		retranslateUi(this);
		TWUtils::insertHelpMenuItems(menuHelp);
		setWindowTitle(title);
		showCursorPosition();
		Research::retranslateWorkspace(this);
		updateTypesettingAction();
	}
	else if (event->type() == QEvent::ActivationChange) {
		// If this window was activated, inform the linked pdf (if any) of it
		// so that future "Goto Source" actions point here.
		if (this == QApplication::activeWindow() && pdfDoc)
			pdfDoc->texActivated(this);
	}
	QMainWindow::changeEvent(event);
}

void TeXDocumentWindow::setLangInternal(const QString& lang)
{
	if (_texDoc == nullptr) {
		return;
	}

	TeXHighlighter * highlighter = _texDoc->getHighlighter();
	if (highlighter == nullptr) {
		return;
	}

	highlighter->setSpellChecker(Tw::Document::SpellChecker(lang));
}

void TeXDocumentWindow::setSpellcheckLanguage(const QString& lang)
{
	// this is called by the %!TEX spellcheck... line, or by scripts;
	// it searches the menu for the given language code, and triggers it if available

	// Determine all aliases for the specified lang
	QList<QString> langAliases;
	foreach (const QString& dictKey, Tw::Document::SpellCheckManager::getDictionaryList()->uniqueKeys()) {
		if(Tw::Document::SpellCheckManager::getDictionaryList()->values(dictKey).contains(lang))
			langAliases += Tw::Document::SpellCheckManager::getDictionaryList()->values(dictKey);
	}
	langAliases.removeAll(lang);
	langAliases.prepend(lang);

	if (menuSpelling) {
		QAction *chosen = menuSpelling->actions()[0]; // default is None
		foreach(QString alias, langAliases) {
			bool found = false;
			foreach (QAction *act, menuSpelling->actions()) {
				if (act->text() == alias || act->text().contains(QString::fromLatin1("(%1)").arg(alias))) {
					chosen = act;
					found = true;
					break;
				}
			}
			if(found) break;
		}
		chosen->trigger();
	}
}

QString TeXDocumentWindow::spellcheckLanguage() const
{
	if (_texDoc == nullptr) {
		return {};
	}
	TeXHighlighter * highlighter = _texDoc->getHighlighter();
	if (highlighter == nullptr) {
		return {};
	}
	return highlighter->getSpellChecker().languages().join(QStringLiteral("\n"));
}

void TeXDocumentWindow::reloadSpellcheckerMenu()
{
	Q_ASSERT(menuSpelling);
	Q_ASSERT(!menuSpelling->actions().empty());

	QActionGroup * group = menuSpelling->actions()[0]->actionGroup();
	Q_ASSERT(group);

	// Remove all but the first menu item ("None") from the action group
	int i = 0;
	QString oldSelected;
	foreach (QAction * act, group->actions()) {
		if (act->isChecked())
			oldSelected = act->text();
		if (i > 0) {
			group->removeAction(act);
			act->deleteLater();
		}
		++i;
	}

	QList<QAction*> dictActions;
	foreach (const QString& dictKey, Tw::Document::SpellCheckManager::getDictionaryList()->uniqueKeys()) {
		foreach (QString dict, Tw::Document::SpellCheckManager::getDictionaryList()->values(dictKey)) {
			const QString label{Tw::Document::SpellCheckManager::labelForDict(dict)};

			QAction * act = new QAction(label, menuSpelling);
			act->setCheckable(true);
			if (!oldSelected.isEmpty() && label == oldSelected)
				act->setChecked(true);
			connect(act, &QAction::triggered, &dictSignalMapper, static_cast<void (QSignalMapper::*)()>(&QSignalMapper::map));
			dictSignalMapper.setMapping(act, dict);
			group->addAction(act);
			dictActions << act;
		}
	}
	std::sort(dictActions.begin(), dictActions.end(), dictActionLessThan);
	foreach (QAction* dictAction, dictActions)
		menuSpelling->addAction(dictAction);
}

void TeXDocumentWindow::clipboardChanged()
{
#ifdef Q_OS_WIN
	// Plain text, file/URL lists and images enter through the existing editor
	// insertion path. Keep delayed OLE data out of this availability check.
	// Qt's OLE format query may synchronously contact another application's
	// delayed clipboard owner. Check the corresponding Windows formats without
	// fetching data; actual paste still uses the normal Qt conversion.
	static const UINT url = RegisterClipboardFormatW(L"UniformResourceLocator");
	static const UINT unicodeUrl = RegisterClipboardFormatW(L"UniformResourceLocatorW");
	static const UINT png = RegisterClipboardFormatW(L"PNG");
	const bool available = IsClipboardFormatAvailable(CF_UNICODETEXT) || IsClipboardFormatAvailable(CF_TEXT) ||
		IsClipboardFormatAvailable(CF_HDROP) || IsClipboardFormatAvailable(url) || IsClipboardFormatAvailable(unicodeUrl) ||
		IsClipboardFormatAvailable(CF_DIB) || IsClipboardFormatAvailable(CF_DIBV5) || IsClipboardFormatAvailable(CF_BITMAP) || IsClipboardFormatAvailable(png);
	actionPaste->setEnabled(!textEdit->isReadOnly() && available);
#else
	actionPaste->setEnabled(textEdit->canPaste());
#endif
}

void TeXDocumentWindow::editMenuAboutToShow()
{
//	undoAction->setText(tr("Undo ") + undoStack->undoText());
//	redoAction->setText(tr("Redo ") + undoStack->redoText());
	actionSelect_All->setEnabled(!textEdit->document()->isEmpty());
}

void TeXDocumentWindow::newFile()
{
	TeXDocumentWindow *doc = new TeXDocumentWindow;
	doc->selectWindow();
	doc->textEdit->updateLineNumberAreaWidth(0);
	doc->runHooks(QString::fromLatin1("NewFile"));
}

void TeXDocumentWindow::newFromTemplate()
{
	bool templateProject=false;QString templateName = TemplateDialog::doTemplateDialog(&templateProject, this);
	if (!templateName.isEmpty()) {
		TeXDocumentWindow *doc = nullptr;
		if (untitled() && textEdit->document()->isEmpty() && !isWindowModified()) {
			loadFile(QFileInfo(templateName), !templateProject);
			doc = this;
		}
		else {
			doc = new TeXDocumentWindow(templateName, !templateProject);
		}
		if (doc) {
			if(!templateProject)doc->makeUntitled();
			doc->selectWindow();
			doc->textEdit->updateLineNumberAreaWidth(0);
			doc->runHooks(QString::fromLatin1("NewFromTemplate"));
		}
	}
}

void TeXDocumentWindow::makeUntitled()
{
	setCurrentFile({});
	conditionallyEnableRemoveAuxFiles();
}

void TeXDocumentWindow::open()
{
	QFileDialog::Options options = QFileDialog::DontResolveSymlinks;
#if defined(Q_OS_WIN)
	if(TWApp::GetWindowsVersion() < 0x06000000) options |= QFileDialog::DontUseNativeDialog;
#endif
	Tw::Settings settings;
	QString lastOpenDir = settings.value(QString::fromLatin1("openDialogDir")).toString();
	if (lastOpenDir.isEmpty())
		lastOpenDir = QDir::homePath();
	QStringList files = QFileDialog::getOpenFileNames(this, QString(tr("Open File")), lastOpenDir, TWUtils::filterList()->join(QLatin1String(";;")), nullptr, options);
	foreach (QString fileName, files) {
		if (!fileName.isEmpty()) {
			TWApp::instance()->openFile(fileName); // not TeXDocument::open() - give the app a chance to open as PDF
		}
	}
}

TeXDocumentWindow* TeXDocumentWindow::open(const QString &fileName)
{
	TeXDocumentWindow *doc = nullptr;
	if (!fileName.isEmpty()) {
		doc = findDocument(fileName);
		if (!doc) {
			if (untitled() && textEdit->document()->isEmpty() && !isWindowModified()) {
				loadFile(QFileInfo(fileName));
				doc = this;
			}
			else {
				doc = new TeXDocumentWindow(fileName);
				if (doc->untitled()) {
					delete doc;
					doc = nullptr;
				}
			}
		}
	}
	if (doc)
		doc->selectWindow();
	return doc;
}

TeXDocumentWindow* TeXDocumentWindow::openDocument(const QString &fileName, bool activate, bool raiseWindow, int lineNo, int selStart, int selEnd) // static
{
    if(activate&&lineNo>0){if(auto *source=qobject_cast<TeXDocumentWindow*>(QApplication::activeWindow()))Research::rememberReadingPlace(source);else if(auto *pdf=qobject_cast<PDFDocumentWindow*>(QApplication::activeWindow()))Research::rememberReadingPlace(pdf);}
	TeXDocumentWindow *doc = findDocument(fileName);
	if (!doc) {
		if (docList.count() == 1) {
			doc = docList[0];
			doc = doc->open(fileName); // open into existing window if untitled/empty
		}
		else {
			doc = new TeXDocumentWindow(fileName);
			if (doc->untitled()) {
				delete doc;
				doc = nullptr;
			}
		}
	}
	if (doc) {
		if(activate&&doc->property("researchBackgroundSource").toBool()){doc->setProperty("researchBackgroundSource",false);if(doc->property("researchRecoveryBlocked").toBool())Research::startRecovery(doc);}
		if (activate)
			doc->selectWindow();
		else {
			doc->show();
			if (raiseWindow) {
				doc->raise();
				if (doc->isMinimized())
					doc->showNormal();
			}
		}
		if (lineNo > 0) {
			Research::cancelRecentWritingPlace(doc);
			doc->goToLine(lineNo, selStart, selEnd);
		}
	}
	return doc;
}

void TeXDocumentWindow::closeEvent(QCloseEvent *event)
{
	if (!Research::commitSlideNotes(this)) { event->ignore(); return; }
	flushScriptEdits();
	if (process) {
		if (QMessageBox::question(this, tr("Abort typesetting?"), tr("A typesetting process is still running and must be stopped before closing this window.\nDo you want to stop it now?"), QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) == QMessageBox::No) {
			event->ignore();
			return;
		}
		interrupt();
		// Wait for the process to actually finish (and be destroyed) as this
		// might try to access, e.g., the log window (which could be destroyed
		// at any time once the close event goes through and this
		// TeXDocumentWindow is started to be destroyed)
		if (process) {
			process->waitForFinished();
		}
	}

	if (maybeSave()) {
		if(researchBuildPreparing)interrupt();
		++researchBuildSerial; researchRetryRequested = false;
		Research::checkpoint(this, true);
		event->accept();
		saveRecentFileInfo(true);
		deleteLater();
	}
	else
		event->ignore();
}

bool TeXDocumentWindow::event(QEvent *event) // based on example at doc.trolltech.com/qq/qq18-macfeatures.html
{
	switch (event->type()) {
		case QEvent::IconDrag:
			if (isActiveWindow()) {
				event->accept();
				Qt::KeyboardModifiers mods = qApp->keyboardModifiers();
				if (mods == Qt::NoModifier) {
					QDrag *drag = new QDrag(this);
					QMimeData *data = new QMimeData();
					data->setUrls(QList<QUrl>() << QUrl::fromLocalFile(textDoc()->absoluteFilePath()));
					drag->setMimeData(data);
					QPixmap dragIcon(QString::fromLatin1(":/images/images/TeXworks-doc-48.png"));
					drag->setPixmap(dragIcon);
					drag->setHotSpot(QPoint(dragIcon.width() - 5, 5));
					drag->exec(Qt::LinkAction | Qt::CopyAction);
				}
				else if (mods == Qt::ShiftModifier) {
					QMenu menu(this);
					connect(&menu, &QMenu::triggered, this, &TeXDocumentWindow::openAt);
					QFileInfo info(textDoc()->getFileInfo());
					QAction *action = menu.addAction(info.fileName());
					action->setIcon(QIcon(QString::fromLatin1(":/images/images/TeXworks-doc.png")));
					QStringList folders = info.absolutePath().split(QChar::fromLatin1('/'));
					QStringListIterator it(folders);
					it.toBack();
					while (it.hasPrevious()) {
						QString str = it.previous();
						QIcon icon;
						if (!str.isEmpty()) {
							icon = style()->standardIcon(QStyle::SP_DirClosedIcon, nullptr, this);
						}
						else {
							str = QChar::fromLatin1('/');
							icon = style()->standardIcon(QStyle::SP_DriveHDIcon, nullptr, this);
						}
						action = menu.addAction(str);
						action->setIcon(icon);
#if defined(Q_OS_DARWIN)
						action->setIconVisibleInMenu(true);
#endif
					}
					QPoint pos(QCursor::pos().x() - 20, frameGeometry().y());
					menu.exec(pos);
				}
				else {
					event->ignore();
				}
				return true;
			}
			break;

		case QEvent::WindowActivate:
			showFloaters();
			emit activatedWindow(this);
			break;

		default:
			break;
	}
	return QMainWindow::event(event);
}

void TeXDocumentWindow::openAt(QAction *action)
{
	QString curFile = textDoc()->getFileInfo().filePath();
	QString path = curFile.left(curFile.indexOf(action->text())) + action->text();
	if (path == curFile)
		return;
	QProcess proc;
	proc.start(QString::fromLatin1("/usr/bin/open"), QStringList() << path, QIODevice::ReadOnly);
	proc.waitForFinished();
}

bool TeXDocumentWindow::save()
{
	if (!Research::commitSlideNotes(this)) return false;
	flushScriptEdits();
	if (untitled())
		return saveAs();
	return saveFile(QFileInfo(textDoc()->absoluteFilePath()));
}

bool TeXDocumentWindow::saveAll()
{
	bool savedAll = true;
	// Notes are an editor draft too, even before they change the source buffer.
	foreach (TeXDocumentWindow* doc, docList) {
		if (!Research::commitSlideNotes(doc)) return false;
	}
	foreach (TeXDocumentWindow* doc, docList) {
		doc->flushScriptEdits();
		if (doc->textEdit->document()->isModified()) {
			if (!doc->save()) {
				savedAll = false;
			}
		}
	}
	return savedAll;
}

bool TeXDocumentWindow::saveAs()
{
	if (!Research::commitSlideNotes(this)) return false;
	flushScriptEdits();
	QFileDialog::Options options;
#if defined(Q_OS_WIN)
	if(TWApp::GetWindowsVersion() < 0x06000000) options |= QFileDialog::DontUseNativeDialog;
#endif
	QString selectedFilter = TWUtils::chooseDefaultFilter(textDoc()->absoluteFilePath(), *(TWUtils::filterList()));;

	// for untitled docs, default to the last dir used, or $HOME if no saved value
	Tw::Settings settings;
	QString lastSaveDir = settings.value(QString::fromLatin1("saveDialogDir")).toString();
	if (lastSaveDir.isEmpty() || !QDir(lastSaveDir).exists())
		lastSaveDir = QDir::homePath();
	QString suggestedDir = (textDoc()->isStoredInFilesystem() ? textDoc()->absoluteFilePath() : QDir(lastSaveDir).filePath(textDoc()->getFileInfo().filePath()));
	QString fileName = QFileDialog::getSaveFileName(this, tr("Save File"),
													suggestedDir,
	                                                TWUtils::filterList()->join(QLatin1String(";;")),
													&selectedFilter, options);
	if (fileName.isEmpty())
		return false;

	// save the old document in "Recent Files"
	saveRecentFileInfo(true);

	// add extension from the selected filter, if unique and not already present
	QRegularExpression re(QStringLiteral("\\(\\*(\\.[^ *]+)\\)"));
	QRegularExpressionMatch m = re.match(selectedFilter);
	if (m.capturedStart() >= 0) {
		QString ext = m.captured(1);
		if (!fileName.endsWith(ext, Qt::CaseInsensitive) && !fileName.endsWith(QChar::fromLatin1('.')))
			fileName.append(ext);
	}

	QFileInfo info(fileName);
	if (info != textDoc()->getFileInfo() && pdfDoc) {
		// For the pdf, it is as if it's source doc was closed
		// Note that this may result in the pdf being closed!
		pdfDoc->texClosed(this);
		// The pdf connection is no longer (necessarily) valid. Detach it for
		// now (the correct connection will be reestablished on next typeset).
		detachPdf();
	}

	settings.setValue(QString::fromLatin1("saveDialogDir"), info.absolutePath());

	return saveFile(QFileInfo(fileName));
}

bool TeXDocumentWindow::maybeSave()
{
	if (textEdit->document()->isModified()) {
		QMessageBox msgBox(QMessageBox::Warning, QApplication::applicationDisplayName(),
						   tr("The document \"%1\" has been modified.\n"
							  "Do you want to save your changes?")
						   .arg(textDoc()->getFileInfo().fileName()),
						   QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
						   this);
		msgBox.button(QMessageBox::Discard)->setShortcut(QKeySequence(tr("Ctrl+D", "shortcut: Don't Save")));
		msgBox.button(QMessageBox::Discard)->setText(Research::englishUi() ? QStringLiteral("Ignore") : Research::ui(QStringLiteral("忽略")));
		msgBox.setWindowModality(Qt::WindowModal);
		QMessageBox::StandardButton ret = static_cast<QMessageBox::StandardButton>(msgBox.exec());
		if (ret == QMessageBox::Save)
			return save();
		if (ret == QMessageBox::Cancel)
			return false;
	}
	return true;
}

bool TeXDocumentWindow::saveFilesHavingRoot(const QString& aRootFile)
{
	foreach (TeXDocumentWindow* doc, docList) {
		if (doc->textDoc() && doc->textDoc()->getRootFilePath() == aRootFile && !Research::commitSlideNotes(doc))
			return false;
	}
	foreach (TeXDocumentWindow* doc, docList) {
		if (doc->textDoc() && doc->textDoc()->getRootFilePath() == aRootFile) {
			if (doc->textEdit->document()->isModified() && !doc->save())
				return false;
		}
	}
	return true;
}

void TeXDocumentWindow::revert()
{
	if (!untitled()) {
		QMessageBox messageBox(QMessageBox::Warning, QApplication::applicationDisplayName(),
					tr("Do you want to discard all changes to the document \"%1\", and revert to the last saved version?")
					   .arg(textDoc()->getFileInfo().fileName()), QMessageBox::Cancel, this);
		QAbstractButton *revertButton = messageBox.addButton(tr("Revert"), QMessageBox::DestructiveRole);
		revertButton->setShortcut(QKeySequence(tr("Ctrl+R", "shortcut: Revert")));
		messageBox.setDefaultButton(QMessageBox::Cancel);
		messageBox.setWindowModality(Qt::WindowModal);
		messageBox.exec();
		if (messageBox.clickedButton() == revertButton)
			loadFile(QFileInfo(textDoc()->absoluteFilePath()));
	}
}

void TeXDocumentWindow::maybeEnableSaveAndRevert(bool modified)
{
	actionSave->setEnabled(modified || untitled() || Research::hasPendingSlideNotes(this));
	actionRevert_to_Saved->setEnabled(modified && !untitled());
}

static const char* texshopSynonyms[] = {
	"MacOSRoman",		"Apple Roman",
	"IsoLatin",			"ISO 8859-1",
	"IsoLatin2",		"ISO 8859-2",
	"IsoLatin5",		"ISO 8859-5",
	"IsoLatin9",		"ISO 8859-9",
//	"MacJapanese",		"",
//	"DOSJapanese",		"",
	"SJIS_X0213",		"Shift-JIS",
	"EUC_JP",			"EUC-JP",
//	"JISJapanese",		"",
//	"MacKorean",		"",
	"UTF-8 Unicode",	"UTF-8",
	"Standard Unicode",	"UTF-16",
//	"Mac Cyrillic",		"",
//	"DOS Cyrillic",		"",
//	"DOS Russian",		"",
	"Windows Cyrillic",	"Windows-1251",
	"KOI8_R",			"KOI8-R",
//	"Mac Chinese Traditional",	"",
//	"Mac Chinese Simplified",	"",
//	"DOS Chinese Traditional",	"",
//	"DOS Chinese Simplified",	"",
//	"GBK",				"",
//	"GB 2312",			"",
	"GB 18030",			"GB18030-0",
	nullptr
};

QTextCodec *TeXDocumentWindow::scanForEncoding(const QString &peekStr, bool &hasMetadata, QString &reqName)
{
	// peek at the file for %!TEX encoding = ....
	QRegularExpression re(QStringLiteral(u"% *!TEX +encoding *= *([^\r\n\x2029]+)[\r\n\x2029]"), QRegularExpression::CaseInsensitiveOption);
	QRegularExpressionMatch m = re.match(peekStr);
	QTextCodec *reqCodec = nullptr;
	if (m.hasMatch()) {
		hasMetadata = true;
		reqName = m.captured(1).trimmed();
		reqCodec = QTextCodec::codecForName(reqName.toLatin1());
		if (!reqCodec) {
			static QHash<QString,QString> *synonyms = nullptr;
			if (!synonyms) {
				synonyms = new QHash<QString,QString>;
				for (int i = 0; texshopSynonyms[i]; i += 2)
					synonyms->insert(QString::fromLatin1(texshopSynonyms[i]).toLower(), QString::fromLatin1(texshopSynonyms[i+1]));
			}
			if (synonyms->contains(reqName.toLower()))
				reqCodec = QTextCodec::codecForName(synonyms->value(reqName.toLower()).toLatin1());
		}
	}
	else
		hasMetadata = false;
	return reqCodec;
}

#define PEEK_LENGTH 1024

QString TeXDocumentWindow::readFile(const QFileInfo & fileInfo,
							  QTextCodec **codecUsed,
							  int *lineEndings,
							  QTextCodec * forceCodec)
	// reads the text from a file, after checking for %!TEX encoding.... metadata
	// sets codecUsed to the QTextCodec used to read the text
	// returns a null (not just empty) QString on failure
{
	if (lineEndings) {
		// initialize to default for the platform
#if defined(Q_OS_WIN)
		*lineEndings = kLineEnd_CRLF;
#else
		*lineEndings = kLineEnd_LF;
#endif
	}

	utf8BOM = false;
	QFile file(fileInfo.absoluteFilePath());
	// Not using QFile::Text because this prevents us reading "classic" Mac files
	// with CR-only line endings. See issue #242.
	if (!file.open(QFile::ReadOnly)) {
		// A failed open may be a still-hidden temporary document that is deleted
		// by openDocument. Keep its error owned by the visible initiating window.
		QWidget *messageParent = isVisible() ? this : QApplication::activeWindow();
		QMessageBox::warning(messageParent ? messageParent : this, QApplication::applicationDisplayName(),
							 tr("Cannot read file \"%1\":\n%2")
							 .arg(fileInfo.absoluteFilePath(), file.errorString()));
		return QString();
	}

	QByteArray peekBytes(file.peek(PEEK_LENGTH));

	QString reqName;
	if (forceCodec)
		*codecUsed = forceCodec;
	else {
		bool hasMetadata{false};
		*codecUsed = scanForEncoding(QString::fromUtf8(peekBytes.constData()), hasMetadata, reqName);
		if (!(*codecUsed)) {
			*codecUsed = TWApp::instance()->getDefaultCodec();
			if (hasMetadata) {
				if (QMessageBox::warning(this, tr("Unrecognized encoding"),
						tr("The text encoding %1 used in %2 is not supported.\n\n"
						   "It will be interpreted as %3 instead, which may result in incorrect text.")
							.arg(reqName, fileInfo.absoluteFilePath(), QString::fromUtf8((*codecUsed)->name().constData())),
						QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Ok) == QMessageBox::Cancel)
					return QString();
			}
		}
	}

	// When using the UTF-8 codec (mib = 106), byte order marks (BOMs) are
	// ignored during reading and not produced when writing. To keep them in
	// files that have them, we need to check for them ourselves.
	if ((*codecUsed)->mibEnum() == 106 && peekBytes.size() >= 3 && peekBytes[0] == '\xEF' && peekBytes[1] == '\xBB' && peekBytes[2] == '\xBF')
		utf8BOM = true;

	// If the file is empty (we're already at the end), don't try to read
	// anything using QTextStream below as that would return a Null-String
	// (QString()) rather than an empty string (QString("")). Instead, return
	// an empty string right away.
	if (file.atEnd())
		return QStringLiteral("");

	QString text = (*codecUsed)->toUnicode(file.readAll());

	if (lineEndings) {
		if (text.contains(QLatin1String("\r\n"))) {
			text.replace(QLatin1String("\r\n"), QChar::fromLatin1('\n'));
			*lineEndings = kLineEnd_CRLF;
		}
		else if (text.contains(QChar::fromLatin1('\r')) && !text.contains(QChar::fromLatin1('\n'))) {
			text.replace(QChar::fromLatin1('\r'), QChar::fromLatin1('\n'));
			*lineEndings = kLineEnd_CR;
		}
		else
			*lineEndings = kLineEnd_LF;

		if (text.contains(QChar::fromLatin1('\r'))) {
			text.replace(QChar::fromLatin1('\r'), QChar::fromLatin1('\n'));
			*lineEndings |= kLineEnd_Mixed;
		}
	}

	return text;
}

void TeXDocumentWindow::loadFile(const QFileInfo & fileInfo, bool asTemplate, bool inBackground, bool reload, QTextCodec * forceCodec)
{
	flushScriptEdits();
	// Opt-in developer timing; normal loads neither log nor write a report.
	const QString profilePath = qEnvironmentVariable("TEXLINE_LOAD_PROFILE");
	QElapsedTimer profileTimer;
	QJsonArray profilePhases;
	qint64 previousProfileTime = 0;
	if (!profilePath.isEmpty()) profileTimer.start();
	auto profilePhase = [&](const char *name) {
		if (profilePath.isEmpty()) return;
		const qint64 now = profileTimer.elapsed();
		profilePhases.append(QJsonObject{{QStringLiteral("phase"), QString::fromLatin1(name)},
			{QStringLiteral("ms"), now - previousProfileTime}});
		previousProfileTime = now;
	};
	QString fileContents = readFile(fileInfo, &codec, &lineEndings, forceCodec);
	profilePhase("read-decode");
	showLineEndingSetting();
	showEncodingSetting();

	if (fileContents.isNull())
		return;
	bool identicalContent{fileContents == textEdit->text()};
	const auto properties = reload ? QMap<QString, QVariant>() :
		TWApp::instance()->getFileProperties(fileInfo.absoluteFilePath());

	// Only re-set the content if it has actually changed. Setting the content
	// has many side effects, e.g., destroying the undo/redo stack.
	if (!reload || !identicalContent) {
		QApplication::setOverrideCursor(Qt::WaitCursor);

		// Preserve a revision before an explicitly requested reload discards edits.
		if (textEdit->document()->isModified()) Research::checkpoint(this, true);
#if QT_VERSION >= QT_VERSION_CHECK(6, 4, 0)
		// Prepare text, paragraph spacing and final width in one layout pass.
		// No events are dispatched while layout is suspended.
		const bool layoutEnabled = textDoc()->isLayoutEnabled();
		textDoc()->setLayoutEnabled(false);
#endif
		textEdit->setPlainText(fileContents);
		profilePhase("set-text");
		setLineSpacing(m_lineSpacing);
		profilePhase("line-spacing");

		// Prepare the final geometry and workspace before the first visible frame.
		// Keep the editor visible for the text-layout workaround below, but never
		// expose its initial maximized state or an unfinished PDF placement.
		if (!reload) {
			bool autoPlace = true;
			if (properties.contains(QString::fromLatin1("geometry"))) {
				restoreGeometry(properties.value(QString::fromLatin1("geometry")).toByteArray());
				autoPlace = false;
			}
			else
				Tw::Utils::WindowManager::zoomToHalfScreen(this, false);
			if (properties.contains(QString::fromLatin1("state")))
				restoreState(properties.value(QString::fromLatin1("state")).toByteArray(), kTeXWindowStateVersion);
			if (!inBackground) Research::restoreWorkspace(this);
			if (properties.contains(QString::fromLatin1("geometry")))
				Tw::Utils::WindowManager::correctPaperHalfScreen(this);
			if (!asTemplate) {
				setCurrentFile(fileInfo);
				const int index = engine->findText(Research::preferredEngine(fileInfo.absoluteFilePath(), fileContents), Qt::MatchFixedString);
				if (index >= 0) engine->setCurrentIndex(index);
				Tw::Settings settings;
				if (!inBackground && settings.value(QString::fromLatin1("openPDFwithTeX"), kDefault_OpenPDFwithTeX).toBool())
					openPdfIfAvailable(false);
				else {
					QString previewFileName;
					actionGo_to_Preview->setEnabled(getPreviewFileName(previewFileName));
				}
				// PDF loading may change this setting; keep the source directory.
				settings.setValue(QString::fromLatin1("openDialogDir"), fileInfo.canonicalPath());
			}
			if (properties.contains(QString::fromLatin1("quotesMode")))
				setSmartQuotesMode(properties.value(QString::fromLatin1("quotesMode")).toString());
			if (properties.contains(QString::fromLatin1("indentMode")))
				setAutoIndentMode(properties.value(QString::fromLatin1("indentMode")).toString());
			if (properties.contains(QString::fromLatin1("syntaxMode")))
				setSyntaxColoringMode(properties.value(QString::fromLatin1("syntaxMode")).toString());
			if (properties.contains(QString::fromLatin1("wrapLines")))
				setWrapLines(properties.value(QString::fromLatin1("wrapLines")).toBool());
			if (properties.contains(QString::fromLatin1("lineNumbers")))
				setLineNumbers(properties.value(QString::fromLatin1("lineNumbers")).toBool());

			if (pdfDoc) {
				if (properties.contains(QString::fromLatin1("pdfgeometry"))) {
					pdfDoc->restoreGeometry(properties.value(QString::fromLatin1("pdfgeometry")).toByteArray());
					autoPlace = false;
				}
				if (properties.contains(QString::fromLatin1("pdfstate")))
					pdfDoc->restoreState(properties.value(QString::fromLatin1("pdfstate")).toByteArray(), kPDFWindowStateVersion);
				Research::restoreWorkspace(pdfDoc);
				if (properties.contains(QString::fromLatin1("pdfgeometry")))
					Tw::Utils::WindowManager::correctPaperHalfScreen(pdfDoc);
			}
			if (autoPlace) {
				if (pdfDoc)
					Tw::Utils::WindowManager::sideBySide(this, pdfDoc);
				else
					Tw::Utils::WindowManager::zoomToHalfScreen(this, false);
			}
			if (!inBackground) show();
		}
#if QT_VERSION >= QT_VERSION_CHECK(6, 4, 0)
		textDoc()->setLayoutEnabled(layoutEnabled);
#endif
		profilePhase("workspace-engine");
#if QT_VERSION >= QT_VERSION_CHECK(6, 4, 0)
		// The corrected Qt layout preserves unlaid suffix paragraphs through
		// partial syntax updates. Only demand the visible prefix here; distant
		// cursor jumps and explicit documentSize() calls still demand full layout.
#ifdef TW_QT_SAFE_LAZY_LAYOUT
		textDoc()->documentLayout()->blockBoundingRect(textDoc()->firstBlock());
#else
		textDoc()->documentLayout()->documentSize();
#endif
		profilePhase("initial-layout");
#endif
		QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
		profilePhase("initial-events");

		if (!inBackground || reload) {
			// Try to work around QTBUG-20354
			// It seems that adding additionalFormats (as is done automatically on
			// setPlainText() by the syntax highlighter) can disturb the layouting
			// process, leaving some blocks with size zero. This causes the
			// corresponding lines to "disappear" and can even crash the application
			// in connection with the "highlight current line" feature.
			QTextDocument * doc = textEdit->document();
			Q_ASSERT(doc);
			QAbstractTextDocumentLayout * docLayout = doc->documentLayout();
			Q_ASSERT(docLayout);

			int tries{0};
			for (tries = 0; tries < 10; ++tries) {
				bool isLayoutOK = true;
				QTextBlock first = doc->firstBlock();
#if QT_VERSION >= QT_VERSION_CHECK(6, 4, 0)
				// QTextEdit lays out offscreen paragraphs on demand. Walking every
				// block forces a complete synchronous layout of very large files.
				const bool checkWholeDocument = doc->blockCount() <= 128;
				if (!checkWholeDocument) first = textEdit->cursorForPosition(QPoint(0, 0)).block();
#endif
				int checked = 0;
				for (QTextBlock b = first; b.isValid(); b = b.next()) {
					const QRectF bounds = docLayout->blockBoundingRect(b);
					// Empty paragraphs legitimately have zero width in Qt 6.
					// A missing line has no height; nonblank text must have width.
					if (bounds.height() <= 0 || (bounds.width() <= 0 && !b.text().trimmed().isEmpty())) {
						isLayoutOK = false;
						break;
					}
#if QT_VERSION >= QT_VERSION_CHECK(6, 4, 0)
					// Include a small lookahead, with a hard bound for tiny lines.
					if (!checkWholeDocument && (++checked >= 128 || bounds.top() > textEdit->verticalScrollBar()->value() + textEdit->viewport()->height() + 256)) break;
#else
					Q_UNUSED(checked)
#endif
				}
				if (isLayoutOK) break;
				// Re-setting the document content naturally triggers a relayout
				// (also a rehighlight). Note that layouting only works sensibly
				// once show() was called, or else there is no valid widget geometry
				// to act as bounding box.
#if QT_VERSION < QT_VERSION_CHECK(5, 9, 0)
				doc->setPlainText(doc->toPlainText());
#else
				doc->setPlainText(doc->toRawText());
#endif
				setLineSpacing(m_lineSpacing);
				QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
			}
			if (tries >= 10) {
				QMessageBox::warning(this, tr("Layout Problem"), tr("A problem occurred while laying out the loaded document in the editor. This is caused by an issue in the underlying Qt framework and can cause TeXworks to crash under certain circumstances. The symptoms of this problem are hidden or overlapping lines. To work around this, please try one of the following:\n -) Turn syntax highlighting off and on\n -) Turn line numbers off and on\n -) Resize the window\n\nWe are sorry for the inconvenience."));
			}
		}
		profilePhase("layout-validation");

		QApplication::restoreOverrideCursor();
	}

	if (asTemplate) {
		lastModified = QDateTime();
	}
	else {
		setCurrentFile(fileInfo);

		Research::showTransientStatusMessage(this, tr("File \"%1\" loaded").arg(textDoc()->getFileInfo().fileName()),
								 kStatusMessageDuration);
		setupFileWatcher();
	}
	maybeEnableSaveAndRevert(false);

	if (!reload) {
		if (!properties.contains(QStringLiteral("researchRecentPlace")) && properties.contains(QString::fromLatin1("selStart"))) {
			QTextCursor c(textEdit->document());
			c.setPosition(qBound(0, properties.value(QString::fromLatin1("selStart")).toInt(), textDoc()->characterCount()-1));
			c.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor, properties.value(QString::fromLatin1("selLength"), 0).toInt());
			textEdit->setTextCursor(c);
		}

		if (pdfDoc && !inBackground)
			pdfDoc->show();

		if (!inBackground) selectWindow();
		saveRecentFileInfo();
	}

	editor()->updateLineNumberAreaWidth(0);
	profilePhase("metadata-selection");

	runHooks(QString::fromLatin1("LoadFile"));
	if (researchCollaboration) researchCollaboration->acceptedDisk(textEdit->toPlainText());
	Research::startRecovery(this);
	profilePhase("hooks-recovery");
	if (!reload && !asTemplate && !inBackground) Research::scheduleRecentWritingPlace(this, properties.value(QStringLiteral("researchRecentPlace")).toMap());
	if (!profilePath.isEmpty()) {
		QSaveFile report(profilePath);
		if (report.open(QIODevice::WriteOnly)) {
			report.write(QJsonDocument(QJsonObject{{QStringLiteral("file"), fileInfo.absoluteFilePath()},
				{QStringLiteral("blocks"), textDoc()->blockCount()}, {QStringLiteral("phases"), profilePhases},
				{QStringLiteral("total_ms"), profileTimer.elapsed()}}).toJson());
			report.commit();
		}
	}
}

void TeXDocumentWindow::delayedInit()
{
	if (_texDoc && !_texDoc->getHighlighter()) {
		Tw::Settings settings;

		TeXHighlighter * highlighter = new TeXHighlighter(_texDoc);
		connect(textEdit, &CompletingEdit::rehighlight, highlighter, &TeXHighlighter::rehighlight);

		// set up syntax highlighting
		// First, use the current file's syntaxMode property (if available)
		QMap<QString,QVariant> properties = TWApp::instance()->getFileProperties(textDoc()->absoluteFilePath());
		if (properties.contains(QString::fromLatin1("syntaxMode")))
			setSyntaxColoringMode(properties.value(QString::fromLatin1("syntaxMode")).toString());
		// Secondly, try the global settings
		else if (settings.contains(QString::fromLatin1("syntaxColoring")))
			    setSyntaxColoringMode(settings.value(QString::fromLatin1("syntaxColoring")).toString());
		// Lastly, use the default setting
		else {
			// This should mimick the code in PrefsDialog::doPrefsDialog()
			QStringList syntaxOptions = TeXHighlighter::syntaxOptions();
			if (kDefault_SyntaxColoring < syntaxOptions.count())
				setSyntaxColoringMode(syntaxOptions[kDefault_SyntaxColoring]);
			else
				setSyntaxColoringMode(QString());
		}

		if (_texDoc && _texDoc->hasModeLine(QStringLiteral("spellcheck"))) {
			setSpellcheckLanguage(_texDoc->getModeLineValue(QStringLiteral("spellcheck")));
		}
		else {
			// set the default spell checking language
			setSpellcheckLanguage(settings.value(QString::fromLatin1("language")).toString());
		}
	}
}

void TeXDocumentWindow::reloadIfChangedOnDisk()
{
    if (researchCollaboration) researchCollaboration->fileChanged();
}

// get expected name of the Preview file, and return whether it exists
bool TeXDocumentWindow::getPreviewFileName(QString &pdfName)
{
	const QString & rootFilePath = textDoc()->getRootFilePath();
	if (rootFilePath.isEmpty())
		return false;
	pdfName = Research::previewFile(rootFilePath);
	return !pdfName.isEmpty();
}

bool TeXDocumentWindow::openPdfIfAvailable(bool show)
{
	detachPdf();
	actionSide_by_Side->setEnabled(false);
	actionGo_to_Preview->setEnabled(false);

	QString pdfName;
	if (getPreviewFileName(pdfName)) {
		PDFDocumentWindow *existingPdf = PDFDocumentWindow::findDocument(pdfName);
		if (existingPdf) {
			pdfDoc = existingPdf;
			if (show) pdfDoc->selectWindow();
			pdfDoc->linkToSource(this);
		}
		else {
			pdfDoc = new PDFDocumentWindow(pdfName, this);
			if (show)
				pdfDoc->show();
		}
	}

	if (pdfDoc) {
		actionSide_by_Side->setEnabled(true);
		actionGo_to_Preview->setEnabled(true);
		connect(pdfDoc, &PDFDocumentWindow::destroyed, this, &TeXDocumentWindow::pdfClosed);
		connect(this, &TeXDocumentWindow::destroyed, pdfDoc, &PDFDocumentWindow::texClosed);
		Research::slidePreviewAvailable(this);
		return true;
	}

	return false;
}

void TeXDocumentWindow::pdfClosed()
{
	pdfDoc = nullptr;
	actionSide_by_Side->setEnabled(false);
}

bool TeXDocumentWindow::saveFile(const QFileInfo & fileInfo)
{
	flushScriptEdits();
	const QString previousPath = untitled() ? QString() : textDoc()->absoluteFilePath();
	const auto showNotSavedMessage = [this]() {
		Research::saveFailure(this, tr("Document \"%1\" was not saved").arg(textDoc()->getFileInfo().fileName()));
	};
	if (researchCollaboration && !researchCollaboration->beforeSave(fileInfo.absoluteFilePath())) return false;

	Research::beforeSave(this, fileInfo.absoluteFilePath());
	QString theText = textEdit->text();
	switch (lineEndings & kLineEnd_Mask) {
		case kLineEnd_CR:
			theText.replace(QChar::fromLatin1('\n'), QChar::fromLatin1('\r'));
			break;
		case kLineEnd_LF:
			break;
		case kLineEnd_CRLF:
			theText.replace(QChar::fromLatin1('\n'), QLatin1String("\r\n"));
			break;
	}

	if (!codec)
		codec = TWApp::instance()->getDefaultCodec();
	if (!codec->canEncode(theText)) {
		if (QMessageBox::warning(this, tr("Text cannot be converted"),
				tr("This document contains characters that cannot be represented in the encoding %1.\n\n"
				   "If you proceed, they will be replaced with default codes. "
				   "Alternatively, you may wish to use a different encoding (such as UTF-8) to avoid loss of data.")
		            .arg(QString::fromUtf8(codec->name().constData())),
				QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel) == QMessageBox::Cancel) {
			showNotSavedMessage();
			return false;
		}
	}

	clearFileWatcher();

	QByteArray encoded=codec->fromUnicode(theText);
	if(codec->mibEnum()==106&&utf8BOM)encoded.prepend("\xEF\xBB\xBF");
	QString error;
	QApplication::setOverrideCursor(Qt::WaitCursor);
	const auto verify=[&]{return !researchCollaboration||researchCollaboration->verifySave(fileInfo.absoluteFilePath());};
	const bool saved=Research::writeObservedFile(fileInfo.absoluteFilePath(),encoded,&error,
		fileInfo.absoluteFilePath()==previousPath?std::function<bool()>(verify):std::function<bool()>());
	QApplication::restoreOverrideCursor();
	if(!saved){
		setupFileWatcher();
		Research::saveFailure(this,Research::words("文稿未保存；当前草稿已保留。","The document was not saved; the current draft was retained.")+QStringLiteral("\n")+error);
		return false;
	}

	// Pass the absoluteFilePath to the function; this will create a new
	// (updated) QFileInfo instance. This is necessary as the original QFileInfo
	// has the existance of the file cached. If the file is saved for the first
	// time (i.e. it did not exist before saveFile() was called),
	// fileInfo.exists() will return the wrong (cached) info.
	setCurrentFile(QFileInfo(fileInfo.absoluteFilePath()));
	Research::saved(this, previousPath);
	if (researchCollaboration) researchCollaboration->acceptedDisk(textEdit->toPlainText());
	saveRecentFileInfo(true);
	Research::showTransientStatusMessage(this, tr("File \"%1\" saved")
								.arg(textDoc()->getFileInfo().fileName()),
								kStatusMessageDuration);

	QTimer::singleShot(0, this, SLOT(setupFileWatcher()));
	return true;
}

void TeXDocumentWindow::clearFileWatcher()
{
	const QStringList files = watcher->files();
	if (files.count() > 0)
		watcher->removePaths(files);
	const QStringList dirs = watcher->directories();
	if (dirs.count() > 0)
		watcher->removePaths(dirs);
}

void TeXDocumentWindow::setupFileWatcher()
{
	clearFileWatcher();
	if (!untitled()) {
		QFileInfo info(textDoc()->getFileInfo());
		lastModified = info.lastModified();
		watcher->addPath(info.absoluteFilePath());
		watcher->addPath(info.canonicalPath());
	}
}

bool TeXDocumentWindow::adoptMovedProjectFile(const QString &oldPath, const QString &newPath)
{
	if (isModified() || QFileInfo(fileName()).absoluteFilePath() != QFileInfo(oldPath).absoluteFilePath()
		|| !QFileInfo(newPath).isFile()) return false;
	clearFileWatcher();
	setCurrentFile(QFileInfo(newPath));
	setupFileWatcher();
	// The existing receiver preserves the cursor, selection and undo history.
	if (researchCollaboration) researchCollaboration->fileChanged();
	return true;
}

void TeXDocumentWindow::setCurrentFile(const QFileInfo & fileInfo)
{
	bool isUntitled = !fileInfo.exists();
	textDoc()->setStoredInFilesystem(!isUntitled);
	if (isUntitled) {
		static int sequenceNumber = 1;
		textDoc()->setFileInfo(QFileInfo(tr("untitled-%1.tex").arg(sequenceNumber++)));
		setWindowIcon(QApplication::windowIcon());
	}
	else {
		textDoc()->setFileInfo(fileInfo);
		QIcon winIcon;
#if defined(Q_OS_UNIX) && !defined(Q_OS_DARWIN)
		// The Compiz window manager doesn't seem to support icons larger than
		// 128x128, so we add a suitable one first
		winIcon.addFile(QString::fromLatin1(":/images/images/TeXworks-doc-128.png"));
#endif
		winIcon.addFile(QString::fromLatin1(":/images/images/TeXworks-doc.png"));
		setWindowIcon(winIcon);
	}
	textEdit->document()->setModified(false);
	setWindowModified(false);

	//: Format for the window title (ex. "file.tex[*] - TeXworks")
	setWindowTitle(tr("%1[*] - %2").arg(textDoc()->getFileInfo().fileName(), QApplication::applicationDisplayName()));

	conditionallyEnableRemoveAuxFiles();

	TWApp::instance()->updateWindowMenus();
    QTimer::singleShot(0,this,[this]{Research::detectSlideMode(this);Research::detectArticleDocument(this);});
}

void TeXDocumentWindow::saveRecentFileInfo(bool captureRecent)
{
	if (untitled())
		return;

	QMap<QString,QVariant> fileProperties;
	const auto placeKey = QStringLiteral("researchRecentPlace");
	auto place = TWApp::instance()->getFileProperties(textDoc()->absoluteFilePath()).value(placeKey).toMap();
	if (captureRecent) { const auto captured = Research::captureRecentWritingPlace(this); if (!captured.isEmpty()) place = captured; }
	if (!place.isEmpty()) fileProperties.insert(placeKey, place);

	fileProperties.insert(QString::fromLatin1("path"), textDoc()->absoluteFilePath());
	fileProperties.insert(QString::fromLatin1("geometry"), saveGeometry());
	fileProperties.insert(QString::fromLatin1("state"), saveState(kTeXWindowStateVersion));
	fileProperties.insert(QString::fromLatin1("selStart"), selectionStart());
	fileProperties.insert(QString::fromLatin1("selLength"), selectionLength());
	fileProperties.insert(QString::fromLatin1("quotesMode"), textEdit->getQuotesMode());
	fileProperties.insert(QString::fromLatin1("indentMode"), textEdit->getIndentMode());
	if (_texDoc && _texDoc->getHighlighter())
		fileProperties.insert(QString::fromLatin1("syntaxMode"), _texDoc->getHighlighter()->getSyntaxMode());
	fileProperties.insert(QString::fromLatin1("lineNumbers"), property("researchFocus").toBool() ? property("researchNormalLineNumbers").toBool() : textEdit->getLineNumbersVisible());
	fileProperties.insert(QString::fromLatin1("wrapLines"), textEdit->lineWrapMode() != QTextEdit::NoWrap);

	if (pdfDoc) {
		fileProperties.insert(QString::fromLatin1("pdfgeometry"), pdfDoc->saveGeometry());
		fileProperties.insert(QString::fromLatin1("pdfstate"), pdfDoc->saveState(kPDFWindowStateVersion));
	}

	TWApp::instance()->addToRecentFiles(fileProperties);
}

void TeXDocumentWindow::updateRecentFileActions()
{
	TWUtils::updateRecentFileActions(this, recentFileActions, menuOpen_Recent, actionClear_Recent_Files);
}

void TeXDocumentWindow::updateWindowMenu()
{
	Tw::Utils::WindowManager::updateWindowMenu(this, menuWindow);

	const QString label = Tw::Utils::WindowManager::uniqueLabelForFile(fileName());
	if (!label.isEmpty()) {
		setWindowTitle(property("researchFocus").toBool() ? tr("%1[*]").arg(label) : tr("%1[*] - %2").arg(label, QApplication::applicationDisplayName()));
	}
}

void TeXDocumentWindow::setupResearchWorkspace()
{
    toolBar_edit->clear();
    removeToolBar(toolBar_edit);
    toolBar_edit->hide();
    // Keep the engine combo as the existing selection controller. Its owning
    // widget action must survive even though selection now lives in a menu.
    for (auto *action : toolBar_run->actions()) {
        auto *widgetAction = qobject_cast<QWidgetAction *>(action);
        if (widgetAction && widgetAction->defaultWidget() == engine) action->setVisible(false);
        else toolBar_run->removeAction(action);
    }
    toolBar_run->setStyleSheet(QString());
    toolBar_run->setToolButtonStyle(Qt::ToolButtonTextOnly);
    auto *outline = Research::workspaceButton(toolBar_run, QString(), QStringLiteral(":/research/icons/outline.svg"), findChild<QAction *>(QStringLiteral("researchOutlineAction")));
    outline->setObjectName(QStringLiteral("researchStructureButton"));
    auto outlineCaption=[this,outline]{const bool slides=property("researchSlideMode").toBool();outline->setToolTip((Research::englishUi()?(slides?QStringLiteral("Slide list"):QStringLiteral("Structure")):(slides?QStringLiteral("幻灯片列表"):QStringLiteral("结构")))+QStringLiteral(" · Ctrl+Shift+O"));outline->setAccessibleName(outline->toolTip());};
    outlineCaption();connect(findChild<QAction *>(QStringLiteral("researchOutlineAction")),&QAction::changed,outline,outlineCaption);
    auto *pdf = Research::workspaceButton(toolBar_run, QStringLiteral("PDF"), QString(), actionGo_to_Preview);
    pdf->setObjectName(QStringLiteral("researchPdfButton"));
    toolBar_run->actions().last()->setProperty("researchFocusRight", true);
    auto *find = Research::workspaceButton(toolBar_run, QString(), QStringLiteral(":/research/icons/find.svg"), actionFind);
    find->setToolTip(Research::ui(QStringLiteral("查找 · Ctrl+F"))); find->setAccessibleName(Research::ui(QStringLiteral("查找")));
    toolBar_run->actions().last()->setProperty("researchFocusHidden", true);
    auto *mode = Research::workspaceButton(toolBar_run, Research::ui(QStringLiteral("幻灯片")));
    mode->setObjectName(QStringLiteral("researchModeButton"));
    auto *modeMenu = new QMenu(mode); modeMenu->setObjectName(QStringLiteral("researchModeMenu"));
    auto *slideMode = findChild<QAction *>(QStringLiteral("researchSlideModeAction"));
    modeMenu->addAction(slideMode);
    modeMenu->addAction(Research::ui(QStringLiteral("普通源码模式")), this, [this,slideMode]{setProperty("researchSlideManualChoice",true);slideMode->setChecked(false);});
    mode->setMenu(modeMenu); mode->setPopupMode(QToolButton::InstantPopup);Research::configureButton(mode,Research::ButtonRole::Text);mode->hide();toolBar_run->actions().last()->setObjectName(QStringLiteral("researchModeControl"));toolBar_run->actions().last()->setVisible(false);
    auto *space = new QWidget(toolBar_run);
    space->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolBar_run->addWidget(space);
    researchProblemsAction = new QAction(this);
    researchProblemsAction->setObjectName(QStringLiteral("researchProblemCount"));
    researchProblemsAction->setVisible(false);
    connect(researchProblemsAction, &QAction::triggered, this, &TeXDocumentWindow::toggleConsoleVisibility);
    auto *problems = new QToolButton(toolBar_run);problems->setDefaultAction(researchProblemsAction);
    auto *problemIndicator=toolBar_run->addWidget(problems);problemIndicator->setObjectName(QStringLiteral("researchProblemIndicator"));problemIndicator->setVisible(false);
    Research::configureButton(problems, Research::ButtonRole::Status);
    connect(researchProblemsAction, &QAction::changed, problems, [this,problems,problemIndicator]() { Research::configureButton(problems, Research::ButtonRole::Status);problemIndicator->setVisible(researchProblemsAction->isVisible()&&property("researchBuildErrors").toInt()>0); });
    toolBar_run->addAction(actionTypeset);
    auto *compile = qobject_cast<QToolButton *>(toolBar_run->widgetForAction(actionTypeset));
    compile->setObjectName(QStringLiteral("researchCompile"));
    compile->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    auto *engines = new QMenu(compile);
    engines->setObjectName(QStringLiteral("researchEngineMenu"));
    connect(engines, &QMenu::aboutToShow, this, [this, engines]() { populateResearchEngineMenu(engines); });
    compile->setMenu(engines); compile->setPopupMode(QToolButton::MenuButtonPopup);
    Research::configureButton(compile, Research::ButtonRole::Compile);
    auto *more = new QMenu(this);
    more->setObjectName(QStringLiteral("researchMoreMenu"));
    more->addAction(actionSave); more->addAction(actionSave_As); more->addAction(actionOpen);
    more->addSeparator(); more->addAction(actionFind); more->addAction(actionReplace);
    more->addSeparator(); more->addAction(actionShow_Hide_Console); more->addAction(actionSide_by_Side);
    more->addAction(actionPreferences);
    Research::installReferenceSource(this, more);
    Research::installWorkspace(this, toolBar_run, more);
    Research::installResearchAuthoring(this);
    Research::installSnippetAuthoring(this);
    auto *documentBuild=new QMenu(Research::englishUi()?QStringLiteral("Document build settings"):Research::ui(QStringLiteral("本文编译设置")),this);documentBuild->setObjectName(QStringLiteral("researchDocumentBuildMenu"));
    if(auto *build=findChild<QMenu *>(QStringLiteral("researchBuildMenu")))build->addMenu(documentBuild);
    connect(documentBuild,&QMenu::aboutToShow,this,[this,documentBuild]{documentBuild->clear();if(auto *root=findChild<QAction *>(QStringLiteral("researchRootAction")))documentBuild->addAction(root);Research::addBuildOutputMenu(this,documentBuild);});
    updateTypesettingAction();
}

void TeXDocumentWindow::populateResearchEngineMenu(QMenu *menu)
{
    menu->clear();
    Research::addBuildDestinationMenu(this,menu);
    if(researchProblemsAction&&researchProblemsAction->isVisible()){menu->addAction(researchProblemsAction);menu->addSeparator();}
    auto *other = menu->findChild<QMenu *>(QStringLiteral("researchOtherEngines"), Qt::FindDirectChildrenOnly);
    if (!other) { other = new QMenu(Research::ui(QStringLiteral("其他编译器")), menu); other->setObjectName(QStringLiteral("researchOtherEngines")); }
    other->clear();
    for (auto *action : engineActions->actions()) {
        if (action->text() == QStringLiteral("pdfLaTeX") || action->text() == QStringLiteral("XeLaTeX")) menu->addAction(action);
        else other->addAction(action);
    }
    if (!other->actions().isEmpty()) {
      menu->addSection(Research::ui(QStringLiteral("其他编译器")));
      for(auto *action:other->actions())menu->addAction(action);
    }
    menu->addSeparator();
    menu->addAction(Research::ui(QStringLiteral("恢复自动识别")), this, [this]() {
        Research::forgetEngine(textDoc()->absoluteFilePath());
        const int index = engine->findText(Research::preferredEngine(textDoc()->absoluteFilePath(), textEdit->toPlainText()), Qt::MatchFixedString);
        if (index >= 0) engine->setCurrentIndex(index);
    });
}

void TeXDocumentWindow::showResearchFind()
{
    Research::showFind(this);
}

void TeXDocumentWindow::researchFind(bool backwards)
{
    Research::findNext(this,backwards);
}

void TeXDocumentWindow::updateEngineList()
{
	engine->disconnect(this);
	while (menuRun->actions().count() > 2)
		menuRun->removeAction(menuRun->actions().last());
	while (engineActions->actions().count() > 0)
		delete engineActions->actions().last();

	QStandardItemModel * model = qobject_cast<QStandardItemModel*>(engine->model());
	Q_ASSERT(model);
	model->clear();
	foreach (Engine e, TWApp::instance()->getEngineList()) {
		QAction *newAction = new QAction(e.name(), engineActions);
		newAction->setCheckable(true);
		bool available{e.isAvailable()};
		newAction->setEnabled(available);
		newAction->setObjectName(QStringLiteral("actionTypesetTool%1").arg(e.name()));
		menuRun->addAction(newAction);
		QStandardItem * item = new QStandardItem(e.name());
		item->setFlags(Qt::ItemIsSelectable | (available ? Qt::ItemIsEnabled : Qt::NoItemFlags));
		model->appendRow(item);
	}
	connect(engine, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, static_cast<void (TeXDocumentWindow::*)(int)>(&TeXDocumentWindow::selectedEngine));
	int index = engine->findText(engineName, Qt::MatchFixedString);
	if (index < 0)
		index = engine->findText(TWApp::instance()->getDefaultEngine().name(), Qt::MatchFixedString);
	if (index >= 0)
		engine->setCurrentIndex(index);
	if (engine->currentIndex() >= 0) selectedEngine(engine->currentIndex());
}

void TeXDocumentWindow::selectedEngine(QAction* engineAction) // sent by actions in menubar menu; update toolbar combo box
{
	engineName = engineAction->text();
	Research::rememberEngine(textDoc()->absoluteFilePath(), engineName);
	for (int i = 0; i < engine->count(); ++i)
		if (engine->itemText(i) == engineName) {
			engine->setCurrentIndex(i);
			break;
		}
}

void TeXDocumentWindow::selectedEngine(int idx) // sent by toolbar combo box; need to update menu
{
	const QString name = engine->itemText(idx);
	engineName = name;
	foreach (QAction *act, engineActions->actions()) {
		if (act->text() == name) {
			act->setChecked(true);
			break;
		}
	}
}

void TeXDocumentWindow::showCursorPosition()
{
	QTextCursor cursor = textEdit->textCursor();
	const int activeLine = cursor.blockNumber() + 1;
	const int selectedCharacters = cursor.selectionEnd() - cursor.selectionStart();
	const int selectedLines = selectedCharacters ? textEdit->document()->findBlock(cursor.selectionEnd() - 1).blockNumber() - textEdit->document()->findBlock(cursor.selectionStart()).blockNumber() + 1 : 0;
	cursor.setPosition(cursor.selectionStart());
	int line = cursor.blockNumber() + 1;
	int total = textEdit->document()->blockCount();
	int col = cursor.position() - textEdit->document()->findBlock(cursor.selectionStart()).position();
	QString position = QStringLiteral("%1 / %2").arg(activeLine).arg(total);
	if (selectedCharacters) position += Research::ui(QStringLiteral(" · 选中 %1 字符，%2 行")).arg(selectedCharacters).arg(selectedLines);
	lineNumberLabel->setText(position);
	lineNumberLabel->setToolTip(tr("Line %1 of %2; col %3").arg(line).arg(total).arg(col));
	if (actionAuto_Follow_Focus->isChecked())
		emit syncFromSource(textDoc()->absoluteFilePath(), line, col, false);
}

void TeXDocumentWindow::showLineEndingSetting()
{
	QString lineEndStr;
	switch (lineEndings & kLineEnd_Mask) {
		case kLineEnd_LF:
		    lineEndStr = tr("LF");
			break;
		case kLineEnd_CRLF:
		    lineEndStr = tr("CRLF");
			break;
		case kLineEnd_CR:
		    lineEndStr = tr("CR");
			break;
	}
	if ((lineEndings & kLineEnd_Mixed) != 0)
		lineEndStr += tr("*");
	lineEndingLabel->setText(lineEndStr);
	lineEndingLabel->setVisible((lineEndings & kLineEnd_Mixed) != 0);
}

void TeXDocumentWindow::lineEndingPopup(const QPoint loc)
{
	QMenu menu;
	QAction * lf = menu.addAction(tr("LF (Unix, Mac OS X)"));
	QAction * crlf = menu.addAction(tr("CRLF (Windows)"));
	QAction * cr = menu.addAction(tr("CR (Mac Classic)"));
	QAction * result = menu.exec(lineEndingLabel->mapToGlobal(loc));
	int newSetting = (lineEndings & kLineEnd_Mask);
	if (result == lf)
		newSetting = kLineEnd_LF;
	else if (result == crlf)
		newSetting = kLineEnd_CRLF;
	else if (result == cr)
		newSetting = kLineEnd_CR;
	if (newSetting != (lineEndings & kLineEnd_Mask)) {
		lineEndings = newSetting;
		showLineEndingSetting();
		textEdit->document()->setModified();
	}
}

void TeXDocumentWindow::showEncodingSetting()
{
	encodingLabel->setText(codec ? QString::fromUtf8(codec->name().constData()) : QString());
	encodingLabel->setVisible(codec && codec->mibEnum() != 106);
}

void TeXDocumentWindow::encodingPopup(const QPoint loc)
{
	QMenu menu;
	//: Item in the encoding popup menu
	QAction * reloadAction = new QAction(tr("Reload using selected encoding"), &menu);
	//: Tooltip for "Reload using selected encoding"
	reloadAction->setToolTip(tr("Reloads the current file with the encoding selected from this menu.\n\nThe selected encoding replaces the default one and overrides all \"%!TEX encoding\" lines."));
	QAction * BOMAction = new QAction(tr("Write UTF-8 byte order mark"), &menu);
	BOMAction->setCheckable(true);
	BOMAction->setChecked(utf8BOM);
	// Only enable this option if we are currently using the UTF-8 codec
	BOMAction->setEnabled(codec && codec->mibEnum() == 106);

	if (!untitled())
		menu.addAction(reloadAction);
	menu.addAction(BOMAction);
	menu.addSeparator();

	foreach (QTextCodec *codec, *TWUtils::findCodecs()) {
		QAction * a = new QAction(QString::fromUtf8(codec->name().constData()), &menu);
		a->setCheckable(true);
		if (codec == this->codec)
			a->setChecked(true);
		menu.addAction(a);
	}
	QAction *result = menu.exec(encodingLabel->mapToGlobal(loc));
	if (result) {
		if (result == reloadAction) {
			if (textEdit->document()->isModified()) {
				if (QMessageBox::warning(this, tr("Unsaved changes"),
										 tr("The file you are trying to reload has unsaved changes.\n\n"
											"Do you want to discard your current changes, and reload the file from disk with the encoding %1?")
				                         .arg(QString::fromUtf8(codec->name().constData())),
										 QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::No) {
					return;
				}
			}
			clearFileWatcher(); // stop watching until next save or reload
			loadFile(textDoc()->getFileInfo(), false, true, true, codec);
		}
		else if (result == BOMAction) {
			utf8BOM = BOMAction->isChecked();
			// If the UTF-8 codec is selected, changing utf8BOM actually
			// modifies how the file is saved. In all other cases, it does not
			// take effect until the UTF-8 codec is selected (in which case the
			// modified flag is set anyway).
			if (codec && codec->mibEnum() == 106)
				textEdit->document()->setModified();
		}
		else {
			QTextCodec *newCodec = QTextCodec::codecForName(result->text().toLatin1());
			if (newCodec && newCodec != codec) {
				codec = newCodec;
				showEncodingSetting();
				textEdit->document()->setModified();
			}
		}
	}
}

void TeXDocumentWindow::sideBySide()
{
	if (pdfDoc) {
		Tw::Utils::WindowManager::sideBySide(this, pdfDoc);
		pdfDoc->selectWindow(false);
		selectWindow();
	}
	else
		placeOnLeft();
}

TeXDocumentWindow *TeXDocumentWindow::findDocument(const QString &fileName)
{
	QString canonicalFilePath = QFileInfo(fileName).canonicalFilePath();
	if (canonicalFilePath.isEmpty())
		canonicalFilePath = fileName;
			// file doesn't exist (probably from find-results in a new untitled doc),
			// so just use the name as-is

	foreach (QWidget *widget, qApp->topLevelWidgets()) {
		TeXDocumentWindow *theDoc = qobject_cast<TeXDocumentWindow*>(widget);
		if (theDoc && theDoc->textDoc()->getFileInfo().canonicalFilePath() == canonicalFilePath)
			return theDoc;
	}
	return nullptr;
}

void TeXDocumentWindow::clear()
{
	textEdit->textCursor().removeSelectedText();
}

QString TeXDocumentWindow::getLineText(int lineNo) const
{
	QTextDocument* doc = textEdit->document();
	if (lineNo < 1 || lineNo > doc->blockCount())
		return QString();
	return doc->findBlockByNumber(lineNo - 1).text();
}

void TeXDocumentWindow::goToLine(int lineNo, int selStart, int selEnd)
{
	QTextDocument* doc = textEdit->document();
	if (lineNo < 1 || lineNo > doc->blockCount())
		return;
	int oldScrollValue = -1;
	if (textEdit->verticalScrollBar())
		oldScrollValue = textEdit->verticalScrollBar()->value();
	QTextCursor cursor(doc->findBlockByNumber(lineNo - 1));
	if (selStart >= 0 && selEnd >= selStart) {
		cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::MoveAnchor, selStart);
		cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor, selEnd - selStart);
	}
	else
		cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
	textEdit->setTextCursor(cursor);
	maybeCenterSelection(oldScrollValue);
}

void TeXDocumentWindow::maybeCenterSelection(int oldScrollValue)
{
	if (oldScrollValue != -1 && textEdit->verticalScrollBar()) {
		int newScrollValue = textEdit->verticalScrollBar()->value();
		if (newScrollValue != oldScrollValue) {
			int delta = (textEdit->height() - textEdit->cursorRect().height()) / 2;
			if (newScrollValue < oldScrollValue)
				delta = -delta;
			textEdit->verticalScrollBar()->setValue(newScrollValue + delta);
		}
	}
}

void TeXDocumentWindow::doFontDialog()
{
	bool ok{false};
	QFont font = QFontDialog::getFont(&ok, textEdit->font());
	if (ok) {
		setEditorAppearance(font, lineSpacing());
		auto consoleFont=font;
		consoleFont.setPointSize(qMax(1,font.pointSize()-1));
		inputLine->setFont(consoleFont);textEdit_console->setFont(consoleFont);
		for(int i=1;i<consoleTabs->count();++i)consoleTabs->widget(i)->setFont(consoleFont);
	}
}

void TeXDocumentWindow::doLineDialog()
{
	QTextCursor cursor = textEdit->textCursor();
	cursor.setPosition(cursor.selectionStart());
	bool ok{false};
	int lineNo = QInputDialog::getInt(this, tr("Go to Line"),
									tr("Line number:"), cursor.blockNumber() + 1,
									1, textEdit->document()->blockCount(), 1, &ok);
	if (ok)
		goToLine(lineNo);
}

void TeXDocumentWindow::doFindDialog()
{
	showResearchFind();
}

void TeXDocumentWindow::doReplaceDialog()
{
	Research::showFind(this,true);
}

void TeXDocumentWindow::doIndent()
{
	textEdit->prefixLines(QString::fromLatin1("\t"));
}

void TeXDocumentWindow::doComment()
{
	textEdit->prefixLines(QString::fromLatin1("%"));
}

void TeXDocumentWindow::doUnindent()
{
	textEdit->unPrefixLines(QString::fromLatin1("\t"));
}

void TeXDocumentWindow::doUncomment()
{
	textEdit->unPrefixLines(QString::fromLatin1("%"));
}

void TeXDocumentWindow::toUppercase()
{
	replaceSelection(textEdit->textCursor().selectedText().toUpper());
}

void TeXDocumentWindow::toLowercase()
{
	replaceSelection(textEdit->textCursor().selectedText().toLower());
}

void TeXDocumentWindow::toggleCase()
{
	QString theText = textEdit->textCursor().selectedText();
	for (int i = 0; i < theText.length(); ++i) {
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
		QCharRef ch = theText[i];
#else
		QChar & ch = theText[i];
#endif
		if (ch.isLower())
			ch = ch.toUpper();
		else
			ch = ch.toLower();
	}
	replaceSelection(theText);
}

void TeXDocumentWindow::replaceSelection(const QString& newText)
{
	prepareScriptEdit();
	QTextCursor cursor = textEdit->textCursor();
	int start = cursor.selectionStart();
	cursor.insertText(newText);
	int end = cursor.selectionEnd();
	cursor.setPosition(start);
	cursor.setPosition(end, QTextCursor::KeepAnchor);
	textEdit->setTextCursor(cursor);
}

void TeXDocumentWindow::selectRange(int start, int length)
{
	// Selection is read-only. Start a document edit block only on the first
	// actual write; even an empty Qt edit block advances its revision.
	QTextCursor c = textCursor();
	c.setPosition(start);
	c.setPosition(start + length, QTextCursor::KeepAnchor);
	editor()->setTextCursor(c);
}

void TeXDocumentWindow::insertText(const QString& text)
{
	prepareScriptEdit();
	textCursor().insertText(text);
}

void TeXDocumentWindow::beginScriptEdits()
{
	flushScriptEdits();
	++_scriptEditDepth;
}

void TeXDocumentWindow::endScriptEdits()
{
	flushScriptEdits();
	if (_scriptEditDepth > 0) --_scriptEditDepth;
}

void TeXDocumentWindow::prepareScriptEdit()
{
	textDoc()->invalidateSourceText();
	if (_scriptEditDepth > 0 && _scriptEditCursor.isNull()) {
		textDoc()->documentLayout()->blockBoundingRect(textEdit->textCursor().block());
		textEdit->setProperty("researchScriptEditPending", true);
		textEdit->setExtraSelections({});
		_scriptEditCursor = QTextCursor(textDoc());
		_scriptEditCursor.beginEditBlock();
	}
}

void TeXDocumentWindow::flushScriptEdits()
{
	// Retire the member before endEditBlock emits source-change notifications.
	if (!_scriptEditCursor.isNull()) {
		QTextCursor cursor = _scriptEditCursor;
		_scriptEditCursor = QTextCursor();
		QPointer<CompletingEdit> edit = textEdit;
		cursor.endEditBlock();
		if (edit) {
			edit->document()->documentLayout()->blockBoundingRect(edit->textCursor().block());
			edit->setProperty("researchScriptEditPending", false);
			QMetaObject::invokeMethod(edit, "resetExtraSelections", Qt::DirectConnection);
		}
	}
}

void TeXDocumentWindow::setWindowModified(bool modified)
{
	QMainWindow::setWindowModified(modified || Research::hasPendingSlideNotes(this));
	TWApp::instance()->updateWindowMenus();
}

void TeXDocumentWindow::balanceDelimiters()
{
	const QString text = textEdit->text();
	QTextCursor cursor = textEdit->textCursor();
	using pos_type = decltype(cursor.position());
	auto openPos = TWUtils::findOpeningDelim(text, cursor.selectionStart());
	if (openPos >= 0 && openPos < text.length() - 1) {
		do {
			auto closePos = TWUtils::balanceDelim(text, openPos + 1, TWUtils::closerMatching(text[openPos]), 1);
			if (closePos < 0)
				break;
			if (closePos >= cursor.selectionEnd()) {
				cursor.setPosition(static_cast<pos_type>(openPos));
				cursor.setPosition(static_cast<pos_type>(closePos + 1), QTextCursor::KeepAnchor);
				textEdit->setTextCursor(cursor);
				return;
			}
			if (openPos > 0)
				openPos = TWUtils::findOpeningDelim(text, openPos - 1);
			else
				break;
		} while (openPos >= 0);
	}
	QApplication::beep();
}

void TeXDocumentWindow::doHardWrapDialog()
{
	HardWrapDialog dlg(this);

	dlg.show();
	if (dlg.exec()) {
		dlg.saveSettings();
		doHardWrap(dlg.mode(), static_cast<int>(dlg.lineWidth()), dlg.rewrap());
	}
}

void TeXDocumentWindow::doInsertCitationsDialog()
{
	CitationSelectDialog dlg(this);
	Tw::Settings settings;
	const QString defaultCiteCmd = settings.value(QStringLiteral("citeCommand"), QStringLiteral("\\cite{%1}")).toString();

	if (!textDoc()->hasModeLine(QStringLiteral("bibfile")) && !textDoc()->hasModeLine(QStringLiteral("bibfiles"))) {
		emit asyncFlashStatusBarMessage(tr("No '%!TEX bibfile' modline found"), kStatusMessageDuration);
		return;
	}

	// Load the bibfiles
	QStringList bibFiles = textDoc()->getModeLineValue(QStringLiteral("bibfile")).split(QLatin1Char{','}) +
						   textDoc()->getModeLineValue(QStringLiteral("bibfiles")).split(QLatin1Char{','});
	for (QString bibFile : bibFiles) {
		bibFile = bibFile.trimmed();
		if (bibFile.isEmpty()) continue;
		// Assume relative paths are given with respect to the current file's
		// directory.
		bibFile = textDoc()->getFileInfo().dir().absoluteFilePath(bibFile);
		dlg.addBibTeXFile(bibFile);
	}

	// Work out the enclosing citation command and already existing BibTeX keys
	// (if any)
	// TODO: Make configurable in a config text file
	QStringList citeCmds = QStringList() << QLatin1String("cite") \
	                                     << QLatin1String("bibentry") \
	                                     << QLatin1String("citet") \
	                                     << QLatin1String("citep") \
	                                     << QLatin1String("citealt") \
	                                     << QLatin1String("citealp") \
	                                     << QLatin1String("citenum") \
	                                     << QLatin1String("citeauthor") \
	                                     << QLatin1String("citeyear") \
	                                     << QLatin1String("citeyearpar") \
	                                     << QLatin1String("citefullauthor") \
	                                     << QLatin1String("Citet") \
	                                     << QLatin1String("Citep") \
	                                     << QLatin1String("Citealt") \
	                                     << QLatin1String("Citealp") \
	                                     << QLatin1String("Citeauthor");

	QString pattern = QString::fromLatin1("(\\\\(?:");
	for (const QString & citeCmd : citeCmds)
		pattern += QRegularExpression::escape(citeCmd) + QString::fromLatin1("|");
	pattern.chop(1);
	pattern += QLatin1String(")\\*?\\s*(?:\\[[^\\]]*\\])?\\s*\\{)([^}]*)\\}");
	// (\\(?:cite|...|Citeauthor)\*?\s*(?:\[[^\]]*\])?\s*\{)([^}]*)\}
	// capture group 1: everything in front of the BibTeX keys (e.g. "\cite{"
	//                  or "\Citep*[bla]{"
	// capture group 2: the existing BibTeX keys

	QTextCursor curs(textCursor());
	using pos_type = decltype(curs.position());
	constexpr int PeekLength = 1024;

	int peekFront = qMin(PeekLength, curs.position());
	int peekBack = qMin(PeekLength, textDoc()->characterCount() - curs.position());

	curs.beginEditBlock();
	curs.movePosition(QTextCursor::PreviousCharacter, QTextCursor::MoveAnchor, peekFront);
	curs.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor, peekFront + peekBack);
	curs.endEditBlock();

	const QString peekStr = curs.selectedText();
	QRegularExpression reCmd(pattern);
	QRegularExpressionMatch mCmd;

	// Check if the cursor is in one of the standard cite commands
#if QT_VERSION >= 0x050500
	auto pos = peekStr.lastIndexOf(reCmd, peekFront, &mCmd);
	Q_UNUSED(pos)
#else
	int pos = peekStr.lastIndexOf(reCmd, peekFront);
	if (pos >= 0)
		mCmd= reCmd.match(peekStr, pos);
#endif
	bool updateExisting = mCmd.hasMatch() && mCmd.capturedStart() < peekFront && mCmd.capturedEnd() > peekFront;

	if (!updateExisting) {
		// If the cursor was not in any of the standard cite commands, check if
		// it was in a (user-defined) default cite command
		const QString pattern{QStringLiteral("(") + QRegularExpression::escape(defaultCiteCmd).replace(QStringLiteral("\\%1"), QStringLiteral(")(.*?)"))};
		QRegularExpression reCmd{pattern};

#if QT_VERSION >= 0x050500
		auto pos = peekStr.lastIndexOf(reCmd, peekFront, &mCmd);
		Q_UNUSED(pos)
#else
		int pos = peekStr.lastIndexOf(reCmd, peekFront);
		if (pos >= 0)
			mCmd= reCmd.match(peekStr, pos);
#endif
		updateExisting = mCmd.hasMatch() && mCmd.capturedStart() < peekFront && mCmd.capturedEnd() > peekFront;
	}

	// If the cursor was in a recognized cite command, pre-seed the dialog with
	// the extracted BibTeX keys
	if (updateExisting) {
		dlg.setInitialKeys(mCmd.captured(2).split(QLatin1Char(',')));
	}

	// Run the dialog
	if (dlg.exec()) {
		// If the dialog succeeded, insert the changes

		// If the dialog was invoked without the cursor inside a citation
		// command, insert a new one (\cite by default)
		if (!updateExisting) {
			insertText(defaultCiteCmd.arg(dlg.getSelectedKeys().join(QLatin1String(","))));
		}
		// Otherwise, replace the argument of the existing citation command
		else {
			curs.beginEditBlock();
			// collapse the selection to the beginning
			curs.setPosition(qMin(curs.position(), curs.anchor()));
			// move to the beginning of the cite argument (just after '{')
			curs.movePosition(QTextCursor::NextCharacter, QTextCursor::MoveAnchor, static_cast<pos_type>(mCmd.capturedEnd(1)));
			// select the cite argument
			curs.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor, static_cast<pos_type>(mCmd.capturedLength(2)));
			// replace the text
			curs.insertText(dlg.getSelectedKeys().join(QLatin1String(",")));
			curs.endEditBlock();
		}
	}
}

void TeXDocumentWindow::doHardWrap(int mode, int lineWidth, bool rewrap)
{
	if (mode == kHardWrapMode_Window) {
		// fudge this for now.... not accurate with proportional fonts, ignores tabs,....
		QFontMetrics fm(textEdit->currentFont());
		lineWidth = textEdit->width() / fm.averageCharWidth();
	}
	else if (mode == kHardWrapMode_Unwrap) {
		lineWidth = INT_MAX;
		rewrap = true;
	}
	if (lineWidth == 0)
		return;

	QTextCursor cur = textEdit->textCursor();
	if (!cur.hasSelection())
		cur.select(QTextCursor::Document);

	int selStart = cur.selectionStart();
	int selEnd = cur.selectionEnd();

	cur.setPosition(selStart);
	if (!cur.atBlockStart()) {
		cur.movePosition(QTextCursor::StartOfBlock);
		selStart = cur.position();
	}

	cur.setPosition(selEnd);
	if (!cur.atBlockStart()) {
		cur.movePosition(QTextCursor::NextBlock);
		selEnd = cur.position();
	}

	cur.setPosition(selStart);
	cur.setPosition(selEnd, QTextCursor::KeepAnchor);

	QString oldText = cur.selectedText();
	QRegularExpression breakPattern(QStringLiteral("\\s+"));
	QString newText;

	while (!oldText.isEmpty()) {
		QString::size_type eol = oldText.indexOf(QChar::ParagraphSeparator);
		if (eol == -1)
			eol = oldText.length();
		else
			eol += 1;
		QString line = oldText.left(eol);
		oldText.remove(0, eol);

		if (rewrap && line.trimmed().length() > 0) {
			while (!oldText.isEmpty()) {
				eol = oldText.indexOf(QChar::ParagraphSeparator);
				if (eol == -1)
					eol = oldText.length();
				QString nextLine = oldText.left(eol).trimmed();
				if (nextLine.isEmpty())
					break;
				line = line.trimmed().append(QChar::fromLatin1(' ')).append(nextLine);
				oldText.remove(0, eol + 1);
			}
		}

		if (line.length() <= lineWidth) {
			newText.append(line);
			continue;
		}

		line = line.trimmed();
		if (line.length() <= lineWidth) {
			newText.append(line);
			continue;
		}

		QString::size_type curLength = 0;
		while (!line.isEmpty()) {
			QRegularExpressionMatch breakMatch = breakPattern.match(line);
			QString::size_type breakPoint = breakMatch.capturedStart();
			QString::size_type matchLen = breakMatch.capturedLength();
			if (breakPoint == -1) {
				breakPoint = line.length();
				matchLen = 0;
			}
			if (curLength > 0 && curLength + breakPoint >= lineWidth) {
				newText.append(QChar::ParagraphSeparator);
				curLength = 0;
			}
			if (curLength > 0) {
				newText.append(QChar::fromLatin1(' '));
				curLength += 1;
			}
			newText.append(line.left(breakPoint));
			curLength += breakPoint;
			line.remove(0, breakPoint + matchLen);
		}
		newText.append(QChar::ParagraphSeparator);
	}

	cur.insertText(newText);

	selEnd = cur.position();
	cur.setPosition(selStart);
	cur.setPosition(selEnd, QTextCursor::KeepAnchor);
	textEdit->setTextCursor(cur);
}


void TeXDocumentWindow::setLineNumbers(bool displayNumbers)
{
	setProperty(property("researchFocus").toBool() ? "researchFocusLineNumbers" : "researchNormalLineNumbers", displayNumbers);
	actionLine_Numbers->setChecked(displayNumbers);
	textEdit->setLineNumberDisplay(displayNumbers);
}

void TeXDocumentWindow::setEditorAppearance(const QFont &font, qreal spacing)
{
	if (textEdit->font() == font && qFuzzyCompare(m_lineSpacing, spacing)) return;
	const auto cursor = textEdit->textCursor();
	const auto anchor = textEdit->cursorForPosition(QPoint(0, 0));
	const int anchorY = textEdit->cursorRect(anchor).top();
	const int scrollX = textEdit->horizontalScrollBar()->value();
	const bool atTop = textEdit->verticalScrollBar()->value() == textEdit->verticalScrollBar()->minimum();
	const bool updates = textEdit->updatesEnabled();
	textEdit->setUpdatesEnabled(false);
#if QT_VERSION >= QT_VERSION_CHECK(6, 4, 0)
	const bool layoutEnabled = textDoc()->isLayoutEnabled();
	if (layoutEnabled) textDoc()->setLayoutEnabled(false);
#endif
	if (textEdit->font() != font) {
		textEdit->setFont(font);
	}
	setLineSpacing(spacing);
#if QT_VERSION >= QT_VERSION_CHECK(6, 4, 0)
	if (layoutEnabled) textDoc()->setLayoutEnabled(true);
#endif
	if (textEdit->textCursor() != cursor) textEdit->setTextCursor(cursor);
	textEdit->verticalScrollBar()->setValue(atTop ? textEdit->verticalScrollBar()->minimum() :
		textEdit->verticalScrollBar()->value() + textEdit->cursorRect(anchor).top() - anchorY);
	textEdit->horizontalScrollBar()->setValue(scrollX);
	textEdit->setUpdatesEnabled(updates);
}

void TeXDocumentWindow::setLineSpacing(qreal percent)
{
	// percent should typically be between 100 (single spacing) and 200 (double
	// spacing). Values below 1 are simply ignored (this includes the "typical
	// invalid return value" of 0).
	if (percent <= 1.)
		return;

	Q_ASSERT(textDoc() != nullptr);

	m_lineSpacing = percent;

#ifdef TW_QT_DISPLAY_LINE_SPACING
	// Source leading belongs to layout, not to block formats or undo history.
	if (textDoc()->property("_texline_display_line_spacing").toReal() != percent) {
		textDoc()->setProperty("_texline_display_line_spacing", percent);
		textDoc()->markContentsDirty(0, textDoc()->characterCount());
	}
	return;
#endif

	// Select the entire document
	QTextCursor cur{textDoc()};
	cur.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);

	// Don't set the block format if it is not necessary
	// (works around QTBUG-120451 in "common" cases)
	if (cur.blockFormat().lineHeightType() == QTextBlockFormat::ProportionalHeight && cur.blockFormat().lineHeight() == percent) {
		return;
	}
	if (cur.blockFormat().lineHeightType() == QTextBlockFormat::SingleHeight && percent == 100) {
		return;
	}

	// Remember the "modified" state. While we consider the line spacing as a
	// purely cosmetic property here, it is set on the underlying text document
	// and therefore will force its "modified" state to true (which we don't
	// want, e.g., when starting up)
	const bool wasModified = isModified();
	// Apply the modified line height
	QTextBlockFormat fmt;
	fmt.setLineHeight(percent, QTextBlockFormat::ProportionalHeight);
	cur.mergeBlockFormat(fmt);
	// Restore "modified" state
	setModified(wasModified);
}

void TeXDocumentWindow::setWrapLines(bool wrap)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 4, 0)
	const bool preserveAnchor = textDoc()->isLayoutEnabled();
#else
	const bool preserveAnchor = true;
#endif
	const QTextCursor anchor = preserveAnchor ? textEdit->cursorForPosition(QPoint(0, 0)) : QTextCursor(textDoc());
	const int anchorY = preserveAnchor ? textEdit->cursorRect(anchor).top() : 0;
	actionWrap_Lines->setChecked(wrap);
	// Long TeX commands and paths must not create horizontal overflow while
	// wrapping is enabled. This changes visual line breaks only.
	textEdit->setLineWrapMode(wrap ? QTextEdit::WidgetWidth : QTextEdit::NoWrap);
	textEdit->setWordWrapMode(wrap ? QTextOption::WrapAtWordBoundaryOrAnywhere : QTextOption::NoWrap);
	// Reflow changes the scroll range. Keep the first visible source position
	// at the same height rather than jumping to an unrelated paragraph.
	if (preserveAnchor) textEdit->verticalScrollBar()->setValue(textEdit->verticalScrollBar()->value()
		+ textEdit->cursorRect(anchor).top() - anchorY);
}

void TeXDocumentWindow::setSyntaxColoring(int index)
{
	if (_texDoc && _texDoc->getHighlighter())
		_texDoc->getHighlighter()->setActiveIndex(index);
}

void TeXDocumentWindow::setSyntaxColoringMode(const QString& mode)
{
	QList<QAction*> actionList = menuSyntax_Coloring->actions();

	if (mode.isEmpty()) {
		Q_ASSERT(actionSyntaxColoring_None);
		actionSyntaxColoring_None->trigger();
		return;
	}
	for (int i = 0; i < actionList.count(); ++i) {
		if (actionList[i]->isCheckable() && actionList[i]->text().compare(mode, Qt::CaseInsensitive) == 0) {
			actionList[i]->trigger();
			return;
		}
	}
}

void TeXDocumentWindow::setSmartQuotesMode(const QString& mode)
{
	QList<QAction*> actionList = menuSmart_Quotes_Mode->actions();
	for (int i = 0; i < actionList.count(); ++i) {
		if (actionList[i]->isCheckable() && actionList[i]->text().compare(mode, Qt::CaseInsensitive) == 0) {
			actionList[i]->trigger();
			return;
		}
	}
	if (mode.isEmpty()) {
		actionSmartQuotes_None->trigger();
		return;
	}
}

void TeXDocumentWindow::setAutoIndentMode(const QString& mode)
{
	QList<QAction*> actionList = menuAuto_indent_Mode->actions();
	for (int i = 0; i < actionList.count(); ++i) {
		if (actionList[i]->isCheckable() && actionList[i]->text().compare(mode, Qt::CaseInsensitive) == 0) {
			actionList[i]->trigger();
			return;
		}
	}
	if (mode.isEmpty()) {
		actionAutoIndent_None->trigger();
		return;
	}
}

void TeXDocumentWindow::doFindAgain(bool fromDialog)
{
	if (!fromDialog && researchFindBar && researchFindBar->isVisible()) { researchFind(); return; }
	Tw::Settings settings;
	QString	searchText = settings.value(QString::fromLatin1("searchText")).toString();
	if (searchText.isEmpty())
		return;

	QTextDocument::FindFlags flags = static_cast<QTextDocument::FindFlags>(settings.value(QString::fromLatin1("searchFlags")).toInt());

	QRegularExpression *regex = nullptr;
	if (settings.value(QString::fromLatin1("searchRegex")).toBool()) {
		regex = new QRegularExpression(searchText, ((flags & QTextDocument::FindCaseSensitively) != 0) ? QRegularExpression::NoPatternOption : QRegularExpression::CaseInsensitiveOption);
		if (!regex->isValid()) {
			qApp->beep();
			Research::showTransientStatusMessage(this, tr("Invalid regular expression"), kStatusMessageDuration);
			delete regex;
			return;
		}
	}

	if (fromDialog && (settings.value(QString::fromLatin1("searchFindAll")).toBool() || settings.value(QString::fromLatin1("searchAllFiles")).toBool())) {
		QList<SearchResult> results;
		flags &= ~QTextDocument::FindBackward;

		QList<TeXDocumentWindow*> docsToSearch{this};
		if (settings.value(QString::fromLatin1("searchAllFiles")).toBool()) {
			for (TeXDocumentWindow * theDoc : docList) {
				if (!docsToSearch.contains(theDoc)) {
					docsToSearch.append(theDoc);
				}
			}
		}

		for (TeXDocumentWindow * theDoc : docsToSearch) {
			if (regex) {
				// The iterator advances zero-length matches, including another
				// nonempty alternative at the same position, without revisiting
				// the same source offset forever.
				const QString source = theDoc->textEdit->text();
				auto matches = regex->globalMatch(source);
				while (matches.hasNext()) {
					const auto match = matches.next();
					const auto block = theDoc->textDoc()->findBlock(int(match.capturedStart()));
					results.append(SearchResult(theDoc, block.blockNumber() + 1,
						int(match.capturedStart()) - block.position(), int(match.capturedEnd()) - block.position()));
				}
				continue;
			}
			QTextCursor curs(theDoc->textDoc());
			curs.movePosition(QTextCursor::End);
			int rangeStart = 0;
			int rangeEnd = curs.position();
			while (true) {
				curs = theDoc->doSearch(searchText, regex, flags, rangeStart, rangeEnd);
				if (curs.isNull())
					break;
				int blockStart = curs.block().position();
				results.append(SearchResult(theDoc, curs.blockNumber() + 1,
								curs.selectionStart() - blockStart, curs.selectionEnd() - blockStart));
				if ((flags & QTextDocument::FindBackward) != 0)
					rangeEnd = curs.selectionStart();
				else
					rangeStart = curs.selectionEnd();
			}
		}

		if (results.count() == 0) {
			qApp->beep();
			Research::showTransientStatusMessage(this, tr("Not found"), kStatusMessageDuration);
		}
		else {
			const bool singleFile = (docsToSearch.size() == 1);
			SearchResults::presentResults(searchText, results, this, singleFile);
			Research::showTransientStatusMessage(this, tr("Found %n occurrence(s)", "", static_cast<int>(results.count())), kStatusMessageDuration);
		}
	}
	else {
		QTextCursor	curs = textEdit->textCursor();
		if (settings.value(QString::fromLatin1("searchSelection")).toBool() && curs.hasSelection()) {
			int rangeStart = curs.selectionStart();
			int rangeEnd = curs.selectionEnd();
			curs = doSearch(searchText, regex, flags, rangeStart, rangeEnd);
		}
		else {
			if ((flags & QTextDocument::FindBackward) != 0) {
				int rangeStart = 0;
				int rangeEnd = curs.selectionStart();
				curs = doSearch(searchText, regex, flags, rangeStart, rangeEnd);
				if (curs.isNull() && settings.value(QString::fromLatin1("searchWrap")).toBool()) {
					curs = QTextCursor(textEdit->document());
					curs.movePosition(QTextCursor::End);
					curs = doSearch(searchText, regex, flags, 0, curs.position());
				}
			}
			else {
				int rangeStart = curs.selectionEnd();
				curs.movePosition(QTextCursor::End);
				int rangeEnd = curs.position();
				curs = doSearch(searchText, regex, flags, rangeStart, rangeEnd);
				if (curs.isNull() && settings.value(QString::fromLatin1("searchWrap")).toBool())
					curs = doSearch(searchText, regex, flags, 0, rangeEnd);
			}
		}

		if (curs.isNull()) {
			qApp->beep();
			Research::showTransientStatusMessage(this, tr("Not found"), kStatusMessageDuration);
		}
		else
			textEdit->setTextCursor(curs);
	}

	delete regex;
}

// Expand exactly the numbered references supported by QString::replace, using
// the match in its full source context (lookarounds must not lose that context).
QString TeXDocumentWindow::replacementForMatch(const QRegularExpressionMatch &match, const QString &replacement)
{
	QString result;
	qsizetype last = 0;
	const int captures = match.regularExpression().captureCount();
	for (qsizetype i = 0; i + 1 < replacement.size(); ++i) {
		if (replacement.at(i) != QLatin1Char('\\')) continue;
		int number = replacement.at(i + 1).digitValue();
		if (number <= 0 || number > captures) continue;
		qsizetype length = 2;
		if (i + 2 < replacement.size()) {
			const int second = replacement.at(i + 2).digitValue();
			if (second >= 0 && number * 10 + second <= captures) { number = number * 10 + second; ++length; }
		}
		result += replacement.mid(last, i - last);
		result += match.captured(number);
		last = i + length;
	}
	return result + replacement.mid(last);
}

QString TeXDocumentWindow::replacementInput(QString replacement)
{
	QRegularExpression escapedChar(QStringLiteral("\\\\([nt\\\\]|x([0-9A-Fa-f]{4}))"));
	QString::size_type index = -1;
	QRegularExpressionMatch escapeMatch;
	while ((escapeMatch = escapedChar.match(replacement, index + 1)).hasMatch()) {
		index = escapeMatch.capturedStart();
		QChar ch;
		if (escapeMatch.capturedLength(1) == 1) {
			// single-char escape code newline/tab/backslash
			ch = escapeMatch.captured(1)[0];
			switch (ch.unicode()) {
				case 'n':
				    ch = QChar::fromLatin1('\n');
					break;
				case 't':
				    ch = QChar::fromLatin1('\t');
					break;
				case '\\':
				    ch = QChar::fromLatin1('\\');
					break;
				default:
					// should not happen!
					break;
			}
		}
		else {
			// Unicode char number \xHHHH
			bool ok{false};
			ch = QChar(escapeMatch.captured(2).toUInt(&ok, 16));
		}
		replacement.replace(index, escapeMatch.capturedLength(), ch);
	}
	return replacement;
}

void TeXDocumentWindow::doReplaceAgain()
{
	if(researchFindBar&&researchFindBar->isVisible()){if(auto *button=findChild<QToolButton *>(QStringLiteral("researchReplaceOne")))button->click();return;}
	doReplace(ReplaceDialog::ReplaceOne);
}

void TeXDocumentWindow::doReplace(ReplaceDialog::DialogCode mode)
{
	Tw::Settings settings;

	QString	searchText = settings.value(QString::fromLatin1("searchText")).toString();
	if (searchText.isEmpty())
		return;

	QTextDocument::FindFlags flags = static_cast<QTextDocument::FindFlags>(settings.value(QString::fromLatin1("searchFlags")).toInt());

	QRegularExpression *regex = nullptr;
	if (settings.value(QString::fromLatin1("searchRegex")).toBool()) {
		regex = new QRegularExpression(searchText, ((flags & QTextDocument::FindCaseSensitively) != 0) ? QRegularExpression::NoPatternOption : QRegularExpression::CaseInsensitiveOption);
		if (!regex->isValid()) {
			qApp->beep();
			Research::showTransientStatusMessage(this, tr("Invalid regular expression"), kStatusMessageDuration);
			delete regex;
			return;
		}
	}

	QString	replacement = settings.value(QString::fromLatin1("replaceText")).toString();
	if (regex) replacement = replacementInput(replacement);

	bool allFiles = (mode == ReplaceDialog::ReplaceAll) && settings.value(QString::fromLatin1("searchAllFiles")).toBool();

	bool searchWrap = settings.value(QString::fromLatin1("searchWrap")).toBool();
	bool searchSel = settings.value(QString::fromLatin1("searchSelection")).toBool();

	int rangeStart{0}, rangeEnd{0};
	QTextCursor searchRange = textCursor();
	if (allFiles) {
		searchRange.select(QTextCursor::Document);
		rangeStart = searchRange.selectionStart();
		rangeEnd = searchRange.selectionEnd();
	}
	else if (searchSel) {
		rangeStart = searchRange.selectionStart();
		rangeEnd = searchRange.selectionEnd();
	}
	else {
		// Note: searchWrap is handled separately below
		if ((flags & QTextDocument::FindBackward) != 0) {
			rangeStart = 0;
			rangeEnd = searchRange.selectionEnd();
		}
		else {
			rangeStart = searchRange.selectionStart();
			searchRange.select(QTextCursor::Document);
			rangeEnd = searchRange.selectionEnd();
		}
	}

	if (mode == ReplaceDialog::ReplaceOne) {
		QTextCursor curs = doSearch(searchText, regex, flags, rangeStart, rangeEnd);
		if (curs.isNull() && searchWrap) {
			// If we haven't found anything and wrapping is enabled, try again
			// with a "wrapped" search range
			if ((flags & QTextDocument::FindBackward) != 0) {
				rangeStart = rangeEnd;
				searchRange.select(QTextCursor::Document);
				rangeEnd = searchRange.selectionEnd();
			}
			else {
				rangeEnd = rangeStart;
				rangeStart = 0;
			}
			curs = doSearch(searchText, regex, flags, rangeStart, rangeEnd);
		}
		if (curs.isNull()) {
			qApp->beep();
			Research::showTransientStatusMessage(this, tr("Not found"), kStatusMessageDuration);
		}
		else {
			// do replacement
			QString target;
			if (regex)
				target = replacementForMatch(regex->match(textDoc()->editorText(), curs.selectionStart()), replacement);
			else
				target = replacement;
			curs.insertText(target);
			textEdit->setTextCursor(curs);
		}
	}
	else if (mode == ReplaceDialog::ReplaceAll) {
		if (allFiles) {
			int replacements = 0;
			foreach (TeXDocumentWindow* doc, docList)
				replacements += doc->doReplaceAll(searchText, regex, replacement, flags);
			QString numOccurrences = tr("%n occurrence(s)", "", replacements);
			QString numDocuments = tr("%n documents", "", static_cast<int>(docList.count()));
			QString message = tr("Replaced %1 in %2").arg(numOccurrences, numDocuments);
			Research::showTransientStatusMessage(this, message, kStatusMessageDuration);
		}
		else {
			if (!searchSel) {
				// If we are not searching within a selection, we implicitly
				// search the whole document with ReplaceAll
				searchRange.select(QTextCursor::Document);
				rangeStart = searchRange.selectionStart();
				rangeEnd = searchRange.selectionEnd();
			}
			int replacements = doReplaceAll(searchText, regex, replacement, flags, rangeStart, rangeEnd);
			Research::showTransientStatusMessage(this, tr("Replaced %n occurrence(s)", "", replacements), kStatusMessageDuration);
		}
	}

	delete regex;
}

int TeXDocumentWindow::doReplaceAll(const QString& searchText, QRegularExpression * regex, const QString& replacement,
								QTextDocument::FindFlags flags, int rangeStart, int rangeEnd)
{
	QTextCursor searchRange = textCursor();
	using pos_type = decltype(searchRange.position());
	searchRange.select(QTextCursor::Document);
	if (rangeStart < 0)
		rangeStart = searchRange.selectionStart();
	if (rangeEnd < 0)
		rangeEnd = searchRange.selectionEnd();

	if (regex) {
		// Signals and cached text are coalesced by beginEditBlock(). Match once
		// before editing, then apply from the end to keep every source offset
		// valid. Qt's iterator also advances zero-length matches safely.
		const QString source = textDoc()->editorText();
		struct Replacement { int start, end; QString text; };
		QList<Replacement> matches;
		auto iterator = regex->globalMatch(source, rangeStart);
		int delta = 0;
		while (iterator.hasNext()) {
			const auto match = iterator.next();
			if (match.capturedEnd() > rangeEnd) break;
			const QString target = replacementForMatch(match, replacement);
			matches.append({int(match.capturedStart()), int(match.capturedEnd()), target});
			delta += int(target.size()) - int(match.capturedLength());
		}
		if (matches.isEmpty()) return 0;
		const int finalPosition = (flags & QTextDocument::FindBackward) ? rangeStart : matches.last().end + delta;
		searchRange.beginEditBlock();
		for (auto i = matches.size(); i-- > 0;) {
			const auto &match = matches.at(i);
			searchRange.setPosition(match.start);
			searchRange.setPosition(match.end, QTextCursor::KeepAnchor);
			searchRange.insertText(match.text);
		}
		searchRange.endEditBlock();
		searchRange.setPosition(finalPosition);
		textEdit->setTextCursor(searchRange);
		return int(matches.size());
	}

	int replacements = 0;
	bool first = true;
	while (true) {
		QTextCursor curs = doSearch(searchText, regex, flags, rangeStart, rangeEnd);
		if (curs.isNull()) {
			if (!first)
				searchRange.endEditBlock();
			break;
		}
		if (first) {
			searchRange.beginEditBlock();
			first = false;
		}
		QString target;
		pos_type oldLen = curs.selectionEnd() - curs.selectionStart();
		if (regex)
			target = textEdit->text().mid(curs.selectionStart(), oldLen).replace(*regex, replacement);
		else
			target = replacement;
		pos_type newLen = static_cast<pos_type>(target.length());
		if ((flags & QTextDocument::FindBackward) != 0)
			rangeEnd = curs.selectionStart();
		else {
			rangeStart = curs.selectionEnd() - oldLen + newLen;
			rangeEnd += newLen - oldLen;
		}
		searchRange.setPosition(curs.selectionStart());
		searchRange.setPosition(curs.selectionEnd(), QTextCursor::KeepAnchor);
		searchRange.insertText(target);
		++replacements;
	}
	if (!first) {
		searchRange.setPosition(rangeStart);
		textEdit->setTextCursor(searchRange);
	}
	return replacements;
}

QTextCursor TeXDocumentWindow::doSearch(const QString& searchText, const QRegularExpression * regex, QTextDocument::FindFlags flags, int s, int e)
{
	QTextCursor curs;
	using pos_type = decltype(curs.position());
	QTextDocument * theDoc = textEdit->document();
	// Literal searches use QTextDocument directly and need no full-text copy.
	const QString docText = regex ? textDoc()->editorText() : QString();

	if ((flags & QTextDocument::FindBackward) != 0) {
		if (regex) {
			// this doesn't seem to match \n or even \x2029 for newline
			// curs = theDoc->find(*regex, e, flags);
			QRegularExpressionMatch m;
#if QT_VERSION >= 0x050500
			QString::size_type offset = docText.lastIndexOf(*regex, e, &m);
#else
			int offset = docText.lastIndexOf(*regex, e);
			if (offset >= 0)
				m = regex->match(docText, offset);
#endif
			while (offset >= s && m.capturedEnd() > e) {
				// QString's negative offset starts again from the document end.
				// A match at zero crossing the range must terminate the search.
				if (offset == 0) { offset = -1; break; }
#if QT_VERSION >= 0x050500
				offset = docText.lastIndexOf(*regex, offset - 1, &m);
#else
				offset = docText.lastIndexOf(*regex, offset - 1);
				if (offset >= 0)
					m = regex->match(docText, offset);
#endif
			}
			if (offset >= s) {
				curs = QTextCursor(textEdit->document());
				curs.setPosition(static_cast<pos_type>(m.capturedStart()));
				curs.setPosition(static_cast<pos_type>(m.capturedEnd()), QTextCursor::KeepAnchor);
			}
		}
		else {
			curs = theDoc->find(searchText, e, flags);
			if (!curs.isNull()) {
				if (curs.selectionEnd() > e)
					curs = theDoc->find(searchText, curs, flags);
				if (curs.selectionStart() < s)
					curs = QTextCursor();
			}
		}
	}
	else {
		if (regex) {
			// this doesn't seem to match \n or even \x2029 for newline
			// curs = theDoc->find(*regex, s, flags);
			QRegularExpressionMatch m = regex->match(docText, s);
			if (m.hasMatch()) {
				curs = QTextCursor(theDoc);
				curs.setPosition(static_cast<pos_type>(m.capturedStart()));
				curs.setPosition(static_cast<pos_type>(m.capturedEnd()), QTextCursor::KeepAnchor);
			}
		}
		else {
			curs = theDoc->find(searchText, s, flags);
		}
		if (curs.selectionEnd() > e)
			curs = QTextCursor();
	}
	return curs;
}

void TeXDocumentWindow::copyToFind()
{
	if (textEdit->textCursor().hasSelection()) {
		QString searchText = textEdit->textCursor().selectedText();
		searchText.replace(QChar::ParagraphSeparator, QChar::LineFeed); // 0x2029 -> \n

		Tw::Settings settings;
		// Note: To search for multi-line strings, we currently need regex
		// enabled (since we only have a single search line). If it was not
		// enabled, we also need to ensure that the replaceText is escaped
		// properly
		const bool isMultiLine = searchText.contains(QChar::LineFeed);
		if (isMultiLine && !settings.value(QString::fromLatin1("searchRegex")).toBool()) {
			settings.setValue(QString::fromLatin1("searchRegex"), true);
			settings.setValue(QString::fromLatin1("replaceText"), QRegularExpression::escape(settings.value(QString::fromLatin1("replaceText")).toString()));
		}
		if (settings.value(QString::fromLatin1("searchRegex")).toBool()) {
			if (isMultiLine)
				settings.setValue(QString::fromLatin1("searchText"), QRegularExpression::escape(searchText).replace(QStringLiteral("\\\n"), QStringLiteral("\\n")));
			else
				settings.setValue(QString::fromLatin1("searchText"), QRegularExpression::escape(searchText));
		}
		else
			settings.setValue(QString::fromLatin1("searchText"), searchText);
	}
}

void TeXDocumentWindow::copyToReplace()
{
	if (textEdit->textCursor().hasSelection()) {
		QString replaceText = textEdit->textCursor().selectedText();
		replaceText.replace(QChar(0x2029), QChar::fromLatin1('\n'));
		Tw::Settings settings;
		// Note: To do multi-line replacements, we currently need regex enabled
		// (since we only have a single replace line). If it was not enabled, we
		// also need to ensure that the searchText is escaped properly
		bool isMultiLine = replaceText.contains(QChar::fromLatin1('\n'));
		if (isMultiLine && !settings.value(QString::fromLatin1("searchRegex")).toBool()) {
			settings.setValue(QString::fromLatin1("searchRegex"), true);
			settings.setValue(QString::fromLatin1("searchText"), QRegularExpression::escape(settings.value(QString::fromLatin1("searchText")).toString()));
		}
		if (settings.value(QString::fromLatin1("searchRegex")).toBool()) {
			if (isMultiLine)
				settings.setValue(QString::fromLatin1("replaceText"), QRegularExpression::escape(replaceText).replace(QChar::fromLatin1('\n'), QLatin1String("\\n")));
			else
				settings.setValue(QString::fromLatin1("replaceText"), QRegularExpression::escape(replaceText));
		}
		else
			settings.setValue(QString::fromLatin1("replaceText"), replaceText);
	}
}

void TeXDocumentWindow::findSelection()
{
	copyToFind();
	doFindAgain();
}

void TeXDocumentWindow::showSelection()
{
	int oldScrollValue = -1;
	if (textEdit->verticalScrollBar())
		oldScrollValue = textEdit->verticalScrollBar()->value();
	textEdit->ensureCursorVisible();
	maybeCenterSelection(oldScrollValue);
}

void TeXDocumentWindow::typeset()
{
	flushScriptEdits();
	if (untitled() && textDoc()->isEmpty()) return;
	if (process || researchBuildPreparing)
		return;	// this shouldn't happen if we disable the command at the right time
	if (!Research::commitSlideNotes(this)) return;
	Research::cancelBuildOutput(this);
	if (!researchRetryRequested) researchBuildRetries = 0;
	researchRetryRequested = false;
	++researchBuildSerial;

	if (untitled() || textEdit->document()->isModified()) {
		if (!save()) {
			Research::showTransientStatusMessage(this, tr("Cannot process unsaved document"), kStatusMessageDuration);
			return;
		}
	}

	QString rootFilePath = textDoc()->getRootFilePath();
	if (!textDoc()->hasModeLine(QStringLiteral("root")) && rootFilePath == textDoc()->absoluteFilePath()) {
		const auto candidates = Research::rootCandidates(rootFilePath, textDoc()->toPlainText());
		if (candidates.size() > 1) {
			QStringList names;
			for (const auto &candidate : candidates) names.append(QDir(QFileInfo(rootFilePath).absolutePath()).relativeFilePath(candidate));
			bool accepted = false;
			const QString choice = QInputDialog::getItem(this, Research::ui(QStringLiteral("选择主文档")), Research::ui(QStringLiteral("发现多个包含当前文件的论文。选择一次，之后会自动使用：")), names, 0, false, &accepted);
			if (!accepted) return;
			rootFilePath = candidates[names.indexOf(choice)];
			Research::rememberRoot(textDoc()->absoluteFilePath(), rootFilePath);
		}
	}
	if (!saveFilesHavingRoot(rootFilePath))
		return;

	QFileInfo fileInfo(rootFilePath);
	if (!fileInfo.isReadable()) {
		Research::showTransientStatusMessage(this, tr("Root document %1 is not readable").arg(rootFilePath), kStatusMessageDuration);
		return;
	}

	// A chapter inherits the main paper's declared engine unless it declares its own.
	if (rootFilePath != textDoc()->absoluteFilePath() && !textDoc()->hasModeLine(QStringLiteral("program"))) {
		QFile root(rootFilePath);
		if (root.open(QIODevice::ReadOnly)) {
			QRegularExpression declaration(QStringLiteral(R"(^\s*%\s*!\s*TEX\s+program\s*=\s*([^\r\n]+))"), QRegularExpression::MultilineOption | QRegularExpression::CaseInsensitiveOption);
			const auto m = declaration.match(QString::fromUtf8(root.read(8192)));
			const int index = engine->findText(Research::canonicalEngineName(m.captured(1).trimmed()), Qt::MatchFixedString);
			if (m.hasMatch() && index >= 0) engine->setCurrentIndex(index);
		}
	}
	Engine e = TWApp::instance()->getNamedEngine(engine->currentText());
	if (!e.isAvailable()) {
		Research::showTransientStatusMessage(this, tr("%1 is not properly configured").arg(engine->currentText()), kStatusMessageDuration);
		return;
	}

	researchBuildPreparing=true;setProperty("researchBuildPreparing",true);
	QPointer<TeXDocumentWindow> reservationGuard(this);const bool reserved=TWApp::instance()->typesetManager().startTypesetting(rootFilePath,this);
	if(!reservationGuard)return;
	if (!reserved) {
		researchBuildPreparing=false;setProperty("researchBuildPreparing",false);
		Research::showTransientStatusMessage(this, tr("%1 is already being processed").arg(rootFilePath), kStatusMessageDuration);updateTypesettingAction();return;
	}
	if(!researchBuildPreparing)return;
	setProperty("researchBuildRoot",rootFilePath);
	Research::setDependencyBinaryPaths(TWApp::instance()->getBinaryPaths());
	QHash<QString,QString> sources;
	for(auto *document:documentList())if(!document->untitled())sources.insert(Tw::Utils::canonicalSourcePath(document->fileName()),document->textDoc()->sourceText());
	const auto epoch=researchPrepareEpoch;const auto ticket=epoch->fetch_add(1)+1;QPointer<TeXDocumentWindow> guard(this);
	statusBar()->showMessage(Research::ui(QStringLiteral("正在准备编译…")));
	buildPreparationPool().start(QRunnable::create([guard,epoch,ticket,rootFilePath,e,sources]{
		auto cancelled=[epoch,ticket]{return epoch->load()!=ticket;};const auto job=Research::prepareBuild(rootFilePath,e,sources,cancelled);if(!qApp)return;
		QMetaObject::invokeMethod(qApp,[guard,epoch,ticket,rootFilePath,e,job]{
			if(!guard||epoch->load()!=ticket)return;guard->researchBuildPreparing=false;guard->setProperty("researchBuildPreparing",false);
			if(job.cancelled|| (job.managed&&job.outputDir.isEmpty())){TWApp::instance()->typesetManager().stopTypesetting(guard);Research::showTransientStatusMessage(guard,Research::ui(QStringLiteral("无法创建本机编译缓存，请检查安装目录的写入权限。")),7000);return;}
			guard->researchBuild=job;guard->startPreparedTypesetting(rootFilePath,e);
		},Qt::QueuedConnection);
	}));
}

void TeXDocumentWindow::startPreparedTypesetting(const QString &rootFilePath, Engine e)
{
	const QFileInfo fileInfo(rootFilePath);
	QString pdfName;
	if (getPreviewFileName(pdfName))
		oldPdfTime = QFileInfo(pdfName).lastModified();
	else
		oldPdfTime = QDateTime();

	// Stop watching the pdf document while it is being changed to avoid
	// interference
	if (pdfDoc && pdfDoc->widget())
		pdfDoc->widget()->setWatchForDocumentChangesOnDisk(false);

	process = e.run(fileInfo, this, researchBuild.outputDir);
	setProperty("researchBuildRoot", rootFilePath);
	if (researchProblemsAction) researchProblemsAction->setVisible(false);
	Research::setBuildIssueCounts(this,0,0);if(pdfDoc)Research::setBuildIssueCounts(pdfDoc,0,0);

	if (process) {
		if(researchBuild.managed)statusBar()->clearMessage();
		Research::buildOutputStarted(this, researchBuild);
		Research::slideBuildStarted(this);
		textEdit_console->setProcess(process);
		// Keep both the reading position and keyboard focus while compiling.
		for (int i = consoleTabs->count() - 1; i > 0; --i) {
			QWidget *tab = consoleTabs->widget(i); consoleTabs->removeTab(i); tab->deleteLater();
		}
		consoleTabs->setTabText(0, Research::ui(QStringLiteral("日志")));
		if (!keepConsoleOpen) hideConsole();
		else inputLine->show();
		if(!researchBuild.managed)statusBar()->showMessage(Research::ui(QStringLiteral("正在编译 · %1")).arg(fileInfo.fileName()));
		if (pdfDoc) pdfDoc->statusBar()->showMessage(Research::ui(QStringLiteral("正在编译 · %1")).arg(fileInfo.fileName()));
		showPdfWhenFinished = e.showPdf();
		userInterrupt = false;

#if QT_VERSION < QT_VERSION_CHECK(5, 6, 0)
		connect(process, static_cast<void (QProcess::*)(QProcess::ProcessError)>(&QProcess::error), this, &TeXDocumentWindow::processError);
#else
		connect(process, &QProcess::errorOccurred, this, &TeXDocumentWindow::processError);
#endif
		connect(process, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), this, &TeXDocumentWindow::processFinished);
	}
	else {
		// Since the process didn't run, restart watching the output immediately
		if (pdfDoc && pdfDoc->widget())
			pdfDoc->widget()->setWatchForDocumentChangesOnDisk(!Research::managedPreview(pdfDoc->fileName()));

		TWApp::instance()->typesetManager().stopTypesetting(this);

#ifdef Q_OS_WIN
		Research::openEnvironmentCheck(this,e.program());
#else
		QMessageBox msgBox(QMessageBox::Critical, tr("Unable to execute %1").arg(e.name()),
		                      QLatin1String("<p>") + tr("The program \"%1\" was not found.").arg(e.program()) + QLatin1String("</p>") +
#if defined(Q_OS_WIN)
		                      QLatin1String("<p>") + tr("You need a <b>TeX distribution</b> like <a href=\"https://tug.org/texlive/\">TeX Live</a> or <a href=\"https://miktex.org/\">MiKTeX</a> installed on your system to typeset your document.") + QLatin1String("</p>") +
#elif defined(Q_OS_DARWIN)
		                      QLatin1String("<p>") + tr("You need a <b>TeX distribution</b> like <a href=\"https://tug.org/mactex/\">MacTeX</a> installed on your system to typeset your document.") + QLatin1String("</p>") +
#else // defined(Q_OS_UNIX) && !defined(Q_OS_DARWIN)
		                      QLatin1String("<p>") + tr("You need a <b>TeX distribution</b> like <a href=\"https://tug.org/texlive/\">TeX Live</a> installed on your system to typeset your document. On most systems such a TeX distribution is available as prebuilt package.") + QLatin1String("</p>") +
#endif
		                      QLatin1String("<p>") + tr("When a TeX distribution is installed you may need to tell TeXworks where to find it in Edit -> Preferences -> Typesetting.") + QLatin1String("</p>"),
							  QMessageBox::Cancel, this);
		msgBox.setDetailedText(
		                      tr("Searched in directories:") + QChar::fromLatin1('\n') +
							  QLatin1String(" * ") + Engine::binPaths().join(QLatin1String("\n * ")) + QChar::fromLatin1('\n') +
							  tr("Check the configuration of the %1 tool and the path settings in the Preferences dialog.").arg(e.name()));
		msgBox.exec();
#endif
	}
}

void TeXDocumentWindow::interrupt()
{
	if(researchBuildFinishing){userInterrupt=true;if(researchFinishCancelled)researchFinishCancelled->store(true);Research::showTransientStatusMessage(this,Research::ui(QStringLiteral("编译已停止")),5000);return;}
	if(researchBuildPreparing){researchPrepareEpoch->fetch_add(1);researchBuildPreparing=false;setProperty("researchBuildPreparing",false);userInterrupt=true;TWApp::instance()->typesetManager().stopTypesetting(this);Research::showTransientStatusMessage(this,Research::ui(QStringLiteral("编译已停止")),5000);return;}
	if (process) {
		userInterrupt = true;
		Engine::stop(process);

		// Start watching for changes in the pdf (again)
		if (pdfDoc && pdfDoc->widget())
			pdfDoc->widget()->setWatchForDocumentChangesOnDisk(!Research::managedPreview(pdfDoc->fileName()));

		// Don't notify the TypesetManager that typesetting was stopped; this is
		// delegated to processError() which will be called after process->kill()
		// and which takes care of resetting the process before notifying the
		// TypesetManager. This ensures that subsequent calls to isTypesetting()
		// return the correct value
	}
}

void TeXDocumentWindow::goToTypesettingWindow()
{
	TeXDocumentWindow * owner = qobject_cast<TeXDocumentWindow*>(TWApp::instance()->typesetManager().getOwnerForRootFile(textDoc()->getRootFilePath()));
	if (owner) {
		owner->raise();
		owner->activateWindow();
	}
}

void TeXDocumentWindow::updateTypesettingAction()
{
	TeXDocumentWindow * owner = qobject_cast<TeXDocumentWindow*>(TWApp::instance()->typesetManager().getOwnerForRootFile(textDoc()->getRootFilePath()));
	actionTypeset->setEnabled(isTypesetting() || owner != nullptr || !(untitled() && textDoc()->isEmpty()));

	disconnect(actionTypeset, &QAction::triggered, this, nullptr);
	if (isTypesetting()) {
		// We are currently running a typesetting process
		// The button should allow the user to stop it
		actionTypeset->setIcon(QIcon::fromTheme(QStringLiteral("process-stop")));
		actionTypeset->setText(tr("Stop Compile"));
		connect(actionTypeset, &QAction::triggered, this, &TeXDocumentWindow::interrupt);
	}
	else if (owner != nullptr) {
		// Someone else is typesetting "our" (root) document
		// The button should take the user to the window from which the process
		// was run
		actionTypeset->setIcon(QIcon::fromTheme(QStringLiteral("go-jump")));
		actionTypeset->setText(tr("Go to Compilation"));
		connect(actionTypeset, &QAction::triggered, this, &TeXDocumentWindow::goToTypesettingWindow);
	}
	else {
		// No process is currently running (for "our" root document)
		// The button should allow the user to start one
		actionTypeset->setIcon(QIcon::fromTheme(QStringLiteral("process-start")));
		actionTypeset->setText(tr("Compile"));
		connect(actionTypeset, &QAction::triggered, this, &TeXDocumentWindow::typeset);
	}
    // Keep the toolbar concise; menus/tooltips retain their descriptive action text.
    if (auto *button = qobject_cast<QToolButton *>(toolBar_run->widgetForAction(actionTypeset))) {
        button->setProperty("researchCompileBusy", isTypesetting() || owner != nullptr);
        button->setText(isTypesetting() ? Research::ui(QStringLiteral("停止")) : owner ? Research::ui(QStringLiteral("转到")) : Research::ui(QStringLiteral("编译")));
        Research::refreshCompileButton(button);
    }
}

void TeXDocumentWindow::conditionallyEnableRemoveAuxFiles()
{
	// In the following cases, we want to disable "Remove Aux Files"
	// 1) There cannot be any aux files (as there is no root file; i.e., the
	//    file has never been saved)
	// 2) A typesetting process is running for "our" root file which may be
	//    accessing the aux files
	const bool enable = [&](){
		QFileInfo rootFileInfo{textDoc()->getRootFilePath()};
		if (!rootFileInfo.exists())
			return false;
		if (TWApp::instance()->typesetManager().isFileBeingTypeset(rootFileInfo.canonicalFilePath()))
			return false;
		return true;
	}();
	actionRemove_Aux_Files->setEnabled(enable);
}

void TeXDocumentWindow::processError(QProcess::ProcessError /*error*/)
{
	if (!process) return;
	if(researchBuildFinishing){if(researchFinishCancelled)researchFinishCancelled->store(true);return;}
	const QString detail = process->errorString();
	textEdit_console->flushOutput();
	if (userInterrupt)
		textEdit_console->append(tr("Process interrupted by user"));
	else
		textEdit_console->append(process->errorString());
	Engine::stop(process);
	textEdit_console->setProcess(nullptr, false);
	setProperty("researchOutputCancelled", userInterrupt);
	Research::buildOutputFinished(this, researchBuild, true, 0);
	Research::slideBuildFinished(this, false, true);
	process->deleteLater();
	process = nullptr;
	inputLine->hide();
	if (!userInterrupt) {
		auto *summary = new QTextBrowser(this);
		summary->setObjectName(QStringLiteral("researchProblems"));
		Research::setLocalizedHtml(summary,
			Research::ui(QStringLiteral("<h3>编译未完成</h3><p>%1</p><p>完整输出可在“日志”中查看。</p>"), false).arg(detail.toHtmlEscaped()),
			Research::ui(QStringLiteral("<h3>编译未完成</h3><p>%1</p><p>完整输出可在“日志”中查看。</p>"), true).arg(detail.toHtmlEscaped()));
		consoleTabs->addTab(summary, Research::ui(QStringLiteral("需要处理")));
		consoleTabs->setCurrentWidget(summary); showConsole(); inputLine->hide();
		statusBar()->showMessage(Research::ui(QStringLiteral("编译未完成 · %1")).arg(detail));
	} else Research::showTransientStatusMessage(this, Research::ui(QStringLiteral("编译已停止")), 5000);

	// Start watching for changes in the pdf (again)
	if (pdfDoc && pdfDoc->widget())
		pdfDoc->widget()->setWatchForDocumentChangesOnDisk(!Research::managedPreview(pdfDoc->fileName()));

	TWApp::instance()->typesetManager().stopTypesetting(this);
}

void TeXDocumentWindow::processFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
	if (!process || researchBuildFinishing) return;
	// Start watching for changes in the pdf (again)
	if (pdfDoc && pdfDoc->widget())
		pdfDoc->widget()->setWatchForDocumentChangesOnDisk(!Research::managedPreview(pdfDoc->fileName()));

	textEdit_console->flushOutput();
	textEdit_console->setProcess(nullptr, false);

	const bool failed = exitCode != 0 || exitStatus == QProcess::CrashExit || userInterrupt;
	if(failed||!researchBuild.managed){completeTypesetting(failed,Research::finishBuild(researchBuild,!failed));return;}
	researchBuildFinishing=true;setProperty("researchBuildFinishing",true);
	researchFinishCancelled=std::make_shared<std::atomic_bool>(false);
	const auto cancellation=researchFinishCancelled;const auto state=std::make_shared<FinishSourceState>();
	const auto connections=std::make_shared<QList<QMetaObject::Connection>>();
	const auto watched=std::make_shared<QSet<Tw::Document::TeXDocument *>>();
	auto watch=[this,state,connections,watched]{
		for(auto *window:documentList())if(!window->untitled()&&!watched->contains(window->textDoc())){
			watched->insert(window->textDoc());const auto id=reinterpret_cast<quintptr>(window->textDoc());QPointer<TeXDocumentWindow> document(window);
			state->update(id,Tw::Utils::canonicalSourcePath(window->fileName()),window->isModified());
			connections->append(connect(window->textDoc(),&QTextDocument::modificationChanged,this,[state,id,document](bool modified){if(document)state->update(id,Tw::Utils::canonicalSourcePath(document->fileName()),modified);}));
			connections->append(connect(window->textDoc(),&QObject::destroyed,this,[state,id]{state->retire(id);}));
		}
	};
	watch();connections->append(connect(TWApp::instance(),&TWApp::windowListChanged,this,watch));
	const auto job=researchBuild;QPointer<TeXDocumentWindow> guard(this);
	buildPreparationPool().start(QRunnable::create([guard,job,state,cancellation,connections]{
		const int result=Research::finishBuildInBackground(job,true,[state,cancellation](const QStringList &paths){return cancellation->load()||state->changed(paths);});
		QMetaObject::invokeMethod(qApp,[guard,job,state,cancellation,connections,result]{
			for(const auto &connection:*connections)QObject::disconnect(connection);
			if(!guard)return;guard->researchBuildFinishing=false;guard->setProperty("researchBuildFinishing",false);
			const bool lateChange=cancellation->load()||state->changed(Research::currentBuildMetadata(job.root).value(QStringLiteral("inputs")).toObject().keys())||Tw::Utils::canonicalSourcePath(guard->getRootFilePath())!=job.root;
			guard->completeTypesetting(guard->userInterrupt,result==1&&lateChange?-1:result);
		},Qt::QueuedConnection);
	}));
}

void TeXDocumentWindow::completeTypesetting(bool failed,int promoted)
{
  QPointer<TeXDocumentWindow> lifetime(this);
  if (researchCollaboration) researchCollaboration->buildFinished();
	if (!failed && (!researchBuild.managed || promoted == 1)) {
		QString pdfName;
		if (getPreviewFileName(pdfName)) {
			actionGo_to_Preview->setEnabled(true);
			if (QFileInfo(pdfName).lastModified() != oldPdfTime) {
				// only open/refresh the PDF if it was changed by the typeset process
				if (!pdfDoc || pdfName != pdfDoc->fileName()) {
					if (pdfDoc && Research::managedPreview(pdfName)) pdfDoc->adoptPreviewFile(pdfName);
					else if (showPdfWhenFinished && openPdfIfAvailable(false)) { if(!property("researchSlideMode").toBool())pdfDoc->show(); }
				}
				else {
					pdfDoc->reload(); // always reload if it is loaded, we don't want a stale window
				}
			}
		}
		else
			actionGo_to_Preview->setEnabled(true);
	}

	executeAfterTypesetHooks();
	if(!lifetime)return;

	const auto issues = Research::compileIssues(property("researchBuildRoot").toString(), consoleText(), failed, researchBuild.logFile);
	inputLine->hide();
	if (!issues.isEmpty() && !userInterrupt) {
		auto *summary = new QTextBrowser(this);
		summary->setObjectName(QStringLiteral("researchProblems"));
		summary->setOpenLinks(false);
		auto summaryHtml = [&](bool english, bool expanded) {
		QString html = QStringLiteral("<div style=\"font-family:'Microsoft YaHei UI';font-size:11pt\"><h3 style=\"margin-top:0;margin-bottom:8px\">%1</h3>").arg(Research::ui(failed ? QStringLiteral("编译未完成") : QStringLiteral("编译完成，有需要检查的提示"), english));
		const int count = failed && !expanded ? qMin(1, int(issues.size())) : int(issues.size());
		for (int index = 0; index < count; ++index) {
			const auto &issue = issues[index];
			auto location = [&](int line, int column = 0) {
			const QString caption = QFileInfo(issue.file).fileName().toHtmlEscaped() + (line > 0 ? QStringLiteral(" : %1").arg(line) : QString());
			if (QFileInfo(issue.file).isFile()) {
				QUrl url = QUrl::fromLocalFile(issue.file); url.setFragment(QString::number(line));
				if (column > 0) url.setQuery(QStringLiteral("column=%1").arg(column));
				return QStringLiteral("<a href=\"%1\">%2</a>").arg(url.toString().toHtmlEscaped(), caption);
			}
			return caption;
			};
			const bool suspected = issue.suspectLine > 0;
			const QString label = suspected ? (english ? QStringLiteral("Check first") : QStringLiteral("先检查")) : Research::ui(issue.error ? QStringLiteral("错误") : QStringLiteral("提示"), english);
			html += QStringLiteral("<p><b>%1</b> · %2</p>").arg(label, location(suspected ? issue.suspectLine : issue.line, issue.suspectColumn));
			const QString explanation = Research::issueExplanation(issue, english);
			if (!explanation.isEmpty()) html += QStringLiteral("<p>%1</p>").arg(explanation.toHtmlEscaped());
			QString detail = issue.message.toHtmlEscaped();
			if (suspected) detail.prepend(QStringLiteral("%1 · %2<br>")
				.arg(english ? QStringLiteral("Detected at") : QStringLiteral("编译器检测位置"), location(issue.line)));
			html += QStringLiteral("<p style=\"color:#647184\">%1</p>").arg(detail);
		}
		if (count < issues.size()) html += QStringLiteral("<p><a href=\"research-errors:show\">%1</a></p>")
			.arg((english ? QStringLiteral("Show %1 further diagnostics") : QStringLiteral("展开后续 %1 条编译信息")).arg(issues.size()-count));
		return html + QStringLiteral("</div>");
		};
		Research::setLocalizedHtml(summary, summaryHtml(false, false), summaryHtml(true, false));
		const QString expandedChinese = summaryHtml(false, true), expandedEnglish = summaryHtml(true, true);
		connect(summary, &QTextBrowser::anchorClicked, this, [this, summary, expandedChinese, expandedEnglish](const QUrl &url) {
			if (url.scheme() == QStringLiteral("research-errors")) {
				Research::setLocalizedHtml(summary, expandedChinese, expandedEnglish);
				return;
			}
			if (!url.isLocalFile()) return;
			if (!QFileInfo(url.toLocalFile()).isFile()) {
				Research::showTransientStatusMessage(this, Research::ui(QStringLiteral("源文件已移动或删除，请重新编译更新错误位置。")), 7000);
				return;
			}
			const int column = QUrlQuery(url).queryItemValue(QStringLiteral("column")).toInt();
			TeXDocumentWindow::openDocument(url.toLocalFile(), true, true, url.fragment().toInt(), column > 0 ? column-1 : -1, column > 0 ? column : -1);
		});
		consoleTabs->addTab(summary, Research::ui(QStringLiteral("需要处理（%1）")).arg(issues.size()));
		consoleTabs->setCurrentWidget(summary);
		if (failed || keepConsoleOpen) { showConsole(); splitter->setSizes({qMax(300, height() - 340), 220}); }
		else hideConsole();
		inputLine->hide();
		if (researchProblemsAction) {
			int errors=0;for(const auto &issue:issues)errors+=issue.error;
			const int warnings=int(issues.size())-errors;
			Research::setBuildIssueCounts(this,errors,warnings);if(pdfDoc)Research::setBuildIssueCounts(pdfDoc,errors,warnings);
			researchProblemsAction->setText(errors?Research::ui(QStringLiteral("%1 个错误")).arg(errors):Research::ui(QStringLiteral("%1 条警告")).arg(warnings));
			researchProblemsAction->setToolTip(Research::ui(QStringLiteral("%1 个错误，%2 条警告 · 查看详情")).arg(errors).arg(warnings));
			researchProblemsAction->setVisible(true);
		}
		const QString sourceNotice = Research::ui(QStringLiteral("%1 · %2 条需要处理的信息")).arg(failed ? Research::ui(QStringLiteral("编译失败")) : Research::ui(QStringLiteral("编译完成"))).arg(issues.size());
		const QString pdfNotice = Research::ui(QStringLiteral("%1 · %2 条提示，返回源码查看")).arg(failed ? Research::ui(QStringLiteral("编译失败")) : Research::ui(QStringLiteral("编译完成"))).arg(issues.size());
		if (failed) {
			if(!researchBuild.managed)statusBar()->showMessage(sourceNotice);
			if (pdfDoc) pdfDoc->statusBar()->showMessage(pdfNotice);
		} else {
			if(!researchBuild.managed)Research::showTransientStatusMessage(this, sourceNotice);
			if (pdfDoc) Research::showTransientStatusMessage(pdfDoc, pdfNotice);
		}
	} else {
		if (researchProblemsAction) researchProblemsAction->setVisible(false);
		Research::setBuildIssueCounts(this,0,0);if(pdfDoc)Research::setBuildIssueCounts(pdfDoc,0,0);
		if (!keepConsoleOpen) hideConsole();
		if(!researchBuild.managed)Research::showTransientStatusMessage(this, userInterrupt ? Research::ui(QStringLiteral("编译已停止")) : Research::ui(QStringLiteral("编译完成")));
		if (pdfDoc) Research::showTransientStatusMessage(pdfDoc, userInterrupt ? Research::ui(QStringLiteral("编译已停止")) : Research::ui(QStringLiteral("编译完成")));
	}

	if (promoted < 0 && !failed) {
		const bool unchanged = promoted == -1 && Research::buildContentUnchanged(researchBuild);
		const bool retry = unchanged && researchBuildRetries == 0;
		const QString message = retry ? Research::ui(QStringLiteral("正在重新核对编译；当前编辑内容已保留。")) :
			promoted == -1 ? Research::ui(unchanged ? QStringLiteral("编译期间输入版本无法确认；保留上一版 PDF，请重新编译。") : QStringLiteral("编译期间源码已更新；保留上一版 PDF，请重新编译。")) : Research::ui(QStringLiteral("预览文件更新失败；保留上一版 PDF。"));
		Research::showTransientStatusMessage(this, message, 8000);
		if (retry) {
			++researchBuildRetries;
			const auto job = researchBuild;
			const auto serial = researchBuildSerial;
			// A fresh build certifies the current inputs. Do not accept a PDF
			// made from an intermediate edit that was subsequently restored.
			QTimer::singleShot(0, this, [this, job, serial] {
				if (serial != researchBuildSerial || process || QFileInfo(textDoc()->getRootFilePath()).canonicalFilePath() != job.root || !Research::buildContentUnchanged(job)) return;
				researchRetryRequested = true; typeset();
			});
		}
	}
	if (researchCollaboration) researchCollaboration->fileChanged();
	for (auto *viewer : PDFDocumentWindow::documentList()) viewer->refreshResearchRevision();
	setProperty("researchOutputCancelled", userInterrupt);
	Research::buildOutputFinished(this, researchBuild, failed, promoted);
	Research::slideBuildFinished(this, !failed && (!researchBuild.managed || promoted == 1), failed || userInterrupt);

	if (process)
		process->deleteLater();
	process = nullptr;

	TWApp::instance()->typesetManager().stopTypesetting(this);
}

void TeXDocumentWindow::executeAfterTypesetHooks()
{
	TWScriptManager * scriptManager = TWApp::instance()->getScriptManager();

	for (int i = consoleTabs->count() - 1; i > 0; --i) {
		QWidget *tab = consoleTabs->widget(i); consoleTabs->removeTab(i); tab->deleteLater();
	}

	foreach (Tw::Scripting::ScriptObject *so, scriptManager->getHookScripts(QString::fromLatin1("AfterTypeset"))) {
		QVariant result;
		QFile original(QStringLiteral(":/resfiles/scripts/Hooks/logParser.js")),installed(so->getFilename());
		const bool stock=original.open(QIODevice::ReadOnly)&&installed.open(QIODevice::ReadOnly)&&original.size()==installed.size()&&original.readAll()==installed.readAll();
		QObject snapshot;snapshot.setProperty("fileName",fileName());
		if(stock)snapshot.setProperty("rootFileName",getRootFilePath());
		Tw::Scripting::ScriptAPI api(so,qApp,this,result);
		LogSnapshotAPI logApi(so,qApp,this,result,&snapshot);
		QPointer<TeXDocumentWindow> lifetime(this);
		bool success = stock?so->run(logApi):so->run(api);
		if(!lifetime)return;
		if (success && !result.isNull()) {
			QString res = result.toString();
			if (res.startsWith(QLatin1String("<html>"), Qt::CaseInsensitive)) {
				QTextBrowser *browser = new QTextBrowser(this);
				// Use console font (which is customizable)
				browser->setFont(textEdit_console->font());
				browser->setOpenLinks(false);
				connect(browser, &QTextBrowser::anchorClicked, this, &TeXDocumentWindow::anchorClicked);
				browser->setHtml(res);
				browser->setTextInteractionFlags(Qt::LinksAccessibleByKeyboard | Qt::LinksAccessibleByMouse | Qt::TextBrowserInteraction | Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);
				consoleTabs->addTab(browser, (so->getTitle()==QStringLiteral("Errors, warnings, badboxes")&&!Research::englishUi()?QStringLiteral("错误、警告与排版提示"):so->getTitle()));
			}
			else {
				QTextEdit *textEdit = new QTextEdit(this);
				textEdit->setPlainText(res);
				textEdit->setReadOnly(true);
				textEdit->setTextInteractionFlags(Qt::LinksAccessibleByKeyboard | Qt::LinksAccessibleByMouse | Qt::TextBrowserInteraction | Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);
				consoleTabs->addTab(textEdit, (so->getTitle()==QStringLiteral("Errors, warnings, badboxes")&&!Research::englishUi()?QStringLiteral("错误、警告与排版提示"):so->getTitle()));
			}
		}
	}
}

void TeXDocumentWindow::anchorClicked(const QUrl& url)
{
	if (url.scheme() == QString::fromLatin1("texworks")) {
		int line = 0;
		if (url.hasFragment()) {
			line = url.fragment().toInt();
		}
		TeXDocumentWindow * target = openDocument(QFileInfo(textDoc()->getRootFilePath()).absoluteDir().filePath(url.path()), true, true, line);
		if (target)
			target->textEdit->setFocus(Qt::OtherFocusReason);
	}
	else {
		TWApp::instance()->openUrl(url);
	}
}

// showConsole() and hideConsole() are used internally to update the visibility;
// they must NOT change the keepConsoleOpen setting that records user choice
void TeXDocumentWindow::showConsole()
{
	consoleTabs->show();
	if (process)
		inputLine->show();
	actionShow_Hide_Console->setText(tr("Hide Log"));
}

void TeXDocumentWindow::hideConsole()
{
	consoleTabs->hide();
	inputLine->hide();
	actionShow_Hide_Console->setText(tr("View Log"));
}

// this is connected to the user command, so remember the choice
// for when typesetting finishes
void TeXDocumentWindow::toggleConsoleVisibility()
{
	if (consoleTabs->isVisible()) {
		hideConsole();
		keepConsoleOpen = false;
	}
	else {
		showConsole();
		keepConsoleOpen = true;
	}
}

void TeXDocumentWindow::acceptInputLine()
{
	if (process) {
		const QString str = inputLine->text() + QStringLiteral("\n");
		textEdit_console->echo(str, inputLine->palette().text().color());
		process->write(str.toUtf8());
		inputLine->clear();
	}
}

void TeXDocumentWindow::goToPreview()
{
	flushScriptEdits();
	if (pdfDoc)
		pdfDoc->selectWindow();
	else {
		if (!openPdfIfAvailable(true)) {
			// This should only fail if the user has done something sneaky like closing the
			// preview window and then renaming the PDF file, since we opened the source
			// and checked that it exists (otherwise Go to Preview would have been disabled).
			// We could issue a status-bar warning here but it's a pretty obscure case...
			// for now just disable the command.
			actionGo_to_Preview->setEnabled(false);
			actionSide_by_Side->setEnabled(false);
		}
	}
}

void TeXDocumentWindow::syncClick(int lineNo, int col)
{
	if (!untitled()) {
		// ensure that there is a pdf to receive our signal
		goToPreview();
		emit syncFromSource(textDoc()->absoluteFilePath(), lineNo, col, true);
	}
}

void TeXDocumentWindow::handleModelineChange(QStringList changedKeys, QStringList removedKeys)
{
	Q_UNUSED(removedKeys);

	if (changedKeys.contains(QStringLiteral("program"))) {
		QString name = Research::canonicalEngineName(_texDoc->getModeLineValue(QStringLiteral("program")));
		int index = engine->findText(name, Qt::MatchFixedString);
		if (index > -1) {
			if (index != engine->currentIndex()) {
				engine->setCurrentIndex(index);
				emit asyncFlashStatusBarMessage(tr("Set engine to \"%1\"").arg(engine->currentText()), kStatusMessageDuration);
			}
		}
		else {
			emit asyncFlashStatusBarMessage(tr("Engine \"%1\" not defined").arg(name), kStatusMessageDuration);
		}
	}
	if (changedKeys.contains(QStringLiteral("encoding"))) {
		bool hasMetadata{false};
		QString reqName;
		QTextCursor curs(textDoc());
		// (begin|end)EditBlock() is a workaround for QTBUG-24718 that causes
		// movePosition() to crash the program under some circumstances.
		// Since we don't change any text in the edit block, it should be a noop
		// in the context of undo/redo.
		curs.beginEditBlock();
		curs.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor, PEEK_LENGTH);
		curs.endEditBlock();

		QTextCodec *newCodec = scanForEncoding(curs.selectedText(), hasMetadata, reqName);
		if (newCodec) {
			codec = newCodec;
			showEncodingSetting();
		}
	}
	if (changedKeys.contains(QStringLiteral("spellcheck"))) {
		setSpellcheckLanguage(_texDoc->getModeLineValue(QStringLiteral("spellcheck")));
	}
}

void TeXDocumentWindow::goToTag(int index)
{
	if (_texDoc && index < _texDoc->getTags().count()) {
		textEdit->setTextCursor(_texDoc->getTags()[index].cursor);
		textEdit->setFocus(Qt::OtherFocusReason);
	}
}

bool TeXDocumentWindow::isTypesetting() const
{
	return (process != nullptr || TWApp::instance()->typesetManager().getOwnerForRootFile(textDoc()->getRootFilePath()) == this);
}

void TeXDocumentWindow::removeAuxFiles()
{
	const QString & rootFilePath = textDoc()->getRootFilePath();
	if (rootFilePath.isEmpty())
		return;

	QFileInfo fileInfo(rootFilePath);
	QString jobname = fileInfo.completeBaseName();
	QDir dir(fileInfo.dir());

	QStringList filterList = TWUtils::cleanupPatterns().split(QRegularExpression(QStringLiteral("\\s+")));
	if (filterList.count() == 0)
		return;
	for (int i = 0; i < filterList.count(); ++i)
		filterList[i].replace(QLatin1String("$jobname"), jobname);

	dir.setNameFilters(filterList);
	QStringList auxFileList = dir.entryList(QDir::Files | QDir::CaseSensitive, QDir::Name);
	if (auxFileList.count() > 0)
		Tw::UI::RemoveAuxFilesDialog::doConfirmDelete(dir, auxFileList);
	else
		(void)QMessageBox::information(this, tr("No files found"),
									   tr("No auxiliary files associated with this document at the moment."));
}

#if defined(Q_OS_DARWIN)
#define OPEN_FILE_IN_NEW_WINDOW	Qt::MoveAction // unmodified drag appears as MoveAction on Mac OS X
#define INSERT_DOCUMENT_TEXT	Qt::CopyAction
#define CREATE_INCLUDE_COMMAND	Qt::LinkAction
#else
#define OPEN_FILE_IN_NEW_WINDOW	Qt::CopyAction // ...but as CopyAction on X11
#define INSERT_DOCUMENT_TEXT	Qt::MoveAction
#define CREATE_INCLUDE_COMMAND	Qt::LinkAction
#endif

void TeXDocumentWindow::dragEnterEvent(QDragEnterEvent *event)
{
	event->ignore();
	if (event->mimeData()->hasUrls()) {
		const QList<QUrl> urls = event->mimeData()->urls();
		foreach (const QUrl& url, urls) {
			if (url.scheme() == QString::fromLatin1("file")) {
				event->acceptProposedAction();
				break;
			}
		}
	}
}

void TeXDocumentWindow::dragMoveEvent(QDragMoveEvent *event)
{
	if (event->proposedAction() == INSERT_DOCUMENT_TEXT || event->proposedAction() == CREATE_INCLUDE_COMMAND) {
		if (dragSavedCursor.isNull())
			dragSavedCursor = textEdit->textCursor();
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
		QTextCursor curs = textEdit->cursorForPosition(textEdit->mapFromGlobal(mapToGlobal(event->pos())));
#else
		QTextCursor curs = textEdit->cursorForPosition(textEdit->mapFromGlobal(mapToGlobal(event->position().toPoint())));
#endif
		textEdit->setTextCursor(curs);
	}
	else {
		if (!dragSavedCursor.isNull()) {
			textEdit->setTextCursor(dragSavedCursor);
			dragSavedCursor = QTextCursor();
		}
	}
	event->acceptProposedAction();
}

void TeXDocumentWindow::dragLeaveEvent(QDragLeaveEvent *event)
{
	if (!dragSavedCursor.isNull()) {
		textEdit->setTextCursor(dragSavedCursor);
		dragSavedCursor = QTextCursor();
	}
	event->accept();
}

void TeXDocumentWindow::dropEvent(QDropEvent *event)
{
	if (event->mimeData()->hasUrls()) {
		Qt::DropAction action = event->proposedAction();
		const QList<QUrl> urls = event->mimeData()->urls();
		bool editBlockStarted = false;
		QString text;
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
		QTextCursor curs = textEdit->cursorForPosition(textEdit->mapFromGlobal(mapToGlobal(event->pos())));
#else
		QTextCursor curs = textEdit->cursorForPosition(textEdit->mapFromGlobal(mapToGlobal(event->position().toPoint())));
#endif
		foreach (const QUrl& url, urls) {
			if (url.scheme() == QString::fromLatin1("file")) {
				QString fileName = url.toLocalFile();
				switch (action) {
					case OPEN_FILE_IN_NEW_WINDOW:
						if(!Tw::Document::isImageFile(fileName)) {
							TWApp::instance()->openFile(fileName);
							break;
						}
						// for graphic files, fall through (there's no point in
						// trying to open binary files as text
						// fall through

					case INSERT_DOCUMENT_TEXT:
						if (!Tw::Document::isPDFfile(fileName) && !Tw::Document::isImageFile(fileName) && !Tw::Document::isPostscriptFile(fileName)) {
							QTextCodec * codecUsed{nullptr};
							text = readFile(QFileInfo(fileName), &codecUsed);
							if (!text.isNull()) {
								if (!editBlockStarted) {
									curs.beginEditBlock();
									editBlockStarted = true;
								}
								textEdit->setTextCursor(curs);
								curs.insertText(text);
							}
							break;
						}
						// for graphic files, fall through -- behave the same as the "link" action
						// fall through

					case CREATE_INCLUDE_COMMAND:
						if (!editBlockStarted) {
							curs.beginEditBlock();
							editBlockStarted = true;
						}
						textEdit->setTextCursor(curs);
						if (Tw::Document::isPDFfile(fileName))
							text = TWUtils::includePdfCommand();
						else if (Tw::Document::isImageFile(fileName))
							text = TWUtils::includeImageCommand();
						else if (Tw::Document::isPostscriptFile(fileName))
							text = TWUtils::includePostscriptCommand();
						else
							text = TWUtils::includeTextCommand();
						curs.insertText(text.arg(fileName));
						break;
					default:
						// do nothing
						break;
				}
			}
		}
		if (editBlockStarted)
			curs.endEditBlock();
	}
	dragSavedCursor = QTextCursor();
	event->accept();
}

void TeXDocumentWindow::detachPdf()
{
	if (pdfDoc) {
		disconnect(pdfDoc, &PDFDocumentWindow::destroyed, this, &TeXDocumentWindow::pdfClosed);
		disconnect(this, &TeXDocumentWindow::destroyed, pdfDoc, &PDFDocumentWindow::texClosed);
		pdfDoc = nullptr;
	}
}
