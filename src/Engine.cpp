/*
	This is part of TeXworks, an environment for working with TeX documents
	Copyright (C) 2018-2022  Jonathan Kew, Stefan Löffler

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

#include "Engine.h"
#include "TWApp.h"
#include "ResearchBuild.h"
#include "utils/TypesetCommand.h"
#include "utils/FilePath.h"

#include <QDir>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <cstring>
#ifdef Q_OS_WIN
#include <windows.h>
namespace {
// Stable, cache-owned junctions keep TeX Live's search results ASCII without
// copying external sources. Keep them for recorder/SyncTeX references; never
// recursively delete a junction or change anything in its target directory.
QString searchDirectoryAlias(const QString &directory, const QString &cacheDirectory,
                             const QString &workingDirectory)
{
  const QString target = QFileInfo(directory).canonicalFilePath();
  if (target.isEmpty() || target.startsWith(QStringLiteral("//"))) return {};
  // Keep a 128-bit key so TeX Live's own file APIs can reach the alias even
  // when its cache is nested deeply. Existing aliases are target-checked.
  const QString key = QString::fromLatin1(QCryptographicHash::hash(target.toCaseFolded().toUtf8(), QCryptographicHash::Sha256).toHex()).left(32);
  const QString alias = QDir(cacheDirectory).absoluteFilePath(key);
  const QString searchPath = QDir(workingDirectory).relativeFilePath(alias);
  for (const QChar c : searchPath) if (c.unicode() > 127) return {};
  QFileInfo existing(alias);
  const QString existingTarget = Tw::Utils::directoryJunctionTarget(alias);
  if (!existingTarget.isEmpty() || existing.exists() || existing.isJunction() || existing.isSymLink()) {
    return existingTarget.compare(target, Qt::CaseInsensitive) == 0 ? alias : QString();
  }
  if (!QDir().mkpath(cacheDirectory)) return {};
  // The cache plus the hashed leaf can exceed MAX_PATH even when the source
  // directory is short. Use the extended local-path form for every Win32 call.
  const std::wstring nativeAlias = L"\\\\?\\" + QDir::toNativeSeparators(alias).toStdWString();
  if (!CreateDirectoryW(nativeAlias.c_str(), nullptr)) return {};
  const auto nativeTarget = QDir::toNativeSeparators(target).toStdWString();
  const std::wstring substitute = L"\\??\\" + nativeTarget;
  const size_t substituteBytes = substitute.size() * sizeof(wchar_t), printBytes = nativeTarget.size() * sizeof(wchar_t);
  struct Header { DWORD tag; WORD length, reserved, substituteOffset, substituteLength, printOffset, printLength; };
  static_assert(sizeof(Header) == 16);
  const size_t pathBytes = substituteBytes + printBytes + 2 * sizeof(wchar_t);
  if (pathBytes + 8 > 0xffff) { RemoveDirectoryW(nativeAlias.c_str()); return {}; }
  const Header header{IO_REPARSE_TAG_MOUNT_POINT, WORD(pathBytes + 8), 0, 0, WORD(substituteBytes),
                      WORD(substituteBytes + sizeof(wchar_t)), WORD(printBytes)};
  QByteArray buffer(int(sizeof(Header) + pathBytes), '\0');
  std::memcpy(buffer.data(), &header, sizeof(header));
  std::memcpy(buffer.data() + sizeof(header), substitute.data(), substituteBytes);
  std::memcpy(buffer.data() + sizeof(header) + substituteBytes + sizeof(wchar_t), nativeTarget.data(), printBytes);
  HANDLE handle = CreateFileW(nativeAlias.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  DWORD written = 0;
  const bool created = handle != INVALID_HANDLE_VALUE &&
      DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, buffer.data(), DWORD(buffer.size()), nullptr, 0, &written, nullptr);
  if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
  if (!created) { RemoveDirectoryW(nativeAlias.c_str()); return {}; }
  return Tw::Utils::directoryJunctionTarget(alias).compare(target, Qt::CaseInsensitive) == 0 ? alias : QString();
}

class TypesetJob final : public QObject {
public:
  explicit TypesetJob(QProcess *process) : QObject(process) {
    setObjectName(QStringLiteral("researchTypesetJob"));
    job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (job && !SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
      CloseHandle(job); job = nullptr;
    }
  }
  ~TypesetJob() override { if (job) CloseHandle(job); }
  HANDLE job{nullptr};
  PROCESS_INFORMATION *information{nullptr};
  bool attached{false};
  void stop() { if (job && attached) TerminateJobObject(job, 0xf291); }
  void detachOnSuccess() {
    // Custom tools may deliberately launch a viewer and then exit normally.
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    if (job) SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
  }
};
}
#endif

void Engine::stop(QProcess *process)
{
  if (!process) return;
#ifdef Q_OS_WIN
  if (auto *job = static_cast<TypesetJob *>(process->findChild<QObject *>(QStringLiteral("researchTypesetJob")))) job->stop();
#endif
  process->kill();
}

Engine::Engine(const QString& name, const QString& program, const QStringList & arguments, bool showPdf)
	: _name(name), _program(program), _arguments(arguments), _showPdf(showPdf)
{
}

Engine::Engine(const Engine& orig)
	: _name(orig._name), _program(orig._program), _arguments(orig._arguments), _showPdf(orig._showPdf)
{
}

Engine& Engine::operator=(const Engine& rhs)
{
	_name = rhs._name;
	_program = rhs._program;
	_arguments = rhs._arguments;
	_showPdf = rhs._showPdf;
	return *this;
}

const QString Engine::name() const
{
	return _name;
}

const QString Engine::program() const
{
	return _program;
}

const QStringList Engine::arguments() const
{
	return _arguments;
}

bool Engine::showPdf() const
{
	return _showPdf;
}

void Engine::setName(const QString& name)
{
	_name = name;
}

void Engine::setProgram(const QString& program)
{
	_program = program;
}

void Engine::setArguments(const QStringList& arguments)
{
	_arguments = arguments;
}

void Engine::setShowPdf(bool showPdf)
{
	_showPdf = showPdf;
}

bool Engine::isAvailable() const
{
	return !(programPath(program()).isEmpty());
}

//static
QStringList Engine::binPaths()
{
	return TWApp::instance()->getBinaryPaths();
}

// static
QString Engine::programPath(const QString & prog)
{
	if (prog.isEmpty()) return QString();

	return TWApp::instance()->findProgram(prog, binPaths());
}


bool Engine::supportsLocalBuild() const
{
	const QString tool = QFileInfo(program()).completeBaseName().toLower();
	const QStringList supported{QStringLiteral("latexmk"), QStringLiteral("pdflatex"), QStringLiteral("xelatex"), QStringLiteral("lualatex"), QStringLiteral("pdftex"), QStringLiteral("xetex"), QStringLiteral("luatex")};
	// Custom job names need their own output mapping; retain their configured tool.
	return supported.contains(tool) && !arguments().join(QLatin1Char(' ')).contains(QStringLiteral("jobname"));
}

QProcess * Engine::run(const QFileInfo & input, QObject * parent, const QString &outputDirectory)
{
	QString exeFilePath = programPath(program());
	if (exeFilePath.isEmpty())
		return nullptr;

	QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
	QProcess * process = new QProcess(parent);

	QString workingDir = input.canonicalPath();
#if defined(Q_OS_WIN)
	// files in the root directory of the current drive have to be handled specially
	// because QFileInfo::canonicalPath() returns a path without trailing slash
	// (i.e., a bare drive letter)
	if (workingDir.length() == 2 && workingDir.endsWith(QChar::fromLatin1(':')))
		workingDir.append(QChar::fromLatin1('/'));
#endif
	process->setWorkingDirectory(workingDir);

#if !defined(Q_OS_DARWIN) // not supported on OS X yet :(
	// Add a (customized) TEXEDIT environment variable
	env.insert(QStringLiteral("TEXEDIT"), QStringLiteral("\"%1\" --position=%d \"%s\"").arg(QCoreApplication::applicationFilePath()));

	// MiKTeX apparently uses it's own variable
	env.insert(QStringLiteral("MIKTEX_EDITOR"), QStringLiteral("\"%1\" --position=%l \"%f\"").arg(QCoreApplication::applicationFilePath()));
#endif

#if defined(Q_OS_DARWIN)
	// On Mac OS X, append the path to the typesetting tool to the PATH
	// environment variable.
	// In recent versions of Mac OS X and Qt, GUI applications (like Tw) get
	// different values for PATH than console applications (because .bashrc etc.
	// don't get parsed). Typesetting tools still run correctly (as they are
	// invoked with a full path), but if they in turn try to run other tools
	// (like epstopdf) without full path, the process will fail.
	// Appending the path to the typesetting tool to PATH acts as a fallback and
	// implicitly assumes that the tool itself and all tools it relies on are in
	// the same (standard) location.
	QStringList envPaths = env.value(QStringLiteral("PATH")).split(QStringLiteral(PATH_LIST_SEP));
	envPaths.append(QFileInfo(exeFilePath).dir().absolutePath());
	env.insert(QStringLiteral("PATH"), envPaths.join(QStringLiteral(PATH_LIST_SEP)));
#endif

	QStringList args = arguments();
	const QString selectedTool = QFileInfo(program()).completeBaseName().toLower();
	if (!outputDirectory.isEmpty()) {
		for (const QString &variable : {QStringLiteral("TEXINPUTS"), QStringLiteral("BIBINPUTS"), QStringLiteral("BSTINPUTS")})
			env.insert(variable, workingDir + QStringLiteral(PATH_LIST_SEP) + env.value(variable));
		if (selectedTool == QStringLiteral("bibtex") || selectedTool == QStringLiteral("biber")) process->setWorkingDirectory(outputDirectory);
	}
	if (!outputDirectory.isEmpty() && supportsLocalBuild()) {
		// TeX must resolve generated inputs (especially .bbl) in the local
		// cache before finding an older copy in the source directory. Keep
		// the working directory for relative graphics and custom commands.
		env.insert(QStringLiteral("TEXINPUTS"), outputDirectory +
			QStringLiteral(PATH_LIST_SEP) + env.value(QStringLiteral("TEXINPUTS")));
		for (int i = args.size() - 1; i >= 0; --i)
			if (args[i].startsWith(QStringLiteral("-outdir=")) || args[i].startsWith(QStringLiteral("-output-directory="))) args.removeAt(i);
		const bool latexmk = QFileInfo(program()).completeBaseName().compare(QStringLiteral("latexmk"), Qt::CaseInsensitive) == 0;
		if (latexmk && QFileInfo(input.absoluteDir().filePath(input.completeBaseName() + QStringLiteral(".bbl"))).isFile()) {
			bool explicitBibMode = false;
			for (const auto &argument : args) if (argument == QStringLiteral("-bibtex") || argument == QStringLiteral("-bibtex-") || argument.startsWith(QStringLiteral("-bibtex-cond"))) explicitBibMode = true;
			if (!explicitBibMode) {
				const bool hasBibSource = Research::usesBibliographySource(Research::buildDependencies(input.absoluteFilePath()));
				// Standalone submissions can supply an authored .bbl without
				// a .bib. Do not run BibTeX against their missing bibliography.
				if (!hasBibSource) args.prepend(QStringLiteral("-bibtex-"));
			}
		}
		args.prepend((latexmk ? QStringLiteral("-outdir=") : QStringLiteral("-output-directory=")) + outputDirectory);
		if (!args.contains(QStringLiteral("-recorder"))) args.prepend(QStringLiteral("-recorder"));
	}

	// for old MikTeX versions: delete $synctexoption if it causes an error
	static bool checkedForSynctex = false;
	static bool synctexSupported = true;
	if (!checkedForSynctex && args.join(QLatin1Char(' ')).contains(QString::fromLatin1("$synctexoption"))) {
		QString pdftex = programPath(QString::fromLatin1("pdftex"));
		if (!pdftex.isEmpty()) {
			int result = QProcess::execute(pdftex, QStringList() << QString::fromLatin1("-synctex=1") << QString::fromLatin1("-version"));
			synctexSupported = (result == 0);
		}
		checkedForSynctex = true;
	}
	if (!synctexSupported)
		args.removeAll(QString::fromLatin1("$synctexoption"));

	args.replaceInStrings(QString::fromLatin1("$synctexoption"), QString::fromLatin1("-synctex=1"));
	args.replaceInStrings(QString::fromLatin1("$fullname"), outputDirectory.isEmpty() ? input.fileName() : input.absoluteFilePath());
	args.replaceInStrings(QString::fromLatin1("$basename"), input.completeBaseName());
	args.replaceInStrings(QString::fromLatin1("$suffix"), input.suffix());
	args.replaceInStrings(QString::fromLatin1("$directory"), input.absoluteDir().absolutePath());

#if defined(Q_OS_WIN)
	// Share the verified Windows BCF path with the default standalone Biber.
	// Native filename handling is safe only for reversible system-codepage paths.
	const auto biberControlFile = [&]() -> QString {
		const auto nativePath = [](const QString &path) {
			return QString::fromLocal8Bit(path.toLocal8Bit()) == path;
		};
		if (outputDirectory.isEmpty() || !nativePath(workingDir) || !nativePath(outputDirectory)) return {};
		const auto shortDirectory = [](const QString &path) {
			wchar_t buffer[32768];
			const auto native = QDir::toNativeSeparators(path).toStdWString();
			const DWORD length = GetShortPathNameW(native.c_str(), buffer, 32768);
			return length && length < 32768 ? QDir::fromNativeSeparators(QString::fromWCharArray(buffer)) : path;
		};
		// LibXML also applies MAX_PATH to an unresolved ../ path. Existing short
		// directory names avoid that limit without moving any authored file.
		const QString bcf = QDir(shortDirectory(workingDir)).relativeFilePath(QDir(shortDirectory(outputDirectory)).filePath(input.completeBaseName() + QStringLiteral(".bcf")));
		for (const QChar c : bcf) if (c.unicode() > 127) return {};
		return bcf;
	};
	if (selectedTool == QStringLiteral("biber") && arguments() == QStringList{QStringLiteral("$basename")}) {
		const QString bcf = biberControlFile();
		if (!bcf.isEmpty()) {
			// Relative bibliography resources are resolved from the project, just
			// as in latexmk. Outputs still accompany the cached control file.
			process->setWorkingDirectory(workingDir);
			args = QStringList{QStringLiteral("--winunicode"), bcf};
		}
	}
  QDir searchRoot(outputDirectory.isEmpty()
      ? QCoreApplication::applicationDirPath() + QStringLiteral("/settings")
      : QFileInfo(outputDirectory).absolutePath());
  // Alias identity depends on its verified target, not on a project bucket.
  // Sharing the cache root also keeps latexmk's Perl file tests below MAX_PATH.
  static const QRegularExpression bucket(QStringLiteral("^[a-f0-9]{32}$"));
  if (!outputDirectory.isEmpty() && QFileInfo(outputDirectory).fileName().startsWith(QStringLiteral("work-")) &&
      bucket.match(searchRoot.dirName()).hasMatch()) searchRoot.cdUp();
  const QString searchCache = searchRoot.filePath(QStringLiteral("search-paths"));
	Tw::Utils::normalizeTeXLiveSearchPaths(exeFilePath, process->workingDirectory(), env,
		[&](const QString &directory) { return searchDirectoryAlias(directory, searchCache, process->workingDirectory()); });
	// Avoid the Lua/kpathsea wrapper when the selected TeX Live installation
	// supplies its own latexmk script and Perl. Other toolchains keep their
	// original invocation, including all user-specified command arguments.
	const bool bundledLatexmk = Tw::Utils::useBundledLatexmk(exeFilePath, args, env);
	if (bundledLatexmk && !outputDirectory.isEmpty() && supportsLocalBuild()) {
		// Old Biber double-decodes Unicode absolute paths supplied by latexmk.
		// Pass the same BCF through an ASCII relative path, using native filename
		// handling only when the working paths round-trip in the system encoding.
		// This changes neither authored files nor a customized Biber command.
		const QString bcf = biberControlFile();
		if (!bcf.isEmpty()) {
			args.insert(1, QStringLiteral("-e"));
			// Hex encoding keeps quote and metacharacter-bearing paths out of Perl source.
			args.insert(2, QStringLiteral("$biber = 'biber --winunicode %O ' . chr(34) . pack('H*','%1') . chr(34) if $biber eq 'biber %O %S';").arg(QString::fromLatin1(bcf.toUtf8().toHex())));
		}
	}
#endif

	process->setEnvironment(env.toStringList());
	process->setProcessChannelMode(QProcess::MergedChannels);

#ifdef Q_OS_WIN
  // Start suspended so the engine cannot launch a child before it joins the
  // job. QProcess::start is synchronous on Windows; Qt retains both handles.
  auto *job = new TypesetJob(process);
  process->setCreateProcessArgumentsModifier([job](QProcess::CreateProcessArguments *arguments) {
    arguments->flags |= CREATE_SUSPENDED;
    job->information = arguments->processInformation;
  });
  QObject::connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), process,
    [job](int exitCode, QProcess::ExitStatus status) { if (exitCode == 0 && status == QProcess::NormalExit) job->detachOnSuccess(); });
#endif
  process->start(exeFilePath, args);
#ifdef Q_OS_WIN
  if (job->information && process->state() == QProcess::Running) {
    job->attached = AssignProcessToJobObject(job->job, job->information->hProcess);
    if (!job->attached || ResumeThread(job->information->hThread) == DWORD(-1)) {
      // Never let an unprotected suspended engine escape into the background.
      qWarning("Could not start the protected typesetting process (error %lu)", GetLastError());
      process->kill();
    }
  }
  job->information = nullptr;
#endif

	return process;
}
