#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "TeXDocumentWindow.h"
#include "CompletingEdit.h"
#include "PrefsDialog.h"
#include "ResearchAppearance.h"
#include "ResearchMotion.h"
#include "Settings.h"
#include <QtWidgets>
#include <QThread>
#include <windows.h>
static void settle(int ms=200){QElapsedTimer t;t.start();while(t.elapsed()<ms){QCoreApplication::processEvents();QThread::msleep(1);}}
static void key(QWidget *w,int key,const QString &text={},Qt::KeyboardModifiers modifiers=Qt::NoModifier){QKeyEvent down(QEvent::KeyPress,key,modifiers,text),up(QEvent::KeyRelease,key,modifiers,text);QApplication::sendEvent(w,&down);QApplication::sendEvent(w,&up);}
int main(int argc,char **argv){
 if(argc<2||!qEnvironmentVariable("TEXLINE_TEST_DESKTOP").startsWith("TeXlineValidation-"))return 2;
 const QString report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
 QTimer::singleShot(100,&app,[&]{
  const auto folder=QFileInfo(report).absolutePath();QJsonArray checks,timings,frames,diagnostics;int failures=0;
  const bool baseline=qEnvironmentVariableIsSet("TEXLINE_DESIGN_BASELINE");
  auto check=[&](QString name,bool pass){checks.append(QJsonObject{{"name",name},{"passed",pass}});failures+=!pass;};
  auto capture=[&](QWidget *w,const QString &name){const auto im=w->grab();im.save(folder+"/"+name+".png");frames.append(QJsonObject{{"name",name},{"dpr",im.devicePixelRatio()},{"width",w->width()},{"height",w->height()}});};
  auto record=[&](QWidget *w,const QString &name,const std::function<void()> &action){
   if(!qEnvironmentVariableIsSet("TEXLINE_DESIGN_RECORD")){action();settle();return;}
   QVector<QImage> images;QJsonArray times;QElapsedTimer clock;clock.start();images.append(w->grab().toImage());times.append(0);action();
   for(int n=0;n<11;++n){settle(18);times.append(double(clock.elapsed()));images.append(w->grab().toImage());}
   QDir().mkpath(folder+"/"+name);QJsonArray entries;for(int n=0;n<images.size();++n){auto file=QString("frame-%1.png").arg(n,3,10,QChar('0'));images[n].save(folder+"/"+name+"/"+file);entries.append(QJsonObject{{"file",file},{"atMs",times[n]}});}
   QFile f(folder+"/"+name+"/frames.json");f.open(QIODevice::WriteOnly);f.write(QJsonDocument(QJsonObject{{"frames",entries},{"dpr",w->devicePixelRatioF()},{"capture","Actual widget rasters; encoding after capture; synthetic input"}}).toJson());
  };
  Tw::Settings().setValue("researchReduceMotion",false);Tw::Settings().setValue("openPDFwithTeX",false);
  const auto file=folder+"/comparison.tex";QFile f(file);f.open(QIODevice::WriteOnly);f.write("\\documentclass{article}\n\\usepackage{amsmath}\n\\title{Research notes}\n\\begin{document}\n\\section{A working draft}\nResearch notes on estimation. A careful argument starts with a clear definition.\n\\[ \\widehat\\theta = \\frac{1}{n}\\sum_{i=1}^{n} X_i. \\]\n\\section{Checks}\nThe selected paragraph remains editable when a tool is dismissed.\n\\end{document}\n");f.close();
  auto *w=new TeXDocumentWindow(file);w->resize(1000,760);w->show();settle(500);auto *editor=w->editor();const auto original=editor->toPlainText();
  capture(w,"source-wide");w->resize(714,760);settle();capture(w,"source-minimum-comparison");w->resize(520,760);settle();capture(w,"source-half");w->resize(1000,760);settle();
  auto cursor=editor->textCursor();const int start=int(original.indexOf("Research notes on"));cursor.setPosition(start);cursor.setPosition(start+14,QTextCursor::KeepAnchor);editor->setTextCursor(cursor);QApplication::setActiveWindow(w);editor->setFocus();const auto selected=cursor.selectedText();const int scroll=editor->verticalScrollBar()->value();
  for(int width:{880,520}){
   const auto oldFont=Tw::Settings().value("font");
   QTimer::singleShot(80,&app,[&,width]{
    auto *d=qobject_cast<QDialog*>(QApplication::activeModalWidget());check("settings opens",d);if(!d)return;d->resize(width,650);settle();
    auto *navigation=d->findChild<QListWidget*>("researchSettingsNavigation");if(navigation->isVisible())check("all category names fit without horizontal scrolling",navigation->horizontalScrollBar()->maximum()==0);
    auto *tabs=d->findChild<QTabWidget*>("tabWidget");auto area=[&](const char *id){return d->findChild<QScrollArea*>(id);};auto select=[&](QScrollArea *page){tabs->setCurrentWidget(page);};auto veil=[&](QScrollArea *page){return page->viewport()->findChild<QWidget*>("researchArrivalVeil",Qt::FindDirectChildrenOnly);};
    auto *general=area("settingsGeneral"),*writing=area("settingsEditor"),*shortcuts=area("settingsShortcuts"),*build=area("settingsBuild");auto *snippetArea=baseline?area("settingsCompletion"):writing;
    auto *snippetDetails=d->findChild<QToolButton*>("researchSnippetDetailsToggle"),*buildDetails=d->findChild<QToolButton*>("researchBuildDetailsToggle");auto *inputTabs=d->findChild<QTabBar*>("researchInputTabs");
    check("settings destinations exist by identity",tabs&&general&&writing&&shortcuts&&build&&snippetArea);if(!tabs||!general||!writing||!shortcuts||!build||!snippetArea){d->reject();return;}
    if(!baseline){check("on-demand settings controls exist",snippetDetails&&buildDetails&&inputTabs);if(!snippetDetails||!buildDetails||!inputTabs){d->reject();return;}}
    auto showSnippets=[&]{select(snippetArea);if(snippetDetails&&!snippetDetails->isChecked())snippetDetails->click();};
    select(build);if(buildDetails&&!buildDetails->isChecked())buildDetails->click();auto *paths=d->findChild<QListWidget*>("binPathList");auto *longPath=new QListWidgetItem("C:/"+QString(180,QChar('W'))+"/bin",paths);settle();build->ensureWidgetVisible(paths);settle();auto *horizontal=paths->horizontalScrollBar();horizontal->setValue(horizontal->maximum());check("long program paths can be scrolled to their end",paths->isVisible()&&horizontal->maximum()>0&&horizontal->value()==horizontal->maximum());capture(d,QString("long-program-path-%1").arg(width));delete longPath;
    select(general);settle();
    record(d,QString("categories-%1").arg(width),[&]{select(writing);});
    select(shortcuts);if(inputTabs)inputTabs->setCurrentIndex(1);settle();capture(d,QString("shortcuts-%1").arg(width));
    if(!baseline){auto *table=d->findChild<QTableWidget*>("researchShortcutTable");shortcuts->ensureWidgetVisible(table);settle();check("shortcut list is usefully sized and reachable without horizontal page scrolling",table&&table->isVisible()&&table->height()>=160&&shortcuts->viewport()->rect().contains(QRect(table->mapTo(shortcuts->viewport(),QPoint()),table->size()))&&shortcuts->horizontalScrollBar()->maximum()==0);}
    select(writing);if(auto *inputDetails=d->findChild<QToolButton*>("researchInputDetailsToggle");inputDetails&&!inputDetails->isChecked())inputDetails->click();writing->verticalScrollBar()->setValue(0);
    select(general);settle();select(writing);
    if(!baseline)check("category transition starts without delaying the active page",tabs->currentWidget()==writing&&veil(writing)&&veil(writing)->isVisible());
    auto *size=d->findChild<QSpinBox*>("fontSize");auto *field=size->findChild<QLineEdit*>();field->setFocus();field->selectAll();key(field,Qt::Key_1,"1");key(field,Qt::Key_8,"8");
    check("typing during a switch reaches the live numeric field",field->text().contains("18"));
    if(!baseline)check("typing releases the category snapshot immediately",!veil(writing)->isVisible());
    if(!baseline){
     select(general);settle();select(writing);auto *lineNumbers=d->findChild<QCheckBox*>("lineNumbers");lineNumbers->setChecked(!lineNumbers->isChecked());check("background checkbox updates release the old category raster",!veil(writing)->isVisible());
     select(general);settle();select(writing);auto *indent=d->findChild<QComboBox*>("autoIndent");indent->setCurrentIndex((indent->currentIndex()+1)%indent->count());check("background choice updates release the old category raster",!veil(writing)->isVisible());
     showSnippets();settle();auto *list=d->findChild<QListWidget*>("completionSnippetList");check("expanded manager has preset rows",list&&list->count()>0);if(!list||!list->count()){d->reject();return;}snippetArea->ensureWidgetVisible(list);settle();select(general);settle();select(snippetArea);const auto text=list->item(0)->text();list->item(0)->setText(text+" ");check("background list updates release the old category raster",!veil(snippetArea)->isVisible());list->item(0)->setText(text);
    }
    select(general);select(writing);select(build);select(shortcuts);select(writing);
    check("rapid reversals do not queue category changes",tabs->currentWidget()==writing);settle(220);
    if(!baseline){int active=0;for(int n=0;n<tabs->count();++n)if(auto *v=veil(qobject_cast<QScrollArea*>(tabs->widget(n)));v&&v->isVisible())++active;check("all temporary category images are released",active==0);}
    if(snippetDetails&&snippetDetails->isChecked()){snippetDetails->click();settle(100);}
    auto *bar=writing->verticalScrollBar();bar->setValue(bar->maximum());const int saved=bar->value();select(build);select(writing);check("returning to a category preserves its reading position",bar->value()==saved);
    if(!baseline){bar->setValue(0);check("scrolling releases the stale category raster",!veil(writing)->isVisible());}
    showSnippets();settle();auto *snippetList=d->findChild<QListWidget*>("completionSnippetList");check("snippet list exists after opening management",snippetList);if(!snippetList){d->reject();return;}snippetArea->ensureWidgetVisible(snippetList);settle();capture(d,QString("snippets-%1").arg(width));
    QJsonArray widgets;for(auto *v:snippetArea->findChildren<QWidget*>())if(v->isVisible())widgets.append(QJsonObject{{"name",v->objectName()},{"class",v->metaObject()->className()},{"height",v->height()},{"minimum",v->minimumHeight()},{"hint",v->sizeHint().height()},{"minHint",v->minimumSizeHint().height()}});diagnostics.append(QJsonObject{{"width",width},{"scrollMax",snippetArea->verticalScrollBar()->maximum()},{"widgets",widgets}});
    if(!baseline)check("expanded snippet list remains useful and fully reachable",snippetList&&snippetList->height()>=128&&snippetArea->viewport()->rect().contains(QRect(snippetList->mapTo(snippetArea->viewport(),QPoint()),snippetList->size()))&&snippetArea->horizontalScrollBar()->maximum()==0);
    if(!baseline){
      auto *basics=d->findChild<QWidget*>("researchWritingBasics");auto *mode=d->findChild<QComboBox*>("completionConfigMode");auto *inputDetails=d->findChild<QToolButton*>("researchInputDetailsToggle");
      check("snippet library uses one scrolling list while keeping completion mode visible",basics&&!basics->isVisible()&&mode&&mode->isVisible()&&snippetArea->verticalScrollBar()->maximum()==0);
      check("writing input disclosure remains available for return-state validation",inputDetails);if(!basics||!mode||!inputDetails){d->reject();return;}
      snippetDetails->click();settle(100);const auto fullSize=d->size();d->resize(width,520);if(!inputDetails->isChecked())inputDetails->click();settle(140);auto *writingScroll=writing->verticalScrollBar();writingScroll->setValue(writingScroll->maximum());const int remembered=writingScroll->value();
      snippetDetails->click();settle(140);snippetDetails->click();settle(140);
      check("returning from snippet management restores writing scroll and input disclosure",remembered>0&&basics->isVisible()&&inputDetails->isChecked()&&writingScroll->value()==remembered);
      snippetDetails->click();settle(140);auto *search=d->findChild<QLineEdit*>("researchSettingsSearch");search->setFocus();search->setText(Research::englishUi()?"Font":Research::traditionalUi()?"字體":"字体");key(search,Qt::Key_Return);settle(220);auto *font=d->findChild<QComboBox*>("editorFont");auto *focused=QApplication::focusWidget();
      check("font search returns to basic writing settings without a delayed scroll override",!snippetDetails->isChecked()&&basics->isVisible()&&inputDetails->isChecked()&&font&&font->isVisible()&&(font->hasFocus()||(focused&&font->isAncestorOf(focused)))&&writing->viewport()->rect().contains(QRect(font->mapTo(writing->viewport(),QPoint()),font->size())));search->clear();d->resize(fullSize);showSnippets();settle(220);
    }
    auto *toggle=d->findChild<QPushButton*>("shorthandTrialToggle");check("trial has an explicit entry inside snippet management",toggle);
    if(toggle){
      check("trial is discoverable without a second editor on arrival",toggle->isVisible()&&!toggle->isChecked());toggle->click();auto *trial=d->findChild<QTextEdit*>("shorthandTrial");check("trial opens a real editor",trial);if(trial){trial->setPlainText("$x+y$");toggle->click();toggle->click();check("collapsing trial retains its draft",trial->toPlainText()=="$x+y$");toggle->click();if(snippetDetails){snippetDetails->click();check("collapsed snippet manager hides its contents",d->findChild<QWidget*>("researchSnippetDetails")&&!d->findChild<QWidget*>("researchSnippetDetails")->isVisible());snippetDetails->click();toggle->click();check("reopening the manager reuses its trial and draft",d->findChild<QTextEdit*>("shorthandTrial")==trial&&trial->toPlainText()=="$x+y$");toggle->click();}}
    }
    if(snippetDetails&&snippetDetails->isChecked())snippetDetails->click();if(auto *inputDetails=d->findChild<QToolButton*>("researchInputDetailsToggle");inputDetails&&inputDetails->isChecked())inputDetails->click();writing->verticalScrollBar()->setValue(0);
    Tw::Settings().setValue("researchReduceMotion",true);select(general);select(writing);if(!baseline)check("reduced motion applies the page immediately",!veil(writing)||!veil(writing)->isVisible());Tw::Settings().setValue("researchReduceMotion",false);settle();
    QJsonArray samples;for(int n=0;n<20;++n){QElapsedTimer call;call.start();select(n%2?general:writing);QCoreApplication::processEvents();samples.append(double(call.nsecsElapsed())/1e6);settle(180);}timings.append(QJsonObject{{"mode",QString("settings-%1").arg(width)},{"callAndEventsMs",samples}});
    key(d,Qt::Key_Escape);
   });
   PrefsDialog::doPrefsDialog(w);settle(100);
   check("Escape discards the settings draft",Tw::Settings().value("font")==oldFont);
   check("Escape restores the source selection and reading position",editor->textCursor().selectedText()==selected&&editor->verticalScrollBar()->value()==scroll);
   check("settings dismissal restores the editor focus",QApplication::focusWidget()==editor);
  }
  // A real draft dialog: incomplete input stays open, Escape cancels, source and undo survive.
  QVector<QImage> popupImages;QJsonArray popupFrames;QElapsedTimer popupRecording;QTimer popupTick;
  if(qEnvironmentVariableIsSet("TEXLINE_DESIGN_RECORD")){
   popupRecording.start();popupTick.setTimerType(Qt::PreciseTimer);popupTick.setInterval(16);
   QObject::connect(&popupTick,&QTimer::timeout,&app,[&]{for(auto *top:QApplication::topLevelWidgets())if(qobject_cast<QDialog*>(top)&&top->isVisible()&&top->findChild<QLineEdit*>("completionSnippetPrefix")){popupFrames.append(QJsonObject{{"file",QString("%1.png").arg(popupImages.size(),3,10,QChar('0'))},{"atMs",double(popupRecording.elapsed())}});popupImages.append(top->grab().toImage());break;}});popupTick.start();
  }
  QElapsedTimer popupClock;popupClock.start();w->findChild<QAction*>("researchSaveSnippet")->trigger();QCoreApplication::processEvents();const double popupMs=double(popupClock.nsecsElapsed())/1e6;auto *snippet=w->findChild<QDialog*>(QString(),Qt::FindDirectChildrenOnly);
  for(auto *candidate:w->findChildren<QDialog*>())if(candidate->isVisible()&&candidate->findChild<QLineEdit*>("completionSnippetPrefix"))snippet=candidate;
  if(popupTick.isActive()){settle(250);popupTick.stop();const auto path=folder+"/popup-open";QDir().mkpath(path);for(int i=0;i<popupImages.size();++i)popupImages[i].save(path+"/"+popupFrames[i].toObject()["file"].toString());QFile framesFile(path+"/frames.json");if(framesFile.open(QIODevice::WriteOnly))framesFile.write(QJsonDocument(QJsonObject{{"frames",popupFrames},{"capture","Actual snippet dialog opening; scripted action; encoding after capture"}}).toJson());popupImages.clear();}
  check("selection can open a snippet draft",snippet&&snippet->isVisible());
  if(snippet){timings.append(QJsonObject{{"mode","snippet-arrival"},{"callAndEventsMs",popupMs},{"captureMs",snippet->property("arrival-capture").toDouble()}});record(snippet,"snippet-arrival",[]{});auto *prefix=snippet->findChild<QLineEdit*>("completionSnippetPrefix");prefix->setText(" ");auto *buttons=snippet->findChild<QDialogButtonBox*>();buttons->button(QDialogButtonBox::Save)->click();check("invalid snippet stays open with a visible error",snippet->isVisible()&&snippet->findChild<QLabel*>("shorthandValidation")->isVisible()&&!snippet->findChild<QLabel*>("shorthandValidation")->text().isEmpty());capture(snippet,"snippet-error");key(snippet,Qt::Key_Escape);settle();}
  check("cancelling the snippet preserves the selected source",editor->toPlainText()==original&&editor->textCursor().selectedText()==selected);
  editor->setFocus();key(editor,Qt::Key_Z,"Z");editor->undo();check("tools add no source undo steps",editor->toPlainText()==original);
  QString longSource="\\documentclass{article}\n\\begin{document}\n";
  for(int section=0;section<160;++section){longSource+="\\section{A long working draft}\n";for(int line=0;line<65;++line)longSource+="The concentration evolves under a conserved flux. We compare the boundary conditions with the stationary solution and retain the spatial discretization in the error estimate.\n";}
  longSource+="\\end{document}\n";editor->setPlainText(longSource);settle(900);
  for(int width:{1000,520}){
   w->resize(width,760);settle(300);auto c=editor->textCursor();c.setPosition(longSource.size()/2);c.setPosition(c.position()+12,QTextCursor::KeepAnchor);editor->setTextCursor(c);editor->ensureCursorVisible();const int selectionStart=c.selectionStart(),selectionEnd=c.selectionEnd();settle();
   auto select=[&]{auto selection=editor->textCursor();selection.setPosition(selectionStart);selection.setPosition(selectionEnd,QTextCursor::KeepAnchor);editor->setTextCursor(selection);};
   auto commit=[&]{QInputMethodEvent event;event.setCommitString(QString::fromUtf8("中文输入"));QApplication::sendEvent(editor,&event);};
   // Compare native undo selection semantics with the same edit without motion.
   commit();editor->undo();const auto control=editor->textCursor();const int controlStart=control.selectionStart(),controlEnd=control.selectionEnd();select();
   auto *outline=w->findChild<QAction*>("researchOutlineAction");check("long manuscript has an article sidebar",outline!=nullptr);
   if(outline){outline->trigger();auto *veil=w->findChild<QWidget*>("researchPanelMotionVeil");commit();check("long manuscript accepts composition commit during sidebar motion",editor->toPlainText().mid(selectionStart,4)==QString::fromUtf8("中文输入")&&(!veil||!veil->isVisible()));editor->undo();check("one undo restores long manuscript with native selection semantics",editor->toPlainText()==longSource&&editor->textCursor().selectionStart()==controlStart&&editor->textCursor().selectionEnd()==controlEnd);}
   select();key(editor,Qt::Key_Z,"Z");editor->undo();const int focusControlStart=editor->textCursor().selectionStart(),focusControlEnd=editor->textCursor().selectionEnd();select();
   settle(550);Research::setFocusMode(true);auto *chrome=w->findChild<QWidget*>("researchChromeVeil");key(editor,Qt::Key_Z,"Z");check("typing during long manuscript focus transition is live",editor->toPlainText().at(selectionStart)==QChar('Z')&&(!chrome||!chrome->isVisible()));editor->undo();Research::setFocusMode(false);Research::setFocusMode(true);Research::setFocusMode(false);settle();check("rapid focus reversal retains long source and native undo selection",editor->toPlainText()==longSource&&editor->textCursor().selectionStart()==focusControlStart&&editor->textCursor().selectionEnd()==focusControlEnd&&!w->property("researchFocus").toBool());

  }
  QFile out(report);out.open(QIODevice::WriteOnly);out.write(QJsonDocument(QJsonObject{{"checks",checks},{"diagnostics",diagnostics},{"failures",failures},{"frames",frames},{"timings",timings},{"baseline",baseline},{"devicePixelRatio",w->devicePixelRatioF()},{"physicalInputVerified",false}}).toJson());w->setModified(false);delete w;app.exit(failures?1:0);
 });QTimer::singleShot(120000,&app,[&]{app.exit(92);});return app.exec();
}
