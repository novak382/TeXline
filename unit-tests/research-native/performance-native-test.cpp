#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "TeXDocumentWindow.h"
#include "PDFDocumentWindow.h"
#include "CompletingEdit.h"
#include "ResearchBuild.h"
#include "ResearchWorkflow.h"
#include "ResearchAppearance.h"
#include "Settings.h"
#include <QAction>
#include <QComboBox>
#include <QMenu>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QScrollBar>
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <QDir>
#include <functional>
static bool waitFor(const std::function<bool()> &ready,int limit=180000){QElapsedTimer t;t.start();while(!ready()&&t.elapsed()<limit){QCoreApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QThread::msleep(4);}return ready();}
static void settle(int ms=180){QElapsedTimer t;t.start();waitFor([&]{return t.elapsed()>=ms;},ms+500);}
static bool put(const QString &path,const QByteArray &bytes){QSaveFile f(path);return f.open(QIODevice::WriteOnly)&&f.write(bytes)==bytes.size()&&f.commit();}
static QByteArray read(const QString &path){QFile f(path);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray();}
int main(int argc,char **argv){const QString report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);QTimer::singleShot(100,&app,[&]{
  QJsonArray checks,runs;int failures=0;const QString assets=QCoreApplication::applicationDirPath();
  auto save=[&]{put(report,QJsonDocument(QJsonObject{{"checks",checks},{"runs",runs},{"failures",failures}}).toJson());};
  auto check=[&](const QString &name,bool pass){checks.append(QJsonObject{{"name",name},{"passed",pass}});failures+=!pass;save();};
  Tw::Settings().setValue("openPDFwithTeX",false);Research::setFocusMode(false);Research::setBuildCacheForTests(QFileInfo(report).absolutePath()+"/performance-build-cache");
  const QStringList roots{assets+QStringLiteral("/papers/benchmark main 研究 论文/main.tex"),assets+QStringLiteral("/papers/benchmark supp 研究 补充/supp-1008.tex")};
  for(const QString &root:roots){const QString name=QFileInfo(root).fileName();const QString artifact=QDir(QFileInfo(root).absolutePath()).dirName()+"-"+name;check(name+": independent real source exists",QFileInfo::exists(root));if(!QFileInfo::exists(root))continue;
    const QByteArray original=read(root),oldInputs=qgetenv("TEXINPUTS");if(name=="lshort.tex")qputenv("TEXINPUTS",(QDir(QFileInfo(root).absolutePath()+"/..").absolutePath()+"//;").toLocal8Bit()+oldInputs);
    QElapsedTimer opening;opening.start();auto *w=new TeXDocumentWindow(root);w->resize(1100,800);w->show();settle();const qint64 openMs=opening.elapsed();
    check(name+": main-file detection resolves this project",QFileInfo(w->textDoc()->getRootFilePath())==QFileInfo(root));
    QMenu engineMenu;w->populateResearchEngineMenu(&engineMenu);bool engineIsPdf=false;for(auto *action:engineMenu.actions())if(action->isChecked()&&Research::canonicalEngineName(action->text())==QStringLiteral("pdfLaTeX"))engineIsPdf=true;check(name+": automatic engine is pdfLaTeX",engineIsPdf);
    PDFDocumentWindow *v=nullptr;
    for(int attempt=0;attempt<3;++attempt){QElapsedTimer build;build.start();w->typeset();const bool finished=waitFor([&]{return !w->isTypesetting();});const qint64 elapsed=build.elapsed();
      put(QFileInfo(report).absolutePath()+"/"+artifact+QString("-run-%1-console.txt").arg(attempt),w->property("consoleOutput").toString().toUtf8());
      const QString pdf=Research::previewFile(root);const bool paired=Research::pdfPairValid(pdf);v=w->pdfDocument();
      runs.append(QJsonObject{{"root",root},{"attempt",attempt},{"openMs",double(openMs)},{"buildMs",double(elapsed)},{"finished",finished},{"paired",paired},{"pages",v&&v->widget()->document()?v->widget()->document().toStrongRef()->numPages():0},{"pdf",pdf}});save();
      check(name+QString(": real compilation %1 finishes with a valid PDF/SyncTeX pair").arg(attempt),finished&&paired&&v);
      if(!finished){w->interrupt();waitFor([&]{return !w->isTypesetting();},10000);w->setModified(false);w->close();app.exit(1);return;}
      if(!paired||!v)break;
      if(attempt==0){v->resize(1100,800);v->show();v->widget()->setZoomLevel(1.1);v->widget()->goToPage(qMin(5,int(v->widget()->document().toStrongRef()->numPages())-1));settle();}
    }
    if(v&&Research::pdfPairValid(Research::previewFile(root))){const auto d=v->widget()->document().toStrongRef();const QString text=w->editor()->toPlainText();QTextCursor c(w->textDoc());c.setPosition(text.size()/2);w->editor()->setTextCursor(c);
      for(int i=0;i<80;++i){auto cursor=w->editor()->textCursor();cursor.insertText(QString("%% maturity %1\n").arg(i));}for(int i=0;i<80;++i)w->editor()->undo();
      check(name+": 80 edit/undo operations preserve the complete manuscript",w->editor()->toPlainText()==text);
      for(int i=0;i<80;++i)w->editor()->redo();for(int i=0;i<80;++i)w->editor()->undo();check(name+": redo/undo does not lose mathematical source",w->editor()->toPlainText()==text);w->setModified(false);
      TWSyncTeXSynchronizer sync(Research::previewFile(root),[&](const QString &file){auto *found=TeXDocumentWindow::findDocument(file);return found?found->textDoc():nullptr;},[&](const QString &){return d;},QFileInfo(root).absolutePath());
      const QStringList files=Research::projectFiles(root);int forwards=0,backwards=0;for(const QString &file:files){QFile f(file);if(!f.open(QIODevice::ReadOnly))continue;const QString source=QString::fromUtf8(f.readAll());const auto lines=source.split('\n');for(int line=1;line<lines.size();++line){const QString s=lines[line-1].trimmed();if(s.size()<24||s.startsWith('%')||s.startsWith('\\'))continue;auto point=sync.syncFromTeX({file,line,6},TWSynchronizer::LineResolution);if(point.page<1||point.rects.isEmpty())continue;++forwards;auto inverse=sync.syncFromPDF({Research::previewFile(root),point.page,{QRectF(point.rects.first().center(),QSizeF())}},TWSynchronizer::LineResolution);const QString result=QFileInfo(QFileInfo(root).absoluteDir().filePath(inverse.filename)).absoluteFilePath();if(QFileInfo(result).isFile()&&inverse.line>0)++backwards;break;}}
      check(name+": real project has valid forward and inverse source locations",forwards>0&&backwards==forwards);
      runs.append(QJsonObject{{"root",root},{"sourceFiles",files.size()},{"forwardLocations",forwards},{"inverseLocations",backwards}});save();
      Research::setFocusMode(true);settle();w->grab().save(QFileInfo(report).absolutePath()+"/"+artifact+"-source-focus.png");v->grab().save(QFileInfo(report).absolutePath()+"/"+artifact+"-pdf-focus.png");Research::setFocusMode(false);
    }
    check(name+": tests left the copied source byte-for-byte intact",read(root)==original);if(v)v->close();QPointer<TeXDocumentWindow> guard(w);w->setModified(false);w->close();settle();check(name+": real source window releases after closing",!guard);qputenv("TEXINPUTS",oldInputs);
  }
  save();app.exit(failures?1:0);
});return app.exec();}



