/*
	This is part of TeXworks, an environment for working with TeX documents
	Copyright (C) 2019-2022  Stefan Löffler

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

#include "document/TeXDocument.h"
#include "TeXHighlighter.h"
#include "ResearchWorkflow.h"

#include <QDir>
#include <QFileInfo>
#include <QTextCursor>

namespace Tw {
namespace Document {

TeXDocument::TeXDocument(QObject * parent) : TextDocument(parent)
{
#ifdef TW_QT_LAZY_SOURCE_EDITS
	// Enabled only with the verified QtGui archive; PDF and rich text documents
	// keep Qt's existing layout behavior.
	setProperty("_texline_lazy_edits", true);
#endif
	connect(this, &TeXDocument::contentsChange, this, &TeXDocument::maybeUpdateModeLines);
	connect(this, &TeXDocument::contentsChanged, this, &TeXDocument::invalidateSourceText);
}

TeXDocument::TeXDocument(const QString & text, QObject * parent) : TextDocument(text, parent)
{
#ifdef TW_QT_LAZY_SOURCE_EDITS
	setProperty("_texline_lazy_edits", true);
#endif
	connect(this, &TeXDocument::contentsChange, this, &TeXDocument::maybeUpdateModeLines);
	connect(this, &TeXDocument::contentsChanged, this, &TeXDocument::invalidateSourceText);
	parseModeLines();
}

TeXHighlighter * TeXDocument::getHighlighter() const
{
	return findChild<TeXHighlighter*>();
}

void TeXDocument::parseModeLines()
{
	QMap<QString, QString> newModeLines;

	QRegularExpression re(QStringLiteral(u"%(?:\\^\\^A)?\\s*!TEX\\s+(?:TS-)?(\\w+)\\s*=\\s*([^\r\n\x2029]+)[\r\n\x2029]"), QRegularExpression::CaseInsensitiveOption);

	// Read the bounded preamble directly. An empty cursor edit block changes
	// Qt's revision and emits another layout/source notification after a write.
	// Block reads also avoid the old movePosition() crash workaround entirely.
	QString preamble;
	for (auto block = firstBlock(); block.isValid() && preamble.size() < PeekLength; block = block.next()) {
		preamble += block.text().left(PeekLength - preamble.size());
		if (preamble.size() < PeekLength && block.next().isValid()) preamble += QChar::ParagraphSeparator;
	}
	QRegularExpressionMatchIterator it = re.globalMatch(preamble);

	while (it.hasNext()) {
		QRegularExpressionMatch m = it.next();
		newModeLines.insert(m.captured(1).trimmed().toLower(), m.captured(2).trimmed());
	}

	if (_modelines != newModeLines) {
		QStringList changedKeys;
		QStringList removedKeys;

		Q_FOREACH(QString key, _modelines.keys()) {
			if (!newModeLines.contains(key)) {
				removedKeys.append(key);
			}
		}
		Q_FOREACH(QString key, newModeLines.keys()) {
			if (!_modelines.contains(key) || _modelines.value(key) != newModeLines.value(key)) {
				changedKeys.append(key);
			}
		}

		_modelines = newModeLines;
		emit modelinesChanged(changedKeys, removedKeys);
	}
}

QString TeXDocument::getRootFilePath() const
{
	// An explicit navigation inside a presentation carries its root for this
	// editing session only. Never write a modeline or a personal root preference.
	if (property("researchPresentationSource").toString() == absoluteFilePath()) {
		const auto root = property("researchPresentationRoot").toString();
		if (!root.isEmpty()) return root;
	}
	if (hasModeLine(QStringLiteral("root"))) {
		const QString rootName{getModeLineValue(QStringLiteral("root")).trimmed()};
		const QFileInfo rootFileInfo{getFileInfo().dir(), rootName};
		return rootFileInfo.absoluteFilePath();
	}

	if (!isStoredInFilesystem()) {
		return {};
	}
	// Article navigation carries a session root without changing the manuscript.
	// Explicit root modelines above continue to take precedence.
	if (property("researchArticleSource").toString() == absoluteFilePath()) {
		const auto root = property("researchArticleRoot").toString();
		if (!root.isEmpty()) return root;
	}
	// Ordinary papers declare their class in the first few paragraphs. Root
	// queries after typing must not materialize the complete source snapshot
	// merely to establish that positive fact. Complex/late headers and subfiles
	// retain complete parsing, and explicit/remembered roots retain precedence.
	QTextCursor prefix(const_cast<TeXDocument *>(this));
	prefix.setPosition(qMin(4096, characterCount() - 1), QTextCursor::KeepAnchor);
	QString preamble = prefix.selectedText();
	for (const QChar separator : {QChar(0xfdd0), QChar(0xfdd1), QChar(QChar::ParagraphSeparator), QChar(QChar::LineSeparator)})
		preamble.replace(separator, QLatin1Char('\n'));
	preamble.replace(QChar::Nbsp, QLatin1Char(' '));
	const QString main = Research::mainRootFromPreamble(absoluteFilePath(), preamble);
	if (!main.isEmpty()) return main;
	return Research::resolveRoot(absoluteFilePath(), sourceText());
}

QString TeXDocument::sourceText() const
{
	if (_sourceText.isNull()) {
		_sourceText = editorText();
		_sourceText.replace(QChar::Nbsp, QLatin1Char(' '));
	}
	return _sourceText;
}

QString TeXDocument::editorText() const
{
	if (_editorText.isNull()) {
		_editorText = toRawText();
		for (int i = 0; i < _editorText.size(); ++i) {
			const auto code = _editorText.at(i).unicode();
			if (code == 0xfdd0 || code == 0xfdd1 || code == QChar::ParagraphSeparator || code == QChar::LineSeparator)
				_editorText[i] = QLatin1Char('\n');
		}
	}
	return _editorText;
}

void TeXDocument::maybeUpdateModeLines(int position, int charsRemoved, int charsAdded)
{
	// Ending a selection-only edit block can emit a zero-length notification.
	// It changes neither the source snapshot nor the modelines.
	if (charsRemoved == 0 && charsAdded == 0) return;
	invalidateSourceText();

	if (position < PeekLength)
		parseModeLines();
}

// static
bool TeXDocument::findNextWord(const QString & text, const QString::size_type index, QString::size_type & start, QString::size_type & end)
{
	// try to do a sensible "word" selection for TeX documents, taking into
	// account the form of control sequences:
	// given an index representing a caret,
	// - if current char (following caret) is a letter, apostrophe, or '@',
	//   extend in both directions
	//   - include apostrophe if surrounded by letters
	//   - include preceding backslash if any, unless word contains apostrophe
	// - if preceeding char is a \, extend to include \ only
	// - if current char is a number, extend in both directions
	// - if current char is a space or tab, extend in both directions to include
	//   all spaces or tabs
	// - if current char is a \, include next char; if letter or '@', extend to
	//   include all following letters or '@'
	// - else select single char following index
	// returns TRUE if the resulting selection consists of word-forming chars

	start = end = index;

	if (text.length() < 1) // empty
		return false;
	if (index >= text.length()) // end of line
		return false;

	QChar ch = text.at(index);

	auto isWordForming = [](const QChar & c) { return c.isLetter() || c.isMark(); };

	if (isWordForming(ch) || ch == QChar::fromLatin1('@') /* || ch == QChar::fromLatin1('\'') || ch == 0x2019 */) {
		bool isControlSeq{false}; // becomes true if we include an @ sign or a leading backslash
		bool includesApos{false}; // becomes true if we include an apostrophe
		if (ch == QChar::fromLatin1('@'))
			isControlSeq = true;
		//else if (ch == QChar::fromLatin1('\'') || ch == 0x2019)
		//	includesApos = true;
		while (start > 0) {
			--start;
			ch = text.at(start);
			if (isWordForming(ch))
				continue;
			if (!includesApos && ch == QChar::fromLatin1('@')) {
				isControlSeq = true;
				continue;
			}
			if (!isControlSeq && (ch == QChar::fromLatin1('\'') || ch == QChar(0x2019)) && start > 0 && isWordForming(text.at(start - 1))) {
				includesApos = true;
				continue;
			}
			++start;
			break;
		}
		if (start > 0 && text.at(start - 1) == QChar::fromLatin1('\\')) {
			isControlSeq = true;
			--start;
		}
		while (++end < text.length()) {
			ch = text.at(end);
			if (isWordForming(ch))
				continue;
			if (!includesApos && ch == QChar::fromLatin1('@')) {
				isControlSeq = true;
				continue;
			}
			if (!isControlSeq && (ch == QChar::fromLatin1('\'') || ch == QChar(0x2019)) && end < text.length() - 1 && isWordForming(text.at(end + 1))) {
				includesApos = true;
				continue;
			}
			break;
		}
		return !isControlSeq;
	}

	if (index > 0 && text.at(index - 1) == QChar::fromLatin1('\\')) {
		start = index - 1;
		end = index + 1;
		return false;
	}

	if (ch.isNumber()) {
		// TODO: handle decimals, leading signs
		while (start > 0) {
			--start;
			ch = text.at(start);
			if (ch.isNumber())
				continue;
			++start;
			break;
		}
		while (++end < text.length()) {
			ch = text.at(end);
			if (ch.isNumber())
				continue;
			break;
		}
		return false;
	}

	if (ch == QChar::fromLatin1(' ') || ch == QChar::fromLatin1('\t')) {
		while (start > 0) {
			--start;
			ch = text.at(start);
			if (!(ch == QChar::fromLatin1(' ') || ch == QChar::fromLatin1('\t'))) {
				++start;
				break;
			}
		}
		while (++end < text.length()) {
			ch = text.at(end);
			if (!(ch == QChar::fromLatin1(' ') || ch == QChar::fromLatin1('\t')))
				break;
		}
		return false;
	}

	if (ch == QChar::fromLatin1('\\')) {
		if (++end < text.length()) {
			ch = text.at(end);
			if (isWordForming(ch) || ch == QChar::fromLatin1('@'))
				while (++end < text.length()) {
					ch = text.at(end);
					if (isWordForming(ch) || ch == QChar::fromLatin1('@'))
						continue;
					break;
				}
			else
				++end;
		}
		return false;
	}

	// else the character is selected in isolation
	end = index + 1;
	return false;
}

} // namespace Document
} // namespace Tw
