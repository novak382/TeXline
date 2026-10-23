/* TeXworks Research: use the bundled interpreter for a standard TeX Live
 * latexmk launcher. Licensed under GPL-2.0-or-later, like TeXworks. */
#ifndef TW_TYPESET_COMMAND_H
#define TW_TYPESET_COMMAND_H

#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QProcessEnvironment>
#include <QStringList>
#include <functional>

namespace Tw {
namespace Utils {

// Windows TeX Live engines and kpsewhich can interpret a non-ASCII environment
// path differently. For explicit existing directories, prefer an equivalent
// ASCII path relative to the compiler's working directory. No manuscript or
// system environment is changed. Keep kpathsea expressions and defaults intact.
inline void normalizeTeXLiveSearchPaths(const QString &program, const QString &workingDirectory,
                                       QProcessEnvironment &environment,
                                       const std::function<QString(const QString &)> &directoryAlias = {})
{
#ifdef Q_OS_WIN
    const QString tool = QFileInfo(program).completeBaseName().toLower();
    const QStringList supported{QStringLiteral("latexmk"), QStringLiteral("pdflatex"), QStringLiteral("xelatex"),
        QStringLiteral("lualatex"), QStringLiteral("pdftex"), QStringLiteral("xetex"), QStringLiteral("luatex"),
        QStringLiteral("bibtex"), QStringLiteral("biber")};
    if (!supported.contains(tool)) return;
    QDir installation = QFileInfo(program).absoluteDir();
    const QString platform = installation.dirName().toLower();
    if ((platform != QStringLiteral("windows") && platform != QStringLiteral("win32") &&
         platform != QStringLiteral("win64")) || !installation.cdUp() ||
        installation.dirName().compare(QStringLiteral("bin"), Qt::CaseInsensitive) != 0 ||
        !installation.cdUp() || !installation.exists(QStringLiteral("texmf-dist/web2c/texmf.cnf")))
        return;
    const QDir working(workingDirectory);
    auto ascii = [](const QString &s) {
        for (const QChar c : s) if (c.unicode() > 127) return false;
        return true;
    };
    for (const QString &variable : {QStringLiteral("TEXINPUTS"), QStringLiteral("BIBINPUTS"), QStringLiteral("BSTINPUTS")}) {
        if (!environment.contains(variable)) continue;
        QStringList entries = environment.value(variable).split(QLatin1Char(';'), Qt::KeepEmptyParts);
        for (QString &entry : entries) {
            if (ascii(entry) || entry.contains(QLatin1Char('$')) || entry.contains(QLatin1Char('{')) ||
                entry.contains(QLatin1Char('}')) || entry.contains(QLatin1Char('~'))) continue;
            QString path = QDir::fromNativeSeparators(entry), suffix;
            if (path.startsWith(QStringLiteral("!!"))) continue; // Database-only searches depend on their registered path.
            if (path.startsWith(QStringLiteral("//"))) continue; // Preserve network paths.
            if (path.endsWith(QStringLiteral("//"))) { suffix = QStringLiteral("//"); path.chop(2); }
            if (path.contains(QStringLiteral("//"))) continue; // Recursive expressions inside a path.
            if (!QDir::isAbsolutePath(path)) path = working.absoluteFilePath(path);
            if (!QFileInfo(path).isDir()) continue;
            const QString relative = working.relativeFilePath(path);
            // Do not use DOS short-name aliases: kpathsea expands them back
            // to long names before the engine opens the file, reviving the
            // encoding mismatch for a non-ASCII directory component.
            if (ascii(relative)) entry = relative + suffix;
            else if (directoryAlias) {
                const QString alias = directoryAlias(path);
                if (!alias.isEmpty()) {
                    const QString aliasPath = working.relativeFilePath(alias);
                    if (ascii(aliasPath)) entry = aliasPath + suffix;
                }
            }
        }
        environment.insert(variable, entries.join(QLatin1Char(';')));
    }
#else
    Q_UNUSED(program)
    Q_UNUSED(workingDirectory)
    Q_UNUSED(environment)
    Q_UNUSED(directoryAlias)
#endif
}

inline bool useBundledLatexmk(QString &program, QStringList &arguments,
                             QProcessEnvironment &environment)
{
    if (environment.value(QStringLiteral("TW_RESEARCH_LATEXMK_DIRECT")) == QStringLiteral("0") ||
        environment.value(QStringLiteral("TEXLIVE_WINDOWS_TRY_EXTERNAL_PERL")) == QStringLiteral("1") ||
        !environment.value(QStringLiteral("TEXMFSCRIPTS")).isEmpty() ||
        !environment.value(QStringLiteral("TEXLIVE_WINDOWS_EXTERNAL_GS")).isEmpty())
        return false;

    const QFileInfo launcher(program);
    QDir root = launcher.absoluteDir();
    const QString platform = root.dirName().toLower();
    if (launcher.fileName().compare(QStringLiteral("latexmk.exe"), Qt::CaseInsensitive) != 0 ||
        !launcher.isFile() ||
        (platform != QStringLiteral("windows") && platform != QStringLiteral("win32") && platform != QStringLiteral("win64")) ||
        !root.cdUp() || root.dirName().compare(QStringLiteral("bin"), Qt::CaseInsensitive) != 0 ||
        !root.cdUp())
        return false;

    // An installation can request external Perl or Ghostscript in texmf.cnf.
    // Leave those customized installations to their original launcher.
    QFile config(root.filePath(QStringLiteral("texmf.cnf")));
    if (config.open(QIODevice::ReadOnly)) {
        for (QByteArray line : config.read(65536).split('\n')) {
            line = line.split('%').first().trimmed();
            const int equals = line.indexOf('=');
            if (equals < 0) continue;
            const QByteArray key = line.left(equals).trimmed();
            const QByteArray value = line.mid(equals + 1).trimmed();
            if ((key == "TEXLIVE_WINDOWS_TRY_EXTERNAL_PERL" && value == "1") ||
                (key == "TEXLIVE_WINDOWS_EXTERNAL_GS" && !value.isEmpty()) ||
                key == "TEXMFSCRIPTS")
                return false;
        }
    }

    const QString perl = root.filePath(QStringLiteral("tlpkg/tlperl/bin/perl.exe"));
    const QString script = root.filePath(QStringLiteral("texmf-dist/scripts/latexmk/latexmk.pl"));
    if (!QFileInfo(perl).isFile() || !QFileInfo(script).isFile() || !QFileInfo(script).isReadable())
        return false;

    // Match runscript.tlu's bundled-Perl environment. Keep TeX and helper tools
    // from the selected distribution ahead of unrelated installations in PATH.
    environment.insert(QStringLiteral("PERL5LIB"), root.filePath(QStringLiteral("tlpkg/tlperl/lib")));
    const QStringList perlVariables = QStringLiteral(
        "PERL5OPT;PERLIO;PERLIO_DEBUG;PERLLIB;PERL5DB;PERL5DB_THREADED;"
        "PERL5SHELL;PERL_ALLOW_NON_IFS_LSP;PERL_DEBUG_MSTATS;PERL_DESTRUCT_LEVEL;"
        "PERL_DL_NONLAZY;PERL_ENCODING;PERL_HASH_SEED;PERL_HASH_SEED_DEBUG;"
        "PERL_ROOT;PERL_SIGNALS;PERL_UNICODE").split(QLatin1Char(';'));
    for (const QString &variable : perlVariables)
        environment.remove(variable);
    QStringList paths{launcher.absolutePath(), root.filePath(QStringLiteral("tlpkg/tlperl/bin"))};
    const QString ghostscript = root.filePath(QStringLiteral("tlpkg/tlgs/bin"));
    if (QDir(ghostscript).exists()) paths.append(ghostscript);
    const QString oldPath = environment.value(QStringLiteral("PATH"));
    if (!oldPath.isEmpty()) paths.append(oldPath);
    environment.insert(QStringLiteral("PATH"), paths.join(QLatin1Char(';')));
    program = perl;
    arguments.prepend(script);
    return true;
}

} // namespace Utils
} // namespace Tw
#endif
