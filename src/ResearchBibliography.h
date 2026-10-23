// Project bibliography drafts and lossless field edits. GPL-2.0-or-later.
#pragma once
#include <QString>
#include <QStringList>
#include <QMap>
#include <QVector>
#include <QUrl>
class QWidget;
class TeXDocumentWindow;
namespace Research {
struct BibField { QString name, expression, value; int start{0}, end{0}; bool literal{false}; };
struct BibRecord { QString type,key; int start{0},end{0},keyStart{0},keyEnd{0},close{0}; QMap<QString,BibField> fields; };
struct BibDocument { QVector<BibRecord> records; QString error; };
BibDocument parseBibliography(const QString &text);
QString patchBibliography(const QString &text,const QString &key,const QMap<QString,QString> &changes,QString *error);
QString mergeBibliography(const QString &existing,const QString &incoming,QStringList *added,QString *error);
// Explicit refresh: retain the project's key, comments and absent local fields.
// Ambiguous identifiers and unrelated key collisions are rejected.
QString updateBibliographyEntry(const QString &existing,const QString &incoming,QString *key,QString *error);
QString normalizedDoi(QString value);
QStringList bibliographyWorkIds(const BibRecord &record);
bool editBibliographyEntry(QWidget *parent,TeXDocumentWindow *owner,const QString &file,const QString &key);
bool removeBibliographyEntry(QWidget *parent,TeXDocumentWindow *owner,const QString &file,const QString &key,int citationCount);
// Returns the accepted BibTeX draft; cancellation never writes a document.
QString acquireBibliography(QWidget *parent,int source,const QUrl &localEndpoint={}); // 0 manual, 1 file, 2 DOI/title, 3 Zotero, 4 pasted BibTeX
bool appendBibliography(QWidget *parent,TeXDocumentWindow *owner,const QString &file,const QString &incoming,QStringList *added);
}
