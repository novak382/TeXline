#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "TeXDocumentWindow.h"
#include "PDFDocumentWindow.h"
#include "CompletingEdit.h"
#include "ResearchAppearance.h"
#include "Settings.h"
#include <QAction>
#include <QAbstractTextDocumentLayout>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QLayout>
#include <QSaveFile>
#include <QPointer>
#include <QScrollBar>
#include <QStyleOptionToolButton>
#include <QThread>
#include <QTextBlock>
#include <QTimer>
#include <QToolButton>
#include <QToolBar>
#include <QWindow>
#include <windows.h>
#include <windowsx.h>

static void settle(int milliseconds=240) {
  QElapsedTimer timer; timer.start();
  while(timer.elapsed()<milliseconds){QCoreApplication::processEvents();QThread::msleep(3);}
}
static bool put(const QString &path,const QByteArray &bytes) {
  QSaveFile file(path);return file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size()&&file.commit();
}
static QJsonObject header(QMainWindow *window) {
  auto *strip=window->findChild<QWidget*>("researchPaperControls");
  auto *title=window->findChild<QLabel*>("researchPaperTitle");
  return {{"width",window->width()},{"height",window->height()},{"stripX",strip->x()},{"devicePixelRatio",window->devicePixelRatioF()},
    {"rightInset",window->width()-strip->geometry().right()-1},{"titleRight",title->geometry().right()},
    {"titleVisible",title->isVisible()},{"nativeHandle",QString::number(window->winId())}};
}
static bool aligned(QMainWindow *window) {
  const auto state=header(window);auto *strip=window->findChild<QWidget*>("researchPaperControls");
  auto *title=window->findChild<QLabel*>("researchPaperTitle");
  return state.value("rightInset").toInt()==16&&strip->y()==(window->property("researchFocus").toBool()?8:4)&&
    title->geometry().right()<strip->x()&&title->height()==32;
}
static QJsonObject scrollState(CompletingEdit *editor) {
  auto *bar=editor->horizontalScrollBar();
  return {{"minimum",bar->minimum()},{"maximum",bar->maximum()},{"value",bar->value()},
    {"visible",bar->isVisible()},{"viewportWidth",editor->viewport()->width()},
    {"wrapMode",int(editor->wordWrapMode())},{"textLength",editor->toPlainText().size()}};
}
int main(int argc,char **argv) {
  const QString report=QString::fromLocal8Bit(argv[1]),reference=QString::fromLocal8Bit(argv[2]);argc=1;
  TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);app.applyTranslation("zh_CN");
  QTimer::singleShot(80,&app,[&]{
    const QString folder=QFileInfo(report).absolutePath(),templatePath=QCoreApplication::applicationDirPath()+"/fixtures/main/main.tex",sourcePath=folder+"/中文 路径/main.tex";
    QDir().mkpath(QFileInfo(sourcePath).absolutePath());
    QJsonArray checks,observations;int failures=0;
    auto save=[&]{put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"observations",observations}}).toJson());};
    auto check=[&](const QString &name,bool passed){checks.append(QJsonObject{{"name",name},{"passed",passed}});if(!passed)++failures;save();};
    bool unexpectedModal=false;
    QTimer modalWatch;modalWatch.setInterval(300);
    QObject::connect(&modalWatch,&QTimer::timeout,&app,[&]{
      for(auto *widget:QApplication::topLevelWidgets())if(auto *box=qobject_cast<QMessageBox*>(widget);box&&box->isVisible()) {
        unexpectedModal=true;observations.append(QJsonObject{{"phase","unexpected-modal"},{"title",box->windowTitle()},{"text",box->text()},{"detail",box->informativeText()}});save();
        if(auto *body=box->parentWidget()->findChild<CompletingEdit*>()) {
          int nonpositiveHeights=0,zeroWidths=0,zeroWidthContent=0;QJsonArray examples;
          for(auto block=body->document()->firstBlock();block.isValid();block=block.next()) {
            const QRectF bounds=body->document()->documentLayout()->blockBoundingRect(block);
            nonpositiveHeights+=bounds.height()<=0;zeroWidths+=bounds.width()<=0;zeroWidthContent+=bounds.width()<=0&&!block.text().trimmed().isEmpty();
            if(bounds.isEmpty()&&examples.size()<12)examples.append(QJsonObject{{"block",block.blockNumber()},{"width",bounds.width()},{"height",bounds.height()},{"characters",block.text().size()},{"blank",block.text().trimmed().isEmpty()}});
          }
          observations.append(QJsonObject{{"phase","modal-layout-bounds"},{"nonpositiveHeights",nonpositiveHeights},{"zeroWidths",zeroWidths},{"zeroWidthContent",zeroWidthContent},{"examples",examples}});save();
        }
        box->grab().save(folder+"/unexpected-modal.png");box->reject();
      }
    });modalWatch.start();
    Tw::Settings().setValue("researchFocusMode",false);Tw::Settings().setValue("openPDFwithTeX",false);
    check("a fresh independent real manuscript exists",QFile::copy(templatePath,sourcePath));
    auto *source=new TeXDocumentWindow(sourcePath);source->setAttribute(Qt::WA_ShowWithoutActivating);source->show();source->lower();settle();
    const QString original=source->editor()->toPlainText();auto *editor=source->editor();
    auto *pdf=new PDFDocumentWindow(reference);pdf->setAttribute(Qt::WA_ShowWithoutActivating);pdf->show();pdf->lower();settle();
    const WId pdfHandle=pdf->winId();
    observations.append(QJsonObject{{"phase","initial-normal"},{"source",header(source)},{"pdf",header(pdf)}});
    check("ordinary first open places source and PDF window controls at the far right",aligned(source)&&aligned(pdf));
    bool resizeAligned=true;
    for(int width:{600,1000,760,1100}) {
      source->resize(width,780);pdf->resize(width-40,760);settle();
      resizeAligned&=aligned(source)&&aligned(pdf);
      observations.append(QJsonObject{{"phase","normal-resize"},{"source",header(source)},{"pdf",header(pdf)}});
    }
    check("normal controls follow every resize even while focus entries are hidden",resizeAligned);
    bool menuInset=true;
    for(QMainWindow *window:{static_cast<QMainWindow*>(source),static_cast<QMainWindow*>(pdf)}) {
      auto *more=window->findChild<QToolButton*>("researchMore");
      const int right=more->mapTo(window,QPoint()).x()+more->width();
      auto *close=window->findChild<QToolButton*>("researchWindowClose");
      menuInset&=window->width()-right==16&&more->width()==32&&more->mapTo(window,more->rect().center()).x()==close->mapTo(window,close->rect().center()).x();
      auto *bar=window->findChild<QToolBar*>(window==source?"toolBar_run":"toolBar");const auto margins=bar->layout()->contentsMargins();
      observations.append(QJsonObject{{"phase","normal-menu-inset"},{"width",window->width()},{"menuRightInset",window->width()-right},{"menuWidth",more->width()},{"layoutMargins",QJsonArray{margins.left(),margins.top(),margins.right(),margins.bottom()}}});save();
    }
    check("normal More hit targets have the same sixteen pixel edge inset as window controls",menuInset);
    pdf->widget()->zoomFitWindow();settle();
    source->grab().save(folder+"/normal-resized.png");pdf->grab().save(folder+"/pdf-normal-resized.png");
    bool maximizeAligned=true;
    for(QMainWindow *window:{static_cast<QMainWindow*>(source),static_cast<QMainWindow*>(pdf)}) {
      window->showMaximized();settle();maximizeAligned&=aligned(window);
      observations.append(QJsonObject{{"phase","normal-maximized"},{"geometry",header(window)}});
      window->showNormal();settle();maximizeAligned&=aligned(window);
    }
    check("ordinary maximize and restore never strand the controls at their old coordinates",maximizeAligned);
    bool compact=true,clickable=true;
    for(QMainWindow *window:{static_cast<QMainWindow*>(source),static_cast<QMainWindow*>(pdf)}) {
      auto *bar=window->findChild<QToolBar*>(window==source?"toolBar_run":"toolBar");
      compact&=bar->mapTo(window,QPoint()).y()==40&&bar->height()<=42;
      for(const char *name:{"researchWindowMinimize","researchWindowMaximize","researchWindowClose"}) {
        auto *button=window->findChild<QToolButton*>(name);const QPoint location=button->mapTo(window,QPoint(16,2));
        POINT point{qRound(location.x()*window->devicePixelRatioF()),qRound(location.y()*window->devicePixelRatioF())};ClientToScreen(reinterpret_cast<HWND>(window->winId()),&point);
        clickable&=SendMessageW(reinterpret_cast<HWND>(window->winId()),WM_NCHITTEST,0,MAKELPARAM(point.x,point.y))==HTCLIENT;
      }
    }
    check("normal title and toolbar use a compact 40 plus 40 pixel proportion",compact);
    check("the entire compact window button remains clickable above the resize band",clickable);
    auto *compile=source->findChild<QToolButton*>("researchCompile"),*pdfCompile=pdf->findChild<QToolButton*>("researchCompile");
    check("primary action has the same restrained weight and size in both windows",compile->font().weight()==QFont::Medium&&pdfCompile->font().weight()==QFont::Medium&&compile->size()==pdfCompile->size());
    auto compactCompile=[&](QToolButton *button,const QString &language) {
      QStyleOptionToolButton option;option.initFrom(button);option.rect=button->rect();option.features=QStyleOptionToolButton::MenuButtonPopup;
      const QRect target=button->style()->subControlRect(QStyle::CC_ToolButton,&option,QStyle::SC_ToolButtonMenu,button);
      const int captionEnd=9+button->iconSize().width()+4+button->fontMetrics().horizontalAdvance(button->text());
      const int gap=target.left()-captionEnd;
      observations.append(QJsonObject{{"phase","compile-caption-spacing"},{"language",language},{"width",button->width()},{"captionGap",gap},{"menuTargetWidth",target.width()}});
      return target.width()>=18&&gap>=4&&gap<=20;
    };
    check("Chinese build captions sit close to a separate usable engine target",compactCompile(compile,"zh_CN")&&compactCompile(pdfCompile,"zh_CN"));
    editor->moveCursor(QTextCursor::End);source->setWrapLines(true);settle();
    observations.append(QJsonObject{{"phase","real-manuscript-wrapped"},{"scroll",scrollState(editor)}});
    check("wrapped real manuscript does not expose a horizontal overflow rail",editor->horizontalScrollBar()->maximum()==0&&!editor->horizontalScrollBar()->isVisible());
    source->grab().save(folder+"/real-manuscript-bottom.png");
    bool realWidths=true;
    for(int width:{600,760,1000}) {source->resize(width,780);settle();realWidths&=editor->horizontalScrollBar()->maximum()==0&&!editor->horizontalScrollBar()->isVisible();}
    check("real paper stays horizontally contained at common normal window widths",realWidths);
    auto cursor=editor->textCursor();cursor.movePosition(QTextCursor::End);
    cursor.insertText("\n%LongCommand: \\operatorname{"+QString(1200,QLatin1Char('a'))+"}\n%Path: C:/中文项目/"+QString("unbroken_tex_macro_").repeated(90)+".tex\n");editor->setTextCursor(cursor);settle();
    const QString extended=editor->toPlainText();
    observations.append(QJsonObject{{"phase","long-token-wrapped"},{"scroll",scrollState(editor)}});
    check("wrapping very long commands and paths keeps all text inside the viewport",editor->horizontalScrollBar()->maximum()==0&&!editor->horizontalScrollBar()->isVisible());
    source->grab().save(folder+"/long-token-wrapped.png");
    source->setWrapLines(false);settle();
    observations.append(QJsonObject{{"phase","long-token-unwrapped"},{"scroll",scrollState(editor)}});
    check("turning wrapping off retains meaningful horizontal scrolling",editor->horizontalScrollBar()->isVisible()&&editor->horizontalScrollBar()->maximum()>500);
    editor->horizontalScrollBar()->setValue(80);settle();source->setWrapLines(true);settle();
    check("turning wrapping back on resets overflow and restores the full left edge",editor->horizontalScrollBar()->maximum()==0&&editor->horizontalScrollBar()->value()==0&&!editor->horizontalScrollBar()->isVisible());
    bool widthClean=true,focusAligned=true;
    for(int pass=0;pass<6;++pass) {
      Research::setFocusMode(pass%2);source->resize(600+pass*90,760);pdf->resize(640+pass*80,740);settle();
      focusAligned&=aligned(source)&&aligned(pdf);
      widthClean&=editor->horizontalScrollBar()->maximum()==0&&editor->horizontalScrollBar()->value()==0;
    }
    check("interleaved modes and sizes keep both window button strips pinned right",focusAligned);
    check("interleaved modes and sizes never reintroduce wrapped-text overflow",widthClean);
    app.applyTranslation("en_US");settle();Research::setFocusMode(false);settle();
    auto *veil=source->findChild<QWidget*>("researchChromeVeil");QElapsedTimer finish;finish.start();
    while(veil&&!veil->isHidden()&&finish.elapsed()<600)settle(10);
    observations.append(QJsonObject{{"phase","normal-after-language-switch"},{"transitionWaitMs",double(finish.elapsed())},{"veilVisible",veil&&!veil->isHidden()},
      {"controlsVisible",source->findChild<QWidget*>("researchPaperControls")->isVisible()},{"titleVisible",source->findChild<QLabel*>("researchPaperTitle")->isVisible()}});
    check("returning from focus finishes the transition and restores the normal title and controls",(!veil||veil->isHidden())&&source->findChild<QWidget*>("researchPaperControls")->isVisible()&&source->findChild<QLabel*>("researchPaperTitle")->isVisible());
    check("language changes keep normal source and PDF title geometry consistent",aligned(source)&&aligned(pdf));
    check("English build captions also fit beside a compact usable engine target",compactCompile(compile,"en_US")&&compactCompile(pdfCompile,"en_US"));
    source->grab().save(folder+"/english-normal.png");
    check("layout and display wrapping never change manuscript contents",editor->toPlainText()==extended);
    editor->undo();settle();check("display wrapping does not interfere with text undo",editor->toPlainText()==original);
    source->move(140,100);source->resize(960,740);settle();source->saveState();source->setModified(false);
    QPointer<TeXDocumentWindow> guard(source);const bool closed=source->close();settle();
    check("normal closing completes the recovery and window lifecycle",closed&&!guard);
    observations.append(QJsonObject{{"phase","before-reopen"}});save();
    source=new TeXDocumentWindow(sourcePath);observations.append(QJsonObject{{"phase","reopen-created"}});save();
    source->setAttribute(Qt::WA_ShowWithoutActivating);source->show();source->lower();settle();
    check("reopening from saved geometry pins the ordinary controls before entering focus",aligned(source));
    check("reopening preserves the original disk manuscript",source->editor()->toPlainText()==original);
    int missingLines=0,collapsedText=0,validBlankLines=0;QJsonArray missingExamples;
    for(auto block=source->editor()->document()->firstBlock();block.isValid();block=block.next()) {
      const QRectF bounds=source->editor()->document()->documentLayout()->blockBoundingRect(block);
      if(bounds.height()<=0&&missingExamples.size()<12)missingExamples.append(QJsonObject{{"block",block.blockNumber()},{"position",block.position()},{"lines",block.layout()->lineCount()},{"boundsY",bounds.y()}});
      missingLines+=bounds.height()<=0;collapsedText+=bounds.width()<=0&&!block.text().trimmed().isEmpty();validBlankLines+=bounds.width()<=0&&bounds.height()>0&&block.text().trimmed().isEmpty();
    }
    observations.append(QJsonObject{{"phase","reopened-layout"},{"missingLines",missingLines},{"collapsedText",collapsedText},{"validBlankLines",validBlankLines},{"missingExamples",missingExamples}});
    check("reopened manuscript keeps every text and blank paragraph at a valid visible height",missingLines==0&&collapsedText==0);
    source->editor()->document()->documentLayout()->documentSize();int afterSizeMissing=0;
    for(auto block=source->editor()->document()->firstBlock();block.isValid();block=block.next())afterSizeMissing+=source->editor()->document()->documentLayout()->blockBoundingRect(block).height()<=0;
    observations.append(QJsonObject{{"phase","after-full-layout-demand"},{"missingLines",afterSizeMissing}});save();
    source->grab().save(folder+"/normal-reopened.png");
    check("the retained PDF keeps its native identity through every operation",pdf->winId()==pdfHandle);
    check("the complete opening and reopening flow requires no unexpected dialog",!unexpectedModal);
    source->setModified(false);source->close();pdf->close();settle();app.exit(failures?1:0);
  });return app.exec();
}
