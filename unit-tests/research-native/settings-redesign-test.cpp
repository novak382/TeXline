// Real widgets and editor events, on an inactive desktop with private settings.
#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "TeXDocumentWindow.h"
#include "PDFDocumentWindow.h"
#include "CompletingEdit.h"
#include "PrefsDialog.h"
#include "ResearchAppearance.h"
#include "ResearchCommands.h"
#include "ResearchCompletion.h"
#include "ResearchShorthand.h"
#include "ResearchCommandCompletion.h"
#include "Settings.h"
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QInputMethodEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPdfWriter>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QSpinBox>
#include <QScreen>
#include <QScrollArea>
#include <QStyle>
#include <QStyleOptionToolButton>
#include <QTableWidget>
#include <QTabBar>
#include <QTabWidget>
#include <QTextBlock>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <windows.h>
#include <functional>
static bool waitFor(const std::function<bool()> &ready,int ms=30000){QElapsedTimer t;t.start();while(!ready()&&t.elapsed()<ms){QCoreApplication::processEvents();QThread::msleep(4);}return ready();}
static bool privateDesktop(){wchar_t name[256]{};DWORD needed=0;return GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()),UOI_NAME,name,sizeof(name),&needed)&&QString::fromWCharArray(name)==qEnvironmentVariable("TEXLINE_TEST_DESKTOP")&&QString::fromWCharArray(name).startsWith("TeXlineValidation-");}
static void settle(int ms=60){QElapsedTimer t;t.start();while(t.elapsed()<ms){QCoreApplication::processEvents();QThread::msleep(2);}}
static void key(QWidget *w,int code,QString text={},Qt::KeyboardModifiers mods=Qt::NoModifier){QKeyEvent e(QEvent::KeyPress,code,mods,text),r(QEvent::KeyRelease,code,mods);QApplication::sendEvent(w,&e);QApplication::sendEvent(w,&r);}
static void type(QWidget *w,const QString &text){for(auto c:text)key(w,int(c.toUpper().unicode()),QString(c));settle(10);}
static bool put(const QString &path,const QByteArray &data){QSaveFile f(path);return f.open(QIODevice::WriteOnly)&&f.write(data)==data.size()&&f.commit();}
int main(int argc,char **argv){
  if(argc<2||!privateDesktop())return 2;const auto report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
  QTimer::singleShot(100,&app,[&]{
    const QString folder=QFileInfo(report).absolutePath();QJsonArray checks,frames,disclosureMetrics;int failures=0;
    // Independent expected text: do not obtain expected labels from the same
    // translation helper used by the controls under test.
    const auto requestedLocale=qEnvironmentVariable("TEXLINE_TEST_LOCALE",Tw::Settings().value("locale","zh_CN").toString());
    const QString locale=requestedLocale.startsWith("en")?"en":requestedLocale;
    const QHash<QString,QStringList> localeLabels{
      {"zh_CN",{"语言 / Language","跟随上一行","编码"}},
      {"zh_TW",{"語言 / Language","跟隨上一行","編碼"}},
      {"en",{"语言 / Language","Follow previous line","Encoding"}}
    };
    const auto expectedLabels=localeLabels.value(locale);
    auto check=[&](const QString &name,bool pass){checks.append(QJsonObject{{"name",name},{"passed",pass}});failures+=!pass;put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"frames",frames},{"disclosureMetrics",disclosureMetrics},{"physicalInputVerified",false}}).toJson());};
    check("requested fixture language is active",localeLabels.contains(locale)&&Research::englishUi()==(locale=="en")&&Research::traditionalUi()==(locale=="zh_TW"));
    if(!localeLabels.contains(locale)){app.exit(1);return;}
    auto measureCaption=[&](QToolButton *button,const QString &name){
      auto rectJson=[](const QRect &r){return QJsonObject{{"x",r.x()},{"y",r.y()},{"width",r.width()},{"height",r.height()}};};
      auto sizeJson=[](const QSize &s){return QJsonObject{{"width",s.width()},{"height",s.height()}};};
        QStyleOptionToolButton option;option.initFrom(button);option.text=button->text();option.font=button->font();option.icon=button->icon();option.iconSize=button->iconSize();option.toolButtonStyle=button->toolButtonStyle();
        if(button->isChecked())option.state|=QStyle::State_On;
        // The stylesheet's frame-content query removes its actual border and
        // padding; QWidget::contentsRect() alone still includes that space.
        const auto content=button->style()->subElementRect(QStyle::SE_FrameContents,&option,button);
        const auto icon=button->icon().isNull()?QSize(0,0):button->icon().actualSize(content.size().boundedTo(button->iconSize()),button->isEnabled()?QIcon::Normal:QIcon::Disabled,button->isChecked()?QIcon::On:QIcon::Off);
        const auto metrics=button->fontMetrics();const int advance=metrics.horizontalAdvance(button->text());
        const bool beside=button->toolButtonStyle()==Qt::ToolButtonTextBesideIcon;
        const int iconSpace=beside&&!button->icon().isNull()?icon.width()+4:0;
        const QRect textRect=content.adjusted(iconSpace,0,0,0);
        // Qt's toolbutton elision gate measures the original text, including
        // mnemonic markers, before painting with TextShowMnemonic.
        const bool fits=(beside||button->toolButtonStyle()==Qt::ToolButtonTextOnly)&&textRect.width()>=advance&&textRect.height()>=metrics.height()&&content.height()>=icon.height();
        disclosureMetrics.append(QJsonObject{{"frame",name},{"object",button->objectName()},{"text",button->text()},{"fontAdvance",advance},{"mnemonicAdvance",metrics.size(Qt::TextShowMnemonic,button->text()).width()},{"mnemonic",QKeySequence::mnemonic(button->text()).toString()},{"rect",rectJson(button->rect())},{"widgetContentsRect",rectJson(button->contentsRect())},{"contentRect",rectJson(content)},{"textRect",rectJson(textRect)},{"iconSize",sizeJson(button->iconSize())},{"actualIconSize",sizeJson(icon)},{"requiredWidth",advance+iconSpace},{"fontHeight",metrics.height()},{"fits",fits}});
        return fits;
    };
    auto capture=[&](QWidget *widget,const QString &name){
      const auto image=widget->grab();image.save(folder+"/"+name+".png");frames.append(QJsonObject{{"name",name},{"dpr",image.devicePixelRatio()},{"width",widget->width()},{"height",widget->height()}});
      int measured=0;bool captionsFit=true;
      for(auto *button:widget->findChildren<QToolButton *>())if(button->isVisible()&&button->property("settingsDisclosureTrigger").toBool()){
        captionsFit&=measureCaption(button,name);++measured;
      }
      if(measured)check("disclosure captions fit without elision: "+name,captionsFit);
    };
    const auto file=folder+"/independent.tex";const QByteArray original="\\documentclass{article}\n\\usepackage{amsmath}\n\\newtheorem{theorem}{Theorem}\n\\begin{document}\n\\section{A working draft}\nResearch notes on estimation.\n\\section{Checks}\n\\begin{theorem}A reference statement.\\end{theorem}\n\\end{document}\n";check("independent fixture written",put(file,original));
    auto *w=new TeXDocumentWindow(file);w->resize(1000,760);w->show();settle(650);auto *e=w->editor();
    auto reset=[&](const QString &text,int position=-1){Research::cancelCommandCompletion(w);e->setPlainText(text);auto c=e->textCursor();c.setPosition(position<0?int(text.size()):position);e->setTextCursor(c);e->setFocus();settle(5);};
    auto *normal=w->findChild<QMenu *>("researchMoreMenu"),*focus=w->findChild<QMenu *>("researchFocusMenu");QMetaObject::invokeMethod(normal,"aboutToShow");
    check("source menu keeps twelve concise entries including reading context",normal&&[&]{int count=0;for(auto *a:normal->actions())count+=!a->isSeparator();return count;}()==12);
    check("normal and focus borrow identical menu actions",focus&&normal->actions()==focus->actions());
    auto *insert=w->findChild<QMenu *>("researchInsertMenu");check("Insert is a top-level menu",insert&&normal->actions().contains(insert->menuAction()));
    auto *language=w->findChild<QMenu *>("researchLanguageMenu");check("language recovery stays bilingual and top-level",language&&language->title()==expectedLabels[0]&&normal->actions().contains(language->menuAction()));
    check("new authoring actions use stable identities",w->findChild<QAction *>("researchSaveSnippet")&&w->findChild<QAction *>("researchCreateCommand"));
    auto *engines=w->findChild<QMenu *>("researchEngineMenu");QMetaObject::invokeMethod(engines,"aboutToShow");check("compile menu keeps sync-directory setup beside engine choices",engines->findChild<QAction *>("researchSyncDirectories")&&engines->actions().size()>3);bool onlyRelevant=true;for(auto *a:engines->actions())if(a->objectName().startsWith("researchOutput"))onlyRelevant &= !a->isVisible();check("ordinary local folders hide sync-only output modes",onlyRelevant);
    check("document output settings remain accessible",w->findChild<QMenu *>("researchDocumentBuildMenu"));
    std::function<int(QMenu *,int)> depth=[&](QMenu *menu,int n){int max=n;for(auto *a:menu->actions())if(a->menu())max=qMax(max,depth(a->menu(),n+1));return max;};check("all visible menu branches stay within three levels",depth(normal,1)<=3);
    Research::setCommandCompletionMode(0);Tw::Settings().setValue("researchShorthandEnabled",false);const auto usage=Tw::Settings().value("researchCommandCompletionUsage");const auto sourceBefore=e->toPlainText();
    QTimer::singleShot(100,&app,[&]{auto *d=qobject_cast<QDialog *>(QApplication::activeModalWidget());check("ordinary preferences opens",d);if(!d)return;auto *tabs=d->findChild<QTabWidget *>("tabWidget");auto *page=d->findChild<QWidget *>("settingsEditor");auto *toggle=d->findChild<QToolButton *>("researchSnippetDetailsToggle");auto *mode=d->findChild<QComboBox *>("completionConfigMode");check("writing exposes mode with snippet management initially collapsed",tabs&&page&&toggle&&!toggle->isChecked()&&mode);if(!tabs||!page||!toggle||!mode){d->reject();return;}tabs->setCurrentWidget(page);settle(220);check("completion mode remains directly available without opening management",mode->isVisible());mode->setCurrentIndex(2);d->findChild<QDialogButtonBox *>("buttonBox")->button(QDialogButtonBox::Ok)->click();check("ordinary Save succeeds without opening snippet management",d->result()==QDialog::Accepted);if(d->isVisible())d->reject();});PrefsDialog::doPrefsDialog(w);check("ordinary Save persists completion mode",Tw::Settings().value("researchCommandCompletionMode").toInt()==2);Research::setCommandCompletionMode(0);
    QTimer::singleShot(100,&app,[&]{
      auto *d=qobject_cast<QDialog *>(QApplication::activeModalWidget());check("settings opens as an actual dialog",d);if(!d)return;
      auto *tabs=d->findChild<QTabWidget *>("tabWidget");auto *nav=d->findChild<QListWidget *>("researchSettingsNavigation");
      const QStringList ids{"settingsGeneral","settingsEditor","settingsPdf","settingsShortcuts","settingsBuild","settingsAdvanced"};QStringList actual;for(int n=0;tabs&&n<tabs->count();++n)actual.append(tabs->widget(n)->objectName());
      check("settings has six stable task groups",actual==ids&&nav&&nav->count()==6);if(actual!=ids||!nav){d->reject();return;}
      auto page=[&](const char *id){return d->findChild<QScrollArea *>(id);};auto showPage=[&](const char *id){tabs->setCurrentWidget(page(id));};
      auto *snippets=d->findChild<QToolButton *>("researchSnippetDetailsToggle");auto *buildDetails=d->findChild<QToolButton *>("researchBuildDetailsToggle");auto *inputTabs=d->findChild<QTabBar *>("researchInputTabs");
      check("secondary settings have explicit on-demand controls",snippets&&buildDetails&&inputTabs);if(!snippets||!buildDetails||!inputTabs){d->reject();return;}
      QToolButton mnemonicProbe(d);mnemonicProbe.setObjectName("settingsMnemonicSizingProbe");mnemonicProbe.setText("Indentation, quotes &spelling…");mnemonicProbe.setIcon(QIcon(":/research/icons/chevron-right.svg"));mnemonicProbe.setToolButtonStyle(Qt::ToolButtonTextBesideIcon);Research::configureButton(&mnemonicProbe,Research::ButtonRole::Text);mnemonicProbe.ensurePolished();
      check("shared text button reserves Qt elision width while retaining its mnemonic",measureCaption(&mnemonicProbe,"configured-mnemonic-probe")&&!QKeySequence::mnemonic(mnemonicProbe.text()).isEmpty()&&mnemonicProbe.shortcut()==QKeySequence::mnemonic(mnemonicProbe.text())&&mnemonicProbe.text()=="Indentation, quotes &spelling…");
      auto showSnippets=[&]{showPage("settingsEditor");if(!snippets->isChecked())snippets->click();page("settingsEditor")->ensureWidgetVisible(snippets);settle(260);};
      auto *indent=d->findChild<QComboBox *>("autoIndent"),*quotes=d->findChild<QComboBox *>("smartQuotes");check("editor mode labels describe behavior and preserve configuration identities",indent->itemText(1)==expectedLabels[1]&&indent->itemData(1).toString()=="Whitespace only"&&quotes->itemData(1).toString()=="TeX ligatures");
      {
        const auto point=nav->visualItemRect(nav->item(tabs->indexOf(page("settingsPdf")))).center();
        for(auto kind:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}){QMouseEvent event(kind,point,nav->viewport()->mapToGlobal(point),Qt::LeftButton,kind==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(nav->viewport(),&event);}
        settle(260);check("pointer selects the PDF settings category",tabs->currentWidget()==page("settingsPdf"));
        nav->setFocus(Qt::MouseFocusReason);settle(60);capture(d,"category-pointer-focus");const auto focused=nav->viewport()->grab().toImage();nav->clearFocus();settle(60);
        check("pointer focus adds no inner rectangle to the selected category",focused==nav->viewport()->grab().toImage());
        nav->setFocus(Qt::TabFocusReason);key(nav,Qt::Key_Down);settle(260);
        check("keyboard category navigation retains focus and selection feedback",nav->hasFocus()&&nav->currentRow()==tabs->indexOf(page("settingsShortcuts"))&&tabs->currentWidget()==page("settingsShortcuts"));capture(d,"category-keyboard-focus");
        showSnippets();check("snippet list exposes its three reading columns",d->findChild<QWidget *>("completionSnippetHeader")&&d->findChild<QWidget *>("completionSnippetHeader")->isVisible());
      }
      snippets->click();
      for(const auto &id:ids){showPage(id.toUtf8().constData());settle(220);capture(d,"settings-"+id);}
      const auto fullSize=d->size();d->resize(768,544);
      for(const char *id:{"settingsGeneral","settingsEditor","settingsShortcuts"}){showPage(id);settle(260);capture(d,QString("comparison-%1-768x544").arg(id));}
      showSnippets();auto *snippetList=d->findChild<QListWidget *>("completionSnippetList");check("snippet list exists after opening management",snippetList);if(!snippetList){d->reject();return;}page("settingsEditor")->ensureWidgetVisible(snippetList);settle(260);
      const auto listRect=QRect(snippetList->mapTo(page("settingsEditor")->viewport(),QPoint()),snippetList->size());
      check("expanded snippet list remains usefully sized and reachable",snippetList->isVisible()&&snippetList->height()>=128&&page("settingsEditor")->viewport()->rect().contains(listRect)&&page("settingsEditor")->horizontalScrollBar()->maximum()==0);capture(d,"comparison-snippets-768x544");
      check("snippet library has no competing outer scroll at the comparison size",page("settingsEditor")->verticalScrollBar()->maximum()==0&&d->findChild<QWidget *>("researchWritingBasics")&&!d->findChild<QWidget *>("researchWritingBasics")->isVisible()&&d->findChild<QComboBox *>("completionConfigMode")->isVisible());
      d->resize(fullSize);d->resize(520,650);
      for(const char *id:{"settingsEditor","settingsShortcuts","settingsBuild"}){showPage(id);if(qstrcmp(id,"settingsShortcuts")==0)inputTabs->setCurrentIndex(1);if(qstrcmp(id,"settingsBuild")==0&&!buildDetails->isChecked())buildDetails->click();settle(220);capture(d,QString("settings-narrow-%1").arg(id));}
      d->resize(fullSize);auto *box=d->findChild<QDialogButtonBox *>("buttonBox");check("save and cancel stay inside the dialog",d->rect().contains(QRect(box->mapTo(d,QPoint()),box->size())));settle(260);auto *saveButton=box->button(QDialogButtonBox::Ok);const auto saveColor=saveButton->grab().toImage().pixelColor(QPoint(7,saveButton->height()/2)*saveButton->devicePixelRatioF());check("primary Save button keeps the shared light surface",saveColor.red()>210&&saveColor.green()>210&&saveColor.blue()>210);
      const auto pathsBefore=app.getPrefsBinaryPaths();QStringList enginesBefore;for(const auto &engine:app.getEngineList())enginesBefore.append(engine.name());showPage("settingsBuild");box->button(QDialogButtonBox::RestoreDefaults)->click();QStringList enginesAfter;for(const auto &engine:app.getEngineList())enginesAfter.append(engine.name());check("resetting build tools changes a draft without mutating the app",pathsBefore==app.getPrefsBinaryPaths()&&enginesBefore==enginesAfter);
      showPage("settingsEditor");d->resize(520,qMin(650,d->height()));settle();capture(d,"settings-narrow");check("narrow settings use a single category selector",d->findChild<QComboBox *>("researchSettingsCategory")->isVisible()&&!nav->isVisible());
      auto *search=d->findChild<QLineEdit *>("researchSettingsSearch");search->setText(expectedLabels[2]);settle();auto *matches=d->findChild<QListWidget *>("researchSettingsMatches");check("search finds advanced settings",matches&&matches->count()>0&&(matches->item(0)->flags()&Qt::ItemIsEnabled));key(search,Qt::Key_Return);check("search navigates to the matching page",tabs->currentWidget()==page("settingsAdvanced"));search->clear();
      auto *completionRoot=d->findChild<QWidget *>("researchCompletionPage");const auto untouchedDraft=completionRoot->property("completionDraft");const QString triggerQuery=locale=="en"?"Trigger":locale=="zh_TW"?"觸發方式":"触发方式";
      auto fullyReadable=[&](QWidget *target){auto *viewport=page("settingsEditor")->viewport();return target&&target->isVisible()&&target->width()>0&&target->height()>0&&viewport->rect().contains(QRect(target->mapTo(viewport,QPoint()),target->size()));};
      if(snippets->isChecked())snippets->click();search->setFocus();search->setText(triggerQuery);key(search,Qt::Key_Return);settle(260);auto *snippetSearch=d->findChild<QLineEdit *>("shorthandSearch");auto *collection=d->findChild<QTabBar *>("shorthandCollection");
      check("trigger search without a selected snippet returns to the library without creating a draft",tabs->currentWidget()==page("settingsEditor")&&snippets->isChecked()&&fullyReadable(snippetSearch)&&snippetSearch->hasFocus()&&fullyReadable(collection)&&d->property("researchSettingsSearchTarget").toString()=="shorthandSearch"&&completionRoot->property("completionDraft")==untouchedDraft);capture(d,"snippet-search-without-selection");search->clear();
      showSnippets();if(auto *toggle=d->findChild<QPushButton *>("shorthandTrialToggle");toggle&&!toggle->isChecked())toggle->click();auto *trial=d->findChild<CompletingEdit *>("shorthandTrial");check("try editor exists without changing the manuscript",trial&&e->toPlainText()==sourceBefore);if(trial){trial->setPlainText("$ $");auto c=trial->textCursor();c.setPosition(2);trial->setTextCursor(c);type(trial,"//");check("trial expands before enabling the feature",trial->toPlainText()=="$ \\frac{}{}$");type(trial,"a");key(trial,Qt::Key_Tab);type(trial,"b");check("trial uses actual snippet navigation",trial->toPlainText()=="$ \\frac{a}{b}$");}
      check("trial does not persist usage or enable settings",Tw::Settings().value("researchCommandCompletionUsage")==usage&&Tw::Settings().value("researchCommandCompletionMode").toInt()==0&&!Tw::Settings().value("researchShorthandEnabled").toBool());
      if(qEnvironmentVariableIsSet("TEXLINE_SETTINGS_PREVIEW")){auto *toggle=d->findChild<QPushButton *>("shorthandFormulaToggle");check("real formula preview is available on demand",toggle);if(toggle){toggle->click();check("formula preview uses actual LaTeX output",waitFor([&]{return d->findChild<QLabel *>("shorthandFormulaPreview")->property("researchDraftReady").toBool();}));capture(d,"formula-trial");check("formula preview does not change source or undo state",e->toPlainText()==sourceBefore);}}
      auto *cursorWidth=d->findChild<QSpinBox *>("cursorWidth");cursorWidth->setValue(3);auto *modeDraft=d->findChild<QComboBox *>("completionConfigMode");modeDraft->setCurrentIndex(2);auto *focusLines=d->findChild<QCheckBox *>("researchFocusLineNumbersSetting");focusLines->setChecked(true);showPage("settingsEditor");box->button(QDialogButtonBox::RestoreDefaults)->click();
      check("writing reset includes merged completion and focus but leaves other categories",cursorWidth->value()==3&&modeDraft->currentIndex()==0&&!focusLines->isChecked());check("editor reset uses the bundled product font",d->findChild<QSpinBox *>("fontSize")->value()==15);
      d->findChild<QPushButton *>("shorthandRecommended")->click();
      d->findChild<QPushButton *>("completionSnippetAdd")->click();auto *draftName=d->findChild<QLineEdit *>("completionSnippetDescription");auto *draftPrefix=d->findChild<QLineEdit *>("completionSnippetPrefix");auto *draftBody=d->findChild<QPlainTextEdit *>("completionSnippetBody");draftName->setText("Uncommitted proof");draftPrefix->setText("draftproof");draftBody->setPlainText("A retained draft.");
      const auto draft=d->findChild<QWidget *>("researchCompletionPage")->property("completionDraft");snippets->click();snippets->click();settle(220);
      check("collapsing and reopening snippet management retains its draft",draftName->text()=="Uncommitted proof"&&draftPrefix->text()=="draftproof"&&draftBody->toPlainText()=="A retained draft."&&d->findChild<QWidget *>("researchCompletionPage")->property("completionDraft")==draft&&modeDraft->currentIndex()==1);
      auto *options=d->findChild<QPushButton *>("shorthandOptions");if(options->isChecked())options->click();snippets->click();search->setFocus();search->setText(triggerQuery);key(search,Qt::Key_Return);settle(260);auto *trigger=d->findChild<QComboBox *>("shorthandTrigger");
      auto *triggerRow=trigger?trigger->parentWidget():nullptr;
      check("trigger search reveals both disclosures and preserves the selected snippet draft",snippets->isChecked()&&options->isChecked()&&fullyReadable(trigger)&&trigger->hasFocus()&&triggerRow&&triggerRow->property("settingTarget").toString()=="shorthandTrigger"&&fullyReadable(triggerRow)&&d->property("researchSettingsSearchTarget").toString()=="shorthandTrigger"&&draftName->text()=="Uncommitted proof"&&draftPrefix->text()=="draftproof"&&draftBody->toPlainText()=="A retained draft."&&completionRoot->property("completionDraft")==draft);capture(d,"snippet-search-selected");search->clear();d->reject();
    });PrefsDialog::doPrefsDialog(w);check("cancel discards recommended shorthand and completion changes",Tw::Settings().value("researchCommandCompletionMode").toInt()==0&&!Tw::Settings().value("researchShorthandEnabled").toBool());check("outer Cancel discards the folded manager snippet draft",Research::userCompletions().isEmpty());
    auto *config=Research::configureLocalCompletion(w);settle();config->findChild<QPushButton *>("shorthandRecommended")->click();check("recommended setup applies and closes in one action",!config->isVisible());settle();check("recommended setup explicitly enables manual suggestions",Tw::Settings().value("researchCommandCompletionMode").toInt()==1&&Tw::Settings().value("researchShorthandEnabled").toBool());
    waitFor([&]{return w->property("researchArticleReady").toBool();},5000);const bool structured=w->property("researchArticleMode").toBool();check("directory fixture is classified as a structured article",structured);Research::setFocusMode(true);settle(240);if(structured){w->findChild<QAction *>("researchOutlineAction")->trigger();settle(240);auto *directory=w->findChild<QDockWidget *>("researchArticleDock");check("focus can open the restrained directory without replacing source",directory&&directory->isVisible()&&e->isVisible());w->findChild<QAction *>("researchOutlineAction")->trigger();settle(240);}
    reset("$ $",2);type(e,"//");check("focus fraction expands as typed",e->toPlainText()=="$ \\frac{}{}$");e->undo();check("one undo restores the complete trigger",e->toPlainText()=="$ //$");e->redo();check("redo restores the expansion",e->toPlainText()=="$ \\frac{}{}$");
    reset("$ $",2);type(e,"//");type(e,"//");type(e,"a");key(e,Qt::Key_Tab);type(e,"b");key(e,Qt::Key_Tab);type(e,"c");put(folder+"/nested-source.txt",e->toPlainText().toUtf8());check("nested shorthand wins before the candidate delay",e->toPlainText()=="$ \\frac{\\frac{a}{b}}{c}$");
    reset("mk");key(e,Qt::Key_Tab);type(e,"x");key(e,Qt::Key_Tab);check("inline math starts with an editable field",e->toPlainText()=="$x$");
    reset("$x.hat$",6);key(e,Qt::Key_Tab);check("postfix wrapper keeps the explicit operand",e->toPlainText()=="$\\hat{x}$");reset("$\\frac{a}{b}.hat$",16);check("ambiguous suffix operands keep the original source",!Research::matchShorthand(e,w,false).valid());
    for(const auto &prefix:QStringList{"ordinary ","% comment ","\\begin{verbatim}\n","$\\text{units ","\\label{key"}){reset(prefix);type(e,"//");check("protected context leaves literal slash: "+prefix,e->toPlainText()==prefix+"//");}
    reset("$ $",2);e->insertPlainText("//");settle();check("paste does not auto-expand",e->toPlainText()=="$ //$");
    reset("$ $",2);QInputMethodEvent preedit("中",{});QApplication::sendEvent(e,&preedit);type(e,"//");check("composition does not auto-expand",!e->toPlainText().contains("\\frac"));QInputMethodEvent end;QApplication::sendEvent(e,&end);
    Research::setFocusMode(false);settle(200);reset("$ $",2);type(e,"//");check("new shorthand stays scoped to focus mode",e->toPlainText()=="$ //$");Research::setFocusMode(true);settle(200);Research::setCommandCompletionMode(0);reset("$ $",2);type(e,"//");check("the master off switch stops shorthand",e->toPlainText()=="$ //$");Research::setCommandCompletionMode(1);
    reset("$a+b$");auto c=e->textCursor();c.setPosition(1);c.setPosition(4,QTextCursor::KeepAnchor);e->setTextCursor(c);w->findChild<QAction *>("researchSaveSnippet")->trigger();settle();QDialog *creator=nullptr;for(auto *top:QApplication::topLevelWidgets())if(auto *d=qobject_cast<QDialog *>(top);d&&d->findChild<QLineEdit *>("completionSnippetPrefix"))creator=d;
    check("selection opens the snippet creator",creator);if(creator){auto *body=creator->findChild<QPlainTextEdit *>("completionSnippetBody");check("selection arrives without placeholder syntax",body&&body->toPlainText()=="a+b");creator->findChild<QLineEdit *>("completionSnippetPrefix")->setText("ab");creator->findChild<QLineEdit *>("completionSnippetDescription")->setText("Two terms");auto b=body->textCursor();b.setPosition(0);b.setPosition(1,QTextCursor::KeepAnchor);body->setTextCursor(b);creator->findChild<QPushButton *>("shorthandMakeField")->click();check("marking a field preserves visible source and adds a highlight",body->toPlainText()=="a+b"&&!body->extraSelections().isEmpty());settle(260);capture(creator,"snippet-from-selection");
      auto *options=creator->findChild<QPushButton *>("shorthandOptions");options->click();settle(260);capture(creator,"snippet-options");check("expanded snippet options stay editable",creator->findChild<QComboBox *>("shorthandTrigger")->isVisible());options->click();
      const auto marked=body->toPlainText();auto cursor=body->textCursor();cursor.clearSelection();body->setTextCursor(cursor);settle(30);check("field actions reflect whether content is selected",!creator->findChild<QPushButton *>("shorthandMakeField")->isEnabled()&&body->toPlainText()==marked);
creator->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();settle();}
    const auto users=Research::userCompletions();check("project snippet is saved with an internal field",users.size()==1&&!users[0].scope.isEmpty()&&users[0].body.contains("${1:a}")&&users[0].context==2);
    reset("$ ab$",4);key(e,Qt::Key_Tab);check("project snippet expands in its math context",e->toPlainText()=="$ a+b$");
    const auto otherFile=folder+"/other.tex";put(otherFile,original);auto *other=new TeXDocumentWindow(otherFile);check("project snippets do not leak to another root",Research::effectiveCompletions(other).isEmpty());const auto savedUsers=Tw::Settings().value("researchLocalSnippetsV1");auto installUsers=[&](const QJsonArray &values){Tw::Settings().setValue("researchLocalSnippetsV1",QJsonDocument(values).toJson(QJsonDocument::Compact));};QJsonObject globalRule{{"prefix","scopecheck"},{"description","Scope check"},{"body","global"},{"scope",""},{"context",0},{"trigger",1}};auto scopedRule=globalRule;scopedRule["scope"]=users[0].scope;scopedRule["body"]="project";installUsers({globalRule});check("global-only snippets work without project selection",Research::effectiveCompletions(w).size()==1&&Research::effectiveCompletions(w)[0].body=="global");installUsers({globalRule,scopedRule});check("project snippets retain precedence over global snippets",Research::effectiveCompletions(w).size()==1&&Research::effectiveCompletions(w)[0].body=="project");scopedRule["trigger"]=0;installUsers({globalRule,scopedRule});check("a disabled project override still suppresses its global rule",Research::effectiveCompletions(w).isEmpty());check("disabled project overrides do not affect another root",Research::effectiveCompletions(other).size()==1&&Research::effectiveCompletions(other)[0].body=="global");Tw::Settings().setValue("researchLocalSnippetsV1",savedUsers);other->setModified(false);other->close();
    QTimer::singleShot(100,&app,[&]{auto *d=qobject_cast<QDialog *>(QApplication::activeModalWidget());check("writing reset opens with a saved custom snippet",d);if(!d)return;auto *tabs=d->findChild<QTabWidget *>("tabWidget");tabs->setCurrentWidget(d->findChild<QWidget *>("settingsEditor"));auto *toggle=d->findChild<QToolButton *>("researchSnippetDetailsToggle");check("reset works while snippet management is collapsed",toggle&&!toggle->isChecked());auto *buttons=d->findChild<QDialogButtonBox *>("buttonBox");buttons->button(QDialogButtonBox::RestoreDefaults)->click();check("merged writing reset remains a draft",Tw::Settings().value("researchLocalSnippetsV1")==savedUsers);buttons->button(QDialogButtonBox::Ok)->click();check("merged writing reset can be saved",d->result()==QDialog::Accepted);if(d->isVisible())d->reject();});PrefsDialog::doPrefsDialog(w);check("saved writing reset retains custom snippet source and scope",Tw::Settings().value("researchLocalSnippetsV1")==savedUsers);check("saved writing reset restores completion and focus defaults",Tw::Settings().value("researchCommandCompletionMode").toInt()==0&&!Tw::Settings().value("researchFocusLineNumbers").toBool());
    reset(QString::fromUtf8(original));w->findChild<QAction *>("researchCreateCommand")->trigger();settle();QDialog *macro=nullptr;for(auto *top:QApplication::topLevelWidgets())if(top->objectName()=="researchCreateCommandDialog")macro=qobject_cast<QDialog *>(top);check("custom command has a graphical entry",macro);if(macro){macro->findChild<QLineEdit *>("researchMacroName")->setText("myresult");macro->findChild<QPlainTextEdit *>("researchMacroBody")->setPlainText("x+y");macro->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();settle();check("custom command is inserted before document body",e->toPlainText().contains("\\newcommand{\\myresult}{x+y}\n\\begin{document}"));e->undo();check("adding a command is one undo",e->toPlainText()==QString::fromUtf8(original));}
    Research::setFocusMode(false);settle(200);
    auto editShortcut=[&](bool save){QTimer::singleShot(100,&app,[&,save]{auto *d=qobject_cast<QDialog *>(QApplication::activeModalWidget());auto *tabs=d->findChild<QTabWidget *>("tabWidget");auto *inputTabs=d->findChild<QTabBar *>("researchInputTabs");check("legacy shortcut destination opens the correct internal section",tabs&&tabs->currentWidget()==d->findChild<QWidget *>("settingsShortcuts")&&inputTabs&&inputTabs->currentIndex()==1);tabs->setCurrentWidget(d->findChild<QWidget *>("settingsShortcuts"));check("shortcuts are available through the combined page",inputTabs);if(!inputTabs){d->reject();return;}inputTabs->setCurrentIndex(1);auto *table=d->findChild<QTableWidget *>("researchShortcutTable");int row=-1;for(int n=0;n<table->rowCount();++n)if(table->item(n,0)->data(Qt::UserRole).toString()=="actionSave")row=n;check("shortcut editor finds the shared save action",row>=0);if(row>=0){table->setCurrentCell(row,0);d->findChild<QKeySequenceEdit *>("researchShortcutRecorder")->setKeySequence(QKeySequence("Ctrl+Alt+9"));d->findChild<QPushButton *>("researchShortcutAssign")->click();check("shortcut assignment is accepted without a conflict",d->findChild<QLabel *>("researchShortcutConflict")->text().isEmpty());}if(save)d->findChild<QDialogButtonBox *>("buttonBox")->button(QDialogButtonBox::Ok)->click();else d->reject();});PrefsDialog::doPrefsDialog(w,5);};
    const auto old=w->findChild<QAction *>("actionSave")->shortcuts();editShortcut(false);check("cancel does not apply a shortcut draft",w->findChild<QAction *>("actionSave")->shortcuts()==old);editShortcut(true);check("save applies the shortcut to the existing action",w->findChild<QAction *>("actionSave")->shortcut()==QKeySequence("Ctrl+Alt+9"));check("translated editor choices save their original identifiers",Tw::Settings().value("autoIndent").toString()=="Whitespace only"&&Tw::Settings().value("smartQuotes").toString()=="TeX ligatures");
    QMetaObject::invokeMethod(normal,"aboutToShow");check("menu opening retains the saved shortcut",w->findChild<QAction *>("actionSave")->shortcut()==QKeySequence("Ctrl+Alt+9"));
    // Intent aliases are deliberately shared across interface languages.
    check("task words discover settings in the command palette",Research::commandSearchText(w->findChild<QAction *>("actionPreferences")).contains("改语言"));
    capture(w,"normal-source");w->resize(520,760);settle(160);capture(w,"normal-half");Research::setFocusMode(true);settle(300);capture(w,"focus-half");Research::setFocusMode(false);w->resize(1000,760);settle(160);normal->popup(w->mapToGlobal(QPoint(w->width()-220,60)));settle(250);capture(normal,"menu-source");normal->close();Research::setFocusMode(true);settle(200);capture(w,"focus-source");focus->popup(w->mapToGlobal(QPoint(w->width()-220,30)));settle(250);capture(focus,"menu-focus");focus->close();Research::setFocusMode(false);
    const auto pdfFile=folder+"/reading.pdf";{QPdfWriter writer(pdfFile);QPainter painter(&writer);painter.drawText(QRect(100,100,1000,500),"Independent PDF");}auto *pdfWindow=new PDFDocumentWindow(pdfFile);pdfWindow->resize(900,700);pdfWindow->show();settle(300);auto *pdfMenu=pdfWindow->findChild<QMenu *>("researchMoreMenu");QMetaObject::invokeMethod(pdfMenu,"aboutToShow");check("PDF uses the same menu model and top-level language",pdfMenu->actions().contains(pdfWindow->findChild<QMenu *>("researchLanguageMenu")->menuAction())&&depth(pdfMenu,1)<=3);capture(pdfWindow,"pdf-source");pdfMenu->popup(pdfWindow->mapToGlobal(QPoint(650,50)));settle(250);capture(pdfMenu,"menu-pdf");pdfMenu->close();pdfWindow->resize(520,760);settle(150);capture(pdfWindow,"pdf-half");Research::setFocusMode(true);settle(300);capture(pdfWindow,"pdf-focus-half");Research::setFocusMode(false);
    for(auto *window:QList<QMainWindow *>{w,pdfWindow}) {
      auto *help=window->findChild<QMenu *>("researchSettingsMenu");QMetaObject::invokeMethod(help,"aboutToShow");
      QStringList names;bool branded=true;for(auto *action:help->actions()){names.append(action->objectName());branded&=!action->text().contains("TeXworks",Qt::CaseInsensitive);}
      check("product help contains introduction, resource locations and TeXline About",branded&&names.contains("researchOnboardingAction")&&names.contains("actionSettings_and_Resources")&&names.contains("actionAbout_TW")&&!names.contains("actionGoToHomePage")&&!names.contains("actionWriteToMailingList"));
      help->popup(window->mapToGlobal(QPoint(100,80)));settle(260);capture(help,window==w?"help-source":"help-pdf");help->close();
    }
    bool aboutSeen=false;QTimer::singleShot(100,&app,[&]{for(auto *top:QApplication::topLevelWidgets())if(auto *box=qobject_cast<QMessageBox *>(top);box&&box->isVisible()){aboutSeen=box->text().contains("TeXline")&&box->text().contains("TeXworks")&&box->text().contains("gnu.org/licenses/gpl.html");capture(box,"about-texline");box->accept();}});
    w->findChild<QAction *>("actionAbout_TW")->trigger();check("About identifies TeXline and retains upstream licensing",aboutSeen);
    QFile disk(file);disk.open(QIODevice::ReadOnly);check("original fixture bytes are unchanged on disk",disk.readAll()==original);w->setModified(false);pdfWindow->close();app.exit(failures?1:0);
  });return app.exec();
}
