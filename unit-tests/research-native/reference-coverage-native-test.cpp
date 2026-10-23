// Build in place of main and ResearchReferences.o to inspect the exact private
// production resolver. No test hooks or dependency scans enter the GUI build.
#include "ResearchReferences.cpp"
#include "ResearchBuild.h"
#include <QElapsedTimer>
#include <QJsonArray>
#include <QSaveFile>

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  const QString report = QString::fromLocal8Bit(argv[1]);
  using namespace Research;
  QJsonArray checks, cases; int failures = 0, equations = 0, statements = 0, references = 0, links = 0;
  QElapsedTimer timer; timer.start();
  auto check = [&](const QString &name, bool passed) {
    checks.append(QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("passed"), passed}}); if (!passed) ++failures;
    QSaveFile f(report); if (f.open(QIODevice::WriteOnly)) { f.write(QJsonDocument(QJsonObject{{QStringLiteral("checks"), checks}, {QStringLiteral("cases"), cases}, {QStringLiteral("failures"), failures}, {QStringLiteral("equations"), equations}, {QStringLiteral("statements"), statements}, {QStringLiteral("references"), references}, {QStringLiteral("links"), links}, {QStringLiteral("elapsedMs"), timer.elapsed()}}).toJson()); f.commit(); }
  };
  const QString assets = QCoreApplication::applicationDirPath();
  const QStringList roots{assets + QStringLiteral("/papers/main 研究 论文/main.tex"), assets + QStringLiteral("/papers/supp 研究 补充/supp-1008.tex"), assets + QStringLiteral("/fixtures/matrix/main.tex")};
  QList<QPair<QString,QString>> papers;
  for (const auto &root : roots) papers.append({root, Research::previewFile(root)});
  for (const auto &paper : papers) {
    Request request; request.root = paper.first; request.pdf = paper.second;
    request.vectorProgram = QStringLiteral("C:/texlive/2023/bin/windows/pdftocairo.exe"); request.temporaryFolder = QFileInfo(report).absolutePath() + QStringLiteral("/vector-preview");
    const QString name = QFileInfo(paper.first).fileName();
    auto index = referenceIndex(paper.first); readNumbers(index, auxFile(paper.first, paper.second));
    auto doc = QtPDF::Backend::Document::newDocument(paper.second); PreviewSync sync(paper.second, paper.first);
    check(name + QStringLiteral(" has the exact current PDF/source pair and valid SyncTeX"), doc && sync.isValid() && validSourcePair(request));
    if (!doc || !sync.isValid()) continue;
    QSet<QString> mathematicalAnchors;
    for (const auto &d : index.definitions) {
      if (d.block.kind.isEmpty()) continue;
      const auto destination = doc->resolveDestination(QtPDF::PDFDestination(d.anchor));
      int page; const QRectF rect = referenceRect(sync, index, d, doc, page);
      QString text; if (page >= 0) if (auto p = doc->page(page).toStrongRef()) text = p->selectedText({QPolygonF(rect.adjusted(-12, -1, 12, 1))});
      cases.append(QJsonObject{{QStringLiteral("paper"), name}, {QStringLiteral("root"), paper.first}, {QStringLiteral("key"), d.key}, {QStringLiteral("number"), d.number}, {QStringLiteral("equation"), d.block.equation}, {QStringLiteral("page"), page + 1}, {QStringLiteral("expectedPage"), double(destination.page() + 1)}, {QStringLiteral("x"), rect.x()}, {QStringLiteral("y"), rect.y()}, {QStringLiteral("width"), rect.width()}, {QStringLiteral("height"), rect.height()}, {QStringLiteral("text"), text.left(500)}});
      if (d.block.equation) {
        ++equations; mathematicalAnchors.insert(d.anchor);
        QString number = d.number; number.remove(QLatin1Char('{')); number.remove(QLatin1Char('}'));
        check(name + QStringLiteral(": ") + d.key + QStringLiteral(" matches its compiled page and equation number"), !rect.isEmpty() && destination.isExplicit() && page == destination.page() && !number.isEmpty() && text.contains(number));
        if (!rect.isEmpty()) {
          Request body = request; body.body = true; body.page = page; body.point = rect.center();
          const auto back = work(body);
          check(name + QStringLiteral(": ") + d.key + QStringLiteral(" formula-body hover resolves its own compiled definition"), back.visual() && back.page == page && back.target.number == d.number && back.target.block.file == d.block.file && back.target.block.start == d.block.start);
          if (d.key.startsWith(QStringLiteral("eq:long-"))) check(d.key + QStringLiteral(" excludes section headings and adjacent numbered rows"), rect.height() < 35 && !text.contains(QStringLiteral("Multiple pages")));
          if (d.key == QStringLiteral("eq:drawing")) check(QStringLiteral("non-text drawing content keeps its full width and height"), rect.width() > 500 && rect.height() >= 30);
        }
      } else {
        ++statements;
        check(name + QStringLiteral(": ") + d.key + QStringLiteral(" starts on the compiled statement's page"), !rect.isEmpty() && destination.isExplicit() && page == destination.page() && !text.trimmed().isEmpty());
      }
    }
    for (const auto &use : index.uses) {
      int position = use.text.indexOf(use.key);
      while (position >= 0 && referenceAt(use.text, position + qMin(2, int(use.key.size()) - 1)).key != use.key) position = use.text.indexOf(use.key, position + use.key.size());
      if (position < 0) { check(name + QStringLiteral(": reference command is present for ") + use.key, false); continue; }
      ++references;
      check(name + QStringLiteral(": source reference ") + use.key + QLatin1Char(':') + QString::number(use.line), referenceAt(use.text, position + qMin(2, int(use.key.size()) - 1)).key == use.key);
    }
    QSet<QString> checkedDestinations;
    for (int n = 0; n < doc->numPages(); ++n) if (auto p = doc->page(n).toStrongRef()) for (const auto &link : p->loadLinks()) {
      const auto *action = link->actionOnActivation(); if (!action || action->type() != QtPDF::PDFAction::ActionTypeGoTo) continue;
      const auto destination = doc->resolveDestination(static_cast<const QtPDF::PDFGotoAction *>(action)->destination());
      const QString identity = QStringLiteral("%1:%2:%3").arg(destination.page()).arg(destination.top()).arg(destination.left());
      if (checkedDestinations.contains(identity)) continue;
      bool equation = false;
      for (const auto &d : index.definitions) if (d.block.equation && !d.anchor.isEmpty()) {
        const auto anchor = doc->resolveDestination(QtPDF::PDFDestination(d.anchor));
        if (anchor.page() == destination.page() && qAbs(anchor.top() - destination.top()) < 2 && qAbs(anchor.left() - destination.left()) < 2) { equation = true; break; }
      }
      if (!equation) continue;
      checkedDestinations.insert(identity); ++links;
      Request hover = request; hover.destination = destination; const auto result = work(hover);
      check(name + QStringLiteral(": PDF formula destination ") + identity, result.visual() && result.target.block.equation && result.page == destination.page());
    }
    if (paper.first.contains(QStringLiteral("/main 研究 论文/"))) {
      Request header = request; header.body = true; header.page = 11; header.point = QPointF(110, 50);
      check(QStringLiteral("a page-12 header sharing equation 4.6's source line is silent"), !work(header).visual());
      Request mismatch = request; mismatch.key = QStringLiteral("cond:full-snr"); mismatch.buffers.insert(path(paper.first), read(paper.first) + QStringLiteral("\n% uncompiled change\n"));
      check(QStringLiteral("a newer unsaved source revision never displays an older formula"), !work(mismatch).visual());
    }
    if (paper.first.contains(QStringLiteral("/matrix/"))) {
      Request a = request; a.key = QStringLiteral("eq:row-one"); auto first = work(a);
      a.key = QStringLiteral("eq:row-two"); auto second = work(a);
      check(QStringLiteral("two numbered align rows on the same source line resolve to distinct crops"), first.visual() && second.visual() && first.page == second.page && first.viewBox.bottom() <= second.viewBox.top() + 3);
      if (first.visual()) { auto p = doc->page(first.page).toStrongRef(); const auto text = p->selectedText({QPolygonF(first.viewBox)}); check(QStringLiteral("the first align row excludes the second equation number"), text.contains(first.target.number) && !text.contains(second.target.number)); }
      if (second.visual()) { auto p = doc->page(second.page).toStrongRef(); const auto text = p->selectedText({QPolygonF(second.viewBox)}); check(QStringLiteral("the second align row excludes the first equation number"), text.contains(second.target.number) && !text.contains(first.target.number)); }
    }
  }
  return failures ? 1 : 0;
}
