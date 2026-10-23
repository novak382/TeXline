// Verify the existing section view bounds reflow work while preserving full source.
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
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QPixmap>
#include <QScrollBar>
#include <QTextBlock>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <windows.h>
#include <shellapi.h>
static bool await(const std::function<bool()> &f,int ms=25000){QElapsedTimer t;t.start();while(!f()&&t.elapsed()<ms){QCoreApplication::processEvents();QThread::msleep(1);}return f();}
static void settle(int ms){QElapsedTimer t;t.start();await([&]{return t.elapsed()>=ms;},ms+1000);}
static void put(const QString &p,const QByteArray &v){QDir().mkpath(QFileInfo(p).absolutePath());QSaveFile f(p);if(f.open(QIODevice::WriteOnly)&&f.write(v)==v.size())f.commit();}
struct PaintTrace : QObject {int count{0};bool eventFilter(QObject *o,QEvent *e)override{count+=e->type()==QEvent::Paint;return QObject::eventFilter(o,e);}};
int main(int argc,char **argv){if(!qEnvironmentVariable("TEXLINE_TEST_DESKTOP").startsWith("TeXlineValidation-"))return 2;int n=0;auto **args=CommandLineToArgvW(GetCommandLineW(),&n);const QString report=QString::fromWCharArray(args[1]);LocalFree(args);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
 QTimer::singleShot(80,&app,[&]{const QString folder=QFileInfo(report).absolutePath();QJsonArray checks,timings;int failures=0;auto save=[&]{put(report,QJsonDocument(QJsonObject{{"checks",checks},{"timings",timings},{"failures",failures}}).toJson());};auto check=[&](QString name,bool good){failures+=!good;checks.append(QJsonObject{{"name",name},{"passed",good}});save();};Tw::Settings().setValue("openPDFwithTeX",false);Tw::Settings().setValue("researchFocusMode",false);
  QString source="\\documentclass{article}\n\\begin{document}\n";for(int sec=0;sec<160;++sec){source+="\\section{Transport and diffusion "+QString::number(sec)+"}\n";for(int row=0;row<65;++row)source+="The concentration evolves under a conserved flux. We compare the boundary conditions with the stationary solution and retain the spatial discretization in the error estimate.\n";}source+="\\end{document}\n";const QString path=folder+"/project/transport.tex";put(path,source.toUtf8());auto *w=new TeXDocumentWindow(path);w->resize(1160,780);w->show();check("large article ready",await([&]{return w->property("researchArticleReady").toBool()&&w->property("researchArticleMode").toBool();}));auto cursor=w->textCursor();cursor.setPosition(int(source.indexOf("The concentration",source.size()/2)));w->editor()->setTextCursor(cursor);w->editor()->ensureCursorVisible();settle(800);auto *section=w->findChild<QToolButton*>("researchArticleSection");auto *full=w->findChild<QToolButton*>("researchArticleFull");check("current section is available",section&&section->isEnabled());QElapsedTimer switching;switching.start();if(section)section->click();const double switchMs=double(switching.nsecsElapsed())/1e6;int visible=0;for(auto b=w->textDoc()->begin();b.isValid();b=b.next())visible+=b.isVisible();check("section view retains complete source with a bounded visible range",w->editor()->property("researchSourceSlice").toBool()&&visible>20&&visible<100&&w->textDoc()->editorText()==source);settle(500);
  auto *outline=w->findChild<QAction*>("researchOutlineAction");Tw::Settings().setValue("researchReduceMotion",true);if(outline&&outline->isChecked())outline->trigger();Tw::Settings().setValue("researchReduceMotion",false);auto *veil=w->findChild<QWidget*>("researchPanelMotionVeil");
  for(int i=0;i<12;++i){PaintTrace trace;if(veil)veil->installEventFilter(&trace);QElapsedTimer clock;clock.start();outline->trigger();const double callMs=double(clock.nsecsElapsed())/1e6;settle(215);if(veil)veil->removeEventFilter(&trace);timings.append(QJsonObject{{"opening",i%2==0},{"callMs",callMs},{"naturalPaints",trace.count},{"visibleBlocks",visible},{"sourceCharacters",source.size()},{"switchToSectionMs",switchMs}});check("section animation settles without text or cursor loss",veil&&!veil->isVisible()&&w->textDoc()->editorText()==source&&w->editor()->property("researchSourceSlice").toBool());}
  w->grab().save(folder+"/section-final.png");if(full)full->click();check("complete source can always be restored",!w->editor()->property("researchSourceSlice").toBool()&&w->textDoc()->editorText()==source);w->setModified(false);delete w;save();app.exit(failures?1:0);
 });return app.exec();}
