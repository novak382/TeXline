#include "ResearchSettings.h"
#include "ResearchAppearance.h"
#include "ResearchMotion.h"
#include "Settings.h"
#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFile>
#include <QFormLayout>
#include <QHash>
#include <QLabel>
#include <QKeyEvent>
#include <QSet>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QPointer>
#include <QRadioButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>
#include <QToolButton>
#include <QTimer>
#include <QVBoxLayout>

namespace Research {
QString words(const char *chinese,const char *english){return (englishUi()?QString::fromUtf8(english):Research::ui(QString::fromUtf8(chinese)));}
void configureSettingsSurface(QWidget *widget){widget->setFont(interfaceFont());widget->setProperty("researchWhiteSurface",true);widget->setAttribute(Qt::WA_StyledBackground,true);}
void configureSettingsList(QAbstractItemView *list){
  list->setProperty("researchSettingsList",true);
  list->setFrameShape(QFrame::NoFrame);
  list->setAlternatingRowColors(false);
  list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  list->setTextElideMode(Qt::ElideRight);
}
void localizeEditorModeChoices(QComboBox *indent,QComboBox *quotes){
  struct Choice {const char *id,*zh,*en;};
  auto label=[](QComboBox *box,const QList<Choice> &choices){
    for(int n=0;n<box->count();++n){const auto id=n?box->itemText(n):QString();box->setItemData(n,id);
      for(const auto &choice:choices)if(id==QLatin1String(choice.id)){box->setItemText(n,words(choice.zh,choice.en));break;}}
  };
  label(indent,{{"Whitespace only","跟随上一行","Follow previous line"},{"TeX comments","同时延续注释","Continue comments"},{"dtx prefixes","延续 dtx 前缀","Continue dtx prefixes"}});
  label(quotes,{{"TeX ligatures","LaTeX 引号","LaTeX quotes"},{"TeX commands","TeX 引号命令","TeX quote commands"},{"LaTeX csquotes","csquotes 宏包","csquotes package"},{"ConTeXt commands","ConTeXt 引号","ConTeXt quotes"},{"Unicode characters","弯引号","Curly quotes"}});
}
QVariant defaultPreference(const char *key,const QVariant &fallback){
  // This bundled, flat General section also seeds a new installation. Keep
  // factory reset tied to that same source instead of UI-font fallbacks.
  static const QHash<QString,QString> defaults=[] {QHash<QString,QString> out;QFile file(QStringLiteral(":/research/defaults.ini"));if(!file.open(QIODevice::ReadOnly))return out;bool general=false;for(const auto &line:QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'))){const auto text=line.trimmed();if(text.startsWith(QLatin1Char('['))){general=text==QStringLiteral("[General]");continue;}if(!general)continue;const int equal=int(text.indexOf(QLatin1Char('=')));if(equal<=0||text.startsWith(QLatin1Char('#'))||text.startsWith(QLatin1Char(';')))continue;auto value=text.mid(equal+1).trimmed();if(value.startsWith(QLatin1Char('"'))&&value.endsWith(QLatin1Char('"')))value=value.mid(1,value.size()-2);out.insert(text.left(equal).trimmed(),value);}return out;}();
  const auto it=defaults.constFind(QString::fromLatin1(key));if(it==defaults.cend())return fallback;
  if(fallback.metaType().id()==QMetaType::Bool)return *it==QStringLiteral("true")||*it==QStringLiteral("1");
  if(fallback.metaType().id()==QMetaType::Int){bool ok=false;const int value=it->toInt(&ok);return ok?QVariant(value):fallback;}
  return *it;
}
const QList<SettingDefinition> &settingDefinitions(){
  static const QList<SettingDefinition> entries{
    {"researchReduceMotion","减少动画","Reduce motion",false,"application","motion 动画"},
    {"researchStartRecentFiles","空白页显示最近文件","Show recent files on the blank page",true,"application","开始 最近 文件 隐私 start recent privacy"},
    {"researchDetectBeamer","自动识别幻灯片文稿","Recognize slide documents",true,"application","Beamer slides 幻灯片"},
    {"researchFocusLineNumbers","专注时显示行号","Show line numbers in focus mode",false,"focus","focus 专注 行号"},
    {"researchTypewriterMode","打字机模式","Typewriter mode",false,"application","输入 当前行 舒适高度 typewriter scroll"},
    {"researchShorthandEnabled","启用片段展开","Expand snippets",false,"focus","速记 shorthand"},
    {"researchCommandCompletionMode","显示补全候选","Suggestions",0,"application","补全 关闭 手动 自动 completion"},
    {"researchCompletionCtrlSpace","同时使用 Ctrl+Space 触发","Also use Ctrl+Space",false,"application","completion shortcut 补全 快捷键"}
  };return entries;
}
const SettingDefinition &settingDefinition(const char *key){for(const auto &d:settingDefinitions())if(qstrcmp(d.key,key)==0)return d;Q_UNREACHABLE();}
QVariant settingValue(const char *key){const auto &d=settingDefinition(key);return Tw::Settings().value(QString::fromLatin1(d.key),defaultPreference(d.key,d.initial));}
QWidget *settingControl(const char *key,QWidget *parent){
  const auto &d=settingDefinition(key);QWidget *control=nullptr;
  if(d.initial.metaType().id()==QMetaType::Bool){auto *box=new QCheckBox(words(d.chinese,d.english),parent);box->setChecked(settingValue(key).toBool());control=box;}
  else {auto *box=new QComboBox(parent);box->addItems({words("关闭","Off"),words("按快捷键 · Alt+/","With shortcut · Alt+/"),words("输入时提示","As you type")});box->setCurrentIndex(qBound(0,settingValue(key).toInt(),2));control=box;}
  control->setObjectName(QString::fromLatin1(d.key)+QStringLiteral("Setting"));control->setProperty("settingKey",QString::fromLatin1(d.key));control->setProperty("settingDefault",defaultPreference(d.key,d.initial));control->setProperty("settingScope",QString::fromLatin1(d.scope));control->setProperty("settingTitle",words(d.chinese,d.english));control->setProperty("settingSearch",words(d.chinese,d.english)+QLatin1Char(' ')+QString::fromUtf8(d.keywords));return control;
}
void saveSettingControls(QWidget *root){for(auto *w:root->findChildren<QWidget *>())if(w->property("settingKey").isValid()){QVariant v;if(auto *b=qobject_cast<QCheckBox *>(w))v=b->isChecked();else if(auto *c=qobject_cast<QComboBox *>(w))v=c->currentIndex();if(v.isValid())Tw::Settings().setValue(w->property("settingKey").toString(),v);}}
void resetSettingControls(QWidget *page){for(auto *w:page->findChildren<QWidget *>())if(w->property("settingDefault").isValid()){if(auto *b=qobject_cast<QCheckBox *>(w))b->setChecked(w->property("settingDefault").toBool());else if(auto *c=qobject_cast<QComboBox *>(w))c->setCurrentIndex(w->property("settingDefault").toInt());}}
QVBoxLayout *settingsPage(QTabWidget *tabs,const char *id,const char *chinese,const char *english){
  auto *scroll=new QScrollArea(tabs);scroll->setObjectName(QString::fromLatin1(id));scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  auto *content=new QWidget;content->setObjectName(QStringLiteral("settingsPageContent"));auto *layout=new QVBoxLayout(content);layout->setContentsMargins(20,8,20,16);layout->setSpacing(6);scroll->setWidget(content);tabs->addTab(scroll,words(chinese,english));return layout;
}
void settingsHeading(QVBoxLayout *layout,const QString &title){auto *label=new QLabel(title);label->setProperty("settingsHeading",true);if(layout->count())layout->addSpacing(10);label->setContentsMargins(0,3,0,3);layout->addWidget(label);}
QVBoxLayout *settingsDetails(QVBoxLayout *layout,const char *id,const QString &title){
  auto *toggle=new QToolButton;toggle->setObjectName(QString::fromLatin1(id)+QStringLiteral("Toggle"));toggle->setText(title);toggle->setCheckable(true);toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);toggle->setProperty("settingsDisclosureTrigger",true);toggle->setProperty("settingTitle",title);toggle->setProperty("settingSearch",title);
  toggle->setIcon(QIcon(QStringLiteral(":/research/icons/chevron-right.svg")));configureButton(toggle,ButtonRole::Text);layout->addWidget(toggle,0,Qt::AlignLeft);
  auto *body=new QWidget;body->setObjectName(QString::fromLatin1(id));body->setProperty("settingsDisclosure",QVariant::fromValue(static_cast<QObject *>(toggle)));auto *rows=new QVBoxLayout(body);rows->setContentsMargins(0,4,0,8);rows->setSpacing(6);layout->addWidget(body);body->hide();
  QObject::connect(toggle,&QToolButton::toggled,body,[=](bool on){body->setVisible(on);toggle->setIcon(QIcon(on?QStringLiteral(":/research/icons/chevron.svg"):QStringLiteral(":/research/icons/chevron-right.svg")));});return rows;
}
QWidget *revealSetting(QWidget *target){
  if(!target)return nullptr;
  auto *surface=target->window();surface->setProperty("settingsRevealRevision",surface->property("settingsRevealRevision").toULongLong()+1);
  for(auto *p=target;p;p=p->parentWidget())if(auto *fallback=qobject_cast<QWidget *>(p->property("settingsRevealFallback").value<QObject *>());fallback&&fallback!=target)return revealSetting(fallback);
  if(auto *toggle=qobject_cast<QToolButton *>(target);toggle&&toggle->property("settingsDisclosureTrigger").toBool())toggle->setChecked(true);
  // Open only explicit disclosures/stacks: never reveal unrelated hidden
  // validation labels or override a control's own visibility contract.
  for(auto *p=target;p;p=p->parentWidget()){
    if(auto *toggle=qobject_cast<QAbstractButton *>(p->property("settingsDisclosureClosedBy").value<QObject *>())){toggle->setProperty("settingsSearchReveal",true);toggle->setChecked(false);toggle->setProperty("settingsSearchReveal",false);}
    if(auto *toggle=qobject_cast<QAbstractButton *>(p->property("settingsDisclosure").value<QObject *>()))toggle->setChecked(true);
    if(auto *stack=qobject_cast<QStackedWidget *>(p->parentWidget()))stack->setCurrentWidget(p);
  }
  const auto revision=surface->property("settingsRevealRevision");
  auto scrollToTarget=[guard=QPointer<QWidget>(target),owner=QPointer<QWidget>(surface),revision]{
    if(!guard||!owner||owner->property("settingsRevealRevision")!=revision)return;
    QList<QWidget *> parents;for(auto *p=guard->parentWidget();p;p=p->parentWidget()){parents.prepend(p);if(p==owner)break;}
    for(auto *p:parents)if(p->layout())p->layout()->activate();
    QWidget *row=guard;if(auto *p=guard->parentWidget();p&&p->property("settingTarget").toString()==guard->objectName())row=p;
    for(auto *p=guard->parentWidget();p;p=p->parentWidget())if(auto *area=qobject_cast<QScrollArea *>(p))area->ensureWidgetVisible(row,12,12);
  };
  scrollToTarget();
  // Opening details and dismissing search results both resize the viewport.
  // Position again after their queued layout, only for the latest request.
  QTimer::singleShot(0,target,scrollToTarget);
  return target;
}
void settingsRow(QVBoxLayout *layout,const QString &title,QWidget *control,const QString &keywords){
  auto *row=new QWidget;row->setProperty("settingSearch",title+QLatin1Char(' ')+keywords+QLatin1Char(' ')+control->property("settingSearch").toString());auto *form=new QFormLayout(row);form->setContentsMargins(0,0,0,0);form->setHorizontalSpacing(16);form->setLabelAlignment(Qt::AlignLeft|Qt::AlignVCenter);form->setVerticalSpacing(8);form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);form->setRowWrapPolicy(QFormLayout::WrapLongRows);
  QString caption=title.isEmpty()?control->property("settingTitle").toString():title;if(caption.isEmpty())if(auto *button=qobject_cast<QAbstractButton *>(control))caption=button->text();
  row->setProperty("settingTitle",caption);row->setProperty("settingSearch",caption+QLatin1Char(' ')+row->property("settingSearch").toString());if(control->objectName().isEmpty())control->setObjectName(QStringLiteral("settingControl%1").arg(quintptr(control),0,16));row->setProperty("settingTarget",control->objectName());control->setProperty("settingSearch",QVariant());
  if(title.isEmpty())form->addRow(control);else {auto *label=new QLabel(title);label->setFixedWidth(120);label->setWordWrap(true);label->setBuddy(control);form->addRow(label,control);}
  if(qobject_cast<QCheckBox *>(control)||qobject_cast<QRadioButton *>(control))control->setMinimumHeight(qMax(26,control->fontMetrics().height()+6));
  control->setMinimumWidth(0);control->setMaximumWidth(qobject_cast<QAbstractItemView *>(control)?QWIDGETSIZE_MAX:600);control->setSizePolicy(QSizePolicy::Expanding,control->sizePolicy().verticalPolicy());layout->addWidget(row);
}
class SettingsNavigation final:public QObject {
  QDialog *dialog;QListWidget *navigation;QComboBox *compact;
public:SettingsNavigation(QDialog *d,QListWidget *n,QComboBox *c):QObject(d),dialog(d),navigation(n),compact(c){d->installEventFilter(this);adapt();}
  void adapt(){const bool narrow=dialog->width()<680;navigation->setVisible(!narrow);compact->setVisible(narrow);}
  bool eventFilter(QObject *,QEvent *e)override{if(e->type()==QEvent::Resize)adapt();return false;}
};
class SettingsSearchKeys final:public QObject {
  QLineEdit *field;QListWidget *matches;
public:SettingsSearchKeys(QLineEdit *f,QListWidget *m):QObject(f),field(f),matches(m){f->installEventFilter(this);m->installEventFilter(this);}
  bool eventFilter(QObject *object,QEvent *event)override {
    if(event->type()!=QEvent::KeyPress)return false;
    const auto key=static_cast<QKeyEvent*>(event)->key();
    if(key==Qt::Key_Escape&&!field->text().isEmpty()){field->clear();field->setFocus();return true;}
    if(object==field&&(key==Qt::Key_Return||key==Qt::Key_Enter)){QMetaObject::invokeMethod(field,"returnPressed");return true;}
    if(object==field&&matches->isVisible()&&(key==Qt::Key_Down||key==Qt::Key_Up)){matches->setCurrentRow(qBound(0,matches->currentRow()+(key==Qt::Key_Down?1:-1),matches->count()-1));return true;}
    return false;
  }
};
class CategoryDelegate final:public QStyledItemDelegate {
public:using QStyledItemDelegate::QStyledItemDelegate;
  void paint(QPainter *p,const QStyleOptionViewItem &option,const QModelIndex &index)const override {
    auto opt=option;
    // Pointer selection already has a full-row surface. Retain the native
    // focus cue only when the user is navigating with the keyboard.
    if(!(opt.state&QStyle::State_KeyboardFocusChange))opt.state&=~QStyle::State_HasFocus;
    QStyledItemDelegate::paint(p,opt,index);
  }
};
void installSettingsShell(QDialog *dialog,QTabWidget *tabs,QDialogButtonBox *buttons){
  configureSettingsSurface(dialog);
  auto *old=dialog->layout();delete old;
  dialog->setWindowTitle(words("设置","Settings"));dialog->setObjectName(QStringLiteral("researchSettingsDialog"));dialog->setProperty("researchWhiteSurface",true);
  dialog->setStyleSheet(QStringLiteral("QDialog#researchSettingsDialog,QWidget#settingsPageContent {background:#ffffff;} QTabWidget#tabWidget::pane{border:0;margin:0;} QListWidget#researchSettingsNavigation{background:#f6f7f8;border:0;padding:8px;} QListWidget#researchSettingsNavigation::item{padding:8px 8px;border-radius:5px;} QListWidget#researchSettingsNavigation::item:selected{background:#e6e9ec;color:#20252a;} QGroupBox{border:0;margin-top:22px;padding-top:10px;} QGroupBox::title{subcontrol-origin:margin;left:0;font-weight:600;}"));
  auto *outer=new QVBoxLayout(dialog);outer->setContentsMargins(16,16,16,12);outer->setSpacing(12);
  auto *search=new QLineEdit(dialog);search->setObjectName(QStringLiteral("researchSettingsSearch"));search->setPlaceholderText(words("搜索设置","Search settings"));search->setClearButtonEnabled(true);configureField(search);outer->addWidget(search);
  auto *matches=new QListWidget(dialog);matches->setObjectName(QStringLiteral("researchSettingsMatches"));matches->setMaximumHeight(160);matches->hide();configureSettingsList(matches);new SettingsSearchKeys(search,matches);outer->addWidget(matches);
  auto *compact=new QComboBox(dialog);compact->setObjectName(QStringLiteral("researchSettingsCategory"));outer->addWidget(compact);
  auto *body=new QHBoxLayout;body->setSpacing(8);auto *navigation=new QListWidget(dialog);navigation->setObjectName(QStringLiteral("researchSettingsNavigation"));navigation->setItemDelegate(new CategoryDelegate(navigation));navigation->setFixedWidth(160);body->addWidget(navigation);body->addWidget(tabs,1);outer->addLayout(body,1);tabs->tabBar()->hide();
  for(int i=0;i<tabs->count();++i){navigation->addItem(tabs->tabText(i));compact->addItem(tabs->tabText(i));}
  navigation->ensurePolished();int categoryWidth=160;
  for(int i=0;i<tabs->count();++i)categoryWidth=qMax(categoryWidth,navigation->fontMetrics().horizontalAdvance(tabs->tabText(i))+40);
  navigation->setFixedWidth(categoryWidth);
  QObject::connect(navigation,&QListWidget::currentRowChanged,tabs,&QTabWidget::setCurrentIndex);QObject::connect(compact,&QComboBox::currentIndexChanged,tabs,&QTabWidget::setCurrentIndex);
  for(int i=0;i<tabs->count();++i)if(auto *area=qobject_cast<QScrollArea *>(tabs->widget(i))){
    QObject::connect(area->verticalScrollBar(),&QScrollBar::valueChanged,area,[area]{cancelArrival(area->viewport());});
    QObject::connect(area->horizontalScrollBar(),&QScrollBar::valueChanged,area,[area]{cancelArrival(area->viewport());});
  }
  QObject::connect(tabs,&QTabWidget::currentChanged,dialog,[=](int n){
    navigation->setCurrentRow(n);compact->setCurrentIndex(n);
    // Each page keeps its own live scroll/focus state. No outgoing overlay or
    // queued transition can cover the new page after another selection.
    for(int i=0;i<tabs->count();++i)if(auto *area=qobject_cast<QScrollArea *>(tabs->widget(i)))cancelArrival(area->viewport());
    if(auto *area=qobject_cast<QScrollArea *>(tabs->widget(n));area&&dialog->isVisible()){
      area->viewport()->setProperty("researchArrivalOpaque",true);
      animateArrival(area->viewport());
    }
  });navigation->setCurrentRow(tabs->currentIndex());compact->setCurrentIndex(tabs->currentIndex());
  outer->addWidget(buttons);buttons->button(QDialogButtonBox::Ok)->setText(words("保存","Save"));buttons->button(QDialogButtonBox::RestoreDefaults)->setText(words("重置本页","Reset page"));buttons->button(QDialogButtonBox::RestoreDefaults)->setFlat(true);
  auto reveal=[=](QListWidgetItem *item){if(!item||!(item->flags()&Qt::ItemIsEnabled))return;int n=item->data(Qt::UserRole).toInt();tabs->setCurrentIndex(n);auto *target=tabs->widget(n)->findChild<QWidget *>(item->data(Qt::UserRole+1).toString());if(target){target=revealSetting(target);target->setFocus();dialog->setProperty("researchSettingsSearchTarget",target->objectName());}matches->hide();};
  QObject::connect(search,&QLineEdit::textChanged,dialog,[=](const QString &s){matches->clear();matches->setVisible(!s.trimmed().isEmpty());if(s.trimmed().isEmpty())return;QSet<QWidget*> seen;for(int n=0;n<tabs->count();++n)for(auto *w:tabs->widget(n)->findChildren<QWidget *>()){const auto text=w->property("settingSearch").toString();if(text.isEmpty()||!uiSearchText(text).contains(uiSearchText(s.trimmed())))continue;QWidget *target=w;if(!w->property("settingTarget").toString().isEmpty())if(auto *field=w->findChild<QWidget *>(w->property("settingTarget").toString()))target=field;if(seen.contains(target))continue;seen.insert(target);if(target->objectName().isEmpty())target->setObjectName(QStringLiteral("settingsTarget%1").arg(matches->count()));auto *item=new QListWidgetItem(tabs->tabText(n)+QStringLiteral(" · ")+w->property("settingTitle").toString(),matches);item->setData(Qt::UserRole,n);item->setData(Qt::UserRole+1,target->objectName());}if(!matches->count()){auto *item=new QListWidgetItem(words("没有匹配的设置","No matching settings"),matches);item->setFlags(Qt::NoItemFlags);}else matches->setCurrentRow(0);});
  QObject::connect(matches,&QListWidget::itemClicked,dialog,reveal);QObject::connect(matches,&QListWidget::itemActivated,dialog,reveal);QObject::connect(search,&QLineEdit::returnPressed,dialog,[=]{reveal(matches->currentItem());});
  dialog->setMinimumSize(0,0);dialog->resize(QSize(820,620).boundedTo(dialog->screen()->availableGeometry().size()-QSize(32,48)));new SettingsNavigation(dialog,navigation,compact);
}
}
