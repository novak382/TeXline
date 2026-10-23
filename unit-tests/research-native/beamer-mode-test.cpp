#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "TeXDocumentWindow.h"
#include "PDFDocumentWindow.h"
#include "CompletingEdit.h"
#include "Engine.h"
#include "ResearchSlides.h"
#include "ResearchBuild.h"
#include "ResearchAppearance.h"
#include "Settings.h"
#include <QAction>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QListWidget>
#include <QSaveFile>
#include <QSpinBox>
#include <QStatusBar>
#include <QGraphicsOpacityEffect>
#include <QTextBlock>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QKeyEvent>
#include <functional>
#include <windows.h>
static QString crashPath;
static LONG WINAPI recordCrash(EXCEPTION_POINTERS *exception) {
  CONTEXT context=*exception->ContextRecord;QJsonArray frames;
  for(int i=0;i<30&&context.Rip;++i) {
    const auto base=reinterpret_cast<DWORD64>(GetModuleHandleW(nullptr));frames.append(QString::number(context.Rip-base+0x140000000ULL,16));
    DWORD64 image=0;auto *entry=RtlLookupFunctionEntry(context.Rip,&image,nullptr);
    if(!entry)break;PVOID data=nullptr;DWORD64 establisher=0;KNONVOLATILE_CONTEXT_POINTERS pointers{};
    RtlVirtualUnwind(UNW_FLAG_NHANDLER,image,context.Rip,entry,&context,&data,&establisher,&pointers);
  }
  QFile f(crashPath);if(f.open(QIODevice::WriteOnly))f.write(QJsonDocument(QJsonObject{{"stack",frames},{"code",double(exception->ExceptionRecord->ExceptionCode)}}).toJson());
  return EXCEPTION_EXECUTE_HANDLER;
}

static bool put(const QString &path,const QByteArray &bytes) {QDir().mkpath(QFileInfo(path).absolutePath());QSaveFile f(path);return f.open(QIODevice::WriteOnly)&&f.write(bytes)==bytes.size()&&f.commit();}
static QByteArray read(const QString &path){QFile f(path);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray();}
static bool waitFor(const std::function<bool()> &ready,int ms=20000) {QElapsedTimer t;t.start();while(!ready()&&t.elapsed()<ms){QCoreApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QThread::msleep(2);}return ready();}
static void settle(int ms=160){QElapsedTimer t;t.start();waitFor([&]{return t.elapsed()>=ms;},ms+1000);}
int main(int argc,char **argv) {
  const QString report=QString::fromLocal8Bit(argv[1]);crashPath=report+".crash.json";SetUnhandledExceptionFilter(recordCrash);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
  QTimer::singleShot(80,&app,[&]{
    const QString folder=QFileInfo(report).absolutePath(),root=folder+"/project/main.tex",child=folder+"/project/parts/chapter.tex";
    QJsonArray checks;QJsonObject details;int failures=0;
    auto save=[&]{put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"details",details},{"physicalInputVerified",false}}).toJson());};
    auto check=[&](const QString &name,bool passed){checks.append(QJsonObject{{"name",name},{"passed",passed}});failures+=!passed;save();};
    Tw::Settings().setValue("openPDFwithTeX",false);Tw::Settings().setValue("researchBuildDestinationPolicy","local-cache");Research::setBuildCacheForTests(folder+"/cache");
    const QByteArray source=
      "\\documentclass[aspectratio=169]{beamer}\n\\usetheme{default}\n\\setbeamertemplate{navigation symbols}{}\n\\newcommand{\\sharedmacro}{Shared}\n\\begin{document}\n\\section{Theory}\n% retained introduction comment\n"
      "\\begin{frame}[label=first]{Perturbation bound}\n"
      "Observe $Y=M+E$.\\pause\nAssume $\\|E\\|\\leq\\delta$.\n\\only<3>{Third step.}\n\\uncover<2->{\\[\\|\\widehat P-P\\|\\leq C\\frac{\\|E\\|}{\\lambda_r(M)}.\\]}\n"
      "\\end{frame} % retained \xC2\xA0trailing comment\n\n\\input{parts/chapter}\n"
      "\\againframe<2>{first}\n"
      "\\begin{frame}[allowframebreaks]{Continuation}\nFirst continuation.\\par\\framebreak\nSecond continuation.\\par\\framebreak\nThird continuation.\n\\end{frame}\n"
      "\\begin{frame}\nUntitled body.\n\\end{frame}\n"
      "\\frame{\\frametitle{Command form} Final body.}\n\\end{document}\n";
    const QByteArray childText="% independent external source\n\\begin{frame}{External input}\nChild body.\n\\end{frame}\n";
    check("independent UTF-8 fixtures created",put(root,source)&&put(child,childText));
    auto parsed=Research::parseSlideDeck(root);
    check("six logical source frames in input order",parsed.beamer&&!parsed.uncertain&&parsed.frames.size()==6&&parsed.frames[1].file==QFileInfo(child).canonicalFilePath());
    if(parsed.frames.size()!=6){details["parserReason"]=parsed.reason;save();app.exit(1);return;}
    check("titles, untitled, command frame and againframe identified",parsed.frames[0].title=="Perturbation bound"&&parsed.frames[4].title.isEmpty()&&parsed.frames[5].title=="Command form"&&parsed.frames[2].kind=="againframe");
    check("label and continuation metadata retained",parsed.frames[0].labelled&&parsed.frames[0].label=="first"&&parsed.frames[3].breaks);
    check("chapter context follows source and included frames",parsed.frames[0].section=="Theory"&&parsed.frames[1].section=="Theory");
    auto *w=new TeXDocumentWindow(root);w->resize(1440,860);w->show();settle();
    details["devicePixelRatio"]=w->devicePixelRatioF();details["logicalDpiX"]=w->logicalDpiX();details["scaleFactor"]=qEnvironmentVariable("QT_SCALE_FACTOR");details["englishUi"]=Research::englishUi();
    check("Beamer is automatically recognized",waitFor([&]{return w->property("researchSlideMode").toBool();}));
    auto *mode=w->findChild<QAction *>("researchSlideModeAction");check("discoverable explicit mode action",mode&&mode->isCheckable()&&mode->shortcut()==QKeySequence("Ctrl+Alt+B"));
    if(!mode){app.exit(1);return;}mode->setChecked(true);
    auto *list=w->findChild<QListWidget *>("researchSlideList");auto *step=w->findChild<QSpinBox *>("researchSlideStep");auto *status=w->findChild<QLabel *>("researchSlideStatus");auto *preview=w->findChild<QtPDF::PDFDocumentView *>("researchSlidePreview");
    check("mode lists six source frames without changing bytes",waitFor([&]{return list&&list->count()==6;})&&w->textDoc()->editorText()==QString::fromUtf8(source)&&!w->isModified());
    auto *document=w->findChild<QTreeWidget *>("researchSlideDocument");
    check("preamble, theme, shared macros and full source remain accessible",document&&document->topLevelItemCount()==4);
    auto visible=[&]{QString value;for(auto b=w->editor()->document()->begin();b.isValid();b=b.next())if(b.isVisible())value+=b.text()+"\n";return value;};
    check("current frame is the only visible source",visible().startsWith("\\begin{frame}")&&!visible().contains("\\documentclass")&&!visible().contains("Untitled body")&&!w->isModified());
    QKeyEvent all(QEvent::KeyPress,Qt::Key_A,Qt::ControlModifier,"a");QApplication::sendEvent(w->editor(),&all);
    check("select all is bounded to current frame",w->textCursor().hasSelection()&&w->textCursor().selectedText().contains("Perturbation bound")&&!w->textCursor().selectedText().contains("\\input"));
    auto at=w->textCursor();at.setPosition(parsed.frames[0].start);w->editor()->setTextCursor(at);
    QKeyEvent back(QEvent::KeyPress,Qt::Key_Backspace,Qt::NoModifier);QApplication::sendEvent(w->editor(),&back);w->editor()->editLines(-1);
    check("backspace and line move cannot cross hidden source boundary",w->textDoc()->editorText()==QString::fromUtf8(source));
    document->itemClicked(document->topLevelItem(2),0);settle(100);
    check("theme entry reveals editable preamble without slide bodies",visible().contains("\\usetheme")&&!visible().contains("\\begin{frame}"));
    document->itemClicked(document->topLevelItem(0),0);check("full source entry restores entire document",visible().contains("\\documentclass")&&visible().contains("Untitled body."));
    list->itemClicked(list->item(0));check("full source navigation keeps the complete document visible",visible().contains("\\documentclass"));
    w->findChild<QToolButton *>("researchSourceFull")->click();check("explicit return to current frame removes unrelated source again",!visible().contains("\\documentclass")&&visible().contains("Perturbation bound"));
    check("uncompiled PDF explains how to preview",!step->isEnabled()&&(status->text().contains(QStringLiteral("编译后"))||status->text().contains("Compile")));
    const Engine engine("Beamer test","C:/texlive/2023/bin/windows/pdflatex.exe",{"-synctex=1","-interaction=nonstopmode","-file-line-error","$fullname"},true);app.setEngineList({engine});
    auto compile=[&]{w->typeset();bool stopped=waitFor([&]{return !w->isTypesetting()&&!w->property("researchBuildPreparing").toBool()&&!w->property("researchBuildFinishing").toBool()&&!w->property("researchPublishing").toBool();},45000);settle(900);return stopped;};
    check("actual Beamer compilation completes",compile()&&!Research::previewFile(root).isEmpty());
    if(!Research::previewFile(root).isEmpty())check("second pass completes matching navigation",compile());
    details["console"]=w->property("consoleOutput").toString();details["status"]=status->text();details["nav"]=Research::currentBuildMetadata(root).value("beamerNavigation");save();
    check("complete compiler generation includes frozen navigation",Research::currentBuildMetadata(root).value("beamerNavigation").toString().contains("\\beamer@framepages")&&Research::pdfPairValid(Research::previewFile(root)));
    check("overlay, againframe, external input and continuations map",waitFor([&]{return step->isEnabled()&&status->isHidden()&&status->text().isEmpty();}));
    auto *notice=w->findChild<QLabel *>("researchFocusMessage");
    const auto updated=Research::englishUi()?QStringLiteral("Updated"):QStringLiteral("已更新");
    const QPoint noticePosition=notice?notice->mapTo(w,QPoint()):QPoint();
    details["noticePositionInWindow"]=QJsonArray{noticePosition.x(),noticePosition.y()};
    check("accepted new preview uses shared lower-left notice",notice&&notice->isVisible()&&notice->toolTip()==updated&&noticePosition.x()<40&&noticePosition.y()>w->height()/2);
    w->grab().save(folder+"/beamer-updated-notice.png");
    check("update notice fades before dismissal",waitFor([&]{auto *effect=notice?qobject_cast<QGraphicsOpacityEffect *>(notice->graphicsEffect()):nullptr;return effect&&effect->opacity()>0&&effect->opacity()<0.85;},5000));
    check("update notice disappears leaving no persistent success label",waitFor([&]{return notice->isHidden()&&w->statusBar()->currentMessage().isEmpty();},1500)&&status->isHidden());
    const bool mapped=step->isEnabled();
    if(mapped) {
      list->setCurrentRow(0);check("one frame groups three overlay pages",step->maximum()==3);
      step->setValue(2);settle();check("step selector moves actual current PDF page",int(preview->currentPage())==1&&step->value()==2);
      w->findChild<QToolButton *>("researchSlideNextButton")->click();check("visible next-step button advances",step->value()==3);w->findChild<QToolButton *>("researchSlidePreviousButton")->click();check("visible previous-step button returns",step->value()==2);
      check("step navigation does not repeat update notice",notice->isHidden()&&w->statusBar()->currentMessage().isEmpty());
      list->setCurrentRow(3);settle();check("allowframebreaks is one source row with three pages",step->maximum()==3);
      list->setCurrentRow(2);settle();check("againframe is a separate source invocation",step->maximum()==1&&w->textCursor().blockNumber()+1==parsed.frames[2].line);
      list->setCurrentRow(4);w->syncClick(parsed.frames[0].endLine,0);settle(300);
      check("existing forward SyncTeX also updates embedded preview",list->currentRow()==0&&int(preview->currentPage())<3);
      list->setCurrentRow(4);auto pdfDoc=preview->document().toStrongRef();auto page=pdfDoc?pdfDoc->page(1).toStrongRef():QSharedPointer<QtPDF::Backend::Page>();
      if(page)preview->triggerContextClick(1,QPointF(100,page->pageSizeF().height()/2));settle(450);
      check("embedded inverse SyncTeX returns to current source frame",list->currentRow()==0&&w->textCursor().blockNumber()+1>=parsed.frames[0].line&&w->textCursor().blockNumber()+1<=parsed.frames[0].endLine);
      list->setCurrentRow(0);step->setValue(2);settle();w->grab().save(folder+"/beamer-normal.png");
      check("compile preserves current frame and overlay step",compile()&&list->currentRow()==0&&step->value()==2&&int(preview->currentPage())==1);
      w->findChild<QToolButton *>("researchSlideThumbnailToggle")->setChecked(true);
      check("selected thumbnail renders on demand",waitFor([&]{return !w->findChild<QLabel *>("researchSlideThumbnail")->pixmap().isNull();})&&w->property("researchSlideThumbnailCache").toInt()<=8);
      const int parses=w->property("researchSlideParses").toInt();settle(700);check("idle mode has no recurring parse",w->property("researchSlideParses").toInt()==parses);
    }
    // Pure source edits use complete-byte and one-undo checks, independent of PDF availability.
    QString reason;auto before=w->textDoc()->editorText();
    check("labelled duplicate is blocked without renaming or changes",!Research::editSlide(w,parsed,0,Research::SlideEdit::Duplicate,&reason)&&!reason.isEmpty()&&w->textDoc()->editorText()==before);
    check("referenced label deletion is blocked",!Research::editSlide(w,parsed,0,Research::SlideEdit::Delete,&reason)&&w->textDoc()->editorText()==before);
    check("move cannot cross an input file boundary",!Research::editSlide(w,parsed,1,Research::SlideEdit::Up,&reason)&&w->textDoc()->editorText()==before);
    for(auto op:{Research::SlideEdit::Duplicate,Research::SlideEdit::Delete,Research::SlideEdit::Up}) {
      const QString name=op==Research::SlideEdit::Duplicate?"duplicate":op==Research::SlideEdit::Delete?"delete":"move";
      const bool changed=Research::editSlide(w,parsed,4,op,&reason);const auto after=w->textDoc()->editorText();
      check(name+" changes only a source edit",changed&&after!=before);
      w->editor()->undo();check(name+" one undo restores all text and comments",w->textDoc()->editorText()==before);
      w->editor()->redo();check(name+" redo restores complete result",w->textDoc()->editorText()==after);w->editor()->undo();
    }
    settle(700);
    list->setCurrentRow(0);w->findChild<QAction *>("researchSlideInsert")->trigger();settle(700);
    check("new frame is inserted and selected without copying labels",list->count()==7&&list->currentRow()==1&&visible().startsWith("\\begin{frame}{}")&&!visible().contains("label="));
    w->findChild<QAction *>("researchSlideUndo")->trigger();settle(700);check("new frame has one-step undo",list->count()==6&&w->textDoc()->editorText()==before);
    list->setCurrentRow(4);auto *copy=w->findChild<QAction *>("researchSlideDuplicate");copy->trigger();settle(700);
    check("visual list action duplicates an unlabelled frame",list->count()==7&&w->isModified());
    w->findChild<QAction *>("researchSlideUndo")->trigger();settle(700);check("list undo restores six frames and source",list->count()==6&&w->textDoc()->editorText()==before);
    const auto pdf=Research::previewFile(root);const auto certifiedPDF=read(pdf);const auto navBefore=Research::currentBuildMetadata(root).value("beamerNavigation");
    w->editor()->moveCursor(QTextCursor::Start);w->editor()->insertPlainText("% unsaved source change\n");
    check("editing marks previous preview while retaining step reading",(status->text().contains(QStringLiteral("上一版"))||status->text().contains("Previous"))&&step->isEnabled());
    check("stale source never replaces complete PDF",read(pdf)==certifiedPDF&&Research::currentBuildMetadata(root).value("beamerNavigation")==navBefore);
    w->grab().save(folder+"/beamer-stale-preview.png");
    w->editor()->undo();settle(750);
    QByteArray broken=source;broken.replace("Untitled body.","\\UndefinedBeamerTestCommand");w->editor()->setPlainText(QString::fromUtf8(broken));check("failure fixture saved in independent project",w->save());
    check("failed build retains prior complete PDF and navigation",compile()&&(status->text().contains(QStringLiteral("失败"))||status->text().contains("failed"))&&read(pdf)==certifiedPDF&&Research::currentBuildMetadata(root).value("beamerNavigation")==navBefore);
    w->editor()->setPlainText(QString::fromUtf8(source));check("valid source restored privately",w->save());check("successful retry returns to current mapped PDF",compile()&&waitFor([&]{return step->isEnabled();}));
    Research::setFocusMode(true);settle(600);w->grab().save(folder+"/beamer-focus.png");check("focus keeps editable source and source unchanged",w->editor()->viewport()->isVisible()&&w->textDoc()->editorText()==before);
    Research::setFocusMode(false);settle(600);
    w->statusBar()->clearMessage();mode->setChecked(false);mode->setChecked(true);settle(900);
    check("reopening a cached preview does not announce an update",step->isEnabled()&&notice->isHidden()&&w->statusBar()->currentMessage().isEmpty());
    w->resize(560,700);settle(250);w->grab().save(folder+"/beamer-narrow.png");check("narrow window releases both side regions",!w->findChild<QDockWidget *>("researchSlideDock")->isVisible()&&!w->findChild<QDockWidget *>("researchSlidePreviewDock")->isVisible()&&w->editor()->viewport()->width()>300);
    mode->setChecked(false);const int parseCount=w->property("researchSlideParses").toInt();w->editor()->insertPlainText("x");w->editor()->undo();settle(700);
    check("exit releases preview and stops parsing while preserving source",!w->property("researchSlideMode").toBool()&&w->property("researchSlideParses").toInt()==parseCount&&w->textDoc()->editorText()==before);
    check("exit restores all source blocks",visible().contains("\\documentclass")&&visible().contains("Untitled body."));
    // Malformed, macro-generated and conditional structures cannot claim a map.
    for(const QByteArray &body:{QByteArray("\\newcommand{\\makeframe}{\\begin{frame}X\\end{frame}}\n\\makeframe\n"),QByteArray("\\iftrue\n\\begin{frame}X\\end{frame}\n\\fi\n"),QByteArray("\\begin{frame}\nUnclosed.\n")}) {
      const QString file=folder+"/unsupported/main.tex";put(file,"\\documentclass{beamer}\n\\begin{document}\n"+body+"\\end{document}\n");check("dynamic or malformed source explicitly downgrades",Research::parseSlideDeck(file).uncertain);
    }
    auto cancelled=Research::parseSlideDeck(root,{},[]{return true;});check("worker parser honours cancellation",cancelled.cancelled);
    auto mismatch=parsed;check("incomplete physical PDF ranges cannot be guessed",!Research::mapSlidePages(mismatch,"\\beamer@framepages{1}{2}",3,QVector<QSet<int>>(6))&&!mismatch.mapped);
    const QString emptyRoot=folder+"/empty/main.tex";const QByteArray empty="\\documentclass{beamer}\n\\begin{document}\n\\end{document}\n";put(emptyRoot,empty);
    auto *emptyWindow=new TeXDocumentWindow(emptyRoot);emptyWindow->show();
    check("empty Beamer document completes automatic mode detection",waitFor([&]{return emptyWindow->findChild<QAction *>("researchSlideInsert");},5000));
    auto *emptyList=emptyWindow->findChild<QListWidget *>("researchSlideList");settle(700);if(auto *insert=emptyWindow->findChild<QAction *>("researchSlideInsert"))insert->trigger();settle(700);
    check("new frame also works in an empty valid Beamer document",emptyList&&emptyList->count()==1&&emptyWindow->textDoc()->editorText().contains("\\begin{frame}{}"));
    emptyWindow->editor()->undo();settle(700);check("first frame insertion has one-step undo",emptyList&&emptyWindow->textDoc()->editorText()==QString::fromUtf8(empty)&&emptyList->count()==0);
    emptyWindow->setModified(false);emptyWindow->close();settle(50);
    details["sourceBytesUnchanged"]=read(root)==source&&read(child)==childText;save();check("original fixture files unchanged after restoration",read(root)==source&&read(child)==childText);
    if(w->pdfDocument())w->pdfDocument()->close();w->setModified(false);w->close();settle(50);Research::setFocusMode(false);Research::setBuildCacheForTests({});
    app.exit(failures?1:0);
  });QTimer::singleShot(90000,&app,[&]{
    QJsonArray windows;for(auto *window:QApplication::topLevelWidgets())if(window->isVisible()){
      windows.append(QJsonObject{{"class",window->metaObject()->className()},{"name",window->objectName()},{"title",window->windowTitle()}});
      window->grab().save(QFileInfo(report).absolutePath()+"/timeout-"+QString::number(windows.size())+".png");
    }
    QFile file(QFileInfo(report).absolutePath()+"/timeout-windows.json");if(file.open(QIODevice::WriteOnly))file.write(QJsonDocument(windows).toJson());app.exit(92);
  });return app.exec();
}
