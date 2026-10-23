#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "TeXDocumentWindow.h"
#include "PDFDocumentWindow.h"
#include "ResearchAppearance.h"
#include "Settings.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QDir>
#include <QMenu>
#include <QAction>
#include <QMenuBar>
#include <QToolButton>
#include <QTimer>
#include <QThread>
#include <QElapsedTimer>
#include <QSet>
static void settle(int ms=100){QElapsedTimer t;t.start();while(t.elapsed()<ms){QCoreApplication::processEvents();QThread::msleep(3);}}
static int depth(QMenu *m,QSet<QMenu*> path={}){if(!m)return 0;if(path.contains(m))return 999;path.insert(m);int n=1;for(auto *a:m->actions())if(a->menu())n=qMax(n,1+depth(a->menu(),path));return n;}
static bool reachable(QMenu *m,QAction *wanted){for(auto *a:m->actions())if(a==wanted||(a->menu()&&reachable(a->menu(),wanted)))return true;return false;}
static bool put(QString p,QByteArray b){QDir().mkpath(QFileInfo(p).absolutePath());QSaveFile f(p);return f.open(QIODevice::WriteOnly)&&f.write(b)==b.size()&&f.commit();}
static void popup(QMenu *m,const QString &image){m->popup(QPoint(100,100));settle(120);m->grab().save(image);m->hide();settle(30);}
int main(int argc,char **argv){const QString report=QString::fromLocal8Bit(argv[1]),pdf=QString::fromLocal8Bit(argv[2]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);QTimer::singleShot(80,&app,[&]{QJsonArray checks;int failures=0;QJsonObject measurements;
auto check=[&](QString n,bool pass){checks.append(QJsonObject{{"name",n},{"passed",pass}});failures+=!pass;put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"measurements",measurements}}).toJson());};
const QString root=QFileInfo(report).absolutePath()+"/menu/main.tex";put(root,"\\documentclass{article}\n\\begin{document}Menu test.\\end{document}\n");Tw::Settings().setValue("openPDFwithTeX",false);
auto *source=new TeXDocumentWindow(root);auto *viewer=new PDFDocumentWindow(pdf,source);check("a real editor and nonempty PDF window open",source&&viewer&&viewer->widget()->document().toStrongRef());source->resize(920,720);viewer->resize(900,740);source->show();viewer->show();settle(180);
for(const auto &locale:{QString("zh_CN"),QString("en_US")}) {
  TWApp::instance()->applyTranslation(locale);Research::setUiLocale(locale);Research::retranslateWorkspace(source);Research::retranslateWorkspace(viewer);
  for(bool focus:{false,true}) {
    Research::setFocusMode(focus);settle(100);QMenu *engines=source->findChild<QMenu*>("researchEngineMenu");source->populateResearchEngineMenu(engines);
    QMetaObject::invokeMethod(viewer->findChild<QMenu*>("researchEngineMenu"),"aboutToShow",Qt::DirectConnection);
    auto *sm=source->findChild<QToolButton*>(focus?"researchFocusMore":"researchMore")->menu();auto *pm=viewer->findChild<QToolButton*>(focus?"researchFocusMore":"researchMore")->menu();
    const QString prefix=locale+(focus?" focus":" default");measurements.insert(prefix+" sourceDepth",depth(sm));measurements.insert(prefix+" pdfDepth",depth(pm));
    check(prefix+": every source and PDF popup path has at most three levels",depth(sm)<=3&&depth(pm)<=3);
    check(prefix+": primary actions and recovery remain reachable",reachable(sm,source->findChild<QAction*>("actionSave"))&&reachable(sm,source->findChild<QAction*>("actionFind"))&&reachable(sm,source->findChild<QMenu*>("researchLanguageMenu")->menuAction()));
    check(prefix+": PDF keeps typesetting, fit modes and mouse tools",reachable(pm,viewer->findChild<QAction*>("actionTypeset"))&&reachable(pm,viewer->findChild<QAction*>("actionFit_to_Content_Width"))&&reachable(pm,viewer->findChild<QAction*>("actionSelect_Text")));
    check(prefix+": source has no duplicated typesetting action",!reachable(sm,source->findChild<QAction*>("actionTypeset")));
    bool flatEngines=true;for(auto *a:engines->actions())flatEngines&=!a->menu();check(prefix+": all engines and automatic selection use one flat menu",flatEngines&&engines->actions().size()>=3);
    popup(sm,QFileInfo(report).absolutePath()+"/"+prefix+"-source-menu.png");popup(pm,QFileInfo(report).absolutePath()+"/"+prefix+"-pdf-menu.png");
  }
}
bool stable=true;for(int n=0;n<24;++n){Research::setFocusMode(n%2);settle(20);stable&=depth(source->findChild<QMenu*>("researchFocusMenu"))<=3&&depth(viewer->findChild<QMenu*>("researchFocusMenu"))<=3;}check("24 mode changes do not recreate deep menu wrappers",stable);
Research::setFocusMode(false);source->menuBar()->show();viewer->menuBar()->show();check("the original menu bar remains available for all advanced commands",source->menuBar()->isVisible()&&viewer->menuBar()->isVisible());
bool barsShallow=true;for(auto *bar:{source->menuBar(),viewer->menuBar()})for(auto *action:bar->actions())if(action->menu())barsShallow&=depth(action->menu())<=3;
check("built-in original menu-bar paths also stay within three levels",barsShallow);
source->setModified(false);delete source;viewer->close();settle(150);app.exit(failures?1:0);
});return app.exec();}
