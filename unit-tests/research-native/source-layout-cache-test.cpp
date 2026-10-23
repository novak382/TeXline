// Compare width-only shaping reuse with an independent, cold Qt document.
// Run on the inactive validation desktop with private settings.
#include "ActualAuthoringRecording.h"
#include "ResearchMotion.h"
#include <QAbstractTextDocumentLayout>
#include <QTextLayout>
#include <psapi.h>
#include "document/TeXDocument.h"

static qint64 privateBytes() {
  PROCESS_MEMORY_COUNTERS_EX s{};s.cb=sizeof(s);
  using F=BOOL(WINAPI*)(HANDLE,PPROCESS_MEMORY_COUNTERS,DWORD);
  auto f=reinterpret_cast<F>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"K32GetProcessMemoryInfo"));
  return f&&f(GetCurrentProcess(),reinterpret_cast<PPROCESS_MEMORY_COUNTERS>(&s),sizeof(s))?qint64(s.PrivateUsage):0;
}
static QImage render(QTextDocument *document, qreal top = 0) {
  QImage image(780,1400,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::white);
  QPainter painter(&image);painter.translate(0,-top);document->drawContents(&painter,QRectF(0,top,780,1400));return image;
}
int main(int argc,char **argv) {
  const auto report=argument(1);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
  QTimer::singleShot(60,&app,[&]{
    QJsonArray checks;QJsonObject metrics;int failures=0;const auto folder=QFileInfo(report).absolutePath();
    auto check=[&](const QString &name,bool pass){checks.append(QJsonObject{{"name",name},{"passed",pass}});failures+=!pass;put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"metrics",metrics},{"devicePixelRatio",qApp->primaryScreen()->devicePixelRatio()}}).toJson());};
    QTextDocument document;document.documentLayout();Research::installSourceLayoutCache(&document);
    const QString paragraph=QString::fromUtf8(u8"English office affinity fi ffi — 汉字上下笔画：蘭鬱鹏鹭。العربية עברית á ë 👩‍🔬 ∑ₙ αβγ\n\\begin{equation} x_i^2 + y_i^2 = z_i^2 \\label{eq:one} \\end{equation}\n\tfirst\tsecond\t中文\n");
    document.setPlainText(paragraph.repeated(9));
    check("render comparison exercises enabled shaping caches",document.begin().layout()->cacheEnabled());
    auto compare=[&](const QString &name,qreal width,qreal top=0){
      const auto prior=qApp->property("_texlineWidthRelayoutDocument");
      qApp->setProperty("_texlineWidthRelayoutDocument",qulonglong(reinterpret_cast<quintptr>(&document)));
      document.setTextWidth(width);const auto warm=render(&document,top);qApp->setProperty("_texlineWidthRelayoutDocument",prior);
      QScopedPointer<QTextDocument> cold(document.clone());
      for(auto block=cold->begin();block.isValid();block=block.next())block.layout()->setCacheEnabled(false);
      cold->markContentsDirty(0,cold->characterCount());const auto fresh=render(cold.data(),top);
      const bool same=warm==fresh;check(name,same);
      if(!same){warm.save(folder+"/"+name+"-cached.png");fresh.save(folder+"/"+name+"-cold.png");}
    };
    for(const auto &family:{QStringLiteral("Segoe UI"),QStringLiteral("Microsoft YaHei UI"),QStringLiteral("Consolas")}) {
      QFont font(family);font.setPointSizeF(11.5);document.setDefaultFont(font);
      for(int width:{720,310,580,310})compare(family+QString::number(width),width);
    }
    QTextOption option=document.defaultTextOption();option.setTabStopDistance(80);option.setTextDirection(Qt::RightToLeft);document.setDefaultTextOption(option);compare("right-to-left-tabs",360);
    document.setUseDesignMetrics(true);compare("design-metrics",640);
    QTextCursor cursor(&document);cursor.setPosition(8);cursor.setPosition(110,QTextCursor::KeepAnchor);QTextCharFormat format;format.setFontItalic(true);format.setForeground(QColor("#395b77"));cursor.mergeCharFormat(format);compare("mixed-format",340);
    const auto original=document.toPlainText();cursor.setPosition(30);cursor.insertText(QString::fromUtf8(u8"新增 Ω\n"));compare("content-edit",570);document.undo();check("undo preserves all original characters",document.toPlainText()==original);compare("undo",330);document.redo();compare("redo",690);
    document.setPlainText(paragraph.repeated(500));document.setTextWidth(730);
    document.documentLayout()->documentSize();
    QFont changedFont=document.defaultFont();changedFont.setPointSizeF(18);document.setDefaultFont(changedFont);
    compare("font-change-before-lazy-tail",330,12000);
    option.setTextDirection(Qt::LeftToRight);option.setTabStopDistance(36);document.setDefaultTextOption(option);
    compare("option-change-before-lazy-tail",630,12000);
    metrics.insert("privateBeforeLarge",double(privateBytes()));
    QTextDocument large;large.documentLayout();Research::installSourceLayoutCache(&large);large.setDefaultFont(document.defaultFont());large.setPlainText(paragraph.repeated(1000));
    metrics.insert("privateLoadedLarge",double(privateBytes()));
    for(int i=0;i<14;++i){const auto prior=qApp->property("_texlineWidthRelayoutDocument");qApp->setProperty("_texlineWidthRelayoutDocument",qulonglong(reinterpret_cast<quintptr>(&large)));large.setTextWidth(i%2?330:730);render(&large);qApp->setProperty("_texlineWidthRelayoutDocument",prior);if(i==5)metrics.insert("privateWarmLarge",double(privateBytes()));}
    metrics.insert("privateAfterLarge",double(privateBytes()));check("repeated width changes retain bounded memory",metrics["privateAfterLarge"].toDouble()<=metrics["privateWarmLarge"].toDouble()+24*1024*1024);
    large.setPlainText(QStringLiteral("a bounded line of source text\n").repeated(73000));check("documents above the cache budget use normal Qt layout",!large.property("researchWidthCacheEnabled").toBool()&&!large.begin().layout()->cacheEnabled());
    Tw::Document::TeXDocument source;
    check("adopted Qt archive enables the verified source layout repair",source.property("_texline_lazy_edits").toBool());
    source.documentLayout();Research::installSourceLayoutCache(&source);
    const QString paper=paragraph.repeated(7000);source.setPlainText(paper);source.setDefaultFont(QFont(QStringLiteral("Consolas"),11));
    auto compareSource=[&](const QString &name,qreal width,int position){
      const auto prior=qApp->property("_texlineWidthRelayoutDocument");qApp->setProperty("_texlineWidthRelayoutDocument",qulonglong(reinterpret_cast<quintptr>(&source)));
      source.setTextWidth(width);const auto block=source.findBlock(position);const auto top=source.documentLayout()->blockBoundingRect(block).top();const auto warm=render(&source,top);qApp->setProperty("_texlineWidthRelayoutDocument",prior);
      QScopedPointer<QTextDocument> cold(source.clone());cold->setProperty("_texline_lazy_edits",false);for(auto b=cold->begin();b.isValid();b=b.next())b.layout()->setCacheEnabled(false);cold->markContentsDirty(0,cold->characterCount());
      const auto coldTop=cold->documentLayout()->blockBoundingRect(cold->findBlock(position)).top();const auto fresh=render(cold.data(),coldTop);check(name,warm==fresh&&qAbs(top-coldTop)<.01);
      if(warm!=fresh){warm.save(folder+"/"+name+"-source.png");fresh.save(folder+"/"+name+"-cold.png");}
    };
    compareSource("incremental-width-middle",360,paper.size()/2);compareSource("incremental-width-tail",710,paper.size()-600);
    QTextCursor edit(&source);edit.setPosition(18);edit.setPosition(22,QTextCursor::KeepAnchor);edit.insertText(QStringLiteral("WXYZ"));compareSource("equal-length-edit-tail",380,paper.size()-600);
    source.undo();check("incremental edit undo preserves complete source",source.toPlainText()==paper);compareSource("incremental-undo-tail",620,paper.size()-600);
    QFont larger=source.defaultFont();larger.setPointSize(14);source.setDefaultFont(larger);compareSource("incremental-font-tail",430,paper.size()-600);
    bool ordered=true;qreal previous=-1;for(auto b=source.begin();b.isValid();b=b.next()){const auto rect=source.documentLayout()->blockBoundingRect(b);ordered&=rect.height()>0&&rect.top()>=previous;previous=rect.top();}check("incremental source retains ordered positive paragraph geometry",ordered);
    app.exit(failures?1:0);
  });return app.exec();
}
