/**
 * Copyright (C) 2013-2023  Stefan Löffler
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */
#include "PDFDocumentWidget.h"

#include "PDFDocumentScene.h"

#if QT_VERSION < QT_VERSION_CHECK(5, 14, 0)
#include <QApplication>
#endif

namespace QtPDF {

PDFDocumentWidget::PDFDocumentWidget(QWidget * parent /* = nullptr */, const double dpi /* = -1 */)
: PDFDocumentView(parent)
{
  if (dpi > 0)
    _dpi = dpi;
  else {
#if QT_VERSION < QT_VERSION_CHECK(5, 14, 0)
    _dpi = QApplication::desktop()->physicalDpiX();
#else
    _dpi = screen()->physicalDotsPerInch();
#endif
  }
}

// Loads the file specified by filename. If this succeeds, the new file is
// displayed and true is returned. Otherwise, the view is not altered and false
// is returned
bool PDFDocumentWidget::load(const QString &filename)
{
  if (_scene) {
    // If we already have the document, reload it instead of replacing it with
    // a new instance to preserve the current state (e.g., viewing area, etc.)
    QSharedPointer<Backend::Document> doc = _scene.data()->document().toStrongRef();
    if (doc && doc.data()->fileName() == filename) {
      const auto pageNumber = currentPage();
      QPointF anchor;
      auto *page = dynamic_cast<PDFPageGraphicsItem *>(_scene->pageAt(pageNumber));
      const bool hasAnchor = page != nullptr;
      if (page) anchor = page->mapFromScene(mapToScene(viewport()->rect().center()));
      _scene.data()->reloadDocument();
      // Preserve a page-relative reading point, even if preceding page sizes change.
      if (hasAnchor && _scene->lastPage() > 0)
        goToPage(qMin(pageNumber, _scene->lastPage() - 1), anchor);
      return true;
    }
  }

  QSharedPointer<QtPDF::Backend::Document> a_pdf_doc = QtPDF::Backend::Document::newDocument(filename);
  return loadDocument(a_pdf_doc);
}
bool PDFDocumentWidget::loadDocument(const QSharedPointer<Backend::Document> &a_pdf_doc)
{
  if (!a_pdf_doc || !a_pdf_doc->isValid())
    return false;

  // Note: Don't pass `this` (or any other QObject*) as parent to the new
  // PDFDocumentScene as that would cause docScene to be destroyed with its
  // parent, thereby bypassing the QSharedPointer mechanism. docScene will be
  // freed automagically when the last QSharedPointer pointing to it will be
  // destroyed.
  _scene = QSharedPointer<QtPDF::PDFDocumentScene>(new QtPDF::PDFDocumentScene(a_pdf_doc, nullptr, _dpi, _dpi));
  setScene(_scene);
  return true;
}

QWeakPointer<Backend::Document> PDFDocumentWidget::document() const
{
  if (!_scene)
    return QWeakPointer<Backend::Document>();
  return _scene->document();
}

bool PDFDocumentWidget::watchForDocumentChangesOnDisk() const
{
  if (_scene) {
    return _scene->watchForDocumentChangesOnDisk();
  }
  return false;
}

void PDFDocumentWidget::setWatchForDocumentChangesOnDisk(const bool doWatch)
{
  if (_scene) {
    _scene->setWatchForDocumentChangesOnDisk(doWatch);
  }
}

void PDFDocumentWidget::setResolution(const double dpi)
{
  if (dpi <= 0)
    return;
  _dpi = dpi;

  if (_scene)
    _scene->setResolution(dpi, dpi);
}


} // namespace QtPDF
