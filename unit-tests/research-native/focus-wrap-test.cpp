#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "TeXDocumentWindow.h"
#include "CompletingEdit.h"
#include "ResearchAppearance.h"
#include "Settings.h"
#include <QAction>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QSaveFile>
#include <QScrollBar>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QVariantAnimation>
#include <QWindow>

static void settle(int ms = 0) {
  QElapsedTimer clock; clock.start();
  do { QCoreApplication::processEvents(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); if (ms) QThread::msleep(2); } while (clock.elapsed() < ms);
}
static bool put(const QString &path, const QByteArray &bytes) {
  QSaveFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
int main(int argc, char **argv) {
  const QString report = QString::fromLocal8Bit(argv[1]); argc = 1;
  TWApp app(argc, argv); app.setQuitOnLastWindowClosed(false);
  QTimer::singleShot(100, &app, [&] {
    const QString folder = QFileInfo(report).absolutePath(), path = folder + "/wrap 中文 空格.tex";
    QDir().mkpath(folder);
    QString content;
    for (int i = 0; i < 180; ++i) content += QString("line %1 ").arg(i) + QString(170, QChar('x')) + " 中文内容\n";
    put(path, content.toUtf8());
    Tw::Settings().setValue("openPDFwithTeX", false);
    Tw::Settings().setValue("researchFocusMode", false);
    Tw::Settings().setValue("wrapLines", true);
    QJsonArray checks; QJsonObject measurements; int failures = 0;
    auto check = [&](const QString &name, bool passed) {
      checks.append(QJsonObject{{"name", name}, {"passed", passed}}); failures += !passed;
      put(report, QJsonDocument(QJsonObject{{"checks", checks}, {"measurements", measurements}, {"failures", failures}, {"scale", qEnvironmentVariable("QT_SCALE_FACTOR")}}).toJson());
    };
    auto *source = new TeXDocumentWindow(path); source->resize(850,600); source->show(); settle(220);
    auto *editor = source->editor(); auto *wrap = source->findChild<QAction *>("actionWrap_Lines");
    check("initial wrapping preserves source and has no horizontal overflow", editor->lineWrapMode() == QTextEdit::WidgetWidth && editor->horizontalScrollBar()->maximum() == 0 && !source->isModified());
    for (const auto &locale : QStringList{"zh_CN", "en_US"}) {
      app.applyTranslation(locale); settle(20);
      const QString expected = locale == "en_US" ? "Word wrap" : QStringLiteral("自动换行");
      for (const char *name : {"researchMore", "researchFocusMore"}) {
        auto *button = source->findChild<QToolButton *>(name);
        check(QString("%1 %2 has direct localized wrap toggle").arg(locale, name), button && button->menu()->actions().contains(wrap) && wrap->isCheckable() && wrap->text() == expected);
      }
    }
    wrap->trigger(); settle();
    check("disable wrap creates usable horizontal scrolling without changing text", editor->lineWrapMode() == QTextEdit::NoWrap && editor->horizontalScrollBar()->maximum() > 0 && editor->toPlainText() == content && !source->isModified());
    wrap->trigger(); settle(); source->close(); settle();
    source = new TeXDocumentWindow(path); source->resize(850,600); source->show(); settle(180); editor = source->editor(); wrap = source->findChild<QAction *>("actionWrap_Lines");
    check("enabled wrap survives closing and reopening the same file", wrap->isChecked() && editor->lineWrapMode() == QTextEdit::WidgetWidth && editor->horizontalScrollBar()->maximum() == 0);
    source->setWrapLines(false); source->close(); settle();
    source = new TeXDocumentWindow(path); source->resize(850,600); source->show(); settle(180); editor = source->editor();
    check("intentional no-wrap also survives closing and reopening", editor->lineWrapMode() == QTextEdit::NoWrap);
    source->setWrapLines(true); editor->verticalScrollBar()->setValue(editor->verticalScrollBar()->maximum()/2); settle();
    const auto native = source->winId(); const bool material = source->property("researchDwmExtended").toBool();
    const QTextCursor anchor = editor->cursorForPosition(QPoint(0,0)); const int anchorY = editor->viewport()->mapTo(source,editor->cursorRect(anchor).topLeft()).y();
    source->grab().save(folder+"/before-mode.png");
    Research::setFocusMode(true);
    auto *veil = source->findChild<QWidget *>("researchChromeVeil");
    auto *transition = source->findChild<QObject *>("researchWorkspaceTransition");
    auto *animation = transition ? transition->findChild<QVariantAnimation *>() : nullptr;
    const bool coversClient = veil && veil->isVisible() && veil->size() == source->size() && veil->testAttribute(Qt::WA_TransparentForMouseEvents);
    const bool nativeStable = source->winId() == native && source->property("researchDwmExtended").toBool() == material;
    // Observe the 180 ms animation before flushing reports to disk. Two
    // synchronous atomic report saves can themselves consume the sample window.
    QElapsedTimer observation; observation.start();
    settle(75);
    const bool intermediate = animation && animation->state() != QAbstractAnimation::Stopped && animation->currentValue().toReal()>0 && animation->currentValue().toReal()<1;
    measurements.insert("transitionObservationDelayMs", observation.elapsed());
    measurements.insert("transitionObservedTimeMs", animation ? animation->currentTime() : -1);
    measurements.insert("transitionObservedProgress", animation ? animation->currentValue().toReal() : -1);
    const QPixmap middle = source->grab();
    check("transition covers visible client and blocks no pointer input", coversClient);
    check("mode change keeps native surface and compositor extension unchanged", nativeStable);
    check("transition has an actual intermediate frame", intermediate);
    middle.save(folder+"/middle-mode.png"); settle(220);
    const int bottomGap = source->height()-editor->viewport()->mapTo(source,QPoint(0,editor->viewport()->height())).y();
    measurements.insert("bottomGapLogicalPixels",bottomGap);
    measurements.insert("anchorDriftPixels",editor->viewport()->mapTo(source,editor->cursorRect(anchor).topLeft()).y()-anchorY);
    check("focus editor bottom occupies at most sixteen logical pixels", bottomGap>=0 && bottomGap<=16);
    check("reading anchor keeps its window position within two pixels", qAbs(editor->viewport()->mapTo(source,editor->cursorRect(anchor).topLeft()).y()-anchorY)<=2);
    check("animation releases its snapshot after completing", veil && veil->isHidden() && veil->property("researchSnapshotBytes").toLongLong()==0);
    source->grab().save(folder+"/after-mode.png");
    const QTextCursor wrapAnchor = editor->cursorForPosition(QPoint(0,0));
    const int wrapAnchorY = editor->cursorRect(wrapAnchor).top();
    source->setWrapLines(false); settle();
    check("disabling wrap keeps the visible source position within two pixels", qAbs(editor->cursorRect(wrapAnchor).top()-wrapAnchorY)<=2);
    const int noWrapGap = source->height()-editor->viewport()->mapTo(source,QPoint(0,editor->viewport()->height())).y();
    measurements.insert("noWrapBottomGapLogicalPixels",noWrapGap);
    check("focus no-wrap keeps a usable horizontal scrollbar within twenty-four bottom pixels", noWrapGap<=24 && editor->horizontalScrollBar()->isVisible() && editor->horizontalScrollBar()->maximum()>0 && editor->toPlainText()==content);
    source->grab().save(folder+"/focus-no-wrap.png");
    const QTextCursor noWrapAnchor = editor->cursorForPosition(QPoint(0,0));
    const int noWrapAnchorY = editor->cursorRect(noWrapAnchor).top();
    source->setWrapLines(true); settle();
    check("enabling wrap keeps the visible source position within two pixels", qAbs(editor->cursorRect(noWrapAnchor).top()-noWrapAnchorY)<=2);
    bool rapid = true;
    for (int i=0; i<18; ++i) { Research::setFocusMode(i%2); settle(12); rapid &= source->winId()==native && editor->toPlainText()==content; }
    Research::setFocusMode(false); settle(260);
    measurements.insert("rapidNativeAndTextStable",rapid);
    measurements.insert("rapidFinalFocus",source->property("researchFocus").toBool());
    measurements.insert("rapidFinalVeilHidden",veil->isHidden());
    measurements.insert("rapidFinalAnimationState",animation ? int(animation->state()) : -1);
    measurements.insert("rapidFinalSnapshotBytes",veil->property("researchSnapshotBytes").toLongLong());
    check("interrupted transitions reach last requested mode without text or HWND changes", rapid && !source->property("researchFocus").toBool() && veil->isHidden() && veil->property("researchSnapshotBytes").toLongLong()==0 && animation && animation->state()==QAbstractAnimation::Stopped);
    // An inactive surface may stop advancing its animation frame clock. The
    // already-applied final layout must still become visible and release the
    // old snapshot within the ordinary transition observation window.
    Research::setFocusMode(true);
    if (animation) animation->pause();
    settle(260);
    check("a stopped frame clock does not indefinitely hide the final layout", source->property("researchFocus").toBool() && veil->isHidden());
    check("a stopped frame clock releases its snapshot and animation", animation && animation->state()==QAbstractAnimation::Stopped && veil->property("researchSnapshotBytes").toLongLong()==0);
    Research::setFocusMode(false); settle(260);
    editor->moveCursor(QTextCursor::End); editor->insertPlainText("added"); Research::setFocusMode(true); settle(35); editor->undo();
    check("editing and undo during transition preserve document", editor->toPlainText()==content);
    Research::setFocusMode(false); source->setModified(false); source->close(); settle();
    QFile disk(path); check("view-only operations never change disk content", disk.open(QIODevice::ReadOnly) && disk.readAll()==content.toUtf8());
    app.exit(failures ? 1 : 0);
  }); return app.exec();
}
