#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "TeXDocumentWindow.h"
#include "CompletingEdit.h"
#include "PrefsDialog.h"
#include "TemplateDialog.h"
#include "ResearchAppearance.h"
#include "ResearchCommandCompletion.h"
#include "Settings.h"
#include <QAbstractItemView>
#include <QAction>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QScreen>
#include <QTabWidget>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <QTextLayout>
#include <QGlyphRun>
#include <windows.h>

static void settle(int ms=150) { QElapsedTimer t;t.start();while(t.elapsed()<ms){QCoreApplication::processEvents();QThread::msleep(2);} }
static void key(QWidget *w,int code,Qt::KeyboardModifiers mods=Qt::NoModifier,const QString &text={}) {
 QKeyEvent press(QEvent::KeyPress,code,mods,text),release(QEvent::KeyRelease,code,mods,text);QApplication::sendEvent(w,&press);QApplication::sendEvent(w,&release);
}
int main(int argc,char **argv) {
 const QString report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
 QTimer::singleShot(80,&app,[&]{
  const QString folder=QFileInfo(report).absolutePath();QJsonArray checks,surfaces,inventory;int failures=0;
  auto flush=[&]{QFile f(report);if(f.open(QIODevice::WriteOnly))f.write(QJsonDocument(QJsonObject{{"checks",checks},{"surfaces",surfaces},{"inventory",inventory},{"failures",failures}}).toJson());};
  auto check=[&](QString name,bool ok){checks.append(QJsonObject{{"name",name},{"passed",ok}});failures+=!ok;flush();};
  const QString root=folder+"/writing.tex";
  {QFile f(root);if(f.open(QIODevice::WriteOnly))f.write("\\documentclass{article}\n\\begin{document}\n\\section{A research note}\nA short bilingual paragraph.\n\\begin{equation}\\label{eq:mean}\n\\bar X_n = n^{-1}\\sum_{i=1}^n X_i\n\\end{equation}\n\\end{document}\n");}
  Tw::Settings().setValue("openPDFwithTeX",false);Research::setFocusMode(false);
  auto *w=new TeXDocumentWindow(root);w->resize(1100,760);w->show();settle(500);
  auto capture=[&](QWidget *surface,const QString &name){
   if(!surface){check(name+" exists",false);return;}
   auto image=surface->grab().toImage().convertToFormat(QImage::Format_ARGB32);image.save(folder+"/"+name+".png");
   const auto handle=reinterpret_cast<HWND>(surface->winId());const auto style=GetWindowLongPtrW(handle,GWL_STYLE);
   QJsonArray corners;bool clear=true;for(auto p:{QPoint(0,0),QPoint(image.width()-1,0),QPoint(0,image.height()-1),QPoint(image.width()-1,image.height()-1)}){corners.append(image.pixelColor(p).alpha());clear&=image.pixelColor(p).alpha()<16;}
   surfaces.append(QJsonObject{{"name",name},{"class",surface->metaObject()->className()},{"width",surface->width()},{"height",surface->height()},{"dpr",surface->devicePixelRatioF()},{"cornerAlpha",corners},{"layered",bool(GetWindowLongPtrW(handle,GWL_EXSTYLE)&WS_EX_LAYERED)},{"frame",bool(style&(WS_CAPTION|WS_THICKFRAME))}});
   check(name+" has four transparent corners",clear);
   check(name+" has an opaque painted interior",image.pixelColor(image.width()/2,image.height()/2).alpha()==255&&image.pixelColor(image.width()/2,image.height()-5).alpha()==255);
   check(name+" uses a frameless layered window",surface->windowFlags().testFlag(Qt::FramelessWindowHint)&&(GetWindowLongPtrW(handle,GWL_EXSTYLE)&WS_EX_LAYERED)&&!(style&(WS_CAPTION|WS_THICKFRAME)));
  };
  auto fonts=[&](QWidget *widget,const QString &name){QTextLayout text(QStringLiteral("设置 编译 中文 ABC"),widget->font());text.beginLayout();auto line=text.createLine();line.setLineWidth(600);text.endLayout();QJsonArray families;for(const auto &run:text.glyphRuns())families.append(run.rawFont().familyName());inventory.append(QJsonObject{{"name",name},{"font",widget->font().toString()},{"requested",widget->font().families().join(" / ")},{"resolved",families}});};
  fonts(w,"source-window-font");fonts(w->editor(),"source-editor-font");
  w->grab().save(folder+"/01-writing.png");
  for(auto *a:w->findChildren<QAction *>())if(!a->isSeparator()&&!a->text().isEmpty())inventory.append(QJsonObject{{"name",a->objectName()},{"text",a->text()},{"tip",a->toolTip()},{"enabled",a->isEnabled()}});
  int menuCount=0;QSet<QMenu *> visited;
  std::function<void(QMenu *,int)> menuVisit=[&](QMenu *menu,int depth){
   if(!menu||visited.contains(menu)||depth>2)return;visited.insert(menu);menu->popup(w->mapToGlobal(QPoint(180,100)));settle(70);
   if(menu->isVisible()&&!menu->actions().isEmpty()){capture(menu,QString("menu-%1-%2").arg(++menuCount).arg(menu->objectName()));menu->hide();}
   for(auto *a:menu->actions())if(a->isVisible()&&a->menu())menuVisit(a->menu(),depth+1);
  };
  if(auto *more=w->findChild<QToolButton *>("researchMore"))menuVisit(more->menu(),0);
  check("ordinary menus are reachable",menuCount>8);
  QToolTip::showText(w->mapToGlobal(QPoint(300,200)),Research::ui(QStringLiteral("Ctrl+Enter 插入补全，Tab 缩进。")),w);settle();
  QWidget *tip=nullptr;for(auto *top:QApplication::topLevelWidgets())if(top->isVisible()&&top->inherits("QTipLabel"))tip=top;capture(tip,"tooltip");QToolTip::hideText();settle(350);
  w->doReplaceDialog();settle();w->grab().save(folder+"/02-replace.png");if(auto *close=w->findChild<QToolButton *>("researchFindClose"))close->click();
  // A true user-facing completion list, not a stand-in QListView.
  Research::setCommandCompletionMode(1);w->raise();w->activateWindow();QApplication::setActiveWindow(w);w->editor()->setFocus();settle(100);check("private editor has Qt focus",w->editor()->hasFocus());
  w->editor()->moveCursor(QTextCursor::Start);w->editor()->insertPlainText("\\beg\n");auto cursor=w->editor()->textCursor();cursor.setPosition(4);w->editor()->setTextCursor(cursor);settle(50);key(w->editor(),Qt::Key_Slash,Qt::AltModifier);settle(400);
  auto *completion=w->findChild<QListView *>("researchCommandCompletion");check("manual command completion is visible",completion&&completion->isVisible());if(completion&&completion->isVisible())capture(completion,"command-completion");key(w->editor(),Qt::Key_Escape);
  w->editor()->moveCursor(QTextCursor::End);w->editor()->insertPlainText("\n\\ref{eq:");settle(50);key(w->editor(),Qt::Key_Slash,Qt::AltModifier);settle(700);
  auto *reference=w->findChild<QListView *>("researchReferenceCompletion");check("manual reference completion is visible",reference&&reference->isVisible());if(reference&&reference->isVisible())capture(reference,"reference-completion");key(w->editor(),Qt::Key_Escape);
  // Search and dismiss through the same modal path exposed in the app.
  QTimer::singleShot(150,&app,[&]{auto *dialog=w->findChild<QDialog *>("researchCommandDialog");if(!dialog){check("command dialog opens",false);return;}
   dialog->grab().save(folder+"/03-commands.png");auto *query=dialog->findChild<QLineEdit *>("researchCommandQuery");query->setText("no-such-command-7b06");settle();dialog->grab().save(folder+"/04-command-empty.png");
   auto *empty=dialog->findChild<QLabel *>("researchCommandEmpty");check("command search explains an empty result",empty&&empty->isVisible());
   auto *list=dialog->findChild<QListWidget *>("researchCommandResults");check("empty command search clears the stale selection",list&&!list->currentItem());
   query->clear();settle();check("clearing command search restores an enabled selection",list&&list->currentItem()&&(list->currentItem()->flags()&Qt::ItemIsEnabled));dialog->reject();
  });w->findChild<QAction *>("researchCommandPalette")->trigger();
  QTimer::singleShot(180,&app,[&]{auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());if(!dialog){check("settings opens",false);return;}
   auto *tabs=dialog->findChild<QTabWidget *>("tabWidget");if(tabs)for(int i=0;i<tabs->count();++i){tabs->setCurrentIndex(i);settle(80);dialog->grab().save(folder+QString("/settings-%1.png").arg(i));}
   auto *buttons=dialog->findChild<QDialogButtonBox *>("buttonBox");check("standard buttons use the selected interface language",buttons&&buttons->button(QDialogButtonBox::Ok)->text().contains(Research::englishUi()?"OK":QStringLiteral("确定"))&&buttons->button(QDialogButtonBox::Cancel)->text().contains(Research::englishUi()?"Cancel":QStringLiteral("取消")));
   fonts(dialog,"settings-font");for(auto *label:dialog->findChildren<QLabel *>())if(!label->text().isEmpty()){fonts(label,"settings-label-font");break;}
   auto *query=dialog->findChild<QLineEdit *>("researchSettingsSearch");auto *matches=dialog->findChild<QListWidget *>("researchSettingsMatches");
   query->setText(Research::englishUi()?"Cursor Width":QStringLiteral("光标宽度"));settle();
   if(matches->count()&&matches->item(0)->flags()!=Qt::NoItemFlags){auto *item=matches->item(0);QMetaObject::invokeMethod(matches,"itemActivated",Qt::DirectConnection,Q_ARG(QListWidgetItem *,item));settle();}
   dialog->grab().save(folder+"/settings-search-target.png");auto *target=dialog->findChild<QWidget *>(dialog->property("researchSettingsSearchTarget").toString());check("settings result is visible within its scroll viewport",target&&target->isVisible()&&([&]{for(auto *parent=target->parentWidget();parent&&parent!=dialog;parent=parent->parentWidget())if(auto *scroll=qobject_cast<QScrollArea *>(parent)){const QRect rect(target->mapTo(scroll->viewport(),QPoint()),target->size());return scroll->viewport()->rect().contains(rect);}return true;})());check("settings search focuses an editable control",target&&target->focusPolicy()!=Qt::NoFocus&&target->hasFocus());
   auto *completion=dialog->findChild<QComboBox *>("researchCompletionSetting");if(completion){for(int i=0;i<tabs->count();++i)if(tabs->widget(i)->isAncestorOf(completion))tabs->setCurrentIndex(i);if(auto *scroll=qobject_cast<QScrollArea *>(tabs->currentWidget()))scroll->ensureWidgetVisible(completion);completion->showPopup();settle();capture(completion->view()->window(),"settings-combo");completion->hidePopup();}
   dialog->reject();
  });PrefsDialog::doPrefsDialog(w);
  {TemplateDialog dialog;QTimer::singleShot(250,&app,[&]{dialog.grab().save(folder+"/06-template-picker.png");check("template picker fits the screen work area",dialog.screen()->availableGeometry().contains(dialog.frameGeometry()));auto *buttons=dialog.findChild<QDialogButtonBox *>("buttonBox");check("template confirmation stays inside its window",buttons&&dialog.rect().contains(QRect(buttons->mapTo(&dialog,QPoint()),buttons->size())));dialog.reject();});dialog.exec();}
  Research::setFocusMode(true);settle(400);w->grab().save(folder+"/05-focus.png");
  if(auto *more=w->findChild<QToolButton *>("researchFocusMore"))menuVisit(more->menu(),0);
  Research::setFocusMode(false);w->setModified(false);flush();app.exit(failures?1:0);
 });QTimer::singleShot(120000,&app,[&]{app.exit(92);});return app.exec();
}
