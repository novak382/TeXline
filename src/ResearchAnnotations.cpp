#include "ResearchAnnotations.h"
#include "ResearchAppearance.h"
#include "ResearchBuild.h"
#include "ResearchProject.h"
#include "ResearchSourceMap.h"
#include "ResearchWorkflow.h"
#include "PDFDocumentWindow.h"
#include "TeXDocumentWindow.h"
#include "TWApp.h"
#include "Settings.h"
#include "CompletingEdit.h"
#include "../modules/QtPDF/src/PDFDocumentScene.h"
#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGraphicsSimpleTextItem>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLockFile>
#include <QRegularExpression>
#include <QDynamicPropertyChangeEvent>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsTextItem>
#include <QGraphicsObject>
#include <QPainter>
#include <QPainterPathStroker>
#include <QKeyEvent>
#include <QMenu>
#include <QMap>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPropertyAnimation>
#include <QVariantAnimation>
#include <QRubberBand>
#include <QRunnable>
#include <QSaveFile>
#include <QScrollBar>
#include <QShortcut>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
namespace Research {
namespace {
QString words(const char *zh, const char *en) {
  return (englishUi()?QString::fromUtf8(en):Research::ui(QString::fromUtf8(zh)));
}
QString canonical(const QString &name) {
  QFileInfo info(name);
  return info.exists() ? info.canonicalFilePath() : info.absoluteFilePath();
}
QString digest(const QString &text) {
  return QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(),QCryptographicHash::Sha256).toHex());
}
bool escaped(const QString &text,int at) {
  int count=0;while(at>0 && text[--at]==QLatin1Char('\\'))++count;return count%2;
}
int lineOffset(const QString &text,int line) {
  int at=0;for(int n=1;n<line;++n){at=text.indexOf(QLatin1Char('\n'),at);if(at<0)return -1;++at;}return at;
}
QString plainTeX(const QString &text) {
  QString result;int run=0;
  for(const QChar ch:text) {
    switch(ch.unicode()) {
      case '\\':result+=QStringLiteral("\\textbackslash{}");break;
      case '{':case '}':case '%':case '#':case '&':case '_':case '$':result+=QLatin1Char('\\');result+=ch;break;
      case '~':result+=QStringLiteral("\\textasciitilde{}");break;
      case '^':result+=QStringLiteral("\\textasciicircum{}");break;
      case '\n':result+=QStringLiteral("\\par\n");break;
      case '\r':break;
      default:result+=ch;
    }
    if(ch.unicode()<128&&ch.isLetterOrNumber())++run;else run=0;
    if(run>=18||QStringLiteral("/._-\\").contains(ch)){result+=QStringLiteral("\\allowbreak{}");run=0;}
  }
  return result;
}
}

AnnotationSpan annotationSpan(const QString &source,int offset) {
  if(offset<0||offset>=source.size()||source.size()>4*1024*1024)return {};
  const auto text=maskProjectText(source);
  struct Open {QString name;int start,body;bool atomic;};
  QVector<Open> stack;QVector<AnnotationSpan> spans;
  int braces=0,paragraph=0;QString math;
  struct Group {bool flowing,paragraphSeen;};
  QVector<Group> groups;
  auto flowingGroups=[&]{return std::all_of(groups.cbegin(),groups.cend(),[](const Group &g){return g.flowing;});};
  static const QRegularExpression env(QStringLiteral(R"(\\(begin|end)\s*\{([^{}]+)\})"));
  const bool root=text.contains(QRegularExpression(QStringLiteral(R"(\\begin\s*\{document\})")));
  bool body=!root;
  const QSet<QString> flowing{QStringLiteral("proof"),QStringLiteral("theorem"),QStringLiteral("lemma"),QStringLiteral("claim"),
    QStringLiteral("proposition"),QStringLiteral("corollary"),QStringLiteral("definition"),QStringLiteral("remark"),
    QStringLiteral("assumption"),QStringLiteral("quote"),QStringLiteral("quotation"),QStringLiteral("landscape"),
    QStringLiteral("multicols"),QStringLiteral("itemize"),QStringLiteral("enumerate"),QStringLiteral("description")};
  auto atomic=[&]{return std::any_of(stack.cbegin(),stack.cend(),[](const Open &open){return open.atomic;});};
  auto paragraphEnd=[&](int end) {
    int start=paragraph;while(start<end&&text[start].isSpace())++start;
    if(end>start)spans.append({start,end,end,{}});paragraph=end;
  };
  for(int i=0;i<text.size();++i) {
    if(text[i]==QLatin1Char('\\')&&!escaped(text,i)) {
      if(body&&!atomic()&&flowingGroups()&&math.isEmpty()) {
        static const QRegularExpression heading(QStringLiteral(R"(\\(part|chapter|section|subsection|subsubsection|paragraph|subparagraph|maketitle|tableofcontents|listoffigures|listoftables)(?![A-Za-z@])\*?\s*(?:\[[^\]]*\]\s*)?)"));
        const auto h=heading.match(text,i,QRegularExpression::NormalMatch,QRegularExpression::AnchorAtOffsetMatchOption);
        if(h.hasMatch()) {
          int end=int(h.capturedEnd());
          if(end<text.size()&&text[end]==QLatin1Char('{')) {
            int depth=1;++end;while(end<text.size()&&depth){if(!escaped(text,end)){if(text[end]==QLatin1Char('{'))++depth;else if(text[end]==QLatin1Char('}'))--depth;}++end;}
            if(depth)return {};
          }
          paragraphEnd(i);spans.append({i,end,end,QStringLiteral("heading")});paragraph=end;i=end-1;continue;
        }
        static const QRegularExpression item(QStringLiteral(R"(\\item(?![A-Za-z@])(?:\s*\[[^\]]*\])?)"));
        const auto list=item.match(text,i,QRegularExpression::NormalMatch,QRegularExpression::AnchorAtOffsetMatchOption);
        if(list.hasMatch()){paragraphEnd(i);paragraph=i;i=int(list.capturedEnd())-1;continue;}
      }
      const auto match=env.match(text,i,QRegularExpression::NormalMatch,QRegularExpression::AnchorAtOffsetMatchOption);
      if(match.hasMatch()) {
        const auto name=match.captured(2);const int end=int(match.capturedEnd());
        if(name==QStringLiteral("document")) {
          if(match.captured(1)==QStringLiteral("begin")){body=true;paragraph=end;}
          else {if(body&&stack.isEmpty()&&!braces&&math.isEmpty())paragraphEnd(i);body=false;}
        } else if(body&&flowingGroups()&&math.isEmpty()) {
          if(match.captured(1)==QStringLiteral("begin")) {
            if(!atomic()){paragraphEnd(i);paragraph=end;}
            stack.append({name,i,end,!flowing.contains(name)});
          } else {
            if(stack.isEmpty()||stack.last().name!=name)return {};
            const auto open=stack.takeLast();
            if(!atomic()) {
              // Floats retain their opinions inside the float, below its caption.
              // Long opinions in ordinary floats are moved out in annotationSources.
              const bool floating=name==QStringLiteral("figure")||name==QStringLiteral("table")||
                                  name==QStringLiteral("figure*")||name==QStringLiteral("table*");
              if(open.atomic)spans.append({open.start,end,floating?i:end,name});
              else {
                int start=paragraph;while(start<i&&text[start].isSpace())++start;
                if(start<i)spans.append({start,end,end,name});
              }
              paragraph=end;
            }
          }
        }
        i=end-1;continue;
      }
      if(body) {
        const auto next=i+1<text.size()?text[i+1]:QChar();
        if(next==QLatin1Char('[')||next==QLatin1Char('(')) {if(math.isEmpty())math=QString(next);}
        else if(next==QLatin1Char(']')||next==QLatin1Char(')')) {
          if(math==(next==QLatin1Char(']')?QStringLiteral("["):QStringLiteral("(")))math.clear();
        }
        if(next==QLatin1Char('[')||next==QLatin1Char('(')||next==QLatin1Char(']')||next==QLatin1Char(')')){++i;continue;}
      }
    }
    if(!body)continue;
    if(!escaped(text,i)) {
      if(text[i]==QLatin1Char('$')) {
        const bool doubled=i+1<text.size()&&text[i+1]==QLatin1Char('$');
        const QString token=doubled?QStringLiteral("$$"):QStringLiteral("$");
        if(math.isEmpty())math=token;else if(math==token)math.clear();
        if(doubled)++i;
      } else if(math.isEmpty()) {
        if(text[i]==QLatin1Char('{')) {
          int previous=i-1;while(previous>=paragraph&&text[previous].isSpace())--previous;
          bool flows=previous<paragraph;
          if(!flows&&text[previous]!=QLatin1Char('}')&&text[previous]!=QLatin1Char(']')) {
            int command=previous;while(command>=paragraph&&(text[command].isLetter()||text[command]==QLatin1Char('@')))--command;
            flows=command<paragraph||text[command]!=QLatin1Char('\\');
          }
          groups.append({flows,false});++braces;
        } else if(text[i]==QLatin1Char('}')) {
          if(groups.isEmpty()||--braces<0)return {};
          if(groups.last().paragraphSeen&&flowingGroups()&&!atomic()){paragraphEnd(i);paragraph=i+1;}
          groups.removeLast();
        }
      }
    }
    if(!atomic()&&flowingGroups()&&math.isEmpty()&&text[i]==QLatin1Char('\n')) {
      int next=i+1;while(next<text.size()&&(text[next]==QLatin1Char(' ')||text[next]==QLatin1Char('\t')))++next;
      if(next<text.size()&&text[next]==QLatin1Char('\n')){paragraphEnd(i);for(auto &group:groups)group.paragraphSeen=true;paragraph=next+1;i=next;}
    }
  }
  if(body&&stack.isEmpty()&&!braces&&math.isEmpty())paragraphEnd(int(text.size()));
  // An unfinished draft must never receive a helper inside an unmatched group.
  if(!stack.isEmpty()||braces||!math.isEmpty())return {};
  for(const auto &span:spans)if(offset>=span.start&&offset<span.end)return span;
  return {};
}

Annotation makeAnnotation(const QString &file,const QString &source,int offset,
                          const QString &quote,int page,const QRectF &region,int endOffset) {
  if(endOffset<0)endOffset=offset;
  Annotation note;const auto first=annotationSpan(source,qMin(offset,endOffset)),last=annotationSpan(source,qMax(offset,endOffset));
  if(!first.valid()||!last.valid())return note;
  const AnnotationSpan span{first.start,last.end,last.insertion,last.environment};
  note.file=canonical(file);note.sourceHash=digest(source);note.offset=span.start;
  note.anchor=source.mid(span.start,span.end-span.start);note.prefix=source.mid(qMax(0,span.start-96),qMin(96,span.start));
  note.suffix=source.mid(span.end,96);note.quote=quote.left(12000);note.page=page;note.region=region;
  return note;
}
int resolveAnnotation(const Annotation &note,const QString &source) {
  if(note.anchor.isEmpty())return -1;
  if(digest(source)==note.sourceHash&&note.offset>=0&&source.mid(note.offset,note.anchor.size())==note.anchor)return note.offset;
  int found=-1,position=0,matches=0,context=-1,contexts=0;
  while((position=source.indexOf(note.anchor,position))>=0) {
    found=position;++matches;
    if(source.mid(qMax(0,position-note.prefix.size()),note.prefix.size())==note.prefix&&
       source.mid(position+note.anchor.size(),note.suffix.size())==note.suffix){context=position;++contexts;}
    ++position;
  }
  return matches==1?found:contexts==1?context:-1;
}
// Use the same source-boundary check for visibility and typesetting.
static AnnotationSpan resolvedAnnotationSpan(const Annotation &note,const QString &source,int *position=nullptr) {
  const int at=resolveAnnotation(note,source);
  if(at<0)return {};
  const auto first=annotationSpan(source,at),last=annotationSpan(source,at+int(note.anchor.size())-1);
  if(!first.valid()||!last.valid()||first.start!=at||last.end!=at+note.anchor.size()||source.mid(at,note.anchor.size())!=note.anchor)return {};
  if(position)*position=at;
  return last;
}
QJsonObject annotationJson(const Annotation &n) {
  QJsonArray rects;for(const auto &rect:n.selectionRects)rects.append(QJsonArray{rect.x(),rect.y(),rect.width(),rect.height()});
  return {{QStringLiteral("id"),n.id},{QStringLiteral("file"),n.file},{QStringLiteral("sourceHash"),n.sourceHash},
          {QStringLiteral("anchor"),n.anchor},{QStringLiteral("prefix"),n.prefix},{QStringLiteral("suffix"),n.suffix},
          {QStringLiteral("quote"),n.quote},{QStringLiteral("opinion"),n.opinion},{QStringLiteral("offset"),n.offset},
          {QStringLiteral("page"),n.page},{QStringLiteral("x"),n.region.x()},{QStringLiteral("y"),n.region.y()},
          {QStringLiteral("w"),n.region.width()},{QStringLiteral("h"),n.region.height()},
          {QStringLiteral("selectionOffset"),n.selectionOffset},{QStringLiteral("geometryHash"),n.geometryHash},
          {QStringLiteral("selectionRects"),rects},
          {QStringLiteral("referenceBox"),QJsonArray{n.referenceBox.x(),n.referenceBox.y(),n.referenceBox.width(),n.referenceBox.height()}}};
}
Annotation annotationFromJson(const QJsonObject &o) {
  Annotation n;n.id=o.value(QStringLiteral("id")).toString();n.file=o.value(QStringLiteral("file")).toString();
  n.sourceHash=o.value(QStringLiteral("sourceHash")).toString();n.anchor=o.value(QStringLiteral("anchor")).toString();
  n.prefix=o.value(QStringLiteral("prefix")).toString();n.suffix=o.value(QStringLiteral("suffix")).toString();
  n.quote=o.value(QStringLiteral("quote")).toString();n.opinion=o.value(QStringLiteral("opinion")).toString();
  n.offset=o.value(QStringLiteral("offset")).toInt(-1);n.page=o.value(QStringLiteral("page")).toInt(-1);
  n.region={o.value(QStringLiteral("x")).toDouble(),o.value(QStringLiteral("y")).toDouble(),
            o.value(QStringLiteral("w")).toDouble(),o.value(QStringLiteral("h")).toDouble()};
  n.selectionOffset=qBound(0,o.value(QStringLiteral("selectionOffset")).toInt(),int(n.anchor.size()));
  n.geometryHash=o.value(QStringLiteral("geometryHash")).toString();
  for(const auto &entry:o.value(QStringLiteral("selectionRects")).toArray()) {
    const auto a=entry.toArray();if(a.size()==4&&n.selectionRects.size()<4096)n.selectionRects.append({a[0].toDouble(),a[1].toDouble(),a[2].toDouble(),a[3].toDouble()});
  }
  const auto a=o.value(QStringLiteral("referenceBox")).toArray();if(a.size()==4)n.referenceBox={a[0].toDouble(),a[1].toDouble(),a[2].toDouble(),a[3].toDouble()};
  return n;
}
#include "ResearchAnnotationGeometry.inc"
QString annotationTeX(const QString &opinion,QString *error) {
  QString result;int plain=0;
  for(int i=0;i<opinion.size();++i) {
    QString end;int count=1;
    if(opinion[i]==QLatin1Char('$')&&!escaped(opinion,i)) {
      if(i+1<opinion.size()&&opinion[i+1]==QLatin1Char('$')){end=QStringLiteral("$$");count=2;}
      else end=QStringLiteral("$");
    } else if(opinion.mid(i,2)==QStringLiteral("\\(")){end=QStringLiteral("\\)");count=2;}
    else if(opinion.mid(i,2)==QStringLiteral("\\[")){end=QStringLiteral("\\]");count=2;}
    if(end.isEmpty())continue;
    int last=i+count;while((last=opinion.indexOf(end,last))>=0&&escaped(opinion,last))++last;
    if(last<0)continue; // Currency and unfinished delimiters remain literal prose.
    const QString math=opinion.mid(i,last+end.size()-i);
    static const QRegularExpression unsafe(QStringLiteral(R"(\\(?:input|include|write|openout|read|catcode|csname|def|let|newcommand|usepackage|documentclass|end|begin|special|directlua|pdfextension|immediate|shipout)\b)"));
    if(unsafe.match(math).hasMatch()) {
      if(error)*error=QStringLiteral("批注公式中含有文档或文件操作命令，请只保留数学表达式。");return {};
    }
    result+=plainTeX(opinion.mid(plain,i-plain))+math;i=last+end.size()-1;plain=i+1;
  }
  result+=plainTeX(opinion.mid(plain));return result;
}

AnnotationSources annotationSources(const QString &root,const QHash<QString,QString> &sources,
                                    const QVector<Annotation> &notes) {
  AnnotationSources result;result.texts=sources;
  if(notes.isEmpty())return result;
  const auto main=sources.value(root);const auto begin=QRegularExpression(QStringLiteral(R"(\\begin\s*\{document\})")).match(maskProjectText(main));
  if(!begin.hasMatch()){result.error=QStringLiteral("未找到主文档的 document 环境。");return result;}
  if(QRegularExpression(QStringLiteral(R"(\\documentclass[^\n]*\{(?:beamer|ctexbeamer)\})")).match(maskProjectText(main)).hasMatch()){
    result.error=QStringLiteral("当前批注排版适用于文章；幻灯片请使用讲者备注。");return result;
  }
  struct Insert {int at;QString text;};
  QHash<QString,QVector<Insert>> insertions;bool chinese=false,pinFloats=false;
  QSet<QString> identifiers;
  for(const auto &note:notes) {
    if(!QRegularExpression(QStringLiteral("^A[0-9]{2,8}$")).match(note.id).hasMatch()||identifiers.contains(note.id)){
      result.error=QStringLiteral("批注编号无效或重复。");return result;
    }
    identifiers.insert(note.id);
    const auto source=sources.value(note.file);int at=-1;
    const auto span=resolvedAnnotationSpan(note,source,&at);
    if(!span.valid()) {
      result.error=QStringLiteral("%1 的原文已改变，请重新选择批注位置。").arg(note.id);return result;
    }
    QString error;const auto text=annotationTeX(note.opinion,&error);
    if(!error.isEmpty()){result.error=error;return result;}
    if(note.opinion.trimmed().isEmpty()||note.opinion.size()>20000){result.error=QStringLiteral("批注意见为空或过长。");return result;}
    for(const QChar ch:note.opinion)if(ch.unicode()>=0x2e80&&ch.unicode()<=0xffff)chinese=true;
    QString opinion=QStringLiteral("\n\\TeXlineReviewNote{%1}{%2}\n").arg(note.id,text);
    if(span.environment==QStringLiteral("figure")||span.environment==QStringLiteral("table")) {
      pinFloats=true;
      // Pin only annotated ordinary floats in the temporary copy. Their opinion
      // then flows outside the unbreakable float, including onto another page.
      const auto open=QRegularExpression(QStringLiteral(R"(\\begin\s*\{(?:figure|table)\}\s*(?:\[[^\]]*\])?)")).match(source,span.start);
      if(!open.hasMatch()){result.error=QStringLiteral("无法确认浮动对象边界。");return result;}
      // A second opinion on the same object must not repeat this replacement.
      auto &rows=insertions[note.file];bool pinned=false;
      for(const auto &row:rows)if(row.at==int(open.capturedStart())&&row.text.startsWith(QStringLiteral("%texline-float:")))pinned=true;
      if(!pinned)rows.append({int(open.capturedStart()),QStringLiteral("%texline-float:%1:%2").arg(open.capturedLength()).arg(QStringLiteral("\\begin{%1}[H]").arg(span.environment))});
      rows.append({span.end,opinion});
    } else {
      if(span.environment.endsWith(QLatin1Char('*'))&&(span.environment.startsWith(QStringLiteral("figure"))||span.environment.startsWith(QStringLiteral("table")))&&note.opinion.size()>600) {
        result.error=QStringLiteral("%1：跨栏浮动对象的长批注请拆成较短意见，以免超出页面。").arg(note.id);return result;
      }
      insertions[note.file].append({span.insertion,opinion});
    }
  }
  QString helpers=QStringLiteral("\n% TeXline temporary review helpers\n\\usepackage{iftex}\n");
  if(pinFloats)helpers+=QStringLiteral("\\usepackage{float}\n");
  helpers+=QStringLiteral(
    "\\makeatletter\n\\@ifpackageloaded{xcolor}{}{\\@ifpackageloaded{color}{}{\\RequirePackage[nosetpagesize]{color}}}\n"
    "\\definecolor{TeXlineReviewInk}{RGB}{116,92,65}\n");
  if(chinese)helpers+=QStringLiteral(
    "\\ifPDFTeX\\RequirePackage{CJKutf8}\\else\\ifXeTeX"
    "\\@ifpackageloaded{xeCJK}{}{\\RequirePackage{xeCJK}\\setCJKmainfont{FandolSong-Regular}}"
    "\\else\\@ifpackageloaded{luatexja}{}{\\RequirePackage{luatexja-fontspec}\\setmainjfont{FandolSong-Regular}}\\fi\\fi\n");
  helpers+=QStringLiteral("\\newcommand{\\TeXlineReviewNote}[2]{\\par\\begingroup\\small\\normalfont"
    "\\color{TeXlineReviewInk}\\setlength{\\parindent}{0pt}\\setlength{\\parskip}{3pt}\\medskip\\noindent ");
  if(chinese)helpers+=QStringLiteral("\\ifPDFTeX\\begin{CJK*}{UTF8}{gbsn}\\fi ");
  helpers+=QStringLiteral("\\textbf{[#1]}\\enspace #2\\par ");
  if(chinese)helpers+=QStringLiteral("\\ifPDFTeX\\end{CJK*}\\fi ");
  helpers+=QStringLiteral("\\medskip\\endgroup}\n\\makeatother\n% End temporary review helpers\n");
  insertions[root].append({int(begin.capturedStart()),helpers});
  for(auto it=insertions.begin();it!=insertions.end();++it) {
    auto text=sources.value(it.key());auto rows=it.value();
    std::reverse(rows.begin(),rows.end());
    std::stable_sort(rows.begin(),rows.end(),[](const Insert &a,const Insert &b){return a.at>b.at;});
    for(const auto &row:rows) {
      if(row.text.startsWith(QStringLiteral("%texline-float:"))) {
        const auto length=row.text.section(QLatin1Char(':'),1,1).toInt();
        text.replace(row.at,length,row.text.section(QLatin1Char(':'),2));
      } else text.insert(row.at,row.text);
    }
    result.texts[it.key()]=text;
  }
  return result;
}

struct AnnotationFiles {
  QTemporaryDir temp{QDir::tempPath()+QStringLiteral("/texline-review-XXXXXX")};
  ~AnnotationFiles() {
#ifdef Q_OS_WIN
    // Engine search aliases are reparse points, never recursive cleanup targets.
    const QDir aliases(temp.path()+QStringLiteral("/search-paths"));
    for(const auto &entry:aliases.entryInfoList(QDir::AllEntries|QDir::NoDotAndDotDot|QDir::Hidden|QDir::System)) {
      const auto native=(QStringLiteral("\\\\?\\")+QDir::toNativeSeparators(entry.absoluteFilePath())).toStdWString();
      const auto attributes=GetFileAttributesW(native.c_str());
      if(attributes==INVALID_FILE_ATTRIBUTES){temp.setAutoRemove(false);continue;}
      if(attributes&FILE_ATTRIBUTE_REPARSE_POINT) {
        if(!((attributes&FILE_ATTRIBUTE_DIRECTORY)?RemoveDirectoryW(native.c_str()):DeleteFileW(native.c_str())))temp.setAutoRemove(false);
      }
    }
#endif
  }
};
AnnotationBuild compileAnnotations(const AnnotationRequest &r,const std::shared_ptr<std::atomic_bool> &cancel) {
  AnnotationBuild result;
  auto stop=[&]{if(!cancel||!cancel->load())return false;result.cancelled=true;return true;};
  if(stop())return result;
  if(!r.engine.supportsLocalBuild()||!r.engine.isAvailable()){result.error=QStringLiteral("请选择可用的 PDF 编译引擎。");return result;}
  if(!r.pdf.isEmpty()&&!pdfCompiledPairValid(r.pdf)){result.error=QStringLiteral("原 PDF 的编译记录不完整，请重新编译原稿。");return result;}
  const auto prepared=annotationSources(r.root,r.sources,r.notes);
  if(!prepared.error.isEmpty()){result.error=prepared.error;return result;}
  result.original=r.sources;result.modified=prepared.texts;
  result.files=QSharedPointer<AnnotationFiles>::create();
  const auto folder=result.files->temp.path();
  if(!result.files->temp.isValid()){result.error=QStringLiteral("无法创建临时批注副本。");return result;}
  auto files=buildDependencies(r.root,r.sources);
  for(auto it=r.sources.cbegin();it!=r.sources.cend();++it)if(!files.contains(it.key()))files.append(it.key());
  for(auto it=r.auxiliaries.cbegin();it!=r.auxiliaries.cend();++it)
    if(const auto relative=QDir(QFileInfo(r.root).absolutePath()).relativeFilePath(it.key());!QDir::isAbsolutePath(relative)&&!relative.startsWith(QStringLiteral("../"))&&!files.contains(it.key()))files.append(it.key());
  // Metadata and the published PDF are provenance, not authored inputs.
  files.erase(std::remove_if(files.begin(),files.end(),[&](const QString &file) {
    return file==r.pdf||file==pdfMetadataReadFile(r.pdf)||!QFileInfo(file).isFile()&&!r.sources.contains(file)&&!r.auxiliaries.contains(file);
  }),files.end());
  // Preserve relative paths within each volume; explicit cross-volume paths
  // are rewritten to their private copies below. No original is compiled in place.
  auto volume=[](const QString &file){const auto normalized=QDir::fromNativeSeparators(file);const auto match=QRegularExpression(QStringLiteral(R"(^([A-Za-z]:/|//[^/]+/[^/]+/))")).match(normalized);return match.hasMatch()?match.captured(1).toCaseFolded():QStringLiteral("/");};
  QHash<QString,QString> commonByVolume;
  for(const auto &file:files){const auto key=volume(file);auto common=commonByVolume.value(key,QFileInfo(file).absolutePath());
    while(QDir(common).relativeFilePath(file).startsWith(QStringLiteral("../"))){QDir parent(common);if(!parent.cdUp()){result.error=QStringLiteral("无法隔离论文依赖。");return result;}common=parent.absolutePath();}
    commonByVolume[key]=common;
  }
  QHash<QString,QString> copies,directories; qint64 total=0;
  const auto inputs=pdfMetadata(r.pdf).value(QStringLiteral("inputs")).toObject();
  for(const auto &file:files) {
    if(stop())return result;
    const auto volumeKey=volume(file);const auto relative=QDir(commonByVolume.value(volumeKey)).relativeFilePath(file);
    if(QDir::isAbsolutePath(relative)||relative.startsWith(QStringLiteral("../"))){result.error=QStringLiteral("无法隔离论文依赖。");return result;}
    const auto copy=folder+QStringLiteral("/source/volume-")+digest(volumeKey).left(12)+QLatin1Char('/')+relative;copies[file]=copy;
    directories[QFileInfo(file).absolutePath()]=QFileInfo(copy).absolutePath();
    if(r.sources.contains(file))result.copyToOriginal[canonical(copy)]=file;
    if(!r.sources.contains(file)&&!r.auxiliaries.contains(file)) {
      total+=QFileInfo(file).size();if(total>256*1024*1024){result.error=QStringLiteral("论文依赖超过 256 MB，请精简资源后生成批注稿。");return result;}
      auto entry=inputs.value(file).toObject();if(entry.isEmpty())entry=inputs.value(QDir(QFileInfo(r.root).absolutePath()).relativeFilePath(file)).toObject();
      const auto expected=entry.value(QStringLiteral("hash")).toString();
      if(!expected.isEmpty()) {
        QFile input(file);QCryptographicHash hash(QCryptographicHash::Sha256);
        if(!input.open(QIODevice::ReadOnly)||!hash.addData(&input)||QString::fromLatin1(hash.result().toHex())!=expected) {
          result.error=QStringLiteral("论文资源已改变，请先重新编译原稿：%1").arg(QFileInfo(file).fileName());return result;
        }
      }
    }
  }
  QStringList replacements=copies.keys()+directories.keys();
  std::sort(replacements.begin(),replacements.end(),[](const QString &a,const QString &b){return a.size()>b.size();});
  for(const auto &file:files) {
    if(stop())return result;
    const auto copy=copies.value(file);QDir().mkpath(QFileInfo(copy).absolutePath());
    if(r.sources.contains(file)||r.auxiliaries.contains(file)) {
      auto text=r.sources.contains(file)?prepared.texts.value(file):r.auxiliaries.value(file);
      for(const auto &key:replacements) {
        const auto destination=copies.contains(key)?copies.value(key):directories.value(key);
        text.replace(key,destination);text.replace(QDir::toNativeSeparators(key),destination);
      }
      if(r.sources.contains(file))result.modified[file]=text;
      QSaveFile output(copy);
      if(!output.open(QIODevice::WriteOnly)||output.write(text.toUtf8())!=text.toUtf8().size()||!output.commit()){result.error=QStringLiteral("无法写入临时编译副本。");return result;}
    } else if(!QFile::copy(file,copy)){result.error=QStringLiteral("无法复制论文资源：%1").arg(file);return result;}
  }
  const auto output=folder+QStringLiteral("/output");QDir().mkpath(output);
  // Recorder output may contain subdirectory aux files.
  for(const auto &file:files) {
    const auto relative=QDir(QFileInfo(r.root).absolutePath()).relativeFilePath(file);
    if(!QDir::isAbsolutePath(relative)&&!relative.startsWith(QStringLiteral("../")))QDir().mkpath(QFileInfo(QDir(output).filePath(relative)).absolutePath());
  }
  const int passes=QFileInfo(r.engine.program()).completeBaseName().compare(QStringLiteral("latexmk"),Qt::CaseInsensitive)==0?1:2;
  auto runPass=[&] {
    if(stop())return false;
    Engine engine=r.engine;std::unique_ptr<QProcess> process(engine.run(QFileInfo(copies.value(r.root)),nullptr,output));
    if(!process){result.error=QStringLiteral("无法启动批注编译。");return false;}
    QElapsedTimer timer;timer.start();
    while(!process->waitForFinished(100)) {
      result.log+=QString::fromLocal8Bit(process->readAll());if(result.log.size()>200000)result.log=result.log.right(200000);
      if(stop()||timer.elapsed()>300000){Engine::stop(process.get());process->waitForFinished(5000);if(!result.cancelled)result.error=QStringLiteral("批注编译超时。");return false;}
      if(process->state()==QProcess::NotRunning)break;
    }
    result.log+=QString::fromLocal8Bit(process->readAll());
    if(process->exitStatus()!=QProcess::NormalExit||process->exitCode()!=0||process->error()==QProcess::FailedToStart) {
      result.error=QStringLiteral("批注编译失败，原稿和已有 PDF 保持原样。");return false;
    }
    return true;
  };
  for(int pass=0;pass<passes;++pass)if(!runPass())return result;
  if(stop())return result;
  if(result.log.contains(QStringLiteral("Float too large for page"),Qt::CaseInsensitive)) {
    result.error=QStringLiteral("批注稿中的图表超出页面，请缩短该对象附近的意见或调整图表后重试。");return result;
  }
  const auto pdf=output+QLatin1Char('/')+QFileInfo(r.root).completeBaseName()+QStringLiteral(".pdf");
  auto doc=QtPDF::Backend::Document::newDocument(pdf);
  if(!doc||doc->numPages()<1){result.error=QStringLiteral("编译未生成有效的批注 PDF。");return result;}
  {
    TWSyncTeXSynchronizer sync(pdf,{}, {},QFileInfo(copies.value(r.root)).absolutePath());
    for(const auto &note:r.notes) {
      if(stop())return result;
      const auto original=r.sources.value(note.file),modified=result.modified.value(note.file);const int start=resolveAnnotation(note,original);
      if(start<0)continue;
      const int offset=mapSourcePosition(original,modified,qMin(start+note.selectionOffset,start+int(note.anchor.size())-1));
      const auto point=sync.syncFromTeX({copies.value(note.file),modified.left(offset).count(QLatin1Char('\n'))+1,0,-1},TWSynchronizer::LineResolution);
      if(point.page>0)result.underlines[note.id]=annotationLocations(note,doc,int(point.page-1),point.rects.toVector(),{},true,digest(original)==note.sourceHash);
    }
  }
  const auto overlay=annotationOverlay(result.underlines,doc);
  if(!overlay.isEmpty()) {
    QVector<QSizeF> beforeSizes;for(int i=0;i<doc->numPages();++i)if(auto page=doc->page(i).toStrongRef())beforeSizes.append(page->pageSizeF());
    auto &main=result.modified[r.root];const auto begin=QRegularExpression(QStringLiteral(R"(\\begin\s*\{document\})")).match(maskProjectText(main));
    if(!begin.hasMatch()){result.error=QStringLiteral("无法放置临时批注标记。");return result;}
    main.insert(begin.capturedStart(),overlay);QSaveFile marked(copies.value(r.root));
    if(!marked.open(QIODevice::WriteOnly)||marked.write(main.toUtf8())!=main.toUtf8().size()||!marked.commit()){result.error=QStringLiteral("无法写入临时批注标记。");return result;}
    doc.clear();if(!runPass())return result;
    doc=QtPDF::Backend::Document::newDocument(pdf);
    if(!doc||doc->numPages()!=beforeSizes.size()){result.error=QStringLiteral("批注标记改变了分页，已保留原稿。");return result;}
    for(int i=0;i<doc->numPages();++i) {
      const auto page=doc->page(i).toStrongRef();if(!page||page->pageSizeF()!=beforeSizes[i]){result.error=QStringLiteral("批注标记改变了纸张，已保留原稿。");return result;}
    }
  }
  if(!r.pdf.isEmpty()) {
    const auto original=QtPDF::Backend::Document::newDocument(r.pdf);QVector<QSizeF> sizes;
    if(original)for(int i=0;i<original->numPages();++i)if(auto page=original->page(i).toStrongRef())if(!sizes.contains(page->pageSizeF()))sizes.append(page->pageSizeF());
    for(int i=0;i<doc->numPages();++i) {
      const auto page=doc->page(i).toStrongRef();bool matches=false;
      if(page)for(const auto &size:sizes)if(qAbs(size.width()-page->pageSizeF().width())<.5&&qAbs(size.height()-page->pageSizeF().height())<.5)matches=true;
      if(!matches){result.error=QStringLiteral("批注稿纸张尺寸与原稿不一致，已保留原稿预览。");return result;}
    }
  }
  result.document=doc;
  doc->setPaperColor(r.paperColor);
  if(!r.notes.isEmpty()) {
    const auto id=r.focusId.isEmpty()?r.notes.first().id:r.focusId;
    // The backend creates pages lazily. Its document-wide search assumes a
    // scene has already instantiated them, which is not true for a fresh copy.
    for(int i=0;i<doc->numPages();++i)if(auto page=doc->page(i).toStrongRef()) {
      const auto hits=page->search(QStringLiteral("[%1]").arg(id),QtPDF::Backend::Search_CaseInsensitive);
      if(!hits.isEmpty()){result.focusPage=i;result.focusBox=hits.first().bbox;break;}
    }
  }
  if(r.renderResolution>0) {
    if(auto page=doc->page(result.focusPage).toStrongRef()) {
      // Exact physical resolution at ordinary zooms. Bound a whole-page warmup
      // at extreme zoom; the normal tile renderer still provides exact detail.
      const auto size=page->pageSizeF();
      const double resolution=qMin(r.renderResolution,72.*std::sqrt(20000000./(size.width()*size.height())));
      const auto rectangle=QRectF(0,0,size.width()*resolution/72.,size.height()*resolution/72.).toAlignedRect();
      page->renderToImage(resolution,resolution,rectangle,true);
    }
  }
  if(stop())return result;
  result.pdf=pdf;return result;
}

// GUI/controller implementation remains in this translation unit so the native
// fixture can exercise actual selection, persistence and switching together.
#include "ResearchAnnotationUI.inc"
}
