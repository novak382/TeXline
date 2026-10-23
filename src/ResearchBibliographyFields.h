// Shared field vocabulary for bibliography creation and editing. GPL-2.0-or-later.
#pragma once
#include "ResearchSettings.h"
#include <QMap>
#include <QStringList>

namespace Research {
struct BibliographyType {
  QString name, label;
  QStringList fields;
};

inline QString bibliographyFieldLabel(const QString &name) {
  const QMap<QString, QString> labels{
    {QStringLiteral("title"), words("题名", "Title")}, {QStringLiteral("author"), words("作者", "Authors")},
    {QStringLiteral("editor"), words("编辑者", "Editors")}, {QStringLiteral("year"), words("年份", "Year")},
    {QStringLiteral("date"), words("日期", "Date")}, {QStringLiteral("journal"), words("期刊", "Journal")},
    {QStringLiteral("journaltitle"), words("期刊", "Journal")}, {QStringLiteral("booktitle"), words("文集", "Book title")},
    {QStringLiteral("publisher"), words("出版社", "Publisher")}, {QStringLiteral("location"), words("出版地", "Location")},
    {QStringLiteral("address"), words("出版地", "Address")}, {QStringLiteral("edition"), words("版次", "Edition")},
    {QStringLiteral("volume"), words("卷", "Volume")}, {QStringLiteral("number"), words("期／编号", "Number")},
    {QStringLiteral("pages"), words("页码", "Pages")}, {QStringLiteral("series"), words("丛书", "Series")},
    {QStringLiteral("school"), words("学校", "School")}, {QStringLiteral("institution"), words("机构", "Institution")},
    {QStringLiteral("organization"), words("组织", "Organization")}, {QStringLiteral("type"), words("文献类别", "Work type")},
    {QStringLiteral("doi"), QStringLiteral("DOI")}, {QStringLiteral("url"), words("链接", "URL")}, {QStringLiteral("urldate"), words("访问日期", "Accessed")},
    {QStringLiteral("isbn"), QStringLiteral("ISBN")}, {QStringLiteral("issn"), QStringLiteral("ISSN")}, {QStringLiteral("eprint"), words("预印本编号", "Preprint ID")},
    {QStringLiteral("archiveprefix"), words("预印本来源", "Archive")}, {QStringLiteral("eprinttype"), words("预印本来源", "Archive")}, {QStringLiteral("eprintclass"), words("预印本分类", "Preprint category")}, {QStringLiteral("version"), words("版本", "Version")}, {QStringLiteral("note"), words("备注", "Note")}, {QStringLiteral("chapter"), words("章", "Chapter")}, {QStringLiteral("howpublished"), words("发表方式", "Published as")}
  };
  return labels.value(name, name);
}

inline QList<BibliographyType> bibliographyTypes() {
  QList<BibliographyType> result{
    {QStringLiteral("article"), words("期刊论文", "Journal article"), {QStringLiteral("title"),QStringLiteral("author"),QStringLiteral("year"),QStringLiteral("journal"),QStringLiteral("volume"),QStringLiteral("number"),QStringLiteral("pages"),QStringLiteral("doi"),QStringLiteral("url")}},
    {QStringLiteral("book"), words("书籍", "Book"), {QStringLiteral("title"),QStringLiteral("author"),QStringLiteral("editor"),QStringLiteral("year"),QStringLiteral("publisher"),QStringLiteral("edition"),QStringLiteral("volume"),QStringLiteral("series"),QStringLiteral("address"),QStringLiteral("isbn"),QStringLiteral("doi"),QStringLiteral("url")}},
    {QStringLiteral("inproceedings"), words("会议论文", "Conference paper"), {QStringLiteral("title"),QStringLiteral("author"),QStringLiteral("year"),QStringLiteral("booktitle"),QStringLiteral("editor"),QStringLiteral("pages"),QStringLiteral("publisher"),QStringLiteral("organization"),QStringLiteral("address"),QStringLiteral("doi"),QStringLiteral("url")}},
    {QStringLiteral("incollection"), words("书籍章节", "Book chapter"), {QStringLiteral("title"),QStringLiteral("author"),QStringLiteral("year"),QStringLiteral("booktitle"),QStringLiteral("editor"),QStringLiteral("publisher"),QStringLiteral("pages"),QStringLiteral("edition"),QStringLiteral("doi"),QStringLiteral("url")}},
    {QStringLiteral("inbook"), words("书中部分", "Part of a book"), {QStringLiteral("title"),QStringLiteral("author"),QStringLiteral("editor"),QStringLiteral("year"),QStringLiteral("chapter"),QStringLiteral("pages"),QStringLiteral("publisher"),QStringLiteral("edition"),QStringLiteral("doi"),QStringLiteral("url")}},
    {QStringLiteral("proceedings"), words("会议文集", "Proceedings"), {QStringLiteral("title"),QStringLiteral("editor"),QStringLiteral("year"),QStringLiteral("publisher"),QStringLiteral("organization"),QStringLiteral("address"),QStringLiteral("isbn"),QStringLiteral("doi"),QStringLiteral("url")}},
    {QStringLiteral("phdthesis"), words("博士论文", "Doctoral thesis"), {QStringLiteral("title"),QStringLiteral("author"),QStringLiteral("year"),QStringLiteral("school"),QStringLiteral("address"),QStringLiteral("doi"),QStringLiteral("url")}},
    {QStringLiteral("mastersthesis"), words("硕士论文", "Master's thesis"), {QStringLiteral("title"),QStringLiteral("author"),QStringLiteral("year"),QStringLiteral("school"),QStringLiteral("address"),QStringLiteral("doi"),QStringLiteral("url")}},
    {QStringLiteral("thesis"), words("学位论文", "Thesis"), {QStringLiteral("title"),QStringLiteral("author"),QStringLiteral("date"),QStringLiteral("institution"),QStringLiteral("type"),QStringLiteral("location"),QStringLiteral("doi"),QStringLiteral("url")}},
    {QStringLiteral("techreport"), words("技术报告", "Technical report"), {QStringLiteral("title"),QStringLiteral("author"),QStringLiteral("year"),QStringLiteral("institution"),QStringLiteral("number"),QStringLiteral("type"),QStringLiteral("doi"),QStringLiteral("url")}},
    {QStringLiteral("report"), words("研究报告", "Report"), {QStringLiteral("title"),QStringLiteral("author"),QStringLiteral("date"),QStringLiteral("institution"),QStringLiteral("number"),QStringLiteral("type"),QStringLiteral("doi"),QStringLiteral("url")}},
    {QStringLiteral("online"), words("在线资料", "Online resource"), {QStringLiteral("title"),QStringLiteral("author"),QStringLiteral("date"),QStringLiteral("url"),QStringLiteral("urldate"),QStringLiteral("doi")}},
    {QStringLiteral("unpublished"), words("未出版稿", "Unpublished work"), {QStringLiteral("title"),QStringLiteral("author"),QStringLiteral("year"),QStringLiteral("note"),QStringLiteral("eprint"),QStringLiteral("archiveprefix"),QStringLiteral("doi"),QStringLiteral("url")}},
    {QStringLiteral("manual"), words("手册", "Manual"), {QStringLiteral("title"),QStringLiteral("author"),QStringLiteral("year"),QStringLiteral("organization"),QStringLiteral("edition"),QStringLiteral("address"),QStringLiteral("url")}},
    {QStringLiteral("booklet"), words("小册子", "Booklet"), {QStringLiteral("title"),QStringLiteral("author"),QStringLiteral("year"),QStringLiteral("howpublished"),QStringLiteral("address"),QStringLiteral("note"),QStringLiteral("url")}},
    {QStringLiteral("misc"), words("其他", "Other"), {QStringLiteral("title"),QStringLiteral("author"),QStringLiteral("year"),QStringLiteral("howpublished"),QStringLiteral("note"),QStringLiteral("eprint"),QStringLiteral("archiveprefix"),QStringLiteral("doi"),QStringLiteral("url")}}
  };
  result.append({QStringLiteral("dataset"),words("数据集","Dataset"),{QStringLiteral("title"),QStringLiteral("author"),QStringLiteral("year"),QStringLiteral("version"),QStringLiteral("publisher"),QStringLiteral("doi"),QStringLiteral("url")}});
  // Standard BibLaTeX names remain available without adding more controls.
  const auto standard=QStringLiteral("artwork audio bibnote bookinbook collection commentary image inreference jurisdiction legal legislation letter movie music mvbook mvcollection mvproceedings mvreference patent performance periodical reference review software standard suppbook suppcollection suppperiodical video").split(QLatin1Char(' '));
  for(const auto &name:standard)result.append({name,QLatin1Char('@')+name,{QStringLiteral("title"),QStringLiteral("author"),QStringLiteral("year"),QStringLiteral("doi"),QStringLiteral("url"),QStringLiteral("note")}});
  for(auto &entry:result){
    if(entry.name==QStringLiteral("misc")||entry.name==QStringLiteral("unpublished")){entry.fields.append(QStringLiteral("eprinttype"));entry.fields.append(QStringLiteral("eprintclass"));}
    if(entry.name==QStringLiteral("online"))entry.fields.insert(2,QStringLiteral("editor"));
  }
  return result;
}

inline QStringList bibliographyFieldsFor(const QString &type) {
  for (const auto &entry : bibliographyTypes()) if (entry.name == type) return entry.fields;
  return {QStringLiteral("title"),QStringLiteral("author"),QStringLiteral("year"),QStringLiteral("doi"),QStringLiteral("url"),QStringLiteral("note")};
}
}
