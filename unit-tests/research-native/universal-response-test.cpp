// Same-path synthetic input-to-verified-paint measurements; not physical latency.
// GPL-2.0-or-later.
#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "ResearchAppearance.h"
#include "TeXDocumentWindow.h"
#include "CompletingEdit.h"
#include "TWApp.h"
#include "Settings.h"
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPointer>
#include <QTextBlock>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <functional>

static bool waitFor(const std::function<bool()> &ready,int ms=20000){QElapsedTimer timer;timer.start();while(!ready()&&timer.elapsed()<ms){QCoreApplication::processEvents();QThread::msleep(2);}return ready();}
static void settle(int ms=200){QElapsedTimer timer;timer.start();waitFor([&]{return timer.elapsed()>=ms;},ms+1000);}
static bool put(const QString &file,const QString &text){QDir().mkpath(QFileInfo(file).absolutePath());QFile output(file);return output.open(QIODevice::WriteOnly)&&output.write(text.toUtf8())==text.toUtf8().size();}
int main(int argc,char **argv){const QString report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
 QTimer::singleShot(60,&app,[&]{
  QJsonArray checks,timings;int failures=0;const auto folder=QFileInfo(report).absolutePath();
  auto save=[&]{QFile output(report);if(output.open(QIODevice::WriteOnly))output.write(QJsonDocument(QJsonObject{{"checks",checks},{"timings",timings},{"failures",failures},{"physicalInput",false}}).toJson());};
  auto check=[&](const QString &name,bool passed){checks.append(QJsonObject{{"name",name},{"passed",passed}});failures+=!passed;save();};
  if(!qEnvironmentVariable("TEXLINE_TEST_DESKTOP").startsWith("TeXlineValidation-")){check("inactive private desktop required",false);app.exit(1);return;}
  Tw::Settings().setValue("openPDFwithTeX",false);Tw::Settings().setValue("researchReduceMotion",true);
  const QString root=qEnvironmentVariable("TEXLINE_MATURITY_PERF_ROOT");
  if(root.isEmpty()||!root.contains("2026-10-20-universal-maturity")){check("explicit independent shared-path fixture required",false);app.exit(1);return;}
  QString text="\\documentclass{article}\n\\usepackage{graphicx}\n\\begin{document}\n";
  for(int section=0;section<60;++section){text+="\\section{Section "+QString::number(section)+"}\n";for(int row=0;row<60;++row)text+="Repeated research paragraph "+QString::number(row)+".\n";}
  text+="\\begin{figure}\n\\centering\n\\includegraphics[width=.6\\linewidth]{sample.png}\n\\caption{Initial caption}\\label{fig:sample}\n\\end{figure}\n\\bibliography{refs}\n\\end{document}\n";
  QString bib;for(int entry=0;entry<4000;++entry)bib+="@article{fixture"+QString::number(entry)+",author={Example, Alice},title={Fictional archive record "+QString::number(entry)+"},year={2026},journal={Independent fixture journal}}\n";
  check("fixed-path documents are reproducible",put(root+"/main.tex",text)&&put(root+"/refs.bib",bib));QImage asset(600,250,QImage::Format_RGB32);asset.fill(QColor("#abc7d3"));asset.save(root+"/sample.png");
  auto *window=new TeXDocumentWindow(root+"/main.tex");window->resize(1100,800);window->show();check("long article becomes ready",waitFor([&]{return window->property("researchArticleReady").toBool();}));
  auto cursor=window->textCursor();cursor.setPosition(text.indexOf("paragraph")+9);window->editor()->setTextCursor(cursor);window->editor()->ensureCursorVisible();QCoreApplication::processEvents();
  auto source=window->textDoc()->editorText();const int point=window->cursorPosition();QJsonArray values;const auto originalRaster=window->editor()->viewport()->grab().toImage();
  for(int i=0;i<24;++i){QElapsedTimer timer;timer.start();QKeyEvent key(QEvent::KeyPress,Qt::Key_X,Qt::NoModifier,"x");QApplication::sendEvent(window->editor(),&key);QCoreApplication::processEvents();const auto raster=window->editor()->viewport()->grab(QRect(0,0,800,220)).toImage();values.append(double(timer.nsecsElapsed())/1e6);if(raster.isNull())check("input paint exists",false);}
  source.insert(point,QString(24,'x'));check("long article input reaches exact source and changed pixels",window->textDoc()->editorText()==source&&window->editor()->viewport()->grab().toImage()!=originalRaster);timings.append(QJsonObject{{"mode","long-source"},{"eventProcessAndVerifiedRasterMs",values},{"lines",window->textDoc()->blockCount()}});window->editor()->undo();settle(600);
  cursor=window->textCursor();cursor.setPosition(window->textDoc()->editorText().indexOf("includegraphics"));window->editor()->setTextCursor(cursor);window->findChild<QAction*>("researchEditObject")->trigger();auto *figure=window->findChild<QDialog*>("researchObjectDialog");
  check("figure tool is available",figure);if(figure){figure->findChild<QToolButton*>("researchObjectPreviewToggle")->setChecked(false);settle(600);auto *caption=figure->findChild<QPlainTextEdit*>("researchObjectCaption");caption->setFocus();cursor=caption->textCursor();cursor.movePosition(QTextCursor::End);caption->setTextCursor(cursor);const auto before=caption->toPlainText();values={};
   for(int i=0;i<18;++i){QElapsedTimer timer;timer.start();QKeyEvent key(QEvent::KeyPress,Qt::Key_X,Qt::NoModifier,"x");QApplication::sendEvent(caption,&key);QCoreApplication::processEvents();auto raster=caption->viewport()->grab().toImage();values.append(double(timer.nsecsElapsed())/1e6);if(raster.isNull())check("caption paint exists",false);}
   check("caption typing reaches the draft without changing the manuscript",caption->toPlainText()==before+QString(18,'x')&&window->textDoc()->editorText()==text);timings.append(QJsonObject{{"mode","figure-caption"},{"eventProcessAndVerifiedRasterMs",values}});figure->reject();settle(200);
  }
  window->findChild<QAction*>("researchInsertCitation")->trigger();auto *citation=window->findChild<QDialog*>("researchCitationDialog");check("large local bibliography loads",citation&&waitFor([&]{return citation->findChild<QListWidget*>("researchCitationList")->count()==200;}));
  if(citation){settle(500);auto *locator=citation->findChild<QLineEdit*>("researchCitationLocator");locator->setFocus();const auto before=locator->text();const auto initial=locator->grab().toImage();values={};
   for(int i=0;i<18;++i){QElapsedTimer timer;timer.start();QKeyEvent key(QEvent::KeyPress,Qt::Key_1,Qt::NoModifier,"1");QApplication::sendEvent(locator,&key);QCoreApplication::processEvents();auto raster=locator->grab().toImage();values.append(double(timer.nsecsElapsed())/1e6);if(raster.isNull())check("locator paint exists",false);}
   check("locator typing reaches visible text and keeps original source",locator->text()==before+QString(18,'1')&&locator->grab().toImage()!=initial&&window->textDoc()->editorText()==text);timings.append(QJsonObject{{"mode","citation-locator"},{"eventProcessAndVerifiedRasterMs",values},{"bibliographyEntries",4000}});citation->reject();settle(100);
  }
  QFile original(root+"/main.tex");check("disk source is unchanged",original.open(QIODevice::ReadOnly)&&original.readAll()==text.toUtf8());save();window->setModified(false);delete window;app.exit(failures?1:0);
 });QTimer::singleShot(180000,&app,[&]{app.exit(92);});return app.exec();
}
