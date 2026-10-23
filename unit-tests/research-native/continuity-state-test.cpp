// Continuity regressions in actual widgets, using only an inactive private desktop.
#include "ActualAuthoringRecording.h"
#include "ResearchProject.h"
#include "ResearchSlides.h"
#include "ResearchBibliography.h"
#include "Engine.h"
#include <QDynamicPropertyChangeEvent>
#include <QInputMethodEvent>
using namespace Research;

class NextSlideParse final:public QObject{
public:
 std::function<void()> action;
 bool eventFilter(QObject *,QEvent *event)override{
  if(action&&event->type()==QEvent::DynamicPropertyChange&&static_cast<QDynamicPropertyChangeEvent*>(event)->propertyName()=="researchSlideParses"){auto run=action;action={};run();}
  return false;
 }
};

int main(int argc,char **argv){
 const auto report=argument(1);if(!qEnvironmentVariable("TEXLINE_TEST_DESKTOP").startsWith("TeXlineValidation-"))return 2;
 argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
 QTimer::singleShot(60,&app,[&]{
  const auto folder=QFileInfo(report).absolutePath();QJsonArray checks;int failures=0;
  auto check=[&](const QString &name,bool pass){checks.append(QJsonObject{{"name",name},{"passed",pass}});failures+=!pass;put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"physicalInput",false},{"devicePixelRatio",qApp->primaryScreen()->devicePixelRatio()}}).toJson());};
  QString phaseName;QTimer watchdog;watchdog.setInterval(10000);
  auto phase=[&](const QString &name){phaseName=name;put(folder+"/phase.json",QJsonDocument(QJsonObject{{"phase",name},{"at",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}}).toJson());watchdog.start();};
  QObject::connect(&watchdog,&QTimer::timeout,&app,[&]{QJsonArray windows;for(auto *widget:QApplication::topLevelWidgets())if(widget->isVisible())windows.append(QJsonObject{{"class",widget->metaObject()->className()},{"object",widget->objectName()},{"title",widget->windowTitle()}});put(folder+"/watchdog.json",QJsonDocument(QJsonObject{{"phase",phaseName},{"windows",windows}}).toJson());if(auto *modal=qobject_cast<QDialog*>(QApplication::activeModalWidget())){modal->grab().save(folder+"/unexpected-modal.png");check("no unexpected modal during "+phaseName,false);modal->reject();}});
  phase("create article sample");
  Tw::Settings().setValue("openPDFwithTeX",false);
  const auto file=folder+"/paper/main.tex",child=folder+"/paper/child.tex";
  QString text="\\documentclass{article}\n\\newtheorem{theorem}{Theorem}\n\\begin{document}\n\\section{Continuity}\n\\begin{theorem}\\label{thm:main}Initial theorem statement.\\end{theorem}\n\\input{child}\n";
  for(int n=0;n<1800;++n){if(n==1000)text+="\\section{Later analysis}\n";text+=QString("Paragraph %1 研究写作 continuity and reading position. ").arg(n)+QString(100,'x')+"\n";}
  text+="\\end{document}\n";
  const QString childText="% !TEX root = main.tex\nChild citation \\ref{thm:main}.\n";
  check("private sample created",put(file,text.toUtf8())&&put(child,childText.toUtf8()));
  auto *w=new TeXDocumentWindow(file);w->resize(qEnvironmentVariableIsSet("TEXLINE_TEST_HALF")?620:1040,760);w->show();QApplication::setActiveWindow(w);w->editor()->setFocus();settle(700);
  check("two-section article supplies the actual structure panel",waitFor([&]{return w->property("researchArticleMode").toBool()&&w->property("researchArticleReady").toBool();}));phase("context navigation");
  auto action=[&](const char *name){auto *entry=w->findChild<QAction*>(name);check(QString("command exists: ")+name,entry);if(entry)entry->trigger();};
  auto select=[&](int a,int b){QTextCursor c(w->textDoc());c.setPosition(a);c.setPosition(b,QTextCursor::KeepAnchor);w->editor()->setTextCursor(c);};
  select(text.indexOf("Initial theorem"),text.indexOf("Initial theorem"));action("researchRelatedAction");
  QPointer<QDialog> dialog=w->findChild<QDialog*>("researchRelatedReading");
  auto *reading=dialog?dialog->findChild<QTextBrowser*>("researchRelatedContents"):nullptr;
  auto *backCommand=w->findChild<QAction*>("researchNavigateBack"),*forwardCommand=w->findChild<QAction*>("researchNavigateForward");
  const auto backName=backCommand?backCommand->text():QString(),forwardName=forwardCommand?forwardCommand->text():QString();
  auto navigationIntact=[&]{
   if(!dialog)return false;
   for(const bool back:{true,false}){auto *button=dialog->findChild<QToolButton*>(back?"researchReadingBack":"researchReadingForward");auto *command=w->findChild<QAction*>(back?"researchNavigateBack":"researchNavigateForward");const auto name=back?backName:forwardName;
    if(!button||!command||button->defaultAction()!=command||button->isEnabled()!=command->isEnabled()||button->toolButtonStyle()!=Qt::ToolButtonIconOnly||button->icon().pixmap(button->iconSize()).isNull()||button->accessibleName()!=name||command->text()!=name)return false;
   }return true;
  };
  check("theorem context initially loads",reading&&waitFor([&]{return reading->toPlainText().contains("Initial theorem statement");}));
  if(reading){
   check("reading navigation has stable SVG icons and original commands",navigationIntact());
   dialog->grab().save(folder+"/context-before.png");
   QMetaObject::invokeMethod(reading,"anchorClicked",Q_ARG(QUrl,QUrl("source:0")));settle();
   const auto at=w->textDoc()->editorText().indexOf("Initial theorem statement");QTextCursor edit(w->textDoc());edit.setPosition(at);edit.setPosition(at+25,QTextCursor::KeepAnchor);edit.insertText("Revised theorem statement");
   action("researchNavigateBack");
   check("Back reloads changed theorem instead of old index",dialog&&waitFor([&]{return dialog->isVisible()&&reading->toPlainText().contains("Revised theorem statement");}));
   check("history updates retain both navigation icons and action states",navigationIntact());
   if(dialog){
    dialog->grab().save(folder+"/context-after.png");QUrl childLink;
    for(auto block=reading->document()->begin();block.isValid();block=block.next())for(auto fragment=block.begin();!fragment.atEnd();++fragment)if(fragment.fragment().text()=="child.tex"&&fragment.fragment().charFormat().isAnchor())childLink=QUrl(fragment.fragment().charFormat().anchorHref());
    check("incoming reference has a child source link",!childLink.isEmpty());
    if(!childLink.isEmpty()){
     QMetaObject::invokeMethod(reading,"anchorClicked",Q_ARG(QUrl,childLink));settle();auto *cw=TeXDocumentWindow::findDocument(child);check("incoming source opens child",cw);
     if(cw){QTextCursor cross(cw->textDoc());cross.setPosition(childText.indexOf("Child citation"));cross.insertText("Updated cross-file ");cw->findChild<QAction*>("researchNavigateBack")->trigger();check("cross-file Back refreshes reference context",waitFor([&]{return dialog&&dialog->isVisible()&&reading->toPlainText().contains("Updated cross-file");}));}
    }
    key(dialog,Qt::Key_Escape);
   }settle();
  }
  // Closed sources can be replaced externally without a metadata change.
  phase("closed child replacement with preserved metadata");
  if(auto *cw=TeXDocumentWindow::findDocument(child)){cw->textDoc()->setModified(false);cw->close();settle();}
  select(w->textDoc()->editorText().indexOf("Revised theorem"),w->textDoc()->editorText().indexOf("Revised theorem"));action("researchRelatedAction");dialog=w->findChild<QDialog*>("researchRelatedReading");reading=dialog?dialog->findChild<QTextBrowser*>("researchRelatedContents"):nullptr;
  const auto childTime=QFileInfo(child).lastModified();const auto childSize=QFileInfo(child).size();
  auto replaceChild=[&](const QString &content){if(!put(child,content.toUtf8()))return false;QFile changed(child);return changed.open(QIODevice::ReadWrite)&&changed.setFileTime(childTime,QFileDevice::FileModificationTime);};
  check("closed child context initially loads",!TeXDocumentWindow::findDocument(child)&&reading&&waitFor([&]{return reading->toPlainText().contains("Child citation");}));
  if(reading){
   QMetaObject::invokeMethod(reading,"anchorClicked",Q_ARG(QUrl,QUrl("source:0")));settle();
   QString replacement=childText;replacement.replace("Child citation","Other citation");
   check("external edit preserves child size and timestamp",replaceChild(replacement)&&QFileInfo(child).size()==childSize&&QFileInfo(child).lastModified()==childTime);
   action("researchNavigateBack");
   check("Back does not flash cached closed-file context",!reading->toPlainText().contains("Child citation"));
   check("Back verifies closed-file content beyond metadata",waitFor([&]{return dialog&&dialog->isVisible()&&reading->toPlainText().contains("Other citation");}));
  }
  check("external child fixture restores original data",replaceChild(childText));
  if(dialog)key(dialog,Qt::Key_Escape);settle();
  select(w->textDoc()->editorText().indexOf("Revised theorem"),w->textDoc()->editorText().indexOf("Revised theorem"));action("researchRelatedAction");dialog=w->findChild<QDialog*>("researchRelatedReading");reading=dialog?dialog->findChild<QTextBrowser*>("researchRelatedContents"):nullptr;
  {QTextCursor edit(w->textDoc());const int at=w->textDoc()->editorText().indexOf("Revised theorem statement");edit.setPosition(at);edit.setPosition(at+25,QTextCursor::KeepAnchor);edit.insertText("Latest asynchronous theorem statement");}
  const bool latestContext=reading&&waitFor([&]{return reading->toPlainText().contains("Latest asynchronous theorem statement");});
  check("edit during context load retains the object and latest content",latestContext);
  if(dialog)dialog->grab().save(folder+"/context-load-edit.png");
  if(!latestContext)put(folder+"/context-load-edit.json",QJsonDocument(QJsonObject{{"reading",reading?reading->toPlainText().left(4000):QString()},{"state",QJsonObject::fromVariantMap(referenceReadingState(w))},{"sourceContainsLatest",w->textDoc()->editorText().contains("Latest asynchronous theorem statement")}}).toJson());
  if(dialog)key(dialog,Qt::Key_Escape);settle();action("researchRelatedAction");dialog=w->findChild<QDialog*>("researchRelatedReading");if(dialog)key(dialog,Qt::Key_Escape);settle(700);check("Escape during loading never reopens context",!w->findChild<QDialog*>("researchRelatedReading"));
  // A missing bibliography must not prevent reading known article relations.
  phase("partial bibliography and recovery");
  const auto missingBib=folder+"/paper/not-yet-linked.bib";check("private missing-bibliography fixture starts absent",!QFileInfo::exists(missingBib)||QFile::remove(missingBib));
  {QTextCursor append(w->textDoc());append.movePosition(QTextCursor::End);append.insertText("\n\\bibliography{not-yet-linked}\n");}
  select(w->textDoc()->editorText().indexOf("Latest asynchronous theorem"),w->textDoc()->editorText().indexOf("Latest asynchronous theorem"));action("researchRelatedAction");dialog=w->findChild<QDialog*>("researchRelatedReading");reading=dialog?dialog->findChild<QTextBrowser*>("researchRelatedContents"):nullptr;
  check("missing bibliography preserves theorem and known references",reading&&waitFor([&]{return reading->toPlainText().contains("Latest asynchronous theorem statement")&&reading->toPlainText().contains("Child citation");}));
  check("missing bibliography shows the file cause without size claim",reading&&reading->toPlainText().contains("not-yet-linked")&&!reading->toPlainText().contains("项目过大")&&!reading->toPlainText().contains("Project too large"));
  if(reading){
   QMetaObject::invokeMethod(reading,"anchorClicked",Q_ARG(QUrl,QUrl("source:0")));settle();
   check("missing bibliography can be supplied",put(missingBib,"@article{recovered,title={Recovered reference},author={A. Writer},year={2026}}\n"));
   action("researchNavigateBack");
   check("Back refreshes a newly supplied bibliography",waitFor([&]{return dialog&&dialog->isVisible()&&reading->toPlainText().contains("Latest asynchronous theorem statement")&&!reading->toPlainText().contains("not-yet-linked");}));
   if(dialog)if(auto *query=dialog->findChild<QLineEdit*>()){query->setText("Recovered reference");check("recovered bibliography is immediately searchable",reading->toPlainText().contains("Recovered reference"));}
  }
  if(dialog)key(dialog,Qt::Key_Escape);settle();
  phase("malformed bibliography diagnostic");
  const QString malformed="@article{recovered,title={Recovered reference},author={A. Writer},year={2026}}\n@article{broken,title={Unfinished\n";
  const auto parseError=parseBibliography(malformed).error;check("private malformed bibliography prepared",!parseError.isEmpty()&&put(missingBib,malformed.toUtf8()));
  select(w->textDoc()->editorText().indexOf("Latest asynchronous theorem"),w->textDoc()->editorText().indexOf("Latest asynchronous theorem"));action("researchRelatedAction");dialog=w->findChild<QDialog*>("researchRelatedReading");reading=dialog?dialog->findChild<QTextBrowser*>("researchRelatedContents"):nullptr;
  check("malformed bibliography retains context and reports actual parse cause",reading&&waitFor([&]{const auto plain=reading->toPlainText();return plain.contains("Latest asynchronous theorem statement")&&plain.contains("Child citation")&&plain.contains("not-yet-linked.bib")&&plain.contains(parseError)&&!plain.contains("项目过大")&&!plain.contains("Project too large");}));
  if(reading){QMetaObject::invokeMethod(reading,"anchorClicked",Q_ARG(QUrl,QUrl("cite:recovered")));check("valid entries preceding malformed data remain readable",waitFor([&]{return reading->toPlainText().contains("Recovered reference")&&reading->toPlainText().contains(parseError);}));}
  if(dialog)key(dialog,Qt::Key_Escape);settle();
  // A live reading anchor must move with inserted text, without changing selection direction.
  phase("reading position after prefix insertion");
  const int at=w->textDoc()->editorText().indexOf("Paragraph 800 ");select(at+24,at);w->editor()->ensureCursorVisible();settle();
  w->editor()->verticalScrollBar()->setValue(w->editor()->verticalScrollBar()->value()+w->editor()->cursorRect().top()-180);settle();
  QTextCursor expectedCursor=w->textCursor(),top=w->editor()->cursorForPosition(QPoint(0,0));const int topY=w->editor()->cursorRect(top).top();
  rememberReadingPlace(w);select(0,0);QTextCursor prefix(w->textDoc());prefix.setPosition(0);prefix.insertText(QString("% Added before saved reading place.\n").repeated(180));action("researchNavigateBack");settle(700);
  check("Back retains moved reverse selection",w->textCursor().position()==expectedCursor.position()&&w->textCursor().anchor()==expectedCursor.anchor());
  check("Back retains reading line after prefix insertion",qAbs(w->editor()->cursorRect(top).top()-topY)<=2);
  w->grab().save(folder+"/reading-return.png");
  // Saved places restore the final layout, then the reading anchor.
  phase("capture writing place");
  auto saved=captureWritingPlace(w);const auto savedTop=w->editor()->cursorForPosition(QPoint(0,0));const int savedY=w->editor()->cursorRect(savedTop).top();
  phase("change article layout");if(w->property("researchArticleMode").toBool())if(auto *outline=w->findChild<QAction*>("researchOutlineAction"))outline->trigger();w->resize(qEnvironmentVariableIsSet("TEXLINE_TEST_HALF")?600:700,690);settle(500);QString error;phase("restore writing place");
  check("writing place restores after layout changes",restoreWritingPlace(saved,&error));settle(700);
  check("writing place retains reading line after layout restoration",qAbs(w->editor()->cursorRect(savedTop).top()-savedY)<=2);
  // Delay the existing article parser through a real source edit, then navigate before it completes.
  phase("input during pending article restore");
  QTextCursor changed(w->textDoc());changed.setPosition(0);changed.insertText("% Index update\n");
  check("writing place restore queues current index work",restoreWritingPlace(saved,&error));
  key(w->editor(),Qt::Key_PageDown);const auto manual=w->editor()->cursorForPosition(QPoint(0,0));const int manualY=w->editor()->cursorRect(manual).top();settle(1000);
  check("manual navigation cancels pending restored viewport",qAbs(w->editor()->cursorRect(manual).top()-manualY)<=2);
  check("manual navigation clears pending article restoration",!w->property("researchRestoreArticlePlace").isValid());
  for(const bool ime:{false,true}){
   const QString name=ime?"synthetic IME commit":"typed text";phase(name+" during pending article restore");
   const auto inputPlace=captureWritingPlace(w);QTextCursor edit(w->textDoc());edit.setPosition(0);edit.insertText("% Input cancellation\n");
   check(name+" has an actual pending restoration",restoreWritingPlace(inputPlace,&error)&&w->property("researchRestoreArticlePlace").isValid());
   const QString inserted=ime?QStringLiteral("中文"):QStringLiteral("z");const auto before=w->textCursor();QString expected=w->textDoc()->editorText();expected.replace(before.selectionStart(),before.selectionEnd()-before.selectionStart(),inserted);
   if(ime){QInputMethodEvent preedit(QStringLiteral("中"),{});QApplication::sendEvent(w->editor(),&preedit);QInputMethodEvent commit;commit.setCommitString(inserted);QApplication::sendEvent(w->editor(),&commit);}else key(w->editor(),Qt::Key_Z,Qt::NoModifier,inserted);
   const auto typed=w->textCursor(),view=w->editor()->cursorForPosition(QPoint(0,0));const int viewY=w->editor()->cursorRect(view).top();const bool cleared=!w->property("researchRestoreArticlePlace").isValid();
   const bool ready=waitFor([&]{return w->property("researchArticleReady").toBool();});settle(80);
   check(name+" cancels delayed restoration without losing input or focus",cleared&&ready&&w->textDoc()->editorText()==expected&&w->textCursor().position()==typed.position()&&w->textCursor().anchor()==typed.anchor()&&w->editor()->hasFocus()&&qAbs(w->editor()->cursorRect(view).top()-viewY)<=2);
  }
  // Reading mode belongs to the saved scene, even when explicitly disabled afterwards.
  phase("slide mode and preview step restoration");
  const auto slidesFile=folder+"/paper/talk.tex";const QString slides="\\documentclass{beamer}\n\\begin{document}\n\\begin{frame}{One}One.\\pause Two.\\end{frame}\n\\begin{frame}{Two}Second.\\end{frame}\n\\end{document}\n";
  put(slidesFile,slides.toUtf8());auto *sw=new TeXDocumentWindow(slidesFile);sw->resize(w->size());sw->show();settle(700);auto *mode=sw->findChild<QAction*>("researchSlideModeAction");if(mode&&!mode->isChecked())mode->setChecked(true);
  check("slide scene starts in slide mode",waitFor([&]{return slideReadingState(sw).value("enabled").toBool()&&sw->findChild<QListWidget*>("researchSlideList");}));
  settle(700);setBuildCacheForTests(folder+"/compile-cache");Tw::Settings().setValue("researchBuildDestinationPolicy","local-cache");app.setEngineList({Engine("pdfLaTeX","C:/texlive/2023/bin/windows/pdflatex.exe",{"-synctex=1","-interaction=nonstopmode","-file-line-error","$fullname"},true)});
  phase("compile private slide sample");sw->typeset();check("private slide sample compiles",waitFor([&]{return !sw->isTypesetting()&&!sw->property("researchBuildPreparing").toBool()&&!sw->property("researchBuildFinishing").toBool()&&!sw->property("researchPublishing").toBool();},90000)&&pdfPairValid(previewFile(slidesFile)));phase("restore compiled slide step");
  auto *step=sw->findChild<QSpinBox*>("researchSlideStep");check("compiled slide has two preview steps",step&&waitFor([&]{return step->maximum()==2;}));if(step&&step->maximum()==2)step->setValue(2);
  QTextCursor slideCursor(sw->textDoc());slideCursor.setPosition(slides.indexOf("One."));sw->editor()->setTextCursor(slideCursor);
  auto slideState=captureWritingPlace(sw);check("saved slide state includes preview step",slideState.value("slides").toMap().value("step").toInt()==2);
  if(mode)mode->setChecked(false);check("saved scene reenables slide mode",restoreWritingPlace(slideState,&error)&&waitFor([&]{return mode&&mode->isChecked()&&slideReadingState(sw).value("enabled").toBool();}));
  check("saved scene restores the preview step",step&&waitFor([&]{return step->value()==2&&!sw->property("researchRestoreSlidePlace").isValid();}));
  phase("user navigation during asynchronous slide restoration");
  auto *slideList=sw->findChild<QListWidget*>("researchSlideList");NextSlideParse nextParse;sw->installEventFilter(&nextParse);bool intercepted=false,queued=false,cancelled=false;QPointer<QWidget> manualFocus;int appliedRows=0;
  QMetaObject::Connection rows;if(slideList)rows=QObject::connect(slideList->model(),&QAbstractItemModel::rowsInserted,&app,[&]{++appliedRows;});
  // This existing counter changes after busy is set, before the worker starts;
  // inject once at that boundary instead of depending on worker timing.
  nextParse.action=[&]{intercepted=true;queued=restoreWritingPlace(slideState,&error)&&sw->property("researchRestoreSlidePlace").isValid();if(slideList){slideList->setFocus();key(slideList,Qt::Key_Down);}cancelled=!sw->property("researchRestoreSlidePlace").isValid();manualFocus=QApplication::focusWidget();};
  {QTextCursor edit(sw->textDoc());edit.setPosition(0);edit.insertText("% Pending slide update\n");}
  const bool applied=waitFor([&]{return intercepted&&appliedRows>0;});settle(80);sw->removeEventFilter(&nextParse);QObject::disconnect(rows);
  check("slide navigation cancels pending restore and survives parser completion",queued&&cancelled&&applied&&slideList&&slideList->currentRow()==1&&slideReadingState(sw).value("selected").toInt()==1&&QApplication::focusWidget()==manualFocus);
  phase("clamp saved preview step after recompile");
  auto *slidePreview=sw->findChild<QtPDF::PDFDocumentView*>("researchSlidePreview");auto *slideStatus=sw->findChild<QLabel*>("researchSlideStatus");QJsonArray slideTrace;QElapsedTimer slideClock;slideClock.start();
  auto traceSlide=[&](const QString &stage){auto document=slidePreview?slidePreview->document().toStrongRef():QSharedPointer<QtPDF::Backend::Document>();slideTrace.append(QJsonObject{{"stage",stage},{"elapsedMs",slideClock.elapsed()},{"state",QJsonObject::fromVariantMap(slideReadingState(sw))},{"pending",QJsonObject::fromVariantMap(sw->property("researchRestoreSlidePlace").toMap())},{"parseCount",sw->property("researchSlideParses").toInt()},{"cursor",sw->textCursor().position()},{"anchor",sw->textCursor().anchor()},{"row",slideList?slideList->currentRow():-1},{"step",step?step->value():-1},{"maximum",step?step->maximum():-1},{"stepEnabled",step&&step->isEnabled()},{"previewPages",document?int(document->numPages()):0},{"previewPage",slidePreview?int(slidePreview->currentPage()):-1},{"status",slideStatus?slideStatus->text():QString()},{"width",sw->width()},{"dpr",sw->devicePixelRatioF()}});put(folder+"/slide-restore-state.json",QJsonDocument(QJsonObject{{"saved",QJsonObject::fromVariantMap(slideState.value("slides").toMap())},{"stages",slideTrace}}).toJson());};
  traceSlide("before one-step edit");
  QString oneStep=slides;oneStep.remove("\\pause");{QTextCursor edit(sw->textDoc());edit.select(QTextCursor::Document);edit.insertText(oneStep);}
  sw->typeset();check("one-step slide sample recompiles",waitFor([&]{return !sw->isTypesetting()&&!sw->property("researchBuildPreparing").toBool()&&!sw->property("researchBuildFinishing").toBool()&&!sw->property("researchPublishing").toBool();},90000)&&pdfPairValid(previewFile(slidesFile)));
  traceSlide("compile finished");
  // The previous PDF's second slide already has one step. Wait for the new
  // two-page document and completed mapping, not just that old spin-box value.
  const bool newPreview=slidePreview&&slideStatus&&waitFor([&]{auto document=slidePreview->document().toStrongRef();return document&&document->numPages()==2&&slideStatus->text().isEmpty();});
  check("one-step compile reaches the current mapped slide preview",newPreview);
  if(slideList)slideList->setCurrentRow(0);const bool onePage=step&&waitFor([&]{return slideList&&slideList->currentRow()==0&&step->isEnabled()&&step->maximum()==1;});traceSlide("first slide mapping ready");
  {QTextCursor first(sw->textDoc());first.setPosition(oneStep.indexOf("One."));sw->editor()->setTextCursor(first);}
  restoreSlideReadingState(sw,slideState.value("slides").toMap());traceSlide("restore requested");
  const bool clamped=waitFor([&]{return step&&step->value()==1&&step->maximum()==1&&slideReadingState(sw).value("step").toInt()==1&&!sw->property("researchRestoreSlidePlace").isValid();});traceSlide("restore completed or timed out");
  check("saved second step clamps to a newly compiled single step",newPreview&&onePage&&clamped);
  phase("expanded slide preview and return");
  if(qEnvironmentVariableIsSet("TEXLINE_TEST_HALF")){sw->resize(600,sw->height());settle(150);check("expanded-preview fixture uses a 600px writing window",sw->width()==600);}
  QApplication::setActiveWindow(sw);sw->editor()->setFocus();QTextCursor writingCursor(sw->textDoc());writingCursor.setPosition(oneStep.indexOf("One.")+4);writingCursor.setPosition(oneStep.indexOf("One."),QTextCursor::KeepAnchor);sw->editor()->setTextCursor(writingCursor);settle(100);
  auto *expand=sw->findChild<QAction*>("researchSlideExpandPreview");auto *expandButton=sw->findChild<QToolButton*>("researchSlideExpandPreviewButton");auto *directory=sw->findChild<QDockWidget*>("researchSlideDock"),*previewDock=sw->findChild<QDockWidget*>("researchSlidePreviewDock");
  check("expanded preview has its existing SVG command",expand&&expandButton&&!expandButton->icon().pixmap(expandButton->iconSize()).isNull()&&directory&&previewDock&&slidePreview);
  if(expand&&directory&&previewDock&&slidePreview){
   const auto writingSize=sw->size();const auto writingArea=sw->centralWidget()->geometry();const bool directoryShown=directory->isVisible(),previewShown=previewDock->isVisible();const int writingScroll=sw->editor()->verticalScrollBar()->value(),writingPage=int(slidePreview->currentPage()),writingStep=step?step->value():-1;sw->grab().save(folder+"/slides-writing.png");
   expand->trigger();const bool expanded=waitFor([&]{return sw->property("researchSlidePreviewExpanded").toBool()&&sw->centralWidget()->isHidden()&&directory->isVisible()&&previewDock->isVisible();});settle(250);
   check("expanded preview fits the existing writing window with its slide list",expanded&&sw->size()==writingSize&&previewDock->width()>=sw->width()-directory->width()-32&&slidePreview->hasFocus());
   sw->grab().save(folder+"/slides-expanded.png");traceSlide("preview expanded");key(slidePreview,Qt::Key_Escape);
   check("Escape restores slide layout selection reading position and focus",waitFor([&]{return !sw->property("researchSlidePreviewExpanded").toBool()&&!sw->centralWidget()->isHidden()&&sw->size()==writingSize&&sw->centralWidget()->geometry()==writingArea&&directory->isVisible()==directoryShown&&previewDock->isVisible()==previewShown&&sw->textDoc()->editorText()==oneStep&&sw->textCursor().position()==writingCursor.position()&&sw->textCursor().anchor()==writingCursor.anchor()&&sw->editor()->verticalScrollBar()->value()==writingScroll&&int(slidePreview->currentPage())==writingPage&&step&&step->value()==writingStep&&sw->editor()->hasFocus();}));
   sw->grab().save(folder+"/slides-restored.png");traceSlide("preview restored");
  }
  {QTextCursor restore(sw->textDoc());restore.select(QTextCursor::Document);restore.insertText(slides);}check("private slide sample restored after recompile",sw->save());
  check("private source files on disk unchanged",bytes(file)==text.toUtf8()&&bytes(child)==childText.toUtf8()&&bytes(slidesFile)==slides.toUtf8());
  phase("close private samples");for(auto *source:TeXDocumentWindow::documentList())source->textDoc()->setModified(false);for(auto *source:TeXDocumentWindow::documentList())source->close();settle();watchdog.stop();app.exit(failures?1:0);
 });return app.exec();
}
