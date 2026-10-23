// Compare the actual review control with the same icon without a menu marker.
#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "TeXDocumentWindow.h"
#include "CompletingEdit.h"
#include "ResearchAppearance.h"
#include "ResearchCollaboration.h"
#include "Settings.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QElapsedTimer>
#include <QThread>
#include <QToolButton>
#include <QPushButton>
#include <QMenu>
#include <QDir>
#include <QFileInfo>
#include <windows.h>
#include <shellapi.h>
static void settle(int ms=220){QElapsedTimer t;t.start();while(t.elapsed()<ms){QCoreApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QThread::msleep(1);}}
static void put(const QString &file,const QByteArray &data){QDir().mkpath(QFileInfo(file).absolutePath());QSaveFile f(file);if(f.open(QIODevice::WriteOnly)){f.write(data);f.commit();}}
static QImage capture(QToolButton *b){auto *w=b->window();return w->grab(QRect(b->mapTo(w,QPoint()),b->size())).toImage().convertToFormat(QImage::Format_RGB32);}
static int extraInk(const QImage &a,const QImage &b){if(a.size()!=b.size())return 100000;int n=0;for(int y=0;y<a.height();++y)for(int x=0;x<a.width();++x){const auto p=a.pixel(x,y),q=b.pixel(x,y);n+=(qRed(p)+qGreen(p)+qBlue(p)<450)&&(qRed(q)+qGreen(q)+qBlue(q)>=450);}return n;}
int main(int argc,char **argv){if(!qEnvironmentVariable("TEXLINE_TEST_DESKTOP").startsWith("TeXlineValidation-"))return 2;int n=0;auto **args=CommandLineToArgvW(GetCommandLineW(),&n);const QString report=QString::fromWCharArray(args[1]);LocalFree(args);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
 QTimer::singleShot(80,&app,[&]{const QString folder=QFileInfo(report).absolutePath();QJsonArray checks,rasters;int failures=0;auto check=[&](QString name,bool good){checks.append(QJsonObject{{"name",name},{"passed",good}});failures+=!good;};Tw::Settings().setValue("openPDFwithTeX",false);Tw::Settings().setValue("researchFocusMode",false);
  const QString base="\\documentclass{article}\n\\begin{document}\n\\section{Conservation}\nA conservative update preserves total mass.\n\\section{Interpretation}\nThe balance is evaluated before normalization.\n\\end{document}\n";
  const QString file=folder+"/project/transport.tex";put(file,base.toUtf8());auto *w=new TeXDocumentWindow(file);w->resize(1100,720);w->show();settle(700);
  const auto local=QString(base).replace("total mass","the cell-weighted mass");auto c=w->textCursor();c.select(QTextCursor::Document);c.insertText(local);const auto remote=QString(base).replace("total mass","the integrated mass");put(file,remote.toUtf8());w->collaboration()->receive(remote);w->collaboration()->review();settle();
  auto *more=w->findChild<QToolButton*>("researchReviewMore");check("actual review has one more control",more&&more->isVisible()&&w->findChildren<QToolButton*>("researchReviewMore").size()==1);
  if(more){auto *menu=more->menu();check("more retains its menu and icon-only entry",menu&&more->popupMode()==QToolButton::InstantPopup&&more->toolButtonStyle()==Qt::ToolButtonIconOnly&&!more->icon().isNull());
   auto raster=[&](const QString &stage){w->editor()->setFocus();Research::configureButton(more,Research::ButtonRole::Icon);settle(30);const auto actual=capture(more);more->setMenu(nullptr);settle(20);const auto reference=capture(more);more->setMenu(menu);Research::configureButton(more,Research::ButtonRole::Icon);const int extra=extraInk(actual,reference);actual.save(folder+"/"+stage+"-actual.png");reference.save(folder+"/"+stage+"-reference.png");rasters.append(QJsonObject{{"stage",stage},{"extraDarkPixels",extra},{"dpr",more->devicePixelRatioF()}});check(stage+": no extra dropdown glyph after shared control refresh",extra<=1);};
   raster("normal");Research::setFocusMode(true);settle();Research::setFocusMode(false);settle();raster("after-focus");
   int decisions=0;if(auto *body=w->findChild<QWidget*>("researchReviewBody"))for(auto *b:body->findChildren<QPushButton*>())decisions+=b->isVisible();check("only two decision buttons remain",decisions==2);
   bool opened=false;QTimer::singleShot(70,menu,[&]{opened=menu->isVisible();menu->hide();});more->click();settle(100);check("clicking the single more entry still opens its menu",opened);
   check("secondary operations remain available",menu->findChild<QAction*>("researchKeepLocal")||w->findChild<QAction*>("researchKeepLocal"));w->grab().save(folder+"/review-row.png");
  }
  check("visual review does not alter the manuscript",w->textDoc()->editorText()==local);w->setModified(false);delete w;put(report,QJsonDocument(QJsonObject{{"checks",checks},{"rasters",rasters},{"failures",failures},{"physicalInputVerified",false}}).toJson());app.exit(failures?1:0);
 });QTimer::singleShot(30000,&app,[&]{app.exit(93);});return app.exec();}
