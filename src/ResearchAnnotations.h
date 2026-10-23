// Source-clean, locally typeset review copies. GPL-2.0-or-later.
#pragma once
#include "Engine.h"
#include <QHash>
#include <QColor>
#include <QJsonObject>
#include <QRectF>
#include <QSharedPointer>
#include <QVector>
#include <atomic>
#include <memory>
class PDFDocumentWindow;
class QToolBar;
class QMenu;
namespace QtPDF { namespace Backend { class Document; } }
namespace Research {
struct Annotation {
  QString id, file, sourceHash, anchor, prefix, suffix, quote, opinion, geometryHash;
  int offset{-1}, page{-1};
  int selectionOffset{0};
  QRectF region; // PDF points, origin at the top left.
  QRectF referenceBox;
  QVector<QRectF> selectionRects;
};
struct AnnotationLocation { int page{-1}; QVector<QRectF> rects; };
struct AnnotationSpan {
  int start{-1}, end{-1}, insertion{-1};
  QString environment;
  bool valid() const { return start >= 0 && end > start && insertion >= start; }
};
AnnotationSpan annotationSpan(const QString &source, int offset);
Annotation makeAnnotation(const QString &file, const QString &source, int offset,
                          const QString &quote, int page, const QRectF &region, int endOffset = -1);
int resolveAnnotation(const Annotation &note, const QString &source);
QJsonObject annotationJson(const Annotation &note);
Annotation annotationFromJson(const QJsonObject &object);
// Opinions are prose; explicit $...$, \(...\), \[...\] preserve math.
QString annotationTeX(const QString &opinion, QString *error = nullptr);
struct AnnotationSources {
  QHash<QString,QString> texts;
  QString error;
};
AnnotationSources annotationSources(const QString &root, const QHash<QString,QString> &sources,
                                    const QVector<Annotation> &notes);
struct AnnotationFiles;
struct AnnotationBuild {
  QString pdf, log, error;
  QSharedPointer<AnnotationFiles> files;
  QSharedPointer<QtPDF::Backend::Document> document;
  int focusPage{0};
  QRectF focusBox;
  QHash<QString,QString> original, modified, copyToOriginal;
  QHash<QString,QVector<AnnotationLocation>> underlines;
  bool cancelled{false};
};
struct AnnotationRequest {
  QString root, pdf;
  QHash<QString,QString> sources, auxiliaries;
  QVector<Annotation> notes;
  Engine engine;
  double renderResolution{0};
  QString focusId;
  QColor paperColor{Qt::white};
};
AnnotationBuild compileAnnotations(const AnnotationRequest &request,
                                  const std::shared_ptr<std::atomic_bool> &cancel);
class AnnotationController;
void installAnnotations(PDFDocumentWindow *window, QToolBar *bar, QMenu *more);
void beforeAnnotationReload(PDFDocumentWindow *window);
QString annotationDisplayedFile(PDFDocumentWindow *window);
}
