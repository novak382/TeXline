// One named writing place, restored after closing the real source and PDF
// windows. Independent files/settings and an inactive desktop only.
#include "ActualAuthoringRecording.h"
#include "ResearchProject.h"
#include "ResearchWritingContext.h"
#include "PDFDocumentWindow.h"
#include <QTextBlock>

using namespace Research;

static bool useSceneDialog(TeXDocumentWindow *window, const char *command,
                           const std::function<void(QDialog *)> &operation) {
  bool seen = false;
  QTimer discover, watchdog;
  discover.setInterval(15); watchdog.setSingleShot(true);
  QObject::connect(&discover, &QTimer::timeout, window, [&] {
    auto *dialog = window->findChild<QDialog *>("researchWritingContextsDialog");
    if (!dialog || !dialog->isVisible()) return;
    seen = true; discover.stop(); operation(dialog);
    if (dialog->isVisible()) dialog->reject();
  });
  QObject::connect(&watchdog, &QTimer::timeout, window, [window] {
    if (auto *dialog = window->findChild<QDialog *>("researchWritingContextsDialog")) dialog->reject();
  });
  auto *action = window->findChild<QAction *>(command);
  if (!action || !action->isEnabled()) return false;
  discover.start(); watchdog.start(25000); action->trigger();
  discover.stop(); watchdog.stop(); return seen;
}

int main(int argc, char **argv) {
  const auto report = argument(1);
  if (!qEnvironmentVariable("TEXLINE_TEST_DESKTOP").startsWith("TeXlineValidation-")) return 2;
  argc = 1; TWApp app(argc, argv); app.setQuitOnLastWindowClosed(false);
  QTimer::singleShot(80, &app, [&] {
    const auto folder = QFileInfo(report).absolutePath();
    QJsonArray checks; int failures = 0;
    auto check = [&](const QString &name, bool passed) {
      checks.append(QJsonObject{{"name", name}, {"passed", passed}}); failures += !passed;
      put(report, QJsonDocument(QJsonObject{{"checks", checks}, {"failures", failures},
        {"devicePixelRatio", qApp->primaryScreen()->devicePixelRatio()},
        {"physicalInput", false}, {"realUserTrial", false}}).toJson());
    };
    Tw::Settings().setValue("openPDFwithTeX", false);
    const auto file = folder + "/paper/main.tex";
    QString source = "\\documentclass{article}\n\\usepackage{amsmath,amsthm}\n"
      "\\newtheorem{theorem}{Theorem}\n\\begin{document}\n\\section{Setup}\n"
      "\\begin{theorem}\\label{thm:bound}The bound follows from the stated assumptions.\\end{theorem}\n"
      "\\newpage\n\\section{Proof}\n\\begin{proof}\n";
    for (int line = 1; line <= 36; ++line)
      source += QString("Step %1. We keep the argument and its assumptions together.\n").arg(line);
    source += "This proves Theorem \\ref{thm:bound}.\n\\end{proof}\n\\newpage\n"
      "\\section{Discussion}\nThe proof is complete.\n\\end{document}\n";
    put(file, source.toUtf8());
    QPointer<TeXDocumentWindow> window = new TeXDocumentWindow(file);
    QTimer overall; bool timedOut = false; overall.setSingleShot(true);
    QObject::connect(&overall, &QTimer::timeout, &app, [&] {
      timedOut = true; QJsonArray modals;
      for (auto *widget : QApplication::allWidgets()) if (auto *dialog = qobject_cast<QDialog *>(widget); dialog && dialog->isVisible()) {
        modals.append(QJsonObject{{"object", dialog->objectName()}, {"title", dialog->windowTitle()}}); dialog->reject();
      }
      put(folder + "/watchdog.json", QJsonDocument(QJsonObject{{"visibleDialogs", modals}}).toJson());
      check("combined scene completes within the overall watchdog", false);
      if (window) QMetaObject::invokeMethod(window, "interrupt", Qt::DirectConnection);
      app.exit(1);
    });
    overall.start(180000);
    const QSize size(qEnvironmentVariableIsSet("TEXLINE_TEST_HALF") ? 620 : 1040, 740);
    window->resize(size); window->show(); QApplication::setActiveWindow(window); settle(350);
    auto finish = [&] {
      overall.stop();
      const auto documents = TeXDocumentWindow::documentList();
      for (auto *document : documents) document->textDoc()->setModified(false);
      for (auto *document : documents) document->close();
      settle(); app.exit(failures ? 1 : 0);
    };
    auto *compile = window->findChild<QAction *>("actionTypeset");
    check("normal Compile action is available", compile && compile->isEnabled());
    if (compile && compile->isEnabled()) compile->trigger();
    const bool finished = waitFor([&] { return !window->isTypesetting() && !window->property("researchBuildPreparing").toBool() && !window->property("researchBuildFinishing").toBool() && !window->property("researchPublishing").toBool(); }, 120000);
    put(folder + "/compile.txt", window->property("consoleOutput").toString().toUtf8());
    check("normal compilation produces a current source/PDF pair", finished && pdfPairValid(previewFile(file)) && pdfRevision(previewFile(file), file) == PdfRevision::Current);
    if (!finished) {
      QMetaObject::invokeMethod(window, "interrupt", Qt::DirectConnection);
      waitFor([&] { return !window->isTypesetting() && !window->property("researchBuildPreparing").toBool() && !window->property("researchBuildFinishing").toBool(); }, 10000);
    }
    if (timedOut || !finished || !pdfPairValid(previewFile(file))) { finish(); return; }
    window->findChild<QAction *>("actionGo_to_Preview")->trigger(); settle(350);
    QPointer<PDFDocumentWindow> pdf = window->pdfDocument();
    check("normal preview opens the compiled paper", pdf && pdf->isVisible());
    if (!pdf) { finish(); return; }
    const auto compiled = pdf->widget()->document().toStrongRef();
    check("paper has multiple actual compiled pages", compiled && compiled->numPages() >= 3);
    check("article scope becomes ready", waitFor([&] { return window->property("researchArticleReady").toBool() && window->findChild<QToolButton *>("researchArticleSection"); }));

    QTextCursor cursor(window->textDoc()); cursor.setPosition(source.indexOf("\\label{thm:bound}"));
    window->editor()->setTextCursor(cursor); pinSourceReference(window, "thm:bound");
    check("the theorem is pinned before creating the combined scene", waitFor([&] {
      auto *dock = window->findChild<QDockWidget *>("researchPinnedReference");
      return dock && dock->isVisible() && dock->property("researchPinnedReady").toBool();
    }));
    const int start = source.indexOf("Step 20."), end = source.indexOf("Step 23.");
    cursor.setPosition(end); cursor.setPosition(start, QTextCursor::KeepAnchor);
    window->editor()->setTextCursor(cursor);
    auto *section = window->findChild<QToolButton *>("researchArticleSection");
    check("current proof can use section scope", section && section->isEnabled());
    if (section) section->click();
    window->editor()->ensureCursorVisible(); settle(250);
    pdf->widget()->setZoomLevel(1.31); pdf->widget()->goToPage(1, Qt::AlignCenter); settle(250);
    pdf->widget()->verticalScrollBar()->setValue(pdf->widget()->verticalScrollBar()->value() + 31);
    window->selectWindow(); window->editor()->setFocus(); settle(300);
    const auto saved = captureWritingPlace(window);
    const int savedPosition = window->textCursor().position(), savedAnchor = window->textCursor().anchor();
    const int savedScroll = window->editor()->verticalScrollBar()->value();
    const QPointF savedCenter = pdf->widget()->mapToScene(pdf->widget()->viewport()->rect().center());
    const auto savedPage = pdf->widget()->currentPage(); const auto savedZoom = pdf->widget()->zoomLevel();
    check("one scene contains section scope, reverse selection, pin and a later PDF page",
      saved.value("article").toMap().value("partial").toBool() && savedPosition == start && savedAnchor == end &&
      saved.value("references").toMap().value("pinned").toBool() && saved.value("references").toMap().value("pinKey").toString() == "thm:bound" && savedPage == 1);
    window->grab().save(folder + "/scene-before-source.png"); pdf->grab().save(folder + "/scene-before-pdf.png");
    check("named scene dialog opens", useSceneDialog(window, "researchWritingContexts", [&](QDialog *dialog) {
      auto *save = dialog->findChild<QPushButton *>("researchSavedWritingSave");
      check("scene save is ready", waitFor([&] { return save->isEnabled(); }));
      dialog->findChild<QLineEdit *>("researchSavedWritingName")->setText("Proof tomorrow"); save->click();
      auto *list = dialog->findChild<QListWidget *>("researchSavedWritingList");
      check("combined scene is saved as one named record", waitFor([&] { return list->count() == 1; }));
      dialog->grab().save(folder + "/scene-named.png"); key(dialog, Qt::Key_Escape);
    }));
    check("Escape after saving preserves the current selection and focus", window->textCursor().position() == savedPosition && window->textCursor().anchor() == savedAnchor && waitFor([&] { return window->editor()->hasFocus(); }));
    const auto pdfFile = pdf->fileName();
    pdf->close(); window->close(); settle(300);
    check("source and PDF windows close before restoration", !TeXDocumentWindow::findDocument(file) && !PDFDocumentWindow::findDocument(pdfFile));
    window = new TeXDocumentWindow(file); window->resize(size); window->show();
    QApplication::setActiveWindow(window); window->editor()->setFocus(); settle(350);
    check("reopened article becomes ready", waitFor([&] { return window->property("researchArticleReady").toBool(); }));
    if (auto *full = window->findChild<QToolButton *>("researchArticleFull")) full->click();
    if (auto *closePin = window->findChild<QToolButton *>("researchPinnedClose")) closePin->click();
    cursor = QTextCursor(window->textDoc()); cursor.setPosition(source.indexOf("\\section{Setup}")); window->editor()->setTextCursor(cursor);
    window->editor()->verticalScrollBar()->setValue(0);
    window->findChild<QAction *>("actionGo_to_Preview")->trigger(); settle(300); pdf = window->pdfDocument();
    if (pdf) { pdf->widget()->setZoomLevel(.95); pdf->widget()->goToPage(static_cast<QtPDF::PDFDocumentView::size_type>(0), Qt::AlignTop); }
    window->selectWindow(); window->editor()->setFocus(); settle(250);
    const auto cancelled = captureWritingPlace(window);
    check("reopened view deliberately differs from saved scene", !articleReadingState(window).value("partial").toBool() && !window->textCursor().hasSelection() && !referenceReadingState(window).value("pinned").toBool());
    auto sameState = [&](const QVariantMap &expected) {
      const auto actual = captureWritingPlace(window);
      return actual.value("range") == expected.value("range") && actual.value("reverse") == expected.value("reverse") && actual.value("selected") == expected.value("selected") &&
        actual.value("x") == expected.value("x") && actual.value("y") == expected.value("y") && actual.value("viewportRange") == expected.value("viewportRange") && actual.value("viewportY") == expected.value("viewportY") &&
        actual.value("article") == expected.value("article") && actual.value("references").toMap().value("pinned") == expected.value("references").toMap().value("pinned") &&
        qAbs(actual.value("zoom").toDouble() - expected.value("zoom").toDouble()) < .001 &&
        QLineF(QPointF(actual.value("pdfX").toDouble(), actual.value("pdfY").toDouble()), QPointF(expected.value("pdfX").toDouble(), expected.value("pdfY").toDouble())).length() < 3;
    };
    for (bool escape : {false, true}) {
      check(escape ? "restore dialog opens for Escape" : "restore dialog opens for Cancel", useSceneDialog(window, "researchRestoreWritingContext", [&](QDialog *dialog) {
        auto *restore = dialog->findChild<QPushButton *>("researchSavedWritingRestore");
        check("saved scene is available after reopening", waitFor([&] { return restore->isEnabled(); }));
        if (escape) key(dialog, Qt::Key_Escape);
        else if (auto *buttons = dialog->findChild<QDialogButtonBox *>()) buttons->button(QDialogButtonBox::Cancel)->click();
      }));
      settle(250);
      check(escape ? "Escape does not restore or change the current scene" : "Cancel does not restore or change the current scene", sameState(cancelled) && window->editor()->hasFocus());
    }
    check("named scene restores through its ordinary command", useSceneDialog(window, "researchRestoreWritingContext", [&](QDialog *dialog) {
      auto *restore = dialog->findChild<QPushButton *>("researchSavedWritingRestore");
      check("named scene restore is available", waitFor([&] { return restore->isEnabled(); })); restore->click();
    }));
    check("combined restoration finishes including asynchronous scope and pin", waitFor([&] {
      auto *dock = window->findChild<QDockWidget *>("researchPinnedReference");
      return articleReadingState(window).value("partial").toBool() && !window->property("researchRestoreArticlePlace").isValid() &&
        dock && dock->isVisible() && dock->property("researchPinnedReady").toBool() && window->textCursor().position() == savedPosition && window->textCursor().anchor() == savedAnchor;
    }));
    settle(350); pdf = window->pdfDocument();
    check("reverse selection restores exactly", window->textCursor().position() == savedPosition && window->textCursor().anchor() == savedAnchor);
    check("restored section scope exposes proof and hides other sections", articleReadingState(window).value("partial").toBool() && window->textDoc()->findBlock(start).isVisible() && !window->textDoc()->findBlock(source.indexOf("\\section{Setup}")).isVisible() && !window->textDoc()->findBlock(source.indexOf("\\section{Discussion}")).isVisible());
    check("source reading position restores with section and pin together", qAbs(window->editor()->verticalScrollBar()->value() - savedScroll) <= 2);
    const auto pin = referenceReadingState(window);
    check("same theorem remains pinned after reopen", pin.value("pinned").toBool() && pin.value("pinKey").toString() == "thm:bound");
    check("PDF page, zoom and reading center restore together", pdf && pdf->isVisible() && pdf->widget()->currentPage() == savedPage && qAbs(pdf->widget()->zoomLevel() - savedZoom) < .001 && QLineF(savedCenter, pdf->widget()->mapToScene(pdf->widget()->viewport()->rect().center())).length() < 3);
    check("restoring the scene returns focus without changing paper contents", window->editor()->hasFocus() && window->textDoc()->editorText() == source && bytes(file) == source.toUtf8());
    put(folder + "/scene-states.json", QJsonDocument(QJsonObject{{"saved", QJsonObject::fromVariantMap(saved)}, {"beforeCancel", QJsonObject::fromVariantMap(cancelled)}, {"restored", QJsonObject::fromVariantMap(captureWritingPlace(window))}}).toJson());
    bool cacheNotice = false;
    if (pdf) for (auto *label : pdf->findChildren<QLabel *>()) if (label->isVisible() && label->text().contains("SyncTeX:")) cacheNotice = true;
    check("successful PDF synchronization adds no cache-path notice", !cacheNotice);
    window->grab().save(folder + "/scene-after-source.png"); if (pdf) pdf->grab().save(folder + "/scene-after-pdf.png");
    if (pdf) pdf->close(); finish();
  });
  return app.exec();
}
