// Real widgets and editor events, on an inactive desktop with private settings.
#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "TeXDocumentWindow.h"
#include "PDFDocumentWindow.h"
#include "CompletingEdit.h"
#include "PrefsDialog.h"
#include "ResearchAppearance.h"
#include "ResearchCommands.h"
#include "ResearchCompletion.h"
#include "ResearchShorthand.h"
#include "ResearchCommandCompletion.h"
#include "Settings.h"
#include <QAction>
#include <QToolButton>
#include <QToolBar>
#include <QStatusBar>
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
 QTimer::singleShot(100,&app,[&]{
 const QString folder=QFileInfo(report).absolutePath();QJsonArray checks,frames;int failures=0;
 auto check=[&](const QString &name,bool ok){checks.append(QJsonObject{{"name",name},{"passed",ok}});failures+=!ok;put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"frames",frames}}).toJson());};
 auto capture=[&](QWidget *w,const QString &name){auto p=w->grab();p.save(folder+"/"+name+".png");frames.append(QJsonObject{{"name",name},{"dpr",p.devicePixelRatio()}});};
 QString clean="\\documentclass{article}\n\\begin{document}\n";for(int n=1;n<=61;++n)clean+=QString("Page %1. Independent typography fixture.\n").arg(n)+(n<61?"\\newpage\n":"");clean+="\\end{document}\n";
 QString warnings;for(int n=0;n<55;++n)warnings+=QString("\\PackageWarningNoLine{quiet}{Independent warning %1}\n").arg(n);
 QString original=clean;original.insert(original.indexOf("\\begin{document}"),warnings);
 const QString file=folder+"/quiet.tex";check("independent 61 page source",put(file,original.toUtf8()));Tw::Settings().setValue("openPDFwithTeX",true);
 auto *w=new TeXDocumentWindow(file);w->resize(1000,760);w->show();settle(500);
 const auto locale=Tw::Settings().value("locale");
 QTimer::singleShot(150,&app,[&]{auto *d=qobject_cast<QDialog*>(QApplication::activeModalWidget());auto *combo=d?d->findChild<QComboBox*>("localePopup"):nullptr;check("only supported Simplified Chinese, Traditional Chinese and English",combo&&combo->count()==3&&combo->itemData(0)=="zh_CN"&&combo->itemData(1)=="zh_TW"&&combo->itemData(2)=="en_US");capture(d,"languages");combo->showPopup();settle(100);capture(combo->view(),"language-options");combo->hidePopup();d->reject();});PrefsDialog::doPrefsDialog(w);
 check("cancel preserves language preference",Tw::Settings().value("locale")==locale);
 check("legacy unsupported locales recover to English",Research::supportedUiLocale("fr_FR")=="en_US"&&Research::supportedUiLocale("zh_TW")=="zh_TW");
 auto *menu=w->findChild<QMenu*>("researchMoreMenu");auto *insert=w->findChild<QMenu*>("researchInsertMenu");
 check("Insert is first and shared with focus",menu&&insert&&menu->actions().first()==insert->menuAction()&&menu->actions()==w->findChild<QMenu*>("researchFocusMenu")->actions());
 auto *more=w->findChild<QToolButton*>("researchMore");check("menu entry has a readable label",more&&more->text()==(Research::englishUi()?"Menu":"菜单"));
 bool duplicate=false;for(auto *a:insert->actions())duplicate|=a->objectName()=="researchConfigureCompletion";check("snippet settings do not duplicate the insertion workflow",!duplicate);
 auto build=[&]{w->typeset();return waitFor([&]{return !w->isTypesetting()&&!w->property("researchBuildPreparing").toBool()&&!w->property("researchBuildFinishing").toBool()&&!w->property("researchPublishing").toBool();},45000);};
 check("real warning compile completes",build());settle(500);
 auto *compile=w->findChild<QToolButton*>("researchCompile");auto *pdf=w->findChild<QToolButton*>("researchPdfButton");auto *indicator=w->findChild<QAction*>("researchProblemIndicator");auto *problems=w->findChild<QAction*>("researchProblemCount");
 check("55 real warnings are counted",w->property("researchBuildWarnings").toInt()==55&&w->property("researchBuildErrors").toInt()==0);
 check("warning count takes no toolbar space",indicator&&!indicator->isVisible()&&problems&&problems->isVisible());
 check("warning is attached to compile control",compile&&!compile->property("researchStatusHint").toString().isEmpty()&&compile->findChild<QWidget*>("researchControlStatusDot")->isVisible());
 auto *engine=w->findChild<QMenu*>("researchEngineMenu");QMetaObject::invokeMethod(engine,"aboutToShow");check("warning details remain available in compile menu",engine->actions().contains(problems));
 problems->trigger();settle(100);check("warning action opens actual build details",w->findChild<QTabWidget*>("consoleTabs")->isVisible());problems->trigger();settle(100);capture(w,"warnings-full");capture(w->findChild<QToolBar*>("toolBar_run"),"toolbar-warnings");capture(w->statusBar(),"status-bar");engine->popup(compile->mapToGlobal(QPoint(0,compile->height())));settle(100);capture(engine,"warning-menu");engine->close();
 const int pdfWidth=pdf->width();w->resize(520,760);settle(150);capture(w,"warnings-half");menu->popup(more->mapToGlobal(QPoint(0,more->height())));settle(100);capture(menu,"menu-half");menu->close();insert->popup(more->mapToGlobal(QPoint(-180,more->height())));settle(100);capture(insert,"insert-menu");insert->close();
 auto viewers=PDFDocumentWindow::documentList();check("compiled PDF opens",!viewers.isEmpty());if(viewers.isEmpty()){app.exit(1);return;}auto *viewer=viewers.last();viewer->resize(520,760);viewer->show();settle(200);
 auto *field=viewer->findChild<QSpinBox*>("researchPdfPageField");auto *total=viewer->findChild<QLabel*>("researchPdfPageTotal");auto *line=field->findChild<QLineEdit*>();check("page control shows 1 of 61",field->value()==1&&field->maximum()==61&&total->text().contains("61"));
 check("numerator and total use same resolved font",QFontInfo(line->font()).family()==QFontInfo(total->font()).family()&&QFontInfo(line->font()).pixelSize()==QFontInfo(total->font()).pixelSize());
 capture(viewer,"pdf-current");capture(field->parentWidget(),"page-control");
 field->setValue(61);settle(120);check("page navigation retains full range",field->value()==61);field->setValue(1);settle(80);
 QTextCursor edit(w->textDoc());edit.setPosition(0);edit.insertText("% draft change\n");settle(500);
 check("draft PDF caption remains short and stable",pdf->text()=="PDF"&&pdf->width()==pdfWidth&&pdf->property("researchPdfNeedsBuild").toBool());
 auto *revision=viewer->findChild<QLabel*>("researchPdfRevision");check("previous PDF has compact marker and full accessible explanation",revision&&revision->text()==QString(QChar(0x2022))&&!revision->toolTip().isEmpty()&&revision->accessibleName()==revision->toolTip());
 capture(w,"draft-half");capture(w->findChild<QToolBar*>("toolBar_run"),"toolbar-draft");capture(viewer,"pdf-previous");Research::setFocusMode(true);settle(300);capture(w,"focus-half");capture(viewer,"pdf-focus-half");check("focus compile shares warning hint",w->findChild<QToolButton*>("researchFocusCompile")->property("researchStatusHint")==compile->property("researchStatusHint"));Research::setFocusMode(false);settle(150);
 w->textDoc()->undo();settle(400);check("undo returns to current PDF without wider caption",!pdf->property("researchPdfNeedsBuild").toBool()&&pdf->text()=="PDF");
 {QTextCursor c(w->textDoc());c.select(QTextCursor::Document);c.insertText(clean);}check("clean rebuild completes",build());settle(300);check("clean rebuild clears warning dot and details",w->property("researchBuildWarnings").toInt()==0&&compile->property("researchStatusHint").toString().isEmpty()&&!problems->isVisible());
 {QTextCursor c(w->textDoc());c.select(QTextCursor::Document);c.insertText(clean.replace("\\begin{document}","\\begin{document}\n\\UndefinedQuietCommand"));}check("error compile finishes",build());settle(300);check("errors remain visible",w->property("researchBuildErrors").toInt()>0&&indicator->isVisible());capture(w,"error-retained");
 for(auto *v:TeXDocumentWindow::documentList())v->setModified(false);check("all native captures completed",frames.size()>=10);app.exit(failures?1:0);
 });return app.exec();}
