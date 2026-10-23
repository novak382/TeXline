#include "ResearchFind.h"
#include "ResearchAppearance.h"
#include "TeXDocumentWindow.h"
#include "CompletingEdit.h"
#include "Settings.h"
#include <QCheckBox>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QSignalBlocker>
#include <QShortcut>
#include <QTextBlock>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QResizeEvent>
#include <QMenu>
#include <QWidgetAction>
namespace Research {
class FindController final:public QObject {
  TeXDocumentWindow *owner;
  QToolBar *bar;QLineEdit *query,*replacement;QLabel *count;
  QCheckBox *sensitive,*words,*regex;QComboBox *scope;
  QWidget *body{nullptr};QMenu *moreMenu{nullptr},*replaceMenu{nullptr};
  QToolButton *moreButton{nullptr},*previous{nullptr},*following{nullptr},*closeButton{nullptr};
  void arrange() {
    const bool compact=bar->width()<450;
    count->setVisible(!compact);previous->setVisible(!compact);following->setVisible(!compact);
    closeButton->setVisible(bar->width()>=320);
    if(scope->currentIndex()>=0)scope->setFixedWidth(scope->fontMetrics().horizontalAdvance(scope->currentText())+56);
  }
  void showReplacement() {
    const int width=replaceMenu->sizeHint().width();
    replaceMenu->popup(bar->mapToGlobal(QPoint(qMax(0,bar->width()-width),bar->height())));
    replacement->setFocus();replacement->selectAll();
  }
  bool eventFilter(QObject *object,QEvent *event) override {
    if(object==bar&&event->type()==QEvent::Resize)arrange();
    return QObject::eventFilter(object,event);
  }
  QTimer debounce;QTextCursor selectedRange,slideRange;
  struct Match {QPointer<TeXDocumentWindow> window;QRegularExpressionMatch match;};QVector<Match> matches;
  QRegularExpression pattern;bool outdated{true},selecting{false},limited{false};int current{-1};
  QString source;int revision{-1};bool sourceWasSliced{false},preferSlice{false};
  QString scopeCaption(const QString &mode) const {
    if(mode==QStringLiteral("slide"))return owner->property("researchArticleMode").toBool()?(englishUi()?QStringLiteral("Section source"):Research::ui(QStringLiteral("本节源码"))):(englishUi()?QStringLiteral("Slide source"):Research::ui(QStringLiteral("当前张源码")));
    if(mode==QStringLiteral("selection"))return englishUi()?QStringLiteral("Selection source"):Research::ui(QStringLiteral("选区源码"));
    if(mode==QStringLiteral("open"))return englishUi()?QStringLiteral("Open sources"):Research::ui(QStringLiteral("已打开源码"));
    return englishUi()?QStringLiteral("File source"):Research::ui(QStringLiteral("文件源码"));
  }
  QPair<int,int> range(TeXDocumentWindow *window) const {
    const auto mode=scope->currentData().toString();const auto cursor=mode==QStringLiteral("slide")?slideRange:selectedRange;
    if(window==owner&&(mode==QStringLiteral("slide")||mode==QStringLiteral("selection")))return {cursor.selectionStart(),cursor.selectionEnd()};
    return {0,window->textDoc()->characterCount()-1};
  }
  void refresh() {
    debounce.stop();if(!outdated&&revision==owner->textDoc()->revision()&&scope->currentData()!=QStringLiteral("open"))return;
    outdated=false;revision=owner->textDoc()->revision();matches.clear();current=-1;limited=false;
    if(query->text().isEmpty()){count->clear();return;}
    QString expression=regex->isChecked()?query->text():QRegularExpression::escape(query->text());
    if(words->isChecked())expression=QStringLiteral("(?<!\\w)(?:")+expression+QStringLiteral(")(?!\\w)");
    pattern=QRegularExpression(expression,QRegularExpression::UseUnicodePropertiesOption|(sensitive->isChecked()?QRegularExpression::NoPatternOption:QRegularExpression::CaseInsensitiveOption));
    if(!pattern.isValid()){count->setText(englishUi()?QStringLiteral("Invalid"):Research::ui(QStringLiteral("正则无效")));count->setToolTip(pattern.errorString());return;}count->setToolTip(QString());
    auto windows=scope->currentData()==QStringLiteral("open")?TeXDocumentWindow::documentList():QList<TeXDocumentWindow *>{owner};windows.removeAll(owner);windows.prepend(owner);
    for(auto *window:windows){const auto bounds=range(window);auto it=pattern.globalMatch(window->textDoc()->editorText(),bounds.first);
      while(it.hasNext()){auto match=it.next();if(match.capturedStart()>bounds.second)break;if(match.capturedEnd()>bounds.second)continue;matches.append({window,match});if(matches.size()>=50000){limited=true;break;}}if(limited)break;
    }
    updateCount();
  }
  void updateCount(){count->setText(limited?QStringLiteral("50000+"):QStringLiteral("%1 / %2").arg(current<0?0:current+1).arg(matches.size()));count->setToolTip(limited?ui(QStringLiteral("结果过多，请缩小范围")):matches.isEmpty()?ui(QStringLiteral("没有匹配项")):scope->currentText());}
  void replace(bool all) {
    outdated=true;refresh();if(matches.isEmpty()||!pattern.isValid())return;
    const QString value=regex->isChecked()?TeXDocumentWindow::replacementInput(replacement->text()):replacement->text();
    if(all){int changed=0;auto windows=scope->currentData()==QStringLiteral("open")?TeXDocumentWindow::documentList():QList<TeXDocumentWindow *>{owner};
      for(auto *window:windows)if(!window->editor()->isReadOnly()){const auto bounds=range(window);changed+=window->doReplaceAll(query->text(),&pattern,value,{},bounds.first,bounds.second);}
      outdated=true;refresh();showTransientStatusMessage(owner,ui(QStringLiteral("已替换 %1 处，可撤销")).arg(changed));return;
    }
    int index=-1;for(int i=0;i<matches.size();++i)if(matches[i].window&&matches[i].window->textCursor().selectionStart()==matches[i].match.capturedStart()&&matches[i].window->textCursor().selectionEnd()==matches[i].match.capturedEnd()){index=i;break;}
    if(index<0){next(false);return;}
    const auto found=matches[index];if(!found.window||found.window->editor()->isReadOnly())return;
    auto cursor=found.window->textCursor();cursor.beginEditBlock();cursor.insertText(regex->isChecked()?TeXDocumentWindow::replacementForMatch(found.match,value):value);cursor.endEditBlock();found.window->editor()->setTextCursor(cursor);
    outdated=true;next(false);
  }
public:
  void sourceScopeChanged() {
    const bool sliced=owner->editor()->property("researchSourceSlice").toBool();
    if(sliced!=sourceWasSliced){
      sourceWasSliced=sliced;preferSlice=sliced;
      if(sliced&&bar->isVisible()){
        if(scope->findData(QStringLiteral("slide"))<0)scope->insertItem(0,scopeCaption(QStringLiteral("slide")),QStringLiteral("slide"));
        scope->setCurrentIndex(scope->findData(QStringLiteral("slide")));preferSlice=false;
      }
    }
    if(scope->currentData()!=QStringLiteral("slide"))return;
    if(!owner->editor()->property("researchSourceSlice").toBool()){
      scope->setCurrentIndex(scope->findData(QStringLiteral("file")));
    }else{
      int begin=-1,end=0;
      for(auto block=owner->textDoc()->begin();block.isValid();block=block.next())if(block.isVisible()){
        if(begin<0)begin=block.position();end=block.position()+block.length()-1;
      }
      slideRange=QTextCursor(owner->textDoc());slideRange.setPosition(qMax(0,begin));
      slideRange.setPosition(qMin(end,owner->textDoc()->characterCount()-1),QTextCursor::KeepAnchor);
      scope->setItemText(scope->currentIndex(),scopeCaption(QStringLiteral("slide")));
    }
    outdated=true;if(bar->isVisible())debounce.start();
  }
  explicit FindController(TeXDocumentWindow *w):QObject(w),owner(w) {
    setObjectName(QStringLiteral("researchFindController"));bar=new QToolBar(w);bar->setObjectName(QStringLiteral("researchFindBar"));bar->setMovable(false);bar->setProperty("researchCompactFind",true);configureToolbar(bar);
    body=new QWidget(bar);body->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);
    auto *row=new QHBoxLayout(body);row->setSizeConstraint(QLayout::SetNoConstraint);row->setContentsMargins(4,2,4,2);row->setSpacing(4);
    query=new QLineEdit(body);query->setObjectName(QStringLiteral("researchFindField"));query->setPlaceholderText(englishUi()?QStringLiteral("Find in source"):Research::ui(QStringLiteral("查找源码")));query->setClearButtonEnabled(true);configureField(query);query->setMinimumWidth(60);query->setMaximumWidth(360);row->addWidget(query,1);
    scope=new QComboBox(body);scope->setFont(query->font());scope->setObjectName(QStringLiteral("researchFindScope"));scope->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);scope->setAccessibleName(englishUi()?QStringLiteral("Source search scope"):Research::ui(QStringLiteral("源码查找范围")));scope->setToolTip(scope->accessibleName());row->addWidget(scope);
    count=new QLabel(body);count->setObjectName(QStringLiteral("researchFindCount"));count->setMinimumWidth(0);count->setMaximumWidth(70);row->addWidget(count);
    auto icon=[&](const QString &name,const QString &file,const QString &caption){auto *button=new QToolButton(body);button->setObjectName(name);button->setIcon(QIcon(file));button->setAccessibleName(caption);button->setToolTip(caption);configureButton(button,ButtonRole::Icon);row->addWidget(button);return button;};
    previous=icon(QStringLiteral("researchFindPrevious"),QStringLiteral(":/research/icons/chevron-up.svg"),ui(QStringLiteral("查找上一个"))+QStringLiteral(" · Shift+Enter"));
    following=icon(QStringLiteral("researchFindNext"),QStringLiteral(":/research/icons/chevron.svg"),ui(QStringLiteral("查找下一个"))+QStringLiteral(" · Enter"));
    moreButton=icon(QStringLiteral("researchFindMore"),QStringLiteral(":/research/icons/more.svg"),englishUi()?QStringLiteral("Replace and search options"):Research::ui(QStringLiteral("替换与查找选项")));
    closeButton=icon(QStringLiteral("researchFindClose"),QStringLiteral(":/research/icons/close.svg"),ui(QStringLiteral("关闭"))+QStringLiteral(" · Esc"));row->addStretch();
    moreMenu=new QMenu(moreButton);moreMenu->setObjectName(QStringLiteral("researchFindMenu"));moreButton->setMenu(moreMenu);moreButton->setPopupMode(QToolButton::InstantPopup);
    auto *replaceAction=moreMenu->addAction(ui(QStringLiteral("替换"))+QStringLiteral("…"));replaceAction->setObjectName(QStringLiteral("researchFindReplaceToggle"));connect(replaceAction,&QAction::triggered,this,[this]{showReplacement();});
    moreMenu->addAction(ui(QStringLiteral("查找上一个")),previous,&QToolButton::click);moreMenu->addAction(ui(QStringLiteral("查找下一个")),following,&QToolButton::click);moreMenu->addSeparator();
    auto option=[&](const QString &text,const QString &name){auto *box=new QCheckBox(text,body);box->setObjectName(name);box->hide();auto *action=moreMenu->addAction(text);action->setCheckable(true);connect(action,&QAction::toggled,box,&QCheckBox::setChecked);connect(box,&QCheckBox::toggled,action,&QAction::setChecked);return box;};
    sensitive=option(ui(QStringLiteral("区分大小写")),QStringLiteral("researchFindCase"));words=option(ui(QStringLiteral("全词匹配")),QStringLiteral("researchFindWords"));regex=option(ui(QStringLiteral("正则表达式")),QStringLiteral("researchFindRegex"));
    auto *results=moreMenu->addAction(ui(QStringLiteral("列出结果")));results->setObjectName(QStringLiteral("researchFindAll"));
    connect(results,&QAction::triggered,this,[this]{refresh();if(!pattern.isValid()||query->text().isEmpty())return;Tw::Settings settings;settings.setValue(QStringLiteral("searchText"),pattern.pattern());settings.setValue(QStringLiteral("searchRegex"),true);settings.setValue(QStringLiteral("searchFlags"),int(sensitive->isChecked()?QTextDocument::FindCaseSensitively:QTextDocument::FindFlags()));settings.setValue(QStringLiteral("searchFindAll"),true);settings.setValue(QStringLiteral("searchAllFiles"),scope->currentData()==QStringLiteral("open"));const bool selection=scope->currentData()==QStringLiteral("slide")||scope->currentData()==QStringLiteral("selection");settings.setValue(QStringLiteral("searchSelection"),selection);if(selection)owner->editor()->setTextCursor(scope->currentData()==QStringLiteral("slide")?slideRange:selectedRange);owner->doFindAgain(true);});
    moreMenu->addSeparator();moreMenu->addAction(ui(QStringLiteral("关闭")),closeButton,&QToolButton::click);
    replaceMenu=new QMenu(body);replaceMenu->setObjectName(QStringLiteral("researchReplaceMenu"));
    auto *replaceBody=new QWidget(replaceMenu);auto *replaceLayout=new QVBoxLayout(replaceBody);replaceLayout->setContentsMargins(12,12,12,12);replaceLayout->setSpacing(8);
    replacement=new QLineEdit(replaceBody);replacement->setObjectName(QStringLiteral("researchReplaceField"));replacement->setPlaceholderText(ui(QStringLiteral("替换为")));replacement->setAccessibleName(replacement->placeholderText());configureField(replacement);replacement->setMinimumWidth(220);replaceLayout->addWidget(replacement);
    auto *buttons=new QHBoxLayout;replaceLayout->addLayout(buttons);
    auto replaceButton=[&](const QString &text,const QString &name){auto *button=new QToolButton(replaceBody);button->setText(text);button->setObjectName(name);configureButton(button,ButtonRole::Text);buttons->addWidget(button);return button;};
    auto *one=replaceButton(ui(QStringLiteral("替换并查找下一个")),QStringLiteral("researchReplaceOne"));auto *all=replaceButton(ui(QStringLiteral("替换所有")),QStringLiteral("researchReplaceAll"));
    auto *popup=new QWidgetAction(replaceMenu);popup->setDefaultWidget(replaceBody);replaceMenu->addAction(popup);
    connect(replaceMenu,&QMenu::aboutToHide,this,[this]{QTimer::singleShot(0,this,[this]{if(bar->isVisible()&&QApplication::activeWindow()==owner&&!QApplication::activePopupWidget())query->setFocus();});});
    bar->addWidget(body);bar->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);bar->setMinimumWidth(0);
    auto *sourceLayout=qobject_cast<QVBoxLayout*>(w->centralWidget()->layout());Q_ASSERT(sourceLayout);sourceLayout->insertWidget(0,bar);sourceLayout->setSpacing(0);
    bar->installEventFilter(this);bar->hide();w->researchFindBar=bar;w->researchFindField=query;
    debounce.setSingleShot(true);debounce.setInterval(140);connect(&debounce,&QTimer::timeout,this,[this]{refresh();});
    auto changed=[this]{outdated=true;debounce.start();};connect(query,&QLineEdit::textChanged,this,changed);for(auto *box:{sensitive,words,regex})connect(box,&QCheckBox::toggled,this,changed);connect(scope,qOverload<int>(&QComboBox::currentIndexChanged),this,[this,changed]{arrange();changed();});
    connect(w->textDoc(),&QTextDocument::contentsChanged,this,[this]{outdated=true;if(bar->isVisible())debounce.start();});
    connect(closeButton,&QToolButton::clicked,this,[this]{replaceMenu->hide();moreMenu->hide();bar->hide();owner->editor()->setFocus();});
    connect(previous,&QToolButton::clicked,this,[this]{next(true);});connect(following,&QToolButton::clicked,this,[this]{next(false);});connect(query,&QLineEdit::returnPressed,this,[this]{next(false);});
    auto *back=new QShortcut(QKeySequence(Qt::SHIFT|Qt::Key_Return),query);connect(back,&QShortcut::activated,this,[this]{next(true);});
    auto *escape=new QShortcut(QKeySequence(Qt::Key_Escape),body);escape->setContext(Qt::WidgetWithChildrenShortcut);connect(escape,&QShortcut::activated,this,[this]{if(replaceMenu->isVisible())replaceMenu->hide();else closeButton->click();});
    connect(one,&QToolButton::clicked,this,[this]{replace(false);});connect(all,&QToolButton::clicked,this,[this]{replace(true);});
  }
  void show(bool replacing) {
    if(owner->property("researchSlidePreviewExpanded").toBool())
      if(auto *expand=owner->findChild<QAction*>(QStringLiteral("researchSlideExpandPreview")))expand->trigger();
    selectedRange=owner->textCursor();slideRange=QTextCursor(owner->textDoc());
    if(owner->editor()->property("researchSourceSlice").toBool()){int begin=-1,end=0;for(auto b=owner->textDoc()->begin();b.isValid();b=b.next())if(b.isVisible()){if(begin<0)begin=b.position();end=b.position()+b.length()-1;}slideRange.setPosition(qMax(0,begin));slideRange.setPosition(qMin(end,owner->textDoc()->characterCount()-1),QTextCursor::KeepAnchor);}
    const auto previous=preferSlice?QVariant():scope->currentData();preferSlice=false;sourceWasSliced=slideRange.hasSelection();{QSignalBlocker block(scope);scope->clear();if(slideRange.hasSelection())scope->addItem(scopeCaption(QStringLiteral("slide")),QStringLiteral("slide"));scope->addItem(scopeCaption(QStringLiteral("file")),QStringLiteral("file"));if(selectedRange.hasSelection())scope->addItem(scopeCaption(QStringLiteral("selection")),QStringLiteral("selection"));scope->addItem(scopeCaption(QStringLiteral("open")),QStringLiteral("open"));if(previous.isValid()&&scope->findData(previous)>=0)scope->setCurrentIndex(scope->findData(previous));}
    if(!selectedRange.selectedText().contains(QChar::ParagraphSeparator)&&selectedRange.hasSelection())query->setText(selectedRange.selectedText());
    query->setPlaceholderText(englishUi()?QStringLiteral("Find in source"):Research::ui(QStringLiteral("查找源码")));scope->setAccessibleName(englishUi()?QStringLiteral("Source search scope"):Research::ui(QStringLiteral("源码查找范围")));
    bar->show();arrange();query->setFocus();query->selectAll();outdated=true;refresh();if(replacing)showReplacement();
  }
  void next(bool backwards) {
    const int former=current;refresh();if(matches.isEmpty())return;
    int index=-1;if(former>=0&&former<matches.size()&&matches[former].window&&matches[former].window->textCursor().selectionStart()==matches[former].match.capturedStart()&&matches[former].window->textCursor().selectionEnd()==matches[former].match.capturedEnd())index=(former+(backwards?-1:1)+matches.size())%matches.size();
    if(index<0){const int at=owner->textCursor().position();for(int i=0;i<matches.size();++i)if(matches[i].window==owner&&(backwards?matches[i].match.capturedEnd()<at:matches[i].match.capturedStart()>=at)){index=i;if(!backwards)break;}if(index<0)index=backwards?int(matches.size())-1:0;}
    current=index;auto found=matches[index];if(!found.window)return;QTextCursor cursor(found.window->textDoc());cursor.setPosition(int(found.match.capturedStart()));cursor.setPosition(int(found.match.capturedEnd()),QTextCursor::KeepAnchor);found.window->editor()->setTextCursor(cursor);found.window->editor()->ensureCursorVisible();if(found.window!=owner)found.window->show();updateCount();
  }
};
static FindController *controller(TeXDocumentWindow *w){auto *c=dynamic_cast<FindController *>(w->findChild<QObject *>(QStringLiteral("researchFindController")));return c?c:new FindController(w);}
void showFind(TeXDocumentWindow *w,bool replace){controller(w)->show(replace);}
void findNext(TeXDocumentWindow *w,bool backwards){controller(w)->next(backwards);}
void findSourceScopeChanged(TeXDocumentWindow *w){if(auto *c=dynamic_cast<FindController *>(w->findChild<QObject *>(QStringLiteral("researchFindController"))))c->sourceScopeChanged();}
}
