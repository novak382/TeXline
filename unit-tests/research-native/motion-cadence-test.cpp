// Natural Qt paint cadence; no forced captures in timed transitions.
// Hidden desktop timings do not certify foreground compositor timing.
#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "ResearchSlides.cpp"
#include "ResearchArticle.h"
#include "ResearchMotion.h"
#include <QJsonArray>
#include <QThread>
#include <QTreeWidget>
#include <QScreen>
#include <windows.h>
#include <psapi.h>
#include <shellapi.h>
static bool await(const std::function<bool()> &f,int ms=30000){QElapsedTimer c;c.start();while(!f()&&c.elapsed()<ms){QCoreApplication::processEvents();QThread::msleep(1);}return f();}
static void pauseFor(int ms=230){QElapsedTimer c;c.start();await([&]{return c.elapsed()>=ms;},ms+2000);}
static void writeFile(const QString &p,const QByteArray &v){QDir().mkpath(QFileInfo(p).absolutePath());QFile f(p);if(f.open(QIODevice::WriteOnly))f.write(v);}
static qint64 privateBytes(){PROCESS_MEMORY_COUNTERS_EX s{};s.cb=sizeof(s);using F=BOOL(WINAPI*)(HANDLE,PPROCESS_MEMORY_COUNTERS,DWORD);auto f=reinterpret_cast<F>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"K32GetProcessMemoryInfo"));return f&&f(GetCurrentProcess(),reinterpret_cast<PPROCESS_MEMORY_COUNTERS>(&s),sizeof(s))?qint64(s.PrivateUsage):0;}
struct Cadence : QObject {
  QElapsedTimer clock;QTimer beat;QJsonArray paints,gaps,resizes;QPointer<QWidget> resizeTarget;double previous{0};bool active{false};int sourcePaints{0};
  Cadence(){beat.setTimerType(Qt::PreciseTimer);beat.setInterval(4);connect(&beat,&QTimer::timeout,this,[this]{if(!active)return;const double t=double(clock.nsecsElapsed())/1e6;gaps.append(t-previous);previous=t;});}
  void start(){paints={};gaps={};resizes={};previous=0;sourcePaints=0;clock.start();active=true;beat.start();}
  QJsonObject stop(double callMs){active=false;beat.stop();return {{"callMs",callMs},{"sourcePaints",sourcePaints},{"paintAtMs",paints},{"heartbeatGapMs",gaps},{"viewportResizes",resizes}};}
  bool eventFilter(QObject *o,QEvent *e)override{if(active&&e->type()==QEvent::Paint&&o==resizeTarget)++sourcePaints;if(active&&e->type()==QEvent::Paint&&o!=resizeTarget)paints.append(double(clock.nsecsElapsed())/1e6);if(active&&o==resizeTarget&&e->type()==QEvent::Resize){auto *r=static_cast<QResizeEvent*>(e);resizes.append(QJsonObject{{"atMs",double(clock.nsecsElapsed())/1e6},{"width",r->size().width()},{"height",r->size().height()}});}return QObject::eventFilter(o,e);}
};
int main(int argc,char **argv){
 if(!qEnvironmentVariable("TEXLINE_TEST_DESKTOP").startsWith("TeXlineValidation-"))return 2;
 int n=0;auto **args=CommandLineToArgvW(GetCommandLineW(),&n);const QString report=QString::fromWCharArray(args[1]);LocalFree(args);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
 QTimer::singleShot(80,&app,[&]{
  QJsonArray checks,samples;int failures=0;const QString folder=QFileInfo(report).absolutePath();
  auto save=[&]{writeFile(report,QJsonDocument(QJsonObject{{"checks",checks},{"samples",samples},{"failures",failures},{"captureDuringTiming",false},{"physicalCompositor",false}}).toJson());};
  auto check=[&](QString name,bool good){failures+=!good;checks.append(QJsonObject{{"name",name},{"passed",good}});save();};
  auto record=[&](QWidget *surface,const QString &name,const std::function<void()> &action){
   if(!qEnvironmentVariableIsSet("TEXLINE_MOTION_RECORD"))return;
   const QString path=folder+"/clips/"+name;QDir().mkpath(path);QVector<QImage> images;QJsonArray entries;QElapsedTimer time;time.start();
   images.append(surface->grab().toImage());entries.append(QJsonObject{{"file","000.png"},{"atMs",0}});action();
   for(int i=1;i<=17;++i){pauseFor(16);images.append(surface->grab().toImage());entries.append(QJsonObject{{"file",QString("%1.png").arg(i,3,10,QChar('0'))},{"atMs",double(time.elapsed())}});}
   for(int i=0;i<images.size();++i)images[i].save(path+"/"+entries[i].toObject()["file"].toString());writeFile(path+"/frames.json",QJsonDocument(QJsonObject{{"frames",entries},{"capture","Actual widget rasters; scripted actions; outside timed samples"}}).toJson());
  };
  Tw::Settings().setValue("openPDFwithTeX",false);Tw::Settings().setValue("researchFocusMode",false);Tw::Settings().setValue("researchReduceMotion",false);
  const QString fixed=qEnvironmentVariable("TEXLINE_MOTION_PERF_ROOT");check("explicit independent fixed-path project",fixed.contains("2026-10-20-motion-maturity"));if(fixed.isEmpty()){app.exit(1);return;}
  for(bool large:{false,true}){
   QString source="\\documentclass{article}\n\\begin{document}\n";
   for(int sec=0;sec<(large?160:5);++sec){source+="\\section{Transport and diffusion "+QString::number(sec)+"}\n";for(int row=0;row<(large?65:3);++row)source+="The concentration evolves under a conserved flux. We compare the boundary conditions with the stationary solution and retain the spatial discretization in the error estimate.\n";}
   source+="\\end{document}\n";const QString path=fixed+(large?"/long.tex":"/short.tex");writeFile(path,source.toUtf8());
   auto *w=new TeXDocumentWindow(path);w->resize(1160,780);w->show();check(large?"long article ready":"short article ready",await([&]{return w->property("researchArticleReady").toBool()&&w->property("researchArticleMode").toBool();}));
   auto *outline=w->findChild<QAction*>("researchOutlineAction");auto *dock=w->findChild<QDockWidget*>("researchArticleDock");if(!outline||!dock){check("outline available",false);w->setModified(false);delete w;continue;}
   auto cursor=w->textCursor();cursor.setPosition(source.indexOf("The concentration",source.size()/2));w->editor()->setTextCursor(cursor);w->editor()->ensureCursorVisible();pauseFor(800);
   if(outline->isChecked()){Tw::Settings().setValue("researchReduceMotion",true);outline->trigger();Tw::Settings().setValue("researchReduceMotion",false);}
   for(bool half:{false,true}){
    const auto area=w->screen()->availableGeometry();w->resize(half?area.width()/2:1160,qMin(780,area.height()-40));pauseFor(500);
    for(int iteration=0;iteration<12;++iteration){
     const int caret=w->cursorPosition(),revision=w->textDoc()->revision();const auto anchor=w->editor()->cursorForPosition(QPoint(0,0));const int anchorY=w->editor()->cursorRect(anchor).top();
     const bool on=!outline->isChecked();Cadence trace;trace.resizeTarget=w->editor()->viewport();trace.resizeTarget->installEventFilter(&trace);auto *veil=w->findChild<QWidget*>("researchPanelMotionVeil");if(veil)veil->installEventFilter(&trace);
     trace.start();QElapsedTimer call;call.start();outline->trigger();const double ms=double(call.nsecsElapsed())/1e6;
     if(!veil){veil=w->findChild<QWidget*>("researchPanelMotionVeil");if(veil)veil->installEventFilter(&trace);}
     pauseFor(215);auto row=trace.stop(ms);trace.resizeTarget->removeEventFilter(&trace);if(veil)veil->removeEventFilter(&trace);row["mode"]=large?"long-panel":"short-panel";row["half"]=half;row["opening"]=on;row["iteration"]=iteration;row["dpr"]=w->devicePixelRatioF();row["refreshHz"]=w->screen()->refreshRate();row["windowWidth"]=w->width();row["sourceChars"]=source.size();row["privateBytes"]=double(privateBytes());QJsonObject phases;for(const auto *name:{"captures-before","anchor-before","visibility-and-resize","layouts","anchor-after","captures-after","done"})if(w->property(name).isValid())phases[name]=w->property(name).toDouble();if(!phases.isEmpty())row["phases"]=phases;samples.append(row);
     if(iteration==0||iteration==11){check("panel keeps at least half the writing width",w->editor()->viewport()->width()>=w->width()/2);check("panel preserves caret and source revision",w->cursorPosition()==caret&&w->textDoc()->revision()==revision);check("panel preserves the top source anchor",qAbs(w->editor()->cursorRect(anchor).top()-anchorY)<=2);check("panel finishes and releases raster",veil&&!veil->isVisible()&&veil->property("researchSnapshotBytes").toLongLong()==0);}
    }
   }
   for(bool panelOpen:{false,true}){if(panelOpen&&!outline->isChecked()){outline->trigger();pauseFor(550);}for(int iteration=0;iteration<6;++iteration){pauseFor(550);const auto focusCursor=w->textCursor();const auto focusAnchor=w->editor()->cursorForPosition(QPoint(0,0));const int focusY=w->editor()->viewport()->mapTo(w,w->editor()->cursorRect(focusAnchor).topLeft()).y();Cadence trace;trace.resizeTarget=w->editor()->viewport();trace.resizeTarget->installEventFilter(&trace);auto *veil=w->findChild<QWidget*>("researchChromeVeil");if(veil)veil->installEventFilter(&trace);trace.start();QElapsedTimer call;call.start();Research::setFocusMode(iteration%2==0);const double ms=double(call.nsecsElapsed())/1e6;if(!veil){veil=w->findChild<QWidget*>("researchChromeVeil");if(veil)veil->installEventFilter(&trace);}pauseFor(210);auto row=trace.stop(ms);if(veil)veil->removeEventFilter(&trace);trace.resizeTarget->removeEventFilter(&trace);QJsonObject phases;for(const auto *name:{"focus-capture","focus-apply","focus-layout","focus-anchor","focus-done"})if(w->property(name).isValid())phases[name]=w->property(name).toDouble();row["phases"]=phases;row["mode"]=panelOpen?(large?"long-focus-open":"short-focus-open"):(large?"long-focus":"short-focus");row["iteration"]=iteration;row["dpr"]=w->devicePixelRatioF();samples.append(row);if(panelOpen){check("focus with an open sidebar preserves cursor and selection",w->textCursor().position()==focusCursor.position()&&w->textCursor().anchor()==focusCursor.anchor());check("focus with an open sidebar preserves reading position",qAbs(w->editor()->viewport()->mapTo(w,w->editor()->cursorRect(focusAnchor).topLeft()).y()-focusY)<=2);}}}
   if(large&&qEnvironmentVariableIsSet("TEXLINE_MOTION_RECORD")){
    if(outline->isChecked()){outline->trigger();pauseFor(250);}pauseFor(550);
    record(w,"long-panel-half",[&]{outline->trigger();});pauseFor(250);
    record(w,"long-panel-reverse",[&]{outline->trigger();QTimer::singleShot(65,w,[outline]{outline->trigger();});});pauseFor(250);
    record(w,"long-focus-half",[&]{Research::setFocusMode(true);});pauseFor(250);Research::setFocusMode(false);pauseFor(250);
   }
   check("view operations retain exact source",w->textDoc()->editorText()==source);w->grab().save(folder+(large?"/long-final.png":"/short-final.png"));w->setModified(false);delete w;save();
  }
  for(int rows:{30,600}){
   Research::SlideList list(nullptr);list.setItemDelegate(new Research::SlideItemDelegate(&list));list.resize(260,720);for(int i=0;i<rows;++i){auto *item=new QListWidgetItem("",&list);item->setData(Qt::UserRole+10,"Conservation laws and numerical convergence "+QString::number(i));}list.show();list.doItemsLayout();pauseFor(500);
   for(int iteration=0;iteration<8;++iteration){Cadence trace;list.viewport()->installEventFilter(&trace);trace.start();QElapsedTimer call;call.start();list.animateGap(iteration%2?2:4);const double ms=double(call.nsecsElapsed())/1e6;pauseFor(210);auto row=trace.stop(ms);list.viewport()->removeEventFilter(&trace);row["mode"]="slide-drag-gap";row["rows"]=rows;row["iteration"]=iteration;row["dpr"]=list.devicePixelRatioF();samples.append(row);}
   list.resetDragPreview();check("drag gap clears without changing model",list.count()==rows&&list.property("researchSlideGaps").toMap().isEmpty());list.hide();save();
  }
  app.exit(failures?1:0);
 });QTimer::singleShot(240000,&app,[&]{app.exit(93);});return app.exec();
}
