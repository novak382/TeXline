// Native article structure: views of the original document, never split copies.
#include "ResearchArticle.h"
#include "ResearchAppearance.h"
#include "ResearchMotion.h"
#include "ResearchDependencies.h"
#include "ResearchFind.h"
#include "ResearchProject.h"
#include "ResearchReferences.h"
#include "ResearchObjects.h"
#include "TeXDocumentWindow.h"
#include "CompletingEdit.h"
#include "Settings.h"
#include "TWApp.h"
#include <QAction>
#include <QApplication>
#include <QCryptographicHash>
#include <QDockWidget>
#include <QDir>
#include <QDynamicPropertyChangeEvent>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPointer>
#include <QRegularExpression>
#include <QRunnable>
#include <QScopedValueRollback>
#include <QSet>
#include <QScreen>
#include <QShortcut>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTextBlock>
#include <QTextLayout>
#include <QThreadPool>
#include <QTimer>
#include <QToolButton>
#include <QToolBar>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>
#include <atomic>
#include <algorithm>
#include <memory>
#include <limits>

namespace Research {
namespace {
QString articlePath(const QString &file) {
  const QFileInfo info(file);
  return info.exists() ? info.canonicalFilePath() : info.absoluteFilePath();
}
bool articleEscaped(const QString &s, int p) {
  int n=0; while(p>0 && s[--p]==QLatin1Char('\\'))++n; return n%2;
}
QString articleGroup(const QString &s, int &p, QChar open=QLatin1Char('{'), QChar close=QLatin1Char('}')) {
  while(p<s.size()&&s[p].isSpace())++p;
  if(p>=s.size()||s[p]!=open)return {};
  const int a=++p; int depth=1;
  while(p<s.size()) {if(!articleEscaped(s,p)){if(s[p]==open)++depth;else if(s[p]==close&&!--depth){const auto value=s.mid(a,p-a);++p;return value;}}++p;}
  return {};
}
QString articleRead(const QString &file) {
  QFile f(file);if(!f.open(QIODevice::ReadOnly)||f.size()>4*1024*1024)return {};
  auto bytes=f.readAll();if(bytes.startsWith("\xef\xbb\xbf"))bytes.remove(0,3);
  auto text=QString::fromUtf8(bytes);if(text.toUtf8()!=bytes)return {};
  text.replace(QStringLiteral("\r\n"),QStringLiteral("\n"));return text;
}
struct ArticleParser {
  ArticleDocument out;
  const QHash<QString,QString> &buffers;
  std::function<bool()> cancelled;
  struct Segment {QString file;int a,b; qint64 at;};
  QVector<Segment> segments;
  QVector<qint64> starts, objectStarts;
  QSet<QString> seen, theoremNames{QStringLiteral("theorem"),QStringLiteral("thm"),QStringLiteral("lemma"),QStringLiteral("lem"),QStringLiteral("proposition"),QStringLiteral("prop"),QStringLiteral("corollary"),QStringLiteral("definition"),QStringLiteral("assumption"),QStringLiteral("remark"),QStringLiteral("example"),QStringLiteral("proof")};
  qint64 length{0}, bytes{0};bool documentClass{false};
  ArticleParser(const QHash<QString,QString> &open,const std::function<bool()> &cancel):buffers(open),cancelled(cancel){}
  bool stop(){if(cancelled&&cancelled())out.cancelled=true;return out.cancelled;}
  void segment(const QString &file,int a,int b){if(b>a){segments.append({file,a,b,length});length+=b-a;}}
  void scan(const QString &file,int nesting=0) {
    if(stop())return;
    if(nesting>32||seen.contains(file)||seen.size()>=256){out.uncertain=true;return;}
    seen.insert(file);
    const QString raw=buffers.contains(file)?buffers.value(file):articleRead(file);
    bytes+=raw.size()*2;if(bytes>24*1024*1024||raw.size()>2*1024*1024){out.uncertain=true;return;}
    if(raw.isEmpty()&&!buffers.contains(file)&&(!QFileInfo::exists(file)||QFileInfo(file).size()!=0)){out.sources.insert(file,raw);out.uncertain=true;return;}
    out.sources.insert(file,raw);const auto clean=maskProjectText(raw);
    static const QRegularExpression commands(QStringLiteral(R"(\\([A-Za-z@]+|[^A-Za-z@])|[{}])"));
    static const QRegularExpression label(QStringLiteral(R"(\\label\s*\{([^{}\\#]+)\})"));
    auto it=commands.globalMatch(clean);int depth=0,conditional=0,skip=0,from=0,line=1,lineAt=0;
    struct Env {QString kind,title;int start,line;};QVector<Env> environments;
    while(it.hasNext()) {
      if(stop())return;const auto m=it.next();const int a=int(m.capturedStart());if(a<skip||articleEscaped(clean,a))continue;
      if(m.captured(1).isEmpty()){if(clean[a]==QLatin1Char('{'))++depth;else if(--depth<0){out.uncertain=true;depth=0;}continue;}
      const auto cmd=m.captured(1);if(cmd==QStringLiteral("{" )||cmd==QStringLiteral("}"))continue;
      int p=int(m.capturedEnd());
      if(cmd.startsWith(QStringLiteral("if"))&&depth==0){++conditional;out.uncertain=true;}
      if(cmd==QStringLiteral("fi")&&conditional>0)--conditional;
      if(depth)continue;
      if(cmd==QStringLiteral("includeonly"))out.uncertain=true;
      if(cmd==QStringLiteral("endinput")){segment(file,from,a);return;}
      line+=clean.mid(lineAt,a-lineAt).count(QLatin1Char('\n'));lineAt=a;
      if(cmd==QStringLiteral("documentclass")) {
        articleGroup(clean,p,QLatin1Char('['),QLatin1Char(']'));const auto cls=articleGroup(clean,p);skip=p;
        documentClass|=!cls.isEmpty();out.beamer|=cls==QStringLiteral("beamer")||cls==QStringLiteral("ctexbeamer");continue;
      }
      if(cmd==QStringLiteral("newtheorem")||cmd==QStringLiteral("declaretheorem")) {
        if(p<clean.size()&&clean[p]==QLatin1Char('*'))++p;
        articleGroup(clean,p,QLatin1Char('['),QLatin1Char(']'));const auto name=articleGroup(clean,p);if(!name.isEmpty())theoremNames.insert(name);skip=p;continue;
      }
      if(cmd==QStringLiteral("[")||cmd==QStringLiteral("]")) {
        if(cmd==QStringLiteral("["))environments.append({QStringLiteral("display"),QString(),a,line});
        else if(!environments.isEmpty()&&environments.last().kind==QStringLiteral("display")){
          const auto env=environments.takeLast();bool embedded=false;for(const auto &outer:environments)if(!objectFamily(outer.kind).isEmpty())embedded=true;
          if(!embedded&&out.objects.size()<10000){const auto key=label.match(clean.mid(env.start,p-env.start)).captured(1);out.objects.append({file,QStringLiteral("display")+(key.isEmpty()?QString():QStringLiteral(" · ")+key),QStringLiteral("display"),key,env.start,p,env.line});objectStarts.append(length+env.start-from);}
        }continue;
      }
      if(cmd==QStringLiteral("begin")||cmd==QStringLiteral("end")) {
        const auto name=articleGroup(clean,p);skip=p;
        if(cmd==QStringLiteral("end")) {
          if(name==QStringLiteral("document")){segment(file,from,a);return;}
          if(environments.isEmpty()||environments.last().kind!=name){out.uncertain=true;continue;}
          const auto env=environments.takeLast();
          if((theoremNames.contains(name)||!objectFamily(name).isEmpty())&&out.objects.size()<10000){
            const auto family=objectFamily(name);bool nested=false;for(const auto &outer:environments)if(!objectFamily(outer.kind).isEmpty()&&(objectFamily(outer.kind)==family||objectFamily(outer.kind)==QStringLiteral("figure")||objectFamily(outer.kind)==QStringLiteral("table")||objectFamily(outer.kind)==QStringLiteral("algorithm")))nested=true;if(nested)continue;
            const auto body=clean.mid(env.start,p-env.start);const auto key=label.match(body).captured(1);
            int captionAt=0;const auto captionMatch=QRegularExpression(QStringLiteral(R"(\\caption\s*(?:\[[^\]]*\]\s*)?\{)")).match(body);QString caption;if(captionMatch.hasMatch()){captionAt=int(captionMatch.capturedEnd())-1;caption=articleGroup(body,captionAt);}
            const auto description=caption.isEmpty()?(env.title.isEmpty()?key:env.title):caption;
            out.objects.append({file,name+(description.isEmpty()?QString():QStringLiteral(" · ")+description),name,key,env.start,p,env.line});
            objectStarts.append(length+env.start-from);
          }
        } else {
          if(name==QStringLiteral("document")){if(file==out.root)out.preambleEnd=a;continue;}
          if(name==QStringLiteral("abstract")&&file==out.root)out.abstractStart=a;
          const auto title=articleGroup(clean,p,QLatin1Char('['),QLatin1Char(']'));skip=p;
          environments.append({name,title,a,line});
        }
        continue;
      }
      if(cmd==QStringLiteral("input")||cmd==QStringLiteral("include")||cmd==QStringLiteral("subfile")) {
        auto name=articleGroup(clean,p);if(name.isEmpty()){while(p<clean.size()&&clean[p].isSpace())++p;const int b=p;while(p<clean.size()&&!clean[p].isSpace()&&clean[p]!=QLatin1Char('}'))++p;name=clean.mid(b,p-b);}
        skip=p;segment(file,from,p);from=p;
        auto child=resolveDependency(name,file,out.root,QStringLiteral(".tex"));
        if(child.isEmpty()&&!name.isEmpty()&&!name.contains(QLatin1Char('\\'))&&!name.contains(QLatin1Char('#'))){
          const auto literal=name.endsWith(QStringLiteral(".tex"),Qt::CaseInsensitive)?name:name+QStringLiteral(".tex");
          for(const auto &folder:{QFileInfo(out.root).absolutePath(),QFileInfo(file).absolutePath()}){
            const auto candidate=articlePath(QDir(folder).filePath(literal));
            if(buffers.contains(candidate)){child=candidate;break;}
            if(out.sources.size()<256)out.sources.insert(candidate,QString());
          }
        }
        if(conditional||!environments.isEmpty()||name.contains(QLatin1Char('\\'))||name.contains(QLatin1Char('#'))||child.isEmpty()||!authoredDependency(child,out.root))out.uncertain=true;
        else scan(articlePath(child),nesting+1);
        continue;
      }
      const QStringList levels{QStringLiteral("part"),QStringLiteral("chapter"),QStringLiteral("section"),QStringLiteral("subsection"),QStringLiteral("subsubsection")};
      const int level=levels.indexOf(cmd);if(level<0)continue;
      if(p<clean.size()&&clean[p]==QLatin1Char('*'))++p;
      articleGroup(clean,p,QLatin1Char('['),QLatin1Char(']'));const int titleAt=p;const auto title=articleGroup(clean,p);skip=p;
      if(title.isEmpty()||p>=clean.size()||out.sections.size()>=10000){out.uncertain=true;continue;}
      // A heading nested in a macro/group/environment is never an edit boundary.
      if(conditional||!environments.isEmpty())out.uncertain=true;
      out.sections.append({file,title.simplified(),a,int(raw.size()),line,level,-1,false});
      starts.append(length+a-from);Q_UNUSED(titleAt);
    }
    segment(file,from,int(raw.size()));if(depth||conditional||!environments.isEmpty())out.uncertain=true;
  }
  ArticleDocument finish() {
    scan(out.root);if(stop())return out;
    QVector<int> stack;QVector<qint64> ends;int minimum=9,count=0;
    for(int i=0;i<out.sections.size();++i) {
      if(stop())return out;
      auto &s=out.sections[i];while(!stack.isEmpty()&&out.sections[stack.last()].level>=s.level)stack.removeLast();
      if(!stack.isEmpty())s.parent=stack.last();stack.append(i);minimum=qMin(minimum,s.level);
      qint64 end=length;for(int j=i+1;j<out.sections.size();++j)if(out.sections[j].level<=s.level){end=starts[j];break;}
      ends.append(end);
      bool cross=false,content=false;int localEnd=s.start;
      for(const auto &part:segments){
        const qint64 lo=qMax(starts[i],part.at),hi=qMin(end,part.at+part.b-part.a);if(hi<=lo)continue;
        const int a=part.a+int(lo-part.at),b=part.a+int(hi-part.at);
        const auto text=maskProjectText(out.sources.value(part.file).mid(a,b-a)).trimmed();
        if(part.file==s.file)localEnd=qMax(localEnd,b);else if(!text.isEmpty())cross=true;
        content|=!text.isEmpty();
      }
      s.end=localEnd;const auto &raw=out.sources[s.file];
      auto lineBoundary=[&](int p){return p==0||p==raw.size()||raw.mid(raw.lastIndexOf(QLatin1Char('\n'),p-1)+1,p-(raw.lastIndexOf(QLatin1Char('\n'),p-1)+1)).trimmed().isEmpty();};
      s.editable=!out.uncertain&&!cross&&content&&s.end>s.start&&lineBoundary(s.start)&&lineBoundary(s.end);
    }
    // A book may have one part but many chapters; a paper may have one part
    // containing several sections. Classify at the actual writing-unit level.
    if(std::any_of(out.sections.begin(),out.sections.end(),[](const auto &s){return s.level==1;}))minimum=1;
    else if(std::any_of(out.sections.begin(),out.sections.end(),[](const auto &s){return s.level==2;}))minimum=2;
    for(const auto &s:out.sections)if(s.level==minimum)++count;
    for(int i=0;i<out.objects.size();++i){
      int parent=int(std::upper_bound(starts.begin(),starts.end(),objectStarts[i])-starts.begin())-1;
      while(parent>=0&&objectStarts[i]>=ends[parent])parent=out.sections[parent].parent;
      out.objects[i].section=parent;
    }
    out.article=documentClass&&!out.beamer&&count>=2;
    return out;
  }
};
QThreadPool &articlePool() {
  static QThreadPool pool;static const bool setup=[] {
    pool.setMaxThreadCount(1);pool.setExpiryTimeout(2000);
    QObject::connect(qApp,&QCoreApplication::aboutToQuit,&pool,[]{pool.clear();pool.waitForDone();},Qt::DirectConnection);return true;
  }();Q_UNUSED(setup);return pool;
}
QString articleWords(const QString &zh,const QString &en){return englishUi()?en:Research::ui(zh);}

bool sameArticleOutline(const ArticleDocument &a,const ArticleDocument &b) {
  if(a.root!=b.root||a.article!=b.article||a.beamer!=b.beamer||
     (a.preambleEnd<0)!=(b.preambleEnd<0)||(a.abstractStart<0)!=(b.abstractStart<0)||
     a.sections.size()!=b.sections.size()||a.objects.size()!=b.objects.size())return false;
  for(int i=0;i<a.sections.size();++i){const auto &x=a.sections[i],&y=b.sections[i];
    if(x.file!=y.file||x.title!=y.title||x.level!=y.level||x.parent!=y.parent||x.editable!=y.editable)return false;}
  for(int i=0;i<a.objects.size();++i){const auto &x=a.objects[i],&y=b.objects[i];
    if(x.file!=y.file||x.title!=y.title||x.kind!=y.kind||x.key!=y.key||x.section!=y.section)return false;}
  return true;
}

class ArticleController final:public QObject {
public:
  TeXDocumentWindow *window;
  ArticleDocument document;
  QTimer debounce;
  QFileSystemWatcher *watcher{nullptr};
  std::shared_ptr<std::atomic<quint64>> epoch=std::make_shared<std::atomic<quint64>>(0);
  bool enabled{false},busy{false},changing{false},partial{false},opened{false},narrow{false},focused{false},initialized{false};
  bool focusOpened{false},scopeUpdateQueued{false};
  int selected{-1};
  QString pendingFile,pendingTitle,pendingKey;
  int pendingKind{0};bool pendingPin{false};
  QAction *symbolSearch{nullptr},*scopeAction{nullptr},*fullChoice{nullptr},*sectionChoice{nullptr},*chaptersChoice{nullptr},*objectsChoice{nullptr};
  QTextCursor first,last;
  QWidget *host{nullptr},*scope{nullptr},*body{nullptr};
  QStackedWidget *stack{nullptr};QDockWidget *dock{nullptr};
  QTreeWidget *tree{nullptr};QLineEdit *search{nullptr};QLabel *location{nullptr},*empty{nullptr};
  QHash<QString,bool> expandedBranches;
  QToolButton *full{nullptr},*section{nullptr},*objects{nullptr},*scopeChoice{nullptr},*browseChoice{nullptr};
  struct Place{QTextCursor cursor;int scroll{0};};QHash<QString,Place> places;
  explicit ArticleController(TeXDocumentWindow *w):QObject(w),window(w) {
    setObjectName(QStringLiteral("researchArticle"));debounce.setSingleShot(true);debounce.setInterval(500);
    symbolSearch=new QAction(articleWords(QStringLiteral("搜索论文结构…"),QStringLiteral("Search document symbols…")),w);symbolSearch->setObjectName(QStringLiteral("researchArticleSymbolSearch"));symbolSearch->setVisible(false);w->addAction(symbolSearch);connect(symbolSearch,&QAction::triggered,this,[w]{openProjectNavigation(w);});
    connect(&debounce,&QTimer::timeout,this,[this]{request();});
    window->installEventFilter(this);window->editor()->installEventFilter(this);window->editor()->viewport()->installEventFilter(this);
    connect(window->editor(),&QTextEdit::cursorPositionChanged,this,[this]{cursorChanged();});
    connect(window->textDoc(),&QTextDocument::contentsChange,this,[this](int p,int removed,int added){
      if(!removed&&!added)return;
      if(partial){reveal(first.position(),last.position(),false,p,added);}
      const auto block=window->textDoc()->findBlock(p);
      if(enabled||!initialized||removed>1||block.text().contains(QRegularExpression(QStringLiteral(R"(\\(?:section|subsection|chapter|part|input|include|documentclass|begin|end))"))))invalidate();
    });
    connect(TWApp::instance(),&TWApp::windowListChanged,this,[this]{watchPeers();});watchPeers();
    connect(TWApp::instance(),&TWApp::updatedTranslators,this,[this]{
      symbolSearch->setText(articleWords(QStringLiteral("搜索论文结构…"),QStringLiteral("Search document symbols…")));
      if(!host)return;
      section->setText(articleWords(QStringLiteral("本节"),QStringLiteral("Section")));section->setAccessibleName(section->text());
      objects->setText(articleWords(QStringLiteral("查阅"),QStringLiteral("Browse")));objects->setAccessibleName(objects->text());objects->setToolTip(articleWords(QStringLiteral("显示正文中的图、表、公式、算法与其他对象"),QStringLiteral("Show figures, tables, equations, algorithms and other objects")));
      search->setPlaceholderText(articleWords(QStringLiteral("搜索章节或对象"),QStringLiteral("Find section or object")));
      if(auto *filter=body->findChild<QToolButton*>(QStringLiteral("researchArticleSearchToggle"))){filter->setText(articleWords(QStringLiteral("筛选"),QStringLiteral("Filter")));filter->setAccessibleName(articleWords(QStringLiteral("筛选章节与正文对象"),QStringLiteral("Filter sections and document objects")));filter->setToolTip(filter->accessibleName());configureButton(filter,ButtonRole::Icon);}
      if(auto *close=body->findChild<QToolButton*>(QStringLiteral("researchArticleClose"))){close->setText(articleWords(QStringLiteral("收起结构"),QStringLiteral("Hide structure")));close->setAccessibleName(close->text());}
      window->editor()->setProperty("researchSourceScopeTitle",section->text());findSourceScopeChanged(window);
      configureButton(section,ButtonRole::Text);configureButton(objects,ButtonRole::Text);updateBrowseChoice();populate();updateScope();if(opened)setOpen(true);
    });
  }
  ~ArticleController()override{epoch->fetch_add(1);}
  QSet<QObject*> peers;
  void watchPeers(){for(auto *w:TeXDocumentWindow::documentList())if(w!=window&&!peers.contains(w)){
    peers.insert(w);connect(w,&QObject::destroyed,this,[this,w]{peers.remove(w);if(enabled)invalidate();});
    connect(w->textDoc(),&QTextDocument::contentsChange,this,[this,w](int,int removed,int added){if((removed||added)&&document.sources.contains(articlePath(w->fileName())))invalidate();});
  }}
  QString file()const{return articlePath(window->untitled()?QStringLiteral("__article_unsaved.tex"):window->fileName());}
  QString root()const {const auto r=window->untitled()?file():window->textDoc()->getRootFilePath();return articlePath(r.isEmpty()?file():r);}
  QString settingsKey()const{return QStringLiteral("researchArticleStructure/")+QString::fromLatin1(QCryptographicHash::hash(document.root.toUtf8(),QCryptographicHash::Sha256).toHex());}
  void invalidate(){epoch->fetch_add(1);window->setProperty("researchArticleReady",false);if(!window->property("researchSlideMode").toBool())debounce.start();}
  void request() {
    if(window->property("researchSlideMode").toBool())return;
    // A new draft has no stable path for navigation or a shared project root.
    // Saving already schedules detection with the real file identity.
    if(window->untitled()){busy=false;apply(ArticleDocument{});return;}
    const auto ticket=epoch->fetch_add(1)+1;busy=true;window->setProperty("researchArticleReady",false);
    const auto source=root();QHash<QString,QString> buffers;QHash<TeXDocumentWindow*,int> revisions;
    for(auto *w:TeXDocumentWindow::documentList())if(w==window||(!w->untitled()&&w->isModified())||document.sources.contains(articlePath(w->fileName()))){
      if(w->textDoc()->characterCount()>2*1024*1024)continue;
      buffers.insert(w->untitled()?file():articlePath(w->fileName()),w->textDoc()->editorText());revisions.insert(w,w->textDoc()->revision());
    }
    QPointer<ArticleController> guard(this);const auto token=epoch;
    articlePool().start(QRunnable::create([guard,token,ticket,source,buffers,revisions]{
      const auto value=parseArticleDocument(source,buffers,[token,ticket]{return token->load()!=ticket;});
      if(qApp)QMetaObject::invokeMethod(qApp,[guard,token,ticket,value,revisions]{if(!guard||token->load()!=ticket||value.cancelled)return;for(auto it=revisions.begin();it!=revisions.end();++it)if(TeXDocumentWindow::documentList().contains(it.key())&&it.key()->textDoc()->revision()!=it.value()){guard->invalidate();return;}guard->busy=false;guard->apply(value);},Qt::QueuedConnection);
    }));
  }
  bool canDock()const {
    return window->width()>=900;
  }
  QToolButton *button(QWidget *parent,const QString &name,const QString &text,const QString &icon={}) {
    auto *b=new QToolButton(parent);b->setObjectName(name);b->setText(text);if(!icon.isEmpty())b->setIcon(QIcon(icon));
    configureButton(b,icon.isEmpty()?ButtonRole::Text:ButtonRole::Icon);b->setAccessibleName(text);return b;
  }
  void setup() {
    if(host)return;
    auto *splitter=qobject_cast<QSplitter*>(window->editor()->parentWidget());if(!splitter)return;
    auto *toolbar=window->findChild<QToolBar*>(QStringLiteral("toolBar_run"));if(!toolbar)return;
    const int index=splitter->indexOf(window->editor());host=new QWidget;host->setObjectName(QStringLiteral("researchArticleEditorHost"));
    auto *layout=new QVBoxLayout(host);layout->setContentsMargins(0,0,0,0);layout->setSpacing(0);
    scope=new QWidget(toolbar);scope->setObjectName(QStringLiteral("researchArticleScope"));auto *row=new QHBoxLayout(scope);row->setContentsMargins(4,0,0,0);row->setSpacing(2);
    location=new QLabel(window->statusBar());location->setObjectName(QStringLiteral("researchArticleLocation"));location->setMinimumWidth(0);location->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);location->setStyleSheet(QStringLiteral("font-size:12px;color:#6e6e73;"));window->statusBar()->addWidget(location,1);
    full=button(scope,QStringLiteral("researchArticleFull"),articleWords(QStringLiteral("全文"),QStringLiteral("Full source")));
    section=button(scope,QStringLiteral("researchArticleSection"),articleWords(QStringLiteral("本节"),QStringLiteral("Section")));
    // Retain the existing scope commands; one menu presents their current state.
    for(auto *b:{full,section}){b->setCheckable(true);b->setProperty("researchArticleScopeControl",true);configureButton(b,ButtonRole::Text);b->hide();}
    scopeChoice=button(scope,QStringLiteral("researchArticleScopeButton"),full->text());scopeChoice->setProperty("researchCompactMenu",true);scopeChoice->setPopupMode(QToolButton::InstantPopup);
    auto *scopeMenu=new QMenu(scopeChoice);scopeMenu->setObjectName(QStringLiteral("researchArticleScopeMenu"));scopeMenu->setToolTipsVisible(true);scopeChoice->setMenu(scopeMenu);configureButton(scopeChoice,ButtonRole::Menu);row->addWidget(scopeChoice);
    fullChoice=scopeMenu->addAction(full->text(),full,&QToolButton::click);fullChoice->setObjectName(QStringLiteral("researchArticleFullChoice"));fullChoice->setCheckable(true);
    sectionChoice=scopeMenu->addAction(section->text(),section,&QToolButton::click);sectionChoice->setObjectName(QStringLiteral("researchArticleSectionChoice"));sectionChoice->setCheckable(true);
    connect(scopeMenu,&QMenu::aboutToShow,this,[this]{updateScope();});
    connect(scopeMenu,&QMenu::aboutToHide,this,[this]{QTimer::singleShot(0,window,[this]{window->editor()->setFocus();});});
    QAction *before=nullptr;for(auto *action:toolbar->actions())if(auto *widget=toolbar->widgetForAction(action);widget&&widget->sizePolicy().horizontalPolicy()==QSizePolicy::Expanding){before=action;break;}
    scopeAction=toolbar->insertWidget(before,scope);scopeAction->setObjectName(QStringLiteral("researchArticleScopeAction"));
    stack=new QStackedWidget(host);layout->addWidget(stack,1);splitter->replaceWidget(index,host);stack->addWidget(window->editor());
    connect(full,&QToolButton::clicked,this,[this]{setPartial(false);window->editor()->setFocus();});
    connect(section,&QToolButton::clicked,this,[this]{setPartial(true);window->editor()->setFocus();});
    body=new QWidget;body->setObjectName(QStringLiteral("researchArticleStructureBody"));body->setStyleSheet(QStringLiteral("QWidget#researchArticleStructureBody {background:#ffffff;} QLabel {font-size:14px;color:#30363c;}"));auto *contents=new QVBoxLayout(body);contents->setContentsMargins(8,0,8,4);contents->setSpacing(4);
    auto *header=new QHBoxLayout;header->setSpacing(2);auto *caption=new QLabel(body);caption->setObjectName(QStringLiteral("researchArticleRoot"));caption->hide();
    objects=button(body,QStringLiteral("researchArticleObjects"),articleWords(QStringLiteral("查阅"),QStringLiteral("Browse")));objects->setCheckable(true);objects->hide();
    browseChoice=button(body,QStringLiteral("researchArticleBrowseButton"),articleWords(QStringLiteral("章节"),QStringLiteral("Outline")));browseChoice->setProperty("researchCompactMenu",true);browseChoice->setPopupMode(QToolButton::InstantPopup);
    auto *browseMenu=new QMenu(browseChoice);browseMenu->setObjectName(QStringLiteral("researchArticleBrowseMenu"));browseChoice->setMenu(browseMenu);configureButton(browseChoice,ButtonRole::Menu);header->addWidget(browseChoice);header->addStretch();
    chaptersChoice=browseMenu->addAction(articleWords(QStringLiteral("章节"),QStringLiteral("Outline")),this,[this]{objects->setChecked(false);updateBrowseChoice();});chaptersChoice->setObjectName(QStringLiteral("researchArticleChaptersChoice"));chaptersChoice->setCheckable(true);
    objectsChoice=browseMenu->addAction(objects->text(),this,[this]{objects->setChecked(true);updateBrowseChoice();});objectsChoice->setObjectName(QStringLiteral("researchArticleObjectsChoice"));objectsChoice->setCheckable(true);updateBrowseChoice();
    auto *find=button(body,QStringLiteral("researchArticleSearchToggle"),articleWords(QStringLiteral("筛选"),QStringLiteral("Filter")),QStringLiteral(":/research/icons/find.svg"));find->setAccessibleName(articleWords(QStringLiteral("筛选章节与正文对象"),QStringLiteral("Filter sections and document objects")));find->setToolTip(find->accessibleName());find->setCheckable(true);header->addWidget(find);
    auto *close=button(body,QStringLiteral("researchArticleClose"),articleWords(QStringLiteral("收起结构"),QStringLiteral("Hide structure")),QStringLiteral(":/research/icons/close.svg"));close->hide();contents->addLayout(header);
    search=new QLineEdit(body);search->setObjectName(QStringLiteral("researchArticleSearch"));search->setPlaceholderText(articleWords(QStringLiteral("搜索章节或对象"),QStringLiteral("Find section or object")));configureField(search);search->setClearButtonEnabled(true);search->hide();contents->addWidget(search);
    tree=new QTreeWidget(body);tree->setObjectName(QStringLiteral("researchArticleTree"));tree->setHeaderHidden(true);tree->setColumnCount(1);tree->setIndentation(14);tree->setFrameShape(QFrame::NoFrame);tree->setWordWrap(false);tree->setTextElideMode(Qt::ElideRight);tree->setAnimated(motionEnabled());tree->setStyleSheet(QStringLiteral("QTreeView {border:0;background:#ffffff;font-size:15px;widget-animation-duration:150;} QTreeView::item {min-height:26px;padding:2px 4px;}"));tree->setContextMenuPolicy(Qt::CustomContextMenu);contents->addWidget(tree,1);
    empty=new QLabel(body);empty->setObjectName(QStringLiteral("researchArticleEmpty"));empty->setWordWrap(true);empty->setAlignment(Qt::AlignTop);empty->hide();contents->insertWidget(contents->indexOf(tree),empty);
    objects->setToolTip(articleWords(QStringLiteral("显示正文中的图、表、公式、算法与其他对象"),QStringLiteral("Show figures, tables, equations, algorithms and other objects")));
    dock=new QDockWidget(window);dock->setObjectName(QStringLiteral("researchArticleDock"));dock->setFeatures(QDockWidget::NoDockWidgetFeatures);dock->setAllowedAreas(Qt::LeftDockWidgetArea);auto *blank=new QWidget(dock);blank->setFixedHeight(0);dock->setTitleBarWidget(blank);dock->setWidget(body);window->addDockWidget(Qt::LeftDockWidgetArea,dock);dock->hide();
    connect(close,&QToolButton::clicked,this,[this]{setOpen(false,true);});
    connect(find,&QToolButton::toggled,this,[this](bool on){search->setVisible(on);if(on)search->setFocus();else{search->clear();tree->setFocus();}});
    connect(search,&QLineEdit::textChanged,this,[this]{populate();});
    auto *escape=new QShortcut(QKeySequence(Qt::Key_Escape),body);escape->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escape,&QShortcut::activated,this,[this,find]{if(find->isChecked())find->setChecked(false);else{setOpen(false,true);window->editor()->setFocus();}});
    search->installEventFilter(this);tree->installEventFilter(this);tree->viewport()->installEventFilter(this);
    connect(search,&QLineEdit::returnPressed,this,[this]{QTreeWidgetItemIterator it(tree);while(*it){if(!(*it)->isHidden()&&(*it)->data(0,Qt::UserRole+1).toInt()!=2){navigate(*it,(*it)->data(0,Qt::UserRole+1).toInt()==1);return;}++it;}});
    for(auto signal:{&QTreeWidget::itemExpanded,&QTreeWidget::itemCollapsed})connect(tree,signal,this,[this](QTreeWidgetItem *item){if(search->text().isEmpty())expandedBranches.insert(item->data(0,Qt::UserRole+2).toString(),item->isExpanded());});
    connect(objects,&QToolButton::toggled,this,[this]{updateBrowseChoice();populate();});
    connect(tree,&QTreeWidget::itemClicked,this,[this](QTreeWidgetItem *item,int){navigate(item,item->data(0,Qt::UserRole+1).toInt()==1);});
    connect(tree,&QTreeWidget::customContextMenuRequested,this,[this](const QPoint &pos){auto *item=tree->itemAt(pos);if(!item)return;QMenu menu(tree);const int kind=item->data(0,Qt::UserRole+1).toInt();if(kind==0){auto *rename=menu.addAction(articleWords(QStringLiteral("修改标题…"),QStringLiteral("Rename heading…")),[this,item]{renameSection(item);});rename->setShortcut(QKeySequence(Qt::Key_F2));menu.addAction(articleWords(QStringLiteral("编辑标题源码"),QStringLiteral("Edit heading source")),[this,item]{renameSection(item,true);});}else if(kind==1){menu.addAction(articleWords(QStringLiteral("查阅"),QStringLiteral("Read alongside")),[this,item]{navigate(item,true);});menu.addAction(articleWords(QStringLiteral("定位源码"),QStringLiteral("Go to source")),[this,item]{navigate(item,false);});}if(!menu.isEmpty())menu.exec(tree->viewport()->mapToGlobal(pos));});
  }
  void updateBrowseChoice() {
    const auto chapters=articleWords(QStringLiteral("章节"),QStringLiteral("Outline"));
    chaptersChoice->setText(chapters);chaptersChoice->setChecked(!objects->isChecked());objectsChoice->setText(objects->text());objectsChoice->setChecked(objects->isChecked());
    browseChoice->setText(objects->isChecked()?objects->text():chapters);browseChoice->setAccessibleName(browseChoice->text());configureButton(browseChoice,ButtonRole::Menu);
  }
  void setOpen(bool on,bool remember=false) {
    if(!enabled)return;setup();if(!host)return;opened=on;narrow=on&&!canDock();
    // An explicit navigation or saved-place restore during focus supersedes
    // the directory visibility folded on entry.
    if(focused)focusOpened=on;
    // Structure must never replace the editor, including a half-screen window.
    stack->setCurrentWidget(window->editor());if(body->parentWidget()!=dock){stack->removeWidget(body);dock->setWidget(body);}
    if(on){
      const int limit=qMax(120,window->width()/3),width=qMin(dock->isHidden()?240:dock->width(),limit);
      const auto label=articleWords(QStringLiteral("查阅"),width<190?QStringLiteral("Browse"):QStringLiteral("Browse"));
      if(objects->text()!=label){objects->setText(label);configureButton(objects,ButtonRole::Text);}
      dock->setMinimumWidth(qMin(160,width));dock->setMaximumWidth(limit);setPanelVisible(dock,true,width,remember);
    }else setPanelVisible(dock,false,0,remember);
    tree->setAnimated(motionEnabled());
    showScope(enabled&&!focused);
    if(remember&&!window->untitled())Tw::Settings().setValue(settingsKey(),on);
    if(auto *a=window->findChild<QAction*>(QStringLiteral("researchOutlineAction"))){a->setCheckable(true);a->setChecked(on);}
  }
  void showScope(bool on){if(scopeAction)scopeAction->setVisible(on);if(scope)scope->setVisible(on);if(location)location->setVisible(on);}
  void populate() {
    if(!tree)return;QSignalBlocker block(tree);tree->setAnimated(false);QHash<QString,int> occurrences;
    const int scroll=tree->verticalScrollBar()->value();tree->clear();QVector<QTreeWidgetItem*> items;
    auto landmark=[&](int index,const QString &title){auto *item=new QTreeWidgetItem(tree);item->setText(0,title);item->setData(0,Qt::UserRole,index);item->setData(0,Qt::UserRole+1,2);item->setToolTip(0,QFileInfo(document.root).fileName());};
    if(document.preambleEnd>=0)landmark(0,articleWords(QStringLiteral("导言"),QStringLiteral("Preamble")));
    if(document.abstractStart>=0)landmark(1,articleWords(QStringLiteral("摘要"),QStringLiteral("Abstract")));
    for(int i=0;i<document.sections.size();++i){const auto &s=document.sections[i];auto *item=new QTreeWidgetItem;item->setText(0,s.title);item->setData(0,Qt::UserRole,i);item->setData(0,Qt::UserRole+1,0);const auto identity=s.file+QLatin1Char('\n')+QString::number(s.level)+QLatin1Char('\n')+s.title;item->setData(0,Qt::UserRole+2,identity+QLatin1Char('\n')+QString::number(occurrences[identity]++));item->setToolTip(0,s.title+QLatin1Char('\n')+QFileInfo(s.file).fileName());if(s.parent>=0&&s.parent<items.size())items[s.parent]->addChild(item);else tree->addTopLevelItem(item);items.append(item);item->setExpanded(expandedBranches.value(item->data(0,Qt::UserRole+2).toString()));if(i==selected)tree->setCurrentItem(item);}
    if(objects->isChecked()||!search->text().isEmpty())for(int i=0;i<document.objects.size();++i){const auto &o=document.objects[i];const int parent=o.section;auto *item=parent>=0&&parent<items.size()?new QTreeWidgetItem(items[parent]):new QTreeWidgetItem(tree);item->setText(0,ui(Research::objectName(o.kind,englishUi()))+(o.title.startsWith(o.kind)?o.title.mid(o.kind.size()):QStringLiteral(" · ")+o.title));item->setData(0,Qt::UserRole,i);item->setData(0,Qt::UserRole+1,1);item->setToolTip(0,o.key+QLatin1Char('\n')+QFileInfo(o.file).fileName());}
    filterTree();tree->verticalScrollBar()->setValue(scroll);tree->setAnimated(motionEnabled());
  }
  void filterTree(){
    if(!tree)return;const auto query=search->text();int shown=0;
    std::function<bool(QTreeWidgetItem*)> filter=[&](QTreeWidgetItem *item){
      bool show=query.isEmpty()||item->text(0).contains(query,Qt::CaseInsensitive)||item->toolTip(0).contains(query,Qt::CaseInsensitive);
      for(int i=0;i<item->childCount();++i)show=filter(item->child(i))||show;
      item->setHidden(!show);if(show)++shown;
      item->setExpanded(query.isEmpty()?expandedBranches.value(item->data(0,Qt::UserRole+2).toString()):show);return show;
    };
    for(int i=0;i<tree->topLevelItemCount();++i)filter(tree->topLevelItem(i));
    empty->setText(articleWords(QStringLiteral("没有匹配的章节或对象"),QStringLiteral("No matching sections or objects")));empty->setVisible(!shown);
  }
  void apply(const ArticleDocument &value) {
    const int previousSelection=selected;
    const bool wasEnabled=enabled,reuseOutline=enabled&&tree&&sameArticleOutline(document,value);document=value;enabled=value.article;initialized=true;symbolSearch->setVisible(enabled);
    window->setProperty("researchArticleMode",enabled);window->setProperty("researchArticleReady",true);QTimer::singleShot(0,this,[this]{if(window->property("researchRestoreArticlePlace").isValid()){const auto state=window->property("researchRestoreArticlePlace").toMap();window->setProperty("researchRestoreArticlePlace",QVariant());restoreArticleReadingState(window,state);}});
    if(!enabled){if(watcher){delete watcher;watcher=nullptr;}if(partial)setPartial(false);if(dock)dock->hide();opened=false;if(stack)stack->setCurrentWidget(window->editor());showScope(false);opened=false;if(auto *a=window->findChild<QAction*>(QStringLiteral("researchOutlineAction"))){a->setChecked(false);a->setCheckable(false);}return;}
    setup();if(!host)return;
    if(auto *label=body->findChild<QLabel*>(QStringLiteral("researchArticleRoot"))){label->setText(QFileInfo(document.root).completeBaseName());label->setToolTip(document.root);}
    // Keep live slice anchors through typing; never reselect a heading by title.
    if(partial&&selected>=0){const int at=first.position();selected=-1;for(int i=0;i<document.sections.size();++i)if(document.sections[i].file==file()&&document.sections[i].start==at){selected=i;break;}}
    if(partial){
      bool valid=selected>=0&&document.sections[selected].editable;
      if(valid){const auto &s=document.sections[selected];int end=s.end;if(end>s.start&&window->textDoc()->characterAt(end-1)==QChar::ParagraphSeparator)--end;valid=end==last.position();}
      if(!valid)setPartial(false);
    }
    if(selected<0||!partial)updateSelected();
    // Offsets always come from the new document. Unchanged visible rows can
    // retain their expansion animation, selection and reading position.
    if(reuseOutline){if(selected!=previousSelection){QSignalBlocker guard(tree);QTreeWidgetItemIterator it(tree);while(*it){if((*it)->data(0,Qt::UserRole+1).toInt()==0&&(*it)->data(0,Qt::UserRole).toInt()==selected){if(tree->currentItem()!=*it)tree->setCurrentItem(*it);break;}++it;}}}
    else populate();updateScope();
    if(!watcher){watcher=new QFileSystemWatcher(this);connect(watcher,&QFileSystemWatcher::fileChanged,this,[this]{invalidate();});connect(watcher,&QFileSystemWatcher::directoryChanged,this,[this]{invalidate();});}auto paths=document.sources.keys();const auto previous=watcher->files()+watcher->directories();if(!previous.isEmpty())watcher->removePaths(previous);for(const auto &p:paths){if(QFileInfo::exists(p))watcher->addPath(p);else{auto folder=QFileInfo(p).absoluteDir();while(!folder.exists()&&folder.cdUp()){}if(folder.exists())watcher->addPath(folder.absolutePath());}}
    if(!wasEnabled){const bool show=Tw::Settings().value(settingsKey(),canDock()).toBool()&&canDock();setOpen(show&&!focused);if(focused)focusOpened=show;}else if(opened)setOpen(true);
    if(!pendingFile.isEmpty()) {
      const auto file=pendingFile,title=pendingTitle,key=pendingKey;const int kind=pendingKind;const bool object=kind==1,pin=pendingPin;pendingFile.clear();
      int found=-1,matches=0;
      if(kind==2){found=title.toInt();matches=(found==0?document.preambleEnd>=0:document.abstractStart>=0)?1:0;}
      else if(object){for(int i=0;i<document.objects.size();++i){const auto &o=document.objects[i];if(o.file==file&&(!key.isEmpty()?o.key==key:o.title==title)){found=i;++matches;}}}
      else {for(int i=0;i<document.sections.size();++i)if(document.sections[i].file==file&&document.sections[i].title==title){found=i;++matches;}}
      if(matches==1)QTimer::singleShot(0,this,[this,kind,pin,found]{QTreeWidgetItemIterator it(tree);while(*it){if((*it)->data(0,Qt::UserRole+1).toInt()==kind&&(*it)->data(0,Qt::UserRole).toInt()==found){navigate(*it,pin);return;}++it;}});
      else showTransientStatusMessage(window,articleWords(QStringLiteral("章节已变化，请重新选择"),QStringLiteral("The section changed. Select it again.")));
    }
  }
  void rememberPlace(){if(selected>=0&&selected<document.sections.size())places.insert(document.sections[selected].file+QString::number(document.sections[selected].start),{window->textCursor(),window->editor()->verticalScrollBar()->value()});}
  void updateSelected(){const int at=window->cursorPosition();const auto currentFile=file();selected=-1;for(int i=0;i<document.sections.size();++i){const auto &s=document.sections[i];if(s.file==currentFile&&at>=s.start&&at<s.end&&(selected<0||s.level>document.sections[selected].level))selected=i;}}
  void updateScope() {
    if(!scope)return;const auto fullText=articleWords(document.sources.size()>1?QStringLiteral("本文件"):QStringLiteral("全文"),document.sources.size()>1?QStringLiteral("File"):QStringLiteral("Full source"));
    if(full->text()!=fullText){full->setText(fullText);full->setAccessibleName(fullText);configureButton(full,ButtonRole::Text);}
    full->setChecked(!partial);section->setChecked(partial);section->setEnabled(selected>=0&&selected<document.sections.size()&&document.sections[selected].editable);
    section->setToolTip(section->isEnabled()?QString():selected<0?articleWords(QStringLiteral("请选择一个章节"),QStringLiteral("Select a section")):articleWords(QStringLiteral("本节跨文件或边界不明确，请按源文件编辑"),QStringLiteral("This section crosses files or has uncertain boundaries. Edit the source file.")));
    const auto title=selected>=0&&selected<document.sections.size()?document.sections[selected].title:QFileInfo(file()).fileName();
    const auto role=file()==document.root?articleWords(QStringLiteral("主文件"),QStringLiteral("Main file")):articleWords(QStringLiteral("子文件"),QStringLiteral("Subfile"));
    const auto fileContext=role+QStringLiteral(" · ")+QFileInfo(file()).fileName();
    const auto context=document.sources.size()>1?fileContext+(selected>=0?QStringLiteral(" › ")+title:QString()):title;
    location->setText(context);location->setToolTip(context+QLatin1Char('\n')+document.root);
    fullChoice->setText(articleWords(document.sources.size()>1?QStringLiteral("编辑本文件"):QStringLiteral("编辑全文"),document.sources.size()>1?QStringLiteral("Edit this file"):QStringLiteral("Edit full source")));fullChoice->setChecked(!partial);
    sectionChoice->setText(articleWords(QStringLiteral("只编辑本节"),QStringLiteral("Edit this section"))+(selected>=0?QStringLiteral(" · ")+title:QString()));sectionChoice->setChecked(partial);sectionChoice->setEnabled(section->isEnabled());sectionChoice->setToolTip(section->toolTip());
    const auto scopeText=partial?section->text()+QStringLiteral(" · ")+title:fullText;
    const auto caption=scopeChoice->fontMetrics().elidedText(scopeText,Qt::ElideRight,qBound(96,window->width()/3,240)-28);
    if(scopeChoice->text()!=caption){scopeChoice->setText(caption);configureButton(scopeChoice,ButtonRole::Menu);}
    scopeChoice->setAccessibleName(articleWords(QStringLiteral("编辑范围：%1"),QStringLiteral("Editing scope: %1")).arg(scopeText));scopeChoice->setToolTip(scopeChoice->accessibleName()+QLatin1Char('\n')+fileContext);
    browseChoice->setToolTip(fileContext+QLatin1Char('\n')+document.root);
    window->setProperty("researchArticleSelected",selected);
  }
  void reveal(int begin,int end,bool all=false,int changed=-1,int added=0) {
    QScopedValueRollback<bool> guard(changing,true);auto *doc=window->textDoc();const int scroll=window->editor()->verticalScrollBar()->value();
    const auto firstBlock=doc->findBlock(begin),lastBlock=doc->findBlock(end);bool dirty=false;
    // Unchanged blocks retain their visibility. Only inserted/merged paragraphs
    // and their immediate neighbours need checking during a local edit.
    auto b=changed<0?doc->begin():doc->findBlock(qMax(0,changed-1));
    const auto stop=changed<0?QTextBlock():doc->findBlock(qMin(doc->characterCount()-1,changed+added+1)).next();
    for(;b.isValid()&&b!=stop;b=b.next()){const bool visible=all||(b.position()>=firstBlock.position()&&b.position()<=lastBlock.position());if(b.isVisible()!=visible){b.setVisible(visible);dirty=true;}}
    window->editor()->setProperty("researchSourceSlice",!all);window->editor()->setProperty("researchSourceScopeTitle",articleWords(QStringLiteral("本节"),QStringLiteral("Section")));
    if(dirty)doc->markContentsDirty(0,doc->characterCount());findSourceScopeChanged(window);window->editor()->viewport()->update();window->editor()->verticalScrollBar()->setValue(scroll);
  }
  void setPartial(bool on) {
    if(on&&(selected<0||selected>=document.sections.size()||!document.sections[selected].editable))return;
    if(on){const auto &s=document.sections[selected];if(document.sources.value(s.file)!=window->textDoc()->editorText()){invalidate();return;}first=QTextCursor(window->textDoc());first.setPosition(s.start);first.setKeepPositionOnInsert(true);last=QTextCursor(window->textDoc());int end=s.end;if(end>s.start&&window->textDoc()->characterAt(end-1)==QChar::ParagraphSeparator)--end;last.setPosition(end);partial=true;reveal(first.position(),last.position());if(window->cursorPosition()<first.position()||window->cursorPosition()>last.position()){auto c=window->textCursor();c.setPosition(first.position());window->editor()->setTextCursor(c);}}
    else {partial=false;reveal(0,0,true);}updateScope();window->editor()->ensureCursorVisible();
  }
#include "ResearchArticleRename.inc"
  void navigate(QTreeWidgetItem *item,bool pin=false) {
    if(!item)return;
    const int kind=item->data(0,Qt::UserRole+1).toInt();const bool object=kind==1,special=kind==2;const int index=item->data(0,Qt::UserRole).toInt();
    QString targetFile;int at=0;if(special){targetFile=document.root;at=index==0?0:document.abstractStart;}else if(object){if(index>=document.objects.size())return;targetFile=document.objects[index].file;at=document.objects[index].start;}else{if(index>=document.sections.size())return;targetFile=document.sections[index].file;at=document.sections[index].start;}
    if(busy||!window->property("researchArticleReady").toBool()) {
      pendingFile=targetFile;pendingKind=kind;pendingPin=pin;pendingTitle=special?QString::number(index):object?document.objects[index].title:document.sections[index].title;pendingKey=object?document.objects[index].key:QString();debounce.start(0);return;
    }
    if(pin&&object){rememberReadingPlace(window);pinSourceObject(window,targetFile,at);return;}
    rememberReadingPlace(window);rememberPlace();auto *target=TeXDocumentWindow::findDocument(targetFile);if(!target)target=TeXDocumentWindow::openDocument(targetFile,false,false);if(!target)return;
    auto *other=dynamic_cast<ArticleController*>(target->findChild<QObject*>(QStringLiteral("researchArticle")));if(!other)return;
    const bool local=partial;const bool closeAfter=narrow;const auto geometry=window->geometry();
    if(target!=window){target->setGeometry(geometry);target->textDoc()->setProperty("researchArticleRoot",document.root);target->textDoc()->setProperty("researchArticleSource",target->textDoc()->absoluteFilePath());other->epoch->fetch_add(1);other->busy=false;other->apply(document);}
    if(other->partial)other->setPartial(false);
    other->selected=object||special?-1:index;auto c=QTextCursor(target->textDoc());c.setPosition(qBound(0,at,target->textDoc()->characterCount()-1));
    if(!object&&!special){const auto key=targetFile+QString::number(at);if(other->places.contains(key)&&other->places[key].cursor.document()==target->textDoc())c=other->places[key].cursor;}
    {QScopedValueRollback<bool> guard(other->changing,true);target->editor()->setTextCursor(c);}if(object)other->updateSelected();
    if(local&&!special)other->setPartial(true);other->updateScope();if(closeAfter){setOpen(false);other->setOpen(false);}else if(target!=window)other->setOpen(opened);
    target->editor()->ensureCursorVisible();if(target!=window)target->selectWindow();target->editor()->setFocus();if(pin)pinSourceReference(target);
  }
  void cursorChanged(){if(changing||!enabled)return;if(partial){const auto c=window->textCursor();if(c.selectionStart()<first.position()||c.selectionEnd()>last.position())setPartial(false);else return;}const int previousSelection=selected;updateSelected();updateScope();if(tree&&selected!=previousSelection){QSignalBlocker guard(tree);QTreeWidgetItemIterator it(tree);while(*it){if((*it)->data(0,Qt::UserRole+1).toInt()==0&&(*it)->data(0,Qt::UserRole).toInt()==selected){tree->setCurrentItem(*it);break;}++it;}}}
  void focus(bool on){
    if(focused==on)return;focused=on;
    // Focus changes chrome, not the live writing scope. The workspace
    // transition already preserves the current cursor and reading anchor.
    if(on){
      focusOpened=opened;
      if(dock){dock->setProperty("researchFocusRestore",false);dock->hide();}
      opened=false;if(stack)stack->setCurrentWidget(window->editor());showScope(false);
    }else if(enabled)setOpen(focusOpened);
    // Keep the existing debounced, cancellable index alive: new headings or
    // changed includes must invalidate a slice even while its toolbar is hidden.
  }
  bool eventFilter(QObject *object,QEvent *event)override {
    if(window->property("researchRestoreArticlePlace").isValid()&&(event->type()==QEvent::KeyPress||event->type()==QEvent::InputMethod||event->type()==QEvent::Wheel||event->type()==QEvent::MouseButtonPress))window->setProperty("researchRestoreArticlePlace",QVariant());
    if(object==tree&&event->type()==QEvent::KeyPress){const auto *key=static_cast<QKeyEvent*>(event);if(key->key()==Qt::Key_Return||key->key()==Qt::Key_Enter){if(auto *item=tree->currentItem())navigate(item,item->data(0,Qt::UserRole+1).toInt()==1);return true;}}
    if(object->objectName()==QStringLiteral("researchArticleTitleEditor")&&(event->type()==QEvent::FocusOut||(event->type()==QEvent::KeyPress&&static_cast<QKeyEvent*>(event)->key()==Qt::Key_Escape))){auto *field=static_cast<QWidget*>(object);field->hide();field->deleteLater();tree->setFocus();return event->type()==QEvent::KeyPress;}
    if(object==tree&&event->type()==QEvent::KeyPress&&static_cast<QKeyEvent*>(event)->key()==Qt::Key_F2){renameSection(tree->currentItem());return true;}
    if(tree&&(object==tree||object==tree->viewport())&&(event->type()==QEvent::KeyPress||event->type()==QEvent::MouseButtonPress))tree->setAnimated(motionEnabled());
    if((object==tree||object==search)&&event->type()==QEvent::KeyPress&&static_cast<QKeyEvent*>(event)->key()==Qt::Key_Escape){
      if(auto *find=body->findChild<QToolButton*>(QStringLiteral("researchArticleSearchToggle"));find&&find->isChecked())find->setChecked(false);else{setOpen(false,true);window->editor()->setFocus();}return true;
    }
    if(object==window&&event->type()==QEvent::DynamicPropertyChange&&static_cast<QDynamicPropertyChangeEvent*>(event)->propertyName()=="researchSlideMode"){
      if(window->property("researchSlideMode").toBool()){if(partial)setPartial(false);enabled=false;opened=false;window->setProperty("researchArticleMode",false);if(stack)stack->setCurrentWidget(window->editor());symbolSearch->setVisible(false);if(dock)dock->hide();showScope(false);epoch->fetch_add(1);debounce.stop();}else invalidate();
    }
    if(object==window&&event->type()==QEvent::Resize&&!scopeUpdateQueued){scopeUpdateQueued=true;QTimer::singleShot(0,this,[this]{scopeUpdateQueued=false;if(scope&&partial)updateScope();if(opened)setOpen(true);});}
    if(!partial||changing||object!=window->editor()||(event->type()!=QEvent::KeyPress&&event->type()!=QEvent::ShortcutOverride))return false;
    auto *key=static_cast<QKeyEvent*>(event);auto c=window->textCursor();const int a=first.position(),b=last.position();
    const bool all=key->matches(QKeySequence::SelectAll),home=key->modifiers().testFlag(Qt::ControlModifier)&&key->key()==Qt::Key_Home,end=key->modifiers().testFlag(Qt::ControlModifier)&&key->key()==Qt::Key_End;
    if(event->type()==QEvent::ShortcutOverride){if(all||home||end){event->accept();return true;}return false;}
    if(all){c.setPosition(a);c.setPosition(b,QTextCursor::KeepAnchor);window->editor()->setTextCursor(c);return true;}
    if(home||end){c.setPosition(home?a:b,key->modifiers().testFlag(Qt::ShiftModifier)?QTextCursor::KeepAnchor:QTextCursor::MoveAnchor);window->editor()->setTextCursor(c);return true;}
    if(!c.hasSelection()&&((key->key()==Qt::Key_Backspace&&c.position()<=a)||(key->key()==Qt::Key_Delete&&c.position()>=b)))return true;
    if((!c.hasSelection()||key->modifiers().testFlag(Qt::ShiftModifier))&&((key->key()==Qt::Key_Left&&c.position()<=a)||(key->key()==Qt::Key_Right&&c.position()>=b)))return true;
    if(!(key->modifiers()&(Qt::AltModifier|Qt::ControlModifier))&&(key->key()==Qt::Key_Up||key->key()==Qt::Key_Down)){
      const auto block=c.block();const auto line=block.layout()->lineForTextPosition(c.positionInBlock());
      if(line.isValid()&&((key->key()==Qt::Key_Up&&block==window->textDoc()->findBlock(a)&&line.lineNumber()==0)||(key->key()==Qt::Key_Down&&block==window->textDoc()->findBlock(b)&&line.lineNumber()==block.layout()->lineCount()-1)))return true;
    }
    if(!c.hasSelection()&&key->modifiers().testFlag(Qt::ControlModifier)&&(key->key()==Qt::Key_Backspace||key->key()==Qt::Key_Delete)){auto word=c;word.movePosition(key->key()==Qt::Key_Backspace?QTextCursor::PreviousWord:QTextCursor::NextWord,QTextCursor::KeepAnchor);if(word.selectionStart()<a||word.selectionEnd()>b){c.setPosition(key->key()==Qt::Key_Backspace?a:b,QTextCursor::KeepAnchor);c.removeSelectedText();window->editor()->setTextCursor(c);return true;}}
    return false;
  }
};
ArticleController *articleController(TeXDocumentWindow *w){return w?dynamic_cast<ArticleController*>(w->findChild<QObject*>(QStringLiteral("researchArticle"))):nullptr;}
}
ArticleDocument parseArticleDocument(const QString &root,const QHash<QString,QString> &buffers,const std::function<bool()> &cancelled){ArticleParser parser(buffers,cancelled);parser.out.root=articlePath(root);return parser.finish();}
QVariantMap articleReadingState(TeXDocumentWindow *w){if(auto *c=articleController(w))return {{QStringLiteral("partial"),c->partial},{QStringLiteral("opened"),c->opened},{QStringLiteral("root"),c->document.root}};return {};}
void restoreArticleReadingState(TeXDocumentWindow *w,const QVariantMap &state){if(!w||state.isEmpty())return;if(w->property("researchSlideMode").toBool()){w->setProperty("researchRestoreArticlePlace",QVariant());return;}if(auto *c=articleController(w)){const auto root=state.value(QStringLiteral("root")).toString();if(!root.isEmpty()&&w->textDoc()->property("researchArticleRoot").toString()!=root){w->textDoc()->setProperty("researchArticleRoot",root);if(c->document.root!=root)c->invalidate();}if(c->busy||!w->property("researchArticleReady").toBool()){w->setProperty("researchRestoreArticlePlace",state);return;}if(!c->enabled)return;if(c->partial)c->setPartial(false);c->updateSelected();if(state.value(QStringLiteral("partial")).toBool())c->setPartial(true);c->setOpen(state.value(QStringLiteral("opened")).toBool());if(state.contains(QStringLiteral("scroll")))w->editor()->verticalScrollBar()->setValue(state.value(QStringLiteral("scroll")).toInt());}}

void installArticleWriting(TeXDocumentWindow *w){if(!articleController(w))new ArticleController(w);}
void detectArticleDocument(TeXDocumentWindow *w){if(auto *c=articleController(w))c->invalidate();}
bool toggleArticleStructure(TeXDocumentWindow *w){if(auto *c=articleController(w);c&&c->enabled&&!w->property("researchSlideMode").toBool()){c->setOpen(!c->opened,true);return true;}return false;}
void setArticleFocusMode(TeXDocumentWindow *w,bool focused){if(auto *c=articleController(w))c->focus(focused);}
}
