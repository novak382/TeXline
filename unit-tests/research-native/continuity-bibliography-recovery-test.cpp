// Exercise a bibliography backend change through the existing native engine menu.
// No cache removal or direct compiler invocation: private sample and settings only.
#include "ActualAuthoringRecording.h"
#include "ResearchWorkflow.h"
#include "Engine.h"
#include "PDFDocumentWindow.h"

using namespace Research;

int main(int argc, char **argv) {
  const auto report = argument(1);
  if (!qEnvironmentVariable("TEXLINE_TEST_DESKTOP").startsWith("TeXlineValidation-")) return 2;
  argc = 1; TWApp app(argc, argv); app.setQuitOnLastWindowClosed(false);
  QTimer::singleShot(80, &app, [&] {
    const auto folder = QFileInfo(report).absolutePath();
    const auto root = folder + "/paper/main.tex", bib = folder + "/paper/refs.bib";
    QJsonArray checks, builds; int failures = 0; bool timedOut = false;
    auto check = [&](const QString &name, bool passed) {
      checks.append(QJsonObject{{"name", name}, {"passed", passed}}); failures += !passed;
      put(report, QJsonDocument(QJsonObject{{"checks", checks}, {"failures", failures}, {"builds", builds},
        {"devicePixelRatio", qApp->primaryScreen()->devicePixelRatio()}, {"physicalInput", false}, {"realUserTrial", false}}).toJson());
    };
    const QByteArray references = "@article{old,title={Earlier bibliography result},author={Able, Ada},journal={Journal of Examples},year={2024}}\n"
      "@article{new,title={Revised bibliography result},author={Chen, Chi},journal={Journal of Examples},year={2026}}\n";
    const QString original = "\\documentclass{article}\n\\begin{document}\n"
      "Original argument \\cite{old}.\n\\bibliographystyle{plain}\n\\bibliography{refs}\n\\end{document}\n";
    const QString revised = "\\documentclass{article}\n\\usepackage[backend=biber]{biblatex}\n\\addbibresource{refs.bib}\n"
      "\\begin{document}\nRevised argument \\autocite{new}.\n\\printbibliography\n\\end{document}\n";
    // A fresh report directory is required. Reusing it would hide the initial state.
    check("sample starts at a fresh private path", !QFileInfo::exists(root));
    if (QFileInfo::exists(root)) { app.exit(1); return; }
    check("private source and bibliography are written", put(root, original.toUtf8()) && put(bib, references));
    Tw::Settings().setValue("openPDFwithTeX", false);
    QPointer<TeXDocumentWindow> window = new TeXDocumentWindow(root);
    QTimer overall; overall.setSingleShot(true);
    QObject::connect(&overall, &QTimer::timeout, &app, [&] {
      timedOut = true; QJsonArray modals;
      for (auto *widget : QApplication::allWidgets()) if (auto *dialog = qobject_cast<QDialog *>(widget); dialog && dialog->isVisible()) {
        modals.append(QJsonObject{{"object", dialog->objectName()}, {"title", dialog->windowTitle()}}); dialog->reject();
      }
      put(folder + "/watchdog.json", QJsonDocument(QJsonObject{{"visibleDialogs", modals}}).toJson());
      check("bibliography recovery completes within the watchdog", false);
      if (window) QMetaObject::invokeMethod(window, "interrupt", Qt::DirectConnection);
      app.exit(1);
    });
    overall.start(240000);
    window->resize(qEnvironmentVariableIsSet("TEXLINE_TEST_HALF") ? 620 : 1040, 740);
    window->show(); settle(300); QApplication::setActiveWindow(window); window->editor()->setFocus();
    auto idle = [&] { return !window || (!window->isTypesetting() && !window->property("researchBuildPreparing").toBool() &&
      !window->property("researchBuildFinishing").toBool() && !window->property("researchPublishing").toBool()); };
    auto finish = [&] {
      if (!idle() && window) { QMetaObject::invokeMethod(window, "interrupt", Qt::DirectConnection); waitFor(idle, 10000); }
      overall.stop();
      if (window && idle()) { if (auto *pdf = window->pdfDocument()) pdf->close(); window->textDoc()->setModified(false); window->close(); }
      settle(); app.exit(failures ? 1 : 0);
    };
    auto *pdfEngine = window->findChild<QAction *>("actionTypesetToolpdfLaTeX");
    auto *biberEngine = window->findChild<QAction *>("actionTypesetToolBiber");
    auto *compile = window->findChild<QAction *>("actionTypeset");
    check("existing pdfLaTeX, Biber and Compile menu commands are available", pdfEngine && pdfEngine->isEnabled() && biberEngine && biberEngine->isEnabled() && compile && compile->isEnabled());
    if (!pdfEngine || !biberEngine || !compile || !pdfEngine->isEnabled() || !biberEngine->isEnabled()) { finish(); return; }
    QJsonArray profiles; bool automatic = false;
    for (const auto &engine : app.getEngineList()) {
      profiles.append(QJsonObject{{"name", engine.name()}, {"program", engine.program()}, {"arguments", QJsonArray::fromStringList(engine.arguments())}});
      if (engine.name() == "pdfLaTeX") automatic = QFileInfo(engine.program()).completeBaseName().compare("latexmk", Qt::CaseInsensitive) == 0 && engine.arguments().contains("-pdf");
    }
    put(folder + "/profiles.json", QJsonDocument(profiles).toJson());
    check("normal pdfLaTeX profile uses the existing automatic build", automatic);
    auto run = [&](QAction *engine, const QString &name) {
      engine->trigger(); check(name + ": engine selection is reflected in the ordinary menu", engine->isChecked());
      const bool invoked = compile->isEnabled(); QElapsedTimer elapsed; elapsed.start();
      if (invoked) compile->trigger();
      const bool finished = waitFor([&] { return timedOut || idle(); }, 120000) && !timedOut && idle();
      const auto log = window->property("consoleOutput").toString();
      put(folder + "/" + name + ".txt", log.toUtf8());
      builds.append(QJsonObject{{"name", name}, {"engine", engine->text()}, {"normalCompileInvocations", invoked ? 1 : 0},
        {"finished", finished}, {"milliseconds", double(elapsed.elapsed())}, {"metadata", currentBuildMetadata(root)}});
      check(name + ": one normal compile operation finishes", invoked && finished);
      return finished;
    };
    auto validCurrentPdf = [&] {
      const auto pdf = previewFile(root); const auto document = QtPDF::Backend::Document::newDocument(pdf);
      return pdfPairValid(pdf) && pdfRevision(pdf, root) == PdfRevision::Current && document && document->isValid();
    };
    if (!run(pdfEngine, "01-bibtex")) { finish(); return; }
    check("initial BibTeX document has a current valid PDF", validCurrentPdf());
    auto document = QtPDF::Backend::Document::newDocument(previewFile(root));
    check("initial PDF contains only the original reference", contains(document, "Earlier bibliography result") && !contains(document, "Revised bibliography result") && !contains(document, "[?]"));
    document.clear();
    if (!validCurrentPdf()) { finish(); return; }
    const QString outputDir = currentBuildMetadata(root).value("outputDir").toString();
    check("normal build has produced a bibliography cache", !outputDir.isEmpty() && QFileInfo(outputDir + "/main.bbl").isFile());
    // This is the sole intentional source change. Recovery must preserve it exactly.
    QTextCursor cursor(window->textDoc()); cursor.select(QTextCursor::Document); cursor.insertText(revised);
    if (!run(pdfEngine, "02-biblatex-old-cache")) { finish(); return; }
    const auto failureLog = window->property("consoleOutput").toString();
    check("same-path backend change exposes the old bibliography format", failureLog.simplified().contains("not created by biblatex"));
    check("failed backend change does not mark the old PDF current", pdfRevision(previewFile(root), root) != PdfRevision::Current);
    bool explained = false; QJsonArray issues;
    for (const auto &issue : compileIssues(root, failureLog, true, outputDir + "/main.log")) {
      const auto explanation = issueExplanation(issue, false), english = issueExplanation(issue, true);
      issues.append(QJsonObject{{"message", issue.message}, {"explanation", explanation}, {"english", english}});
      if (issue.message.contains("not created by biblatex")) explained = explanation.contains("Biber") && english.contains("Biber");
    }
    put(folder + "/actual-failure-issues.json", QJsonDocument(issues).toJson());
    check("actual failure receives the actionable Biber recovery explanation", explained);
    auto *summary = window->findChild<QTextBrowser *>("researchProblems");
    check("native error panel shows the same recovery step", summary && summary->isVisible() && summary->toPlainText().contains("Biber"));
    window->grab().save(folder + "/old-bibliography-error.png");
    check("failed compilation saves but does not rewrite the requested source", bytes(root) == revised.toUtf8() && window->textDoc()->editorText() == revised && bytes(bib) == references);
    if (!run(biberEngine, "03-biber-recovery")) { finish(); return; }
    check("manual Biber updates the existing cache in place", bytes(outputDir + "/main.bbl").contains("biblatex"));
    check("Biber recovery preserves source and bibliography", bytes(root) == revised.toUtf8() && window->textDoc()->editorText() == revised && bytes(bib) == references);
    if (!run(pdfEngine, "04-original-engine-recovery")) { finish(); return; }
    check("original engine is selected again after recovery", pdfEngine->isChecked());
    check("recovery produces a current valid PDF", validCurrentPdf());
    document = QtPDF::Backend::Document::newDocument(previewFile(root));
    check("recovered PDF contains the new reference and no old entry", contains(document, "Revised bibliography result") && contains(document, "Chen") && !contains(document, "Earlier bibliography result"));
    check("recovered PDF has no unresolved citation", document && document->isValid() && !contains(document, "[?]"));
    document.clear();
    check("recovery never changes source or reference data", bytes(root) == revised.toUtf8() && window->textDoc()->editorText() == revised && bytes(bib) == references);
    if (validCurrentPdf()) {
      if (auto *preview = window->findChild<QAction *>("actionGo_to_Preview")) preview->trigger();
      settle(350); auto *pdf = window->pdfDocument(); check("recovered PDF opens normally", pdf && pdf->isVisible());
      if (pdf) pdf->grab().save(folder + "/recovered-bibliography-pdf.png");
    }
    finish();
  });
  return app.exec();
}
