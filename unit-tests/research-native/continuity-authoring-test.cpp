// Regression paths discovered by the writing-continuity review. Real widgets,
// independent documents, private settings and an inactive desktop only.
#include "ActualAuthoringRecording.h"
#include "ResearchWritingContext.h"
#include "Engine.h"
#include "PDFDocumentWindow.h"

using namespace Research;

static void inWritingDialog(QWidget *parent, const QString &name,
                            const std::function<void(QDialog *)> &operation) {
  auto *timer = new QTimer(parent);
  QObject::connect(timer, &QTimer::timeout, timer, [=] {
    auto *dialog = parent->findChild<QDialog *>(name);
    if (!dialog || !dialog->isVisible()) return;
    timer->stop(); timer->deleteLater();
    QTimer::singleShot(20000, dialog, &QDialog::reject);
    operation(dialog);
  });
  timer->start(15);
}

int main(int argc, char **argv) {
  const auto report = argument(1);
  if (!qEnvironmentVariable("TEXLINE_TEST_DESKTOP").startsWith("TeXlineValidation-")) return 2;
  QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
  argc = 1; TWApp app(argc, argv); app.setQuitOnLastWindowClosed(false);
  QTimer::singleShot(80, &app, [&] {
    const auto folder = QFileInfo(report).absolutePath();
    QJsonArray checks, builds; int failures = 0;
    auto check = [&](const QString &name, bool passed) {
      checks.append(QJsonObject{{"name", name}, {"passed", passed}}); failures += !passed;
      put(report, QJsonDocument(QJsonObject{{"checks", checks}, {"failures", failures},
        {"devicePixelRatio", qApp->primaryScreen()->devicePixelRatio()},
        {"physicalInput", false}, {"realUserTrial", false}, {"builds", builds}}).toJson());
    };
    Tw::Settings().setValue("openPDFwithTeX", false);
    const auto root = folder + "/paper/main.tex", bib = folder + "/paper/refs.bib";
    const QString bibliography = "@article{A,title={Alpha study},author={Able, Ada},year={2024}}\n"
      "@article{B,title={Beta study},author={Baker, Ben},year={2025}}\n"
      "@article{C,title={Gamma study},author={Chen, Chi},year={2026}}\n";
    const QString bare = "\\documentclass{article}\n\\usepackage{amsmath}\n\\begin{document}\n"
      "\\section{Proof}\nFirst argument.\n\nA stable middle paragraph.\n\nLast argument.\n\\end{document}\n";
    QString linked = bare; linked.insert(linked.indexOf("\\end{document}"), "\\bibliographystyle{plain}\n\\bibliography{refs}\n");
    put(root, linked.toUtf8()); put(bib, bibliography.toUtf8()); QByteArray expectedDisk = linked.toUtf8();
    auto *window = new TeXDocumentWindow(root);
    window->resize(qEnvironmentVariableIsSet("TEXLINE_TEST_HALF") ? 620 : 1040, 740);
    window->show(); settle(); QApplication::setActiveWindow(window); window->editor()->setFocus();
    if (qEnvironmentVariableIsSet("TEXLINE_TEST_FOCUS")) { setFocusMode(true); settle(300); }
    auto select = [&](int from, int to) { QTextCursor cursor(window->textDoc()); cursor.setPosition(from); cursor.setPosition(to, QTextCursor::KeepAnchor); window->editor()->setTextCursor(cursor); };
    auto openCitation = [&]() -> QDialog * {
      auto *action = window->findChild<QAction *>("researchInsertCitation"); if (action) action->trigger();
      auto *dialog = window->findChild<QDialog *>("researchCitationDialog");
      check("citation picker opens", dialog);
      return dialog;
    };
    // Use the installed product profile (latexmk) through the normal Compile
    // command. One command must run BibTeX/Biber and all required LaTeX passes.
    // Do not substitute a single pdflatex call or manually run bibliography tools.
    auto citeAndCompile = [&](const QString &source, const QString &name) {
      const auto compileRoot = window->fileName();
      select(source.indexOf("First argument"), source.indexOf("First argument"));
      auto *dialog = openCitation(); if (!dialog) return;
      auto *list = dialog->findChild<QListWidget *>("researchCitationList");
      check(name + ": linked references ready for insertion", waitFor([&] { return list->count() == 3; }));
      dialog->findChild<QLineEdit *>("researchCitationSearch")->setText("Gamma");
      auto *apply = dialog->findChild<QDialogButtonBox *>("researchCitationButtons")->button(QDialogButtonBox::Ok);
      check(name + ": reference can be selected and inserted", list->count() == 1 && apply->isEnabled());
      if (!apply->isEnabled()) { dialog->reject(); settle(); return; }
      apply->click(); settle();
      check(name + ": citation comes from the selected paper", window->textDoc()->editorText().contains("{C}"));
      QJsonArray profiles; bool automaticProfile = false;
      for (const auto &engine : app.getEngineList()) {
        profiles.append(QJsonObject{{"name", engine.name()}, {"program", engine.program()}, {"arguments", QJsonArray::fromStringList(engine.arguments())}});
        if (engine.name() == "pdfLaTeX") automaticProfile = QFileInfo(engine.program()).completeBaseName().compare("latexmk", Qt::CaseInsensitive) == 0 && engine.arguments().contains("-pdf");
      }
      check(name + ": existing pdfLaTeX profile drives automatic bibliography compilation", automaticProfile);
      auto *compile = window->findChild<QAction *>("actionTypeset");
      const bool invoked = compile && compile->isEnabled();
      check(name + ": normal Compile action is available", invoked);
      QElapsedTimer elapsed; elapsed.start(); if (invoked) compile->trigger();
      const bool finished = waitFor([&] { return !window->isTypesetting() && !window->property("researchBuildPreparing").toBool() && !window->property("researchBuildFinishing").toBool() && !window->property("researchPublishing").toBool(); }, 120000);
      const auto pdf = previewFile(compileRoot); auto document = QtPDF::Backend::Document::newDocument(pdf);
      builds.append(QJsonObject{{"name", name}, {"source", compileRoot}, {"normalCompileInvocations", invoked ? 1 : 0}, {"milliseconds", double(elapsed.elapsed())}, {"finished", finished}, {"profiles", profiles}, {"metadata", currentBuildMetadata(compileRoot)}});
      put(folder + "/" + name + "-compile.txt", window->property("consoleOutput").toString().toUtf8());
      check(name + ": one normal compilation produces a current valid PDF", finished && pdfPairValid(pdf) && pdfRevision(pdf, compileRoot) == PdfRevision::Current && document && document->isValid());
      check(name + ": bibliography entry is present in the actual PDF", contains(document, "Gamma study") && contains(document, "Chen"));
      check(name + ": PDF does not retain an unresolved citation", !contains(document, "[?]"));
      expectedDisk = bytes(compileRoot);
      if (finished && pdfPairValid(pdf)) {
        window->findChild<QAction *>("actionGo_to_Preview")->trigger(); settle(350);
        check(name + ": resulting PDF opens through the normal preview action", window->pdfDocument() && window->pdfDocument()->isVisible());
        if (auto *viewer = window->pdfDocument()) { viewer->grab().save(folder + "/" + name + "-bibliography-pdf.png"); viewer->hide(); }
      }
      window->show(); QApplication::setActiveWindow(window); window->editor()->setFocus();
    };
    select(linked.indexOf("First argument"), linked.indexOf("First argument"));
    if (QPointer<QDialog> dialog = openCitation()) {
      auto *list = dialog->findChild<QListWidget *>("researchCitationList");
      auto *search = dialog->findChild<QLineEdit *>("researchCitationSearch");
      auto *notice = dialog->findChild<QLabel *>("researchCitationNotice");
      auto *apply = dialog->findChild<QDialogButtonBox *>("researchCitationButtons")->button(QDialogButtonBox::Ok);
      check("initial references load", waitFor([&] { return list->count() == 3 && apply->isEnabled(); }));
      search->setText("Alpha"); check("matching search has its candidate", list->count() == 1 && apply->isEnabled());
      search->setText("no-paper-has-this-name");
      // Text changes queue a layout request; capture only after the native
      // layout has assigned the notice its actual painted height.
      settle();
      check("no results disable implicit citation insertion", list->count() == 0 && !apply->isEnabled());
      check("no-results feedback has visible readable geometry", notice && !notice->text().trimmed().isEmpty() && notice->isVisible() && notice->height() >= notice->fontMetrics().height() && notice->contentsRect().height() >= notice->fontMetrics().height());
      dialog->grab().save(folder + "/citation-no-results.png");
      const auto before = window->textDoc()->editorText(); key(search, Qt::Key_Return, Qt::NoModifier, "\r"); settle();
      check("Enter on empty search leaves source unchanged", window->textDoc()->editorText() == before);
      check("empty search keeps the picker available for recovery", dialog && dialog->isVisible());
      if (dialog && dialog->isVisible()) {
        search->clear(); settle();
        check("clearing search restores candidates", list->count() == 3 && apply->isEnabled());
        check("clearing search removes no-results feedback", notice && notice->text().isEmpty());
        dialog->grab().save(folder + "/citation-search-restored.png");
        if (apply->isEnabled()) apply->click(); settle();
        check("cleared search inserts the current reference normally", window->textDoc()->editorText().contains("\\cite{A}") && window->textDoc()->editorText() != before && (!dialog || !dialog->isVisible()));
        if (dialog && dialog->isVisible()) { dialog->reject(); settle(); }
        window->editor()->undo();
        check("citation inserted after search recovery is one undo", window->textDoc()->editorText() == before);
      }
    }
    QString cited = linked; const int citeAt = cited.indexOf("First argument"); cited.insert(citeAt, "\\cite{C} ");
    window->textDoc()->setPlainText(cited); select(citeAt + 7, citeAt + 7);
    if (auto *dialog = openCitation()) {
      auto *list = dialog->findChild<QListWidget *>("researchCitationList");
      check("existing citation loads", waitFor([&] { return list->count() == 3; }));
      check("existing citation current row and selection agree", list->currentItem() && list->currentItem()->data(Qt::UserRole).toString() == "C" && list->selectedItems().size() == 1 && list->selectedItems().first() == list->currentItem());
      check("existing citation details describe cited paper", dialog->findChild<QLabel *>("researchCitationTitle")->text() == "Gamma study");
      settle();
      dialog->grab().save(folder + "/citation-existing-C.png");
      dialog->findChild<QLineEdit *>("researchCitationSearch")->setText("Alpha");
      settle();
      const auto *notice = dialog->findChild<QLabel *>("researchCitationNotice");
      check("retained citation outside search is visible in feedback", notice && notice->text().contains("C") && notice->isVisible() && notice->contentsRect().height() >= notice->fontMetrics().height());
      key(dialog, Qt::Key_Escape); settle();
      check("citation Escape preserves source and returns focus", window->textDoc()->editorText() == cited && window->editor()->hasFocus());
    }
    QString missing = bare; missing.insert(missing.indexOf("\\end{document}"), "\\bibliography{absent-references}\n");
    window->textDoc()->setPlainText(missing); select(missing.indexOf("First argument"), missing.indexOf("First argument"));
    if (auto *dialog = openCitation()) {
      auto *notice = dialog->findChild<QLabel *>("researchCitationNotice");
      check("missing bibliography reports the actual file", waitFor([&] { return notice->text().contains("absent-references"); }));
      settle();
      check("missing bibliography feedback is visibly laid out", notice->isVisible() && notice->height() >= notice->fontMetrics().height());
      dialog->grab().save(folder + "/citation-missing-file.png"); dialog->reject(); settle();
    }
    // Exercise the native file chooser and inspect the resulting source transaction.
    int linkCase = 0;
    auto linkFile = [&](const QString &source, const QString &name) {
      // Each first-link case needs a genuinely fresh main file. Reusing one
      // compiled path retains recorded bibliography dependencies and .bbl
      // products, turning this into an unrelated backend-migration test.
      if (auto *viewer = window->pdfDocument()) viewer->close();
      window->textDoc()->setModified(false); window->close(); settle();
      const auto caseFile = folder + QString("/paper/link-%1.tex").arg(++linkCase);
      put(caseFile, source.toUtf8()); expectedDisk = source.toUtf8();
      window = new TeXDocumentWindow(caseFile);
      window->resize(qEnvironmentVariableIsSet("TEXLINE_TEST_HALF") ? 620 : 1040, 740);
      window->show(); QApplication::setActiveWindow(window); window->editor()->setFocus(); settle();
      select(source.indexOf("First argument"), source.indexOf("First argument"));
      auto *dialog = openCitation(); if (!dialog) return QString();
      auto *attach = dialog->findChild<QToolButton *>("researchCitationAttach");
      check(name + ": empty picker offers bibliography linking", waitFor([&] { return attach->isVisible(); }));
      QTimer picker; picker.setInterval(40); QElapsedTimer clock; clock.start();
      QObject::connect(&picker, &QTimer::timeout, &app, [&] {
        for (auto *widget : QApplication::allWidgets()) if (auto *file = qobject_cast<QFileDialog *>(widget); file && file->isVisible()) {
          file->setDirectory(QFileInfo(bib).absolutePath());
          if (auto *field = file->findChild<QLineEdit *>("fileNameEdit")) { field->setText("refs.bib"); QMetaObject::invokeMethod(file, "accept", Qt::QueuedConnection); }
          if (clock.elapsed() > 8000) file->reject();
        }
      });
      picker.start(); attach->click(); picker.stop();
      auto *list = dialog->findChild<QListWidget *>("researchCitationList");
      check(name + ": linked entries appear in the same picker", waitFor([&] { return list->count() == 3; }));
      const auto result = window->textDoc()->editorText(); dialog->reject(); settle();
      window->editor()->undo(); check(name + ": linking setup is one undo", window->textDoc()->editorText() == source);
      window->editor()->redo(); check(name + ": linking setup has redo", window->textDoc()->editorText() == result);
      return result;
    };
    const auto firstLinked = linkFile(bare, "minimal article");
    check("first standard article receives a complete BibTeX setup", firstLinked.count("\\bibliographystyle{plain}") == 1 && firstLinked.contains("\\bibliography{refs}"));
    put(folder + "/first-linked-source.tex", firstLinked.toUtf8());
    citeAndCompile(firstLinked, "first-bibtex");
    auto natbib = bare; natbib.replace("{amsmath}", "{amsmath,natbib}");
    const auto natbibLinked = linkFile(natbib, "minimal natbib article");
    check("natbib uses its compatible standard style", natbibLinked.count("\\bibliographystyle{plainnat}") == 1);
    auto custom = bare; custom.replace("{article}", "{myjournal}");
    const auto customLinked = linkFile(custom, "custom class");
    check("custom class implicit style is not overridden", !customLinked.contains("\\bibliographystyle"));
    auto existingStyle = bare; existingStyle.insert(existingStyle.indexOf("\\begin{document}"), "\\bibliographystyle{unsrt}\n");
    const auto styleLinked = linkFile(existingStyle, "existing style");
    check("explicit bibliography style stays unchanged", styleLinked.count("\\bibliographystyle") == 1 && styleLinked.contains("\\bibliographystyle{unsrt}"));
    put(folder + "/paper/journal.sty", "\\ProvidesPackage{journal}\n\\bibliographystyle{unsrt}\n");
    auto localPackage = bare; localPackage.replace("{amsmath}", "{amsmath,journal}");
    const auto localLinked = linkFile(localPackage, "local template package");
    check("local template package style is not overridden", !localLinked.contains("\\bibliographystyle"));
    auto biblatex = bare; biblatex.replace("{amsmath}", "{amsmath,biblatex}");
    biblatex.insert(biblatex.indexOf("\\end{document}"), "\\printbibliography[heading=none]\n");
    const auto biblatexLinked = linkFile(biblatex, "existing biblatex layout");
    check("biblatex resource keeps its existing output configuration", biblatexLinked.contains("\\addbibresource{refs.bib}") && !biblatexLinked.contains("\\bibliographystyle") && biblatexLinked.count("\\printbibliography[heading=none]") == 1);
    citeAndCompile(biblatexLinked, "linked-biber");
    // Save one local version, open its comparison and change the live source
    // while the dialog is open, as an external update can do.
    window->textDoc()->setPlainText(bare);
    const int start = bare.indexOf("First argument"), end = bare.indexOf("\\end{document}"); select(start, end);
    inWritingDialog(window, "researchLocalVersionsDialog", [&](QDialog *dialog) {
      auto *save = dialog->findChild<QPushButton *>("researchSavedWritingSave");
      check("version dialog loads", waitFor([&] { return save->isEnabled(); }));
      dialog->findChild<QLineEdit *>("researchSavedWritingName")->setText("Original proof"); save->click();
      check("one local version saved", waitFor([&] { return dialog->findChild<QListWidget *>("researchSavedWritingList")->count() == 1; }));
      dialog->reject();
    });
    window->findChild<QAction *>("researchLocalVersions")->trigger();
    auto revised = bare; revised.replace("First argument.", "Revised argument."); window->textDoc()->setPlainText(revised);
    inWritingDialog(window, "researchLocalVersionsDialog", [&](QDialog *dialog) {
      auto *restore = dialog->findChild<QPushButton *>("researchSavedWritingRestore");
      auto *list = dialog->findChild<QListWidget *>("researchSavedWritingList");
      check("single-record comparison becomes available", waitFor([&] { return restore->isEnabled(); }));
      QTextCursor change(window->textDoc()); change.setPosition(window->textDoc()->editorText().indexOf("Last argument")); change.insertText("An added last step. ");
      restore->click(); check("stale comparison never restores old offsets", window->textDoc()->editorText().contains("An added last step."));
      if (list->currentItem()) QMetaObject::invokeMethod(list, "itemClicked", Q_ARG(QListWidgetItem *, list->currentItem()));
      check("same single record can refresh without reopening", waitFor([&] { return restore->isEnabled(); }));
      settle(); dialog->grab().save(folder + "/local-version-refreshed.png");
      const auto beforeRestore = window->textDoc()->editorText(); restore->click(); settle();
      check("refreshed restore returns the intended passage", window->textDoc()->editorText() == bare);
      window->editor()->undo(); check("refreshed restoration is one undo", window->textDoc()->editorText() == beforeRestore);
      if (dialog->isVisible()) dialog->reject();
    });
    window->findChild<QAction *>("researchLocalVersions")->trigger(); settle();
    check("later draft operations leave the intentionally compiled private source unchanged on disk", bytes(window->fileName()) == expectedDisk);
    check("initial picker-only manuscript remains unchanged on disk", bytes(root) == linked.toUtf8());
    const auto documents = TeXDocumentWindow::documentList();
    for (auto *source : documents) source->textDoc()->setModified(false);
    for (auto *source : documents) {
      put(folder + "/shutdown.json", QJsonDocument(QJsonObject{{"phase", "closing private document"}, {"file", source->fileName()}}).toJson());
      source->close();
    }
    settle(); put(folder + "/shutdown.json", QJsonDocument(QJsonObject{{"phase", "requesting application exit"}}).toJson()); app.exit(failures ? 1 : 0);
  });
  return app.exec();
}
