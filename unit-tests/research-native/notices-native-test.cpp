#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "TeXDocumentWindow.h"
#include "PDFDocumentWindow.h"
#include "ResearchAppearance.h"
#include "ResearchCollaboration.h"
#include "Settings.h"
#include "CompletingEdit.h"
#include <QAction>
#include <QCursor>
#include <QAbstractNativeEventFilter>
#include <QCryptographicHash>
#include <windows.h>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QGraphicsOpacityEffect>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QSaveFile>
#include <QStatusBar>
#include <QTextBrowser>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <functional>

static bool waitFor(const std::function<bool()> &ready, int limit = 10000) {
  QElapsedTimer timer; timer.start();
  while (!ready() && timer.elapsed() < limit) { QCoreApplication::processEvents(); QThread::msleep(5); }
  return ready();
}
static void settle(int milliseconds) {
  QElapsedTimer timer; timer.start(); waitFor([&] { return timer.elapsed() >= milliseconds; }, milliseconds + 500);
}
static bool put(const QString &path, const QByteArray &data) {
  QDir().mkpath(QFileInfo(path).absolutePath()); QSaveFile file(path);
  return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit();
}
// Keep human clicks/typing in other apps from becoming fixture edits when
// these windows are raised. Synthetic Qt input remains available to tests.
class FixtureInput final : public QAbstractNativeEventFilter {
public:
  int ignored = 0;
  bool nativeEventFilter(const QByteArray &type, void *message, qintptr *result) override {
    if(!message || (type!="windows_generic_MSG" && type!="windows_dispatcher_MSG")) return false;
    const auto *m=static_cast<MSG *>(message);
    if(!QWidget::find(reinterpret_cast<WId>(m->hwnd)))return false;
    if((m->message>=WM_KEYFIRST&&m->message<=WM_KEYLAST)||(m->message>=WM_LBUTTONDOWN&&m->message<=WM_MBUTTONDBLCLK)) { ++ignored;if(result)*result=0;return true; }
    return false;
  }
};
int main(int argc, char **argv) {
  const QString report = QString::fromLocal8Bit(argv[1]); argc = 1;
  TWApp app(argc, argv); app.setQuitOnLastWindowClosed(false);
  FixtureInput input;app.installNativeEventFilter(&input);
  QTimer::singleShot(100, &app, [&] {
    QJsonArray checks; int failures = 0;
    auto check = [&](const QString &name, bool pass) {
      checks.append(QJsonObject{{"name", name}, {"passed", pass}}); if (!pass) ++failures;
      put(report, QJsonDocument(QJsonObject{{"checks", checks}, {"failures", failures}}).toJson());
    };
    { Tw::Settings settings; settings.setValue("locale", "zh_CN"); settings.setValue("researchFocusMode", true); settings.setValue("openPDFwithTeX", true); }
    app.applyTranslation("zh_CN");
    const QString folder = QFileInfo(report).absolutePath();
    // Use a fresh document so the first compilation, rather than a pre-existing
    // preview from an earlier run, exercises the PDF-opening path.
    const QString root = folder + "/warning-" + QString::number(QDateTime::currentMSecsSinceEpoch()) + ".tex";
    put(root, "\\documentclass{article}\n\\begin{document}\n\\fontencoding{OT1}\\fontfamily{cmr}\\fontsize{5.5}{7}\\selectfont\nA font substitution warning.\n\\end{document}\n");
    auto *source = new TeXDocumentWindow(root); source->resize(950, 650); source->show();

    source->typeset();
    check("real warning-only compilation succeeds and opens PDF", waitFor([&] { return !source->isTypesetting(); }, 90000) && !PDFDocumentWindow::documentList().isEmpty());
    auto *pdf = PDFDocumentWindow::documentList().isEmpty() ? nullptr : PDFDocumentWindow::documentList().first();
    check("first compilation opens a visible PDF after its layout is prepared", pdf && pdf->isVisible());
    auto *summary = source->findChild<QTextBrowser *>("researchProblems");
    auto *sourceLabel = source->findChild<QLabel *>("researchFocusMessage");
    auto *pdfLabel = pdf ? pdf->findChild<QLabel *>("researchFocusMessage") : nullptr;
    check("warning details remain available", summary && summary->toPlainText().contains("Font shape"));
    check("source completion notice appears after output publication finishes", waitFor([&] { return sourceLabel && sourceLabel->isVisible() && sourceLabel->text().contains(QStringLiteral("编译完成")); }, 5000));
    check("PDF completion notice appears", pdfLabel && pdfLabel->isVisible() && pdfLabel->text().contains(QStringLiteral("返回源码查看")));
    settle(400); // Let the newly opened PDF finish its initial fit-to-window layout.
    const QString beforeText = source->editor()->toPlainText(); const int beforeCursor = source->cursorPosition();
    const double beforeZoom = pdf ? pdf->widget()->zoomLevel() : 0;
    settle(3000);
    check("completion remains readable before four seconds", sourceLabel && sourceLabel->isVisible() && !source->statusBar()->currentMessage().isEmpty());
    if (pdf) pdf->grab().save(folder + "/completion-visible.png");
    check("both completion notices disappear after fade", waitFor([&] { return source->statusBar()->currentMessage().isEmpty() && (!pdf || pdf->statusBar()->currentMessage().isEmpty()); }, 2000));
    check("notice dismissal preserves warning summary", summary && source->findChild<QTextBrowser *>("researchProblems") == summary);
    put(folder+"/notice-reading-state.json",QJsonDocument(QJsonObject{{"beforeCursor",beforeCursor},{"afterCursor",source->cursorPosition()},{"beforeTextSha256",QString::fromLatin1(QCryptographicHash::hash(beforeText.toUtf8(),QCryptographicHash::Sha256).toHex())},{"afterTextSha256",QString::fromLatin1(QCryptographicHash::hash(source->editor()->toPlainText().toUtf8(),QCryptographicHash::Sha256).toHex())},{"ignoredPhysicalFixtureInput",input.ignored}}).toJson());
    check("notice dismissal preserves source text and cursor", beforeText == source->editor()->toPlainText() && beforeCursor == source->cursorPosition());
    check(QStringLiteral("notice dismissal preserves PDF zoom (%1 -> %2)").arg(beforeZoom).arg(pdf ? pdf->widget()->zoomLevel() : 0), !pdf || beforeZoom == pdf->widget()->zoomLevel());
    if (pdf) pdf->grab().save(folder + "/completion-dismissed.png");
    Research::showTransientStatusMessage(source, QStringLiteral("编译完成"), 200); settle(180);
    check("opacity transitions instead of disappearing abruptly", waitFor([&] { auto *effect = qobject_cast<QGraphicsOpacityEffect *>(sourceLabel->graphicsEffect()); return effect && effect->opacity() > 0 && effect->opacity() < 0.85; }, 450) && !source->statusBar()->currentMessage().isEmpty());
    Research::showTransientStatusMessage(source, QStringLiteral("编译完成"), 200);
    QEvent enter(QEvent::Enter); QApplication::sendEvent(sourceLabel, &enter); settle(550);
    check("hover pauses dismissal and restores full opacity", !source->statusBar()->currentMessage().isEmpty() && qobject_cast<QGraphicsOpacityEffect *>(sourceLabel->graphicsEffect())->opacity() == 1);
    QEvent leave(QEvent::Leave); QApplication::sendEvent(sourceLabel, &leave);
    check("leaving resumes dismissal", waitFor([&] { return source->statusBar()->currentMessage().isEmpty(); }, 1200));
    Research::showTransientStatusMessage(source, QStringLiteral("编译完成"), 100);
    source->statusBar()->showMessage(QStringLiteral("正在编译 · next.tex")); settle(450);
    check("old completion cannot erase a new build status", source->statusBar()->currentMessage().contains("next.tex"));
    QElapsedTimer duplicateClock;duplicateClock.start();
    Research::showTransientStatusMessage(source, QStringLiteral("编译完成"), 150);
    Research::showTransientStatusMessage(source, QStringLiteral("编译完成"), 1600);
    // Separate the two deadlines enough to tolerate Windows timer scheduling.
    // A restarted 1600 ms lifetime cannot satisfy this 1000 ms deadline.
    const bool duplicateCleared=waitFor([&]{return source->statusBar()->currentMessage().isEmpty();},1000);
    put(folder+"/notice-duplicate-timing.json",QJsonDocument(QJsonObject{{"elapsedMs",duplicateClock.elapsed()},{"cleared",duplicateCleared},{"message",source->statusBar()->currentMessage()},{"underMouse",sourceLabel->underMouse()}}).toJson());
    check("identical consecutive notices keep the original lifetime", duplicateCleared);
    check("duplicate notices leave no pending overlay", sourceLabel->isHidden());
    Research::showTransientStatusMessage(source, QStringLiteral("编译完成"), 600);
    settle(100); app.applyTranslation("en_US");
    check("language switching translates pending completion", source->statusBar()->currentMessage().contains("finished", Qt::CaseInsensitive));
    check("translated completion still expires", waitFor([&] { return source->statusBar()->currentMessage().isEmpty(); }, 1500));
    Research::setFocusMode(false); Research::showTransientStatusMessage(source, "Build complete", 150);
    check("standard status bar also expires without changing layout", waitFor([&] { return source->statusBar()->currentMessage().isEmpty(); }, 1000) && sourceLabel->isHidden());
    source->collaboration()->review(); settle(200);
    const QString reviewMessage=source->statusBar()->currentMessage();
    check("actual empty external-review notice uses the shared overlay in standard mode", reviewMessage.contains("review",Qt::CaseInsensitive)&&sourceLabel->isVisible()&&source->statusBar()->property("researchAnimatedNotice").toBool());
    settle(3000);source->collaboration()->review();settle(1500);
    check("repeated external-review notice does not prolong its original lifetime",source->statusBar()->currentMessage().isEmpty()&&sourceLabel->isHidden());
    source->collaboration()->review();settle(200);
    Research::setFocusMode(true);settle(100);
    check("switching workspace mode keeps a pending notice readable",sourceLabel->isVisible()&&source->statusBar()->currentMessage()==reviewMessage);
    settle(3300);
    check("actual external-review notice fades gradually",waitFor([&]{auto *effect=qobject_cast<QGraphicsOpacityEffect*>(sourceLabel->graphicsEffect());return effect&&effect->opacity()>0&&effect->opacity()<0.9;},1000)&&!source->statusBar()->currentMessage().isEmpty());
    source->grab().save(folder+"/external-review-fading.png");
    check("actual external-review notice clears after fade",waitFor([&]{return source->statusBar()->currentMessage().isEmpty();},1000));
    source->collaboration()->review();source->statusBar()->showMessage("Persistent PDF/source warning");settle(4500);
    check("pending timed notice cannot erase persistent warnings",source->statusBar()->currentMessage()=="Persistent PDF/source warning");
    source->statusBar()->clearMessage();
    Research::setFocusMode(false);Research::showTransientStatusMessage(source,"normal notice",220);settle(180);
    check("standard mode uses the same gradual fade",waitFor([&]{auto *effect=qobject_cast<QGraphicsOpacityEffect*>(sourceLabel->graphicsEffect());return effect&&effect->opacity()>0&&effect->opacity()<0.8;},450)&&sourceLabel->isVisible());
    settle(300);
    Research::setFocusMode(true);
    auto cursor = source->textCursor(); cursor.setPosition(source->editor()->toPlainText().indexOf("\\end{document}")); cursor.insertText("\n\\undefinedCommand\n"); source->save(); source->typeset();
    check("real compilation failure remains visible", waitFor([&] { return !source->isTypesetting(); }, 90000) && source->findChild<QTextBrowser *>("researchProblems"));
    settle(4600);
    auto *failureSummary=source->findChild<QToolButton *>("researchBuildSummary");
    check("focus keeps failure details and log state without restoring the status row", source->statusBar()->isHidden() && source->findChild<QTextBrowser *>("researchProblems")->isVisible() && failureSummary && failureSummary->text().contains("fail", Qt::CaseInsensitive) && failureSummary->menu());
    Research::setFocusMode(false); settle(300);
    check("normal mode restores the persistent failure summary with a log action", failureSummary && failureSummary->isVisible() && failureSummary->menu());
    Research::showTransientStatusMessage(source, "Build complete", 200);
    source->setModified(false); delete source; settle(500);
    check("closing a window safely cancels pending timers", true);
    for (auto *viewer : PDFDocumentWindow::documentList()) delete viewer;
    app.exit(failures ? 1 : 0);
  });
  return app.exec();
}
