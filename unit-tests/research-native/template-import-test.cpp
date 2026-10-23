// Real widgets and editor events, on an inactive desktop with private settings.
#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "TeXDocumentWindow.h"
#include "PDFDocumentWindow.h"
#include "CompletingEdit.h"
#include "PrefsDialog.h"
#include "ResearchTemplates.h"
#include "ResearchSlideExport.h"
#include "ResearchBuild.h"
#include "TemplateDialog.h"
#include "utils/ResourcesLibrary.h"
#include <QTemporaryDir>
#include <QPointer>
#include "ResearchAppearance.h"
#include "ResearchCommands.h"
#include "ResearchCompletion.h"
#include "ResearchShorthand.h"
#include "ResearchCommandCompletion.h"
#include "Settings.h"
#include <QAction>
#include <QToolButton>
#include <QFontInfo>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QInputMethodEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPdfWriter>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QSpinBox>
#include <QScreen>
#include <QScrollArea>
#include <QTableWidget>
#include <QTabWidget>
#include <QTextBlock>
#include <QThread>
#include <QTimer>
#include <windows.h>
#include <functional>
static bool waitFor(const std::function<bool()> &ready,int ms=30000){QElapsedTimer t;t.start();while(!ready()&&t.elapsed()<ms){QCoreApplication::processEvents();QThread::msleep(4);}return ready();}
static bool privateDesktop(){wchar_t name[256]{};DWORD needed=0;return GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()),UOI_NAME,name,sizeof(name),&needed)&&QString::fromWCharArray(name)==qEnvironmentVariable("TEXLINE_TEST_DESKTOP")&&QString::fromWCharArray(name).startsWith("TeXlineValidation-");}
static void settle(int ms=60){QElapsedTimer t;t.start();while(t.elapsed()<ms){QCoreApplication::processEvents();QThread::msleep(2);}}
static void key(QWidget *w,int code,QString text={},Qt::KeyboardModifiers mods=Qt::NoModifier){QKeyEvent e(QEvent::KeyPress,code,mods,text),r(QEvent::KeyRelease,code,mods);QApplication::sendEvent(w,&e);QApplication::sendEvent(w,&r);}
static void type(QWidget *w,const QString &text){for(auto c:text)key(w,int(c.toUpper().unicode()),QString(c));settle(10);}
static bool put(const QString &path,const QByteArray &data){QSaveFile f(path);return f.open(QIODevice::WriteOnly)&&f.write(data)==data.size()&&f.commit();}
int main(int argc,char **argv){
 if(argc<2||!privateDesktop())return 2;const QString report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
 QTimer::singleShot(100,&app,[&]{using namespace Research;const QString folder=QFileInfo(report).absolutePath();QJsonArray checks,frames;int failures=0;
 auto check=[&](const QString &name,bool ok){checks.append(QJsonObject{{"name",name},{"passed",ok}});failures+=!ok;put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"frames",frames}}).toJson());};
 auto capture=[&](QWidget *w,const QString &name){auto p=w->grab();p.save(folder+"/"+name+".png");frames.append(QJsonObject{{"name",name},{"dpr",p.devicePixelRatio()}});};
 const QString zip=qEnvironmentVariable("TEXLINE_TEMPLATE_ZIP"),library=Tw::Utils::ResourcesLibrary::getLibraryPath("templates")+"/Imported";auto token=std::make_shared<std::atomic_bool>(false);auto staged=stageTemplate(zip,folder+"/unpack",token);
 check("real USTC ZIP is imported as a complete project",staged.error.isEmpty()&&staged.mains==QStringList{"main.tex"}&&QFileInfo::exists(staged.root+"/ustcbeamer.sty")&&QFileInfo::exists(staged.root+"/theme/ustc_cover.tex")&&QFileInfo::exists(staged.root+"/figures/ustc_logo.pdf"));
 for(const auto &invalid:QStringList{"traversal","no-main","case-collision"}){const auto dest=folder+"/invalid-"+invalid;const auto rejected=stageTemplate(QFileInfo(zip).absolutePath()+"/"+invalid+".zip",dest,token);check("invalid archive is rejected without partial files: "+invalid,!rejected.error.isEmpty()&&!QFileInfo::exists(dest));}
 auto stopped=std::make_shared<std::atomic_bool>(true);check("cancelled import leaves no partial directory",!stageTemplate(zip,folder+"/cancelled",stopped).error.isEmpty()&&!QFileInfo::exists(folder+"/cancelled"));
 const auto copied=stageTemplate(staged.root,folder+QString::fromUtf8("/学校模板"),token);check("folder import supports Chinese paths",copied.error.isEmpty()&&copied.mains==staged.mains);
 auto *owner=new TeXDocumentWindow();owner->resize(520,760);owner->show();settle(300);
 QTimer::singleShot(100,&app,[&]{auto *d=qobject_cast<QDialog*>(QApplication::activeModalWidget());check("import is an actual modal wizard",d&&d->objectName()=="researchImportTemplateDialog");if(!d)return;d->resize(520,650);auto *source=d->findChild<QLineEdit*>("templateImportSource");source->setText(zip);QMetaObject::invokeMethod(source,"editingFinished");auto *main=d->findChild<QComboBox*>("templateImportMain");check("wizard detects main file without source editing",waitFor([&]{return main->count()>0;},15000));d->findChild<QLineEdit*>("templateImportName")->setText("USTC Beamer");check("wizard chooses XeLaTeX",d->findChild<QComboBox*>("templateImportEngine")->currentText()=="XeLaTeX");capture(d,"import-ready");d->findChild<QPushButton*>("templateImportRender")->click();check("actual USTC preview compiles",waitFor([&]{return d->property("researchTemplatePreviewReady").toBool();},60000));capture(d,"import-preview");auto *save=d->findChild<QPushButton*>("templateImportSave");check("import stays available after preview",save->isEnabled());save->click();QPointer<QDialog> guard(d);QTimer::singleShot(6000,&app,[&,guard]{if(guard&&guard->isVisible()){capture(guard,"import-save-failure");put(folder+"/save-failure.txt",(guard->findChild<QLabel*>("templateImportNotice")->text()+"\n"+guard->findChild<QComboBox*>("templateImportMain")->currentText()+"\n"+guard->findChild<QLineEdit*>("templateImportName")->text()).toUtf8());guard->reject();}});});
 const QString saved=importTemplateDialog(owner,library);check("import persists a reusable template and real preview",!saved.isEmpty()&&templateInfo(saved).valid()&&QFileInfo::exists(templateInfo(saved).preview));
 if(saved.isEmpty()){owner->setModified(false);app.exit(1);return;}
 const auto info=templateInfo(saved);QString error;const auto created=instantiateTemplate(saved,folder+QString::fromUtf8("/报告一"),&error);check("new project copies all template resources",!created.isEmpty()&&QFileInfo::exists(QFileInfo(created).absolutePath()+"/theme/ustc_background.tex")&&QFileInfo::exists(QFileInfo(created).absolutePath()+"/LICENSE"));
 QFile original(saved);original.open(QIODevice::ReadOnly);const auto bytes=original.readAll();check("existing destination is never overwritten",instantiateTemplate(saved,folder+QString::fromUtf8("/报告一"),&error).isEmpty());
 auto *w=new TeXDocumentWindow(created);w->resize(1000,760);w->show();settle(400);w->typeset();check("created USTC document compiles in normal editor",waitFor([&]{return !w->isTypesetting()&&!w->property("researchBuildPreparing").toBool()&&!w->property("researchBuildFinishing").toBool()&&!w->property("researchPublishing").toBool();},60000)&&pdfPairValid(previewFile(created)));check("imported project activates slide editing",waitFor([&]{auto *status=w->findChild<QLabel*>("researchSlideStatus");return w->property("researchSlideMode").toBool()&&status&&!status->text().contains(QString::fromUtf8("正在核对"))&&!status->text().contains("Checking slides");},10000));capture(w,"created-project");
 QTextCursor cursor(w->textDoc());cursor.setPosition(0);cursor.insertText("% personal draft\n");w->save();QFile after(saved);after.open(QIODevice::ReadOnly);check("editing a project does not alter the saved template",after.readAll()==bytes);
 QTimer::singleShot(100,&app,[&]{auto *d=qobject_cast<QDialog*>(QApplication::activeModalWidget());d->findChild<QLineEdit*>("templateProjectParent")->setText(folder);d->findChild<QLineEdit*>("templateProjectName")->setText(QString::fromUtf8("报告二"));capture(d,"create-project");d->findChild<QPushButton*>("templateProjectCreate")->click();});const auto second=createTemplateProjectDialog(owner,saved);check("same saved template creates a second independent project",!second.isEmpty()&&second!=created);
 QTimer::singleShot(100,&app,[&]{auto *d=qobject_cast<QDialog*>(QApplication::activeModalWidget());d->reject();});check("cancel new project makes no document",createTemplateProjectDialog(owner,saved).isEmpty());
 bool project=false;QTimer::singleShot(150,&app,[&]{auto *d=qobject_cast<QDialog*>(QApplication::activeModalWidget());d->resize(520,650);auto *tree=d->findChild<QTreeView*>("treeView");auto *model=qobject_cast<QFileSystemModel*>(tree->model());waitFor([&]{return model->index(info.root).isValid();},3000);const auto index=model->index(info.root);tree->setCurrentIndex(index);settle(300);check("saved template is one visual library entry",model->data(index).toString()=="USTC Beamer"&&!model->hasChildren(index));capture(d,"template-library-half");QTimer::singleShot(150,&app,[&]{auto *create=qobject_cast<QDialog*>(QApplication::activeModalWidget());create->findChild<QLineEdit*>("templateProjectParent")->setText(folder);create->findChild<QLineEdit*>("templateProjectName")->setText("third-project");create->findChild<QPushButton*>("templateProjectCreate")->click();});d->accept();});const auto third=TemplateDialog::doTemplateDialog(&project);check("template chooser creates a saved project rather than an untitled main-file copy",project&&!third.isEmpty()&&QFileInfo::exists(third));
 QTimer::singleShot(150,&app,[&]{auto *d=qobject_cast<QDialog*>(QApplication::activeModalWidget());auto *tree=d->findChild<QTreeView*>("treeView");auto *model=qobject_cast<QFileSystemModel*>(tree->model());tree->setCurrentIndex(model->index(info.root));QTimer::singleShot(150,&app,[&]{auto *destination=qobject_cast<QDialog*>(QApplication::activeModalWidget());destination->reject();QTimer::singleShot(150,&app,[&]{auto *chooser=qobject_cast<QDialog*>(QApplication::activeModalWidget());check("cancel destination returns to template chooser",chooser&&chooser->objectName()=="researchTemplateDialog");if(chooser)chooser->reject();});});d->accept();});bool cancelledProject=true;check("cancelled destination and chooser create no project",TemplateDialog::doTemplateDialog(&cancelledProject).isEmpty()&&!cancelledProject);
 const int libraryCount=QDir(library).entryList(QDir::Dirs|QDir::NoDotAndDotDot).size();QTimer::singleShot(100,&app,[&]{auto *d=qobject_cast<QDialog*>(QApplication::activeModalWidget());auto *source=d->findChild<QLineEdit*>("templateImportSource");source->setText(zip);QMetaObject::invokeMethod(source,"editingFinished");key(d,Qt::Key_Escape);});check("Esc during archive loading cancels import",importTemplateDialog(owner,library).isEmpty());settle(500);check("cancelled wizard creates no library entry",QDir(library).entryList(QDir::Dirs|QDir::NoDotAndDotDot).size()==libraryCount);

 const QString broken=folder+"/broken";QDir().mkpath(broken);put(broken+"/main.tex","\\documentclass{article}\n\\usepackage{TeXlineMissingTestPackage}\n\\begin{document}Test\\end{document}\n");
 QTimer::singleShot(100,&app,[&]{auto *d=qobject_cast<QDialog*>(QApplication::activeModalWidget());auto *source=d->findChild<QLineEdit*>("templateImportSource");source->setText(broken);QMetaObject::invokeMethod(source,"editingFinished");auto *render=d->findChild<QPushButton*>("templateImportRender");waitFor([&]{return render->isEnabled();},10000);render->click();check("preview failure returns control",waitFor([&]{return render->isEnabled();},30000)&&!d->property("researchTemplatePreviewReady").toBool());auto *notice=d->findChild<QLabel*>("templateImportNotice");check("preview failure has a concise log entry",notice->text().contains("href"));QTimer::singleShot(100,&app,[&]{auto *log=qobject_cast<QDialog*>(QApplication::activeModalWidget());auto *text=log?log->findChild<QPlainTextEdit*>("researchTemplateLog"):nullptr;check("log opens with actual compiler failure",log&&log->objectName()=="researchTemplateLogDialog"&&text&&text->toPlainText().contains("TeXlineMissingTestPackage"));if(log){if(text)put(folder+"/preview-failure-log.txt",text->toPlainText().toUtf8());capture(log,"preview-failure-log");key(log,Qt::Key_Escape);}});QMetaObject::invokeMethod(notice,"linkActivated",Q_ARG(QString,QString("log")));check("Esc closes log and restores import",QApplication::activeModalWidget()==d);key(d,Qt::Key_Escape);});check("failed preview can be cancelled without saving",importTemplateDialog(owner,library).isEmpty());
 const auto privateLibrary=Tw::Utils::ResourcesLibrary::getLibraryPath("templates");const auto classic=privateLibrary+"/Beamer presentations/classic-blank.tex";QFile classicFile(classic);classicFile.open(QIODevice::ReadOnly);const auto classicSource=classicFile.readAll();check("classic blank default is bundled without a decorative theme",classicSource.contains("\\usetheme{default}")&&classicSource.count("\\begin{frame}")==1);
 auto *blank=new TeXDocumentWindow(classic,true);const auto blankPath=folder+"/classic.tex";check("independent classic source copied",put(blankPath,classicSource));blank->setModified(false);delete blank;auto *classicWindow=new TeXDocumentWindow(blankPath);classicWindow->show();classicWindow->typeset();check("classic blank compiles",waitFor([&]{return !classicWindow->isTypesetting()&&!classicWindow->property("researchBuildPreparing").toBool()&&!classicWindow->property("researchBuildFinishing").toBool()&&!classicWindow->property("researchPublishing").toBool();},45000)&&pdfPairValid(previewFile(blankPath)));
 for(auto *v:TeXDocumentWindow::documentList())v->setModified(false);check("template flow captures are complete",frames.size()>=4);app.exit(failures?1:0);
 });return app.exec();}
