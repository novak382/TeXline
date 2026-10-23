// Native empty-document start controls. IME events below are synthetic; physical
// input, native file-dialog integration and system IME still need screen review.
#include "ActualAuthoringRecording.h"
#include "ResearchStart.h"
#include <QPushButton>
#include <QMenu>
#include <QInputMethodEvent>
#include <QInputMethodQueryEvent>
using namespace Research;

int main(int argc,char **argv) {
  const auto report=argument(1);
  if(!qEnvironmentVariable("TEXLINE_TEST_DESKTOP").startsWith("TeXlineValidation-"))return 2;
  argc=1;QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
  QTimer::singleShot(80,&app,[&]{
    const auto folder=QFileInfo(report).absolutePath();QJsonArray checks,frames,focusTrace;int failures=0;
    auto save=[&]{put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"frames",frames},{"focusTrace",focusTrace},{"devicePixelRatio",qApp->primaryScreen()->devicePixelRatio()},{"physicalInput",false},{"systemIme",false},{"nativeFileDialog",false}}).toJson());};
    auto check=[&](const QString &name,bool passed){checks.append(QJsonObject{{"name",name},{"passed",passed}});failures+=!passed;save();};
    auto capture=[&](QWidget *widget,const QString &name){QCoreApplication::sendPostedEvents(nullptr,QEvent::LayoutRequest);settle(60);const auto image=widget->grab();const bool saved=image.save(folder+"/"+name+".png");frames.append(QJsonObject{{"file",name+".png"},{"width",widget->width()},{"height",widget->height()},{"devicePixelRatio",image.devicePixelRatio()},{"saved",saved}});check(name+": actual widget captured",saved);};
    QTimer watchdog;watchdog.setSingleShot(true);watchdog.setInterval(75000);
    QObject::connect(&watchdog,&QTimer::timeout,&app,[&]{check("fixture finishes without an unexpected modal",false);if(auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget()))dialog->reject();app.exit(1);});watchdog.start();
    Tw::Settings().setValue("openPDFwithTeX",false);Tw::Settings().setValue("researchFocusMode",false);Tw::Settings().setValue("openDialogDir",folder);
    app.clearRecentFiles();
    const auto screen=qApp->primaryScreen()->availableGeometry();
    const QSize normalSize(qEnvironmentVariableIsSet("TEXLINE_TEST_HALF")?screen.width()/2:1120,qMin(720,screen.height()));
    auto startButton=[](TeXDocumentWindow *w,const char *name){return w->findChild<QPushButton*>(QString::fromLatin1(name));};
    auto visible=[&](TeXDocumentWindow *w){auto *a=startButton(w,"researchStartOpen"),*b=startButton(w,"researchStartNew");return a&&b&&a->isVisible()&&b->isVisible();};
    auto absent=[&](TeXDocumentWindow *w){auto *a=startButton(w,"researchStartOpen"),*b=startButton(w,"researchStartNew");return (!a||!a->isVisible())&&(!b||!b->isVisible());};
    auto fresh=[&]{auto *w=new TeXDocumentWindow();w->resize(normalSize);w->show();QApplication::setActiveWindow(w);w->editor()->setFocus();check("new untitled document offers both starting points",waitFor([&]{return visible(w);},3000));return w;};
    auto close=[](TeXDocumentWindow *w){w->textDoc()->setModified(false);w->close();settle(80);};
    auto geometry=[&](TeXDocumentWindow *w,const QString &name){
      auto *a=startButton(w,"researchStartOpen"),*b=startButton(w,"researchStartNew");if(!a||!b){check(name+": start controls exist",false);return;}
      auto *viewport=w->editor()->viewport();const auto cursor=w->editor()->cursorRect();const QRect line(0,cursor.top(),viewport->width(),cursor.height());
      check(name+": controls fit their writing viewport without overlap",visible(w)&&a->parentWidget()==viewport&&b->parentWidget()==viewport&&viewport->rect().contains(a->geometry())&&viewport->rect().contains(b->geometry())&&!a->geometry().intersects(b->geometry()));
      check(name+": the whole first line stays unobstructed",!line.intersects(a->geometry())&&!line.intersects(b->geometry())&&viewport->childAt(cursor.center())!=a&&viewport->childAt(cursor.center())!=b);
      check(name+": captions fit at the existing font size",a->height()>=a->fontMetrics().height()&&b->height()>=b->fontMetrics().height()&&a->width()>=a->fontMetrics().horizontalAdvance(a->text())&&b->width()>=b->fontMetrics().horizontalAdvance(b->text()));
      check(name+": no parent surface intercepts document drops",!a->acceptDrops()&&!b->acceptDrops()&&w->editor()->acceptDrops()&&viewport->childAt(QPoint(8,viewport->height()/2))==nullptr);
      capture(w,name);
    };
    auto *w=fresh();
    if(!visible(w)){close(w);watchdog.stop();app.exit(1);return;}
    check("initial focus remains in the empty source",w->untitled()&&w->textDoc()->editorText().isEmpty()&&w->textCursor().position()==0&&w->editor()->hasFocus()&&!w->isModified());
    installWritingStart(w);check("install is idempotent",w->findChildren<QObject*>("researchWritingStart",Qt::FindDirectChildrenOnly).size()==1&&w->findChildren<QPushButton*>("researchStartOpen").size()==1);
    geometry(w,"start-normal");
    auto *open=startButton(w,"researchStartOpen"),*fromTemplate=startButton(w,"researchStartNew");
    fromTemplate->setFocus(Qt::TabFocusReason);key(fromTemplate,Qt::Key_Tab);check("Tab moves from New to Open without inserting text",open->hasFocus()&&w->textDoc()->editorText().isEmpty());
    key(open,Qt::Key_Backtab,Qt::ShiftModifier);check("Backtab returns to New",fromTemplate->hasFocus());
    key(fromTemplate,Qt::Key_Backtab,Qt::ShiftModifier);check("Backtab from the first start control returns to writing",w->editor()->hasFocus());
    open->setFocus(Qt::TabFocusReason);key(open,Qt::Key_Tab);check("Tab after the last start control returns to writing",w->editor()->hasFocus()&&visible(w));

    auto cancel=[&](QPushButton *button,int code,bool fileDialog,const QString &name){
      bool reached=false;QTimer responder,timeout;responder.setInterval(20);timeout.setSingleShot(true);timeout.setInterval(6000);
      QObject::connect(&responder,&QTimer::timeout,&app,[&]{if(!fileDialog)if(auto *menu=qobject_cast<QMenu*>(QApplication::activePopupWidget())){menu->setActiveAction(menu->actions().last());key(menu,Qt::Key_Return);return;}auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog)return;responder.stop();reached=fileDialog?qobject_cast<QFileDialog*>(dialog)!=nullptr:dialog->objectName()=="researchTemplateDialog";capture(dialog,name);key(dialog,Qt::Key_Escape);});
      QObject::connect(&timeout,&QTimer::timeout,&app,[&]{if(auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget()))dialog->reject();if(auto *popup=QApplication::activePopupWidget())popup->close();});
      button->setFocus(Qt::TabFocusReason);responder.start();timeout.start();key(button,code,Qt::NoModifier,code==Qt::Key_Space?QStringLiteral(" "):QString());responder.stop();timeout.stop();settle(100);
      auto objectName=[](QObject *o){return o?QString::fromLatin1(o->metaObject()->className())+":"+o->objectName():QStringLiteral("null");};
      focusTrace.append(QJsonObject{{"name",name},{"activeWindow",objectName(QApplication::activeWindow())},{"windowActive",w->isActiveWindow()},{"focusWidget",objectName(QApplication::focusWidget())},{"editorFocus",w->editor()->hasFocus()},{"windowFocusWidget",objectName(w->focusWidget())},{"applicationState",int(QApplication::applicationState())},{"popup",objectName(QApplication::activePopupWidget())},{"modal",objectName(QApplication::activeModalWidget())}});
      check(name+": existing action opens the intended dialog",reached);
      check(name+": Escape keeps a fresh page and restores writing focus",waitFor([&]{return !QApplication::activeModalWidget()&&w->editor()->hasFocus();},2000)&&w->untitled()&&w->textDoc()->editorText().isEmpty()&&!w->isModified()&&visible(w));
    };
    cancel(open,Qt::Key_Return,true,"start-open-cancel");
    cancel(fromTemplate,Qt::Key_Space,false,"start-template-cancel");
    check("original Open and Template shortcuts remain available",w->findChild<QAction*>("actionOpen")->shortcut()==QKeySequence("Ctrl+O")&&w->findChild<QAction*>("actionNew_from_Template")->shortcut()==QKeySequence("Ctrl+Shift+N"));

    const auto locale=uiLocale();
    for(const auto &language:QStringList{"zh_CN","zh_TW","en"}){
      TWApp::instance()->applyTranslation(language);settle(80);
      const auto expectedOpen=language=="en"?QStringLiteral("Open file…"):language=="zh_TW"?QStringLiteral("開啟檔案…"):QStringLiteral("打开文件…");
      const auto expectedTemplate=language=="en"?QStringLiteral("New file…"):language=="zh_TW"?QStringLiteral("新建檔案…"):QStringLiteral("新建文件…");
      check(language+": both labels follow the current interface language",open->text()==expectedOpen&&fromTemplate->text()==expectedTemplate);
      geometry(w,"start-"+language);
    }TWApp::instance()->applyTranslation(locale);settle(60);
    w->resize(screen.width()/2,qMin(720,screen.height()));settle(100);geometry(w,"start-half");
    setFocusMode(true);settle(200);geometry(w,"start-focus-half");
    check("focus-mode first line and input focus are preserved",w->editor()->hasFocus()&&w->textCursor().position()==0&&w->textDoc()->editorText().isEmpty());
    setFocusMode(false);settle(160);
    open->setFocus(Qt::TabFocusReason);key(open,Qt::Key_Escape);check("Escape removes the starting points and returns to writing",absent(w)&&w->editor()->hasFocus()&&w->textDoc()->editorText().isEmpty());
    w->resize(normalSize);settle(80);check("Escape dismissal survives layout changes",absent(w));close(w);

    // Printable input, undo/redo and clearing are tested on separate fresh windows.
    w=fresh();open=startButton(w,"researchStartOpen");if(open){open->setFocus(Qt::TabFocusReason);key(open,Qt::Key_A,Qt::NoModifier,"a");}
    check("typing on a focused start control reaches the first line exactly once",w->editor()->hasFocus()&&w->textDoc()->editorText()=="a"&&w->textCursor().position()==1&&absent(w));
    w->editor()->undo();check("undo to an empty document does not resurrect the start controls",w->textDoc()->editorText().isEmpty()&&absent(w));w->editor()->redo();check("redo restores the exact first keystroke",w->textDoc()->editorText()=="a"&&absent(w));
    w->editor()->selectAll();key(w->editor(),Qt::Key_Delete);check("deleting written content to empty does not reopen the start controls",w->textDoc()->editorText().isEmpty()&&absent(w));close(w);
    w=fresh();key(w->editor(),Qt::Key_Tab,Qt::NoModifier,"\t");check("Tab in the source keeps the existing indentation behavior",!w->textDoc()->editorText().isEmpty()&&w->textDoc()->editorText().trimmed().isEmpty()&&absent(w)&&w->editor()->hasFocus());close(w);

    for(const bool onButton:{false,true}){
      w=fresh();open=startButton(w,"researchStartOpen");auto *editor=w->editor();const QString label=onButton?"button IME":"editor IME";
      if(onButton&&open){
        open->setFocus(Qt::TabFocusReason);
        QInputMethodEvent reset;QApplication::sendEvent(open,&reset);check("an empty IME focus reset is not mistaken for typing",open->hasFocus()&&visible(w)&&w->textDoc()->editorText().isEmpty());
        QInputMethodQueryEvent query(Qt::ImEnabled|Qt::ImCursorRectangle|Qt::ImAnchorRectangle|Qt::ImInputItemClipRectangle|Qt::ImSurroundingText|Qt::ImCurrentSelection|Qt::ImCursorPosition|Qt::ImAnchorPosition|Qt::ImHints);QApplication::sendEvent(open,&query);
        check("keyboard-focused start control is a real input-method target",open->testAttribute(Qt::WA_InputMethodEnabled)&&query.value(Qt::ImEnabled).toBool());
        bool rectangles=true;for(const auto property:{Qt::ImCursorRectangle,Qt::ImAnchorRectangle,Qt::ImInputItemClipRectangle}){const auto expected=editor->inputMethodQuery(property).toRectF();const auto actual=query.value(property).toRectF().translated(open->mapTo(editor,QPoint()));rectangles&=expected==actual;}
        check("IME candidate geometry maps to the source cursor, not the start control",rectangles&&!query.value(Qt::ImCursorRectangle).toRectF().isEmpty());
        check("IME queries expose the empty source and its input hints",query.value(Qt::ImSurroundingText).toString().isEmpty()&&query.value(Qt::ImCurrentSelection).toString().isEmpty()&&query.value(Qt::ImCursorPosition).toInt()==0&&query.value(Qt::ImAnchorPosition).toInt()==0&&query.value(Qt::ImHints)==editor->inputMethodQuery(Qt::ImHints));
      }
      bool composing=false;const auto connection=QObject::connect(editor,&CompletingEdit::compositionChanged,&app,[&](bool value){composing=value;});
      QInputMethodEvent preedit("zhong",{QInputMethodEvent::Attribute(QInputMethodEvent::Cursor,5,1,QVariant())});QApplication::sendEvent(onButton&&open?static_cast<QWidget*>(open):editor,&preedit);
      check(label+": synthetic preedit removes controls without committing source",composing&&editor->hasFocus()&&w->textDoc()->editorText().isEmpty()&&absent(w));
      QInputMethodEvent commit;commit.setCommitString(QStringLiteral("中文"));QApplication::sendEvent(editor,&commit);
      check(label+": synthetic commit reaches the document exactly once",!composing&&w->textDoc()->editorText()==QStringLiteral("中文")&&w->textCursor().position()==2&&absent(w));
      editor->undo();check(label+": one undo clears the committed text without reopening start",w->textDoc()->editorText().isEmpty()&&absent(w));editor->redo();check(label+": redo preserves committed Chinese text",w->textDoc()->editorText()==QStringLiteral("中文")&&absent(w));
      capture(w,onButton?"start-ime-from-button":"start-ime-in-editor");QObject::disconnect(connection);close(w);
    }
    w=fresh();open=startButton(w,"researchStartOpen");if(open){open->setFocus(Qt::TabFocusReason);QInputMethodEvent preedit("quxiao",{});QApplication::sendEvent(open,&preedit);QInputMethodEvent cancelComposition;QApplication::sendEvent(w->editor(),&cancelComposition);}
    check("cancelling synthetic composition leaves a clean writable page",w->textDoc()->editorText().isEmpty()&&w->textCursor().position()==0&&w->editor()->hasFocus()&&absent(w));close(w);

    const auto emptyFile=folder+"/paper/empty.tex";check("independent empty file is created",put(emptyFile,{}));
    w=new TeXDocumentWindow(emptyFile);w->resize(normalSize);w->show();QApplication::setActiveWindow(w);w->editor()->setFocus();settle(100);
    check("an existing empty file never receives start controls",!w->untitled()&&w->textDoc()->editorText().isEmpty()&&absent(w));installWritingStart(w);settle(40);check("explicit install also guards existing empty files",absent(w));capture(w,"start-existing-empty");close(w);
    const auto contentFile=folder+"/paper/existing.tex";const QByteArray source("\\documentclass{article}\n\\begin{document}\nAn existing paragraph.\n\\end{document}\n");check("independent existing manuscript is created",put(contentFile,source));
    w=new TeXDocumentWindow(contentFile);w->resize(normalSize);w->show();QApplication::setActiveWindow(w);w->editor()->setFocus();settle(80);w->editor()->selectAll();key(w->editor(),Qt::Key_Delete);settle(40);check("clearing a loaded manuscript never presents it as a new paper",w->textDoc()->editorText().isEmpty()&&absent(w));w->editor()->undo();check("loaded manuscript deletion is one exact undo",w->textDoc()->editorText().toUtf8()==source&&absent(w));close(w);
    for(const auto &language:QStringList{"zh_CN","zh_TW","en"})for(int choice=0;choice<3;++choice){
      TWApp::instance()->applyTranslation(language);w=fresh();
      check(language+": empty untitled page cannot start a compile",!w->findChild<QAction*>("actionTypeset")->isEnabled());
      QTimer picker;picker.setInterval(30);bool picked=false;
      QObject::connect(&picker,&QTimer::timeout,&app,[&]{if(auto *menu=qobject_cast<QMenu*>(QApplication::activePopupWidget())){picker.stop();picked=true;menu->setActiveAction(menu->actions()[choice]);key(menu,Qt::Key_Return);}});
      picker.start();startButton(w,"researchStartNew")->click();picker.stop();settle(100);
      check(language+": native new menu selection completes",picked);
      const auto text=w->textDoc()->editorText();
      if(choice<2){
        check(language+": starter keeps current unnamed window and writing focus",w->untitled()&&w->isModified()&&w->editor()->hasFocus()&&absent(w));
        check(language+": starter contains document boundaries",text.contains("\\begin{document}")&&text.endsWith("\\end{document}\n"));
        check(language+": caret is on the blank body line",text.mid(w->textCursor().position()).startsWith(choice==1?"\n\\end{frame}":"\n\\end{document}"));
        check(language+": starter declares the appropriate engine",text.startsWith(language=="en"?"% !TEX TS-program = pdflatex":"% !TEX TS-program = xelatex"));
        check(language+": compile is available after creating content",w->findChild<QAction*>("actionTypeset")->isEnabled());
        w->editor()->undo();check(language+": one undo clears only the starter",w->textDoc()->isEmpty()&&absent(w)&&!w->findChild<QAction*>("actionTypeset")->isEnabled());
        w->editor()->redo();check(language+": redo returns exact starter source",w->textDoc()->editorText()==text);
      }else check(language+": blank source dismisses start without adding text or undo",text.isEmpty()&&!w->textDoc()->isUndoAvailable()&&absent(w)&&w->editor()->hasFocus());
      close(w);
    }
    TWApp::instance()->applyTranslation(locale);
    // Reuse ordinary recent-file actions; settings only control their start-page visibility.
    app.clearRecentFiles();app.addToRecentFiles({{"path",contentFile}});w=fresh();
    auto *recent=startButton(w,"researchStartRecent0");
    check("existing recent file appears with its full accessible name",recent&&recent->isVisible()&&recent->accessibleName().contains("existing.tex"));
    Tw::Settings().setValue("researchStartRecentFiles",false);refreshWritingStart(w);settle(50);
    check("saved visibility preference hides recent rows",recent&&!recent->isVisible()&&visible(w));
    Tw::Settings().setValue("researchStartRecentFiles",true);refreshWritingStart(w);settle(50);
    check("visibility can be restored without changing source",recent&&recent->isVisible()&&w->textDoc()->isEmpty());
    app.clearRecentFiles();settle(50);check("clearing existing history removes start rows immediately",recent&&!recent->isVisible());close(w);
    check("private source files remain unchanged",bytes(emptyFile).isEmpty()&&bytes(contentFile)==source);
    watchdog.stop();save();app.exit(failures?1:0);
  });return app.exec();
}
