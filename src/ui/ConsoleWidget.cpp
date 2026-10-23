/*
	This is part of TeXworks, an environment for working with TeX documents
	Copyright (C) 2023  Stefan Löffler

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

#include "ui/ConsoleWidget.h"

namespace Tw {
namespace UI {

ConsoleWidget::ConsoleWidget(QWidget * parent /* = nullptr */)
 : QTextEdit(parent)
{
	setUndoRedoEnabled(false);
	m_outputTimer.setSingleShot(true);
	m_outputTimer.setInterval(40);
	connect(&m_outputTimer, &QTimer::timeout, this, &ConsoleWidget::flushOutput);
}

ConsoleWidget::~ConsoleWidget()
{
	setProcess(nullptr, false);
}

void ConsoleWidget::setProcess(QProcess *p, const bool clearConsole /* = true */)
{
	flushOutput();
	if (m_process) {
		m_process->disconnect(this);
	}
	if (clearConsole) {
		clear();
	}
	m_unicodeCarry.clear();
	m_process = p;
	if (m_process) {
		connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() {
			// Drain the pipe immediately; batch expensive text layout at most
			// once every 40 ms without dropping or truncating the complete log.
			m_pendingOutput.append(m_process->readAllStandardOutput());
			if (!m_outputTimer.isActive()) m_outputTimer.start();
		});
		connect(m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
		        [this](int, QProcess::ExitStatus) { flushOutput(); });
		connect(m_process, &QProcess::destroyed, this, [this]() {
			// QProcess has already been destroyed: only flush buffered bytes.
			m_process = nullptr;
			flushOutput();
			m_unicodeCarry.clear();
		});
	}
}

void ConsoleWidget::flushOutput()
{
	m_outputTimer.stop();
	if (m_process && m_process->isOpen()) m_pendingOutput.append(m_process->readAllStandardOutput());
	if (m_pendingOutput.isEmpty()) return;
	QByteArray output;
	output.swap(m_pendingOutput);
	appendOutput(output);
}

void ConsoleWidget::echo(const QString &str, const QColor foregroundColor /* = {} */)
{
	flushOutput();
	QTextCursor curs(document());
	using pos_type = decltype(curs.position());
	curs.movePosition(QTextCursor::End);
	setTextCursor(curs);
	QTextCharFormat inputFormat(currentCharFormat());
	if (foregroundColor.isValid()) {
		inputFormat.setForeground(QBrush(foregroundColor));
	}
	insertPlainText(str);
	curs.movePosition(QTextCursor::PreviousCharacter);
	curs.movePosition(QTextCursor::PreviousCharacter, QTextCursor::KeepAnchor, static_cast<pos_type>(str.length() - 1));
	curs.setCharFormat(inputFormat);
}

void ConsoleWidget::appendOutput(QByteArray output)
{
	processIncompleteUTF8Codes(output);
	QTextCursor cursor(document());
	cursor.movePosition(QTextCursor::End);
	cursor.insertText(QString::fromUtf8(output));
	setTextCursor(cursor);
}

void ConsoleWidget::processIncompleteUTF8Codes(QByteArray &data)
{
	using size_type = decltype(data.size());

	if (!m_unicodeCarry.isEmpty()) {
		data.prepend(m_unicodeCarry);
		m_unicodeCarry.clear();
	}

	unsigned char mask{0xF8};
	unsigned char value{0xF0};

	for (size_type codePointLength = 4; codePointLength > 1; --codePointLength, mask <<= 1, value <<= 1) {
		for (size_type offset = 1; offset < qMin(data.size(), codePointLength); ++offset) {
			if ((data[data.size() - offset] & mask) == value) {
				m_unicodeCarry.append(data.right(offset));
				data.remove(data.size() - offset, offset);
				break;
			}
		}
	}
}

} // namespace UI
} // namespace Tw
