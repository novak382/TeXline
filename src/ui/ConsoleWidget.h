/*
	This is part of TeXworks, an environment for working with TeX documents
	Copyright (C) 2008-2023  Stefan Löffler

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

#ifndef ConsoleWidget_H
#define ConsoleWidget_H

#include <QProcess>
#include <QTextEdit>
#include <QTimer>

namespace Tw {
namespace UI {

class ConsoleWidget : public QTextEdit
{
	Q_OBJECT
public:
	ConsoleWidget(QWidget * parent = nullptr);
	~ConsoleWidget() override;

	QProcess * process() const { return m_process; }
	void setProcess(QProcess * p, const bool clearConsole = true);

	void echo(const QString & str, const QColor foregroundColor = {});
	void flushOutput();

private slots:
	void appendOutput(QByteArray output);

private:
	void processIncompleteUTF8Codes(QByteArray & data);

	QProcess * m_process{nullptr};
	QByteArray m_unicodeCarry;
	QByteArray m_pendingOutput;
	QTimer m_outputTimer;
};

} // namespace UI
} // namespace Tw

#endif // !defined(ConsoleWidget_H)
