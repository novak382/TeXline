#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "ResearchSlides.cpp"
#include <QJsonArray>
#include <QElapsedTimer>
#include <QThread>
#include <windows.h>
#include <shellapi.h>

static void settle(int ms=200){QElapsedTimer t;t.start();while(t.elapsed()<ms){QCoreApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QThread::msleep(2);}}
static bool waitFor(const std::function<bool()> &ready,int ms=12000){QElapsedTimer t;t.start();while(!ready()&&t.elapsed()<ms)settle(3);return ready();}
static void put(const QString &path,const QByteArray &value){QDir().mkpath(QFileInfo(path).absolutePath());QSaveFile f(path);if(f.open(QIODevice::WriteOnly)){f.write(value);f.commit();}}
static int solidRun(const QImage &image,bool vertical){int best=0;for(int a=0;a<(vertical?image.width():image.height());++a){int run=0;for(int b=0;b<(vertical?image.height():image.width());++b){const auto p=vertical?image.pixel(a,b):image.pixel(b,a);run=(p&0xffffff)==0x383c40?run+1:0;best=qMax(best,run);}}return best;}
static QPair<int,int> colorBand(const QImage &image,QRgb color,const QRect &region,int minimum){int first=-1,last=-1;const auto r=region.intersected(image.rect());for(int y=r.top();y<=r.bottom();++y){int n=0;for(int x=r.left();x<=r.right();++x)n+=(image.pixel(x,y)&0xffffff)==color;if(n>=minimum){if(first<0)first=y;last=y;}}return {first,last};}
int main(int argc,char **argv){
 if(!qEnvironmentVariable("TEXLINE_TEST_DESKTOP").startsWith("TeXlineValidation-"))return 2;
 int n=0;auto args=CommandLineToArgvW(GetCommandLineW(),&n);const auto report=QString::fromWCharArray(args[1]);LocalFree(args);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
 QTimer::singleShot(100,&app,[&]{
  QJsonArray checks,geometry;int failures=0;const auto folder=QFileInfo(report).absolutePath();
  auto check=[&](const QString &name,bool passed){checks.append(QJsonObject{{"name",name},{"passed",passed}});failures+=!passed;put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"geometry",geometry}}).toJson());};
  QString original="\\documentclass{beamer}\n\\begin{document}\n";for(QChar title:QString("ABCDEFGH"))original+="\\begin{frame}{"+QString(title)+"}\nContent "+QString(title)+"\n\\end{frame}\n";original+="\\end{document}\n";
  for(const auto &language:{QString("zh_CN"),QString("en_US")}){
   app.applyTranslation(language);Tw::Settings().setValue("researchReduceMotion",false);const auto path=folder+"/"+language+"/main.tex";put(path,original.toUtf8());
   auto *w=new TeXDocumentWindow(path);w->resize(1200,760);w->show();auto ready=[&]{return waitFor([&]{auto *c=Research::controller(w);return c&&c->enabled&&!c->busy&&!c->dirty&&c->deck.sources.value(Research::path(path))==w->textDoc()->editorText();});};
   check(language+": independent eight-frame source is ready",ready());
   if(auto *side=dynamic_cast<Research::SlideList*>(w->findChild<QListWidget*>("researchSlideList"))){
    side->clearSelection();side->item(0)->setSelected(true);side->setProperty("researchSlideDragging",QVariantList{0});side->animateGap(5);settle(200);
    const auto raster=side->viewport()->grab().toImage();check(language+": sidebar destination is one solid horizontal insertion line",solidRun(raster,false)>=int((side->viewport()->width()-20)*side->devicePixelRatioF())&&solidRun(raster,true)<=int(5*side->devicePixelRatioF()));w->grab().save(folder+"/sidebar-insertion-"+language+".png");
    side->hide();side->show();settle(50);check(language+": hiding the sidebar clears every temporary gap",side->property("researchSlideGaps").toMap().isEmpty()&&solidRun(side->viewport()->grab().toImage(),false)<int(50*side->devicePixelRatioF()));side->setProperty("researchSlideDragging",QVariant());
   }else check(language+": sidebar is available for insertion-line checks",false);
   auto *button=w->findChild<QToolButton *>("researchSlideOverviewButton");if(button)button->click();settle(250);
   auto *d=dynamic_cast<Research::SlideOverview *>(w->findChild<QDialog *>("researchSlideOverview"));check(language+": the actual overview opens",d);if(!d){w->setModified(false);delete w;continue;}
   auto *g=d->grid;auto *animation=g->findChild<QVariantAnimation *>();auto rects=[&]{QVector<QRect> out;for(int i=0;i<g->count();++i)out<<g->visualItemRect(g->item(i));return out;};auto select=[&](QVector<int> rows){g->clearSelection();QVariantList values;for(int row:rows){g->item(row)->setSelected(true);values<<row;}g->setProperty("researchSlideDragging",values);};
   const auto base=rects();const bool undoBefore=w->textDoc()->isUndoAvailable();check(language+": overview starts with multiple columns",g->count()==8&&base[1].top()==base[0].top()&&base[1].left()>base[0].left());
   select({0});g->animateGap(5);const auto hitPoint=base[4].center();const int hit=g->gridDropIndex(hitPoint);
   check(language+": neighbours interpolate into the new slots",waitFor([&]{const auto p=g->visualItemRect(g->item(1)).topLeft();return p!=base[1].topLeft()&&p!=base[0].topLeft();},1000));
   g->grab().save(folder+"/drag-intermediate-"+language+".png");settle(200);
   check(language+": preview makes room across a row boundary",g->visualItemRect(g->item(1)).topLeft()==base[0].topLeft()&&g->visualItemRect(g->item(0)).topLeft()==base[4].topLeft());
   check(language+": moving cards do not change a stationary pointer target",g->gridDropIndex(hitPoint)==hit);
   check(language+": space below the final row targets the end",g->gridDropIndex(QPoint(base.last().center().x(),base.last().bottom()+300))==g->count());
   check(language+": preview changes neither source nor undo history",w->textDoc()->editorText()==original&&w->textDoc()->isUndoAvailable()==undoBefore&&g->item(0)->data(Qt::UserRole).toInt()==0);
   const auto gridRaster=g->viewport()->grab().toImage();const qreal dpr=g->devicePixelRatioF();check(language+": grid destination is one solid vertical insertion line",solidRun(gridRaster,true)>=int(100*dpr)&&solidRun(gridRaster,false)<=int(5*dpr)&&!g->showDropIndicator());
   const auto neighbour=g->visualItemRect(g->item(5));const QRect physical(qRound(neighbour.left()*dpr),qRound(neighbour.top()*dpr),qRound(neighbour.width()*dpr),qRound(neighbour.height()*dpr));
   const auto photoBand=colorBand(gridRaster,0xedf0f3,physical,qRound(100*dpr)),lineBand=colorBand(gridRaster,0x383c40,gridRaster.rect(),1);
   check(language+": insertion line aligns with actual thumbnail top and bottom",photoBand.first>=0&&lineBand.first>=0&&qAbs(photoBand.first-lineBand.first)<=qCeil(dpr)&&qAbs(photoBand.second-lineBand.second)<=qCeil(dpr));
   const auto originalIcon=g->item(0)->icon(),neighbourIcon=g->item(5)->icon();QPixmap wideThumb(210,90);wideThumb.fill(QColor("#edf0f3"));g->item(0)->setIcon(QIcon(wideThumb));g->item(5)->setIcon(QIcon(wideThumb));settle(30);const auto wideRaster=g->viewport()->grab().toImage();const auto widePhoto=colorBand(wideRaster,0xedf0f3,physical,qRound(100*dpr)),wideLine=colorBand(wideRaster,0x383c40,wideRaster.rect(),1);check(language+": insertion endpoints follow a different thumbnail aspect ratio",widePhoto.first>=0&&wideLine.first>=0&&qAbs(widePhoto.first-wideLine.first)<=qCeil(dpr)&&qAbs(widePhoto.second-wideLine.second)<=qCeil(dpr));g->grab().save(folder+"/wide-thumbnail-line-"+language+".png");g->item(0)->setIcon(originalIcon);g->item(5)->setIcon(neighbourIcon);settle(30);
   d->grab().save(folder+"/drag-destination-"+language+".png");g->animateGap(-1);settle(230);check(language+": cancelling restores all natural positions",rects()==base&&w->textDoc()->editorText()==original&&solidRun(g->viewport()->grab().toImage(),true)<int(100*g->devicePixelRatioF()));
   select({7});g->animateGap(0);settle(230);check(language+": reverse drag previews the beginning of the grid",g->visualItemRect(g->item(7)).topLeft()==base[0].topLeft()&&g->visualItemRect(g->item(0)).topLeft()==base[1].topLeft());g->animateGap(-1);settle(230);select({0});
   g->animateGap(5);settle(50);const auto mid=g->visualItemRect(g->item(0));g->animateGap(3);check(language+": retargeting starts from the current position",g->visualItemRect(g->item(0))==mid);g->animateGap(-1);settle(230);
   select({0,1});g->animateGap(7);settle(230);check(language+": a block moves as one preview without editing source",g->visualItemRect(g->item(2)).topLeft()==base[0].topLeft()&&g->visualItemRect(g->item(0)).topLeft()==base[5].topLeft()&&g->visualItemRect(g->item(1)).topLeft()==base[6].topLeft()&&w->textDoc()->editorText()==original);g->animateGap(-1);settle(230);
   Tw::Settings().setValue("researchReduceMotion",true);select({0});g->animateGap(5);check(language+": reduced motion places the destination immediately",g->visualItemRect(g->item(0)).topLeft()==base[4].topLeft()&&animation->state()==QAbstractAnimation::Stopped);g->animateGap(-1);check(language+": reduced motion cancellation is immediate",rects()==base);Tw::Settings().setValue("researchReduceMotion",false);
   g->animateGap(5);settle(40);d->resize(720,650);settle(230);bool regular=true;const auto narrow=rects();for(int i=1;i<narrow.size();++i)regular&=narrow[i].top()>narrow[i-1].top()||(narrow[i].top()==narrow[i-1].top()&&narrow[i].left()>narrow[i-1].left());check(language+": resize cancels a preview and restores the normal grid",regular&&animation->state()==QAbstractAnimation::Stopped);d->resize(940,650);settle(200);
   select({0,1});g->animateGap(7);settle(30);auto *search=d->findChild<QLineEdit *>("researchOverviewSearch");search->setText("A");settle(200);check(language+": filter changes stop motion and reject hidden participants",animation->state()==QAbstractAnimation::Stopped&&!g->dropProblem(7).isEmpty()&&w->textDoc()->editorText()==original);search->clear();settle(150);g->setProperty("researchSlideDragging",QVariant());
   g->clearSelection();g->item(0)->setSelected(true);g->item(1)->setSelected(true);w->editor()->setReadOnly(true);check(language+": read-only source rejects a drop",!g->dropBefore(7)&&w->textDoc()->editorText()==original);w->editor()->setReadOnly(false);
   check(language+": real overview drop callback applies the move",g->dropBefore(7)&&ready());QString order;for(const auto &f:Research::controller(w)->deck.frames)order+=f.title;
   check(language+": source and outline agree on the new order",order=="CDEFGABH");const auto moved=w->textDoc()->editorText();w->editor()->undo();check(language+": one undo restores every original byte",ready()&&w->textDoc()->editorText()==original);w->editor()->redo();check(language+": one redo restores the moved source",ready()&&w->textDoc()->editorText()==moved);w->editor()->undo();ready();
   g->setProperty("researchSlideDragging",QVariantList{0});g->animateGap(5);settle(30);d->hide();settle();check(language+": hiding the overview stops animation",animation->state()==QAbstractAnimation::Stopped);
   geometry.append(QJsonObject{{"language",language},{"devicePixelRatio",w->devicePixelRatioF()},{"gridWidth",g->viewport()->width()}});check(language+": temporary previews leave the source intact",w->textDoc()->editorText()==original);
   w->setModified(false);delete w;
  }
  app.exit(failures?1:0);
 });QTimer::singleShot(120000,&app,[&]{app.exit(92);});return app.exec();
}
