/*
	This is part of TeXworks, an environment for working with TeX documents
	Copyright (C) 2007-2024  Jonathan Kew, Stefan Löffler, Charlie Sharpsteen

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

#include "TWUtils.h"
#include "ResearchSettings.h"

#include "PDFDocumentWindow.h"
#include "Settings.h"
#include "TWApp.h"
#include "TeXDocumentWindow.h"
#include "utils/IniConfig.h"
#include "utils/ResourcesLibrary.h"
#include "utils/WindowManager.h"

#include <QAction>
#include <QCompleter>
#include <QDateTime>
#if QT_VERSION < QT_VERSION_CHECK(5, 14, 0)
#include <QDesktopWidget>
#endif
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QKeyEvent>
#include <QMenu>
#include <QString>
#include <QStringList>
#include <QTextCodec>
#include <QTextBlock>
#include <QTextDocument>

void TWUtils::insertHelpMenuItems(QMenu* helpMenu)
{
  // Product help must not inherit the upstream website, mailing list or
  // installed TeXworks manual. The introduction is installed by TeXline.
  QAction *about=nullptr,*introduction=nullptr,*resources=nullptr;
  for(auto *action:helpMenu->actions()) {
    if(action->menuRole()==QAction::AboutRole||action->objectName()==QStringLiteral("actionAbout_TW"))about=action;
    if(action->objectName()==QStringLiteral("researchOnboardingAction"))introduction=action;
    if(action->objectName()==QStringLiteral("actionSettings_and_Resources"))resources=action;
    helpMenu->removeAction(action);
  }
  if(introduction)helpMenu->addAction(introduction);
  if(resources){resources->setText(Research::words("设置与资源位置…","Settings & resource locations…"));helpMenu->addAction(resources);}
  if(about) {
    about->setText(Research::words("关于 TeXline…","About TeXline…"));
    helpMenu->addAction(about);
  }
}

QList<QTextCodec*> *TWUtils::codecList = nullptr;

QList<QTextCodec*> *TWUtils::findCodecs()
{
	if (codecList)
		return codecList;

	codecList = new QList<QTextCodec*>;
	QMap<QString, QTextCodec*> codecMap;
	QRegularExpression iso8859RegExp(QStringLiteral("^ISO[- ]8859-([0-9]+)"));

	foreach (QByteArray name, QTextCodec::availableCodecs()) {
		QTextCodec * codec = QTextCodec::codecForName(name);
		QString sortKey = QString::fromUtf8(codec->name().constData()).toUpper();
		QRegularExpressionMatch iso8859Match = iso8859RegExp.match(sortKey);
		int rank{5};
		if (sortKey.startsWith(QLatin1String("UTF-8")))
			rank = 1;
		else if (sortKey.startsWith(QLatin1String("UTF-16")))
			rank = 2;
		else if (iso8859Match.hasMatch()) {
			if (iso8859Match.captured(1).size() == 1)
				rank = 3;
			else
				rank = 4;
		}
		sortKey.prepend(QChar('0' + rank));
		// Add the codec if it is not already in the list
		// (NB: QTextCodec::availableCodecs() lists all aliases separately)
		if (!codecMap.contains(sortKey)) {
			codecMap.insert(sortKey, codec);
		}
	}
	*codecList = codecMap.values();
	return codecList;
}

QStringList* TWUtils::filters;
QStringList* TWUtils::filterList()
{
	return filters;
}

void TWUtils::setDefaultFilters()
{
	*filters << Research::words("TeX 文件 (*.tex *.ltx *.sty *.cls *.bib *.dtx)", "TeX files (*.tex *.ltx *.sty *.cls *.bib *.dtx)");
	*filters << QObject::tr("LaTeX documents (*.ltx)");
	*filters << QObject::tr("Log files (*.log *.blg)");
	*filters << QObject::tr("BibTeX databases (*.bib)");
	*filters << QObject::tr("Style files (*.sty)");
	*filters << QObject::tr("Class files (*.cls)");
	*filters << QObject::tr("Documented macros (*.dtx)");
	*filters << QObject::tr("Auxiliary files (*.aux *.toc *.lot *.lof *.nav *.out *.snm *.ind *.idx *.bbl *.brf)");
	*filters << QObject::tr("Text files (*.txt)");
	*filters << QObject::tr("PDF documents (*.pdf)");
#ifdef Q_OS_WIN
	// It seems (contrary to documentation) that on Windows, this has to be
	// *.* to allow saving files with non-standard extensions (though *.* still
	// does not allow to save without any extension at all, see below)
	const QString allFilesFilter = QStringLiteral("*.*");
//	const QString allFilesFilter = QStringLiteral("*");
#else
	// On other systems, *.* might require a . in the filename, which would
	// preclude filenames without extension. In line with the documentation, *
	// should be used in those cases
	const QString allFilesFilter = QStringLiteral("*");
#endif
	*filters << QObject::tr("All files") + QStringLiteral(" (%1)").arg(allFilesFilter);
}

/*static*/
QString TWUtils::chooseDefaultFilter(const QString & filename, const QStringList & filters)
{
	QString extension = QFileInfo(filename).completeSuffix();

	if (extension.isEmpty())
		return filters.last();

	foreach (QString filter, filters) {
		// return filter if it corresponds to the given extension
		// note that the extension must be the first one in the list to match;
		// otherwise, the file dialog would replace the actual extension by the
		// first one in the list, thereby altering it without cause
		if (filter.contains(QString::fromLatin1("(*.%1").arg(extension)))
			return filter;
	}
	// if no filter matched, return the last one (which should be "All files")
	return filters.last();
}

void TWUtils::updateRecentFileActions(QObject *parent, QList<QAction*> &actions, QMenu *menu, QAction * clearAction) /* static */
{
	Tw::Settings settings;
	QStringList fileList, labelList;
	if (settings.contains(QString::fromLatin1("recentFiles"))) {
		QList<QVariant> files = settings.value(QString::fromLatin1("recentFiles")).toList();
		foreach (const QVariant& v, files) {
			QMap<QString,QVariant> map = v.toMap();
			if (map.contains(QString::fromLatin1("path")))
				fileList.append(map.value(QString::fromLatin1("path")).toString());
		}
	}
	else {
		// check for an old "recentFilesList" entry, and migrate it
		if (settings.contains(QString::fromLatin1("recentFileList"))) {
			fileList = settings.value(QString::fromLatin1("recentFileList")).toStringList();
			QList<QVariant> files;
			foreach (const QString& path, fileList) {
				QMap<QString,QVariant> map;
				map.insert(QString::fromLatin1("path"), path);
				files.append(QVariant(map));
			}
			settings.remove(QString::fromLatin1("recentFileList"));
			settings.setValue(QString::fromLatin1("recentFiles"), files);
		}
	}

	// Generate label list (list of filenames without directory components)
	labelList = Tw::Utils::WindowManager::constructUniqueFileLabels(fileList);

	QStringList::size_type numRecentFiles = fileList.size();

	foreach(QAction * sep, menu->actions()) {
		if (sep->isSeparator())
			delete sep;
	}

	while (actions.size() < numRecentFiles) {
		QAction *act = new QAction(parent);
		act->setVisible(false);
		QObject::connect(act, &QAction::triggered, TWApp::instance(), &TWApp::openRecentFile);
		actions.append(act);
		menu->insertAction(clearAction, act);
	}

	while (actions.size() > numRecentFiles) {
		QAction *act = actions.takeLast();
		delete act;
	}

	for (QStringList::size_type i = 0; i < numRecentFiles; ++i) {
		// a "&" inside a menu label is considered a mnemonic, thus, we need to escape them
		labelList[i].replace(QString::fromLatin1("&"), QString::fromLatin1("&&"));

		actions[i]->setText(QDir::toNativeSeparators(labelList[i]));
		actions[i]->setData(fileList[i]);
		actions[i]->setToolTip(QDir::toNativeSeparators(fileList[i]));
		actions[i]->setVisible(true);
	}
	menu->setToolTipsVisible(true);

	if (numRecentFiles > 0)
		menu->insertSeparator(clearAction);
	if (clearAction)
		clearAction->setEnabled(numRecentFiles > 0);
}

void TWUtils::applyToolbarOptions(QMainWindow *theWindow, int iconSize, bool showText)
{
	iconSize = iconSize * 8 + 8;	// convert 1,2,3 to 16,24,32
	foreach (QObject *object, theWindow->children()) {
		QToolBar *theToolBar = qobject_cast<QToolBar*>(object);
		if (theToolBar && !theToolBar->property("researchWorkspace").toBool()) {
			theToolBar->setToolButtonStyle(showText ? Qt::ToolButtonTextUnderIcon : Qt::ToolButtonIconOnly);
			theToolBar->setIconSize(QSize(iconSize, iconSize));
		}
	}
}

QMap<QChar,QChar> TWUtils::pairOpeners;
QMap<QChar,QChar> TWUtils::pairClosers;

QChar TWUtils::closerMatching(QChar c)
{
	return pairClosers.value(c);
}

QChar TWUtils::openerMatching(QChar c)
{
	return pairOpeners.value(c);
}

QString TWUtils::sIncludePdfCommand;
QString TWUtils::sIncludeImageCommand;
QString TWUtils::sIncludePostscriptCommand;
QString TWUtils::sIncludeTextCommand;
QString TWUtils::sCleanupPatterns;

const QString& TWUtils::includePdfCommand()
{
	return sIncludePdfCommand;
}

const QString& TWUtils::includeImageCommand()
{
	return sIncludeImageCommand;
}

const QString& TWUtils::includePostscriptCommand()
{
	return sIncludePostscriptCommand;
}

const QString& TWUtils::includeTextCommand()
{
	return sIncludeTextCommand;
}

const QString& TWUtils::cleanupPatterns()
{
	return sCleanupPatterns;
}

void TWUtils::readConfig()
{
	pairOpeners.clear();
	pairClosers.clear();

	QDir configDir(Tw::Utils::ResourcesLibrary::getLibraryPath(QStringLiteral("configuration")));
	QRegularExpression pair(QString::fromLatin1("^([^\\s])\\s+([^\\s])\\s*(?:#.*)?$"));

	QFile pairsFile(configDir.filePath(QString::fromLatin1("delimiter-pairs.txt")));
	if (pairsFile.open(QIODevice::ReadOnly)) {
		while (true) {
			QByteArray ba = pairsFile.readLine();
			if (ba.size() == 0)
				break;
			if (ba[0] == '#' || ba[0] == '\n')
				continue;
			ba.chop(1);
			QString line = QString::fromUtf8(ba.data(), ba.size());
			QRegularExpressionMatch pairMatch = pair.match(line);
			if (pairMatch.hasMatch()) {
				pairClosers[pairMatch.captured(1).at(0)] = pairMatch.captured(2).at(0);
				pairOpeners[pairMatch.captured(2).at(0)] = pairMatch.captured(1).at(0);
			}
		}
	}

	// defaults in case config file not found
	sIncludeTextCommand			= QString::fromLatin1("\\include{%1}\n");
	sIncludePdfCommand			= QString::fromLatin1("\\includegraphics[]{%1}\n");
	sIncludeImageCommand		= QString::fromLatin1("\\includegraphics[]{%1}\n");
	sIncludePostscriptCommand	= QString::fromLatin1("\\includegraphics[]{%1}\n");

	sCleanupPatterns = QString::fromLatin1("*.aux $jobname.log $jobname.lof $jobname.lot $jobname.toc");

	filters = new QStringList;

	QFile configFile(configDir.filePath(QString::fromLatin1("texworks-config.txt")));
	if (configFile.open(QIODevice::ReadOnly)) {
		QRegularExpression keyVal(QStringLiteral("^([-a-z]+):\\s*([^ \\t].+)$"));
			// looking for keyword, colon, optional whitespace, value
		while (true) {
			QByteArray ba = configFile.readLine();
			if (ba.size() == 0)
				break;
			if (ba[0] == '#' || ba[0] == '\n')
				continue;
			QString	line = QString::fromUtf8(ba.data(), ba.size());
			QRegularExpressionMatch keyValMatch = keyVal.match(line);
			if (keyValMatch.hasMatch()) {
				// if that matched, keyVal.cap(1) is the keyword, cap(2) is the value
				const QString& keyword = keyValMatch.captured(1);
				QString value = keyValMatch.captured(2).trimmed();
				if (keyword == QString::fromLatin1("include-text")) {
					sIncludeTextCommand = value.replace(QString::fromLatin1("#RET#"), QChar::fromLatin1('\n'));
					continue;
				}
				if (keyword == QString::fromLatin1("include-pdf")) {
					sIncludePdfCommand = value.replace(QString::fromLatin1("#RET#"), QChar::fromLatin1('\n'));
					continue;
				}
				if (keyword == QString::fromLatin1("include-image")) {
					sIncludeImageCommand = value.replace(QString::fromLatin1("#RET#"), QChar::fromLatin1('\n'));
					continue;
				}
				if (keyword == QString::fromLatin1("include-postscript")) {
					sIncludePostscriptCommand = value.replace(QString::fromLatin1("#RET#"), QChar::fromLatin1('\n'));
					continue;
				}
				if (keyword == QString::fromLatin1("cleanup-patterns")) {
					static bool first = true;
					if (first) {
						sCleanupPatterns = value;
						first = false;
					}
					else {
						sCleanupPatterns += QChar::fromLatin1(' ');
						sCleanupPatterns += value;
					}
					continue;
				}
				if (keyword == QString::fromLatin1("file-open-filter")) {
					*filters << value;
				}
			}
		}
	}

	if (filters->count() == 0)
		setDefaultFilters();
}

namespace {
template<typename Position, typename Character>
Position balanceSequence(Position length, Position pos, QChar delim, int direction, Character character)
{
	if ((direction != -1 && direction != 1) || pos < 0 || pos >= length) return -1;
	// Explicit expected closers keep malformed nesting behavior while avoiding
	// a recursive call for every nested delimiter (and possible stack overflow).
	QList<QChar> expected{delim};
	for (; pos >= 0 && pos < length; pos += direction) {
		const QChar c = character(pos);
		if (c == expected.last()) {
			expected.removeLast();
			if (expected.isEmpty()) return pos;
		} else {
			const QChar next = direction < 0 ? TWUtils::openerMatching(c) : TWUtils::closerMatching(c);
			if (!next.isNull()) expected.append(next);
			else if (!(direction < 0 ? TWUtils::closerMatching(c) : TWUtils::openerMatching(c)).isNull()) return -1;
		}
	}
	return -1;
}
}
QString::size_type TWUtils::balanceDelim(const QString& text, QString::size_type pos, QChar delim, int direction)
{
	return balanceSequence(text.length(), pos, delim, direction, [&text](auto at) { return text[at]; });
}
int TWUtils::balanceDelim(const QTextDocument *document, int pos, QChar delim, int direction)
{
	if (!document) return -1;
	QTextBlock block;
	QString text;
	return balanceSequence(document->characterCount() - 1, pos, delim, direction, [&](int at) {
		if (!block.isValid() || at < block.position() || at >= block.position() + block.length()) {
			block = document->findBlock(at);
			text = block.text();
		}
		const int offset = at - block.position();
		return offset < text.size() ? text[offset] : QChar(QChar::ParagraphSeparator);
	});
}

QString::size_type TWUtils::findOpeningDelim(const QString& text, QString::size_type pos)
	// find the first opening delimiter before offset /pos/
{
	while (--pos >= 0) {
		QChar c = text[pos];
		if (!closerMatching(c).isNull())
			return pos;
	}
	return -1;
}

void TWUtils::installCustomShortcuts(QWidget * widget, bool recursive /* = true */, QSettings * map /* = nullptr */)
{
	std::unique_ptr<Tw::Utils::IniConfig> iniConfig;

	if (!widget)
		return;

	if (!map) {
		const QString filename = QDir(Tw::Utils::ResourcesLibrary::getLibraryPath(QStringLiteral("configuration"))).absoluteFilePath(QString::fromLatin1("shortcuts.ini"));
		if (filename.isEmpty() || !QFileInfo(filename).exists())
			return;

		iniConfig.reset(new Tw::Utils::IniConfig(filename));
		map = iniConfig.get();
		if (map->status() != QSettings::NoError) {
			return;
		}
	}

	foreach (QAction * act, widget->actions()) {
		if (act->objectName().isEmpty())
			continue;
		if (map->contains(act->objectName()))
			act->setShortcut(QKeySequence(map->value(act->objectName()).toString()));
	}

	if (recursive) {
		foreach (QObject * obj, widget->children()) {
			QWidget * child = qobject_cast<QWidget*>(obj);
			if (child)
				installCustomShortcuts(child, true, map);
		}
	}
}
