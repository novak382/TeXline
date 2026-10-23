// Temporary, source-preserving research tools. GPL-2.0-or-later.
#pragma once
#include <QString>
#include <QStringList>
#include <QVector>
#include <QPair>
class TeXDocumentWindow;
class QImage;
class QMenu;
class QWidget;
class QTextEdit;
namespace Research {
struct AuthoringSpan {QString kind;int start{-1},end{-1};bool valid()const{return start>=0&&end>start;}};
struct SimpleTable {
  QString columnSpec;
  QStringList alignment, rules;
  QVector<QStringList> rows;
  QVector<QVector<QPair<int,int>>> cells;
  bool header{false},booktabs{false},valid{false};
  int start{-1},end{-1},columnsStart{-1},columnsEnd{-1};
};
AuthoringSpan authoringObjectAt(const QString &source,int position);
SimpleTable parseSimpleTable(const QString &source);
QString simpleTableSource(const SimpleTable &table);
QString citationSource(const QString &command,const QString &key,const QString &locator);
void installResearchAuthoring(TeXDocumentWindow *window);
void addResearchAuthoringContext(QMenu *menu,TeXDocumentWindow *window,int position);
bool pasteResearchImage(TeXDocumentWindow *window,const QImage &image);
void editResearchObject(TeXDocumentWindow *window);
QWidget *formulaTrialPreview(QWidget *parent,TeXDocumentWindow *window,QTextEdit *trial);
}
