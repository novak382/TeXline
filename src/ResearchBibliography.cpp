#include "ResearchFileWrite.h"
#include <QCheckBox>
// Independent native implementation; see PROJECT-COLLABORATION-PLAN-2026-10-22.md.
#include "ResearchBibliography.h"
#include "ResearchBibliographyFields.h"
#include "ResearchAppearance.h"
#include "ResearchSettings.h"
#include "ResearchTask.h"
#include "ResearchWorkspaceUi.h"
#include "ResearchProject.h"
#include "ResearchWorkflow.h"
#include "TeXDocumentWindow.h"
#include "CompletingEdit.h"
#include <QCryptographicHash>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QComboBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QMenu>
#include <QSignalBlocker>
#include <QScreen>
#include <QScrollArea>
#include <QTabWidget>
#include <QTextCursor>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <algorithm>
#include <memory>

namespace Research {
namespace {
bool escapedBib(const QString &s,int p){int n=0;while(p>0&&s[--p]==QLatin1Char('\\'))++n;return n%2;}
void whitespace(const QString &s,int &p){while(p<s.size()){if(s[p].isSpace()){++p;continue;}if(s[p]==QLatin1Char('%')){while(p<s.size()&&s[p]!=QLatin1Char('\n'))++p;continue;}break;}}
bool keyValid(const QString &s){return !s.isEmpty()&&s.size()<256&&!s.contains(QRegularExpression(QStringLiteral(R"([\s{},()\\%#"=])")));}
bool literalValid(const QString &s){int depth=0;for(int p=0;p<s.size();++p)if(!escapedBib(s,p)){if(s[p]==QLatin1Char('{'))++depth;if(s[p]==QLatin1Char('}')&&--depth<0)return false;}return depth==0;}
QString fieldValue(const BibRecord &r,const QString &name){return r.fields.value(name).value;}
QString jsonText(const QJsonValue &v){if(v.isArray())return v.toArray().isEmpty()?QString():v.toArray().first().toString();return v.toString();}
void dialogSize(QDialog *d,int width=700,int height=550){auto area=d->screen()->availableGeometry().size()-QSize(32,48);d->resize(QSize(qMin(width,qMax(400,d->parentWidget()->width()-24)),height).boundedTo(area));}
QDialogButtonBox *footer(QDialog *d,QVBoxLayout *layout,const QString &accept){auto *b=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,d);b->button(QDialogButtonBox::Ok)->setText(accept);b->button(QDialogButtonBox::Cancel)->setText(words("取消","Cancel"));for(auto *button:b->buttons())configureButton(qobject_cast<QPushButton*>(button));QObject::connect(b,&QDialogButtonBox::rejected,d,&QDialog::reject);layout->addWidget(b);return b;}
QByteArray fileHash(const QString &path){QFile f(path);if(!f.open(QIODevice::ReadOnly))return {};QCryptographicHash h(QCryptographicHash::Sha256);return h.addData(&f)?h.result():QByteArray();}
struct BibEdit {
  QPointer<TeXDocumentWindow> target; QString original,file; QByteArray hash;
  BibEdit(TeXDocumentWindow *owner,const QString &path):file(path),hash(fileHash(path)){
    target=TeXDocumentWindow::findDocument(path);if(!target)target=TeXDocumentWindow::openDocument(path,false,false);
    if(target){original=target->textDoc()->editorText();rememberRoot(path,completionProjectRoot(owner));}
  }
  bool apply(const QString &next,QLabel *notice){
    if(!target||target->editor()->isReadOnly()||target->textDoc()->editorText()!=original||fileHash(file)!=hash){notice->setText(words("文献文件已变化，请重新核对后应用。","The bibliography changed. Reopen the draft before applying."));return false;}
    if(next==original)return true;
    // Keep unchanged prefix/suffix outside the undo operation and preserve the
    // document's own codec/newline policy. No whole-file serialization.
    int first=0;while(first<original.size()&&first<next.size()&&original[first]==next[first])++first;
    int oldEnd=original.size(),newEnd=next.size();while(oldEnd>first&&newEnd>first&&original[oldEnd-1]==next[newEnd-1]){--oldEnd;--newEnd;}
    QTextCursor cursor(target->textDoc());cursor.beginEditBlock();cursor.setPosition(first);cursor.setPosition(oldEnd,QTextCursor::KeepAnchor);cursor.insertText(next.mid(first,newEnd-first));cursor.endEditBlock();return true;
  }
};

class BibliographyFields final : public QWidget {
  QFormLayout *form;
  BibRecord original;
  QLabel *missing{nullptr};
public:
  QComboBox *type;
  QLineEdit *key{nullptr};
  QMap<QString,QLineEdit*> fields;
  BibliographyFields(const BibRecord &record, const QString &prefix, bool creating, QWidget *parent)
      : QWidget(parent), form(new QFormLayout(this)), original(record) {
    configureSettingsSurface(this);
    form->setSpacing(10);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    type = new QComboBox(this);
    type->setObjectName(QStringLiteral("researchBibType"));
    for (const auto &entry : bibliographyTypes()) type->addItem(entry.label,entry.name);
    const auto name=record.type.isEmpty()?QStringLiteral("article"):record.type;
    if (type->findData(name)<0) type->addItem(QLatin1Char('@')+name,name);
    type->setCurrentIndex(type->findData(name));
    form->addRow(words("类型","Type"),type);
    if (creating) {
      key = new QLineEdit(this); key->setObjectName(prefix+QStringLiteral("key")); configureField(key);
      key->setPlaceholderText(words("根据作者和年份生成","Generated from author and year"));
      form->addRow(words("引用键","Citation key"),key);
    } else form->addRow(words("引用键","Citation key"),new QLabel(record.key,this));
    QStringList names;
    for (const auto &entry : bibliographyTypes()) for (const auto &field : entry.fields) if (!names.contains(field)) names.append(field);
    for (auto it=record.fields.begin();it!=record.fields.end();++it) if (!names.contains(it.key())) names.append(it.key());
    for (const auto &name : names) {
      auto *field=new QLineEdit(fieldValue(record,name),this);
      field->setObjectName(prefix+name); configureField(field);
      const auto value=record.fields.value(name);
      if (!value.expression.isEmpty()&&!value.literal&&!QRegularExpression(QStringLiteral("^\\d+$")).match(value.expression).hasMatch()) {
        field->setReadOnly(true);
        field->setToolTip(words("宏或拼接表达式可在条目源码中修改。","Edit this macro or concatenation in Entry source."));
      }
      fields.insert(name,field); form->addRow(bibliographyFieldLabel(name),field);
    }
    connect(type,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{refreshRows();});
    refreshRows();
    missing=new QLabel(this);missing->setObjectName(QStringLiteral("researchBibMissingFields"));missing->setWordWrap(true);missing->setStyleSheet(QStringLiteral("color:#747d85;"));form->addRow(missing);
    for(auto *field:fields)connect(field,&QLineEdit::textChanged,this,[this]{updateMissing();});
    connect(type,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{updateMissing();});updateMissing();
  }
  void updateMissing(){
    if(!missing)return;
    auto has=[&](const char *name){auto *field=fields.value(QString::fromLatin1(name));return field&&!field->text().trimmed().isEmpty();};
    // Cross-references inherit fields; do not claim they are missing locally.
    if(has("crossref")||has("xref")||has("xdata")){missing->hide();return;}
    QStringList absent;auto need=[&](const char *name){if(!has(name))absent.append(bibliographyFieldLabel(QString::fromLatin1(name)));};
    const auto kind=typeName();need("title");
    if(kind!=QStringLiteral("misc")&&kind!=QStringLiteral("manual")&&kind!=QStringLiteral("booklet")){
      if(!has("author")&&!has("editor"))absent.append(words("作者或编辑者","Author or editor"));
      if(!has("year")&&!has("date"))absent.append(words("年份或日期","Year or date"));
    }
    if(kind==QStringLiteral("article")&&!has("journal")&&!has("journaltitle"))absent.append(words("期刊","Journal"));
    if(kind==QStringLiteral("book")||kind==QStringLiteral("inbook")||kind==QStringLiteral("incollection"))need("publisher");
    if(kind==QStringLiteral("incollection")||kind==QStringLiteral("inproceedings"))need("booktitle");
    if((kind==QStringLiteral("thesis")||kind==QStringLiteral("phdthesis")||kind==QStringLiteral("mastersthesis"))&&!has("school")&&!has("institution"))absent.append(words("学校或机构","School or institution"));
    if(kind==QStringLiteral("techreport")||kind==QStringLiteral("report"))need("institution");
    if(kind==QStringLiteral("online")&&!has("url")&&!has("doi"))absent.append(words("链接或 DOI","URL or DOI"));
    missing->setText(absent.isEmpty()?QString():words("待补充：","To complete: ")+absent.join(words("、",", ")));missing->setVisible(!absent.isEmpty());
  }
  QString typeName() const { return type->currentData().toString(); }
  void refreshRows() {
    auto visible=bibliographyFieldsFor(typeName());
    if (original.fields.contains(QStringLiteral("journaltitle"))) visible.replaceInStrings(QRegularExpression(QStringLiteral("^journal$")),QStringLiteral("journaltitle"));
    if(original.fields.contains(QStringLiteral("eprinttype")))visible.removeAll(QStringLiteral("archiveprefix"));
    else visible.removeAll(QStringLiteral("eprinttype"));
    // Keep each type's reading order; the union of all fields is only storage.
    if(missing){auto row=form->takeRow(missing);delete row.labelItem;delete row.fieldItem;}
    QMap<QString,QWidget*> labels;
    for(auto it=fields.begin();it!=fields.end();++it){auto row=form->takeRow(it.value());labels.insert(it.key(),row.labelItem?row.labelItem->widget():nullptr);delete row.labelItem;delete row.fieldItem;}
    QStringList order=visible;for(const auto &name:fields.keys())if(!order.contains(name))order.append(name);
    for(const auto &name:order)if(fields.contains(name))form->addRow(labels.value(name),fields.value(name));
    if(missing)form->addRow(missing);
    for (auto it=fields.begin();it!=fields.end();++it)
      form->setRowVisible(it.value(),visible.contains(it.key())||original.fields.contains(it.key())||!it.value()->text().isEmpty());
  }
  QMap<QString,QString> changes() const {
    QMap<QString,QString> values;
    for (auto it=fields.begin();it!=fields.end();++it) values.insert(it.key(),it.value()->text());
    return values;
  }
  QString suggestedKey() const {
    auto author=fields.value(QStringLiteral("author"))->text().section(QStringLiteral(" and "),0,0).trimmed();
    author=author.contains(QLatin1Char(','))?author.section(QLatin1Char(','),0,0):author.section(QLatin1Char(' '),-1);
    author.remove(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}_:-]")));
    auto year=fields.value(QStringLiteral("year"))->text();year.remove(QRegularExpression(QStringLiteral("[^0-9]")));
    return (author.isEmpty()?QStringLiteral("reference"):author)+year;
  }
};

QString changeEntryType(QString text,const QString &key,const QString &type) {
  for (const auto &entry:parseBibliography(text).records) if(entry.key==key) {
    text.replace(entry.start+1,entry.type.size(),type);break;
  }
  return text;
}
}
BibDocument parseBibliography(const QString &text){
  BibDocument result;int p=0;
  auto fail=[&]{result.error=words("BibTeX 结构不完整，请在源码中检查该条目。","Incomplete BibTeX structure. Check this entry in source.");return result;};
  while(p<text.size()){
    whitespace(text,p);if(p>=text.size())break;if(text[p]!=QLatin1Char('@')){++p;continue;}
    BibRecord r;r.start=p++;int a=p;while(p<text.size()&&(text[p].isLetterOrNumber()||text[p]==QLatin1Char('_')||text[p]==QLatin1Char('-')||text[p]==QLatin1Char('.')))++p;r.type=text.mid(a,p-a).toLower();whitespace(text,p);
    if(r.type.isEmpty()||p>=text.size()||(text[p]!=QLatin1Char('{')&&text[p]!=QLatin1Char('(')))return fail();
    const QChar close=text[p++]==QLatin1Char('{')?QLatin1Char('}'):QLatin1Char(')');
    if(r.type==QStringLiteral("comment")||r.type==QStringLiteral("string")||r.type==QStringLiteral("preamble")){
      int braces=0;bool quoted=false,closed=false;for(;p<text.size();++p){if(escapedBib(text,p))continue;const auto c=text[p];if(c==QLatin1Char('"')&&braces==0)quoted=!quoted;if(!quoted){if(c==close&&braces==0){++p;closed=true;break;}if(c==QLatin1Char('{'))++braces;else if(c==QLatin1Char('}'))--braces;}}if(!closed)return fail();continue;
    }
    whitespace(text,p);r.keyStart=p;while(p<text.size()&&text[p]!=QLatin1Char(',')&&text[p]!=close)++p;r.keyEnd=p;while(r.keyEnd>r.keyStart&&text[r.keyEnd-1].isSpace())--r.keyEnd;r.key=text.mid(r.keyStart,r.keyEnd-r.keyStart);if(!keyValid(r.key)||p>=text.size())return fail();
    if(text[p]==QLatin1Char(','))++p;
    while(p<text.size()){
      whitespace(text,p);if(p>=text.size())return fail();if(text[p]==close){r.close=p;r.end=++p;break;}
      a=p;while(p<text.size()&&(text[p].isLetterOrNumber()||text[p]==QLatin1Char('_')||text[p]==QLatin1Char('-')))++p;BibField f;f.name=text.mid(a,p-a).toLower();whitespace(text,p);if(f.name.isEmpty()||r.fields.contains(f.name)||p>=text.size()||text[p++]!=QLatin1Char('='))return fail();whitespace(text,p);f.start=p;
      int braces=0,valueEnd=p;bool quoted=false;
      for(;p<text.size();++p){const auto c=text[p];if(escapedBib(text,p)){valueEnd=p+1;continue;}if(braces==0&&!quoted&&(c==QLatin1Char(',')||c==close))break;
        if(c==QLatin1Char('%')&&braces==0&&!quoted){while(p<text.size()&&text[p]!=QLatin1Char('\n'))++p;if(p>=text.size())break;continue;}
        if(c==QLatin1Char('"')&&braces==0)quoted=!quoted;else if(c==QLatin1Char('{'))++braces;else if(c==QLatin1Char('}')&&--braces<0)return fail();if(!c.isSpace()||braces||quoted)valueEnd=p+1;
      }
      if(p>=text.size()||braces||quoted)return fail();f.end=valueEnd;f.expression=text.mid(f.start,f.end-f.start);if(f.expression.isEmpty())return fail();
      f.literal=(f.expression.startsWith(QLatin1Char('{'))&&f.expression.endsWith(QLatin1Char('}')))||(f.expression.startsWith(QLatin1Char('"'))&&f.expression.endsWith(QLatin1Char('"')));
      // A compound expression is shown as source and never flattened implicitly.
      if(f.literal){int depth=0;bool q=false;for(int j=0;j<f.expression.size();++j)if(!escapedBib(f.expression,j)){auto c=f.expression[j];if(c==QLatin1Char('{'))++depth;else if(c==QLatin1Char('}'))--depth;else if(c==QLatin1Char('"')&&depth==0)q=!q;else if(c==QLatin1Char('#')&&depth==0&&!q)f.literal=false;}}
      f.value=f.literal?f.expression.mid(1,f.expression.size()-2):f.expression;r.fields.insert(f.name,f);if(text[p]==QLatin1Char(','))++p;
    }
    if(!r.end)return fail();result.records.append(r);
  }
  return result;
}
QString normalizedDoi(QString value){value=value.trimmed();value.remove(QRegularExpression(QStringLiteral("^(?:https?://(?:dx\\.)?doi\\.org/|doi:\\s*)"),QRegularExpression::CaseInsensitiveOption));return value.toCaseFolded();}
QStringList bibliographyWorkIds(const BibRecord &record) {
  QStringList ids;
  // Identity comes from persistent identifiers, never a title shared by editions.
  auto value=[&](const char *name){const auto field=record.fields.value(QString::fromLatin1(name));return field.literal?field.value.trimmed():QString();};
  const auto doi=normalizedDoi(value("doi"));
  if(QRegularExpression(QStringLiteral("^10\\.\\d{4,9}/\\S+$")).match(doi).hasMatch())ids.append(QStringLiteral("doi:")+doi);
  const QString pattern=QStringLiteral("(\\d{4}\\.\\d{4,5}|[a-z-]+(?:\\.[a-z]{2})?/\\d{7})(?:v\\d+)?");
  auto add=[&](const QString &text,const QString &expression){const auto match=QRegularExpression(expression,QRegularExpression::CaseInsensitiveOption).match(text);if(match.hasMatch()){const auto id=QStringLiteral("arxiv:")+match.captured(1).toLower();if(!ids.contains(id))ids.append(id);}};
  add(doi,QStringLiteral("^10\\.48550/arxiv\\.")+pattern+QStringLiteral("$"));
  const auto archive=value("eprinttype").isEmpty()?value("archiveprefix"):value("eprinttype");
  if(archive.isEmpty()||archive.compare(QStringLiteral("arxiv"),Qt::CaseInsensitive)==0)
    add(value("eprint"),QStringLiteral("^(?:arxiv:\\s*)?")+pattern+QStringLiteral("$"));
  add(value("url"),QStringLiteral("^https?://(?:www\\.)?arxiv\\.org/(?:abs|pdf)/")+pattern+QStringLiteral("(?:\\.pdf)?(?:[?#].*)?$"));
  for(const auto *name:{"journal","journaltitle","note","howpublished"})add(value(name),QStringLiteral("\\barxiv:\\s*")+pattern+QStringLiteral("(?![\\w.])"));
  return ids;
}
QString patchBibliography(const QString &text,const QString &key,const QMap<QString,QString> &changes,QString *error){
  error->clear();const auto parsed=parseBibliography(text);if(!parsed.error.isEmpty()){*error=parsed.error;return {};}
  QVector<BibRecord> matches;for(const auto &r:parsed.records)if(r.key==key)matches.append(r);if(matches.size()!=1){*error=words("引用键缺失或重复。","Missing or duplicated citation key.");return {};}
  const auto &r=matches.first();struct Change{int start,end;QString value;};QVector<Change> edits;QString additions;
  for(auto it=changes.begin();it!=changes.end();++it){if(!literalValid(it.value())){*error=words("字段中的花括号不匹配。","Unbalanced braces in a field.");return {};}
    const auto f=r.fields.value(it.key());if(r.fields.contains(it.key())){if(it.value()!=f.value){if(!f.literal&&!QRegularExpression(QStringLiteral("^\\d+$")).match(f.expression).hasMatch()){*error=words("此字段包含宏或拼接，请使用条目源码编辑。","This field contains a macro or concatenation. Edit its source.");return {};}edits.append({f.start,f.end,QLatin1Char('{')+it.value()+QLatin1Char('}')});}}
    else if(!it.value().isEmpty())additions+=QStringLiteral("  ")+it.key()+QStringLiteral(" = {")+it.value()+QStringLiteral("},\n");
  }
  QString output=text;
  if(!additions.isEmpty()){
    int end=r.keyEnd;for(const auto &f:r.fields)end=qMax(end,f.end);
    int at=end;whitespace(text,at);const bool comma=at>=text.size()||text[at]!=QLatin1Char(',');
    // A field ending directly at the record's closing brace shares its offset
    // with the insertion. Keep both fragments in one ordered edit.
    if(comma&&end!=r.close)edits.append({end,end,QStringLiteral(",")});
    edits.append({r.close,r.close,(comma&&end==r.close?QStringLiteral(","):QString())+QStringLiteral("\n")+additions});
  }
  std::sort(edits.begin(),edits.end(),[](const Change&a,const Change&b){return a.start>b.start;});for(const auto &e:edits)output.replace(e.start,e.end-e.start,e.value);
  const auto checked=parseBibliography(output);if(!checked.error.isEmpty()){*error=checked.error;return {};}return output;
}
QString mergeBibliography(const QString &existing,const QString &incoming,QStringList *added,QString *error){
  error->clear();added->clear();const auto a=parseBibliography(existing),b=parseBibliography(incoming);if(!a.error.isEmpty()||!b.error.isEmpty()||b.records.isEmpty()){*error=!a.error.isEmpty()?a.error:!b.error.isEmpty()?b.error:words("未找到可导入的 BibTeX 条目。","No BibTeX entries to import.");return {};}
  QMap<QString,QString> keys;QHash<QString,QStringList> keyIds;QSet<QString> works;
  for(const auto &r:a.records){
    if(keys.contains(r.key)){*error=words("现有文献包含重复引用键，请先处理。","Resolve duplicate keys in the existing bibliography first.");return {};}
    keys.insert(r.key,existing.mid(r.start,r.end-r.start));const auto ids=bibliographyWorkIds(r);keyIds.insert(r.key,ids);for(const auto &id:ids)works.insert(id);
  }
  QString append;
  for(const auto &r:b.records){
    const auto raw=incoming.mid(r.start,r.end-r.start);const auto ids=bibliographyWorkIds(r);bool duplicate=false,same=false;
    for(const auto &id:ids){duplicate|=works.contains(id);same|=keyIds.value(r.key).contains(id);}
    if(keys.contains(r.key)){if(keys.value(r.key)!=raw&&!same){*error=words("引用键冲突：","Citation key conflict: ")+r.key;return {};}continue;}
    if(duplicate)continue;
    keys.insert(r.key,raw);keyIds.insert(r.key,ids);for(const auto &id:ids)works.insert(id);added->append(r.key);append+=QStringLiteral("\n\n")+raw;
  }
  // Preserve macro/preamble dependencies by requiring a standalone source import
  // when non-entry records are present; silently dropping them corrupts citations.
  if(QRegularExpression(QStringLiteral("@(?:string|preamble)\\s*[({]"),QRegularExpression::CaseInsensitiveOption).match(incoming).hasMatch()){
    if(existing.trimmed().isEmpty()){*added={};for(const auto &r:b.records)added->append(r.key);return incoming;}
    *error=words("导入文件含宏定义，请直接关联该 .bib 文件以保留定义。","This file defines bibliography macros. Link the .bib file to preserve them.");return {};
  }
  return existing+append+(append.isEmpty()?QString():QStringLiteral("\n"));
}
QString updateBibliographyEntry(const QString &existing,const QString &incoming,QString *key,QString *error) {
  key->clear();error->clear();const auto old=parseBibliography(existing),fresh=parseBibliography(incoming);
  if(!old.error.isEmpty()||!fresh.error.isEmpty()||fresh.records.size()!=1){*error=words("无法读取待更新条目。","Cannot read the reference update.");return {};}
  const auto &source=fresh.records.first();const auto ids=bibliographyWorkIds(source);
  QVector<BibRecord> matches;QSet<QString> keys;
  for(const auto &record:old.records){
    if(keys.contains(record.key)){*error=words("请先处理重复引用键。","Resolve duplicate citation keys first.");return {};}keys.insert(record.key);
    const auto targetIds=bibliographyWorkIds(record);bool same=false;
    for(const auto &id:ids)same|=targetIds.contains(id);
    if(record.key==source.key&&!ids.isEmpty()&&!targetIds.isEmpty()&&!same){*error=words("引用键相同，但文献标识不同：","Same citation key, different reference identifiers: ")+source.key;return {};}
    if(same||record.key==source.key)matches.append(record);
  }
  if(matches.size()!=1){*error=matches.isEmpty()?words("没有可更新的条目。","No matching reference to update."):words("多条文献匹配，请先核对重复项。","Several references match. Review the duplicates first.");return {};}
  const auto target=matches.first();QMap<QString,QString> changes;
  for(const auto &field:source.fields){
    if(field.expression==target.fields.value(field.name).expression)continue;
    if(!field.literal&&!QRegularExpression(QStringLiteral("^\\d+$")).match(field.expression).hasMatch()){
      *error=words("更新含宏或拼接，请关联原 .bib 文件或核对条目源码。","The update contains macros or concatenation. Link its .bib file or review the entry source.");return {};
    }
    if(!field.value.isEmpty())changes.insert(field.name,field.value);
  }
  auto next=patchBibliography(existing,target.key,changes,error);if(!error->isEmpty())return {};
  next=changeEntryType(next,target.key,source.type);*key=target.key;return next;
}
bool removeBibliographyEntry(QWidget *parent,TeXDocumentWindow *owner,const QString &file,const QString &key,int citationCount) {
  BibEdit edit(owner,file);if(!edit.target)return false;
  const auto parsed=parseBibliography(edit.original);if(!parsed.error.isEmpty())return false;
  QVector<BibRecord> matches;for(const auto &record:parsed.records)if(record.key==key)matches.append(record);
  if(matches.size()!=1)return false;
  const auto record=matches.first();
  MaterialDialog dialog(parent);configureSettingsSurface(&dialog);dialog.setWindowTitle(words("删除文献","Delete reference"));
  auto *layout=new QVBoxLayout(&dialog);auto *notice=new QLabel(key+QStringLiteral("\n")+fieldValue(record,QStringLiteral("title")),&dialog);notice->setWordWrap(true);notice->setTextFormat(Qt::PlainText);layout->addWidget(notice);
  auto *impact=new QLabel(citationCount>0?words("文稿中仍有 %1 处引用；删除条目后，这些引用会缺少来源。","This reference is still cited %1 times. Deleting it leaves those citations unresolved.").arg(citationCount):words("当前文稿中未发现引用。","No citations found in the current document project."),&dialog);impact->setWordWrap(true);layout->addWidget(impact);
  auto *buttons=footer(&dialog,layout,words("删除条目","Delete entry"));
  QObject::connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{auto next=edit.original;next.remove(record.start,record.end-record.start);if(edit.apply(next,notice))dialog.accept();});
  fitWorkspaceDialog(&dialog,600,260);return dialog.exec()==QDialog::Accepted;
}
bool editBibliographyEntry(QWidget *parent,TeXDocumentWindow *owner,const QString &file,const QString &key) {
  BibEdit edit(owner,file);
  if (!edit.target) return false;
  const auto parsed=parseBibliography(edit.original);
  BibRecord record; int count=0;
  for (const auto &entry:parsed.records) if(entry.key==key){record=entry;++count;}
  MaterialDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("researchBibEntryDialog"));
  configureSettingsSurface(&dialog);dialog.setWindowTitle(words("编辑文献","Edit reference"));
  auto *layout=new QVBoxLayout(&dialog);layout->setContentsMargins(16,12,16,12);
  auto *tabs=new QTabWidget(&dialog);
  auto *scroll=new QScrollArea(tabs);scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);
  configureSettingsSurface(scroll->viewport());
  auto *fields=new BibliographyFields(record,QStringLiteral("researchBibField_"),false,scroll);
  scroll->setWidget(fields);tabs->addTab(scroll,words("字段","Fields"));
  auto *raw=new QPlainTextEdit(tabs);configureWorkspaceText(raw);raw->setObjectName(QStringLiteral("researchBibRaw"));
  tabs->addTab(raw,words("条目源码","Entry source"));layout->addWidget(tabs,1);
  auto *notice=new QLabel(&dialog);notice->setWordWrap(true);layout->addWidget(notice);
  auto *buttons=footer(&dialog,layout,words("应用","Apply"));
  if(count!=1||!parsed.error.isEmpty()) {
    notice->setText(parsed.error.isEmpty()?words("引用键缺失或重复。","Missing or duplicated citation key."):parsed.error);
    buttons->button(QDialogButtonBox::Ok)->setEnabled(false);dialogSize(&dialog);dialog.exec();return false;
  }
  QString generatedSource=edit.original.mid(record.start,record.end-record.start);
  raw->setPlainText(generatedSource);raw->document()->setModified(false);
  auto fieldDraft=[&](QString *error) {
    const auto next=patchBibliography(edit.original,key,fields->changes(),error);
    return error->isEmpty()?changeEntryType(next,key,fields->typeName()):QString();
  };
  bool syncing=false;
  QObject::connect(tabs,&QTabWidget::currentChanged,&dialog,[&](int tab) {
    if(syncing)return;
    if(tab==1) {
      QString error;const auto next=fieldDraft(&error);
      if(error.isEmpty()) {
        for(const auto &entry:parseBibliography(next).records)if(entry.key==key) {
          generatedSource=next.mid(entry.start,entry.end-entry.start);raw->setPlainText(generatedSource);raw->document()->setModified(false);
        }
      } else notice->setText(error);
    } else if(raw->toPlainText()!=generatedSource) {
      syncing=true;tabs->setCurrentIndex(1);syncing=false;
      notice->setText(words("源码已修改，请应用或取消此草稿。","Source was edited. Apply or cancel this draft."));
    }
  });
  QObject::connect(buttons,&QDialogButtonBox::accepted,&dialog,[&] {
    QString next,error;
    if(tabs->currentIndex()==0) next=fieldDraft(&error);
    else {
      const auto draft=parseBibliography(raw->toPlainText());
      if(!draft.error.isEmpty()||draft.records.size()!=1||draft.records.first().key!=key)
        error=words("请保留原引用键，并只编辑当前条目。","Keep the citation key and edit only this entry.");
      else {next=edit.original;next.replace(record.start,record.end-record.start,raw->toPlainText());}
    }
    if(!error.isEmpty()){notice->setText(error);return;}
    if(edit.apply(next,notice))dialog.accept();
  });
  dialogSize(&dialog,650,560);return dialog.exec()==QDialog::Accepted;
}
#include "ResearchBibliographyImport.inc"
QString acquireBibliography(QWidget *parent,int source,const QUrl &localEndpoint){
  if(source==4){
    MaterialDialog draft(parent);configureSettingsSurface(&draft);draft.setWindowTitle(words("粘贴 BibTeX","Paste BibTeX"));draft.setObjectName(QStringLiteral("researchBibPaste"));
    auto *layout=new QVBoxLayout(&draft);auto *text=new QPlainTextEdit(&draft);configureWorkspaceText(text);text->setObjectName(QStringLiteral("researchBibPasteText"));text->setPlaceholderText(words("在此粘贴一个或多个 BibTeX 条目","Paste one or more BibTeX entries here"));layout->addWidget(text,1);
    auto *notice=new QLabel(&draft);notice->setWordWrap(true);layout->addWidget(notice);auto *buttons=footer(&draft,layout,words("继续","Continue"));
    QObject::connect(buttons,&QDialogButtonBox::accepted,&draft,[&]{
      if(text->toPlainText().size()>2*1024*1024){notice->setText(words("内容较多，请分批导入。","Import this content in smaller batches."));return;}
      const auto parsed=parseBibliography(text->toPlainText());if(!parsed.error.isEmpty()||parsed.records.isEmpty()){notice->setText(parsed.error.isEmpty()?words("未找到 BibTeX 条目。","No BibTeX entries found."):parsed.error);return;}draft.accept();
    });
    fitWorkspaceDialog(&draft,680,480);return draft.exec()==QDialog::Accepted?text->toPlainText():QString();
  }
  const auto zoteroEndpoint=localEndpoint.isEmpty()?QUrl(QStringLiteral("http://127.0.0.1:23119/better-bibtex/json-rpc")):localEndpoint;
  if(source==3&&(zoteroEndpoint.scheme()!=QStringLiteral("http")||zoteroEndpoint.host()!=QStringLiteral("127.0.0.1")))return {};
  if(source==1){const auto file=QFileDialog::getOpenFileName(parent,words("导入 BibTeX","Import BibTeX"),{},QStringLiteral("BibTeX (*.bib)"));QFile f(file);return f.open(QIODevice::ReadOnly)&&f.size()<=4*1024*1024?QString::fromUtf8(f.readAll()):QString();}
  MaterialDialog dialog(parent);dialog.setObjectName(QStringLiteral("researchBibAcquire"));configureSettingsSurface(&dialog);dialog.setWindowTitle(source==3?words("从 Zotero 选择","Choose from Zotero"):source==2?words("查找文献","Find references"):words("新建文献","New reference"));auto *layout=new QVBoxLayout(&dialog);QString selected;
  if(source==0) {
    auto *scroll=new QScrollArea(&dialog);scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);
    configureSettingsSurface(scroll->viewport());
    auto *fields=new BibliographyFields({},QStringLiteral("researchBibNew_"),true,scroll);
    auto *tabs=new QTabWidget(&dialog);scroll->setWidget(fields);tabs->addTab(scroll,words("字段","Fields"));
    auto *raw=new QPlainTextEdit(tabs);configureWorkspaceText(raw);raw->setObjectName(QStringLiteral("researchBibRaw"));raw->document()->setModified(false);tabs->addTab(raw,words("条目源码","Entry source"));layout->addWidget(tabs,1);
    auto *notice=new QLabel(&dialog);notice->setWordWrap(true);layout->addWidget(notice);
    auto *buttons=footer(&dialog,layout,words("继续","Continue"));
    auto fieldDraft=[&]() -> QString {
      const auto key=fields->key->text().trimmed().isEmpty()?fields->suggestedKey():fields->key->text().trimmed();
      if(!keyValid(key)){notice->setText(words("请输入不含空格的引用键。","Enter a citation key without spaces."));return {};}
      QString draft=QLatin1Char('@')+fields->typeName()+QLatin1Char('{')+key+QStringLiteral(",\n");
      const auto values=fields->changes();
      for(auto it=values.begin();it!=values.end();++it)if(!it.value().isEmpty()) {
        if(!literalValid(it.value())){notice->setText(words("字段花括号不匹配。","Unbalanced field braces."));return {};}
        draft+=QStringLiteral("  ")+it.key()+QStringLiteral(" = {")+it.value()+QStringLiteral("},\n");
      }
      return draft+QLatin1Char('}');
    };
    bool switching=false,sourceInitialized=false;QString generatedSource;
    QObject::connect(tabs,&QTabWidget::currentChanged,&dialog,[&](int tab){
      if(switching)return;
      if(tab==1&&(!sourceInitialized||raw->toPlainText()==generatedSource)){generatedSource=fieldDraft();raw->setPlainText(generatedSource);sourceInitialized=true;}
      else if(tab==0&&sourceInitialized&&raw->toPlainText()!=generatedSource){switching=true;tabs->setCurrentIndex(1);switching=false;notice->setText(words("源码已修改，请继续或取消此草稿。","Source was edited. Continue or cancel this draft."));}
    });
    QObject::connect(buttons,&QDialogButtonBox::accepted,&dialog,[&] {
      const auto draft=tabs->currentIndex()==1?raw->toPlainText():fieldDraft();if(draft.isEmpty())return;
      const auto parsed=parseBibliography(draft);
      if(!parsed.error.isEmpty()||parsed.records.size()!=1){notice->setText(parsed.error.isEmpty()?words("请输入一个完整的文献条目。","Enter one complete reference."):parsed.error);return;}
      selected=draft;dialog.accept();
    });
    dialogSize(&dialog,650,560);return dialog.exec()==QDialog::Accepted?selected:QString();
  }
  auto *row=new QHBoxLayout;auto *query=new QLineEdit(&dialog);query->setObjectName(QStringLiteral("researchBibOnlineQuery"));configureField(query);query->setPlaceholderText(source==3?words("搜索本机 Zotero 文献库","Search the local Zotero library"):words("输入题名或 DOI","Enter a title or DOI"));row->addWidget(query,1);auto *search=new QPushButton(words("查找","Search"),&dialog);configureButton(search);row->addWidget(search);layout->addLayout(row);auto *list=new QListWidget(&dialog);list->setObjectName(QStringLiteral("researchBibOnlineResults"));configureSettingsList(list);list->setSelectionMode(source==3?QAbstractItemView::ExtendedSelection:QAbstractItemView::SingleSelection);layout->addWidget(list,1);auto *notice=new QLabel(source==3?words("需要运行 Zotero 和 Better BibTeX。","Requires Zotero with Better BibTeX running."):QString(),&dialog);notice->setWordWrap(true);layout->addWidget(notice);auto *buttons=footer(&dialog,layout,words("选择","Choose"));auto *choose=buttons->button(QDialogButtonBox::Ok);choose->setEnabled(false);QPointer<ExternalTask> task;
  auto request=[&](const QUrl &url,const QByteArray &post,std::function<void(TaskResult)> callback){if(task)task->cancel();QStringList args{QStringLiteral("--silent"),QStringLiteral("--show-error"),QStringLiteral("--fail"),QStringLiteral("--location"),QStringLiteral("--max-redirs"),QStringLiteral("5"),QStringLiteral("--connect-timeout"),QStringLiteral("5"),QStringLiteral("--max-time"),QStringLiteral("20"),QStringLiteral("--max-filesize"),QStringLiteral("4194304"),QStringLiteral("--proto"),source==3?QStringLiteral("=http"):QStringLiteral("=https"),QStringLiteral("--proto-redir"),source==3?QStringLiteral("=http"):QStringLiteral("=https")};if(source==3)args<<QStringLiteral("--noproxy")<<QStringLiteral("*");if(!post.isEmpty())args<<QStringLiteral("-H")<<QStringLiteral("Content-Type: application/json")<<QStringLiteral("--data-binary")<<QStringLiteral("@-");else args<<QStringLiteral("-H")<<(url.host()==QStringLiteral("doi.org")?QStringLiteral("Accept: application/x-bibtex"):QStringLiteral("Accept: application/json"));args<<QStringLiteral("--url")<<url.toString(QUrl::FullyEncoded);task=new ExternalTask(&dialog,systemTool(QStringLiteral("curl")),args,{},std::move(callback),23000,post);};
  auto addResult=[&](const QString &title,const QString &value,bool bib){auto *item=new QListWidgetItem(title,list);item->setData(Qt::UserRole,value);item->setData(Qt::UserRole+1,bib);};
  QObject::connect(search,&QPushButton::clicked,&dialog,[&]{list->clear();choose->setEnabled(false);const auto text=query->text().trimmed();if(text.isEmpty()&&source!=3)return;notice->setText(words("正在查找…","Searching…"));if(source==3){QJsonObject rpc{{QStringLiteral("jsonrpc"),QStringLiteral("2.0")},{QStringLiteral("id"),1},{QStringLiteral("method"),QStringLiteral("item.search")},{QStringLiteral("params"),QJsonArray{text}}};request(zoteroEndpoint,QJsonDocument(rpc).toJson(QJsonDocument::Compact),[&](TaskResult r){auto doc=QJsonDocument::fromJson(r.output).object();if(!r.ok||doc.contains(QStringLiteral("error"))){notice->setText(words("未能连接 Zotero。请检查 Better BibTeX 和本地服务。","Could not reach Zotero. Check Better BibTeX and its local service."));return;}for(const auto &v:doc.value(QStringLiteral("result")).toArray()){const auto o=v.toObject();auto key=o.value(QStringLiteral("citekey")).toString(o.value(QStringLiteral("citation-key")).toString());if(!key.isEmpty())addResult(o.value(QStringLiteral("title")).toString()+QStringLiteral("\n")+key,key,false);if(list->count()>=100)break;}notice->setText(words("找到 %1 项","%1 results").arg(list->count()));});}else{const auto doi=normalizedDoi(text);const bool isDoi=QRegularExpression(QStringLiteral("^10\\.\\d{4,9}/\\S+$")).match(doi).hasMatch();QUrl url(isDoi?QStringLiteral("https://doi.org/")+doi:QStringLiteral("https://api.crossref.org/works"));if(!isDoi){QUrlQuery q;q.addQueryItem(QStringLiteral("query.bibliographic"),text);q.addQueryItem(QStringLiteral("rows"),QStringLiteral("20"));url.setQuery(q);}request(url,{},[&,isDoi](TaskResult r){if(!r.ok){notice->setText(words("查找失败，请检查网络后重试。","Lookup failed. Check the connection and retry."));return;}if(isDoi){auto p=parseBibliography(QString::fromUtf8(r.output));if(p.error.isEmpty()&&p.records.size()==1)addResult(fieldValue(p.records.first(),QStringLiteral("title")),QString::fromUtf8(r.output),true);}else{const auto items=QJsonDocument::fromJson(r.output).object().value(QStringLiteral("message")).toObject().value(QStringLiteral("items")).toArray();for(const auto &v:items){auto o=v.toObject();const auto doi=o.value(QStringLiteral("DOI")).toString();if(!doi.isEmpty())addResult(jsonText(o.value(QStringLiteral("title")))+QStringLiteral("\n")+doi,doi,false);}}notice->setText(words("找到 %1 项","%1 results").arg(list->count()));});}});
  QObject::connect(query,&QLineEdit::returnPressed,search,&QPushButton::click);QObject::connect(list,&QListWidget::itemSelectionChanged,&dialog,[&]{choose->setEnabled(!list->selectedItems().isEmpty());});
  QObject::connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{auto rows=list->selectedItems();if(rows.isEmpty())return;choose->setEnabled(false);if(source==3){QJsonArray keys;for(auto *i:rows)keys.append(i->data(Qt::UserRole).toString());QJsonObject rpc{{QStringLiteral("jsonrpc"),QStringLiteral("2.0")},{QStringLiteral("id"),2},{QStringLiteral("method"),QStringLiteral("item.export")},{QStringLiteral("params"),QJsonArray{keys,QStringLiteral("Better BibTeX")}}};request(zoteroEndpoint,QJsonDocument(rpc).toJson(QJsonDocument::Compact),[&](TaskResult r){auto o=QJsonDocument::fromJson(r.output).object();const auto v=o.value(QStringLiteral("result"));selected=v.isString()?v.toString():jsonText(v);if(r.ok&&!selected.isEmpty()&&parseBibliography(selected).error.isEmpty())dialog.accept();else{notice->setText(words("Zotero 导出失败，请重试。","Zotero export failed. Retry."));choose->setEnabled(true);}});}else{auto *i=rows.first();if(i->data(Qt::UserRole+1).toBool()){selected=i->data(Qt::UserRole).toString();dialog.accept();return;}request(QUrl(QStringLiteral("https://doi.org/")+i->data(Qt::UserRole).toString()),{},[&](TaskResult r){selected=QString::fromUtf8(r.output);const auto p=parseBibliography(selected);if(r.ok&&p.error.isEmpty()&&!p.records.isEmpty())dialog.accept();else{notice->setText(words("未取得 BibTeX 条目，可重试或手动导入。","No BibTeX entry was returned. Retry or import manually."));choose->setEnabled(true);}});}});
  dialogSize(&dialog);const bool accepted=dialog.exec()==QDialog::Accepted;if(task)task->cancel();return accepted?selected:QString();
}
}
