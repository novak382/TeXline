// All live fields must release a panel's temporary raster before editing.
#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "TeXDocumentWindow.h"
#include "CompletingEdit.h"
#include "ResearchAppearance.h"
#include "ResearchMotion.h"
#include "Settings.h"
#include <QAction>
#include <QDockWidget>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QSlider>
#include <QPushButton>
#include <QToolButton>
#include <QAbstractButton>
#include <functional>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QThread>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTimer>
#include <windows.h>
#include <shellapi.h>
static bool await(const std::function<bool()> &f,int ms=15000){QElapsedTimer t;t.start();while(!f()&&t.elapsed()<ms){QCoreApplication::processEvents();QThread::msleep(1);}return f();}
static void settle(int ms){QElapsedTimer t;t.start();await([&]{return t.elapsed()>=ms;},ms+1000);}
static bool put(const QString &p,const QByteArray &text){QSaveFile f(p);return f.open(QIODevice::WriteOnly)&&f.write(text)==text.size()&&f.commit();}
static void key(QWidget *w,int code,QString text={}){QKeyEvent e(QEvent::KeyPress,code,Qt::NoModifier,text);QApplication::sendEvent(w,&e);}
static void click(QWidget *w){const QPoint local=w->rect().center();for(auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}){QMouseEvent e(type,QPointF(local),QPointF(w->mapToGlobal(local)),Qt::LeftButton,type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(w,&e);}}
int main(int argc,char **argv){
 if(!qEnvironmentVariable("TEXLINE_TEST_DESKTOP").startsWith("TeXlineValidation-"))return 2;
 int n=0;auto **args=CommandLineToArgvW(GetCommandLineW(),&n);const QString report=QString::fromWCharArray(args[1]);LocalFree(args);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
 QTimer::singleShot(80,&app,[&]{
  const auto folder=QFileInfo(report).absolutePath(),path=folder+"/transport.tex";const QString source="\\documentclass{article}\n\\begin{document}\n\\section{Transport}Conservation of mass.\n\\section{Boundary}The normal flux vanishes.\n\\section{Balance}The total mass is constant.\n\\end{document}\n";put(path,source.toUtf8());
  Tw::Settings().setValue("openPDFwithTeX",false);Tw::Settings().setValue("researchReduceMotion",false);Tw::Settings().setValue("researchFocusMode",false);
  QJsonArray checks;int failures=0;auto check=[&](QString name,bool good){failures+=!good;checks.append(QJsonObject{{"name",name},{"passed",good}});};
  auto *w=new TeXDocumentWindow(path);w->resize(1100,740);w->show();check("actual article is ready",await([&]{return w->property("researchArticleReady").toBool()&&w->property("researchArticleMode").toBool();}));
  auto *outline=w->findChild<QAction*>("researchOutlineAction");auto *filter=w->findChild<QToolButton*>("researchArticleSearchToggle");auto *search=w->findChild<QLineEdit*>("researchArticleSearch");
  check("actual structure search exists",outline&&filter&&search);
  if(outline&&filter&&search){if(outline->isChecked()){outline->trigger();settle(200);}filter->setChecked(true);outline->trigger();search->setFocus();key(search,Qt::Key_T,"Transport");auto *veil=w->findChild<QWidget*>("researchPanelMotionVeil");check("typing immediately exposes the live structure search",search->text()=="Transport"&&veil&&!veil->isVisible()&&veil->property("researchSnapshotBytes").toLongLong()==0);search->clear();filter->setChecked(false);outline->trigger();settle(200);}
  auto *dock=new QDockWidget(w);dock->setFeatures(QDockWidget::NoDockWidgetFeatures);auto *body=new QWidget(dock);auto *layout=new QVBoxLayout(body);auto *line=new QLineEdit(body);line->setClearButtonEnabled(true);auto *spin=new QSpinBox(body);auto *combo=new QComboBox(body);combo->addItems({"Closed wall","Open boundary"});auto *slider=new QSlider(Qt::Horizontal,body);auto *close=new QPushButton("Close",body);for(auto *field:QList<QWidget*>{line,spin,combo,slider,close})layout->addWidget(field);dock->setWidget(body);w->addDockWidget(Qt::LeftDockWidgetArea,dock);dock->hide();
  auto arm=[&]{Research::setPanelVisible(dock,false,0,false);Research::setPanelVisible(dock,true,220,true);return w->findChild<QWidget*>("researchPanelMotionVeil");};
  auto released=[&](QWidget *veil){return veil&&!veil->isVisible()&&veil->property("researchSnapshotBytes").toLongLong()==0;};
  auto *veil=arm();check("actual fields begin inside a moving panel",veil&&veil->isVisible());line->setFocus();key(line,Qt::Key_A,"a");check("line input is immediate during a reveal",line->text()=="a"&&released(veil));
  veil=arm();spin->setFocus();key(spin,Qt::Key_Up);check("numeric input exposes its changed value immediately",spin->value()==1&&released(veil));
  veil=arm();combo->setFocus();key(combo,Qt::Key_Down);check("choice input exposes its changed value immediately",combo->currentIndex()==1&&released(veil));
  veil=arm();slider->setFocus();key(slider,Qt::Key_Right);check("slider input exposes its changed value immediately",slider->value()==1&&released(veil));
  line->setText("Boundary");settle(20);veil=arm();auto *clear=line->findChild<QAbstractButton*>();check("clear field has an actual button",clear);if(clear){click(clear);check("an embedded clear button also releases the field raster",line->text().isEmpty()&&released(veil));}
  Research::setPanelVisible(dock,false,0,false);veil=arm();settle(30);w->grab();const qreal before=veil->property("researchMotionExtent").toReal();QObject::connect(close,&QPushButton::clicked,w,[&]{Research::setPanelVisible(dock,false);});click(close);w->grab();check("panel command buttons retain smooth reversal",veil->isVisible()&&qAbs(before-veil->property("researchMotionFrom").toReal())<35);settle(220);check("panel command reaches its closed state",dock->isHidden()&&!veil->isVisible());
  check("field interactions preserve the complete source",w->textDoc()->editorText()==source&&!w->isModified());w->setModified(false);delete w;put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures}}).toJson());app.exit(failures?1:0);
 });return app.exec();
}
