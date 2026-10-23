#include "ResearchShorthand.h"
#include "ResearchAppearance.h"
#include "ResearchAuthoring.h"
#include "ResearchSettings.h"
#include "ResearchCommandCompletion.h"
#include "ResearchProject.h"
#include "TeXDocumentWindow.h"
#include "CompletingEdit.h"
#include "document/TeXDocument.h"
#include "Settings.h"
#include "TWApp.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QFormLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMap>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QStyle>
#include <QRegularExpression>
#include <QScreen>
#include <QScrollArea>
#include <QSet>
#include <QStackedWidget>
#include <QTabBar>
#include <QTextBlock>
#include <QTextCursor>
#include <QVBoxLayout>
#include <memory>
#include <algorithm>

namespace Research {
namespace {
QString literalTemplate(QString text){
  static const QRegularExpression metadata(QStringLiteral(R"(\$(?=[0-9{]))"));
  text.replace(metadata,QStringLiteral("\\$"));return text;
}
class TemplateEdit final:public QPlainTextEdit {
  QVector<SnippetStop> fields;bool loading{false};bool raw{false};int finalPosition{-1};
  void highlight(){QList<QTextEdit::ExtraSelection> selections;for(const auto &f:fields){QTextEdit::ExtraSelection item;item.cursor=QTextCursor(document());item.cursor.setPosition(f.start);item.cursor.setPosition(f.start+f.length,QTextCursor::KeepAnchor);item.format.setBackground(QColor(QStringLiteral("#e9edf0")));item.format.setToolTip(words("填写处 ","Field ")+QString::number(f.number));selections.append(item);}setExtraSelections(selections);}
public:
  explicit TemplateEdit(QWidget *parent):QPlainTextEdit(parent){
    connect(document(),&QTextDocument::contentsChange,this,[this](int p,int removed,int added){if(loading||raw||(!removed&&!added))return;const int delta=added-removed;for(int n=int(fields.size())-1;n>=0;--n){auto &f=fields[n];if(p>=f.start&&p+removed<=f.start+f.length)f.length+=delta;else if(f.start>=p+removed)f.start+=delta;else if(p<f.start+f.length&&p+removed>f.start)fields.removeAt(n);}if(finalPosition>=p+removed)finalPosition+=delta;else if(finalPosition>=p)finalPosition=p+added;highlight();});
  }
  void setTemplate(const QString &source){loading=true;const auto parsed=parseCompletionSnippet(source);raw=!parsed.valid()||parsed.usesSelection;fields=raw?QVector<SnippetStop>{}:parsed.stops;finalPosition=raw?-1:parsed.finalPosition;setPlainText(raw?source:parsed.text);loading=false;highlight();}
  QString templateBody()const{
    if(raw)return toPlainText();const auto text=toPlainText();auto ranges=fields;std::sort(ranges.begin(),ranges.end(),[](const SnippetStop &a,const SnippetStop &b){return a.start<b.start;});
    QString result;int p=0;QSet<int> seen;bool finalAdded=false;
    auto append=[&](int end){if(!finalAdded&&finalPosition>=p&&finalPosition<=end){result+=literalTemplate(text.mid(p,finalPosition-p))+QStringLiteral("$0");p=finalPosition;finalAdded=true;}result+=literalTemplate(text.mid(p,end-p));p=end;};
    for(const auto &f:ranges){if(f.start<p||f.start+f.length>text.size())continue;append(f.start);if(seen.contains(f.number))result+=QStringLiteral("$%1").arg(f.number);else{seen.insert(f.number);result+=QStringLiteral("${%1:%2}").arg(f.number).arg(literalTemplate(text.mid(f.start,f.length)));}p=f.start+f.length;}
    append(int(text.size()));return result;
  }
  QVector<SnippetStop> stops()const{return fields;}
  void markField(int number){auto c=textCursor();if(!c.hasSelection()||raw)return;const int a=c.selectionStart(),b=c.selectionEnd();for(const auto &f:fields)if(a<f.start+f.length&&b>f.start)return;fields.append({number,a,b-a});highlight();emit textChanged();}
};
// Header and rows use the same columns at every width and font scale.
QVector<QRect> snippetColumns(const QRect &bounds) {
  const auto rect=bounds.adjusted(10,0,-10,0);const int gap=12;
  const int width=qMax(0,rect.width()-2*gap);
  const int prefix=qMin(100,qRound(width*.25)),mode=qMin(130,qRound(width*.30)),title=width-prefix-mode;
  return {{rect.x(),rect.y(),title,rect.height()},
          {rect.x()+title+gap,rect.y(),prefix,rect.height()},
          {rect.x()+title+prefix+2*gap,rect.y(),width-title-prefix,rect.height()}};
}
class SnippetHeader final:public QWidget {
  QListWidget *list;
public:
  explicit SnippetHeader(QListWidget *view,QWidget *parent):QWidget(parent),list(view) {
    setObjectName(QStringLiteral("completionSnippetHeader"));
    setFixedHeight(qMax(26,fontMetrics().height()+8));
    setAccessibleName(words("名称、缩写、展开方式","Name, abbreviation, expansion"));
    list->viewport()->installEventFilter(this);
  }
  bool eventFilter(QObject *,QEvent *event)override {if(event->type()==QEvent::Resize)update();return false;}
  void paintEvent(QPaintEvent *)override {
    QPainter p(this);p.setPen(QColor(QStringLiteral("#737b82")));
    const auto columns=snippetColumns(QRect(0,0,list->viewport()->width(),height()));
    const QStringList labels{words("名称","Name"),words("缩写","Abbreviation"),words("展开方式","Expansion")};
    for(int i=0;i<3;++i)p.drawText(columns[i],Qt::AlignLeft|Qt::AlignVCenter,fontMetrics().elidedText(labels[i],Qt::ElideRight,columns[i].width()));
  }
};
class SnippetDelegate final:public QStyledItemDelegate {
public:using QStyledItemDelegate::QStyledItemDelegate;
  QSize sizeHint(const QStyleOptionViewItem &option,const QModelIndex &)const override{
    return {240,qMax(32,option.fontMetrics.height()+12)};
  }
  void paint(QPainter *p,const QStyleOptionViewItem &option,const QModelIndex &index)const override{
    QStyleOptionViewItem opt(option);initStyleOption(&opt,index);opt.text.clear();
    const auto *widget=option.widget;auto *style=widget?widget->style():QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem,&opt,p,widget);
    p->save();p->setClipRect(option.rect);p->setFont(option.font);
    const auto columns=snippetColumns(option.rect);
    const QColor colors[]{QColor("#30363c"),QColor("#51565b"),QColor("#737b82")};
    for(int i=0;i<3;++i){p->setPen(colors[i]);p->drawText(columns[i],Qt::AlignLeft|Qt::AlignVCenter,option.fontMetrics.elidedText(index.data(Qt::UserRole+2+i).toString(),Qt::ElideRight,columns[i].width()));}
    p->restore();
  }
};
class DetailStack final:public QStackedWidget {
public:using QStackedWidget::QStackedWidget;
  QSize sizeHint()const override{return currentWidget()?currentWidget()->sizeHint():QSize();}
  QSize minimumSizeHint()const override{return currentWidget()?currentWidget()->minimumSizeHint():QSize();}
};
QString projectScope(QMainWindow *window){if(auto *w=qobject_cast<TeXDocumentWindow *>(window);w&&!w->untitled())return completionProjectRoot(w);return {};}
QString scopeForRules(QMainWindow *window,const QVector<UserCompletion> &values){return std::any_of(values.cbegin(),values.cend(),[](const UserCompletion &v){return !v.scope.isEmpty();})?projectScope(window):QString();}
QJsonObject encode(const UserCompletion &v){return {{QStringLiteral("prefix"),v.prefix},{QStringLiteral("description"),v.description},{QStringLiteral("body"),v.body},{QStringLiteral("scope"),v.scope},{QStringLiteral("context"),v.context},{QStringLiteral("trigger"),v.trigger}};}
QVector<UserCompletion> presets(){return {
 {QStringLiteral("mk"),words("行内公式","Inline math"),QStringLiteral("$${1:}$$0"),{},1,1},
 {QStringLiteral("dm"),words("独立公式","Display math"),QStringLiteral("\\[\n  ${1:}\n\\]$0"),{},1,1},
 {QStringLiteral("//"),words("空分式","Fraction"),QStringLiteral("\\frac{${1:}}{${2:}}$0"),{},2,2},
 {QStringLiteral("beg"),words("环境","Environment"),QStringLiteral("\\begin{${1:equation}}\n  ${2:}\n\\end{$1}$0"),{},1,1},
 {QStringLiteral(".hat"),words("帽子","Hat"),QStringLiteral("\\hat{${1:}}$0"),{},2,1},
 {QStringLiteral(".bar"),words("横线","Bar"),QStringLiteral("\\overline{${1:}}$0"),{},2,1},
 {QStringLiteral(".sqrt"),words("根式","Square root"),QStringLiteral("\\sqrt{${1:}}$0"),{},2,1},
 {QStringLiteral(".vec"),words("向量","Vector"),QStringLiteral("\\vec{${1:}}$0"),{},2,1},
 {QStringLiteral(".norm"),words("范数","Norm"),QStringLiteral("\\left\\|${1:}\\right\\|$0"),{},2,1}
 };}
struct MathState {QString delimiter,literal;QStringList environments;QVector<bool> braces;bool text{false},uncertain{false};};
bool mathEnvironment(QString s){if(s.endsWith(QLatin1Char('*')))s.chop(1);return QStringList{QStringLiteral("math"),QStringLiteral("displaymath"),QStringLiteral("equation"),QStringLiteral("align"),QStringLiteral("alignat"),QStringLiteral("gather"),QStringLiteral("multline"),QStringLiteral("flalign"),QStringLiteral("eqnarray")}.contains(s);}
// Preserve state across paragraphs. Comments, escaped delimiters, literal
// environments and text within math never become automatic rewrite targets.
MathState scan(const QString &s,int end,MathState state,bool *blocked=nullptr){
  if(state.uncertain){if(blocked)*blocked=true;return state;}
  auto letter=[](QChar c){return (c>=QLatin1Char('A')&&c<=QLatin1Char('Z'))||(c>=QLatin1Char('a')&&c<=QLatin1Char('z'))||c==QLatin1Char('@');};
  for(int i=0;i<end;){
    if(state.braces.size()>128||state.environments.size()>128){state.uncertain=true;if(blocked)*blocked=true;return state;}
    if(!state.literal.isEmpty()){const QString terminator=QStringLiteral("\\end{")+state.literal+QLatin1Char('}');const int p=int(s.indexOf(terminator,i));if(p<0||p>=end){if(blocked)*blocked=true;return state;}i=p+int(terminator.size());state.literal.clear();continue;}
    QChar c=s[i];if(c==QLatin1Char('%')){if(blocked)*blocked=true;return state;}
    if(c==QLatin1Char('\\')){
      if(i+1>=end){++i;continue;}const auto next=s[i+1];
      if(next==QLatin1Char('(')||next==QLatin1Char('[')){if(state.delimiter.isEmpty())state.delimiter=QString(next);i+=2;continue;}
      if(next==QLatin1Char(')')||next==QLatin1Char(']')){if(state.delimiter==(next==QLatin1Char(')')?QStringLiteral("("):QStringLiteral("[")))state.delimiter.clear();i+=2;continue;}
      if(!letter(next)){i+=2;continue;}const int first=++i;while(i<end&&letter(s[i]))++i;const QString name=s.mid(first,i-first);if(i<end&&s[i]==QLatin1Char('*'))++i;
      if(name==QStringLiteral("verb")||name==QStringLiteral("lstinline")||name==QStringLiteral("mintinline")){
        int p=i;while(p<end&&s[p].isSpace())++p;
        auto group=[&](QChar open,QChar close){if(p>=end||s[p]!=open)return false;int depth=1;++p;while(p<end&&depth){if(s[p]==QLatin1Char('\\')){p+=2;continue;}if(s[p]==open)++depth;else if(s[p]==close)--depth;++p;}return depth==0;};
        if(name!=QStringLiteral("verb")){if(p<end&&s[p]==QLatin1Char('[')&&!group(QLatin1Char('['),QLatin1Char(']'))){if(blocked)*blocked=true;return state;}while(p<end&&s[p].isSpace())++p;if(name==QStringLiteral("mintinline")){if(!group(QLatin1Char('{'),QLatin1Char('}'))){if(blocked)*blocked=true;return state;}while(p<end&&s[p].isSpace())++p;}}
        if(p<end&&name!=QStringLiteral("verb")&&s[p]==QLatin1Char('{')){if(!group(QLatin1Char('{'),QLatin1Char('}'))){if(blocked)*blocked=true;return state;}i=p;continue;}
        if(p>=end){if(blocked)*blocked=true;return state;}const int finish=int(s.indexOf(s[p],p+1));if(finish<0||finish>=end){if(blocked)*blocked=true;return state;}i=finish+1;continue;
      }
      int p=i;while(p<end&&s[p].isSpace())++p;
      if((name==QStringLiteral("begin")||name==QStringLiteral("end"))&&p<end&&s[p]==QLatin1Char('{')){int q=int(s.indexOf(QLatin1Char('}'),p));if(q>=0&&q<end){QString env=s.mid(p+1,q-p-1);if(name==QStringLiteral("begin")){if(QStringList{QStringLiteral("verbatim"),QStringLiteral("verbatim*"),QStringLiteral("Verbatim"),QStringLiteral("lstlisting"),QStringLiteral("minted"),QStringLiteral("comment"),QStringLiteral("filecontents"),QStringLiteral("filecontents*")}.contains(env))state.literal=env;if(mathEnvironment(env))state.environments.append(env);}else if(!state.environments.isEmpty()&&state.environments.last()==env)state.environments.removeLast();i=q+1;}continue;}
      if((name==QStringLiteral("text")||name==QStringLiteral("textrm")||name==QStringLiteral("textnormal")||name==QStringLiteral("mbox")||name==QStringLiteral("hbox")||name==QStringLiteral("label")||name==QStringLiteral("url")||name==QStringLiteral("path")||name==QStringLiteral("href")||name.contains(QStringLiteral("ref"))||name.contains(QStringLiteral("cite")))&&p<end&&s[p]==QLatin1Char('{')){state.braces.append(state.text);state.text=true;i=p+1;}
      continue;
    }
    if(c==QLatin1Char('{'))state.braces.append(state.text);
    else if(c==QLatin1Char('}')&&!state.braces.isEmpty())state.text=state.braces.takeLast();
    else if(c==QLatin1Char('$')&&!state.text){QString d=QStringLiteral("$");if(i+1<end&&s[i+1]==QLatin1Char('$')){d+=QLatin1Char('$');++i;}if(state.delimiter.isEmpty())state.delimiter=d;else if(state.delimiter==d)state.delimiter.clear();}
    if(state.braces.size()>128||state.environments.size()>128){state.uncertain=true;if(blocked)*blocked=true;return state;}
    ++i;
  }return state;
}
class ContextCache final:public QObject {
public:QTextDocument *document;QMap<int,MathState> checkpoints;
  explicit ContextCache(QTextDocument *d):QObject(d),document(d){setObjectName(QStringLiteral("researchShorthandContext"));checkpoints.insert(0,{});connect(d,&QTextDocument::contentsChange,this,[this](int p,int r,int a){if(!r&&!a)return;auto it=checkpoints.upperBound(document->findBlock(p).blockNumber());while(it!=checkpoints.end())it=checkpoints.erase(it);});}
  int context(int position){auto block=document->findBlock(position);auto it=checkpoints.upperBound(block.blockNumber());--it;auto state=it.value();auto b=document->findBlockByNumber(it.key());while(b.isValid()&&b!=block){state=scan(b.text(),int(b.text().size()),state);b=b.next();if(b.isValid()&&b.blockNumber()%64==0)checkpoints.insert(b.blockNumber(),state);}bool blocked=false;state=scan(block.text(),position-block.position(),state,&blocked);if(blocked||!state.literal.isEmpty()||state.text)return -1;return state.delimiter.isEmpty()&&state.environments.isEmpty()?1:2;}
};
int writingContext(QTextEdit *editor,int position){auto *d=editor->document();auto *cache=dynamic_cast<ContextCache *>(d->findChild<QObject *>(QStringLiteral("researchShorthandContext"),Qt::FindDirectChildrenOnly));if(!cache)cache=new ContextCache(d);return cache->context(position);}
QVector<UserCompletion> rules(QTextEdit *editor,QMainWindow *window){
  if(editor->property("researchCompletionSandbox").toBool()){QVector<UserCompletion> out;for(const auto &item:editor->property("researchShorthandRules").toJsonArray()){auto v=item.toObject();out.append({v[QStringLiteral("prefix")] .toString(),v[QStringLiteral("description")] .toString(),v[QStringLiteral("body")] .toString(),{},v[QStringLiteral("context")] .toInt(),v[QStringLiteral("trigger")] .toInt()});}return out;}
  if(!window->property("researchFocus").toBool()||!settingValue("researchShorthandEnabled").toBool()||settingValue("researchCommandCompletionMode").toInt()==0)return {};
  auto out=effectiveCompletions(window);const auto configured=Tw::Settings().value(QStringLiteral("researchShorthandPresetsV1")).toMap();QSet<QString> custom;const auto values=userCompletions();const QString project=scopeForRules(window,values);for(const auto &v:values)if(v.scope.isEmpty()||v.scope==project)custom.insert(v.prefix);
  for(auto v:presets())if(!custom.contains(v.prefix)){v.trigger=configured.value(v.prefix,0).toInt();if(v.trigger)out.append(v);}return out;
}
}
QVector<UserCompletion> effectiveCompletions(QMainWindow *window){
  const auto values=userCompletions();
  // Global rules do not need root discovery. Keep disabled project overrides
  // in this decision: they can intentionally suppress an enabled global rule.
  const QString project=scopeForRules(window,values);QMap<QString,UserCompletion> selected;for(const auto &v:values)if(v.scope.isEmpty())selected.insert(v.prefix,v);if(!project.isEmpty())for(const auto &v:values)if(v.scope==project)selected.insert(v.prefix,v);QVector<UserCompletion> result;for(const auto &v:selected)if(v.trigger)result.append(v);return result;
}
QVector<UserCompletion> completionsAtCursor(QTextEdit *editor,QMainWindow *window){auto values=effectiveCompletions(window);int context=-2;for(int n=int(values.size())-1;n>=0;--n)if(values[n].context){if(context==-2)context=writingContext(editor,editor->textCursor().selectionStart());if(values[n].context!=context)values.removeAt(n);}return values;}
ShorthandMatch matchShorthand(QTextEdit *editor,QMainWindow *window,bool automatic,const QString &pending){
  if(!editor||editor->isReadOnly()||editor->textCursor().hasSelection())return {};
  const auto cursor=editor->textCursor();const QString left=cursor.block().text().left(cursor.positionInBlock())+pending;
  const auto configuredRules=rules(editor,window);QSet<QString> seenPrefixes;
  for(auto it=configuredRules.crbegin();it!=configuredRules.crend();++it){const auto &v=*it;if(seenPrefixes.contains(v.prefix))continue;seenPrefixes.insert(v.prefix);
    if(v.trigger!=(automatic?2:1)||v.prefix.isEmpty()||!left.endsWith(v.prefix))continue;
    int begin=int(left.size()-v.prefix.size());QString body=v.body;
    if(v.prefix.startsWith(QLatin1Char('.'))){
      const QString term=left.left(begin);int start=int(term.size());
      if(start&&term[start-1]==QLatin1Char('}')){int depth=1;--start;while(start>0&&depth){QChar c=term[--start];if(c==QLatin1Char('}'))++depth;else if(c==QLatin1Char('{'))--depth;}if(depth)return {};int name=start;while(name>0&&term[name-1].isLetter())--name;if(name>0&&term[name-1]==QLatin1Char('\\'))start=name-1;else if(start>0&&!term[start-1].isSpace()&&!QStringLiteral("$=+-*/,(>[|").contains(term[start-1]))continue;}
      else {while(start>0&&(term[start-1].isLetterOrNumber()||term[start-1]==QLatin1Char('_')))--start;if(start>0&&term[start-1]==QLatin1Char('\\'))--start;}
      if(start==term.size())continue;const auto operand=term.mid(start);body.replace(QStringLiteral("${1:}"),operand);begin=start;
    }else if(begin>0&&(left[begin-1].isLetterOrNumber()||left[begin-1]==QLatin1Char('_')||left[begin-1]==QLatin1Char('\\')))continue;
    const int start=cursor.block().position()+begin;const int context=writingContext(editor,start);if(context<0||(v.context&&v.context!=context))continue;
    const auto next=editor->document()->characterAt(cursor.position());if(!automatic&&(next.isLetterOrNumber()||next==QLatin1Char('_')))continue;
    return {start,cursor.position()+int(pending.size()),v.prefix,body};
  }return {};
}

QWidget *completionSettings(QWidget *parent,QMainWindow *window,const QString &selection,bool create,bool compact){
  const bool profiling=window&&qEnvironmentVariableIsSet("TEXLINE_MOTION_PROFILE");QElapsedTimer elapsed;if(profiling)elapsed.start();auto phase=[&](const char *name){if(profiling)window->setProperty(name,double(elapsed.nsecsElapsed())/1e6);};
  auto *page=new QWidget(parent);configureSettingsSurface(page);page->setObjectName(QStringLiteral("researchCompletionPage"));auto *layout=new QVBoxLayout(page);layout->setContentsMargins(0,0,0,0);layout->setSpacing(8);
  auto *mode=settingControl("researchCommandCompletionMode",page);mode->setObjectName(QStringLiteral("completionConfigMode"));settingsRow(layout,words("输入补全","Completion"),mode);
  if(compact)layout=settingsDetails(layout,"researchSnippetDetails",words("管理片段…","Manage snippets…"));
  auto *switches=new QWidget(page);auto *switchRow=new QHBoxLayout(switches);switchRow->setContentsMargins(0,0,0,0);switchRow->setSpacing(20);
  auto *enabled=settingControl("researchShorthandEnabled",switches);enabled->setToolTip(words("在专注模式中使用片段缩写","Use snippet abbreviations in focus mode"));auto *shortcut=settingControl("researchCompletionCtrlSpace",switches);
  for(auto *control:{enabled,shortcut}){control->setMinimumHeight(ControlMetrics::Height);switchRow->addWidget(control);}switchRow->addStretch();layout->addWidget(switches);
  auto *stack=new DetailStack(page);stack->layout()->setSizeConstraint(QLayout::SetNoConstraint);stack->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Preferred);QObject::connect(stack,&QStackedWidget::currentChanged,page,[stack]{stack->updateGeometry();});layout->addWidget(stack,1);auto *library=new QWidget(stack);auto *libraryLayout=new QVBoxLayout(library);libraryLayout->setContentsMargins(0,0,0,0);stack->addWidget(library);
  auto *row=new QHBoxLayout;auto *recommended=new QPushButton(words("启用推荐","Enable recommended"),page);recommended->setObjectName(QStringLiteral("shorthandRecommended"));auto *add=new QPushButton(words("新建片段…","New snippet…"),page);add->setObjectName(QStringLiteral("completionSnippetAdd"));
  auto *search=new QLineEdit(page);search->setPlaceholderText(words("搜索效果或缩写","Find an effect or abbreviation"));search->setObjectName(QStringLiteral("shorthandSearch"));row->addWidget(search,1);row->addWidget(add);libraryLayout->addLayout(row);
  auto *collection=new QTabBar(page);collection->setObjectName(QStringLiteral("shorthandCollection"));collection->setExpanding(false);collection->addTab(words("常用","Common"));collection->addTab(words("我的","Mine"));row->insertWidget(0,collection);search->setMinimumWidth(0);recommended->setFlat(true);libraryLayout->setSpacing(4);
  auto *list=new QListWidget(page);list->setObjectName(QStringLiteral("completionSnippetList"));configureSettingsList(list);list->setItemDelegate(new SnippetDelegate(list));list->setUniformItemSizes(true);list->setMinimumHeight(128);list->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Ignored);libraryLayout->addWidget(new SnippetHeader(list,page));libraryLayout->addWidget(list,1);
  auto filter=[=]{for(int n=0;n<list->count();++n){auto *item=list->item(n);item->setHidden(item->data(Qt::UserRole+1).toBool()!=(collection->currentIndex()==0)||!item->text().contains(search->text(),Qt::CaseInsensitive));}};
  auto values=std::make_shared<QVector<UserCompletion>>(userCompletions());auto presetValues=std::make_shared<QVector<UserCompletion>>(presets());const auto configured=Tw::Settings().value(QStringLiteral("researchShorthandPresetsV1")).toMap();for(auto &v:*presetValues)v.trigger=configured.value(v.prefix,0).toInt();page->setProperty("shorthandTrialUseDefaults",configured.isEmpty());
  auto *detail=new QWidget(stack);detail->setProperty("settingsRevealFallback",QVariant::fromValue(static_cast<QObject *>(search)));auto *fields=new QVBoxLayout(detail);fields->setContentsMargins(0,0,0,0);stack->addWidget(detail);
  auto *back=new QPushButton(words("‹ 返回","‹ Back"),detail);back->setFlat(true);auto *detailActions=new QHBoxLayout;detailActions->addWidget(back);detailActions->addStretch();fields->addLayout(detailActions);
  auto *description=new QLineEdit(detail);description->setObjectName(QStringLiteral("completionSnippetDescription"));settingsRow(fields,words("名称","Name"),description);
  auto *prefix=new QLineEdit(detail);prefix->setObjectName(QStringLiteral("completionSnippetPrefix"));prefix->setPlaceholderText(QStringLiteral("rel"));settingsRow(fields,words("缩写","Abbreviation"),prefix);
  auto *context=new QComboBox(detail);context->addItems({words("任意位置","Anywhere"),words("正文中","In text"),words("公式内","In math")});context->setObjectName(QStringLiteral("shorthandContext"));
  auto *trigger=new QComboBox(detail);trigger->addItems({words("关闭","Off"),words("Tab 展开","Expand with Tab"),words("输入即展开","Expand as typed")});trigger->setObjectName(QStringLiteral("shorthandTrigger"));
  auto *scope=new QComboBox(detail);scope->setObjectName(QStringLiteral("shorthandScope"));scope->addItem(words("所有文稿","All documents"),QString());phase("snippet-controls");const QString project=projectScope(window);phase("snippet-root");if(!project.isEmpty())scope->addItem(words("此项目","This project"),project);
  auto *optionsToggle=new QPushButton(detail);optionsToggle->setObjectName(QStringLiteral("shorthandOptions"));optionsToggle->setFlat(true);optionsToggle->setCheckable(true);
  auto *options=new QWidget(detail);options->setProperty("settingsDisclosure",QVariant::fromValue(static_cast<QObject *>(optionsToggle)));auto *optionsLayout=new QVBoxLayout(options);optionsLayout->setContentsMargins(0,0,0,0);settingsRow(optionsLayout,words("使用位置","Use in"),context);settingsRow(optionsLayout,words("触发方式","Trigger"),trigger);settingsRow(optionsLayout,words("适用文稿","Documents"),scope);options->hide();QObject::connect(optionsToggle,&QPushButton::toggled,page,[=](bool on){options->setVisible(on);stack->updateGeometry();});
  auto *contentLabel=new QLabel(words("片段内容","Snippet content"),detail);fields->addWidget(contentLabel);
  auto *body=new TemplateEdit(detail);contentLabel->setBuddy(body);body->setObjectName(QStringLiteral("completionSnippetBody"));body->setMinimumHeight(110);body->setMaximumHeight(140);fields->addWidget(body);
  auto *markRow=new QHBoxLayout;auto *mark=new QPushButton(words("设为填写处","Make a field"),detail);mark->setObjectName(QStringLiteral("shorthandMakeField"));auto *link=new QComboBox(detail);link->setObjectName(QStringLiteral("shorthandLinkField"));link->addItem(words("联动到…","Link to…"),0);auto *remove=new QPushButton(words("删除","Delete"),detail);remove->setObjectName(QStringLiteral("completionSnippetRemove"));markRow->addWidget(mark);markRow->addWidget(link);markRow->addStretch();remove->setFlat(true);detailActions->addWidget(remove);fields->addLayout(markRow);fields->addWidget(optionsToggle,0,Qt::AlignLeft);fields->addWidget(options);
  auto *preview=new QLabel(detail);preview->setObjectName(QStringLiteral("completionSnippetPreview"));preview->setWordWrap(true);preview->setTextFormat(Qt::PlainText);fields->addWidget(preview);
  auto *trialToggle=new QPushButton(words("试写…","Try it…"),page);trialToggle->setObjectName(QStringLiteral("shorthandTrialToggle"));trialToggle->setFlat(true);trialToggle->setCheckable(true);auto *trialActions=new QHBoxLayout;trialActions->addWidget(trialToggle);trialActions->addStretch();trialActions->addWidget(recommended);layout->addLayout(trialActions);
  auto *trialPane=new QWidget(page);trialPane->setObjectName(QStringLiteral("shorthandTrialPane"));auto *trialLayout=new QVBoxLayout(trialPane);trialLayout->setContentsMargins(0,0,0,0);trialLayout->setSpacing(8);layout->addWidget(trialPane);trialPane->hide();
  phase("snippet-before-trial");
  // A closed trial has no editor to lay out or completion engine to initialize.
  // Keep the created editor for subsequent opens so its draft is preserved.
  auto trial=std::make_shared<QPointer<CompletingEdit>>();
  if(auto *source=qobject_cast<TeXDocumentWindow *>(window)){auto font=source->editor()->font();font.setPointSize(12);body->setFont(font);}
  auto ensureTrial=[=]() -> CompletingEdit * {
    if(*trial)return trial->data();
    auto *sandbox=new QMainWindow(page);sandbox->setWindowFlags(Qt::Widget);sandbox->setObjectName(QStringLiteral("shorthandSandboxHost"));sandbox->setProperty("researchFocus",true);
    auto *editor=new CompletingEdit(sandbox);*trial=editor;editor->setObjectName(QStringLiteral("shorthandTrial"));editor->setProperty("researchCompletionSandbox",true);sandbox->setCentralWidget(editor);sandbox->setMinimumHeight(80);sandbox->setMaximumHeight(100);editor->setLineNumberDisplay(false);editor->setFont(body->font());
    editor->setPlainText(QStringLiteral("$D = $"));auto cursor=editor->textCursor();cursor.setPosition(5);editor->setTextCursor(cursor);installCompletionEditor(sandbox,editor);editor->setProperty("researchShorthandRules",page->property("researchShorthandTrialRules"));trialLayout->addWidget(sandbox);
    if(auto *source=qobject_cast<TeXDocumentWindow *>(window))trialLayout->addWidget(formulaTrialPreview(trialPane,source,editor));
    return editor;
  };
  QObject::connect(trialToggle,&QPushButton::toggled,page,[=](bool on){if(on)ensureTrial();trialPane->setVisible(on);if(on)(*trial)->setFocus();else if(auto *formula=trialPane->findChild<QPushButton *>(QStringLiteral("shorthandFormulaToggle")))formula->setChecked(false);});
  phase("snippet-after-trial");auto *status=new QLabel(page);status->setObjectName(QStringLiteral("shorthandValidation"));status->setWordWrap(true);status->hide();layout->addWidget(status);
  auto active=std::make_shared<int>(-1);auto builtIn=std::make_shared<bool>(false);auto updating=std::make_shared<bool>(false);
  auto refreshTrial=[=]{QJsonArray data;for(const auto &v:*presetValues){auto effective=v;if(!effective.trigger&&page->property("shorthandTrialUseDefaults").toBool())effective.trigger=presets().value(int(&v-presetValues->data())).trigger;data.append(encode(effective));}for(const auto &v:*values)if(v.scope.isEmpty()||v.scope==project)data.append(encode(v));page->setProperty("researchShorthandTrialRules",data);if(*trial)(*trial)->setProperty("researchShorthandRules",data);};
  auto refresh=[=]{list->clear();for(int n=0;n<presetValues->size();++n){const auto &v=presetValues->at(n);auto *item=new QListWidgetItem(v.description+QStringLiteral("    ")+v.prefix+QStringLiteral("    ")+(v.trigger==0?words("关闭","Off"):v.trigger==1?QStringLiteral("Tab"):words("输入即展开","As typed")),list);item->setData(Qt::UserRole,n);item->setData(Qt::UserRole+1,true);item->setData(Qt::UserRole+2,v.description);item->setData(Qt::UserRole+3,v.prefix);item->setData(Qt::UserRole+4,v.trigger==0?words("关闭","Off"):v.trigger==1?QStringLiteral("Tab"):words("输入即展开","As typed"));item->setToolTip(item->text());}for(int n=0;n<values->size();++n){const auto &v=values->at(n);if(!v.scope.isEmpty()&&v.scope!=project)continue;auto *item=new QListWidgetItem((v.description.isEmpty()?v.prefix:v.description)+QStringLiteral("    ")+v.prefix,list);item->setData(Qt::UserRole,n);item->setData(Qt::UserRole+1,false);item->setData(Qt::UserRole+2,v.description.isEmpty()?v.prefix:v.description);item->setData(Qt::UserRole+3,v.prefix);item->setData(Qt::UserRole+4,v.trigger==0?words("关闭","Off"):v.trigger==1?QStringLiteral("Tab"):words("输入即展开","As typed"));item->setToolTip(item->text());}filter();refreshTrial();};
  auto flush=[=]{if(*updating||*active<0)return;optionsToggle->setText(words("展开设置…","Expansion settings…"));optionsToggle->setToolTip(context->currentText()+QStringLiteral(" · ")+trigger->currentText()+QStringLiteral(" · ")+scope->currentText());UserCompletion v{prefix->text(),description->text(),body->templateBody(),scope->currentData().toString(),context->currentIndex(),trigger->currentIndex()};if(*builtIn){page->setProperty("shorthandTrialUseDefaults",false);if(*active<presetValues->size())(*presetValues)[*active].trigger=v.trigger;}else if(*active<values->size())(*values)[*active]=v;const auto parsed=parseCompletionSnippet(v.body);preview->setText(parsed.error);preview->setVisible(!parsed.valid());const auto error=validateUserCompletions(*values);status->setText(error);status->setVisible(!error.isEmpty());page->setProperty("completionValid",error.isEmpty());QJsonArray data;for(const auto &entry:*values)data.append(encode(entry));page->setProperty("completionDraft",data);QVariantMap modes;for(const auto &entry:*presetValues)modes.insert(entry.prefix,entry.trigger);page->setProperty("shorthandPresetDraft",modes);refreshTrial();};
  auto load=[=](int n,bool builtin){detail->setProperty("settingsRevealFallback",QVariant());*updating=true;*active=n;*builtIn=builtin;const auto v=builtin?presetValues->value(n):values->value(n);prefix->setText(v.prefix);description->setText(v.description);body->setTemplate(v.body);context->setCurrentIndex(v.context);trigger->setCurrentIndex(v.trigger);int scopeIndex=scope->findData(v.scope);scope->setCurrentIndex(qMax(0,scopeIndex));for(QWidget *w:QList<QWidget *>{prefix,description,context,scope,mark,link,remove})w->setEnabled(!builtin);link->clear();link->addItem(words("联动到…","Link to…"),0);QSet<int> seen;for(const auto &s:parseCompletionSnippet(v.body).stops)if(!seen.contains(s.number)){seen.insert(s.number);link->addItem(QString::number(s.number),s.number);}body->setReadOnly(builtin);optionsToggle->setText(words("展开设置…","Expansion settings…"));optionsToggle->setToolTip(context->currentText()+QStringLiteral(" · ")+trigger->currentText()+QStringLiteral(" · ")+scope->currentText());*updating=false;flush();optionsToggle->setChecked(false);stack->setCurrentWidget(detail);};
  QObject::connect(list,&QListWidget::itemActivated,page,[=](QListWidgetItem *item){load(item->data(Qt::UserRole).toInt(),item->data(Qt::UserRole+1).toBool());});QObject::connect(list,&QListWidget::itemClicked,page,[=](QListWidgetItem *item){load(item->data(Qt::UserRole).toInt(),item->data(Qt::UserRole+1).toBool());});
  QObject::connect(back,&QPushButton::clicked,page,[=]{flush();refresh();collection->setCurrentIndex(*builtIn?0:1);stack->setCurrentWidget(library);});
  for(auto *field:{prefix,description})QObject::connect(field,&QLineEdit::textChanged,page,[=]{flush();});QObject::connect(body,&QPlainTextEdit::textChanged,page,[=]{flush();});for(auto *field:{context,trigger,scope})QObject::connect(field,&QComboBox::currentIndexChanged,page,[=]{flush();});
  QObject::connect(add,&QPushButton::clicked,page,[=]{flush();values->append({{}, {},literalTemplate(selection),project,0,1});load(int(values->size())-1,false);prefix->setFocus();});
  QObject::connect(remove,&QPushButton::clicked,page,[=]{if(*builtIn||*active<0)return;values->removeAt(*active);*active=-1;detail->setProperty("settingsRevealFallback",QVariant::fromValue(static_cast<QObject *>(search)));refresh();stack->setCurrentWidget(library);page->setProperty("completionValid",validateUserCompletions(*values).isEmpty());QJsonArray data;for(const auto &v:*values)data.append(encode(v));page->setProperty("completionDraft",data);});
  QObject::connect(recommended,&QPushButton::clicked,page,[=]{page->setProperty("shorthandTrialUseDefaults",false);for(int n=0;n<presetValues->size();++n)(*presetValues)[n].trigger=presets()[n].trigger;static_cast<QCheckBox *>(enabled)->setChecked(true);if(static_cast<QComboBox *>(mode)->currentIndex()==0)static_cast<QComboBox *>(mode)->setCurrentIndex(1);QVariantMap modes;for(const auto &v:*presetValues)modes.insert(v.prefix,v.trigger);page->setProperty("shorthandPresetDraft",modes);refresh();trialToggle->setChecked(true);ensureTrial()->setFocus();if(page->property("completionQuickSetup").toBool()&&saveCompletionSettings(page))if(auto *dialog=qobject_cast<QDialog *>(page->window()))dialog->accept();});
  auto *resetPresets=new QPushButton(page);resetPresets->setObjectName(QStringLiteral("completionResetPresets"));resetPresets->hide();QObject::connect(resetPresets,&QPushButton::clicked,page,[=]{QVariantMap modes;for(auto &v:*presetValues){v.trigger=0;modes.insert(v.prefix,0);}page->setProperty("shorthandPresetDraft",modes);page->setProperty("shorthandTrialUseDefaults",false);if(*builtIn&&*active>=0)trigger->setCurrentIndex(0);refresh();});
  QObject::connect(search,&QLineEdit::textChanged,page,[=]{filter();});QObject::connect(collection,&QTabBar::currentChanged,page,[=]{filter();});
  auto updateFieldActions=[=]{const bool selected=body->textCursor().hasSelection()&&!body->isReadOnly();mark->setEnabled(selected);link->setEnabled(selected&&link->count()>1);};
  QObject::connect(body,&QPlainTextEdit::selectionChanged,page,updateFieldActions);QObject::connect(stack,&QStackedWidget::currentChanged,page,[=]{updateFieldActions();});updateFieldActions();
  auto makeField=[=](int number){body->markField(number);if(link->findData(number)<0)link->addItem(QString::number(number),number);};
  QObject::connect(mark,&QPushButton::clicked,page,[=]{int number=1;for(const auto &s:parseCompletionSnippet(body->templateBody()).stops)number=qMax(number,s.number+1);makeField(number);});QObject::connect(link,&QComboBox::activated,page,[=](int index){const int n=link->itemData(index).toInt();if(n>0)makeField(n);link->setCurrentIndex(0);});
  QJsonArray initial;for(const auto &v:*values)initial.append(encode(v));page->setProperty("completionBase",Tw::Settings().value(QStringLiteral("researchLocalSnippetsV1")).toByteArray());page->setProperty("completionDraft",initial);page->setProperty("completionValid",true);page->setProperty("shorthandPresetDraft",configured);refresh();if(create){page->findChild<QWidget *>(QStringLiteral("researchCompletionCtrlSpaceSetting"))->parentWidget()->hide();enabled->parentWidget()->hide();int selectedContext=0;phase("snippet-before-context");if(auto *source=qobject_cast<TeXDocumentWindow *>(window);source&&!selection.isEmpty()&&writingContext(source->editor(),source->textCursor().selectionStart())==2)selectedContext=2;phase("snippet-after-context");mode->parentWidget()->hide();enabled->hide();back->hide();remove->hide();values->append({{}, {},literalTemplate(selection),project,selectedContext,1});load(int(values->size())-1,false);}phase("snippet-done");return page;
}
bool validateCompletionSettings(QWidget *page){if(!page||!page->property("completionValid").toBool())return false;Tw::Settings s;if(s.value(QStringLiteral("researchLocalSnippetsV1")).toByteArray()!=page->property("completionBase").toByteArray()){if(auto *status=page->findChild<QLabel *>(QStringLiteral("shorthandValidation"))){status->setText(words("片段已在另一个窗口更改，请重新打开。","Snippets changed in another window. Reopen this page."));status->show();}return false;}return true;}
bool saveCompletionSettings(QWidget *page){if(!validateCompletionSettings(page))return false;Tw::Settings s;s.setValue(QStringLiteral("researchLocalSnippetsV1"),QJsonDocument(page->property("completionDraft").toJsonArray()).toJson(QJsonDocument::Compact));s.setValue(QStringLiteral("researchShorthandPresetsV1"),page->property("shorthandPresetDraft"));page->setProperty("completionBase",s.value(QStringLiteral("researchLocalSnippetsV1")).toByteArray());saveSettingControls(page);setCommandCompletionMode(settingValue("researchCommandCompletionMode").toInt());return true;}
void installSnippetAuthoring(TeXDocumentWindow *window){
  auto *action=new QAction(words("存为片段…","Save as snippet…"),window);action->setObjectName(QStringLiteral("researchSaveSnippet"));window->addAction(action);
  auto update=[=]{action->setEnabled(window->textCursor().hasSelection()&&!window->editor()->isReadOnly());};QObject::connect(window->editor(),&QTextEdit::selectionChanged,action,update);update();
  QObject::connect(action,&QAction::triggered,window,[=]{auto *dialog=new MaterialDialog(window);configureSettingsSurface(dialog);dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setWindowTitle(words("存为片段","Save as snippet"));auto *layout=new QVBoxLayout(dialog);layout->setContentsMargins(16,16,16,12);layout->setSpacing(12);auto *scroll=new QScrollArea(dialog);scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);auto *page=completionSettings(scroll,window,window->textCursor().selectedText().replace(QChar::ParagraphSeparator,QLatin1Char('\n')),true);scroll->setWidget(page);layout->addWidget(scroll,1);auto *buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel,dialog);layout->addWidget(buttons);QObject::connect(buttons,&QDialogButtonBox::rejected,dialog,&QDialog::reject);QObject::connect(buttons,&QDialogButtonBox::accepted,dialog,[=]{if(saveCompletionSettings(page))dialog->accept();});dialog->resize(QSize(qMin(650,qMax(360,window->width()-24)),440).boundedTo(window->screen()->availableGeometry().size()-QSize(32,48)));dialog->open();});
  if(auto *menu=window->findChild<QMenu *>(QStringLiteral("researchInsertMenu")))menu->addAction(action);
  auto *command=new QAction(words("自定义命令…","Custom command…"),window);command->setObjectName(QStringLiteral("researchCreateCommand"));
  if(auto *menu=window->findChild<QMenu *>(QStringLiteral("researchInsertMenu")))menu->addAction(command);
  QObject::connect(TWApp::instance(),&TWApp::updatedTranslators,window,[action,command]{action->setText(words("存为片段…","Save as snippet…"));command->setText(words("自定义命令…","Custom command…"));});
  QObject::connect(command,&QAction::triggered,window,[=]{
    auto *dialog=new MaterialDialog(window);configureSettingsSurface(dialog);dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setObjectName(QStringLiteral("researchCreateCommandDialog"));dialog->setWindowTitle(words("添加命令","Add a command"));
    auto *layout=new QVBoxLayout(dialog);auto *name=new QLineEdit(dialog);name->setObjectName(QStringLiteral("researchMacroName"));name->setPlaceholderText(QStringLiteral("myresult"));settingsRow(layout,words("命令名称","Command name"),name);
    auto *body=new TemplateEdit(dialog);body->setObjectName(QStringLiteral("researchMacroBody"));body->setTemplate(literalTemplate(window->textCursor().selectedText().replace(QChar::ParagraphSeparator,QLatin1Char('\n'))));body->setMinimumHeight(140);layout->addWidget(body);
    auto *parameter=new QPushButton(words("设为参数","Make a parameter"),dialog);parameter->setObjectName(QStringLiteral("researchMacroParameter"));layout->addWidget(parameter,0,Qt::AlignLeft);QObject::connect(parameter,&QPushButton::clicked,dialog,[body]{int n=1;for(const auto &s:body->stops())n=qMax(n,s.number+1);if(n<=9)body->markField(n);});
    auto *location=new QComboBox(dialog);location->setObjectName(QStringLiteral("researchMacroLocation"));location->addItems({words("文稿导言区","Document preamble"),words("当前位置","Current position")});settingsRow(layout,words("加入位置","Insert in"),location);
    auto *error=new QLabel(dialog);error->setWordWrap(true);layout->addWidget(error);auto *buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel,dialog);buttons->button(QDialogButtonBox::Save)->setText(words("加入文稿","Add to document"));layout->addWidget(buttons);
    QObject::connect(buttons,&QDialogButtonBox::rejected,dialog,&QDialog::reject);
    QObject::connect(buttons,&QDialogButtonBox::accepted,dialog,[=]{
      QString id=name->text().trimmed();if(id.startsWith(QLatin1Char('\\')))id.remove(0,1);static const QRegularExpression valid(QStringLiteral("^[A-Za-z]+$"));if(!valid.match(id).hasMatch()||body->toPlainText().isEmpty()){error->setText(words("填写命令名称与内容。","Enter a command name and content."));return;}
      const auto source=window->textDoc()->editorText();const QRegularExpression existing(QStringLiteral(R"(\\(?:newcommand|renewcommand|providecommand|DeclareRobustCommand)\*?\s*\{?\s*\\%1(?![A-Za-z]))").arg(QRegularExpression::escape(id)));if(existing.match(source).hasMatch()){error->setText(words("文稿中已有这个命令。","This command already exists in the document."));return;}
      auto stops=body->stops();std::sort(stops.begin(),stops.end(),[](const SnippetStop &a,const SnippetStop &b){return a.start>b.start;});QString contents=body->toPlainText();int count=0;for(const auto &s:stops){contents.replace(s.start,s.length,QStringLiteral("#%1").arg(s.number));count=qMax(count,s.number);}if(count>9)return;
      const QString definition=QStringLiteral("\\newcommand{\\%1}%2{%3}\n").arg(id,count?QStringLiteral("[%1]").arg(count):QString(),contents);
      auto cursor=window->textCursor();if(location->currentIndex()==0){const QRegularExpression begin(QStringLiteral(R"(\\begin\s*\{document\})"));const auto match=begin.match(source);cursor.setPosition(match.hasMatch()?int(match.capturedStart()):0);}
      cursor.beginEditBlock();cursor.insertText(definition);cursor.endEditBlock();window->editor()->setTextCursor(cursor);dialog->accept();
    });
    dialog->resize(QSize(qMin(540,qMax(360,window->width()-24)),440).boundedTo(window->screen()->availableGeometry().size()-QSize(32,48)));dialog->open();
  });
}
}
