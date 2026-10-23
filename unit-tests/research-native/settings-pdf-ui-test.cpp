#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "TeXDocumentWindow.h"
#include "PDFDocumentWindow.h"
#include "PrefsDialog.h"
#include "TemplateDialog.h"
#include "ResearchAppearance.h"
#include "Settings.h"
#include "utils/ResourcesLibrary.h"
#include <QAction>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileSystemModel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QComboBox>
#include <QMenu>
#include <QPainter>
#include <QPdfWriter>
#include <QPushButton>
#include <QSpinBox>
#include <QStatusBar>
#include <QKeyEvent>
#include <QTabWidget>
#include <QTimer>
#include <QThread>
#include <QTreeView>
#include <QElapsedTimer>
static void settle(int ms=250){QElapsedTimer t;t.start();while(t.elapsed()<ms){QCoreApplication::processEvents();QThread::msleep(2);}}
int main(int argc,char **argv){const QString report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);QTimer::singleShot(80,&app,[&]{
 QJsonArray checks;int failures=0;const auto folder=QFileInfo(report).absolutePath();auto check=[&](QString name,bool passed){checks.append(QJsonObject{{"name",name},{"passed",passed}});QFile f(report);if(f.open(QIODevice::WriteOnly))f.write(QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures+=!passed}}).toJson());};
 auto *w=new TeXDocumentWindow();w->resize(1280,800);w->show();settle();check("ordinary documents keep the slide mode toolbar control hidden",w->findChild<QAction *>("researchModeControl")&&!w->findChild<QAction *>("researchModeControl")->isVisible());
 check("Windows replace and redo aliases remain available",w->findChild<QAction *>("actionReplace")->shortcuts().contains(QKeySequence("Ctrl+H"))&&w->findChild<QAction *>("actionRedo")->shortcuts().contains(QKeySequence("Ctrl+Y")));
 int scriptsBefore=0;for(auto *action:w->findChildren<QAction *>())scriptsBefore+=action->objectName().startsWith("Script: ");
 for(int refresh=0;refresh<6;++refresh){w->updateScriptsMenu();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);TWApp::instance()->updateScriptsList();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);}
 int scriptsAfter=0;for(auto *action:w->findChildren<QAction *>())scriptsAfter+=action->objectName().startsWith("Script: ");
 check("repeated script menu replacement and reload keep only live actions",scriptsBefore>0&&scriptsAfter==scriptsBefore);
 const int before=Tw::Settings().value("researchCommandCompletionMode",0).toInt();bool searched=false;
 QTimer::singleShot(180,&app,[&]{auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());if(!dialog){check("preferences dialog opens",false);return;}auto *query=dialog->findChild<QLineEdit *>("researchSettingsSearch");auto *mode=dialog->findChild<QComboBox *>("completionConfigMode");check("preferences exposes one three-mode completion control",mode&&mode->count()==3);if(query){query->setText(Research::englishUi()?"Completion":"补全");searched=dialog->findChild<QListWidget *>("researchSettingsMatches")->count()>0;}if(mode)mode->setCurrentIndex((before+1)%3);dialog->grab().save(folder+"/settings.png");dialog->reject();});PrefsDialog::doPrefsDialog(w);check("settings search finds completion controls",searched);check("cancelling preferences preserves the chosen completion mode",Tw::Settings().value("researchCommandCompletionMode",0).toInt()==before);
 QTimer::singleShot(180,&app,[&]{auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());if(!dialog)return;dialog->findChild<QComboBox *>("completionConfigMode")->setCurrentIndex(1);dialog->accept();});PrefsDialog::doPrefsDialog(w);check("accepted completion setting updates the shared command menu",Tw::Settings().value("researchCommandCompletionMode").toInt()==1&&w->findChild<QAction *>("researchCommandCompletionMode1")->isChecked());
 bool commandsFound=false;QTimer::singleShot(100,&app,[&]{auto *dialog=w->findChild<QDialog *>("researchCommandDialog");if(!dialog)return;dialog->findChild<QLineEdit *>("researchCommandQuery")->setText("Ctrl+H");auto *items=dialog->findChild<QListWidget *>("researchCommandResults");for(int i=0;i<items->count();++i)if(!items->item(i)->isHidden())commandsFound=true;dialog->grab().save(folder+"/commands.png");dialog->reject();});w->findChild<QAction *>("researchCommandPalette")->trigger();check("command search includes alternate shortcuts",commandsFound);
 {TemplateDialog dialog;QTimer::singleShot(400,&app,[&]{auto *tree=dialog.findChild<QTreeView *>("treeView");auto *model=qobject_cast<QFileSystemModel *>(tree->model());const auto templateFile=Tw::Utils::ResourcesLibrary::getLibraryPath("templates")+"/Beamer presentations/minimal-seminar.tex";tree->setCurrentIndex(model->index(templateFile));settle(100);auto *image=dialog.findChild<QLabel *>("researchTemplateImage");check("template picker displays a verified raster preview",image&&!image->pixmap().isNull()&&dialog.findChild<QTabWidget *>("researchTemplateTabs")->count()==2);dialog.grab().save(folder+"/templates.png");dialog.reject();});dialog.exec();}
 const auto pdf=folder+"/reading.pdf";{QPdfWriter writer(pdf);writer.setPageSize(QPageSize(QPageSize::A4));QPainter painter(&writer);for(int i=0;i<4;++i){if(i)writer.newPage();painter.drawText(QRect(100,100,2000,500),QStringLiteral("Reading page %1").arg(i+1));}}
 auto *p=new PDFDocumentWindow(pdf);p->resize(1100,800);p->show();settle(500);auto *page=p->findChild<QSpinBox *>("researchPdfPageField");auto *zoom=p->findChild<QDoubleSpinBox *>("researchPdfZoomField");check("PDF has inline page and zoom fields",page&&zoom&&page->maximum()==4);
 if(page){page->setValue(3);QMetaObject::invokeMethod(page,"editingFinished");settle();check("inline page input navigates without a modal",p->widget()->currentPage()==2&&!QApplication::activeModalWidget());}
 if(zoom){zoom->setValue(150);QMetaObject::invokeMethod(zoom,"editingFinished");settle();check("inline zoom input changes scale without a modal",qAbs(p->widget()->zoomLevel()-1.5)<0.001&&!QApplication::activeModalWidget());}
 auto *fit=p->findChild<QMenu *>("researchPdfFitMenu");check("fit control presents direct page width content and actual choices",fit&&fit->actions().size()==4);if(fit){fit->actions()[1]->trigger();QMetaObject::invokeMethod(fit,"aboutToShow");check("chosen fit mode is marked in menu",fit->actions()[1]->isChecked()&&!fit->actions()[0]->isChecked());}
 p->grab().save(folder+"/pdf.png");
 QJsonArray geometry;
 auto bars=[&](const QString &mode){
  auto *sourceBar=w->statusBar();auto *pdfBar=p->statusBar();
  check(mode+" source and PDF status bars share a compact row with room for glyphs",sourceBar->height()==pdfBar->height()&&sourceBar->height()>=24&&sourceBar->height()<=28);
  const QRect pageRect(page->mapTo(pdfBar,QPoint()),page->size()),zoomRect(zoom->mapTo(pdfBar,QPoint()),zoom->size());
  check(mode+" page and zoom fields share a 22px height and vertical center",page->height()==22&&zoom->height()==22&&pageRect.center().y()==zoomRect.center().y());
  check(mode+" PDF fields fit inside the status bar",pdfBar->rect().contains(pageRect)&&pdfBar->rect().contains(zoomRect));
  geometry.append(QJsonObject{{"mode",mode},{"sourceHeight",sourceBar->height()},{"pdfHeight",pdfBar->height()},{"pageHeight",page->height()},{"zoomHeight",zoom->height()},{"devicePixelRatio",p->devicePixelRatioF()}});
  QPixmap strip(QSize(w->width()+p->width(),48)*p->devicePixelRatioF());strip.setDevicePixelRatio(p->devicePixelRatioF());strip.fill(Qt::white);QPainter painter(&strip);painter.drawPixmap(0,0,w->grab(QRect(0,w->height()-48,w->width(),48)));painter.drawPixmap(w->width(),0,p->grab(QRect(0,p->height()-48,p->width(),48)));painter.end();strip.save(folder+"/status-"+mode+".png");
 };
 Research::setFocusMode(false);w->resize(960,720);p->resize(960,720);settle();zoom->setValue(98.34);page->setValue(2);bars("normal");
 Research::setFocusMode(true);settle();check("focus PDF status remains hidden until editing is requested",p->statusBar()->isHidden());
 p->findChild<QAction *>("actionGo_to_Page")->trigger();settle();bars("focus-page");
 QKeyEvent escapePage(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(page,&escapePage);settle();check("Escape restores PDF values and hides the focus status row",p->statusBar()->isHidden()&&page->value()==int(p->widget()->currentPage())+1);
 QMetaObject::invokeMethod(p,"doScaleDialog");settle();bars("focus-zoom");
 QKeyEvent escapeZoom(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(zoom,&escapeZoom);settle();check("Escape cancels zoom editing without changing the PDF",p->statusBar()->isHidden()&&qAbs(zoom->value()-p->widget()->zoomLevel()*100)<0.011);
 Research::setFocusMode(false);settle();bars("restored");
 QFile metrics(folder+"/status-geometry.json");if(metrics.open(QIODevice::WriteOnly))metrics.write(QJsonDocument(geometry).toJson());
 w->setModified(false);app.exit(failures?1:0);
 });return app.exec();}
