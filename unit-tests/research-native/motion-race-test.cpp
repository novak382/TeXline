// Interrupt actual list raster transitions at their midpoint; source UI is tested below.
#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "ResearchSlides.cpp"
#include "ResearchMotion.h"
#include <QJsonArray>
#include <QThread>
#include <windows.h>
#include <shellapi.h>
static void settle(int ms){QElapsedTimer t;t.start();while(t.elapsed()<ms){QCoreApplication::processEvents();QThread::msleep(1);}}
static void put(const QString &p,const QByteArray &v){QDir().mkpath(QFileInfo(p).absolutePath());QFile f(p);if(f.open(QIODevice::WriteOnly))f.write(v);}
static int redInk(const QImage &raw){const auto image=raw.convertToFormat(QImage::Format_RGB32);int n=0;for(int y=0;y<image.height();++y){const auto *row=reinterpret_cast<const QRgb*>(image.constScanLine(y));for(int x=0;x<image.width();++x)n+=qRed(row[x])-qGreen(row[x])>80&&qRed(row[x])-qBlue(row[x])>80;}return n;}
int main(int argc,char **argv){if(!qEnvironmentVariable("TEXLINE_TEST_DESKTOP").startsWith("TeXlineValidation-"))return 2;int n=0;auto **args=CommandLineToArgvW(GetCommandLineW(),&n);const QString report=QString::fromWCharArray(args[1]);LocalFree(args);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
 QTimer::singleShot(80,&app,[&]{const QString folder=QFileInfo(report).absolutePath();QJsonArray checks;QJsonObject measurements;int failures=0;auto check=[&](QString name,bool good){failures+=!good;checks.append(QJsonObject{{"name",name},{"passed",good}});put(report,QJsonDocument(QJsonObject{{"checks",checks},{"measurements",measurements},{"failures",failures}}).toJson());};Tw::Settings().setValue("researchReduceMotion",false);
  Research::SlideList list(nullptr);list.resize(340,520);list.setIconSize(QSize(100,72));list.setSpacing(6);list.show();
  auto fill=[&](const QStringList &keys){list.clear();for(const auto &key:keys){QPixmap icon(100,72);icon.fill(key=="red"?QColor("#e40000"):QColor("#63859a"));auto *item=new QListWidgetItem(QIcon(icon),key,&list);item->setSizeHint(QSize(300,88));}list.doItemsLayout();};
  const QStringList all{"first","red","last"},removed{"first","last"};fill(all);settle(300);
  list.reflow->prepare(all,removed);fill(removed);list.reflow->complete(removed);settle(20);QElapsedTimer captureGap;captureGap.start();auto before=list.viewport()->grab().toImage();
  list.reflow->prepare(removed,all);fill(all);list.reflow->complete(all);auto after=list.viewport()->grab().toImage();measurements["captureGapMs"]=double(captureGap.nsecsElapsed())/1e6;
  // PNG compression must not consume part of the 160ms transition between the
  // two observations. Keep the actual before/after pixels and the same bound.
  before.save(folder+"/before-undo.png");after.save(folder+"/after-undo.png");measurements["redPixelsBeforeUndo"]=redInk(before);measurements["redPixelsAfterUndo"]=redInk(after);
  check("undo during deletion keeps the still-visible card on screen",redInk(before)>100&&redInk(after)>=redInk(before)*.7);settle(220);check("undo finishes with original model and no snapshots",list.count()==3&&!list.reflow->isVisible()&&list.reflow->property("researchSnapshotBytes").toLongLong()==0);
  // Repeating a reversal must preserve opacity instead of creating a new ghost.
  for(int i=0;i<20;++i){list.reflow->prepare(all,removed);fill(removed);list.reflow->complete(removed);settle(8+i%4);list.reflow->prepare(removed,all);fill(all);list.reflow->complete(all);settle(8+i%5);}
  settle(240);const auto final=list.viewport()->grab().toImage();measurements["finalRedPixels"]=redInk(final);check("rapid repeated undo restores one opaque card",list.count()==3&&redInk(final)>100&&!list.reflow->isVisible());
  check("uninterrupted completion returns all snapshot memory",list.reflow->property("researchSnapshotBytes").toLongLong()==0);
  list.reflow->prepare(all,removed);fill(removed);list.reflow->complete(removed);QEvent density(QEvent::DevicePixelRatioChange);QApplication::sendEvent(list.viewport(),&density);check("density changes discard an old-resolution card raster",!list.reflow->isVisible()&&list.reflow->property("researchSnapshotBytes").toLongLong()==0);list.hide();
  {Research::MaterialDialog dialog;auto *layout=new QVBoxLayout(&dialog);auto *field=new QLineEdit(&dialog);layout->addWidget(field);dialog.resize(500,220);dialog.show();settle(20);auto *veil=dialog.findChild<QWidget*>("researchArrivalVeil");check("dialog starts its actual arrival",veil&&veil->isVisible());QEvent font(QEvent::FontChange);QApplication::sendEvent(field,&font);check("font changes discard stale dialog text immediately",veil&&!veil->isVisible()&&veil->property("researchSnapshotBytes").toLongLong()==0);dialog.hide();}
  const QString file=folder+"/project/paper.tex";put(file,"\\documentclass{article}\n\\begin{document}\n\\section{Flux}\nConservation of mass.\n\\section{Boundary}\nThe normal flux vanishes.\n\\end{document}\n");auto *w=new TeXDocumentWindow(file);w->resize(1000,700);w->show();settle(800);auto *outline=w->findChild<QAction*>("researchOutlineAction");if(outline&&outline->isChecked()){Tw::Settings().setValue("researchReduceMotion",true);outline->trigger();Tw::Settings().setValue("researchReduceMotion",false);}if(outline)outline->trigger();auto *panel=w->findChild<QWidget*>("researchPanelMotionVeil");check("actual article panel starts its reveal",panel&&panel->isVisible());QEvent changedDensity(QEvent::DevicePixelRatioChange);QApplication::sendEvent(w,&changedDensity);check("density changes discard a stale panel raster",panel&&!panel->isVisible()&&panel->property("researchSnapshotBytes").toLongLong()==0);settle(550);Research::setFocusMode(true);auto *chrome=w->findChild<QWidget*>("researchChromeVeil");check("actual workspace starts its transition",chrome&&chrome->isVisible());QApplication::sendEvent(w,&changedDensity);check("density changes discard stale workspace chrome",chrome&&!chrome->isVisible()&&chrome->property("researchSnapshotBytes").toLongLong()==0);Research::setFocusMode(false);w->setModified(false);delete w;app.exit(failures?1:0);
 });QTimer::singleShot(30000,&app,[&]{app.exit(93);});return app.exec();}
