// Contextual starting points for a new, empty writing window. GPL-2.0-or-later.
#include "ResearchStart.h"
#include "ResearchAppearance.h"
#include "ResearchSettings.h"
#include "ResearchSlides.h"
#include "TeXDocumentWindow.h"
#include "CompletingEdit.h"
#include "TWApp.h"
#include <QAction>
#include <QApplication>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QPointer>
#include <QPushButton>
#include <QTextCursor>
#include <QTimer>

namespace Research {
namespace {
class StartButton final : public QPushButton {
public:
  StartButton(CompletingEdit *editor,QWidget *parent):QPushButton(parent),editor(editor) {
    setAutoDefault(false);
    setAttribute(Qt::WA_InputMethodEnabled);setInputMethodHints(editor->inputMethodHints());
  }
  QVariant inputMethodQuery(Qt::InputMethodQuery query) const override {
    const auto value=editor->inputMethodQuery(query);
    if(query==Qt::ImCursorRectangle||query==Qt::ImAnchorRectangle||query==Qt::ImInputItemClipRectangle)
      return value.toRectF().translated(mapFrom(editor,QPoint()));
    return value;
  }
private:
  CompletingEdit *editor;
};
class WritingStart final : public QObject {
public:
  WritingStart(TeXDocumentWindow *owner,QAction *open,QAction *fromTemplate)
    :QObject(owner),window(owner),editor(owner->editor()),viewport(editor->viewport()),templateAction(fromTemplate) {
    setObjectName(QStringLiteral("researchWritingStart"));
    // Individual viewport children leave the canvas between controls available
    // for native text selection, wheel, paste and file drops.
    newButton=makeButton("researchStartNew");openButton=makeButton("researchStartOpen");
    newButton->setDefault(true);
    recentLabel=new QLabel(viewport);recentLabel->hide();recentLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    recentLabel->setObjectName(QStringLiteral("researchStartRecentLabel"));
    recentLabel->setStyleSheet(QStringLiteral("color:#6e767c;background:transparent;"));
    for(int i=0;i<3;++i){auto *row=makeButton(qPrintable(QStringLiteral("researchStartRecent%1").arg(i)));row->setFlat(true);recentButtons.append(row);}
    connect(openButton,&QPushButton::clicked,this,[this,open]{invoke(open);});
    connect(newButton,&QPushButton::clicked,this,[this]{chooseNew();});
    for(int i=0;i<recentButtons.size();++i)connect(recentButtons[i],&QPushButton::clicked,this,[this,i]{invoke(recentActions.value(i));});
    connect(open,&QAction::changed,this,[this,open]{openButton->setEnabled(open->isEnabled());});
    openButton->setEnabled(open->isEnabled());
    if(!window->untitled()||!editor->document()->isEmpty()||editor->document()->isUndoAvailable()||editor->document()->isRedoAvailable()||window->isModified()){dismiss();return;}
    window->installEventFilter(this);editor->installEventFilter(this);viewport->installEventFilter(this);
    connect(editor->document(),&QTextDocument::contentsChanged,this,[this]{if(!editor->document()->isEmpty())dismiss();});
    connect(editor,&CompletingEdit::compositionChanged,this,[this](bool composing){if(composing)dismiss();});
    connect(TWApp::instance(),&TWApp::windowListChanged,this,[this]{refreshEligibility();});
    connect(TWApp::instance(),&TWApp::recentFileActionsChanged,this,[this]{refreshRecent();queueLayout();});
    connect(TWApp::instance(),&TWApp::updatedTranslators,this,[this]{updateLabels();refreshRecent();queueLayout();});
    updateLabels();refreshRecent();queueLayout();
  }
  void refreshSettings(){if(!dismissed){refreshRecent();queueLayout();}}
private:
  TeXDocumentWindow *window;
  CompletingEdit *editor;
  QWidget *viewport;
  StartButton *newButton{nullptr},*openButton{nullptr};
  QLabel *recentLabel{nullptr};
  QList<StartButton*> recentButtons;
  QList<QPointer<QAction>> recentActions;
  QPointer<QAction> templateAction;
  QPointer<QMenu> activeMenu;
  bool dismissed{false},layoutQueued{false};

  StartButton *makeButton(const char *name) {
    auto *button=new StartButton(editor,viewport);button->setObjectName(QString::fromLatin1(name));
    button->hide();button->setFocusPolicy(Qt::TabFocus);button->setAcceptDrops(false);button->installEventFilter(this);return button;
  }
  QList<StartButton*> buttons()const {QList<StartButton*> all{newButton,openButton};all.append(recentButtons);return all;}
  void updateLabels() {
    if(dismissed)return;
    newButton->setText(words("新建文件…","New file…"));openButton->setText(words("打开文件…","Open file…"));
    recentLabel->setText(words("最近使用","Recent files"));recentLabel->setFont(interfaceFont());
    for(auto *button:{newButton,openButton}){button->setAccessibleName(button->text());configureButton(button);}
  }
  void refreshRecent() {
    if(dismissed)return;
    recentActions.clear();
    // Share the existing history actions, including unique labels and the normal
    // open/recovery/error path. Do not stat cloud paths just to draw this page.
    auto *menu=window->findChild<QMenu*>(QStringLiteral("menuOpen_Recent"));
    if(settingValue("researchStartRecentFiles").toBool()&&menu)for(auto *action:menu->actions()){
      if(action->isSeparator()||!action->isVisible()||action->data().toString().isEmpty())continue;
      recentActions.append(action);if(recentActions.size()==3)break;
    }
    for(int i=0;i<recentButtons.size();++i){
      auto *row=recentButtons[i];auto *action=recentActions.value(i).data();
      const QString label=action?action->text().replace(QStringLiteral("&&"),QStringLiteral("&")):QString();
      row->setProperty("startFullLabel",label);row->setAccessibleName(label);
      row->setToolTip(action?action->toolTip():QString());row->setEnabled(action&&action->isEnabled());
      if(!action){if(row->hasFocus())editor->setFocus(Qt::OtherFocusReason);row->hide();}
    }
  }
  void refreshEligibility() {
    if(dismissed)return;
    if(!window->untitled()||!editor->document()->isEmpty()){dismiss();return;}
    queueLayout();
  }
  void queueLayout() {
    if(dismissed||layoutQueued)return;
    layoutQueued=true;QTimer::singleShot(0,this,[this]{layoutQueued=false;place();});
  }
  void place() {
    if(dismissed)return;
    if(!window->untitled()||!editor->document()->isEmpty()){dismiss();return;}
    const int margin=ControlMetrics::Margin,gap=ControlMetrics::Gap,h=ControlMetrics::Height;
    const int available=viewport->width()-2*margin;
    for(auto *button:{newButton,openButton})configureButton(button);
    const int actionWidth=qMax(136,qMax(newButton->width(),openButton->width()));
    const bool horizontal=2*actionWidth+gap<=available;
    const int width=qMin(available,qMax(280,horizontal?2*actionWidth+gap:actionWidth));
    const int actionsHeight=horizontal?h:2*h+gap;
    QTextCursor first(editor->document());first.movePosition(QTextCursor::Start);
    const int top=qMax(editor->cursorRect(first).bottom()+2*margin,(viewport->height()-actionsHeight)/4);
    if(width<actionWidth||top+actionsHeight+margin>viewport->height()||!viewport->isVisible()){
      bool focused=false;for(auto *button:buttons())focused|=button->hasFocus();
      for(auto *button:buttons())button->hide();
      if(focused)focusEditor();
      recentLabel->hide();return;
    }
    const int left=(viewport->width()-width)/2,buttonWidth=horizontal?(width-gap)/2:width;
    for(auto *button:{newButton,openButton})configureButton(button,buttonWidth);
    newButton->move(left,top);openButton->move(horizontal?left+buttonWidth+gap:left,horizontal?top:top+h+gap);
    newButton->show();openButton->show();newButton->raise();openButton->raise();
    int y=top+actionsHeight+20;
    const int labelHeight=qMax(24,recentLabel->fontMetrics().height()+4);
    const int rows=qMin(int(recentActions.size()),qMax(0,(viewport->height()-margin-y-labelHeight-4)/h));
    recentLabel->setVisible(rows>0);recentLabel->setGeometry(left+12,y,width-24,labelHeight);y+=labelHeight+4;
    for(int i=0;i<recentButtons.size();++i){
      auto *row=recentButtons[i];const bool focused=row->hasFocus();row->setVisible(i<rows);
      if(i>=rows){if(focused)focusEditor();continue;}
      const auto full=row->property("startFullLabel").toString();
      row->setFont(interfaceFont());row->setText(row->fontMetrics().elidedText(full,Qt::ElideMiddle,width-36).replace(QStringLiteral("&"),QStringLiteral("&&")));
      configureButton(row,width);row->setStyleSheet(row->styleSheet()+QStringLiteral("QPushButton{text-align:left;}"));
      row->move(left,y+i*h);row->raise();
    }
  }
  void focusEditor() {
    if(window->isActiveWindow()&&editor->isVisible()&&!QApplication::activeModalWidget()&&!QApplication::activePopupWidget())editor->setFocus(Qt::OtherFocusReason);
  }
  void dismiss() {
    if(dismissed)return;
    bool buttonFocused=false;for(auto *button:buttons()){buttonFocused|=button->hasFocus();button->hide();button->removeEventFilter(this);}
    dismissed=true;recentLabel->hide();if(activeMenu)activeMenu->close();
    window->removeEventFilter(this);editor->removeEventFilter(this);viewport->removeEventFilter(this);
    disconnect(editor->document(),nullptr,this,nullptr);disconnect(editor,nullptr,this,nullptr);disconnect(TWApp::instance(),nullptr,this,nullptr);
    if(buttonFocused&&window->isVisible())editor->setFocus(Qt::OtherFocusReason);
  }
  void invoke(QAction *command) {
    if(!command||!command->isEnabled())return;
    QPointer<WritingStart> guard(this);command->trigger();if(!guard)return;
    refreshEligibility();focusEditor();QTimer::singleShot(0,this,[this]{focusEditor();});
  }
  void chooseNew() {
    // Create on this pristine page, so cancelling never leaves another tab.
    auto *menu=new QMenu(window);menu->setFont(interfaceFont());preparePopupSurface(menu);menu->setObjectName(QStringLiteral("researchStartNewMenu"));
    menu->addAction(words("普通文档","Document"));
    menu->addAction(words("讲稿","Slides"));
    menu->addAction(words("空白源码","Blank source"));
    menu->addSeparator();auto *templates=menu->addAction(words("更多模板…","More templates…"));templates->setEnabled(templateAction&&templateAction->isEnabled());
    QPointer<WritingStart> guard(this);QPointer<QMenu> menuGuard(menu);activeMenu=menu;
    auto *chosen=menu->exec(newButton->mapToGlobal(QPoint(0,newButton->height())));
    const int choice=menuGuard&&chosen?int(menuGuard->actions().indexOf(chosen)):-1;
    // Destroy the owned popup before opening another modal. Otherwise a
    // parentless popup can remain the activation target after Template closes.
    if(menuGuard)delete menuGuard.data();
    if(!guard)return;
    activeMenu=nullptr;
    if(choice<0){focusEditor();QTimer::singleShot(0,this,[this]{focusEditor();});return;}
    if(choice==4){invoke(templateAction);return;}
    if(dismissed||!window->untitled()||!editor->document()->isEmpty())return;
    if(choice==2){dismiss();focusEditor();return;}
    const bool slide=choice==1,chinese=!englishUi();
    const QString documentClass=slide?(chinese?QStringLiteral("ctexbeamer"):QStringLiteral("beamer")):(chinese?QStringLiteral("ctexart"):QStringLiteral("article"));
    QString prefix=QStringLiteral("% !TEX TS-program = %1\n\\documentclass[%2]{%3}\n").arg(chinese?QStringLiteral("xelatex"):QStringLiteral("pdflatex"),slide?QStringLiteral("aspectratio=169"):chinese?QStringLiteral("UTF8,11pt"):QStringLiteral("11pt"),documentClass);
    if(slide)prefix+=QStringLiteral("\\usetheme{default}\n\\setbeamertemplate{navigation symbols}{}\n");
    else prefix+=QStringLiteral("\\usepackage[a4paper,margin=28mm]{geometry}\n\\usepackage{amsmath,amssymb,graphicx}\n");
    prefix+=QStringLiteral("\\begin{document}\n");
    if(slide)prefix+=QStringLiteral("\\begin{frame}{}\n");
    const QString suffix=slide?QStringLiteral("\n\\end{frame}\n\\end{document}\n"):QStringLiteral("\n\\end{document}\n");
    // Preserve one native undo step, then reuse the bounded slide detector.
    dismiss();QTextCursor cursor(editor->document());cursor.beginEditBlock();cursor.insertText(prefix+suffix);cursor.endEditBlock();
    cursor.setPosition(int(prefix.size()));editor->setTextCursor(cursor);editor->ensureCursorVisible();focusEditor();
    if(slide)detectSlideMode(window);
  }
  bool eventFilter(QObject *object,QEvent *event) override {
    if(dismissed)return false;
    const auto all=buttons();auto *control=qobject_cast<QPushButton*>(object);
    bool button=false;for(auto *item:all)button|=object==item;
    if((object==viewport||object==editor||object==window)&&(event->type()==QEvent::Resize||event->type()==QEvent::Show||event->type()==QEvent::FontChange||event->type()==QEvent::StyleChange))queueLayout();
    if(event->type()==QEvent::KeyPress) {
      auto *key=static_cast<QKeyEvent*>(event);
      if(key->key()==Qt::Key_Escape){dismiss();return button;}
      if(button&&(key->key()==Qt::Key_Tab||key->key()==Qt::Key_Backtab)&&!(key->modifiers()&(Qt::ControlModifier|Qt::AltModifier|Qt::MetaModifier))){
        QList<QWidget*> stops;for(auto *item:all)if(item->isVisible()&&item->isEnabled())stops.append(item);stops.append(editor);
        const bool back=key->key()==Qt::Key_Backtab||key->modifiers().testFlag(Qt::ShiftModifier);
        const auto index=stops.indexOf(control);stops[(index+(back?-1:1)+stops.size())%stops.size()]->setFocus(back?Qt::BacktabFocusReason:Qt::TabFocusReason);return true;
      }
      if(button&&(key->key()==Qt::Key_Return||key->key()==Qt::Key_Enter)&&!(key->modifiers()&(Qt::ControlModifier|Qt::AltModifier|Qt::MetaModifier))){control->click();return true;}
      const bool typing=!key->text().isEmpty()&&!(key->modifiers()&(Qt::ControlModifier|Qt::AltModifier|Qt::MetaModifier))&&key->key()!=Qt::Key_Space&&key->key()!=Qt::Key_Tab&&key->key()!=Qt::Key_Backtab;
      if(button&&(typing||key->key()==Qt::Key_Backspace||key->key()==Qt::Key_Delete||key->matches(QKeySequence::Paste))){dismiss();editor->setFocus(Qt::OtherFocusReason);QApplication::sendEvent(editor,event);return true;}
    }
    if(button&&event->type()==QEvent::InputMethod){
      auto *input=static_cast<QInputMethodEvent*>(event);
      if(input->preeditString().isEmpty()&&input->commitString().isEmpty()&&input->replacementLength()==0){input->accept();return true;}
      dismiss();editor->setFocus(Qt::OtherFocusReason);QApplication::sendEvent(editor,event);return true;
    }
    return false;
  }
};
}
void installWritingStart(TeXDocumentWindow *window) {
  if(!window||window->findChild<QObject*>(QStringLiteral("researchWritingStart"),Qt::FindDirectChildrenOnly))return;
  auto *open=window->findChild<QAction*>(QStringLiteral("actionOpen"));
  auto *fromTemplate=window->findChild<QAction*>(QStringLiteral("actionNew_from_Template"));
  if(open&&fromTemplate)new WritingStart(window,open,fromTemplate);
}
void refreshWritingStart(TeXDocumentWindow *window) {
  if(!window)return;
  if(auto *start=window->findChild<QObject*>(QStringLiteral("researchWritingStart"),Qt::FindDirectChildrenOnly))static_cast<WritingStart*>(start)->refreshSettings();
}
}
