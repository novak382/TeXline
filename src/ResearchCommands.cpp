#include "ResearchCommands.h"
#include "ResearchAppearance.h"
#include "ResearchCompletion.h"
#include "ResearchSettings.h"
#include "Settings.h"
#include "PrefsDialog.h"
#include <QAction>
#include <QApplication>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMenu>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QSet>
#include <QShortcut>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QStyledItemDelegate>
#include <QPainter>
#include <algorithm>

namespace Research {
const QList<CommandDefinition> &commandDefinitions() {
  // Object names are persistent identities, independent of translated captions.
  static const QList<CommandDefinition> definitions{
    {"actionNew","file","新建 new draft"},{"actionOpen","file","打开 open"},
    {"actionNew_from_Template","file","模板 导入模板 学校模板 经典空白 template import theme school ZIP slides 幻灯片"},
    {"researchProjectFilesAction","file","项目 文件 子文件 模板 样式 文献 project files cls sty bib"},
    {"researchVersionControl","file","版本 协作 Git 提交 分支 同步 历史 冲突 commit branch merge stash push pull"},
    {"researchEnvironmentCheck","build","环境 安装 编译器 路径 TeX Live MiKTeX Git setup tools compiler"},
    {"actionSave","file","保存 save"},{"researchWritingContexts","file","写作现场 保存 命名 workspace context session","记住当前章节、选区、查阅与 PDF 位置","Keep your section, selection, references and PDF position","明天接着写|今天先停|保存进度|保存位置|明天继续|save my place|stop for today"},{"researchRestoreWritingContext","search","恢复 写作现场 继续 restore resume session","回到已保存的写作位置","Return to a saved writing place","接着写|接着上次|恢复现场|继续昨天|回到上次|resume writing|continue writing"},{"actionSave_As","file","另存 save as"},
    {"actionSave_All","file","保存全部 save all"},{"actionClose","file","关闭 close"},
    {"actionQuit","file","退出 quit"},{"researchHistoryAction","file","历史 history"},
    {"researchRecoveryAction","file","恢复 recovery"},{"researchReviewUpdates","file","外部修改 合并 merge"},
    {"researchLocalVersions","edit","局部版本 改写 命名版本 段落 证明 比较 local alternatives revisions","保留选中段落，改写后可比较并取回","Keep a selected passage, compare edits and restore it","改之前备份|改之前留一份|怕改坏|换一种写法|保留旧内容|取回旧内容|before rewriting|keep a version|restore passage"},
    {"researchPublishPdf","file","写回 同步 publish pdf"},
    {"actionUndo","edit","撤销 undo"},{"actionRedo","edit","重做 redo"},
    {"actionCut","edit","剪切 cut"},{"actionCopy","edit","复制 copy"},
    {"actionPaste","edit","粘贴 paste"},{"actionSelect_All","edit","全选 select all"},
    {"actionIndent","edit","缩进 indent"},{"actionUnindent","edit","缩进 indent"},
    {"researchLineEdit0","edit","移动行 move line"},{"researchLineEdit1","edit","移动行 move line"},
    {"researchLineEdit2","edit","复制行 duplicate line"},{"researchLineEdit3","edit","删除行 delete line"},
    {"researchLineEdit4","edit","注释 comment"},{"researchShowCompletion","edit","补全 suggestions"},
    {"researchPictureGroup","insert","多图 子图 排列 裁剪 图片 subfigure images crop layout","排列一组图片并编辑标题与裁剪","Arrange a group of images, captions and crops","四张图|图片并排|多图排版|arrange pictures"},{"researchFormulaStructure","insert","公式 矩阵 分段 对齐 matrix cases align","编辑矩阵、分段函数与多行公式","Edit matrices, cases and aligned equations","矩阵加一列|公式对齐|分段函数|edit matrix"},{"researchSlideReveal","search","幻灯片 逐步显示 步骤 overlay uncover pause","设置内容在第几步出现与结束","Set when slide content appears and disappears","第二步出现|逐条显示|reveal steps"},{"researchInsertFigure","insert","图片 image figure"},{"researchInsertTable","insert","表格 table"},
    {"researchInsertCitation","insert","文献 引用 citation bibliography","关联文献文件，选择条目并插入引用","Link a bibliography, choose a reference and cite it","插入文献|引用论文|加一条引用|关联文献|cite a paper|insert a reference"},{"researchInsertFootnote","insert","脚注 footnote"},
    {"researchWrapQuotation","insert","引文 quotation"},{"researchSaveSnippet","insert","添加片段 缩写 速记 snippet shorthand"},
    {"researchCreateCommand","insert","宏 自定义命令 macro command"},
    {"researchConfigureCompletion","edit","添加片段 创建缩写 片段 补全 snippets"},
    {"researchSettingsLanguage","root","改语言 恢复中文 language"},{"researchSettingsShortcuts","root","修改快捷键 shortcuts"},
    {"researchOutlineAction","view","目录 structure outline"},{"researchArticleSymbolSearch","view","查阅 搜索 定理 reference"},
    {"researchRelatedAction","root","查阅 上下文 引用 related reference context","查看此处引用了什么，以及哪里引用此处","See what this passage cites and where it is cited","哪里用到|在哪用到|引用了什么|哪些位置引用|定理用在哪|where is this used|what cites this|references here"},{"researchPinReferenceAction","view","固定到下方 pin reference"},
    {"researchNavigateBack","view","返回 back"},{"researchNavigateForward","view","前进 forward"},
    {"actionLine_Numbers","view","行号 line numbers"},{"actionWrap_Lines","view","换行 wrap"},
    {"actionGo_to_Preview","view","转到 pdf"},{"actionGo_to_Source","view","转到源码 source"},
    {"actionSide_by_Side","view","窗口 半屏 layout"},{"researchSlideModeAction","view","幻灯片 slides"},
    {"researchSlideExpandPreview","search","展开 预览 讲稿 slides preview","保留幻灯片列表，让预览占满剩余空间","Keep the slide list and enlarge the preview","不看源码|专心看讲稿|看大幻灯片|expand preview|hide slide source"},{"actionTypeset","build","编译 停止 build stop","将当前论文排版为 PDF","Compile the paper to PDF","看到PDF|看到 PDF|排版论文|compile paper"},{"actionShow_Hide_Console","build","日志 输出 console log"},
    {"researchRootAction","document","主文件 root"},{"researchCommandPalette","root","命令 搜索 command"},
    {"actionPreferences","root","设置 偏好 改语言 快捷键 settings preferences language shortcuts"}
  };
  return definitions;
}
QList<QAction *> workspaceCommands(QMainWindow *window) {
  QList<QAction *> result; QSet<QString> seen;
  if (!window) return result;
  for (auto *a : window->findChildren<QAction *>()) {
    if (a->isSeparator() || a->menu() || a->objectName().isEmpty() || a->text().isEmpty() || seen.contains(a->objectName())) continue;
    seen.insert(a->objectName()); result.append(a);
  }
  return result;
}
QString commandSearchText(QAction *action) {
  QString text = action->text().remove(QLatin1Char('&')) + QLatin1Char(' ') + action->toolTip();
  for (const auto &d : commandDefinitions()) if (action->objectName() == QLatin1String(d.id)) text += QLatin1Char(' ') + QString::fromUtf8(d.keywords);
  for (const auto &key : action->shortcuts()) text += QLatin1Char(' ') + key.toString(QKeySequence::NativeText);
  return text;
}
QString commandPurpose(QAction *action) {
  for(const auto &d:commandDefinitions())if(action->objectName()==QLatin1String(d.id)){
    const auto *purpose=englishUi()?d.purposeEn:d.purposeZh;return purpose?(englishUi()?QString::fromUtf8(purpose):ui(QString::fromUtf8(purpose))):QString();
  }
  return {};
}
int commandMatchScore(QAction *action,const QString &query) {
  const auto term=uiSearchText(query.simplified());int score=0;
  if(!term.isEmpty()){
    const auto text=uiSearchText(commandSearchText(action));bool literal=true;
    for(const auto &word:term.split(QLatin1Char(' '),Qt::SkipEmptyParts))literal&=text.contains(word);
    score=literal?100:-1;
    if(uiSearchText(action->text()).contains(term))score=200;
    for(const auto &d:commandDefinitions())if(d.intents&&action->objectName()==QLatin1String(d.id))
      for(const auto &raw:QString::fromUtf8(d.intents).split(QLatin1Char('|')))
        if(const auto phrase=uiSearchText(raw);term.contains(phrase,Qt::CaseInsensitive)||(term.size()>=3&&phrase.startsWith(term,Qt::CaseInsensitive)))score=qMax(score,300);
    if(score<0)return -1;
  }
  return score+(action->isEnabled()?1000:0);
}
class CommandResultDelegate final:public QStyledItemDelegate {
public:using QStyledItemDelegate::QStyledItemDelegate;
  QSize sizeHint(const QStyleOptionViewItem &o,const QModelIndex &i)const override{return QSize(0,QFontMetrics(o.font).height()*(i.data(Qt::UserRole+2).toString().isEmpty()?1:2)+16);}
  void paint(QPainter *p,const QStyleOptionViewItem &o,const QModelIndex &i)const override{
    p->save();p->setClipRect(o.rect);const auto rect=o.rect.adjusted(8,7,-8,-7);
    if(o.state&QStyle::State_Selected)p->fillRect(o.rect,QColor(QStringLiteral("#e8edef")));
    p->setFont(o.font);const QFontMetrics fm(o.font);p->setPen(QColor(o.state&QStyle::State_Enabled?QStringLiteral("#30363c"):QStringLiteral("#90999f")));
    p->drawText(QRect(rect.left(),rect.top(),rect.width(),fm.height()),Qt::AlignVCenter|Qt::AlignLeft,fm.elidedText(i.data().toString(),Qt::ElideRight,rect.width()));
    p->setPen(QColor(QStringLiteral("#74808a")));p->drawText(QRect(rect.left(),rect.top()+fm.height()+2,rect.width(),fm.height()),Qt::AlignVCenter|Qt::AlignLeft,fm.elidedText(i.data(Qt::UserRole+2).toString(),Qt::ElideRight,rect.width()));p->restore();
  }
};
void applyCommandShortcuts(QMainWindow *window) {
  const auto custom = Tw::Settings().value(QStringLiteral("researchShortcutsV1")).toMap();
  for (auto *action : window->findChildren<QAction *>()) {
    if(action->objectName().isEmpty()||action->isSeparator()||action->menu())continue;
    if (!action->property("researchDefaultShortcuts").isValid()) {
      QStringList keys; for (const auto &key : action->shortcuts()) keys.append(key.toString(QKeySequence::PortableText));
      action->setProperty("researchDefaultShortcuts", keys);
    }
    if (custom.contains(action->objectName())) action->setShortcuts(QKeySequence::listFromString(custom.value(action->objectName()).toString(), QKeySequence::PortableText));
    else { QList<QKeySequence> keys; for (const auto &s : action->property("researchDefaultShortcuts").toStringList()) keys.append(QKeySequence(s)); action->setShortcuts(keys); }
    if (!action->shortcuts().isEmpty()) window->addAction(action);
  }
}
void showCommandPalette(QMainWindow *window, QAction *invoker) {
  MaterialDialog dialog(window); dialog.setObjectName(QStringLiteral("researchCommandDialog"));
  dialog.setWindowTitle(englishUi()?QStringLiteral("Commands"):Research::ui(QStringLiteral("命令面板")));
  auto *layout = new QVBoxLayout(&dialog); layout->setContentsMargins(20,20,20,20);
  auto *search = new QLineEdit(&dialog); search->setObjectName(QStringLiteral("researchCommandQuery"));
  search->setPlaceholderText(englishUi()?QStringLiteral("What would you like to do?"):Research::ui(QStringLiteral("想完成什么？例如：改之前备份"))); configureField(search); layout->addWidget(search);
  auto *list = new QListWidget(&dialog); list->setObjectName(QStringLiteral("researchCommandResults")); configureSettingsList(list);list->setItemDelegate(new CommandResultDelegate(list));layout->addWidget(list);
  QList<QPointer<QAction>> actions;
  for (auto *a : workspaceCommands(window)) if (a != invoker && a->isVisible()) {
    auto *item = new QListWidgetItem(a->text().remove(QLatin1Char('&')) + QStringLiteral("    ") + a->shortcut().toString(QKeySequence::NativeText), list);
    item->setData(Qt::UserRole, actions.size()); item->setData(Qt::UserRole+1, commandSearchText(a));item->setData(Qt::UserRole+2,commandPurpose(a));item->setToolTip(commandPurpose(a));
    if (!a->isEnabled()) item->setFlags(item->flags() & ~Qt::ItemIsEnabled); actions.append(a);
  }
  auto filter = [list,actions](const QString &query) {
    QList<QListWidgetItem*> items;while(list->count())items.append(list->takeItem(0));
    for(auto *item:items){auto action=actions.value(item->data(Qt::UserRole).toInt());item->setData(Qt::UserRole+3,action?commandMatchScore(action,query):-1);}
    std::stable_sort(items.begin(),items.end(),[](auto *a,auto *b){const int x=a->data(Qt::UserRole+3).toInt(),y=b->data(Qt::UserRole+3).toInt();return x==y?a->text()<b->text():x>y;});
    QListWidgetItem *first=nullptr;for(auto *item:items){list->addItem(item);item->setHidden(item->data(Qt::UserRole+3).toInt()<0);if(!first&&!item->isHidden()&&(item->flags()&Qt::ItemIsEnabled))first=item;}list->setCurrentItem(first);
  };
  QObject::connect(search,&QLineEdit::textChanged,&dialog,filter);
  QPointer<QAction> selected;
  auto activate=[&](QListWidgetItem *item){if(item&&!item->isHidden()&&(item->flags()&Qt::ItemIsEnabled)){selected=actions.value(item->data(Qt::UserRole).toInt());dialog.accept();}};
  QObject::connect(search,&QLineEdit::returnPressed,&dialog,[&]{activate(list->currentItem());}); QObject::connect(list,&QListWidget::itemActivated,&dialog,activate);
  for(int direction:{-1,1}) {auto *move=new QShortcut(QKeySequence(direction<0?Qt::Key_Up:Qt::Key_Down),search);QObject::connect(move,&QShortcut::activated,list,[list,direction]{if(!list->count())return;int row=list->currentRow();for(int i=0;i<list->count();++i){row=(row+direction+list->count())%list->count();auto *item=list->item(row);if(!item->isHidden()&&(item->flags()&Qt::ItemIsEnabled)){list->setCurrentItem(item);break;}}});}
  dialog.resize(QSize(600,440).boundedTo(dialog.screen()->availableGeometry().size()-QSize(32,48)));filter({});search->setFocus();
  if(dialog.exec()==QDialog::Accepted&&selected)QTimer::singleShot(0,window,[selected]{if(selected&&selected->isEnabled())selected->trigger();});
}
QWidget *shortcutSettings(QWidget *parent,QMainWindow *window) {
  auto *page=new QWidget(parent); page->setObjectName(QStringLiteral("researchShortcutSettings"));auto *layout=new QVBoxLayout(page);layout->setContentsMargins(0,0,0,0);
  auto *query=new QLineEdit(page);query->setPlaceholderText(englishUi()?QStringLiteral("Find an action"):Research::ui(QStringLiteral("搜索操作")));query->setObjectName(QStringLiteral("researchShortcutSearch"));layout->addWidget(query);
  auto *table=new QTableWidget(page);table->setObjectName(QStringLiteral("researchShortcutTable"));table->setColumnCount(2);table->setHorizontalHeaderLabels({englishUi()?QStringLiteral("Action"):Research::ui(QStringLiteral("操作")),englishUi()?QStringLiteral("Shortcut"):Research::ui(QStringLiteral("快捷键"))});table->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);table->horizontalHeader()->setSectionResizeMode(1,QHeaderView::ResizeToContents);table->verticalHeader()->hide();table->setSelectionBehavior(QAbstractItemView::SelectRows);table->setEditTriggers(QAbstractItemView::NoEditTriggers);configureSettingsList(table);table->setShowGrid(false);table->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft|Qt::AlignVCenter);table->verticalHeader()->setDefaultSectionSize(qMax(32,table->fontMetrics().height()+12));table->setMinimumHeight(160);layout->addWidget(table,1);
  auto actions=workspaceCommands(window);auto rank=[](QAction *action){int n=0;for(const auto &entry:commandDefinitions()){if(action->objectName()==QLatin1String(entry.id))return n;++n;}return n;};
  std::stable_sort(actions.begin(),actions.end(),[&](QAction *a,QAction *b){const int ra=rank(a),rb=rank(b);return ra==rb?a->text()<b->text():ra<rb;});
  for(auto *a:actions)if(a->isVisible()) {int n=table->rowCount();table->insertRow(n);auto *name=new QTableWidgetItem(a->text().remove(QLatin1Char('&')));name->setData(Qt::UserRole,a->objectName());name->setData(Qt::UserRole+1,commandSearchText(a));table->setItem(n,0,name);auto *key=new QTableWidgetItem(QKeySequence::listToString(a->shortcuts(),QKeySequence::PortableText));key->setData(Qt::UserRole+1,a->property("researchDefaultShortcuts").toStringList());table->setItem(n,1,key);}
  auto *record=new QKeySequenceEdit(page);record->setObjectName(QStringLiteral("researchShortcutRecorder"));record->setMaximumSequenceLength(4);auto *assignment=new QHBoxLayout;assignment->setSpacing(8);assignment->addWidget(record,1);layout->addLayout(assignment);
  auto *message=new QLabel(page);message->setObjectName(QStringLiteral("researchShortcutConflict"));message->setWordWrap(true);message->hide();layout->addWidget(message);
  auto *set=new QPushButton(englishUi()?QStringLiteral("Assign shortcut"):Research::ui(QStringLiteral("使用此快捷键")),page);set->setObjectName(QStringLiteral("researchShortcutAssign"));assignment->addWidget(set);
  QObject::connect(query,&QLineEdit::textChanged,page,[table](const QString &s){for(int n=0;n<table->rowCount();++n)table->setRowHidden(n,!table->item(n,0)->data(Qt::UserRole+1).toString().contains(s,Qt::CaseInsensitive));});
  QObject::connect(table,&QTableWidget::currentCellChanged,page,[=](int row,int,int,int){if(row>=0)record->setKeySequence(QKeySequence::listFromString(table->item(row,1)->text()).value(0));});
  QObject::connect(set,&QPushButton::clicked,page,[=]{int row=table->currentRow();if(row<0)return;const auto key=record->keySequence();message->show();
    auto conflict=[&](const QKeySequence &other){return !key.isEmpty()&&!other.isEmpty()&&(key.matches(other)!=QKeySequence::NoMatch||other.matches(key)!=QKeySequence::NoMatch);};
    for(int n=0;n<table->rowCount();++n)if(n!=row)for(const auto &other:QKeySequence::listFromString(table->item(n,1)->text()))if(conflict(other)){message->setText((englishUi()?QStringLiteral("Already used by: "):Research::ui(QStringLiteral("已用于：")))+table->item(n,0)->text());return;}
    if(window)for(auto *s:window->findChildren<QShortcut *>())for(const auto &other:s->keys())if(s->isEnabled()&&conflict(other)){message->setText(englishUi()?QStringLiteral("This key is already used in the editor."):Research::ui(QStringLiteral("该按键已用于编辑操作。")));return;}
    if(key==QKeySequence(Qt::Key_Tab)||key==QKeySequence(Qt::Key_Return)||key==QKeySequence(Qt::Key_Escape)){message->setText(englishUi()?QStringLiteral("Choose a key combination."):Research::ui(QStringLiteral("请使用组合键。")));return;}
    table->item(row,1)->setText(key.toString(QKeySequence::PortableText));table->item(row,1)->setData(Qt::UserRole,true);table->item(row,1)->setData(Qt::UserRole+2,false);message->clear();message->hide();
  });
  return page;
}
void resetShortcutSettings(QWidget *page){if(auto *table=page->findChild<QTableWidget *>(QStringLiteral("researchShortcutTable")))for(int n=0;n<table->rowCount();++n){auto *item=table->item(n,1);item->setText(item->data(Qt::UserRole+1).toStringList().join(QStringLiteral("; ")));item->setData(Qt::UserRole,true);item->setData(Qt::UserRole+2,true);}}
bool saveShortcutSettings(QWidget *page) {
  auto *table=page?page->findChild<QTableWidget *>(QStringLiteral("researchShortcutTable")):nullptr;if(!table)return true;
  auto custom=Tw::Settings().value(QStringLiteral("researchShortcutsV1")).toMap();
  for(int n=0;n<table->rowCount();++n)if(table->item(n,1)->data(Qt::UserRole).toBool()){const auto id=table->item(n,0)->data(Qt::UserRole).toString();if(table->item(n,1)->data(Qt::UserRole+2).toBool())custom.remove(id);else custom.insert(id,table->item(n,1)->text());}
  Tw::Settings().setValue(QStringLiteral("researchShortcutsV1"),custom);
  for(auto *w:QApplication::topLevelWidgets())if(auto *m=qobject_cast<QMainWindow *>(w))applyCommandShortcuts(m);
  return true;
}
}
