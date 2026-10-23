/*
	This is part of TeXworks, an environment for working with TeX documents
	Copyright (C) 2008-2020  Jonathan Kew, Stefan Löffler, Charlie Sharpsteen

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

#ifndef TemplateDialog_H
#define TemplateDialog_H

#include "ui_TemplateDialog.h"

#include <QDialog>
#include <QFileSystemModel>
#include <QItemSelection>
#include <QString>

class QDirModel;

class TemplateDialog : public QDialog, private Ui::TemplateDialog
{
	Q_OBJECT

public:
	explicit TemplateDialog(QWidget *parent = nullptr);
	~TemplateDialog() override = default;

	static QString doTemplateDialog(bool *project = nullptr, QWidget *parent = nullptr);

protected:
	void showEvent(QShowEvent * event) override;
	void resizeEvent(QResizeEvent * event) override;

private slots:
	void selectionChanged(const QItemSelection &selected, const QItemSelection &deselected);
	void itemActivated(const QModelIndex & index);

private:
	void init();

	QFileSystemModel * model{nullptr};
	bool _shouldResizeColumns{true};
};

#endif
