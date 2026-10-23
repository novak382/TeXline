#include "utils/FilePath.h"
#include "ResearchCollaboration.h"
#include "ResearchBuild.h"
#include "ResearchDependencies.h"
#include "ResearchWorkflow.h"
#include "ResearchAppearance.h"
#include "ResearchSettings.h"
#include "ResearchProject.h"
#include "TWApp.h"
#include "TeXDocumentWindow.h"
#include "PDFDocumentWindow.h"
#include <QAction>
#include <QApplication>
#include <QCryptographicHash>
#include <QDockWidget>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QDir>
#include <QMap>
#include <QSet>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPointer>
#include <QRegularExpression>
#include <QPushButton>
#include <QRunnable>
#include <QScrollBar>
#include <QTextBrowser>
#include <QTextCodec>
#include <QThreadPool>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QStatusBar>
#include <QUrl>
#include <QDialogButtonBox>
#include <QPlainTextEdit>
#include <QScreen>
#include <algorithm>

namespace Research {
namespace {
QString canonical(const QString&p){return Tw::Utils::canonicalSourcePath(p);}
QString normalized(QString s){s.replace(QStringLiteral("\r\n"),QStringLiteral("\n"));s.replace(QLatin1Char('\r'),QLatin1Char('\n'));if(s.startsWith(QChar::ByteOrderMark))s.remove(0,1);return s;}
QString excerpt(const QString &text,int start,int end) {
  start=qBound(0,start,int(text.size()));end=qBound(start,end,int(text.size()));
  int left=start,right=end;
  for(int n=0;n<3&&left>0;++n){const int p=text.lastIndexOf(QLatin1Char('\n'),left-1);left=p<0?0:p;}
  if(left&&text[left]==QLatin1Char('\n'))++left;
  for(int n=0;n<3;++n){const int p=text.indexOf(QLatin1Char('\n'),right);right=p<0?int(text.size()):p+1;if(right==text.size())break;}
  constexpr int limit=12000;
  left=qMax(left,start-limit/2);right=qMin(right,qMax(end,start)+limit/2);
  const QString changed=text.mid(start,qMin(end-start,limit)).toHtmlEscaped();
  return text.mid(left,start-left).toHtmlEscaped()+QStringLiteral("<span style=\"background:#dce9f8\">")+
    (changed.isEmpty()?QStringLiteral("&#8203;"):changed)+QStringLiteral("</span>")+
    (end-start>limit?QStringLiteral("…"):QString())+text.mid(end,qMax(0,right-end)).toHtmlEscaped();
}
QByteArray digest(const QByteArray&s){return QCryptographicHash::hash(s,QCryptographicHash::Sha256);}
struct Read {bool ok{false};QString text;QByteArray hash;};
Read read(const QString&path,const QByteArray&codecName){
  QFile f(path);if(!f.open(QIODevice::ReadOnly)||f.size()>16*1024*1024)return {};
  const QByteArray bytes=f.readAll();if(f.error()!=QFileDevice::NoError)return {};
  auto *codec=QTextCodec::codecForName(codecName.isEmpty()?QByteArray("UTF-8"):codecName);
  QTextCodec::ConverterState state;const QString text=codec?codec->toUnicode(bytes.constData(),bytes.size(),&state):QString::fromUtf8(bytes);
  if(state.invalidChars)return {};
  return {true,normalized(text),digest(bytes)};
}
}

namespace {
// A small disk-only graph. Workers never consult live QTextDocuments. The
// editor's existing per-file merge remains the sole writer of open buffers.
struct DiskVersion { Read content; bool missing{false}; };
struct ReviewEntry {
  QString file,before,after,category,kind;
  int line{1},offset{0},length{0},localIndex{-1};
  bool conflict{false},missing{false};
  QPointer<Collaboration> owner;
  QTextCursor anchor;
};
struct FileReview { QString base,after; QVector<ReviewEntry> entries; bool limited{false}; };
struct ProjectScan {
  QMap<QString,DiskVersion> files;
  QMap<QString,FileReview> changes;
  QStringList paths;
  bool limited{false};
};
bool sameVersion(const DiskVersion &a,const DiskVersion &b) {
  return a.missing==b.missing&&a.content.ok==b.content.ok&&a.content.text==b.content.text;
}
bool sameScan(const ProjectScan &a,const ProjectScan &b) {
  if(a.paths!=b.paths)return false;
  if(a.files.keys()!=b.files.keys())return false;
  for(auto it=a.files.cbegin();it!=a.files.cend();++it)if(!sameVersion(it.value(),b.files.value(it.key())))return false;
  return true;
}
QString reviewKind(const QString &file,const QString &text,int position) {
  if(QFileInfo(file).suffix().compare(QStringLiteral("bib"),Qt::CaseInsensitive)==0 ||
     QFileInfo(file).suffix().compare(QStringLiteral("bbl"),Qt::CaseInsensitive)==0)return QStringLiteral("参考文献");
  const QString code=maskProjectText(text).left(qBound(0,position,int(text.size())));
  static const QRegularExpression env(QStringLiteral(R"(\\(begin|end)\s*\{([^{}]+)\}|\\(\[|\]))"));
  static const QStringList math{QStringLiteral("equation"),QStringLiteral("equation*"),QStringLiteral("align"),QStringLiteral("align*"),QStringLiteral("alignat"),QStringLiteral("alignat*"),QStringLiteral("gather"),QStringLiteral("gather*"),QStringLiteral("multline"),QStringLiteral("multline*"),QStringLiteral("eqnarray"),QStringLiteral("eqnarray*")};
  QStringList stack;auto it=env.globalMatch(code);
  while(it.hasNext()){const auto m=it.next();const auto name=m.captured(2);
    if(m.captured(3)==QStringLiteral("["))stack.append(QStringLiteral("equation"));
    else if(m.captured(3)==QStringLiteral("]")){if(!stack.isEmpty()&&stack.last()==QStringLiteral("equation"))stack.removeLast();}
    else if(m.captured(1)==QStringLiteral("begin"))stack.append(name);
    else {const int p=stack.lastIndexOf(name);if(p>=0)while(stack.size()>p)stack.removeLast();}
  }
  for(auto i=stack.crbegin();i!=stack.crend();++i){if(math.contains(*i))return QStringLiteral("公式");if(*i==QStringLiteral("proof"))return QStringLiteral("证明");}
  return QStringLiteral("正文");
}
FileReview diskReview(const QString &path,const QString &before,const DiskVersion &after) {
  FileReview result;result.base=before;result.after=after.content.text;const QString &text=after.content.text;
  const auto edits=textEdits(before,text);result.limited=edits.size()>256;int shift=0;
  for(const auto &edit:edits){
    const int offset=edit.start+shift;QString kind=reviewKind(path,text,offset+edit.text.size());
    if(kind==QStringLiteral("正文"))kind=reviewKind(path,before,edit.end);
    if(kind==QStringLiteral("正文")){
      const QString changed=maskProjectText(before.mid(edit.start,edit.end-edit.start)+QLatin1Char('\n')+edit.text);
      static const QRegularExpression block(QStringLiteral(R"(\\begin\s*\{(proof|equation\*?|align(?:at)?\*?|gather\*?|multline\*?|eqnarray\*?)\}|\\\[)"));
      const auto m=block.match(changed);if(m.hasMatch())kind=m.captured(1)==QStringLiteral("proof")?QStringLiteral("证明"):QStringLiteral("公式");
    }
    result.entries.append({path,before.mid(edit.start,edit.end-edit.start),edit.text,
      changeCategory(text,offset),kind,text.left(offset).count(QLatin1Char('\n'))+1,offset,int(edit.text.size()),-1,false,after.missing,{}, {}});
    shift+=edit.text.size()-(edit.end-edit.start);
    if(result.entries.size()>=256)break;
  }
  if(after.missing&&result.entries.isEmpty())result.entries.append({path,before,{}, {},QStringLiteral("正文"),1,0,0,-1,false,true,{}, {}});
  return result;
}
ProjectScan scanUpdates(const QString &root,const QMap<QString,DiskVersion> &known,
                        const QMap<QString,QString> &pending,const QHash<QString,QByteArray> &codecs,const QStringList &openFiles={}) {
  ProjectScan result;QStringList todo{root};QSet<QString> seen;qsizetype bytes=0;
  constexpr qsizetype limit=8*1024*1024;
  static const QRegularExpression links(QStringLiteral(R"(\\(input|include|subfile)(?![A-Za-z@])\s*(?:\{([^{}]+)\}|([^\s%{}]+))|\\(?:bibliography|addbibresource)\s*(?:\[[^\]]*\]\s*)?\{([^{}]+)\})"));
  const QDir rootDir(QFileInfo(root).absolutePath());
  // Keep known paths during deletion or a changed include list, until review
  // acknowledges them. Never recursively scan unrelated project folders.
  todo.append(pending.keys());todo.append(openFiles);
  for(const auto &path:recordedDependencies(root)) {
    result.paths.append(path);
    if(textDependency(path))todo.append(path);
  }
  for(auto it=known.cbegin();it!=known.cend();++it)if(!it.value().missing&&!QFileInfo::exists(it.key()))todo.append(it.key());
  while(!todo.isEmpty()&&seen.size()<512){
    const QString file=canonical(todo.takeFirst());if(seen.contains(file))continue;seen.insert(file);
    if(!result.paths.contains(file))result.paths.append(file);
    DiskVersion version;version.missing=!QFileInfo::exists(file);
    if(version.missing)version.content.ok=true;
    else if(QFileInfo(file).size()>4*1024*1024){result.limited=true;continue;}
    else version.content=read(file,codecs.value(file,QByteArray("UTF-8")));
    if(!version.content.ok){result.limited=true;continue;}
    bytes+=version.content.text.size()*2;if(bytes>limit){result.limited=true;break;}
    result.files.insert(file,version);
    if(known.contains(file)&&!sameVersion(known.value(file),version)){
      const QString base=pending.contains(file)?pending.value(file):known.value(file).content.text;
      result.changes.insert(file,diskReview(file,base,version));
      result.limited|=result.changes.value(file).limited;
    }
    if(QFileInfo(file).suffix().compare(QStringLiteral("tex"),Qt::CaseInsensitive)!=0)continue;
    const QString code=maskProjectText(version.content.text);auto it=links.globalMatch(code);
    while(it.hasNext()){auto m=it.next();const bool bib=!m.captured(4).isEmpty();
      const QString value=bib?m.captured(4):(m.captured(2).isEmpty()?m.captured(3):m.captured(2));
      for(QString name: value.split(QLatin1Char(','))){name=name.trimmed();if(name.isEmpty()||name.contains(QLatin1Char('\\'))||name.contains(QLatin1Char('#')))continue;
        const QString suffix=bib?QStringLiteral(".bib"):QStringLiteral(".tex");
        QString chosen=resolveDependency(name,file,root,suffix);
        if(!chosen.isEmpty()&&!authoredDependency(chosen,root))continue;
        if(chosen.isEmpty())chosen=rootDir.filePath(name.endsWith(suffix,Qt::CaseInsensitive)?name:name+suffix);
        todo.append(chosen);
      }
    }
  }
  result.limited|=!todo.isEmpty();return result;
}
}

class ProjectUpdates final : public QObject {
public:
  explicit ProjectUpdates(const QString &path):QObject(qApp),root(path),watcher(this),timer(this) {
    setObjectName(QStringLiteral("researchProjectUpdates"));timer.setSingleShot(true);
    connect(&watcher,&QFileSystemWatcher::fileChanged,this,[this](const QString &){schedule();});
    connect(&watcher,&QFileSystemWatcher::directoryChanged,this,[this](const QString &){schedule();});
    connect(&timer,&QTimer::timeout,this,[this]{scan();});
  }
  QString root;
  QSet<Collaboration*> members;
  QMap<QString,DiskVersion> known;
  QMap<QString,FileReview> records;
  QFileSystemWatcher watcher;
  QTimer timer;
  ProjectScan candidate;
  bool busy{false},hasCandidate{false},limited{false};
  int epoch{0};QByteArray lastNotice;
  void schedule(){++epoch;hasCandidate=false;timer.start(600);}
  void accept(const QString &path,const QString &text){known.insert(path,{{true,normalized(text),{}},false});schedule();}
  void capture(Collaboration *owner) {
    const QString path=canonical(owner->window->fileName());FileReview record;record.base=owner->baseline;record.after=owner->window->textDoc()->editorText();
    for(int i=0;i<owner->items.size();++i){const auto &item=owner->items[i];
      record.entries.append({path,item.before,item.after,item.category,item.kind,
        item.range.blockNumber()+1,item.range.selectionStart(),item.range.selectionEnd()-item.range.selectionStart(),i,item.conflict,false,owner,item.range});}
    if(record.entries.isEmpty())records.remove(path);else records.insert(path,record);
    accept(path,owner->baseline);
  }
  QVector<ReviewEntry> entries() const {
    QVector<ReviewEntry> result;
    QStringList paths=records.keys();if(paths.removeAll(root))paths.prepend(root);
    for(const auto &path:paths){auto *w=TeXDocumentWindow::findDocument(path);auto *owner=w?w->collaboration():nullptr;
      if(owner&&!owner->items.isEmpty()){
        for(int i=0;i<owner->items.size();++i){const auto &item=owner->items[i];result.append({path,item.before,item.after,item.category,item.kind,
          item.range.blockNumber()+1,item.range.selectionStart(),item.range.selectionEnd()-item.range.selectionStart(),i,item.conflict,false,owner,item.range});}
      }else for(auto entry:records.value(path).entries){entry.localIndex=-1;if(entry.owner&&canonical(entry.owner->window->fileName())!=path)entry.owner=nullptr;if(!entry.owner)entry.anchor=QTextCursor();result.append(entry);}
      if(result.size()>=1024){result.resize(1024);break;}
    }
    return result;
  }
  QString description() const {
    const auto list=entries();QSet<QString> files;QMap<QString,int> kinds;int conflicts=0;
    for(const auto &e:list){files.insert(e.file);++kinds[e.kind];conflicts+=e.conflict;}
    QString text=ui(QStringLiteral("%1 个文件更新 · %2 处改动")).arg(files.size()).arg(list.size());
    for(const auto &kind:{QStringLiteral("公式"),QStringLiteral("证明"),QStringLiteral("参考文献")})if(kinds.value(kind))text+=QStringLiteral(" · ")+ui(QStringLiteral("%1 %2 处")).arg(ui(kind)).arg(kinds.value(kind));
    if(conflicts)text+=ui(QStringLiteral(" · %1 处需要合并")).arg(conflicts);
    bool partial=limited||list.size()>=1024;for(const auto &r:records)partial|=r.limited;
    if(partial)text+=ui(QStringLiteral(" · 摘要仅显示部分改动"));
    return text;
  }
  void refresh(bool notice=false) {
    const auto list=entries();QByteArray fingerprint;
    for(const auto &e:list){fingerprint+=e.file.toUtf8();fingerprint+=digest(e.before.toUtf8());fingerprint+=digest(e.after.toUtf8());fingerprint+=e.conflict?'1':'0';}
    for(auto *m:members){m->updateActions();if(notice)m->revisionTimer.start();if(m->panel&&m->panel->isVisible())m->showItem(false);}
    if(notice&&!list.isEmpty()&&fingerprint!=lastNotice){lastNotice=fingerprint;Collaboration *target=nullptr;
      for(auto *m:members){if(m->window==QApplication::activeWindow()){target=m;break;}if(!target||canonical(m->window->fileName())==root)target=m;}
      if(target)showTransientStatusMessage(target->window,description(),5000);
    }
  }
  void acknowledge(){
    const auto paths=records.keys();
    for(const auto &path:paths){auto *w=TeXDocumentWindow::findDocument(path);auto *c=w?w->collaboration():nullptr;
      if(c&&c->conflictCount())continue;
      const auto record=records.value(path);
      if(!retainReviewVersions(path,record.base,record.after,true)){
        for(auto *m:members)showTransientStatusMessage(m->window,ui(QStringLiteral("无法保存审核副本，请稍后重试。")),7000);
        continue;
      }
      if(c){c->items.clear();c->hasReviewOriginal=false;++c->reviewEpoch;c->updateActions();}records.remove(path);
    }
    if(records.isEmpty())lastNotice.clear();
    schedule();refresh();
  }
  void scan(){
    if(busy){timer.start(180);return;}busy=true;const int serial=epoch;
    QHash<QString,QByteArray> codecs;for(auto *w:TeXDocumentWindow::documentList())if(!w->untitled())codecs.insert(canonical(w->fileName()),w->getCurrentCodecName().toLatin1());
    const auto old=known;QMap<QString,QString> pending;for(auto it=records.cbegin();it!=records.cend();++it)pending.insert(it.key(),it.value().base);
    QStringList openFiles;for(auto *m:members)openFiles.append(canonical(m->window->fileName()));
    const QString path=root;QPointer<ProjectUpdates> receiver(this);
    QThreadPool::globalInstance()->start(QRunnable::create([receiver,serial,path,old,pending,codecs,openFiles]{
      const auto result=scanUpdates(path,old,pending,codecs,openFiles);
      QMetaObject::invokeMethod(qApp,[receiver,serial,result]{
        if(!receiver)return;
        auto *self=receiver.data();self->busy=false;
        if(serial!=self->epoch){self->timer.start(180);return;}
        if(!self->hasCandidate||!sameScan(self->candidate,result)){self->candidate=result;self->hasCandidate=true;self->timer.start(180);return;}
        self->hasCandidate=false;self->candidate={};self->limited=result.limited;
        for(auto it=result.changes.cbegin();it!=result.changes.cend();++it){
          auto *w=TeXDocumentWindow::findDocument(it.key());
          // The original receiver owns merging and conflict protection. A
          // deleted file is only reported; the editing buffer stays intact.
          if(w&&w->collaboration()&&!result.files.value(it.key()).missing){w->collaboration()->fileChanged();continue;}
          if(!retainReviewVersions(it.key(),it.value().base,it.value().after))self->limited=true;
          if(it.value().entries.isEmpty())self->records.remove(it.key());else self->records.insert(it.key(),it.value());
        }
        if(!result.limited)self->known=result.files;else for(auto it=result.files.cbegin();it!=result.files.cend();++it)self->known.insert(it.key(),it.value());
        QStringList watchedPaths=self->known.keys();for(const auto &path:result.paths)if(!watchedPaths.contains(path))watchedPaths.append(path);
        QStringList watches,folders;for(const auto &file:watchedPaths){if(QFileInfo(file).isFile())watches.append(file);const auto folder=QFileInfo(file).absolutePath();if(QFileInfo(folder).isDir()&&!folders.contains(folder))folders.append(folder);}
        const auto oldFiles=self->watcher.files(),oldDirs=self->watcher.directories();
        for(const auto &file:oldFiles)if(!watches.contains(file))self->watcher.removePath(file);
        for(const auto &folder:oldDirs)if(!folders.contains(folder))self->watcher.removePath(folder);
        for(const auto &file:watches)if(!oldFiles.contains(file))if(!self->watcher.addPath(file))self->limited=true;
        for(const auto &folder:folders)if(!oldDirs.contains(folder))if(!self->watcher.addPath(folder))self->limited=true;
        self->refresh(true);
      },Qt::QueuedConnection);
    }));
  }
};
namespace {QMap<QString,ProjectUpdates*> updateProjects;}
Collaboration::~Collaboration(){if(project){project->members.remove(this);if(project->members.isEmpty()){updateProjects.remove(project->root);project->deleteLater();}}}
void Collaboration::attachProject(){
  if(window->untitled())return;
  const QString path=canonical(window->fileName()),root=canonical(window->textDoc()->getRootFilePath());
  ProjectUpdates *target=updateProjects.value(root);
  if(!target)for(auto *p:updateProjects)if(p->known.contains(path)){target=p;break;}
  if(!target){target=new ProjectUpdates(root);updateProjects.insert(root,target);}
  if(project!=target){if(project){project->members.remove(this);if(project->members.isEmpty()){updateProjects.remove(project->root);project->deleteLater();}}project=target;project->members.insert(this);}
  project->accept(path,baseline);
}

Collaboration::Collaboration(TeXDocumentWindow *w):QObject(w),window(w) {
  setDependencyBinaryPaths(TWApp::instance()->getBinaryPaths());
  setObjectName(QStringLiteral("researchCollaboration"));
  connect(TWApp::instance(), &TWApp::updatedTranslators, this, [this] {
    updateActions(); refreshPdfState(); if (panel) showItem(false);
    retranslateWorkspace(window);
  });
  reviewAction=new QAction(this);reviewAction->setObjectName(QStringLiteral("researchExternalReview"));reviewAction->setVisible(false);
  connect(reviewAction,&QAction::triggered,this,&Collaboration::review);

  if(auto *bar=w->findChild<QToolBar*>(QStringLiteral("toolBar_run"))) {
    auto *compile=w->findChild<QAction*>(QStringLiteral("actionTypeset"));
    bar->insertAction(compile,reviewAction);
    if(auto *button=qobject_cast<QToolButton*>(bar->widgetForAction(reviewAction))) {
      configureButton(button,ButtonRole::Status);
      connect(reviewAction,&QAction::changed,button,[button](){configureButton(button,ButtonRole::Status);});
    }
    if(auto *more=w->findChild<QMenu*>(QStringLiteral("researchFileMenu"))) {
      auto *review=more->addAction(Research::ui(QStringLiteral("查看外部更新…")),this,&Collaboration::review);review->setObjectName(QStringLiteral("researchReviewUpdates"));
      auto *publish=more->addAction(Research::ui(QStringLiteral("更新共享 PDF")),w,[w](){QString error;const QString root=w->textDoc()->getRootFilePath();
        if(publishPreview(root,&error))Research::showTransientStatusMessage(w, Research::ui(QStringLiteral("共享 PDF 已更新到论文目录；上传由同步软件处理。")),7000);
        else Research::showTransientStatusMessage(w, ui(error),7000);
      });
      publish->setObjectName(QStringLiteral("researchPublishPdf"));
    }
  }
  debounce.setSingleShot(true);debounce.setInterval(180);connect(&debounce,&QTimer::timeout,this,&Collaboration::readAsync);
  revisionTimer.setSingleShot(true);revisionTimer.setInterval(350);connect(&revisionTimer,&QTimer::timeout,this,&Collaboration::refreshPdfState);
  connect(w->editor(),&QTextEdit::textChanged,this,[this](){if(!applying)revisionTimer.start();});
}
void Collaboration::acceptedDisk(const QString &text) {
  const QPointer<ProjectUpdates> previousProject=project;const bool reviewing=panel&&panel->isVisible();
  ++generation;++reviewEpoch;hasReviewOriginal=false;reading=false;hasBaseline=true;baseline=normalized(text);conflictBase.clear();pendingRemote.clear();items.clear();hasCandidate=false;readRetries=0;
  debounce.stop();expectedSaveHash.clear();if(panel)panel->hide();updateActions();revisionTimer.start();
  attachProject();
  // Saving a reviewed chapter must keep the remaining project review at hand.
  if(reviewing&&previousProject==project&&!project->entries().isEmpty()){panel->show();showItem(false);}
}
void Collaboration::fileChanged(){if(!window->untitled()&&hasBaseline)debounce.start();revisionTimer.start();}
void Collaboration::buildFinished(){if(project)project->schedule();revisionTimer.start();}
void Collaboration::readAsync() {
  if(reading){debounce.start();return;}if(window->untitled()||!hasBaseline)return;
  reading=true;const int serial=generation;const QString path=window->textDoc()->absoluteFilePath();const QByteArray codec=window->getCurrentCodecName().toLatin1();
  const QPointer<Collaboration> receiver(this);
  QThreadPool::globalInstance()->start(QRunnable::create([receiver,serial,path,codec](){
    const Read result=read(path,codec);
    QMetaObject::invokeMethod(qApp,[receiver,serial,result](){
      if(!receiver||receiver->generation!=serial)return;
      auto *self=receiver.data();self->reading=false;
      if(!result.ok){if(++self->readRetries<5)self->debounce.start(400);else Research::showTransientStatusMessage(self->window, Research::ui(QStringLiteral("暂时无法读取文件，请稍后重试。")),7000);return;}
      self->readRetries=0;
      if(!self->hasCandidate||self->stableCandidate!=result.text){self->hasCandidate=true;self->stableCandidate=result.text;self->debounce.start(120);return;}
      self->hasCandidate=false;self->receive(result.text);
      QMetaObject::invokeMethod(self->window,"setupFileWatcher",Qt::QueuedConnection);
    },Qt::QueuedConnection);
  }));
}
int Collaboration::conflictCount() const {int count=0;for(const auto&i:items)if(i.conflict)++count;return count;}
void Collaboration::applyText(const QString &text,bool dirty) {
  const QString old=window->textDoc()->editorText();if(old==text){window->setModified(dirty);return;}
  recordRevision(window,old);checkpoint(window);
  auto *edit=window->editor();QTextCursor selection=edit->textCursor(),top=edit->cursorForPosition(QPoint(0,0));
  const int oldTopY=edit->cursorRect(top).top(),oldY=edit->verticalScrollBar()->value(),oldX=edit->horizontalScrollBar()->value();
  const auto patches=textEdits(old,text);QTextCursor cursor(edit->document());applying=true;cursor.beginEditBlock();
  for(auto it=patches.crbegin();it!=patches.crend();++it){cursor.setPosition(it->start);cursor.setPosition(it->end,QTextCursor::KeepAnchor);cursor.insertText(it->text);}
  cursor.endEditBlock();edit->setTextCursor(selection);
  edit->verticalScrollBar()->setValue(oldY+edit->cursorRect(top).top()-oldTopY);edit->horizontalScrollBar()->setValue(oldX);
  window->setModified(dirty);applying=false;checkpoint(window);revisionTimer.start();
}
void Collaboration::receive(const QString &external) {
  const QString remote=normalized(external);if(!hasBaseline){acceptedDisk(remote);return;}if(remote==baseline)return;
  // Supersede any already queued read of the previous disk version. Its GUI
  // callback must not treat an old stable candidate as a fresh external edit.
  ++generation;++reviewEpoch;reading=false;hasCandidate=false;readRetries=0;debounce.stop();
  const QString local=window->textDoc()->editorText(),base=baseline;
  if(!hasReviewOriginal){reviewOriginal=local;hasReviewOriginal=true;}
  if(!retainReviewVersions(window->fileName(),reviewOriginal,remote))showTransientStatusMessage(window,ui(QStringLiteral("无法保存审核副本，请稍后重试。")),7000);
  const auto merged=mergeText(base,local,remote);
  const auto incoming=textEdits(local,merged.text);
  // Compose received changes against their original, unreviewed alternatives.
  // This is a review-only buffer: the real three-way merge still uses the
  // current local text, so local drafts never become remote edits.
  QString reviewBefore=local;QVector<TextEdit> rewind;QVector<Item> retained;
  const auto remoteDifferences=textEdits(local,remote);
  auto overlaps=[](int start,int end,int a,int b){
    if(start==end&&a==b)return start==a;
    if(a==b)return start<a&&a<end;
    if(start==end)return a<=start&&start<=b;
    return start<b&&a<end;
  };
  for(const auto &item:items) {
    const int start=item.range.selectionStart(),end=item.range.selectionEnd();
    if(!item.conflict&&local.mid(start,end-start)==item.after)rewind.append({start,end,item.before});
    else {
      bool touched=false;
      int mappedStart=start,mappedEnd=end;
      for(const auto &edit:incoming)if(overlaps(start,end,edit.start,edit.end)){touched=true;break;}
      for(const auto &edit:incoming){const int shift=edit.text.size()-(edit.end-edit.start);if(edit.end<=start)mappedStart+=shift;if(edit.end<=end)mappedEnd+=shift;}
      for(const auto &conflict:merged.conflicts)if(overlaps(mappedStart,mappedEnd,conflict.start,conflict.end)){touched=true;break;}
      bool stillDifferent=!item.conflict;
      if(item.conflict)for(const auto &edit:remoteDifferences)if(overlaps(start,end,edit.start,edit.end)){stillDifferent=true;break;}
      if(!touched&&stillDifferent)retained.append(item);
    }
  }
  std::sort(rewind.begin(),rewind.end(),[](const TextEdit &a,const TextEdit &b){return a.start>b.start;});
  for(const auto &edit:rewind)reviewBefore.replace(edit.start,edit.end-edit.start,edit.text);
  const auto changes=textEdits(reviewBefore,merged.text);
  // Retain the external alternative in local history, including conflicts
  // whose text is deliberately kept out of the editing buffer.
  recordRevision(window,remote);
  applyText(merged.text,merged.text!=remote);
  items=std::move(retained);index=0;
  for(const auto &change:changes){
    int offset=0;for(const auto &prior:changes)if(prior.start<change.start)offset+=prior.text.size()-(prior.end-prior.start);
    QTextCursor range(window->textDoc());range.setPosition(change.start+offset);range.setPosition(change.start+offset+change.text.size(),QTextCursor::KeepAnchor);
    QString kind=reviewKind(window->fileName(),merged.text,range.position());if(kind==QStringLiteral("正文"))kind=reviewKind(window->fileName(),reviewBefore,change.end);
    items.append({range,reviewBefore.mid(change.start,change.end-change.start),change.text,changeCategory(merged.text,range.position()),false,kind});
  }
  for(const auto &conflict:merged.conflicts){QTextCursor range(window->textDoc());range.setPosition(conflict.start);range.setPosition(conflict.end,QTextCursor::KeepAnchor);items.append({range,conflict.local,conflict.remote,changeCategory(merged.text,conflict.start),true,reviewKind(window->fileName(),merged.text,range.position())});}
  std::stable_sort(items.begin(),items.end(),[](const Item &a,const Item &b){return a.range.selectionStart()<b.range.selectionStart();});
  if(!merged.conflicts.isEmpty()){conflictBase=base;pendingRemote=remote;}else{conflictBase.clear();pendingRemote.clear();}
  baseline=remote;if(items.isEmpty())hasReviewOriginal=false;updateActions();if(panel&&panel->isVisible())showItem(false);revisionTimer.start();
  if(project){project->capture(this);project->refresh();}
}
bool Collaboration::beforeSave(const QString &path) {
  expectedSaveHash.clear();
  if(window->untitled()||canonical(path)!=canonical(window->textDoc()->absoluteFilePath()))return true;
  const Read latest=read(path,window->getCurrentCodecName().toLatin1());
  if(!latest.ok){saveFailure(window,ui(QStringLiteral("无法读取磁盘版本，请重试或另存为。")));return false;}
  receive(latest.text);expectedSaveHash=latest.hash;
  if(conflictCount()){review();saveFailure(window,ui(QStringLiteral("请先解决冲突再保存。")));return false;}
  return true;
}
bool Collaboration::verifySave(const QString &path) {
  if(expectedSaveHash.isEmpty())return true;
  const Read latest=read(path,window->getCurrentCodecName().toLatin1());
  if(!latest.ok||latest.hash!=expectedSaveHash){fileChanged();saveFailure(window,ui(QStringLiteral("文件在保存时已更改，请核对后重试。")));return false;}
  return true;
}
void Collaboration::updateActions(){
  if(panel){if(auto *all=panel->findChild<QPushButton*>(QStringLiteral("researchUseAllRemote"))){all->setText(englishUi()?QStringLiteral("Accept all"):Research::ui(QStringLiteral("全部采用")));configureButton(all);}for(const auto &pair:{qMakePair(QStringLiteral("researchReviewPrevious"),ui(QStringLiteral("上一处"))),qMakePair(QStringLiteral("researchReviewNext"),ui(QStringLiteral("下一处")))})if(auto *button=panel->findChild<QToolButton*>(pair.first)){button->setToolTip(pair.second);button->setAccessibleName(pair.second);}}

  const auto list=project?project->entries():QVector<ReviewEntry>{};int conflicts=0;for(const auto &e:list)conflicts+=e.conflict;
  const int count=project?list.size():items.size();if(!project)conflicts=conflictCount();
  reviewAction->setText(conflicts?ui(QStringLiteral("需要合并 · %1 处")).arg(conflicts):ui(QStringLiteral("外部更新 · %1 处")).arg(count));reviewAction->setVisible(count>0);
  if(panel)if(auto *done=panel->findChild<QAction *>(QStringLiteral("researchReviewDone"))){done->setEnabled(conflicts==0);done->setToolTip(conflicts?ui(QStringLiteral("请先解决冲突。")):QString());}
}
void Collaboration::review() {
  if(project?project->entries().isEmpty():items.isEmpty()){Research::showTransientStatusMessage(window, Research::ui(QStringLiteral("目前没有待审阅的外部更新。")),4000);return;}
  if(!panel){
    panel=new QDockWidget(Research::ui(QStringLiteral("外部更新")),window);panel->setObjectName(QStringLiteral("researchReviewPanel"));panel->setAllowedAreas(Qt::BottomDockWidgetArea);panel->setFeatures(QDockWidget::DockWidgetClosable);
    auto *title=new QWidget(panel);title->setFixedHeight(0);title->setStyleSheet(QStringLiteral("background: #f5f5f7;"));panel->setTitleBarWidget(title);
    auto *body=new QWidget(panel);body->setObjectName(QStringLiteral("researchReviewBody"));body->setFont(smoothTextFont(QFont(QStringLiteral("Microsoft YaHei UI"),10)));auto *layout=new QVBoxLayout(body);auto *row=new QHBoxLayout;
    layout->setContentsMargins(ControlMetrics::Margin,ControlMetrics::Margin,ControlMetrics::Margin,ControlMetrics::Margin);layout->setSpacing(ControlMetrics::Gap);row->setSpacing(ControlMetrics::Gap);
    // One quiet row: location, compact navigation, two explicit decisions.
    // Secondary operations remain reachable without occupying writing space.
    summary=new QLabel(body);summary->setObjectName(QStringLiteral("researchProjectUpdateSummary"));summary->hide();
    heading=new QLabel(body);heading->setMinimumWidth(0);heading->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);row->addWidget(heading,1);
    auto *previous=new QToolButton(body),*next=new QToolButton(body);
    previous->setObjectName(QStringLiteral("researchReviewPrevious"));next->setObjectName(QStringLiteral("researchReviewNext"));
    previous->setArrowType(Qt::LeftArrow);next->setArrowType(Qt::RightArrow);
    for(auto *button:{previous,next}){configureButton(button,ButtonRole::Icon);row->addWidget(button);}
    previous->setToolTip(englishUi()?QStringLiteral("Previous change"):Research::ui(QStringLiteral("上一处")));next->setToolTip(englishUi()?QStringLiteral("Next change"):Research::ui(QStringLiteral("下一处")));
    previous->setAccessibleName(previous->toolTip());next->setAccessibleName(next->toolTip());
    theirs=new QPushButton(body);theirs->setObjectName(QStringLiteral("researchUseRemote"));
    auto *all=new QPushButton(englishUi()?QStringLiteral("Accept all"):Research::ui(QStringLiteral("全部采用")),body);all->setObjectName(QStringLiteral("researchUseAllRemote"));
    all->setToolTip(englishUi()?QStringLiteral("Use every pending external change, including conflicts. Local recovery copies are retained."):Research::ui(QStringLiteral("采用所有待审阅的外部修改（含冲突），保留本地恢复副本。")));
    for(auto *button:{theirs,all}){configureButton(button);row->addWidget(button);}
    auto *more=new QToolButton(body);more->setObjectName(QStringLiteral("researchReviewMore"));more->setIcon(QIcon(QStringLiteral(":/research/icons/more.svg")));more->setText(QStringLiteral("⋯"));more->setToolButtonStyle(Qt::ToolButtonTextOnly);more->setAccessibleName(ui(QStringLiteral("其他操作")));configureButton(more,ButtonRole::Icon);
    auto *menu=new QMenu(more);more->setMenu(menu);more->setPopupMode(QToolButton::InstantPopup);more->setToolButtonStyle(Qt::ToolButtonIconOnly);more->setStyleSheet(more->styleSheet()+QStringLiteral("QToolButton::menu-indicator {image:none;width:0px;height:0px;}"));row->addWidget(more);
    mine=menu->addAction(QString());mine->setObjectName(QStringLiteral("researchKeepLocal"));
    merge=menu->addAction(ui(QStringLiteral("合并编辑器…")));merge->setObjectName(QStringLiteral("researchEditMerge"));
    auto *done=menu->addAction(ui(QStringLiteral("确认剩余修改")));done->setObjectName(QStringLiteral("researchReviewDone"));
    menu->addSeparator();auto *collapse=menu->addAction(ui(QStringLiteral("收起")));collapse->setObjectName(QStringLiteral("researchReviewCollapse"));
    connect(collapse,&QAction::triggered,panel,&QDockWidget::hide);layout->addLayout(row);
    preview=new QTextBrowser(body);preview->setObjectName(QStringLiteral("researchReviewText"));preview->setOpenLinks(false);preview->setFont(window->editor()->font());layout->addWidget(preview);
    panel->setWidget(body);window->addDockWidget(Qt::BottomDockWidgetArea,panel);panel->setMinimumHeight(180);
    connect(previous,&QToolButton::clicked,this,[this](){stepReview(-1);});
    connect(next,&QToolButton::clicked,this,[this](){stepReview(1);});
    auto resolve=[this](bool external){int selected=0;if(auto *owner=selectedOwner(selected)){const auto &item=owner->items[selected];const QString current=item.range.selectedText().replace(QChar::ParagraphSeparator,QLatin1Char('\n'));owner->applyChoice(selected,external?item.after:(!item.conflict&&current==item.after?item.before:current));}};
    connect(mine,&QAction::triggered,this,[resolve](){resolve(false);});connect(theirs,&QPushButton::clicked,this,[resolve](){resolve(true);});
    connect(all,&QPushButton::clicked,this,&Collaboration::useAllExternal);
    connect(merge,&QAction::triggered,this,&Collaboration::editMerge);
    connect(done,&QAction::triggered,this,&Collaboration::finishReview);
    connect(preview,&QTextBrowser::anchorClicked,this,[this](const QUrl &url){if(url.scheme()!=QStringLiteral("research-change"))return;bool ok=false;const int selected=url.path().mid(1).toInt(&ok);if(ok){index=selected;showItem();}});
  }
  updateActions();panel->show();window->resizeDocks({panel},{210},Qt::Vertical);showItem();
}
void Collaboration::showItem(bool navigate) {
  if(project){
    const auto list=project->entries();if(list.isEmpty()){if(panel)panel->hide();return;}index=qBound(0,index,int(list.size()-1));auto entry=list[index];
    summary->setText(project->description());
    const QDir folder(QFileInfo(project->root).absolutePath());const QString file=folder.relativeFilePath(entry.file);
    heading->setText(QStringLiteral("%1 / %2 · %3").arg(index+1).arg(list.size()).arg(file));heading->setMinimumWidth(0);heading->setWordWrap(false);heading->setToolTip(heading->text()+QLatin1Char('\n')+project->description());
    const bool actionable=!entry.missing&&entry.owner&&entry.localIndex>=0;
    mine->setVisible(actionable);theirs->setVisible(actionable);merge->setVisible(actionable);
    QString links;QSet<QString> seen;
    for(int n=0;n<list.size();++n)if(!seen.contains(list[n].file)){seen.insert(list[n].file);const auto name=folder.relativeFilePath(list[n].file).toHtmlEscaped();
      links+=QStringLiteral("<a href=\"research-change:/%1\">%2</a>&nbsp;&nbsp; ").arg(n).arg(name);}
    QString text=entry.owner?entry.owner->window->textDoc()->editorText():project->records.value(entry.file).after;
    int start=entry.offset,end=entry.offset+entry.length;
    if(entry.owner&&!entry.anchor.isNull()){start=entry.anchor.selectionStart();end=entry.anchor.selectionEnd();}
    const QString current=text.mid(start,end-start),before=entry.conflict?current:entry.before;
    mine->setText(ui(!entry.conflict&&current==entry.after?QStringLiteral("保留原文"):QStringLiteral("保留当前")));
    theirs->setText(englishUi()?QStringLiteral("Accept change"):Research::ui(QStringLiteral("采用此处")));configureButton(theirs);
    QString beforeText=text,afterText=text;beforeText.replace(start,end-start,before);afterText.replace(start,end-start,entry.after);
    const QString context=entry.missing?ui(QStringLiteral("文件已删除；原有内容保留在审核中。")):ui(QStringLiteral("%1 · 第 %2 行")).arg(ui(entry.kind)).arg(entry.line);
    preview->setHtml(QStringLiteral("<p style=\"font-family:'Microsoft YaHei UI';font-size:10pt;margin:0\">%1</p><p style=\"font-family:'Microsoft YaHei UI';font-size:10pt;margin:4px 0\">%2</p><table width=\"100%\" cellspacing=\"0\" cellpadding=\"4\"><tr><td width=\"50%\" valign=\"top\"><p style=\"font-family:'Microsoft YaHei UI';font-size:10pt;margin:0\">%3</p><pre style=\"font-family:'JetBrains Mono NL';font-size:11pt;margin:4px 0\">%4</pre></td><td width=\"50%\" valign=\"top\"><p style=\"font-family:'Microsoft YaHei UI';font-size:10pt;margin:0\">%5</p><pre style=\"font-family:'JetBrains Mono NL';font-size:11pt;margin:4px 0\">%6</pre></td></tr></table>")
      .arg(links,context.toHtmlEscaped(),entry.conflict?ui(QStringLiteral("当前编辑")):ui(QStringLiteral("更新前")),excerpt(beforeText,start,start+before.size()),entry.conflict?ui(QStringLiteral("外部版本")):ui(QStringLiteral("更新后")),excerpt(afterText,start,start+entry.after.size())));
    if(!navigate)return;
    if(entry.missing||!QFileInfo::exists(entry.file)){showTransientStatusMessage(window,ui(QStringLiteral("文件已删除；原有内容保留在审核中。")),5000);return;}
    auto *target=TeXDocumentWindow::findDocument(entry.file);if(!target)target=TeXDocumentWindow::openDocument(entry.file,true,true);
    if(!target)return;
    QTextCursor range;
    if(entry.owner&&!entry.anchor.isNull())range=entry.anchor;
    else if(normalized(target->textDoc()->editorText())==project->records.value(entry.file).after){
      auto *receiver=target->collaboration();const auto stored=project->records.value(entry.file);
      if(!retainReviewVersions(entry.file,stored.base,stored.after)||!recordRevision(target,stored.base)||!recordRevision(target,stored.after)){showTransientStatusMessage(target,ui(QStringLiteral("无法保存审核副本，请稍后重试。")),7000);return;}
      receiver->reviewOriginal=stored.base;receiver->hasReviewOriginal=true;
      if(receiver->items.isEmpty())for(const auto &e:stored.entries){
        QTextCursor anchor(target->textDoc());const int last=target->textDoc()->characterCount()-1;anchor.setPosition(qBound(0,e.offset,last));anchor.setPosition(qBound(0,e.offset+e.length,last),QTextCursor::KeepAnchor);
        receiver->items.append({anchor,e.before,e.after,e.category,e.conflict,e.kind});
      }
      ++receiver->reviewEpoch;project->capture(receiver);project->refresh();
      range=QTextCursor(target->textDoc());const int end=target->textDoc()->characterCount()-1;range.setPosition(qBound(0,entry.offset,end));range.setPosition(qBound(0,entry.offset+entry.length,end),QTextCursor::KeepAnchor);
    }else {showTransientStatusMessage(window,ui(QStringLiteral("文件位置已变化；请先核对当前内容。")),5000);return;}
    target->editor()->setTextCursor(range);target->editor()->ensureCursorVisible();
    if(target!=window){
      auto *destination=target->collaboration();
      if(destination&&destination->project==project){
        // Keep the review controls beside the selected change. Each panel
        // stays owned by its window, so closing a chapter cannot orphan it.
        panel->hide();destination->index=index;destination->review();
      }
    }
    target->show();target->raise();target->activateWindow();return;
  }
  if(items.isEmpty()){if(panel)panel->hide();return;}index=qBound(0,index,int(items.size()-1));const auto &item=items[index];
  if(summary)summary->clear();
  QString category=item.category;
  static const QRegularExpression categoryLine(QStringLiteral("^(.*) · 第 ([0-9]+) 行$"));
  const auto match=categoryLine.match(category);
  if(match.hasMatch())category=(match.captured(1)==QStringLiteral("正文")?ui(QStringLiteral("正文")):match.captured(1))+ui(QStringLiteral(" · 第 %1 行")).arg(match.captured(2));
  heading->setText(QStringLiteral("%1 / %2 · %3%4").arg(index+1).arg(items.size()).arg(category,item.conflict?Research::ui(QStringLiteral(" · 需要合并")):QString()));
  mine->setVisible(true);theirs->setVisible(true);merge->setVisible(true);
  const QString current=item.range.selectedText().replace(QChar::ParagraphSeparator,QLatin1Char('\n'));
  mine->setText(ui(!item.conflict&&current==item.after?QStringLiteral("保留原文"):QStringLiteral("保留当前")));
  theirs->setText(englishUi()?QStringLiteral("Accept change"):Research::ui(QStringLiteral("采用此处")));configureButton(theirs);
  const QString before=item.conflict?item.range.selectedText().replace(QChar::ParagraphSeparator,QLatin1Char('\n')):item.before;
  preview->setHtml(QStringLiteral("<p style=\"font-family:'Microsoft YaHei UI';font-size:10pt\">%1</p><pre style=\"font-family:'JetBrains Mono NL';font-size:11pt\">%2</pre><p style=\"font-family:'Microsoft YaHei UI';font-size:10pt\">%3</p><pre style=\"font-family:'JetBrains Mono NL';font-size:11pt\">%4</pre>").arg(item.conflict?Research::ui(QStringLiteral("当前编辑")):Research::ui(QStringLiteral("更新前")),before.toHtmlEscaped(),item.conflict?Research::ui(QStringLiteral("外部版本")):Research::ui(QStringLiteral("更新后")),item.after.toHtmlEscaped()));
  if(navigate){window->editor()->setTextCursor(item.range);window->editor()->ensureCursorVisible();}
}
void Collaboration::stepReview(int direction){const int count=project?project->entries().size():items.size();if(count){index=(index+direction+count)%count;showItem();}}
void Collaboration::finishReview(){
  bool conflicts=conflictCount()>0;if(project)for(const auto &e:project->entries())conflicts|=e.conflict;
  if(conflicts){showTransientStatusMessage(window,ui(QStringLiteral("请先解决冲突。")));return;}
  if(project)project->acknowledge();
  else if(!conflicts){items.clear();updateActions();}
  if(conflicts)showTransientStatusMessage(window,ui(QStringLiteral("还有未解决的冲突。")),5000);
  if(panel)panel->hide();
}
void Collaboration::useAllExternal() {
  // Validate and retain every file before changing any editing buffer. The
  // operation never saves a manuscript and never opens untouched chapter tabs.
  struct Pending {Collaboration *owner;QString original;QTextCursor cursor;int scroll;};
  QVector<Pending> pending;QSet<Collaboration*> owners;
  if(project){for(auto *owner:project->members)if(!owner->items.isEmpty())owners.insert(owner);}else if(!items.isEmpty())owners.insert(this);
  auto changed=[this]{showTransientStatusMessage(window,englishUi()?QStringLiteral("The external version changed again. Check the refreshed review and retry."):Research::ui(QStringLiteral("外部版本又有更新，请核对后重试。")),6000);};
  for(auto *owner:owners){const auto latest=read(owner->window->fileName(),owner->window->getCurrentCodecName().toLatin1());if(!latest.ok){saveFailure(window,ui(QStringLiteral("无法读取磁盘版本，请重试或另存为。")));return;}if(latest.text!=owner->baseline){owner->receive(latest.text);changed();return;}
    if(owner->window->editor()->isReadOnly()){changed();return;}pending.append({owner,owner->window->textDoc()->editorText(),owner->window->textCursor(),owner->window->editor()->verticalScrollBar()->value()});}
  if(project)for(auto it=project->records.cbegin();it!=project->records.cend();++it){if(TeXDocumentWindow::findDocument(it.key()))continue;const bool missing=!QFileInfo::exists(it.key());const auto latest=missing?Read{}:read(it.key(),QByteArray("UTF-8"));if((!missing&&(!latest.ok||latest.text!=it.value().after))||(missing&&!it.value().after.isEmpty())){project->schedule();changed();return;}}
  for(const auto &entry:pending){auto *owner=entry.owner;QString result=entry.original;for(int i=owner->items.size()-1;i>=0;--i){const auto &item=owner->items[i];result.replace(item.range.selectionStart(),item.range.selectionEnd()-item.range.selectionStart(),item.after);}
    if(!retainReviewVersions(owner->window->fileName(),owner->hasReviewOriginal?owner->reviewOriginal:entry.original,owner->baseline)||!recordRevision(owner->window,entry.original)||!recordRevision(owner->window,result)){showTransientStatusMessage(window,ui(QStringLiteral("无法保存审核副本，请稍后重试。")),7000);return;}}
  if(project)for(auto it=project->records.cbegin();it!=project->records.cend();++it)if(!retainReviewVersions(it.key(),it.value().base,it.value().after,true)){showTransientStatusMessage(window,ui(QStringLiteral("无法保存审核副本，请稍后重试。")),7000);return;}
  for(const auto &entry:pending){const auto latest=read(entry.owner->window->fileName(),entry.owner->window->getCurrentCodecName().toLatin1());if(!latest.ok||latest.text!=entry.owner->baseline||entry.owner->window->textDoc()->editorText()!=entry.original){if(latest.ok)entry.owner->receive(latest.text);changed();return;}}
  for(const auto &entry:pending){auto *owner=entry.owner;auto cursor=QTextCursor(owner->window->textDoc());cursor.beginEditBlock();for(int i=owner->items.size()-1;i>=0;--i){const auto item=owner->items[i];if(item.range.selectedText().replace(QChar::ParagraphSeparator,QLatin1Char('\n'))==item.after)continue;cursor.setPosition(item.range.selectionStart());cursor.setPosition(item.range.selectionEnd(),QTextCursor::KeepAnchor);cursor.insertText(item.after);}cursor.endEditBlock();owner->items.clear();owner->hasReviewOriginal=false;owner->conflictBase.clear();owner->pendingRemote.clear();++owner->reviewEpoch;owner->window->setModified(owner->window->textDoc()->editorText()!=owner->baseline);owner->window->editor()->setTextCursor(entry.cursor);owner->window->editor()->verticalScrollBar()->setValue(entry.scroll);checkpoint(owner->window);owner->revisionTimer.start();owner->updateActions();if(owner->panel)owner->panel->hide();}
  if(project){project->records.clear();project->lastNotice.clear();project->schedule();project->refresh();for(auto *owner:project->members)if(owner->panel)owner->panel->hide();}
  else {updateActions();if(panel)panel->hide();}
  showTransientStatusMessage(window,englishUi()?QStringLiteral("All external changes accepted. Recovery copies retained."):Research::ui(QStringLiteral("已采用全部外部修改，恢复副本已保留。")));
}
Collaboration *Collaboration::selectedOwner(int &selected) {
  if(!project){selected=index;return index>=0&&index<items.size()?this:nullptr;}
  const auto list=project->entries();if(index<0||index>=list.size())return nullptr;const auto entry=list[index];
  selected=entry.localIndex;return entry.owner&&selected>=0&&selected<entry.owner->items.size()?entry.owner.data():nullptr;
}
bool Collaboration::applyChoice(int selected,const QString &replacement) {
  if(selected<0||selected>=items.size())return false;
  const auto item=items[selected];const QString current=window->textDoc()->editorText();
  const int start=item.range.selectionStart(),end=item.range.selectionEnd();
  QString external=current,original=current;external.replace(start,end-start,item.after);original.replace(start,end-start,item.before);
  if(!retainReviewVersions(window->fileName(),hasReviewOriginal?reviewOriginal:current,baseline)||!recordRevision(window,current)||!recordRevision(window,external)||!recordRevision(window,original)){
    showTransientStatusMessage(window,ui(QStringLiteral("无法保存审核副本，请稍后重试。")),7000);return false;
  }
  if(current.mid(start,end-start)!=replacement){auto cursor=item.range;cursor.beginEditBlock();cursor.insertText(replacement);cursor.endEditBlock();window->editor()->setTextCursor(cursor);}
  items.removeAt(selected);++reviewEpoch;index=qBound(0,index,qMax(0,int(items.size())-1));
  if(items.isEmpty())hasReviewOriginal=false;
  if(!conflictCount()){conflictBase.clear();pendingRemote.clear();window->setModified(window->textDoc()->editorText()!=baseline);}
  else window->setModified(true);
  checkpoint(window);updateActions();if(panel)showItem();revisionTimer.start();
  if(project){project->capture(this);project->refresh();}
  return true;
}
void Collaboration::resolveCurrent(bool useExternal) {
  if(index<0||index>=items.size())return;const auto item=items[index];
  const QString current=item.range.selectedText().replace(QChar::ParagraphSeparator,QLatin1Char('\n'));
  applyChoice(index,useExternal?item.after:(!item.conflict&&current==item.after?item.before:current));
}
void Collaboration::editMerge() {
  int selected=0;auto *owner=selectedOwner(selected);if(!owner)return;
  const auto item=owner->items[selected];const QString source=owner->window->textDoc()->editorText();
  const int start=item.range.selectionStart(),end=item.range.selectionEnd(),revision=owner->window->textDoc()->revision();
  const quint64 epoch=owner->reviewEpoch;QPointer<Collaboration> receiver(owner);
  QPointer<MaterialDialog> dialog=new MaterialDialog(owner->window);dialog->setObjectName(QStringLiteral("researchMergeDialog"));dialog->setWindowTitle(ui(QStringLiteral("合并编辑器")));
  auto *layout=new QVBoxLayout(dialog);auto *context=new QTextBrowser(dialog);context->setObjectName(QStringLiteral("researchMergeContext"));context->setOpenLinks(false);context->setFont(owner->window->editor()->font());
  context->document()->setDefaultFont(owner->window->editor()->font());
  QString family=owner->window->editor()->font().family();family.replace(QLatin1Char('\''),QStringLiteral("\\'"));
  context->document()->setDefaultStyleSheet(QStringLiteral("pre { font-family: '%1'; }").arg(family));
  QString external=source;external.replace(start,end-start,item.after);
  context->setHtml(QStringLiteral("<table width=\"100%\"><tr><td width=\"50%\" valign=\"top\">%1<pre>%2</pre></td><td width=\"50%\" valign=\"top\">%3<pre>%4</pre></td></tr></table>").arg(ui(QStringLiteral("当前编辑")),excerpt(source,start,end),ui(QStringLiteral("外部版本")),excerpt(external,start,start+item.after.size())));
  layout->addWidget(context,1);layout->addWidget(new QLabel(ui(QStringLiteral("合并结果（高亮部分）")),dialog));
  auto *draft=new QPlainTextEdit(dialog);draft->setObjectName(QStringLiteral("researchMergeDraft"));draft->setFont(owner->window->editor()->font());draft->setPlainText(source.mid(start,end-start));layout->addWidget(draft,1);
  auto *notice=new QLabel(dialog);notice->setObjectName(QStringLiteral("researchMergeNotice"));notice->setWordWrap(true);layout->addWidget(notice);
  auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,dialog);buttons->setObjectName(QStringLiteral("researchMergeButtons"));buttons->button(QDialogButtonBox::Ok)->setText(ui(QStringLiteral("完成合并")));buttons->button(QDialogButtonBox::Cancel)->setText(ui(QStringLiteral("取消")));for(auto *button:buttons->buttons())if(auto *push=qobject_cast<QPushButton*>(button))configureButton(push);layout->addWidget(buttons);
  connect(buttons,&QDialogButtonBox::rejected,dialog,&QDialog::reject);
  connect(buttons,&QDialogButtonBox::accepted,dialog,[receiver,dialog,draft,notice,buttons,source,start,end,revision,epoch,selected]{
    if(!receiver||!dialog)return;
    const auto latest=read(receiver->window->fileName(),receiver->window->getCurrentCodecName().toLatin1());
    if(!latest.ok){notice->setText(ui(QStringLiteral("无法读取外部版本，请稍后重试。")));return;}
    if(latest.text!=receiver->baseline)receiver->receive(latest.text);
    if(receiver->reviewEpoch!=epoch||receiver->window->textDoc()->revision()!=revision){
      QString saved=source;saved.replace(start,end-start,draft->document()->toRawText().replace(QChar::ParagraphSeparator,QLatin1Char('\n')).replace(QChar::LineSeparator,QLatin1Char('\n')));
      if(recordRevision(receiver->window,saved)){showTransientStatusMessage(receiver->window,ui(QStringLiteral("内容已更改，请重新核对。合并草稿见“本地历史记录”。")),8000);dialog->reject();}
      else {notice->setText(ui(QStringLiteral("内容已更改，合并草稿尚未保存。")));buttons->button(QDialogButtonBox::Ok)->setEnabled(false);}return;
    }
    if(receiver->applyChoice(selected,draft->document()->toRawText().replace(QChar::ParagraphSeparator,QLatin1Char('\n')).replace(QChar::LineSeparator,QLatin1Char('\n'))))dialog->accept();
    else notice->setText(ui(QStringLiteral("无法保存审核副本，请稍后重试。")));
  });
  const QSize room=owner->window->screen()->availableGeometry().size();dialog->resize(qMin(820,room.width()-40),qMin(620,room.height()-40));dialog->exec();if(dialog)delete dialog;
}
void Collaboration::refreshPdfState() {
  const QString root=window->untitled()?QString():window->textDoc()->getRootFilePath(),pdf=root.isEmpty()?QString():previewFile(root);
  const auto state=pdf.isEmpty()?PdfRevision::Unknown:pdfRevision(pdf,root);
  if(auto *button=window->findChild<QToolButton*>(QStringLiteral("researchPdfButton"))) {
    const bool pending=!pdf.isEmpty()&&(state==PdfRevision::Stale||state==PdfRevision::Unpaired);
    const QString caption=QStringLiteral("PDF");
    const QString hint=state==PdfRevision::Unpaired?words("PDF 版本不匹配 · 请重新编译","PDF version mismatch · Rebuild to update"):pending?words("上一版 PDF","Previous PDF")+words(" · 编译后更新"," · Rebuild to update"):words("打开 PDF","Open PDF");
    button->setProperty("researchPdfNeedsBuild",pending);
    button->setText(caption);button->setAccessibleName(pending?hint:caption);
    const auto *action=window->findChild<QAction*>(QStringLiteral("actionGo_to_Preview"));
    button->setToolTip(action&&!action->shortcut().isEmpty()?hint+QStringLiteral(" · ")+action->shortcut().toString(QKeySequence::NativeText):hint);
    configureButton(button,ButtonRole::Text);setControlStatus(button,pending?hint:QString(),state==PdfRevision::Unpaired);
  }
  for(auto *viewer:PDFDocumentWindow::documentList())viewer->refreshResearchRevision();
}
}
