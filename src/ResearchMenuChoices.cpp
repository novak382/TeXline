#include "ResearchMenuChoices.h"
#include "ResearchSettings.h"
#include "Settings.h"
#include <QAction>
#include <QActionEvent>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QMenu>
#include <QPointer>
#include <QStackedWidget>
#include <QTabWidget>
#include <QVBoxLayout>

namespace Research {
namespace {
struct Choice { MenuSurface surface; const char *key, *zh, *en, *actions; };
const QList<Choice> &choices() {
  static const QList<Choice> entries{
    {MenuSurface::Context,"contextPin","固定到下方","Pin below","researchContextPin"},
    {MenuSurface::Context,"contextSnippet","存为片段…","Save as snippet…","researchSaveSnippet"},
    {MenuSurface::Context,"contextQuotation","设为引文","Set as quotation","researchWrapQuotation"},
    {MenuSurface::Context,"contextInsert","插入子菜单","Insert submenu","researchInsertMenu"},
    {MenuSurface::Edit,"editLines","移动、复制和删除行","Move, duplicate and delete lines","researchLineEdit0 researchLineEdit1 researchLineEdit2 researchLineEdit3"},
    {MenuSurface::Edit,"editCompletion","补全与片段快捷入口","Completion and snippet shortcuts","researchShowCompletion researchConfigureCompletion"},
    {MenuSurface::Insert,"insertQuotation","设为引文","Set as quotation","researchWrapQuotation"},
    {MenuSurface::Insert,"insertSnippet","存为片段…","Save as snippet…","researchSaveSnippet"},
    {MenuSurface::Insert,"insertMacro","自定义命令…","Custom command…","researchCreateCommand"},
    {MenuSurface::Slides,"slidesPictures","右键：插入与调整图片","Context menu: insert and adjust pictures","researchSlideInsertImage researchSlideAdjustImage"},
    {MenuSurface::Slides,"slidesNotes","右键：讲者备注","Context menu: speaker notes","researchSlideNotes"},
    {MenuSurface::Slides,"slidesSteps","右键：上一步与下一步","Context menu: previous and next step","researchSlidePrevious researchSlideNext"},
    {MenuSurface::Slides,"slidesExit","右键：退出幻灯片模式","Context menu: exit slide mode","researchSlideContextExit"},
    {MenuSurface::Slides,"slidesPreamble","目录菜单：导言快捷入口","Outline menu: preamble shortcut","researchSourcePreambleMenuAction"},
    {MenuSurface::Slides,"slidesThumbnail","目录菜单：缩略图","Outline menu: thumbnail","researchSlideThumbnailMenuAction"}
  };
  return entries;
}
QString settingKey(const Choice &entry) { return QStringLiteral("researchMenu/")+QString::fromLatin1(entry.key); }
QString identity(QAction *action) { return action->menu()?action->menu()->objectName():action->objectName(); }

// A menu borrows commands. Removing its entry must never hide/disable the
// QAction or remove it from the widget that owns its keyboard shortcut.
class MenuChoices final : public QObject {
  QMenu *menu;
  MenuSurface surface;
  QList<QPointer<QAction>> order;
  bool applying=false;
public:
  MenuChoices(QMenu *m,MenuSurface s):QObject(m),menu(m),surface(s) {
    setObjectName(QStringLiteral("researchMenuChoices"));
    for(auto *a:m->actions())order.append(a);
    m->installEventFilter(this);
    connect(m,&QMenu::aboutToShow,this,[this]{apply();});
  }
  bool eventFilter(QObject *,QEvent *event) override {
    if(applying)return false;
    if(event->type()==QEvent::ActionAdded){
      const auto *added=static_cast<QActionEvent *>(event);
      order.removeAll(added->action());
      const auto before=order.indexOf(added->before());
      order.insert(before<0?order.size():before,added->action());
    }else if(event->type()==QEvent::ActionRemoved)order.removeAll(static_cast<QActionEvent *>(event)->action());
    return false;
  }
  void apply() {
    applying=true;
    for(auto *a:menu->actions())menu->removeAction(a);
    for(const auto &a:order)if(a&&menuEntryEnabled(surface,identity(a)))menu->addAction(a);
    applying=false;
  }
};
}
bool menuEntryEnabled(MenuSurface surface,const QString &action) {
  for(const auto &entry:choices())if(entry.surface==surface&&QString::fromLatin1(entry.actions).split(QLatin1Char(' ')).contains(action))
    return Tw::Settings().value(settingKey(entry),false).toBool();
  return true;
}
void filterMenuEntries(QMenu *menu,MenuSurface surface) {
  for(auto *a:menu->actions())if(!menuEntryEnabled(surface,identity(a)))menu->removeAction(a);
  menu->setSeparatorsCollapsible(true);
}
void installMenuChoices(QMenu *menu,MenuSurface surface) {
  if(menu&&!menu->findChild<QObject *>(QStringLiteral("researchMenuChoices"),Qt::FindDirectChildrenOnly))new MenuChoices(menu,surface);
}
void appendMenuChoiceSettings(QTabWidget *tabs) {
  auto *layout=settingsPage(tabs,"settingsMenus","菜单与面板","Menus and panels");
  appendMenuChoiceSettings(layout);
}
void appendMenuChoiceSettings(QVBoxLayout *layout) {
  auto *area=new QComboBox;area->setObjectName(QStringLiteral("researchMenuSurface"));
  area->addItems({words("正文右键","Editor context menu"),words("编辑菜单","Edit menu"),words("插入菜单","Insert menu"),words("幻灯片菜单","Slide menus")});
  settingsRow(layout,words("位置","Location"),area);
  auto *stack=new QStackedWidget;stack->setObjectName(QStringLiteral("researchMenuChoicePages"));layout->addWidget(stack);
  for(int i=0;i<4;++i){
    auto *page=new QWidget;auto *rows=new QVBoxLayout(page);rows->setContentsMargins(0,0,0,0);rows->setSpacing(6);
    settingsHeading(rows,words("额外显示","Also show"));
    for(const auto &entry:choices())if(int(entry.surface)==i){
      auto *box=new QCheckBox(words(entry.zh,entry.en));box->setObjectName(QString::fromLatin1(entry.key));
      box->setProperty("settingKey",settingKey(entry));box->setProperty("settingDefault",false);
      box->setProperty("settingSearch",words(entry.zh,entry.en));box->setChecked(Tw::Settings().value(settingKey(entry),false).toBool());
      settingsRow(rows,{},box);
    }
    rows->addStretch();stack->addWidget(page);
  }
  QObject::connect(area,qOverload<int>(&QComboBox::currentIndexChanged),stack,&QStackedWidget::setCurrentIndex);
  QObject::connect(stack,&QStackedWidget::currentChanged,area,&QComboBox::setCurrentIndex);
  layout->addStretch();
}
}
