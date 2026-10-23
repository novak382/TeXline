/*
	This is part of TeXworks, an environment for working with TeX documents
	Copyright (C) 2007-2023  Jonathan Kew, Stefan Löffler, Charlie Sharpsteen

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

#include "TWApp.h"

#if defined(STATIC_QT5) && defined(Q_OS_WIN)
  #include <QtPlugin>
  Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)
#endif


#include "ResearchWorkflow.h"
#if defined(Q_OS_WIN)
#include <QDir>
#include <windows.h>
#include <shlobj.h>

static void repairInheritedHome()
{
	// A launcher can supply a mangled Unicode profile path. Resolve the current
	// Windows account locally; never alter the system/user environment settings.
	const QString inherited = qEnvironmentVariable("USERPROFILE");
	if (!inherited.contains(QChar::ReplacementCharacter) && QDir(inherited).exists()) return;
	wchar_t profile[MAX_PATH]{};
	if (FAILED(SHGetFolderPathW(nullptr, CSIDL_PROFILE, nullptr, SHGFP_TYPE_CURRENT, profile))) return;
	const QString actual = QString::fromWCharArray(profile);
	if (!QDir(actual).exists()) return;
	qputenv("USERPROFILE", actual.toLocal8Bit());
	const QString home = qEnvironmentVariable("HOME");
	if (home.isEmpty() || home == inherited) qputenv("HOME", actual.toLocal8Bit());
}
#endif

int main(int argc, char *argv[])
{
#if defined(Q_OS_WIN)
	repairInheritedHome();
#endif
	if (argc == 3 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--research-self-test")) {
#if defined(Q_OS_WIN)
		// The shipping single EXE includes the Windows platform, not offscreen.
		qputenv("QT_QPA_PLATFORM", "windows");
#endif
		QApplication app(argc, argv);
		return Research::selfTest(QString::fromLocal8Bit(argv[2]));
	}
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
	QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#	if QT_VERSION >= QT_VERSION_CHECK(5, 6, 0)
	QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#	endif
#endif
	TWApp app(argc, argv);
	return app.exec();
}
