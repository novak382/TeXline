/*
	This is part of TeXworks, an environment for working with TeX documents
	Copyright (C) 2019-2020  Stefan Löffler

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

#include "LineNumberWidget.h"

#include <QAbstractTextDocumentLayout>
#include <QPainter>
#include <QScrollBar>
#include <QTextBlock>

namespace Tw {
namespace UI {

LineNumberWidget::LineNumberWidget(QTextEdit * parent)
	: QWidget(parent)
	, _editor(parent)
	, _bgColor(palette().color(QPalette::Mid))
{
	setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Ignored);
}

QSize LineNumberWidget::sizeHint() const
{
	int digits = 1;

	if (_editor) {
		int max = qMax(1, _editor->document()->blockCount());
		while (max >= 10) {
			max /= 10;
			++digits;
		}
	}

#if QT_VERSION < QT_VERSION_CHECK(5, 11, 0)
	int space = 3 + fontMetrics().width(QChar::fromLatin1('9')) * digits;
#else
	int space = 3 + fontMetrics().horizontalAdvance(QChar::fromLatin1('9')) * digits;
#endif
	return QSize(space, 0);
}

void LineNumberWidget::paintEvent(QPaintEvent * event)
{
	QPainter painter(this);
	painter.fillRect(event->rect(), _bgColor);

	if (!_editor)
		return;

	// QTextEdit already knows the visible position: never walk from line one.
	QTextBlock block = _editor->cursorForPosition(QPoint(0, event->rect().top())).block();

	QAbstractTextDocumentLayout *layout = _editor->document()->documentLayout();
	int top = static_cast<int>(layout->blockBoundingRect(block).top() - _editor->verticalScrollBar()->value());
	int bottom = static_cast<int>(top + layout->blockBoundingRect(block).height());

	while (block.isValid() && top <= event->rect().bottom()) {
		if (block.isVisible() && bottom >= event->rect().top() && top >= event->rect().top() - fontMetrics().height()) {
			const bool current = block == _editor->textCursor().block();
			QFont f = font(); f.setBold(current); painter.setFont(f);
			painter.setPen(current ? QColor(QStringLiteral("#737980")) : QColor(QStringLiteral("#a8adb3")));
			QString number = QString::number(block.blockNumber() + 1);
			painter.drawText(0, top, width() - 1, fontMetrics().height(),
							 Qt::AlignRight, number);
		}

		block = block.next();
		// A slide view has one contiguous visible source range. Hidden suffix
		// paragraphs must neither receive numbers nor trigger a full-tail walk.
		if (_editor->property("researchSourceSlice").toBool() && block.isValid() && !block.isVisible()) break;
		if (block == _editor->document()->end())
			break;
		// NB: The top of this block may not coincide with the bottom of the
		// previous block in case the line spacing is not 100%
		top = static_cast<int>(layout->blockBoundingRect(block).top() - _editor->verticalScrollBar()->value());
		bottom = top + static_cast<int>(layout->blockBoundingRect(block).height());
	}
}

void LineNumberWidget::changeEvent(QEvent * event)
{
	if (event->type() == QEvent::ParentChange) {
		_editor = qobject_cast<QTextEdit*>(parentWidget());
	}
	QWidget::changeEvent(event);
}

} // namespace UI
} // namespace Tw
