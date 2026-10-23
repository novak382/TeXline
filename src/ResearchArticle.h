// Automatic, source-preserving article navigation. GPL-2.0-or-later.
#pragma once
#include <QHash>
#include <QString>
#include <QVariantMap>
#include <QVector>
#include <functional>
class TeXDocumentWindow;
namespace Research {
struct ArticleSection {
  QString file, title;
  int start{0}, end{0}, line{1}, level{0}, parent{-1};
  bool editable{false};
};
struct ArticleObject {
  QString file, title, kind, key;
  int start{0}, end{0}, line{1}, section{-1};
};
struct ArticleDocument {
  QString root;
  QHash<QString, QString> sources;
  QVector<ArticleSection> sections;
  QVector<ArticleObject> objects;
  int preambleEnd{-1}, abstractStart{-1};
  bool article{false}, beamer{false}, uncertain{false}, cancelled{false};
};
// Bounded worker-only scan. Logical order follows literal input/include calls.
ArticleDocument parseArticleDocument(const QString &root,
  const QHash<QString, QString> &buffers = {}, const std::function<bool()> &cancelled = {});
QVariantMap articleReadingState(TeXDocumentWindow *window);
void restoreArticleReadingState(TeXDocumentWindow *window,const QVariantMap &state);
void installArticleWriting(TeXDocumentWindow *window);
void detectArticleDocument(TeXDocumentWindow *window);
bool toggleArticleStructure(TeXDocumentWindow *window);
void setArticleFocusMode(TeXDocumentWindow *window, bool focused);
}
