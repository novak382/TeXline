// Temporary research tools operating on ordinary TeX, without another document model.
// GPL-2.0-or-later.
#include "ResearchAuthoring.h"
#include "ResearchTikz.h"
#include "ResearchOnboarding.h"
#include "ResearchBibliography.h"
#include "ResearchWorkspaceUi.h"
#include "ResearchAppearance.h"
#include "ResearchObjects.h"
#include "ResearchProject.h"
#include "ResearchSlideExport.h"
#include "ResearchWorkflow.h"
#include "ResearchReferences.h"
#include "ResearchSourceMap.h"
#include "ResearchSourceHistory.h"
#include "BibTeXFile.h"
#include "TeXDocumentWindow.h"
#include "CompletingEdit.h"
#include "TWApp.h"
#include "PDFBackend.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QMimeData>
#include <QKeyEvent>
#include <QContextMenuEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QtMath>
#include <QCryptographicHash>
#include <QFormLayout>
#include <QHeaderView>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QRunnable>
#include <QSaveFile>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QSplitter>
#include <QSlider>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QDropEvent>
#include <QDrag>
#include <QProcess>
#include <QtSvg/QSvgRenderer>
#include <QTabWidget>
#include <QTabBar>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTextBlock>
#include <QThreadPool>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <atomic>
#include <memory>
#include <vector>

namespace Research {
namespace {
bool escaped(const QString &s,int at){int count=0;while(at>0&&s[--at]==QLatin1Char('\\'))++count;return count%2;}
QString group(const QString &s,int &at,QChar open=QLatin1Char('{'),QChar close=QLatin1Char('}')) {
  while(at<s.size()&&s[at].isSpace())++at;
  if(at>=s.size()||s[at]!=open)return {};
  const int first=++at;int depth=1;
  while(at<s.size()){if(!escaped(s,at)){if(s[at]==open)++depth;else if(s[at]==close&&!--depth)return s.mid(first,at++-first);}++at;}
  at=-1;return {};
}
QString rootOf(TeXDocumentWindow *window){auto root=window->textDoc()->getRootFilePath();return root.isEmpty()?window->fileName():root;}
QHash<QString,QString> sourceBuffers(){QHash<QString,QString> out;for(auto *w:TeXDocumentWindow::documentList())if(!w->untitled())out.insert(QFileInfo(w->fileName()).canonicalFilePath(),w->textDoc()->editorText());return out;}
QString sourceOf(const QString &file,const QHash<QString,QString> &buffers){const auto path=QFileInfo(file).canonicalFilePath();if(buffers.contains(path))return buffers.value(path);QFile f(file);return f.open(QIODevice::ReadOnly)&&f.size()<4*1024*1024?QString::fromUtf8(f.readAll()).replace(QStringLiteral("\r\n"),QStringLiteral("\n")):QString();}
QSet<QString> loadedPackages(const QString &source){
  QSet<QString> packages;auto it=QRegularExpression(QStringLiteral(R"(\\(?:usepackage|RequirePackage)\s*(?:\[[^\]]*\]\s*)?\{([^{}]+)\})")).globalMatch(maskProjectText(source));
  while(it.hasNext())for(const auto &name:it.next().captured(1).split(QLatin1Char(','),Qt::SkipEmptyParts))packages.insert(name.trimmed());
  return packages;
}
// Supply the standard BibTeX default only for an unambiguous basic article.
// A custom class, local package or included preamble may set its own style.
QString initialBibliographyStyle(const QString &source,const QString &root){
  const auto clean=maskProjectText(source);
  const auto documentClass=QRegularExpression(QStringLiteral(R"(\\documentclass\s*(?:\[[^\]]*\]\s*)?\{([^{}]+)\})")).match(clean);
  if(!documentClass.hasMatch()||documentClass.captured(1).trimmed()!=QStringLiteral("article"))return {};
  if(QRegularExpression(QStringLiteral(R"(\\(?:bibliographystyle|bibstyle|input|include|import|subimport|InputIfFileExists|IfFileExists)\b)")).match(clean).hasMatch())return {};
  const auto directory=QFileInfo(root).absoluteDir();
  if(directory.exists(QStringLiteral("article.cls")))return {};
  const QSet<QString> known{QStringLiteral("amsmath"),QStringLiteral("amssymb"),QStringLiteral("amsfonts"),QStringLiteral("amsthm"),QStringLiteral("mathtools"),QStringLiteral("graphicx"),QStringLiteral("geometry"),QStringLiteral("inputenc"),QStringLiteral("fontenc"),QStringLiteral("babel"),QStringLiteral("hyperref"),QStringLiteral("xcolor"),QStringLiteral("booktabs"),QStringLiteral("natbib")};
  const auto packages=loadedPackages(clean);
  for(const auto &package:packages)if(!known.contains(package)||directory.exists(package+QStringLiteral(".sty")))return {};
  return packages.contains(QStringLiteral("natbib"))?QStringLiteral("plainnat"):QStringLiteral("plain");
}
QStringList draftPackages(const QString &source){
  const auto clean=maskProjectText(source);QStringList packages;
  if(QRegularExpression(QStringLiteral(R"(\\includegraphics\b)")).match(clean).hasMatch())packages.append(QStringLiteral("graphicx"));
  if(QRegularExpression(QStringLiteral(R"(\\begin\s*\{tikzpicture\})")).match(clean).hasMatch())packages.append(QStringLiteral("tikz"));
  if(QRegularExpression(QStringLiteral(R"(\\(?:toprule|midrule|bottomrule)\b)")).match(clean).hasMatch())packages.append(QStringLiteral("booktabs"));
  return packages;
}
bool literalKey(const QString &s){return !s.isEmpty()&&s.size()<250&&!QRegularExpression(QStringLiteral(R"([\s{}\\%,#\[\]])")).match(s).hasMatch();}
bool balancedText(const QString &s){const auto clean=maskProjectText(s);int depth=0;for(int i=0;i<clean.size();++i)if(!escaped(clean,i)){if(clean[i]==QLatin1Char('{'))++depth;else if(clean[i]==QLatin1Char('}')&&--depth<0)return false;}return depth==0;}
template<class T>T *named(T *widget,const char *name){widget->setObjectName(QString::fromLatin1(name));return widget;}
QLineEdit *field(QWidget *parent,const char *name){auto *f=named(new QLineEdit(parent),name);configureField(f);return f;}
QToolButton *textButton(QWidget *parent,const char *name,const QString &text){auto *b=named(new QToolButton(parent),name);b->setToolButtonStyle(Qt::ToolButtonTextOnly);b->setText(text);b->setAccessibleName(text);configureButton(b,ButtonRole::Text);return b;}
void fitDialog(QDialog *d,int width,int height){const auto area=d->parentWidget()->screen()->availableGeometry();d->resize(qMin(width,qMax(340,area.width()-48)),qMin(height,qMax(360,area.height()-64)));}
void authoringSurface(QDialog *d){d->setProperty("researchWhiteSurface",true);d->setProperty("researchArrivalOpaque",true);auto palette=d->palette();palette.setColor(QPalette::Window,Qt::white);palette.setColor(QPalette::Base,Qt::white);palette.setColor(QPalette::Text,QColor(QStringLiteral("#30363c")));palette.setColor(QPalette::WindowText,QColor(QStringLiteral("#30363c")));d->setPalette(palette);d->setStyleSheet(QStringLiteral("QWidget#researchObjectForm,QWidget#researchObjectPreviewPane,QWidget#researchTableMeta,QScrollArea,QScrollArea>QWidget>QWidget {background:#ffffff;} QListWidget,QPlainTextEdit,QTableWidget,QLineEdit,QComboBox {background:#ffffff;color:#30363c;selection-background-color:#e8eef2;selection-color:#20252a;} QListWidget::item:selected {background:#e8eef2;color:#20252a;}"));}
struct Command {int start{-1},end{-1},valueStart{-1},valueEnd{-1};QString value;bool valid()const{return start>=0&&end>=start;}};
Command command(const QString &source,const QString &name) {
  const auto clean=maskProjectText(source);const auto m=QRegularExpression(QStringLiteral("\\\\")+name+QStringLiteral("(?![A-Za-z@])\\s*(?:\\[[^\\]]*\\]\\s*)?\\{")).match(clean);
  if(!m.hasMatch())return {};
  int at=int(m.capturedEnd())-1;const int first=at+1;group(clean,at);if(at<0)return {};const auto value=source.mid(first,at-first-1);
  return {int(m.capturedStart()),at,first,at-1,value};
}
QString setCommand(QString text,const QString &name,const QString &value){const auto c=command(text,name);if(c.valid()){if(value.isEmpty())text.remove(c.start,c.end-c.start);else text.replace(c.valueStart,c.valueEnd-c.valueStart,value);return text;}if(value.isEmpty())return text;const int end=text.lastIndexOf(QStringLiteral("\\end"));if(end>=0)text.insert(end,QStringLiteral("\\")+name+QLatin1Char('{')+value+QStringLiteral("}\n"));return text;}

// The snapshot guard covers both in-memory edits and disk replacements. A failed
// guard leaves the dialog and its draft open. It never applies old offsets.
struct SourceEdit {
  QPointer<TeXDocumentWindow> target;QString original,file,rootFile,rootOriginal;QByteArray disk,rootDisk;int start,end;QTextCursor cursor,range;int scroll;
  SourceEdit(TeXDocumentWindow *w,int a,int b):target(w),original(w->textDoc()->editorText()),file(w->fileName()),disk(w->untitled()?QByteArray():slideExportHash(file)),start(a),end(b),cursor(w->textCursor()),scroll(w->editor()->verticalScrollBar()->value()){
    range=QTextCursor(w->textDoc());range.setPosition(a);range.setPosition(b,QTextCursor::KeepAnchor);captureRoot();
  }
  void captureRoot(){rootFile=target->untitled()?file:QFileInfo(rootOf(target)).canonicalFilePath();rootOriginal=target->untitled()?original:sourceOf(rootFile,sourceBuffers());rootDisk=target->untitled()?QByteArray():slideExportHash(rootFile);}
  void rebase(){if(!target)return;original=target->textDoc()->editorText();start=range.selectionStart();end=range.selectionEnd();disk=target->untitled()?QByteArray():slideExportHash(file);captureRoot();}
  bool current()const{return target&&!target->editor()->isReadOnly()&&target->fileName()==file&&target->textDoc()->editorText()==original&&(target->untitled()||slideExportHash(file)==disk);}
  bool sameRoot()const{return target->untitled()||QFileInfo(file).canonicalFilePath()==rootFile;}
  bool prepare(const QStringList &packages,QString &dependency,QLabel *notice)const {
    if(!current()){notice->setText(words("原稿已变化，请保留草稿并重新核对。","The source changed. Keep this draft and check the current source."));return false;}
    if(!packages.isEmpty()&&!target->untitled()&&(QFileInfo(rootOf(target)).canonicalFilePath()!=rootFile||sourceOf(rootFile,sourceBuffers())!=rootOriginal||slideExportHash(rootFile)!=rootDisk)){notice->setText(words("主文件已变化，请保留草稿并重新核对。","The main document changed. Keep this draft and check its preamble."));return false;}
    dependency.clear();const auto clean=maskProjectText(rootOriginal);const auto begin=QRegularExpression(QStringLiteral(R"(\\begin\s*\{document\})")).match(clean);const auto present=loadedPackages(begin.hasMatch()?rootOriginal.left(begin.capturedStart()):rootOriginal);
    for(const auto &package:packages)if(!present.contains(package))dependency+=QStringLiteral("\\usepackage{")+package+QStringLiteral("}\n");
    if(dependency.isEmpty())return true;
    if(!begin.hasMatch()||(!sameRoot()&&(rootFile.isEmpty()||!QFileInfo(rootFile).isWritable()))){notice->setText(words("无法修改主文件导言，请打开可写的主文件后重试。","Open a writable main document before adding the required packages."));return false;}
    if(packages.contains(QStringLiteral("subcaption"))&&(present.contains(QStringLiteral("subfig"))||present.contains(QStringLiteral("subfigure")))){notice->setText(words("现有子图宏包与 subcaption 冲突，请先调整主文件导言。","The existing subfigure package conflicts with subcaption. Adjust the main preamble first."));return false;}
    return true;
  }
  bool apply(const QString &replacement,const QStringList &packages,QLabel *notice) {
    QString dependency;if(!prepare(packages,dependency,notice))return false;
    if(replacement==original.mid(start,end-start)&&dependency.isEmpty())return true;
    QPointer<TeXDocumentWindow> main=target;
    if(!dependency.isEmpty()&&!sameRoot()){
      main=TeXDocumentWindow::findDocument(rootFile);
      if((!main||!main->property("researchRecoveryReady").toBool())&&hasPendingRecovery(rootFile)){notice->setText(words("主文件有未恢复的内容。请先打开主文件处理，再应用草稿。","The main file has unsaved recovery content. Open it to resolve that content, then apply this draft."));return false;}
      const bool openedHere=!main;
      if(!main)main=new TeXDocumentWindow(rootFile,false,true);
      if(!main||main->untitled()||main->property("researchRecoveryBlocked").toBool()||main->editor()->isReadOnly()||main->textDoc()->editorText()!=rootOriginal||!prepare(packages,dependency,notice)){
        if(openedHere&&main&&!main->isModified())delete main.data();
        notice->setText(words("主文件无法更新，草稿已保留。请核对主文件后重试。","The main document could not be updated. Check it and retry; your draft is retained."));return false;
      }
    }
    const auto begin=QRegularExpression(QStringLiteral(R"(\\begin\s*\{document\})")).match(maskProjectText(rootOriginal));
    QVector<SourceHistoryFile> history;if(main!=target&&!dependency.isEmpty()){history.append(sourceHistoryBefore(target));history.append(sourceHistoryBefore(main));}
    const int oldPosition=cursor.position(),oldAnchor=cursor.anchor();
    // Guard every source before starting either native edit block. No event
    // processing occurs between the package insertion and the object change.
    auto edit=cursor;edit.beginEditBlock();
    if(!dependency.isEmpty()){
      QTextCursor preamble(main->textDoc());preamble.setPosition(int(begin.capturedStart()));
      if(main!=target)preamble.beginEditBlock();preamble.insertText(dependency);if(main!=target)preamble.endEditBlock();
    }
    const int shift=main==target&&!dependency.isEmpty()&&begin.capturedStart()<=start?dependency.size():0;
    edit.setPosition(start+shift);edit.setPosition(end+shift,QTextCursor::KeepAnchor);edit.insertText(replacement);edit.endEditBlock();
    if(!history.isEmpty())registerSourceHistory(std::move(history));
    const auto after=target->textDoc()->editorText();cursor.setPosition(mapSourcePosition(original,after,oldAnchor));cursor.setPosition(mapSourcePosition(original,after,oldPosition),QTextCursor::KeepAnchor);target->editor()->setTextCursor(cursor);target->editor()->verticalScrollBar()->setValue(scroll);return true;
  }
};

class PreviewImage final : public QLabel {
  QImage content;QSvgRenderer renderer;QRectF bounds;bool vector{false};
  void fit(){if(vector){update();return;}if(content.isNull())return;auto image=QPixmap::fromImage(content).scaled(contentsRect().size()*devicePixelRatioF(),Qt::KeepAspectRatio,Qt::SmoothTransformation);image.setDevicePixelRatio(devicePixelRatioF());setPixmap(image);}
public:
  using QLabel::QLabel;
  void setImage(const QImage &value){vector=false;content=value;setProperty("researchDraftVector",false);setProperty("researchDraftRasterPixels",value.size());fit();}
  bool setVector(const QByteArray &svg,const QRectF &crop){if(svg.isEmpty()||!renderer.load(svg)||!renderer.isValid()||crop.isEmpty())return false;clear();content={};vector=true;bounds=crop;renderer.setViewBox(crop);setProperty("researchDraftVector",true);setProperty("researchDraftVectorBytes",svg.size());update();return true;}
  void reset(const QString &text){vector=false;content={};setProperty("researchDraftVector",false);clear();setText(text);}
protected:
  void paintEvent(QPaintEvent *event)override{QLabel::paintEvent(event);if(!vector){if(property("researchSlidePreview").toBool()&&!content.isNull()){QPainter p(this);p.setPen(QColor(QStringLiteral("#e2e7eb")));auto size=QSizeF(content.size());size.scale(contentsRect().size(),Qt::KeepAspectRatio);const QRectF page(QRectF(contentsRect()).center()-QPointF(size.width()/2,size.height()/2),size);p.drawRect(page.adjusted(.5,.5,-.5,-.5));}return;}QPainter painter(this);painter.setRenderHints(QPainter::Antialiasing|QPainter::TextAntialiasing|QPainter::SmoothPixmapTransform);const QRectF area=contentsRect().adjusted(2,2,-2,-2);QSizeF size=bounds.size();auto available=area.size();if(property("researchNaturalPreview").toBool()){available.setWidth(qMin(available.width(),bounds.width()*2));available.setHeight(qMin(available.height(),bounds.height()*2));}size.scale(available,Qt::KeepAspectRatio);const QRectF destination(area.center()-QPointF(size.width()/2,size.height()/2),size);painter.setClipRect(area);renderer.render(&painter,destination);}
protected:
  void resizeEvent(QResizeEvent *event)override{QLabel::resizeEvent(event);fit();}
};
// Reuse the installed PDF converter. Fonts remain outlines, so the caption and
// table rules are repainted at the display's device resolution on every resize.
QByteArray draftVector(const QString &program,const QString &pdf,const QString &directory,const std::shared_ptr<std::atomic_bool> &cancel) {
  if(program.isEmpty()||cancel->load())return {};QProcess process;process.setProgram(program);process.setWorkingDirectory(directory);process.setArguments({QStringLiteral("-svg"),QStringLiteral("-f"),QStringLiteral("1"),QStringLiteral("-l"),QStringLiteral("1"),pdf,QStringLiteral("draft.svg")});process.setStandardOutputFile(QProcess::nullDevice());process.setStandardErrorFile(QProcess::nullDevice());
#ifdef Q_OS_WIN
  process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args){args->flags|=0x08000000;});
#endif
  process.start();QElapsedTimer time;time.start();if(!process.waitForStarted(500))return {};
  while(!process.waitForFinished(50)){if(cancel->load()||time.elapsed()>3000){process.kill();process.waitForFinished(500);return {};}}
  if(process.exitStatus()!=QProcess::NormalExit||process.exitCode()!=0)return {};QFile file(QDir(directory).filePath(QStringLiteral("draft.svg")));if(!file.open(QIODevice::ReadOnly)||file.size()>16*1024*1024)return {};auto svg=file.readAll();svg.replace("<symbol","<g").replace("</symbol>","</g>");return svg;
}
class PercentField final : public QDoubleSpinBox {
public:using QDoubleSpinBox::QDoubleSpinBox;
protected:QString textFromValue(double value)const override{auto text=QDoubleSpinBox::textFromValue(value);const auto point=locale().decimalPoint();if(text.contains(point)){while(text.endsWith(QLatin1Char('0')))text.chop(1);if(text.endsWith(point))text.chop(point.size());}return text;}
};
// Uses the existing independent export worker. No writes to the manuscript,
// normal build cache or its PDF; cancellation only stops this private job.
class DraftPreview final : public QObject {
  QPointer<PreviewImage> image;QPointer<QLabel> notice;QPointer<QPushButton> trigger;std::shared_ptr<std::atomic_bool> cancel{std::make_shared<std::atomic_bool>(false)};quint64 generation{0};
  std::shared_ptr<SlidePreviewSession> previewSession{std::make_shared<SlidePreviewSession>()};
  QVector<QImage> pages;bool running{false};std::function<void()> pending;
  void retry(bool failed){if(trigger&&trigger->property("researchRetryOnly").toBool()){trigger->setText(words("重试","Retry"));trigger->setVisible(failed);}}
public:
  std::function<void(int)> pagesReady;
  void showPage(int page){if(image&&page>=0&&page<pages.size())image->setImage(pages[page]);}
  DraftPreview(PreviewImage *i,QLabel *n,QPushButton *b):QObject(i),image(i),notice(n),trigger(b){}
  ~DraftPreview()override{cancel->store(true);auto retired=std::move(previewSession);QThreadPool::globalInstance()->start(QRunnable::create([retired]{}));}
  void invalidate(bool clear=false){cancel->store(true);pending={};++generation;pages.clear();retry(false);if(trigger)trigger->setEnabled(true);if(image){if(clear)image->reset(words("草稿已更改","Draft changed"));image->setProperty("researchDraftReady",false);}}
  void start(TeXDocumentWindow *w,const QString &draft,const QStringList &packages,bool allSteps=false,QSharedPointer<QTemporaryDir> assets={}) {
    invalidate();if(running){QPointer<TeXDocumentWindow> owner(w);pending=[this,owner,draft,packages,allSteps,assets]{if(owner)start(owner,draft,packages,allSteps,assets);};return;}if(w->untitled()){notice->setText(words("保存文稿后可使用项目预览。","Save the document to preview with its project settings."));retry(true);return;}
    SlideExportRequest request;request.previewSession=previewSession;request.root=QFileInfo(rootOf(w)).canonicalFilePath();request.sources=sourceBuffers();request.engine=TWApp::instance()->getNamedEngine(w->researchEngineName());
    // Preview never opts into external commands, even if the normal build does.
    auto args=request.engine.arguments();for(int i=args.size()-1;i>=0;--i)if(args[i].contains(QStringLiteral("shell-escape"))||args[i].contains(QStringLiteral("enable-write18")))args.removeAt(i);args.prepend(QStringLiteral("-no-shell-escape"));request.engine.setArguments(args);
    const auto rootSource=sourceOf(request.root,request.sources);const auto begin=QRegularExpression(QStringLiteral(R"(\\begin\s*\{document\})")).match(maskProjectText(rootSource));
    if(!begin.hasMatch()){notice->setText(words("未找到主文档，仍可编辑源码。","The main document is unavailable. Source editing remains available."));retry(true);return;}
    auto preamble=rootSource.left(begin.capturedStart());const auto present=loadedPackages(preamble);for(const auto &package:packages)if(!present.contains(package))preamble+=QStringLiteral("\n\\usepackage{")+package+QStringLiteral("}\n");
    QString body=draft;if(!allSteps&&QRegularExpression(QStringLiteral(R"(\\documentclass[^\n]*\{(?:beamer|ctexbeamer)\})")).match(maskProjectText(preamble)).hasMatch())body=QStringLiteral("\\begin{frame}\n")+body+QStringLiteral("\n\\end{frame}");
    request.sources[request.root]=preamble+QStringLiteral("\n\\begin{document}\n\\pagestyle{empty}\n")+body+QStringLiteral("\n\\thispagestyle{empty}\n\\end{document}\n");request.profiles={0};
    cancel=std::make_shared<std::atomic_bool>(false);const auto token=cancel;const auto ticket=++generation;QPointer<DraftPreview> guard(this);trigger->setEnabled(false);notice->setText(words("正在排版草稿…","Typesetting draft…"));
    const QString vectorProgram=TWApp::instance()->findProgram(QStringLiteral("pdftocairo"),Engine::binPaths());
    const QSize targetPixels=qMax(1.0,image->devicePixelRatioF())*image->size()*2;
    running=true;
    QThreadPool::globalInstance()->start(QRunnable::create([guard,token,ticket,request,vectorProgram,targetPixels,allSteps,assets]()mutable{
      Q_UNUSED(assets); // Keep safe private aliases alive until the cancelled job exits.
      QTemporaryDir folder(QDir::tempPath()+QStringLiteral("/texline-draft-XXXXXX"));QImage rendered;QByteArray vector;QRectF bounds;QString error,log;
      QVector<QImage> steps;
      if(folder.isValid()){
        const auto pdf=folder.filePath(QStringLiteral("draft.pdf"));request.destinations={pdf};request.destinationHashes.insert(pdf,{});const auto result=exportSlideCopies(request,token);error=result.error;log=result.log;
        if(!token->load()&&error.isEmpty()){
          auto doc=QtPDF::Backend::Document::newDocument(pdf);
          if(allSteps&&doc){if(doc->numPages()>40)error=words("草稿超过 40 步，请缩小步骤范围。","Draft exceeds 40 steps. Reduce its step range.");else for(int i=0;i<doc->numPages()&&!token->load();++i)if(auto page=doc->page(i).toStrongRef()){const auto size=page->pageSizeF();const auto scale=qMin(qMin(2000.0/qMax(1.0,size.width()),1500.0/qMax(1.0,size.height())),qSqrt(64.0*1024*1024/(4*qMax(1,doc->numPages())*qMax(1.0,size.width()*size.height()))));steps.append(page->renderToImage(scale*72,scale*72,QRect(),false));}}
          if(!allSteps&&doc&&doc->numPages()>0)if(auto page=doc->page(0).toStrongRef()){
            // This small image finds whitespace only; it is never enlarged for
            // display. The actual preview is vector or a fresh high-DPI crop.
            const auto size=page->pageSizeF();const qreal coarseScale=qMin(1100.0/qMax(1.0,size.width()),1400.0/qMax(1.0,size.height()));const auto pixels=page->renderToImage(coarseScale*72,coarseScale*72,QRect(),false).convertToFormat(QImage::Format_RGB32);
            int left=pixels.width(),right=-1,top=pixels.height(),bottom=-1;for(int y=0;y<pixels.height();++y){const auto *line=reinterpret_cast<const QRgb*>(pixels.constScanLine(y));for(int x=0;x<pixels.width();++x)if(qRed(line[x])<247||qGreen(line[x])<247||qBlue(line[x])<247){left=qMin(left,x);right=qMax(right,x);top=qMin(top,y);bottom=qMax(bottom,y);}}
            if(right>=left){const auto crop=QRect(QPoint(left,top),QPoint(right,bottom)).adjusted(-12,-12,12,12).intersected(pixels.rect());bounds=QRectF(crop.x()/coarseScale,crop.y()/coarseScale,crop.width()/coarseScale,crop.height()/coarseScale);vector=draftVector(vectorProgram,pdf,folder.path(),token);
              // A bounded raster fallback also renders the crop directly at
              // twice the current physical pixel size, rather than scaling a
              // low-resolution full-page screenshot.
              const qreal scale=qMax(.5,qMin(qMin(targetPixels.width()/bounds.width(),targetPixels.height()/bounds.height()),qMin(4096.0/bounds.width(),4096.0/bounds.height())));rendered=page->renderToImage(scale*72,scale*72,QRect(qFloor(bounds.x()*scale),qFloor(bounds.y()*scale),qCeil(bounds.width()*scale),qCeil(bounds.height()*scale)),false);
            }
          }
        }
      }
      if(qApp)QMetaObject::invokeMethod(qApp,[guard,token,ticket,rendered,vector,bounds,error,log,steps]{if(!guard)return;guard->running=false;if(guard->pending){auto next=std::move(guard->pending);guard->pending={};next();return;}if(token->load()||ticket!=guard->generation)return;guard->trigger->setEnabled(true);guard->pages=steps;const bool isVector=guard->image->setVector(vector,bounds);const bool ready=isVector||!rendered.isNull()||!steps.isEmpty();guard->image->setProperty("researchDraftReady",ready);guard->retry(!ready);if(!ready){guard->image->reset(words("无法生成草稿预览","Draft preview unavailable"));guard->notice->setText(localizedSlideExportError(error,englishUi()));guard->notice->setToolTip(log.right(12000));}else{if(!steps.isEmpty()){guard->showPage(0);if(guard->pagesReady)guard->pagesReady(steps.size());}else if(!isVector)guard->image->setImage(rendered);guard->notice->setText(guard->image->property("researchQuietPreview").toBool()?QString():words("草稿预览","Draft preview"));guard->notice->setToolTip(words("编号与浮动位置以全文编译为准","Numbering and placement follow the full document"));}},Qt::QueuedConnection);
    }));
  }
};
}

QWidget *formulaTrialPreview(QWidget *parent,TeXDocumentWindow *window,QTextEdit *trial) {
  auto *pane=new QWidget(parent);auto *layout=new QVBoxLayout(pane);layout->setContentsMargins(0,0,0,0);layout->setSpacing(6);
  auto *toggle=new QPushButton(words("公式效果","Formula preview"),pane);toggle->setObjectName(QStringLiteral("shorthandFormulaToggle"));toggle->setCheckable(true);toggle->setFlat(true);layout->addWidget(toggle,0,Qt::AlignLeft);
  auto *image=new PreviewImage(pane);image->setObjectName(QStringLiteral("shorthandFormulaPreview"));image->setFixedHeight(120);image->setAlignment(Qt::AlignCenter);layout->addWidget(image);image->hide();
  auto *notice=new QLabel(pane);notice->setObjectName(QStringLiteral("shorthandFormulaNotice"));notice->setWordWrap(true);layout->addWidget(notice);notice->hide();
  auto *renderer=new DraftPreview(image,notice,toggle);auto *delay=new QTimer(pane);delay->setSingleShot(true);delay->setInterval(500);
  const QPointer<TeXDocumentWindow> owner(window);
  QObject::connect(delay,&QTimer::timeout,pane,[=]{if(toggle->isChecked()&&owner)renderer->start(owner,trial->toPlainText(),{QStringLiteral("amsmath"),QStringLiteral("amssymb")});});
  QObject::connect(toggle,&QPushButton::toggled,pane,[=](bool on){image->setVisible(on);notice->setVisible(on);if(on)delay->start();else {delay->stop();renderer->invalidate();}});
  QObject::connect(trial,&QTextEdit::textChanged,pane,[=]{renderer->invalidate();if(toggle->isChecked())delay->start();});
  return pane;
}

AuthoringSpan authoringObjectAt(const QString &source,int position) {
  const auto clean=maskProjectText(source);const QRegularExpression re(QStringLiteral(R"(\\(begin|end)\s*\{([^{}]+)\})"));auto it=re.globalMatch(clean);QVector<AuthoringSpan> stack;QVector<AuthoringSpan> found;
  while(it.hasNext()){const auto m=it.next();const auto kind=m.captured(2);if(m.captured(1)==QStringLiteral("begin"))stack.append({kind,int(m.capturedStart()),-1});else{if(stack.isEmpty()||stack.last().kind!=kind){stack.clear();continue;}auto span=stack.takeLast();span.end=int(m.capturedEnd());if(position>=span.start&&position<=span.end&&!objectFamily(kind).isEmpty())found.append(span);}}
  // Prefer the enclosing logical object over its drawing or cell structure.
  std::sort(found.begin(),found.end(),[](const auto &a,const auto &b){return a.end-a.start>b.end-b.start;});for(const auto &s:found)if(objectFamily(s.kind)==QStringLiteral("figure")||objectFamily(s.kind)==QStringLiteral("table"))return s;
  const auto images=QRegularExpression(QStringLiteral(R"(\\includegraphics\s*(?:\[[^\]]*\])?\s*\{[^{}]+\})"));auto image=images.globalMatch(clean);while(image.hasNext()){const auto m=image.next();if(position>=m.capturedStart()&&position<=m.capturedEnd())return {QStringLiteral("includegraphics"),int(m.capturedStart()),int(m.capturedEnd())};}
  return {};
}
SimpleTable parseSimpleTable(const QString &source) {
  SimpleTable table;const auto clean=maskProjectText(source);
  const auto begin=QRegularExpression(QStringLiteral(R"(\\begin\s*\{tabular\}\s*\{([lcr| ]+)\})")).match(clean);
  if(!begin.hasMatch()||QRegularExpression(QStringLiteral(R"(\\(?:multicolumn|multirow|cline|cmidrule|hhline|specialrule|rowcolor|cellcolor|input|newcommand|def)\b|\\begin\s*\{(?:tabular\*|longtable|tabularx|tabularray|tblr)\})")).match(clean).hasMatch())return table;
  const auto end=QRegularExpression(QStringLiteral(R"(\\end\s*\{tabular\})")).match(clean,begin.capturedEnd());if(!end.hasMatch())return table;
  table.start=int(begin.capturedStart());table.end=int(end.capturedEnd());table.columnsStart=int(begin.capturedStart(1));table.columnsEnd=int(begin.capturedEnd(1));
  table.columnSpec=begin.captured(1);for(const auto ch:table.columnSpec)if(ch!=QLatin1Char(' ')&&ch!=QLatin1Char('|'))table.alignment.append(QString(ch));if(table.alignment.isEmpty()||table.alignment.size()>40)return table;
  const int body=int(begin.capturedEnd());const QString cells=source.mid(body,end.capturedStart()-body),syntax=maskProjectText(cells);
  // These layouts retain their original source. Escaped percentages are data,
  // whereas comments and nested environments need a richer table model.
  for(int i=0;i<cells.size();++i)if(cells[i]==QLatin1Char('%')&&!escaped(cells,i))return table;
  if(QRegularExpression(QStringLiteral(R"(\\begin\s*\{)")).match(syntax).hasMatch())return table;
  static const QRegularExpression rule(QStringLiteral(R"(\\(?:toprule|midrule|bottomrule|hline)\b)"));
  QStringList row;QVector<QPair<int,int>> spans;QString pendingRule;int depth=0,first=0;bool math=false;
  auto addCell=[&](int last){int a=first,b=last;while(a<b&&cells[a].isSpace())++a;while(b>a&&cells[b-1].isSpace())--b;row.append(cells.mid(a,b-a));spans.append({body+a,body+b});};
  auto addRow=[&]{if(row.size()!=table.alignment.size())return false;table.rows.append(row);table.cells.append(spans);table.rules.append(pendingRule);row.clear();spans.clear();pendingRule.clear();return table.rows.size()<=200;};
  for(int i=0;i<syntax.size();++i){const auto ch=syntax[i];if(escaped(syntax,i))continue;
    if(ch==QLatin1Char('{'))++depth;else if(ch==QLatin1Char('}')&&--depth<0)return table;else if(ch==QLatin1Char('$'))math=!math;
    if(depth||math)continue;
    if(ch==QLatin1Char('\\')){
      const auto separator=rule.match(syntax,i,QRegularExpression::NormalMatch,QRegularExpression::AnchorAtOffsetMatchOption);
      if(separator.hasMatch()){
        if(!row.isEmpty()||!cells.mid(first,i-first).trimmed().isEmpty())return table;
        int next=int(separator.capturedEnd());while(next<syntax.size()&&syntax[next].isSpace())++next;
        if(next<syntax.size()&&syntax[next]==QLatin1Char('['))return table;
        pendingRule+=cells.mid(i,separator.capturedLength())+QLatin1Char('\n');i=int(separator.capturedEnd())-1;first=i+1;continue;
      }
      if(i+1<syntax.size()&&syntax[i+1]==QLatin1Char('\\')){
        addCell(i);if(!addRow())return table;++i;first=i+1;int next=first;while(next<syntax.size()&&syntax[next].isSpace())++next;
        if(next<syntax.size()&&(syntax[next]==QLatin1Char('[')||syntax[next]==QLatin1Char('*')))return table;continue;
      }
    }
    if(ch==QLatin1Char('&')){addCell(i);first=i+1;}
  }
  if(depth||math)return table;
  if(!cells.mid(first).trimmed().isEmpty()||!row.isEmpty()){addCell(int(cells.size()));if(!addRow())return table;}
  table.rules.append(pendingRule);
  table.booktabs=QRegularExpression(QStringLiteral(R"(\\(?:toprule|midrule|bottomrule)\b)")).match(table.rules.join(QString())).hasMatch();
  table.header=table.rules.size()>1&&QRegularExpression(QStringLiteral(R"(\\(?:midrule|hline)\b)")).match(table.rules[1]).hasMatch();
  table.valid=!table.rows.isEmpty();return table;
}
namespace {
QString tableColumns(const SimpleTable &table){QString columns=table.columnSpec;int count=0;for(auto &ch:columns)if(ch==QLatin1Char('l')||ch==QLatin1Char('c')||ch==QLatin1Char('r')){if(count<table.alignment.size())ch=table.alignment[count][0];++count;}if(count!=table.alignment.size())columns=table.columnSpec.contains(QLatin1Char('|'))?QLatin1Char('|')+table.alignment.join(QLatin1Char('|'))+QLatin1Char('|'):table.alignment.join(QString());return columns;}
QString spreadsheetCell(const QString &text){
  QString out;for(const auto ch:text){if(ch==QLatin1Char('\\'))out+=QStringLiteral("\\textbackslash{}");else if(ch==QLatin1Char('~'))out+=QStringLiteral("\\textasciitilde{}");else if(ch==QLatin1Char('^'))out+=QStringLiteral("\\textasciicircum{}");else{if(QStringLiteral("&%$#_{}").contains(ch))out+=QLatin1Char('\\');out+=ch;}}return out;
}
}
QString simpleTableSource(const SimpleTable &table) {
  if(!table.valid||table.alignment.isEmpty()||table.rows.isEmpty())return {};
  for(const auto &column:table.alignment)if(column!=QStringLiteral("l")&&column!=QStringLiteral("c")&&column!=QStringLiteral("r"))return {};
  for(const auto &row:table.rows)if(row.size()!=table.alignment.size())return {};
  QStringList rules=table.rules;if(rules.size()!=table.rows.size()+1)rules=QStringList(table.rows.size()+1,QString());
  static const QRegularExpression headerRule(QStringLiteral(R"(\\(?:midrule|hline)\b)"));
  const bool previousHeader=rules.size()>1&&headerRule.match(rules[1]).hasMatch();
  if(table.header!=previousHeader){rules[1].remove(headerRule);if(table.header)rules[1]=(table.booktabs?QStringLiteral("\\midrule\n"):QStringLiteral("\\hline\n"))+rules[1];}
  if(table.header&&!previousHeader){if(rules.first().trimmed().isEmpty())rules.first()=table.booktabs?QStringLiteral("\\toprule\n"):QStringLiteral("\\hline\n");if(rules.last().trimmed().isEmpty())rules.last()=table.booktabs?QStringLiteral("\\bottomrule\n"):QStringLiteral("\\hline\n");}
  QString out=QStringLiteral("\\begin{tabular}{")+tableColumns(table)+QStringLiteral("}\n");
  for(int r=0;r<table.rows.size();++r)out+=rules[r]+table.rows[r].join(QStringLiteral(" & "))+QStringLiteral(" \\\\\n");
  return out+rules.last()+QStringLiteral("\\end{tabular}");
}
QString citationSource(const QString &cmd,const QString &key,const QString &locator) {
  static const QSet<QString> commands{QStringLiteral("cite"),QStringLiteral("citep"),QStringLiteral("citet"),QStringLiteral("citealp"),QStringLiteral("citealt"),QStringLiteral("autocite"),QStringLiteral("parencite"),QStringLiteral("textcite"),QStringLiteral("footcite")};
  auto base=cmd;if(base.endsWith(QLatin1Char('*')))base.chop(1);bool validKeys=!key.isEmpty();for(const auto &part:key.split(QLatin1Char(',')))validKeys&=literalKey(part.trimmed());
  if(!commands.contains(base)||!validKeys||!balancedText(locator)||locator.contains(QLatin1Char(']'))||locator.contains(QLatin1Char('['))||locator.contains(QLatin1Char('%')))return {};
  return QLatin1Char('\\')+cmd+(locator.isEmpty()?QString():QLatin1Char('[')+locator+QLatin1Char(']'))+QLatin1Char('{')+key+QLatin1Char('}');
}

namespace {
struct AssetImport {
  QString file,directory;QByteArray hash;
  AssetImport()=default;AssetImport(const AssetImport &)=delete;AssetImport &operator=(const AssetImport &)=delete;
  void keep(){file.clear();directory.clear();}
  ~AssetImport(){if(!file.isEmpty()&&slideExportHash(file)==hash)QFile::remove(file);if(!directory.isEmpty())QDir().rmdir(directory);}
};
QString assetReference(const QString &file,const QString &root,QLabel *notice,AssetImport &imported,const QByteArray &expected) {
  const auto info=QFileInfo(file);if(!info.isFile()||info.size()>64*1024*1024){notice->setText(words("图片不可读取或超过 64 MB。","The image is unreadable or larger than 64 MB."));return {};}
  const auto suffix=info.suffix().toLower();if(!QStringList{QStringLiteral("pdf"),QStringLiteral("png"),QStringLiteral("jpg"),QStringLiteral("jpeg")}.contains(suffix)){notice->setText(words("请选择 PDF、PNG 或 JPEG 图片。","Choose a PDF, PNG or JPEG image."));return {};}
  const auto folder=QFileInfo(root).absoluteDir();const auto relative=folder.relativeFilePath(info.canonicalFilePath());
  if(!relative.startsWith(QStringLiteral("../"))&&!QDir::isAbsolutePath(relative)&&!relative.contains(QRegularExpression(QStringLiteral(R"([{}%#\\])"))))return relative;
  const auto hash=expected;if(hash.size()!=32)return {};
  const auto name=QStringLiteral("figures/figure-")+QString::fromLatin1(hash.toHex().left(16))+QLatin1Char('.')+suffix;const auto dest=folder.filePath(name);
  if(QFileInfo::exists(dest)){if(slideExportHash(dest)==hash)return name;notice->setText(words("资源重名，请核对 figures 文件夹。","An asset name conflicts. Check the figures folder."));return {};}
  const QString directory=QFileInfo(dest).absolutePath();const bool existed=QFileInfo::exists(directory);
  const auto project=folder.canonicalPath();const auto existingDirectory=QDir(directory).canonicalPath();
  if(existed&&(existingDirectory.isEmpty()||!existingDirectory.startsWith(project+QLatin1Char('/'),Qt::CaseInsensitive))){notice->setText(words("figures 指向项目外部，请先选择项目内的资源目录或编辑源码。","The figures folder points outside this project. Use a local resource folder or edit the source."));return {};}
  if(!QDir().mkpath(directory)){notice->setText(words("无法创建图片资源目录。","Cannot create the image resource folder."));return {};}
  if(!existed)imported.directory=directory;
  QTemporaryFile stage(QDir(directory).filePath(QStringLiteral(".texline-import-XXXXXX")));QFile input(file);QCryptographicHash copied(QCryptographicHash::Sha256);qint64 bytes=0;
  bool ready=stage.open()&&input.open(QIODevice::ReadOnly);
  while(ready&&!input.atEnd()){const auto chunk=input.read(256*1024);bytes+=chunk.size();ready=!chunk.isEmpty()&&bytes<=64*1024*1024&&stage.write(chunk)==chunk.size();if(ready)copied.addData(chunk);}
  ready=ready&&input.error()==QFile::NoError&&stage.flush()&&copied.result()==hash;input.close();stage.close();
  if(!ready){notice->setText(words("图片读取失败或已变化，请重新选择。","The image could not be read or changed. Choose it again."));return {};}
  // Rename a validated temporary file without overwriting a concurrent asset.
  if(!stage.rename(dest)){if(slideExportHash(dest)==hash)return name;notice->setText(words("无法复制图片资源。","Cannot copy the image asset."));return {};}
  stage.setAutoRemove(false);imported.file=dest;imported.hash=hash;
  return name;
}
struct FigureFields {bool valid{false},clip{false};Command image;QString file,caption,label,position;double percent{90};QList<double> trim{0,0,0,0};};
FigureFields parseFigure(const QString &text) {
  FigureFields result;const auto clean=maskProjectText(text);
  const auto m=QRegularExpression(QStringLiteral(R"(\\includegraphics\s*(?:\[([^\]]*)\])?\s*\{([^{}]+)\})")).match(clean);
  if(!m.hasMatch()||QRegularExpression(QStringLiteral(R"(\\includegraphics\b)")).match(clean,m.capturedEnd()).hasMatch())return result;
  // Preserve uncommon layout commands through source editing rather than
  // normalizing them into the small form.
  auto remainder=clean;remainder.remove(m.capturedStart(),m.capturedLength());
  for(const auto &cmd:{QStringLiteral("caption"),QStringLiteral("label")}){const auto c=command(remainder,cmd);if(c.valid())remainder.remove(c.start,c.end-c.start);}
  remainder.remove(QRegularExpression(QStringLiteral(R"(\\(?:begin|end)\s*\{figure\*?\}\s*(?:\[[^\]]*\])?|\\(?:centering|raggedright|raggedleft)\b)")));
  if(!remainder.trimmed().isEmpty())return result;
  result.percent=0;for(const auto &raw:m.captured(1).split(QLatin1Char(','),Qt::SkipEmptyParts)){const auto option=raw.trimmed();const auto width=QRegularExpression(QStringLiteral(R"(^width\s*=\s*([0-9.]+)\\linewidth$)")).match(option);const auto crop=QRegularExpression(QStringLiteral(R"(^trim\s*=\s*([0-9.]+)bp\s+([0-9.]+)bp\s+([0-9.]+)bp\s+([0-9.]+)bp$)")).match(option);if(width.hasMatch()){result.percent=width.captured(1).toDouble()*100;if(result.percent<=0||result.percent>100)return FigureFields{};}else if(crop.hasMatch()){for(int i=0;i<4;++i)result.trim[i]=crop.captured(i+1).toDouble();}else if(option==QStringLiteral("clip"))result.clip=true;else return FigureFields{};}
  result.valid=true;result.file=text.mid(m.capturedStart(2),m.capturedLength(2));result.caption=command(text,QStringLiteral("caption")).value;result.label=command(text,QStringLiteral("label")).value;
  result.image={int(m.capturedStart()),int(m.capturedEnd()),int(m.capturedStart(2)),int(m.capturedEnd(2)),result.file};
  result.position=QRegularExpression(QStringLiteral(R"(\\begin\s*\{figure\*?\}\s*\[([^\]]*)\])")).match(clean).captured(1);return result;
}

#include "ResearchTableClipboard.inc"
QString previewAssetReference(const QString &,const QByteArray &,QSharedPointer<QTemporaryDir> &,QLabel *);
#include "ResearchVisualAuthoring.inc"

QString previewAssetReference(const QString &file,const QByteArray &expected,QSharedPointer<QTemporaryDir> &cache,QLabel *notice) {
  QString path=QDir::fromNativeSeparators(file);
  if(path.contains(QRegularExpression(QStringLiteral(R"([{}%#])")))) {
    if(!cache)cache=QSharedPointer<QTemporaryDir>::create(QDir::tempPath()+QStringLiteral("/texline-assets-XXXXXX"));
    if(!cache->isValid()){notice->setText(words("无法准备图片预览，请重试。","Could not prepare the image preview. Retry."));return {};}
    AssetImport imported;const auto name=assetReference(file,cache->filePath(QStringLiteral("main.tex")),notice,imported,expected);
    if(name.isEmpty())return {};imported.keep();path=QDir::fromNativeSeparators(cache->filePath(name));
    if(path.contains(QRegularExpression(QStringLiteral(R"([{}%#])")))){notice->setText(words("临时目录路径无法用于图片预览。","The temporary directory path cannot be used for image preview."));return {};}
  }
  return QStringLiteral("\\detokenize{")+path+QLatin1Char('}');
}

void openTikz(TeXDocumentWindow *,bool,const AuthoringSpan &,const QString &seed=QString(),const SourceEdit *inherited=nullptr);

class ObjectDialog final : public MaterialDialog {
  SourceEdit edit;QString originalDraft,baseDraft,chosenAsset,assetOriginal;QByteArray assetHash;QStringList packages;bool tableMode{false},newObject{false},syncing{false},sourceOnly{false};
  QSharedPointer<QTemporaryDir> clipboardImage,previewAssets;
  QTabWidget *tabs;QWidget *form;QPlainTextEdit *source;QLabel *notice;PreviewImage *image;QLineEdit *caption,*label;QComboBox *origin,*position;QDoubleSpinBox *width;QList<QDoubleSpinBox*> trim;QCheckBox *clip;QComboBox *figureAlignment;QTableWidget *grid{nullptr};QCheckBox *header{nullptr};QComboBox *alignment{nullptr};QPushButton *preview;DraftPreview *renderer{nullptr};SimpleTable initialTable,baseTable;
  QSplitter *layoutSplit{nullptr},*sourceSplit{nullptr};QWidget *previewPane{nullptr},*tableMeta{nullptr},*tableTools{nullptr};QToolButton *previewToggle{nullptr};QPlainTextEdit *figureCaption{nullptr};QTimer previewDelay;QWidget *tabHeader{nullptr};QHBoxLayout *tabRow{nullptr};QToolButton *layoutTab{nullptr},*sourceTab{nullptr};int shownTab{-1};bool compactBefore{false};QList<int> layoutSizes,sourceSizes;
  QString text()const{return source->toPlainText();}
  QString captionText()const{return figureCaption?figureCaption->toPlainText():caption->text();}
  void refreshPreview(){previewDelay.stop();if(!edit.target||!isVisible()||!previewToggle->isChecked())return;if(newObject&&!tableMode&&origin->currentIndex()==0&&text().contains(QStringLiteral("__TEXLINE_DRAFT_IMAGE__"))&&assetHash.isEmpty()){image->reset(words("选择图片，或切换为绘图（TikZ）","Choose an image, or switch to Drawing (TikZ)"));notice->clear();return;}const auto draft=resolvedDraft(false);if(!draft.isEmpty())renderer->start(edit.target,draft,draftPackages(draft),false,previewAssets);}
  void schedulePreview(){renderer->invalidate(false);if(isVisible()&&previewToggle&&previewToggle->isChecked())previewDelay.start();}
  void changed(const QString &draft){syncing=true;source->setPlainText(draft);syncing=false;schedulePreview();}
  void selectTab(int index){if(tableMode&&QWidget::width()<740)previewToggle->setChecked(false);tabs->setCurrentIndex(index);updateWorkbench();}
  void updateWorkbench(){
    if(!layoutSplit||!sourceSplit||!previewPane||!previewToggle)return;layoutTab->setChecked(tabs->currentIndex()==0);sourceTab->setChecked(tabs->currentIndex()==1);auto *active=tabs->currentIndex()==0?layoutSplit:sourceSplit;
    auto &saved=active==layoutSplit?layoutSizes:sourceSizes;bool restore=false;
    if(previewPane->parentWidget()!=active){if(auto *previous=qobject_cast<QSplitter*>(previewPane->parentWidget())){const auto sizes=previous->sizes();if(sizes.size()==2&&sizes[0]>0&&sizes[1]>0)(previous==layoutSplit?layoutSizes:sourceSizes)=sizes;}previewPane->setParent(nullptr);active->insertWidget(tableMode?active->count():0,previewPane);restore=true;}
    const int available=QWidget::width();const bool compact=available<740;
    // A tab names the editor it reveals. In a compact table, choosing a tab
    // returns to that editor; the explicit Preview toggle can replace it.
    if(tableMode&&compact&&(shownTab!=tabs->currentIndex()||!compactBefore)){QSignalBlocker block(previewToggle);previewToggle->setChecked(false);previewDelay.stop();if(renderer)renderer->invalidate(false);}
    shownTab=tabs->currentIndex();compactBefore=compact;const bool show=previewToggle->isChecked();setProperty("researchAuthoringCompact",compact);
    const auto sizes=active->sizes();if(!show&&sizes.size()==2&&sizes[0]>0&&sizes[1]>0)saved=sizes;
    if(show&&previewPane->isHidden())restore=true;previewPane->setVisible(show);if(tableMeta)tableMeta->setVisible(tableMode&&tabs->currentIndex()==0);
    if(tableTools){tableTools->setVisible(!sourceOnly&&tabs->currentIndex()==0);tableTools->setEnabled(!(compact&&show));}
    if(tableMode&&compact){if(tabs->currentIndex()==0)form->setVisible(!show);else source->setVisible(!show);}else {form->show();source->show();}
    if(active->count()==2){if(restore&&!saved.isEmpty())active->setSizes(saved);else if(!active->property("researchAuthoringSized").toBool())active->setSizes(tableMode?QList<int>{available/2,available/2}:QList<int>{available*3/5,available*2/5});active->setStretchFactor(0,1);active->setStretchFactor(1,1);active->setProperty("researchAuthoringSized",true);}
  }
  void populateFigure() {
    auto layout=new QVBoxLayout(form);layout->setContentsMargins(16,14,8,12);layout->setSpacing(16);auto *fields=new QFormLayout;fields->setSpacing(14);layout->addLayout(fields);origin=named(new QComboBox(form),"researchFigureOrigin");origin->addItems({words("图片文件","Image file"),words("绘图（TikZ）","Drawing (TikZ)")});origin->setMaximumWidth(150);
    auto *fileRow=new QHBoxLayout;auto *fileName=named(new QLabel(form),"researchFigureFile");fileName->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);fileName->setMinimumWidth(0);fileRow->addWidget(origin);fileRow->addWidget(fileName,1);auto *choose=textButton(form,"researchFigureChoose",(newObject?words("选择…","Choose…"):words("替换…","Replace…")));fileRow->addWidget(choose);fields->addRow(words("内容","Content"),fileRow);
    width=named(new PercentField(form),"researchFigureWidth");width->setDecimals(3);width->setRange(0,100);width->setSpecialValueText(words("原始尺寸","Natural size"));width->setSuffix(QStringLiteral(" %"));width->setMaximumWidth(100);auto *widthRow=new QHBoxLayout;auto *slider=named(new QSlider(Qt::Horizontal,form),"researchFigureWidthSlider");slider->setRange(5,100);slider->setSingleStep(1);widthRow->addWidget(slider,1);widthRow->addWidget(width);fields->addRow(words("宽度","Width"),widthRow);width->setToolTip(words("相对于当前栏，保持原始比例","Relative to the current column, preserving aspect ratio"));connect(slider,&QSlider::valueChanged,this,[this](int v){width->setValue(v);});connect(width,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[slider](double v){QSignalBlocker block(slider);slider->setValue(qRound(v));});
    figureCaption=named(new QPlainTextEdit(form),"researchObjectCaption");figureCaption->setMinimumHeight(76);figureCaption->setMaximumHeight(96);figureCaption->setFont(smoothTextFont(QFont(QStringLiteral("Segoe UI"),10)));figureCaption->setTabChangesFocus(true);fields->addRow(words("图注","Caption"),figureCaption);
    auto parsed=parseFigure(baseDraft);const bool drawing=baseDraft.contains(QStringLiteral("\\begin{tikzpicture}"));origin->setCurrentIndex(drawing?1:0);
    chosenAsset=parsed.file.isEmpty()?QString():QFileInfo(QDir(QFileInfo(rootOf(edit.target)).absolutePath()).filePath(parsed.file)).absoluteFilePath();assetOriginal=chosenAsset;assetHash=slideExportHash(chosenAsset);fileName->setText(QFileInfo(parsed.file).fileName());fileName->setToolTip(chosenAsset);width->setValue(parsed.percent);slider->setValue(qRound(parsed.percent));figureCaption->setPlainText(parsed.caption);
    addAdvanced(layout,parsed.label,parsed.position);
    auto *advanced=findChild<QWidget*>(QStringLiteral("researchObjectAdvanced"));auto *advancedFields=qobject_cast<QFormLayout*>(advanced->layout());figureAlignment=named(new QComboBox(advanced),"researchFigureAlignment");figureAlignment->addItems({words("保留源码设置","Keep source setting"),words("居中","Center"),words("靠左","Left"),words("靠右","Right")});advancedFields->addRow(words("对齐","Alignment"),figureAlignment);auto *cropWidget=new QWidget(advanced);auto *crop=new QGridLayout(cropWidget);crop->setContentsMargins(0,0,0,0);const QStringList edges{words("左","Left"),words("下","Bottom"),words("右","Right"),words("上","Top")};for(int i=0;i<4;++i){auto *value=new QDoubleSpinBox(advanced);value->setObjectName(QStringLiteral("researchFigureTrim%1").arg(i));value->setRange(0,1000);value->setDecimals(2);value->setSuffix(QStringLiteral(" bp"));value->setValue(parsed.trim[i]);value->setAccessibleName(edges[i]);value->setToolTip(edges[i]);trim.append(value);crop->addWidget(new QLabel(edges[i],cropWidget),i/2,(i%2)*2);crop->addWidget(value,i/2,(i%2)*2+1);}advancedFields->setRowWrapPolicy(QFormLayout::WrapLongRows);advancedFields->addRow(words("裁边","Trim"),cropWidget);clip=named(new QCheckBox(words("隐藏裁边外的内容","Clip outside the trim"),advanced),"researchFigureClip");clip->setChecked(parsed.clip);advancedFields->addRow(QString(),clip);
    const double displayedWidth=width->value();QList<double> displayedTrim;for(auto *v:trim)displayedTrim.append(v->value());
    auto update=[this,parsed,displayedWidth,displayedTrim]{if(sourceOnly)return;QString draft=baseDraft;
      if(origin->currentIndex()==1){draft=QStringLiteral("\\begin{figure}[htbp]\n\\centering\n\\begin{tikzpicture}\n  \\draw[->] (0,0) -- (3,0);\n  \\draw[->] (0,0) -- (0,2);\n\\end{tikzpicture}\n\\end{figure}");packages={QStringLiteral("tikz")};}
      else {packages={QStringLiteral("graphicx")};const auto current=parseFigure(draft);QString path=chosenAsset==assetOriginal?current.file:QStringLiteral("__TEXLINE_DRAFT_IMAGE__");if(path.isEmpty())path=QStringLiteral("__TEXLINE_DRAFT_IMAGE__");QStringList options,crop;bool cropped=false,unchanged=qAbs(width->value()-displayedWidth)<0.00001&&clip->isChecked()==parsed.clip;for(int i=0;i<4;++i){crop.append(QString::number(qAbs(trim[i]->value()-displayedTrim[i])<0.00001?parsed.trim[i]:trim[i]->value(),'g',12)+QStringLiteral("bp"));cropped|=trim[i]->value()>0;unchanged&=qAbs(trim[i]->value()-displayedTrim[i])<0.00001;}if(width->value()>0){const auto originalWidth=QRegularExpression(QStringLiteral(R"(width\s*=\s*[0-9.]+\\linewidth)")).match(baseDraft);options.append(qAbs(width->value()-displayedWidth)<0.00001&&originalWidth.hasMatch()?originalWidth.captured():QStringLiteral("width=%1\\linewidth").arg(QString::number(width->value()/100.0,'g',10)));}if(cropped)options.append(QStringLiteral("trim=")+crop.join(QLatin1Char(' ')));if(clip->isChecked())options.append(QStringLiteral("clip"));QString commandText=QStringLiteral("\\includegraphics")+(options.isEmpty()?QString():QLatin1Char('[')+options.join(QLatin1Char(','))+QLatin1Char(']'))+QLatin1Char('{')+path+QLatin1Char('}');if(current.valid&&unchanged){commandText=baseDraft.mid(current.image.start,current.image.end-current.image.start);commandText.replace(current.image.valueStart-current.image.start,current.image.valueEnd-current.image.valueStart,path);}if(current.valid)draft.replace(current.image.start,current.image.end-current.image.start,commandText);else draft=QStringLiteral("\\begin{figure}[htbp]\n\\centering\n")+commandText+QStringLiteral("\n\\end{figure}");}
      if(!QRegularExpression(QStringLiteral(R"(\\begin\s*\{figure\*?\})")).match(maskProjectText(draft)).hasMatch()&&(!captionText().isEmpty()||!label->text().isEmpty()||position->currentIndex()!=0))draft=QStringLiteral("\\begin{figure}[htbp]\n\\centering\n")+draft+QStringLiteral("\n\\end{figure}");
      if(figureAlignment->currentIndex()){draft.remove(QRegularExpression(QStringLiteral(R"(\\(?:centering|raggedright|raggedleft)\b\s*)")));const auto start=QRegularExpression(QStringLiteral(R"(\\begin\s*\{figure\*?\}\s*(?:\[[^\]]*\])?)")).match(draft);if(start.hasMatch())draft.insert(start.capturedEnd(),QStringLiteral("\n\\")+(figureAlignment->currentIndex()==1?QStringLiteral("centering"):figureAlignment->currentIndex()==2?QStringLiteral("raggedright"):QStringLiteral("raggedleft"))+QLatin1Char('\n'));}
      draft=setCommand(draft,QStringLiteral("caption"),captionText());draft=setCommand(draft,QStringLiteral("label"),label->text());
      if(!position->currentData().toString().isEmpty())draft.replace(QRegularExpression(QStringLiteral(R"((\\begin\s*\{figure\*?\})\s*(?:\[[^\]]*\])?)")),QStringLiteral("\\1[")+position->currentData().toString()+QLatin1Char(']'));
      changed(draft);};
    connect(width,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[update]{update();});connect(figureCaption,&QPlainTextEdit::textChanged,this,[update]{update();});connect(label,&QLineEdit::textChanged,this,[update]{update();});connect(position,qOverload<int>(&QComboBox::currentIndexChanged),this,[update]{update();});connect(figureAlignment,qOverload<int>(&QComboBox::currentIndexChanged),this,[update]{update();});connect(clip,&QCheckBox::toggled,this,[update]{update();});for(auto *value:trim)connect(value,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this,update]{if(std::any_of(trim.begin(),trim.end(),[](auto *v){return v->value()>0;})){QSignalBlocker b(clip);clip->setChecked(true);}update();});
    connect(origin,qOverload<int>(&QComboBox::currentIndexChanged),this,[this,choose,fileName,update](int at){if(at==1){const auto target=edit.target;const SourceEdit snapshot=edit;const bool insert=newObject;const AuthoringSpan span{QStringLiteral("figure"),edit.start,edit.end};QString draft=QStringLiteral("\\begin{figure}[htbp]\n\\centering\n")+Tikz::emptyPicture()+QStringLiteral("\n\\end{figure}");draft=setCommand(draft,QStringLiteral("caption"),captionText());draft=setCommand(draft,QStringLiteral("label"),label->text());reject();if(target)openTikz(target,insert,span,draft,&snapshot);return;}choose->setEnabled(true);fileName->setEnabled(true);width->setEnabled(true);update();});
    connect(choose,&QToolButton::clicked,this,[this,fileName,update]{QPointer<ObjectDialog> guard(this);const auto selected=QFileDialog::getOpenFileName(this,words("选择图片","Choose image"),QFileInfo(rootOf(edit.target)).absolutePath(),QStringLiteral("Images (*.pdf *.png *.jpg *.jpeg)"));if(!guard||selected.isEmpty())return;chosenAsset=selected;assetHash=slideExportHash(selected);fileName->setText(QFileInfo(selected).fileName());fileName->setToolTip(selected);update();showAsset();});
    if(!newObject&&!parsed.valid){sourceOnly=true;form->setEnabled(false);tabs->setCurrentIndex(1);notice->setText(words("此图含自定义排版，请直接编辑源码并预览。","This figure uses custom layout. Edit its source and preview."));}
    layout->addStretch();if(drawing)packages={QStringLiteral("tikz")};else packages={QStringLiteral("graphicx")};showAsset();
  }
  void showAsset(){if(chosenAsset.isEmpty())return;QImageReader reader(chosenAsset);reader.setAutoTransform(true);if(reader.size().isValid()){reader.setScaledSize(reader.size().scaled(720,480,Qt::KeepAspectRatio));const auto result=reader.read();if(!result.isNull())image->setImage(result);}}
  void addAdvanced(QVBoxLayout *layout,const QString &key,const QString &floating){
    auto *parent=layout->parentWidget();auto *more=textButton(parent,"researchObjectAdvancedToggle",words("更多","More"));more->setCheckable(true);layout->addWidget(more,0,Qt::AlignLeft);auto *advanced=named(new QWidget(parent),"researchObjectAdvanced");auto *fields=new QFormLayout(advanced);fields->setContentsMargins(0,0,0,0);label=field(advanced,"researchObjectLabel");label->setText(key);fields->addRow(words("标签","Label"),label);position=named(new QComboBox(advanced),"researchObjectPosition");position->addItem(words("保留源码设置","Keep source setting"),QString());position->addItem(words("此处优先","Prefer here"),QStringLiteral("htbp"));position->addItem(words("页顶优先","Prefer page top"),QStringLiteral("tbp"));position->setToolTip(floating);fields->addRow(words("浮动位置","Placement"),position);layout->addWidget(advanced);advanced->hide();connect(more,&QToolButton::toggled,advanced,&QWidget::setVisible);
  }
  void populateTable() {
    auto *layout=new QVBoxLayout(form);baseTable=initialTable=parseSimpleTable(baseDraft);tableTools=new QWidget(tabHeader);auto *tools=new QHBoxLayout(tableTools);tools->setContentsMargins(0,0,0,0);tools->setSpacing(4);
    auto *rows=textButton(form,"researchTableRows",words("行","Rows"));auto *columns=textButton(form,"researchTableColumns",words("列","Columns"));auto *paste=textButton(form,"researchTablePaste",words("粘贴数据","Paste data"));
    for(auto *b:{rows,columns,paste})tools->addWidget(b);header=named(new QCheckBox(words("首行为表头","Header row"),form),"researchTableHeader");header->setChecked(initialTable.header);alignment=named(new QComboBox(form),"researchTableAlignment");alignment->addItem(words("左对齐","Left"),QStringLiteral("l"));alignment->addItem(words("居中","Center"),QStringLiteral("c"));alignment->addItem(words("右对齐","Right"),QStringLiteral("r"));tabRow->insertWidget(tabRow->count()-1,tableTools);
    grid=named(new ClipboardTable(form),"researchTableGrid");grid->setRowCount(initialTable.valid?int(initialTable.rows.size()):3);grid->setColumnCount(initialTable.valid?int(initialTable.alignment.size()):3);grid->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);grid->setSelectionMode(QAbstractItemView::ContiguousSelection);layout->addWidget(grid,1);auto *cellOptions=new QHBoxLayout;cellOptions->addWidget(header);cellOptions->addStretch();cellOptions->addWidget(new QLabel(words("对齐","Alignment"),form));cellOptions->addWidget(alignment);layout->addLayout(cellOptions);
    for(int r=0;r<grid->rowCount();++r)for(int c=0;c<grid->columnCount();++c)grid->setItem(r,c,new QTableWidgetItem(initialTable.valid?initialTable.rows[r][c]:QString()));
    grid->setToolTip(words("单元格支持 LaTeX；保留原始文字和数值。","Cells accept LaTeX; text and numeric strings are preserved."));
    auto *meta=new QVBoxLayout(tableMeta);meta->setContentsMargins(0,0,0,0);meta->setSpacing(8);auto *captionRow=new QFormLayout;caption=field(tableMeta,"researchObjectCaption");caption->setText(command(baseDraft,QStringLiteral("caption")).value);captionRow->addRow(words("表题","Caption"),caption);meta->addLayout(captionRow);addAdvanced(meta,command(baseDraft,QStringLiteral("label")).value,QString());
    auto update=[this]{if(sourceOnly||syncing)return;static_cast<ClipboardTable*>(grid)->record();SimpleTable table=initialTable;table.valid=true;table.header=header->isChecked();table.rows.clear();while(table.alignment.size()<grid->columnCount())table.alignment.append(QStringLiteral("l"));while(table.alignment.size()>grid->columnCount())table.alignment.removeLast();
      for(int r=0;r<grid->rowCount();++r){QStringList row;for(int c=0;c<grid->columnCount();++c)row.append(grid->item(r,c)?grid->item(r,c)->text():QString());table.rows.append(row);}QString draft=baseDraft;const auto &parsed=baseTable;
      if(parsed.valid&&table.rows.size()==parsed.rows.size()&&table.alignment.size()==parsed.alignment.size()&&table.header==parsed.header&&table.rules==parsed.rules){
        QVector<TextEdit> edits;for(int r=0;r<table.rows.size();++r)for(int c=0;c<table.alignment.size();++c)if(table.rows[r][c]!=parsed.rows[r][c])edits.append({parsed.cells[r][c].first,parsed.cells[r][c].second,table.rows[r][c]});
        if(table.alignment!=parsed.alignment)edits.append({parsed.columnsStart,parsed.columnsEnd,tableColumns(table)});
        std::sort(edits.begin(),edits.end(),[](const auto &a,const auto &b){return a.start>b.start;});for(const auto &change:edits)draft.replace(change.start,change.end-change.start,change.text);
      }else if(parsed.valid)draft.replace(parsed.start,parsed.end-parsed.start,simpleTableSource(table));else draft=QStringLiteral("\\begin{table}[htbp]\n\\centering\n")+simpleTableSource(table)+QStringLiteral("\n\\end{table}");
      if(!QRegularExpression(QStringLiteral(R"(\\begin\s*\{table\*?\})")).match(maskProjectText(draft)).hasMatch()&&(!caption->text().isEmpty()||!label->text().isEmpty()||position->currentIndex()!=0))draft=QStringLiteral("\\begin{table}[htbp]\n\\centering\n")+draft+QStringLiteral("\n\\end{table}");
      draft=setCommand(draft,QStringLiteral("caption"),caption->text());draft=setCommand(draft,QStringLiteral("label"),label->text());if(!position->currentData().toString().isEmpty())draft.replace(QRegularExpression(QStringLiteral(R"((\\begin\s*\{table\*?\})\s*(?:\[[^\]]*\])?)")),QStringLiteral("\\1[")+position->currentData().toString()+QLatin1Char(']'));changed(draft);};
    connect(grid,&QTableWidget::itemChanged,this,[update]{update();});connect(header,&QCheckBox::toggled,this,[update]{update();});connect(position,qOverload<int>(&QComboBox::currentIndexChanged),this,[update]{update();});connect(caption,&QLineEdit::textChanged,this,[update]{update();});connect(label,&QLineEdit::textChanged,this,[update]{update();});
    connect(alignment,qOverload<int>(&QComboBox::activated),this,[this,update](int i){if(initialTable.alignment.size()!=grid->columnCount())initialTable.alignment=QStringList(grid->columnCount(),QStringLiteral("l"));const int c=qMax(0,grid->currentColumn());initialTable.alignment[c]=alignment->itemData(i).toString();update();});
    connect(grid,&QTableWidget::currentCellChanged,this,[this](int,int c,int,int){if(c>=0&&c<initialTable.alignment.size()){QSignalBlocker b(alignment);alignment->setCurrentIndex(alignment->findData(initialTable.alignment[c]));}});
    grid->setCurrentCell(0,0);
    for(bool isRow:{true,false}){
      auto *button=isRow?rows:columns;auto *menu=new QMenu(button);button->setMenu(menu);button->setPopupMode(QToolButton::InstantPopup);configureButton(button,ButtonRole::Text);
      for(int operation=0;operation<3;++operation){
        const auto title=operation==0?words("在前面插入","Insert before"):operation==1?words("在后面插入","Insert after"):(isRow?words("删除当前行","Delete current row"):words("删除当前列","Delete current column"));auto *action=menu->addAction(title);action->setObjectName(QStringLiteral("researchTable%1%2").arg(isRow?QStringLiteral("Row"):QStringLiteral("Column")).arg(operation));
        connect(menu,&QMenu::aboutToShow,this,[this,action,isRow,operation]{const int count=isRow?grid->rowCount():grid->columnCount();action->setEnabled(operation==2?count>1:count<(isRow?200:40));});
        connect(action,&QAction::triggered,this,[this,isRow,operation,update]{
          const int current=qMax(0,isRow?grid->currentRow():grid->currentColumn()),count=isRow?grid->rowCount():grid->columnCount();if(operation<2&&count>=(isRow?200:40))return;if(operation==2&&count<=1)return;
          syncing=true;const int at=current+(operation==1);
          if(isRow){
            if(initialTable.rules.size()!=count+1)initialTable.rules=QStringList(count+1,QString());
            if(operation==2){grid->removeRow(current);initialTable.rules.removeAt(current?current:1);}
            else{grid->insertRow(at);initialTable.rules.insert(qMin(at+1,int(initialTable.rules.size())-1),QString());}
          }else{
            if(initialTable.alignment.size()!=count)initialTable.alignment=QStringList(count,QStringLiteral("l"));
            if(operation==2){grid->removeColumn(current);initialTable.alignment.removeAt(current);}else{grid->insertColumn(at);initialTable.alignment.insert(at,QStringLiteral("l"));}
          }
          syncing=false;update();
        });
      }
    }
    auto *cells=static_cast<ClipboardTable*>(grid);cells->metadata=[this]{return QVariantMap{{QStringLiteral("alignment"),initialTable.alignment},{QStringLiteral("rules"),initialTable.rules}};};cells->restoreMetadata=[this](const QVariantMap &state){initialTable.alignment=state.value(QStringLiteral("alignment")).toStringList();initialTable.rules=state.value(QStringLiteral("rules")).toStringList();};cells->changed=update;cells->trackSelection=[this]{return !syncing;};cells->record();connect(cells,&QTableWidget::currentCellChanged,cells,[cells]{cells->rememberSelection();});connect(cells,&QTableWidget::itemSelectionChanged,cells,[cells]{cells->rememberSelection();});
    cells->clearCells=[this,update]{if(sourceOnly)return;syncing=true;for(const auto &range:grid->selectedRanges())for(int r=range.topRow();r<=range.bottomRow();++r)for(int c=range.leftColumn();c<=range.rightColumn();++c)if(auto *item=grid->item(r,c))item->setText(QString());syncing=false;update();};
    cells->importCells=[this,update](const QVector<QStringList> &data,bool raw){
      if(sourceOnly)return;int columns=0;for(const auto &line:data)columns=qMax(columns,int(line.size()));
      const auto ranges=grid->selectedRanges();const int row=ranges.isEmpty()?qMax(0,grid->currentRow()):ranges.first().topRow(),col=ranges.isEmpty()?qMax(0,grid->currentColumn()):ranges.first().leftColumn();
      if(data.isEmpty()||row+data.size()>200||col+columns>40){notice->setText(words("可视化表格最多 200 行、40 列；更大表格可编辑源码。","The grid supports 200 rows and 40 columns. Use source for larger tables."));return;}
      syncing=true;grid->setRowCount(qMax(grid->rowCount(),row+int(data.size())));grid->setColumnCount(qMax(grid->columnCount(),col+columns));while(initialTable.alignment.size()<grid->columnCount())initialTable.alignment.append(QStringLiteral("l"));
      while(initialTable.rules.size()<grid->rowCount()+1)initialTable.rules.insert(qMax(0,int(initialTable.rules.size())-1),QString());
      for(int r=0;r<data.size();++r)for(int c=0;c<data[r].size();++c)grid->setItem(row+r,col+c,new QTableWidgetItem(raw?data[r][c]:spreadsheetCell(data[r][c])));
      grid->clearSelection();grid->setRangeSelected(QTableWidgetSelectionRange(row,col,row+int(data.size())-1,col+columns-1),true);syncing=false;update();
    };
    paste->hide(); // Ctrl+V and the cell context menu use the same import path.
    connect(paste,&QToolButton::clicked,this,[cells]{cells->pasteCells(QApplication::clipboard()->mimeData());});
    grid->setToolTip(words("选择区域后可复制、剪切、粘贴或按 Delete 清空；支持 Excel 数据。","Select cells to copy, cut, paste or clear with Delete. Excel data is supported."));
    if(!newObject&&!initialTable.valid){sourceOnly=true;form->setEnabled(false);tabs->setCurrentIndex(1);notice->setText(words("复杂表格保留原排版，请在源码中编辑并预览。","This table has custom structure. Edit its source and preview."));}
  }
  QString resolvedDraft(bool applying,AssetImport *imported=nullptr){QString draft=text();if(!draft.contains(QStringLiteral("__TEXLINE_DRAFT_IMAGE__")))return draft;if(applying&&edit.target&&edit.target->untitled()){notice->setText(words("请先保存文稿，再添加图片资源；草稿仍保留在这里。","Save the document before adding image assets. Your draft is retained here."));return {};}if(chosenAsset.isEmpty()||slideExportHash(chosenAsset)!=assetHash){notice->setText(words("请选择图片；若文件已变动，请重新选择。","Choose an image again if the file has changed."));return {};}
    QString reference;if(applying&&imported)reference=assetReference(chosenAsset,rootOf(edit.target),notice,*imported,assetHash);else reference=previewAssetReference(chosenAsset,assetHash,previewAssets,notice);
    return reference.isEmpty()?QString():draft.replace(QStringLiteral("__TEXLINE_DRAFT_IMAGE__"),reference);
  }
public:
  ObjectDialog(TeXDocumentWindow *w,bool table,bool inserting,const AuthoringSpan &span):MaterialDialog(w),edit(w,inserting?w->textCursor().selectionEnd():span.start,inserting?w->textCursor().selectionEnd():span.end),tableMode(table),newObject(inserting) {
    setAttribute(Qt::WA_DeleteOnClose);setObjectName(QStringLiteral("researchObjectDialog"));authoringSurface(this);setWindowTitle(table?(inserting?words("插入表","Insert table"):words("编辑表","Edit table")):(inserting?words("插入图","Insert figure"):words("编辑图","Edit figure")));
    originalDraft=inserting?(table?QStringLiteral("\\begin{table}[htbp]\n\\centering\n\\begin{tabular}{lll}\n & & \\\\\n & & \\\\\n & & \\\\\n\\end{tabular}\n\\end{table}"):QStringLiteral("\\begin{figure}[htbp]\n\\centering\n\\includegraphics[width=0.90\\linewidth]{__TEXLINE_DRAFT_IMAGE__}\n\\end{figure}")):edit.original.mid(edit.start,edit.end-edit.start);baseDraft=originalDraft;
    auto *layout=new QVBoxLayout(this);layout->setContentsMargins(16,12,16,12);layout->setSpacing(10);
    tabs=named(new QTabWidget(this),"researchObjectTabs");tabs->tabBar()->hide();
    tabHeader=named(new QWidget(this),"researchObjectHeader");tabRow=new QHBoxLayout(tabHeader);tabRow->setContentsMargins(0,0,0,0);tabRow->setSpacing(2);
    layoutTab=textButton(tabHeader,"researchObjectLayoutTab",table?words("表格","Table"):words("排版","Layout"));sourceTab=textButton(tabHeader,"researchObjectSourceTab",words("源码","Source"));
    for(auto *button:{layoutTab,sourceTab}){button->setCheckable(true);button->setProperty("researchArticleScopeControl",true);configureButton(button,ButtonRole::Text);tabRow->addWidget(button);}
    tabRow->addStretch();layout->addWidget(tabHeader);connect(layoutTab,&QToolButton::clicked,this,[this]{selectTab(0);});connect(sourceTab,&QToolButton::clicked,this,[this]{selectTab(1);});
    layoutSplit=named(new QSplitter(Qt::Horizontal,tabs),"researchObjectLayoutSplit");sourceSplit=named(new QSplitter(Qt::Horizontal,tabs),"researchObjectSourceSplit");
    for(auto *split:{layoutSplit,sourceSplit}){split->setHandleWidth(1);split->setChildrenCollapsible(false);split->setStyleSheet(QStringLiteral("QSplitter::handle {background:#e2e7eb;}"));}
    form=named(new QWidget(layoutSplit),"researchObjectForm");source=named(new QPlainTextEdit(sourceSplit),"researchObjectSource");source->setFont(w->editor()->font());source->setPlainText(originalDraft);sourceSplit->addWidget(source);
    previewPane=named(new QWidget(layoutSplit),"researchObjectPreviewPane");auto *previewLayout=new QVBoxLayout(previewPane);previewLayout->setContentsMargins(8,12,8,12);image=named(new PreviewImage(previewPane),"researchObjectPreview");image->setMinimumSize(100,120);image->setAlignment(Qt::AlignCenter);image->setText(words("草稿预览","Draft preview"));image->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Ignored);image->setStyleSheet(QStringLiteral("background:#ffffff;border:1px solid #e2e7eb;"));previewLayout->addWidget(image,1);
    if(table){layoutSplit->addWidget(form);layoutSplit->addWidget(previewPane);}else {auto *scroll=named(new QScrollArea(layoutSplit),"researchObjectFormScroll");scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);scroll->setWidget(form);layoutSplit->insertWidget(0,previewPane);layoutSplit->addWidget(scroll);scroll->setMinimumWidth(280);}
    tabs->addTab(layoutSplit,table?words("表格","Table"):words("排版","Layout"));tabs->addTab(sourceSplit,words("源码","Source"));previewToggle=textButton(tabHeader,"researchObjectPreviewToggle",words("预览","Preview"));previewToggle->setCheckable(true);previewToggle->setChecked(true);tabRow->addWidget(previewToggle);layout->addWidget(tabs,1);
    tableMeta=named(new QWidget(this),"researchTableMeta");if(table)layout->addWidget(tableMeta);else tableMeta->hide();
    notice=named(new QLabel(this),"researchObjectNotice");notice->setWordWrap(true);notice->setMinimumWidth(0);notice->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);notice->setStyleSheet(QStringLiteral("color:#6e6e73;font-size:12px;"));
    auto *footer=new QHBoxLayout;preview=named(new QPushButton(words("更新预览","Update preview"),this),"researchObjectPreviewButton");configureButton(preview);preview->setProperty("researchRetryOnly",true);preview->hide();footer->addWidget(preview);footer->addWidget(notice,1);
    auto *buttons=named(new QDialogButtonBox(QDialogButtonBox::Apply|QDialogButtonBox::Cancel,this),"researchObjectButtons");buttons->button(QDialogButtonBox::Apply)->setText(words("应用","Apply"));buttons->button(QDialogButtonBox::Cancel)->setText(words("取消","Cancel"));for(auto *button:buttons->buttons())configureButton(qobject_cast<QPushButton*>(button));footer->addWidget(buttons);layout->addLayout(footer);renderer=new DraftPreview(image,notice,preview);preview->setAutoDefault(false);buttons->button(QDialogButtonBox::Apply)->setDefault(true);
    previewDelay.setSingleShot(true);previewDelay.setInterval(450);connect(&previewDelay,&QTimer::timeout,this,[this]{refreshPreview();});
    if(table)populateTable();else populateFigure();
    connect(tabs,&QTabWidget::currentChanged,this,[this]{updateWorkbench();});connect(previewToggle,&QToolButton::toggled,this,[this](bool on){updateWorkbench();if(on)refreshPreview();else{previewDelay.stop();renderer->invalidate(false);}});
    connect(source,&QPlainTextEdit::textChanged,this,[this]{if(syncing)return;sourceOnly=true;form->setEnabled(false);tableMeta->setEnabled(false);updateWorkbench();schedulePreview();});
    connect(preview,&QPushButton::clicked,this,[this]{if(!previewToggle->isChecked())previewToggle->setChecked(true);else refreshPreview();});
    connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);connect(buttons->button(QDialogButtonBox::Apply),&QPushButton::clicked,this,[this]{
      if(!edit.current()){notice->setText(words("原稿已变化，草稿仍保留在此窗口。","The source changed. Your draft is retained here."));return;}
      if(!balancedText(text())){notice->setText(words("草稿中有未配对的花括号，请核对源码。","Check the unmatched braces in the draft source."));return;}
      if(!sourceOnly&&!label->text().isEmpty()&&!literalKey(label->text())){notice->setText(words("标签含有不支持的字符。","The label contains unsupported characters."));return;}
      if(!sourceOnly&&tableMode){const auto parsed=parseSimpleTable(text());if(!parsed.valid){notice->setText(words("请核对单元格中的 LaTeX；普通 & 请写为 \\&。","Check cell LaTeX; write a literal & as \\&."));return;}}
      const auto required=text()==originalDraft&&!newObject?QStringList():draftPackages(text());QString dependency;
      if(!edit.prepare(required,dependency,notice))return;
      AssetImport imported;const auto draft=resolvedDraft(true,&imported);if(draft.isEmpty())return;
      // A form no-op keeps every original byte and creates no undo command.
      const auto replacement=newObject?QStringLiteral("\n")+draft+QLatin1Char('\n'):draft;
      if(edit.apply(replacement,required,notice)){imported.keep();if(edit.target){if(newObject){auto cursor=edit.target->textCursor();cursor.setPosition(mapSourcePosition(edit.original,edit.target->textDoc()->editorText(),edit.end,true));edit.target->editor()->setTextCursor(cursor);edit.target->editor()->ensureCursorVisible();}edit.target->editor()->setFocus();}accept();}
    });connect(this,&QDialog::finished,w,[w]{w->editor()->setFocus();});fitDialog(this,1100,700);updateWorkbench();
  }
public:
  bool setClipboardImage(const QImage &imageData) {
    if(imageData.isNull()||imageData.sizeInBytes()>64*1024*1024)return false;
    clipboardImage=QSharedPointer<QTemporaryDir>::create(QDir::tempPath()+QStringLiteral("/texline-clipboard-XXXXXX"));
    if(!clipboardImage->isValid())return false;
    chosenAsset=clipboardImage->filePath(QStringLiteral("clipboard.png"));
    if(!imageData.save(chosenAsset,"PNG"))return false;assetHash=slideExportHash(chosenAsset);if(auto *name=findChild<QLabel*>(QStringLiteral("researchFigureFile"))){name->setText(words("剪贴板图片","Clipboard image"));name->setToolTip(QString());}showAsset();schedulePreview();return true;
  }
protected:
  void showEvent(QShowEvent *event)override{MaterialDialog::showEvent(event);schedulePreview();}
  void hideEvent(QHideEvent *event)override{previewDelay.stop();renderer->invalidate(false);MaterialDialog::hideEvent(event);}
  void resizeEvent(QResizeEvent *event)override{MaterialDialog::resizeEvent(event);updateWorkbench();}
};

#include "ResearchTikzWorkbench.inc"

void openObject(TeXDocumentWindow *w,bool table,bool inserting) {
  if(!w||w->editor()->isReadOnly())return;
  if(auto *drawing=w->findChild<QDialog*>(QStringLiteral("researchTikzDialog"))){drawing->show();drawing->raise();return;}
  if(auto *existing=w->findChild<QDialog*>(QStringLiteral("researchObjectDialog"))){existing->show();existing->raise();existing->activateWindow();return;}
  const auto span=authoringObjectAt(w->textDoc()->editorText(),w->cursorPosition());
  if(!inserting&&(!span.valid()||objectFamily(span.kind)!=(table?QStringLiteral("table"):QStringLiteral("figure")))){showTransientStatusMessage(w,words("将光标放在要编辑的图或表中。","Place the cursor inside the figure or table to edit."));return;}
  if(!table&&!inserting&&Tikz::parse(w->textDoc()->editorText().mid(span.start,span.end-span.start)).valid){openTikz(w,false,span);return;}
  (new ObjectDialog(w,table,inserting,span))->show();
}

class CitationDelegate final : public QStyledItemDelegate {
public:using QStyledItemDelegate::QStyledItemDelegate;
  QSize sizeHint(const QStyleOptionViewItem &,const QModelIndex &)const override{return {260,66};}
  void paint(QPainter *p,const QStyleOptionViewItem &option,const QModelIndex &index)const override{p->save();const auto rect=option.rect;if(option.state&QStyle::State_Selected)p->fillRect(rect,QColor(QStringLiteral("#e8eef2")));const auto parts=index.data().toString().split(QLatin1Char('\n'));auto font=option.font;font.setPixelSize(15);p->setFont(smoothTextFont(font));p->setPen(QColor(QStringLiteral("#30363c")));p->drawText(rect.adjusted(14,8,-12,-30),Qt::AlignVCenter,p->fontMetrics().elidedText(parts.value(0),Qt::ElideRight,rect.width()-26));font.setPixelSize(13);p->setFont(smoothTextFont(font));p->setPen(QColor(QStringLiteral("#747d85")));p->drawText(rect.adjusted(14,33,-12,-8),Qt::AlignVCenter,p->fontMetrics().elidedText(parts.value(1),Qt::ElideRight,rect.width()-26));p->restore();}
};
struct ExistingCitation {int start{-1},end{-1};QString command,locator,prenote;QStringList keys;QString raw;bool valid()const{return start>=0&&end>start;}};
ExistingCitation citationAt(const QString &source,int position) {
  const auto clean=maskProjectText(source);auto matches=QRegularExpression(QStringLiteral(R"(\\(citep|citet|citealp|citealt|autocite|parencite|textcite|footcite|cite)(?![A-Za-z@])\*?)")).globalMatch(clean);
  while(matches.hasNext()){const auto match=matches.next();if(match.capturedStart()>position)break;int at=int(match.capturedEnd());QStringList options;while(at<clean.size()&&clean[at].isSpace())++at;
    while(options.size()<2&&at<clean.size()&&clean[at]==QLatin1Char('[')){const int start=++at;int braces=0;while(at<clean.size()){if(!escaped(clean,at)){if(clean[at]==QLatin1Char('{'))++braces;if(clean[at]==QLatin1Char('}'))--braces;if(clean[at]==QLatin1Char(']')&&!braces)break;}++at;}if(at>=clean.size())return {};options.append(source.mid(start,at-start));++at;while(at<clean.size()&&clean[at].isSpace())++at;}
    if(at>=clean.size()||clean[at]!=QLatin1Char('{'))continue;const int start=at+1;group(clean,at);if(at<0||position<match.capturedStart()||position>at)continue;auto keys=source.mid(start,at-start-1).split(QLatin1Char(','));for(auto &key:keys){key=key.trimmed();if(!literalKey(key))return {};}
    return {int(match.capturedStart()),at,match.captured(0).mid(1),options.isEmpty()?QString():options.last(),options.size()==2?options.first():QString(),keys,source.mid(match.capturedStart(),at-match.capturedStart())};
  }return {};
}
// Discover only a bounded set inside the already opened project, without linking it.
QStringList unlinkedBibliographies(const QString &root,const std::shared_ptr<std::atomic_bool> &cancelled) {
  QStringList result;QList<QPair<QString,int>> directories{{QFileInfo(root).absolutePath(),0}};int examined=0;bool complete=true;
  const QSet<QString> excluded{QStringLiteral("build"),QStringLiteral("out"),QStringLiteral("node_modules"),QStringLiteral("figures"),QStringLiteral("images")};
  while(!directories.isEmpty()&&examined<1000&&result.size()<16&&!cancelled->load()) {
    const auto next=directories.takeFirst();QDirIterator files(next.first,QDir::Files|QDir::Dirs|QDir::NoDotAndDotDot|QDir::NoSymLinks);
    while(files.hasNext()&&examined++<1000&&result.size()<16&&!cancelled->load()) {
      files.next();const auto info=files.fileInfo();
      if(info.isDir()){if(!excluded.contains(info.fileName().toLower())){if(next.second<4&&directories.size()<128)directories.append({info.absoluteFilePath(),next.second+1});else complete=false;}}
      else if(info.suffix().compare(QStringLiteral("bib"),Qt::CaseInsensitive)==0)result.append(info.canonicalFilePath());
    }
  }
  if(!complete||!directories.isEmpty()||examined>=1000||cancelled->load())return {};
  return result;
}
class CitationDialog final : public MaterialDialog {
  ExistingCitation existing;SourceEdit edit;QLineEdit *search,*locator;QStringList chosenKeys;QListWidget *list;QLabel *notice,*insertion;QPlainTextEdit *metadata;QComboBox *style;QPushButton *apply;
  QSharedPointer<const ProjectIndex> index;QVector<ProjectEntry> entries;QString commandName;std::shared_ptr<std::atomic_bool> cancelled{std::make_shared<std::atomic_bool>(false)};
  QLabel *detailTitle,*detailAuthor,*detailYear,*detailKey,*sources;QStackedWidget *detailStack;QToolButton *sourceToggle;
  QString shownKey,retainedNotice,suggestedBibliography;QHash<QString,int> keyCounts,citationCounts;bool capped{false},selectionExplicit{false};
  bool reloadOnReturn{false},managing{false},loading{false},reloadPending{false},leftWindow{false};
  QFileSystemWatcher bibliographyWatch;QTimer reloadDelay;QHash<QString,QPair<qint64,qint64>> watchedFiles;
  void scheduleReload(){apply->setEnabled(false);if(!isVisible()){reloadOnReturn=true;return;}reloadDelay.start();}
  void watchBibliographies(){
    if(!bibliographyWatch.files().isEmpty())bibliographyWatch.removePaths(bibliographyWatch.files());
    if(!bibliographyWatch.directories().isEmpty())bibliographyWatch.removePaths(bibliographyWatch.directories());
    watchedFiles.clear();if(!index)return;QStringList directories;
    for(const auto &file:index->bibliographies){QFileInfo info(file);watchedFiles.insert(file,{info.size(),info.lastModified().toMSecsSinceEpoch()});if(info.isFile())bibliographyWatch.addPath(file);const auto directory=info.absolutePath();if(!directories.contains(directory)&&QFileInfo(directory).isDir())directories.append(directory);}
    if(!directories.isEmpty())bibliographyWatch.addPaths(directories);
  }
  void bibliographyChanged(){for(auto i=watchedFiles.cbegin();i!=watchedFiles.cend();++i){QFileInfo info(i.key());if(i.value()!=qMakePair(info.size(),info.lastModified().toMSecsSinceEpoch())){scheduleReload();return;}}}

#include "ResearchCitationManagement.inc"
  void removeEntry(){
    if(!edit.target||!index||index->truncated||keyCounts.value(key())!=1)return;
    for(const auto &entry:entries)if(entry.key==key()&&QFileInfo(entry.file).suffix().compare(QStringLiteral("bib"),Qt::CaseInsensitive)==0){
      int count=0;for(const auto &use:index->uses)if(use.kind==QStringLiteral("citation-use")&&use.key==entry.key)++count;
      if(removeBibliographyEntry(this,edit.target,entry.file,entry.key,count)){rememberBibliographyUndo(entry.file);chosenKeys.removeAll(entry.key);load();}return;
    }
  }
  void editEntrySource(){
    if(!edit.target||keyCounts.value(key())!=1)return;
    for(const auto &entry:entries)if(entry.key==key()){
      if(QFileInfo(entry.file).suffix().compare(QStringLiteral("bib"),Qt::CaseInsensitive)==0){if(editBibliographyEntry(this,edit.target,entry.file,entry.key)){rememberBibliographyUndo(entry.file);load();}return;}
    }
    openEntrySource();
  }
  void linkBibliography(QString file = {}){
    const auto root=edit.target?rootOf(edit.target):QString();const bool biblatex=loadedPackages(maskProjectText(sourceOf(root,sourceBuffers()))).contains(QStringLiteral("biblatex"));
      if(!edit.target)return;
      if(edit.target->untitled()){retainedNotice=words("请先保存文稿，再关联文献来源。","Save the document before linking a bibliography.");notice->setText(retainedNotice);return;}
      if(root.isEmpty()||!edit.current()||rootOf(edit.target)!=root){retainedNotice=words("文稿已变化，请重新打开引用后关联。","The document changed. Reopen citations before linking.");notice->setText(retainedNotice);return;}
      QPointer<TeXDocumentWindow> target=TeXDocumentWindow::findDocument(root);if(!target)target=TeXDocumentWindow::openDocument(root,false,false);if(!target)return;
      QPointer<CitationDialog> guard(this);SourceEdit linking(target,0,0);if(file.isEmpty())file=QFileDialog::getOpenFileName(this,words("关联文献来源","Link bibliography"),QFileInfo(root).absolutePath(),QStringLiteral("BibTeX (*.bib)"));if(!guard||file.isEmpty()||!target)return;
      const auto rel=QFileInfo(root).absoluteDir().relativeFilePath(file);if(rel.contains(QRegularExpression(QStringLiteral(R"([{},%#\\])")))||QFileInfo(file).suffix().compare(QStringLiteral("bib"),Qt::CaseInsensitive)!=0){notice->setText(words("请选择路径中不含 TeX 特殊字符的 .bib 文件。","Choose a .bib file without TeX special characters in its path."));return;}
      const auto name=biblatex?rel:QString(rel).remove(QRegularExpression(QStringLiteral("\\.bib$"),QRegularExpression::CaseInsensitiveOption));const auto existing=command(linking.original,biblatex?QStringLiteral("addbibresource"):QStringLiteral("bibliography"));
      if(biblatex){auto resources=QRegularExpression(QStringLiteral(R"(\\addbibresource\s*(?:\[[^\]]*\]\s*)?\{([^{}]+)\})")).globalMatch(maskProjectText(linking.original));while(resources.hasNext()){const auto resource=resources.next().captured(1).trimmed();if(QFileInfo(QFileInfo(root).absoluteDir().filePath(resource)).canonicalFilePath()==QFileInfo(file).canonicalFilePath()){edit.rebase();load();return;}}}
      QString replacement;if(!biblatex&&existing.valid()){auto sources=existing.value.split(QLatin1Char(','),Qt::SkipEmptyParts);for(auto &entry:sources)entry=entry.trimmed();if(!sources.contains(name))sources.append(name);linking.start=existing.valueStart;linking.end=existing.valueEnd;replacement=sources.join(QLatin1Char(','));}
      else {if(biblatex&&existing.valid()&&existing.value==name){edit.rebase();load();return;}const auto boundary=QRegularExpression(biblatex?QStringLiteral(R"(\\begin\s*\{document\})"):QStringLiteral(R"(\\end\s*\{document\})")).match(maskProjectText(linking.original));if(!boundary.hasMatch()){notice->setText(words("请在主文件中关联文献来源。","Link the bibliography in the main document."));return;}linking.start=linking.end=int(boundary.capturedStart());replacement=(biblatex?QStringLiteral("\\addbibresource{"):QStringLiteral("\\bibliography{"))+name+QStringLiteral("}\n");
        if(!biblatex){const auto style=initialBibliographyStyle(linking.original,root);if(!style.isEmpty())replacement.prepend(QStringLiteral("\\bibliographystyle{")+style+QStringLiteral("}\n"));}
      }
      if(linking.apply(replacement,{},notice)){edit.rebase();load();search->setFocus();}
  }
  void addEntries(int source){
    if(!edit.target||edit.target->untitled()){notice->setText(words("请先保存文稿。","Save the document first."));return;}
    const auto incoming=acquireBibliography(this,source);if(incoming.isEmpty()||!edit.target)return;
    QString file=managing?edit.target->fileName():index?index->bibliographies.value(0):QString();
    if(!managing&&index&&index->bibliographies.size()>1){file=QFileDialog::getOpenFileName(this,words("选择目标文献文件","Choose bibliography destination"),QFileInfo(rootOf(edit.target)).absolutePath(),QStringLiteral("BibTeX (*.bib)"));if(file.isEmpty())return;}
    const bool unlinked=file.isEmpty();if(unlinked)file=QFileInfo(rootOf(edit.target)).absoluteDir().filePath(QStringLiteral("references.bib"));
    QStringList added;if(!appendBibliography(this,edit.target,file,incoming,&added))return;rememberBibliographyUndo(file);
    if(unlinked)linkBibliography(file);else load();
  }
  void openEntrySource(){
    if(!edit.target||keyCounts.value(key())!=1)return;
    for(const auto &entry:entries)if(entry.key==key()){
      const auto text=sourceOf(entry.file,sourceBuffers());
      const auto record=BibTeXFile::entrySource(text,entry.key);
      const int at=record.isEmpty()?entry.offset:int(text.indexOf(record));
      if(at<0){load();return;}
      rememberReadingPlace(edit.target);rememberRoot(entry.file,completionProjectRoot(edit.target));hide();reloadOnReturn=true;
      if(auto *target=TeXDocumentWindow::openDocument(entry.file,true,true)){
        auto cursor=QTextCursor(target->textDoc());cursor.setPosition(qBound(0,at,target->textDoc()->characterCount()-1));
        if(!record.isEmpty())cursor.setPosition(qMin(at+int(record.size()),target->textDoc()->characterCount()-1),QTextCursor::KeepAnchor);
        target->editor()->setTextCursor(cursor);target->editor()->ensureCursorVisible();target->editor()->setFocus();
      }else{show();notice->setText(words("无法打开文献来源。","Could not open the bibliography."));}
      return;
    }
  }
protected:
  void showEvent(QShowEvent *event)override{MaterialDialog::showEvent(event);if(reloadOnReturn||index){reloadOnReturn=false;scheduleReload();}}
  bool event(QEvent *event)override{const auto handled=MaterialDialog::event(event);if(event->type()==QEvent::WindowDeactivate)leftWindow=true;else if(event->type()==QEvent::WindowActivate&&leftWindow){leftWindow=false;scheduleReload();}return handled;}
private:
  QString styleCaption(const QString &command)const{
    if(command==QStringLiteral("cite"))return words("默认引用","Default");
    if(command==QStringLiteral("citep")||command==QStringLiteral("parencite"))return words("括号引用","Parenthetical");
    if(command==QStringLiteral("citet")||command==QStringLiteral("textcite"))return words("正文作者","Author in text");
    if(command==QStringLiteral("footcite"))return words("脚注引用","Footnote");
    if(command==QStringLiteral("autocite"))return words("自动引用","Automatic");
    if(command==QStringLiteral("citealp"))return words("无括号引用","Without parentheses");
    if(command==QStringLiteral("citealt"))return words("正文无括号","Text without parentheses");
    return words("当前引用方式","Current citation style");
  }
  QString key()const{auto *item=list->currentItem();return item?item->data(Qt::UserRole).toString():QString();}
  QString snippet()const{if(existing.valid()&&style->currentData().toString()==existing.command&&locator->text()==existing.locator&&chosenKeys==existing.keys)return existing.raw;const auto keys=chosenKeys;const auto text=citationSource(style->currentData().toString(),keys.join(QLatin1Char(',')),locator->text());if(existing.prenote.isEmpty()||text.isEmpty())return text;const int at=text.indexOf(QLatin1Char('['));const int brace=text.indexOf(QLatin1Char('{'));auto result=text;result.insert(at>=0?at:brace,QLatin1Char('[')+existing.prenote+QLatin1Char(']')+(at<0?QStringLiteral("[]"):QString()));return result;}
  void updateInsertion(){
    if(auto *button=findChild<QToolButton*>(QStringLiteral("researchCitationEditSource")))button->setEnabled(keyCounts.value(key())==1);
    const auto text=snippet();insertion->setText(text);insertion->setToolTip(text);bool duplicate=false,missing=false;for(const auto &selected:chosenKeys){duplicate|=keyCounts.value(selected)>1;missing|=keyCounts.value(selected)==0;}apply->setEnabled(!managing&&!loading&&!reloadPending&&!reloadDelay.isActive()&&!text.isEmpty()&&!duplicate&&!missing);const auto caption=(existing.valid()?words("更新","Update"):words("插入","Insert"))+(chosenKeys.size()>1?QStringLiteral(" (%1)").arg(chosenKeys.size()):QString());if(apply->text()!=caption){apply->setText(caption);configureButton(apply);}
    QStringList missingFiles,outside;
    if(index)for(const auto &issue:index->issues)if(issue.kind==QStringLiteral("missing-bibliography")&&!missingFiles.contains(issue.key))missingFiles.append(issue.key);
    if(selectionExplicit)for(const auto &selected:chosenKeys){bool visible=false;for(int row=0;row<list->count();++row)if(list->item(row)->data(Qt::UserRole).toString()==selected){visible=true;break;}if(!visible)outside.append(selected);}
    if(!retainedNotice.isEmpty())notice->setText(retainedNotice);
    else if(!index)notice->setText(words("正在读取本地文献…","Reading local references…"));
    else if(!missingFiles.isEmpty())notice->setText(words("找不到文献文件：%1。请恢复文件或修正文献路径。","Bibliography file not found: %1. Restore it or correct its path.").arg(missingFiles.join(QStringLiteral(", "))));
    else if(index->citationCoveragePartial||index->truncated)notice->setText(words("部分文献未能读取，请检查文献文件。","Some references could not be read. Check the bibliography files."));
    else if(entries.isEmpty())notice->setText(words("先关联一个文献文件，再选择要插入的条目。","Link a bibliography file, then choose a reference to insert."));
    else if(duplicate)notice->setText(words("引用键重复，请先核对文献来源。","This citation key is duplicated. Check the bibliography first."));
    else if(missing)notice->setText(words("原引用中的文献未找到，请选择替代条目。","A cited reference is missing. Select its replacement."));
    else if(!outside.isEmpty())notice->setText(words("已选（筛选外）：%1","Selected outside this search: %1").arg(outside.mid(0,3).join(QStringLiteral(", "))+(outside.size()>3?QStringLiteral("…"):QString())));
    else if(!list->currentItem())notice->setText(words("没有匹配的文献。","No matching references."));
    else if(capped)notice->setText(words("显示 200 项，输入关键词缩小范围。","Showing 200 entries. Type to narrow the results."));
    else notice->clear();
  }
  void selection(){updateInsertion();const auto selected=key();if(selected==shownKey)return;shownKey=selected;for(const auto &entry:entries)if(entry.key==selected){const bool bib=QFileInfo(entry.file).suffix().compare(QStringLiteral("bib"),Qt::CaseInsensitive)==0;const int divider=entry.detail.lastIndexOf(QStringLiteral(" · "));detailTitle->setText(entry.title.isEmpty()?entry.key:entry.title);detailAuthor->setText(bib&&divider>=0?entry.detail.left(divider):QStringLiteral("—"));detailYear->setText(bib&&divider>=0?entry.detail.mid(divider+3):QStringLiteral("—"));detailKey->setText(entry.key+(citationCounts.value(entry.key)>0?words(" · 引用 %1 处"," · Cited %1 times").arg(citationCounts.value(entry.key)):QString()));QString details=entry.title+QLatin1Char('\n')+entry.detail+QLatin1Char('\n')+entry.key+QStringLiteral("\n\n")+QFileInfo(entry.file).fileName();metadata->setToolTip(entry.file);const auto original=sourceOf(entry.file,sourceBuffers());
      // Display the actual local entry. It is metadata, never paper full text.
      const auto record=bib?BibTeXFile::entrySource(original,entry.key):QString();if(!record.isEmpty())details+=QStringLiteral("\n\n")+record;
      else if(!bib){const int offset=qBound(0,entry.offset,int(original.size()));details+=QStringLiteral("\n\n")+original.mid(offset,1000).left(original.indexOf(QStringLiteral("\\bibitem"),offset+1)>offset?original.indexOf(QStringLiteral("\\bibitem"),offset+1)-offset:1000);}
      metadata->setPlainText(details);return;}metadata->clear();detailTitle->clear();detailAuthor->clear();detailYear->clear();detailKey->clear();}
  void filter(){
    const auto selected=key().isEmpty()?chosenKeys.value(0):key(),query=search->text().trimmed();capped=false;
    {QSignalBlocker block(list);list->clear();QSet<QString> keys;int selectedRow=-1;
      for(const auto &entry:entries){
        if(entry.key.isEmpty()||keys.contains(entry.key))continue;
        if(!query.isEmpty()&&!(entry.title+entry.detail+entry.key).contains(query,Qt::CaseInsensitive))continue;
        keys.insert(entry.key);
        if(list->count()>=200){
          capped=true;if(selectedRow>=0||selected.isEmpty())break;
          // Broadening a search must not silently cite a different paper while
          // retaining the previous locator. Keep the selected match in the cap.
          if(entry.key!=selected)continue;
          delete list->takeItem(199);
        }
        auto *item=new QListWidgetItem((entry.title.isEmpty()?entry.key:entry.title)+QStringLiteral("\n")+entry.detail+QStringLiteral("  ·  ")+entry.key,list);item->setData(Qt::UserRole,entry.key);item->setToolTip(entry.file);if(entry.key==selected)selectedRow=list->count()-1;
      }
      if(list->count()){list->setCurrentRow(selectedRow>=0?selectedRow:0,QItemSelectionModel::NoUpdate);for(int i=0;i<list->count();++i)list->item(i)->setSelected(chosenKeys.contains(list->item(i)->data(Qt::UserRole).toString()));if(chosenKeys.isEmpty()||!selectionExplicit){chosenKeys=QStringList{key()};for(int i=0;i<list->count();++i)list->item(i)->setSelected(i==list->currentRow());}}
      else if(!selectionExplicit)chosenKeys.clear();
    }selection();
  }
public:
  explicit CitationDialog(TeXDocumentWindow *w):MaterialDialog(w),existing(citationAt(w->textDoc()->editorText(),w->textCursor().position())),edit(w,existing.valid()?existing.start:w->textCursor().selectionEnd(),existing.valid()?existing.end:w->textCursor().selectionEnd()) {
    managing=QFileInfo(w->fileName()).suffix().compare(QStringLiteral("bib"),Qt::CaseInsensitive)==0;
    for(const auto *name:{"researchNavigateBack","researchNavigateForward"})if(auto *action=w->findChild<QAction*>(QString::fromLatin1(name)))addAction(action);
    chosenKeys=existing.keys;selectionExplicit=existing.valid();
    setAttribute(Qt::WA_DeleteOnClose);setObjectName(QStringLiteral("researchCitationDialog"));authoringSurface(this);setWindowTitle(words("文献引用","Cite a reference"));auto *layout=new QVBoxLayout(this);layout->setContentsMargins(16,12,16,12);layout->setSpacing(8);
    search=field(this,"researchCitationSearch");search->setPlaceholderText(words("搜索作者、题名、年份或引用键","Search author, title, year or key"));search->setClearButtonEnabled(true);layout->addWidget(search);
    sources=named(new QLabel(words("当前项目","Current project"),this),"researchCitationSources");sources->setStyleSheet(QStringLiteral("color:#747d85;font-size:13px;"));sources->setMinimumWidth(0);sources->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);layout->addWidget(sources);
    auto *split=named(new QSplitter(Qt::Horizontal,this),"researchCitationSplit");split->setHandleWidth(1);split->setStyleSheet(QStringLiteral("QSplitter::handle {background:#e2e7eb;}"));list=named(new QListWidget(split),"researchCitationList");list->setItemDelegate(new CitationDelegate(list));list->setMinimumWidth(210);list->setFrameShape(QFrame::NoFrame);list->setUniformItemSizes(true);list->setSelectionMode(QAbstractItemView::ExtendedSelection);list->setToolTip(words("Ctrl / Shift 多选文献","Ctrl / Shift to select multiple references"));
    auto *details=new QWidget(split);auto *detailsLayout=new QVBoxLayout(details);detailsLayout->setContentsMargins(18,14,12,12);detailStack=new QStackedWidget(details);auto *detailsPage=new QWidget(detailStack);auto *fields=new QFormLayout(detailsPage);fields->setContentsMargins(0,0,0,0);fields->setSpacing(18);fields->setSizeConstraint(QLayout::SetMinAndMaxSize);fields->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);fields->setRowWrapPolicy(QFormLayout::WrapLongRows);
    auto value=[&](const char *name,const QString &label){auto *result=named(new QLabel(detailsPage),name);result->setTextInteractionFlags(Qt::TextSelectableByMouse);result->setWordWrap(true);result->setMinimumWidth(0);result->setStyleSheet(QStringLiteral("font-size:15px;color:#30363c;"));fields->addRow(label,result);return result;};detailTitle=value("researchCitationTitle",words("题名","Title"));detailAuthor=value("researchCitationAuthor",words("作者","Author"));detailYear=value("researchCitationYear",words("年份","Year"));detailKey=value("researchCitationKey",words("引用键","Key"));
    auto *detailsScroll=named(new QScrollArea(detailStack),"researchCitationDetailsScroll");detailsScroll->setWidgetResizable(true);detailsScroll->setFrameShape(QFrame::NoFrame);detailsScroll->setWidget(detailsPage);detailStack->addWidget(detailsScroll);metadata=named(new QPlainTextEdit(detailStack),"researchCitationMetadata");metadata->setReadOnly(true);auto codeFont=w->editor()->font();codeFont.setPixelSize(13);metadata->setFont(codeFont);detailStack->addWidget(metadata);detailsLayout->addWidget(detailStack,1);sourceToggle=textButton(details,"researchCitationViewSource",words("查看条目源码","View entry source"));auto *entryActions=new QHBoxLayout;entryActions->addWidget(sourceToggle);entryActions->addStretch();auto *editSource=textButton(details,"researchCitationEditSource",words("编辑条目…","Edit entry…"));editSource->setToolTip(words("编辑字段或条目源码","Edit fields or entry source"));auto *editMenu=new QMenu(editSource);auto *sourceAction=editMenu->addAction(words("在文献文件中打开","Open in bibliography file"));sourceAction->setObjectName(QStringLiteral("researchCitationOpenSource"));connect(sourceAction,&QAction::triggered,this,[this]{openEntrySource();});connect(editMenu->addAction(words("更改引用键…","Rename citation key…")),&QAction::triggered,this,[this]{renameEntry();});
    connect(editMenu->addAction(words("删除条目…","Delete entry…")),&QAction::triggered,this,[this]{removeEntry();});
    editMenu->addSeparator();
    connect(editMenu->addAction(words("撤销文献修改","Undo reference edit")),&QAction::triggered,this,[this]{bibliographyHistory(false);});
    connect(editMenu->addAction(words("重做文献修改","Redo reference edit")),&QAction::triggered,this,[this]{bibliographyHistory(true);});
    editSource->setMenu(editMenu);editSource->setPopupMode(QToolButton::MenuButtonPopup);entryActions->addWidget(editSource);detailsLayout->addLayout(entryActions);connect(editSource,&QToolButton::clicked,this,[this]{editEntrySource();});connect(sourceToggle,&QToolButton::clicked,this,[this]{const bool raw=detailStack->currentIndex()==0;detailStack->setCurrentIndex(raw?1:0);sourceToggle->setText(raw?words("返回详情","Back to details"):words("查看条目源码","View entry source"));configureButton(sourceToggle,ButtonRole::Text);});split->addWidget(list);split->addWidget(details);split->setStretchFactor(0,1);split->setStretchFactor(1,1);split->setSizes({460,460});layout->addWidget(split,1);
    locator=field(this,"researchCitationLocator");locator->setPlaceholderText(words("可选","Optional"));locator->setMaximumWidth(200);locator->setText(existing.locator);auto *locatorRow=new QHBoxLayout;locatorRow->addWidget(new QLabel(words("页码","Pages"),this));locatorRow->addWidget(locator);layout->addLayout(locatorRow);insertion=named(new QLabel(this),"researchCitationInsertion");insertion->setTextInteractionFlags(Qt::TextSelectableByMouse);insertion->setWordWrap(true);insertion->setStyleSheet(QStringLiteral("background:#f5f7f8;color:#30363c;padding:10px;border-radius:4px;font-size:13px;"));layout->addWidget(insertion);
    auto *more=textButton(this,"researchCitationMore",words("更多","More"));more->setCheckable(true);locatorRow->addWidget(more);locatorRow->addStretch();style=named(new QComboBox(this),"researchCitationStyle");style->addItem(QStringLiteral("\\cite"),QStringLiteral("cite"));
    auto buffers=sourceBuffers();const auto root=rootOf(w);const auto rootText=maskProjectText(sourceOf(root,buffers));
    const auto packages=loadedPackages(rootText);const bool biblatex=packages.contains(QStringLiteral("biblatex")),natbib=packages.contains(QStringLiteral("natbib"));
    if(biblatex)for(const auto &cmd:{QStringLiteral("autocite"),QStringLiteral("parencite"),QStringLiteral("textcite"),QStringLiteral("footcite")})style->addItem(QLatin1Char('\\')+cmd,cmd);
    if(natbib)for(const auto &cmd:{QStringLiteral("citep"),QStringLiteral("citet"),QStringLiteral("citealp"),QStringLiteral("citealt")})style->addItem(QLatin1Char('\\')+cmd,cmd);
    const auto used=QRegularExpression(QStringLiteral(R"(\\(citep|citet|citealp|citealt|autocite|parencite|textcite|footcite|cite)\b)")).match(maskProjectText(edit.original));if(used.hasMatch()&&style->findData(used.captured(1))>=0)style->setCurrentIndex(style->findData(used.captured(1)));else if(biblatex)style->setCurrentIndex(style->findData(QStringLiteral("autocite")));else if(natbib)style->setCurrentIndex(style->findData(QStringLiteral("citep")));
    if(existing.valid()){if(style->findData(existing.command)<0)style->addItem(QLatin1Char('\\')+existing.command,existing.command);style->setCurrentIndex(style->findData(existing.command));}
    for(int i=0;i<style->count();++i)style->setItemText(i,styleCaption(style->itemData(i).toString()));style->setAccessibleName(words("引用方式","Citation style"));style->setToolTip(style->accessibleName());style->setMaximumWidth(170);style->setMinimumWidth(0);locator->setMinimumWidth(70);locatorRow->insertWidget(2,style);style->show();insertion->hide();connect(more,&QToolButton::toggled,insertion,&QWidget::setVisible);notice=named(new QLabel(words("正在读取本地文献…","Reading local references…"),this),"researchCitationNotice");notice->setWordWrap(true);layout->addWidget(notice);
    auto *footer=new QHBoxLayout;auto *attach=textButton(this,"researchCitationAttach",words("关联文献文件…","Link bibliography file…"));attach->hide();auto *add=textButton(this,"researchCitationAdd",words("添加…","Add…"));auto *addMenu=new QMenu(add);for(int source=0;source<5;++source){const QStringList captions{words("新建条目…","New entry…"),words("导入 BibTeX…","Import BibTeX…"),words("按题名或 DOI 查找…","Find by title or DOI…"),words("从 Zotero 选择…","Choose from Zotero…"),words("粘贴 BibTeX…","Paste BibTeX…")};connect(addMenu->addAction(captions[source]),&QAction::triggered,this,[this,source]{addEntries(source);});}addMenu->addSeparator();connect(addMenu->addAction(words("关联文献文件…","Link bibliography file…")),&QAction::triggered,attach,&QToolButton::click);connect(addMenu->addAction(words("使用示范…","Short demo…")),&QAction::triggered,this,[this]{showOnboardingTopic(this,QStringLiteral("citation"));});add->setMenu(addMenu);add->setPopupMode(QToolButton::InstantPopup);footer->addWidget(attach);footer->addWidget(add);footer->addStretch();auto *buttons=named(new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,this),"researchCitationButtons");apply=buttons->button(QDialogButtonBox::Ok);apply->setText(existing.valid()?words("更新","Update"):words("插入","Insert"));buttons->button(QDialogButtonBox::Cancel)->setText(words("取消","Cancel"));for(auto *b:buttons->buttons())configureButton(qobject_cast<QPushButton*>(b));apply->setEnabled(false);footer->addWidget(buttons);layout->addLayout(footer);
    connect(list,&QListWidget::itemSelectionChanged,this,[this]{selectionExplicit=true;QStringList visible;for(int i=0;i<list->count();++i)visible.append(list->item(i)->data(Qt::UserRole).toString());const auto selectedItems=list->selectedItems();QStringList next;for(const auto &key:chosenKeys)if(keyCounts.contains(key)&&(!visible.contains(key)||std::any_of(selectedItems.cbegin(),selectedItems.cend(),[&](auto *item){return item->data(Qt::UserRole).toString()==key;})))next.append(key);for(auto *item:list->selectedItems())if(!next.contains(item->data(Qt::UserRole).toString()))next.append(item->data(Qt::UserRole).toString());chosenKeys=next;updateInsertion();});
    connect(search,&QLineEdit::textChanged,this,[this]{filter();});connect(list,&QListWidget::currentItemChanged,this,[this]{selection();});connect(locator,&QLineEdit::textChanged,this,[this]{updateInsertion();});connect(style,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{updateInsertion();});connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
    connect(buttons,&QDialogButtonBox::accepted,this,[this]{if(managing||loading||reloadPending||reloadDelay.isActive())return;const auto text=snippet();if(text.isEmpty())return;const auto buffers=sourceBuffers();int matches=0;for(const auto &entry:entries)if(chosenKeys.contains(entry.key)){++matches;if(index&&((index->bibliographyBufferHashes.contains(entry.file)&&QCryptographicHash::hash(sourceOf(entry.file,buffers).toUtf8(),QCryptographicHash::Sha256)!=index->bibliographyBufferHashes.value(entry.file))||(index->parsedHashes.contains(entry.file)&&(slideExportHash(entry.file)!=index->parsedHashes.value(entry.file)||(buffers.contains(entry.file)&&buffers.value(entry.file)!=sourceOf(entry.file,{})))))){retainedNotice=words("文献来源已更新，正在重新读取…","References changed. Reloading…");notice->setText(retainedNotice);load();return;}}if(matches!=chosenKeys.size()){notice->setText(words("引用键重复，请先核对文献来源。","This citation key is duplicated. Check the bibliography first."));return;}if(edit.apply(text,{},notice)){if(edit.target){auto c=edit.target->textCursor();c.setPosition(edit.start+text.size());edit.target->editor()->setTextCursor(c);edit.target->editor()->setFocus();}accept();}else retainedNotice=notice->text();});
    connect(attach,&QToolButton::clicked,this,[this]{linkBibliography(suggestedBibliography);});
    if(managing){
      setWindowTitle(words("文献管理","Manage references"));apply->hide();buttons->button(QDialogButtonBox::Cancel)->setText(words("关闭","Close"));
      for(int i=0;i<locatorRow->count();++i)if(auto *widget=locatorRow->itemAt(i)->widget())widget->hide();
      insertion->hide();for(auto *action:addMenu->actions())if(action->isSeparator()||action->text()==words("关联文献文件…","Link bibliography file…"))action->setVisible(false);
    }
    reloadDelay.setSingleShot(true);reloadDelay.setInterval(250);
    connect(&reloadDelay,&QTimer::timeout,this,[this]{if(isVisible()&&(!QApplication::activeModalWidget()||QApplication::activeModalWidget()==this))load();else reloadOnReturn=true;});
    connect(&bibliographyWatch,&QFileSystemWatcher::fileChanged,this,[this]{scheduleReload();});
    connect(&bibliographyWatch,&QFileSystemWatcher::directoryChanged,this,[this]{bibliographyChanged();});
    load();connect(this,&QDialog::finished,w,[w]{
      // Hidden bibliography editors can become Qt's last active source window.
      // Restore the requesting manuscript after the dialog's own close handling.
      QTimer::singleShot(0,w,[w]{w->activateWindow();w->editor()->setFocus(Qt::OtherFocusReason);});
    });fitWorkspaceDialog(this,1000,650);search->setFocus();
  }
  void load(){reloadDelay.stop();if(loading){reloadPending=true;cancelled->store(true);return;}loading=true;cancelled->store(true);cancelled=std::make_shared<std::atomic_bool>(false);const auto token=cancelled;QPointer<CitationDialog> guard(this);const auto buffers=sourceBuffers();const auto root=edit.target?rootOf(edit.target):QString();apply->setEnabled(false);QThreadPool::globalInstance()->start(QRunnable::create([guard,token,root,buffers]{const auto result=buildProjectIndex(root,buffers,true,[token]{return token->load();});const auto candidates=result->bibliographies.isEmpty()&&!result->cancelled?unlinkedBibliographies(root,token):QStringList();if(qApp)QMetaObject::invokeMethod(qApp,[guard,token,result,candidates]{if(!guard)return;guard->loading=false;if(guard->reloadPending){guard->reloadPending=false;guard->load();return;}if(token->load()||result->cancelled)return;const int scroll=guard->list->verticalScrollBar()->value();if(guard->index&&!guard->chosenKeys.isEmpty())guard->selectionExplicit=true;guard->index=result;guard->entries.clear();guard->keyCounts.clear();guard->citationCounts.clear();for(const auto &use:result->uses)if(use.kind==QStringLiteral("citation-use"))++guard->citationCounts[use.key];guard->retainedNotice.clear();guard->shownKey.clear();for(const auto &entry:result->entries)if(entry.kind==QStringLiteral("cite")){guard->entries.append(entry);++guard->keyCounts[entry.key];}QStringList names;for(const auto &file:result->bibliographies)names.append(QFileInfo(file).fileName());guard->sources->setText(words("当前项目 · ","Current project · ")+names.join(QStringLiteral(", ")));guard->sources->setToolTip(result->bibliographies.join(QLatin1Char('\n')));guard->filter();guard->list->verticalScrollBar()->setValue(scroll);guard->watchBibliographies();guard->suggestedBibliography=candidates.size()==1?candidates.first():QString();if(auto *attach=guard->findChild<QToolButton*>(QStringLiteral("researchCitationAttach"))){attach->setText(guard->suggestedBibliography.isEmpty()?words("关联文献文件…","Link bibliography file…"):words("关联 %1…","Link %1…").arg(QFileInfo(guard->suggestedBibliography).fileName()));attach->setToolTip(guard->suggestedBibliography);configureButton(attach,ButtonRole::Text);attach->setVisible(!guard->managing&&guard->entries.isEmpty());}},Qt::QueuedConnection);}));}
  ~CitationDialog()override{cancelled->store(true);}
};
void citation(TeXDocumentWindow *w){if(!w||w->editor()->isReadOnly())return;if(auto *dialog=w->findChild<QDialog*>(QStringLiteral("researchCitationDialog"))){dialog->show();dialog->raise();return;}(new CitationDialog(w))->show();}
void wrapSelection(TeXDocumentWindow *w,bool footnote){if(!w||w->editor()->isReadOnly())return;auto c=w->textCursor();const auto content=c.selectedText().replace(QChar::ParagraphSeparator,QLatin1Char('\n'));if(!footnote&&content.isEmpty())return;const auto before=footnote?QStringLiteral("\\footnote{"):QStringLiteral("\\begin{quote}\n"),after=footnote?QStringLiteral("}"):QStringLiteral("\n\\end{quote}");const auto start=c.selectionStart();c.beginEditBlock();c.insertText(before+content+after);c.endEditBlock();c.setPosition(start+before.size());c.setPosition(start+before.size()+content.size(),QTextCursor::KeepAnchor);w->editor()->setTextCursor(c);w->editor()->setFocus();}
}

bool pasteResearchImage(TeXDocumentWindow *window,const QImage &image) {
  if(!window||window->editor()->isReadOnly()||image.isNull())return false;
  if(auto *existing=window->findChild<QDialog*>(QStringLiteral("researchObjectDialog"))){existing->show();existing->raise();return true;}
  auto *dialog=new ObjectDialog(window,false,true,{});
  if(!dialog->setClipboardImage(image)){delete dialog;showTransientStatusMessage(window,words("无法读取剪贴板图片，或图片超过 64 MB。","The clipboard image is unreadable or larger than 64 MB."));return true;}
  dialog->show();return true;
}
void editResearchObject(TeXDocumentWindow *window){if(!window)return;if(enclosingEnvironment(window->textDoc()->editorText(),window->cursorPosition(),mathStructures).valid()){openStructure(window);return;}const auto group=enclosingEnvironment(window->textDoc()->editorText(),window->cursorPosition(),{QStringLiteral("figure"),QStringLiteral("figure*")});if(group.valid()&&window->textDoc()->editorText().mid(group.start,group.end-group.start).count(QStringLiteral("\\includegraphics"))>1){openPictures(window);return;}const auto span=authoringObjectAt(window->textDoc()->editorText(),window->cursorPosition());if(span.valid())openObject(window,objectFamily(span.kind)==QStringLiteral("table"),false);}
void installResearchAuthoring(TeXDocumentWindow *window) {
  if(window->findChild<QAction*>(QStringLiteral("researchInsertFigure")))return;
  auto *edit=window->findChild<QMenu*>(QStringLiteral("researchEditMenu"));if(!edit)return;auto *menu=window->findChild<QMenu*>(QStringLiteral("researchInsertMenu"));if(!menu)return;
  auto add=[window,menu](const char *name,const QString &text,const std::function<void()> &callback){auto *a=menu->addAction(text);a->setObjectName(QString::fromLatin1(name));QObject::connect(a,&QAction::triggered,window,callback);return a;};
  add("researchInsertFigure",words("图…","Figure…"),[window]{openObject(window,false,true);});add("researchPictureGroup",words("多图排版…","Arrange pictures…"),[window]{openPictures(window);});add("researchFormulaStructure",words("公式结构…","Formula structure…"),[window]{openStructure(window);});add("researchInsertTable",words("表…","Table…"),[window]{openObject(window,true,true);});add("researchInsertCitation",words("文献引用…","Citation…"),[window]{citation(window);});add("researchInsertFootnote",words("脚注","Footnote"),[window]{wrapSelection(window,true);});
  auto *drawing=new QAction(words("绘图（TikZ）…","Drawing (TikZ)…"),window);drawing->setObjectName(QStringLiteral("researchTikzDrawing"));QObject::connect(drawing,&QAction::triggered,window,[window]{openTikz(window,true,{},Tikz::emptyPicture());});QObject::connect(TWApp::instance(),&TWApp::updatedTranslators,window,[drawing]{drawing->setText(words("绘图（TikZ）…","Drawing (TikZ)…"));});
  auto citationCaption=[window]{if(auto *a=window->findChild<QAction*>(QStringLiteral("researchInsertCitation")))a->setText(QFileInfo(window->fileName()).suffix().compare(QStringLiteral("bib"),Qt::CaseInsensitive)==0?words("管理文献…","Manage references…"):words("文献引用…","Citation…"));};
  QObject::connect(menu,&QMenu::aboutToShow,window,citationCaption);QObject::connect(window,&TeXDocumentWindow::activatedWindow,window,citationCaption);QTimer::singleShot(0,window,citationCaption);
  auto *reveal=new QAction(words("逐步显示…","Progressive reveal…"),window);reveal->setObjectName(QStringLiteral("researchSlideReveal"));edit->addAction(reveal);QObject::connect(reveal,&QAction::triggered,window,[window]{openReveals(window);});QObject::connect(edit,&QMenu::aboutToShow,window,[window,reveal]{reveal->setVisible(enclosingEnvironment(window->textDoc()->editorText(),window->cursorPosition(),{QStringLiteral("frame")}).valid());reveal->setEnabled(!window->editor()->isReadOnly());});
  auto *quote=new QAction(words("设为引文","Set as quotation"),window);quote->setObjectName(QStringLiteral("researchWrapQuotation"));menu->addAction(quote);QObject::connect(quote,&QAction::triggered,window,[window]{wrapSelection(window,false);});
  QObject::connect(menu,&QMenu::aboutToShow,window,[window,quote]{quote->setEnabled(window->textCursor().hasSelection()&&!window->editor()->isReadOnly());});
  auto *adjust=new QAction(words("编辑此处…","Edit this object…"),window);adjust->setObjectName(QStringLiteral("researchEditObject"));edit->addAction(adjust);QObject::connect(adjust,&QAction::triggered,window,[window]{editResearchObject(window);});
  QObject::connect(edit,&QMenu::aboutToShow,window,[window,quote,adjust]{quote->setEnabled(window->textCursor().hasSelection()&&!window->editor()->isReadOnly());adjust->setEnabled(!window->editor()->isReadOnly()&&(authoringObjectAt(window->textDoc()->editorText(),window->cursorPosition()).valid()||enclosingEnvironment(window->textDoc()->editorText(),window->cursorPosition(),mathStructures).valid()));});
  QObject::connect(TWApp::instance(),&TWApp::updatedTranslators,window,[window,menu,quote,adjust,citationCaption]{menu->setTitle(words("插入","Insert"));quote->setText(words("设为引文","Set as quotation"));adjust->setText(words("编辑此处…","Edit this object…"));const QList<QPair<QString,QString>> captions{{QStringLiteral("researchInsertFigure"),words("图…","Figure…")},{QStringLiteral("researchInsertTable"),words("表…","Table…")},{QStringLiteral("researchInsertCitation"),words("文献引用…","Citation…")},{QStringLiteral("researchInsertFootnote"),words("脚注","Footnote")}};for(const auto &pair:captions)if(auto *action=window->findChild<QAction*>(pair.first))action->setText(pair.second);citationCaption();if(auto *a=window->findChild<QAction*>(QStringLiteral("researchPictureGroup")))a->setText(words("多图排版…","Arrange pictures…"));if(auto *a=window->findChild<QAction*>(QStringLiteral("researchFormulaStructure")))a->setText(words("公式结构…","Formula structure…"));if(auto *a=window->findChild<QAction*>(QStringLiteral("researchSlideReveal")))a->setText(words("逐步显示…","Progressive reveal…"));});
}
void addResearchAuthoringContext(QMenu *menu,TeXDocumentWindow *window,int position){
  if(!menu||!window||window->editor()->isReadOnly())return;menu->addSeparator();
  if(QFileInfo(window->fileName()).suffix().compare(QStringLiteral("bib"),Qt::CaseInsensitive)==0)if(auto *action=window->findChild<QAction*>(QStringLiteral("researchInsertCitation"))){action->setText(words("管理文献…","Manage references…"));menu->addAction(action);return;}
  if(window->textCursor().hasSelection())if(auto *snippet=window->findChild<QAction *>(QStringLiteral("researchSaveSnippet")))menu->addAction(snippet);
  if(window->textCursor().hasSelection())if(auto *a=window->findChild<QAction*>(QStringLiteral("researchWrapQuotation"))){a->setEnabled(true);menu->addAction(a);}
  if(citationAt(window->textDoc()->editorText(),position).valid()){auto *action=menu->addAction(words("修改文献引用…","Edit citation…"));action->setObjectName(QStringLiteral("researchEditCitation"));QPointer<TeXDocumentWindow> guard(window);QObject::connect(action,&QAction::triggered,menu,[guard,position]{if(guard){auto cursor=guard->textCursor();cursor.setPosition(position);guard->editor()->setTextCursor(cursor);citation(guard);}});}
  QTextCursor clicked(window->textDoc());clicked.setPosition(qBound(0,position,window->textDoc()->characterCount()-1));
  if(authoringObjectAt(window->textDoc()->editorText(),clicked.position()).valid()||enclosingEnvironment(window->textDoc()->editorText(),clicked.position(),mathStructures).valid()){
    auto *action=menu->addAction(words("编辑此处…","Edit this object…"));action->setObjectName(QStringLiteral("researchContextEditObject"));QPointer<TeXDocumentWindow> guard(window);
    QObject::connect(action,&QAction::triggered,menu,[guard,clicked]{if(guard){guard->editor()->setTextCursor(clicked);editResearchObject(guard);}});
  }
  if(enclosingEnvironment(window->textDoc()->editorText(),position,{QStringLiteral("frame")}).valid()){auto *a=menu->addAction(words("逐步显示…","Progressive reveal…"));a->setObjectName(QStringLiteral("researchContextReveal"));QPointer<TeXDocumentWindow> guard(window);QObject::connect(a,&QAction::triggered,menu,[guard,position]{if(!guard)return;auto cursor=guard->textCursor();if(!cursor.hasSelection()||position<cursor.selectionStart()||position>cursor.selectionEnd()){cursor.setPosition(position);guard->editor()->setTextCursor(cursor);}openReveals(guard);});}
  if(auto *insert=window->findChild<QMenu*>(QStringLiteral("researchInsertMenu")))menu->addMenu(insert);
}
}
