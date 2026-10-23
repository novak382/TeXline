// Regression fixture for reading-position navigation and inverse-search input.
#include "PDFBackend.h"
#include "PDFDocumentScene.h"
#include "PDFDocumentView.h"
#include "PDFPageProcessingThread.h"
#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>
#include <QScrollBar>
#include <cmath>

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  if (argc < 3) return 2;
  using namespace QtPDF;
  auto doc = Backend::Document::newDocument(QString::fromLocal8Bit(argv[1]));
  if (!doc || !doc->isValid()) return 3;
  auto scene = QSharedPointer<PDFDocumentScene>::create(doc, nullptr, 72, 72);
  scene->setWatchForDocumentChangesOnDisk(false);
  PDFDocumentView view;
  view.resize(1000, 850);
  view.setScene(scene);
  view.show();
  app.processEvents();
  QJsonArray checks;
  int failures = 0;
  auto check = [&](const QString &name, bool pass) {
    checks.append(QJsonObject{{"name", name}, {"passed", pass}});
    if (!pass) ++failures;
  };
  auto pageView = [&]() {
    auto *page = dynamic_cast<PDFPageGraphicsItem *>(scene->pageAt(view.currentPage()));
    const QRectF visible = page->mapRectFromScene(view.mapToScene(view.viewport()->rect()).boundingRect());
    return QRectF(page->mapToPage(visible.topLeft()), page->mapToPage(visible.bottomRight()));
  };
  PDFDestination zoomDest(0);
  zoomDest.setRect(QRectF(123, 456, 0, 0));
  zoomDest.setZoom(2);
  const QRectF scaled = zoomDest.viewport(doc.data(), QRectF(10, 700, 400, -500), 1);
  check("explicit zoom keeps absolute anchor and halves visible extent",
        scaled.topLeft() == QPointF(123, 456) && scaled.size() == QSizeF(200, -250));

  bool stableZoom = true, stableX = true;
  int jumps = 0;
  for (auto mode : {PDFDocumentView::PageMode_OneColumnContinuous, PDFDocumentView::PageMode_SinglePage}) {
    view.setPageMode(mode);
    for (qreal zoom : {1.0, 1.25, 1.8, 2.8}) {
      view.goToPage(QtPDF::PDFDocumentView::size_type(0));
      view.setZoomLevel(zoom);
      view.centerOn(scene->pageAt(0)->sceneBoundingRect().center());
      for (int n = 0; n < 160; ++n) {
        const QRectF before = pageView();
        const qreal oldZoom = view.zoomLevel();
        PDFDestination dest((n * 7) % doc->numPages());
        // The point is visible near the right margin, as with an equation number.
        dest.setRect(QRectF(qMax(0.0, before.left() + before.width() * .8), 500, 0, 0));
        view.goToPDFDestination(dest, false);
        const QRectF after = pageView();
        stableZoom &= view.zoomLevel() == oldZoom;
        stableX &= std::abs(after.left() - before.left()) <= 2.0 / zoom;
        ++jumps;
        if (n % 20 == 0) app.processEvents();
      }
    }
  }
  check("1280 anchor jumps preserve exact zoom", stableZoom);
  check("1280 anchor jumps preserve horizontal reading position", stableX);
  zoomDest.setZoom(2.25);
  view.goToPDFDestination(zoomDest);
  check("explicit destination zoom is exact", view.zoomLevel() == 2.25);
  const QRectF validView = pageView();
  view.goToPDFDestination(PDFDestination(doc->numPages() + 3));
  check("invalid destinations leave view unchanged", pageView() == validView);

  view.setPageMode(PDFDocumentView::PageMode_OneColumnContinuous);
  view.goToPage(QtPDF::PDFDocumentView::size_type(0));
  view.zoomFitContentWidth();
  const qreal readingZoom = view.zoomLevel();
  const int readingX = view.horizontalScrollBar()->value();
  const QRectF readingView = pageView();
  PDFDestination reference(qMin(13, int(doc->numPages() - 1)));
  reference.setRect(QRectF(qMax(0.0, readingView.center().x()), 500, 0, 0));
  view.goToPDFDestination(reference);
  check("content-width reading survives equation-reference jump",
        view.zoomLevel() == readingZoom && std::abs(view.horizontalScrollBar()->value() - readingX) <= 2);
  view.goPrevViewRect();
  check("back navigation returns to original page", view.currentPage() == 0);

  int clicks = 0;
  bool coordinates = true;
  bool clickZoomStable = true;
  QPointF expected;
  QJsonArray clickZoomObservations;
  QObject::connect(&view, &PDFDocumentView::contextClick, [&](auto page, const QPointF &point) {
    ++clicks;
    coordinates &= page == 0 && std::abs(point.x() - expected.x()) < 2 && std::abs(point.y() - expected.y()) < 2;
  });
  for (qreal zoom : {0.75, 1.0, 1.25, 1.8, 2.8}) {
    view.goToPage(QtPDF::PDFDocumentView::size_type(0));
    view.setZoomLevel(zoom);
    const qreal zoomBeforeClicks = view.zoomLevel();
    auto *page = static_cast<PDFPageGraphicsItem *>(scene->pageAt(0));
    expected = QPointF(280, 420);
    view.centerOn(page->mapToScene(page->mapFromPage(expected)));
    const QPoint point = view.mapFromScene(page->mapToScene(page->mapFromPage(expected)));
    for (int n = 0; n < 100; ++n) {
      QMouseEvent press(QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
      QMouseEvent release(QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
      QMouseEvent dbl(QEvent::MouseButtonDblClick, point, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
      view.setMouseMode(n % 2 ? PDFDocumentView::MouseMode_MarqueeZoom : PDFDocumentView::MouseMode_Select);
      QApplication::sendEvent(view.viewport(), &press);
      QApplication::sendEvent(view.viewport(), &release);
      QApplication::sendEvent(view.viewport(), &dbl);
      QApplication::sendEvent(view.viewport(), &release);
      // Multiplicative zoom may round the requested value (for example 1.8).
      // Inverse-search input must preserve the actual pre-click value exactly.
      clickZoomStable &= view.zoomLevel() == zoomBeforeClicks;
    }
    clickZoomObservations.append(QJsonObject{{"requestedZoom", zoom},
      {"zoomBeforeClicks", zoomBeforeClicks}, {"zoomAfterClicks", view.zoomLevel()},
      {"unchanged", view.zoomLevel() == zoomBeforeClicks}});
  }
  check("500 double clicks map to the same PDF point at five zoom levels", coordinates && clicks == 500);
  check("double clicks in selection and zoom tools do not change zoom", clickZoomStable);
  doc->processingThread().clearWorkStack();
  app.processEvents();
  QFile report(QString::fromLocal8Bit(argv[2]));
  if (!report.open(QIODevice::WriteOnly)) return 4;
  report.write(QJsonDocument(QJsonObject{{"failures", failures}, {"checks", checks},
    {"destinationJumps", jumps}, {"doubleClicks", clicks}, {"devicePixelRatio", view.devicePixelRatioF()},
    {"clickZoomObservations", clickZoomObservations}}).toJson());
  return failures ? 1 : 0;
}
