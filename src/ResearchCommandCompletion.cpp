// Opt-in local command assistance; no worker, network or idle polling.
// GPL-2.0-or-later.
#include "ResearchCommandCompletion.h"
#include "ResearchAppearance.h"
#include "ResearchProject.h"
#include "ResearchCompletion.h"
#include "ResearchShorthand.h"
#include "TeXDocumentWindow.h"
#include "CompletingEdit.h"
#include "Settings.h"
#include "TWApp.h"
#include <QActionGroup>
#include <QApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QInputMethodEvent>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QListView>
#include <QMainWindow>
#include <QMap>
#include <QLabel>
#include <QMouseEvent>
#include <QMenu>
#include <QPointer>
#include <QPlainTextEdit>
#include <QPainter>
#include <QScreen>
#include <QScrollBar>
#include <QSet>
#include <QStringListModel>
#include <QStyledItemDelegate>
#include <QShortcut>
#include <QStyle>
#include <QTextBlock>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <atomic>
#include <memory>

namespace Research {
namespace {
const QString modeKey = QStringLiteral("researchCommandCompletionMode");
const QString usageKey = QStringLiteral("researchCommandCompletionUsage");
int completionMode() { return qBound(0, Tw::Settings().value(modeKey, 0).toInt(), 2); }
bool letter(QChar c) { return (c >= QLatin1Char('A') && c <= QLatin1Char('Z')) || (c >= QLatin1Char('a') && c <= QLatin1Char('z')) || c == QLatin1Char('@'); }
const QStringList &standardCommands() {
  static const QStringList values = QStringLiteral("le leq ge geq neq infty sum prod int oint iint iiint lVert rVert langle rangle rightarrow leftarrow Longrightarrow implies iff mathbb mathcal mathfrak mathsf mathtt mathrm mathit mathnormal operatorname mathbf boldsymbol frac dfrac tfrac binom dbinom tbinom sqrt left right alpha beta gamma delta epsilon varepsilon zeta eta theta vartheta iota kappa varkappa lambda mu nu xi pi varpi rho varrho sigma varsigma tau upsilon phi varphi chi psi omega Gamma Delta Theta Lambda Xi Pi Sigma Upsilon Phi Psi Omega partial nabla ell hbar Re Im wp aleph dots ldots cdots vdots ddots in notin ni subset subseteq supset supseteq approx equiv sim simeq cong propto prec preceq succ succeq perp parallel mid nmid forall exists nexists neg emptyset varnothing top bot angle triangle diamond oplus otimes odot cap cup bigcap bigcup uplus setminus sqcup sqcap wedge vee cdot times div pm mp star circ bullet sin cos tan cot sec csc sinh cosh tanh coth arcsin arccos arctan arg det dim exp gcd hom inf ker lg lim liminf limsup ln log max min Pr sup hat widehat tilde widetilde bar vec dot ddot overline underline overbrace underbrace overset underset displaystyle textstyle scriptstyle scriptscriptstyle").split(QLatin1Char(' '));
  return values;
}
const QSet<QString> &commonCommands() {
  static const QSet<QString> values = [] {
      QSet<QString> result;
      for (const auto &name : QStringLiteral("le leq ge geq neq infty sum prod int lVert rVert langle rangle rightarrow leftarrow implies iff mathbb mathcal operatorname mathbf boldsymbol frac sqrt left right label ref eqref cite section subsection begin end text textbf emph item input include").split(QLatin1Char(' '))) result.insert(QLatin1Char('\\')+name);
      for (const auto &name : standardCommands()) result.insert(QLatin1Char('\\')+name);
      return result;
    }();
  return values;
}
struct Entry {
  QString label, text; int caret = 0; bool snippet = false; bool familiar = false;
  QString detail, source, environmentName; QByteArray sourceHash; QVector<SnippetStop> stops;
  int finalPosition{-1}; bool indent{false}, environment{false}, custom{false};
};
class CompletionRows final : public QStringListModel {
public:
  QVector<Entry> entries;
  using QStringListModel::QStringListModel;
  QVariant data(const QModelIndex &i,int role) const override {
    if(i.isValid()&&i.row()<entries.size()){
      const auto &e=entries[i.row()];
      if(role==Qt::UserRole+1||role==Qt::AccessibleDescriptionRole)return e.detail.isEmpty()?e.text:e.detail;
      if(role==Qt::UserRole+2)return !e.source.isEmpty()?e.detail.section(QStringLiteral(" · "),0,0):e.custom?e.detail:QString();
      if(role==Qt::UserRole+3)return e.environment?e.label.section(QStringLiteral(" ↵"),0,0):e.label;
      if(role==Qt::ToolTipRole)return {};
    }
    return QStringListModel::data(i,role);
  }
};
class CompletionDelegate final : public QStyledItemDelegate {
public:
  using QStyledItemDelegate::QStyledItemDelegate;
  QSize sizeHint(const QStyleOptionViewItem &,const QModelIndex &) const override {return {300,28};}
  void paint(QPainter *p,const QStyleOptionViewItem &option,const QModelIndex &i) const override {
    QStyleOptionViewItem item(option);initStyleOption(&item,i);item.text.clear();
    auto *style=option.widget?option.widget->style():QApplication::style();style->drawControl(QStyle::CE_ItemViewItem,&item,p,option.widget);
    p->save();QFont font=option.font;font.setPixelSize(14);p->setFont(smoothTextFont(font));p->setPen(QColor(QStringLiteral("#1d1d1f")));
    const auto rect=option.rect.adjusted(8,0,-24,0);const auto detail=i.data(Qt::UserRole+2).toString();const int detailWidth=detail.isEmpty()?0:qMin(rect.width()/3,130);
    p->drawText(rect.adjusted(0,0,-detailWidth,0),Qt::AlignVCenter,p->fontMetrics().elidedText(i.data(Qt::UserRole+3).toString(),Qt::ElideRight,rect.width()-detailWidth));
    font.setPixelSize(12);p->setFont(smoothTextFont(font));p->setPen(QColor(QStringLiteral("#808088")));if(detailWidth)p->drawText(QRect(rect.right()-detailWidth+1,rect.top(),detailWidth,rect.height()),Qt::AlignRight|Qt::AlignVCenter,p->fontMetrics().elidedText(detail,Qt::ElideMiddle,detailWidth));
    if(option.state&QStyle::State_Selected)p->drawText(QRect(option.rect.right()-21,option.rect.top(),20,option.rect.height()),Qt::AlignCenter,QString(QChar(0x203a)));p->restore();
  }
};
QString completionText(const char *zh,const char *en){return (englishUi()?QString::fromUtf8(en):Research::ui(QString::fromUtf8(zh)));}
Entry snippetEntry(const QString &label,const QString &body,const QString &detail,const QString &selected={}){
  const auto parsed=parseCompletionSnippet(body,selected);Entry e;e.label=label;e.text=parsed.text;e.detail=detail;e.snippet=true;e.familiar=true;e.stops=parsed.stops;e.finalPosition=parsed.finalPosition;e.caret=e.stops.isEmpty()?e.finalPosition:e.stops.first().start;e.indent=true;return e;
}
struct Choice { QString text; int count = 0; };
QVector<Choice> &choices() {
  // Lazily loaded only when assistance is used. Store confirmed expansions,
  // never document text, keystrokes or timestamps. Bound both memory and INI.
  static QVector<Choice> result = [] {
    QVector<Choice> values; QSet<QString> seen;
    const auto stored = Tw::Settings().value(usageKey).toStringList();
    for (const auto &item : stored) {
      const int separator = int(item.indexOf(QLatin1Char('\t')));
      QString text = item.mid(separator+1);
      text.remove(QChar(0x2022)); // normalize preferences saved by the old snippet inserter
      const int count = item.left(separator).toInt();
      static const QRegularExpression abbreviation(QStringLiteral(R"(^[A-Za-z][A-Za-z0-9_-]*$)"));
      if (separator < 1 || count < 1 || text.size() > 512 || (!text.startsWith(QLatin1Char('\\'))&&!abbreviation.match(text).hasMatch()) || seen.contains(text)) continue;
      seen.insert(text); values.append({text, qMin(count, 1024)});
      if (values.size() == 64) break;
    }
    return values;
  }();
  return result;
}
void remember(const Entry &entry) {
  const auto identity=entry.custom?entry.label:entry.text;
  if (identity.size() > 512) return;
  auto &values = choices(); int count = 1;
  for (int i = 0; i < values.size(); ++i) if (values[i].text == identity) {
    count = qMin(1024, values[i].count+1); values.removeAt(i); break;
  }
  values.prepend({identity, count}); if (values.size() > 64) values.removeLast();
  QStringList stored; for (const auto &value : values) stored.append(QString::number(value.count)+QLatin1Char('\t')+value.text);
  Tw::Settings().setValue(usageKey, stored);
}
const QVector<Entry> &catalog() {
  static const QVector<Entry> entries = [] {
    QVector<Entry> result; QSet<QString> seen;
    static const QRegularExpression name(QStringLiteral(R"(^\\[A-Za-z@]+)"));
    auto add = [&](QString expansion, bool snippet) {
      if (expansion.isEmpty() || expansion.size() > 4096) return;
      const int marker = int(expansion.indexOf(QStringLiteral("#INS#")));
      int caret = marker;
      if (marker >= 0) { QString before = expansion.left(marker); before.remove(QChar(0x2022)); caret = int(before.size()); }
      QString label = expansion;
      // The old Tab cycler uses bullets as navigation stops, including a
      // final stop after a complete snippet. They are metadata, never TeX.
      int last = int(label.size())-1;
      while (last >= 0 && label[last].isSpace()) --last;
      while (last >= 0 && label[last] == QChar(0x2022)) { label.remove(last,1); --last; while (last >= 0 && label[last].isSpace()) --last; }
      label.replace(QStringLiteral("#INS#"), QString(QChar(0x2026)));
      label.replace(QChar(0x2022), QChar(0x2026));
      // Keep newline variants distinguishable in a one-line list. Collapsing
      // them into the same caption used to accept a different expansion.
      label.replace(QLatin1Char('\n'), QChar(0x21b5)); label = label.simplified();
      QVector<SnippetStop> stops;QString clean;int number=1,finalPosition=-1;
      for(int p=0;p<expansion.size();){
        if(expansion.mid(p,5)==QStringLiteral("#INS#")){stops.append({number++,int(clean.size()),0});p+=5;}
        else if(expansion[p]==QChar(0x2022)){
          if(expansion.mid(p+1).trimmed().isEmpty())finalPosition=int(clean.size());else stops.append({number++,int(clean.size()),0});++p;
        }else clean+=expansion[p++];
      }
      expansion=clean;
      if (caret < 0) caret = int(expansion.size());
      if (seen.contains(expansion)) return;
      seen.insert(expansion); Entry entry{label, expansion, caret, snippet, commonCommands().contains(name.match(expansion).captured())};entry.stops=stops;entry.finalPosition=finalPosition<0?int(expansion.size()):finalPosition;result.append(entry);
    };
    // Canonical commands are separate from abbreviation expansions. In
    // particular, a legal \le must never silently become \leq.
    for (const auto &item : CompletingEdit::localCompletionEntries()) {
      const auto match = name.match(item.second);
      if (!match.hasMatch()) continue;
      add(match.captured(), false);
      if (item.second.contains(QStringLiteral("#INS#"))) add(item.second, true);
    }
    // Fill the small gaps in the bundled abbreviation table. Do not expose
    // shorthand keys such as \xa as if they were actual TeX commands.
    for (const auto &value : standardCommands()) add(QLatin1Char('\\')+value, false);
    // Small argument templates complement the local abbreviation table.
    // Normalized duplicates reuse its existing caption and insertion point.
    for (const auto &value : QStringLiteral("operatorname mathbb mathcal mathfrak mathsf mathtt mathrm mathit mathnormal mathbf boldsymbol text textbf emph overline underline sqrt").split(QLatin1Char(' ')))
      add(QLatin1Char('\\')+value+QStringLiteral("{#INS#}"), true);
    for (const auto &value : QStringLiteral("frac dfrac tfrac binom dbinom tbinom overset underset").split(QLatin1Char(' ')))
      add(QLatin1Char('\\')+value+QStringLiteral("{#INS#}{\u2022}"), true);
    add(QStringLiteral("\\left(#INS#\\right)"), true);
    add(QStringLiteral("\\left[#INS#\\right]"), true);
    add(QStringLiteral("\\right)"), true);
    add(QStringLiteral("\\right]"), true);
    return result;
  }();
  return entries;
}
struct Lexical { QString environment; bool protectedText = false; };
struct Nearby { QHash<QString, int> usage; QList<QChar> left; };
Lexical lexicalPrefix(const QString &line, int stop, QString environment, Nearby *nearby = nullptr) {
  static const QSet<QString> literal{QStringLiteral("verbatim"), QStringLiteral("verbatim*"), QStringLiteral("Verbatim"), QStringLiteral("BVerbatim"), QStringLiteral("LVerbatim"), QStringLiteral("lstlisting"), QStringLiteral("minted"), QStringLiteral("comment"), QStringLiteral("filecontents"), QStringLiteral("filecontents*")};
  auto argument = [&](int &p, QChar open, QChar close) {
    while (p < stop && line[p].isSpace()) ++p;
    if (p >= stop || line[p] != open) return QString();
    const int first = ++p; int depth = 1;
    while (p < stop) { if (line[p] == QLatin1Char('\\')) { p += 2; continue; } if (line[p] == open) ++depth; if (line[p] == close && --depth == 0) { const auto value = line.mid(first, p-first); ++p; return value; } ++p; }
    return QString();
  };
  int i = 0;
  while (i < stop) {
    if (!environment.isEmpty()) {
      const int end = int(line.indexOf(QStringLiteral("\\end"), i));
      if (end < 0 || end >= stop) return {environment, true};
      int p = end + 4;
      if (p < stop && letter(line[p])) { i = p; continue; }
      if (argument(p, QLatin1Char('{'), QLatin1Char('}')) != environment) { i = p; continue; }
      environment.clear(); i = p; continue;
    }
    if (line[i] == QLatin1Char('%')) return {{}, true};
    if (line[i++] != QLatin1Char('\\')) continue;
    const int first = i; while (i < stop && letter(line[i])) ++i;
    if (i == first) { ++i; continue; } // escaped %, backslash or other control symbol
    const QString command = line.mid(first, i-first);
    if (nearby) {
      QString identity = QLatin1Char('\\')+command;
      int delimiter = i; while (delimiter < stop && line[delimiter].isSpace()) ++delimiter;
      if (command == QStringLiteral("left")) {
        const QChar shape = delimiter < stop ? line[delimiter] : QChar();
        // Unknown left delimiters still nest; don't match an outer '[' to
        // a nearer '\\left\\langle', nor count text in verbatim or comments.
        nearby->left.append(shape);
        if (shape == QLatin1Char('(')) identity = QStringLiteral("\\left(\\right)");
        else if (shape == QLatin1Char('[')) identity = QStringLiteral("\\left[\\right]");
      } else if (command == QStringLiteral("right") && !nearby->left.isEmpty()) nearby->left.removeLast();
      if (nearby->usage.contains(identity) || nearby->usage.size() < 512) ++nearby->usage[identity];
    }
    if (command == QStringLiteral("begin")) {
      int p = i; const auto value = argument(p, QLatin1Char('{'), QLatin1Char('}'));
      if (literal.contains(value)) { environment = value; i = p; }
    } else if (command == QStringLiteral("verb") || command == QStringLiteral("lstinline") || command == QStringLiteral("mintinline")) {
      if (i < stop && line[i] == QLatin1Char('*')) ++i;
      if (command != QStringLiteral("verb")) {
        while (i < stop && line[i].isSpace()) ++i;
        if (i < stop && line[i] == QLatin1Char('[')) argument(i, QLatin1Char('['), QLatin1Char(']'));
        if (command == QStringLiteral("mintinline")) argument(i, QLatin1Char('{'), QLatin1Char('}'));
        while (i < stop && line[i].isSpace()) ++i;
      }
      if (i >= stop) return {{}, true};
      if (command != QStringLiteral("verb") && line[i] == QLatin1Char('{')) {
        const int before = i; argument(i, QLatin1Char('{'), QLatin1Char('}'));
        if (i >= stop && (stop <= before || line[stop-1] != QLatin1Char('}'))) return {{}, true};
      } else {
        const QChar delimiter = line[i++]; const int end = int(line.indexOf(delimiter, i));
        if (end < 0 || end >= stop) return {{}, true};
        i = end + 1;
      }
    }
  }
  return {environment, !environment.isEmpty()};
}
struct Context { int start = -1, end = -1; QString prefix; int kind{0}; QString selection; bool valid() const { return start >= 0; } }; // 0 command, 1 begin, 2 end, 3 abbreviation, 4 selection
class CommandCompletion final : public QObject {
public:
  QMainWindow *window; CompletingEdit *editor;
  QListView *popup = nullptr; CompletionRows *model = nullptr;
  PopupFrame *documentation=nullptr;QPlainTextEdit *documentationText=nullptr;bool showDocumentation{false};
  bool selectionTouched{false},projectPending{false};quint64 indexedEpoch{0};
  QTimer delay; QVector<Entry> candidates; QList<QAction *> modes;
  QPointer<QTextDocument> document; QMetaObject::Connection documentChange;
  QMap<int, QString> checkpoints; quint64 generation = 0;
  int expectedPosition = -1; bool inputTurn = false, composing = false, manualSession = false,mirroring=false;
  Context shownContext; int shownRevision = -1;
  QSharedPointer<const ProjectIndex> project;
  std::shared_ptr<std::atomic<quint64>> projectTicket=std::make_shared<std::atomic<quint64>>(0);
  QVector<SnippetStop> stops; int activeStop{-1}, finalStop{-1}; bool placingStop{false};
  bool imeTracking{false}; int imeCharacters{0},imeStart{0},imeLength{0}; QString imeBefore;
  struct SnippetSession{QVector<SnippetStop> stops;int active,final;};QVector<SnippetSession> outerSnippets;
  explicit CommandCompletion(QMainWindow *w, CompletingEdit *e) : QObject(w), window(w), editor(e), delay(this) {
    setObjectName(QStringLiteral("researchCommandCompletionController")); delay.setSingleShot(true); delay.setTimerType(Qt::PreciseTimer);
    connect(&delay, &QTimer::timeout, this, [this] { show(); });
    ensureDocument(); editor->installEventFilter(this); window->installEventFilter(this);
    connect(editor, &QTextEdit::textChanged, this, [this] { ensureDocument();syncMirrors(); if (!inputTurn&&!mirroring) cancel(); });
    connect(editor, &QTextEdit::cursorPositionChanged, this, [this] { if (!inputTurn&&!mirroring) cancel(); if(!placingStop&&!imeTracking&&activeStop>=0&&!followSnippetCursor())finishSnippet(); });
    connect(editor,&CompletingEdit::inputMethodEditStarted,this,[this](const QInputMethodEvent *event,bool paired){
      if(activeStop<0)return;
      const auto field=stops[activeStop];const int first=editor->textCursor().selectionStart()+event->replacementStart();
      if(paired||!insideStop()||field.length>65536||first<field.start||first+event->replacementLength()>field.start+field.length){finishSnippet();return;}
      imeStart=field.start;imeLength=field.length;imeCharacters=document->characterCount();
      QTextCursor text(document);text.setPosition(imeStart);text.setPosition(imeStart+imeLength,QTextCursor::KeepAnchor);imeBefore=text.selectedText();imeTracking=true;
    });
    connect(editor,&CompletingEdit::inputMethodEditFinished,this,[this]{
      if(!imeTracking)return;imeTracking=false;
      const int length=imeLength+document->characterCount()-imeCharacters;
      if(activeStop<0||length<0||length>65536||imeStart+length>=document->characterCount()){finishSnippet();return;}
      QTextCursor text(document);text.setPosition(imeStart);text.setPosition(imeStart+length,QTextCursor::KeepAnchor);const auto after=text.selectedText();
      int prefix=0,suffix=0;while(prefix<imeBefore.size()&&prefix<after.size()&&imeBefore[prefix]==after[prefix])++prefix;
      while(suffix<imeBefore.size()-prefix&&suffix<after.size()-prefix&&imeBefore[imeBefore.size()-suffix-1]==after[after.size()-suffix-1])++suffix;
      updateSnippetRanges(imeStart+prefix,int(imeBefore.size())-prefix-suffix,int(after.size())-prefix-suffix);imeBefore.clear();syncMirrors();
      if(activeStop>=0&&!insideStop())finishSnippet();if(inputTurn)expectedPosition=editor->textCursor().position();
    });
    for (auto *bar : {editor->verticalScrollBar(), editor->horizontalScrollBar()}) connect(bar, &QScrollBar::valueChanged, this, [this] { if (inputTurn) { if (popup) popup->hide(); } else cancel(); });
    connect(TWApp::instance(), &TWApp::updatedTranslators, this, [this] { cancel(); labels(); });
  }
  void rememberEntry(const Entry &entry){if(!editor->property("researchCompletionSandbox").toBool())remember(entry);}
  int mode() const {return editor->property("researchCompletionSandbox").toBool()?1:completionMode();}
  ~CommandCompletion() override {projectTicket->fetch_add(1);}
  void finishSnippet(){imeTracking=false;imeBefore.clear();stops.clear();outerSnippets.clear();activeStop=-1;finalStop=-1;editor->setProperty("researchSnippetActive",false);editor->setProperty("researchSnippetMirrored",false);}
  void refreshSnippetMirrors(){
    auto repeated=[](const QVector<SnippetStop> &ranges){QSet<int> seen;for(const auto &s:ranges){if(seen.contains(s.number))return true;seen.insert(s.number);}return false;};
    bool mirrored=activeStop>=0&&repeated(stops);for(const auto &outer:outerSnippets)mirrored|=repeated(outer.stops);
    editor->setProperty("researchSnippetMirrored",mirrored);
  }
  void applySnippetChange(int position,int removed,int added,int session,int target){
    const int delta=added-removed;
    auto adjust=[&](QVector<SnippetStop> &ranges,int edited){for(int n=0;n<ranges.size();++n){if(n==edited)ranges[n].length+=delta;else if(ranges[n].start>=position+removed)ranges[n].start+=delta;}};
    adjust(stops,session<0?target:-1);
    if(finalStop>=position+removed)finalStop+=delta;
    for(int i=0;i<outerSnippets.size();++i){auto &outer=outerSnippets[i];const auto field=outer.stops[outer.active];
      const int edited=i==session?target:(field.start<=position&&position+removed<=field.start+field.length?outer.active:-1);
      adjust(outer.stops,edited);if(outer.final>=position+removed)outer.final+=delta;
    }
  }
  void updateSnippetRanges(int position,int removed,int added){
    if(activeStop<0||(!removed&&!added))return;const auto active=stops[activeStop];
    if(position<active.start||position+removed>active.start+active.length){finishSnippet();return;}
    applySnippetChange(position,removed,added,-1,activeStop);
  }
  void syncMirrors(){
    if(activeStop<0||mirroring||placingStop||imeTracking||composing)return;
    mirroring=true;placingStop=true;auto caret=editor->textCursor();const int caretOffset=caret.position()-stops[activeStop].start,anchorOffset=caret.anchor()-stops[activeStop].start;QTextCursor batch(document);bool grouped=false;
    auto sync=[&](int session){
      auto &ranges=session<0?stops:outerSnippets[session].stops;const int active=session<0?activeStop:outerSnippets[session].active;
      const auto primary=ranges[active];QTextCursor source(document);source.setPosition(primary.start);source.setPosition(primary.start+primary.length,QTextCursor::KeepAnchor);
      const auto value=source.selectedText().replace(QChar::ParagraphSeparator,QLatin1Char('\n'));if(value.size()>65536)return;
      QVector<int> targets;for(int n=0;n<ranges.size();++n)if(n!=active&&ranges[n].number==primary.number)targets.append(n);
      std::sort(targets.begin(),targets.end(),[&](int a,int b){return ranges[a].start>ranges[b].start;});
      for(int n:targets){const auto field=ranges[n];QTextCursor target(document);target.setPosition(field.start);target.setPosition(field.start+field.length,QTextCursor::KeepAnchor);
        if(target.selectedText().replace(QChar::ParagraphSeparator,QLatin1Char('\n'))==value)continue;
        if(!grouped){batch.joinPreviousEditBlock();grouped=true;}target.insertText(value);applySnippetChange(field.start,field.length,int(value.size()),session,n);
      }
    };
    sync(-1);for(int i=int(outerSnippets.size())-1;i>=0;--i)sync(i);
    if(grouped){batch.endEditBlock();const auto field=stops[activeStop];caret.setPosition(field.start+qBound(0,anchorOffset,field.length));caret.setPosition(field.start+qBound(0,caretOffset,field.length),QTextCursor::KeepAnchor);editor->setTextCursor(caret);}
    placingStop=false;mirroring=false;if(inputTurn)expectedPosition=editor->textCursor().position();
  }
  bool insideStop()const{if(activeStop<0||activeStop>=stops.size())return false;const auto c=editor->textCursor();const auto s=stops[activeStop];return c.selectionStart()>=s.start&&c.selectionEnd()<=s.start+s.length;}
  bool followSnippetCursor(){if(insideStop())return true;const auto c=editor->textCursor();for(int n=0;n<stops.size();++n)if(c.selectionStart()>=stops[n].start&&c.selectionEnd()<=stops[n].start+stops[n].length){activeStop=n;return true;}return false;}
  void jumpStop(int direction){
    if(activeStop<0)return;int next=activeStop+direction;const int number=stops[activeStop].number;
    while(next>=0&&next<stops.size()&&stops[next].number==number)next+=direction;
    if(direction<0&&next>=0)while(next>0&&stops[next-1].number==stops[next].number)--next;
    if(next<0)return;placingStop=true;auto c=editor->textCursor();
    if(next>=stops.size()){
      c.setPosition(finalStop);
      if(!outerSnippets.isEmpty()){const auto outer=outerSnippets.takeLast();stops=outer.stops;activeStop=outer.active;finalStop=outer.final;editor->setTextCursor(c);placingStop=false;jumpStop(1);return;}
      finishSnippet();
    }
    else{activeStop=next;const auto s=stops[next];c.setPosition(s.start);c.setPosition(s.start+s.length,QTextCursor::KeepAnchor);}
    editor->setTextCursor(c);placingStop=false;refreshSnippetMirrors();
  }
  void ensureDocument() {
    if (document == editor->document()) return;
    disconnect(documentChange); finishSnippet(); document = editor->document(); checkpoints.clear(); checkpoints.insert(0, {});
    documentChange = connect(document, &QTextDocument::contentsChange, this, [this](int position, int removed, int added) {
      if (!removed && !added) return;
      if(!imeTracking&&!mirroring)updateSnippetRanges(position,removed,added);
      auto end = checkpoints.upperBound(document->findBlock(position).blockNumber()); while (end != checkpoints.end()) end = checkpoints.erase(end);
      if (inputTurn&&!mirroring) expectedPosition = position + added;
    });
  }
  QString stateBefore(const QTextBlock &block) {
    ensureDocument(); auto checkpoint = checkpoints.upperBound(block.blockNumber()); --checkpoint;
    QString state = checkpoint.value(); auto current = document->findBlockByNumber(checkpoint.key());
    while (current.isValid() && current.blockNumber() < block.blockNumber()) {
      state = lexicalPrefix(current.text(), int(current.text().size()), state).environment; current = current.next();
      if (current.isValid() && current.blockNumber()%64 == 0) checkpoints.insert(current.blockNumber(), state);
    }
    return state;
  }
  Nearby nearby(const Context &ctx) {
    // One bounded, local pass per popup. No project scan, watcher or worker.
    auto first = editor->textCursor().block(); int characters = int(first.text().size()), blocks = 1;
    while (first.previous().isValid() && blocks < 128 && characters+first.previous().length() <= 16384) {
      first = first.previous(); characters += first.length(); ++blocks;
    }
    Nearby result; QString state = stateBefore(first);
    for (auto block = first; block.isValid() && block.position() <= ctx.start; block = block.next()) {
      const auto line = block.text(); const int stop = qMin(int(line.size()), ctx.start-block.position());
      state = lexicalPrefix(line, stop, state, &result).environment;
    }
    return result;
  }
  Context context() {
    if (editor->isReadOnly()) return {};
    const auto cursor = editor->textCursor();
    if(cursor.hasSelection())return {cursor.selectionStart(),cursor.selectionEnd(),{},4,cursor.selectedText().replace(QChar::ParagraphSeparator,QLatin1Char('\n'))};
    const auto block = cursor.block(); const auto line = block.text(); const int p = cursor.positionInBlock();
    if (line.size() > 8192 || (p < line.size() && letter(line[p]))) return {};
    static const QRegularExpression environment(QStringLiteral(R"(\\(begin|end)\s*\{([A-Za-z0-9*@:_-]*)$)"));
    const auto env=environment.match(line.left(p));
    if(env.hasMatch()){
      int before=int(env.capturedStart());while(before>0&&line[before-1]==QLatin1Char('\\'))--before;
      if((env.capturedStart()-before)%2||lexicalPrefix(line,int(env.capturedStart()),stateBefore(block)).protectedText)return {};
      if(p<line.size()&&line[p]!=QLatin1Char('}')&&!line[p].isSpace())return {};
      return {block.position()+int(env.capturedStart(2)),cursor.position(),env.captured(2),env.captured(1)==QStringLiteral("begin")?1:2};
    }
    static const QRegularExpression tail(QStringLiteral(R"(\\[A-Za-z@]*$)"));
    const auto match = tail.match(line.left(p));
    if(!match.hasMatch()) {
      if(p<line.size()&&(line[p].isLetterOrNumber()||line[p]==QLatin1Char('_')||line[p]==QLatin1Char('-')))return {};
      static const QRegularExpression word(QStringLiteral(R"(([A-Za-z][A-Za-z0-9_-]*)$)"));const auto token=word.match(line.left(p));
      if(!token.hasMatch())return {};const auto prefix=token.captured();if(prefix.size()>64)return {};
      const auto snippets=completionsAtCursor(editor,window);
      if(std::none_of(snippets.cbegin(),snippets.cend(),[&](const UserCompletion &v){return !v.prefix.startsWith(QLatin1Char('\\'))&&v.prefix.startsWith(prefix);}))return {};
      if(projectContext(cursor).valid||lexicalPrefix(line,p,stateBefore(block)).protectedText)return {};
      return {block.position()+int(token.capturedStart()),cursor.position(),prefix,3};
    }
    int preceding = int(match.capturedStart()); while (preceding > 0 && line[preceding-1] == QLatin1Char('\\')) --preceding;
    if ((match.capturedStart()-preceding) % 2 || projectContext(cursor).valid) return {};
    const QString state = stateBefore(block);
    if (lexicalPrefix(line, p, state).protectedText) return {};
    return {block.position()+int(match.capturedStart()), cursor.position(), match.captured()};
  }
  void cancel() { ++generation; projectTicket->fetch_add(1);projectPending=false; inputTurn = false; manualSession = false; delay.stop(); if (popup) popup->hide();showDocumentation=false;if(documentation)documentation->hide(); }
  void updateDocumentation(){
    if(!showDocumentation||!popup||!popup->isVisible()||!popup->currentIndex().isValid()){if(documentation)documentation->hide();return;}
    const auto row=popup->currentIndex().row();if(row<0||row>=candidates.size())return;
    if(!documentation){documentation=new PopupFrame(window);documentation->setObjectName(QStringLiteral("researchCompletionDocumentation"));documentation->setWindowFlags(Qt::ToolTip|Qt::NoDropShadowWindowHint);documentation->setFocusPolicy(Qt::NoFocus);auto *layout=new QVBoxLayout(documentation);layout->setContentsMargins(8,8,8,8);documentationText=new QPlainTextEdit(documentation);documentationText->setReadOnly(true);documentationText->setFocusPolicy(Qt::NoFocus);documentationText->setFrameShape(QFrame::NoFrame);documentationText->setStyleSheet(QStringLiteral("QPlainTextEdit{background:white;color:#404048;font-size:13px;}"));layout->addWidget(documentationText);preparePopupSurface(documentation);}
    const auto &e=candidates[row];QString text=e.detail;if(text.isEmpty())text=e.text;else if(e.snippet)text+=QStringLiteral("\n\n")+e.text;
    if(documentationText->toPlainText()!=text)documentationText->setPlainText(text);
    QRect area(window->mapToGlobal(QPoint()),window->size());if(auto *screen=window->screen())area=area.intersected(screen->availableGeometry());area.adjust(8,8,-8,-8);const auto list=popup->geometry();
    const int right=area.right()-list.right()-8,left=list.left()-area.left()-8;QRect target;
    if(qMax(right,left)>=180){const bool onRight=right>=180;const int width=qMin(320,onRight?right:left),height=qMin(180,area.height());target=QRect(onRight?list.right()+8:list.left()-width-8,qBound(area.top(),list.top(),area.bottom()-height+1),width,height);}
    else{const int below=area.bottom()-list.bottom()-8,above=list.top()-area.top()-8;const bool under=below>=above;const int height=qMin(160,under?below:above);if(height<60){documentation->hide();return;}target=QRect(list.left(),under?list.bottom()+8:list.top()-height-8,list.width(),height);}
    const auto cursor=editor->cursorRect();const QRect protectedLine(editor->viewport()->mapToGlobal(QPoint(0,cursor.top())),QSize(editor->viewport()->width(),cursor.height()));if(target.intersects(protectedLine)||!area.contains(target)){documentation->hide();return;}
    documentation->setGeometry(target);documentation->show();
  }
  void labels() {
    const QStringList names{ui(QStringLiteral("补全：关闭")), ui(QStringLiteral("补全：手动")), ui(QStringLiteral("补全：自动"))};
    for (int i = 0; i < modes.size(); ++i) {
      modes[i]->setText(names[i] + (i == 1 ? QStringLiteral("\tAlt+/") : QString())); modes[i]->setChecked(i == mode());
      modes[i]->setToolTip(completionText("Alt+/ 显示 · Tab 接受 · Esc 取消","Alt+/ suggestions · Tab accept · Esc dismiss"));
    }
  }
  void inputStarted() {
    ++generation;projectTicket->fetch_add(1);projectPending=false;selectionTouched=false; inputTurn = true; expectedPosition = -1; delay.stop(); if (popup) popup->hide();if(documentation)documentation->hide();
    const auto ticket = generation; const auto before = editor->document()->revision();
    QTimer::singleShot(0, this, [this, ticket, before] {
      if (ticket != generation) return;
      inputTurn = false;
      const auto ctx=context();
      if (composing || !mode() || editor->document()->revision() == before || editor->textCursor().position() != expectedPosition || !ctx.valid() || (ctx.kind==4&&!manualSession) || ((ctx.kind==0||ctx.kind==3)&&ctx.prefix.size()<2&&!manualSession)) { manualSession = false; return; }
      if (manualSession || mode() == 2) delay.start(manualSession ? 0 : 300);
    });
  }
  QVector<Entry> extraEntries(const Context &ctx) {
    QVector<Entry> result;
    if(ctx.kind>=3) {
      for(const auto &v:completionsAtCursor(editor,window))if((ctx.kind==4&&parseCompletionSnippet(v.body).usesSelection)||(ctx.kind==3&&!v.prefix.startsWith(QLatin1Char('\\'))&&v.prefix.startsWith(ctx.prefix))) {
        auto e=snippetEntry(v.prefix,v.body,v.description,ctx.selection);e.custom=true;e.indent=ctx.kind!=4;result.append(e);
      }
      return result;
    }
    auto location=[&](const MacroCompletion &macro){auto *owner=qobject_cast<TeXDocumentWindow *>(window);return (owner&&owner->untitled()&&macro.file==completionProjectRoot(owner)?QStringLiteral("Untitled"):QFileInfo(macro.file).fileName())+QStringLiteral(":%1").arg(macro.line);};
    auto arguments=[&](Entry &e,const QVector<CompletionArgument> &args,bool optional){
      for(const auto &arg:args){if(arg.optional&&!optional)continue;const auto open=arg.optional?QLatin1Char('['):QLatin1Char('{'),close=arg.optional?QLatin1Char(']'):QLatin1Char('}');
        e.text+=open;e.label+=open;e.stops.append({int(e.stops.size())+1,int(e.text.size()),int(arg.value.size())});e.text+=arg.value;e.label+=arg.value.isEmpty()?QString(QChar(0x2026)):arg.value;e.text+=close;e.label+=close;}
    };
    auto addEnvironment=[&](const MacroCompletion &macro){
      const auto name=macro.name;if(ctx.kind&& !name.startsWith(ctx.prefix))return;
      if(!ctx.kind&&!(QStringLiteral("\\begin{")+name).startsWith(ctx.prefix))return;
      for(bool optional:{false,true}){
        if(optional&&std::none_of(macro.arguments.begin(),macro.arguments.end(),[](const CompletionArgument &a){return a.optional;}))continue;
        Entry e;e.environment=true;e.environmentName=name;e.source=macro.file;e.sourceHash=macro.sourceHash;e.familiar=true;e.snippet=ctx.kind!=2;e.indent=true;
        e.detail=macro.file.isEmpty()?QString():location(macro)+QStringLiteral(" · ")+macro.definition;
        if(ctx.kind==2){e.label=name;e.text=name;e.caret=int(name.size());e.finalPosition=e.caret;result.append(e);break;}
        if(!macro.signatureKnown){e.label=ctx.kind==1?name:QStringLiteral("\\begin{")+name+QLatin1Char('}');e.text=e.label;e.caret=int(e.text.size());e.finalPosition=e.caret;e.snippet=false;e.detail+=completionText(" · 参数需手工填写"," · fill arguments manually");result.append(e);break;}
        e.text=(ctx.kind==1?QString():QStringLiteral("\\begin{"))+name+QLatin1Char('}');e.label=ctx.kind==1?name:e.text;
        if(macro.signatureKnown)arguments(e,macro.arguments,optional);
        e.text+=QStringLiteral("\n  ");e.stops.append({int(e.stops.size())+1,int(e.text.size()),0});e.text+=QStringLiteral("\n\\end{")+name+QLatin1Char('}');e.finalPosition=int(e.text.size());e.caret=e.stops.first().start;
        if(!ctx.kind)e.label+=QStringLiteral(" ↵ … \\end{")+name+QLatin1Char('}');
        if(!macro.signatureKnown)e.detail+=completionText(" · 参数需手工填写"," · fill arguments manually");
        result.append(e);
      }
    };
    QSet<QString> projectEnvironments,projectCommands;
    if(project){
      QHash<QString,int> counts;for(const auto &m:project->macros)++counts[(m.environment?QStringLiteral("env:"):QString())+m.name];QSet<QString> seen;
      for(auto macro:project->macros){
        const auto identity=(macro.environment?QStringLiteral("env:"):QString())+macro.name;if(seen.contains(identity))continue;seen.insert(identity);
        if(counts.value(identity)>1){macro.signatureKnown=false;macro.arguments.clear();macro.definition=completionText("多处定义；仅补全名称","Multiple definitions; name only");}
        if(macro.environment){projectEnvironments.insert(macro.name);addEnvironment(macro);continue;}
        projectCommands.insert(macro.name);
        if(ctx.kind)continue;
        const auto name=QLatin1Char('\\')+macro.name;if(!name.startsWith(ctx.prefix))continue;
        Entry e;e.label=name;e.text=name;e.caret=int(name.size());e.familiar=true;e.source=macro.file;e.sourceHash=macro.sourceHash;
        e.detail=location(macro)+QStringLiteral(" · ")+macro.definition;
        if(!macro.signatureKnown)e.detail+=completionText(" · 参数需手工填写"," · fill arguments manually");
        result.append(e);
        if(!macro.signatureKnown||macro.arguments.isEmpty())continue;
        for(bool optional:{false,true}){
          if(optional&&std::none_of(macro.arguments.begin(),macro.arguments.end(),[](const CompletionArgument &a){return a.optional;}))continue;
          auto templ=e;templ.snippet=true;arguments(templ,macro.arguments,optional);if(templ.stops.isEmpty())continue;
          templ.finalPosition=int(templ.text.size());templ.caret=templ.stops.first().start;result.append(templ);
        }
      }
    }
    for(const auto &name:QStringLiteral("equation equation* align align* aligned gather gather* multline multline* cases matrix bmatrix pmatrix vmatrix Vmatrix itemize enumerate description abstract center figure table frame array tabular minipage alignat").split(QLatin1Char(' '))){
      if(projectEnvironments.contains(name))continue;MacroCompletion env;env.name=name;env.environment=true;
      if(name==QStringLiteral("array")||name==QStringLiteral("tabular"))env.arguments.append({false,QStringLiteral("cc")});
      if(name==QStringLiteral("minipage"))env.arguments.append({false,QStringLiteral("0.8\\linewidth")});
      if(name==QStringLiteral("alignat"))env.arguments.append({false,QStringLiteral("2")});
      if(name==QStringLiteral("figure")||name==QStringLiteral("table"))env.arguments.append({true,QStringLiteral("htbp")});
      addEnvironment(env);
    }
    if(!ctx.kind){
      for(const auto &v:completionsAtCursor(editor,window)){if(!v.prefix.startsWith(ctx.prefix))continue;auto e=snippetEntry(v.prefix,v.body,v.description);e.custom=true;result.append(e);}
      // Common paper commands absent from some legacy abbreviation files.
      for(const auto &name:QStringLiteral("section subsection subsubsection chapter part label ref eqref autoref cref Cref cite citep citet parencite textcite input include includegraphics caption footnote").split(QLatin1Char(' '))){
        if(projectCommands.contains(name))continue;
        const QString command=QLatin1Char('\\')+name;if(!command.startsWith(ctx.prefix))continue;
        result.append(snippetEntry(command+QStringLiteral("{…}"),command+QStringLiteral("{$1}$0"),QString()));
        if(name==QStringLiteral("includegraphics")||name==QStringLiteral("citep")||name==QStringLiteral("citet")||name==QStringLiteral("parencite")||name==QStringLiteral("textcite"))
          result.append(snippetEntry(command+QStringLiteral("[…]{…}"),command+QStringLiteral("[$1]{$2}$0"),QString()));
      }
    }
    return result;
  }
  QString nearestEnvironment(const Context &ctx){
    if(ctx.kind!=2)return {};auto first=editor->textCursor().block();int count=0;
    while(first.previous().isValid()&&++count<4096&&ctx.start-first.previous().position()<65536)first=first.previous();
    QTextCursor text(editor->document());text.setPosition(first.position());text.setPosition(ctx.start,QTextCursor::KeepAnchor);auto source=maskProjectText(text.selectedText().replace(QChar::ParagraphSeparator,QLatin1Char('\n')));
    static const QRegularExpression tokens(QStringLiteral(R"(\\(begin|end)\s*\{([^{}]+)\})"));QStringList stack;auto it=tokens.globalMatch(source);
    while(it.hasNext()){const auto m=it.next();int p=int(m.capturedStart()),slashes=0;while(p>0&&source[--p]==QLatin1Char('\\'))++slashes;if(slashes%2)continue;if(m.captured(1)==QStringLiteral("begin"))stack.append(m.captured(2));else if(!stack.isEmpty()&&stack.last()==m.captured(2))stack.removeLast();else return {};}
    return stack.isEmpty()?QString():stack.last();
  }
  void show(bool fromProject=false) {
    if (composing || !mode() || !editor->hasFocus()) return;
    const auto ctx = context(); if (!ctx.valid()||(ctx.kind==4&&!manualSession)) return;
    QString selected;if(fromProject&&selectionTouched&&popup&&popup->isVisible()&&popup->currentIndex().isValid())selected=popup->currentIndex().data().toString();
    if(!fromProject){project.reset();selectionTouched=false;}
    candidates=extraEntries(ctx);QSet<QString> identities;for(const auto &entry:candidates)identities.insert(entry.text);
    QSet<QString> declared;if(project)for(const auto &m:project->macros)if(!m.environment)declared.insert(QLatin1Char('\\')+m.name);
    static const QRegularExpression entryName(QStringLiteral(R"(^\\[A-Za-z@]+)"));
    if(!ctx.kind)for(auto entry:catalog())if(entry.label.startsWith(ctx.prefix,Qt::CaseSensitive)&&!identities.contains(entry.text)&&!declared.contains(entryName.match(entry.text).captured())){candidates.append(entry);identities.insert(entry.text);}
    const auto local = ctx.kind>=3?Nearby():nearby(ctx);
    QSet<QString> available; for (const auto &entry : candidates) available.insert(entry.text);
    int extra = 0;
    // Just like project command providers, also offer names actually used
    // nearby. Never turn abbreviation-table aliases into supposed commands.
    static const QRegularExpression commandName(QStringLiteral(R"(^\\[A-Za-z@]+$)"));
    auto names = local.usage.keys();
    std::sort(names.begin(), names.end(), [&](const QString &a, const QString &b) { const int x=local.usage.value(a), y=local.usage.value(b); return x==y ? a<b : x>y; });
    for (const auto &name : names) {
      if (!ctx.kind&&name.startsWith(ctx.prefix) && !available.contains(name) && commandName.match(name).hasMatch()) {
        Entry e{name, name, int(name.size()), false};candidates.append(e); if (++extra == 64) break;
      }
    }
    QHash<QString, int> preference; int age = 0;
    for (const auto &choice : choices()) {
      static const int recent[]{240,120,80,50,30,20};
      preference.insert(choice.text, (age < 6 ? recent[age] : 0)+6*qMin(choice.count,12)); ++age;
    }
    const auto nearest=nearestEnvironment(ctx);
    if(ctx.kind==2&&!nearest.isEmpty()&&nearest.startsWith(ctx.prefix)&&std::none_of(candidates.begin(),candidates.end(),[&](const Entry &e){return e.label==nearest;})){Entry e{nearest,nearest,int(nearest.size()),false,true};e.environment=true;e.environmentName=nearest;e.detail=completionText("结束当前环境","Close current environment");candidates.append(e);}
    const bool exactName=std::any_of(candidates.cbegin(),candidates.cend(),[&](const Entry &e){return !e.snippet&&e.text==ctx.prefix;});
    auto score = [&](const Entry &entry) {
      int value = entry.text == ctx.prefix ? 100 : entry.familiar ? 60 : entry.snippet ? 10 : 20;
      if(!entry.source.isEmpty())value+=130;
      if(!entry.stops.isEmpty()&&(!exactName||entryName.match(entry.text).captured()==ctx.prefix))value+=80;
      if(ctx.kind==2&&entry.environmentName==nearest)value+=1000;
      if(entry.custom)value+=150;
      value += preference.value(entry.custom?entry.label:entry.text)+12*qMin(local.usage.value(entry.text),6);
      if (!local.left.isEmpty() && ((local.left.last() == QLatin1Char('(') && entry.text == QStringLiteral("\\right)")) || (local.left.last() == QLatin1Char('[') && entry.text == QStringLiteral("\\right]")))) value += 480;
      return value;
    };
    std::stable_sort(candidates.begin(), candidates.end(), [&](const Entry &a, const Entry &b) { const int x=score(a), y=score(b); return x==y ? a.label<b.label : x>y; });
    // Local results paint immediately; project work shares the existing single
    // expiring worker. A cancelled/closed session can never publish its result.
    if(!fromProject&&ctx.kind<3)if(auto *owner=qobject_cast<TeXDocumentWindow *>(window)){
      const auto epoch=projectTicket;const auto ticket=epoch->fetch_add(1)+1;const auto revision=completionProjectEpoch();QPointer<CommandCompletion> guard(this);projectPending=true;
      requestCompletionProject(owner,this,[guard,ctx,revision](QSharedPointer<const ProjectIndex> value){if(!guard)return;guard->projectPending=false;if(completionProjectEpoch()!=revision)return;const auto now=guard->context();if(now.start!=ctx.start||now.end!=ctx.end||now.prefix!=ctx.prefix||now.kind!=ctx.kind)return;guard->indexedEpoch=revision;guard->project=value;guard->show(true);},[epoch,ticket]{return epoch->load()!=ticket;});
    }
    if (candidates.isEmpty()) {if(popup)popup->hide();return;}
    if (!popup) {
      popup = new PopupListView(window); popup->setObjectName(QStringLiteral("researchCommandCompletion"));
      popup->setWindowFlags(Qt::ToolTip | Qt::NoDropShadowWindowHint); popup->setFocusPolicy(Qt::NoFocus);
      popup->setUniformItemSizes(true); popup->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
      popup->setTextElideMode(Qt::ElideRight); popup->setWordWrap(false);
      QFont font; font.setFamilies({QStringLiteral("Segoe UI"), QStringLiteral("Microsoft YaHei UI")}); font.setPixelSize(14); popup->setFont(smoothTextFont(font));
      popup->setStyleSheet(QStringLiteral("QListView{background:#ffffff;color:#1d1d1f;border:1px solid #d2d2d7;border-radius:8px;padding:2px;} QListView::item{border-radius:5px;} QListView::item:selected{background:#e9e9ed;color:#1d1d1f;}"));
      preparePopupSurface(popup);
      model = new CompletionRows(popup); popup->setModel(model);popup->setItemDelegate(new CompletionDelegate(popup));popup->viewport()->installEventFilter(this);
      connect(popup->selectionModel(),&QItemSelectionModel::currentChanged,this,[this]{updateDocumentation();});
      connect(popup, &QListView::clicked, this, [this](const QModelIndex &index) { accept(index.row()); });
    }
    QStringList labels; for (const auto &entry : candidates) labels.append(entry.label);model->entries=candidates;model->setStringList(labels);popup->setCurrentIndex(model->index(qMax(0,int(labels.indexOf(selected)))));popup->scrollTo(popup->currentIndex());
    const auto anchor = editor->cursorRect(); const QPoint top = editor->viewport()->mapToGlobal(anchor.topLeft()), bottom = editor->viewport()->mapToGlobal(anchor.bottomLeft());
    QRect area(window->mapToGlobal(QPoint()), window->size()); if (auto *screen = QGuiApplication::screenAt(bottom)) area = area.intersected(screen->availableGeometry()); area.adjust(8, 8, -8, -8);
    const int below = area.bottom()-bottom.y()-4, above = top.y()-area.top()-4;
    const bool underneath = below >= qMin(6, int(candidates.size()))*28+6 || below >= above;
    const int space = underneath ? below : above; const int rows = qMin(6, qMin(int(candidates.size()), (space-6)/28));
    if (rows < 1 || area.width() < 100) { popup->hide(); return; }
    popup->resize(qMin(420, area.width()), rows*28+6);
    popup->move(qBound(area.left(), bottom.x(), area.right()-popup->width()+1), underneath ? bottom.y()+4 : top.y()-popup->height()-4);
    shownContext = ctx; shownRevision = editor->document()->revision();
    popup->setProperty("researchFocus", window->property("researchFocus")); popup->show();updateDocumentation();
  }
  void accept(int row,const Entry *direct=nullptr,const Context *directContext=nullptr) {
    if (composing || !mode() || (!direct&&(row < 0 || row >= candidates.size() || !popup || !popup->isVisible()))) return;
    const auto ctx = directContext?*directContext:context(); auto entry = direct?*direct:candidates[row];
    if (!direct&&(!ctx.valid() || editor->document()->revision() != shownRevision || ctx.start != shownContext.start || ctx.end != shownContext.end || ctx.kind!=shownContext.kind || ctx.prefix != shownContext.prefix || !entry.label.startsWith(ctx.prefix, Qt::CaseSensitive))) { cancel(); return; }
    if(!entry.source.isEmpty()){
      if(indexedEpoch!=completionProjectEpoch()){cancel();return;}
      QByteArray bytes;bool buffered=false;
      if(auto *owner=qobject_cast<TeXDocumentWindow *>(window);owner&&owner->untitled()&&entry.source==completionProjectRoot(owner)){bytes=editor->toPlainText().toUtf8();buffered=true;}
      for(auto *w:TeXDocumentWindow::documentList()){const QFileInfo file(w->fileName());if(!w->untitled()&&w->isModified()&&(file.exists()?file.canonicalFilePath():file.absoluteFilePath())==entry.source){bytes=w->editor()->toPlainText().toUtf8();buffered=true;break;}}
      if(!buffered){QFile f(entry.source);if(f.open(QIODevice::ReadOnly)&&f.size()<=4*1024*1024)bytes=QString::fromUtf8(f.readAll()).toUtf8();}
      if(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256)!=entry.sourceHash){cancel();showTransientStatusMessage(window,completionText("定义已变化，请重新显示补全。","Definition changed. Show suggestions again."));return;}
    }
    const bool mirroredName=ctx.kind==1&&entry.environment&&insideStop()&&editor->property("researchSnippetMirrored").toBool();
    if(mirroredName){entry.snippet=false;entry.stops.clear();entry.indent=false;}
    if(ctx.kind==1&&entry.environment&&!mirroredName){
      auto names=editor->completionEnvironmentNames();if(names.second.isNull())names=editor->completionEnvironmentNames(entry.environmentName);
      if(!names.second.isNull()){
        cancel();finishSnippet();placingStop=true;auto cursor=editor->textCursor();cursor.beginEditBlock();const int start=names.first.selectionStart();auto tail=names.second;tail.insertText(entry.environmentName);auto head=names.first;head.insertText(entry.environmentName);cursor.endEditBlock();head.setPosition(start+entry.environmentName.size());editor->setTextCursor(head);placingStop=false;rememberEntry(entry);return;
      }
    }
    // Reuse an already typed closing delimiter; a confirmed \right] must
    // not turn "\ri]" into "\right]]". Keep this in the same undo block.
    int end = ctx.end;
    if(ctx.kind==1&&entry.environment&&!entry.snippet){entry.text=entry.environmentName;if(editor->document()->characterAt(end)!=QLatin1Char('}'))entry.text+=QLatin1Char('}');entry.caret=int(entry.text.size());}
    else if(ctx.kind==1&&entry.environment&&editor->document()->characterAt(end)==QLatin1Char('}'))++end;
    if(ctx.kind==2&&editor->document()->characterAt(end)!=QLatin1Char('}')){entry.text+=QLatin1Char('}');entry.caret=int(entry.text.size());}
    if(!ctx.kind&&entry.snippet&&!entry.custom&&!entry.environment&&(editor->document()->characterAt(end)==QLatin1Char('{')||editor->document()->characterAt(end)==QLatin1Char('['))){
      static const QRegularExpression name(QStringLiteral(R"(^\\[A-Za-z@]+(?=[{\[]))"));const auto command=name.match(entry.text).captured();
      if(!command.isEmpty()){entry.text=command;entry.stops.clear();entry.caret=int(command.size())+1;entry.indent=false;}
    }
    if ((entry.text == QStringLiteral("\\right)") || entry.text == QStringLiteral("\\right]"))
        && editor->document()->characterAt(end) == entry.text.back()) ++end;
    if(entry.indent){
      const auto line=editor->textCursor().block().text();int n=0;while(n<line.size()&&(line[n]==QLatin1Char(' ')||line[n]==QLatin1Char('\t')))++n;const auto indent=line.left(n);
      if(!indent.isEmpty()){auto offset=[&](int p){return p+entry.text.left(p).count(QLatin1Char('\n'))*int(indent.size());};for(auto &stop:entry.stops){const int finish=offset(stop.start+stop.length);stop.start=offset(stop.start);stop.length=finish-stop.start;}entry.caret=offset(entry.caret);entry.finalPosition=offset(entry.finalPosition<0?int(entry.text.size()):entry.finalPosition);entry.text.replace(QStringLiteral("\n"),QStringLiteral("\n")+indent);}
    }
    const bool keepOuter=activeStop>=0&&insideStop()&&(entry.stops.isEmpty()||outerSnippets.size()<8);
    const bool nested=keepOuter&&!entry.stops.isEmpty();
    cancel();if(!keepOuter)finishSnippet();placingStop=true;auto cursor = editor->textCursor(); cursor.beginEditBlock(); cursor.setPosition(ctx.start); cursor.setPosition(end, QTextCursor::KeepAnchor); cursor.insertText(entry.text); cursor.endEditBlock();
    if(keepOuter&&entry.stops.isEmpty()){cursor.setPosition(ctx.start+entry.caret);editor->setTextCursor(cursor);placingStop=false;syncMirrors();rememberEntry(entry);return;}
    if(nested&&activeStop>=0)outerSnippets.append({stops,activeStop,finalStop});
    stops=entry.stops;for(auto &stop:stops)stop.start+=ctx.start;finalStop=ctx.start+(entry.finalPosition<0?int(entry.text.size()):entry.finalPosition);
    if(!stops.isEmpty()){activeStop=0;cursor.setPosition(stops.first().start);cursor.setPosition(stops.first().start+stops.first().length,QTextCursor::KeepAnchor);editor->setProperty("researchSnippetActive",true);}else cursor.setPosition(ctx.start+entry.caret);
    editor->setTextCursor(cursor);placingStop=false;refreshSnippetMirrors();syncMirrors();rememberEntry(entry);
  }
  bool eventFilter(QObject *object, QEvent *event) override {
    if(popup&&object==popup->viewport()&&(event->type()==QEvent::MouseButtonPress||event->type()==QEvent::MouseButtonRelease)){
      auto *mouse=static_cast<QMouseEvent *>(event);if(mouse->button()==Qt::LeftButton&&mouse->position().x()>=popup->viewport()->width()-22){const auto index=popup->indexAt(mouse->position().toPoint());if(index.isValid()){selectionTouched=true;popup->setCurrentIndex(index);if(event->type()==QEvent::MouseButtonRelease){showDocumentation=!showDocumentation;updateDocumentation();}return true;}}
    }
    if (object == window && (event->type() == QEvent::Hide || event->type() == QEvent::WindowDeactivate || event->type() == QEvent::Resize || event->type() == QEvent::Move || event->type() == QEvent::ScreenChangeInternal || event->type() == QEvent::Close)) cancel();
    if (object != editor) return false;
    if (event->type() == QEvent::FocusOut || event->type() == QEvent::MouseButtonPress || event->type() == QEvent::Resize) { cancel(); if(event->type()==QEvent::FocusOut)finishSnippet(); return false; }
    if (event->type() == QEvent::InputMethod) {
      auto *ime = static_cast<QInputMethodEvent *>(event); cancel(); composing = !ime->preeditString().isEmpty();
      if (!composing && !ime->commitString().isEmpty() && mode()) inputStarted();
      return false;
    }
    if (event->type() != QEvent::ShortcutOverride && event->type() != QEvent::KeyPress) return false;
    auto *key = static_cast<QKeyEvent *>(event); const auto modifiers = key->modifiers() & ~Qt::KeyboardModifiers(Qt::KeypadModifier);
    const bool trigger = ((key->key() == Qt::Key_Slash && modifiers == Qt::AltModifier)||(key->key()==Qt::Key_Space&&modifiers==Qt::ControlModifier&&Tw::Settings().value(QStringLiteral("researchCompletionCtrlSpace"),false).toBool()))&&completionShortcutAvailable(window,QKeySequence(int(modifiers)|key->key()));
    const bool visible = popup && popup->isVisible();
    const bool tab=key->key()==Qt::Key_Tab&&modifiers==Qt::NoModifier;
    const bool backtab=(key->key()==Qt::Key_Backtab&&(modifiers==Qt::ShiftModifier||modifiers==Qt::NoModifier))||(key->key()==Qt::Key_Tab&&modifiers==Qt::ShiftModifier);
    const auto *references=window->findChild<QListView *>(QStringLiteral("researchReferenceCompletion"));
    const bool referenceVisible=references&&references->isVisible();
    const bool confirm = visible && (tab||((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) && modifiers == Qt::ControlModifier&&completionShortcutAvailable(window,QKeySequence(int(modifiers)|key->key()))));
    const bool enter=key->key()==Qt::Key_Return||key->key()==Qt::Key_Enter;
    const auto quick = !composing&&!visible&&!referenceVisible&&mode()&&((tab)||((modifiers==Qt::NoModifier||modifiers==Qt::ShiftModifier)&&!key->text().isEmpty()&&key->text().size()==1))
        ?matchShorthand(editor,window,!tab,tab?QString():key->text()):ShorthandMatch{};
    const bool snippetTab=activeStop>=0&&mode()!=0;
    bool next=enter&&modifiers==Qt::ControlModifier,previous=enter&&modifiers==(Qt::ControlModifier|Qt::ShiftModifier);
    if(snippetTab){next|=tab;previous|=backtab;}
    // Preserve explicitly configured legacy placeholder bindings as aliases.
    const QKeySequence sequence(int(modifiers)|key->key());
    QAction *legacyBinding=nullptr;
    if(activeStop>=0){
      if(auto *action=editor->findChild<QAction *>(QStringLiteral("actionNext_Completion_Placeholder")))if(!action->shortcut().isEmpty()&&sequence==action->shortcut()){next=true;legacyBinding=action;}
      if(auto *action=editor->findChild<QAction *>(QStringLiteral("actionPrevious_Completion_Placeholder")))if(!action->shortcut().isEmpty()&&sequence==action->shortcut()){previous=true;legacyBinding=action;}
    }
    if(snippetTab&&(modifiers==Qt::NoModifier||modifiers==Qt::ShiftModifier)&&(key->key()==Qt::Key_Tab||key->key()==Qt::Key_Backtab)&&(next||previous))
      legacyBinding=editor->findChild<QAction *>(previous?QStringLiteral("actionPrevious_Completion"):QStringLiteral("actionNext_Completion"));
    const auto cursor=editor->textCursor();
    const bool multiline=cursor.hasSelection()&&editor->document()->findBlock(cursor.selectionStart())!=editor->document()->findBlock(cursor.selectionEnd()-1);
    const bool jump=mode()!=0&&activeStop>=0&&!editor->isReadOnly()&&!multiline&&(!visible||backtab)&&!(referenceVisible&&tab)&&(next||previous)&&((tab||backtab)||completionShortcutAvailable(window,sequence,legacyBinding));
    if (event->type() == QEvent::ShortcutOverride) { if (!composing&&((trigger && (!mode() || context().valid())) || confirm || jump || quick.valid())) { event->accept(); return true; } return false; }
    if (composing) { if (popup) popup->hide(); return false; }
    // The reference provider owns its visible candidates regardless of which
    // filter Qt installed last. Never turn the same Tab into a snippet jump.
    if(referenceVisible&&(tab||(enter&&modifiers==Qt::ControlModifier)))return false;
    if (trigger) {if(visible){showDocumentation=!showDocumentation;updateDocumentation();return true;}cancel(); if (!mode()) return true; if (!context().valid()) return false; manualSession = true; show(); return true; }
    if (confirm) { accept(popup->currentIndex().row()); return true; }
    if(quick.valid()){
      if(!tab){auto c=editor->textCursor();c.insertText(key->text());editor->setTextCursor(c);}
      auto entry=snippetEntry(quick.prefix,quick.body,QString());entry.custom=true;const Context ctx{quick.start,quick.end,quick.prefix,3};accept(-1,&entry,&ctx);return true;
    }
    if(jump){cancel();cancelProjectWriting(window);jumpStop(previous?-1:1);return true;}
    if(multiline&&(tab||backtab))finishSnippet();
    if (visible && modifiers == Qt::NoModifier && (key->key() == Qt::Key_Up || key->key() == Qt::Key_Down)) {selectionTouched=true; const int row = qBound(0, popup->currentIndex().row()+(key->key()==Qt::Key_Up?-1:1), model->rowCount()-1); popup->setCurrentIndex(model->index(row)); popup->scrollTo(model->index(row)); return true; }
    if (key->key() == Qt::Key_Escape) {if(visible||referenceVisible||delay.isActive()||projectPending||activeStop>=0){cancel();cancelProjectWriting(window);finishSnippet();return true;}return false;}
    if(key->matches(QKeySequence::Undo)||key->matches(QKeySequence::Redo))finishSnippet();
    if (!mode()) return false;
    const bool editing = (modifiers == Qt::NoModifier || modifiers == Qt::ShiftModifier) && ((!key->text().isEmpty() && key->key() != Qt::Key_Return && key->key() != Qt::Key_Enter && key->key() != Qt::Key_Tab && key->key() != Qt::Key_Backtab) || key->key() == Qt::Key_Backspace || key->key() == Qt::Key_Delete);
    if (editing) inputStarted(); else cancel(); return false;
  }
};
}
void installCompletionEditor(QMainWindow *window,CompletingEdit *editor){new CommandCompletion(window,editor);}
void cancelCommandCompletion(QMainWindow *window) {
  if(window)if(auto *current=dynamic_cast<CommandCompletion *>(window->findChild<QObject *>(QStringLiteral("researchCommandCompletionController")))){current->cancel();current->finishSnippet();}
}
void setCommandCompletionMode(int mode) {
  Tw::Settings().setValue(modeKey, qBound(0, mode, 2));
  for (auto *window : QApplication::topLevelWidgets()) {
    cancelProjectWriting(qobject_cast<QMainWindow *>(window));
    if (auto *current = dynamic_cast<CommandCompletion *>(window->findChild<QObject *>(QStringLiteral("researchCommandCompletionController")))) { current->cancel(); current->finishSnippet(); current->labels(); }
  }
}
void installCommandCompletion(QMainWindow *window, QMenu *normal, QMenu *focus) {
  auto *editor = window->findChild<CompletingEdit *>(); if (!editor) return;
  auto *controller = new CommandCompletion(window, editor); auto *group = new QActionGroup(controller); group->setExclusive(true);
  normal->addSeparator();
  auto *show=new QAction(completionText("显示补全…","Show suggestions…"),controller);show->setObjectName(QStringLiteral("researchShowCompletion"));show->setToolTip(QStringLiteral("Alt+/ · Tab · Ctrl+Enter"));
  auto *settings=new QAction(completionText("补全与片段…","Completion & snippets…"),controller);settings->setObjectName(QStringLiteral("researchConfigureCompletion"));
  normal->addAction(show);focus->addAction(settings);
  QObject::connect(show,&QAction::triggered,controller,[controller]{if(!completionMode())setCommandCompletionMode(1);controller->editor->setFocus();if(auto *w=qobject_cast<TeXDocumentWindow *>(controller->window))if(showProjectCompletion(w))return;controller->cancel();controller->manualSession=true;controller->show();});
  QObject::connect(settings,&QAction::triggered,controller,[controller]{controller->cancel();configureLocalCompletion(controller->window);});
  const auto actionLabels=[show,settings]{show->setText(completionText("显示补全…","Show suggestions…"));settings->setText(completionText("补全与片段…","Completion & snippets…"));};
  QObject::connect(TWApp::instance(),&TWApp::updatedTranslators,controller,actionLabels);
  for (int mode = 0; mode < 3; ++mode) {
    auto *action = new QAction(group); action->setObjectName(QStringLiteral("researchCommandCompletionMode%1").arg(mode)); action->setCheckable(true); controller->modes.append(action);
    QObject::connect(action, &QAction::triggered, window, [mode] {
      setCommandCompletionMode(mode);
    });
  }
  controller->labels();
  for (auto *menu : {normal, focus,window->findChild<QMenu *>(QStringLiteral("researchMoreMenu")),window->findChild<QMenu *>(QStringLiteral("researchFocusMenu"))}) if(menu)QObject::connect(menu, &QMenu::aboutToShow, controller, [controller,actionLabels] { controller->labels(); actionLabels(); });
}
}
