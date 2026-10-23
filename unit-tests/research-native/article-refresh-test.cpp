// A body edit must update navigation without destroying an unchanged outline.
#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "TeXDocumentWindow.h"
#include "CompletingEdit.h"
#include "ResearchArticle.h"
#include "Settings.h"
#include <QAction>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPersistentModelIndex>
#include <QThread>
#include <QTimer>
#include <QTreeWidget>
#include <QToolButton>
#include <windows.h>
#include <shellapi.h>
static bool await(const std::function<bool()> &f,int ms=20000){QElapsedTimer t;t.start();while(!f()&&t.elapsed()<ms){QCoreApplication::processEvents();QThread::msleep(1);}return f();}
static void pauseFor(int ms){QElapsedTimer t;t.start();await([&]{return t.elapsed()>=ms;},ms+1000);}
static void put(const QString &p,const QByteArray &v){QDir().mkpath(QFileInfo(p).absolutePath());QFile f(p);if(f.open(QIODevice::WriteOnly))f.write(v);}
int main(int argc,char **argv){if(!qEnvironmentVariable("TEXLINE_TEST_DESKTOP").startsWith("TeXlineValidation-"))return 2;int n=0;auto **args=CommandLineToArgvW(GetCommandLineW(),&n);const QString report=QString::fromWCharArray(args[1]);LocalFree(args);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
 QTimer::singleShot(80,&app,[&]{const auto folder=QFileInfo(report).absolutePath();QJsonArray checks;int failures=0;auto check=[&](QString name,bool good){failures+=!good;checks.append(QJsonObject{{"name",name},{"passed",good}});put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures}}).toJson());};Tw::Settings().setValue("openPDFwithTeX",false);Tw::Settings().setValue("researchFocusMode",false);
  QString source="\\documentclass{article}\n\\begin{document}\n";for(int sec=0;sec<60;++sec){source+="\\section{Transport regime "+QString::number(sec)+"}\nBoundary flux and concentration.\n";for(int obj=0;obj<10;++obj)source+="\\begin{equation}c_t=D\\nabla^2c\\label{eq:r"+QString::number(sec)+"n"+QString::number(obj)+"}\\end{equation}\n";}source+="\\end{document}\n";const QString path=folder+"/project/transport.tex";put(path,source.toUtf8());auto *w=new TeXDocumentWindow(path);w->resize(1120,760);w->show();check("article detection ready",await([&]{return w->property("researchArticleReady").toBool()&&w->property("researchArticleMode").toBool();}));auto *outline=w->findChild<QAction*>("researchOutlineAction");if(!outline->isChecked())outline->trigger();auto *objects=w->findChild<QToolButton*>("researchArticleObjects");objects->setChecked(true);auto *tree=w->findChild<QTreeWidget*>("researchArticleTree");pauseFor(250);
  QTreeWidgetItem *first=nullptr;for(int i=0;i<tree->topLevelItemCount();++i)if(tree->topLevelItem(i)->text(0)=="Transport regime 0")first=tree->topLevelItem(i);check("actual section and objects available",first&&first->childCount()==10);if(!first){w->setModified(false);delete w;app.exit(1);return;}
  first->setExpanded(true);pauseFor(200);const QPersistentModelIndex persistent(tree->model()->index(tree->indexOfTopLevelItem(first),0));
  auto cursor=w->textCursor();cursor.setPosition(source.indexOf("Boundary flux"));w->editor()->setTextCursor(cursor);w->editor()->insertPlainText("Additional boundary assumptions.\n");check("body edit reparses current source",await([&]{return w->property("researchArticleReady").toBool();}));
  check("body-only parsing preserves the live outline row",persistent.isValid());check("expanded reading branch remains expanded",tree->topLevelItem(1)&&tree->topLevelItem(1)->isExpanded());
  QTreeWidgetItem *last=nullptr;{QTreeWidgetItemIterator it(tree);while(*it){if((*it)->text(0)=="Transport regime 59")last=*it;++it;}}check("later section remains navigable",last);if(last){tree->itemClicked(last,0);const auto current=w->textDoc()->editorText();check("retained row navigates to updated source offsets",w->cursorPosition()==current.indexOf("\\section{Transport regime 59}"));}
  w->editor()->undo();check("body edit undo restores exact source",w->textDoc()->editorText()==source);await([&]{return w->property("researchArticleReady").toBool();});cursor=w->textCursor();const int title=int(source.indexOf("Transport regime 5}"));cursor.setPosition(title);cursor.setPosition(title+18,QTextCursor::KeepAnchor);cursor.insertText("Changed regime five");check("a real heading change reparses",await([&]{return w->property("researchArticleReady").toBool();}));bool renamed=false;{QTreeWidgetItemIterator names(tree);while(*names){renamed|=(*names)->text(0)=="Changed regime five";++names;}}check("actual outline changes remain visible",renamed);w->editor()->undo();check("heading change undo restores exact source",w->textDoc()->editorText()==source);w->setModified(false);delete w;app.exit(failures?1:0);
 });return app.exec();}
