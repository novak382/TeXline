// On-demand reference previews and related reading. GPL-2.0-or-later.
#include "ResearchReferences.h"
#include "ResearchOnboarding.h"
#include "ResearchTextAnchor.h"
#include "ResearchBibliography.h"
#include "ResearchWorkspaceUi.h"
#include "ResearchReadingUi.h"
#include <QSignalBlocker>
#include "ResearchProject.h"
#include "ResearchObjects.h"
#include "ResearchAuthoring.h"
#include "ResearchProject.h"
#include "ResearchAppearance.h"
#include "ResearchMotion.h"
#include "ResearchBuild.h"
#include "ResearchSlides.h"
#include "ResearchWorkflow.h"
#include "TeXDocumentWindow.h"
#include "PDFDocumentWindow.h"
#include "CompletingEdit.h"
#include "Engine.h"
#include "TWApp.h"
#include "TWSynchronizer.h"
#include <QAction>
#include <QApplication>
#include <QCryptographicHash>
#include <QCache>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDynamicPropertyChangeEvent>
#include <QDir>
#include <QFile>
#include <QFrame>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMap>
#include <QMutex>
#include <QMouseEvent>
#include <QPointer>
#include <QProcess>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QRegularExpression>
#include <QRunnable>
#include <QScreen>
#include <QScrollBar>
#include <QSet>
#include <QTextBlock>
#include <QTextBrowser>
#include <QScrollArea>
#include <QToolButton>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QTimer>
#include <QVBoxLayout>
#include <QtSvg/QSvgRenderer>
#include <algorithm>
#include <climits>
#include <limits>
#include <atomic>
#include <memory>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace SyncTeX {
#include <synctex_parser_advanced.h>
}

namespace Research {
static ReferenceIndex readReferenceIndex(const QString &root,const QHash<QString,QString> &open,bool retainSources,const std::function<bool()> &cancelled,QSet<QString> *unavailable);
namespace {
QString path(const QString &file) {
  QFileInfo info(file); return info.exists() ? info.canonicalFilePath() : info.absoluteFilePath();
}
QString read(const QString &file,bool *ok=nullptr) {
  if(ok)*ok=false;
  QFile f(file); if (!f.open(QIODevice::ReadOnly) || f.size() > 4 * 1024 * 1024) return {};
  const auto data=f.readAll();if(f.error()!=QFileDevice::NoError)return {};
  QString text = QString::fromUtf8(data); text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));if(ok)*ok=true;return text;
}
bool escaped(const QString &text, int i) {
  int count = 0; while (i > 0 && text[--i] == QLatin1Char('\\')) ++count; return count % 2;
}
QString masked(QString text) {
  auto blank = [&](int a, int b) { for (int i = a; i < b; ++i) if (text[i] != QLatin1Char('\n')) text[i] = QLatin1Char(' '); };
  // Mask literal material before comments: a percent inside \verb is not a comment.
  static const QRegularExpression literal(QStringLiteral(R"(\\begin\s*\{(verbatim\*?|Verbatim|lstlisting|minted|comment|filecontents\*?)\}[\s\S]*?\\end\s*\{\1\}|\\verb\*?([^\w\s])[^\n]*?\2)"));
  auto literals = literal.globalMatch(text); while (literals.hasNext()) { auto m = literals.next(); blank(m.capturedStart(), m.capturedEnd()); }
  for (int i = 0; i < text.size(); ++i) if (text[i] == QLatin1Char('%') && !escaped(text, i)) {
    int end = text.indexOf(QLatin1Char('\n'), i); if (end < 0) end = text.size(); blank(i, end); i = end;
  }
  return text;
}
// Balanced groups preserve nested theorem titles and .aux numbers.
QString group(const QString &text, int &pos) {
  while (pos < text.size() && text[pos].isSpace()) ++pos;
  if (pos >= text.size() || text[pos] != QLatin1Char('{')) return {};
  const int first = ++pos; int depth = 1;
  while (pos < text.size()) {
    if (!escaped(text, pos)) {
      if (text[pos] == QLatin1Char('{')) ++depth;
      else if (text[pos] == QLatin1Char('}') && !--depth) { const QString value = text.mid(first, pos - first); ++pos; return value; }
    }
    ++pos;
  }
  return {};
}
struct Locations {
  QVector<int> newlines, headingOffsets; QStringList headings;
  explicit Locations(const QString &text) {
    for (int i = 0; i < text.size(); ++i) if (text[i] == QLatin1Char('\n')) newlines.append(i);
    static const QRegularExpression re(QStringLiteral(R"(\\(?:chapter|section|subsection|subsubsection)\*?\s*(?:\[[^\]]*\]\s*)?\{([^{}]*)\})"));
    auto matches = re.globalMatch(text);
    while (matches.hasNext()) { auto m = matches.next(); headingOffsets.append(m.capturedStart()); headings.append(m.captured(1)); }
  }
  int line(int offset) const { return int(std::lower_bound(newlines.begin(), newlines.end(), offset) - newlines.begin()) + 1; }
  QString heading(int offset) const {
    const int n = int(std::upper_bound(headingOffsets.begin(), headingOffsets.end(), offset) - headingOffsets.begin());
    return n ? headings[n - 1] : QString();
  }
  QString lines(const QString &text, int first, int last) const {
    const int start = first <= 1 ? 0 : newlines[qMin(first - 2, int(newlines.size()) - 1)] + 1;
    const int end = last > newlines.size() ? text.size() : newlines[last - 1];
    return text.mid(start, end - start);
  }
};
bool equation(const QString &env) {
  QString name = env; name.remove(QLatin1Char('*'));
  return QStringList{QStringLiteral("equation"), QStringLiteral("align"), QStringLiteral("alignat"), QStringLiteral("gather"), QStringLiteral("multline"), QStringLiteral("eqnarray"), QStringLiteral("displaymath"), QStringLiteral("display")}.contains(name);
}
bool graphicMath(const ReferenceBlock &block) {
  static const QRegularExpression graphics(QStringLiteral(R"(\\(?:includegraphics|rule|tikz|cancel|bcancel|xcancel)\b|\\begin\s*\{(?:picture|tikzpicture|pspicture)\})"));
  return graphics.match(masked(block.text)).hasMatch();
}
const QRegularExpression &referencePattern() {
  static const QRegularExpression re(QStringLiteral(R"(\\(?:eqref|ref|pageref|autoref|cref|Cref|vref|Vref|nameref|label)\*?\s*\{([^{}]*)\})")); return re;
}
void parseFile(ReferenceIndex &index, const QString &file, const QString &text, const QSet<QString> &theorems) {
  const int firstBlock = index.blocks.size();
  const QString clean = maskProjectText(text);
  const Locations locations(clean);
  struct Open { QString kind; int start; }; QVector<Open> stack;
  static const QRegularExpression env(QStringLiteral(R"(\\(begin|end)\s*\{([^{}]+)\}|\\(\[|\]))"));
  auto matches = env.globalMatch(clean);
  while (matches.hasNext()) {
    const auto m = matches.next(); const bool begin = m.captured(1) == QStringLiteral("begin") || m.captured(3) == QStringLiteral("[");
    const QString kind = m.captured(3).isEmpty() ? m.captured(2) : QStringLiteral("display");
    if (begin) { stack.append({kind, int(m.capturedStart())}); continue; }
    if (stack.isEmpty() || stack.last().kind != kind) continue;
    const auto open = stack.takeLast();
    if (!equation(kind) && !theorems.contains(kind) && kind != QStringLiteral("proof") && objectFamily(kind).isEmpty() && kind!=QStringLiteral("quote") && kind!=QStringLiteral("quotation")) continue;
    bool nested=false;for(const auto &outer:stack)if(!objectFamily(outer.kind).isEmpty()&&(objectFamily(outer.kind)==objectFamily(kind)||objectFamily(outer.kind)==QStringLiteral("figure")||objectFamily(outer.kind)==QStringLiteral("table")||objectFamily(outer.kind)==QStringLiteral("algorithm")))nested=true;if(nested)continue;
    index.blocks.append({file, kind, text.mid(open.start, m.capturedEnd() - open.start), locations.heading(open.start), open.start, int(m.capturedEnd()), locations.line(open.start), locations.line(m.capturedEnd()), equation(kind)});
  }
  static const QRegularExpression footnotes(QStringLiteral(R"(\\footnote\s*(?:\[[^\]]*\]\s*)?\{)"));auto notes=footnotes.globalMatch(clean);
  while(notes.hasNext()){const auto m=notes.next();int end=int(m.capturedEnd())-1;group(clean,end);if(end>m.capturedEnd()&&clean[end-1]==QLatin1Char('}'))index.blocks.append({file,QStringLiteral("footnote"),text.mid(m.capturedStart(),end-m.capturedStart()),locations.heading(m.capturedStart()),int(m.capturedStart()),end,locations.line(m.capturedStart()),locations.line(end),false});}
  QVector<const ReferenceBlock *> sortedBlocks, activeBlocks;
  for (int i = firstBlock; i < index.blocks.size(); ++i) sortedBlocks.append(&index.blocks[i]);
  std::sort(sortedBlocks.begin(), sortedBlocks.end(), [](const ReferenceBlock *a, const ReferenceBlock *b) { return a->start < b->start; });
  int nextBlock = 0;
  auto refs = referencePattern().globalMatch(clean);
  while (refs.hasNext()) {
    const auto m = refs.next(); const bool label = m.captured().startsWith(QStringLiteral("\\label"));
    for (const QString &raw : m.captured(1).split(QLatin1Char(','))) {
      const QString key = raw.trimmed(); if (key.isEmpty() || key.contains(QLatin1Char('\\'))) continue;
      const int offset = m.capturedStart(), line = locations.line(offset);
      if (label) {
        ReferenceBlock owner; owner.file = file; owner.start = offset; owner.end = m.capturedEnd(); owner.firstLine = owner.lastLine = line;
        owner.text = locations.lines(text, line, line); owner.heading = locations.heading(offset);
        int span = INT_MAX;
        while (nextBlock < sortedBlocks.size() && sortedBlocks[nextBlock]->start <= offset) activeBlocks.append(sortedBlocks[nextBlock++]);
        activeBlocks.erase(std::remove_if(activeBlocks.begin(), activeBlocks.end(), [offset](const ReferenceBlock *block) { return block->end < offset; }), activeBlocks.end());
        for (const auto *block : activeBlocks) if (block->end - block->start < span) { owner = *block; span = block->end - block->start; }
        index.definitions.append({key, {}, {}, owner, line});
      } else index.uses.append({key, file, locations.lines(text, qMax(1, line - 1), line + 1), locations.heading(offset), line});
    }
  }
}
QString auxFile(const QString &root, const QString &pdf) {
  const auto metadata=pdfMetadata(pdf);
  if (!metadata.isEmpty()) {
    const QString output = metadata.value(QStringLiteral("outputDir")).toString();
    const QString candidate = QDir(output).filePath(QFileInfo(root).completeBaseName() + QStringLiteral(".aux"));
    if (!output.isEmpty() && QFileInfo(candidate).isFile()) return candidate;
  }
  return QFileInfo(root).absoluteDir().filePath(QFileInfo(root).completeBaseName() + QStringLiteral(".aux"));
}
void readNumbers(ReferenceIndex &index, const QString &first,const QHash<QString,QString> &snapshots={}) {
  QHash<QString, QVector<int>> definitions;
  for (int i = 0; i < index.definitions.size(); ++i) definitions[index.definitions[i].key].append(i);
  QStringList todo{first}; QSet<QString> seen; int bytes = 0;
  static const QRegularExpression commands(QStringLiteral(R"(\\(newlabel|@input)\s*)"));
  while (!todo.isEmpty() && seen.size() < 250 && bytes < 16 * 1024 * 1024) {
    const QString file = path(todo.takeFirst()); if (seen.contains(file)) continue; seen.insert(file);
    const QString text = snapshots.isEmpty()?read(file):snapshots.value(file); bytes += text.size() * 2; auto matches = commands.globalMatch(text);
    QSet<QString> slideLabels;static const QRegularExpression slides(QStringLiteral(R"(\\beamer@slide\s*\{([^{}]+)\})"));auto marks=slides.globalMatch(text);while(marks.hasNext())slideLabels.insert(marks.next().captured(1));
    while (matches.hasNext()) {
      auto m = matches.next(); int pos = m.capturedEnd(); const QString key = group(text, pos);
      if (m.captured(1) == QStringLiteral("@input")) { if (!key.contains(QLatin1Char('\\'))) todo.append(QFileInfo(file).absoluteDir().filePath(key)); continue; }
      const QString payload = group(text, pos); int p = 0; const QString number = group(payload, p); group(payload, p); group(payload, p); QString anchor = group(payload, p);
      if(anchor==QStringLiteral("Doc-Start")&&slideLabels.contains(key))anchor=key;
      for (const int i : definitions.value(key)) { index.definitions[i].number = number; index.definitions[i].anchor = anchor; }
    }
  }
}
QHash<QString, QString> buffers() {
  QHash<QString, QString> result;
  for (auto *window : TeXDocumentWindow::documentList()) if (!window->untitled()) result.insert(path(window->fileName()), window->editor()->toPlainText());
  return result;
}
struct Request {
  QString root, file, pdf, key, anchor;
  QString vectorProgram, temporaryFolder;
  QHash<QString, QString> buffers;
  int offset{-1}, page{-1},selectionStart{-1},selectionEnd{-1}; QPointF point;
  qint64 pdfSize{-1}, pdfTime{-1};
  qreal density{1};
  QtPDF::PDFDestination destination;
  bool related{false}, body{false}, pinned{false};
  bool citation{false},restorePinnedSelection{false};QVariantMap contextRange;
  std::function<bool()> cancelled;
};
struct Result {
  ReferenceIndex index; QString key, title, message;
  QImage image; ReferenceDefinition target; bool found{false};
  qreal density{1};
  QByteArray vector; QRectF viewBox, formulaBox, numberBox; QSizeF visualSize;
  int page{-1};
  bool previous{false};
  bool citation{false};
  QSharedPointer<const ProjectIndex> project;
  QHash<QString,QString> sourceHashes;
  QHash<QString,QPair<qint64,qint64>> sourceStamps;
  QSet<QString> unavailableSources;
  QHash<QString,QString> bibliographyErrors;
  bool sourcesCurrent{true};
  QVector<QPair<QString,BibRecord>> bibliography;
  bool visual() const { return !image.isNull() || !vector.isEmpty(); }
};
QByteArray vectorPage(const Request &request, int page) {
  if (request.vectorProgram.isEmpty()) return {};
  const QFileInfo info(request.pdf);
  const QString digest=pdfMetadata(request.pdf).value(QStringLiteral("pdfHash")).toString();
  const QString key = request.pdf + QStringLiteral(":%1:%2:%3:%4").arg(info.size()).arg(info.lastModified().toMSecsSinceEpoch()).arg(page).arg(digest);
  // A single conversion in flight and at most 4 MiB of SVG data. All work is
  // off the GUI thread, and cached pages are shared between source/PDF hovers.
  static QMutex mutex; static QCache<QString, QByteArray> cache(4096);
  QMutexLocker lock(&mutex); if (auto *cached = cache.object(key)) return *cached;
  if (!QDir().mkpath(request.temporaryFolder)) return {};
  QTemporaryDir directory(request.temporaryFolder + QStringLiteral("/preview-XXXXXX")); if (!directory.isValid()) return {};
  const QString output = directory.filePath(QStringLiteral("page.svg"));
  QProcess process; process.setProgram(request.vectorProgram);
#ifdef Q_OS_WIN
  process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) { args->flags |= CREATE_NO_WINDOW; });
#endif
  // Cairo on Windows cannot open Unicode output paths. Use a relative filename
  // in the private Unicode working directory; Qt opens that directory natively.
  process.setWorkingDirectory(directory.path());
  process.setArguments({QStringLiteral("-svg"), QStringLiteral("-f"), QString::number(page + 1), QStringLiteral("-l"), QString::number(page + 1), request.pdf, QStringLiteral("page.svg")});
  process.setStandardOutputFile(QProcess::nullDevice()); process.setStandardErrorFile(QProcess::nullDevice()); process.start();
  if (!process.waitForStarted(500) || !process.waitForFinished(2000)) { process.kill(); process.waitForFinished(300); return {}; }
  if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) return {};
  QFile file(output); if (!file.open(QIODevice::ReadOnly) || file.size() > 2 * 1024 * 1024) return {};
  QByteArray bytes = file.readAll();
  // Cairo's symbols contain outlined embedded fonts. Groups with the same IDs
  // preserve those paths in Qt's SVG Tiny renderer, with no font substitution.
  bytes.replace("<symbol", "<g").replace("</symbol>", "</g>");
  if (!bytes.contains("<svg")) return {};
  cache.insert(key, new QByteArray(bytes), qMax(1, int(bytes.size() + 1023) / 1024)); return bytes;
}
bool validSourcePair(const Request &request) {
  // This runs off the GUI thread: do not consult editor objects or shared UI caches.
  const auto metadata=pdfMetadata(request.pdf);
  if(metadata.isEmpty())return false;
  auto hash = [](const QString &file) {
    QFile input(file); QCryptographicHash digest(QCryptographicHash::Sha256);
    if (!input.open(QIODevice::ReadOnly) || !digest.addData(&input)) return QString();
    return QString::fromLatin1(digest.result().toHex());
  };
  const QString pdfHash = metadata.value(QStringLiteral("pdfHash")).toString();
  const QString syncHash = metadata.value(QStringLiteral("syncHash")).toString();
  const QString suffix = metadata.value(QStringLiteral("syncSuffix")).toString();
  if (pdfHash.size() != 64 || syncHash.size() != 64 || (suffix != QStringLiteral(".synctex") && suffix != QStringLiteral(".synctex.gz"))) return false;
  if (hash(request.pdf) != pdfHash || hash(request.pdf.left(request.pdf.size() - 4) + suffix) != syncHash) return false;
  const auto inputs = metadata.value(QStringLiteral("inputs")).toObject();
  if (inputs.isEmpty()) return false;
  for (auto it = inputs.begin(); it != inputs.end(); ++it) {
    const QString file = QDir::isAbsolutePath(it.key()) ? path(it.key()) : path(QFileInfo(request.pdf).absoluteDir().filePath(it.key()));
    if (hash(file) != it.value().toObject().value(QStringLiteral("hash")).toString()) return false;
    if (request.buffers.contains(file) && read(file) != request.buffers.value(file)) return false;
  }
  return true;
}
// Preview queries need every candidate: a page's running header can carry the
// same source line as a formula on the following page. The navigation API keeps
// only its first page, so give this worker its own scanner and explicit bounds.
class PreviewSync final {
  SyncTeX::synctex_scanner_p scanner{nullptr};
  QHash<QString, QByteArray> names;
  QHash<int, QString> files;
public:
  PreviewSync(const QString &pdf, const QString &root) {
    scanner = SyncTeX::synctex_scanner_new_with_output_file(pdf.toLocal8Bit().constData(), nullptr, 1);
    if (!scanner) return;
    const QDir directory(QFileInfo(root).absolutePath());
    for (auto node = SyncTeX::synctex_scanner_input(scanner); node; node = SyncTeX::synctex_node_sibling(node)) {
      const int tag = SyncTeX::synctex_node_tag(node);
      const QByteArray name(SyncTeX::synctex_scanner_get_name(scanner, tag));
      const QString file = path(directory.filePath(QString::fromLocal8Bit(name)));
      names.insert(file, name); files.insert(tag, file);
    }
  }
  ~PreviewSync() { if (scanner) SyncTeX::synctex_scanner_free(scanner); }
  PreviewSync(const PreviewSync &) = delete;
  PreviewSync &operator=(const PreviewSync &) = delete;
  bool isValid() const { return scanner; }
  QRectF strictStatement(const ReferenceBlock &block,const ReferenceIndex &index,const QSharedPointer<QtPDF::Backend::Document> &doc,int &page) {
    page=-1;
    if(!scanner||!doc||graphicMath(block)||QRegularExpression(QStringLiteral(R"(\\(?:input|include|subfile)\b)")).match(masked(block.text)).hasMatch())return {};
    // Different environments on one source line cannot be separated by SyncTeX.
    for(const auto &peer:index.blocks)if(peer.file==block.file&&(peer.start>=block.end||peer.end<=block.start)&&peer.firstLine<=block.lastLine&&peer.lastLine>=block.firstLine)return {};
    QRectF result;QSet<int> pages;
    if(block.lastLine-block.firstLine>256)return {};
    for(int line=block.firstLine;line<=block.lastLine;++line){const auto hits=fromTeX(block,line,-1);for(auto it=hits.begin();it!=hits.end();++it)pages.insert(it.key());}
    if(pages.size()>16)return {};
    for(int n:pages) {
      if(n<0||n>=doc->numPages())continue;
      QRectF bounds;QList<QRectF> outside;
      std::function<void(SyncTeX::synctex_node_p)> visit=[&](SyncTeX::synctex_node_p node){for(auto current=node;current;current=SyncTeX::synctex_node_sibling(current)){
        if(SyncTeX::synctex_node_type(current)==SyncTeX::synctex_node_type_hbox&&files.value(SyncTeX::synctex_node_tag(current))==block.file){
          const int line=SyncTeX::synctex_node_line(current);const QRectF box(SyncTeX::synctex_node_visible_h(current),SyncTeX::synctex_node_visible_v(current)-SyncTeX::synctex_node_visible_height(current),SyncTeX::synctex_node_visible_width(current),SyncTeX::synctex_node_visible_height(current)+SyncTeX::synctex_node_visible_depth(current));
          if(box.width()>0&&box.height()>0){if(line>=block.firstLine&&line<=block.lastLine)bounds=bounds.isEmpty()?box:bounds.united(box);else if(line>0)outside.append(box);}
        }
        visit(SyncTeX::synctex_node_child(current));
      }};visit(SyncTeX::synctex_sheet_content(scanner,n+1));
      if(bounds.isEmpty())continue;
      if(page>=0)return {}; // Multi-page statements use complete current source.
      for(const auto &box:outside)if(bounds.adjusted(0,1,0,-1).contains(box.center()))return {};
      auto p=doc->page(n).toStrongRef();if(!p)return {};
      QtPDF::Backend::Page::BoxBoundaryList glyphs;p->selectedText({QPolygonF(bounds)},nullptr,&glyphs);QRectF ink;
      for(const auto &box:glyphs)if(!box.isEmpty()&&bounds.adjusted(-1,-1,1,1).contains(box.center()))ink=ink.isEmpty()?box:ink.united(box);
      if(ink.isEmpty())return {};bounds.setLeft(qMin(bounds.left(),ink.left()));bounds.setRight(ink.right());bounds=bounds.united(ink);page=n;result=bounds;
    }
    return result;
  }
  TWSynchronizer::TeXSyncPoint fromPDF(int page, QPointF point) {
    TWSynchronizer::TeXSyncPoint hit{{}, -1, -1, -1};
    if (scanner && SyncTeX::synctex_edit_query(scanner, page + 1, static_cast<float>(point.x()), static_cast<float>(point.y())) > 0)
      while (auto node = SyncTeX::synctex_scanner_next_result(scanner)) {
        if (SyncTeX::synctex_node_line(node) <= 0) continue;
        hit.filename = files.value(SyncTeX::synctex_node_tag(node));
        hit.line = SyncTeX::synctex_node_line(node); break;
      }
    return hit;
  }
  QRectF numberedDisplay(int page,const QRectF &number,qreal pageWidth){
    QRectF best;if(!scanner||number.isEmpty())return {};
    std::function<void(SyncTeX::synctex_node_p)> visit=[&](SyncTeX::synctex_node_p node){for(auto n=node;n;n=SyncTeX::synctex_node_sibling(n)){
      if(SyncTeX::synctex_node_type(n)==SyncTeX::synctex_node_type_hbox){
        const QRectF r(SyncTeX::synctex_node_visible_h(n),SyncTeX::synctex_node_visible_v(n)-SyncTeX::synctex_node_visible_height(n),SyncTeX::synctex_node_visible_width(n),SyncTeX::synctex_node_visible_height(n)+SyncTeX::synctex_node_visible_depth(n));
        if(r.width()>qMin(pageWidth*.5,number.width()*2)&&r.adjusted(-1,-1,1,1).contains(number.center())&&(best.isEmpty()||r.width()*r.height()<best.width()*best.height()))best=r;
      }visit(SyncTeX::synctex_node_child(n));
    }};visit(SyncTeX::synctex_sheet_content(scanner,page+1));return best;
  }
  QList<QList<QRectF>> displayRows(int page, const QString &file, int line, qreal pageWidth) {
    QList<QList<QRectF>> groups;if(!scanner)return groups;
    auto box=[](SyncTeX::synctex_node_p n){return QRectF(SyncTeX::synctex_node_visible_h(n),SyncTeX::synctex_node_visible_v(n)-SyncTeX::synctex_node_visible_height(n),SyncTeX::synctex_node_visible_width(n),SyncTeX::synctex_node_visible_height(n)+SyncTeX::synctex_node_visible_depth(n));};
    std::function<void(SyncTeX::synctex_node_p)> visit=[&](SyncTeX::synctex_node_p node){
      for(auto n=node;n;n=SyncTeX::synctex_node_sibling(n)){
        const auto parent=box(n);
        if(SyncTeX::synctex_node_type(n)==SyncTeX::synctex_node_type_vbox&&parent.width()>pageWidth*.55){
          QList<QRectF> rows;
          for(auto child=SyncTeX::synctex_node_child(n);child;child=SyncTeX::synctex_node_sibling(child)){
            if(SyncTeX::synctex_node_type(child)!=SyncTeX::synctex_node_type_hbox||SyncTeX::synctex_node_line(child)!=line||files.value(SyncTeX::synctex_node_tag(child))!=file)continue;
            const auto r=box(child);
            // Paragraphs retain the entire line width; display rows have their
            // natural math width. Keep their full TeX height, including rules.
            if(r.width()>3&&r.height()>3&&qAbs(r.width()-parent.width())>2)rows.append(r);
          }
          if(!rows.isEmpty())groups.append(rows);
        }
        visit(SyncTeX::synctex_node_child(n));
      }
    };visit(SyncTeX::synctex_sheet_content(scanner,page+1));return groups;
  }
  QMap<int, QList<QRectF>> fromTeX(const ReferenceBlock &block, int line, int pageHint) {
    QMap<int, QList<QRectF>> candidates;
    const QByteArray name = names.value(block.file);
    if (!scanner || name.isEmpty() || SyncTeX::synctex_display_query(scanner, name.constData(), line, 0, pageHint + 1) <= 0) return candidates;
    while (auto node = SyncTeX::synctex_scanner_next_result(scanner)) {
      const int page = SyncTeX::synctex_node_page(node) - 1;
      if (page < 0 || (pageHint >= 0 && page != pageHint)) continue;
      const int sourceLine = SyncTeX::synctex_node_line(node);
      if (files.value(SyncTeX::synctex_node_tag(node)) != block.file || sourceLine < block.firstLine - 2 || sourceLine > block.lastLine + 2) continue;
      const QRectF box(SyncTeX::synctex_node_box_visible_h(node),
                       SyncTeX::synctex_node_box_visible_v(node) - SyncTeX::synctex_node_box_visible_height(node),
                       qMax(qreal(1), qreal(SyncTeX::synctex_node_box_visible_width(node))),
                       SyncTeX::synctex_node_box_visible_height(node) + SyncTeX::synctex_node_box_visible_depth(node));
      if (box.width() > 0 && box.height() > 0 && !candidates[page].contains(box)) candidates[page].append(box);
    }
    return candidates;
  }
};
ReferenceBlock previewBlock(const ReferenceDefinition &definition, const ReferenceIndex &index) {
  ReferenceBlock block = definition.block; QString kind = block.kind; kind.remove(QLatin1Char('*'));
  if (!QStringList{QStringLiteral("align"), QStringLiteral("alignat"), QStringLiteral("gather"), QStringLiteral("eqnarray")}.contains(kind)) return block;
  QSet<QString> anchors;
  for (const auto &peer : index.definitions) if (peer.block.file == block.file && peer.block.start == block.start && peer.block.end == block.end) anchors.insert(peer.anchor.isEmpty() ? peer.key : peer.anchor);
  if (anchors.size() < 2) return block; // A single numbered, multi-line formula.
  const QString clean = masked(block.text);
  static const QRegularExpression begin(QStringLiteral(R"(^\\begin\s*\{[^{}]+\})")), label(QStringLiteral(R"(\\label\s*\{([^{}]+)\})"));
  int wanted = -1; auto labels = label.globalMatch(clean);
  while (labels.hasNext()) { const auto m = labels.next(); if (m.captured(1) == definition.key) { wanted = m.capturedStart(); break; } }
  if (wanted < 0) return block;
  int start = begin.match(clean).capturedEnd(); if (start < 0) return block;
  if (kind == QStringLiteral("alignat")) group(clean, start);
  int braces = 0, nested = 0, finish = clean.size();
  for (int i = start; i < clean.size(); ++i) {
    if (clean[i] == QLatin1Char('\\')) {
      if (clean.mid(i, 6) == QStringLiteral("\\begin") || clean.mid(i, 4) == QStringLiteral("\\end")) {
        const bool opening = clean.mid(i, 6) == QStringLiteral("\\begin"); int pos = i + (opening ? 6 : 4); group(clean, pos);
        if (opening) ++nested;
        else if (!nested) { finish = i; break; } else --nested;
        i = pos - 1; continue;
      }
      if (i + 1 < clean.size() && clean[i + 1] == QLatin1Char('\\')) {
        if (!braces && !nested) {
          if (wanted < i) { finish = i; break; }
          start = i + 2;
          if (start < clean.size() && clean[start] == QLatin1Char('*')) ++start;
          if (start < clean.size() && clean[start] == QLatin1Char('[')) { const int end = clean.indexOf(QLatin1Char(']'), start); if (end >= 0) start = end + 1; }
        }
        ++i; continue;
      }
      if (i + 1 < clean.size() && QStringLiteral("{}%\\").contains(clean[i + 1])) { ++i; continue; }
    }
    if (clean[i] == QLatin1Char('{')) ++braces;
    else if (clean[i] == QLatin1Char('}')) --braces;
  }
  if (wanted < start || wanted >= finish) return block;
  block.firstLine += block.text.left(start).count(QLatin1Char('\n'));
  block.lastLine = block.firstLine + block.text.mid(start, finish - start).count(QLatin1Char('\n'));
  block.start += start; block.end = definition.block.start + finish; block.text = block.text.mid(start, finish - start);
  return block;
}
QRectF blockRect(PreviewSync &sync, const ReferenceBlock &block, int &page, int pageHint = -1, qreal targetY = -1, qreal top = -1, qreal bottom = -1, const ReferenceBlock *sourceBounds = nullptr) {
  QMap<int, QList<QRectF>> candidates; page = -1;
  const auto &source = sourceBounds ? *sourceBounds : block;
  const QStringList lines = source.text.split(QLatin1Char('\n')); QSet<int> queried;
  auto query = [&](int line) {
    if (queried.contains(line)) return; queried.insert(line);
    const auto hit = sync.fromTeX(source, line, pageHint);
    for (auto it = hit.begin(); it != hit.end(); ++it) for (auto box : it.value()) {
      if (top >= 0 || bottom >= 0) box = box.intersected(QRectF(box.left(), qMax(qreal(0), top), box.width(), (bottom < 0 ? 100000 : bottom) - qMax(qreal(0), top)));
      if (!box.isEmpty() && !candidates[it.key()].contains(box)) candidates[it.key()].append(box);
    }
  };
  // TeX may defer all nodes of an AMS environment until its closing line.
  // Include that record even for a row near the beginning of a long display.
  query(source.lastLine);
  for (int n = 0; n < lines.size() && n < 80; ++n) {
    QString line = masked(lines[n]).trimmed(); line.remove(QRegularExpression(QStringLiteral(R"(\\label\s*\{[^{}]*\})")));
    if (line.isEmpty() || (block.equation && QRegularExpression(QStringLiteral(R"(^\\(?:begin|end)\s*\{[^{}]+\}\s*$|^\\[\[\]]\s*$)")).match(line).hasMatch())) continue;
    query(source.firstLine + n);
  }
  QRectF best; qreal bestDistance = std::numeric_limits<qreal>::max(), bestArea = -1; int regionCount = 0;
  for (auto it = candidates.begin(); it != candidates.end(); ++it) {
    auto boxes = it.value();
    // A parent vbox can enclose a whole multi-page align. Prefer its line boxes
    // so it cannot connect unrelated rows into one giant preview rectangle.
    if (!graphicMath(block)) boxes.erase(std::remove_if(boxes.begin(), boxes.end(), [&](const QRectF &box) {
      for (const auto &child : it.value()) if (box.height() > child.height() + 4 && box.contains(child)) return true;
      return false;
    }), boxes.end());
    std::sort(boxes.begin(), boxes.end(), [](const QRectF &a, const QRectF &b) { return a.top() < b.top(); });
    QList<QRectF> regions;
    for (const auto &box : boxes) {
      const qreal gap = sourceBounds && sourceBounds->start != block.start ? 0 : 10;
      if (regions.isEmpty() || (block.equation && box.top() > regions.last().bottom() + gap)) regions.append(box);
      else regions.last() = regions.last().united(box);
    }
    for (const auto &region : regions) {
      ++regionCount;
      const qreal distance = targetY < 0 ? 0 : qMax(qMax(region.top() - targetY, targetY - region.bottom()), qreal(0));
      const qreal area = region.width() * region.height();
      if (distance < bestDistance || (distance == bestDistance && area > bestArea)) { best = region; page = it.key(); bestDistance = distance; bestArea = area; }
    }
  }
  // An equation destination belongs near its formula, never a remote header.
  if (block.equation && targetY >= 0 && bestDistance > 36) { page = -1; return {}; }
  if (targetY < 0 && regionCount != 1) { page = -1; return {}; }
  return best;
}
QRectF referenceRect(PreviewSync &sync, const ReferenceIndex &index, const ReferenceDefinition &target, const QSharedPointer<QtPDF::Backend::Document> &doc, int &page, int pageHint=-1) {
  int targetPage = -1; qreal targetY = -1, top = -1, bottom = -1;
  QRectF numberBox;
  if (!target.anchor.isEmpty()) {
    const auto destination = doc->resolveDestination(QtPDF::PDFDestination(target.anchor));
    auto p = doc->page(destination.page()).toStrongRef();
    if (p && destination.isExplicit() && destination.top() >= 0) {
      targetPage = destination.page(); targetY = p->pageSizeF().height() - destination.top();
      if (target.block.equation && !target.number.isEmpty()) {
        QString number = target.number; number.remove(QLatin1Char('{')); number.remove(QLatin1Char('}'));
        const qreal anchorY = targetY; qreal closest = 72;
        for (const auto &match : p->search(QLatin1Char('(') + number + QLatin1Char(')'), {})) {
          const qreal distance = qAbs(match.bbox.center().y() - anchorY);
          if (distance < closest) { closest = distance; numberBox = match.bbox; }
        }
      }
      if (!target.block.equation) top = qMax(qreal(0), targetY - 2);
      // Each numbered row in align/gather can have its own anchor even when
      // several rows occupy one source line. Bound that row between its peers.
      const auto block = previewBlock(target, index);
      if (block.start != target.block.start) {
        // AMS can record every align row against its final source line. Its
        // rendered number supplies the exact row height without guessing text.
        if (!numberBox.isEmpty()) targetY = numberBox.center().y();
        // Unlabelled numbered rows also separate an align. Use actual glyph
        // boxes in the number column, including rows with no source label.
        if (!numberBox.isEmpty()) {
          QtPDF::Backend::Page::BoxBoundaryList chars;
          const QString text = p->selectedText({QPolygonF(QRectF(QPointF(), p->pageSizeF()))}, nullptr, &chars);
          static const QRegularExpression numbers(QStringLiteral(R"(\([^()\s]*[0-9][^()\s]*\))"));
          auto matches = numbers.globalMatch(text);
          while (matches.hasNext()) {
            const auto match = matches.next(); if (match.capturedEnd() > chars.size()) continue;
            QRectF box;
            for (int i = match.capturedStart(); i < match.capturedEnd(); ++i) if (!chars[i].isEmpty()) box = box.isEmpty() ? chars[i] : box.united(chars[i]);
            if (box.isEmpty() || box.height() > 30 || box.width() > 90 || box.right() < numberBox.left() || box.left() > numberBox.right()) continue;
            const qreal y = box.center().y(); if (qAbs(y - targetY) < 3) continue;
            if (y < targetY) top = qMax(top, (y + targetY) / 2);
            else bottom = bottom < 0 ? (y + targetY) / 2 : qMin(bottom, (y + targetY) / 2);
          }
          if (top < 0 && bottom >= 0) top = qMax(qreal(0), 2 * targetY - bottom);
          if (bottom < 0 && top >= 0) bottom = 2 * targetY - top;
        }
      }
      if (block.start != target.block.start && numberBox.isEmpty()) for (const auto &peer : index.definitions) {
        if (peer.anchor.isEmpty() || peer.anchor == target.anchor || peer.block.file != target.block.file || peer.block.start != target.block.start || peer.block.end != target.block.end) continue;
        const auto other = doc->resolveDestination(QtPDF::PDFDestination(peer.anchor));
        if (!other.isExplicit() || other.page() != targetPage || other.top() < 0) continue;
        const qreal y = p->pageSizeF().height() - other.top();
        if (y < targetY) top = qMax(top, (y + targetY) / 2);
        else if (y > targetY) bottom = bottom < 0 ? (y + targetY) / 2 : qMin(bottom, (y + targetY) / 2);
      }
    }
  }
  // Beamer repeats a labelled formula on overlay pages without repeating its
  // named PDF destination. Resolve the visible occurrence from its own SyncTeX.
  if(pageHint>=0&&target.anchor==target.key&&pageHint!=targetPage){
    auto visible=doc->page(pageHint).toStrongRef();QString number=target.number;number.remove(QLatin1Char('{'));number.remove(QLatin1Char('}'));
    if(!visible||number.isEmpty()){page=-1;return {};}
    const auto matches=visible->search(QLatin1Char('(')+number+QLatin1Char(')'),{});
    if(matches.size()!=1){page=-1;return {};}
    targetPage=pageHint;numberBox=matches.first().bbox;targetY=numberBox.center().y();top=bottom=-1;
  }
  QRectF rect = blockRect(sync, previewBlock(target, index), page, targetPage, targetY, top, bottom, &target.block);
  if(target.anchor==target.key&&target.block.equation&&!numberBox.isEmpty()){
    if(auto p=doc->page(targetPage).toStrongRef()){rect=sync.numberedDisplay(targetPage,numberBox,p->pageSizeF().width());if(!rect.isEmpty())page=targetPage;}
  }
  // Beamer may assign every source node to the closing frame line. In that
  // case use the actual PDF glyph band containing the anchored equation number,
  // never a whole-frame SyncTeX box (which also contains titles and prose).
  if(rect.isEmpty()&&target.anchor==target.key&&target.block.equation&&!numberBox.isEmpty()){
    if(auto visible=doc->page(targetPage).toStrongRef()){
      QtPDF::Backend::Page::BoxBoundaryList glyphs;
      visible->selectedText({QPolygonF(QRectF(QPointF(),visible->pageSizeF()))},nullptr,&glyphs);
      QList<QRectF> boxes;for(const auto &box:glyphs)if(!box.isEmpty())boxes.append(box);
      std::sort(boxes.begin(),boxes.end(),[](const QRectF &a,const QRectF &b){return a.top()<b.top();});
      QList<QRectF> bands;const qreal gap=qMax(qreal(2),numberBox.height()*0.4);
      for(const auto &box:boxes){if(bands.isEmpty()||box.top()>bands.last().bottom()+gap)bands.append(box);else bands.last()=bands.last().united(box);}
      for(const auto &band:bands)if(band.top()<=numberBox.center().y()&&band.bottom()>=numberBox.center().y()){
        bool ambiguous=false;const auto origin=doc->resolveDestination(QtPDF::PDFDestination(target.anchor));
        for(const auto &peer:index.definitions){if(!peer.block.equation||peer.number.isEmpty()||peer.key==target.key||(peer.block.file==target.block.file&&peer.block.start==target.block.start&&peer.block.end==target.block.end))continue;
          if(doc->resolveDestination(QtPDF::PDFDestination(peer.anchor)).page()!=origin.page())continue;
          QString n=peer.number;n.remove(QLatin1Char('{'));n.remove(QLatin1Char('}'));for(const auto &hit:visible->search(QLatin1Char('(')+n+QLatin1Char(')'),{}))if(band.contains(hit.bbox.center()))ambiguous=true;
        }
        if(!ambiguous){rect=band;page=targetPage;}break;
      }
    }
  }
  if (!rect.isEmpty() && page == targetPage) {
    if (!numberBox.isEmpty()) rect = rect.united(numberBox);
    if (target.block.equation) {
      // Some engines report a zero-width math box. The compiled PDF's glyphs
      // supply the horizontal extent and font descenders without raster guesses.
      auto p = doc->page(page).toStrongRef(); QtPDF::Backend::Page::BoxBoundaryList chars;
      if (p) {
        if (graphicMath(target.block)) { rect.setLeft(0); rect.setRight(p->pageSizeF().width()); return rect; }
        const QRectF band(0, rect.top(), p->pageSizeF().width(), rect.height());
        p->selectedText({QPolygonF(band)}, nullptr, &chars);
        QRectF glyphs;
        for (const auto &box : chars) if (!box.isEmpty() && box.center().y() >= band.top() && box.center().y() <= band.bottom()) glyphs = glyphs.isEmpty() ? box : glyphs.united(box);
        if (!glyphs.isEmpty()) { rect.setLeft(glyphs.left());rect.setRight(glyphs.right());rect=rect.united(glyphs); }
      }
    }
  }
  return rect;
}
// Beamer collapses source lines to the frame end. Use source order only when
// every display row has a unique matching TeX box in the same frame container.
// A mismatch stays quiet instead of borrowing a neighbouring paragraph/formula.
QList<QPair<ReferenceBlock,QRectF>> beamerFormulaRects(PreviewSync &sync,const ReferenceIndex &index,const SlideFrame &frame,int page,const QSharedPointer<QtPDF::Backend::Document> &doc){
  QList<ReferenceBlock> blocks;QList<int> counts;int total=0;
  for(const auto &block:index.blocks)if(block.equation&&block.file==frame.file&&block.start>=frame.start&&block.end<=frame.end){
    int rows=1;QString kind=block.kind;kind.remove(QLatin1Char('*'));
    if(QStringList{QStringLiteral("align"),QStringLiteral("alignat"),QStringLiteral("gather"),QStringLiteral("multline"),QStringLiteral("eqnarray")}.contains(kind)){
      const auto text=masked(block.text);int braces=0,nested=-1;
      if(text.contains(QStringLiteral("\\intertext"))||text.contains(QStringLiteral("\\shortintertext")))return {};
      for(int i=0;i<text.size();++i){
        if(text[i]==QLatin1Char('\\')){
          if(text.mid(i,6)==QStringLiteral("\\begin")||text.mid(i,4)==QStringLiteral("\\end")){bool begin=text.mid(i,6)==QStringLiteral("\\begin");int end=i+(begin?6:4);group(text,end);nested+=begin?1:-1;i=end-1;continue;}
          if(i+1<text.size()&&text[i+1]==QLatin1Char('\\')){if(braces==0&&nested==0)++rows;++i;continue;}
          if(i+1<text.size()&&QStringLiteral("{}%").contains(text[i+1])){++i;continue;}
        }
        if(text[i]==QLatin1Char('{'))++braces;else if(text[i]==QLatin1Char('}'))--braces;
      }
    }
    blocks.append(block);counts.append(rows);total+=rows;
  }
  if(blocks.isEmpty())return {};auto p=doc->page(page).toStrongRef();if(!p)return {};
  QList<QPair<ReferenceBlock,QRectF>> result;int matches=0;
  for(auto rows:sync.displayRows(page,frame.file,frame.endLine,p->pageSizeF().width())){
    if(rows.size()!=total)continue;std::sort(rows.begin(),rows.end(),[](QRectF a,QRectF b){return a.top()<b.top();});
    QList<QPair<ReferenceBlock,QRectF>> candidate;int offset=0;bool valid=true;
    for(int i=0;i<blocks.size();++i){QRectF rect;for(int n=0;n<counts[i];++n)rect=rect.isEmpty()?rows[offset++]:rect.united(rows[offset++]);
      QtPDF::Backend::Page::BoxBoundaryList chars;p->selectedText({QPolygonF(rect)},nullptr,&chars);QRectF ink;
      for(const auto &c:chars)if(!c.isEmpty())ink=ink.isEmpty()?c:ink.united(c);
      if(ink.isEmpty()){valid=false;break;}
      if(!graphicMath(blocks[i])){rect.setLeft(ink.left()-2);rect.setRight(ink.right()+2);}rect=rect.united(ink);
      candidate.append(qMakePair(blocks[i],rect));
    }
    if(valid){bool same=result.size()==candidate.size();for(int i=0;same&&i<candidate.size();++i)same=result[i].second==candidate[i].second;if(!same){++matches;result=candidate;}}
  }
  return matches==1?result:QList<QPair<ReferenceBlock,QRectF>>{};
}
void crop(Result &result, const Request &request, const QSharedPointer<QtPDF::Backend::Document> &doc, int page, QRectF rect) {
  if (!doc || page < 0 || page >= doc->numPages() || rect.isEmpty()) return;
  auto p = doc->page(page).toStrongRef(); if (!p) return;
  const qreal padding = result.target.block.equation && previewBlock(result.target, result.index).start != result.target.block.start ? 0 : 1;
  const QSizeF size = p->pageSizeF(); rect = rect.adjusted(-12, -padding, 12, padding).intersected(QRectF(QPointF(), size));
  if (rect.isEmpty()) return;
  // Keep the complete display; the vector widget fits its size, never crops its height.
  result.page = page;result.viewBox = rect;
  result.vector = vectorPage(request, page);
  if (!result.vector.isEmpty()) {
    result.viewBox = rect;
    qreal contentWidth = rect.width();
    if (result.target.block.equation && !graphicMath(result.target.block) && !result.target.number.isEmpty()) {
      QString number=result.target.number;number.remove(QLatin1Char('{'));number.remove(QLatin1Char('}'));
      const auto matches=p->search(QLatin1Char('(')+number+QLatin1Char(')'),{});
      QRectF label;int count=0;
      for(const auto &match:matches)if(rect.contains(match.bbox.center())){label=match.bbox;++count;}
      if(count==1){
        QtPDF::Backend::Page::BoxBoundaryList chars;p->selectedText({QPolygonF(rect)},nullptr,&chars);QRectF math;
        for(const auto &box:chars)if(!box.isEmpty()&&!label.adjusted(-1,-1,1,1).contains(box.center()))math=math.isEmpty()?box:math.united(box);
        // Only close an empty horizontal gap. Never rearrange aligned systems,
        // graphical mathematics, or a number that overlaps the formula.
        if(!math.isEmpty()&&label.left()-math.right()>18){
          result.formulaBox=QRectF(math.left()-3,rect.top(),math.width()+6,rect.height());
          result.numberBox=QRectF(label.left()-2,rect.top(),label.width()+4,rect.height());
          contentWidth=result.formulaBox.width()+14+result.numberBox.width();
        }
      }
    }
    const qreal scale=qMin(1.6,580./contentWidth);
    result.visualSize=QSizeF(contentWidth*scale,rect.height()*scale);return;
  }
  // Crop directly: never render/cache another complete PDF page for a tooltip.
  const double scale = qBound(1.2, 580. * request.density / rect.width(), 6.);
  result.image = p->renderToImage(72 * scale, 72 * scale, QRect(int(rect.x() * scale), int(rect.y() * scale), qMax(1, int(rect.width() * scale)), qMax(1, int(rect.height() * scale))), false);
}
Result work(const Request &request) {
  Result result; result.key = request.key; result.density = request.density;
  if(request.pinned&&request.key.isEmpty()&&!request.contextRange.isEmpty()){
    bool ok=request.buffers.contains(request.file);const auto text=ok?request.buffers.value(request.file):read(request.file,&ok);int a=0,b=0;
    if(!ok||!locateTextAnchor(text,request.contextRange,&a,&b)){result.message=words("固定查阅范围已变化，请重新选择。","The pinned range changed. Select it again.");return result;}
    auto mapped=request;mapped.contextRange.clear();mapped.offset=a;mapped.buffers.insert(request.file,text);
    if(request.restorePinnedSelection){mapped.selectionStart=a;mapped.selectionEnd=b;}
    return work(mapped);
  }
  if(request.related){
    if(request.cancelled&&request.cancelled())return result;
    result.citation=request.citation;
    result.index=readReferenceIndex(request.root,request.buffers,true,request.cancelled,&result.unavailableSources);
    result.project=buildProjectIndex(request.root,request.buffers,true,request.cancelled);
    if(!result.project||result.project->cancelled)return result;
    // Keep missing literal dependencies observable without treating a stable
    // partial project as an inconsistent snapshot.
    for(const auto &file:result.project->dependencies){
      const auto suffix=QFileInfo(file).suffix().toLower();
      if((suffix==QStringLiteral("tex")||suffix==QStringLiteral("bib"))&&!result.index.sources.contains(file)&&!request.buffers.contains(file)&&!QFileInfo(file).exists()){result.index.sources.insert(file,{});result.unavailableSources.insert(file);}
    }
    qsizetype bytes=0;
    for(const auto &file:result.project->bibliographies){
      if(request.cancelled&&request.cancelled())return result;
      bool ok=request.buffers.contains(file);const auto text=ok?request.buffers.value(file):read(file,&ok);
      bytes+=text.size()*2;if(bytes>8*1024*1024){result.index.truncated=true;break;}
      result.index.sources.insert(file,text);
      if(!ok){result.unavailableSources.insert(file);continue;}result.unavailableSources.remove(file);
      const auto parsed=parseBibliography(text);
      if(!parsed.error.isEmpty())result.bibliographyErrors.insert(file,parsed.error);
      for(const auto &record:parsed.records){result.bibliography.append({file,record});
        if(result.key.isEmpty()&&file==request.file&&request.offset>=record.start&&request.offset<=record.end){result.key=record.key;result.citation=true;}
      }
    }
    for(auto it=result.index.sources.cbegin();it!=result.index.sources.cend();++it){
      const bool available=!result.unavailableSources.contains(it.key());
      if(available)result.sourceHashes.insert(it.key(),QString::fromLatin1(QCryptographicHash::hash(it.value().toUtf8(),QCryptographicHash::Sha256).toHex()));
      const QFileInfo file(it.key());result.sourceStamps.insert(it.key(),{file.size(),file.lastModified().toMSecsSinceEpoch()});
      if(!request.buffers.contains(it.key())){bool ok=false;const auto current=read(it.key(),&ok);if(ok!=available||(ok&&current!=it.value()))result.sourcesCurrent=false;}
    }
    if(result.key.isEmpty()&&request.contextRange.isEmpty()){
      for(const auto &use:result.project->uses)if(use.file==request.file&&request.offset>=use.offset){
        const auto text=result.index.sources.value(use.file);const int close=int(text.indexOf(QLatin1Char('}'),use.offset));
        if(close>=0&&request.offset<=close){result.key=use.key;result.citation=use.kind==QStringLiteral("citation-use");break;}
      }
    }
    int contextOffset=request.offset;
    if(result.key.isEmpty()&&!request.contextRange.isEmpty()){int a=0,b=0;if(locateTextAnchor(result.index.sources.value(request.file),request.contextRange,&a,&b))contextOffset=a;else {result.message=words("原位置已改变，请重新选择查阅对象。","The location changed. Select the object again.");contextOffset=-1;}}
    if(result.key.isEmpty()){
      int span=INT_MAX;
      for(const auto &block:result.index.blocks)if(block.file==request.file&&contextOffset>=block.start&&contextOffset<block.end&&block.end-block.start<span){result.target.block=block;result.found=true;span=block.end-block.start;}
      // An unlabelled proof remains a useful object in the same reading chain.
      for(const auto &d:result.index.definitions)if(result.found&&d.block.file==result.target.block.file&&d.block.start==result.target.block.start){result.key=d.key;break;}
    }
    return result;
  }
  const bool integrity=!request.pdf.isEmpty()&&pdfCompiledPairValid(request.pdf);
  const auto compiled=integrity?pdfCompiledSources(request.pdf):QHash<QString,QString>();
  const bool compiledSource=compiled.contains(path(request.root));
  const bool current=integrity&&validSourcePair(request);
  result.previous=integrity&&!current;
  auto compiledIndex=compiledSource?referenceIndex(request.root,compiled):ReferenceIndex();
  const auto auxiliary=pdfCompiledAux(request.pdf);
  if(compiledSource&&(!auxiliary.isEmpty()||current))readNumbers(compiledIndex,auxFile(request.root,request.pdf),auxiliary);
  const bool pdfQuery=request.body||!request.anchor.isEmpty()||request.destination.isValid();
  result.index=pdfQuery&&compiledSource?compiledIndex:referenceIndex(request.root,request.buffers);
  if(!pdfQuery&&current)readNumbers(result.index,auxFile(request.root,request.pdf),auxiliary);
  if(request.pinned&&request.selectionStart>=0&&request.selectionEnd>request.selectionStart){
    const auto text=request.buffers.value(request.file,read(request.file));const auto a=request.selectionStart,b=request.selectionEnd;if(b<=text.size()){result.target.block={request.file,QStringLiteral("selection"),text.mid(a,b-a),QString(),a,b,text.left(a).count(QLatin1Char('\n'))+1,text.left(b).count(QLatin1Char('\n'))+1,false};result.found=true;result.title=QStringLiteral("Selection");return result;}
  }
  if(request.pinned&&result.key.isEmpty()) {
    int span=INT_MAX;
    for(const auto &d:result.index.definitions)if(d.block.file==request.file&&request.offset>=d.block.start&&request.offset<d.block.end&&d.block.end-d.block.start<span){result.key=d.key;span=d.block.end-d.block.start;}
    if(result.key.isEmpty()) {
      for(const auto &b:result.index.blocks)if(b.file==request.file&&request.offset>=b.start&&request.offset<b.end&&b.end-b.start<span){result.target.block=b;result.title=b.kind;result.found=true;span=b.end-b.start;}
      return result;
    }
  }
  if (request.related) {
    if (result.key.isEmpty()) {
      int span = INT_MAX;
      for (const auto &d : result.index.definitions) if (d.block.file == request.file && request.offset >= d.block.start && request.offset <= d.block.end && d.block.end - d.block.start < span) { result.key = d.key; span = d.block.end - d.block.start; }
    }
    return result;
  }
  auto doc = request.pdf.isEmpty() ? QSharedPointer<QtPDF::Backend::Document>() : QtPDF::Backend::Document::newDocument(request.pdf);
  PreviewSync sync(request.pdf, request.root);
  const bool paired = doc && sync.isValid() && integrity && compiledSource;
  if (request.body && paired) {
    // Inverse SyncTeX can coalesce adjacent environments. Test compiled formula
    // regions on this page first, then use source lines for unlabelled displays.
    for (const auto &d : result.index.definitions) if (d.block.equation && !d.anchor.isEmpty()) {
      const auto destination = doc->resolveDestination(QtPDF::PDFDestination(d.anchor));
      if (!destination.isExplicit() || (destination.page() != request.page && d.anchor != d.key)) continue;
      int page; const QRectF rect = referenceRect(sync, result.index, d, doc, page, request.page);
      if (page != request.page || !rect.adjusted(-2, -2, 2, 2).contains(request.point)) continue;
      result.key = d.key; result.target = d; result.found = true; crop(result, request, doc, page, rect); return result;
    }
    auto hit = sync.fromPDF(request.page, request.point);
    const auto deck=parseSlideDeck(request.root,compiled);
    if(deck.beamer&&!deck.uncertain)for(const auto &frame:deck.frames){
      if(frame.file!=path(hit.filename))continue;
      ReferenceBlock bounds;bounds.file=frame.file;bounds.firstLine=bounds.lastLine=frame.endLine;
      if(sync.fromTeX(bounds,frame.endLine,request.page).isEmpty())continue;
      for(const auto &entry:beamerFormulaRects(sync,result.index,frame,request.page,doc))if(entry.second.adjusted(-2,-2,2,2).contains(request.point)){
        result.target.block=entry.first;result.found=true;result.title=QStringLiteral("公式");crop(result,request,doc,request.page,entry.second);return result;
      }
      return result;
    }
    for (const auto &block : result.index.blocks) if (block.equation && path(hit.filename) == block.file && hit.line >= block.firstLine - 2 && hit.line <= block.lastLine + 2) {
      bool labelled = false;
      for (const auto &d : result.index.definitions) if (d.block.file == block.file && d.block.start == block.start && d.block.end == block.end) {
        labelled = true; int page; const QRectF rect = referenceRect(sync, result.index, d, doc, page, request.page);
        if (page != request.page || !rect.adjusted(-2, -2, 2, 2).contains(request.point)) continue;
        result.key = d.key; result.target = d; result.found = true; crop(result, request, doc, page, rect); return result;
      }
      if (labelled) continue; // A header can inherit a formula's source line.
      int page; const QRectF rect = blockRect(sync, block, page);
      if (page != request.page || !rect.adjusted(-2, -2, 2, 2).contains(request.point)) continue;
      result.target.block = block; result.found = true; result.title = QStringLiteral("公式"); crop(result, request, doc, page, rect); return result;
    }
    return result;
  }
  if (request.body) return result; // Ordinary prose must remain quiet.
  if (result.key.isEmpty() && !request.anchor.isEmpty()) for (const auto &d : result.index.definitions) if (d.anchor == request.anchor) { result.key = d.key; break; }
  if (result.key.isEmpty() && request.destination.isValid() && paired) {
    // Poppler often resolves named links before exposing them. Recover the label
    // from its .aux anchor, comparing actual PDF coordinates rather than guessing
    // from the displayed equation number or moving the viewer.
    const auto destination = doc->resolveDestination(request.destination);
    auto page = doc->page(destination.page()).toStrongRef();
    if (page && destination.top() >= 0) {
      for (const auto &d : result.index.definitions) {
        if (d.anchor.isEmpty() || d.block.kind.isEmpty()) continue;
        const auto anchor = doc->resolveDestination(QtPDF::PDFDestination(d.anchor));
        if (anchor.page() == destination.page() && qAbs(anchor.top() - destination.top()) < 2 && qAbs(anchor.left() - destination.left()) < 2) { result.key = d.key; break; }
      }
    }
    if (result.key.isEmpty()) return result;
  }
  int count = 0; for (const auto &d : result.index.definitions) if (d.key == result.key) { result.target = d; ++count; }
  if (count > 1) { result.message = QStringLiteral("此标签有多个定义，请先消除重复。"); return result; }
  if (count == 1) {
    result.found = true; result.title = result.target.number.isEmpty() ? result.key : result.target.number + QStringLiteral(" · ") + result.key;
    if (paired) {
      ReferenceDefinition target;int matches=0;for(const auto &d:compiledIndex.definitions)if(d.key==result.key){target=d;++matches;}
      if(matches==1){int page;const QRectF rect=request.pinned&&!target.block.equation?sync.strictStatement(target.block,compiledIndex,doc,page):referenceRect(sync,compiledIndex,target,doc,page);crop(result,request,doc,page,rect);}
    }
    if (!result.visual()) result.message = QStringLiteral("当前源码尚无匹配的已编译预览。");
    return result;
  }
  // PDF links can preview an existing destination even without local source.
  if (doc && request.destination.isValid() && !request.anchor.isEmpty()) {
    const auto dest = doc->resolveDestination(request.destination); auto p = doc->page(dest.page()).toStrongRef();
    if (p && dest.isExplicit() && dest.top() >= 0) {
      const QSizeF size = p->pageSizeF(); const qreal y = size.height() - dest.top();
      crop(result, request, doc, dest.page(), QRectF(36, qMax(0., y - 10), size.width() - 72, 100));
      result.title = request.anchor; result.found = result.visual(); return result;
    }
  }
  if (!request.key.isEmpty()) result.message = QStringLiteral("没有找到此标签的定义。");
  return result;
}

class VectorPreview final : public QWidget {
  QSvgRenderer renderer;
  QRectF formulaBox, numberBox;
public:
  void fitWidth(int available) {auto size=property("researchOriginalVisualSize").toSize();if(size.width()>available)size.scale(QSize(qMax(1,available),10000),Qt::KeepAspectRatio);setFixedSize(size);}
  explicit VectorPreview(const Result &result, QWidget *parent, QSize limit = QSize(580,10000)) : QWidget(parent), renderer(result.vector), formulaBox(result.formulaBox), numberBox(result.numberBox) {
    setProperty("researchOriginalVisualSize",result.visualSize.toSize());
    renderer.setViewBox(result.viewBox); auto size=result.visualSize.toSize();if(size.width()>limit.width()||size.height()>limit.height())size.scale(limit,Qt::KeepAspectRatio);setFixedSize(size);
    setObjectName(QStringLiteral("researchReferenceImage")); setProperty("researchVectorPreview", true);
    setProperty("researchVectorBytes", result.vector.size());
  }
protected:
  void paintEvent(QPaintEvent *) override { QPainter painter(this); painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing); if(formulaBox.isEmpty()){renderer.render(&painter,QRectF(rect()));return;}
    const qreal scale=width()/(formulaBox.width()+14+numberBox.width());
    auto draw=[&](const QRectF &box,qreal x){painter.save();const QRectF destination(x,0,box.width()*scale,height());painter.setClipRect(destination);renderer.setViewBox(box);renderer.render(&painter,destination);painter.restore();};
    draw(formulaBox,0);draw(numberBox,(formulaBox.width()+14)*scale); }
};
class References final : public QObject {
  QPointer<TeXDocumentWindow> source;
  QPointer<PDFDocumentWindow> preview;
  QPointer<QtPDF::PDFDocumentView> slidePreview;
  QPointer<QFrame> card;
  QPointer<MaterialDialog> related;
  QPointer<QTextBrowser> reading;
  QPointer<QLineEdit> filter;
  QTimer timer;
  QString identity; QPoint position; Request pending;
  quint64 generation{0}, readingGeneration{0}; bool busy{false};
  std::shared_ptr<std::atomic<quint64>> readingEpoch=std::make_shared<std::atomic<quint64>>(0);
  QHash<QString,QVariantMap> readingLinks;QVariantMap requestedContextRange;QString requestedContextFile;
  Result relatedResult;bool readingBusy{false};QVariantMap pendingReadingState;int pendingPinnedScroll{-1};quint64 readingRestoreSerial{0};
  QHash<QString,QString> readingBuffers;bool readingDirty{false};QTimer readingRefresh;int readingRetries{0};
  QSet<QObject *> observedEditors;
  QPointer<QDockWidget> pinnedDock;
  QPointer<QScrollArea> pinnedArea;
  QPointer<QLabel> pinnedTitle;
  QTimer pinTimer;
  Result pinnedResult;
  QString pinnedKey;
  quint64 pinGeneration{0};
  int pinnedOffset{-1};QString pinnedFile;
  QTextCursor pinnedAnchor;
  bool pinnedRange{false},pinnedSelection{false};QVariantMap pinnedSavedRange;
public:
  bool readingCurrent()const{
    if(readingDirty||readingBuffers!=buffers())return false;
    for(auto it=relatedResult.sourceStamps.cbegin();it!=relatedResult.sourceStamps.cend();++it)if(!readingBuffers.contains(it.key())){const QFileInfo file(it.key());if(it.value()!=qMakePair(file.size(),file.lastModified().toMSecsSinceEpoch()))return false;}
    return true;
  }
  bool verifyClosedReading(const QVariantMap &state){
    QHash<QString,QString> hashes;
    for(auto it=relatedResult.sourceHashes.cbegin();it!=relatedResult.sourceHashes.cend();++it)if(!readingBuffers.contains(it.key()))hashes.insert(it.key(),it.value());
    for(const auto &file:relatedResult.unavailableSources)if(!readingBuffers.contains(file))hashes.insert(file,{});
    if(hashes.isEmpty())return false;
    // Metadata alone misses replacements that preserve size and modification
    // time. Check only closed sources on a worker, reusing the parsed result
    // when their content is unchanged.
    readingRefresh.stop();readingRetries=0;readingBusy=true;pendingReadingState=state;
    requestedContextFile=state.value(QStringLiteral("contextFile")).toString();requestedContextRange=state.value(QStringLiteral("contextRange")).toMap();
    if(reading)reading->setPlainText(words("正在读取上下文…","Reading context…"));
    related->show();related->raise();
    const auto serial=++readingGeneration;const auto epoch=readingEpoch;const auto ticket=++*epoch;const QPointer<References> self(this);
    QThreadPool::globalInstance()->start(QRunnable::create([self,serial,epoch,ticket,hashes]{
      bool unchanged=true;
      for(auto it=hashes.cbegin();it!=hashes.cend();++it){
        if(epoch->load()!=ticket)return;
        bool ok=false;const auto text=read(it.key(),&ok);
        if(it.value().isEmpty()?ok:(!ok||QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(),QCryptographicHash::Sha256).toHex())!=it.value())){unchanged=false;break;}
      }
      if(epoch->load()!=ticket||!qApp)return;
      QMetaObject::invokeMethod(qApp,[self,serial,unchanged]{
        if(!self||self->readingGeneration!=serial||!self->related)return;
        if(!unchanged||!self->readingCurrent()){self->readingDirty=true;if(self->related->isVisible())self->refreshReading();else self->readingBusy=false;return;}
        const auto state=self->pendingReadingState;self->pendingReadingState.clear();self->readingBusy=false;
        self->restoreReadingState(state,false);
      },Qt::QueuedConnection);
    }));
    return true;
  }
  void refreshReading(){
    if(!related||!related->isVisible())return;
    const auto state=readingState();Request request=context();request.key=state.value(QStringLiteral("key")).toString();request.citation=state.value(QStringLiteral("citation")).toBool();
    const auto file=state.value(QStringLiteral("contextFile")).toString();if(!file.isEmpty())request.file=file;
    request.contextRange=state.value(QStringLiteral("contextRange")).toMap();request.offset=source?source->cursorPosition():-1;
    loadReading(request,true);pendingReadingState=state;
  }
  void invalidateReading(bool sourceChanged=true){
    if(!related)return;const auto state=readingState();readingDirty=true;++readingGeneration;++*readingEpoch;readingBusy=false;pendingReadingState=state;
    if(sourceChanged)readingRetries=0;
    if(!related->isVisible())return;
    // Coalesce actual edits, but retry a failed snapshot only once. A file
    // that keeps changing must not create a self-sustaining background loop.
    if(sourceChanged||readingRetries++<1){readingRefresh.start();return;}
    readingRefresh.stop();related->setProperty("researchReadingRefreshStopped",true);
    if(reading)reading->setHtml(readingStyle()+QStringLiteral("<p>%1</p><p><a href='retry:'>%2</a></p>")
      .arg(words("文稿已变化，未能更新查阅。","The document changed and the context could not be refreshed.").toHtmlEscaped(),words("重新读取","Reload").toHtmlEscaped()));
  }
  QVariantMap readingState()const {
    auto state=QVariantMap{{QStringLiteral("dialog"),related&&related->isVisible()},{QStringLiteral("key"),relatedResult.key},{QStringLiteral("query"),filter?filter->text():QString()},{QStringLiteral("scroll"),reading?reading->verticalScrollBar()->value():0},
      {QStringLiteral("pinned"),pinnedDock&&!pinnedDock->isHidden()},{QStringLiteral("pinRestore"),pinnedDock&&pinnedDock->property("researchFocusRestore").toBool()},{QStringLiteral("pinKey"),pinnedKey},{QStringLiteral("pinFile"),pinnedFile},{QStringLiteral("pinOffset"),pinnedOffset},{QStringLiteral("pinAnchor"),QVariant::fromValue(pinnedAnchor)},{QStringLiteral("pinRange"),pinnedRange},{QStringLiteral("pinSelection"),pinnedSelection},{QStringLiteral("pinScroll"),pinnedArea?pinnedArea->verticalScrollBar()->value():0}};
    if(!pinnedAnchor.isNull())state.insert(QStringLiteral("pinTextRange"),textAnchor(pinnedAnchor.document()->toPlainText(),pinnedAnchor.selectionStart(),pinnedAnchor.selectionEnd()));
    else if(!pinnedSavedRange.isEmpty())state.insert(QStringLiteral("pinTextRange"),pinnedSavedRange);
    state.insert(QStringLiteral("citation"),relatedResult.citation);
    // A first load has not identified the cursor's object yet. Preserve that
    // distinction when an edit cancels it, rather than restoring an empty
    // target over the replacement worker's newly resolved theorem/citation.
    if(readingBusy&&relatedResult.key.isEmpty()&&requestedContextRange.isEmpty()&&pendingReadingState.isEmpty())state.insert(QStringLiteral("resolveObject"),true);
    if((readingBusy||readingDirty)&&!requestedContextRange.isEmpty()){state.insert(QStringLiteral("contextFile"),requestedContextFile);state.insert(QStringLiteral("contextRange"),requestedContextRange);}
    else if(relatedResult.found){const auto &b=relatedResult.target.block;state.insert(QStringLiteral("contextFile"),b.file);state.insert(QStringLiteral("contextRange"),textAnchor(relatedResult.index.sources.value(b.file),b.start,b.end,relatedResult.sourceHashes.value(b.file)));}
    if((readingBusy||readingDirty)&&!pendingReadingState.isEmpty())for(const auto &key:{QStringLiteral("key"),QStringLiteral("citation"),QStringLiteral("query"),QStringLiteral("scroll"),QStringLiteral("contextFile"),QStringLiteral("contextRange"),QStringLiteral("restoreScroll"),QStringLiteral("resolveObject")})if(pendingReadingState.contains(key))state.insert(key,pendingReadingState.value(key));
    return state;
  }
  void restoreReadingState(const QVariantMap &state,bool activate=true,bool passive=false) {
    if(state.isEmpty()||!activate&&(!related||!related->isVisible()))return;
    if(state.value(QStringLiteral("dialog")).toBool()) {
      if(!related){openReading(state.value(QStringLiteral("key")).toString());pendingReadingState=state;}
      else if(activate&&!readingBusy&&!readingCurrent()){
        Request request=context();request.key=state.value(QStringLiteral("key")).toString();request.citation=state.value(QStringLiteral("citation")).toBool();const auto file=state.value(QStringLiteral("contextFile")).toString();if(!file.isEmpty())request.file=file;request.contextRange=state.value(QStringLiteral("contextRange")).toMap();request.offset=source->cursorPosition();
        related->show();related->raise();loadReading(request);pendingReadingState=state;
      }
      else if(activate&&!readingBusy&&verifyClosedReading(state)){}
      else if(readingBusy){pendingReadingState=state;relatedResult.key=state.value(QStringLiteral("key")).toString();relatedResult.citation=state.value(QStringLiteral("citation")).toBool();requestedContextFile=state.value(QStringLiteral("contextFile")).toString();requestedContextRange=state.value(QStringLiteral("contextRange")).toMap();if(activate){related->show();related->raise();}}
      else {relatedResult.key=state.value(QStringLiteral("key")).toString();relatedResult.citation=state.value(QStringLiteral("citation")).toBool();relatedResult.found=false;
        const auto file=state.value(QStringLiteral("contextFile")).toString();int a=0,b=0;
        if(locateTextAnchor(relatedResult.index.sources.value(file),state.value(QStringLiteral("contextRange")).toMap(),&a,&b))for(const auto &block:relatedResult.index.blocks)if(block.file==file&&block.start==a&&block.end==b){relatedResult.target.block=block;relatedResult.found=true;break;}
        if(filter){QSignalBlocker block(filter);filter->setText(state.value(QStringLiteral("query")).toString());}showReading();if(activate){related->show();related->raise();}if(reading&&state.value(QStringLiteral("restoreScroll"),true).toBool()){const int scroll=state.value(QStringLiteral("scroll")).toInt();reading->verticalScrollBar()->setValue(scroll);const auto ticket=++readingRestoreSerial;QTimer::singleShot(0,this,[this,ticket,scroll]{if(ticket==readingRestoreSerial&&reading&&related&&related->isVisible())reading->verticalScrollBar()->setValue(scroll);});}}
    }else if(related)related->hide();
    const bool wantsPin=state.value(QStringLiteral("pinned")).toBool()||state.value(QStringLiteral("pinRestore")).toBool();
    QTextCursor restoredAnchor=state.value(QStringLiteral("pinAnchor")).value<QTextCursor>();
    const auto restoredFile=state.value(QStringLiteral("pinFile")).toString();
    if(wantsPin&&restoredAnchor.isNull()&&!state.value(QStringLiteral("pinTextRange")).toMap().isEmpty()){
      auto *target=TeXDocumentWindow::findDocument(restoredFile);
      if(!target&&!passive&&QFileInfo(restoredFile).isFile())target=TeXDocumentWindow::openDocument(restoredFile,false,false);
      int a=0,b=0;if(target&&locateTextAnchor(target->textDoc()->editorText(),state.value(QStringLiteral("pinTextRange")).toMap(),&a,&b)){
        restoredAnchor=QTextCursor(target->textDoc());restoredAnchor.setPosition(a);restoredAnchor.setPosition(b,QTextCursor::KeepAnchor);
      }else if(!passive&&state.value(QStringLiteral("pinKey")).toString().isEmpty()){
        if(pinnedDock)setPanelVisible(pinnedDock,false);showTransientStatusMessage(source,englishUi()?QStringLiteral("The pinned range changed. Select it again."):Research::ui(QStringLiteral("固定查阅范围已变化，请重新选择。")));return;
      }
    }
    if(wantsPin&&!pinnedDock)pin(state.value(QStringLiteral("pinKey")).toString(),restoredFile,restoredAnchor.isNull()?state.value(QStringLiteral("pinOffset")).toInt():restoredAnchor.selectionStart(),!passive);
    pinnedSavedRange=passive&&restoredAnchor.isNull()?state.value(QStringLiteral("pinTextRange")).toMap():QVariantMap();
    if(pinnedDock){pinnedKey=state.value(QStringLiteral("pinKey")).toString();pinnedFile=state.value(QStringLiteral("pinFile")).toString();pinnedOffset=state.value(QStringLiteral("pinOffset"),-1).toInt();pinnedAnchor=restoredAnchor;pinnedRange=state.value(QStringLiteral("pinRange")).toBool();pinnedSelection=state.value(QStringLiteral("pinSelection")).toBool();pinnedDock->setProperty("researchFocusRestore",state.value(QStringLiteral("pinRestore")).toBool());setPanelVisible(pinnedDock,state.value(QStringLiteral("pinned")).toBool());pendingPinnedScroll=(pinnedDock->isVisible()||state.value(QStringLiteral("pinRestore")).toBool())?state.value(QStringLiteral("pinScroll")).toInt():-1;if(pinnedDock->isVisible())invalidatePinned();}
  }
  ~References() override { ++*readingEpoch; cancel(); }
  explicit References(TeXDocumentWindow *w) : QObject(w), source(w) { initialize(w->editor()); }
  explicit References(PDFDocumentWindow *w) : QObject(w), preview(w) {
    initialize(w->widget()); connect(w, &PDFDocumentWindow::reloaded, this, [this]() { cancel(); });
    connect(w->widget(), &QtPDF::PDFDocumentView::changedZoom, this, [this]() { cancel(); });
    connect(w->widget(), &QtPDF::PDFDocumentView::changedDocument, this, [this]() { cancel(); });
  }
  References(TeXDocumentWindow *w, QtPDF::PDFDocumentView *view) : QObject(view), source(w), slidePreview(view) {
    initialize(view);
    setObjectName(QStringLiteral("researchSlideReferences"));
    connect(view, &QtPDF::PDFDocumentView::changedZoom, this, [this]() { cancel(); });
    connect(view, &QtPDF::PDFDocumentView::changedDocument, this, [this]() { cancel(); });
    connect(view, &QtPDF::PDFDocumentView::changedPage, this, [this]() { cancel(); });
    connect(w->editor(), &QTextEdit::cursorPositionChanged, this, [this]() { cancel(); });
    w->installEventFilter(this);
  }
  QAbstractScrollArea *area() const {
    if(slidePreview)return slidePreview;
    return source?static_cast<QAbstractScrollArea *>(source->editor()):static_cast<QAbstractScrollArea *>(preview->widget());
  }
  void initialize(QAbstractScrollArea *area) {
    readingRefresh.setSingleShot(true);readingRefresh.setInterval(150);connect(&readingRefresh,&QTimer::timeout,this,[this]{refreshReading();});
    setObjectName(QStringLiteral("researchReferences")); area->viewport()->setMouseTracking(true); area->viewport()->installEventFilter(this);
    area->installEventFilter(this); parent()->installEventFilter(this);
    timer.setSingleShot(true); timer.setInterval(ReferenceHoverDelay); timer.setTimerType(Qt::PreciseTimer);
    connect(&timer, &QTimer::timeout, this, [this]() { startHover(); });
    pinTimer.setSingleShot(true);pinTimer.setInterval(500);connect(&pinTimer,&QTimer::timeout,this,[this]{refreshPinned();});
    for (auto *bar : {area->horizontalScrollBar(), area->verticalScrollBar()}) connect(bar, &QScrollBar::valueChanged, this, [this]() { cancel(); });
    if (source) { connect(source->editor(), &QTextEdit::textChanged, this, [this]() { cancel(); }); connect(source->editor(), &QTextEdit::cursorPositionChanged, this, [this]() { cancel(); }); }
    connect(TWApp::instance(), &TWApp::updatedTranslators, this, [this]() {
      cancel();
      if (related) { related->setWindowTitle((englishUi()?QStringLiteral("References"):Research::ui(QStringLiteral("查阅")))); if (filter) filter->setPlaceholderText(ui(QStringLiteral("查找标签…"))); showReading(); }
      if(pinnedDock){
        if(auto *go=pinnedDock->findChild<QToolButton*>(QStringLiteral("researchPinnedGoToSource"))){go->setText(englishUi()?QStringLiteral("Go to source"):Research::ui(QStringLiteral("定位源码")));configureButton(go,ButtonRole::Text);}
        if(auto *close=pinnedDock->findChild<QToolButton*>(QStringLiteral("researchPinnedClose")))close->setAccessibleName(englishUi()?QStringLiteral("Close reference"):Research::ui(QStringLiteral("收起查阅")));
        refreshPinned();
      }
    });
  }
  void cancel() { timer.stop(); ++generation; identity.clear(); if (card) delete card.data(); }
  Request context() {
    for (auto *window : TeXDocumentWindow::documentList()) {
      QObject *edit = window->editor();
      if (observedEditors.contains(edit)) continue;
      observedEditors.insert(edit);
      connect(window->editor(), &QTextEdit::textChanged, this, [this]() { cancel();invalidatePinned();invalidateReading(); });
      connect(edit, &QObject::destroyed, this, [this, edit]() { observedEditors.remove(edit); cancel(); invalidatePinned();invalidateReading(); });
    }
    Request request; request.buffers = buffers();
    if (source && !source->untitled()) {
      request.file = path(source->fileName()); request.root = source->textDoc()->getRootFilePath(); request.pdf = previewFile(request.root);
    } else if (preview) {
      request.pdf = preview->fileName(); request.root = pdfSourceRoot(request.pdf);
      if (request.root.isEmpty()) { const QString candidate = request.pdf.left(request.pdf.size() - 4) + QStringLiteral(".tex"); if (QFileInfo(candidate).isFile()) request.root = candidate; }
    }
    const QFileInfo info(request.pdf); request.pdfSize = info.size(); request.pdfTime = info.lastModified().toMSecsSinceEpoch();
    request.density = source ? source->devicePixelRatioF() : preview->devicePixelRatioF();
    request.vectorProgram = TWApp::instance()->findProgram(QStringLiteral("pdftocairo"), Engine::binPaths());
    request.temporaryFolder = QCoreApplication::applicationDirPath() + QStringLiteral("/settings/research-preview");
    return request;
  }
  bool eventFilter(QObject *watched, QEvent *event) override {
    if((reading&&(watched==reading||watched==reading->viewport()))||(filter&&watched==filter)){if(event->type()==QEvent::MouseButtonPress||event->type()==QEvent::Wheel||event->type()==QEvent::KeyPress||event->type()==QEvent::InputMethod){++readingRestoreSerial;if(!pendingReadingState.isEmpty())pendingReadingState.insert(QStringLiteral("restoreScroll"),false);}return false;}
    if(pinnedArea&&watched==pinnedArea->viewport()&&(event->type()==QEvent::MouseButtonPress||event->type()==QEvent::Wheel||event->type()==QEvent::KeyPress))pendingPinnedScroll=-1;
    // Review copies have a different typeset layout; the original PDF's hover
    // geometry must never be reused on that layout.
    if(preview&&preview->property("researchAnnotationReview").toBool()){cancel();return false;}
    if(source&&watched==source->editor()&&event->type()==QEvent::FontChange&&pinnedArea)
      if(auto *text=pinnedArea->findChild<QTextBrowser*>(QStringLiteral("researchPinnedSource")))sourceReferenceFont(text);
    if(source&&watched==source&&event->type()==QEvent::DynamicPropertyChange&&static_cast<QDynamicPropertyChangeEvent*>(event)->propertyName()=="researchBuildFinishing"&&!source->property("researchBuildFinishing").toBool())invalidatePinned();
    if(pinnedDock&&event->type()==QEvent::Resize){
      if(watched==source)pinnedDock->setMaximumHeight(qMax(110,source->height()/3));
      if(pinnedArea&&watched==pinnedArea->viewport())if(auto *image=dynamic_cast<VectorPreview*>(pinnedArea->findChild<QWidget*>(QStringLiteral("researchReferenceImage"))))image->fitWidth(pinnedArea->viewport()->width()-16);
    }
    if (event->type() == QEvent::MouseMove) {
      auto *mouse = static_cast<QMouseEvent *>(event);
      if (mouse->buttons() != Qt::NoButton) { cancel(); return false; }
      auto *area = this->area();
      if (watched != area->viewport()) return false;
      const QPoint pos = mouse->position().toPoint(); QString next; Request request;
      if (source && !slidePreview) {
        const auto cursor = source->editor()->cursorForPosition(pos); const auto block = cursor.block(); const auto token = referenceAt(block.text(), cursor.positionInBlock());
        if (token.start >= 0 && source->editor()->cursorRect(cursor).adjusted(-3, -2, 12, 2).contains(pos)) { request.key = token.key; request.offset = block.position() + token.start; next = token.key + QLatin1Char(':') + QString::number(request.offset); }
      } else {
        auto *view = slidePreview ? slidePreview.data() : preview->widget(); const auto items = view->items(pos);
        for (auto *item : items) if (auto *link = qgraphicsitem_cast<QtPDF::PDFLinkGraphicsItem *>(item)) {
          const auto *action = link->previewAction(); if (!action || action->type() != QtPDF::PDFAction::ActionTypeGoTo) continue;
          const auto *go = static_cast<const QtPDF::PDFGotoAction *>(action); if (go->isRemote()) continue;
          link->setToolTip(QString());
          request.destination = go->destination(); request.anchor = request.destination.destinationName();
          if (!request.destination.isValid()) continue;
          next = QStringLiteral("link:%1:%2:%3:%4").arg(request.anchor).arg(request.destination.page()).arg(request.destination.left()).arg(request.destination.top()); break;
        }
        if (next.isEmpty()) for (auto *item : items) if (auto *page = qgraphicsitem_cast<QtPDF::PDFPageGraphicsItem *>(item)) {
          request.body = true; request.page = page->pageNum();
          const QPointF pdfPoint = page->mapToPage(page->mapFromScene(view->mapToScene(pos)));
          auto backend = page->page().toStrongRef(); if (!backend) break;
          request.point = QPointF(pdfPoint.x(), backend->pageSizeF().height() - pdfPoint.y());
          next = QStringLiteral("body:%1:%2:%3").arg(request.page).arg(qRound(request.point.x() / 4)).arg(qRound(request.point.y() / 4)); break;
        }
      }
      const QPoint global = area->viewport()->mapToGlobal(pos);
      if (next.isEmpty()) cancel();
      else if (next != identity || (global - position).manhattanLength() > 6) { cancel(); identity = next; pending = request; position = global; timer.start(ReferenceHoverDelay); }
    } else if (event->type() == QEvent::ToolTip && (preview || slidePreview)) {
      return !identity.isEmpty() && !pending.body;
    } else if (event->type() == QEvent::DynamicPropertyChange && static_cast<QDynamicPropertyChangeEvent *>(event)->propertyName()=="researchSlideMode") cancel();
    else if (event->type() == QEvent::Leave || event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonDblClick || event->type() == QEvent::Wheel || event->type() == QEvent::KeyPress || event->type() == QEvent::Hide || event->type() == QEvent::Resize || event->type() == QEvent::WindowDeactivate) cancel();
    return false;
  }
  void startHover() {
    if (identity.isEmpty()) return;
    if (busy) { timer.start(100); return; }
    Request request = context(); request.key = pending.key; request.anchor = pending.anchor; request.destination = pending.destination;
    request.body = pending.body; request.page = pending.page; request.point = pending.point;
    if (request.pdf.isEmpty() && request.root.isEmpty()) return;
    busy = true; const quint64 serial = generation; const QPointer<References> self(this);
    QThreadPool::globalInstance()->start(QRunnable::create([self, serial, request]() {
      const Result result = work(request);
      QMetaObject::invokeMethod(qApp, [self, serial, result, request]() {
        if (!self) return;
        self->busy = false;
        const QFileInfo current(request.pdf);
        if (self->generation != serial || self->identity.isEmpty() || buffers() != request.buffers || current.size() != request.pdfSize || current.lastModified().toMSecsSinceEpoch() != request.pdfTime) return;
        self->showCard(result);
      }, Qt::QueuedConnection);
    }));
  }
  void showCard(const Result &result) {
    if ((pending.body && !result.visual()) || (!result.visual() && result.title.isEmpty() && result.message.isEmpty())) return;
    if (card) delete card.data();
    auto *frame = new PopupFrame(source ? static_cast<QWidget *>(source.data()) : static_cast<QWidget *>(preview.data()), Qt::ToolTip);
    card = frame; frame->setObjectName(QStringLiteral("researchReferenceCard")); frame->setAttribute(Qt::WA_ShowWithoutActivating); frame->setAttribute(Qt::WA_TransparentForMouseEvents);
    frame->setProperty("researchReferenceKey", result.key); frame->setProperty("researchReferencePage", result.page);
    frame->setProperty("researchReferenceCrop", result.viewBox);
    frame->setStyleSheet(QStringLiteral("QFrame#researchReferenceCard {background:#ffffff;border:1px solid #d2d2d7;border-radius:8px;} QLabel {background:transparent;border:0;color:#1d1d1f;}"));
    preparePopupSurface(frame);
    auto *layout = new QVBoxLayout(frame); layout->setContentsMargins(16, 12, 16, 10); layout->setSpacing(8);
    if(result.previous&&result.visual()){auto *revision=new QLabel(englishUi()?QStringLiteral("Previous PDF"):Research::ui(QStringLiteral("上一版 PDF")),frame);revision->setObjectName(QStringLiteral("researchReferenceRevision"));revision->setStyleSheet(QStringLiteral("color:#6e6e73;font-size:11px;"));layout->addWidget(revision);}
    const bool slideMode = source && source->property("researchSlideMode").toBool();
    auto *viewport = area()->viewport();
    QScreen *screen = QGuiApplication::screenAt(position); if (!screen) screen = QGuiApplication::primaryScreen();
    QRect available = screen->availableGeometry();
    if(slideMode)available=available.intersected(QRect(viewport->mapToGlobal(QPoint()),viewport->size()));
    QRect protectedArea(position-QPoint(0,18),QSize(1,36)),pageArea;
    if(slidePreview){
      for(auto *item:slidePreview->items(viewport->mapFromGlobal(position)))if(auto *page=qgraphicsitem_cast<QtPDF::PDFPageGraphicsItem *>(item)){
        auto backend=page->page().toStrongRef();if(!backend)break;
        auto map=[&](QPointF p){return viewport->mapToGlobal(slidePreview->mapFromScene(page->mapToScene(page->mapFromPage(QPointF(p.x(),backend->pageSizeF().height()-p.y())))));};
        pageArea=QRect(map(QPointF()),map(QPointF(backend->pageSizeF().width(),backend->pageSizeF().height()))).normalized();
        if(pending.body&&page->pageNum()==result.page)protectedArea=QRect(map(result.viewBox.topLeft()),map(result.viewBox.bottomRight())).normalized().adjusted(-6,-6,6,6);
        break;
      }
    }
    QSize limit(slideMode?qMax(80,qMin(580,available.width()-40)):580,slideMode?qMax(60,available.height()/2-50):qMax(60,available.height()-70));
    if(slideMode&&!pageArea.isEmpty()){
      const int blankHeight=qMax(pageArea.top()-available.top(),available.bottom()-pageArea.bottom())-12;
      if(blankHeight>=120&&result.visualSize.height()*.7<=blankHeight-54)limit.setHeight(qMin(limit.height(),blankHeight-54));
    }
    if (result.visual()) {
      if (!result.vector.isEmpty()) layout->addWidget(new VectorPreview(result, frame, limit));
      else {
      auto *image = new QLabel(frame); image->setObjectName(QStringLiteral("researchReferenceImage"));
      // Preserve physical pixels. Scaling to 580 *logical* pixels before Windows
      // applies 125–200% display scaling was discarding the high-resolution image.
      QPixmap pixmap = QPixmap::fromImage(result.image);
      const QSize pixelLimit(qRound(limit.width()*result.density),qRound(limit.height()*result.density));
      if(pixmap.width()>pixelLimit.width()||pixmap.height()>pixelLimit.height())pixmap=pixmap.scaled(pixelLimit,Qt::KeepAspectRatio,Qt::SmoothTransformation);
      pixmap.setDevicePixelRatio(result.density); image->setPixmap(pixmap); layout->addWidget(image);
      }
      if (!result.target.block.file.isEmpty()) {
        auto *origin = new QLabel(QStringLiteral("%1:%2").arg(QFileInfo(result.target.block.file).fileName()).arg(result.target.block.firstLine), frame);
        origin->setObjectName(QStringLiteral("researchReferenceOrigin")); origin->setTextFormat(Qt::PlainText);
        origin->setStyleSheet(QStringLiteral("color:#6e6e73;font-size:11px;")); layout->addWidget(origin);
      }
    } else {
      auto *title = new QLabel(result.title.isEmpty() ? result.key : ui(result.title), frame); title->setTextFormat(Qt::PlainText); layout->addWidget(title);
      auto *message = new QLabel(ui(result.message), frame); message->setTextFormat(Qt::PlainText); message->setWordWrap(true); message->setMaximumWidth(qMin(500,limit.width())); layout->addWidget(message);
      if (result.found) { auto *snippet = new QLabel(result.target.block.text.left(650), frame); snippet->setTextFormat(Qt::PlainText); snippet->setWordWrap(true); snippet->setMaximumWidth(qMin(500,limit.width())); snippet->setFont(smoothTextFont(source ? source->editor()->font() : QFont(QStringLiteral("JetBrains Mono NL"), 10))); layout->addWidget(snippet); }
    }
    frame->adjustSize();
    frame->setProperty("researchReferenceProtectedRect",protectedArea);frame->setProperty("researchReferencePageRect",pageArea);
    const int proposedX=slideMode?position.x()-frame->width()/2:position.x()+16;
    const int x=qBound(available.left(),proposedX,qMax(available.left(),available.right()-frame->width()+1));
    int y=slideMode?protectedArea.top()-frame->height()-12:position.y()+24;
    if(slideMode&&y<available.top())y=protectedArea.bottom()+13;
    else if(!slideMode&&y+frame->height()>available.bottom())y=position.y()-frame->height()-12;
    // Prefer the unused canvas outside the PDF page so surrounding prose also
    // stays visible. Only fall back to the protected-formula placement if needed.
    if(slideMode&&!pageArea.isEmpty()){
      if(pageArea.top()-frame->height()-12>=available.top())y=pageArea.top()-frame->height()-12;
      else if(pageArea.bottom()+13+frame->height()<=available.bottom()+1)y=pageArea.bottom()+13;
    }
    y=qBound(available.top(),y,qMax(available.top(),available.bottom()-frame->height()+1));
    if(slideMode&&QRect(QPoint(x,y),frame->size()).intersects(protectedArea)){delete frame;return;}
    frame->move(x,y);
    if (motionEnabled() && QApplication::isEffectEnabled(Qt::UI_FadeTooltip)) {
      frame->setWindowOpacity(0); frame->show();
      auto *fade = new QPropertyAnimation(frame, "windowOpacity", frame); fade->setDuration(120); fade->setStartValue(0.); fade->setEndValue(1.); fade->setEasingCurve(QEasingCurve::OutCubic); fade->start(QAbstractAnimation::DeleteWhenStopped);
    } else frame->show();
  }
  void sourceReferenceFont(QTextBrowser *text) {
    const auto font=source->editor()->font();auto family=font.family();family.replace(QLatin1Char('"'),QStringLiteral("\\\""));
    // QWidget's global UI family otherwise wins over setFont after polishing.
    text->setStyleSheet(QStringLiteral("QTextBrowser#researchPinnedSource {font-family:\"%1\";font-size:%2%3;}")
      .arg(family).arg(font.pixelSize()>0?font.pixelSize():font.pointSizeF()).arg(font.pixelSize()>0?QStringLiteral("px"):QStringLiteral("pt")));
    text->setFont(font);text->document()->setDefaultFont(font);
  }
  void invalidatePinned() {
    if(!pinnedDock||pinnedDock->isHidden())return;
    ++pinGeneration;
    pinnedDock->setProperty("researchPinnedReady",false);
    pinnedDock->setProperty("researchPinnedVisual",false);
    pinnedResult=Result();if(pinnedTitle)pinnedTitle->setText(englishUi()?QStringLiteral("Updating reference…"):Research::ui(QStringLiteral("正在更新查阅…")));
    if(auto *go=pinnedDock->findChild<QToolButton*>(QStringLiteral("researchPinnedGoToSource")))go->setEnabled(false);
    // Remove the compiled excerpt immediately; never leave a stale theorem
    // looking current during the debounce or a failed refresh.
    auto *notice=qobject_cast<QLabel*>(pinnedArea->widget());
    if(!notice||notice->objectName()!=QStringLiteral("researchPinnedLoading")){
      if(auto *old=pinnedArea->takeWidget())delete old;
      notice=new QLabel;notice->setObjectName(QStringLiteral("researchPinnedLoading"));notice->setWordWrap(true);notice->setMargin(8);pinnedArea->setWidget(notice);
    }
    notice->setText(englishUi()?QStringLiteral("Loading reference…"):Research::ui(QStringLiteral("正在读取查阅内容…")));
    refreshPanelMotion(pinnedDock);
    pinTimer.start();
  }
  void pin(const QString &key,const QString &targetFile={},int offset=-1,bool activate=true) {
    if(!source)return;if(source->untitled()){showTransientStatusMessage(source,englishUi()?QStringLiteral("Save the document before pinning a reference."):Research::ui(QStringLiteral("请先保存文稿，再固定查阅。")));return;}
    cancel();pinnedSavedRange.clear();auto cursor=source->editor()->textCursor();pinnedKey=key.isEmpty()?referenceAt(cursor.block().text(),cursor.positionInBlock()).key:key;
    if(key.isEmpty()&&cursor.hasSelection())pinnedKey.clear();
    pinnedOffset=cursor.position();pinnedFile=path(source->fileName());pinnedAnchor=cursor;pinnedRange=cursor.hasSelection();pinnedSelection=pinnedRange;
    if(!targetFile.isEmpty()&&offset>=0){
      pinnedKey.clear();pinnedFile=path(targetFile);pinnedOffset=offset;pinnedRange=false;pinnedSelection=false;pinnedAnchor=QTextCursor();
      if(auto *target=TeXDocumentWindow::findDocument(pinnedFile)){pinnedAnchor=QTextCursor(target->textDoc());pinnedAnchor.setPosition(qBound(0,offset,target->textDoc()->characterCount()-1));}
    }
    if(!pinnedDock) {
      auto *dock=new QDockWidget(source);pinnedDock=dock;dock->setObjectName(QStringLiteral("researchPinnedReference"));dock->setFeatures(QDockWidget::NoDockWidgetFeatures);dock->setAllowedAreas(Qt::BottomDockWidgetArea);
      auto *header=new QWidget(dock);header->setStyleSheet(QStringLiteral("QWidget {background:#ffffff;} QLabel {font-size:14px;color:#30363c;}"));auto *row=new QHBoxLayout(header);row->setContentsMargins(8,0,4,0);row->setSpacing(4);auto *title=new QLabel(header);pinnedTitle=title;title->setMinimumWidth(0);title->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);row->addWidget(title,1);
      auto *go=new QToolButton(header);go->setObjectName(QStringLiteral("researchPinnedGoToSource"));go->setText(englishUi()?QStringLiteral("Go to source"):Research::ui(QStringLiteral("定位源码")));configureButton(go,ButtonRole::Text);row->addWidget(go);
      auto *uses=new QToolButton(header);uses->setObjectName(QStringLiteral("researchPinnedUses"));uses->setText(englishUi()?QStringLiteral("Context"):Research::ui(QStringLiteral("上下文")));configureButton(uses,ButtonRole::Text);row->addWidget(uses);uses->hide();connect(uses,&QToolButton::clicked,this,[this]{openReading(pinnedKey,false,pinnedFile,pinnedAnchor.isNull()?pinnedOffset:pinnedAnchor.selectionStart());});
      auto *edit=new QToolButton(header);edit->setObjectName(QStringLiteral("researchPinnedEditObject"));edit->setText(englishUi()?QStringLiteral("Edit"):Research::ui(QStringLiteral("编辑")));configureButton(edit,ButtonRole::Text);row->addWidget(edit);edit->hide();
      connect(edit,&QToolButton::clicked,this,[this]{const auto b=pinnedResult.target.block;rememberReadingPlace(source);auto *w=TeXDocumentWindow::findDocument(b.file);if(!w)w=TeXDocumentWindow::openDocument(b.file,true,true,b.firstLine);if(w){QTextCursor c(w->textDoc());c.setPosition(b.start);w->editor()->setTextCursor(c);editResearchObject(w);}});
      auto *close=new QToolButton(header);close->setIcon(QIcon(QStringLiteral(":/research/icons/close.svg")));close->setObjectName(QStringLiteral("researchPinnedClose"));close->setAccessibleName(englishUi()?QStringLiteral("Close reference"):Research::ui(QStringLiteral("收起查阅")));configureButton(close,ButtonRole::Icon);row->addWidget(close);dock->setTitleBarWidget(header);
      auto *content=new QScrollArea(dock);pinnedArea=content;content->setObjectName(QStringLiteral("researchPinnedContent"));content->setStyleSheet(QStringLiteral("QScrollArea,QScrollArea>QWidget>QWidget {background:#ffffff;border:0;}"));content->setFrameShape(QFrame::NoFrame);content->setWidgetResizable(true);dock->setWidget(content);source->addDockWidget(Qt::BottomDockWidgetArea,dock);
      content->viewport()->installEventFilter(this);
      connect(close,&QToolButton::clicked,this,[this]{++pinGeneration;pinTimer.stop();setPanelVisible(pinnedDock,false);source->editor()->setFocus();});
      connect(go,&QToolButton::clicked,this,[this]{const auto &b=pinnedResult.target.block;if(pinnedDock->property("researchPinnedReady").toBool()&&pinnedResult.found&&!b.file.isEmpty()){rememberReadingPlace(source);TeXDocumentWindow::openDocument(b.file,true,true,b.firstLine);}});
      connect(dock,&QDockWidget::visibilityChanged,this,[this](bool visible){if(!visible){++pinGeneration;pinTimer.stop();}else invalidatePinned();});
    }
    pinnedDock->setMaximumHeight(qMax(110,source->height()/3));setPanelVisible(pinnedDock,true,qMin(210,source->height()/4));
    invalidatePinned();
    if(activate){refreshPinned();source->editor()->setFocus();}
  }
  void refreshPinned() {
    if(!source||!pinnedDock||pinnedDock->isHidden())return;
    pinTimer.stop();
    pinnedDock->setProperty("researchPinnedReady",false);
    Request request=context();request.key=pinnedKey;request.offset=pinnedOffset;request.file=pinnedFile;request.pinned=true;
    if(pinnedAnchor.isNull()&&!pinnedSavedRange.isEmpty()){request.contextRange=pinnedSavedRange;request.restorePinnedSelection=pinnedSelection;}
    if(pinnedKey.isEmpty()&&!pinnedAnchor.isNull())request.offset=pinnedRange?(pinnedAnchor.hasSelection()?pinnedAnchor.selectionStart():-1):pinnedAnchor.position();
    if(pinnedKey.isEmpty()&&pinnedSelection&&pinnedAnchor.hasSelection()){request.selectionStart=pinnedAnchor.selectionStart();request.selectionEnd=pinnedAnchor.selectionEnd();}
    const auto serial=++pinGeneration;QPointer<References> guard(this);
    QThreadPool::globalInstance()->start(QRunnable::create([guard,serial,request]{const auto result=work(request);if(qApp)QMetaObject::invokeMethod(qApp,[guard,serial,request,result]{
      if(!guard||serial!=guard->pinGeneration||!guard->pinnedDock||guard->pinnedDock->isHidden())return;
      const QFileInfo current(request.pdf);if(buffers()!=request.buffers||current.size()!=request.pdfSize||current.lastModified().toMSecsSinceEpoch()!=request.pdfTime){guard->invalidatePinned();return;}
      guard->pinnedResult=result;if(auto *uses=guard->pinnedDock->findChild<QToolButton*>(QStringLiteral("researchPinnedUses"))){uses->setVisible(result.found);}if(auto *edit=guard->pinnedDock->findChild<QToolButton*>(QStringLiteral("researchPinnedEditObject"))){const auto family=objectFamily(result.target.block.kind);edit->setVisible(result.found&&(family==QStringLiteral("figure")||family==QStringLiteral("table")));}if(!result.key.isEmpty())guard->pinnedKey=result.key;
      if(auto *go=guard->pinnedDock->findChild<QToolButton*>(QStringLiteral("researchPinnedGoToSource")))go->setEnabled(result.found&&!result.target.block.file.isEmpty());
      if(result.found&&result.key.isEmpty()&&result.target.block.file==request.file&&guard->pinnedKey.isEmpty()){
        if(auto *target=TeXDocumentWindow::findDocument(request.file)){guard->pinnedAnchor=QTextCursor(target->textDoc());guard->pinnedAnchor.setPosition(result.target.block.start);guard->pinnedAnchor.setPosition(result.target.block.end,QTextCursor::KeepAnchor);guard->pinnedRange=true;}
      }
      auto *body=new QWidget;auto *layout=new QVBoxLayout(body);layout->setContentsMargins(8,4,8,8);
      const auto state=result.visual()?(result.previous?(englishUi()?QStringLiteral("Previous PDF"):Research::ui(QStringLiteral("上一版 PDF"))):(englishUi()?QStringLiteral("Compiled"):Research::ui(QStringLiteral("已编译")))):(englishUi()?QStringLiteral("Source"):Research::ui(QStringLiteral("源码")));
      const auto name=result.target.block.kind==QStringLiteral("selection")?(englishUi()?QStringLiteral("Selection"):Research::ui(QStringLiteral("选区"))):(result.title.isEmpty()?result.key:result.title);
      guard->pinnedTitle->setText(result.found?(englishUi()?QStringLiteral("Reference · "):Research::ui(QStringLiteral("查阅 · ")))+name+QStringLiteral(" · ")+QFileInfo(result.target.block.file).fileName()+QStringLiteral(" · ")+state:(englishUi()?QStringLiteral("Reference unavailable"):Research::ui(QStringLiteral("查阅对象不可用"))));
      guard->pinnedTitle->setToolTip(guard->pinnedTitle->text());
      if(!result.vector.isEmpty())layout->addWidget(new VectorPreview(result,body,QSize(qMax(160,guard->pinnedDock->width()-32),10000)),0,Qt::AlignLeft);
      else if(!result.image.isNull()){auto *label=new QLabel(body);label->setPixmap(QPixmap::fromImage(result.image));layout->addWidget(label);}
      else {auto *text=new QTextBrowser(body);text->setObjectName(QStringLiteral("researchPinnedSource"));text->setFrameShape(QFrame::NoFrame);guard->sourceReferenceFont(text);text->setPlainText(result.found?result.target.block.text:!result.message.isEmpty()?ui(result.message):(englishUi()?QStringLiteral("The reference was not found. It may have been removed or renamed."):Research::ui(QStringLiteral("未找到查阅对象，它可能已被删除或改名。"))));layout->addWidget(text);}
      if(auto *old=guard->pinnedArea->takeWidget())delete old;guard->pinnedArea->setWidget(body);if(guard->pendingPinnedScroll>=0){guard->pinnedArea->verticalScrollBar()->setValue(guard->pendingPinnedScroll);guard->pendingPinnedScroll=-1;}guard->pinnedDock->setProperty("researchPinnedReady",true);guard->pinnedDock->setProperty("researchPinnedVisual",result.visual());guard->pinnedDock->setProperty("researchPinnedPrevious",result.previous);guard->pinnedDock->setProperty("researchPinnedFile",result.target.block.file);guard->pinnedDock->setProperty("researchPinnedCrop",result.viewBox);guard->pinnedDock->setProperty("researchPinnedPage",result.page);
      // One visual transition per surface: the opening panel already reveals
      // its freshly rendered content. A second fade would make it turn pale
      // again when the panel's raster disappears.
      if(!refreshPanelMotion(guard->pinnedDock))QTimer::singleShot(0,body,[body]{animateArrival(body);});
    },Qt::QueuedConnection);}));
  }
  void loadReading(Request request,bool retry=false) {
    readingRefresh.stop();if(!retry)readingRetries=0;if(related)related->setProperty("researchReadingRefreshStopped",false);pendingReadingState.clear();readingBusy=true;readingDirty=false;request.related=true;const quint64 serial=++readingGeneration;
    relatedResult.key=request.key;relatedResult.citation=request.citation;relatedResult.found=false;
    requestedContextFile=request.file;requestedContextRange=request.contextRange;
    if(reading)reading->setPlainText(words("正在读取上下文…","Reading context…"));
    const auto epoch=readingEpoch;const auto ticket=++*epoch;request.cancelled=[epoch,ticket]{return epoch->load()!=ticket;};
    const QPointer<References> self(this);
    QThreadPool::globalInstance()->start(QRunnable::create([self,serial,request]{
      if(request.cancelled())return;const Result result=work(request);if(request.cancelled()||!qApp)return;
      QMetaObject::invokeMethod(qApp,[self,serial,result,request]{
        if(!self||self->readingGeneration!=serial||!self->related)return;
        if(!result.sourcesCurrent||buffers()!=request.buffers){self->invalidateReading(false);return;}
        self->relatedResult=result;self->readingBuffers=request.buffers;self->readingBusy=false;self->readingDirty=false;self->showReading();
        if(!self->pendingReadingState.isEmpty()){
          auto state=self->pendingReadingState;self->pendingReadingState.clear();
          if(state.take(QStringLiteral("resolveObject")).toBool()){
            state.insert(QStringLiteral("key"),result.key);state.insert(QStringLiteral("citation"),result.citation);
            state.remove(QStringLiteral("contextFile"));state.remove(QStringLiteral("contextRange"));
            if(result.found){const auto &b=result.target.block;state.insert(QStringLiteral("contextFile"),b.file);state.insert(QStringLiteral("contextRange"),textAnchor(result.index.sources.value(b.file),b.start,b.end,result.sourceHashes.value(b.file)));}
          }
          self->restoreReadingState(state,false);
        }
      },Qt::QueuedConnection);
    }));
  }
  void openReading(const QString &key={},bool citation=false,const QString &file={},int offset=-1) {
    if(!source)return;cancel();Request request=context();if(request.root.isEmpty())return;
    request.offset=offset<0?source->editor()->textCursor().position():offset;
    if(!file.isEmpty())request.file=file;request.key=key;request.citation=citation;
    if(related){related->show();related->raise();loadReading(request);return;}
    rememberReadingPlace(source);
    auto *dialog=new MaterialDialog(source);related=dialog;dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setObjectName(QStringLiteral("researchRelatedReading"));dialog->setWindowTitle(words("查阅","References"));configureSettingsSurface(dialog);
    auto *layout=new QVBoxLayout(dialog);layout->setContentsMargins(20,20,20,16);layout->setSpacing(12);
    auto *navigation=new QHBoxLayout;
    for(const auto &name:{QStringLiteral("researchNavigateBack"),QStringLiteral("researchNavigateForward")})if(auto *action=source->findChild<QAction*>(name)){
      const bool back=name.endsWith(QStringLiteral("Back"));const QIcon icon(back?QStringLiteral(":/research/icons/codicons/arrow-left.svg"):QStringLiteral(":/research/icons/codicons/arrow-right.svg"));
      auto *button=new QToolButton(dialog);button->setObjectName(back?QStringLiteral("researchReadingBack"):QStringLiteral("researchReadingForward"));button->setDefaultAction(action);
      // The shared action updates text and icon when history changes. Keep its
      // binding, but restore this compact view's local presentation afterwards.
      const auto refresh=[button,action,icon]{button->setIcon(icon);button->setToolButtonStyle(Qt::ToolButtonIconOnly);button->setAccessibleName(action->text());button->setToolTip(action->text()+QStringLiteral(" (")+action->shortcut().toString(QKeySequence::NativeText)+QLatin1Char(')'));};
      connect(action,&QAction::changed,button,refresh);refresh();configureButton(button,ButtonRole::Icon);button->setAutoRaise(true);navigation->addWidget(button);
    }
    filter=new QLineEdit(dialog);filter->setPlaceholderText(words("查找标签或文献…","Find a label or reference…"));configureField(filter);navigation->addWidget(filter,1);layout->addLayout(navigation);
    reading=new QTextBrowser(dialog);reading->setObjectName(QStringLiteral("researchRelatedContents"));configureReadingBrowser(reading);layout->addWidget(reading,1);
    reading->setPlainText(words("正在读取上下文…","Reading context…"));reading->installEventFilter(this);reading->viewport()->installEventFilter(this);filter->installEventFilter(this);
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Close,dialog);buttons->button(QDialogButtonBox::Close)->setText(ui(QStringLiteral("关闭")));configureButton(buttons->button(QDialogButtonBox::Close));layout->addWidget(buttons);connect(buttons,&QDialogButtonBox::rejected,dialog,&QDialog::reject);auto *demo=buttons->addButton(words("使用示范","Short demo"),QDialogButtonBox::HelpRole);configureButton(demo);connect(demo,&QPushButton::clicked,dialog,[dialog]{showOnboardingTopic(dialog,QStringLiteral("reference"));});
    connect(filter,&QLineEdit::textChanged,this,[this]{if(!pendingReadingState.isEmpty())pendingReadingState.insert(QStringLiteral("query"),filter->text());showReading();});
    connect(reading,&QTextBrowser::anchorClicked,this,[this](const QUrl &url){
      if(url.scheme()==QStringLiteral("retry")){readingRetries=0;refreshReading();}
      else if(url.scheme()==QStringLiteral("label")||url.scheme()==QStringLiteral("cite")){
        rememberReadingPlace(source);if(filter){QSignalBlocker block(filter);filter->clear();}
        Request request=context();request.key=url.path();request.citation=url.scheme()==QStringLiteral("cite");loadReading(request);
      }else if(url.scheme()==QStringLiteral("block")){
        bool ok=false;const int row=url.path().toInt(&ok);if(!ok||row<0||row>=relatedResult.index.blocks.size())return;
        const auto block=relatedResult.index.blocks[row];rememberReadingPlace(source);
        Request request=context();request.key.clear();request.file=block.file;request.offset=block.start;
        request.contextRange=textAnchor(relatedResult.index.sources.value(block.file),block.start,block.end,relatedResult.sourceHashes.value(block.file));
        if(filter){QSignalBlocker guard(filter);filter->clear();}loadReading(request);
      }else if(url.scheme()==QStringLiteral("source")){
        const auto link=readingLinks.value(url.path());const auto file=link.value(QStringLiteral("file")).toString();
        auto *target=TeXDocumentWindow::findDocument(file);if(!target&&QFileInfo(file).isFile())target=TeXDocumentWindow::openDocument(file,false,false);
        int a=0,b=0;
        if(!target||!locateTextAnchor(target->textDoc()->editorText(),link.value(QStringLiteral("range")).toMap(),&a,&b)){
          showTransientStatusMessage(source,words("原位置已改变，重新查阅以更新上下文。","The location changed. Reopen context to refresh it."));return;
        }
        rememberReadingPlace(source);if(related){++readingRestoreSerial;pendingReadingState.clear();related->hide();}
        QTextCursor cursor(target->textDoc());cursor.setPosition(a);target->editor()->setTextCursor(cursor);target->editor()->ensureCursorVisible();target->selectWindow();target->editor()->setFocus();
      }
    });
    for(const auto &name:{QStringLiteral("researchNavigateBack"),QStringLiteral("researchNavigateForward")})if(auto *action=source->findChild<QAction*>(name))dialog->addAction(action);
    connect(dialog,&QDialog::finished,source,[owner=source]{if(owner)owner->editor()->setFocus();});
    connect(dialog,&QObject::destroyed,this,[this]{++readingGeneration;++*readingEpoch;relatedResult={};readingLinks.clear();readingBusy=false;readingDirty=false;readingBuffers.clear();readingRefresh.stop();pendingReadingState.clear();});
    fitWorkspaceDialog(dialog,740,620);dialog->show();loadReading(request);
  }
  void showReading() {
    if(!reading||!filter||readingBusy||readingDirty)return;
    const auto &index=relatedResult.index;const auto project=relatedResult.project;readingLinks.clear();
    QString html=readingStyle();
    auto link=[](const QString &scheme,const QString &key,const QString &caption){QUrl url;url.setScheme(scheme);url.setPath(key);return QStringLiteral("<a href='%1'>%2</a>").arg(url.toString().toHtmlEscaped(),caption.toHtmlEscaped());};
    auto kindName=[](const QString &kind){
      if(equation(kind))return words("公式","Equation");
      const QHash<QString,QString> names{{QStringLiteral("theorem"),words("定理","Theorem")},{QStringLiteral("lemma"),words("引理","Lemma")},{QStringLiteral("proof"),words("证明","Proof")},{QStringLiteral("proposition"),words("命题","Proposition")},{QStringLiteral("corollary"),words("推论","Corollary")},{QStringLiteral("definition"),words("定义","Definition")},{QStringLiteral("remark"),words("注记","Remark")}};
      return names.value(kind,kind.isEmpty()?words("正文","Passage"):kind);
    };
    auto caption=[&](const QString &key,bool citation){
      if(citation&&project)for(const auto &entry:project->entries)if(entry.kind==QStringLiteral("cite")&&entry.key==key)return entry.title.isEmpty()?key:entry.title;
      for(const auto &d:index.definitions)if(d.key==key)return kindName(d.block.kind)+(d.number.isEmpty()?QString():QLatin1Char(' ')+d.number)+QStringLiteral(" · ")+key;
      return key;
    };
    auto excerpt=[&](QString body){
      // Only omit an outer prose environment and its label. Math and unknown
      // commands remain literal; the source link always opens the exact range.
      const QRegularExpression outer(QStringLiteral(R"(^\s*\\begin\{([^{}]+)\})"));body=body.trimmed();const auto opening=outer.match(body);
      if(opening.hasMatch()&&!equation(opening.captured(1))&&body.trimmed().endsWith(QStringLiteral("\\end{")+opening.captured(1)+QLatin1Char('}'))){body=body.trimmed();body.chop(int(opening.captured(1).size())+6);body.remove(0,opening.capturedEnd());}
      body.remove(QRegularExpression(QStringLiteral(R"(\\label\s*\{[^{}]*\})")));
      body=body.left(12000).trimmed();QString rendered;int position=0;
      const QRegularExpression references(QStringLiteral(R"(\\(ref|eqref|autoref|cref|Cref|cite|citep|citet)\s*\{([^{}]+)\})"));
      auto matches=references.globalMatch(masked(body));
      while(matches.hasNext()){const auto match=matches.next();rendered+=body.mid(position,match.capturedStart()-position).toHtmlEscaped();QStringList links;const bool citation=match.captured(1).startsWith(QStringLiteral("cite"));for(const auto &key:match.captured(2).split(QLatin1Char(',')))links.append(link(citation?QStringLiteral("cite"):QStringLiteral("label"),key.trimmed(),caption(key.trimmed(),citation)));rendered+=links.join(QStringLiteral(", "));position=match.capturedEnd();}
      rendered+=body.mid(position).toHtmlEscaped();rendered.replace(QStringLiteral("\n\n"),QStringLiteral("</p><p>"));rendered.replace(QLatin1Char('\n'),QStringLiteral("<br>"));return QStringLiteral("<p>%1</p>").arg(rendered);
    };
    auto section=[&](const QString &title,const QString &file,int start,int end,const QString &body,const QString &heading){
      const auto text=index.sources.value(file);const auto id=QString::number(readingLinks.size());
      readingLinks.insert(id,{{QStringLiteral("file"),file},{QStringLiteral("range"),textAnchor(text,start,end,relatedResult.sourceHashes.value(file))}});
      QString nearby;for(int i=0;i<index.blocks.size();++i){const auto &block=index.blocks[i];if(block.file==file&&start>=block.start&&start<block.end){nearby=QStringLiteral(" &nbsp; · &nbsp; ")+link(QStringLiteral("block"),QString::number(i),words("查阅上下文","Read context"));break;}}
      if(!title.isEmpty())html+=readingSection(title.toHtmlEscaped());
      if(!heading.isEmpty())html+=QStringLiteral("<p class='meta'>%1</p>").arg(heading.toHtmlEscaped());
      html+=excerpt(body)+QStringLiteral("<p class='meta'>%1%2</p>").arg(link(QStringLiteral("source"),id,QFileInfo(file).fileName()),nearby);
    };
    if(!relatedResult.message.isEmpty())html+=QStringLiteral("<p>%1</p>").arg(relatedResult.message.toHtmlEscaped());
    const QString search=filter->text().trimmed();
    if(!search.isEmpty()||(relatedResult.key.isEmpty()&&!relatedResult.found)){
      html+=readingTitle(words("标签与文献","Labels and references"));int count=0;
      for(const auto &d:index.definitions)if(search.isEmpty()||d.key.contains(search,Qt::CaseInsensitive)||d.block.heading.contains(search,Qt::CaseInsensitive)){if(++count>200)break;html+=QStringLiteral("<p>%1<br><span style='color:#768089'>%2</span></p>").arg(link(QStringLiteral("label"),d.key,caption(d.key,false)),d.block.heading.toHtmlEscaped());}
      if(project)for(const auto &entry:project->entries)if(entry.kind==QStringLiteral("cite")&&(search.isEmpty()||(entry.key+entry.title+entry.detail).contains(search,Qt::CaseInsensitive))){if(++count>200)break;html+=QStringLiteral("<p>%1<br><span style='color:#768089'>%2</span></p>").arg(link(QStringLiteral("cite"),entry.key,entry.title.isEmpty()?entry.key:entry.title),entry.key.toHtmlEscaped());}
      if(count>200)html+=QStringLiteral("<p>%1</p>").arg(words("继续输入以缩小范围。","Type more to narrow the results."));
      if(!count)html+=QStringLiteral("<p class='meta'>%1</p>").arg(words("未找到匹配的标签或文献。","No matching labels or references."));
    }else{
      QVector<ReferenceBlock> selected;
      if(relatedResult.citation){
        html+=readingTitle(words("文献","Reference"))+QStringLiteral("<p class='meta'>%1</p>").arg(relatedResult.key.toHtmlEscaped());
        int count=0;for(const auto &entry:relatedResult.bibliography)if(entry.second.key==relatedResult.key){const auto &record=entry.second;section({},entry.first,record.start,record.end,record.fields.value(QStringLiteral("title")).value+QStringLiteral("\n\n")+record.fields.value(QStringLiteral("author")).value+QStringLiteral(" · ")+record.fields.value(QStringLiteral("year")).value,{});++count;}
        if(!count&&project)for(const auto &entry:project->entries)if(entry.kind==QStringLiteral("cite")&&entry.key==relatedResult.key){const auto text=index.sources.value(entry.file);section({},entry.file,entry.offset,qMin(int(text.size()),entry.offset+160),entry.title+QStringLiteral("\n\n")+entry.detail,{});++count;}
        if(!count)html+=QStringLiteral("<p>%1</p>").arg(words("尚未找到此文献的条目。","This bibliography entry was not found."));
        if(count>1)html+=QStringLiteral("<p>%1</p>").arg(words("存在重复文献键，请核对。","Duplicate citation keys need review."));
      }else{
        if(!relatedResult.key.isEmpty())for(const auto &definition:index.definitions)if(definition.key==relatedResult.key&&selected.size()<10)selected.append(definition.block);
        if(selected.isEmpty()&&relatedResult.found)selected.append(relatedResult.target.block);
        const auto kind=selected.isEmpty()?QString():selected.first().kind;
        html+=readingTitle(kindName(kind).toHtmlEscaped()+(relatedResult.key.isEmpty()?QString():QStringLiteral(" &nbsp; <span style='font-weight:400;color:#768089'>%1</span>").arg(relatedResult.key.toHtmlEscaped())));
        QStringList origin;if(!selected.isEmpty()&&!selected.first().heading.isEmpty())origin.append(selected.first().heading);html+=QStringLiteral("<p class='meta'>%1</p>").arg(origin.join(QStringLiteral(" · ")).toHtmlEscaped());
        if(selected.size()>1)html+=QStringLiteral("<p>%1</p>").arg(ui(QStringLiteral("此标签有多个定义，请先消除重复。")));
        const auto definitions=selected;
        for(const auto &block:definitions){
          section({},block.file,block.start,block.end,block.text,{});
          if(block.kind!=QStringLiteral("proof"))for(const auto &proof:index.blocks)if(proof.kind==QStringLiteral("proof")){
            const int newline=proof.text.indexOf(QLatin1Char('\n'));const auto header=proof.text.left(newline<0?qMin(300,int(proof.text.size())):newline);bool explicitProof=false;
            auto refs=referencePattern().globalMatch(masked(header));while(refs.hasNext()){const auto match=refs.next();if(!relatedResult.key.isEmpty()&&match.captured(1).split(QLatin1Char(',')).contains(relatedResult.key))explicitProof=true;}
            const bool adjacent=proof.file==block.file&&proof.start>=block.end&&proof.firstLine-block.lastLine<=3;
            if((explicitProof||adjacent)&&selected.size()<20){section(explicitProof?ui(QStringLiteral("明确关联的证明")):ui(QStringLiteral("相邻证明")),proof.file,proof.start,proof.end,proof.text,{});selected.append(proof);}
          }
        }
        QStringList outgoing;QSet<QString> shown;
        if(project)for(const auto &use:project->uses)for(const auto &block:selected)if(use.file==block.file&&use.offset>=block.start&&use.offset<block.end){const bool citation=use.kind==QStringLiteral("citation-use");const auto id=use.kind+QLatin1Char(':')+use.key;if(shown.contains(id))break;shown.insert(id);if(shown.size()<=100)outgoing.append(link(citation?QStringLiteral("cite"):QStringLiteral("label"),use.key,caption(use.key,citation)));break;}
        html+=readingSection(words("此处引用","References from here"));
        html+=outgoing.isEmpty()?QStringLiteral("<p class='meta'>%1</p>").arg(words("没有显式引用","No explicit references")):QStringLiteral("<p>%1</p>").arg(outgoing.join(QStringLiteral(" &nbsp; · &nbsp; ")));
      }
      int count=0;
      if(project&&!relatedResult.key.isEmpty())for(const auto &use:project->uses)if(use.key==relatedResult.key&&(use.kind==QStringLiteral("citation-use"))==relatedResult.citation){
        if(++count>100)continue;if(count==1)html+=readingSection(words("引用此处","Referenced from"));
        const auto text=index.sources.value(use.file);const int start=qMax(0,int(text.lastIndexOf(QLatin1Char('\n'),qMax(0,use.offset-1)))+1);int end=int(text.indexOf(QLatin1Char('\n'),use.offset));if(end<0)end=int(text.size());
        section({},use.file,use.offset,qMin(end,use.offset+160),text.mid(start,end-start),use.detail);
      }
      if(count>100)html+=QStringLiteral("<p>%1</p>").arg(ui(QStringLiteral("仅显示前 100 处引用。")));
    }
    QSet<QString> missingBibliographies;
    if(project)for(const auto &issue:project->issues)if(issue.kind==QStringLiteral("missing-bibliography")){
      auto file=QFileInfo(issue.key).isAbsolute()?issue.key:QFileInfo(issue.file).absoluteDir().filePath(issue.key);if(QFileInfo(file).suffix().isEmpty())file+=QStringLiteral(".bib");missingBibliographies.insert(QDir::cleanPath(file));
      const auto id=QString::number(readingLinks.size());const auto text=index.sources.value(issue.file);readingLinks.insert(id,{{QStringLiteral("file"),issue.file},{QStringLiteral("range"),textAnchor(text,issue.offset,issue.offset,relatedResult.sourceHashes.value(issue.file))}});
      html+=QStringLiteral("<p class='meta'>%1 · %2</p>").arg(ui(QStringLiteral("文献文件未找到")).toHtmlEscaped(),link(QStringLiteral("source"),id,issue.key));
    }
    QStringList unavailable=relatedResult.unavailableSources.values();unavailable.sort();
    for(const auto &file:unavailable)if(!missingBibliographies.contains(file))html+=QStringLiteral("<p class='meta'>%1 · %2</p>").arg(words("无法读取文件","Cannot read file").toHtmlEscaped(),QDir(QFileInfo(source?source->fileName():QString()).absolutePath()).relativeFilePath(file).toHtmlEscaped());
    auto malformed=relatedResult.bibliographyErrors.keys();malformed.sort();
    for(const auto &file:malformed){
      const auto id=QString::number(readingLinks.size());readingLinks.insert(id,{{QStringLiteral("file"),file},{QStringLiteral("range"),textAnchor(index.sources.value(file),0,0,relatedResult.sourceHashes.value(file))}});
      html+=QStringLiteral("<p class='meta'>%1 · %2</p>").arg(link(QStringLiteral("source"),id,QFileInfo(file).fileName()),relatedResult.bibliographyErrors.value(file).toHtmlEscaped());
    }
    if(index.truncated||(project&&project->truncated))html+=QStringLiteral("<p>%1</p>").arg(ui(QStringLiteral("项目过大，当前显示的是已读取部分。")));
    else if(project&&project->citationCoveragePartial&&missingBibliographies.isEmpty()&&unavailable.isEmpty()&&malformed.isEmpty())html+=QStringLiteral("<p class='meta'>%1</p>").arg(words("部分文献尚未读取","Some references could not be read"));
    reading->setHtml(html);
  }
};
}

ReferenceToken referenceAt(const QString &text, int position) {
  const QString clean = masked(text); auto refs = referencePattern().globalMatch(clean);
  while (refs.hasNext()) {
    const auto m = refs.next(); if (position < m.capturedStart() || position >= m.capturedEnd()) continue;
    int start = m.capturedStart(1); const auto keys = m.captured(1).split(QLatin1Char(','));
    for (const QString &key : keys) { const int end = start + key.size(); if (position >= start && position < end) return {key.trimmed(), start, end}; start = end + 1; }
    if (keys.size() == 1) return {keys.first().trimmed(), int(m.capturedStart()), int(m.capturedEnd())};
  }
  return {};
}
ReferenceIndex referenceIndex(const QString &root, const QHash<QString, QString> &open,bool retainSources,const std::function<bool()> &cancelled) {
  return readReferenceIndex(root,open,retainSources,cancelled,nullptr);
}
static ReferenceIndex readReferenceIndex(const QString &root,const QHash<QString,QString> &open,bool retainSources,const std::function<bool()> &cancelled,QSet<QString> *unavailable) {
  ReferenceIndex index; if (root.isEmpty()) return index;
  QStringList todo{path(root)}; QSet<QString> seen; QHash<QString, QString> texts; QStringList order;
  int bytes = 0; const QDir rootDir(QFileInfo(root).absolutePath());
  static const QRegularExpression include(QStringLiteral(R"(\\(?:input|include|subfile)\s*(?:\{([^{}]+)\}|([^\s%{}]+)))"));
  while (!todo.isEmpty() && seen.size() < 250 && bytes < 24 * 1024 * 1024) {
    if(cancelled&&cancelled())return {};
    const QString file = path(todo.takeFirst()); if (seen.contains(file)) continue; seen.insert(file);
    bool ok=open.contains(file);const QString text=ok?open.value(file):read(file,&ok);if(!ok&&unavailable)unavailable->insert(file);bytes += text.size()*2;texts.insert(file,text);order.append(file);
    auto includes = include.globalMatch(masked(text));
    while (includes.hasNext()) {
      auto m = includes.next(); QString name = (m.captured(1).isEmpty() ? m.captured(2) : m.captured(1)).trimmed();
      if (name.contains(QLatin1Char('\\')) || name.contains(QLatin1Char('#'))) continue;
      if (!name.endsWith(QStringLiteral(".tex"), Qt::CaseInsensitive)) name += QStringLiteral(".tex");
      for (const auto &dir : {rootDir, QFileInfo(file).absoluteDir()}) { const QString child = path(dir.filePath(name)); if (open.contains(child) || QFileInfo(child).isFile()) { todo.append(child); break; } }
    }
  }
  index.truncated = !todo.isEmpty();
  QSet<QString> theorems{QStringLiteral("theorem"), QStringLiteral("lemma"), QStringLiteral("proposition"), QStringLiteral("corollary"), QStringLiteral("definition"), QStringLiteral("assumption"), QStringLiteral("remark"), QStringLiteral("example")};
  static const QRegularExpression declared(QStringLiteral(R"(\\(?:newtheorem|declaretheorem)\*?\s*(?:\[[^\]]*\]\s*)?\{([^{}]+)\})"));
  for (const QString &text : texts) { auto declarations = declared.globalMatch(masked(text)); while (declarations.hasNext()) theorems.insert(declarations.next().captured(1)); }
  for (const QString &file : order) {if(cancelled&&cancelled())return {};parseFile(index, file, texts.value(file), theorems);}
  if(retainSources)index.sources=texts;
  return index;
}
void installReferenceSource(TeXDocumentWindow *window, QMenu *more) {
  auto *controller = new References(window); auto *action = new QAction((englishUi()?QStringLiteral("Read context here…"):Research::ui(QStringLiteral("查阅此处上下文…"))), window);
  action->setObjectName(QStringLiteral("researchRelatedAction")); action->setShortcut(QKeySequence(QStringLiteral("Alt+F12"))); window->addAction(action); more->addAction(action);
  QObject::connect(action, &QAction::triggered, controller, [controller]() { controller->openReading(); });
  auto *pin=new QAction(englishUi()?QStringLiteral("Pin below"):Research::ui(QStringLiteral("固定到下方")),window);pin->setObjectName(QStringLiteral("researchPinReferenceAction"));window->addAction(pin);more->addAction(pin);QObject::connect(pin,&QAction::triggered,controller,[controller]{controller->pin({});});
  QObject::connect(TWApp::instance(),&TWApp::updatedTranslators,window,[action,pin]{action->setText(englishUi()?QStringLiteral("Read context here…"):Research::ui(QStringLiteral("查阅此处上下文…")));pin->setText(englishUi()?QStringLiteral("Pin below"):Research::ui(QStringLiteral("固定到下方")));});
}
void openReferenceContext(TeXDocumentWindow *window,int offset){if(window)if(auto *c=dynamic_cast<References*>(window->findChild<QObject*>(QStringLiteral("researchReferences"),Qt::FindDirectChildrenOnly)))c->openReading({},false,{},offset);}
QVariantMap referenceReadingState(TeXDocumentWindow *window){if(window)if(auto *c=dynamic_cast<References*>(window->findChild<QObject*>(QStringLiteral("researchReferences"),Qt::FindDirectChildrenOnly)))return c->readingState();return {};}
void restoreReferenceReadingState(TeXDocumentWindow *window,const QVariantMap &state,bool passive){if(window)if(auto *c=dynamic_cast<References*>(window->findChild<QObject*>(QStringLiteral("researchReferences"),Qt::FindDirectChildrenOnly))){auto restored=state;if(passive)restored.insert(QStringLiteral("dialog"),false);c->restoreReadingState(restored,true,passive);}}
void pinSourceReference(TeXDocumentWindow *window,const QString &key){if(!window)return;if(auto *c=dynamic_cast<References*>(window->findChild<QObject*>(QStringLiteral("researchReferences"),Qt::FindDirectChildrenOnly)))c->pin(key);}
void pinSourceObject(TeXDocumentWindow *window,const QString &file,int offset){if(!window)return;if(auto *c=dynamic_cast<References*>(window->findChild<QObject*>(QStringLiteral("researchReferences"),Qt::FindDirectChildrenOnly)))c->pin({},file,offset);}
void installReferencePreview(PDFDocumentWindow *window) { new References(window); }
void installReferenceSlidePreview(TeXDocumentWindow *window, QtPDF::PDFDocumentView *view) { new References(window,view); }
}
