// Content-driven names shared by navigation and on-demand authoring.
// GPL-2.0-or-later.
#pragma once
#include <QString>
#include <QSet>
namespace Research {
inline QString objectFamily(QString kind) {
  if(kind.endsWith(QLatin1Char('*')))kind.chop(1);
  if(kind==QStringLiteral("includegraphics")||kind==QStringLiteral("figure")||kind==QStringLiteral("wrapfigure")||kind==QStringLiteral("tikzpicture")||kind==QStringLiteral("picture"))return QStringLiteral("figure");
  if(kind==QStringLiteral("table")||kind==QStringLiteral("longtable")||kind==QStringLiteral("tabular"))return QStringLiteral("table");
  if(kind==QStringLiteral("algorithm")||kind==QStringLiteral("algorithm2e")||kind==QStringLiteral("algorithmic"))return QStringLiteral("algorithm");
  static const QSet<QString> math{QStringLiteral("equation"),QStringLiteral("eqnarray"),QStringLiteral("align"),QStringLiteral("alignat"),QStringLiteral("gather"),QStringLiteral("multline"),QStringLiteral("flalign"),QStringLiteral("displaymath"),QStringLiteral("display")};
  if(math.contains(kind))return QStringLiteral("equation");
  return {};
}
inline QString objectName(const QString &kind,bool english) {
  const auto family=objectFamily(kind);
  if(family==QStringLiteral("figure"))return english?QStringLiteral("Figure"):QStringLiteral("图");
  if(family==QStringLiteral("table"))return english?QStringLiteral("Table"):QStringLiteral("表");
  if(family==QStringLiteral("algorithm"))return english?QStringLiteral("Algorithm"):QStringLiteral("算法");
  if(family==QStringLiteral("equation"))return english?QStringLiteral("Equation"):QStringLiteral("公式");
  return kind;
}
}
