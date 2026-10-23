#include "ResearchProjectOperations.h"
#include "ResearchFileWrite.h"
#include "ResearchWorkspaceUi.h"
#include "ResearchProject.h"
#include "ResearchWorkflow.h"
#include "TeXDocumentWindow.h"
#include "CompletingEdit.h"
#include <QDirIterator>
#include <QFileSystemModel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QSet>
#include <QPointer>
#include <QRunnable>
#include <QSaveFile>
#include <QTemporaryFile>
#include <QStandardPaths>
#include <QThreadPool>
#include <QThread>
#include <QToolButton>
#include <QTreeView>
#include <QTimer>
#include <QUuid>
#include <QRegularExpression>
#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QUrl>
#include <QDropEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QListWidget>
#include <QDateTime>
#include <QScrollBar>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <algorithm>
#include <atomic>
#include <memory>
#include <cstring>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif
namespace Research {
namespace {
struct ProjectOperationHistory final : QObject {
  QVector<ProjectFilePlan> plans,redo;
  std::function<const QMimeData*()> readClipboard{[]{return QApplication::clipboard()->mimeData();}};
  std::function<void(QMimeData*)> writeClipboard{[](QMimeData *mime){QApplication::clipboard()->setMimeData(mime);}};
  explicit ProjectOperationHistory(QObject *parent):QObject(parent){setObjectName(QStringLiteral("researchProjectOperationHistory"));}
};
class ProjectTreeDropFilter final : public QObject {
  QTreeView *tree;QFileSystemModel *model;QString folder;
  std::function<void(QStringList,QString,ProjectTransfer)> transfer;
  bool eventFilter(QObject *object,QEvent *event) override {
    if(object!=tree->viewport()||(event->type()!=QEvent::DragEnter&&event->type()!=QEvent::DragMove&&event->type()!=QEvent::Drop))return QObject::eventFilter(object,event);
    auto *drop=static_cast<QDropEvent*>(event);QStringList sources;
    for(const auto &url:drop->mimeData()->urls())if(url.isLocalFile())sources.append(url.toLocalFile());
    if(sources.isEmpty()){drop->ignore();return true;}
    const auto index=tree->indexAt(drop->position().toPoint());const auto path=model->filePath(index);
    const auto destination=path.isEmpty()?folder:QFileInfo(path).isDir()?path:QFileInfo(path).absolutePath();
    const bool move=drop->source()==tree&&!(drop->modifiers()&Qt::ControlModifier);
    // The reviewed transaction owns source removal. Returning Move from Drop
    // would let the source view delete rows before that review is accepted.
    drop->setDropAction(event->type()==QEvent::Drop?Qt::CopyAction:move?Qt::MoveAction:Qt::CopyAction);drop->accept();
    if(event->type()==QEvent::Drop)transfer(sources,destination,move?ProjectTransfer::Move:ProjectTransfer::Copy);
    return true;
  }
public:
  ProjectTreeDropFilter(QTreeView *view,QFileSystemModel *files,const QString &root,std::function<void(QStringList,QString,ProjectTransfer)> callback):QObject(view),tree(view),model(files),folder(root),transfer(std::move(callback)){
    tree->setDragEnabled(true);tree->setAcceptDrops(true);tree->viewport()->setAcceptDrops(true);tree->setDragDropMode(QAbstractItemView::DragDrop);tree->viewport()->installEventFilter(this);
  }
};
bool withinProject(const QString &root,const QString &path) {
  const auto base=QFileInfo(root).canonicalFilePath();
  const auto full=QDir::cleanPath(QFileInfo(path).absoluteFilePath());
  if(base.isEmpty())return false;
  const auto rel=QDir(base).relativeFilePath(full);
  if(rel==QStringLiteral(".")||rel==QStringLiteral("..")||rel.startsWith(QStringLiteral("../"))||QDir::isAbsolutePath(rel))return false;
  auto part=full;
  while(QFileInfo(part)!=QFileInfo(base)) {
    QFileInfo f(part);if(f.isSymLink())return false;
    if(f.exists()){const auto actual=QDir(base).relativeFilePath(f.canonicalFilePath());if(actual==QStringLiteral("..")||actual.startsWith(QStringLiteral("../"))||QDir::isAbsolutePath(actual))return false;}
    const auto parent=f.absolutePath();if(parent==part)return false;part=parent;
  }return true;
}
void includeCreatedParents(ProjectFilePlan &plan){QStringList destinations=plan.createDirectories;for(const auto &f:plan.files)if(!f.existed&&f.existsAfter)destinations.append(QFileInfo(f.path).absolutePath());for(auto path:destinations)while(withinProject(plan.root,path)&&!QFileInfo::exists(path)){if(!plan.createDirectories.contains(path))plan.createDirectories.append(path);path=QFileInfo(path).absolutePath();}}
QByteArray bytesOf(const QString &path,bool *ok){QFile f(path);*ok=f.open(QIODevice::ReadOnly)&&f.size()<=64*1024*1024;return *ok?f.readAll():QByteArray();}
QStringList projectTextFiles(const QString &root,const std::function<bool()> &cancelled,QString *error){
  QStringList result,dirs{root};int total=0;
  while(!dirs.isEmpty()){
    if(cancelled&&cancelled()){*error=QStringLiteral("Cancelled");return {};}
    QDirIterator entries(dirs.takeLast(),QDir::Files|QDir::Dirs|QDir::NoDotAndDotDot|QDir::NoSymLinks);
    while(entries.hasNext()){
      if(cancelled&&cancelled()){*error=QStringLiteral("Cancelled");return {};}
      entries.next();const auto f=entries.fileInfo();
      if(++total>50000){*error=words("项目文件过多，请缩小操作范围。","Too many project files. Narrow the scope.");return {};}
      if(f.isDir()){if(!QStringList{QStringLiteral("build"),QStringLiteral("out"),QStringLiteral("node_modules"),QStringLiteral("dist")}.contains(f.fileName())&&!f.fileName().startsWith(QLatin1Char('.')))dirs.append(f.absoluteFilePath());}
      else if(QStringList{QStringLiteral("tex"),QStringLiteral("bib"),QStringLiteral("sty"),QStringLiteral("cls"),QStringLiteral("md"),QStringLiteral("txt")}.contains(f.suffix().toLower()))result.append(f.absoluteFilePath());
    }
  }
  result.sort();return result;
}
QByteArray searchBytes(const QString &path,const std::function<bool()> &cancelled,bool *ok){
  QFile file(path);*ok=false;if(!file.open(QIODevice::ReadOnly)||file.size()>64*1024*1024)return {};
  QByteArray bytes;
  while(!file.atEnd()){
    if(cancelled&&cancelled())return {};
    const auto chunk=file.read(256*1024);if(file.error()!=QFileDevice::NoError)return {};
    bytes+=chunk;if(bytes.size()>64*1024*1024)return {};
  }
  *ok=true;return bytes;
}

class ProjectSearchInput final : public QLineEdit {
  bool preedit{false};
protected:
  void keyPressEvent(QKeyEvent *event) override {
    QLineEdit::keyPressEvent(event);
    // Enter in the query belongs to search, never to a dialog default button.
    if(event->key()==Qt::Key_Return||event->key()==Qt::Key_Enter)event->accept();
  }
  void inputMethodEvent(QInputMethodEvent *event) override {
    const bool before=preedit;preedit=!event->preeditString().isEmpty();
    // Mark composition before QLineEdit emits textChanged for committed parts.
    if(preedit&&!before&&compositionChanged)compositionChanged();
    QLineEdit::inputMethodEvent(event);
    if(before&&!preedit&&compositionChanged)compositionChanged();
  }
public:
  explicit ProjectSearchInput(QWidget *parent):QLineEdit(parent){}
  std::function<void()> compositionChanged;
  bool composing() const{return preedit;}
};

// One active scan and one replaceable pending query. A newer edit cancels the
// active scan; its completion releases the slot even when its result is stale.
class ProjectSearchFlow final : public QObject {
  QDialog *dialog;ProjectSearchInput *query;QListWidget *results;QLabel *notice;
  QPushButton *find,*preview;QString folder,pending;QTimer delay;
  std::shared_ptr<std::atomic_int> epoch{std::make_shared<std::atomic_int>(0)};
  bool running{false},ready{false},closed{false};
  void start(){
    if(closed||running||!ready||pending.isEmpty()||query->composing())return;
    const auto text=pending;const int ticket=epoch->load();const auto generation=epoch;
    const auto root=folder;QPointer<ProjectSearchFlow> guard(this);running=true;ready=false;
    notice->setText(words("正在查找…","Searching…"));notice->show();
    QThreadPool::globalInstance()->start(QRunnable::create([guard,generation,ticket,text,root]{
      const auto cancelled=[=]{return generation->load()!=ticket;};
      auto plan=planProjectReplace(root,text,text,cancelled);
      QStringList labels;QVector<QPair<QString,int>> matches;
      if(plan.error.isEmpty())for(const auto &file:plan.files){
        if(cancelled()||matches.size()>=2000)break;
        const auto source=QString::fromUtf8(file.before);qsizetype begin=0;int line=1;
        while(begin<source.size()&&matches.size()<2000){
          if(cancelled())break;
          auto end=source.indexOf(QLatin1Char('\n'),begin);if(end<0)end=source.size();
          const auto content=QStringView(source).mid(begin,end-begin);
          if(content.contains(text)){labels.append(QDir(root).relativeFilePath(file.path)+QStringLiteral(":%1  ").arg(line)+content.trimmed().left(200).toString());matches.append({file.path,line});}
          begin=end+1;++line;
        }
      }
      if(!qApp)return;
      QMetaObject::invokeMethod(qApp,[guard,generation,ticket,text,plan=std::move(plan),labels=std::move(labels),matches=std::move(matches)]{
        if(!guard)return;auto *self=guard.data();self->running=false;if(self->closed)return;
        if(generation->load()==ticket){
          self->found=plan;self->searched=text;
          for(int n=0;n<matches.size();++n){auto *item=new QListWidgetItem(labels[n],self->results);item->setData(Qt::UserRole,matches[n].first);item->setData(Qt::UserRole+1,matches[n].second);}
          self->notice->setText(!plan.error.isEmpty()?plan.error:matches.size()>=2000?words("显示前 2000 处匹配，请缩小范围。","Showing the first 2000 matches. Narrow the search."):words("%1 处匹配","%1 matches").arg(matches.size()));
          self->notice->show();self->find->setVisible(!plan.error.isEmpty());
          self->preview->setEnabled(plan.error.isEmpty()&&!plan.files.isEmpty());
        }
        self->start();
      },Qt::QueuedConnection);
    }));
  }
public:
  ProjectFilePlan found;QString searched;
  ProjectSearchFlow(QDialog *owner,ProjectSearchInput *field,QListWidget *list,QLabel *status,QPushButton *retry,QPushButton *apply,const QString &root)
    :QObject(owner),dialog(owner),query(field),results(list),notice(status),find(retry),preview(apply),folder(root){
    delay.setSingleShot(true);delay.setInterval(250);
    QObject::connect(&delay,&QTimer::timeout,this,[this]{ready=true;start();});
    QObject::connect(query,&QLineEdit::textChanged,this,[this]{request(false);});
    QObject::connect(query,&QLineEdit::returnPressed,this,[this]{request(true);});
    QObject::connect(find,&QPushButton::clicked,this,[this]{request(true);});
    query->compositionChanged=[this]{request(false);};
    QObject::connect(dialog,&QDialog::finished,this,[this]{closed=true;++*epoch;delay.stop();ready=false;});
    notice->hide();find->hide();
  }
  ~ProjectSearchFlow() override{++*epoch;}
  void request(bool immediate){
    if(closed)return;++*epoch;delay.stop();ready=false;pending=query->text();
    results->clear();found={};searched.clear();preview->setEnabled(false);notice->clear();notice->hide();find->hide();
    if(pending.isEmpty()||query->composing())return;
    if(immediate){ready=true;start();}else delay.start();
  }
};
bool writeBytes(const QString &path,const QByteArray &data,QString *error,const std::function<bool()> &retryAllowed={}){
 return writeObservedFile(path,data,error,retryAllowed);
}
QString resolveFile(QString name,const QString &file,const QString &root,const QString &suffix){for(const auto &base:{root,QFileInfo(file).absolutePath()})if(!base.isEmpty())for(const auto &ext:{QString(),suffix}){QFileInfo f(QDir(base).filePath(name+ext));if(f.isFile())return f.canonicalFilePath();}return {};}
QString updatePaths(QString text,const QString &file,const QString &newFile,const QString &root,const QString &newRoot,const QMap<QString,QString> &moves){
  const auto clean=maskProjectText(text);QRegularExpression re(QStringLiteral(R"(\\(input|include|includegraphics|bibliography|addbibresource)\s*(?:\[[^\]]*\]\s*)?\{([^{}]+)\})"));auto it=re.globalMatch(clean);struct Edit{int start,length;QString value;};QVector<Edit> edits;
  while(it.hasNext()){const auto m=it.next();const auto kind=m.captured(1);QStringList values=text.mid(m.capturedStart(2),m.capturedLength(2)).split(QLatin1Char(','));bool changed=false;for(auto &value:values){const auto original=value.trimmed();if(original.contains(QLatin1Char('\\'))||original.contains(QLatin1Char('#')))continue;const QString suffix=kind==QStringLiteral("input")||kind==QStringLiteral("include")?QStringLiteral(".tex"):kind==QStringLiteral("bibliography")||kind==QStringLiteral("addbibresource")?QStringLiteral(".bib"):QString();auto resolved=resolveFile(original,file,root,suffix);if(resolved.isEmpty()&&kind==QStringLiteral("includegraphics"))for(const auto &ext:{QStringLiteral(".pdf"),QStringLiteral(".png"),QStringLiteral(".jpg"),QStringLiteral(".jpeg")}){resolved=resolveFile(original,file,root,ext);if(!resolved.isEmpty())break;}if(resolved.isEmpty())continue;const auto target=moves.value(resolved,resolved);if(target==resolved&&file==newFile&&root==newRoot)continue;bool relativeToSource=resolveFile(original,QDir(root).filePath(QStringLiteral(".resolve")),QString(),suffix)!=resolved;if(kind==QStringLiteral("includegraphics"))for(const auto &ext:{QStringLiteral(".pdf"),QStringLiteral(".png"),QStringLiteral(".jpg"),QStringLiteral(".jpeg")})relativeToSource=relativeToSource&&resolveFile(original,QDir(root).filePath(QStringLiteral(".resolve")),QString(),ext)!=resolved;auto rel=QDir(relativeToSource?QFileInfo(newFile).absolutePath():newRoot).relativeFilePath(target);if(QFileInfo(original).suffix().isEmpty()&&!suffix.isEmpty()&&rel.endsWith(suffix))rel.chop(suffix.size());value=rel;changed=true;}if(changed)edits.append({int(m.capturedStart(2)),int(m.capturedLength(2)),values.join(QLatin1Char(','))});}
  QRegularExpression directive(QStringLiteral(R"((?m)^(%\s*!\s*TEX\s+root\s*=\s*)([^\r\n]+))"),QRegularExpression::CaseInsensitiveOption);auto roots=directive.globalMatch(text);
  while(roots.hasNext()){auto m=roots.next();auto target=resolveFile(m.captured(2).trimmed(),file,QString(),QStringLiteral(".tex"));if(target.isEmpty())continue;const auto moved=moves.value(target,target);if(moved!=target||newFile!=file)edits.append({int(m.capturedStart(2)),int(m.capturedLength(2)),QDir(QFileInfo(newFile).absolutePath()).relativeFilePath(moved)});}
  std::sort(edits.begin(),edits.end(),[](const Edit &a,const Edit &b){return a.start<b.start;});
  for(auto i=edits.crbegin();i!=edits.crend();++i)text.replace(i->start,i->length,i->value);return text;
}
}
#include "ResearchProjectTransfer.inc"
ProjectFilePlan planProjectMove(const QString &root,const QString &from,const QString &to,const std::function<bool()> &cancelled,const QString &mainFile){
  ProjectFilePlan plan;plan.root=QFileInfo(root).canonicalFilePath();if(!withinProject(root,from)||!withinProject(root,to)||!QFileInfo(from).exists()||QFileInfo(to).exists()||to.contains(QRegularExpression(QStringLiteral(R"([{}%#])")))){plan.error=words("请选择项目内尚不存在的目标路径。","Choose a new path inside the project.");return plan;}
  QMap<QString,QString> moves;if(QFileInfo(from).isDir()){if(QDir(from).relativeFilePath(to).startsWith(QStringLiteral(".."))==false){plan.error=words("不能将文件夹移入自身。","A folder cannot be moved inside itself.");return plan;}plan.createDirectories.append(to);plan.removeDirectories.append(from);QDirIterator it(from,QDir::Files|QDir::Dirs|QDir::NoDotAndDotDot|QDir::Hidden,QDirIterator::Subdirectories);while(it.hasNext()){it.next();if(it.fileInfo().isSymLink()){plan.error=words("文件夹包含链接，请单独处理。","Handle folders containing links separately.");return plan;}if(it.fileInfo().isDir()){plan.removeDirectories.append(it.filePath());plan.createDirectories.append(QDir(to).filePath(QDir(from).relativeFilePath(it.filePath())));}if(it.fileInfo().isFile())moves.insert(it.fileInfo().canonicalFilePath(),QDir(to).filePath(QDir(from).relativeFilePath(it.filePath())));if(moves.size()>10000){plan.error=words("文件夹过大，请分批移动。","Move this large folder in smaller batches.");return plan;}}}else moves.insert(QFileInfo(from).canonicalFilePath(),QFileInfo(to).absoluteFilePath());
  plan.moves=moves;auto texts=projectTextFiles(root,cancelled,&plan.error);if(!plan.error.isEmpty())return plan;QStringList all=texts;for(const auto &p:moves.keys())if(!all.contains(p))all.append(p);qint64 bytes=0;
  for(const auto &file:all){if(cancelled&&cancelled()){plan.error=QStringLiteral("Cancelled");return plan;}bool ok=false;const auto before=bytesOf(file,&ok);if(!ok){plan.error=words("无法读取文件：","Cannot read: ")+file;return plan;}bytes+=before.size();if(bytes>128*1024*1024){plan.error=words("此次操作超过 128 MB，请分批处理。","This operation exceeds 128 MB. Use smaller batches.");return plan;}auto after=before;const auto destination=moves.value(file,file);if(texts.contains(file)){const bool bom=before.startsWith("\xef\xbb\xbf");const auto body=bom?before.mid(3):before;const auto text=QString::fromUtf8(body);if(text.toUtf8()!=body){plan.error=words("请先将此文件保存为 UTF-8，再移动项目路径：","Save this file as UTF-8 before moving project paths: ")+file;return plan;}else{after=updatePaths(text,file,destination,root,mainFile.isEmpty()?root:QFileInfo(moves.value(mainFile,mainFile)).absolutePath(),moves).toUtf8();if(bom)after.prepend("\xef\xbb\xbf");}}
    if(destination!=file){plan.files.append({file,before,{},true,false});plan.files.append({destination,{},after,false,true});}else if(before!=after)plan.files.append({file,before,after,true,true});
  }includeCreatedParents(plan);return plan;
}
ProjectFilePlan planProjectReplace(const QString &root,const QString &query,const QString &replacement,const std::function<bool()> &cancelled){
  ProjectFilePlan plan;plan.root=QFileInfo(root).canonicalFilePath();if(query.isEmpty()){plan.error=words("请输入查找内容。","Enter search text.");return plan;}
  const auto files=projectTextFiles(root,cancelled,&plan.error);qint64 total=0;
  for(const auto &file:files){
    if(cancelled&&cancelled()){plan.error=QStringLiteral("Cancelled");return plan;}
    bool ok;const auto before=searchBytes(file,cancelled,&ok);total+=before.size();
    if(cancelled&&cancelled()){plan.error=QStringLiteral("Cancelled");return plan;}
    if(!ok||total>128*1024*1024){plan.error=words("查找范围过大或文件不可读。","Search scope is too large or unreadable.");return plan;}
    const bool bom=before.startsWith("\xef\xbb\xbf");const auto body=bom?before.mid(3):before;auto text=QString::fromUtf8(body);
    if(text.toUtf8()!=body)continue;if(!text.contains(query))continue;
    auto after=before;if(query!=replacement){text.replace(query,replacement);after=text.toUtf8();if(bom)after.prepend("\xef\xbb\xbf");}
    plan.files.append({file,before,after,true,true});
  }
  return plan;
}
bool applyProjectPlan(const ProjectFilePlan &plan,const QString &journal,QString *error,bool reverse){
  error->clear();if(!plan.error.isEmpty()){*error=plan.error;return false;}
  qint64 snapshotBytes=0;for(const auto &file:plan.files){snapshotBytes+=file.before.size()+file.after.size();if(snapshotBytes>128*1024*1024){*error=words("恢复快照超过 128 MB，请分批操作。","Recovery snapshots exceed 128 MB. Use smaller batches.");return false;}}
  auto files=plan.files;auto create=plan.createDirectories,remove=plan.removeDirectories;
  if(reverse){for(auto &f:files){std::swap(f.before,f.after);std::swap(f.existed,f.existsAfter);}std::swap(create,remove);}
  auto unchanged=[&](const ProjectFileChange &f){if(!withinProject(plan.root,f.path)||QFileInfo::exists(f.path)!=f.existed)return false;if(!f.existed)return true;bool ok;const auto bytes=bytesOf(f.path,&ok);return ok&&bytes==f.before;};
  for(const auto &f:files)if(!unchanged(f)){*error=words("文件已变化，请重新预览：","File changed. Preview again: ")+f.path;return false;}
  for(const auto &dir:create)if(!withinProject(plan.root,dir)||QFileInfo::exists(dir)){*error=words("目标文件夹已变化。","Destination folder changed.");return false;}
  for(const auto &dir:remove)if(!withinProject(plan.root,dir)||!QFileInfo(dir).isDir()){*error=words("原文件夹已变化。","Source folder changed.");return false;}
  QJsonArray entries;for(const auto &f:files)entries.append(QJsonObject{{QStringLiteral("path"),f.path},{QStringLiteral("existed"),f.existed},{QStringLiteral("existsAfter"),f.existsAfter},{QStringLiteral("before"),QString::fromLatin1(f.before.toBase64())},{QStringLiteral("after"),QString::fromLatin1(f.after.toBase64())}});
  if(!writeBytes(journal,QJsonDocument(QJsonObject{{QStringLiteral("root"),plan.root},{QStringLiteral("files"),entries},{QStringLiteral("createDirectories"),QJsonArray::fromStringList(create)},{QStringLiteral("removeDirectories"),QJsonArray::fromStringList(remove)}}).toJson(QJsonDocument::Compact),error))return false;
  std::sort(create.begin(),create.end(),[](const QString &a,const QString &b){return a.size()<b.size();});
  std::sort(remove.begin(),remove.end(),[](const QString &a,const QString &b){return a.size()>b.size();});
  QVector<int> order,changed;QStringList created,removed;bool success=true;
  // Materialize destinations before removing sources. The journal survives a crash.
  for(int i=0;i<files.size();++i)if(files[i].existsAfter)order.append(i);
  for(int i=0;i<files.size();++i)if(!files[i].existsAfter)order.append(i);
  for(const auto &dir:create){if(!QDir().mkpath(dir)){*error=words("无法创建文件夹：","Cannot create folder: ")+dir;success=false;break;}created.append(dir);}
  if(success)for(int i:order){const auto &f=files[i];if(!unchanged(f)){*error=words("文件在操作期间发生变化：","File changed during the operation: ")+f.path;success=false;break;}if(f.existsAfter){if(!writeBytes(f.path,f.after,error,[&]{return unchanged(f);})){success=false;break;}}else if(f.existed&&!QFile::remove(f.path)){*error=words("无法移除原文件：","Cannot remove source file: ")+f.path;success=false;break;}changed.append(i);}
  if(success)for(const auto &dir:remove){if(!QDir().rmdir(dir)){*error=words("文件夹内容已变化：","Folder contents changed: ")+dir;success=false;break;}removed.append(dir);}
  if(!success){for(auto i=removed.crbegin();i!=removed.crend();++i)QDir().mkpath(*i);for(auto i=changed.crbegin();i!=changed.crend();++i){const auto &f=files[*i];bool ok;const auto current=bytesOf(f.path,&ok);if((f.existsAfter&&(!ok||current!=f.after))||(!f.existsAfter&&QFileInfo::exists(f.path))){*error+=words("\n恢复记录已保留；此文件又有修改：","\nRecovery journal retained; this file changed again: ")+f.path;continue;}QString restoreError;if(f.existed){if(!writeBytes(f.path,f.before,&restoreError,[&]{bool readable;return bytesOf(f.path,&readable)==f.after&&readable;}))*error+=QStringLiteral("\n")+restoreError;}else QFile::remove(f.path);}for(auto i=created.crbegin();i!=created.crend();++i)QDir().rmdir(*i);return false;}
  return true;
}
void installProjectFileOperations(TeXDocumentWindow *window,QDialog *dialog,QTreeView *tree,QFileSystemModel *model,QToolButton *operations,const QString &folder,const QString &root){
  auto *menu=new QMenu(operations);operations->setMenu(menu);operations->setPopupMode(QToolButton::InstantPopup);tree->setContextMenuPolicy(Qt::CustomContextMenu);QObject::connect(tree,&QTreeView::customContextMenuRequested,dialog,[=](const QPoint &p){if(tree->indexAt(p).isValid()&&!tree->selectionModel()->isSelected(tree->indexAt(p)))tree->setCurrentIndex(tree->indexAt(p));menu->exec(tree->viewport()->mapToGlobal(p));});
  auto *store=static_cast<ProjectOperationHistory*>(window->findChild<QObject*>(QStringLiteral("researchProjectOperationHistory")));if(!store)store=new ProjectOperationHistory(window);auto *history=&store->plans;auto cancelled=std::make_shared<std::atomic_bool>(false);auto busy=std::make_shared<bool>(false);QObject::connect(dialog,&QDialog::finished,dialog,[cancelled]{cancelled->store(true);});
  auto execute=[=](ProjectFilePlan plan) -> bool {includeCreatedParents(plan);if(!plan.error.isEmpty()){showTransientStatusMessage(window,plan.error);return false;}if(plan.files.isEmpty()&&plan.createDirectories.isEmpty())return false;QString description;for(const auto &dir:plan.createDirectories)description+=words("新建文件夹 ","Create folder ")+QDir(folder).relativeFilePath(dir)+QStringLiteral("\n");for(const auto &f:plan.files)description+=(f.existed?(f.existsAfter?words("修改 ","Modify "):words("移除 ","Remove ")):words("新建 ","Create "))+QDir(folder).relativeFilePath(f.path)+QStringLiteral("\n");for(const auto &f:plan.files)if(f.existed&&f.existsAfter&&f.before!=f.after){description+=QStringLiteral("\n── ")+QDir(folder).relativeFilePath(f.path)+QStringLiteral(" ──\n");const auto old=QString::fromUtf8(f.before).split(QLatin1Char('\n')),now=QString::fromUtf8(f.after).split(QLatin1Char('\n'));int displayed=0;for(int i=0;i<qMax(old.size(),now.size())&&displayed<300;++i)if(old.value(i)!=now.value(i)){description+=QStringLiteral("%1  − %2\n       + %3\n").arg(i+1).arg(old.value(i),now.value(i));++displayed;}if(displayed>=300)description+=words("预览仅列出前 300 行变化。\n","Preview shows the first 300 changed lines.\n");}description+=plan.warnings.join(QLatin1Char('\n'));if(!workspaceConfirm(dialog,words("核对项目操作","Review project operation"),description,words("应用","Apply")))return false;
    for(auto *w:TeXDocumentWindow::documentList())for(const auto &f:plan.files)if(QFileInfo(w->fileName()).absoluteFilePath()==f.path&&w->isModified()){showTransientStatusMessage(window,words("请先保存受影响文件。","Save the affected files first."));return false;}
    QString error;const auto journal=QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath(QStringLiteral("project-operations/")+QUuid::createUuid().toString(QUuid::WithoutBraces)+QStringLiteral(".json"));if(!applyProjectPlan(plan,journal,&error)){showTransientStatusMessage(window,error);return false;}for(auto *w:TeXDocumentWindow::documentList()){const auto path=QFileInfo(w->fileName()).absoluteFilePath();if(plan.moves.contains(path)){const auto destination=plan.moves.value(path);w->adoptMovedProjectFile(path,destination);rememberRoot(destination,plan.moves.value(root,root));}else if(plan.moves.contains(root))rememberRoot(path,plan.moves.value(root));}if(plan.moves.contains(root))for(const auto &file:projectFiles(plan.moves.value(root)))rememberRoot(file,plan.moves.value(root));store->redo.clear();history->append(plan);qint64 retained=0;for(const auto &entry:*history)for(const auto &file:entry.files)retained+=file.before.size()+file.after.size();
    while(history->size()>1&&(history->size()>5||retained>128*1024*1024)){for(const auto &file:history->first().files)retained-=file.before.size()+file.after.size();history->removeFirst();}showTransientStatusMessage(window,words("项目已更新，可从“操作”撤销。","Project updated. Undo is available in Actions."));return true;};
  auto prepare=[=](std::function<ProjectFilePlan()> build){if(*busy)return;*busy=true;operations->setEnabled(false);QPointer<QDialog> guard(dialog);QThreadPool::globalInstance()->start(QRunnable::create([=]{auto plan=build();if(qApp)QMetaObject::invokeMethod(qApp,[=]{if(!guard||cancelled->load())return;*busy=false;operations->setEnabled(true);execute(plan);},Qt::QueuedConnection);}));};
  new ProjectTreeDropFilter(tree,model,folder,[=](QStringList paths,QString destination,ProjectTransfer operation){
    prepare([=]{return planProjectTransfer(folder,paths,destination,operation,[cancelled]{return cancelled->load();},root);});
  });
  auto action=[=](const QString &caption,std::function<void()> fn){QObject::connect(menu->addAction(caption),&QAction::triggered,dialog,std::move(fn));};
  action(words("新建文件…","New file…"),[=]{const auto current=model->filePath(tree->currentIndex());const auto base=current.isEmpty()?folder:QFileInfo(current).isDir()?current:QFileInfo(current).absolutePath();const auto name=workspaceInput(dialog,words("新建文件","New file"),words("文件名（含扩展名）","Filename including extension"));if(name.isEmpty())return;const auto dest=QDir(base.isEmpty()?folder:base).filePath(name);ProjectFilePlan plan;plan.root=folder;if(!withinProject(folder,dest)||QFileInfo::exists(dest))return;plan.files.append({dest,{},{},false,true});execute(plan);});
  action(words("新建文件夹…","New folder…"),[=]{const auto name=workspaceInput(dialog,words("新建文件夹","New folder"),words("项目内路径","Path inside project"));if(name.isEmpty())return;const auto dest=QDir(folder).filePath(name);if(withinProject(folder,dest)&&!QFileInfo::exists(dest)){ProjectFilePlan p;p.root=folder;p.createDirectories.append(dest);execute(p);}});
  action(words("重命名或移动…","Rename or move…"),[=]{const auto from=model->filePath(tree->currentIndex());if(!withinProject(folder,from))return;const auto name=workspaceInput(dialog,words("重命名或移动","Rename or move"),words("新的项目内路径","New path inside project"),QDir(folder).relativeFilePath(from));if(name.isEmpty())return;const auto to=QDir(folder).filePath(name);prepare([=]{return planProjectMove(folder,from,to,[cancelled]{return cancelled->load();},root);});});
  action(words("复制文件…","Copy file…"),[=]{const auto from=model->filePath(tree->currentIndex());if(!QFileInfo(from).isFile())return;const auto name=workspaceInput(dialog,words("复制文件","Copy file"),words("新的项目内路径","New path inside project"));if(name.isEmpty())return;const auto dest=QDir(folder).filePath(name);if(!withinProject(folder,dest)||QFileInfo::exists(dest))return;bool ok;const auto data=bytesOf(from,&ok);if(!ok)return;ProjectFilePlan p;p.root=folder;p.files.append({dest,{},data,false,true});execute(p);});
  action(words("删除所选项目…","Delete selected items…"),[=]{
    QStringList selected;for(const auto &index:tree->selectionModel()->selectedRows())selected.append(model->filePath(index));
    prepare([=]{return planProjectTransfer(folder,selected,{},ProjectTransfer::Remove,[cancelled]{return cancelled->load();},root);});
  });
  action(words("撤销上次文件操作","Undo last file operation"),[=]{if(history->isEmpty())return;const auto p=history->last();for(auto *w:TeXDocumentWindow::documentList())for(const auto &f:p.files)if(w->fileName()==f.path&&w->isModified()){showTransientStatusMessage(window,words("请先保存受影响文件。","Save affected files first."));return;}QString error;const auto journal=QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath(QStringLiteral("project-operations/undo-")+QUuid::createUuid().toString(QUuid::WithoutBraces)+QStringLiteral(".json"));if(applyProjectPlan(p,journal,&error,true)){for(auto *w:TeXDocumentWindow::documentList())for(auto it=p.moves.cbegin();it!=p.moves.cend();++it)if(w->adoptMovedProjectFile(it.value(),it.key()))rememberRoot(it.key(),root);store->redo.append(p);history->removeLast();}else showTransientStatusMessage(window,error);});
  action(words("重做文件操作","Redo file operation"),[=]{
    if(store->redo.isEmpty())return;
    const auto pending=store->redo;const auto plan=pending.last();
    if(execute(plan)){store->redo=pending;store->redo.removeLast();}
  });
  tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
  auto selectedPaths=[=]{QStringList paths;for(const auto &index:tree->selectionModel()->selectedRows())paths.append(model->filePath(index));return paths;};
  auto copySelection=[=](bool cut){
    const auto paths=selectedPaths();if(paths.isEmpty())return;auto *mime=new QMimeData;QList<QUrl> urls;
    for(const auto &path:paths)urls.append(QUrl::fromLocalFile(path));mime->setUrls(urls);
    if(cut)mime->setData(QStringLiteral("application/x-texline-project-cut"),folder.toUtf8());
    store->writeClipboard(mime);
  };
  auto pasteSelection=[=]{
    const auto *mime=store->readClipboard();if(!mime||!mime->hasUrls())return;
    QStringList paths;for(const auto &url:mime->urls())if(url.isLocalFile())paths.append(url.toLocalFile());
    const auto current=model->filePath(tree->currentIndex());
    const auto dest=current.isEmpty()?folder:QFileInfo(current).isDir()?current:QFileInfo(current).absolutePath();
    const bool cut=mime->data(QStringLiteral("application/x-texline-project-cut"))==folder.toUtf8();
    prepare([=]{return planProjectTransfer(folder,paths,dest,cut?ProjectTransfer::Move:ProjectTransfer::Copy,[cancelled]{return cancelled->load();},root);});
  };
  auto shortcut=[=](const QString &caption,QKeySequence key,std::function<void()> fn){
    auto *item=menu->addAction(caption);item->setShortcut(key);item->setShortcutContext(Qt::WidgetWithChildrenShortcut);tree->addAction(item);
    QObject::connect(item,&QAction::triggered,dialog,std::move(fn));
  };
  shortcut(words("复制","Copy"),QKeySequence::Copy,[=]{copySelection(false);});
  shortcut(words("剪切","Cut"),QKeySequence::Cut,[=]{copySelection(true);});
  shortcut(words("粘贴","Paste"),QKeySequence::Paste,pasteSelection);
  for(auto *item:menu->actions()){
    if(item->text()==words("删除所选项目…","Delete selected items…"))item->setShortcut(QKeySequence::Delete);
    else if(item->text()==words("撤销上次文件操作","Undo last file operation"))item->setShortcut(QKeySequence::Undo);
    else if(item->text()==words("重做文件操作","Redo file operation"))item->setShortcut(QKeySequence::Redo);
    else continue;
    item->setShortcutContext(Qt::WidgetWithChildrenShortcut);tree->addAction(item);
  }
  menu->addSeparator();action(words("设为主文档","Set as main document"),[=]{const auto file=model->filePath(tree->currentIndex());if(QFileInfo(file).suffix().compare(QStringLiteral("tex"),Qt::CaseInsensitive)!=0)return;rememberRoot(window->fileName(),file);for(const auto &f:projectFiles(file))rememberRoot(f,file);showTransientStatusMessage(window,words("主文档：","Main document: ")+QFileInfo(file).fileName());});
  action(words("插入所选子文件","Insert selected subfile"),[=]{const auto file=model->filePath(tree->currentIndex());if(QFileInfo(file).suffix().compare(QStringLiteral("tex"),Qt::CaseInsensitive)!=0||file==window->fileName()||window->editor()->isReadOnly())return;const auto rel=QFileInfo(root).absoluteDir().relativeFilePath(file);if(rel.contains(QRegularExpression(QStringLiteral(R"([{}%#\\])"))))return;auto cursor=window->textCursor();cursor.beginEditBlock();cursor.insertText(QStringLiteral("\\input{")+rel+QStringLiteral("}\n"));cursor.endEditBlock();rememberRoot(file,root);dialog->accept();window->editor()->setFocus();});
  action(words("项目内查找与替换…","Find and replace in project…"),[=]{
    MaterialDialog search(dialog);configureSettingsSurface(&search);search.setObjectName(QStringLiteral("researchProjectSearch"));search.setWindowTitle(words("项目内查找","Find in project"));auto *layout=new QVBoxLayout(&search);
    auto *top=new QHBoxLayout;auto *query=new ProjectSearchInput(&search);query->setObjectName(QStringLiteral("researchProjectTextQuery"));configureField(query);query->setPlaceholderText(words("查找文本","Find text"));auto *find=new QPushButton(words("查找","Find"),&search);configureButton(find);top->addWidget(query,1);top->addWidget(find);layout->addLayout(top);
    auto *results=new QListWidget(&search);configureSettingsList(results);layout->addWidget(results,1);auto *notice=new QLabel(&search);notice->setWordWrap(true);layout->addWidget(notice);auto *replacement=new QLineEdit(&search);replacement->setObjectName(QStringLiteral("researchProjectReplacement"));configureField(replacement);replacement->setPlaceholderText(words("替换为（可留空）","Replace with (may be empty)"));layout->addWidget(replacement);auto *buttons=workspaceFooter(&search,layout,words("预览替换…","Preview replacement…"));buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
    results->setObjectName(QStringLiteral("researchProjectTextResults"));notice->setObjectName(QStringLiteral("researchProjectSearchNotice"));find->setObjectName(QStringLiteral("researchProjectSearchRetry"));
    auto *flow=new ProjectSearchFlow(&search,query,results,notice,find,buttons->button(QDialogButtonBox::Ok),folder);
    QObject::connect(results,&QListWidget::itemActivated,&search,[=,&search](QListWidgetItem *item){rememberReadingPlace(window);const auto file=item->data(Qt::UserRole).toString();rememberRoot(file,root);TeXDocumentWindow::openDocument(file,true,true,item->data(Qt::UserRole+1).toInt());search.accept();dialog->accept();});
    QObject::connect(buttons,&QDialogButtonBox::accepted,&search,[=,&search]{if(flow->searched!=query->text()||flow->found.error.size()||query->composing())return;auto plan=flow->found;for(auto &f:plan.files){auto text=QString::fromUtf8(f.before);text.replace(flow->searched,replacement->text());f.after=text.toUtf8();if(f.before.startsWith("\xef\xbb\xbf")&&!f.after.startsWith("\xef\xbb\xbf"))f.after.prepend("\xef\xbb\xbf");}search.accept();execute(plan);});
    fitWorkspaceDialog(&search,760,540);query->setFocus();search.exec();
  });
  action(words("恢复文件操作…","Recover file operation…"),[=]{
    const auto location=QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath(QStringLiteral("project-operations"));MaterialDialog recovery(dialog);configureSettingsSurface(&recovery);recovery.setWindowTitle(words("恢复文件操作","Recover file operation"));auto *layout=new QVBoxLayout(&recovery);auto *list=new QListWidget(&recovery);configureSettingsList(list);layout->addWidget(list,1);const auto files=QDir(location).entryInfoList({QStringLiteral("*.json")},QDir::Files,QDir::Time);for(const auto &info:files){if(list->count()>=50)break;QFile file(info.filePath());if(!file.open(QIODevice::ReadOnly)||file.size()>200*1024*1024)continue;auto data=QJsonDocument::fromJson(file.readAll()).object();if(QFileInfo(data.value(QStringLiteral("root")).toString())!=QFileInfo(folder))continue;auto *item=new QListWidgetItem(info.lastModified().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"))+QStringLiteral(" · ")+words("%1 个文件","%1 files").arg(data.value(QStringLiteral("files")).toArray().size()),list);item->setData(Qt::UserRole,info.filePath());}auto *buttons=workspaceFooter(&recovery,layout,words("预览恢复…","Preview recovery…"));QObject::connect(buttons,&QDialogButtonBox::accepted,&recovery,[&]{auto *item=list->currentItem();if(!item)return;QFile file(item->data(Qt::UserRole).toString());if(!file.open(QIODevice::ReadOnly))return;auto data=QJsonDocument::fromJson(file.readAll()).object();ProjectFilePlan plan;plan.root=folder;for(const auto &value:data.value(QStringLiteral("files")).toArray()){auto f=value.toObject();plan.files.append({f.value(QStringLiteral("path")).toString(),QByteArray::fromBase64(f.value(QStringLiteral("after")).toString().toLatin1()),QByteArray::fromBase64(f.value(QStringLiteral("before")).toString().toLatin1()),f.value(QStringLiteral("existsAfter")).toBool(),f.value(QStringLiteral("existed")).toBool()});}for(const auto &v:data.value(QStringLiteral("removeDirectories")).toArray())plan.createDirectories.append(v.toString());for(const auto &v:data.value(QStringLiteral("createDirectories")).toArray())plan.removeDirectories.append(v.toString());recovery.accept();execute(plan);});fitWorkspaceDialog(&recovery,640,440);recovery.exec();
  });
  auto moveAction=[=](const QString &caption,QMenu *destination){
    for(auto *item:menu->actions())if(item->text()==caption){menu->removeAction(item);destination->addAction(item);break;}
  };
  auto *createMenu=new QMenu(words("新建","New"),menu);menu->insertMenu(menu->actions().value(0),createMenu);
  moveAction(words("新建文件…","New file…"),createMenu);moveAction(words("新建文件夹…","New folder…"),createMenu);
  auto *historyMenu=menu->addMenu(words("历史与恢复","History and recovery"));
  QObject::connect(historyMenu->addAction(words("所选文件的本地历史…","Local history of selected file…")),&QAction::triggered,dialog,[=]{
    const auto file=model->filePath(tree->currentIndex());if(!QFileInfo(file).isFile())return;
    rememberRoot(file,root);auto *target=TeXDocumentWindow::openDocument(file,true,true);if(!target)return;
    dialog->accept();QTimer::singleShot(0,target,[target]{if(auto *item=target->findChild<QAction*>(QStringLiteral("researchHistoryAction")))item->trigger();});
  });
  for(const auto &caption:QStringList{words("撤销上次文件操作","Undo last file operation"),words("重做文件操作","Redo file operation"),words("恢复文件操作…","Recover file operation…")})moveAction(caption,historyMenu);
  QObject::connect(menu,&QMenu::aboutToShow,dialog,[=]{
    const auto current=model->filePath(tree->currentIndex());const bool selected=!selectedPaths().isEmpty();
    for(auto *item:menu->actions()){
      item->setEnabled(!*busy);
      if(item->text()==words("复制","Copy")||item->text()==words("剪切","Cut")||item->text()==words("删除所选项目…","Delete selected items…"))item->setEnabled(!*busy&&selected);
      if(item->text()==words("设为主文档","Set as main document")||item->text()==words("插入所选子文件","Insert selected subfile"))item->setVisible(QFileInfo(current).suffix().compare(QStringLiteral("tex"),Qt::CaseInsensitive)==0);
    }
    for(auto *item:historyMenu->actions()){
      if(item->text()==words("撤销上次文件操作","Undo last file operation"))item->setEnabled(!history->isEmpty());
      if(item->text()==words("重做文件操作","Redo file operation"))item->setEnabled(!store->redo.isEmpty());
    }
  });

}
}
