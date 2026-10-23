// Typography for on-demand writing and reading dialogs. GPL-2.0-or-later.
#pragma once
#include "ResearchAppearance.h"
#include <QTextBrowser>
#include <QTextDocument>
namespace Research {
inline void configureReadingBrowser(QTextBrowser *browser) {
  browser->setFont(interfaceFont());
  browser->document()->setDefaultFont(interfaceFont());
  browser->document()->setDocumentMargin(0);
  browser->setFrameShape(QFrame::NoFrame);
  browser->setOpenLinks(false);
  browser->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  browser->setLineWrapMode(QTextEdit::WidgetWidth);
}
inline QString readingStyle() {
  return QStringLiteral("<html><head><style>body{font-family:'Segoe UI','Microsoft YaHei UI';font-size:15px;color:#30363c}"
    "p{margin-top:0;margin-bottom:8px;line-height:140%}"
    "p.meta{color:#768089;margin-bottom:10px}"
    "a{color:#536c7b;text-decoration:none}"
    "pre{font-family:'JetBrains Mono NL','Consolas';font-size:15px;white-space:pre-wrap;line-height:150%}"
    "td{padding:0}hr{color:#e7ebed}</style></head><body>");
}
// An excerpt is source text, not a TeX renderer: retain commands and all math.
inline QString readingSection(const QString &title) {
  return QStringLiteral("<p style='font-weight:500;color:#69747c;margin-top:16px;margin-bottom:8px'>%1</p>").arg(title);
}
inline QString readingTitle(const QString &title) {
  return QStringLiteral("<p style='font-weight:600;margin-top:0;margin-bottom:8px'>%1</p>").arg(title);
}
inline QString readingExcerpt(const QString &source) {
  QString result;
  for(const auto &paragraph:source.trimmed().split(QStringLiteral("\n\n"))) {
    result+=QStringLiteral("<p>%1</p>").arg(paragraph.toHtmlEscaped().replace(QLatin1Char('\n'),QStringLiteral("<br>")));
  }
  return result;
}
}
