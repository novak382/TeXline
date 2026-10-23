// Named places and local alternatives reuse navigation, diff, history and undo.
// GPL-2.0-or-later.
#include "ResearchWritingContext.h"
#include "ResearchReferences.h"
#include "ResearchProject.h"
#include "ResearchWorkflow.h"
#include "ResearchMerge.h"
#include "ResearchTextAnchor.h"
#include "ResearchFileWrite.h"
#include "ResearchWorkspaceUi.h"
#include "ResearchReadingUi.h"
#include "ResearchOnboarding.h"
#include <QTextBlock>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QResizeEvent>
#include "TeXDocumentWindow.h"
#include "PDFDocumentWindow.h"
#include "CompletingEdit.h"
#include "TWApp.h"
#include <QAction>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListWidget>
#include <QMenu>
#include <QPointer>
#include <QPlainTextEdit>
#include <QRunnable>
#include <QSplitter>
#include <QTabWidget>
#include <QTabBar>
#include <QTextBrowser>
#include <QThreadPool>
#include <QUuid>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QTimer>
#include <QScrollBar>

namespace Research {
namespace {
class RecentWritingPlace final:public QObject {
  TeXDocumentWindow *window;
  QString file;
  QVariantMap state;
  int revision,position,anchor,x,y;
  bool cancelled{false},applying{false};
public:
  RecentWritingPlace(TeXDocumentWindow *w,const QVariantMap &place):QObject(w),window(w),file(w->fileName()),state(place),revision(w->textDoc()->revision()),position(w->textCursor().position()),anchor(w->textCursor().anchor()),x(w->editor()->horizontalScrollBar()->value()),y(w->editor()->verticalScrollBar()->value()){
    setObjectName(QStringLiteral("researchPendingRecentPlace"));w->setProperty("researchRecentPlaceStatus",QStringLiteral("queued"));qApp->installEventFilter(this);
    connect(w->textDoc(),&QTextDocument::contentsChange,this,[this](int,int removed,int added){if(!applying&&(removed||added))cancel();});
    connect(w->editor(),&QTextEdit::selectionChanged,this,[this]{if(!applying)cancel();});
    connect(w->editor(),&QTextEdit::cursorPositionChanged,this,[this]{if(!applying)cancel();});
    for(auto *bar:{w->editor()->horizontalScrollBar(),w->editor()->verticalScrollBar()})connect(bar,&QScrollBar::actionTriggered,this,[this]{if(!applying)cancel();});
    QTimer::singleShot(0,this,[this]{
      if(cancelled)return;
      if(window->untitled()||QFileInfo(window->fileName())!=QFileInfo(file)||window->textDoc()->revision()!=revision||window->textCursor().position()!=position||window->textCursor().anchor()!=anchor||window->editor()->horizontalScrollBar()->value()!=x||window->editor()->verticalScrollBar()->value()!=y){cancel();return;}
      applying=true;QString error;const bool restored=restoreRecentWritingPlace(window,state,&error);applying=false;
      window->setProperty("researchRecentPlaceStatus",restored?QStringLiteral("restored"):QStringLiteral("unavailable"));
      if(!restored&&!error.isEmpty())showTransientStatusMessage(window,error);
      qApp->removeEventFilter(this);deleteLater();
    });
  }
  ~RecentWritingPlace()override{qApp->removeEventFilter(this);}
  void cancel(){if(cancelled)return;cancelled=true;window->setProperty("researchRecentPlaceStatus",QStringLiteral("cancelled"));qApp->removeEventFilter(this);deleteLater();}
  bool eventFilter(QObject *object,QEvent *event)override{
    if(cancelled||applying)return false;auto *widget=qobject_cast<QWidget*>(object);
    if(widget&&widget->window()==window)switch(event->type()){
      case QEvent::KeyPress:case QEvent::InputMethod:case QEvent::Wheel:case QEvent::MouseButtonPress:case QEvent::TouchBegin:case QEvent::NonClientAreaMouseButtonPress:case QEvent::WindowDeactivate:cancel();break;
      default:break;
    }
    return false;
  }
};
QJsonObject readRecord(const QString &file){QFile f(file);if(!f.open(QIODevice::ReadOnly)||f.size()>2*1024*1024)return {};return QJsonDocument::fromJson(f.readAll()).object();}
// Same two-line list rhythm as project versions, with measured text bounds.
class SavedWritingDelegate final:public QStyledItemDelegate {
public:using QStyledItemDelegate::QStyledItemDelegate;
 QSize sizeHint(const QStyleOptionViewItem &option,const QModelIndex &)const override{return QSize(0,QFontMetrics(option.font).height()*2+20);}
 void paint(QPainter *p,const QStyleOptionViewItem &option,const QModelIndex &index)const override {
   p->save();p->setClipRect(option.rect);p->setRenderHint(QPainter::Antialiasing);
   const auto rect=option.rect.adjusted(2,2,-2,-2);const bool selected=option.state&QStyle::State_Selected;
   if(selected||option.state&QStyle::State_MouseOver){p->setPen(Qt::NoPen);p->setBrush(QColor(selected?QStringLiteral("#e8edef"):QStringLiteral("#f0f3f4")));p->drawRoundedRect(rect,5,5);}
   if((option.state&QStyle::State_HasFocus)&&(option.state&QStyle::State_KeyboardFocusChange)){p->setBrush(Qt::NoBrush);p->setPen(QColor(QStringLiteral("#8d98a8")));p->drawRoundedRect(rect.adjusted(1,1,-1,-1),4,4);}
   const int x=rect.left()+10,width=qMax(0,rect.width()-20);QFont font=option.font;font.setWeight(selected?QFont::Medium:QFont::Normal);p->setFont(font);p->setPen(QColor(QStringLiteral("#30363c")));const QFontMetrics fm(font);
   p->drawText(QRect(x,rect.top()+7,width,fm.height()),Qt::AlignLeft|Qt::AlignVCenter,fm.elidedText(index.data(Qt::DisplayRole).toString(),Qt::ElideRight,width));
   font.setPixelSize(ControlMetrics::FontPixels);font.setWeight(QFont::Normal);p->setFont(font);p->setPen(QColor(QStringLiteral("#74808a")));const QFontMetrics meta(font);
   p->drawText(QRect(x,rect.top()+9+fm.height(),width,meta.height()),Qt::AlignLeft|Qt::AlignVCenter,meta.elidedText(index.data(Qt::UserRole+1).toString(),Qt::ElideMiddle,width));p->restore();
 }
};
class WritingContextDialog final:public MaterialDialog {
 QPointer<TeXDocumentWindow> owner,target;
 QString savedNotice;bool restoreFirst{false};
 bool loading=false;bool versions;QString directory,root,current;QTextCursor savedSelection;int rangeStart=0,rangeEnd=0;quint64 generation=0;
 QJsonObject selected;QVector<TextEdit> changes;QVector<QJsonObject> records;
 QLineEdit *name;QPushButton *save,*restore;QListWidget *list;QLabel *notice;QTextBrowser *preview;QPlainTextEdit *currentSource,*savedSource;QTabWidget *tabs;QSplitter *split=nullptr;QLabel *detailTitle,*detailMeta;
 void message(const QString &text){notice->setText(text);notice->setVisible(!text.isEmpty());}
 void load(const QString &select={}){
   loading=true;save->setEnabled(false);list->setEnabled(false);restore->setEnabled(false);const auto ticket=++generation;QPointer<WritingContextDialog> guard(this);const auto folder=directory;
   QThreadPool::globalInstance()->start(QRunnable::create([guard,ticket,folder,select]{
     QVector<QJsonObject> data;const auto files=QDir(folder).entryInfoList({QStringLiteral("*.json")},QDir::Files,QDir::Time);
     for(const auto &file:files){if(data.size()>=200)break;auto value=readRecord(file.absoluteFilePath());if(value.value(QStringLiteral("version")).toInt()!=1||value.value(QStringLiteral("name")).toString().isEmpty())continue;value.insert(QStringLiteral("_path"),file.absoluteFilePath());data.append(value);}
     if(qApp)QMetaObject::invokeMethod(qApp,[guard,ticket,data,select]{if(!guard||guard->generation!=ticket)return;guard->loading=false;guard->save->setEnabled(true);guard->list->setEnabled(true);guard->records=data;QSignalBlocker block(guard->list);guard->list->clear();int row=0;
       for(const auto &record:data){auto *item=new QListWidgetItem(record.value(QStringLiteral("name")).toString(),guard->list);const auto meta=QFileInfo(record.value(QStringLiteral("file")).toString()).fileName()+QStringLiteral(" · ")+QDateTime::fromString(record.value(QStringLiteral("time")).toString(),Qt::ISODateWithMs).toLocalTime().toString(QStringLiteral("MM-dd HH:mm"));item->setData(Qt::UserRole+1,meta);item->setToolTip(item->text()+QLatin1Char('\n')+meta);item->setData(Qt::UserRole,record.value(QStringLiteral("_path")).toString());if(item->data(Qt::UserRole).toString()==select)row=guard->list->count()-1;}
       guard->list->setVisible(!data.isEmpty());guard->tabs->tabBar()->setVisible(guard->versions&&!data.isEmpty());guard->restore->setVisible(!data.isEmpty());guard->list->setCurrentRow(data.isEmpty()?-1:row);guard->showSelected();
       guard->save->setEnabled(!guard->versions||guard->savedSelection.hasSelection());
       if(guard->name->text().isEmpty()){
         const auto base=(QFileInfo(guard->owner->fileName()).completeBaseName()+words(" · 第 %1 行"," · Line %1").arg(guard->savedSelection.blockNumber()+1)+(guard->versions?words(" · 改写前"," · Before rewriting"):QString())).left(68);
         QString title=base;int suffix=2;auto exists=[&](const QString &value){for(const auto &record:data)if(record.value(QStringLiteral("name")).toString()==value)return true;return false;};while(exists(title))title=base+QStringLiteral(" (%1)").arg(suffix++);guard->name->setText(title);
       }
       if(!guard->savedNotice.isEmpty()){guard->message(guard->savedNotice);guard->savedNotice.clear();}
       if(guard->restoreFirst){guard->restoreFirst=false;guard->list->setFocus();}else if(guard->name->hasFocus())guard->name->selectAll();
     },Qt::QueuedConnection);
   }));
 }
 void showSelected(){
   const auto ticket=++generation;restore->setEnabled(false);changes.clear();target=nullptr;message({});currentSource->clear();savedSource->clear();detailTitle->clear();detailMeta->clear();
   const int row=list->currentRow();if(row<0||row>=records.size()){
     selected={};detailTitle->setText(versions?words("将保留的选区","Passage to keep"):words("当前写作位置","Current writing place"));
     detailMeta->setText(QFileInfo(owner->fileName()).fileName()+words(" · 第 %1 行"," · Line %1").arg(savedSelection.blockNumber()+1));
     if(versions)preview->setHtml(savedSelection.hasSelection()?readingStyle()+readingExcerpt(savedSelection.selectedText().replace(QChar::ParagraphSeparator,QLatin1Char('\n'))):words("先选中要保留的段落、证明或章节，再打开这里。","Select a passage, proof or section, then open this window."));
     else{const auto state=captureWritingPlace(owner);QStringList details{state.value(QStringLiteral("article")).toMap().value(QStringLiteral("partial")).toBool()?words("本节范围","Current section"):words("全文范围","Full source")};if(state.value(QStringLiteral("pdfPage")).toInt()>0)details.append(words("PDF 第 %1 页","PDF page %1").arg(state.value(QStringLiteral("pdfPage")).toInt()));preview->setHtml(readingStyle()+readingExcerpt(state.value(QStringLiteral("excerpt")).toString())+QStringLiteral("<p class='meta'>%1</p>").arg(details.join(QStringLiteral(" · ")).toHtmlEscaped()));}return;
   }
   selected=records[row];detailTitle->setText(selected.value(QStringLiteral("name")).toString());detailMeta->setText(QDir(QFileInfo(root).absolutePath()).relativeFilePath(selected.value(QStringLiteral("file")).toString()));
   if(!versions){
     const auto state=selected.value(QStringLiteral("state")).toObject();const auto references=state.value(QStringLiteral("references")).toObject();
     detailMeta->setText(detailMeta->text()+words(" · 第 %1 行"," · Line %1").arg(state.value(QStringLiteral("line")).toInt()));
     QStringList details{state.value(QStringLiteral("article")).toObject().value(QStringLiteral("partial")).toBool()?words("章节范围","Section scope"):words("全文范围","Full source")};
     if(references.value(QStringLiteral("pinned")).toBool()||references.value(QStringLiteral("pinRestore")).toBool())details.append(words("固定查阅 ","Pinned ")+references.value(QStringLiteral("pinKey")).toString());
     if(state.value(QStringLiteral("pdfPage")).toInt()>0)details.append(words("PDF 第 %1 页","PDF page %1").arg(state.value(QStringLiteral("pdfPage")).toInt()));
     preview->setHtml(readingStyle()+readingSection(words("从这里继续","Continue from here"))+readingExcerpt(state.value(QStringLiteral("excerpt")).toString())+readingSection(words("恢复位置","Saved position"))+QStringLiteral("<p class='meta'>%1</p>").arg(details.join(QStringLiteral(" · ")).toHtmlEscaped()));restore->setEnabled(true);return;
   }
   const auto file=selected.value(QStringLiteral("file")).toString();target=TeXDocumentWindow::findDocument(file);
   if(!target&&QFileInfo(file).isFile())target=TeXDocumentWindow::openDocument(file,false,false);
   if(!target){preview->clear();message(words("文件已移动或删除。","The document was moved or deleted."));return;}
   current=target->textDoc()->editorText();
   if(!locateTextAnchor(current,selected.value(QStringLiteral("range")).toObject().toVariantMap(),&rangeStart,&rangeEnd)){
     savedSource->setPlainText(selected.value(QStringLiteral("text")).toString());preview->setPlainText(words("原范围已改变，无法唯一定位。仍可从源码对照复制保留的内容。","The original range cannot be located uniquely. The saved source remains available to copy."));return;
   }
   const auto body=current.mid(rangeStart,rangeEnd-rangeStart),saved=selected.value(QStringLiteral("text")).toString();currentSource->setPlainText(body);savedSource->setPlainText(saved);preview->setPlainText(words("正在比较…","Comparing…"));
   QPointer<WritingContextDialog> guard(this);
   QThreadPool::globalInstance()->start(QRunnable::create([guard,ticket,body,saved]{const auto edits=textEdits(body,saved);if(qApp)QMetaObject::invokeMethod(qApp,[guard,ticket,body,edits]{if(!guard||guard->generation!=ticket)return;guard->changes=edits;QString html=readingStyle();if(!edits.isEmpty())html+=QStringLiteral("<p class='meta'>%1</p>").arg(words("当前内容 → 保存的版本","Current passage → Saved version"));
     for(int i=0;i<edits.size();++i){const auto &edit=edits[i];const auto removed=body.mid(edit.start,edit.end-edit.start);const bool whitespace=removed.simplified()==edit.text.simplified();
       const int lineStart=edit.start?int(body.lastIndexOf(QLatin1Char('\n'),edit.start-1))+1:0;
       int lineEnd=int(body.indexOf(QLatin1Char('\n'),edit.end));if(lineEnd<0)lineEnd=int(body.size());
       const int contextStart=qMax(lineStart,edit.start-160),contextEnd=qMin(lineEnd,edit.end+160);
       const auto left=(contextStart>lineStart?QStringLiteral("…"):QString())+body.mid(contextStart,edit.start-contextStart);
       const auto right=body.mid(edit.end,contextEnd-edit.end)+(contextEnd<lineEnd?QStringLiteral("…"):QString());
       html+=QStringLiteral("<p style='font-size:15px;margin-top:16px;color:#69747c'>%1 &nbsp; · &nbsp; <a href='restore:%2'>%3</a></p><p style='background:#faf1ef'>%4</p><p style='background:#f0f5f1'>%5</p>").arg((whitespace?words("空白变化","Whitespace change"):words("内容变化","Change"))+QStringLiteral(" %1").arg(i+1)).arg(i).arg(words("取回这处","Restore this change"),left.toHtmlEscaped()+QStringLiteral("<span style='color:#854f46'>")+removed.left(12000).toHtmlEscaped()+QStringLiteral("</span>")+right.toHtmlEscaped(),left.toHtmlEscaped()+QStringLiteral("<span style='color:#35644d'>")+edit.text.left(12000).toHtmlEscaped()+QStringLiteral("</span>")+right.toHtmlEscaped());
     }
     guard->preview->setHtml(edits.isEmpty()?words("与当前内容相同。","Identical to the current passage."):html);guard->restore->setEnabled(!edits.isEmpty());
   },Qt::QueuedConnection);}));
 }
 void saveCurrent(){
   if(!owner||loading)return;const auto title=name->text().trimmed();if(title.isEmpty()){name->setFocus();return;}
   if(title.size()>80){message(words("名称请控制在 80 个字符内。","Keep the name within 80 characters."));return;}
   for(const auto &record:records)if(record.value(QStringLiteral("name")).toString()==title){message(words("已有同名记录，请换一个名称。","That name is already in use."));return;}
   if(records.size()>=200){message(words("已有 200 个记录，请先删除不再需要的记录。","There are 200 saved records. Remove unused records first."));return;}
   QJsonObject record{{QStringLiteral("version"),1},{QStringLiteral("name"),title},{QStringLiteral("time"),QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},{QStringLiteral("root"),root},{QStringLiteral("file"),owner->fileName()}};
   if(versions){
     if(savedSelection.isNull()||!savedSelection.hasSelection()){message(words("请先选中要保留的段落、证明或章节。","Select the passage, proof or section to keep."));return;}
     const auto text=owner->textDoc()->editorText();const int a=savedSelection.selectionStart(),b=savedSelection.selectionEnd();if(b-a>512*1024){message(words("范围较大，请分段保留。","Keep this large selection in smaller passages."));return;}
     record.insert(QStringLiteral("range"),QJsonObject::fromVariantMap(textAnchor(text,a,b)));record.insert(QStringLiteral("text"),text.mid(a,b-a));
   }else record.insert(QStringLiteral("state"),QJsonObject::fromVariantMap(captureWritingPlace(owner)));
   if(!QDir().mkpath(directory)){message(words("无法保存本地记录。","Cannot save the local record."));return;}
   const auto file=QDir(directory).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces)+QStringLiteral(".json"));QString error;
   if(!writeNewFile(file,QJsonDocument(record).toJson(),&error)){message(error);return;}savedNotice=(versions?words("已保留：","Version kept: "):words("已保存现场：","Writing place saved: "))+title;name->clear();load(file);
 }
 void restoreSelection(int change=-1){
   if(selected.isEmpty()||!owner)return;
   if(!versions){QString error;rememberReadingPlace(owner);if(restoreWritingPlace(selected.value(QStringLiteral("state")).toObject().toVariantMap(),&error)){QPointer<TeXDocumentWindow> restored=TeXDocumentWindow::findDocument(selected.value(QStringLiteral("file")).toString());accept();if(restored)QTimer::singleShot(0,restored,[restored]{if(restored){restored->selectWindow();restored->editor()->setFocus();}});}else message(error);return;}
   if(!target||target->editor()->isReadOnly()||target->textDoc()->editorText()!=current){message(words("内容已变化，请重新选择版本以更新对照。","The passage changed. Select the version again to refresh the comparison."));restore->setEnabled(false);return;}
   TextEdit edit{0,rangeEnd-rangeStart,selected.value(QStringLiteral("text")).toString()};if(change>=0){if(change>=changes.size())return;edit=changes[change];}
   if(!recordRevision(target,current)){message(words("无法保存恢复前的历史，当前内容未修改。","Cannot retain the current history. The passage was not changed."));return;}
   rememberReadingPlace(owner);QTextCursor cursor(target->textDoc());cursor.beginEditBlock();cursor.setPosition(rangeStart+edit.start);cursor.setPosition(rangeStart+edit.end,QTextCursor::KeepAnchor);cursor.insertText(edit.text);cursor.endEditBlock();
   cursor.setPosition(rangeStart+edit.start);cursor.setPosition(rangeStart+edit.start+edit.text.size(),QTextCursor::KeepAnchor);target->editor()->setTextCursor(cursor);QPointer<TeXDocumentWindow> restored=target;accept();QTimer::singleShot(0,restored,[restored]{if(restored){restored->selectWindow();restored->editor()->setFocus();}});
 }
 void menu(QPoint point){
   auto *item=list->itemAt(point);if(!item)return;list->setCurrentItem(item);const auto file=item->data(Qt::UserRole).toString();QMenu menu(this);
   auto *rename=menu.addAction(words("重命名…","Rename…"));auto *remove=menu.addAction(words("删除记录…","Delete saved record…"));menu.addSeparator();auto *history=menu.addAction(words("本地历史…","Local history…"));auto *git=menu.addAction(words("项目版本与协作…","Project versions & collaboration…"));const auto chosen=menu.exec(list->viewport()->mapToGlobal(point));
   if(chosen==history||chosen==git){const auto id=chosen==git?QStringLiteral("researchVersionControl"):QStringLiteral("researchHistoryAction");accept();if(owner)if(auto *action=owner->findChild<QAction*>(id))QTimer::singleShot(0,action,&QAction::trigger);return;}
   auto value=readRecord(file);if(value.isEmpty())return;
   if(chosen==rename){const auto title=workspaceInput(this,words("重命名","Rename"),words("名称","Name"),value.value(QStringLiteral("name")).toString());if(title.isEmpty()||title.size()>80)return;for(const auto &record:records)if(record.value(QStringLiteral("_path")).toString()!=file&&record.value(QStringLiteral("name")).toString()==title){message(words("已有同名记录。","That name is already in use."));return;}value.insert(QStringLiteral("name"),title);QSaveFile f(file);const auto data=QJsonDocument(value).toJson();if(!f.open(QIODevice::WriteOnly)||f.write(data)!=data.size()||!f.commit()){message(f.errorString());return;}load(file);}
   if(chosen==remove&&workspaceConfirm(this,words("删除记录","Delete saved record"),value.value(QStringLiteral("name")).toString(),words("删除","Delete"))){if(QFile::remove(file))load();else message(words("无法删除此记录。","Cannot delete this record."));}
 }
 void adapt(){const bool narrow=width()<680;split->setOrientation(narrow?Qt::Vertical:Qt::Horizontal);list->setMaximumHeight(narrow?116:QWIDGETSIZE_MAX);if(narrow)split->setSizes({100,300});}
 void resizeEvent(QResizeEvent *event)override{MaterialDialog::resizeEvent(event);if(split)adapt();}
public:
 WritingContextDialog(TeXDocumentWindow *w,bool local,bool restoring=false):MaterialDialog(w),owner(w),versions(local),root(completionProjectRoot(w)),savedSelection(w->textCursor()){
   restoreFirst=restoring;configureSettingsSurface(this);setObjectName(local?QStringLiteral("researchLocalVersionsDialog"):QStringLiteral("researchWritingContextsDialog"));setWindowTitle(local?(savedSelection.hasSelection()?words("保留选区版本","Keep selection version"):words("查看局部版本","View local versions")):(restoring?words("恢复写作现场","Restore writing place"):words("保存写作现场","Save writing place")));
   const auto id=QString::fromLatin1(QCryptographicHash::hash(root.toUtf8(),QCryptographicHash::Sha256).toHex());directory=writingHistoryDirectory()+QStringLiteral("/named/")+id+(local?QStringLiteral("/versions"):QStringLiteral("/places"));
   auto *layout=new QVBoxLayout(this);layout->setContentsMargins(20,20,20,16);layout->setSpacing(16);
   auto *heading=new QLabel(windowTitle(),this);auto titleFont=interfaceFont();titleFont.setPixelSize(ControlMetrics::FontPixels);titleFont.setWeight(QFont::DemiBold);heading->setFont(titleFont);heading->setStyleSheet(QStringLiteral("font-size:15px;font-weight:600;"));layout->addWidget(heading);
   split=new QSplitter(Qt::Horizontal,this);split->setHandleWidth(12);split->setChildrenCollapsible(false);split->setStyleSheet(QStringLiteral("QSplitter::handle{background:transparent;}"));
   list=new QListWidget(split);list->setObjectName(QStringLiteral("researchSavedWritingList"));configureSettingsList(list);list->setItemDelegate(new SavedWritingDelegate(list));list->setStyleSheet(QStringLiteral("QListWidget{background:#f6f7f8;border:0;border-radius:6px;padding:4px;}"));list->setContextMenuPolicy(Qt::CustomContextMenu);list->setMinimumSize(150,80);list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
   auto *detail=new QWidget(split);auto *detailLayout=new QVBoxLayout(detail);detailLayout->setContentsMargins(8,4,0,0);detailLayout->setSpacing(8);
   detailTitle=new QLabel(detail);detailTitle->setTextFormat(Qt::PlainText);detailTitle->setWordWrap(true);auto detailFont=interfaceFont();detailFont.setPixelSize(ControlMetrics::FontPixels);detailFont.setWeight(QFont::Medium);detailTitle->setFont(detailFont);detailTitle->setStyleSheet(QStringLiteral("font-size:15px;font-weight:500;"));detailLayout->addWidget(detailTitle);
   detailMeta=new QLabel(detail);detailMeta->setTextFormat(Qt::PlainText);detailMeta->setWordWrap(true);detailMeta->setStyleSheet(QStringLiteral("color:#74808a;font-size:15px;"));detailLayout->addWidget(detailMeta);
   tabs=new QTabWidget(detail);tabs->setObjectName(QStringLiteral("researchSavedWritingTabs"));tabs->setStyleSheet(QStringLiteral("QTabWidget#researchSavedWritingTabs::pane{border:0;}"));tabs->tabBar()->setVisible(local);detailLayout->addWidget(tabs,1);
   preview=new QTextBrowser(tabs);preview->setObjectName(QStringLiteral("researchSavedWritingChanges"));configureReadingBrowser(preview);tabs->addTab(preview,local?words("内容变化","Changes"):words("现场","Place"));
   auto *sources=new QSplitter(Qt::Vertical,tabs);auto sourcePane=[&](const QString &title){auto *pane=new QWidget(sources);auto *column=new QVBoxLayout(pane);column->setContentsMargins(0,8,0,4);auto *label=new QLabel(title,pane);label->setStyleSheet(QStringLiteral("color:#74808a;font-size:15px;"));column->addWidget(label);auto *text=new QPlainTextEdit(pane);column->addWidget(text,1);return text;};currentSource=sourcePane(words("当前内容","Current passage"));savedSource=sourcePane(words("保存的版本","Saved version"));for(auto *source:{currentSource,savedSource}){source->setReadOnly(true);configureWorkspaceText(source);source->setFont(w->editor()->font());}currentSource->setPlaceholderText(words("当前内容","Current passage"));savedSource->setPlaceholderText(words("保留的版本","Saved version"));savedSource->setObjectName(QStringLiteral("researchSavedWritingSource"));
   if(local)tabs->addTab(sources,words("源码对照","Source comparison"));else sources->hide();split->setStretchFactor(0,0);split->setStretchFactor(1,1);split->setSizes({210,530});layout->addWidget(split,1);
   auto *top=new QHBoxLayout;top->setSpacing(8);name=new QLineEdit(this);name->setObjectName(QStringLiteral("researchSavedWritingName"));name->setPlaceholderText(local?words("给当前选区命名…","Name the current selection…"):words("给当前现场命名…","Name this writing place…"));configureField(name);top->addWidget(name,1);save=new QPushButton(local?words("保留版本","Keep version"):words("保存现场","Save place"),this);save->setObjectName(QStringLiteral("researchSavedWritingSave"));configureButton(save);save->setAutoDefault(false);top->addWidget(save);layout->addLayout(top);
   notice=new QLabel(this);notice->hide();notice->setTextFormat(Qt::PlainText);notice->setWordWrap(true);notice->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Maximum);layout->addWidget(notice);auto *buttons=workspaceFooter(this,layout,local?words("恢复整段","Restore passage"):words("恢复写作现场","Restore writing place"));restore=buttons->button(QDialogButtonBox::Ok);restore->setObjectName(QStringLiteral("researchSavedWritingRestore"));restore->setEnabled(false);restore->setAutoDefault(false);
   auto *demo=buttons->addButton(words("使用示范","Short demo"),QDialogButtonBox::HelpRole);demo->setObjectName(QStringLiteral("researchWritingDemo"));configureButton(demo);connect(demo,&QPushButton::clicked,this,[this]{showOnboardingTopic(this,versions?QStringLiteral("versions"):QStringLiteral("places"));});
   connect(save,&QPushButton::clicked,this,[this]{saveCurrent();});connect(name,&QLineEdit::returnPressed,this,[this]{saveCurrent();});connect(list,&QListWidget::currentRowChanged,this,[this]{showSelected();});
   // A stale comparison may have only one record: selecting that same row
   // again must provide the recovery promised by the source-change message.
   auto refreshComparison=[this]{if(versions&&target&&target->textDoc()->editorText()!=current)showSelected();};
   connect(list,&QListWidget::itemClicked,this,[refreshComparison](QListWidgetItem *){refreshComparison();});
   connect(list,&QListWidget::itemActivated,this,[refreshComparison](QListWidgetItem *){refreshComparison();});
   connect(list,&QListWidget::customContextMenuRequested,this,[this](QPoint p){menu(p);});connect(buttons,&QDialogButtonBox::accepted,this,[this]{restoreSelection();});
   connect(preview,&QTextBrowser::anchorClicked,this,[this](QUrl url){if(url.scheme()==QStringLiteral("restore")){bool valid=false;const int index=url.path().toInt(&valid);if(valid)restoreSelection(index);}});
   connect(this,&QDialog::finished,w,[this,w]{++generation;if(result()!=QDialog::Accepted)w->editor()->setFocus();});fitWorkspaceDialog(this,800,520);adapt();if(!restoring)name->setFocus();load();
 }
};
}
QVariantMap captureRecentWritingPlace(TeXDocumentWindow *window){
 if(!window||window->untitled())return {};
 const auto prior=TWApp::instance()->getFileProperties(window->fileName()).value(QStringLiteral("researchRecentPlace")).toMap();
 // Closing with discarded edits must not replace a trustworthy saved anchor
 // with coordinates into content that will never be written to disk.
 if(window->isModified()||window->findChild<QObject*>(QStringLiteral("researchPendingRecentPlace"))||window->findChild<QObject*>(QStringLiteral("researchPendingSourcePlace")))return prior;
 auto state=captureWritingPlace(window);if(state.isEmpty())return prior;
 state.insert(QStringLiteral("version"),1);
 for(const auto &key:{QStringLiteral("width"),QStringLiteral("height"),QStringLiteral("focus"),QStringLiteral("excerpt")})state.remove(key);
 auto references=state.value(QStringLiteral("references")).toMap();references.insert(QStringLiteral("dialog"),false);state.insert(QStringLiteral("references"),references);
 return QJsonDocument(QJsonObject::fromVariantMap(state)).toJson(QJsonDocument::Compact).size()<=64*1024?state:prior;
}
void cancelRecentWritingPlace(TeXDocumentWindow *window){
 if(!window)return;
 if(auto *pending=window->findChild<QObject*>(QStringLiteral("researchPendingRecentPlace")))delete pending;
 if(auto *pending=window->findChild<QObject*>(QStringLiteral("researchPendingSourcePlace")))delete pending;
 window->setProperty("researchRestoreArticlePlace",QVariant());window->setProperty("researchRestoreSlidePlace",QVariant());
 if(auto *pdf=window->pdfDocument())if(auto *pending=pdf->findChild<QObject*>(QStringLiteral("researchPendingPdfPlace")))delete pending;
 window->setProperty("researchRecentPlaceStatus",QStringLiteral("cancelled"));
}
void scheduleRecentWritingPlace(TeXDocumentWindow *window,const QVariantMap &state){
 if(!window||window->untitled()||state.value(QStringLiteral("version")).toInt()!=1||QFileInfo(state.value(QStringLiteral("file")).toString())!=QFileInfo(window->fileName())||QJsonDocument(QJsonObject::fromVariantMap(state)).toJson(QJsonDocument::Compact).size()>64*1024)return;
 cancelRecentWritingPlace(window);new RecentWritingPlace(window,state);
}
void addWritingContextActions(QMenu *menu,TeXDocumentWindow *window,int position){
 if(!menu||!window)return;
 const auto selection=window->textCursor();
 QTextCursor clicked(window->textDoc());clicked.setPosition(qBound(0,position,window->textDoc()->characterCount()-1));
 if(!clicked.block().text().trimmed().isEmpty()){
 auto *read=menu->addAction(words("查阅此处上下文…","Read context here…"));read->setObjectName(QStringLiteral("researchContextRead"));
 // Read the clicked object without moving the writing cursor or discarding a selection.
 QObject::connect(read,&QAction::triggered,window,[window,position]{openReferenceContext(window,position);});
 }
 if(selection.hasSelection()&&!window->editor()->isReadOnly())if(auto *command=window->findChild<QAction*>(QStringLiteral("researchLocalVersions"))){
  auto *version=menu->addAction(words("保留选区版本…","Keep selection version…"));version->setObjectName(QStringLiteral("researchContextLocalVersion"));
  QObject::connect(version,&QAction::triggered,command,&QAction::trigger);
 }
 menu->addSeparator();
}
void installWritingContexts(TeXDocumentWindow *window){
 for(bool local:{false,true}){
   auto *action=new QAction(window);action->setObjectName(local?QStringLiteral("researchLocalVersions"):QStringLiteral("researchWritingContexts"));auto label=[=]{action->setText(local?(window->textCursor().hasSelection()?words("保留选区版本…","Keep selection version…"):words("查看局部版本…","View local versions…")):words("保存写作现场…","Save writing place…"));};label();window->addAction(action);QObject::connect(TWApp::instance(),&TWApp::updatedTranslators,action,label);if(local)QObject::connect(window->editor(),&QTextEdit::selectionChanged,action,label);
   QObject::connect(action,&QAction::triggered,window,[=]{if(window->untitled()){showTransientStatusMessage(window,words("请先保存文稿。","Save the document first."));return;}WritingContextDialog dialog(window,local);dialog.exec();});
 }
 auto *restore=new QAction(window);restore->setObjectName(QStringLiteral("researchRestoreWritingContext"));auto update=[restore]{restore->setText(words("恢复写作现场…","Restore writing place…"));};update();window->addAction(restore);QObject::connect(TWApp::instance(),&TWApp::updatedTranslators,restore,update);
 QObject::connect(restore,&QAction::triggered,window,[window]{if(window->untitled()){showTransientStatusMessage(window,words("先打开一篇已保存的文稿。","Open a saved document first."));return;}WritingContextDialog dialog(window,false,true);dialog.exec();});

}
}
