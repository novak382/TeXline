#include "ResearchMenuChoices.h"
#include <QClipboard>
#include <QMimeData>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include "ResearchReferences.h"
// Beamer mode is detected on open or enabled per window. No idle parsing for papers.
#include "ResearchSlides.h"
#include "ResearchProject.h"
#include "ResearchSlideExport.h"
#include <QFileDialog>
#include <QWidgetAction>
#include <QGridLayout>
#include <QCheckBox>
#include <QMimeData>
#include <QUrl>
#include <QToolTip>
#include <QMessageBox>
#include <QSaveFile>
#include <QImageReader>
#include "ResearchProject.h"
#include "ResearchDependencies.h"
#include "ResearchBuild.h"
#include "ResearchAppearance.h"
#include "ResearchMotion.h"
#include "ResearchWorkflow.h"
#include "ResearchFind.h"
#include "TeXDocumentWindow.h"
#include "PDFDocumentWindow.h"
#include "CompletingEdit.h"
#include "TWSynchronizer.h"
#include "TWApp.h"
#include "Settings.h"
#include "PDFDocumentScene.h"
#include "utils/FilePath.h"
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCache>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QDockWidget>
#include <QDropEvent>
#include <QDrag>
#include <QVariantAnimation>
#include <QFile>
#include <QFileSystemWatcher>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QKeyEvent>
#include <QListWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QInputDialog>
#include <QDialogButtonBox>
#include <QPlainTextEdit>
#include <QTabWidget>
#include <QTabBar>
#include <QPushButton>
#include <QMenu>
#include <QMenuBar>
#include <QPointer>
#include <QRegularExpression>
#include <QRunnable>
#include <QScrollBar>
#include <QShortcut>
#include <QResizeEvent>
#include <QScopedValueRollback>
#include <QStyledItemDelegate>
#include <QStyle>
#include <QPainter>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSlider>
#include <QTextBlock>
#include <QTextLayout>
#include <QThreadPool>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <atomic>
#include <limits>
#include <memory>
#include <algorithm>

namespace Research {
namespace {
QString path(const QString &name) { return Tw::Utils::canonicalSourcePath(name); }
QString read(const QString &name) {
  QFile file(name);
  if (!file.open(QIODevice::ReadOnly) || file.size() > 4*1024*1024) return {};
  const auto bytes=file.readAll();
  // Other encodings can still be edited normally, but cannot certify offsets.
  QString text=QString::fromUtf8(bytes);
  if(text.startsWith(QChar::ByteOrderMark))text.remove(0,1);
  text.replace(QStringLiteral("\r\n"),QStringLiteral("\n"));text.replace(QLatin1Char('\r'),QLatin1Char('\n'));
  return text.contains(QChar::ReplacementCharacter) ? QString() : text;
}
void spaces(const QString &s, int &p) { while (p<s.size() && s[p].isSpace()) ++p; }
bool escaped(const QString &s, int p) { int count=0; while (p>0 && s[--p]==QLatin1Char('\\')) ++count; return count%2; }
QString group(const QString &s, int &p, QChar left=QLatin1Char('{'), QChar right=QLatin1Char('}')) {
  spaces(s,p);
  if (p>=s.size() || s[p]!=left) return {};
  const int start=++p; int depth=1, braces=0;
  for (;p<s.size();++p) if (!escaped(s,p)) {
    if(s[p]==QLatin1Char('%')){while(p<s.size()&&s[p]!=QLatin1Char('\n'))++p;if(p>=s.size())break;continue;}
    if (left!=QLatin1Char('{')) {
      if(s[p]==QLatin1Char('{')) ++braces;
      if(s[p]==QLatin1Char('}')) --braces;
    }
    if(braces) continue;
    if(s[p]==left) ++depth;
    else if(s[p]==right && --depth==0) { const auto value=s.mid(start,p-start); ++p; return value; }
  }
  return {};
}
QString quotedGroup(const QString &value) {
  bool comment=false;for(int i=int(value.lastIndexOf(QLatin1Char('\n')))+1;i<value.size();++i)if(value[i]==QLatin1Char('%')&&!escaped(value,i)){comment=true;break;}
  return QLatin1Char('{')+value+(comment?QStringLiteral("\n"):QString())+QLatin1Char('}');
}
QString command(const QString &s,int &p) {
  const int start=++p;
  while(p<s.size() && (s[p].isLetter()||s[p]==QLatin1Char('@'))) ++p;
  if(p==start && p<s.size()) ++p;
  return s.mid(start,p-start);
}
void options(const QString &s,int &p,QString &value) {
  for(int n=0;n<4;++n) {
    spaces(s,p); if(p>=s.size())return;
    if(s[p]==QLatin1Char('<')) group(s,p,QLatin1Char('<'),QLatin1Char('>'));
    else if(s[p]==QLatin1Char('[')) value+=QLatin1Char(',')+group(s,p,QLatin1Char('['),QLatin1Char(']'));
    else return;
  }
}
QString pretty(QString title) {
  title.replace(QRegularExpression(QStringLiteral(R"(\\(?:texorpdfstring)\{([^{}]*)\}\{([^{}]*)\})")),QStringLiteral("\\2"));
  title.remove(QRegularExpression(QStringLiteral(R"(\\[A-Za-z@]+\*?)")));
  title.remove(QLatin1Char('{'));title.remove(QLatin1Char('}'));
  return title.simplified();
}
// Suppress only controller-owned transient cursor/selection changes. Explicit
// cross-scope selections still reveal the full source through the normal guard.
struct SlideSourceEdit {
  QPointer<CompletingEdit> editor;QVariant previous;
  explicit SlideSourceEdit(TeXDocumentWindow *w){if(w){editor=w->editor();previous=editor->property("researchSlideScopeTransaction");editor->setProperty("researchSlideScopeTransaction",true);}}
  ~SlideSourceEdit(){if(editor)editor->setProperty("researchSlideScopeTransaction",previous);}
};
struct Parser {
  SlideDeck deck;
  QString root;
  QHash<QString,QString> buffers;
  QSet<QString> active;
  QString section,subsection;
  bool backup{false};
  int conditionalDepth{0};
  std::function<bool()> cancelled;
  int total{0};
  bool stop() { if(cancelled&&cancelled()){deck.cancelled=true;return true;}return false; }
  void unsure() { deck.uncertain=true; }
#include "ResearchBeamerSource.inc"
  void file(const QString &name,int depth=0) {
    if(stop())return;
    const auto filename=path(name);
    if(depth>32 || active.contains(filename) || deck.frames.size()>4000 || total>12*1024*1024){unsure();return;}
    const QString raw=buffers.contains(filename)?buffers.value(filename):read(filename);
    if(raw.isEmpty()||raw.size()>4*1024*1024){unsure();return;}
    total+=int(raw.size());active.insert(filename);deck.sources.insert(filename,raw);
    const auto s=maskProjectText(raw);
    if(filename==root) {
      static const QRegularExpression cls(QStringLiteral(R"(\\documentclass\s*(?:\[[^\]]*\]\s*)?\{(?:beamer|ctexbeamer)\})"));
      deck.beamer=cls.match(s).hasMatch();
    }
    int braces=0;QStringList envs;
    QSet<QString> landmarks;
    if(filename==root){deck.document.append({QStringLiteral("完整源码"),filename,1});deck.document.append({QStringLiteral("导言"),filename,1});}
    else if(!deck.preambleEnd.contains(root))deck.document.append({QStringLiteral("导言"),filename,1});
    for(int p=0;p<s.size();) {
      if(stop())break;
      if(s[p]!=QLatin1Char('\\')) {
        if(s[p]==QLatin1Char('{')&&braces==0&&envs==QStringList{QStringLiteral("document")}&&groupedFrame(filename,raw,s,p,depth))continue;
        if(s[p]==QLatin1Char('{'))++braces;
        if(s[p]==QLatin1Char('}')){if(--braces<0){unsure();braces=0;}}
        ++p;continue;
      }
      const int start=p;const QString cmd=command(s,p);
      // A conditional inside a template does not conditionally create frames.
      // newif declares the following control sequence; it does not execute it.
      if(cmd==QStringLiteral("newif")){spaces(s,p);if(p<s.size()&&s[p]==QLatin1Char('\\'))command(s,p);continue;}
      if(braces==0) {
        if(cmd.startsWith(QStringLiteral("if"))){++conditionalDepth;continue;}
        if(cmd==QStringLiteral("fi")){if(conditionalDepth>0)--conditionalDepth;else unsure();continue;}
        if(cmd==QStringLiteral("else"))continue;
        if(cmd==QStringLiteral("appendix")){backup=true;section.clear();subsection.clear();continue;}
      }
      if((cmd==QStringLiteral("section")||cmd==QStringLiteral("subsection"))&&braces==0) {
        if(p<s.size()&&s[p]==QLatin1Char('*'))++p;
        QString opts;options(s,p,opts);const auto title=pretty(group(s,p));
        if(cmd==QStringLiteral("section")){section=title;subsection.clear();}else subsection=title;
        continue;
      }
      if(envs.isEmpty()&&braces==0) {
        QString title;
        if(cmd==QStringLiteral("usetheme")||cmd==QStringLiteral("usecolortheme")||cmd==QStringLiteral("usefonttheme")||cmd==QStringLiteral("setbeamertemplate")||cmd==QStringLiteral("addtobeamertemplate"))title=QStringLiteral("主题与模板");
        else if(cmd==QStringLiteral("newcommand")||cmd==QStringLiteral("renewcommand")||cmd==QStringLiteral("def")||cmd==QStringLiteral("newenvironment"))title=QStringLiteral("公共宏");
        if(!title.isEmpty()&&!landmarks.contains(title)){deck.document.append({title,filename,int(QStringView(raw).left(start).count(QLatin1Char('\n')))+1});landmarks.insert(title);}
      }
      const bool literalTop=braces==0&&(envs.isEmpty()||envs==QStringList{QStringLiteral("document")});
      if(declaration(cmd,s,p,filename,literalTop))continue;
      if(templateCommand(cmd,s,p))continue;
      if(frameMacros.contains(cmd)){if(!literalTop||conditionalDepth)unsure();else macroFrame(frameMacros.value(cmd),filename,raw,s,start,p,depth);continue;}
      if(cmd==QStringLiteral("includeonly")||cmd==QStringLiteral("includeonlyframes")||cmd==QStringLiteral("mode")||cmd==QStringLiteral("endinput")) unsure();
      QString env,opts,kind;
      if(cmd==QStringLiteral("begin")||cmd==QStringLiteral("end")) {
        env=group(s,p);
        if(cmd==QStringLiteral("end")){if(!envs.isEmpty()&&envs.last()==env)envs.removeLast();else unsure();continue;}
        if(env!=QStringLiteral("frame")){if(env==QStringLiteral("document"))deck.preambleEnd.insert(filename,start);envs.append(env);continue;}
        kind=QStringLiteral("frame");
      } else if(cmd==QStringLiteral("frame")||cmd==QStringLiteral("againframe")) kind=cmd;
      else if(cmd==QStringLiteral("input")||cmd==QStringLiteral("include")) {
        if(braces||(!envs.isEmpty()&&envs.last()!=QStringLiteral("document"))){unsure();continue;}
        spaces(s,p);QString child;
        if(p<s.size()&&s[p]==QLatin1Char('{'))child=group(s,p);
        else {int from=p;while(p<s.size()&&!s[p].isSpace()&&s[p]!=QLatin1Char('}'))++p;child=s.mid(from,p-from);}
        if(child.contains(QLatin1Char('\\'))||child.contains(QLatin1Char('#'))){unsure();continue;}
        const auto resolved=resolveDependency(child,filename,root,QStringLiteral(".tex"));
        if(resolved.isEmpty()||!authoredDependency(resolved,root)){unsure();continue;}
        file(resolved,depth+1);continue;
      } else continue;
      // A frame in a macro, condition body, or non-document environment is not
      // an independently movable source range. Do not infer execution of TeX.
      const bool top=braces==0&&(envs.isEmpty()||envs==QStringList{QStringLiteral("document")});
      if(!top) {unsure();continue;}
      if(conditionalDepth)unsure();
      options(s,p,opts);
      QString title,label;
      const int bodyStart=p;
      int end=p;
      if(kind==QStringLiteral("againframe")) {label=group(s,p);end=p;title=QStringLiteral("againframe: ")+label;}
      else if(env.isEmpty()) {group(s,p);end=p;}
      else {
        static const QRegularExpression finish(QStringLiteral(R"(\\(begin|end)\s*\{frame\})"));
        auto it=finish.globalMatch(s,p);bool found=false;
        while(it.hasNext()) {
          const auto m=it.next();
          if(escaped(s,int(m.capturedStart())))continue;
          if(m.captured(1)==QStringLiteral("begin")) {unsure();break;}
          end=int(m.capturedEnd());found=true;break;
        }
        if(!found){unsure();break;}
        p=end;
      }
      if(end<=bodyStart){unsure();continue;}
      const QString body=s.mid(bodyStart,end-bodyStart);
      if(kind!=QStringLiteral("againframe")) {
        int at=bodyStart;spaces(s,at);
        if(env==QStringLiteral("frame")&&at<s.size()&&s[at]==QLatin1Char('{')) title=pretty(group(s,at));
        static const QRegularExpression ft(QStringLiteral(R"(\\frametitle\*?\s*(?:<[^>]*>\s*)?(?:\[[^\]]*\]\s*)?\{)"));
        auto m=ft.match(body);if(m.hasMatch()){int q=bodyStart+int(m.capturedEnd())-1;title=pretty(group(s,q));}
        static const QRegularExpression lab(QStringLiteral(R"((?:^|,)\s*label\s*=\s*([^,]+))"));
        m=lab.match(opts);if(m.hasMatch())label=m.captured(1).trimmed();
      }
      int rangeStart=start,rangeEnd=end;
      // Keep indentation and trailing comments on the frame's own lines. Text
      // and comments between frames are never silently attached to either one.
      const int lineStart=int(raw.lastIndexOf(QLatin1Char('\n'),qMax(0,start-1)))+1;
      if(raw.mid(lineStart,start-lineStart).trimmed().isEmpty())rangeStart=lineStart;
      const int nl=int(raw.indexOf(QLatin1Char('\n'),end));
      const int lineEnd=nl<0?int(raw.size()):nl;
      if(s.mid(end,lineEnd-end).trimmed().isEmpty())rangeEnd=nl<0?lineEnd:nl+1;
      const int firstLine=int(QStringView(raw).left(start).count(QLatin1Char('\n')))+1;
      const int lastLine=int(QStringView(raw).left(end-1).count(QLatin1Char('\n')))+1;
      const bool inlineFrame=rangeStart!=lineStart||(rangeEnd==end&&nl>=0);
      const bool hasLabel=!label.isEmpty()||QRegularExpression(QStringLiteral(R"(\\(?:label|hypertarget|bibitem)(?![A-Za-z@]))")).match(body).hasMatch();
      const bool embeddedInput=QRegularExpression(QStringLiteral(R"(\\(?:input|include|subfile)(?![A-Za-z@]))")).match(body).hasMatch();
      if(embeddedInput){auto inside=body;if(env==QStringLiteral("frame"))inside.remove(QRegularExpression(QStringLiteral(R"(\\end\s*\{frame\}\s*$)")));if(!inlineDependencies(inside,filename,depth))unsure();}
      deck.frames.append({filename,title,label,kind,rangeStart,rangeEnd,firstLine,lastLine,0,0,
        !inlineFrame&&!embeddedInput&&!conditionalDepth,hasLabel,opts.contains(QStringLiteral("allowframebreaks")),{},backup});
      deck.frames.last().sliceable=!inlineFrame;
      deck.frames.last().section=section+(subsection.isEmpty()?QString():QStringLiteral(" / ")+subsection);
      deck.frames.last().titlePage=QRegularExpression(QStringLiteral(R"(\\(?:titlepage|maketitle)(?![A-Za-z@]))")).match(body).hasMatch();
    }
    if(braces!=0||!envs.isEmpty())unsure();
    active.remove(filename);
  }
};
}

SlideDeck parseSlideDeck(const QString &root,const QHash<QString,QString> &buffers,const std::function<bool()> &cancelled) {
  Parser p;p.root=path(root);p.buffers=buffers;p.cancelled=cancelled;p.file(p.root);
  if(p.conditionalDepth)p.deck.uncertain=true;
  for(auto &f:p.deck.frames)if(f.kind==QStringLiteral("againframe"))for(const auto &original:p.deck.frames)
    if(original.kind!=QStringLiteral("againframe")&&original.label==f.label){f.title=original.title;f.titlePage=original.titlePage;break;}
  QSet<QString> ranges;for(const auto &f:p.deck.frames){const auto key=f.file+QLatin1Char(':')+QString::number(f.start);if(ranges.contains(key))p.deck.uncertain=true;ranges.insert(key);}
  // Repeated input executes one source range more than once. Editing a single
  // occurrence would also alter the others, so whole-slide operations are unsafe.
  QHash<QString,int> occurrences;for(const auto &f:p.deck.frames)++occurrences[f.file+QLatin1Char(':')+QString::number(f.start)];
  for(auto &f:p.deck.frames)if(occurrences.value(f.file+QLatin1Char(':')+QString::number(f.start))>1)f.editable=false;
  if(p.deck.uncertain) p.deck.reason=QStringLiteral("存在动态结构或不完整边界；仅列出可定位源码，PDF 映射停用。");
  if(!p.deck.beamer)p.deck.reason=QStringLiteral("主文件不是可确认的 Beamer 文档。请检查主文件设置。");
  return p.deck;
}

bool mapSlidePages(SlideDeck &deck,const QString &navigation,int pages,const QVector<QSet<int>> &sourcePages) {
  deck.mapped=false;for(auto &f:deck.frames)f.firstPage=f.lastPage=0;
  if(!deck.beamer||deck.uncertain||deck.frames.isEmpty()||pages<1||sourcePages.size()!=deck.frames.size())return false;
  static const QRegularExpression re(QStringLiteral(R"(\\beamer@framepages\s*\{(\d+)\}\s*\{(\d+)\})"));
  QVector<QPair<int,int>> ranges;auto it=re.globalMatch(navigation);int last=0;
  while(it.hasNext()) {
    auto m=it.next();int a=m.captured(1).toInt(),b=m.captured(2).toInt();
    if(b<a||b>pages)return false;
    // Beamer continuations write cumulative ranges (6/6, 6/7, 6/8), not
    // independent frames. Retain their one logical range.
    if(!ranges.isEmpty()&&a==ranges.last().first&&b>=last){ranges.last().second=b;last=b;continue;}
    if(a!=last+1)return false;
    ranges.append({a,b});last=b;
  }
  if(last!=pages||ranges.isEmpty())return false;
  static const QRegularExpression total(QStringLiteral(R"(\\beamer@documentpages\s*\{(\d+)\})"));
  const auto count=total.match(navigation);if(!count.hasMatch()||count.captured(1).toInt()!=pages)return false;
  // Each physical range must be certified by SyncTeX at its literal frame's
  // closing line. Continuations belong to the same allowframebreaks source.
  int r=0;QVector<QPair<int,int>> grouped;
  for(int i=0;i<deck.frames.size();++i) {
    if(r>=ranges.size())return false;
    const int first=ranges[r].first;
    if(!sourcePages[i].contains(first))return false;
    int end=ranges[r++].second;
    while(deck.frames[i].breaks&&r<ranges.size()&&sourcePages[i].contains(ranges[r].first))end=ranges[r++].second;
    grouped.append({first,end});
  }
  if(r!=ranges.size())return false;
  for(int i=0;i<deck.frames.size();++i){deck.frames[i].firstPage=grouped[i].first;deck.frames[i].lastPage=grouped[i].second;}
  deck.mapped=true;return true;
}

bool editSlide(TeXDocumentWindow *w,const SlideDeck &deck,int index,SlideEdit operation,QString *reason) {
  auto reject=[&](const QString &why){if(reason)*reason=why;return false;};
  if(!w)return false;
  SlideSourceEdit sourceEdit(w);
  if(w->editor()->isReadOnly())return reject(QStringLiteral("源文件处于只读状态，不能执行整张编辑。"));
  if(operation==SlideEdit::Insert&&deck.frames.isEmpty()&&deck.beamer&&!deck.uncertain) {
    const auto file=path(w->fileName());const auto original=w->textDoc()->editorText();
    if(original!=deck.sources.value(file))return reject(QStringLiteral("源码正在更新，请稍后重试。"));
    const auto clean=maskProjectText(original);const auto m=QRegularExpression(QStringLiteral(R"(\\end\s*\{document\})")).match(clean);
    if(!m.hasMatch())return false;
    QTextCursor edit(w->editor()->document());edit.beginEditBlock();edit.setPosition(int(m.capturedStart()));edit.insertText(QStringLiteral("\\begin{frame}{}\n\n\\end{frame}\n\n"));edit.endEditBlock();
    edit.setPosition(int(m.capturedStart())+14);w->editor()->setTextCursor(edit);return true;
  }
  if(index<0||index>=deck.frames.size())return false;
  const auto &f=deck.frames[index];const auto file=path(w->fileName());
  if(!f.editable||file!=f.file)return reject(QStringLiteral("此张的源码范围无法确认，请直接编辑源码。"));
  w->flushScriptEdits();
  const auto original=w->textDoc()->editorText();
  if(original!=deck.sources.value(file))return reject(QStringLiteral("源码正在更新，请稍后重试。"));
  if(operation==SlideEdit::Duplicate&&f.labelled)return reject(QStringLiteral("此张含标签或锚点。为避免重复定义，复制已停用；请在源码中复制并自行命名。"));
  if(operation==SlideEdit::Delete&&!f.label.isEmpty())for(int i=0;i<deck.frames.size();++i)
    if(i!=index&&deck.frames[i].kind==QStringLiteral("againframe")&&deck.frames[i].label==f.label)
      return reject(QStringLiteral("此张被其他页面重复引用；请先在源码中处理引用。"));
  int a=f.start,b=f.end,cursor=a;QString replacement;
  if(operation==SlideEdit::Insert) {
    a=b;replacement=QStringLiteral("\n\\begin{frame}{}\n\n\\end{frame}\n");cursor=a+15;
  } else if(operation==SlideEdit::Duplicate) {
    a=b;replacement=original.mid(f.start,f.end-f.start);
    if(a>0&&original[a-1]!=QLatin1Char('\n'))replacement.prepend(QLatin1Char('\n'));
    cursor=a;
  } else if(operation==SlideEdit::Up||operation==SlideEdit::Down) {
    const int other=index+(operation==SlideEdit::Up?-1:1);
    if(other<0||other>=deck.frames.size())return false;
    const auto &g=deck.frames[other];
    if(g.file!=file||!g.editable)return reject(QStringLiteral("不能跨文件或动态边界移动；请在源码中处理。"));
    const auto &left=operation==SlideEdit::Up?g:f;const auto &right=operation==SlideEdit::Up?f:g;
    const auto gap=original.mid(left.end,right.start-left.end);
    // Moving across sections, labels, macros or input commands changes their
    // ownership. Only whitespace/comments between consecutive frames are safe.
    if(!maskProjectText(gap).trimmed().isEmpty())return reject(QStringLiteral("两张之间有章节、命令或文件边界；移动已停用。"));
    if(left.kind==QStringLiteral("againframe")||right.kind==QStringLiteral("againframe"))return reject(QStringLiteral("againframe 的顺序可能依赖标签定义；请在源码中移动。"));
    QString l=original.mid(left.start,left.end-left.start),r=original.mid(right.start,right.end-right.start);
    if(!r.endsWith(QLatin1Char('\n'))&&(!gap.isEmpty()||!l.isEmpty()))r+=QLatin1Char('\n');
    replacement=r+gap+l;a=left.start;b=right.end;cursor=operation==SlideEdit::Up?a:a+int(r.size()+gap.size());
  }
  if(a<0||b<a||b>original.size())return false;
  QTextCursor edit(w->editor()->document());edit.beginEditBlock();edit.setPosition(a);edit.setPosition(b,QTextCursor::KeepAnchor);edit.insertText(replacement);edit.endEditBlock();
  edit.setPosition(qMin(cursor,w->editor()->document()->characterCount()-1));w->editor()->setTextCursor(edit);
  return true;
}

bool editSlideSelection(TeXDocumentWindow *w,const SlideDeck &deck,QVector<int> rows,SlideEdit operation,int before,QString *reason,bool apply) {
  auto reject=[&](const QString &text){if(reason)*reason=text;return false;};
  if(!w||rows.isEmpty())return false;SlideSourceEdit sourceEdit(w);std::sort(rows.begin(),rows.end());rows.erase(std::unique(rows.begin(),rows.end()),rows.end());
  if(rows.first()<0||rows.last()>=deck.frames.size())return false;
  w->flushScriptEdits();const auto file=path(w->fileName());const auto original=w->textDoc()->editorText();
  if(w->editor()->isReadOnly()||original!=deck.sources.value(file))return reject(QStringLiteral("源码已变化或处于只读状态，请稍后重试。"));
  for(const int i:rows){const auto &f=deck.frames[i];if(!f.editable||f.file!=file)return reject(QStringLiteral("请选择同一文件中可以确认源码范围的幻灯片。"));
    if(operation==SlideEdit::Duplicate&&f.labelled)return reject(QStringLiteral("所选幻灯片含标签，复制会产生重名；请在源码中复制并重新命名。"));
    if(operation==SlideEdit::Delete&&!f.label.isEmpty())for(int j=0;j<deck.frames.size();++j)if(!rows.contains(j)&&deck.frames[j].kind==QStringLiteral("againframe")&&deck.frames[j].label==f.label)return reject(QStringLiteral("所选幻灯片仍被其他幻灯片引用，请先处理引用。"));
  }
  struct Change{int start,end;QString text;};QVector<Change> changes;int destination=deck.frames[rows.first()].start;
  if(operation==SlideEdit::Delete){for(const int i:rows)changes.append({deck.frames[i].start,deck.frames[i].end,QString()});}
  else if(operation==SlideEdit::Duplicate){QString content;for(const int i:rows){const auto &f=deck.frames[i];content+=original.mid(f.start,f.end-f.start);if(!content.endsWith(QLatin1Char('\n')))content+=QLatin1Char('\n');}const int at=deck.frames[rows.last()].end;changes.append({at,at,QLatin1Char('\n')+content});destination=at+1;}
  else if(operation==SlideEdit::Up||operation==SlideEdit::Down){
    const int first=rows.first(),last=rows.last();if(rows.size()!=last-first+1)return reject(QStringLiteral("移动时请选择连续的幻灯片。"));
    if(before<0)before=operation==SlideEdit::Up?first-1:last+2;
    if(before<0||before>deck.frames.size())return false;if(before>=first&&before<=last+1)return false;
    const int lo=qMin(first,before),hi=qMax(last,before-1);
    for(int i=lo;i<=hi;++i){const auto &f=deck.frames[i];if(f.file!=file||!f.editable||f.kind==QStringLiteral("againframe"))return reject(QStringLiteral("不能跨文件、动态结构或重复引用移动。"));if(i<hi&&!maskProjectText(original.mid(f.end,deck.frames[i+1].start-f.end)).trimmed().isEmpty())return reject(QStringLiteral("不能跨章节或其他命令移动；请在源码中调整。"));}
    const int a=deck.frames[first].start,b=deck.frames[last].end;QString moved=original.mid(a,b-a);if(!moved.endsWith(QLatin1Char('\n')))moved+=QLatin1Char('\n');
    if(before<first){const int at=deck.frames[before].start;changes.append({at,b,moved+original.mid(at,a-at)});destination=at;}
    else {const int at=deck.frames[before-1].end;const auto intervening=original.mid(b,at-b);changes.append({a,at,intervening+QLatin1Char('\n')+moved});destination=a+int(intervening.size())+1;}
  }else return false;
  if(!apply)return true;
  QTextCursor edit(w->editor()->document());edit.beginEditBlock();for(int i=changes.size()-1;i>=0;--i){const auto &c=changes[i];edit.setPosition(c.start);edit.setPosition(c.end,QTextCursor::KeepAnchor);edit.insertText(c.text);}edit.endEditBlock();edit.setPosition(qMin(destination,w->editor()->document()->characterCount()-1));w->editor()->setTextCursor(edit);return true;
}

namespace {
#include "ResearchSlideMotion.inc"
class SlideDragDelegate final : public QStyledItemDelegate {
public:
  using QStyledItemDelegate::QStyledItemDelegate;
  QRect thumbnailRect(const QStyleOptionViewItem &option,const QModelIndex &index) const {
    auto o=option;initStyleOption(&o,index);
    return (o.widget?o.widget->style():QApplication::style())->subElementRect(QStyle::SE_ItemViewItemDecoration,&o,o.widget);
  }
  void paint(QPainter *p,const QStyleOptionViewItem &option,const QModelIndex &index) const override {
    p->save();if(parent()->property("researchSlideDragging").toList().contains(index.row()))p->setOpacity(0.25);
    QStyledItemDelegate::paint(p,option,index);p->restore();
  }
};
class SlideList final : public QListWidget {
public:
  explicit SlideList(QWidget *parent):QListWidget(parent),animation(this),gapTick(this){
    reflow=new SlideReflow(this);
    setDropIndicatorShown(false);
    animation.setEasingCurve(QEasingCurve::OutCubic);
    gapTick.setTimerType(Qt::PreciseTimer);connect(&gapTick,&QTimer::timeout,this,[this]{advanceGap();});
    connect(&animation,&QVariantAnimation::valueChanged,this,[this](const QVariant &v){
      const qreal t=v.toReal();if(viewMode()==QListView::IconMode){placeGrid(t);return;}
      QVariantMap gaps;for(auto it=from.begin();it!=from.end();++it)gaps.insert(it.key(),it.value().toReal()*(1-t));if(target>=0){const auto key=target==count()?QStringLiteral("end"):QString::number(target);gaps[key]=gaps.value(key).toReal()+14*t;}setProperty("researchSlideGaps",gaps);doItemsLayout();viewport()->update();
    });
    connect(&animation,&QVariantAnimation::finished,this,[this]{gapTick.stop();if(viewMode()==QListView::IconMode&&target<0)resetDragPreview();});
    connect(model(),&QAbstractItemModel::modelAboutToBeReset,this,[this]{resetDragPreview();});
    connect(model(),&QAbstractItemModel::rowsAboutToBeRemoved,this,[this]{resetDragPreview();});
    connect(model(),&QAbstractItemModel::rowsAboutToBeInserted,this,[this]{resetDragPreview();});
    hint=new QLabel(viewport());hint->setObjectName(QStringLiteral("researchSlideDragHint"));hint->setWordWrap(true);hint->setStyleSheet(QStringLiteral("background:#fff4ed;color:#92552f;padding:7px;border:1px solid #e6c7ae;border-radius:8px;"));hint->hide();
  }
  std::function<const QMimeData*()> readClipboard{[]{return QApplication::clipboard()->mimeData();}};
  std::function<void(QMimeData*)> writeClipboard{[](QMimeData *mime){QApplication::clipboard()->setMimeData(mime);}};
  std::function<bool(QKeyEvent*)> clipboardKey;
  bool event(QEvent *e)override{if(e->type()==QEvent::ShortcutOverride){auto *k=static_cast<QKeyEvent*>(e);if(clipboardKey&&(k->matches(QKeySequence::Copy)||k->matches(QKeySequence::Cut)||k->matches(QKeySequence::Paste)||k->matches(QKeySequence::Undo)||k->matches(QKeySequence::Redo))){e->accept();return true;}}return QListWidget::event(e);}
  void keyPressEvent(QKeyEvent *k)override{if(clipboardKey&&clipboardKey(k))return;QListWidget::keyPressEvent(k);}
  std::function<bool(int)> dropBefore;
  SlideReflow *reflow{nullptr};
  ~SlideList() override {
    // QListWidget tears down its model after our members/children. Do not run
    // preview callbacks from that base-class teardown with a dead hint/animation.
    QObject::disconnect(model(),nullptr,this,nullptr);gapTick.stop();animation.stop();
  }
  std::function<QString(int)> dropProblem;
  std::function<quint64()> revision;
  void resetDragPreview() {
    gapTick.stop();animation.stop();target=-1;hint->hide();
    if(gridBase.isEmpty()){const bool hadGaps=!property("researchSlideGaps").toMap().isEmpty();from.clear();setProperty("researchSlideGaps",QVariant());if(hadGaps){QListWidget::doItemsLayout();viewport()->update();}return;}
    QVector<int> selected,hidden;for(int row=0;row<count();++row){if(item(row)->isSelected())selected<<row;if(item(row)->isHidden())hidden<<row;}
    const int current=currentRow(),scroll=verticalScrollBar()->value();
    gridBase.clear();gridFrom.clear();gridTo.clear();gridCurrent.clear();gridRows.clear();
    // Reset the view's temporary positions, retaining selection and filtering.
    const QSignalBlocker block(this);QListWidget::reset();for(int row=0;row<count();++row)item(row)->setHidden(hidden.contains(row));
    setCurrentRow(current,QItemSelectionModel::NoUpdate);for(int row:selected)item(row)->setSelected(true);
    QListWidget::doItemsLayout();verticalScrollBar()->setValue(scroll);viewport()->update();
  }
  void doItemsLayout() override {
    QListWidget::doItemsLayout();
    positionGrid();
  }
  int gridDropIndex(const QPoint &point) const {
    // Hit-test the stable slots, not cards moving beneath a stationary pointer.
    const QPoint pos=point+QPoint(horizontalOffset(),verticalOffset());
    int before=count();qint64 best=std::numeric_limits<qint64>::max();
    int bottom=-1;for(int row:gridRows)bottom=qMax(bottom,qRound(gridBase.value(row).y())+gridSize().height());
    if(!gridRows.isEmpty()&&pos.y()>bottom)return gridRows.last()+1;
    for(int row:gridRows){const QRect rect(gridBase.value(row).toPoint(),gridSize());
      const int dx=qMax(qMax(rect.left()-pos.x(),0),pos.x()-rect.right()),dy=qMax(qMax(rect.top()-pos.y(),0),pos.y()-rect.bottom());
      const qint64 distance=qint64(dx)*dx+qint64(dy)*dy;
      if(distance<best){best=distance;before=row+(pos.x()>rect.center().x()?1:0);}
    }
    return before;
  }
  void animateGap(int before) {
    if(target==before)return;gapTick.stop();animation.stop();from=property("researchSlideGaps").toMap();target=before;
    if(viewMode()==QListView::IconMode){
      if(gridBase.isEmpty()){
        if(before<0)return;captureGrid();
      }
      gridFrom.clear();for(int row:gridRows)gridFrom.insert(row,visualItemRect(item(row)).topLeft()+QPoint(horizontalOffset(),verticalOffset()));
      gridTo=gridBase;
      if(before>=0){QVector<int> moving,remaining;const auto dragging=property("researchSlideDragging").toList();
        for(int row:gridRows)(dragging.contains(row)?moving:remaining).append(row);
        int insertion=0;while(insertion<remaining.size()&&remaining[insertion]<before)++insertion;
        for(int i=0;i<moving.size();++i)remaining.insert(insertion+i,moving[i]);
        for(int i=0;i<remaining.size();++i)gridTo[remaining[i]]=gridBase.value(gridRows[i]);
      }
      if(!motionEnabled()){placeGrid(1);if(before<0)resetDragPreview();return;}
      startGapMotion();return;
    }
    if(!motionEnabled()){QVariantMap gaps;if(before>=0)gaps.insert(before==count()?QStringLiteral("end"):QString::number(before),14);setProperty("researchSlideGaps",gaps);doItemsLayout();viewport()->update();return;}
    startGapMotion();
  }
protected:
  void resizeEvent(QResizeEvent *event) override {resetDragPreview();QListWidget::resizeEvent(event);doItemsLayout();}
  void hideEvent(QHideEvent *event) override {resetDragPreview();QListWidget::hideEvent(event);}
  void paintEvent(QPaintEvent *event) override {
    if(gapTick.isActive()&&gapAge.elapsed()>=animation.duration())advanceGap();
    QListWidget::paintEvent(event);if(target<0)return;
    QPainter painter(viewport());painter.setPen(QPen(QColor(QStringLiteral("#383c40")),2,Qt::SolidLine,Qt::FlatCap));
    if(viewMode()==QListView::IconMode){
      const auto rows=property("researchSlideDragging").toList();if(rows.isEmpty())return;
      int first=count();for(const auto &row:rows)first=qMin(first,row.toInt());if(!gridTo.contains(first))return;
      const auto index=model()->index(first,0);auto *delegate=dynamic_cast<SlideDragDelegate*>(itemDelegateForIndex(index));if(!delegate)return;
      QStyleOptionViewItem option;initViewItemOption(&option);option.rect=visualItemRect(item(first));option.rect.moveTopLeft(gridTo.value(first).toPoint()-QPoint(horizontalOffset(),verticalOffset()));
      const QRect thumbnail=delegate->thumbnailRect(option,index);
      // Align with the thumbnail itself, excluding the caption and cell padding.
      const int gutter=qMax(4,(gridSize().width()-thumbnail.width())/2);
      const int x=qBound(2,thumbnail.left()-gutter,qMax(2,viewport()->width()-2));painter.drawLine(x,thumbnail.top(),x,thumbnail.bottom());
    }else if(count()){
      const auto gaps=property("researchSlideGaps").toMap();int y=0;
      if(target<count()){const auto rect=visualItemRect(item(target));y=rect.top()+gaps.value(QString::number(target)).toInt()/2;}
      else {const auto rect=visualItemRect(item(count()-1));y=rect.bottom()-gaps.value(QStringLiteral("end")).toInt()/2;}
      painter.drawLine(8,y,qMax(8,viewport()->width()-8),y);
    }
  }
  void startDrag(Qt::DropActions) override {
    // A filter can hide a selected/current row. It must never become an
    // invisible participant in a subsequent Ctrl-selection and native drag.
    for(auto *item:selectedItems())if(item->isHidden())item->setSelected(false);
    if(selectedItems().isEmpty())return;resetDragPreview();dragCommitted=false;dragRevision=revision?revision():0;QVariantList rows;for(auto *item:selectedItems())rows.append(row(item));setProperty("researchSlideDragging",rows);
    const QSize cardSize(qMin(270,viewport()->width()),64);const QRect cardRect(QPoint(),cardSize);QPixmap card(cardSize*devicePixelRatioF());card.setDevicePixelRatio(devicePixelRatioF());card.fill(Qt::transparent);QPainter painter(&card);painter.setRenderHint(QPainter::Antialiasing);painter.setPen(Qt::NoPen);painter.setBrush(QColor(32,50,70,26));painter.drawRoundedRect(cardRect.adjusted(4,5,-1,-1),6,6);painter.setBrush(QColor(QStringLiteral("#f6f9ff")));painter.setPen(QColor(QStringLiteral("#9cb5cf")));painter.drawRoundedRect(cardRect.adjusted(0,0,-5,-6),6,6);painter.setPen(QColor(QStringLiteral("#293746")));auto font=smoothTextFont(painter.font());font.setPixelSize(14);painter.setFont(font);const QString label=selectedItems().size()>1?ui(QStringLiteral("%1 张幻灯片")).arg(selectedItems().size()):selectedItems().first()->text().section(QLatin1Char('\n'),0,0);painter.drawText(cardRect.adjusted(12,8,-14,-12),Qt::AlignVCenter,QFontMetrics(font).elidedText(label,Qt::ElideRight,cardSize.width()-26));painter.end();
    auto *drag=new QDrag(this);drag->setMimeData(model()->mimeData(selectedIndexes()));drag->setPixmap(card);drag->setHotSpot(QPoint(20,20));viewport()->update();drag->exec(Qt::MoveAction,Qt::MoveAction);drag->deleteLater();setProperty("researchSlideDragging",QVariant());hint->hide();if(!dragCommitted||viewMode()!=QListView::IconMode)animateGap(-1);viewport()->update();
  }
  void dragMoveEvent(QDragMoveEvent *event) override {
    if(viewMode()==QListView::IconMode)QAbstractItemView::dragMoveEvent(event);else QListWidget::dragMoveEvent(event);if(event->source()!=this){event->ignore();return;}
    int before=count();if(viewMode()==QListView::IconMode){if(gridBase.isEmpty())captureGrid();before=gridDropIndex(event->position().toPoint());}
    else if(auto *item=itemAt(event->position().toPoint())){before=row(item);const auto rect=visualItemRect(item);const int gap=property("researchSlideGaps").toMap().value(QString::number(before)).toInt();if(before==target&&event->position().y()<rect.top()+gap)return;if(event->position().y()>rect.top()+gap+(rect.height()-gap)/2)++before;}
    const auto problem=revision&&revision()!=dragRevision?ui(QStringLiteral("源码已变化，请重新拖动")):(dropProblem?dropProblem(before):QString());if(!problem.isEmpty()){event->ignore();hint->setText(problem);hint->setFixedWidth(qMax(100,viewport()->width()-16));hint->adjustSize();hint->move(8,qMax(8,viewport()->height()-hint->height()-8));hint->show();hint->raise();animateGap(-1);return;}
    hint->hide();animateGap(before);event->acceptProposedAction();
  }
  void dragLeaveEvent(QDragLeaveEvent *event) override {QListWidget::dragLeaveEvent(event);hint->hide();animateGap(-1);}
  void dropEvent(QDropEvent *event) override {
    if(event->source()!=this||!dropBefore||(revision&&revision()!=dragRevision)){event->ignore();return;}
    const int before=target;hint->hide();if(before<0||(dropProblem&&!dropProblem(before).isEmpty())||!dropBefore(before)){animateGap(-1);event->ignore();return;}
    dragCommitted=true;if(viewMode()!=QListView::IconMode)animateGap(-1);event->acceptProposedAction();
  }
private:
  QVariantAnimation animation;QTimer gapTick;QElapsedTimer gapAge;QVariantMap from;int target{-1};QLabel *hint;quint64 dragRevision{0};
  void startGapMotion(){
    animation.setDuration(150);animation.setStartValue(0.0);animation.setEndValue(1.0);animation.start();animation.pause();
    // Retain Qt's interpolation/easing; one elapsed-time clock supplies frames
    // at this display's cadence instead of the default 16ms unified timer.
    gapAge.start();gapTick.start(motionFrameInterval(this));
  }
  void advanceGap(){
    if(!gapTick.isActive())return;const int elapsed=qMin(int(gapAge.elapsed()),animation.duration());
    if(elapsed>=animation.duration())gapTick.stop();animation.setCurrentTime(elapsed);
    if(elapsed>=animation.duration()){animation.stop();if(viewMode()==QListView::IconMode&&target<0)resetDragPreview();}
  }
  bool dragCommitted{false};QVector<int> gridRows;QHash<int,QPointF> gridBase,gridFrom,gridTo,gridCurrent;
  void captureGrid(){
    QListWidget::doItemsLayout();for(int row=0;row<count();++row)if(!item(row)->isHidden()){gridRows<<row;gridBase.insert(row,visualItemRect(item(row)).topLeft()+QPoint(horizontalOffset(),verticalOffset()));}
  }
  void placeGrid(qreal t){
    gridCurrent.clear();for(auto it=gridTo.cbegin();it!=gridTo.cend();++it){const auto start=gridFrom.value(it.key(),it.value());gridCurrent.insert(it.key(),start+(it.value()-start)*t);}positionGrid();viewport()->update();
  }
  void positionGrid(){
    if(gridCurrent.isEmpty())return;
    // The icon view permits programmatic positions. Our drag/drop handlers
    // only preview slots and commit source moves; no native free drop is used.
    for(auto it=gridCurrent.cbegin();it!=gridCurrent.cend();++it)if(it.key()<count()&&!item(it.key())->isHidden())setPositionForIndex(it.value().toPoint(),model()->index(it.key(),0));
  }
};
QThreadPool &slidePool() {
  static QThreadPool pool;
  static const bool init=[] {
    pool.setMaxThreadCount(1);pool.setExpiryTimeout(1500);
    // Workers use function-local parsers created after the pool. Drain them
    // before application shutdown, while those parsers are still alive.
    QObject::connect(qApp,&QCoreApplication::aboutToQuit,&pool,[]{pool.clear();pool.waitForDone();},Qt::DirectConnection);
    return true;
  }();
  Q_UNUSED(init);return pool;
}
QString hash(const QString &file,const std::function<bool()> &cancelled) {
  QFile f(file);if(!f.open(QIODevice::ReadOnly))return {};
  QCryptographicHash h(QCryptographicHash::Sha256);
  while(!f.atEnd()){if(cancelled())return {};const auto bytes=f.read(256*1024);if(bytes.isEmpty()&&f.error()!=QFile::NoError)return {};h.addData(bytes);}
  return QString::fromLatin1(h.result().toHex());
}
bool certified(const QJsonObject &meta,const QString &pdf,const QString &root,const std::function<bool()> &cancelled) {
  if(!managedPreview(pdf)||meta.value(QStringLiteral("source")).toString()!=root)return false;
  const auto inputs=meta.value(QStringLiteral("inputs")).toObject();if(inputs.isEmpty())return false;
  for(auto it=inputs.begin();it!=inputs.end();++it){const auto expected=it.value().toObject().value(QStringLiteral("hash")).toString();if(expected.size()!=64||hash(it.key(),cancelled)!=expected)return false;}
  const auto suffix=meta.value(QStringLiteral("syncSuffix")).toString();
  if(suffix!=QStringLiteral(".synctex")&&suffix!=QStringLiteral(".synctex.gz"))return false;
  if(meta.value(QStringLiteral("pdfHash")).toString().size()!=64||meta.value(QStringLiteral("syncHash")).toString().size()!=64)return false;
  return hash(pdf,cancelled)==meta.value(QStringLiteral("pdfHash")).toString()&&
    hash(pdf.left(pdf.size()-4)+suffix,cancelled)==meta.value(QStringLiteral("syncHash")).toString();
}
QVector<QSet<int>> syncPages(const SlideDeck &deck,const QString &pdf,const QString &root,const std::function<bool()> &cancelled) {
  QVector<QSet<int>> pages(deck.frames.size());
  struct Scanner : TWSyncTeXSynchronizer {
    using TWSyncTeXSynchronizer::TWSyncTeXSynchronizer;
    SyncTeX::synctex_scanner_p scanner() {return _scanner;}
  } sync(pdf,{}, {},QFileInfo(root).absolutePath());
  if(!sync.isValid())return pages;
  QHash<QString,QString> names;
  const QDir directory(QFileInfo(root).absolutePath());
  auto *scanner=sync.scanner();
  for(auto *node=SyncTeX::synctex_scanner_input(scanner);node;node=SyncTeX::synctex_node_sibling(node)) {
    if(cancelled())return {};
    const auto name=QString::fromLocal8Bit(SyncTeX::synctex_scanner_get_name(scanner,SyncTeX::synctex_node_tag(node)));
    names.insert(path(Tw::Utils::resolveSearchAlias(directory.absoluteFilePath(name))),name);
  }
  // Separate scanner, used exclusively on this worker. Line resolution does
  // not consult editors or render PDF pages.
  for(int i=0;i<deck.frames.size();++i) {
    if(cancelled())return {};
    const auto &f=deck.frames[i];
    const auto name=names.value(f.file);if(name.isEmpty())continue;
    // Unlike an ordinary jump, a nearby-line fallback is insufficient evidence
    // for assigning a whole physical page to a source frame.
    if(SyncTeX::synctex_display_query(scanner,name.toLocal8Bit().constData(),f.endLine,0,-1)>0)
      while(auto *node=SyncTeX::synctex_scanner_next_result(scanner))
        if(SyncTeX::synctex_node_line(node)==f.endLine)pages[i].insert(SyncTeX::synctex_node_page(node));
  }
  // Resolve aliases once per input tag, using the same canonical path helper
  // as the existing two-way synchronization.
  return pages;
}

QString slideCaption(const SlideFrame &frame) {
  QString title=frame.title;
  if(title.isEmpty())title=englishUi()?(frame.titlePage?QStringLiteral("Title slide"):QStringLiteral("Untitled")):(frame.titlePage?QStringLiteral("封面"):QStringLiteral("无标题"));
  if(frame.kind==QStringLiteral("againframe")) {
    if(title.startsWith(QStringLiteral("againframe: ")))title=frame.label;
    return (englishUi()?QStringLiteral("Repeat: %1"):Research::ui(QStringLiteral("再次显示：%1"))).arg(title);
  }
  return title;
}
QString slideDescription(const SlideFrame &frame) {
  QStringList parts;
  if(frame.kind==QStringLiteral("againframe"))parts.append(englishUi()?QStringLiteral("Reuses a labelled slide; edit the original to change its content."):Research::ui(QStringLiteral("重复引用已有幻灯片；内容修改请到原页。")));
  if(frame.lastPage>frame.firstPage&&frame.firstPage>0)parts.append((englishUi()?(frame.breaks?QStringLiteral("Content continues across %1 pages"):QStringLiteral("Progressive reveal in %1 steps")):(frame.breaks?QStringLiteral("内容续页 · %1 页"):QStringLiteral("逐步显示 · %1 步"))).arg(frame.lastPage-frame.firstPage+1));
  if(frame.backup)parts.append(englishUi()?QStringLiteral("Backup slide"):Research::ui(QStringLiteral("备用页")));
  if(!frame.section.isEmpty())parts.append(frame.section);
  return parts.join(QLatin1Char('\n'));
}
void fillSlideGroups(QComboBox *groups,const SlideDeck &deck,const QString &selected={}) {
  const QSignalBlocker block(groups);groups->clear();groups->addItem(englishUi()?QStringLiteral("All slides"):Research::ui(QStringLiteral("全部幻灯片")),QString());
  int backup=0;QStringList sections;
  for(const auto &f:deck.frames){backup+=f.backup;if(!f.section.isEmpty()&&!sections.contains(f.section))sections.append(f.section);}
  if(backup){groups->addItem((englishUi()?QStringLiteral("Main · %1 slides"):Research::ui(QStringLiteral("正文 · %1 张"))).arg(deck.frames.size()-backup),QStringLiteral("main"));groups->addItem((englishUi()?QStringLiteral("Backup · %1 slides"):Research::ui(QStringLiteral("备用页 · %1 张"))).arg(backup),QStringLiteral("backup"));}
  for(const auto &section:sections)groups->addItem(section,QStringLiteral("section:")+section);
  const int index=groups->findData(selected);if(index>=0)groups->setCurrentIndex(index);
}
bool matchesSlide(const SlideFrame &frame,int index,const QString &query,const QString &group) {
  const bool matches=query.isEmpty()||QString::number(index+1)==query||slideCaption(frame).contains(query,Qt::CaseInsensitive)||frame.section.contains(query,Qt::CaseInsensitive);
  return matches&&(group.isEmpty()||(group==QStringLiteral("main")&&!frame.backup)||(group==QStringLiteral("backup")&&frame.backup)||group==QStringLiteral("section:")+frame.section);
}
class SlideOverview final : public MaterialDialog {
public:
  SlideList *grid;int chosen{-1};std::function<void(SlideEdit)> editSlides;std::function<void(int)> openSlide;
  QVector<int> rows() const {QVector<int> values;for(auto *item:grid->selectedItems())if(!item->isHidden())values<<item->data(Qt::UserRole).toInt();std::sort(values.begin(),values.end());return values;}
  SlideOverview(QWidget *parent,const SlideDeck &deck,const QSharedPointer<QtPDF::Backend::Document> &pdf,bool previous,int selected,const QString &query={},const QString &group={}):MaterialDialog(parent){
    setObjectName(QStringLiteral("researchSlideOverview"));setWindowTitle(englishUi()?QStringLiteral("Slide overview"):Research::ui(QStringLiteral("幻灯片总览")));resize(940,650);auto *layout=new QVBoxLayout(this);auto *row=new QHBoxLayout;
    search=new QLineEdit(this);search->setObjectName(QStringLiteral("researchOverviewSearch"));search->setPlaceholderText(englishUi()?QStringLiteral("Find title, section or slide number"):Research::ui(QStringLiteral("搜索标题、章节或页码")));search->setClearButtonEnabled(true);configureField(search);search->setText(query);row->addWidget(search,1);
    groups=new QComboBox(this);groups->setObjectName(QStringLiteral("researchOverviewGroups"));fillSlideGroups(groups,deck,group);row->addWidget(groups);layout->addLayout(row);notice=new QLabel(this);notice->setWordWrap(false);notice->setMinimumWidth(0);notice->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);
    grid=new SlideList(this);grid->setObjectName(QStringLiteral("researchOverviewGrid"));grid->setViewMode(QListView::IconMode);grid->setResizeMode(QListView::Adjust);grid->setMovement(QListView::Free);grid->setWordWrap(true);grid->setIconSize(QSize(210,125));grid->setGridSize(QSize(225,178));grid->setSpacing(8);grid->setUniformItemSizes(true);grid->setItemDelegate(new SlideDragDelegate(grid));grid->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);grid->setSelectionMode(QAbstractItemView::ExtendedSelection);grid->setDragEnabled(true);grid->setAcceptDrops(true);grid->setDragDropMode(QAbstractItemView::InternalMove);grid->setDefaultDropAction(Qt::MoveAction);grid->setContextMenuPolicy(Qt::ActionsContextMenu);grid->setToolTip(englishUi()?QStringLiteral("Double-click to edit. Ctrl / Shift to select; drag to reorder."):Research::ui(QStringLiteral("双击编辑；Ctrl / Shift 多选；拖动排序。")));layout->addWidget(grid);
    QPixmap blank(210,125);blank.fill(QColor(QStringLiteral("#edf0f3")));placeholder=QIcon(blank);
    const QList<SlideEdit> operations{SlideEdit::Up,SlideEdit::Down,SlideEdit::Duplicate,SlideEdit::Delete};const QStringList names=englishUi()?QStringList{QStringLiteral("Move up"),QStringLiteral("Move down"),QStringLiteral("Duplicate"),QStringLiteral("Delete")}:QStringList{QStringLiteral("上移"),QStringLiteral("下移"),QStringLiteral("复制"),QStringLiteral("删除")};const QStringList keys{QStringLiteral("Alt+Up"),QStringLiteral("Alt+Down"),QStringLiteral("Ctrl+D"),QStringLiteral("Delete")};
    for(int i=0;i<operations.size();++i){auto *action=new QAction(names[i],grid);action->setObjectName(QStringLiteral("researchOverviewEdit%1").arg(i));action->setShortcut(QKeySequence(keys[i]));action->setShortcutContext(Qt::WidgetWithChildrenShortcut);grid->addAction(action);connect(action,&QAction::triggered,this,[this,op=operations[i]]{if(editSlides&&!rows().isEmpty())editSlides(op);});}
    auto *footer=new QHBoxLayout;count=new QLabel(this);count->setObjectName(QStringLiteral("researchOverviewCount"));footer->addWidget(count,1);footer->addWidget(notice,2);auto *edit=new QPushButton(englishUi()?QStringLiteral("Edit slide"):Research::ui(QStringLiteral("编辑此页")),this);footer->addWidget(edit);auto choose=[this]{const auto selected=rows();if(selected.size()!=1)return;chosen=selected[0];if(openSlide)openSlide(chosen);else accept();};connect(edit,&QPushButton::clicked,this,choose);connect(grid,&QListWidget::itemActivated,this,[choose](QListWidgetItem *){choose();});connect(grid,&QListWidget::itemSelectionChanged,this,[this,edit]{const int selected=rows().size();edit->setEnabled(selected==1);count->setText((englishUi()?QStringLiteral("%1 slides · %2 selected"):Research::ui(QStringLiteral("%1 张 · 已选 %2 张"))).arg(grid->count()).arg(selected));});
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Close,this);buttons->button(QDialogButtonBox::Close)->setText(englishUi()?QStringLiteral("Close"):Research::ui(QStringLiteral("关闭")));connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);footer->addWidget(buttons);layout->addLayout(footer);
    connect(search,&QLineEdit::textChanged,this,[this]{filter();});connect(groups,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{filter();});schedule.setSingleShot(true);connect(&schedule,&QTimer::timeout,this,[this]{renderVisible();});connect(grid->verticalScrollBar(),&QScrollBar::valueChanged,this,[this]{schedule.start(50);});grid->viewport()->installEventFilter(this);refresh(deck,pdf,previous,selected);
  }
  void filter(){grid->resetDragPreview();int visible=0;for(int i=0;i<grid->count();++i){auto *item=grid->item(i);const bool show=matchesSlide(currentDeck.frames[i],i,search->text().trimmed(),groups->currentData().toString());item->setHidden(!show);if(!show)item->setSelected(false);visible+=show;}count->setText((englishUi()?QStringLiteral("%1 / %2 slides"):Research::ui(QStringLiteral("%1 / %2 张"))).arg(visible).arg(grid->count()));schedule.start(30);}
  void refresh(const SlideDeck &deck,const QSharedPointer<QtPDF::Backend::Document> &pdf,bool previous,int selected){
    const auto keys=slideMotionKeys(deck),oldKeys=slideMotionKeys(currentDeck);QHash<QString,QIcon> thumbnails;
    // Unchanged cards retain their actual last-build thumbnails while source
    // edits invalidate page mapping. This is reading-only, with the existing
    // Previous build notice; no certified ranges or SyncTeX state are reused.
    if(document==pdf)for(int i=0;i<grid->count()&&i<oldKeys.size();++i)if(grid->item(i)->data(Qt::UserRole+2).toBool())thumbnails.insert(oldKeys[i],grid->item(i)->icon());
    grid->reflow->prepare(oldKeys,keys);
    cancelled->store(true);cancelled=std::make_shared<std::atomic<bool>>(false);rendering=false;retained.clear();setProperty("researchOverviewRendered",0);currentDeck=deck;document=pdf;const int scroll=grid->verticalScrollBar()->value();const auto group=groups->currentData().toString();fillSlideGroups(groups,deck,group);grid->clear();
    for(int i=0;i<deck.frames.size();++i){const auto &frame=deck.frames[i];const bool cached=thumbnails.contains(keys[i]);auto *item=new QListWidgetItem(cached?thumbnails.value(keys[i]):placeholder,QStringLiteral("%1  %2").arg(i+1).arg(slideCaption(frame)),grid);item->setData(Qt::UserRole,i);item->setData(Qt::UserRole+1,frame.firstPage-1);item->setData(Qt::UserRole+2,cached);if(cached)retained.append(i);item->setToolTip(item->text()+QLatin1Char('\n')+slideDescription(frame));}
    setProperty("researchOverviewRendered",retained.size());
    notice->setText(previous?(englishUi()?QStringLiteral("Previous build · compile to update"):Research::ui(QStringLiteral("上一版 · 编译后更新缩略图"))):(englishUi()?QStringLiteral("Compile to show thumbnails."):Research::ui(QStringLiteral("编译后显示缩略图。"))));notice->setToolTip(notice->text());notice->setVisible(previous||!document||!deck.mapped);if(layout())layout()->activate();filter();if(selected>=0&&selected<grid->count())grid->setCurrentRow(selected);grid->doItemsLayout();grid->verticalScrollBar()->setValue(scroll);grid->reflow->complete(keys);schedule.start(0);
  }
  ~SlideOverview() override {cancelled->store(true);}
protected:
  bool eventFilter(QObject *object,QEvent *event) override {if(object==grid->viewport()&&event->type()==QEvent::Resize)schedule.start(50);return MaterialDialog::eventFilter(object,event);}
private:
  SlideDeck currentDeck;QLineEdit *search;QComboBox *groups;QLabel *notice,*count;
  QTimer schedule;QIcon placeholder;QSharedPointer<QtPDF::Backend::Document> document;bool rendering{false};QList<int> retained;std::shared_ptr<std::atomic<bool>> cancelled{std::make_shared<std::atomic<bool>>(false)};
  bool visible(int i) const {return !grid->item(i)->isHidden()&&grid->visualItemRect(grid->item(i)).intersects(grid->viewport()->rect());}
  void renderVisible(){
    if(rendering||!document||!isVisible())return;
    for(int i=0;i<grid->count();++i){auto *item=grid->item(i);const int page=item->data(Qt::UserRole+1).toInt();if(page<0||retained.contains(i)||!visible(i))continue;
      // Evict off-screen images first. An unusually large viewport must not
      // continuously evict and re-render its own visible tiles.
      if(retained.size()>=24){int old=-1;for(int cached:retained)if(!visible(cached)){old=cached;break;}if(old<0)return;retained.removeOne(old);grid->item(old)->setIcon(placeholder);grid->item(old)->setData(Qt::UserRole+2,false);}
      rendering=true;const auto pdf=document;const auto stop=cancelled;const qreal scale=devicePixelRatioF();QPointer<SlideOverview> guard(this);
      slidePool().start(QRunnable::create([guard,pdf,stop,page,i,scale]{QImage image;if(!stop->load())if(auto p=pdf->page(page).toStrongRef()){const auto size=p->pageSizeF();if(size.width()>0&&size.height()>0){const qreal dpi=qMin(210.0/size.width(),125.0/size.height())*72*scale;image=p->renderToImage(dpi,dpi,QRect(),false);image.setDevicePixelRatio(scale);}}
        if(qApp)QMetaObject::invokeMethod(qApp,[guard,stop,image,i]{if(!guard||stop->load()||i>=guard->grid->count())return;guard->rendering=false;if(!image.isNull()){guard->grid->item(i)->setIcon(QIcon(QPixmap::fromImage(image)));guard->grid->item(i)->setData(Qt::UserRole+2,true);}guard->retained.append(i);guard->setProperty("researchOverviewRendered",guard->retained.size());guard->schedule.start(0);},Qt::QueuedConnection);
      }));return;
    }
  }
};

class SlideItemDelegate final : public QStyledItemDelegate {
  // Drag gaps alter row geometry, not title shaping. Keep the bounded text
  // measurements across those frames; title, width and font are all in the key.
  mutable QCache<QString,QStringList> titleLayouts{4*1024*1024};
  QStringList cachedTitleLines(const QString &text,int width,const QFont &font) const {
    const QString key=QString::number(width)+QChar(0)+font.key()+QChar(0)+text;
    if(const auto *cached=titleLayouts.object(key))return *cached;
    auto lines=titleLines(text,width,font);qsizetype units=key.size();for(const auto &line:lines)units+=line.size();
    const qsizetype cost=units*qsizetype(sizeof(QChar))+qsizetype(sizeof(QStringList))+lines.size()*qsizetype(sizeof(QString))+96;
    if(cost<=titleLayouts.maxCost())titleLayouts.insert(key,new QStringList(lines),int(cost));
    return lines;
  }
public:
  using QStyledItemDelegate::QStyledItemDelegate;
  static QStringList titleLines(const QString &text,int width,const QFont &font) {
    QTextLayout layout(text,font);QTextOption option;option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);layout.setTextOption(option);layout.beginLayout();QStringList lines;
    for(int i=0;i<3;++i){auto line=layout.createLine();if(!line.isValid())break;line.setLineWidth(qMax(24,width));const int start=line.textStart();QString value=text.mid(start,line.textLength()).trimmed();if(i==2&&start+line.textLength()<text.size())value=QFontMetrics(font).elidedText(text.mid(start),Qt::ElideRight,qMax(24,width));lines.append(value);}layout.endLayout();return lines;
  }
  QSize sizeHint(const QStyleOptionViewItem &option,const QModelIndex &index) const override {
    const auto *view=qobject_cast<QListWidget *>(parent());QFont font=smoothTextFont(option.font);font.setPixelSize(ControlMetrics::FontPixels);const int width=(view?view->viewport()->width()-2*view->spacing():210)-50;
    const auto gaps=parent()->property("researchSlideGaps").toMap();const int lines=qMax(1,cachedTitleLines(index.data(Qt::UserRole+10).toString(),width,font).size());
    return {160,16+lines*22+(index.data(Qt::UserRole+11).toString().isEmpty()?0:20)+gaps.value(QString::number(index.row())).toInt()+(index.row()==index.model()->rowCount()-1?gaps.value(QStringLiteral("end")).toInt():0)};
  }
  void paint(QPainter *p,const QStyleOptionViewItem &option,const QModelIndex &index) const override {
    auto o=option;const auto gaps=parent()->property("researchSlideGaps").toMap();const int gap=gaps.value(QString::number(index.row())).toInt(),tail=index.row()==index.model()->rowCount()-1?gaps.value(QStringLiteral("end")).toInt():0;
    p->save();if(gap)o.rect.setTop(o.rect.top()+gap);if(tail)o.rect.setBottom(o.rect.bottom()-tail);
    if(parent()->property("researchSlideDragging").toList().contains(index.row()))p->setOpacity(0.38);const bool selected=o.state.testFlag(QStyle::State_Selected);
    if(selected){p->fillRect(o.rect,QColor(QStringLiteral("#e5e7e9")));p->fillRect(QRect(o.rect.left(),o.rect.top(),2,o.rect.height()),QColor(QStringLiteral("#596168")));}
    else if(o.state.testFlag(QStyle::State_MouseOver))p->fillRect(o.rect,QColor(QStringLiteral("#eeeef1")));
    QFont font=smoothTextFont(o.font);font.setPixelSize(13);p->setFont(font);p->setPen(QColor(QStringLiteral("#51565b")));
    const QRect number=o.rect.adjusted(7,9,-o.rect.width()+33,-8);p->drawText(number,Qt::AlignTop|Qt::AlignRight,QString::number(index.row()+1));
    QRect rect=o.rect.adjusted(41,8,-9,-8);font.setPixelSize(ControlMetrics::FontPixels);p->setFont(font);p->setPen(QColor(QStringLiteral("#1d1d1f")));
    const auto lines=cachedTitleLines(index.data(Qt::UserRole+10).toString(),rect.width(),font);for(const auto &line:lines){p->drawText(rect,Qt::AlignTop|Qt::AlignLeft,line);rect.setTop(rect.top()+22);}
    font.setPixelSize(13);p->setFont(font);p->setPen(QColor(QStringLiteral("#51565b")));p->drawText(rect,Qt::AlignTop|Qt::AlignLeft,QFontMetrics(font).elidedText(index.data(Qt::UserRole+11).toString(),Qt::ElideRight,rect.width()));p->restore();
  }
};
void styleSlideButton(QToolButton *button,bool primary=false) {
  // Keep symbols for icon-only navigation and New. Short secondary captions
  // already identify these actions without repeating them with stock icons.
  if(QStringList{QStringLiteral("researchSlideSettings"),QStringLiteral("researchSlideNotesButton"),QStringLiteral("researchSlideOutputButton")}.contains(button->objectName())) {
    button->setIcon(QIcon());button->setToolButtonStyle(Qt::ToolButtonTextOnly);
  }
  button->setProperty("researchSlideEmphasis",primary?QStringLiteral("primary"):QStringLiteral("quiet"));configureButton(button,button->property("researchButtonRole")==QStringLiteral("menu")?ButtonRole::Menu:ButtonRole::Text);
}
class SlideController final : public QObject {
public:
  TeXDocumentWindow *window;
  QAction *toggle;
  QDockWidget *dock{nullptr};
  QDockWidget *previewDock{nullptr};
  QtPDF::PDFDocumentView *preview{nullptr};
  QVBoxLayout *previewLayout{nullptr};
  QPointer<QLabel> emptyPreview;
  QSharedPointer<QtPDF::PDFDocumentScene> previewScene;
  QString loadedRevision;
  bool notifyUpdate{false};
  QAction *directoryAction{nullptr},*previewAction{nullptr};
  QAction *expandPreviewAction{nullptr};QShortcut *previewEscape{nullptr};
  bool expandedPreview{false};QByteArray writingLayout;QPointer<QWidget> writingFocus;
  bool savedAutoDirectory{false},savedAutoPreview{false};
  int writingSlide{-1};
  void updatePreviewExpansion() {
    if(!expandPreviewAction)return;
    expandPreviewAction->setText(words(expandedPreview?QStringLiteral("返回编辑"):QStringLiteral("展开预览"),expandedPreview?QStringLiteral("Return to editing"):QStringLiteral("Expand preview")));
    expandPreviewAction->setIcon(QIcon(expandedPreview?QStringLiteral(":/research/icons/preview-restore.svg"):QStringLiteral(":/research/icons/preview-expand.svg")));
    expandPreviewAction->setToolTip(expandPreviewAction->text()+(expandedPreview?QStringLiteral(" · Esc"):QString()));
    expandPreviewAction->setChecked(expandedPreview);
  }
  void setExpandedPreview(bool on) {
    if(expandedPreview==on||!previewDock)return;
    if(on){
      writingLayout=window->saveState();writingFocus=QApplication::focusWidget();
      savedAutoDirectory=autoHiddenDirectory;savedAutoPreview=autoHiddenPreview;
      writingSlide=selected;
    }
    expandedPreview=on;window->setProperty("researchSlidePreviewExpanded",on);
    previewEscape->setEnabled(on);updatePreviewExpansion();
    if(on){
      window->centralWidget()->hide();dock->show();previewDock->show();
      window->resizeDocks({dock,previewDock},{228,qMax(320,window->width()-228)},Qt::Horizontal);
      preview->setFocus();
    }else{
      window->centralWidget()->show();window->restoreState(writingLayout);writingLayout.clear();
      autoHiddenDirectory=savedAutoDirectory;autoHiddenPreview=savedAutoPreview;
      if(writingFocus&&writingFocus->isVisible())writingFocus->setFocus();else window->editor()->setFocus();
      writingFocus=nullptr;
      if(enabled&&selected>=0&&selected!=writingSlide)select(selected,true);
    }
    QTimer::singleShot(0,this,[this]{if(preview&&previewScene)fitPreview();});
  }
  QListWidget *list{nullptr};
  QTreeWidget *documentList{nullptr},*chapterTree{nullptr};
  QMenu *documentMenu{nullptr},*filterMenu{nullptr};
  QToolButton *filterButton{nullptr};
  QWidget *searchBar{nullptr},*scopeBar{nullptr};
  void showSearch(bool on){if(!searchBar)return;scopeBar->hide();searchBar->setVisible(on);if(on){filter->setFocus();}else{filter->clear();groups->setCurrentIndex(0);if(expandedPreview&&preview)preview->setFocus();else window->editor()->setFocus();}}
  bool resumeAfterFocus=false,focusDirectory=false,focusPreview=false,focusNotes=false;
  QToolButton *fullSourceButton{nullptr};
  QLineEdit *filter{nullptr};QComboBox *groups{nullptr};
  QLabel *status{nullptr},*position{nullptr},*thumbnail{nullptr};
  QSpinBox *step{nullptr};
  QToolButton *previousStep{nullptr},*nextStep{nullptr};
  QToolButton *images{nullptr};
  QTimer parseTimer,thumbTimer;
  QFileSystemWatcher files;
  SlideDeck deck;
  bool enabled{false},busy{false},pending{false},dirty{false},stale{false},compiling{false},failed{false},thumbBusy{false},applying{false},inPreamble{false};
  int selected{-1},selectedStep{1};
  QString root,rememberFile,rememberTitle;
  QString documentFile,pendingGroup;
  bool hasPendingGroup{false};
  int pendingScroll{-1};
  int rememberStart{0};
  int preferredStart{-1};QString preferredFile;
  std::shared_ptr<std::atomic<quint64>> epoch{std::make_shared<std::atomic<quint64>>(0)};
  quint64 thumbSerial{0};
  QPointer<PDFDocumentWindow> viewer;
  QPointer<TeXDocumentWindow> editTarget;
  struct SourceView {QPointer<CompletingEdit> editor;QPointer<QTextDocument> doc;QTextCursor first,last;QString style,appliedStyle;bool partial{false};};
  QVector<SourceView> sourceViews;
  bool changingSourceView{false},fullSourceSelected{false};
  bool autoHiddenDirectory{false},autoHiddenPreview{false};
  QToolButton *openPdf{nullptr};
  QPointer<SlideOverview> overviewWindow;
  QPointer<QWidget> notesPane;QString notesFile;bool notesChanged{false},notesNavigate{false},notesCommitting{false};int pendingNotesStep{-1};QVector<QTextCursor> notesFrameAnchors;std::function<bool()> commitNotes;
  QCache<QString,QImage> cache{8*1024}; // KiB, at most eight selected-page images below.
  QStringList cacheKeys;
  QList<QMetaObject::Connection> activeConnections,pdfConnections;
  explicit SlideController(TeXDocumentWindow *w,QAction *action):QObject(w),window(w),toggle(action) {
    setObjectName(QStringLiteral("researchSlides"));
    parseTimer.setSingleShot(true);parseTimer.setInterval(500);
    thumbTimer.setSingleShot(true);thumbTimer.setInterval(80);
    connect(&parseTimer,&QTimer::timeout,this,[this]{request();});
    connect(&thumbTimer,&QTimer::timeout,this,[this]{render();});
    connect(&files,&QFileSystemWatcher::fileChanged,this,[this]{invalidate(false);});
    connect(&files,&QFileSystemWatcher::directoryChanged,this,[this]{invalidate(false);});
    connect(TWApp::instance(),&TWApp::updatedTranslators,this,[this]{
      updatePreviewExpansion();
      if(dock)dock->setWindowTitle(words(QStringLiteral("幻灯片列表"),QStringLiteral("Slide list")));
      if(auto *overview=window->findChild<QToolButton *>(QStringLiteral("researchSlideOverviewButton"))){overview->setText(words(QStringLiteral("排序"),QStringLiteral("Arrange")));overview->setAccessibleName(words(QStringLiteral("查看并排列幻灯片"),QStringLiteral("View and arrange slides")));overview->setToolTip(overview->accessibleName());configureButton(overview,ButtonRole::Text);}
      if(filter)filter->setPlaceholderText(words(QStringLiteral("筛选标题或页码"),QStringLiteral("Filter titles or slide numbers")));
      if(enabled)if(auto *a=window->findChild<QAction*>(QStringLiteral("researchOutlineAction")))a->setText(words(QStringLiteral("幻灯片列表"),QStringLiteral("Slide list")));
    });
  }
  ~SlideController() override {epoch->fetch_add(1);releaseSourceViews();}
  QString words(const QString &zh,const QString &en)const {return englishUi()?en:Research::ui(zh);}
  TeXDocumentWindow *sourceWindow(const QString &file) const {return path(window->fileName())==file?window:TeXDocumentWindow::findDocument(file);}
  // Source windows keep their own document, undo stack, saving and recovery.
  // Carry the presentation root and view state so navigation never strands the
  // user in a chapter-only directory or requires a trip back to the main file.
  void carryContext(TeXDocumentWindow *target) {
    if(!target||target==window)return;
    target->textDoc()->setProperty("researchPresentationSource",target->textDoc()->absoluteFilePath());
    target->textDoc()->setProperty("researchPresentationRoot",root);
    auto *action=target->findChild<QAction *>(QStringLiteral("researchSlideModeAction"));
    if(!action)return;
    action->setChecked(true);
    auto *other=dynamic_cast<SlideController *>(target->findChild<QObject *>(QStringLiteral("researchSlides")));
    if(!other)return;
    other->rememberFile=rememberFile;other->rememberStart=rememberStart;other->rememberTitle=rememberTitle;
    other->preferredFile=rememberFile;other->preferredStart=rememberStart;
    other->selected=selected;other->selectedStep=selectedStep;other->filter->setText(filter->text());
    other->pendingGroup=groups->currentData().toString();other->hasPendingGroup=true;
    other->pendingScroll=list->verticalScrollBar()->value();
    target->setGeometry(window->geometry());if(window->isMaximized()&&!target->isMaximized())target->showMaximized();else if(!window->isMaximized()&&target->isMaximized())target->showNormal();
    other->autoHiddenDirectory=autoHiddenDirectory;other->autoHiddenPreview=autoHiddenPreview;
    other->dock->setVisible(!dock->isHidden());other->previewDock->setVisible(!previewDock->isHidden());
    if(!dock->isHidden())target->resizeDocks({other->dock},{dock->width()},Qt::Horizontal);
    if(!previewDock->isHidden())target->resizeDocks({other->previewDock},{previewDock->width()},Qt::Horizontal);
    other->fullSourceSelected=fullSourceSelected;other->inPreamble=inPreamble;other->documentFile=documentFile;
    if(fullSourceSelected)other->showSource(target,0,0,true);
    other->invalidate(false);other->parseTimer.start(0);
  }
  SourceView &sourceView(TeXDocumentWindow *w) {
    for(auto &v:sourceViews)if(v.editor==w->editor())return v;
    SourceView v;v.editor=w->editor();v.doc=w->editor()->document();v.style=w->editor()->styleSheet();v.appliedStyle=v.style;sourceViews.append(v);w->editor()->installEventFilter(this);w->editor()->viewport()->installEventFilter(this);
    activeConnections.append(connect(w->editor(),&CompletingEdit::selectionChanged,this,[this,w]{
      if(!enabled||changingSourceView||w->editor()->property("researchSlideScopeTransaction").toBool())return;
      for(auto &v:sourceViews)if(v.editor==w->editor()&&v.partial) {
        const auto c=w->textCursor();if(c.hasSelection()&&(c.selectionStart()<v.first.position()||c.selectionEnd()>v.last.position())) {
          fullSourceSelected=true;showSource(w,0,0,true);updatePosition();
        }break;
      }
    }));
    return sourceViews.last();
  }
  void showSource(TeXDocumentWindow *w,int begin,int end,bool full=false) {
    if(!w||!enabled||w!=window)return;
    QScopedValueRollback<bool> change(changingSourceView,true);auto &v=sourceView(w);auto *doc=w->editor()->document();
    begin=qBound(0,begin,doc->characterCount()-1);end=qBound(begin,end,doc->characterCount()-1);
    if(!full&&end>begin&&doc->characterAt(end-1)==QChar::ParagraphSeparator)--end;
    v.partial=!full;v.first=QTextCursor(doc);v.first.setPosition(begin);v.first.setKeepPositionOnInsert(true);v.last=QTextCursor(doc);v.last.setPosition(end);
    const auto first=doc->findBlock(begin),last=doc->findBlock(end);bool changed=false;
    for(auto block=doc->begin();block.isValid();block=block.next()) {
      const bool visible=full||(block.position()>=first.position()&&block.position()<=last.position());
      if(block.isVisible()!=visible){block.setVisible(visible);changed=true;}
    }
    if(!full&&(w->cursorPosition()<begin||w->cursorPosition()>end)){auto c=w->textCursor();c.setPosition(begin);w->editor()->setTextCursor(c);}
    w->editor()->setProperty("researchSourceSlice",!full);
    w->editor()->setProperty("researchSourceScopeTitle",ui(begin==0&&end<=deck.preambleEnd.value(path(w->fileName()))?QStringLiteral("导言"):QStringLiteral("当前张")));
    Research::findSourceScopeChanged(w);
    if(changed)doc->markContentsDirty(0,doc->characterCount());
    w->editor()->viewport()->update();
  }
  void releaseSourceViews() {
    QScopedValueRollback<bool> change(changingSourceView,true);
    for(auto &v:sourceViews) {
      if(v.doc&&v.partial){v.partial=false;for(auto b=v.doc->begin();b.isValid();b=b.next())b.setVisible(true);v.doc->markContentsDirty(0,v.doc->characterCount());}
      if(v.editor){v.editor->setProperty("researchSourceSlice",false);if(auto *w=qobject_cast<TeXDocumentWindow *>(v.editor->window()))Research::findSourceScopeChanged(w);v.editor->viewport()->update();v.editor->removeEventFilter(this);v.editor->viewport()->removeEventFilter(this);if(v.editor->styleSheet()==v.appliedStyle)v.editor->setStyleSheet(v.style);}
    }
    sourceViews.clear();
  }
  void fitPreview() {
    if(!enabled||!previewScene||!preview)return;
    if(auto *page=previewScene->pageAt(preview->currentPage()))preview->zoomToRect(page->sceneBoundingRect().adjusted(-18,-18,18,18));
  }
  void createPreview() {
    preview=new QtPDF::PDFDocumentView(previewDock->widget());preview->setObjectName(QStringLiteral("researchSlidePreview"));preview->setMinimumWidth(200);preview->setFrameShape(QFrame::NoFrame);preview->setBackgroundBrush(QColor(QStringLiteral("#eeeef0")));preview->setSinglePageMode();preview->setMouseModeMove();previewLayout->addWidget(preview,1);preview->installEventFilter(this);installReferenceSlidePreview(window,preview);
    preview->setBackgroundRole(QPalette::Base);
    emptyPreview=new QLabel(preview->viewport());emptyPreview->setObjectName(QStringLiteral("researchSlideEmptyPreview"));emptyPreview->setAlignment(Qt::AlignCenter);emptyPreview->setWordWrap(true);emptyPreview->setStyleSheet(QStringLiteral("QLabel {background:#ffffff;color:#6e6e73;font-size:14px;padding:24px;}"));emptyPreview->setGeometry(preview->viewport()->rect());emptyPreview->show();preview->viewport()->installEventFilter(this);
    connect(preview,&QtPDF::PDFDocumentView::changedPage,this,[this](auto page){pageChanged(int(page));});
    connect(preview,&QtPDF::PDFDocumentView::contextClick,this,[this](auto page,const QPointF &point){reverse(int(page),point);});
  }
  bool eventFilter(QObject *object,QEvent *event) override {
    if(notesPane&&!notesPane->isHidden()&&(object==notesPane||notesPane->isAncestorOf(qobject_cast<QWidget *>(object)))&&(event->type()==QEvent::ShortcutOverride||event->type()==QEvent::KeyPress)&&static_cast<QKeyEvent *>(event)->key()==Qt::Key_Escape){
      if(event->type()==QEvent::KeyPress)hideNotes();event->accept();return true;
    }
    if(window->property("researchRestoreSlidePlace").isValid()&&(event->type()==QEvent::KeyPress||event->type()==QEvent::InputMethod||event->type()==QEvent::Wheel||event->type()==QEvent::MouseButtonPress))window->setProperty("researchRestoreSlidePlace",QVariant());
    if(chapterTree&&(object==chapterTree||object==chapterTree->viewport())&&(event->type()==QEvent::KeyPress||event->type()==QEvent::MouseButtonPress))chapterTree->setAnimated(motionEnabled());
    if(preview&&object==preview->viewport()&&event->type()==QEvent::Resize&&emptyPreview)emptyPreview->setGeometry(preview->viewport()->rect());
    if(enabled&&(object==window||object==window->editor()||object==window->editor()->viewport())&&(event->type()==QEvent::DragEnter||event->type()==QEvent::DragMove||event->type()==QEvent::Drop)){
      auto *drop=static_cast<QDropEvent *>(event);auto *viewport=window->editor()->viewport();const auto position=object==viewport?drop->position().toPoint():viewport->mapFrom(static_cast<QWidget *>(object),drop->position().toPoint());
      const auto urls=drop->mimeData()->urls();if(viewport->rect().contains(position)&&urls.size()==1&&urls.first().isLocalFile()){const auto file=urls.first().toLocalFile(),ext=QFileInfo(file).suffix().toLower();if(QStringList{QStringLiteral("png"),QStringLiteral("jpg"),QStringLiteral("jpeg"),QStringLiteral("pdf")}.contains(ext)){drop->acceptProposedAction();if(event->type()==QEvent::Drop){window->editor()->setTextCursor(window->editor()->cursorForPosition(position));queueImageDrop(file,window->textDoc()->editorText());}return true;}}
    }
    if(object->objectName()==QStringLiteral("researchInlineSlideTitle")&&event->type()==QEvent::ShortcutOverride){const int key=static_cast<QKeyEvent *>(event)->key();if(key==Qt::Key_Return||key==Qt::Key_Enter||key==Qt::Key_Escape){event->accept();return true;}}
    if(object->objectName()==QStringLiteral("researchInlineSlideTitle")&&(event->type()==QEvent::FocusOut||(event->type()==QEvent::KeyPress&&static_cast<QKeyEvent *>(event)->key()==Qt::Key_Escape))){static_cast<QWidget *>(object)->hide();object->setObjectName(QString());object->deleteLater();return event->type()==QEvent::KeyPress;}
    if(object==filter&&event->type()==QEvent::KeyPress&&static_cast<QKeyEvent *>(event)->key()==Qt::Key_Escape){showSearch(false);return true;}
    if(object==window&&event->type()==QEvent::Close){if(notesChanged&&commitNotes&&!commitNotes()){event->ignore();return true;}setExpandedPreview(false);}
    if(object==previewDock&&event->type()==QEvent::Close&&notesChanged&&commitNotes&&!commitNotes()){event->ignore();return true;}
    if(enabled&&!changingSourceView&&(event->type()==QEvent::KeyPress||event->type()==QEvent::ShortcutOverride)) {
      for(const auto &v:sourceViews)if(v.partial&&object==v.editor&&v.doc) {
        auto *key=static_cast<QKeyEvent *>(event);auto c=v.editor->textCursor();const int first=v.first.position(),last=v.last.position();
        const bool all=key->matches(QKeySequence::SelectAll),home=(key->modifiers()&Qt::ControlModifier)&&key->key()==Qt::Key_Home,end=(key->modifiers()&Qt::ControlModifier)&&key->key()==Qt::Key_End;
        if(event->type()==QEvent::ShortcutOverride){if(all||home||end){event->accept();return true;}break;}
        if(all){c.setPosition(first);c.setPosition(last,QTextCursor::KeepAnchor);v.editor->setTextCursor(c);return true;}
        if(home||end){c.setPosition(home?first:last,key->modifiers()&Qt::ShiftModifier?QTextCursor::KeepAnchor:QTextCursor::MoveAnchor);v.editor->setTextCursor(c);return true;}
        if(!c.hasSelection()&&((key->key()==Qt::Key_Backspace&&c.position()<=first)||(key->key()==Qt::Key_Delete&&c.position()>=last)))return true;
        if((!c.hasSelection()||(key->modifiers()&Qt::ShiftModifier))&&((key->key()==Qt::Key_Left&&c.position()<=first)||(key->key()==Qt::Key_Right&&c.position()>=last)))return true;
        if(!(key->modifiers()&(Qt::AltModifier|Qt::ControlModifier))&&(key->key()==Qt::Key_Up||key->key()==Qt::Key_Down)) {
          const auto block=c.block();const auto line=block.layout()->lineForTextPosition(c.positionInBlock());
          if(line.isValid()&&((key->key()==Qt::Key_Up&&block==v.doc->findBlock(first)&&line.lineNumber()==0)||
             (key->key()==Qt::Key_Down&&block==v.doc->findBlock(last)&&line.lineNumber()==block.layout()->lineCount()-1)))return true;
        }
        if(!c.hasSelection()&&(key->modifiers()&Qt::ControlModifier)&&(key->key()==Qt::Key_Backspace||key->key()==Qt::Key_Delete)) {
          auto word=c;word.movePosition(key->key()==Qt::Key_Backspace?QTextCursor::PreviousWord:QTextCursor::NextWord,QTextCursor::KeepAnchor);
          if(word.selectionStart()<first||word.selectionEnd()>last){c.setPosition(key->key()==Qt::Key_Backspace?first:last,QTextCursor::KeepAnchor);c.removeSelectedText();v.editor->setTextCursor(c);return true;}
        }
        break;
      }
    }
    if(enabled&&!expandedPreview&&object==window&&event->type()==QEvent::Resize) {
      if(previewDock){if(window->width()<960&&!previewDock->isHidden()){if(!notesChanged||!commitNotes||commitNotes()){autoHiddenPreview=true;previewDock->hide();}}else if(window->width()>=960&&autoHiddenPreview){autoHiddenPreview=false;previewDock->show();}}
      if(dock){if(window->width()<620&&!dock->isHidden()){autoHiddenDirectory=true;dock->hide();}else if(window->width()>=620&&autoHiddenDirectory){autoHiddenDirectory=false;dock->show();}}
    }
    if(enabled&&object==preview&&event->type()==QEvent::Resize&&previewScene)
      QTimer::singleShot(0,this,[this]{fitPreview();});
    return QObject::eventFilter(object,event);
  }
  void setup() {
    if(dock)return;
    dock=new QDockWidget(window);dock->setObjectName(QStringLiteral("researchSlideDock"));
    dock->setStyleSheet(QStringLiteral("QDockWidget {font-size:14px;} QDockWidget::title {background:#f5f5f7;padding:8px 10px;border-bottom:1px solid #dedee3;}"));
    dock->setWindowTitle(words(QStringLiteral("幻灯片列表"),QStringLiteral("Slide list")));
    dock->setFeatures(QDockWidget::DockWidgetClosable);dock->setAllowedAreas(Qt::LeftDockWidgetArea|Qt::RightDockWidgetArea);
    auto *body=new QWidget(dock);auto *layout=new QVBoxLayout(body);layout->setContentsMargins(10,8,10,8);layout->setSpacing(8);
    auto *documentButton=new QToolButton(body);documentButton->setObjectName(QStringLiteral("researchSlideSettings"));documentButton->setText(words(QStringLiteral("文档设置"),QStringLiteral("Document settings")));auto *settingsAction=new QAction(documentButton->text(),documentButton);settingsAction->setObjectName(QStringLiteral("researchDocumentOptionsAction"));connect(settingsAction,&QAction::triggered,this,[this]{documentOptions();});documentButton->setDefaultAction(settingsAction);documentButton->setIcon(QIcon(QStringLiteral(":/research/icons/settings.svg")));documentButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);configureButton(documentButton,ButtonRole::Menu);documentMenu=new QMenu(documentButton);documentButton->setMenu(documentMenu);documentButton->setPopupMode(QToolButton::MenuButtonPopup);layout->addWidget(documentButton);
    documentList=new QTreeWidget(body);documentList->setObjectName(QStringLiteral("researchSlideDocument"));documentList->setHeaderHidden(true);documentList->setRootIsDecorated(false);documentList->setIndentation(0);documentList->setMaximumHeight(126);documentList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);documentList->setStyleSheet(QStringLiteral("QTreeWidget {border:none;border-bottom:1px solid #dedee3;} QTreeWidget::item {padding:5px 7px;}"));layout->addWidget(documentList);
    connect(documentList,&QTreeWidget::itemClicked,this,[this](QTreeWidgetItem *item,int){
      const int i=item->data(0,Qt::UserRole).toInt();if(i<0||i>=deck.document.size())return;
      if(notesChanged&&commitNotes&&!commitNotes())return;
      const auto &entry=deck.document[i];auto *target=sourceWindow(entry.file);
      documentFile=entry.file;
      if(!target)target=TeXDocumentWindow::openDocument(entry.file,false,false);
      const bool full=entry.title==QStringLiteral("完整源码");fullSourceSelected=full;
      inPreamble=!full;
      if(target){carryContext(target);showSource(target,0,deck.preambleEnd.value(entry.file,target->editor()->document()->characterCount()-1),full);target->goToLine(entry.line,0,0);if(target!=window)target->selectWindow();}
      QSignalBlocker b(list);list->clearSelection();
      inPreamble=!full;updatePosition();
    });
    documentList->hide();
    scopeBar=new QWidget(body);scopeBar->setObjectName(QStringLiteral("researchSourceNavigation"));auto *scopeLayout=new QHBoxLayout(scopeBar);scopeLayout->setContentsMargins(0,0,0,0);scopeLayout->setSpacing(2);
    auto scopeButton=[&](const QString &name,const QString &zh,const QString &en,bool checkable){auto *button=new QToolButton(scopeBar);button->setObjectName(name);button->setText(words(zh,en));button->setCheckable(checkable);configureButton(button,ButtonRole::Text);button->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);scopeLayout->addWidget(button);return button;};
    auto *preambleButton=scopeButton(QStringLiteral("researchSourcePreamble"),QStringLiteral("导言"),QStringLiteral("Preamble"),false);
    fullSourceButton=scopeButton(QStringLiteral("researchSourceFull"),QStringLiteral("返回单页"),QStringLiteral("Back to slide"),false);fullSourceButton->setText(words(QStringLiteral("全文"),QStringLiteral("Full source")));
    preambleButton->setToolTip(words(QStringLiteral("直接编辑主文件导言"),QStringLiteral("Edit the main file's preamble")));
    connect(fullSourceButton,&QToolButton::clicked,this,[this]{setExpandedPreview(false);if(notesChanged&&commitNotes&&!commitNotes()){updatePosition();return;}fullSourceSelected=!fullSourceSelected;inPreamble=false;if(fullSourceSelected)showSource(window,0,0,true);else if(selected>=0)select(selected,true);else showSource(window,0,0,true);updatePosition();window->editor()->setFocus();});
    connect(preambleButton,&QToolButton::clicked,this,[this]{setExpandedPreview(false);if(notesChanged&&commitNotes&&!commitNotes())return;for(int i=0;i<deck.document.size();++i)if(deck.document[i].file==root&&deck.document[i].title==QStringLiteral("导言")){documentList->itemClicked(documentList->topLevelItem(i),0);if(auto *target=sourceWindow(root))target->editor()->setFocus();break;}});
    layout->addWidget(scopeBar);
    auto *searchButton=new QToolButton(scopeBar);searchButton->setObjectName(QStringLiteral("researchSlideSearchButton"));searchButton->setIcon(QIcon(QStringLiteral(":/research/icons/find.svg")));configureButton(searchButton,ButtonRole::Icon);searchButton->setToolTip(words(QStringLiteral("筛选幻灯片"),QStringLiteral("Filter slides")));searchButton->setAccessibleName(searchButton->toolTip());scopeLayout->addWidget(searchButton);connect(searchButton,&QToolButton::clicked,this,[this]{showSearch(true);});
    searchBar=new QWidget(body);searchBar->setObjectName(QStringLiteral("researchSlideSearchBar"));layout->addWidget(searchBar);searchBar->hide();
    filter=new QLineEdit(body);filter->setObjectName(QStringLiteral("researchSlideFilter"));filter->setPlaceholderText(words(QStringLiteral("筛选标题或页码"),QStringLiteral("Filter titles or slide numbers")));filter->setToolTip(words(QStringLiteral("筛选左侧幻灯片列表；Ctrl+F 查找源码"),QStringLiteral("Filter the slide list; Ctrl+F searches source")));filter->setClearButtonEnabled(true);configureField(filter);auto *searchRow=new QHBoxLayout(searchBar);searchRow->setContentsMargins(0,0,0,0);searchRow->setSpacing(2);searchRow->addWidget(filter,1);filter->installEventFilter(this);
    groups=new QComboBox(body);groups->setObjectName(QStringLiteral("researchSlideGroups"));groups->hide();
    filterButton=new QToolButton(body);filterButton->setObjectName(QStringLiteral("researchSlideFilterButton"));filterButton->setText(words(QStringLiteral("筛选"),QStringLiteral("Filter")));filterButton->setIcon(QIcon(QStringLiteral(":/research/icons/codicons/filter.svg")));filterButton->setToolButtonStyle(Qt::ToolButtonIconOnly);configureButton(filterButton,ButtonRole::Icon);filterButton->setStyleSheet(filterButton->styleSheet()+QStringLiteral("QToolButton::menu-indicator {image:none;width:0px;}"));filterButton->setAccessibleName(filterButton->text());filterMenu=new QMenu(filterButton);filterButton->setMenu(filterMenu);filterButton->setPopupMode(QToolButton::InstantPopup);searchRow->addWidget(filterButton);
    auto *closeSearch=new QToolButton(searchBar);closeSearch->setObjectName(QStringLiteral("researchCloseSlideSearch"));closeSearch->setIcon(QIcon(QStringLiteral(":/research/icons/close.svg")));configureButton(closeSearch,ButtonRole::Icon);closeSearch->setToolTip(words(QStringLiteral("结束搜索"),QStringLiteral("Close search")));searchRow->addWidget(closeSearch);connect(closeSearch,&QToolButton::clicked,this,[this]{showSearch(false);});
    connect(filterMenu,&QMenu::aboutToShow,this,[this]{filterMenu->clear();auto *chapters=filterMenu->addAction(words(QStringLiteral("按章节浏览"),QStringLiteral("Browse sections")));chapters->setObjectName(QStringLiteral("researchBrowseSections"));chapters->setCheckable(true);chapters->setChecked(chapterTree&&!chapterTree->isHidden());connect(chapters,&QAction::toggled,this,[this](bool on){chapterTree->setVisible(on);list->setVisible(!on);});filterMenu->addSeparator();auto *exclusive=new QActionGroup(filterMenu);exclusive->setExclusive(true);connect(filterMenu,&QMenu::aboutToHide,exclusive,&QObject::deleteLater);for(int i=0;i<groups->count();++i){auto *choice=filterMenu->addAction(groups->itemText(i),this,[this,i]{groups->setCurrentIndex(i);});choice->setObjectName(QStringLiteral("researchSlideGroup%1").arg(i));choice->setCheckable(true);choice->setChecked(i==groups->currentIndex());exclusive->addAction(choice);}});
    connect(groups,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{if(groups->currentIndex()>0)showSearch(true);filterButton->setToolTip(words(QStringLiteral("筛选：%1"),QStringLiteral("Filter: %1")).arg(groups->currentText()));filterButton->setStyleSheet(filterButton->styleSheet().section(QStringLiteral("/* active filter */"),0,0)+QStringLiteral("/* active filter */QToolButton {background:%1;}").arg(groups->currentIndex()>0?QStringLiteral("#e7e8ec"):QStringLiteral("transparent")));});
    auto *slides=new SlideList(body);list=slides;list->setObjectName(QStringLiteral("researchSlideList"));list->setMinimumWidth(140);list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);list->setTextElideMode(Qt::ElideRight);list->setSpacing(3);list->setMouseTracking(true);layout->addWidget(list,1);
    chapterTree=new QTreeWidget(body);chapterTree->setObjectName(QStringLiteral("researchSlideChapters"));chapterTree->setHeaderHidden(true);chapterTree->setAnimated(motionEnabled());chapterTree->setIndentation(14);chapterTree->setMinimumWidth(140);chapterTree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);chapterTree->setStyleSheet(QStringLiteral("QTreeWidget {background:transparent;border:none;font-size:15px;widget-animation-duration:150;} QTreeWidget::item {padding:6px 2px;}"));chapterTree->installEventFilter(this);chapterTree->viewport()->installEventFilter(this);chapterTree->hide();layout->addWidget(chapterTree,1);connect(chapterTree,&QTreeWidget::itemClicked,this,[this](QTreeWidgetItem *item,int){bool ok=false;const int row=item->data(0,Qt::UserRole).toInt(&ok);if(ok&&row>=0)list->setCurrentRow(row);});
    list->setSelectionMode(QAbstractItemView::ExtendedSelection);list->setDragDropMode(QAbstractItemView::InternalMove);list->setDefaultDropAction(Qt::MoveAction);list->setDropIndicatorShown(false);list->setAutoScrollMargin(28);
    slides->revision=[this]{return epoch->load();};
    slides->dropProblem=[this](int before){for(const int row:selection())if(list->item(row)->isHidden())return words(QStringLiteral("筛选已变化，请重新拖动"),QStringLiteral("The filter changed. Start a new drag."));if(dirty||busy)return words(QStringLiteral("正在更新目录"),QStringLiteral("Updating slides"));const auto rows=selection();if(rows.isEmpty())return words(QStringLiteral("请先选择幻灯片"),QStringLiteral("Select slides first"));auto *target=sourceWindow(deck.frames[rows.first()].file);QString why;if(editSlideSelection(target,deck,rows,SlideEdit::Down,before,&why,false))return QString();return words(why.isEmpty()?QStringLiteral("当前位置不变"):why,QStringLiteral("This move crosses a source boundary, dependency, or the current selection."));};
    slides->dropBefore=[this](int before){const auto rows=selection();if(rows.isEmpty())return false;auto *target=sourceWindow(deck.frames[rows.first()].file);QString why;if(!editSlideSelection(target,deck,rows,SlideEdit::Down,before,&why))return false;editTarget=target;preferredFile=path(target->fileName());preferredStart=target->cursorPosition();parseTimer.start(0);return true;};
    list->setItemDelegate(new SlideItemDelegate(list));
    connect(filter,&QLineEdit::textChanged,this,[this]{if(!filter->text().isEmpty()&&searchBar->isHidden())showSearch(true);filterSlides();});connect(groups,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{filterSlides();});
    connect(list,&QListWidget::itemDoubleClicked,this,[this](QListWidgetItem *item){renameSlide(list->row(item));});
    connect(list,&QListWidget::itemSelectionChanged,this,[this]{updateActions();});
    list->setStyleSheet(QStringLiteral("QListWidget {background:transparent;border:none;} QListWidget::item {border:none;}"));
    auto *row=new QHBoxLayout;row->addStretch();
    auto *slideActions=new QHBoxLayout;slideActions->setSpacing(4);layout->insertLayout(0,slideActions);
    auto *overview=new QToolButton(body);overview->setObjectName(QStringLiteral("researchSlideOverviewButton"));overview->setText(words(QStringLiteral("排序"),QStringLiteral("Arrange")));overview->setAccessibleName(words(QStringLiteral("查看并排列幻灯片"),QStringLiteral("View and arrange slides")));overview->setToolTip(overview->accessibleName());overview->setIcon(QIcon(QStringLiteral(":/research/icons/grid.svg")));overview->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);configureButton(overview,ButtonRole::Text);styleSlideButton(overview);slideActions->addWidget(overview);connect(overview,&QToolButton::clicked,this,[this]{showOverview();});
    layout->removeWidget(documentButton);row->insertWidget(0,documentButton);styleSlideButton(documentButton);
    images=new QToolButton(body);images->setObjectName(QStringLiteral("researchSlideThumbnailToggle"));images->setText(words(QStringLiteral("缩略图"),QStringLiteral("Thumbnail")));images->setCheckable(true);row->addWidget(images);layout->addLayout(row);
    thumbnail=new QLabel(body);thumbnail->setObjectName(QStringLiteral("researchSlideThumbnail"));thumbnail->setFixedHeight(140);thumbnail->setAlignment(Qt::AlignCenter);thumbnail->setAccessibleName(words(QStringLiteral("当前张预览"),QStringLiteral("Current slide preview")));thumbnail->hide();layout->addWidget(thumbnail);
    dock->setWidget(body);dock->setMaximumWidth(340);window->addDockWidget(Qt::LeftDockWidgetArea,dock);window->resizeDocks({dock},{228},Qt::Horizontal);
    previewDock=new QDockWidget(window);previewDock->setObjectName(QStringLiteral("researchSlidePreviewDock"));previewDock->setWindowTitle(words(QStringLiteral("当前张预览"),QStringLiteral("Current slide")));previewDock->setFeatures(QDockWidget::DockWidgetClosable);
    previewDock->setStyleSheet(QStringLiteral("QDockWidget {font-size:13px;} QDockWidget::title {background:#f5f5f7;padding:8px 10px;border-bottom:1px solid #dedee3;}"));
    auto *previewBody=new QWidget(previewDock);previewLayout=new QVBoxLayout(previewBody);previewLayout->setContentsMargins(0,0,0,0);previewLayout->setSpacing(0);
    auto *header=new QWidget(previewBody);auto *headerLayout=new QHBoxLayout(header);headerLayout->setContentsMargins(10,2,10,2);headerLayout->setSpacing(6);
    position=new QLabel(header);position->setObjectName(QStringLiteral("researchSlidePosition"));position->setWordWrap(true);headerLayout->addWidget(position,1);
    previousStep=new QToolButton(header);previousStep->setObjectName(QStringLiteral("researchSlidePreviousButton"));previousStep->setIcon(QIcon(QStringLiteral(":/research/icons/chevron-left.svg")));previousStep->setToolTip(words(QStringLiteral("上一步"),QStringLiteral("Previous step")));previousStep->setAccessibleName(previousStep->toolTip());configureButton(previousStep,ButtonRole::Icon);headerLayout->addWidget(previousStep);
    step=new QSpinBox(header);step->setObjectName(QStringLiteral("researchSlideStep"));step->setRange(1,1);step->setKeyboardTracking(false);step->setButtonSymbols(QAbstractSpinBox::NoButtons);step->setAlignment(Qt::AlignCenter);step->setFixedWidth(66);headerLayout->addWidget(step);
    nextStep=new QToolButton(header);nextStep->setObjectName(QStringLiteral("researchSlideNextButton"));nextStep->setIcon(QIcon(QStringLiteral(":/research/icons/chevron-right.svg")));nextStep->setToolTip(words(QStringLiteral("下一步"),QStringLiteral("Next step")));nextStep->setAccessibleName(nextStep->toolTip());configureButton(nextStep,ButtonRole::Icon);headerLayout->addWidget(nextStep);
    connect(previousStep,&QToolButton::clicked,step,&QSpinBox::stepDown);connect(nextStep,&QToolButton::clicked,step,&QSpinBox::stepUp);previewLayout->addWidget(header);
    auto *previewFooter=new QWidget(previewBody);auto *footerLayout=new QHBoxLayout(previewFooter);footerLayout->setContentsMargins(10,0,10,7);footerLayout->setSpacing(6);previewLayout->addWidget(previewFooter);
    status=new QLabel(previewFooter);status->setObjectName(QStringLiteral("researchSlideStatus"));status->setWordWrap(true);status->setTextInteractionFlags(Qt::TextSelectableByMouse);footerLayout->addWidget(status,1);
    openPdf=new QToolButton(previewFooter);openPdf->setObjectName(QStringLiteral("researchSlideOpenPdf"));openPdf->setText(words(QStringLiteral("打开完整 PDF"),QStringLiteral("Open complete PDF")));configureButton(openPdf,ButtonRole::Text);footerLayout->addWidget(openPdf);openPdf->hide();
    connect(openPdf,&QToolButton::clicked,this,[this]{if(auto *a=window->findChild<QAction *>(QStringLiteral("actionGo_to_Preview")))a->trigger();});
    previewDock->setWidget(previewBody);createPreview();window->addDockWidget(Qt::RightDockWidgetArea,previewDock);window->resizeDocks({previewDock},{qMax(320,(window->width()-220)/2)},Qt::Horizontal);
    directoryAction=new QAction(dock->windowTitle(),dock);directoryAction->setObjectName(QStringLiteral("researchSlideDirectoryAction"));previewAction=new QAction(previewDock->windowTitle(),previewDock);previewAction->setObjectName(QStringLiteral("researchSlidePreviewAction"));
    previewDock->installEventFilter(this);
    for(auto *panel:{dock,previewDock}){auto *action=panel==dock?directoryAction:previewAction;action->setCheckable(true);action->setChecked(!panel->isHidden());connect(action,&QAction::triggered,panel,[this,panel,action](bool on){if(!on&&panel==previewDock){if(notesChanged&&commitNotes&&!commitNotes()){QSignalBlocker block(action);action->setChecked(true);return;}setExpandedPreview(false);}setPanelVisible(panel,on);});connect(panel,&QDockWidget::visibilityChanged,action,&QAction::setChecked);connect(panel,&QDockWidget::windowTitleChanged,action,&QAction::setText);}
    auto titleBar=[this](QDockWidget *panel){auto *title=new QWidget(panel);auto *row=new QHBoxLayout(title);row->setContentsMargins(10,2,4,2);auto *label=new QLabel(panel->windowTitle(),title);label->setObjectName(QStringLiteral("researchSlidePanelTitle"));label->setStyleSheet(QStringLiteral("color:#1d1d1f;font-size:15px;font-weight:500;"));label->setMinimumWidth(0);connect(panel,&QDockWidget::windowTitleChanged,label,&QLabel::setText);row->addWidget(label,1);auto *close=new QToolButton(title);close->setIcon(QIcon(QStringLiteral(":/research/icons/close.svg")));configureButton(close,ButtonRole::Icon);close->setToolTip(words(QStringLiteral("收起面板"),QStringLiteral("Collapse panel")));connect(close,&QToolButton::clicked,panel,[panel]{setPanelVisible(panel,false);});row->addWidget(close);panel->setTitleBarWidget(title);};titleBar(dock);
    connect(previewDock,&QDockWidget::visibilityChanged,this,[this](bool visible){images->hide();if(visible){images->setChecked(false);thumbnail->hide();}});
    for(auto *a:window->menuBar()->actions())if(a->menu()&&a->menu()->actions().contains(toggle)){a->menu()->addAction(directoryAction);a->menu()->addAction(previewAction);break;}
    if(auto *menu=window->findChild<QMenu *>(QStringLiteral("researchReadingMenu"))){menu->addAction(directoryAction);menu->addAction(previewAction);}
    if(auto *menu=window->findChild<QMenu *>(QStringLiteral("researchModeMenu"))){menu->addSeparator();menu->addAction(directoryAction);menu->addAction(previewAction);}
    connect(directoryAction,&QAction::triggered,this,[this]{autoHiddenDirectory=false;});
    connect(dock,&QDockWidget::visibilityChanged,this,[this](bool visible){if(enabled)if(auto *a=window->findChild<QAction*>(QStringLiteral("researchOutlineAction"))){a->setText(words(QStringLiteral("幻灯片列表"),QStringLiteral("Slide list")));a->setCheckable(true);a->setChecked(visible);}});
    connect(previewAction,&QAction::triggered,this,[this]{autoHiddenPreview=false;});
    window->installEventFilter(this);
    connect(list,&QListWidget::currentRowChanged,this,[this](int row){if(row<0)return;select(row,!(QApplication::keyboardModifiers()&(Qt::ControlModifier|Qt::ShiftModifier)));});
    connect(list,&QListWidget::itemClicked,this,[this](QListWidgetItem *item){if((inPreamble||fullSourceSelected)&&list->row(item)==selected)select(selected,true);});
    connect(step,qOverload<int>(&QSpinBox::valueChanged),this,[this](int n){selectedStep=n;showPage();updatePosition();previousStep->setEnabled(step->isEnabled()&&n>1);nextStep->setEnabled(step->isEnabled()&&n<step->maximum());thumbTimer.start();});
    connect(images,&QToolButton::toggled,this,[this](bool on){thumbnail->setVisible(on);if(on)thumbTimer.start();else{++thumbSerial;thumbnail->clear();}});
    list->setContextMenuPolicy(Qt::CustomContextMenu);installSlideClipboard(slides);
    connect(list,&QWidget::customContextMenuRequested,this,[this](const QPoint &point){
      QMenu menu(list);menu.setObjectName(QStringLiteral("researchSlideContextMenu"));
      const QStringList groups{QStringLiteral("researchSlideInsert"),QStringLiteral("researchSlideInsertImage"),QStringLiteral("researchSlideUndo"),QStringLiteral("researchSlidePrevious"),QStringLiteral("researchSlideContextExit")};
      for(auto *entry:list->actions()){
        if(groups.contains(entry->objectName()))menu.addSeparator();
        menu.addAction(entry);
      }
      filterMenuEntries(&menu,MenuSurface::Slides);
      menu.exec(list->viewport()->mapToGlobal(point));
    });
    auto action=[this](const QString &zh,const QString &en,const QString &key,const QString &name,std::function<void()> run){
      auto *a=new QAction(words(zh,en),list);a->setObjectName(name);a->setShortcut(QKeySequence(key));a->setShortcutContext(Qt::WidgetWithChildrenShortcut);list->addAction(a);connect(a,&QAction::triggered,this,run);return a;
    };
    auto *insert=action(QStringLiteral("新建幻灯片"),QStringLiteral("New slide"),QStringLiteral("Insert"),QStringLiteral("researchSlideInsert"),[this]{edit(SlideEdit::Insert);});
    auto *add=new QToolButton(body);add->setObjectName(QStringLiteral("researchSlideAddButton"));add->setText(words(QStringLiteral("新建"),QStringLiteral("New")));connect(add,&QToolButton::clicked,insert,&QAction::trigger);connect(insert,&QAction::changed,add,[add,insert]{add->setEnabled(insert->isEnabled());});add->setAccessibleName(insert->text());add->setToolTip(insert->text());add->setIcon(QIcon(QStringLiteral(":/research/icons/slide-add.svg")));add->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);configureButton(add,ButtonRole::Menu);styleSlideButton(add,true);slideActions->insertWidget(0,add);slideActions->insertStretch(1,1);
    auto *layouts=new QMenu(add);add->setMenu(layouts);add->setPopupMode(QToolButton::MenuButtonPopup);
    layouts->addAction(insert);
    const QList<QPair<QString,QString>> samples={
      {words(QStringLiteral("要点列表"),QStringLiteral("Bullet list")),QStringLiteral("\\begin{itemize}\n  \\item \n\\end{itemize}\n")},
      {words(QStringLiteral("双栏"),QStringLiteral("Two columns")),QStringLiteral("\\begin{columns}[T,onlytextwidth]\n\\begin{column}{0.48\\textwidth}\n\n\\end{column}\n\\begin{column}{0.48\\textwidth}\n\n\\end{column}\n\\end{columns}\n")},
      {words(QStringLiteral("图文"),QStringLiteral("Image and text")),QStringLiteral("\\begin{columns}[T,onlytextwidth]\n\\begin{column}{0.55\\textwidth}\n% \\includegraphics[width=\\linewidth]{figures/example.pdf}\n\\end{column}\n\\begin{column}{0.4\\textwidth}\n\n\\end{column}\n\\end{columns}\n")},
      {words(QStringLiteral("公式"),QStringLiteral("Equation")),QStringLiteral("\\[\n  % Enter a formula here.\n\\]\n")},
      {words(QStringLiteral("定理"),QStringLiteral("Theorem")),QStringLiteral("\\begin{theorem}\n\n\\end{theorem}\n")},
      {words(QStringLiteral("例子"),QStringLiteral("Example")),QStringLiteral("\\begin{exampleblock}{Example}\n\n\\end{exampleblock}\n")}};
    installLayoutGallery(layouts,samples);
    chapterTree->addAction(action(QStringLiteral("重命名…"),QStringLiteral("Rename…"),QStringLiteral("F2"),QStringLiteral("researchSlideRename"),[this]{renameSlide(selected);}));
    connect(chapterTree,&QTreeWidget::itemDoubleClicked,this,[this](QTreeWidgetItem *item,int){bool ok=false;const int row=item->data(0,Qt::UserRole).toInt(&ok);if(ok){list->setCurrentRow(row);renameSlide(row);}});
    action(QStringLiteral("编辑正文"),QStringLiteral("Edit content"),QStringLiteral("Return"),QStringLiteral("researchSlideEditBody"),[this]{if(selected<0||dirty||busy)return;const auto &f=deck.frames[selected];auto *target=sourceWindow(f.file);if(target){setExpandedPreview(false);select(selected,true);target->goToLine(f.line+1,0,0);target->editor()->setFocus();}});
    auto *quickInsert=new QAction(insert->text(),window);quickInsert->setObjectName(QStringLiteral("researchSlideQuickInsert"));quickInsert->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+N")));window->addAction(quickInsert);connect(quickInsert,&QAction::triggered,insert,&QAction::trigger);connect(insert,&QAction::changed,quickInsert,[quickInsert,insert]{quickInsert->setEnabled(insert->isEnabled());});quickInsert->setEnabled(insert->isEnabled());
    layouts->addSeparator();auto *imageAction=action(QStringLiteral("插入图片…"),QStringLiteral("Insert image…"),QString(),QStringLiteral("researchSlideInsertImage"),[this]{const auto file=QFileDialog::getOpenFileName(window,words(QStringLiteral("插入图片"),QStringLiteral("Insert image")),QFileInfo(root).absolutePath(),QStringLiteral("Images (*.pdf *.png *.jpg *.jpeg)"));if(!file.isEmpty())imageOptions(file);});layouts->addAction(imageAction);auto *adjust=action(QStringLiteral("调整图片…"),QStringLiteral("Adjust picture…"),QString(),QStringLiteral("researchSlideAdjustImage"),[this]{adjustImage();});layouts->addAction(adjust);
    auto *notes=action(QStringLiteral("讲者备注…"),QStringLiteral("Speaker notes…"),QString(),QStringLiteral("researchSlideNotes"),[this]{editNotes();});
    if(auto *menu=window->findChild<QMenu *>(QStringLiteral("researchModeMenu")))menu->addAction(notes);
    auto *noteButton=new QToolButton(header);noteButton->setObjectName(QStringLiteral("researchSlideNotesButton"));noteButton->setText(words(QStringLiteral("备注"),QStringLiteral("Notes")));noteButton->setToolTip(notes->text());noteButton->setIcon(QIcon(QStringLiteral(":/research/icons/notes.svg")));noteButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);configureButton(noteButton,ButtonRole::Text);styleSlideButton(noteButton);connect(noteButton,&QToolButton::clicked,notes,&QAction::trigger);connect(notes,&QAction::changed,noteButton,[noteButton,notes]{noteButton->setEnabled(notes->isEnabled());});headerLayout->addWidget(noteButton);
    action(QStringLiteral("创建副本"),QStringLiteral("Duplicate slide"),QStringLiteral("Ctrl+D"),QStringLiteral("researchSlideDuplicate"),[this]{edit(SlideEdit::Duplicate);});
    action(QStringLiteral("上移整张"),QStringLiteral("Move slide up"),QStringLiteral("Alt+Up"),QStringLiteral("researchSlideUp"),[this]{edit(SlideEdit::Up);});
    action(QStringLiteral("下移整张"),QStringLiteral("Move slide down"),QStringLiteral("Alt+Down"),QStringLiteral("researchSlideDown"),[this]{edit(SlideEdit::Down);});
    action(QStringLiteral("删除整张"),QStringLiteral("Delete slide"),QStringLiteral("Delete"),QStringLiteral("researchSlideDelete"),[this]{edit(SlideEdit::Delete);});
    auto *output=new QToolButton(header);output->setObjectName(QStringLiteral("researchSlideOutputButton"));output->setText(words(QStringLiteral("导出"),QStringLiteral("Export")));output->setIcon(QIcon(QStringLiteral(":/research/icons/export.svg")));output->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);output->setToolTip(words(QStringLiteral("选择投影版或讲者版并生成 PDF"),QStringLiteral("Choose a projection or speaker version and generate its PDF")));configureButton(output,ButtonRole::Text);styleSlideButton(output);connect(output,&QToolButton::clicked,this,[this]{outputOptions();});headerLayout->addWidget(output);
    // Each panel has one quiet header. Secondary actions remain in its menu.
    auto *directoryTitle=dock->titleBarWidget();
    auto *directoryHeader=qobject_cast<QHBoxLayout *>(directoryTitle->layout());
    auto *closeDirectory=directoryTitle->findChild<QToolButton *>();closeDirectory->setObjectName(QStringLiteral("researchCloseSlideDirectory"));closeDirectory->hide();
    if(auto *label=directoryTitle->findChild<QLabel *>(QStringLiteral("researchSlidePanelTitle")))label->hide();
    while(auto *item=directoryHeader->takeAt(0))delete item;
    directoryHeader->setContentsMargins(8,4,8,4);directoryHeader->setSpacing(4);
    slideActions->removeWidget(add);slideActions->removeWidget(overview);layout->removeItem(slideActions);delete slideActions;
    add->setParent(directoryTitle);add->setProperty("researchCompactMenu",true);configureButton(add,ButtonRole::Menu);directoryHeader->addWidget(add);directoryHeader->addStretch();overview->setParent(directoryTitle);directoryHeader->addWidget(overview);
    auto *directoryMore=new QToolButton(directoryTitle);directoryMore->setObjectName(QStringLiteral("researchSlideMoreButton"));directoryMore->setIcon(QIcon(QStringLiteral(":/research/icons/more.svg")));directoryMore->setAccessibleName(words(QStringLiteral("幻灯片选项"),QStringLiteral("Slide options")));configureButton(directoryMore,ButtonRole::Icon);directoryMore->setPopupMode(QToolButton::InstantPopup);
    auto *panelMenu=new QMenu(directoryMore);panelMenu->setObjectName(QStringLiteral("researchSlidePanelMenu"));directoryMore->setMenu(panelMenu);directoryHeader->addWidget(directoryMore);
    auto proxy=[](QMenu *menu,QToolButton *button,const QString &name){
      auto *entry=menu->addAction(button->icon(),button->text());entry->setObjectName(name);entry->setCheckable(button->isCheckable());
      QObject::connect(entry,&QAction::triggered,button,&QToolButton::click);
      QObject::connect(menu,&QMenu::aboutToShow,button,[button,entry]{entry->setText(button->text());entry->setEnabled(button->isEnabled());entry->setChecked(button->isChecked());});return entry;
    };
    auto *overviewChoice=proxy(panelMenu,overview,QStringLiteral("researchSlideOverviewMenuAction"));panelMenu->removeAction(overviewChoice);
    auto *searchAction=panelMenu->addAction(QIcon(QStringLiteral(":/research/icons/find.svg")),words(QStringLiteral("筛选幻灯片"),QStringLiteral("Filter slides")),searchButton,&QToolButton::click);searchAction->setObjectName(QStringLiteral("researchSlideSearchMenuAction"));
    auto *browse=panelMenu->addMenu(words(QStringLiteral("筛选"),QStringLiteral("Filter")));browse->setObjectName(QStringLiteral("researchSlideBrowseMenu"));
    connect(browse,&QMenu::aboutToShow,this,[this,browse]{filterMenu->aboutToShow();browse->clear();for(auto *entry:filterMenu->actions())browse->addAction(entry);});
    connect(browse,&QMenu::aboutToHide,filterMenu,[this]{filterMenu->aboutToHide();});
    panelMenu->addSeparator();proxy(panelMenu,preambleButton,QStringLiteral("researchSourcePreambleMenuAction"));proxy(panelMenu,fullSourceButton,QStringLiteral("researchSourceFullMenuAction"));
    panelMenu->addAction(settingsAction);proxy(panelMenu,output,QStringLiteral("researchSlideDirectoryExportAction"));panelMenu->addMenu(documentMenu)->setText(words(QStringLiteral("导言与模板"),QStringLiteral("Preamble and templates")));
    auto *thumbnailAction=proxy(panelMenu,images,QStringLiteral("researchSlideThumbnailMenuAction"));connect(panelMenu,&QMenu::aboutToShow,this,[this,thumbnailAction]{thumbnailAction->setVisible(!previewDock->isVisible());});
    panelMenu->addSeparator();panelMenu->addAction(words(QStringLiteral("收起目录"),QStringLiteral("Hide outline")),dock,[this]{setPanelVisible(dock,false);});
    installMenuChoices(panelMenu,MenuSurface::Slides);
    scopeBar->hide();overview->show();layout->removeWidget(documentButton);row->removeWidget(documentButton);row->removeWidget(images);documentButton->hide();images->hide();layout->removeItem(row);while(auto *item=row->takeAt(0))delete item;delete row;
    previewLayout->removeWidget(header);previewDock->setTitleBarWidget(header);headerLayout->setContentsMargins(12,4,8,4);headerLayout->setSpacing(4);
    expandPreviewAction=new QAction(window);expandPreviewAction->setObjectName(QStringLiteral("researchSlideExpandPreview"));expandPreviewAction->setCheckable(true);updatePreviewExpansion();
    if(auto *menu=window->findChild<QMenu *>(QStringLiteral("researchModeMenu")))menu->addAction(expandPreviewAction);
    connect(expandPreviewAction,&QAction::triggered,this,[this](bool on){setExpandedPreview(on);});
    auto *expandPreview=new QToolButton(header);expandPreview->setObjectName(QStringLiteral("researchSlideExpandPreviewButton"));expandPreview->setDefaultAction(expandPreviewAction);expandPreview->setToolButtonStyle(Qt::ToolButtonIconOnly);configureButton(expandPreview,ButtonRole::Icon);headerLayout->addWidget(expandPreview);
    previewEscape=new QShortcut(QKeySequence(Qt::Key_Escape),window);previewEscape->setEnabled(false);
    connect(previewEscape,&QShortcut::activated,this,[this]{if(filter&&filter->hasFocus()&&searchBar->isVisible())showSearch(false);else setExpandedPreview(false);});
    auto *previewMore=new QToolButton(header);previewMore->setObjectName(QStringLiteral("researchSlidePreviewMore"));previewMore->setIcon(QIcon(QStringLiteral(":/research/icons/more.svg")));previewMore->setAccessibleName(words(QStringLiteral("预览选项"),QStringLiteral("Preview options")));configureButton(previewMore,ButtonRole::Icon);previewMore->setPopupMode(QToolButton::InstantPopup);
    auto *previewMenu=new QMenu(previewMore);previewMenu->setObjectName(QStringLiteral("researchSlidePreviewMenu"));previewMore->setMenu(previewMenu);headerLayout->addWidget(previewMore);
    headerLayout->removeWidget(noteButton);noteButton->hide();previewMenu->addAction(notes);
    headerLayout->removeWidget(output);output->hide();proxy(previewMenu,output,QStringLiteral("researchSlideExportMenuAction"));
    previewMenu->addAction(words(QStringLiteral("打开完整 PDF"),QStringLiteral("Open complete PDF")),openPdf,&QToolButton::click);previewMenu->addSeparator();
    auto *closePreview=new QToolButton(header);closePreview->setObjectName(QStringLiteral("researchCloseSlidePreview"));closePreview->hide();connect(closePreview,&QToolButton::clicked,previewDock,[this]{if(notesChanged&&commitNotes&&!commitNotes())return;setExpandedPreview(false);setPanelVisible(previewDock,false);});
    previewMenu->addAction(words(QStringLiteral("收起预览"),QStringLiteral("Hide preview")),closePreview,&QToolButton::click);

    list->setToolTip(words(QStringLiteral("Ctrl / Shift 多选；拖动连续选区排序。右键可复制、移动和删除，均可撤销。"),QStringLiteral("Ctrl / Shift to select multiple slides; drag a continuous selection to reorder. Right-click to duplicate, move or delete; undo is available.")));
    action(QStringLiteral("撤销"),QStringLiteral("Undo"),QStringLiteral("Ctrl+Z"),QStringLiteral("researchSlideUndo"),[this]{(editTarget?editTarget.data():window)->editor()->undo();});
    action(QStringLiteral("重做"),QStringLiteral("Redo"),QStringLiteral("Ctrl+Shift+Z"),QStringLiteral("researchSlideRedo"),[this]{(editTarget?editTarget.data():window)->editor()->redo();});
    action(QStringLiteral("上一步／上一续页"),QStringLiteral("Previous step"),QStringLiteral("Left"),QStringLiteral("researchSlidePrevious"),[this]{step->stepDown();});
    action(QStringLiteral("下一步／下一续页"),QStringLiteral("Next step"),QStringLiteral("Right"),QStringLiteral("researchSlideNext"),[this]{step->stepUp();});
    auto *exit=new QAction(words(QStringLiteral("退出幻灯片模式"),QStringLiteral("Exit Slide Mode")),list);exit->setObjectName(QStringLiteral("researchSlideContextExit"));list->addAction(exit);connect(exit,&QAction::triggered,toggle,&QAction::toggle);
  }
  bool overviewAfterNotes=false;
  void showOverview() {
    if(notesChanged&&commitNotes){if(!commitNotes())return;overviewAfterNotes=true;parseTimer.start(0);return;}if(dirty||busy)return;if(overviewWindow){overviewWindow->show();overviewWindow->raise();return;}
    auto *dialog=new SlideOverview(window,deck,preview->document().toStrongRef(),stale,selected,filter->text(),groups->currentData().toString());overviewWindow=dialog;dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setModal(false);
    dialog->openSlide=[this](int row){filter->clear();groups->setCurrentIndex(0);list->setCurrentRow(row);select(row,true);window->raise();window->editor()->setFocus();};
    auto problem=[this,dialog](int before){const auto rows=dialog->rows();for(const auto &row:dialog->grid->property("researchSlideDragging").toList())if(row.toInt()>=dialog->grid->count()||dialog->grid->item(row.toInt())->isHidden())return words(QStringLiteral("筛选已变化，请重新拖动"),QStringLiteral("The filter changed. Start a new drag."));if(dirty||busy||rows.isEmpty())return words(QStringLiteral("目录正在更新，请稍后重试。"),QStringLiteral("Wait for the outline to update."));auto *target=sourceWindow(deck.frames[rows[0]].file);QString why;if(!editSlideSelection(target,deck,rows,SlideEdit::Down,before,&why,false))return why.isEmpty()?words(QStringLiteral("不能跨越源码边界或引用依赖。"),QStringLiteral("This crosses a source boundary or dependency.")):why;return QString();};
    installSlideClipboard(dialog->grid);dialog->grid->setContextMenuPolicy(Qt::ActionsContextMenu);
    dialog->grid->revision=[this]{return epoch->load();};dialog->grid->dropProblem=problem;dialog->grid->dropBefore=[this,dialog,problem](int before){const auto why=problem(before);if(!why.isEmpty()){showTransientStatusMessage(window,why);return false;}const auto rows=dialog->rows();auto *target=sourceWindow(deck.frames[rows[0]].file);QString error;if(!editSlideSelection(target,deck,rows,SlideEdit::Down,before,&error))return false;editTarget=target;preferredFile=path(target->fileName());preferredStart=target->cursorPosition();parseTimer.start(0);return true;};
    dialog->editSlides=[this,dialog](SlideEdit operation){if(dirty||busy)return;const auto rows=dialog->rows();if(rows.isEmpty())return;auto *target=sourceWindow(deck.frames[rows[0]].file);QString why;if(editSlideSelection(target,deck,rows,operation,-1,&why)){editTarget=target;preferredFile=path(target->fileName());preferredStart=target->cursorPosition();parseTimer.start(0);}else showTransientStatusMessage(window,why);};
    auto *undo=new QAction(words(QStringLiteral("撤销"),QStringLiteral("Undo")),dialog->grid);undo->setShortcut(QKeySequence(QStringLiteral("Ctrl+Z")));undo->setShortcutContext(Qt::WidgetWithChildrenShortcut);dialog->grid->addAction(undo);connect(undo,&QAction::triggered,this,[this]{if(editTarget)editTarget->editor()->undo();});dialog->show();
  }
  void filterSlides() {
    if(!filter||!groups||!list)return;const auto query=filter->text().trimmed();const auto group=groups->currentData().toString();
    for(int i=0;i<list->count()&&i<deck.frames.size();++i){auto *item=list->item(i);item->setHidden(!matchesSlide(deck.frames[i],i,query,group));if(item->isHidden())item->setSelected(false);}
    if(chapterTree){QSignalBlocker block(chapterTree);chapterTree->setAnimated(false);QSet<QString> expanded;for(int i=0;i<chapterTree->topLevelItemCount();++i)if(chapterTree->topLevelItem(i)->isExpanded())expanded.insert(chapterTree->topLevelItem(i)->text(0));const bool initial=chapterTree->topLevelItemCount()==0;chapterTree->clear();QTreeWidgetItem *parent=nullptr;QString previous;for(int i=0;i<deck.frames.size();++i){const auto &frame=deck.frames[i];const QString section=(frame.backup?words(QStringLiteral("备用 · "),QStringLiteral("Backup · ")):QString())+(frame.section.isEmpty()?words(QStringLiteral("未分节"),QStringLiteral("Unsectioned")):frame.section);if(!parent||section!=previous){parent=new QTreeWidgetItem(chapterTree,{section});parent->setExpanded(initial||expanded.contains(section));previous=section;}if(!list->item(i)->isHidden()){auto *item=new QTreeWidgetItem(parent,{QStringLiteral("%1  %2").arg(i+1).arg(slideCaption(frame))});item->setData(0,Qt::UserRole,i);item->setToolTip(0,slideCaption(frame));}}for(int i=0;i<chapterTree->topLevelItemCount();++i)chapterTree->topLevelItem(i)->setHidden(chapterTree->topLevelItem(i)->childCount()==0);chapterTree->setAnimated(motionEnabled());}
    updateActions();
  }

  void documentOptions() {
    if(notesChanged&&commitNotes&&!commitNotes())return;
    auto *target=sourceWindow(root);if(!target)target=TeXDocumentWindow::openDocument(root,false,false);if(!target||target->editor()->isReadOnly())return;
    const QString original=target->textDoc()->editorText(),clean=maskProjectText(original);const auto begin=QRegularExpression(QStringLiteral(R"(\\begin\s*\{document\})")).match(clean);if(!begin.hasMatch())return;const int end=int(begin.capturedStart());
    struct Field{QString name;int start{-1},end{-1},count{0};QString value;QLineEdit *editor{nullptr};};QVector<Field> fields{{QStringLiteral("title")},{QStringLiteral("subtitle")},{QStringLiteral("author")},{QStringLiteral("institute")},{QStringLiteral("date")},{QStringLiteral("usetheme")},{QStringLiteral("usecolortheme")},{QStringLiteral("usefonttheme")}};
    int depth=0,conditional=0;for(int p=0;p<end;){if(clean[p]==QLatin1Char('\\')){const auto cmd=command(clean,p);if(cmd==QStringLiteral("newif")){spaces(clean,p);if(p<end&&clean[p]==QLatin1Char('\\'))command(clean,p);continue;}if(depth==0&&cmd.startsWith(QStringLiteral("if"))){++conditional;continue;}if(depth==0&&cmd==QStringLiteral("fi")){conditional=qMax(0,conditional-1);continue;}if(depth==0)for(auto &field:fields)if(field.name==cmd){QString opts;options(clean,p,opts);spaces(clean,p);if(p<end&&clean[p]==QLatin1Char('{')){field.start=p;field.value=group(original,p);field.end=p;field.count+=conditional?2:1;}}}else {if(clean[p]==QLatin1Char('{'))++depth;else if(clean[p]==QLatin1Char('}'))--depth;++p;}}
    for(auto &field:fields){
      const QRegularExpression declaration(QStringLiteral("\\\\")+field.name+QStringLiteral("(?![A-Za-z@])"));
      if(declaration.globalMatch(clean.left(end)).hasNext()&&field.count==0)field.count=2;
      for(auto it=deck.sources.cbegin();it!=deck.sources.cend();++it)if(it.key()!=root&&declaration.match(maskProjectText(it.value())).hasMatch())field.count=2;
    }
    MaterialDialog dialog(window);dialog.setObjectName(QStringLiteral("researchSlideDocumentOptions"));dialog.setWindowTitle(words(QStringLiteral("文档设置"),QStringLiteral("Document settings")));dialog.resize(640,450);
    auto *layout=new QVBoxLayout(&dialog);layout->setContentsMargins(20,16,20,16);layout->setSpacing(12);
    auto *tabs=new QTabWidget(&dialog);tabs->setObjectName(QStringLiteral("researchDocumentTabs"));auto *information=new QWidget(tabs),*appearance=new QWidget(tabs);
    tabs->addTab(information,words(QStringLiteral("基本信息"),QStringLiteral("Information")));tabs->addTab(appearance,words(QStringLiteral("外观"),QStringLiteral("Appearance")));
    auto *infoLayout=new QVBoxLayout(information),*appearanceLayout=new QVBoxLayout(appearance);auto *infoForm=new QFormLayout,*appearanceForm=new QFormLayout;
    for(auto *form:{infoForm,appearanceForm}){form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);form->setHorizontalSpacing(18);form->setVerticalSpacing(12);}
    infoLayout->addLayout(infoForm);infoLayout->addStretch();appearanceLayout->addLayout(appearanceForm);appearanceLayout->addStretch();layout->addWidget(tabs,1);
    const QStringList labels{words(QStringLiteral("标题"),QStringLiteral("Title")),words(QStringLiteral("副标题"),QStringLiteral("Subtitle")),words(QStringLiteral("作者"),QStringLiteral("Author")),words(QStringLiteral("单位"),QStringLiteral("Institute")),words(QStringLiteral("日期"),QStringLiteral("Date")),words(QStringLiteral("主题"),QStringLiteral("Theme")),words(QStringLiteral("配色"),QStringLiteral("Colors")),words(QStringLiteral("字体"),QStringLiteral("Fonts"))};
    const QList<QStringList> choices{{QStringLiteral("default"),QStringLiteral("Madrid"),QStringLiteral("Boadilla"),QStringLiteral("CambridgeUS"),QStringLiteral("Berlin"),QStringLiteral("Frankfurt"),QStringLiteral("Singapore"),QStringLiteral("Warsaw")},{QStringLiteral("default"),QStringLiteral("dolphin"),QStringLiteral("seahorse"),QStringLiteral("dove"),QStringLiteral("crane"),QStringLiteral("beaver")},{QStringLiteral("default"),QStringLiteral("serif"),QStringLiteral("professionalfonts"),QStringLiteral("structurebold")}};
    for(int i=0;i<fields.size();++i){auto &field=fields[i];QWidget *control=nullptr;if(i<5){field.editor=new QLineEdit(field.value,information);configureField(field.editor);control=field.editor;}else{auto *choice=new QComboBox(appearance);choice->setObjectName(QStringLiteral("researchDocumentChoice_")+field.name);choice->setEditable(true);choice->setInsertPolicy(QComboBox::NoInsert);choice->setCompleter(nullptr);choice->addItems(choices[i-5]);choice->setEditText(field.value);choice->setMinimumHeight(32);field.editor=choice->lineEdit();field.editor->setPlaceholderText(QStringLiteral("default"));control=choice;}field.editor->setObjectName(QStringLiteral("researchDocument_")+field.name);control->setEnabled(field.count<=1);field.editor->setAccessibleName(labels[i]);control->setToolTip(words(field.count>1?QStringLiteral("此项由子文件或自定义声明控制，请从“导言与模板”编辑。"):QStringLiteral("支持 LaTeX 和自定义值；应用后可撤销。"),field.count>1?QStringLiteral("Controlled by an included file or custom declaration. Edit under Preamble and templates."):QStringLiteral("LaTeX and custom values are supported; applying can be undone.")));(i<5?infoForm:appearanceForm)->addRow(labels[i],control);}
    const auto cls=QRegularExpression(QStringLiteral(R"(\\documentclass\s*(?:\[([^\]]*)\]\s*)?\{(beamer|ctexbeamer)\})")).match(clean);auto *aspect=new QComboBox(appearance);aspect->setObjectName(QStringLiteral("researchDocumentAspect"));
    const auto currentRatio=QRegularExpression(QStringLiteral(R"((?:^|,)\s*aspectratio\s*=\s*(169|43|1610)\s*(?:,|$))")).match(cls.captured(1));const QMap<QString,QString> ratios{{QStringLiteral("169"),QStringLiteral("16:9")},{QStringLiteral("43"),QStringLiteral("4:3")},{QStringLiteral("1610"),QStringLiteral("16:10")}};
    aspect->addItem(currentRatio.hasMatch()?words(QStringLiteral("当前：%1"),QStringLiteral("Current: %1")).arg(ratios.value(currentRatio.captured(1))):words(QStringLiteral("保留模板比例"),QStringLiteral("Keep template ratio")),QString());aspect->addItem(QStringLiteral("16:9"),QStringLiteral("169"));aspect->addItem(QStringLiteral("4:3"),QStringLiteral("43"));aspect->addItem(QStringLiteral("16:10"),QStringLiteral("1610"));aspect->setEnabled(cls.hasMatch());appearanceForm->addRow(words(QStringLiteral("页面比例"),QStringLiteral("Aspect ratio")),aspect);
    auto *notice=new QLabel(words(QStringLiteral("应用到讲稿源码，可撤销；编译后更新预览。"),QStringLiteral("Apply to the source with undo. Compile to update the preview.")),&dialog);notice->setObjectName(QStringLiteral("researchDocumentNotice"));notice->setWordWrap(true);notice->setStyleSheet(QStringLiteral("color:#51565b;font-size:13px;"));layout->addWidget(notice);
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel,&dialog);auto *apply=buttons->button(QDialogButtonBox::Save);apply->setText(words(QStringLiteral("应用"),QStringLiteral("Apply")));apply->setDefault(true);configureButton(apply);auto *cancel=buttons->button(QDialogButtonBox::Cancel);cancel->setText(words(QStringLiteral("取消"),QStringLiteral("Cancel")));configureButton(cancel);buttons->setFixedWidth(apply->width()+cancel->width()+qMax(6,buttons->layout()->spacing()));QWidget::setTabOrder(apply,cancel);auto *footer=new QHBoxLayout;footer->addStretch();footer->addWidget(buttons);layout->addLayout(footer);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);QPointer<TeXDocumentWindow> guard(target);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,[&,guard]{if(!guard||guard->editor()->isReadOnly()||guard->textDoc()->editorText()!=original){notice->setText(words(QStringLiteral("源码已变化，请关闭后重新打开。"),QStringLiteral("Source changed. Close and reopen this dialog.")));return;}struct Change{int start,end;QString text;};QVector<Change> changes;QString insertion;
      for(const auto &field:fields){auto value=field.editor->text();if(field.count>1||value==field.value)continue;if(value.isEmpty()&&field.name.startsWith(QStringLiteral("use")))value=QStringLiteral("default");int balance=0;const auto masked=maskProjectText(value);for(int p=0;p<masked.size();++p)if(!escaped(masked,p)){if(masked[p]==QLatin1Char('{'))++balance;if(masked[p]==QLatin1Char('}')&&--balance<0)break;}if(balance){tabs->setCurrentIndex(field.name.startsWith(QStringLiteral("use"))?1:0);field.editor->setFocus();notice->setText(words(QStringLiteral("请检查花括号是否配对。"),QStringLiteral("Check that braces are balanced.")));return;}if(field.start<0)insertion+=QLatin1Char('\\')+field.name+quotedGroup(value)+QLatin1Char('\n');else changes.append({field.start,field.end,quotedGroup(value)});}
      if(!insertion.isEmpty())changes.append({end,end,insertion});if(cls.hasMatch()&&!aspect->currentData().toString().isEmpty()){QString options=cls.captured(1);const QRegularExpression ratio(QStringLiteral(R"((?:^|,)\s*aspectratio\s*=\s*[^,]*)"));options.remove(ratio);if(options.startsWith(QLatin1Char(',')))options.remove(0,1);options=QStringLiteral("aspectratio=")+aspect->currentData().toString()+(options.trimmed().isEmpty()?QString():QLatin1Char(',')+options);const QString replacement=QStringLiteral("\\documentclass[")+options+QStringLiteral("]{")+cls.captured(2)+QLatin1Char('}');changes.append({int(cls.capturedStart()),int(cls.capturedEnd()),replacement});}
      std::sort(changes.begin(),changes.end(),[](const Change &a,const Change &b){return a.start>b.start;});QTextCursor cursor(guard->textDoc());cursor.beginEditBlock();for(const auto &change:changes){cursor.setPosition(change.start);cursor.setPosition(change.end,QTextCursor::KeepAnchor);cursor.insertText(change.text);}cursor.endEditBlock();editTarget=guard;dialog.accept();
    });
    bool indirect=false;for(const auto &field:fields)indirect|=field.count>1;if(indirect)notice->setText(words(QStringLiteral("部分设置位于子文件或自定义结构中。请从“导言与模板”选择对应源码，避免覆盖原有声明。"),QStringLiteral("Some settings come from included files or custom structures. Choose their source under Preamble and templates to preserve those declarations.")));
    auto *sourcePage=new QWidget(tabs);tabs->addTab(sourcePage,words(QStringLiteral("导言与模板"),QStringLiteral("Preamble and templates")));auto *sourceLayout=new QVBoxLayout(sourcePage);
    auto *sources=new QListWidget(sourcePage);sources->setObjectName(QStringLiteral("researchDocumentSources"));sources->setSelectionMode(QAbstractItemView::SingleSelection);sources->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);sources->setWordWrap(true);sources->setStyleSheet(QStringLiteral("QListWidget {border:1px solid #dedee3;border-radius:4px;background:white;} QListWidget::item {padding:9px 10px;}"));sourceLayout->addWidget(sources,1);
    for(int i=0;i<deck.document.size();++i){const auto &entry=deck.document[i];if(entry.title==QStringLiteral("完整源码"))continue;const auto label=entry.title==QStringLiteral("导言")?words(QStringLiteral("导言"),QStringLiteral("Preamble")):(entry.title==QStringLiteral("主题与模板")?words(QStringLiteral("主题与模板"),QStringLiteral("Theme and templates")):words(QStringLiteral("公共宏"),QStringLiteral("Shared macros")));auto *item=new QListWidgetItem(label+QLatin1Char('\n')+QDir(QFileInfo(root).absolutePath()).relativeFilePath(entry.file),sources);item->setData(Qt::UserRole,i);item->setToolTip(entry.file+QLatin1Char(':')+QString::number(entry.line));}
    if(sources->count())sources->setCurrentRow(0);auto *openSource=new QPushButton(sourcePage);openSource->setObjectName(QStringLiteral("researchDocumentEditSource"));configureButton(openSource);auto *sourceActions=new QHBoxLayout;sourceActions->addStretch();sourceActions->addWidget(openSource);sourceLayout->addLayout(sourceActions);
    auto hasDraft=[&]{for(const auto &field:fields)if(field.count<=1&&field.editor->text()!=field.value)return true;return !aspect->currentData().toString().isEmpty();};
    auto updateSourceAction=[&]{const bool draft=hasDraft();openSource->setText(draft?words(QStringLiteral("应用并编辑源码"),QStringLiteral("Apply and edit source")):words(QStringLiteral("编辑源码"),QStringLiteral("Edit source")));configureButton(openSource);openSource->setEnabled(sources->currentItem());};
    for(const auto &field:fields)connect(field.editor,&QLineEdit::textChanged,&dialog,updateSourceAction);connect(aspect,qOverload<int>(&QComboBox::currentIndexChanged),&dialog,updateSourceAction);connect(sources,&QListWidget::currentRowChanged,&dialog,updateSourceAction);updateSourceAction();
    connect(openSource,&QPushButton::clicked,&dialog,[&]{auto *selectedSource=sources->currentItem();if(!selectedSource)return;const int i=selectedSource->data(Qt::UserRole).toInt();if(hasDraft()){apply->click();if(dialog.result()!=QDialog::Accepted)return;}else dialog.reject();if(auto *item=documentList->topLevelItem(i))documentList->itemClicked(item,0);});connect(sources,&QListWidget::itemActivated,openSource,[openSource]{openSource->click();});
    dialog.exec();
  }
  int currentProfile() const {
    const auto *target=sourceWindow(root);const QString source=target?target->textDoc()->editorText():deck.sources.value(root);
    const int start=source.indexOf(QStringLiteral("% TeXline presentation profile\n")),end=source.indexOf(QStringLiteral("% End TeXline presentation profile\n"),start);
    if(start<0||end<start)return 0;
    return source.mid(start,end-start).contains(QStringLiteral("show notes on second screen"))?2:1;
  }
  void outputOptions() {
    if(notesChanged&&commitNotes&&!commitNotes())return;
    if(window->untitled()||deck.sources.isEmpty()){showTransientStatusMessage(window,words(QStringLiteral("请先保存讲稿后再导出。"),QStringLiteral("Save the presentation before exporting.")));return;}
    MaterialDialog dialog(window);dialog.setObjectName(QStringLiteral("researchSlideOutputDialog"));dialog.setWindowTitle(words(QStringLiteral("导出 PDF"),QStringLiteral("Export PDF")));dialog.resize(560,300);auto *layout=new QVBoxLayout(&dialog);auto *form=new QFormLayout;layout->addLayout(form);
    auto *profile=new QComboBox(&dialog);profile->setObjectName(QStringLiteral("researchSlideOutputProfile"));profile->addItems({words(QStringLiteral("使用原有设置"),QStringLiteral("Original settings")),words(QStringLiteral("投影版"),QStringLiteral("Projection")),words(QStringLiteral("讲者版 · 右侧备注"),QStringLiteral("Speaker · notes on right")),words(QStringLiteral("投影版和讲者版"),QStringLiteral("Projection and speaker"))});profile->setCurrentIndex(1);form->addRow(words(QStringLiteral("版本"),QStringLiteral("Version")),profile);
    auto *name=new QLineEdit(QFileInfo(root).completeBaseName(),&dialog);name->setObjectName(QStringLiteral("researchExportName"));configureField(name);form->addRow(words(QStringLiteral("文件名"),QStringLiteral("Name")),name);
    auto *directory=new QLineEdit(QFileInfo(root).absolutePath(),&dialog);directory->setObjectName(QStringLiteral("researchExportDirectory"));configureField(directory);auto *location=new QHBoxLayout;location->addWidget(directory);auto *browse=new QPushButton(words(QStringLiteral("选择…"),QStringLiteral("Choose…")),&dialog);location->addWidget(browse);form->addRow(words(QStringLiteral("保存到"),QStringLiteral("Save to")),location);connect(browse,&QPushButton::clicked,&dialog,[&]{const auto chosen=QFileDialog::getExistingDirectory(&dialog,words(QStringLiteral("保存到"),QStringLiteral("Save to")),directory->text());if(!chosen.isEmpty())directory->setText(chosen);});
    auto *notice=new QLabel(&dialog);notice->setWordWrap(true);notice->setTextFormat(Qt::PlainText);layout->addWidget(notice);auto describe=[&]{QStringList suffixes;if(profile->currentIndex()==1||profile->currentIndex()==3)suffixes<<QStringLiteral("-projection.pdf");if(profile->currentIndex()==2||profile->currentIndex()==3)suffixes<<QStringLiteral("-speaker.pdf");if(profile->currentIndex()==0)suffixes<<QStringLiteral("-export.pdf");QStringList names;for(const auto &suffix:suffixes)names<<name->text()+suffix;notice->setText(names.join(QLatin1Char('\n')));};connect(profile,qOverload<int>(&QComboBox::currentIndexChanged),&dialog,describe);connect(name,&QLineEdit::textChanged,&dialog,describe);describe();
    auto *log=new QPlainTextEdit(&dialog);log->setObjectName(QStringLiteral("researchSlideExportLog"));log->setReadOnly(true);log->setMaximumHeight(160);log->hide();layout->addWidget(log);auto *buttons=new QDialogButtonBox(QDialogButtonBox::Cancel,&dialog);auto *generate=buttons->addButton(words(QStringLiteral("导出"),QStringLiteral("Export")),QDialogButtonBox::AcceptRole);generate->setObjectName(QStringLiteral("researchGenerateSlidesPdf"));layout->addWidget(buttons);auto cancel=std::make_shared<std::atomic_bool>(false);connect(&dialog,&QObject::destroyed,[cancel]{cancel->store(true);});connect(buttons,&QDialogButtonBox::rejected,&dialog,[&]{cancel->store(true);dialog.reject();});
    connect(generate,&QPushButton::clicked,&dialog,[&]{
      const auto stem=name->text().trimmed();if(stem.isEmpty()||stem==QStringLiteral(".")||stem==QStringLiteral("..")||stem.contains(QRegularExpression(QStringLiteral(R"([<>:"/\\|?*])")))||stem.endsWith(QLatin1Char('.'))||!QDir(directory->text()).exists()){notice->setText(words(QStringLiteral("请输入有效文件名并选择已有目录。"),QStringLiteral("Enter a valid name and choose an existing folder.")));return;}
      SlideExportRequest request;request.root=root;request.sources=deck.sources;QHash<QString,QString> buffers;for(auto *w:TeXDocumentWindow::documentList())if(!w->untitled())buffers[path(w->fileName())]=w->textDoc()->editorText();for(const auto &file:projectFiles(root,buffers))if(buffers.contains(file))request.sources[file]=buffers.value(file);request.engine=TWApp::instance()->getNamedEngine(window->researchEngineName());request.profiles=profile->currentIndex()==3?QList<int>{1,2}:QList<int>{profile->currentIndex()};QStringList existing;
      for(int p:request.profiles){const auto dest=QDir(directory->text()).absoluteFilePath(stem+(p==1?QStringLiteral("-projection.pdf"):p==2?QStringLiteral("-speaker.pdf"):QStringLiteral("-export.pdf")));request.destinations<<dest;request.destinationHashes.insert(dest,slideExportHash(dest));if(QFileInfo::exists(dest))existing<<dest;}
      if(!existing.isEmpty()&&QMessageBox::question(&dialog,words(QStringLiteral("替换已有 PDF？"),QStringLiteral("Replace existing PDFs?")),existing.join(QLatin1Char('\n')),QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes)return;
      generate->setEnabled(false);profile->setEnabled(false);name->setEnabled(false);directory->setEnabled(false);browse->setEnabled(false);log->hide();notice->setText(words(QStringLiteral("正在导出…"),QStringLiteral("Exporting…")));QPointer<QDialog> guard(&dialog);
      QThreadPool::globalInstance()->start(QRunnable::create([request,cancel,guard,notice,log,generate,profile,name,directory,browse]{const auto result=exportSlideCopies(request,cancel);if(!guard)return;QMetaObject::invokeMethod(guard,[=]{if(!guard)return;generate->setEnabled(true);profile->setEnabled(true);name->setEnabled(true);directory->setEnabled(true);browse->setEnabled(true);if(result.cancelled)return;if(!result.error.isEmpty()){notice->setText(localizedSlideExportError(result.error,englishUi())+(result.written.isEmpty()?QString():(englishUi()?QStringLiteral("\nAlready exported:\n"):Research::ui(QStringLiteral("\n已生成以下文件：\n")))+result.written.join(QLatin1Char('\n'))));log->setPlainText(result.log);log->show();return;}guard->setProperty("researchExportWritten",result.written);showTransientStatusMessage(qobject_cast<QMainWindow *>(guard->parentWidget()),englishUi()?QStringLiteral("PDF exported"):Research::ui(QStringLiteral("PDF 已导出")));guard->accept();},Qt::QueuedConnection);}));
    });dialog.exec();
  }
  bool presentationProfile(int profile) {
    auto *target=sourceWindow(root);if(!target)target=TeXDocumentWindow::openDocument(root,false,false);if(!target||target->editor()->isReadOnly())return false;
    const auto source=target->textDoc()->editorText();const auto clean=maskProjectText(source);const auto begin=QRegularExpression(QStringLiteral(R"(\\begin\s*\{document\})")).match(clean);if(!begin.hasMatch())return false;
    const QString first=QStringLiteral("% TeXline presentation profile\n"),last=QStringLiteral("% End TeXline presentation profile\n");if(source.count(first)>1||source.count(last)>1||source.count(first)!=source.count(last))return false;int a=source.indexOf(first),b=-1;if(a>=0){b=source.indexOf(last,a);if(b<0||b>begin.capturedStart()){showTransientStatusMessage(window,words(QStringLiteral("请在导言中检查放映设置块。"),QStringLiteral("Check the presentation settings block in the preamble.")));return false;}b+=last.size();}else a=b=int(begin.capturedStart());
    QString value;if(profile==1)value=first+QStringLiteral("\\setbeameroption{hide notes}\n\\makeatletter\n\\@ifpackageloaded{pgfpages}{\\pgfpagesuselayout{resize to}[physical paper width=\\paperwidth,physical paper height=\\paperheight]}{}\n\\makeatother\n")+last;else if(profile==2)value=first+QStringLiteral("\\usepackage{pgfpages}\n\\setbeameroption{show notes on second screen=right}\n")+last;
    if(source.mid(a,b-a)==value)return true;QTextCursor cursor(target->textDoc());cursor.beginEditBlock();cursor.setPosition(a);cursor.setPosition(b,QTextCursor::KeepAnchor);cursor.insertText(value);cursor.endEditBlock();editTarget=target;showTransientStatusMessage(window,words(QStringLiteral("输出版本已修改，编译后生效；可撤销"),QStringLiteral("Presentation settings changed. Compile to apply; undo is available.")));return true;
  }
#include "ResearchSlideClipboard.inc"
  QVector<int> selection() const {QVector<int> rows;if(list&&list->property("researchSlideDragging").isValid()){for(const auto &v:list->property("researchSlideDragging").toList())rows.append(v.toInt());return rows;}if(list)for(auto *item:list->selectedItems())if(!item->isHidden()){const int row=list->row(item);if(row>=0&&row<deck.frames.size())rows.append(row);}std::sort(rows.begin(),rows.end());if(rows.isEmpty()&&selected>=0&&selected<deck.frames.size()&&!list->item(selected)->isHidden())rows.append(selected);return rows;}
  void updateActions() {
    if(!list)return;const bool ready=enabled&&!dirty&&!busy;const auto rows=selection();auto *target=rows.isEmpty()?window:sourceWindow(deck.frames[rows.first()].file);
    for(const auto &name:{QStringLiteral("researchSlideCopy"),QStringLiteral("researchSlideCut"),QStringLiteral("researchSlidePaste"),QStringLiteral("researchSlidePasteBefore")})if(auto *a=list->findChild<QAction*>(name))a->setEnabled(ready&&(name==QStringLiteral("researchSlideCopy")?!rows.isEmpty():!target||!target->editor()->isReadOnly()));
    for(const auto &entry:QList<QPair<QString,SlideEdit>>{{QStringLiteral("researchSlideDuplicate"),SlideEdit::Duplicate},{QStringLiteral("researchSlideDelete"),SlideEdit::Delete},{QStringLiteral("researchSlideUp"),SlideEdit::Up},{QStringLiteral("researchSlideDown"),SlideEdit::Down}}){if(auto *a=list->findChild<QAction *>(entry.first)){QString reason;const bool allowed=ready&&target&&editSlideSelection(target,deck,rows,entry.second,-1,&reason,false);a->setEnabled(allowed);a->setToolTip(allowed?words(QStringLiteral("可一步撤销"),QStringLiteral("Undo in one step")):words(ready?reason:QStringLiteral("正在更新目录，完成后即可操作"),QStringLiteral("Updating slides, source boundary or a label dependency prevents this operation.")));}}
    for(const auto &name:{QStringLiteral("researchSlideRename"),QStringLiteral("researchSlideInsertImage"),QStringLiteral("researchSlideAdjustImage"),QStringLiteral("researchSlideEditBody")})if(auto *a=list->findChild<QAction *>(name))a->setEnabled(ready&&target&&rows.size()==1&&deck.frames[rows.first()].editable&&!target->editor()->isReadOnly());
    if(auto *a=list->findChild<QAction *>(QStringLiteral("researchSlideNotes")))a->setEnabled(ready&&target&&rows.size()==1&&deck.frames[rows.first()].editable&&!target->editor()->isReadOnly());
    if(auto *a=list->findChild<QAction *>(QStringLiteral("researchSlideInsert")))a->setEnabled(ready&&deck.beamer&&target&&!target->editor()->isReadOnly()&&((selected<0&&rows.isEmpty()&&!deck.uncertain)||(!rows.isEmpty()&&deck.frames[rows.first()].editable)));
    for(const auto &entry:QList<QPair<QString,QString>>{{QStringLiteral("researchSlideDuplicate"),words(QStringLiteral("创建副本"),QStringLiteral("Duplicate"))},{QStringLiteral("researchSlideDelete"),words(QStringLiteral("删除"),QStringLiteral("Delete"))},{QStringLiteral("researchSlideUp"),words(QStringLiteral("上移"),QStringLiteral("Move up"))},{QStringLiteral("researchSlideDown"),words(QStringLiteral("下移"),QStringLiteral("Move down"))}})if(auto *a=list->findChild<QAction *>(entry.first))a->setText(entry.second+(rows.size()>1?words(QStringLiteral(" %1 张"),QStringLiteral(" %1 slides")).arg(rows.size()):QString()));
    list->setDragEnabled(ready&&!rows.isEmpty());
  }
  QString importSlideAsset(const QString &file) {
    QFile input(file);if(!input.open(QIODevice::ReadOnly))return QString();const auto extension=QFileInfo(file).suffix().toLower();
    if(extension!=QStringLiteral("pdf")&&!QImageReader(file).canRead()){showTransientStatusMessage(window,words(QStringLiteral("无法读取这张图片"),QStringLiteral("Cannot read this image")));return QString();}
    if(extension==QStringLiteral("pdf")&&input.peek(5)!=QByteArray("%PDF-"))return QString();
    const auto digest=slideExportHash(file).toHex().left(12);if(digest.isEmpty())return QString();
    auto stem=QFileInfo(file).completeBaseName();stem.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]")),QStringLiteral("-"));stem=stem.left(48);if(stem.isEmpty())stem=QStringLiteral("image");
    const auto relative=QStringLiteral("figures/")+stem+QLatin1Char('-')+QString::fromLatin1(digest)+QLatin1Char('.')+extension;
    const auto destination=QDir(QFileInfo(root).absolutePath()).filePath(relative);
    if(!QFileInfo::exists(destination)){if(!QDir().mkpath(QFileInfo(destination).absolutePath()))return QString();QSaveFile copy(destination);if(!copy.open(QIODevice::WriteOnly))return QString();while(!input.atEnd()){const auto data=input.read(256*1024);if(data.isEmpty()&&input.error()!=QFile::NoError)return QString();if(copy.write(data)!=data.size())return QString();}if(!copy.commit())return QString();}
    else if(slideExportHash(file)!=slideExportHash(destination)){showTransientStatusMessage(window,words(QStringLiteral("同名资源已有不同内容，请先检查资源目录。"),QStringLiteral("An existing resource differs. Check the resource folder.")));return QString();}
    return relative;
  }
  bool insertImageFile(const QString &file,int percent=90,bool centered=false) {
    if(dirty||busy||selected<0||selected>=deck.frames.size())return false;
    const auto frame=deck.frames[selected];auto *target=sourceWindow(frame.file);if(!target||target->editor()->isReadOnly()||!frame.editable)return false;
    const auto source=target->textDoc()->editorText();if(source!=deck.sources.value(frame.file))return false;
    const auto relative=importSlideAsset(file);if(relative.isEmpty())return false;
    const int bodyStart=source.indexOf(QLatin1Char('\n'),frame.start)+1,bodyEnd=source.lastIndexOf(QStringLiteral("\\end{frame}"),frame.end);if(bodyStart<1||bodyEnd<bodyStart)return false;
    auto cursor=target->editor()->textCursor();if(cursor.selectionStart()<bodyStart||cursor.selectionEnd()>bodyEnd){cursor=QTextCursor(target->textDoc());cursor.setPosition(bodyEnd);}
    cursor.beginEditBlock();cursor.insertText((centered?QStringLiteral("\n\\begin{center}\n"):QStringLiteral("\n"))+QStringLiteral("\\includegraphics[width=%1\\linewidth]{%2}\n").arg(QString::number(percent/100.0,'f',2),relative)+(centered?QStringLiteral("\\end{center}\n"):QString()));cursor.endEditBlock();target->editor()->setTextCursor(cursor);target->editor()->setFocus();editTarget=target;
    showTransientStatusMessage(window,words(QStringLiteral("图片已插入，可撤销；资源保留在 figures 中。"),QStringLiteral("Image inserted. Undo restores the text; the asset stays in figures.")));return true;
  }
#include "ResearchSlideAuthoring.inc"
  void insertLayout(const QString &content) {
    if(dirty||busy){showTransientStatusMessage(window,words(QStringLiteral("正在更新目录，请稍后重试"),QStringLiteral("Updating slides. Please try again shortly.")));return;}
    auto *target=selected>=0?sourceWindow(deck.frames[selected].file):window;if(!target||target->editor()->isReadOnly())return;
    SlideSourceEdit sourceEdit(target);
    auto before=target->textDoc()->editorText();if(before!=deck.sources.value(path(target->fileName())))return;
    int at=selected>=0?deck.frames[selected].end:-1;
    if(selected>=0&&!deck.frames[selected].editable)return;
    if(at<0){const auto m=QRegularExpression(QStringLiteral(R"(\\end\s*\{document\})")).match(maskProjectText(before));if(!m.hasMatch())return;at=int(m.capturedStart());}
    const QString text=QStringLiteral("\n\\begin{frame}{}\n")+content+QStringLiteral("\\end{frame}\n");
    QTextCursor cursor(target->editor()->document());cursor.beginEditBlock();cursor.setPosition(at);cursor.insertText(text);cursor.endEditBlock();cursor.setPosition(at+15);target->editor()->setTextCursor(cursor);editTarget=target;preferredFile=path(target->fileName());preferredStart=at+1;
  }
  void setNotesChanged(bool changed) {
    notesChanged=changed;
    for(auto *target:TeXDocumentWindow::documentList())if(target==window||path(target->fileName())==notesFile) {
      if(auto *save=target->findChild<QAction *>(QStringLiteral("actionSave")))save->setEnabled(target->isModified()||target->untitled()||hasPendingSlideNotes(target));
      target->setWindowModified(target->isModified());
    }
  }
  void editNotes() {
    if(notesPane&&notesPane->isVisible()){hideNotes();return;}
    openNotes();if(notesPane)if(auto *editor=notesPane->findChild<QPlainTextEdit *>())editor->setFocus();
  }
  void hideNotes() {
    if(commitNotes&&!commitNotes())return;
    if(notesPane)notesPane->hide();
    if(expandedPreview&&preview)preview->setFocus();else window->editor()->setFocus();
  }
  void openNotes() {
    if(dirty||busy||selected<0)return;const auto frame=deck.frames[selected];auto *target=sourceWindow(frame.file);if(!target||!frame.editable)return;
    const auto source=target->textDoc()->editorText();if(source!=deck.sources.value(frame.file))return;
    // A background parse must not recreate an unchanged editor or reset its
    // focus, selection, draft undo stack or scroll position.
    if(notesPane&&notesFile==frame.file&&notesPane->property("researchNotesSource").toString()==source&&notesPane->property("researchNotesFrameStart").toInt()==frame.start){notesPane->show();setPanelVisible(previewDock,true);return;}
    notesFile=frame.file;
    struct Note{int start,end;QString text;};QVector<Note> notes;const auto clean=maskProjectText(source);const QRegularExpression pattern(QStringLiteral(R"(\\note\s*(?:\[[^\]]*\]\s*)?\{)"));int offset=frame.start;
    while(offset<frame.end){const auto m=pattern.match(clean,offset);if(!m.hasMatch()||m.capturedStart()>=frame.end)break;int pos=int(m.capturedEnd())-1;const int start=pos;auto text=group(source,pos);if(pos>frame.end||pos<=start)break;notes.append({start,pos,text});offset=pos;}
    const bool inserting=notes.isEmpty();if(inserting)notes.append({frame.end,frame.end,QString()});
    struct Draft{QString source;QVector<Note> notes;bool inserting;int frameEnd;};
    auto draft=std::make_shared<Draft>(Draft{source,notes,inserting,frame.end});
    int focusedNote=-1,focusPosition=0,focusAnchor=0,focusScroll=0;
    if(notesPane)for(auto *previous:notesPane->findChildren<QPlainTextEdit *>())if(previous->hasFocus()){focusedNote=previous->objectName().mid(QStringLiteral("researchSlideNote").size()).toInt();focusPosition=previous->textCursor().position();focusAnchor=previous->textCursor().anchor();focusScroll=previous->verticalScrollBar()->value();break;}
    if(notesPane){notesPane->hide();notesPane->deleteLater();}commitNotes={};setNotesChanged(false);
    auto *pane=new QWidget(previewDock->widget());notesPane=pane;pane->setObjectName(QStringLiteral("researchSlideNotesPane"));pane->setProperty("researchNotesSource",source);pane->setProperty("researchNotesFrameStart",frame.start);pane->setStyleSheet(QStringLiteral("QWidget#researchSlideNotesPane {border-top:1px solid #dedee3;}"));pane->setMinimumHeight(170);pane->setMaximumHeight(270);pane->installEventFilter(this);auto *layout=new QVBoxLayout(pane);layout->setContentsMargins(10,8,10,8);
    auto *heading=new QLabel(words(QStringLiteral("备注 · %1"),QStringLiteral("Notes · %1")).arg(slideCaption(frame)),pane);heading->setWordWrap(true);layout->addWidget(heading);auto *tabs=new QTabWidget(pane);layout->addWidget(tabs,1);QList<QPlainTextEdit *> editors;
    for(int i=0;i<notes.size();++i){auto *editor=new QPlainTextEdit(tabs);editor->setObjectName(QStringLiteral("researchSlideNote%1").arg(i));editor->setPlainText(notes[i].text);editor->installEventFilter(this);editors<<editor;tabs->addTab(editor,words(QStringLiteral("备注 %1"),QStringLiteral("Note %1")).arg(i+1));}tabs->tabBar()->setVisible(notes.size()>1);tabs->installEventFilter(this);tabs->tabBar()->installEventFilter(this);
    auto *notice=new QLabel(pane);notice->setObjectName(QStringLiteral("researchSlideNotesNotice"));notice->setWordWrap(true);notice->hide();layout->addWidget(notice);
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Cancel,pane);buttons->button(QDialogButtonBox::Cancel)->setText(words(QStringLiteral("收起"),QStringLiteral("Hide")));auto *reset=buttons->addButton(words(QStringLiteral("放弃改动"),QStringLiteral("Discard")),QDialogButtonBox::ResetRole);reset->setObjectName(QStringLiteral("researchDiscardNotesButton"));reset->hide();for(auto *button:buttons->buttons())button->installEventFilter(this);layout->addWidget(buttons);
    // Kept only for older command callers; normal editing commits at boundaries.
    auto *save=new QPushButton(words(QStringLiteral("应用备注"),QStringLiteral("Apply notes")),pane);save->setObjectName(QStringLiteral("researchApplyNotes"));save->hide();save->setEnabled(false);
    previewLayout->addWidget(pane);setPanelVisible(previewDock,true);
    for(auto *editor:editors)connect(editor,&QPlainTextEdit::textChanged,pane,[this,draft,editors,save,reset,notice]{bool changed=false;for(int i=0;i<editors.size();++i)changed|=editors[i]->toPlainText()!=draft->notes[i].text;setNotesChanged(changed);save->setEnabled(changed);reset->setVisible(changed);notice->clear();notice->hide();});
    QPointer<TeXDocumentWindow> guard(target);QPointer<QWidget> paneGuard(pane);
    auto error=[this,notice,tabs,paneGuard](const QString &message,QPlainTextEdit *editor){if(!paneGuard)return false;notice->setText(message);notice->show();paneGuard->show();setPanelVisible(previewDock,true);tabs->setCurrentWidget(editor);window->selectWindow();editor->setFocus();return false;};
    commitNotes=[this,guard,paneGuard,draft,editors,frame,notice,save,reset,error]()mutable{
      if(!paneGuard||!notesChanged)return true;if(notesCommitting)return false;QScopedValueRollback<bool> committing(notesCommitting,true);
      if(!guard)guard=sourceWindow(frame.file);
      if(!guard)return error(words(QStringLiteral("来源文件已关闭。备注草稿已保留，请重新打开文件。"),QStringLiteral("Source file closed. Reopen it; your note draft is retained.")),editors.first());
      if(guard->editor()->isReadOnly())return error(words(QStringLiteral("文件只读。请解除只读后重试；备注草稿已保留。"),QStringLiteral("File is read-only. Make it writable and retry; your note draft is retained.")),editors.first());
      if(guard->textDoc()->editorText()!=draft->source)return error(words(QStringLiteral("源码已变化。请核对源码或放弃改动；备注草稿已保留。"),QStringLiteral("Source changed. Review the source or discard the changes; your note draft is retained.")),editors.first());
      QStringList values;for(auto *editor:editors){const auto value=editor->toPlainText();const auto masked=maskProjectText(value);int depth=0;for(int i=0;i<masked.size();++i)if(!escaped(masked,i)){if(masked[i]==QLatin1Char('{'))++depth;if(masked[i]==QLatin1Char('}')&&--depth<0)break;}if(depth!=0)return error(words(QStringLiteral("备注中的花括号需要配对；草稿已保留。"),QStringLiteral("Balance the braces. Your draft is retained.")),editor);values<<value;}
      struct Change{int start,end;QString text;};QVector<Change> changes;auto updated=draft->notes;int delta=0;
      for(int i=0;i<draft->notes.size();++i){const auto &note=draft->notes[i];const auto replacement=quotedGroup(values[i]);int a=note.start,b=note.end;QString text=replacement;
        if(draft->inserting){if(values[i].isEmpty())continue;a=maskProjectText(draft->source).lastIndexOf(QStringLiteral("\\end{frame}"),draft->frameEnd);if(a<frame.start)return error(words(QStringLiteral("请在源码中编辑此页备注；草稿已保留。"),QStringLiteral("Edit this frame's notes in source; the draft is retained.")),editors[i]);b=a;text=QStringLiteral("\\note")+replacement+QLatin1Char('\n');}
        const bool changed=values[i]!=note.text;if(changed)changes.append({a,b,text});
        updated[i]={a+delta+(draft->inserting?5:0),changed?a+delta+(draft->inserting?5:0)+int(replacement.size()):b+delta,values[i]};if(changed)delta+=int(text.size())-(b-a);
      }
      if(!changes.isEmpty()){
        if(notesFrameAnchors.isEmpty())for(const auto &slide:deck.frames){QTextCursor anchor;if(auto *source=sourceWindow(slide.file)){anchor=QTextCursor(source->textDoc());anchor.setPosition(qBound(0,slide.start,source->textDoc()->characterCount()-1));anchor.setKeepPositionOnInsert(true);}notesFrameAnchors.append(anchor);}
        QTextCursor cursor(guard->textDoc());cursor.beginEditBlock();for(int i=changes.size()-1;i>=0;--i){const auto &change=changes[i];cursor.setPosition(change.start);cursor.setPosition(change.end,QTextCursor::KeepAnchor);cursor.insertText(change.text);}cursor.endEditBlock();
        // Advance the same draft immediately, before an asynchronous deck parse.
        // A quick Save/continue/Save sequence must never compare with old source.
        draft->source=guard->textDoc()->editorText();draft->notes=updated;draft->inserting=false;draft->frameEnd+=delta;paneGuard->setProperty("researchNotesSource",draft->source);editTarget=guard;
      }
      setNotesChanged(false);save->setEnabled(false);reset->hide();notice->clear();notice->hide();return true;
    };
    connect(save,&QPushButton::clicked,pane,[this]{if(commitNotes&&commitNotes()&&dirty)parseTimer.start(0);});connect(buttons,&QDialogButtonBox::rejected,pane,[this]{hideNotes();});
    auto discardChanges=[this,draft,guard,paneGuard,editors,save,reset,notice]{if(!paneGuard)return;setNotesChanged(false);save->setEnabled(false);reset->hide();notice->clear();notice->hide();if(guard&&guard->textDoc()->editorText()==draft->source){for(int i=0;i<editors.size();++i){QSignalBlocker block(editors[i]);editors[i]->setPlainText(draft->notes[i].text);}editors.first()->setFocus();}else{paneGuard->hide();focusNotes=true;parseTimer.start(0);}};
    connect(reset,&QPushButton::clicked,pane,discardChanges);
    auto *discard=new QAction(words(QStringLiteral("放弃本次备注改动"),QStringLiteral("Discard note changes")),pane);discard->setObjectName(QStringLiteral("researchDiscardNotes"));pane->addAction(discard);pane->setContextMenuPolicy(Qt::ActionsContextMenu);connect(discard,&QAction::triggered,pane,discardChanges);pane->show();animateArrival(pane);
    if(focusedNote>=0){auto *editor=editors[qBound(0,focusedNote,int(editors.size())-1)];tabs->setCurrentWidget(editor);QTextCursor cursor(editor->document());const int last=editor->document()->characterCount()-1;cursor.setPosition(qBound(0,focusAnchor,last));cursor.setPosition(qBound(0,focusPosition,last),QTextCursor::KeepAnchor);editor->setTextCursor(cursor);editor->setFocus();editor->verticalScrollBar()->setValue(focusScroll);}
  }
  void renameSlide(int index,const QString &newTitle=QString(),const QString &snapshot=QString()) {
    if(dirty||busy||index<0||index>=deck.frames.size())return;const auto f=deck.frames[index];auto *target=sourceWindow(f.file);if(!target||target->editor()->isReadOnly()||!f.editable||f.kind==QStringLiteral("againframe"))return;
    const auto original=target->textDoc()->editorText();if(original!=deck.sources.value(f.file))return;
    const auto clean=maskProjectText(original);int p=f.start;spaces(clean,p);const auto cmd=command(clean,p);if(cmd==QStringLiteral("begin"))group(clean,p);QString opts;options(clean,p,opts);spaces(clean,p);
    int a=-1,b=-1;QString title;
    if(p<clean.size()&&clean[p]==QLatin1Char('{')&&cmd==QStringLiteral("begin")){a=p;title=group(original,p);b=p;}
    else {const auto m=QRegularExpression(QStringLiteral(R"(\\frametitle\*?\s*(?:<[^>]*>\s*)?(?:\[[^\]]*\]\s*)?\{)")).match(clean,f.start);if(m.hasMatch()&&m.capturedStart()<f.end){a=int(m.capturedEnd())-1;p=a;title=group(original,p);b=p;}}
    if(snapshot.isNull()){
      if(auto *old=window->findChild<QLineEdit *>(QStringLiteral("researchInlineSlideTitle"))){old->hide();old->setObjectName(QString());old->deleteLater();}
      QWidget *viewport=list->viewport();QRect rowRect;
      if(chapterTree&&!chapterTree->isHidden()){
        for(int i=0;i<chapterTree->topLevelItemCount();++i){auto *section=chapterTree->topLevelItem(i);for(int j=0;j<section->childCount();++j){auto *item=section->child(j);if(item->data(0,Qt::UserRole).toInt()==index){section->setExpanded(true);chapterTree->scrollToItem(item);rowRect=chapterTree->visualItemRect(item);break;}}if(!rowRect.isNull())break;}
        viewport=chapterTree->viewport();
      }else{auto *item=list->item(index);if(!item)return;list->scrollToItem(item);rowRect=list->visualItemRect(item);}
      if(rowRect.isNull())return;
      auto *field=new QLineEdit(title,viewport);field->setObjectName(QStringLiteral("researchInlineSlideTitle"));configureField(field);rowRect.adjust(24,0,0,0);const int height=qMin(rowRect.height(),32);field->setGeometry(rowRect.x(),rowRect.center().y()-height/2,rowRect.width(),height);field->setToolTip(words(QStringLiteral("Enter 确认，Esc 取消；支持 LaTeX"),QStringLiteral("Enter to apply, Esc to cancel; LaTeX supported")));field->installEventFilter(this);field->show();field->setFocus();field->selectAll();
      connect(field,&QLineEdit::returnPressed,this,[this,index,field,original,title,guard=QPointer<TeXDocumentWindow>(target)]{if(!guard||index>=deck.frames.size())return;if(guard->textDoc()->editorText()!=original){field->setToolTip(words(QStringLiteral("源码已变化，请重新编辑标题"),QStringLiteral("Source changed; reopen title editing")));return;}if(dirty||busy){field->setToolTip(words(QStringLiteral("源码已变化，请重新编辑标题"),QStringLiteral("Source changed; reopen title editing")));return;}renameSlide(index,field->text(),original);if(guard->textDoc()->editorText()!=original||field->text()==title){field->hide();field->setObjectName(QString());field->deleteLater();}});return;
    }
    if(snapshot!=original){showTransientStatusMessage(window,words(QStringLiteral("源码已变化，请重新编辑标题"),QStringLiteral("Source changed; reopen title editing")));return;}
    const auto value=newTitle;
    if(value==title)return;
    // User input is literal TeX; reject unmatched groups before altering a frame.
    const auto masked=maskProjectText(value);int depth=0;for(int i=0;i<masked.size();++i)if(!escaped(masked,i)){if(masked[i]==QLatin1Char('{'))++depth;if(masked[i]==QLatin1Char('}')&&--depth<0)break;}
    if(depth!=0){showTransientStatusMessage(window,words(QStringLiteral("标题中的花括号需要配对"),QStringLiteral("Balance the braces in the title.")));return;}
    QString replacement=quotedGroup(value);
    if(a<0){a=b=f.start;p=f.start;spaces(clean,p);command(clean,p);if(cmd==QStringLiteral("begin")){group(clean,p);options(clean,p,opts);a=b=p;}else{showTransientStatusMessage(window,words(QStringLiteral("请在源码中添加标题"),QStringLiteral("Add the title in source.")));return;}}
    QTextCursor edit(target->editor()->document());edit.beginEditBlock();edit.setPosition(a);edit.setPosition(b,QTextCursor::KeepAnchor);edit.insertText(replacement);edit.endEditBlock();
  }
  void activate(bool on) {
    if(!on&&notesChanged&&commitNotes&&!commitNotes()){QSignalBlocker block(toggle);toggle->setChecked(true);return;}
    if(!on)setExpandedPreview(false);
    if(!on&&notesPane)notesPane->hide();
    if(!on&&overviewWindow)overviewWindow->close();
    if(enabled==on)return;
    enabled=on;window->setProperty("researchSlideMode",on);
    if(auto *a=window->findChild<QAction*>(QStringLiteral("researchOutlineAction"))){a->setText(words(on?QStringLiteral("幻灯片列表"):QStringLiteral("结构…"),on?QStringLiteral("Slide list"):QStringLiteral("Structure…")));a->setCheckable(on);a->setChecked(false);}
    if(auto *pdf=window->findChild<QToolButton *>(QStringLiteral("researchPdfButton"))){pdf->setText(words(on?QStringLiteral("完整 PDF"):QStringLiteral("PDF"),on?QStringLiteral("Full PDF"):QStringLiteral("PDF")));pdf->setToolTip(words(on?QStringLiteral("打开整份讲稿 PDF"):QStringLiteral("打开 PDF"),on?QStringLiteral("Open the complete presentation PDF"):QStringLiteral("Open PDF")));configureButton(pdf,ButtonRole::Text);}
    epoch->fetch_add(1);++thumbSerial;parseTimer.stop();thumbTimer.stop();
    if(!on) {
      window->textDoc()->setProperty("researchPresentationRoot",QVariant());window->textDoc()->setProperty("researchPresentationSource",QVariant());
      notifyUpdate=false;
      releaseSourceViews();
      if(auto *quick=window->findChild<QAction *>(QStringLiteral("researchSlideQuickInsert")))quick->setEnabled(false);
      for(const auto &c:activeConnections)disconnect(c);
      activeConnections.clear();
      for(const auto &c:pdfConnections)disconnect(c);
      pdfConnections.clear();viewer=nullptr;
      if(!files.files().isEmpty())files.removePaths(files.files());
      if(!files.directories().isEmpty())files.removePaths(files.directories());
      cache.clear();cacheKeys.clear();if(dock){dock->hide();previewDock->hide();directoryAction->setVisible(false);previewAction->setVisible(false);expandPreviewAction->setVisible(false);delete preview;preview=nullptr;previewScene.reset();loadedRevision.clear();}return;
    }
    setup();if(!preview)createPreview();directoryAction->setVisible(true);previewAction->setVisible(true);expandPreviewAction->setVisible(true);dock->setVisible(window->width()>=620);previewDock->setVisible(window->width()>=960);
    auto watch=[this]{for(auto *w:TeXDocumentWindow::documentList()) {
      // Connections belong to this controller and are disconnected on exit.
      bool seen=false;for(const auto &v:watched)if(v==w){seen=true;break;}if(seen)continue;
      watched.append(w);
      activeConnections.append(connect(w->textDoc(),&QTextDocument::contentsChange,this,[this,w](int,int removed,int added){if((removed||added)&&(w==window||deck.sources.contains(path(w->fileName()))||deck.dependencies.contains(path(w->fileName()))))invalidate();}));
      activeConnections.append(connect(w->editor(),&CompletingEdit::cursorPositionChanged,this,[this,w]{
        if(!enabled||changingSourceView||w->editor()->property("researchSlideScopeTransaction").toBool())return;
        if(!dirty&&!busy)cursor(w);
        else for(const auto &v:sourceViews)if(v.editor==w->editor()&&v.partial&&(w->cursorPosition()<v.first.position()||w->cursorPosition()>v.last.position())){showSource(w,0,0,true);break;}
      }));
    }};
    watched.clear();watch();activeConnections.append(connect(TWApp::instance(),&TWApp::windowListChanged,this,watch));
    request();
  }
  QList<QPointer<TeXDocumentWindow>> watched;
  void invalidate(bool sourceChanged=true) {
    if(!enabled)return;
    dirty=true;if(sourceChanged)stale=true;epoch->fetch_add(1);++thumbSerial;
    if(thumbnail)thumbnail->clear();
    updateStatus();parseTimer.start();
  }
  void updateStatus() {
    if(!status)return;
    updateActions();
    QString text;
    const bool hasPreview=bool(previewScene)&&!loadedRevision.isEmpty();
    if(compiling&&!hasPreview)text=words(QStringLiteral("正在生成首次预览…"),QStringLiteral("Generating the first preview…"));
    else if(failed&&!hasPreview)text=words(QStringLiteral("尚无预览，请先修正编译错误"),QStringLiteral("No preview yet. Resolve the build errors first."));
    else if(stale&&!hasPreview)text=words(QStringLiteral("编译后显示预览"),QStringLiteral("Compile to preview"));
    else if(compiling)text=words(QStringLiteral("正在编译 · 保留上一版预览"),QStringLiteral("Compiling · previous preview"));
    else if(failed)text=words(QStringLiteral("编译失败 · 保留上一版预览"),QStringLiteral("Build failed · previous preview"));
    else if(stale)text=words(QStringLiteral("上一版预览"),QStringLiteral("Previous preview"));
    else if(busy)text=words(QStringLiteral("正在核对幻灯片…"),QStringLiteral("Checking slides…"));
    else if(deck.mapped)text.clear();
    else if(!deck.beamer)text=words(QStringLiteral("主文件不是可确认的 Beamer 文档"),QStringLiteral("Main file is not a confirmed Beamer document"));
    else text=deck.previewCurrent?words(QStringLiteral("PDF 已生成，暂时无法对应到每一张"),QStringLiteral("PDF ready. Individual slides could not be matched.")):words(QStringLiteral("编译后显示预览"),QStringLiteral("Compile to preview"));
    status->setText(text);status->setToolTip(deck.reason.isEmpty()?words(QStringLiteral("仅接受完整编译且已核验来源的 PDF。"),QStringLiteral("Only a verified, complete build can be mapped.")):words(deck.reason,QStringLiteral("Mapping is unavailable: dynamic source, missing compiler data or source/PDF mismatch. Source navigation remains available.")));
    if(openPdf)openPdf->setVisible(!deck.mapped&&!deck.pdf.isEmpty());
    status->setStyleSheet(stale||failed?QStringLiteral("color:#98703f;"):QStringLiteral("color:#51565b;"));
    if(emptyPreview){emptyPreview->setText(text);emptyPreview->setGeometry(preview->viewport()->rect());emptyPreview->setVisible(!hasPreview);if(!hasPreview)emptyPreview->raise();}
    status->setVisible(hasPreview&&!text.isEmpty());
    status->parentWidget()->setVisible(!status->isHidden()||(openPdf&&!openPdf->isHidden()));
  }
  void request() {
    if(!enabled)return;
    if(busy){pending=true;return;}
    window->flushScriptEdits();
    root=window->untitled()?window->fileName():window->textDoc()->getRootFilePath();root=path(root);
    QHash<QString,QString> buffers;
    for(auto *w:TeXDocumentWindow::documentList())if(!w->untitled())buffers.insert(path(w->fileName()),w->textDoc()->editorText());
    if(window->untitled())buffers.insert(root,window->textDoc()->editorText());
    busy=true;pending=false;const auto ticket=epoch->fetch_add(1)+1;const auto token=epoch;const auto source=root;
    remember();updateStatus();window->setProperty("researchSlideParses",window->property("researchSlideParses").toInt()+1);
    QPointer<SlideController> guard(this);
    slidePool().start(QRunnable::create([guard,token,ticket,source,buffers]{
      const auto cancelled=[token,ticket]{return token->load()!=ticket;};auto result=parseSlideDeck(source,buffers,cancelled);
      QSharedPointer<QtPDF::Backend::Document> document;
      if(!cancelled()&&result.beamer) {
        const QString pdf=previewFile(source);result.pdf=pdf;
        // All expensive hashing and SyncTeX parsing stay on this worker. Dirty
        // snapshots must match the certified disk revision byte for byte.
        bool same=true;for(auto it=result.sources.begin();it!=result.sources.end();++it)if(read(it.key())!=it.value()){same=false;break;}
        if(!pdf.isEmpty()) {
          const auto meta=pdfMetadata(pdf);const auto marker=QJsonDocument(meta).toJson(QJsonDocument::Compact);
          const auto inputs=meta.value(QStringLiteral("inputs")).toObject();result.dependencies=inputs.keys();result.dependencies.append(pdfMetadataReadFile(pdf));
          for(auto it=buffers.begin();it!=buffers.end();++it)if(inputs.contains(it.key())&&read(it.key())!=it.value()){same=false;break;}
          const auto nav=meta.value(QStringLiteral("beamerNavigation")).toString();
          result.previewCurrent=same&&certified(meta,pdf,source,cancelled)&&!cancelled();
          if(result.previewCurrent) {
            document=QtPDF::Backend::Document::newDocument(pdf);
            if(document&&document->isValid()&&!cancelled()) {
              result.revision=meta.value(QStringLiteral("pdfHash")).toString();
              if(!result.uncertain)mapSlidePages(result,nav,int(document->numPages()),syncPages(result,pdf,source,cancelled));
              if(!certified(meta,pdf,source,cancelled)||QJsonDocument(pdfMetadata(pdf)).toJson(QJsonDocument::Compact)!=marker){result.mapped=false;result.previewCurrent=false;for(auto &f:result.frames)f.firstPage=f.lastPage=0;document.reset();}
            }
          }
        }
        if(!result.mapped&&result.reason.isEmpty())result.reason=QStringLiteral("可以继续编辑源码或打开完整 PDF；逐张对应需要完整编译和有效的定位信息。");
      }
      if(!qApp)return;
      QMetaObject::invokeMethod(qApp,[guard,token,ticket,document,result=std::move(result)]()mutable{
        if(!guard)return;
        guard->busy=false;
        if(guard->enabled&&token->load()==ticket&&!result.cancelled)guard->accept(std::move(result),document);
        if(guard->enabled&&(guard->pending||guard->dirty)){guard->pending=false;guard->parseTimer.start();}
      },Qt::QueuedConnection);
    }));
  }
  void remember() {
    if(selected>=0&&selected<deck.frames.size()){const auto &f=deck.frames[selected];rememberFile=f.file;rememberTitle=f.title;rememberStart=f.start;}
  }
  void accept(SlideDeck value,QSharedPointer<QtPDF::Backend::Document> document) {
    QScopedValueRollback<bool> applyingScope(applying,true);
    auto *slideList=static_cast<SlideList*>(list);const auto keys=slideMotionKeys(value);slideList->reflow->prepare(slideMotionKeys(deck),keys);
    const bool preservePreamble=inPreamble&&!fullSourceSelected,initial=rememberFile.isEmpty();
    // The visible source slice has a live cursor anchor. A title edit changes
    // its name, so matching another empty/duplicate title can select a different
    // frame and clamp the typing caret into it when the parse completes.
    int sourceAnchor=-1;const QString sourceFile=path(window->fileName());
    if(window->editor()->hasFocus()&&!preservePreamble&&!fullSourceSelected)for(const auto &v:sourceViews)
      if(v.editor==window->editor()&&v.partial){sourceAnchor=v.first.position();break;}
    const int scroll=pendingScroll>=0?pendingScroll:list->verticalScrollBar()->value();const bool restoreScroll=pendingScroll>=0||!initial;pendingScroll=-1;
    const int oldPage=int(preview->currentPage());
    const qreal oldZoom=preview->zoomLevel();QPointF readingPoint;bool hasReadingPoint=false;
    if(previewScene)if(auto *page=previewScene->pageAt(preview->currentPage())){readingPoint=page->mapFromScene(preview->mapToScene(preview->viewport()->rect().center()));hasReadingPoint=true;}
    const auto oldRevision=deck.revision;
    // Keep the last certified page ranges for reading while editing, never for
    // SyncTeX. Transfer only when the exact source ranges still identify a frame.
    if(!value.previewCurrent&&previewScene&&deck.frames.size()==value.frames.size())for(int i=0;i<value.frames.size();++i)
      if(value.frames[i].file==deck.frames[i].file&&value.frames[i].title==deck.frames[i].title){value.frames[i].firstPage=deck.frames[i].firstPage;value.frames[i].lastPage=deck.frames[i].lastPage;}
    deck=std::move(value);dirty=false;stale=!deck.previewCurrent&&bool(previewScene);
    dock->setWindowTitle(words(QStringLiteral("幻灯片 · %1"),QStringLiteral("Slides · %1")).arg(QFileInfo(root).completeBaseName()));dock->setToolTip(root);
    if(oldRevision!=deck.revision){cache.clear();cacheKeys.clear();++thumbSerial;thumbnail->clear();}
    if(!files.files().isEmpty())files.removePaths(files.files());
    if(!files.directories().isEmpty())files.removePaths(files.directories());
    QStringList paths=deck.dependencies,dirs;for(auto it=deck.sources.begin();it!=deck.sources.end();++it)paths.append(it.key());paths.removeDuplicates();for(const auto &file:paths)dirs.append(QFileInfo(file).absolutePath());dirs.removeDuplicates();
    if(!paths.isEmpty())files.addPaths(paths);
    if(!dirs.isEmpty())files.addPaths(dirs);
    int row=0,best=INT_MAX;
    // A submenu owns its menuAction, so QMenu::clear alone retains its widget
    // and action group. Release obsolete owned submenus on every model refresh.
    for(auto *menu:documentMenu->findChildren<QMenu*>(QString(),Qt::FindDirectChildrenOnly))delete menu;
    documentMenu->clear();documentList->clear();for(int i=0;i<deck.document.size();++i){const auto &entry=deck.document[i];QString title=entry.title;if(englishUi()){if(title==QStringLiteral("完整源码"))title=QStringLiteral("Complete source");else if(title==QStringLiteral("导言"))title=QStringLiteral("Preamble");else if(title==QStringLiteral("主题与模板"))title=QStringLiteral("Theme and templates");else title=QStringLiteral("Shared macros");}else title=ui(title);auto *item=new QTreeWidgetItem(documentList,{title});item->setData(0,Qt::UserRole,i);item->setToolTip(0,entry.file+QLatin1Char(':')+QString::number(entry.line));documentMenu->addAction(title+(entry.file==root?QString():QStringLiteral(" · ")+QFileInfo(entry.file).fileName()),this,[this,i]{if(auto *item=documentList->topLevelItem(i))documentList->itemClicked(item,0);});}
    documentMenu->addSeparator();
    auto *profiles=documentMenu->addMenu(words(QStringLiteral("输出版本"),QStringLiteral("Output version")));auto *profileGroup=new QActionGroup(profiles);profileGroup->setExclusive(true);for(int i=0;i<3;++i){const QStringList labels{words(QStringLiteral("使用原有设置"),QStringLiteral("Use original settings")),words(QStringLiteral("投影版 · 仅幻灯片"),QStringLiteral("Projection · slides only")),words(QStringLiteral("讲者版 · 右侧备注"),QStringLiteral("Speaker · notes on right"))};auto *a=profiles->addAction(labels[i],this,[this,i]{presentationProfile(i);});a->setObjectName(QStringLiteral("researchPresentationProfile%1").arg(i));a->setCheckable(true);a->setChecked(i==currentProfile());profileGroup->addAction(a);a->setToolTip(words(QStringLiteral("更新导言区，可撤销；编译后生效"),QStringLiteral("Updates the preamble; undo is available. Compile to apply.")));}
    documentList->setFixedHeight(qBound(34,int(deck.document.size())*29+6,126));
    QSignalBlocker block(list);list->clear();
    for(int i=0;i<deck.frames.size();++i) {
      const auto &f=deck.frames[i];QString title=slideCaption(f);
      QString detail;
      if(f.firstPage&&f.lastPage>f.firstPage)detail=words(f.breaks?QStringLiteral("%1 页"):QStringLiteral("%1 步"),f.breaks?QStringLiteral("%1 pages"):QStringLiteral("%1 steps")).arg(f.lastPage-f.firstPage+1);
      auto *item=new QListWidgetItem(QStringLiteral("%1  %2").arg(i+1,2,10,QLatin1Char('0')).arg(title)+(detail.isEmpty()?QString():QLatin1Char('\n')+detail),list);item->setToolTip(title+QLatin1Char('\n')+f.file+QLatin1Char(':')+QString::number(f.line)+QLatin1Char('\n')+slideDescription(f));
      item->setData(Qt::UserRole+10,title);item->setData(Qt::UserRole+11,detail);
      if(f.file==rememberFile){int score=qAbs(f.start-rememberStart)+(f.title==rememberTitle?0:100000);if(score<best){row=i;best=score;}}
    }
    fillSlideGroups(groups,deck,hasPendingGroup?pendingGroup:groups->currentData().toString());hasPendingGroup=false;

    filterSlides();
    if(preferredStart>=0){for(int i=0;i<deck.frames.size();++i){const auto &f=deck.frames[i];if(f.file==preferredFile&&preferredStart>=f.start&&preferredStart<f.end){row=i;break;}}preferredStart=-1;preferredFile.clear();}
    else if(sourceAnchor>=0)for(int i=0;i<deck.frames.size();++i){const auto &f=deck.frames[i];if(f.file==sourceFile&&sourceAnchor>=f.start&&sourceAnchor<f.end){row=i;break;}}
    notesFrameAnchors.clear();
    if(row!=selected)selectedStep=1;
    selected=deck.frames.isEmpty()?-1:row;
    if(pendingNotesStep>0){selectedStep=pendingNotesStep;pendingNotesStep=-1;}
    const bool newPreview=deck.previewCurrent&&document&&loadedRevision!=deck.revision;
    if(newPreview) {
      previewScene=QSharedPointer<QtPDF::PDFDocumentScene>::create(document,nullptr,72.0,72.0);
      previewScene->setWatchForDocumentChangesOnDisk(false);preview->setScene(previewScene);preview->setSinglePageMode();
      preview->setBackgroundBrush(QColor(QStringLiteral("#eeeef0")));
      document->setPaperColor(Tw::Settings().value(QStringLiteral("pdfPaperColor"),QColor(Qt::white)).value<QColor>());
      loadedRevision=deck.revision;
    }
    if(!deck.frames.isEmpty()){list->setCurrentRow(row);select(row,false,!preservePreamble);}
    else {position->clear();step->setEnabled(false);releaseSourceViews();}
    list->doItemsLayout();if(restoreScroll)list->verticalScrollBar()->setValue(scroll);else if(list->currentItem())list->scrollToItem(list->currentItem());
    slideList->reflow->complete(keys);
    if(overviewWindow)overviewWindow->refresh(deck,preview->document().toStrongRef(),stale,selected);
    restorePastedSelection();
    if((focusNotes||(notesPane&&notesPane->isVisible()))&&!notesChanged){const bool restoreFocus=focusNotes;focusNotes=false;openNotes();if(restoreFocus&&notesPane)if(auto *editor=notesPane->findChild<QPlainTextEdit *>())editor->setFocus();}
    if(notesNavigate){notesNavigate=false;select(selected,true);}
    if(window->property("researchRestoreSlidePlace").isValid())QTimer::singleShot(0,this,[this]{const auto state=window->property("researchRestoreSlidePlace").toMap();window->setProperty("researchRestoreSlidePlace",QVariant());if(!state.isEmpty())restoreSlideReadingState(window,state);});
    updateStatus();attachPdf();if(overviewAfterNotes){overviewAfterNotes=false;showOverview();}if(initial)cursor(window);else if(preservePreamble){inPreamble=true;list->clearSelection();if(auto *w=sourceWindow(documentFile))showSource(w,0,deck.preambleEnd.value(documentFile,w->editor()->document()->characterCount()-1));updatePosition();}
    if(deck.mapped)showPage();
    if(newPreview){
      const int physical=deck.mapped&&selected>=0?deck.frames[selected].firstPage+selectedStep-2:qBound(0,oldPage,int(document->numPages())-1);
      if(hasReadingPoint){preview->setZoomLevel(oldZoom);preview->goToPage(physical,readingPoint);}
      else{preview->goToPage(physical,Qt::AlignCenter);fitPreview();}
    }
    updatePosition();
    if(deck.mapped&&!compiling&&!failed) {
      const bool announce=notifyUpdate&&newPreview;notifyUpdate=false;
      if(announce)showTransientStatusMessage(window,words(QStringLiteral("已更新"),QStringLiteral("Updated")));
    }
  }
  void updatePosition() {
    for(const auto &entry:QList<QPair<QString,bool>>{{QStringLiteral("researchSourcePreamble"),inPreamble},{QStringLiteral("researchSourceFull"),fullSourceSelected}})
      if(auto *button=window->findChild<QToolButton *>(entry.first);button&&button->property("researchActiveScope").toBool()!=entry.second){button->setProperty("researchActiveScope",entry.second);button->style()->unpolish(button);button->style()->polish(button);button->update();}
    if(fullSourceButton){fullSourceButton->setText(fullSourceSelected?words(QStringLiteral("返回单页"),QStringLiteral("Back to slide")):words(QStringLiteral("全文"),QStringLiteral("Full source")));configureButton(fullSourceButton,ButtonRole::Text);fullSourceButton->setToolTip(fullSourceSelected?words(QStringLiteral("恢复只编辑当前幻灯片"),QStringLiteral("Edit only the current slide")):words(QStringLiteral("编辑当前文件全部源码；点击幻灯片只定位；Ctrl+End 前往文末"),QStringLiteral("Edit the full current file; slide navigation keeps it visible. Ctrl+End goes to the end.")));}
    if(selected>=0&&selected<deck.frames.size()){const auto &f=deck.frames[selected];position->setToolTip(slideCaption(f)+QLatin1Char('\n')+QDir(QFileInfo(root).absolutePath()).relativeFilePath(f.file));}
    else position->setToolTip(status->toolTip());
    if(!deck.mapped){
      QString title=inPreamble?words(QStringLiteral("导言"),QStringLiteral("Preamble")):fullSourceSelected?words(QStringLiteral("完整源码"),QStringLiteral("Complete source")):selected>=0&&selected<deck.frames.size()?words(QStringLiteral("源码第 %1 / %2 张"),QStringLiteral("Source slide %1 / %2")).arg(selected+1).arg(deck.frames.size()):words(QStringLiteral("源码"),QStringLiteral("Source"));
      if(previewScene)if(auto document=preview->document().toStrongRef())title+=words(QStringLiteral(" · PDF 第 %1 / %2 页"),QStringLiteral(" · PDF page %1 / %2")).arg(preview->currentPage()+1).arg(document->numPages());
      position->setText(title);position->setAccessibleName(title);return;
    }
    if(selected<0||selected>=deck.frames.size())return;
    const auto &f=deck.frames[selected];const int count=f.firstPage?f.lastPage-f.firstPage+1:0;
    const auto title=words(QStringLiteral("第 %1 / %2 张"),QStringLiteral("Slide %1 / %2")).arg(selected+1).arg(deck.frames.size());
    position->setText(title);
    position->setAccessibleName(title+(count>1?words(f.breaks?QStringLiteral(" · 第 %1 / %2 页"):QStringLiteral(" · 第 %1 / %2 步"),f.breaks?QStringLiteral(" · page %1 / %2"):QStringLiteral(" · step %1 / %2")).arg(selectedStep).arg(count):QString()));
    if(inPreamble)position->setText(words(QStringLiteral("导言 · 预览第 %1 张"),QStringLiteral("Preamble · preview slide %1")).arg(selected+1));
    if(fullSourceSelected)position->setText(words(QStringLiteral("完整源码 · 预览第 %1 张"),QStringLiteral("Complete source · preview slide %1")).arg(selected+1));
  }
  void select(int row,bool navigate,bool reveal=true) {
    if(navigate)rememberReadingPlace(window);
    if(row<0||row>=deck.frames.size())return;
    if((navigate||row!=selected)&&notesPane&&notesChanged){const auto destination=deck.frames[row];QTextCursor anchor;if(row<notesFrameAnchors.size())anchor=notesFrameAnchors[row];if(anchor.isNull())if(auto *target=sourceWindow(destination.file)){anchor=QTextCursor(target->textDoc());anchor.setPosition(destination.start);}if(commitNotes&&!commitNotes()){QSignalBlocker block(list);list->setCurrentRow(selected);return;}if(dirty){preferredFile=destination.file;preferredStart=anchor.isNull()?destination.start:anchor.position();notesNavigate=navigate;pendingNotesStep=-1;parseTimer.start(0);return;}}
    if(dirty&&!notesFrameAnchors.isEmpty()&&(navigate||row!=selected)){preferredFile=deck.frames[row].file;preferredStart=row<notesFrameAnchors.size()&&!notesFrameAnchors[row].isNull()?notesFrameAnchors[row].position():deck.frames[row].start;notesNavigate=navigate;pendingNotesStep=-1;parseTimer.start(0);return;}
    const bool followNotes=row!=selected&&notesPane&&notesPane->isVisible()&&!notesChanged;
    // Full-source editing remains active while navigating slides.
    if(selected!=row)selectedStep=1;
    selected=row;remember();
    inPreamble=false;documentList->clearSelection();
    // Opening a child emits document-change notifications to this controller.
    // Keep the validated destination and navigation decision across that refresh.
    const bool navigationReady=navigate&&!dirty&&!expandedPreview;
    const auto f=deck.frames[row];const int count=f.firstPage?f.lastPage-f.firstPage+1:0;
    auto *target=sourceWindow(f.file);
    if(!target&&navigationReady){target=TeXDocumentWindow::openDocument(f.file,false,false);if(target){target->setGeometry(window->geometry());if(window->isMaximized())target->showMaximized();}}
    SlideSourceEdit sourceNavigation(target);
    if(reveal&&target&&!fullSourceSelected&&!expandedPreview)showSource(target,f.start,f.end,!f.sliceable);
    {QSignalBlocker b(step);step->setRange(1,qMax(1,count));selectedStep=qBound(1,selectedStep,qMax(1,count));step->setValue(selectedStep);step->setSuffix(count?QStringLiteral(" / %1").arg(count):QString());step->setEnabled(count>0);}
    previousStep->setEnabled(count>0&&selectedStep>1);nextStep->setEnabled(count>0&&selectedStep<count);
    previousStep->setToolTip(words(f.breaks?QStringLiteral("上一页"):QStringLiteral("上一步"),f.breaks?QStringLiteral("Previous page"):QStringLiteral("Previous step")));nextStep->setToolTip(words(f.breaks?QStringLiteral("下一页"):QStringLiteral("下一步"),f.breaks?QStringLiteral("Next page"):QStringLiteral("Next step")));previousStep->setAccessibleName(previousStep->toolTip());nextStep->setAccessibleName(nextStep->toolTip());
    step->setVisible(count>1);previousStep->setVisible(count>1);nextStep->setVisible(count>1);
    updatePosition();
    if(navigationReady) {
      editTarget=target;
      if(target){carryContext(target);target->goToLine(f.line,0,0);if(target!=window)target->selectWindow();}

      showPage();
    }
    if(expandedPreview)showPage();
    thumbTimer.start();if(followNotes)openNotes();
  }
  void cursor(TeXDocumentWindow *w) {
    if(!enabled||dirty||busy||changingSourceView)return;
    const auto file=path(w->fileName());const int p=w->cursorPosition();
    for(int i=0;i<deck.frames.size();++i){const auto &f=deck.frames[i];if(f.file==file&&p>=f.start&&p<f.end){if(i!=selected||inPreamble){QSignalBlocker b(list);list->setCurrentRow(i);select(i,false);showPage();}return;}}
    if(fullSourceSelected){inPreamble=false;updatePosition();return;}
    if(file==root&&!deck.frames.isEmpty()&&w->textCursor().blockNumber()+1<deck.frames.first().line){inPreamble=true;documentFile=file;if(!fullSourceSelected)showSource(w,0,deck.preambleEnd.value(file,w->editor()->document()->characterCount()-1));list->clearSelection();updatePosition();}
  }
  void attachPdf() {
    if(!enabled)return;
    auto *p=window->pdfDocument();
    if(p==viewer)return;
    for(const auto &c:pdfConnections)disconnect(c);
    pdfConnections.clear();viewer=p;
    if(!p)return;
    pdfConnections.append(connect(p->widget(),&QtPDF::PDFDocumentView::changedPage,this,[this](auto page){
      pageChanged(int(page));
    }));
    pdfConnections.append(connect(p,&PDFDocumentWindow::reloaded,this,[this]{if(enabled)parseTimer.start(0);}));
  }
  void showPage() {
    if(!enabled||selected<0||!deck.frames[selected].firstPage)return;
    attachPdf();
    const auto &f=deck.frames[selected];const int page=f.firstPage+selectedStep-2;
    if(previewScene&&int(preview->currentPage())!=page)preview->goToPage(page,Qt::AlignCenter);
    if(!stale&&!dirty&&deck.mapped&&viewer&&path(viewer->fileName())==path(deck.pdf)) {
      if(int(viewer->widget()->currentPage())!=page)viewer->widget()->goToPage(page);
    }
  }
  void pageChanged(int page) {
    if(!enabled)return;
    if(!deck.mapped){updatePosition();return;}
    if(dirty||applying||compiling)return;
    const int physical=page+1;
    QScopedValueRollback<bool> updating(applying,true);
    for(int i=0;i<deck.frames.size();++i){const auto &f=deck.frames[i];if(physical>=f.firstPage&&physical<=f.lastPage){
      const int destinationStep=physical-f.firstPage+1;QSignalBlocker b(list);list->setCurrentRow(i);select(i,false);if(dirty){pendingNotesStep=destinationStep;return;}if(selected==i)selectedStep=destinationStep;showPage();return;
    }}
  }
  void reverse(int page,const QPointF &point) {
    if(!enabled||dirty||stale||!deck.mapped||!previewScene)return;
    setExpandedPreview(false);
    // Reuse the existing viewer's guarded, deferred SyncTeX path. An explicit
    // inverse click may focus the source; passive page changes never do.
    attachPdf();
    if(viewer&&path(viewer->fileName())==path(deck.pdf))viewer->widget()->triggerContextClick(page,point);
  }
  void edit(SlideEdit operation) {
    if(dirty||busy){showTransientStatusMessage(window,words(QStringLiteral("正在更新目录，请稍后重试"),QStringLiteral("Updating slides. Please try again shortly.")));return;}
    if(selected<0&&operation!=SlideEdit::Insert)return;
    auto *target=selected>=0?sourceWindow(deck.frames[selected].file):window;
    if(!target){showTransientStatusMessage(window,words(QStringLiteral("请先打开该帧的源文件，再执行整张操作。"),QStringLiteral("Open this slide's source file before editing it.")),6000);return;}
    QString reason;const auto rows=selection();const bool changed=rows.size()>1&&operation!=SlideEdit::Insert?editSlideSelection(target,deck,rows,operation,-1,&reason):editSlide(target,deck,selected,operation,&reason);
    if(changed){editTarget=target;preferredFile=path(target->fileName());preferredStart=target->cursorPosition();parseTimer.start(0);}
    else if(!reason.isEmpty())showTransientStatusMessage(window,words(reason,QStringLiteral("Slide edit blocked: labels, dynamic structure, intervening commands or a file boundary. Edit the source explicitly.")),8000);
  }
  void render() {
    if(!enabled||!images->isChecked()||dirty||!deck.mapped||selected<0||thumbBusy)return;
    const auto &f=deck.frames[selected];const int page=f.firstPage+selectedStep-2;
    const auto key=deck.revision+QLatin1Char(':')+QString::number(page);
    if(auto *cached=cache.object(key)){thumbnail->setPixmap(QPixmap::fromImage(*cached));return;}
    auto document=preview->document().toStrongRef();if(!document)return;
    thumbBusy=true;const auto serial=++thumbSerial;const auto token=epoch;const auto ticket=token->load();QPointer<SlideController> guard(this);
    slidePool().start(QRunnable::create([guard,document,page,key,serial,token,ticket]{
      QImage img;
      if(token->load()==ticket)if(auto p=document->page(page).toStrongRef()) {
        const auto size=p->pageSizeF();if(size.width()>0&&size.height()>0) {
          const double dpi=qMin(220.0/size.width(),140.0/size.height())*72.0;
          if(token->load()==ticket)img=p->renderToImage(dpi,dpi,QRect(),false);
        }
      }
      if(!qApp)return;
      QMetaObject::invokeMethod(qApp,[guard,img,key,serial,token,ticket]{if(!guard)return;guard->thumbBusy=false;
        if(guard->enabled&&token->load()==ticket&&guard->thumbSerial==serial&&!img.isNull()) {
          while(guard->cacheKeys.size()>=8){guard->cache.remove(guard->cacheKeys.takeFirst());}
          guard->cacheKeys.append(key);guard->cache.insert(key,new QImage(img),qMax(1,int(img.sizeInBytes()/1024)));
          guard->thumbnail->setPixmap(QPixmap::fromImage(img));guard->window->setProperty("researchSlideThumbnailCache",guard->cacheKeys.size());
        } else if(guard->enabled)guard->thumbTimer.start();
      },Qt::QueuedConnection);
    }));
  }
};
SlideController *controller(TeXDocumentWindow *w) {return w?dynamic_cast<SlideController *>(w->findChild<QObject *>(QStringLiteral("researchSlides"))):nullptr;}
}

bool commitSlideNotes(TeXDocumentWindow *window) {
  if(!window)return true;for(auto *owner:TeXDocumentWindow::documentList())if(auto *c=controller(owner);c&&c->notesChanged&&(owner==window||c->notesFile==path(window->fileName()))&&c->commitNotes&&!c->commitNotes()){if(c->previewDock)c->previewDock->show();return false;}return true;
}
bool hasPendingSlideNotes(TeXDocumentWindow *window) {
  if(!window)return false;
  for(auto *owner:TeXDocumentWindow::documentList())if(auto *c=controller(owner);c&&c->notesChanged&&(owner==window||c->notesFile==path(window->fileName())))return true;
  return false;
}
void setSlideFocusMode(TeXDocumentWindow *w,bool focused) {
  if(!w)return;auto *action=w->findChild<QAction *>(QStringLiteral("researchSlideModeAction"));if(!action)return;
  auto *c=controller(w);
  if(focused){
    w->setProperty("researchSlideDetectionSerial",w->property("researchSlideDetectionSerial").toInt()+1);
    if(c&&c->enabled){c->resumeAfterFocus=true;c->focusDirectory=c->dock->property("researchFocusRestore").toBool();c->focusPreview=c->previewDock->property("researchFocusRestore").toBool();c->focusNotes=c->notesPane&&!c->notesPane->isHidden();const auto root=w->textDoc()->property("researchPresentationRoot"),source=w->textDoc()->property("researchPresentationSource");action->setChecked(false);w->textDoc()->setProperty("researchPresentationRoot",root);w->textDoc()->setProperty("researchPresentationSource",source);}
  }
  action->setEnabled(!focused);action->setVisible(!focused);
  if(auto *detect=w->findChild<QAction *>(QStringLiteral("researchDetectBeamerAction"))){detect->setEnabled(!focused);detect->setVisible(!focused);}
  if(auto *mode=w->findChild<QAction *>(QStringLiteral("researchModeControl")))mode->setVisible(!focused&&c);
  if(!focused){if(c&&c->resumeAfterFocus){c->resumeAfterFocus=false;action->setChecked(true);c->dock->setVisible(c->focusDirectory);c->previewDock->setVisible(c->focusPreview);}else detectSlideMode(w);}
}
QVariantMap slideReadingState(TeXDocumentWindow *w){if(auto *c=controller(w))return {{QStringLiteral("enabled"),c->enabled},{QStringLiteral("full"),c->fullSourceSelected},{QStringLiteral("preamble"),c->inPreamble},{QStringLiteral("selected"),c->selected},{QStringLiteral("step"),c->selectedStep},{QStringLiteral("root"),c->root},{QStringLiteral("savedCursor"),!c->fullSourceSelected&&!c->inPreamble}};return {{QStringLiteral("enabled"),false}};}
void restoreSlideReadingState(TeXDocumentWindow *w,const QVariantMap &state){
 if(!w||state.isEmpty()||w->property("researchFocus").toBool())return;
 auto *c=controller(w);
 if(!state.value(QStringLiteral("enabled")).toBool()){w->setProperty("researchRestoreSlidePlace",QVariant());if(c&&c->enabled)c->toggle->setChecked(false);return;}
 if(!c||!c->enabled){
   const auto root=state.value(QStringLiteral("root")).toString();if(!root.isEmpty()){w->textDoc()->setProperty("researchPresentationSource",w->textDoc()->absoluteFilePath());w->textDoc()->setProperty("researchPresentationRoot",root);}
   if(auto *action=w->findChild<QAction*>(QStringLiteral("researchSlideModeAction")))action->setChecked(true);c=controller(w);
 }
 if(!c||!c->enabled)return;
 if(c->busy||c->dirty){w->setProperty("researchRestoreSlidePlace",state);return;}
 const bool full=state.value(QStringLiteral("full")).toBool(),preamble=state.value(QStringLiteral("preamble")).toBool();int selected=state.value(QStringLiteral("selected")).toInt();
 if(state.value(QStringLiteral("savedCursor")).toBool()){
   selected=-1;const auto file=path(w->fileName());const int offset=w->textCursor().position();
   for(int i=0;i<c->deck.frames.size();++i){const auto &frame=c->deck.frames[i];if(frame.file==file&&offset>=frame.start&&offset<=frame.end){selected=i;break;}}
 }
 if(selected>=0&&selected<c->deck.frames.size()){
   c->selected=selected;c->selectedStep=state.value(QStringLiteral("step"),1).toInt();
   {QSignalBlocker block(c->list);c->list->setCurrentRow(selected);}c->select(selected,false,false);c->showPage();
 }
 c->fullSourceSelected=full||(!preamble&&selected<0);c->inPreamble=preamble;
 if(c->fullSourceSelected)c->showSource(w,0,0,true);else if(preamble)c->showSource(w,0,c->deck.preambleEnd.value(path(w->fileName())));else if(selected>=0&&selected<c->deck.frames.size()){const auto &f=c->deck.frames[selected];c->showSource(w,f.start,f.end,!f.sliceable);}
 c->updatePosition();if(state.contains(QStringLiteral("scroll")))w->editor()->verticalScrollBar()->setValue(state.value(QStringLiteral("scroll")).toInt());
}
void installSlideMode(TeXDocumentWindow *w) {
  auto *action=new QAction(ui(QStringLiteral("Beamer 幻灯片模式")),w);action->setObjectName(QStringLiteral("researchSlideModeAction"));action->setCheckable(true);
  action->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+B")));w->addAction(action);
  for(auto *a:w->menuBar()->actions())if(a->menu()&&a->menu()->objectName()==QStringLiteral("menuFormat"))a->menu()->addAction(action);
  // Some upstream menu packs call View "Format". Keep an ordinary visible
  // entry in Search too, beside the existing source navigation.
  bool added=false;for(auto *a:w->menuBar()->actions())if(a->menu()&&a->menu()->actions().contains(action))added=true;
  if(!added)for(auto *a:w->menuBar()->actions())if(a->menu()&&a->menu()->objectName()==QStringLiteral("menuSearch"))a->menu()->addAction(action);
    QObject::connect(action,&QAction::toggled,w,[w,action](bool on){if(on&&w->property("researchFocus").toBool()){QSignalBlocker block(action);action->setChecked(false);return;}auto *c=controller(w);if(!c&&on)c=new SlideController(w,action);if(c)c->activate(on);
      if(auto *b=w->findChild<QToolButton *>(QStringLiteral("researchModeButton"))){b->setVisible(!w->property("researchFocus").toBool());b->setText(ui(on?QStringLiteral("幻灯片"):QStringLiteral("源码")));}
      if(auto *a=w->findChild<QAction *>(QStringLiteral("researchModeControl")))a->setVisible(!w->property("researchFocus").toBool());
    });
  // Check only once after the document has loaded, on a worker. Ordinary papers
  // do not start a slide controller or a recurring parser. A manual choice wins.
  QObject::connect(action,&QAction::triggered,w,[w]{w->setProperty("researchSlideManualChoice",true);});
  QTimer::singleShot(0,w,[w,action]{
    if(auto *menu=w->findChild<QMenu *>(QStringLiteral("researchReadingMenu")))if(!menu->actions().contains(action))menu->addAction(action);
    if(w->property("researchFocus").toBool()||Tw::Settings().value(QStringLiteral("researchFocusMode"),false).toBool())setSlideFocusMode(w,true);else detectSlideMode(w);
  });
}
void detectSlideMode(TeXDocumentWindow *w) {
  if(!w||w->property("researchFocus").toBool()||Tw::Settings().value(QStringLiteral("researchFocusMode"),false).toBool())return;if(auto *c=controller(w);c&&c->enabled)c->invalidate(false);
  if(!Tw::Settings().value(QStringLiteral("researchDetectBeamer"),true).toBool()||w->property("researchSlideManualChoice").toBool())return;
  const auto root=path(w->untitled()?w->fileName():w->textDoc()->getRootFilePath());const auto text=w->untitled()?w->textDoc()->editorText():QString();const bool unsaved=w->untitled();const int serial=w->property("researchSlideDetectionSerial").toInt()+1;w->setProperty("researchSlideDetectionSerial",serial);QPointer<TeXDocumentWindow> guard(w);
  slidePool().start(QRunnable::create([guard,root,text,unsaved,serial]{const bool isBeamer=QRegularExpression(QStringLiteral(R"(\\documentclass\s*(?:\[[^\]]*\]\s*)?\{(?:beamer|ctexbeamer)\})")).match(maskProjectText(unsaved?text:read(root))).hasMatch();if(qApp)QMetaObject::invokeMethod(qApp,[guard,serial,isBeamer]{if(guard&&isBeamer&&!guard->property("researchFocus").toBool()&&!Tw::Settings().value(QStringLiteral("researchFocusMode"),false).toBool()&&guard->property("researchSlideDetectionSerial").toInt()==serial&&!guard->property("researchSlideManualChoice").toBool())if(auto *a=guard->findChild<QAction *>(QStringLiteral("researchSlideModeAction")))a->setChecked(true);},Qt::QueuedConnection);}));
}
void slideBuildStarted(TeXDocumentWindow *w) {for(auto *window:TeXDocumentWindow::documentList())if(auto *c=controller(window);c&&c->enabled&&(window==w||c->root==path(w->textDoc()->getRootFilePath()))){c->epoch->fetch_add(1);c->notifyUpdate=false;c->compiling=true;c->failed=false;c->updateStatus();}}
void slideBuildFinished(TeXDocumentWindow *w,bool accepted,bool failed) {
  for(auto *window:TeXDocumentWindow::documentList())if(auto *c=controller(window);c&&c->enabled&&(window==w||c->root==path(w->textDoc()->getRootFilePath()))) {
    c->epoch->fetch_add(1);c->notifyUpdate=accepted;c->compiling=false;c->failed=failed;c->stale=!accepted;c->remember();c->updateStatus();if(accepted)c->parseTimer.start(0);
  }
}
bool toggleSlideDirectory(TeXDocumentWindow *w){if(auto *c=controller(w);c&&c->enabled&&c->directoryAction){c->directoryAction->trigger();return true;}return false;}
void slidePreviewAvailable(TeXDocumentWindow *w) {if(auto *c=controller(w);c&&c->enabled){c->attachPdf();c->showPage();c->thumbTimer.start();}}

} // namespace Research

