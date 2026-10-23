#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "TeXDocumentWindow.h"
#include "CompletingEdit.h"
#include "ResearchSlides.h"
#include "ResearchSlideExport.h"
#include <QPushButton>
#include "ResearchBuild.h"
#include "ResearchAppearance.h"
#include "utils/WindowManager.h"
#include <QScreen>
#include <QPainter>
#include <QToolButton>
#include "Engine.h"
#include "PDFDocumentWindow.h"
#include "Settings.h"
#include <QAction>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QLabel>
#include <QListWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QInputDialog>
#include <QDialogButtonBox>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QTextBlock>
#include <QThread>
#include <QTimer>
#include <QTreeWidget>
#include <functional>
static bool waitFor(const std::function<bool()> &ready,int ms=20000){QElapsedTimer t;t.start();while(!ready()&&t.elapsed()<ms){QCoreApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QThread::msleep(2);}return ready();}
static void settle(int ms=300){QElapsedTimer t;t.start();waitFor([&]{return t.elapsed()>=ms;},ms+1000);}
int main(int argc,char **argv){
 const QString report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
 QTimer::singleShot(80,&app,[&]{
  const QString folder=QFileInfo(report).absolutePath(),root=qEnvironmentVariable("TEXLINE_AUDIT_PROJECT")+"/main.tex";
  QJsonObject data;QJsonArray checks;auto save=[&]{QFile f(report);if(f.open(QIODevice::WriteOnly))f.write(QJsonDocument(QJsonObject{{"details",data},{"checks",checks}}).toJson());};
  auto check=[&](QString n,bool p){checks.append(QJsonObject{{"name",n},{"passed",p}});save();};
  QFile f(root);f.open(QIODevice::ReadOnly);const auto bytes=f.readAll();f.close();check("private lecture copy readable",!bytes.isEmpty());
  Tw::Settings().setValue("openPDFwithTeX",false);Tw::Settings().setValue("researchBuildDestinationPolicy","local-cache");Research::setBuildCacheForTests(folder+"/cache");
  const auto deck=Research::parseSlideDeck(root);data["frames"]=deck.frames.size();data["uncertain"]=deck.uncertain;data["reason"]=deck.reason;
  QJsonArray frames;for(const auto &s:deck.frames)frames.append(QJsonObject{{"title",s.title},{"line",s.line},{"section",s.section},{"editable",s.editable}});data["frameDetails"]=frames;save();
  auto *w=new TeXDocumentWindow(root);w->resize(1440,860);w->show();settle();
  const QString openedText=w->textDoc()->editorText();QString expectedText=QString::fromUtf8(bytes);expectedText.replace("\r\n","\n");
  check("opened text matches lecture after newline normalization",openedText==expectedText);data["initialModified"]=w->isModified();
  auto *mode=w->findChild<QAction *>("researchSlideModeAction");check("mode action exists",mode);if(!mode){app.exit(1);return;}
  check("real Beamer automatically enters slide mode",waitFor([&]{return mode->isChecked();}));
  auto *list=w->findChild<QListWidget *>("researchSlideList");auto *step=w->findChild<QSpinBox *>("researchSlideStep");auto *status=w->findChild<QLabel *>("researchSlideStatus");
  check("all literal frames listed",waitFor([&]{return list&&list->count()==deck.frames.size();}));
  check("notes, appendix and template conditionals preserve frame recognition",!deck.uncertain&&deck.frames.size()==82&&deck.frames.last().backup);
  auto visible=[&]{QString v;for(auto b=w->editor()->document()->begin();b.isValid();b=b.next())if(b.isVisible())v+=b.text()+"\n";return v;};
  data["initialStatus"]=status?status->text():QString();data["initialVisibleFullPreamble"]=visible().contains("\\documentclass");data["listFirstTitle"]=list&&list->count()?list->item(0)->text():QString();
  if(list&&list->count()>40){list->setCurrentRow(40);settle();data["selected41VisiblePreamble"]=visible().contains("\\documentclass");data["selected41VisibleFrameMarkers"]=visible().count("\\begin{frame}");}
  check("selecting slide 41 isolates its source",visible().count("\\begin{frame}")==1&&!visible().contains("\\documentclass"));
  w->grab().save(folder+"/lecture-before-build.png");save();
  app.setEngineList({Engine("XeLaTeX","C:/texlive/2023/bin/windows/xelatex.exe",{"-synctex=1","-interaction=nonstopmode","-file-line-error","$fullname"},true)});
  auto compile=[&]{w->typeset();bool stop=waitFor([&]{return !w->isTypesetting()&&!w->property("researchBuildPreparing").toBool()&&!w->property("researchBuildFinishing").toBool()&&!w->property("researchPublishing").toBool();},150000);settle(1000);return stop;};
  QElapsedTimer timer;timer.start();check("first compile completes",compile());data["firstCompileMs"]=timer.elapsed();data["previewAfterFirst"]=Research::previewFile(root);save();
  if(!Research::previewFile(root).isEmpty())check("second compile completes",compile());
  if(qEnvironmentVariableIsSet("TEXLINE_TEST_INDEPENDENT_EXPORT")) {
   Research::SlideExportRequest exportRequest;exportRequest.root=QFileInfo(root).canonicalFilePath();exportRequest.sources=Research::parseSlideDeck(root,{}).sources;exportRequest.engine=Engine("XeLaTeX","C:/texlive/2023/bin/windows/xelatex.exe",{"-synctex=1","-interaction=nonstopmode","-file-line-error","$fullname"},true);exportRequest.profiles={1,2};exportRequest.destinations={folder+"/real-projection.pdf",folder+"/real-speaker.pdf"};auto previewHash=Research::slideExportHash(Research::previewFile(root));auto exported=Research::exportSlideCopies(exportRequest,std::make_shared<std::atomic_bool>(false));check("real lecture independently exports both versions",exported.error.isEmpty()&&exported.written.size()==2);QFile exportLog(folder+"/independent-export.log");if(exportLog.open(QIODevice::WriteOnly))exportLog.write((exported.error+exported.log).toUtf8());check("real export preserves current preview and source",previewHash==Research::slideExportHash(Research::previewFile(root))&&w->textDoc()->editorText()==openedText);
  }

  settle(3200);
  const QString pdf=Research::previewFile(root);data["preview"]=pdf;data["pairValid"]=Research::pdfPairValid(pdf);data["metadata"]=Research::currentBuildMetadata(root);data["statusAfterBuild"]=status?status->text():QString();data["stepEnabledAfterBuild"]=step&&step->isEnabled();data["console"]=w->property("consoleOutput").toString();
  data["afterBuildModified"]=w->isModified();check("viewing and compilation preserve full source",w->textDoc()->editorText()==openedText&&!w->isModified());
  check("real lecture has a current mapped preview",waitFor([&]{return step&&step->isEnabled()&&status->isHidden();},25000));
  auto *view=w->findChild<QtPDF::PDFDocumentView *>("researchSlidePreview");bool allMapped=true;
  for(int i=0;view&&i<deck.frames.size();++i){list->setCurrentRow(i);settle(10);allMapped=allMapped&&int(view->currentPage())==i;}
  check("each of 82 source frames selects its physical PDF page",view&&allMapped);
  if(list)list->setCurrentRow(40);
  QString reason;bool edited=Research::editSlide(w,deck,1,Research::SlideEdit::Duplicate,&reason);data["duplicateAllowed"]=edited;data["duplicateReason"]=reason;if(edited)w->editor()->undo();check("edit probe leaves source unchanged",w->textDoc()->editorText()==openedText);
  check("safe literal frame duplication is allowed",edited);settle(1800);
  if(auto *filter=w->findChild<QLineEdit *>("researchSlideFilter")){filter->setText("41");int visible=0;for(int i=0;i<list->count();++i)visible+=!list->item(i)->isHidden();check("slide-number search isolates one row",visible==1&&!list->item(40)->isHidden());filter->clear();}
  if(auto *groups=w->findChild<QComboBox *>("researchSlideGroups")){groups->setCurrentIndex(groups->findData("backup"));int visible=0;for(int i=0;i<list->count();++i)visible+=!list->item(i)->isHidden();check("backup filter isolates 25 appendix frames",visible==25);groups->setCurrentIndex(0);}
  list->itemDoubleClicked(list->item(40));auto *title=w->findChild<QLineEdit *>("researchInlineSlideTitle");bool renamed=title;if(title){title->setText("Private title edit");title->returnPressed();}settle(750);check("title edit affects selected frame",renamed&&w->textDoc()->editorText().contains("\\begin{frame}{Private title edit}"));w->editor()->undo();settle(750);check("one undo restores title and all lecture bytes",w->textDoc()->editorText()==openedText);
  list->setCurrentRow(40);if(auto *notes=w->findChild<QAction *>("researchSlideNotes"))notes->trigger();settle(200);
  auto *pane=w->findChild<QWidget *>("researchSlideNotesPane");auto *note=pane?pane->findChild<QPlainTextEdit *>("researchSlideNote0"):nullptr;if(note)note->appendPlainText("Private notes test.");auto *apply=pane?pane->findChild<QPushButton *>("researchApplyNotes"):nullptr;if(apply)apply->click();settle(750);check("speaker notes edit reaches current frame",note&&w->textDoc()->editorText().contains("Private notes test."));w->editor()->undo();settle(750);check("one undo restores speaker notes and complete source",w->textDoc()->editorText()==openedText);if(auto *notes=w->findChild<QAction *>("researchSlideNotes"))notes->trigger();
  bool overviewOpened=false;QTimer::singleShot(120,&app,[&]{auto *dialog=w->findChild<QDialog *>("researchSlideOverview");if(!dialog)return;overviewOpened=true;auto *grid=dialog->findChild<QListWidget *>("researchOverviewGrid");check("overview lists all 82 slides",grid&&grid->count()==82);check("overview renders only visible thumbnails",waitFor([&]{return dialog->property("researchOverviewRendered").toInt()>0;},10000)&&dialog->property("researchOverviewRendered").toInt()<=24);auto *overviewGroups=dialog->findChild<QComboBox *>("researchOverviewGroups");overviewGroups->setCurrentIndex(overviewGroups->findData("backup"));int backupVisible=0;for(int i=0;i<grid->count();++i)backupVisible+=!grid->item(i)->isHidden();check("overview filters 25 backup slides",backupVisible==25);overviewGroups->setCurrentIndex(0);
   bool browsed=true;for(int i=0;i<grid->count();i+=8){grid->scrollToItem(grid->item(i),QAbstractItemView::PositionAtTop);browsed&=waitFor([&]{return grid->item(i)->data(Qt::UserRole+2).toBool();},15000);browsed&=dialog->property("researchOverviewRendered").toInt()<=24;}grid->scrollToItem(grid->item(81));browsed&=waitFor([&]{return grid->item(81)->data(Qt::UserRole+2).toBool();},15000);check("overview browses the entire real lecture with bounded image cache",browsed);grid->scrollToItem(grid->item(0));check("evicted overview images render again when returning to the beginning",waitFor([&]{return grid->item(0)->data(Qt::UserRole+2).toBool();},15000));settle(1000);dialog->grab().save(folder+"/lecture-overview.png");auto *search=dialog->findChild<QLineEdit *>("researchOverviewSearch");search->setText("41");int visibleRows=0;for(int i=0;i<grid->count();++i)visibleRows+=!grid->item(i)->isHidden();check("overview search identifies slide 41",visibleRows==1&&!grid->item(40)->isHidden());grid->itemActivated(grid->item(40));});w->findChild<QToolButton *>("researchSlideOverviewButton")->click();check("overview selection returns to the selected source",waitFor([&]{return overviewOpened&&list->currentRow()==40;},120000)&&list->currentRow()==40&&visible().count("\\begin{frame}")==1);if(auto *dialog=w->findChild<QDialog *>("researchSlideOverview"))dialog->close();
  // Update after reversible edit probes, so final screenshots show a current PDF.
  check("preview returns to current state after edit undo and compile",compile()&&waitFor([&]{return status->isHidden();},25000));settle(4800);
  data["devicePixelRatio"]=w->devicePixelRatioF();
  auto *noteButton=w->findChild<QToolButton *>("researchSlideNotesButton");auto *addButton=w->findChild<QToolButton *>("researchSlideAddButton");check("notes caption stays compact when action state changes",noteButton&&noteButton->text()==(Research::englishUi()?QStringLiteral("Notes"):QStringLiteral("备注"))&&noteButton->fontMetrics().horizontalAdvance(noteButton->text())<noteButton->width()-12);check("new slide split menu uses the themed arrow and hit area",addButton&&addButton->property("researchButtonRole")=="menu");
  if(auto *closeSearch=w->findChild<QToolButton *>("researchCloseSlideSearch"))closeSearch->click();settle(500);
  w->grab().save(folder+"/lecture-after-build.png");
  Research::setFocusMode(true);settle(700);check("focus mode shows complete code without slide panels",!w->findChild<QDockWidget *>("researchSlidePreviewDock")->isVisible()&&!w->property("researchSlideMode").toBool()&&visible().contains("\\documentclass")&&visible().count("\\begin{frame}")==82);w->grab().save(folder+"/lecture-focus.png");if(auto *pdf=w->pdfDocument()){pdf->show();Tw::Utils::WindowManager::sideBySide(w,pdf);settle(700);const auto left=w->geometry(),right=pdf->geometry();check("focused source and PDF fit left and right halves",!w->isMaximized()&&!pdf->isMaximized()&&qAbs(left.width()-right.width())<30&&left.center().x()<right.center().x());auto a=w->grab(),b=pdf->grab();QPixmap pair(a.width()+b.width(),qMax(a.height(),b.height()));pair.fill(Qt::white);{QPainter painter(&pair);painter.drawPixmap(0,0,a);painter.drawPixmap(a.width(),0,b);}pair.save(folder+"/lecture-focus-pair.png");pdf->hide();}Research::setFocusMode(false);settle(700);

  w->resize(560,700);settle();data["narrowDirectoryVisible"]=w->findChild<QDockWidget *>("researchSlideDock")->isVisible();w->resize(1440,860);settle();data["wideDirectoryRestored"]=w->findChild<QDockWidget *>("researchSlideDock")->isVisible();
  check("responsive directory returns when width is restored",data["wideDirectoryRestored"].toBool());
  // Probe the conservative parser with independent tiny strings, never lecture edits.
  QJsonArray probes;for(QString extra:{QString(),QString("\\appendix\n"),QString("\\setbeameroption{hide notes}\n"),QString("\\newif\\ifbackup\n")}){QHash<QString,QString> b;b.insert(QFileInfo(root).canonicalFilePath(),"\\documentclass{beamer}\n"+extra+"\\begin{document}\n\\begin{frame}{Example}Body\\end{frame}\n\\end{document}\n");const auto d=Research::parseSlideDeck(root,b);probes.append(QJsonObject{{"extra",extra},{"uncertain",d.uncertain},{"reason",d.reason}});}data["parserProbes"]=probes;
  save();w->setModified(false);int failures=0;for(const auto &c:checks)failures+=!c.toObject()["passed"].toBool();app.exit(failures?1:0);
 });return app.exec();
}
