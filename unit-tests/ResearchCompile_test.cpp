// Regression checks for the native compilation path and buffered console.
// GPL-2.0-or-later.
#include "utils/TypesetCommand.h"
#include "ui/ConsoleWidget.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QThread>
#include <cstdio>

static QByteArray payload()
{
    return QStringLiteral("中文日志 € 𝄞\n").repeated(20).toUtf8();
}

static int waitForProcess(QProcess &process, int timeoutMs = 60000)
{
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), &loop, &QEventLoop::quit);
    QObject::connect(&process, &QProcess::errorOccurred, &loop, &QEventLoop::quit);
    timeout.start(timeoutMs);
    if (process.state() != QProcess::NotRunning) loop.exec();
    if (process.state() != QProcess::NotRunning) {
        process.kill();
        process.waitForFinished();
        return -1;
    }
    return process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -2;
}

int main(int argc, char **argv)
{
    if (argc > 1 && QByteArray(argv[1]) == "--emit-log") {
        std::fwrite("BEGIN\n", 1, 6, stdout);
        std::fflush(stdout);
        QThread::msleep(120);
        const QByteArray bytes = payload();
        for (int pos = 0; pos < bytes.size(); pos += 3) {
            std::fwrite(bytes.constData() + pos, 1, qMin(3, int(bytes.size()) - pos), stdout);
            std::fflush(stdout);
            QThread::msleep(1);
        }
        std::fwrite("END\n", 1, 4, stderr);
        std::fflush(stderr);
        return 0;
    }
    QApplication app(argc, argv);
    const QStringList arguments = QCoreApplication::arguments();
    if (arguments.size() < 3) return 2;
    QJsonArray checks;
    bool passed = true;
    auto check = [&](const QString &name, bool ok, const QJsonObject &details = {}) {
        QJsonObject result = details;
        result.insert("name", name);
        result.insert("passed", ok);
        checks.append(result);
        passed &= ok;
    };
    const QString launcher = arguments.at(2);
    QString program = launcher;
    QStringList args{QStringLiteral("-pdf"), QStringLiteral("paper with spaces.tex")};
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.remove("TEXLIVE_WINDOWS_TRY_EXTERNAL_PERL");
    env.remove("TEXMFSCRIPTS");
    env.remove("TEXLIVE_WINDOWS_EXTERNAL_GS");
    env.remove("TW_RESEARCH_LATEXMK_DIRECT");
    env.insert("PERL5OPT", "invalid unrelated Perl option");
    check("bundled_latexmk_detected", Tw::Utils::useBundledLatexmk(program, args, env));
    check("arguments_preserved", args.size() == 3 && args.at(1) == "-pdf" && args.at(2) == "paper with spaces.tex");
    check("bundled_perl_environment", program.endsWith("/tlpkg/tlperl/bin/perl.exe") &&
          !env.contains("PERL5OPT") && env.value("PERL5LIB").endsWith("/tlpkg/tlperl/lib") &&
          env.value("PATH").startsWith(QFileInfo(launcher).absolutePath()));

    for (const QString &variable : {QStringLiteral("TW_RESEARCH_LATEXMK_DIRECT"), QStringLiteral("TEXLIVE_WINDOWS_TRY_EXTERNAL_PERL"), QStringLiteral("TEXMFSCRIPTS")}) {
        program = launcher;
        args = {"-pdf", "paper.tex"};
        const QStringList before = args;
        auto customized = env;
        customized.insert(variable, variable == "TW_RESEARCH_LATEXMK_DIRECT" ? "0" : "1");
        check("custom_override_" + variable, !Tw::Utils::useBundledLatexmk(program, args, customized) && program == launcher && args == before);
    }
    program = QFileInfo(launcher).absoluteDir().filePath("pdflatex.exe");
    check("other_engines_unchanged", !Tw::Utils::useBundledLatexmk(program, args, env));
    QTemporaryDir temp;
    QDir().mkpath(temp.filePath("bin/windows"));
    const QString incomplete = temp.filePath("bin/windows/latexmk.exe");
    QFile empty(incomplete);
    if (!empty.open(QIODevice::WriteOnly)) return 3;
    empty.close();
    program = incomplete;
    check("incomplete_installation_falls_back", !Tw::Utils::useBundledLatexmk(program, args, env) && program == incomplete);

    {
        Tw::UI::ConsoleWidget console;
        QProcess process;
        process.setProcessChannelMode(QProcess::MergedChannels);
        console.setProcess(&process);
        int reads = 0, layouts = 0;
        QObject::connect(&process, &QProcess::readyReadStandardOutput, &app, [&]() { ++reads; });
        QObject::connect(&console, &QTextEdit::textChanged, &app, [&]() { ++layouts; });
        process.start(QCoreApplication::applicationFilePath(), {"--emit-log"});
        const int exit = waitForProcess(process);
        check("complete_utf8_and_final_stderr", exit == 0 && console.toPlainText().toUtf8() == "BEGIN\n" + payload() + "END\n");
        check("console_updates_batched", layouts < reads, {{"pipe_reads", reads}, {"text_updates", layouts}});
        check("console_has_no_undo_history", !console.isUndoRedoEnabled());
    }
    {
        Tw::UI::ConsoleWidget console;
        QProcess process;
        process.setProcessChannelMode(QProcess::MergedChannels);
        console.setProcess(&process);
        bool echoAdded = false;
        QObject::connect(&process, &QProcess::readyReadStandardOutput, &app, [&]() {
            if (!echoAdded) { echoAdded = true; console.echo("input\n"); }
        });
        process.start(QCoreApplication::applicationFilePath(), {"--emit-log"});
        check("echo_preserves_output_order", waitForProcess(process) == 0 &&
              console.toPlainText().toUtf8() == "BEGIN\ninput\n" + payload() + "END\n");
        console.setProcess(nullptr, false);
        check("detach_keeps_log", console.toPlainText().toUtf8() == "BEGIN\ninput\n" + payload() + "END\n");
    }
    {
        Tw::UI::ConsoleWidget console;
        auto *process = new QProcess;
        process->setProcessChannelMode(QProcess::MergedChannels);
        console.setProcess(process);
        QObject::connect(process, &QProcess::readyReadStandardOutput, &app, [&]() { process->kill(); });
        process->start(QCoreApplication::applicationFilePath(), {"--emit-log"});
        waitForProcess(*process);
        delete process;
        check("interruption_and_destruction_keep_log", console.process() == nullptr && console.toPlainText().startsWith("BEGIN\n"));
    }
    // Additional arguments are isolated paper copies. Exercise the very same
    // command resolver with an event-driven QProcess and the real console.
    for (int i = 3; i < arguments.size(); ++i) {
        // Windows' narrow CRT argv loses supplementary characters. Qt's
        // argument list retains the Unicode command line used by the editor.
        const QFileInfo input(arguments.at(i));
        program = launcher;
        args = {"-pdf", "-synctex=1", "-interaction=nonstopmode", "-file-line-error", input.fileName()};
        if (input.baseName().startsWith("xe-")) args[0] = "-xelatex";
        if (input.baseName().startsWith("lua-")) args[0] = "-lualatex";
        auto buildEnv = QProcessEnvironment::systemEnvironment();
        buildEnv.insert("LC_ALL", "C"); buildEnv.insert("LANG", "C");
        const bool direct = Tw::Utils::useBundledLatexmk(program, args, buildEnv);
        Tw::UI::ConsoleWidget console;
        QProcess process;
        process.setWorkingDirectory(input.absolutePath());
        process.setProcessEnvironment(buildEnv);
        process.setProcessChannelMode(QProcess::MergedChannels);
        console.setProcess(&process);
        QElapsedTimer timing; timing.start();
        process.start(program, args);
        const int exit = waitForProcess(process, 120000);
        console.flushOutput();
        QFile output(input.absoluteDir().filePath(input.completeBaseName() + "-native-console.txt"));
        if (!output.open(QIODevice::WriteOnly)) return 3;
        output.write(console.toPlainText().toUtf8());
        const bool errorFixture = input.baseName().startsWith("error-");
        check("native_build_" + input.baseName(), direct && (errorFixture ? exit != 0 && console.toPlainText().contains("Undefined control sequence") : exit == 0),
              {{"exit", exit}, {"elapsed_ms", timing.elapsed()}, {"log_characters", int(console.toPlainText().size())}});
    }
    QFile report(arguments.at(1));
    if (!report.open(QIODevice::WriteOnly)) return 3;
    report.write(QJsonDocument(QJsonObject{{"passed", passed}, {"checks", checks}}).toJson());
    return passed ? 0 : 1;
}
