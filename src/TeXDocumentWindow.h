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

#ifndef TeXDocumentWindow_H
#define TeXDocumentWindow_H

#include "DefaultPrefs.h"
#include "ResearchBuild.h"
#include "FindDialog.h"
#include "TWScriptableWindow.h"
#include "document/TeXDocument.h"
#include "ui_TeXDocumentWindow.h"

#include <QDateTime>
#include <QList>
#include <QMouseEvent>
#include <QProcess>
#include <QRegularExpression>
#include <QSignalMapper>
#include <atomic>
#include <memory>

class Engine;
class QAction;
class QMenu;
class QTextEdit;
class QToolBar;
class QLabel;
class QComboBox;
class QLineEdit;
class QActionGroup;
class QTextCodec;
class QFileSystemWatcher;

class PDFDocumentWindow;
namespace Research { class Collaboration; class FindController; }

namespace Tw {
namespace UI {
class ClickableLabel;
} // namespace UI
} // namespace Tw

const int kTeXWindowStateVersion = 1; // increment this if we add toolbars/docks/etc

#define kLineEnd_Mask   0x00FF
#define kLineEnd_LF     0x0000
#define kLineEnd_CRLF   0x0001
#define kLineEnd_CR     0x0002

#define kLineEnd_Flags_Mask  0xFF00
#define kLineEnd_Mixed       0x0100

class TeXDocumentWindow : public TWScriptableWindow, private Ui::TeXDocumentWindow
{
	Q_OBJECT

public:
	explicit TeXDocumentWindow();
	explicit TeXDocumentWindow(const QString & fileName, bool asTemplate = false, bool inBackground = false);

	~TeXDocumentWindow() override;

	static TeXDocumentWindow *findDocument(const QString &fileName);
	static QList<TeXDocumentWindow*> documentList()
		{
			return docList;
		}
	static TeXDocumentWindow *openDocument(const QString &fileName, bool activate = true, bool raiseWindow = true,
									 int lineNo = 0, int selStart = -1, int selEnd = -1);

	TeXDocumentWindow *open(const QString &fileName);
	void makeUntitled();
	bool untitled() const
		{ return !textDoc()->isStoredInFilesystem(); }
	QString fileName() const
		{ return textDoc()->getFileInfo().filePath(); }
	QTextCursor textCursor() const
		{ return textEdit->textCursor(); }
	Tw::Document::TeXDocument* textDoc()
		{ return _texDoc; }
	const Tw::Document::TeXDocument* textDoc() const
		{ return _texDoc; }
	QString getLineText(int lineNo) const;
	CompletingEdit* editor()
		{ return textEdit; }
	int cursorPosition() const { return textCursor().position(); }
	int selectionStart() const { return textCursor().selectionStart(); }
	int selectionLength() const { return textCursor().selectionEnd() - textCursor().selectionStart(); }
	QString getCurrentCodecName() const { return (codec ? QString::fromUtf8(codec->name().constData()) : QString()); }
	bool getUTF8BOM() const { return utf8BOM; }

	QString spellcheckLanguage() const;

	PDFDocumentWindow* pdfDocument()
		{ return pdfDoc; }
	bool openPdfIfAvailable(bool show);

	void goToLine(int lineNo, int selStart = -1, int selEnd = -1);
	void goToTag(int index);
	// Script writes coalesce until yield/read/save or completion, preserving
	// native undo and flushing before any operation that consumes the source.
	void beginScriptEdits();
	void endScriptEdits();
	void flushScriptEdits();

	bool isModified() const { return textEdit->document()->isModified(); }
	void setModified(const bool m = true) { flushScriptEdits(); textEdit->document()->setModified(m); }
	bool scriptModified() { flushScriptEdits(); return isModified(); }

	bool isTypesetting() const;
	void populateResearchEngineMenu(QMenu *menu);
	QString researchEngineName() const { return engineName; }
	Research::Collaboration *collaboration() { return researchCollaboration; }
	bool adoptMovedProjectFile(const QString &oldPath, const QString &newPath);

	qreal lineSpacing() const { return m_lineSpacing; }
	void setEditorAppearance(const QFont &font, qreal spacing);

	Q_PROPERTY(int cursorPosition READ cursorPosition STORED false)
	Q_PROPERTY(QString selection READ selectedText STORED false)
	Q_PROPERTY(int selectionStart READ selectionStart STORED false)
	Q_PROPERTY(int selectionLength READ selectionLength STORED false)
	Q_PROPERTY(QString consoleOutput READ consoleText STORED false)
	Q_PROPERTY(QString text READ text STORED false)
    Q_PROPERTY(QString fileName READ fileName)
	Q_PROPERTY(QString rootFileName READ getRootFilePath STORED false)
	Q_PROPERTY(bool untitled READ untitled STORED false)
	Q_PROPERTY(bool modified READ scriptModified WRITE setModified STORED false)
	Q_PROPERTY(QString spellcheckLanguage READ spellcheckLanguage WRITE setSpellcheckLanguage STORED false)
	Q_PROPERTY(QString currentCodecName READ getCurrentCodecName STORED false)
	Q_PROPERTY(bool writeUTF8BOM READ getUTF8BOM STORED false)

signals:
	void syncFromSource(const QString& sourceFile, int lineNo, int col, bool activatePreview);
	void activatedWindow(QWidget*);
	void asyncFlashStatusBarMessage(const QString & msg, const int timeout = 0);

protected:
	void changeEvent(QEvent *event) override;
	void closeEvent(QCloseEvent *event) override;
	bool event(QEvent *event) override;
	void dragEnterEvent(QDragEnterEvent *event) override;
	void dragMoveEvent(QDragMoveEvent *event) override;
	void dragLeaveEvent(QDragLeaveEvent *event) override;
	void dropEvent(QDropEvent *event) override;

	QString scriptContext() const override { return QStringLiteral("TeXDocument"); }

public slots:
	void typeset();
	void interrupt();
	void goToTypesettingWindow();
	void newFile();
	void newFromTemplate();
	void open();
	bool save();
	bool saveAll();
	bool saveAs();
	void revert();
	void clear();
	void doFontDialog();
	void doLineDialog();
	void doFindDialog();
	void doFindAgain(bool fromDialog = false);
	void doReplaceDialog();
	void doReplaceAgain();
	void doIndent();
	void doUnindent();
	void doComment();
	void doUncomment();
	void toUppercase();
	void toLowercase();
	void toggleCase();
	void balanceDelimiters();
	void doHardWrapDialog();
	void doInsertCitationsDialog();
	void setLineNumbers(bool displayNumbers);
	void setLineSpacing(qreal percent);
	void setWrapLines(bool wrap);
	void setSyntaxColoring(int index);
	void copyToFind();
	void copyToReplace();
	void findSelection();
	void showSelection();
	void toggleConsoleVisibility();
	void goToPreview();
	void syncClick(int lineNo, int col);
	void openAt(QAction *action);
	void sideBySide();
	void removeAuxFiles();
	void setSpellcheckLanguage(const QString& lang);
	void reloadSpellcheckerMenu();
	void selectRange(int start, int length = 0);
	void insertText(const QString& text);
	void selectAll() { textEdit->selectAll(); }
	void setWindowModified(bool modified);
	void setSmartQuotesMode(const QString& mode);
	void setAutoIndentMode(const QString& mode);
	void setSyntaxColoringMode(const QString& mode);

private slots:
	void setLangInternal(const QString& lang);
	void maybeEnableSaveAndRevert(bool modified);
	void clipboardChanged();
	void doReplace(ReplaceDialog::DialogCode mode);
	void pdfClosed();
	void updateRecentFileActions();
	void updateWindowMenu();
	void updateEngineList();
	void showCursorPosition();
	void editMenuAboutToShow();
	void processError(QProcess::ProcessError error);
	void processFinished(int exitCode, QProcess::ExitStatus exitStatus);
	void acceptInputLine();
	void selectedEngine(QAction* engineAction);
	void selectedEngine(int idx);
	void handleModelineChange(QStringList changedKeys, QStringList removedKeys);
	void reloadIfChangedOnDisk();
	void setupFileWatcher();
	void lineEndingPopup(const QPoint loc);
	void encodingPopup(const QPoint loc);
	void lineEndingLabelClick(QMouseEvent * event) { lineEndingPopup(event->pos()); }
	void encodingLabelClick(QMouseEvent * event) { encodingPopup(event->pos()); }
	void anchorClicked(const QUrl& url);
	void delayedInit();

private:
	friend class Research::FindController;
	static QString replacementForMatch(const QRegularExpressionMatch &match,const QString &replacement);
	static QString replacementInput(QString replacement);
	void init(bool openingFile = false);
	bool maybeSave();
	void detachPdf();
	bool saveFilesHavingRoot(const QString& aRootFile);
	void clearFileWatcher();
	QTextCodec *scanForEncoding(const QString &peekStr, bool &hasMetadata, QString &reqName);
	QString readFile(const QFileInfo & fileInfo, QTextCodec **codecUsed, int *lineEndings = nullptr, QTextCodec * forceCodec = nullptr);
	void loadFile(const QFileInfo & fileInfo, bool asTemplate = false, bool inBackground = false, bool reload = false, QTextCodec * forceCodec = nullptr);
	bool saveFile(const QFileInfo & fileInfo);
	void setCurrentFile(const QFileInfo & fileInfo);
	void saveRecentFileInfo(bool captureRecent = false);
	bool getPreviewFileName(QString &pdfName);
	void replaceSelection(const QString& newText);
	void doHardWrap(int mode, int lineWidth, bool rewrap);
	QTextCursor doSearch(const QString& searchText, const QRegularExpression *regex,
						 QTextDocument::FindFlags flags, int rangeStart, int rangeEnd);
	int doReplaceAll(const QString& searchText, QRegularExpression* regex, const QString& replacement,
						QTextDocument::FindFlags flags, int rangeStart = -1, int rangeEnd = -1);
	void executeAfterTypesetHooks();
	void showConsole();
	void hideConsole();
	void updateTypesettingAction();
	void conditionallyEnableRemoveAuxFiles();
	QString getRootFilePath() { flushScriptEdits(); return textDoc()->getRootFilePath(); }
	void maybeCenterSelection(int oldScrollValue = -1);
	void presentResults(const QList<SearchResult>& results);
	void showLineEndingSetting();
	void showEncodingSetting();

	QString selectedText() { return textCursor().selectedText().replace(QChar(QChar::ParagraphSeparator), QChar::fromLatin1('\n')); }
	QString consoleText() { textEdit_console->flushOutput(); return textEdit_console->toPlainText(); }
	QString text() { flushScriptEdits(); return textEdit->text(); }

	Tw::Document::TeXDocument * _texDoc;
	int _scriptEditDepth{0};
	QTextCursor _scriptEditCursor;
	void prepareScriptEdit();
	PDFDocumentWindow * pdfDoc{nullptr};

	QTextCodec * codec{nullptr};
	// When using the UTF-8 codec, byte order marks (BOMs) are ignored during
	// reading and not produced when writing. To keep them in files that have
	// them, we need to keep track of them ourselves.
	bool utf8BOM{false};
	int lineEndings{kLineEnd_LF};
	QDateTime lastModified;
	qreal m_lineSpacing{kDefault_LineSpacing};

	Tw::UI::ClickableLabel * lineNumberLabel{nullptr};
	Tw::UI::ClickableLabel * encodingLabel{nullptr};
	Tw::UI::ClickableLabel * lineEndingLabel{nullptr};

	QActionGroup *engineActions{nullptr};
	QString engineName;

	QSignalMapper dictSignalMapper;

	QComboBox * engine{nullptr};
	void setupResearchWorkspace();
	void showResearchFind();
	void researchFind(bool backwards = false);
	QToolBar *researchFindBar{nullptr};
	QLineEdit *researchFindField{nullptr};
	QAction *researchProblemsAction{nullptr};
	QProcess * process{nullptr};
	Research::Collaboration *researchCollaboration{nullptr};
	Research::BuildJob researchBuild;
	void startPreparedTypesetting(const QString &root, Engine selected);
	void completeTypesetting(bool failed, int promoted);
	bool researchBuildPreparing{false};
	bool researchBuildFinishing{false};
	std::shared_ptr<std::atomic_bool> researchFinishCancelled;
	std::shared_ptr<std::atomic<quint64>> researchPrepareEpoch{std::make_shared<std::atomic<quint64>>(0)};
	bool researchRetryRequested{false};
	int researchBuildRetries{0};
	quint64 researchBuildSerial{0};
	bool keepConsoleOpen{false};
	bool showPdfWhenFinished{true};
	bool userInterrupt{false};
	QDateTime oldPdfTime;

	QList<QAction*> recentFileActions;

	QFileSystemWatcher * watcher{nullptr};

	QTextCursor	dragSavedCursor;

	static QList<TeXDocumentWindow*> docList;
};

#endif // !defined(TeXDocumentWindow_H)
