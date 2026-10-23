#include "ResearchVersionControl.h"
#include "ResearchWorkspaceUi.h"
#include "ResearchTask.h"
#include "ResearchProject.h"
#include "ResearchWorkflow.h"
#include "ResearchMerge.h"
#include "TeXDocumentWindow.h"
#include "CompletingEdit.h"
#include "TWApp.h"
#include <QAction>
#include <QComboBox>
#include <QFileDialog>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListWidget>
#include <QMenu>
#include <QPointer>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSplitter>
#include <QTabBar>
#include <QTabWidget>
#include <QSyntaxHighlighter>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QStyle>
#include <QApplication>
#include <QCloseEvent>
#include <QTextBlock>
#include <QTextLayout>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QToolButton>
#include <QCryptographicHash>
#include <QUuid>
#include <memory>

namespace Research {
QString gitFailureMessage(const QByteArray &error) {
  auto detail=QString::fromUtf8(error).trimmed();
  // A remote may echo its URL. Never show embedded credentials in a notice.
  detail.replace(QRegularExpression(QStringLiteral("(https?://)[^\\s/@]+(?::[^\\s/@]*)?@")),QStringLiteral("\\1"));
  const auto text=detail.toLower();QString hint;
  if(text.contains(QStringLiteral("authentication failed"))||text.contains(QStringLiteral("could not read username"))||text.contains(QStringLiteral("permission denied (publickey")))
    hint=words("身份验证失败。请检查系统 Git 凭据或 SSH 密钥，并在“更多 → 项目与仓库 → 管理远程仓库”核对地址。","Authentication failed. Check your system Git credentials or SSH key and the address in More → Project and repository → Manage remotes.");
  else if(text.contains(QStringLiteral("could not resolve host"))||text.contains(QStringLiteral("failed to connect"))||text.contains(QStringLiteral("timed out")))
    hint=words("连接未完成。请检查网络与远程地址，再从“同步”重试。","Connection failed. Check the network and remote address, then retry from Sync.");
  else if(text.contains(QStringLiteral("non-fast-forward"))||text.contains(QStringLiteral("fetch first")))
    hint=words("远程已有新版本。请先获取并合并远程修改，解决冲突后再推送。","The remote has new commits. Fetch and integrate them, resolve conflicts, then push again.");
  else if(text.contains(QStringLiteral("repository not found"))||text.contains(QStringLiteral("does not appear to be a git repository")))
    hint=words("无法访问远程项目。请在“更多 → 项目与仓库 → 管理远程仓库”检查地址与账户权限。","Cannot access the remote repository. Check the address and account permissions in More → Project and repository → Manage remotes.");
  else if(text.contains(QStringLiteral("ssl certificate")))
    hint=words("证书验证失败。请检查系统时间和 Git 的受信任证书设置。","Certificate verification failed. Check the system time and Git's trusted certificates.");
  if(detail.isEmpty())detail=words("操作未完成，请重试。","The operation did not complete. Try again.");
  return hint.isEmpty()?detail.left(1200):hint+QStringLiteral("\n")+detail.left(600);
}
QVector<GitFile> parseGitStatus(const QByteArray &bytes,QString *error){
  QVector<GitFile> out;error->clear();int p=0;
  while(p<bytes.size()){int end=bytes.indexOf('\0',p);if(end<0||end-p<4||bytes[p+2]!=' '){*error=QStringLiteral("Incomplete Git status.");return {};}
    GitFile f;f.index=bytes[p];f.worktree=bytes[p+1];f.path=QString::fromUtf8(bytes.mid(p+3,end-p-3));p=end+1;
    QByteArray state;state.append(f.index);state.append(f.worktree);f.conflict=QList<QByteArray>{"DD","AU","UD","UA","DU","AA","UU"}.contains(state);
    if(f.index=='R'||f.index=='C'||f.worktree=='R'||f.worktree=='C'){end=bytes.indexOf('\0',p);if(end<0){*error=QStringLiteral("Incomplete rename status.");return {};}f.previous=QString::fromUtf8(bytes.mid(p,end-p));p=end+1;}
    if(f.path.isEmpty()||QDir::isAbsolutePath(f.path)||f.path==QStringLiteral("..")||f.path.startsWith(QStringLiteral("../"))){*error=QStringLiteral("Invalid repository path.");return {};}out.append(f);
  }return out;
}
QString gitPatchAtLine(const QString &diff,int line){
  const auto rows=diff.split(QLatin1Char('\n'));if(line<0||line>=rows.size()||diff.contains(QStringLiteral("GIT binary patch"))||diff.contains(QStringLiteral("rename from ")))return {};
  int file=-1,hunk=-1,end=rows.size();for(int i=0;i<=line;++i){if(rows[i].startsWith(QStringLiteral("diff --git "))){file=i;hunk=-1;}if(rows[i].startsWith(QStringLiteral("@@ ")))hunk=i;}
  if(file<0||hunk<0)return {};int firstHunk=file;while(firstHunk<rows.size()&&!rows[firstHunk].startsWith(QStringLiteral("@@ ")))++firstHunk;
  for(int i=hunk+1;i<rows.size();++i)if(rows[i].startsWith(QStringLiteral("@@ "))||rows[i].startsWith(QStringLiteral("diff --git "))){end=i;break;}
  QStringList result;for(int i=file;i<firstHunk;++i)result.append(rows[i]);for(int i=hunk;i<end;++i)result.append(rows[i]);while(!result.isEmpty()&&result.last().isEmpty())result.removeLast();return result.join(QLatin1Char('\n'))+QLatin1Char('\n');
}
QString gitPatchForSelectedLines(const QString &diff,const QSet<int> &selected,bool reverse) {
  if(selected.isEmpty()||diff.contains(QStringLiteral("GIT binary patch"))||diff.contains(QStringLiteral("rename from "))||diff.contains(QStringLiteral("copy from ")))return {};
  const auto rows=diff.split(QLatin1Char('\n'));
  const QRegularExpression header(QStringLiteral("^@@ -(\\d+)(?:,(\\d+))? \\+(\\d+)(?:,(\\d+))? @@"));
  QString fileHeader,oldPath,newPath,mode;QStringList hunks;int delta=0,totalNew=0,fileCount=0;
  for(int i=0;i<rows.size();++i) {
    const auto &line=rows[i];
    if(line.startsWith(QStringLiteral("diff --git "))){fileHeader=line;if(++fileCount>1)return {};continue;}
    if(line.startsWith(QStringLiteral("--- "))){oldPath=line.mid(4);continue;}
    if(line.startsWith(QStringLiteral("+++ "))){newPath=line.mid(4);continue;}
    if(line.startsWith(QStringLiteral("new file mode "))||line.startsWith(QStringLiteral("deleted file mode "))){mode=line;continue;}
    const auto match=header.match(line);if(!match.hasMatch())continue;
    const int start=match.captured(reverse?3:1).toInt();
    QStringList body;int oldCount=0,newCount=0;bool changed=false,lastIncluded=false;
    for(++i;i<rows.size()&&!rows[i].startsWith(QStringLiteral("@@ "))&&!rows[i].startsWith(QStringLiteral("diff --git "));++i) {
      const auto &row=rows[i];if(row.isEmpty())continue;
      if(row.startsWith(QLatin1Char('\\'))){if(lastIncluded)body.append(row);continue;}
      auto sign=row[0];if(reverse){if(sign==QLatin1Char('+'))sign=QLatin1Char('-');else if(sign==QLatin1Char('-'))sign=QLatin1Char('+');}
      lastIncluded=true;
      if(sign==QLatin1Char('+')) {
        if(!selected.contains(i)){lastIncluded=false;continue;}
        body.append(QLatin1Char('+')+row.mid(1));++newCount;changed=true;
      } else if(sign==QLatin1Char('-')) {
        ++oldCount;
        if(selected.contains(i)){body.append(QLatin1Char('-')+row.mid(1));changed=true;}
        else {body.append(QLatin1Char(' ')+row.mid(1));++newCount;}
      } else if(sign==QLatin1Char(' ')){body.append(row);++oldCount;++newCount;}
      else return {};
    }
    --i;
    if(!changed)continue;
    const int nextStart=newCount==0?qMax(0,start+delta-1):(oldCount==0?start+delta+1:start+delta);
    hunks.append(QStringLiteral("@@ -%1,%2 +%3,%4 @@").arg(start).arg(oldCount).arg(nextStart).arg(newCount));
    hunks.append(body);delta+=newCount-oldCount;totalNew+=newCount;
  }
  if(hunks.isEmpty()||oldPath.isEmpty()||newPath.isEmpty())return {};
  if(reverse){std::swap(oldPath,newPath);if(mode.startsWith(QStringLiteral("new file mode ")))mode.replace(0,QStringLiteral("new file mode ").size(),QStringLiteral("deleted file mode "));else if(mode.startsWith(QStringLiteral("deleted file mode ")))mode.replace(0,QStringLiteral("deleted file mode ").size(),QStringLiteral("new file mode "));}
  // A partial deletion leaves a real file, including when unstaging only some
  // lines of a newly added file. Do not accidentally emit a whole-file deletion.
  if(newPath==QStringLiteral("/dev/null")&&totalNew>0){newPath=oldPath;mode.clear();}
  QStringList result{fileHeader};if(!mode.isEmpty())result.append(mode);
  result<<QStringLiteral("--- ")+oldPath<<QStringLiteral("+++ ")+newPath;result.append(hunks);
  return result.join(QLatin1Char('\n'))+QLatin1Char('\n');
}
namespace {
class VersionDiffHighlighter final:public QSyntaxHighlighter {
  void highlightBlock(const QString &line) override {QTextCharFormat f;if(line.startsWith(QLatin1Char('+'))&&!line.startsWith(QStringLiteral("+++"))){f.setBackground(QColor(QStringLiteral("#eef5ef")));f.setForeground(QColor(QStringLiteral("#315c3c")));}else if(line.startsWith(QLatin1Char('-'))&&!line.startsWith(QStringLiteral("---"))){f.setBackground(QColor(QStringLiteral("#faf0ef")));f.setForeground(QColor(QStringLiteral("#86443d")));}else if(line.startsWith(QStringLiteral("@@"))||line.startsWith(QStringLiteral("第 "))||line.startsWith(QStringLiteral("From line "))||line.startsWith(QStringLiteral("diff --git"))||line.startsWith(QStringLiteral("index "))){f.setForeground(QColor(QStringLiteral("#78818a")));}else return;setFormat(0,int(line.size()),f);}
public:explicit VersionDiffHighlighter(QTextDocument *doc):QSyntaxHighlighter(doc){}
};
class VersionFileDelegate final : public QStyledItemDelegate {
public:
  explicit VersionFileDelegate(QObject *parent):QStyledItemDelegate(parent){}
  QSize sizeHint(const QStyleOptionViewItem &option,const QModelIndex &index) const override {
    if(index.data(Qt::UserRole+7).isValid())return QSize(0,index.data(Qt::UserRole).isValid()?QFontMetrics(option.font).height()*2+14:12);
    if(!index.data(Qt::UserRole+3).isValid())return QStyledItemDelegate::sizeHint(option,index);
    return QSize(0,QFontMetrics(option.font).height()*2+14);
  }
  void paint(QPainter *p,const QStyleOptionViewItem &option,const QModelIndex &index) const override {
    if(index.data(Qt::UserRole+7).isValid()){
      QStyleOptionViewItem base(option);initStyleOption(&base,index);base.text.clear();
      const auto *style=base.widget?base.widget->style():QApplication::style();style->drawControl(QStyle::CE_ItemViewItem,&base,p,base.widget);
      const auto graph=index.data(Qt::UserRole+7).toString();const int width=qMin(84,qMax(16,int(graph.size())*7));
      p->save();p->setClipRect(option.rect);p->setRenderHint(QPainter::Antialiasing);p->setPen(QPen(QColor(QStringLiteral("#a6b2bb")),1.2));
      const auto r=option.rect;const int middle=r.center().y();
      for(int i=0;i<graph.size()&&i*7<width;++i){const int x=r.left()+7+i*7;const auto c=graph[i];
        if(c==QLatin1Char('|'))p->drawLine(x,r.top(),x,r.bottom()+1);
        else if(c==QLatin1Char('/'))p->drawLine(x+7,r.top(),x-7,r.bottom()+1);
        else if(c==QLatin1Char('\\'))p->drawLine(x-7,r.top(),x+7,r.bottom()+1);
        else if(c==QLatin1Char('*')){p->drawLine(x,r.top(),x,r.bottom()+1);p->setBrush(QColor(QStringLiteral("#627b8c")));p->drawEllipse(QPointF(x,middle),2.8,2.8);p->setBrush(Qt::NoBrush);}
        else if(c==QLatin1Char('-'))p->drawLine(x-4,middle,x+4,middle);
      }
      if(index.data(Qt::UserRole).isValid()){
        auto text=r.adjusted(width+10,5,-8,-5);p->setFont(option.font);p->setPen(QColor(QStringLiteral("#30373d")));const QFontMetrics fm(option.font);
        p->drawText(text.adjusted(0,0,0,-fm.height()),Qt::AlignLeft|Qt::AlignVCenter,fm.elidedText(index.data(Qt::UserRole+8).toString(),Qt::ElideRight,text.width()));
        p->setPen(QColor(QStringLiteral("#737d85")));p->drawText(text.adjusted(0,fm.height(),0,0),Qt::AlignLeft|Qt::AlignVCenter,fm.elidedText(index.data(Qt::UserRole+9).toString(),Qt::ElideRight,text.width()));
      }
      p->restore();return;
    }
    const auto path=index.data(Qt::UserRole+3).toString();
    if(path.isEmpty()){QStyledItemDelegate::paint(p,option,index);return;}
    QStyleOptionViewItem base(option);initStyleOption(&base,index);base.text.clear();
    const auto *style=base.widget?base.widget->style():QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem,&base,p,base.widget);
    auto rect=style->subElementRect(QStyle::SE_ItemViewItemText,&base,base.widget).adjusted(3,4,-6,-4);
    p->save();p->setFont(option.font);p->setPen(QColor(QStringLiteral("#30373d")));
    const QFontMetrics fm(option.font);p->drawText(rect.adjusted(0,0,0,-fm.height()),Qt::AlignLeft|Qt::AlignVCenter,fm.elidedText(QFileInfo(path).fileName(),Qt::ElideMiddle,rect.width()));
    auto secondary=option.font;secondary.setPixelSize(qMax(11,option.font.pixelSize()-1));p->setFont(secondary);p->setPen(QColor(QStringLiteral("#737d85")));
    const auto folder=QFileInfo(path).path();const auto detail=index.data(Qt::UserRole+2).toString()+(folder==QStringLiteral(".")?QString():QStringLiteral(" · ")+folder);
    p->drawText(rect.adjusted(0,fm.height(),0,0),Qt::AlignLeft|Qt::AlignVCenter,QFontMetrics(secondary).elidedText(detail,Qt::ElideMiddle,rect.width()));p->restore();
  }
};
QByteArray readGitFile(const QString &p){QFile f(p);return f.open(QIODevice::ReadOnly)&&f.size()<=8*1024*1024?f.readAll():QByteArray();}
bool hasDirty(const QString &root){for(auto *w:TeXDocumentWindow::documentList())if(!w->untitled()&&w->isModified()){const auto rel=QDir(root).relativeFilePath(w->fileName());if(!rel.startsWith(QStringLiteral("../"))&&!QDir::isAbsolutePath(rel))return true;}return false;}
class VersionDialog final:public MaterialDialog {
  QPointer<TeXDocumentWindow> owner;QString root,branch;QTabBar *tabs;QListWidget *files;QPlainTextEdit *preview;QLabel *status,*fileTitle,*diffSummary;QVector<int> diffLines;QLineEdit *message;QPushButton *commit;QToolButton *more,*sync;QPointer<ExternalTask> task;QVector<GitFile> state;quint64 generation{0};bool busy{false},repository{false},mutating{false},closeRequested{false},loading{false};QString operationError;QString shownDiff,shownPath;bool shownStaged{false};
  bool away{false},refreshPending{false},refreshScheduled{false},refreshing{false},replaceList{false};
  int completionDepth{0},listedTab{-1};
  struct ViewState {
    int tab{-1},position{0},anchor{0},vertical{0},horizontal{0},listScroll{0},topColumn{0},topOffset{0},column{0};
    QString key,text,line,topText;
    bool staged{false},valid{false},selected{false};
  } savedView;
  bool event(QEvent *event) override {
    if(event->type()==QEvent::WindowDeactivate)away=true;
    else if(event->type()==QEvent::WindowActivate&&away){away=false;requestRefresh();}
    return MaterialDialog::event(event);
  }
  void requestRefresh(){
    if(closeRequested||!isVisible())return;
    refreshPending=true;scheduleRefresh();
  }
  void scheduleRefresh(){
    if(!refreshPending||refreshScheduled||busy||completionDepth||closeRequested)return;
    refreshScheduled=true;
    QTimer::singleShot(0,this,[this]{refreshScheduled=false;if(refreshPending&&!busy&&!completionDepth&&isVisible()&&!closeRequested)refresh();});
  }
  QString itemKey(QListWidgetItem *item)const{return item?(tabs->currentIndex()==0?item->data(Qt::UserRole+3).toString():item->data(Qt::UserRole).toString()):QString();}
  void rememberView(){
    savedView={};savedView.tab=tabs->currentIndex();
    if(listedTab!=savedView.tab)return;
    auto *item=files->currentItem();if(!item)return;
    savedView.key=itemKey(item);savedView.valid=!savedView.key.isEmpty();
    // Checkboxes are the actual Git index. Preserve the selected side, never
    // stage an old UI snapshot over changes made by another Git client.
    savedView.staged=item->checkState()==Qt::Checked;
    savedView.listScroll=files->verticalScrollBar()->value();
  }
  void rememberDiffView(){
    // Read at publication time so scrolling/selection performed while Git
    // was running takes precedence over the state at the start of refresh.
    savedView.text=preview->toPlainText();const auto cursor=preview->textCursor();
    savedView.position=cursor.position();savedView.anchor=cursor.anchor();
    savedView.line=cursor.block().text();savedView.column=cursor.positionInBlock();
    savedView.vertical=preview->verticalScrollBar()->value();savedView.horizontal=preview->horizontalScrollBar()->value();
    const auto top=preview->cursorForPosition(QPoint(0,0));savedView.topColumn=top.positionInBlock();savedView.topText=top.block().text();
    savedView.topOffset=savedView.vertical-visualLine(top);
  }
  int visualLine(const QTextCursor &cursor)const{
    preview->cursorRect(cursor); // Ensure this block's wrap layout is current.
    const auto block=cursor.block();const auto *layout=block.layout();
    const auto line=layout?layout->lineForTextPosition(cursor.positionInBlock()):QTextLine();
    return block.firstLineNumber()+(line.isValid()?line.lineNumber():0);
  }
  void restoreListSelection(){
    if(!savedView.valid||savedView.selected||savedView.tab!=tabs->currentIndex())return;
    savedView.selected=true;QListWidgetItem *match=nullptr;
    for(int n=0;n<files->count();++n){auto *item=files->item(n);if(itemKey(item)!=savedView.key)continue;match=item;
      if(savedView.tab!=0||item->data(Qt::UserRole+1).toBool()==savedView.staged)break;
    }
    if(match){QSignalBlocker guard(files);files->setCurrentItem(match);}
    else savedView.valid=false;
  }
  void finishRefresh(){
    if(!refreshing||busy)return;
    restoreListSelection();
    if(savedView.valid)files->verticalScrollBar()->setValue(savedView.listScroll);
    if(files->count()==0){shownDiff.clear();shownPath.clear();diffLines.clear();preview->clear();preview->setExtraSelections({});setFileTitle(words("修改预览","Change preview"));diffSummary->clear();}
    refreshing=false;replaceList=false;savedView={};
  }
  void restoreDiffView(){
    if(!savedView.valid||!savedView.selected||savedView.tab!=tabs->currentIndex()||itemKey(files->currentItem())!=savedView.key)return;
    auto cursor=preview->textCursor();
    if(preview->toPlainText()==savedView.text){
      cursor.setPosition(savedView.anchor);cursor.setPosition(savedView.position,QTextCursor::KeepAnchor);
      preview->setTextCursor(cursor);preview->verticalScrollBar()->setValue(savedView.vertical);
    }else{
      // Relocate only unambiguous content; a changed diff must not inherit a
      // stale selected range that could stage different lines.
      auto uniqueBlock=[this](const QString &text){QTextBlock found;if(text.isEmpty())return found;for(auto block=preview->document()->firstBlock();block.isValid();block=block.next())if(block.text()==text){if(found.isValid())return QTextBlock();found=block;}return found;};
      const auto block=uniqueBlock(savedView.line);
      if(block.isValid()){cursor=QTextCursor(block);cursor.setPosition(block.position()+qMin(savedView.column,int(block.text().size())));preview->setTextCursor(cursor);}
      const auto top=uniqueBlock(savedView.topText);
      auto topCursor=QTextCursor(top);if(top.isValid())topCursor.setPosition(top.position()+qMin(savedView.topColumn,int(top.text().size())));
      preview->verticalScrollBar()->setValue(top.isValid()?visualLine(topCursor)+savedView.topOffset:savedView.vertical);
    }
    preview->horizontalScrollBar()->setValue(savedView.horizontal);
  }
#include "ResearchVersionActions.inc"
  void setFileTitle(const QString &text){fileTitle->setProperty("fullTitle",text);fileTitle->setText(fileTitle->fontMetrics().elidedText(text,Qt::ElideMiddle,qMax(0,fileTitle->contentsRect().width())));}
  bool eventFilter(QObject *object,QEvent *event) override {
    if(object==fileTitle&&(event->type()==QEvent::Resize||event->type()==QEvent::FontChange)&&fileTitle->property("fullTitle").isValid())setFileTitle(fileTitle->property("fullTitle").toString());
    return MaterialDialog::eventFilter(object,event);
  }
  void run(QStringList args,std::function<void(TaskResult)> done,bool mutation=false,const QByteArray &input={}){
    if(busy){status->setText(words("请等待当前操作完成。","Wait for the current operation."));return;}
    const bool cloning=args.value(0)==QStringLiteral("clone");
    if(root.isEmpty()&&!cloning){status->setText(words("请先打开或克隆项目。","Open or clone a project first."));return;}
    const bool metadataOnly=QStringList{QStringLiteral("fetch"),QStringLiteral("push"),QStringLiteral("remote"),QStringLiteral("config"),QStringLiteral("branch"),QStringLiteral("update-ref")}.contains(args.value(0));
    if(mutation&&!cloning&&!metadataOnly&&hasDirty(root)){status->setText(words("项目中有未保存内容。请先保存，再进行版本操作。","Save the project's open changes before a version operation."));return;}
    const bool replace=refreshing&&replaceList&&((tabs->currentIndex()==0&&args.value(0)==QStringLiteral("status"))||(tabs->currentIndex()==1&&args.value(0)==QStringLiteral("log"))||(tabs->currentIndex()==2&&args.value(0)==QStringLiteral("for-each-ref")));
    if(replace)replaceList=false;
    busy=true;mutating=mutation;commit->setEnabled(false);tabs->setEnabled(false);files->setEnabled(false);more->setEnabled(false);sync->setEnabled(false);const auto ticket=++generation;
    auto env=QProcessEnvironment::systemEnvironment();for(const auto &key:{QStringLiteral("GIT_DIR"),QStringLiteral("GIT_WORK_TREE"),QStringLiteral("GIT_INDEX_FILE"),QStringLiteral("GIT_OBJECT_DIRECTORY"),QStringLiteral("GIT_ALTERNATE_OBJECT_DIRECTORIES")})env.remove(key);env.insert(QStringLiteral("GIT_TERMINAL_PROMPT"),QStringLiteral("0"));if(!env.contains(QStringLiteral("GCM_INTERACTIVE")))env.insert(QStringLiteral("GCM_INTERACTIVE"),QStringLiteral("auto"));env.insert(QStringLiteral("GIT_EDITOR"),QStringLiteral("true"));env.insert(QStringLiteral("GIT_SEQUENCE_EDITOR"),QStringLiteral("true"));
    args.prepend(QStringLiteral("core.quotepath=false"));args.prepend(QStringLiteral("-c"));args.prepend(QStringLiteral("--no-pager"));
    task=new ExternalTask(this,systemTool(QStringLiteral("git")),args,root,[this,ticket,replace,done=std::move(done)](TaskResult r){if(ticket!=generation)return;busy=false;mutating=false;task=nullptr;tabs->setEnabled(true);files->setEnabled(true);more->setEnabled(true);sync->setEnabled(true);if(closeRequested){MaterialDialog::reject();return;}commit->setEnabled(repository&&tabs->currentIndex()==0);if(!r.ok)status->setText(gitFailureMessage(r.error));
      if(replace&&(r.ok||listedTab!=tabs->currentIndex())){QSignalBlocker guard(files);files->clear();listedTab=tabs->currentIndex();}
      ++completionDepth;done(r);--completionDepth;
      if(!completionDepth){finishRefresh();scheduleRefresh();}
    },120000,input,env);
  }
  void change(const QStringList &args,const QByteArray &input={}){run(args,[this,args](TaskResult r){if(r.ok){operationError.clear();if(!args.isEmpty()&&args.first()==QStringLiteral("commit"))message->clear();refresh();}else{operationError=gitFailureMessage(r.error);refresh();}},true,input);}
  void refresh(){
    if(busy){refreshPending=true;return;}refreshPending=false;
    if(root.isEmpty()){files->setEnabled(true);status->setText(words("从“更多”克隆项目，或保存文稿后启用版本管理。","Clone a project from More, or save a document to enable version control."));return;}
    if(!refreshing){rememberView();refreshing=true;setProperty("researchGitRefreshCount",property("researchGitRefreshCount").toInt()+1);}replaceList=true;const int tab=tabs->currentIndex();
    if(!repository){run({QStringLiteral("rev-parse"),QStringLiteral("--show-toplevel")},[this](TaskResult r){if(r.ok){root=QString::fromUtf8(r.output).trimmed();repository=true;refresh();}else status->setText(words("此文件夹尚未启用版本管理。在“更多”中初始化或克隆项目。","This folder is not a repository. Use More to initialize or clone."));});return;}
    if(tab==0){run({QStringLiteral("status"),QStringLiteral("--porcelain=v1"),QStringLiteral("-z"),QStringLiteral("--untracked-files=all")},[this](TaskResult r){if(!r.ok)return;QString error;state=parseGitStatus(r.output,&error);if(!error.isEmpty()){status->setText(error);return;}loading=true;for(int n=0;n<state.size();++n){const auto &f=state[n];auto add=[&](bool staged){auto *item=new QListWidgetItem((f.conflict?words("冲突 · ","Conflict · "):staged?words("待保存版本 · ","Staged · "):words("工作区 · ","Working tree · "))+f.path,files);item->setData(Qt::UserRole,n);item->setData(Qt::UserRole+1,staged);item->setData(Qt::UserRole+2,f.conflict?words("待解决冲突","Conflict"):staged?words("加入本次版本","Staged"):f.index=='?'?words("新文件","New file"):words("工作区修改","Modified"));item->setData(Qt::UserRole+3,f.path);if(!f.conflict){item->setFlags(item->flags()|Qt::ItemIsUserCheckable);item->setCheckState(staged?Qt::Checked:Qt::Unchecked);}item->setToolTip(staged?words("取消勾选可移出本次版本。","Uncheck to leave this out of the commit."):words("勾选可加入本次版本；右侧预览差异。","Check to stage this file; preview its diff on the right."));};if(f.index!=' '&&f.index!='?')add(true);if(!f.conflict&&(f.worktree!=' '||f.index=='?'))add(false);}loading=false;status->setText(operationError.isEmpty()?(state.isEmpty()?words("所有修改已记录","All changes recorded"):words("%1 个文件有修改","Changed files: %1").arg(state.size())):operationError);run({QStringLiteral("branch"),QStringLiteral("--show-current")},[this](TaskResult r){if(r.ok){branch=QString::fromUtf8(r.output).trimmed();setWindowTitle(words("版本与协作 · ","Versions and collaboration · ")+(branch.isEmpty()?words("分离的版本","Detached HEAD"):branch));showTrackingStatus();}});});}
    else if(tab==1)refreshHistory();
    else refreshBranches();
  }
  void showSelection(){if(busy)return;if(refreshing)restoreListSelection();auto *item=files->currentItem();if(!item)return;shownDiff.clear();shownPath.clear();diffLines.clear();setFileTitle(words("修改预览","Change preview"));diffSummary->clear();diffSummary->setMinimumWidth(0);preview->setExtraSelections({});if(tabs->currentIndex()==0){const auto f=state.value(item->data(Qt::UserRole).toInt());shownPath=f.path;setFileTitle(QFileInfo(f.path).fileName());fileTitle->setToolTip(f.path);shownStaged=item->data(Qt::UserRole+1).toBool();if(f.index=='?'){
      run({QStringLiteral("diff"),QStringLiteral("--no-index"),QStringLiteral("--no-ext-diff"),QStringLiteral("--no-textconv"),QStringLiteral("--"),QStringLiteral("/dev/null"),f.path},[this](TaskResult result){
        if(result.ok||(result.code==1&&result.error.isEmpty())){displayDiff(QString::fromUtf8(result.output));if(operationError.isEmpty())status->setText(words("新文件 · 可选取文字行加入版本","New file · Select lines to stage"));}
      });return;
    }QStringList args{QStringLiteral("diff"),QStringLiteral("--no-ext-diff"),QStringLiteral("--no-textconv")};if(shownStaged)args<<QStringLiteral("--cached");args<<QStringLiteral("--")<<f.path;if(!f.previous.isEmpty())args<<f.previous;run(args,[this](TaskResult r){if(r.ok){displayDiff(QString::fromUtf8(r.output));}});}else if(tabs->currentIndex()==1){run({QStringLiteral("show"),QStringLiteral("--no-ext-diff"),QStringLiteral("--no-textconv"),item->data(Qt::UserRole).toString(),QStringLiteral("--")},[this](TaskResult r){if(r.ok)displayDiff(QString::fromUtf8(r.output));});}}
  void displayDiff(const QString &diff){
    if(savedView.valid&&savedView.selected&&savedView.tab==tabs->currentIndex()&&itemKey(files->currentItem())==savedView.key)rememberDiffView();
    shownDiff=diff;diffLines.clear();QStringList display;const auto lines=diff.split(QLatin1Char('\n'));int added=0,removed=0;
    for(int n=0;n<lines.size();++n){const auto &line=lines[n];
      if(line.startsWith(QStringLiteral("+++ "))){
        auto path=line.mid(4);if(path==QStringLiteral("/dev/null"))path=lines.value(n-1).mid(4);
        if(path.startsWith(QStringLiteral("a/"))||path.startsWith(QStringLiteral("b/")))path=path.mid(2);
        if(path!=fileTitle->property("fullTitle").toString()){display.append(path);diffLines.append(-1);}continue;
      }
      if(line.startsWith(QStringLiteral("diff --git "))||line.startsWith(QStringLiteral("index "))||line.startsWith(QStringLiteral("--- ")))continue;
      if(line.startsWith(QLatin1Char('+')))++added;else if(line.startsWith(QLatin1Char('-')))++removed;
      diffLines.append(n);const auto hunk=QRegularExpression(QStringLiteral("^@@ -[^ ]+ [+](\\d+)(?:,\\d+)? @@(.*)$")).match(line);
      display.append(hunk.hasMatch()?words("第 %1 行起","From line %1").arg(hunk.captured(1)):line);
    }
    preview->setPlainText(display.join(QLatin1Char('\n')));
    QList<QTextEdit::ExtraSelection> highlights;auto block=preview->document()->firstBlock();
    for(const auto &line:display){if(highlights.size()<4000&&(line.startsWith(QLatin1Char('+'))||line.startsWith(QLatin1Char('-')))){QTextEdit::ExtraSelection selection;selection.cursor=QTextCursor(block);selection.format.setProperty(QTextFormat::FullWidthSelection,true);selection.format.setBackground(QColor(line.startsWith(QLatin1Char('+'))?QStringLiteral("#eef5ef"):QStringLiteral("#faf0ef")));highlights.append(selection);}block=block.next();}preview->setExtraSelections(highlights);

    const auto summary=words("新增 %1 · 删除 %2","%1 added · %2 removed").arg(added).arg(removed);diffSummary->setText(summary);diffSummary->setMinimumWidth(diffSummary->fontMetrics().horizontalAdvance(summary)+2);
    // Finish positioning the expanded label before the next paint; otherwise
    // one frame can expose only its first glyph at the old right-hand edge.
    if(auto *layout=diffSummary->parentWidget()->layout())layout->activate();
    restoreDiffView();
  }
  void setStaged(const GitFile &file,bool staged){QStringList paths{file.path};if(!file.previous.isEmpty())paths<<file.previous;if(staged)change(QStringList{QStringLiteral("add"),QStringLiteral("--")}+paths);else run({QStringLiteral("rev-parse"),QStringLiteral("--verify"),QStringLiteral("HEAD")},[this,paths](TaskResult r){if(r.ok)change(QStringList{QStringLiteral("reset"),QStringLiteral("-q"),QStringLiteral("HEAD"),QStringLiteral("--")}+paths);else change(QStringList{QStringLiteral("rm"),QStringLiteral("--cached"),QStringLiteral("--")}+paths);});}
  void context(const QPoint &point){auto *item=files->itemAt(point);if(!item||busy||!item->data(Qt::UserRole).isValid())return;files->setCurrentItem(item);QMenu menu(this);
    if(tabs->currentIndex()==0){const auto f=state.value(item->data(Qt::UserRole).toInt());const bool staged=item->data(Qt::UserRole+1).toBool();auto *stage=menu.addAction(staged?words("暂不加入","Unstage"):words("加入本次版本","Stage file"));stage->setEnabled(!f.conflict);auto *open=menu.addAction(words("打开文件","Open file"));QAction *resolve=f.conflict?menu.addAction(words("解决冲突…","Resolve conflict…")):nullptr;QAction *restore=!staged&&f.index!='?'?menu.addAction(words("恢复工作区文件…","Restore working file…")):nullptr;
      const auto choice=menu.exec(files->viewport()->mapToGlobal(point));if(choice==stage){setStaged(f,!staged);}else if(choice==open){rememberReadingPlace(owner);const auto main=completionProjectRoot(owner);const auto relative=QDir(root).relativeFilePath(main);if(!QDir::isAbsolutePath(relative)&&!relative.startsWith(QStringLiteral("../")))rememberRoot(QDir(root).filePath(f.path),main);TeXDocumentWindow::openDocument(QDir(root).filePath(f.path),true,true);}else if(resolve&&choice==resolve)resolveConflict(f.path);else if(restore&&choice==restore)restoreFile(f.path);
    }else if(tabs->currentIndex()==1){
      const auto sha=item->data(Qt::UserRole).toString();
      auto *compare=menu.addAction(words("与其他版本比较…","Compare with…"));
      auto *pick=menu.addAction(words("应用这个版本的修改…","Cherry-pick commit…"));
      auto *revert=menu.addAction(words("撤回这个版本…","Revert this commit…"));
      const auto chosen=menu.exec(files->viewport()->mapToGlobal(point));
      if(chosen==compare)compareReference(sha);
      else if(chosen==pick)revisionOperation(QStringLiteral("cherry-pick"),sha);
      else if(chosen==revert&&workspaceConfirm(this,words("撤回版本","Revert commit"),item->text(),words("生成撤回版本","Create revert commit")))change({QStringLiteral("revert"),QStringLiteral("--no-edit"),sha});
    }
    else branchContext(item,point);
  }
  void restoreFile(const QString &path){const auto full=QDir(root).filePath(path);const auto original=readGitFile(full);if(QFileInfo(full).isSymLink()||QFileInfo(full).size()>8*1024*1024){status->setText(words("请在文件窗口处理此文件。","Handle this file in the project window."));return;}run({QStringLiteral("show"),QStringLiteral(":")+path},[this,path,full,original](TaskResult r){if(!r.ok)return;if(!workspaceConfirm(this,words("恢复文件","Restore file"),QString::fromUtf8(r.output),words("恢复并保留备份","Restore and keep backup")))return;if(hasDirty(root)||readGitFile(full)!=original){status->setText(words("文件已变化，请重新核对。","The file changed. Review it again."));return;}
      run({QStringLiteral("rev-parse"),QStringLiteral("--absolute-git-dir")},[this,full,original,data=r.output](TaskResult repo){if(!repo.ok)return;const auto backup=QDir(QString::fromUtf8(repo.output).trimmed()).filePath(QStringLiteral("texline-recovery/"));if(!QDir().mkpath(backup)){status->setText(words("无法保存恢复备份。","Cannot save recovery backup."));return;}const auto name=backup+QUuid::createUuid().toString(QUuid::WithoutBraces);QSaveFile saved(name);if(!saved.open(QIODevice::WriteOnly)||saved.write(original)!=original.size()||!saved.commit())return;if(readGitFile(full)!=original)return;QSaveFile output(full);if(output.open(QIODevice::WriteOnly)&&output.write(data)==data.size()&&output.commit()){refresh();status->setText(words("已恢复。原文件备份：","Restored. Original backup: ")+name);}});
    });}
  void conflictDraft(const QString &path,const QByteArray &original,const QVector<QByteArray> &versions){
    const auto full=QDir(root).filePath(path);if(hasDirty(root)){status->setText(words("请先保存项目中的修改。","Save the project's changes first."));return;}
    for(const auto &bytes:versions)if(bytes.contains('\0')||QString::fromUtf8(bytes).toUtf8()!=bytes){status->setText(words("此冲突需要在对应文件编辑器中处理。","Resolve this conflict in the file's own editor."));return;}
    if(original.contains('\0')||QString::fromUtf8(original).toUtf8()!=original){status->setText(words("此文件不是 UTF-8 文本，请在对应编辑器中处理。","This file is not UTF-8 text. Use its own editor."));return;}
    MaterialDialog draft(this);configureSettingsSurface(&draft);draft.setObjectName(QStringLiteral("researchGitConflict"));draft.setWindowTitle(words("解决冲突 · ","Resolve conflict · ")+path);auto *l=new QVBoxLayout(&draft);
    auto *split=new QSplitter(Qt::Vertical,&draft);auto *reference=new QTabWidget(split);const QStringList labels{words("共同版本","Base"),words("当前分支","Current"),words("传入版本","Incoming")};
    for(int n=0;n<3;++n){auto *view=new QPlainTextEdit(reference);configureWorkspaceText(view);view->setReadOnly(true);view->setPlainText(QString::fromUtf8(versions.value(n)));reference->addTab(view,labels[n]);}
    auto *edit=new QPlainTextEdit(split);configureWorkspaceText(edit);edit->setObjectName(QStringLiteral("researchGitMergedText"));edit->setPlainText(QString::fromUtf8(original));split->addWidget(reference);split->addWidget(edit);split->setSizes({190,310});l->addWidget(split,1);
    auto *row=new QHBoxLayout;auto *hint=new QLabel(words("合并结果","Merged result"),&draft);hint->setWordWrap(true);row->addWidget(hint,1);auto *choices=new QToolButton(&draft);configureButton(choices,ButtonRole::Text);choices->setText(words("保留内容…","Keep content…"));choices->setPopupMode(QToolButton::InstantPopup);auto *menu=new QMenu(choices);choices->setMenu(menu);row->addWidget(choices);l->addLayout(row);
    const QStringList actions{words("此处保留当前内容","Keep current here"),words("此处保留传入内容","Keep incoming here"),words("此处保留双方内容","Keep both here")};
    for(int side=0;side<3;++side)connect(menu->addAction(actions[side]),&QAction::triggered,&draft,[&,side]{const auto text=edit->toPlainText();QRegularExpression re(QStringLiteral(R"((?m)^<<<<<<< [^\n]*\n([\s\S]*?)(?:^\|\|\|\|\|\|\| [^\n]*\n[\s\S]*?)?^=======\n([\s\S]*?)^>>>>>>> [^\n]*(?:\n|$))"));auto matches=re.globalMatch(text);while(matches.hasNext()){auto m=matches.next();if(edit->textCursor().position()>m.capturedEnd())continue;auto cursor=edit->textCursor();cursor.setPosition(int(m.capturedStart()));cursor.setPosition(int(m.capturedEnd()),QTextCursor::KeepAnchor);cursor.beginEditBlock();cursor.insertText(side==0?m.captured(1):side==1?m.captured(2):m.captured(1)+m.captured(2));cursor.endEditBlock();edit->setTextCursor(cursor);edit->setFocus();break;}});
    auto *b=workspaceFooter(&draft,l,words("应用并标记已解决","Apply and mark resolved"));connect(b,&QDialogButtonBox::accepted,&draft,[&]{if(QRegularExpression(QStringLiteral("^(<<<<<<< |=======|>>>>>>> |\\|\\|\\|\\|\\|\\|\\| )"),QRegularExpression::MultilineOption).match(edit->toPlainText()).hasMatch()){hint->setText(words("仍有冲突标记。","Conflict markers remain."));return;}if(hasDirty(root)||readGitFile(full)!=original){hint->setText(words("文件已变化，请重新核对。","The file changed. Review it again."));return;}QSaveFile f(full);auto bytes=edit->toPlainText().toUtf8();if(original.contains("\r\n"))bytes.replace("\n","\r\n");if(original.startsWith("\xef\xbb\xbf")&&!bytes.startsWith("\xef\xbb\xbf"))bytes.prepend("\xef\xbb\xbf");if(!f.open(QIODevice::WriteOnly)||f.write(bytes)!=bytes.size()||!f.commit()){hint->setText(f.errorString());return;}draft.accept();});fitWorkspaceDialog(&draft);if(draft.exec()==QDialog::Accepted)change({QStringLiteral("add"),QStringLiteral("--"),path});
  }
  void resolveConflict(const QString &path){
    const auto full=QDir(root).filePath(path);if(QFileInfo(full).isSymLink()||QFileInfo(full).size()>4*1024*1024)return;const auto original=readGitFile(full);
    auto versions=std::make_shared<QVector<QByteArray>>(3);
    run({QStringLiteral("show"),QStringLiteral(":1:")+path},[this,path,original,versions](TaskResult base){(*versions)[0]=base.output;run({QStringLiteral("show"),QStringLiteral(":2:")+path},[this,path,original,versions](TaskResult ours){(*versions)[1]=ours.output;run({QStringLiteral("show"),QStringLiteral(":3:")+path},[this,path,original,versions](TaskResult theirs){(*versions)[2]=theirs.output;conflictDraft(path,original,*versions);});});});
  }
  void remote(const QString &operation){run({QStringLiteral("remote")},[this,operation](TaskResult r){if(!r.ok)return;const auto names=QString::fromUtf8(r.output).split(QLatin1Char('\n'),Qt::SkipEmptyParts);if(names.isEmpty()){status->setText(words("尚未设置远程仓库，请在“更多”中添加。","Add a remote repository in More first."));return;}
    MaterialDialog d(this);d.setObjectName(QStringLiteral("researchGitRemote"));configureSettingsSurface(&d);d.setWindowTitle(operation==QStringLiteral("push")?words("推送版本","Push commits"):operation==QStringLiteral("pull")?words("拉取并合并","Pull and merge"):words("获取远程版本","Fetch commits"));auto *l=new QVBoxLayout(&d);auto *remote=new QComboBox(&d);remote->addItems(names);l->addWidget(new QLabel(words("远程仓库","Remote repository"),&d));l->addWidget(remote);auto *branchEdit=new QLineEdit(branch,&d);configureField(branchEdit);l->addWidget(new QLabel(words("分支","Branch"),&d));l->addWidget(branchEdit);if(operation==QStringLiteral("fetch"))branchEdit->setEnabled(false);auto *b=workspaceFooter(&d,l,words("继续","Continue"));connect(b,&QDialogButtonBox::accepted,&d,&QDialog::accept);fitWorkspaceDialog(&d,520,220);if(d.exec()!=QDialog::Accepted)return;const auto name=branchEdit->text().trimmed();if(operation!=QStringLiteral("fetch")&&(name.isEmpty()||name.startsWith(QLatin1Char('-'))))return;QStringList args{operation};if(operation==QStringLiteral("pull"))args<<QStringLiteral("--no-rebase")<<QStringLiteral("--no-edit");if(operation==QStringLiteral("push"))args<<QStringLiteral("--set-upstream");args<<QStringLiteral("--")<<remote->currentText();if(operation!=QStringLiteral("fetch"))args<<name;change(args);
  });}
  void reject() override {if(busy&&mutating){closeRequested=true;hide();return;}if(task)task->cancel();MaterialDialog::reject();}
public:
  explicit VersionDialog(TeXDocumentWindow *window):MaterialDialog(window),owner(window),root(window->untitled()?QString():QFileInfo(completionProjectRoot(window)).absolutePath()){
    setObjectName(QStringLiteral("researchVersionDialog"));setAttribute(Qt::WA_DeleteOnClose);configureSettingsSurface(this);setWindowTitle(words("版本与协作","Versions and collaboration"));auto *layout=new QVBoxLayout(this);layout->setContentsMargins(20,16,20,16);layout->setSpacing(10);auto *top=new QHBoxLayout;tabs=new QTabBar(this);tabs->addTab(words("修改","Changes"));tabs->addTab(words("历史","History"));tabs->addTab(words("分支","Branches"));tabs->setExpanding(false);tabs->setDrawBase(false);top->addWidget(tabs);top->addStretch();
    auto menuButton=[&](const QString &label){auto *b=new QToolButton(this);b->setText(label);b->setPopupMode(QToolButton::InstantPopup);configureButton(b,ButtonRole::Text);top->addWidget(b);b->setMenu(new QMenu(b));return b;};sync=menuButton(words("同步","Sync"));more=menuButton(words("更多","More"));layout->addLayout(top);status=new QLabel(this);status->setWordWrap(true);status->setStyleSheet(QStringLiteral("color:#737d85;"));status->setTextInteractionFlags(Qt::TextSelectableByMouse);layout->addWidget(status);
    auto *split=new QSplitter(this);split->setChildrenCollapsible(false);split->setHandleWidth(1);
    files=new QListWidget(split);files->setObjectName(QStringLiteral("researchGitFiles"));configureSettingsList(files);files->setFrameShape(QFrame::NoFrame);files->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);files->setMinimumWidth(140);files->setContextMenuPolicy(Qt::CustomContextMenu);files->setItemDelegate(new VersionFileDelegate(files));
    auto *detail=new QWidget(split);auto *detailLayout=new QVBoxLayout(detail);detailLayout->setContentsMargins(16,2,0,0);detailLayout->setSpacing(8);auto *heading=new QHBoxLayout;
    fileTitle=new QLabel(words("修改预览","Change preview"),detail);fileTitle->setObjectName(QStringLiteral("researchGitFileTitle"));fileTitle->installEventFilter(this);fileTitle->setTextFormat(Qt::PlainText);auto titleFont=interfaceFont();titleFont.setWeight(QFont::DemiBold);fileTitle->setFont(titleFont);fileTitle->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);heading->addWidget(fileTitle,1);
    diffSummary=new QLabel(detail);diffSummary->setObjectName(QStringLiteral("researchGitDiffSummary"));diffSummary->setWordWrap(false);diffSummary->setSizePolicy(QSizePolicy::Minimum,QSizePolicy::Preferred);diffSummary->setStyleSheet(QStringLiteral("color:#737d85;"));heading->addWidget(diffSummary);detailLayout->addLayout(heading);
    preview=new QPlainTextEdit(detail);configureWorkspaceText(preview);preview->document()->setDocumentMargin(0);new VersionDiffHighlighter(preview->document());preview->setReadOnly(true);preview->setObjectName(QStringLiteral("researchGitDiff"));preview->setPlaceholderText(words("选择文件查看修改","Select a file to review changes"));preview->setMinimumWidth(160);detailLayout->addWidget(preview,1);split->addWidget(files);split->addWidget(detail);split->setSizes({210,590});layout->addWidget(split,1);
    auto *bottom=new QHBoxLayout;message=new QLineEdit(this);configureField(message);message->setPlaceholderText(words("版本说明","Describe this version"));message->setObjectName(QStringLiteral("researchGitMessage"));bottom->addWidget(message,1);commit=new QPushButton(words("保存版本","Commit"),this);commit->setObjectName(QStringLiteral("researchGitCommit"));configureButton(commit);bottom->addWidget(commit);auto *close=new QPushButton(words("关闭","Close"),this);configureButton(close);bottom->addWidget(close);layout->addLayout(bottom);connect(close,&QPushButton::clicked,this,&QDialog::reject);
    auto action=[&](QMenu *menu,const QString &text,std::function<void()> fn){connect(menu->addAction(text),&QAction::triggered,this,std::move(fn));};
    action(more->menu(),words("刷新","Refresh"),[this]{refresh();});action(more->menu(),words("初始化版本管理…","Initialize repository…"),[this]{if(repository)return;if(root.isEmpty()){status->setText(words("请先保存文稿，再启用版本管理。","Save a document before initializing version control."));return;}if(workspaceConfirm(this,words("初始化版本管理","Initialize repository"),root,words("初始化","Initialize")))run({QStringLiteral("init")},[this](TaskResult r){if(r.ok)refresh();},true);});
    action(more->menu(),words("克隆项目…","Clone project…"),[this]{auto url=workspaceInput(this,words("克隆项目","Clone project"),words("仓库地址","Repository URL"));if(url.isEmpty()||url.startsWith(QLatin1Char('-')))return;auto parent=QFileDialog::getExistingDirectory(this,words("选择存放文件夹","Choose destination folder"));if(parent.isEmpty())return;auto name=workspaceInput(this,words("项目名称","Project name"),words("新文件夹名称","New folder name"));if(name.isEmpty()||name.contains(QLatin1Char('/'))||name.contains(QLatin1Char('\\'))||name==QStringLiteral(".."))return;auto dest=QDir(parent).filePath(name);if(QFileInfo::exists(dest))return;if(!workspaceConfirm(this,words("克隆项目","Clone project"),url+QStringLiteral("\n")+dest,words("克隆","Clone")))return;run({QStringLiteral("clone"),QStringLiteral("--"),url,dest},[this,dest](TaskResult r){if(r.ok){root=dest;repository=false;refresh();const auto main=QDir(dest).filePath(QStringLiteral("main.tex"));if(QFileInfo(main).isFile())TeXDocumentWindow::openDocument(main,true,true);}},true);});
    action(more->menu(),words("本项目的作者信息…","Author for this project…"),[this]{const auto name=workspaceInput(this,words("作者姓名","Author name"),words("提交记录中显示的姓名","Name shown in commits"));if(name.isEmpty())return;const auto email=workspaceInput(this,words("作者邮箱","Author email"),words("提交记录中显示的邮箱","Email shown in commits"));if(email.isEmpty())return;run({QStringLiteral("config"),QStringLiteral("--local"),QStringLiteral("user.name"),name},[this,email](TaskResult r){if(r.ok)change({QStringLiteral("config"),QStringLiteral("--local"),QStringLiteral("user.email"),email});},true);});
    action(more->menu(),words("打开项目文稿…","Open project document…"),[this]{if(root.isEmpty())return;const auto file=QFileDialog::getOpenFileName(this,words("选择项目主文档","Choose the project main document"),root,QStringLiteral("LaTeX (*.tex);;All files (*)"));if(!file.isEmpty())TeXDocumentWindow::openDocument(file,true,true);});
    action(more->menu(),words("新建分支…","New branch…"),[this]{const auto name=workspaceInput(this,words("新建分支","New branch"),words("分支名称","Branch name"));if(!name.isEmpty()&&!name.startsWith(QLatin1Char('-')))change({QStringLiteral("switch"),QStringLiteral("-c"),name});});
    action(more->menu(),words("暂存工作进度…","Stash work…"),[this]{const auto label=workspaceInput(this,words("暂存工作进度","Stash work"),words("进度说明","Description"));if(!label.isEmpty())change({QStringLiteral("stash"),QStringLiteral("push"),QStringLiteral("--include-untracked"),QStringLiteral("-m"),label});});
    action(more->menu(),words("恢复暂存进度…","Apply stash…"),[this]{manageStashes();});
    action(more->menu(),words("添加远程仓库…","Add remote…"),[this]{const auto name=workspaceInput(this,words("远程名称","Remote name"),words("例如 origin","For example origin"),QStringLiteral("origin"));if(!QRegularExpression(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]*$")).match(name).hasMatch())return;const auto url=workspaceInput(this,words("远程仓库","Remote repository"),words("仓库地址","Repository URL"));if(!url.isEmpty()&&!url.startsWith(QLatin1Char('-')))change({QStringLiteral("remote"),QStringLiteral("add"),name,url});});
    action(more->menu(),words("加入当前差异块","Stage current hunk"),[this]{if(busy||shownStaged||tabs->currentIndex()!=0||shownPath.isEmpty())return;const auto patch=gitPatchAtLine(shownDiff,diffLines.value(preview->textCursor().blockNumber(),-1));if(!patch.isEmpty())change({QStringLiteral("apply"),QStringLiteral("--cached"),QStringLiteral("--recount"),QStringLiteral("-")},patch.toUtf8());});
    action(more->menu(),words("撤销暂存当前差异块","Unstage current hunk"),[this]{if(busy||!shownStaged||tabs->currentIndex()!=0||shownPath.isEmpty())return;const auto patch=gitPatchAtLine(shownDiff,diffLines.value(preview->textCursor().blockNumber(),-1));if(!patch.isEmpty())change({QStringLiteral("apply"),QStringLiteral("--cached"),QStringLiteral("--reverse"),QStringLiteral("--recount"),QStringLiteral("-")},patch.toUtf8());});
    auto *selectedLines=more->menu()->addAction(words("加入选中行","Stage selected lines"));
    connect(selectedLines,&QAction::triggered,this,[this]{stageSelectedLines();});
    connect(more->menu(),&QMenu::aboutToShow,this,[this,selectedLines]{
      selectedLines->setText(shownStaged?words("撤销暂存选中行","Unstage selected lines"):words("加入选中行","Stage selected lines"));
      selectedLines->setEnabled(!busy&&tabs->currentIndex()==0&&!shownPath.isEmpty()&&!shownDiff.isEmpty()&&preview->textCursor().hasSelection());
    });
    preview->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(preview,&QWidget::customContextMenuRequested,this,[this](const QPoint &point){
      std::unique_ptr<QMenu> menu(preview->createStandardContextMenu());
      if(tabs->currentIndex()==0&&!shownPath.isEmpty()&&!shownDiff.isEmpty()&&preview->textCursor().hasSelection()){
        menu->addSeparator();auto *stage=menu->addAction(shownStaged?words("撤销暂存选中行","Unstage selected lines"):words("加入选中行","Stage selected lines"));
        stage->setEnabled(!busy);connect(stage,&QAction::triggered,this,[this]{stageSelectedLines();});
      }
      menu->exec(preview->viewport()->mapToGlobal(point));
    });
    action(sync->menu(),words("获取远程版本","Fetch"),[this]{remote(QStringLiteral("fetch"));});action(sync->menu(),words("拉取并合并…","Pull and merge…"),[this]{remote(QStringLiteral("pull"));});action(sync->menu(),words("推送版本…","Push…"),[this]{remote(QStringLiteral("push"));});

    auto *branchMenu=more->menu()->addMenu(words("分支操作","Branch actions"));
    action(branchMenu,words("从指定版本新建分支…","Create branch from…"),[this]{createBranchFrom();});
    action(branchMenu,words("重命名当前分支…","Rename current branch…"),[this]{
      const auto name=workspaceInput(this,words("重命名分支","Rename branch"),words("新的分支名称","New branch name"),branch);
      if(!name.isEmpty()&&!name.startsWith(QLatin1Char('-')))change({QStringLiteral("branch"),QStringLiteral("-m"),name});
    });
    action(branchMenu,words("变基到其他分支…","Rebase onto…"),[this]{chooseReference(words("选择目标分支","Choose target branch"),[this](QString ref){revisionOperation(QStringLiteral("rebase"),ref);});});
    auto *stashMenu=more->menu()->addMenu(words("工作进度","Stashed work"));
    action(stashMenu,words("只暂存已加入版本的修改…","Stash staged changes…"),[this]{
      bool accepted=false;const auto text=workspaceInput(this,words("暂存工作进度","Stash work"),words("进度说明","Description"),{},&accepted);
      if(accepted)change({QStringLiteral("stash"),QStringLiteral("push"),QStringLiteral("--staged"),QStringLiteral("-m"),text});
    });
    auto *commitMenu=more->menu()->addMenu(words("上次版本","Last commit"));
    action(commitMenu,words("修订上次版本…","Amend last commit…"),[this]{reviseLastCommit(true);});
    action(commitMenu,words("撤销上次提交…","Undo last commit…"),[this]{reviseLastCommit(false);});
    action(commitMenu,words("从保留版本恢复…","Recover retained version…"),[this]{recoverCommit();});
    action(more->menu(),words("当前文稿的历史…","Current file history…"),[this]{currentFileHistory();});
    action(more->menu(),words("当前行的作者","Who changed this line"),[this]{currentLineAuthor();});
    auto *ongoing=more->menu()->addMenu(words("进行中的操作","Operation in progress"));
    connect(more->menu(),&QMenu::aboutToShow,this,[this,ongoing,action]{
      ongoing->clear();auto gitDir=QDir(root).filePath(QStringLiteral(".git"));
      if(QFileInfo(gitDir).isFile()){QFile file(gitDir);if(file.open(QIODevice::ReadOnly)){const auto value=QString::fromUtf8(file.read(8192)).trimmed();if(value.startsWith(QStringLiteral("gitdir: ")))gitDir=QDir(root).absoluteFilePath(value.mid(8));}}
      QString operation;
      if(QFileInfo::exists(QDir(gitDir).filePath(QStringLiteral("rebase-merge")))||QFileInfo::exists(QDir(gitDir).filePath(QStringLiteral("rebase-apply"))))operation=QStringLiteral("rebase");
      else if(QFileInfo::exists(QDir(gitDir).filePath(QStringLiteral("CHERRY_PICK_HEAD"))))operation=QStringLiteral("cherry-pick");
      else if(QFileInfo::exists(QDir(gitDir).filePath(QStringLiteral("REVERT_HEAD"))))operation=QStringLiteral("revert");
      else if(QFileInfo::exists(QDir(gitDir).filePath(QStringLiteral("MERGE_HEAD"))))operation=QStringLiteral("merge");
      ongoing->menuAction()->setVisible(!operation.isEmpty());if(operation.isEmpty())return;
      action(ongoing,words("继续","Continue"),[this,operation]{manageOperation(operation,QStringLiteral("continue"));});
      if(operation!=QStringLiteral("merge"))action(ongoing,words("跳过当前版本","Skip current commit"),[this,operation]{manageOperation(operation,QStringLiteral("skip"));});
      action(ongoing,words("取消操作","Abort operation"),[this,operation]{manageOperation(operation,QStringLiteral("abort"));});
    });
    action(branchMenu,words("新建工作副本…","New working copy…"),[this]{worktrees(true);});
    action(branchMenu,words("打开其他工作副本…","Open another working copy…"),[this]{worktrees(false);});
    action(branchMenu,words("移除工作副本…","Remove working copy…"),[this]{worktrees(2);});
    connect(files,&QListWidget::itemDoubleClicked,this,[this](QListWidgetItem *item){if(tabs->currentIndex()==2&&!busy)switchReference(item->data(Qt::UserRole).toString());});
    auto *repositoryMenu=more->menu()->addMenu(words("项目与仓库","Project and repository"));
    action(repositoryMenu,words("管理远程仓库…","Manage remotes…"),[this]{manageRemotes();});
    auto relocate=[&](const QString &caption,QMenu *destination){for(auto *item:more->menu()->actions())if(item->text()==caption){more->menu()->removeAction(item);destination->addAction(item);break;}};
    relocate(words("新建分支…","New branch…"),branchMenu);
    relocate(words("暂存工作进度…","Stash work…"),stashMenu);
    relocate(words("恢复暂存进度…","Apply stash…"),stashMenu);
    for(const auto &caption:QStringList{words("初始化版本管理…","Initialize repository…"),words("克隆项目…","Clone project…"),words("本项目的作者信息…","Author for this project…"),words("打开项目文稿…","Open project document…"),words("添加远程仓库…","Add remote…")})relocate(caption,repositoryMenu);
    connect(commit,&QPushButton::clicked,this,[this]{if(message->text().trimmed().isEmpty()){message->setFocus();return;}change({QStringLiteral("commit"),QStringLiteral("-F"),QStringLiteral("-")},message->text().toUtf8());});connect(tabs,&QTabBar::currentChanged,this,[this,detail](int tab){detail->setVisible(tab!=2);message->setVisible(tab==0);commit->setVisible(tab==0);refresh();});connect(files,&QListWidget::currentRowChanged,this,[this]{showSelection();});connect(files,&QListWidget::itemChanged,this,[this](QListWidgetItem *item){if(loading||busy)return;const bool staged=item->data(Qt::UserRole+1).toBool(),wanted=item->checkState()==Qt::Checked;if(staged!=wanted)setStaged(state.value(item->data(Qt::UserRole).toInt()),wanted);});connect(files,&QListWidget::customContextMenuRequested,this,[this](QPoint p){context(p);});connect(this,&QDialog::finished,window,[window]{window->editor()->setFocus();});fitWorkspaceDialog(this);files->setEnabled(false);QTimer::singleShot(0,this,[this]{refresh();});
  }
};
}
void openVersionControl(TeXDocumentWindow *window){if(auto *d=window->findChild<QDialog*>(QStringLiteral("researchVersionDialog"))){d->show();d->raise();return;}if(systemTool(QStringLiteral("git")).isEmpty()){openEnvironmentCheck(window);showTransientStatusMessage(window,words("安装 Git 后可使用版本与协作。","Install Git to use versions and collaboration."));return;}(new VersionDialog(window))->show();}
void installProjectTools(TeXDocumentWindow *window){if(window->findChild<QAction*>(QStringLiteral("researchVersionControl")))return;for(const auto &name:{QStringLiteral("researchVersionControl"),QStringLiteral("researchEnvironmentCheck")}){auto *a=new QAction(window);a->setObjectName(name);window->addAction(a);const bool versions=name==QStringLiteral("researchVersionControl");auto translate=[a,versions]{a->setText(versions?words("版本与协作…","Versions and collaboration…"):words("检查写作环境…","Check writing environment…"));};translate();QObject::connect(TWApp::instance(),&TWApp::updatedTranslators,window,translate);QObject::connect(a,&QAction::triggered,window,[window,versions]{if(versions)openVersionControl(window);else openEnvironmentCheck(window);});}}
}
