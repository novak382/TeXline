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

#include "ResearchWorkflow.h"
#include "TWUtils.h"
#include <QDirIterator>
#include <QCryptographicHash>
static QByteArray bytes(const QString &p){QFile f(p);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray();}
int main(int argc,char **argv){
 if(argc<2||!privateDesktop())return 2;const QString report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
 QTimer::singleShot(100,&app,[&]{using namespace Research;const QString folder=QFileInfo(report).absolutePath();QJsonArray checks,frames,timings;int failures=0;
 auto flush=[&]{put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"frames",frames},{"timings",timings}}).toJson());};
 auto check=[&](const QString &name,bool ok){checks.append(QJsonObject{{"name",name},{"passed",ok}});failures+=!ok;flush();};
 auto capture=[&](QWidget *w,const QString &name){auto image=w->grab();image.save(folder+"/"+name+".png");frames.append(QJsonObject{{"name",name},{"dpr",image.devicePixelRatio()}});flush();};
 check("open defaults expose class, style and bibliography files",TWUtils::filterList()->first().contains("*.cls")&&TWUtils::filterList()->first().contains("*.sty")&&TWUtils::filterList()->first().contains("*.bib"));
 const QString zip=QFileInfo(qEnvironmentVariable("TEXLINE_TEMPLATE_ZIP")).absolutePath()+"/elegantbook.zip",library=folder+"/library";
 const auto staged=stageTemplate(zip,folder+QString::fromUtf8("/原始书稿"),std::make_shared<std::atomic_bool>(false));
 check("ElegantBook detects Chinese and English main files",staged.error.isEmpty()&&staged.mains.contains("elegantbook-cn.tex")&&staged.mains.contains("elegantbook-en.tex"));
 if(!staged.error.isEmpty()){app.exit(1);return;}
 check("class, bibliography, figures and license retained",QFileInfo::exists(staged.root+"/elegantbook.cls")&&QFileInfo::exists(staged.root+"/reference.bib")&&QFileInfo::exists(staged.root+"/figure/cover.jpg")&&QFileInfo::exists(staged.root+"/License"));
 QHash<QString,QByteArray> originals;QDirIterator files(staged.root,QDir::Files,QDirIterator::Subdirectories);while(files.hasNext()){const auto f=files.next();originals.insert(f,QCryptographicHash::hash(bytes(f),QCryptographicHash::Sha256));}
 auto *owner=new TeXDocumentWindow();owner->resize(1000,760);owner->show();settle(150);
 QTimer::singleShot(100,&app,[&]{auto *d=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!d)return;d->resize(660,650);auto *source=d->findChild<QLineEdit*>("templateImportSource");source->setText(staged.root);QMetaObject::invokeMethod(source,"editingFinished");auto *main=d->findChild<QComboBox*>("templateImportMain");check("ordinary folder wizard lists both book mains",waitFor([&]{return main->count()==2;},15000));main->setCurrentText("elegantbook-cn.tex");d->findChild<QLineEdit*>("templateImportName")->setText("ElegantBook");check("Chinese book chooses XeLaTeX",d->findChild<QComboBox*>("templateImportEngine")->currentText()=="XeLaTeX");capture(d,"book-main-selection");QElapsedTimer elapsed;elapsed.start();d->findChild<QPushButton*>("templateImportRender")->click();const bool rendered=waitFor([&]{return d->property("researchTemplatePreviewReady").toBool()||d->findChild<QPushButton*>("templateImportRender")->isEnabled();},150000)&&d->property("researchTemplatePreviewReady").toBool();timings.append(QJsonObject{{"name","book-preview"},{"milliseconds",elapsed.elapsed()}});check("unmodified official Chinese book preview compiles",rendered);put(folder+"/preview-log.txt",d->findChild<QLabel*>("templateImportNotice")->toolTip().toUtf8());capture(d,"book-preview");d->findChild<QPushButton*>("templateImportSave")->click();QPointer<QDialog> guard(d);QTimer::singleShot(10000,&app,[guard]{if(guard&&guard->isVisible())guard->reject();});});
 const auto saved=importTemplateDialog(owner,library);check("ordinary book is saved as reusable multi-file template",!saved.isEmpty()&&templateInfo(saved).valid());if(saved.isEmpty()){app.exit(1);return;}
 QString error;const auto created=instantiateTemplate(saved,folder+QString::fromUtf8("/我的书稿"),&error);check("book creates independent ordinary files",!created.isEmpty()&&QFileInfo::exists(QFileInfo(created).absolutePath()+"/elegantbook.cls"));
 const auto root=QFileInfo(created).absolutePath();
 auto *book=new TeXDocumentWindow(created);book->resize(1000,760);book->show();settle(300);
 auto compile=[&](TeXDocumentWindow *w,const QString &label){QElapsedTimer t;t.start();w->typeset();const bool finished=waitFor([&]{return !w->isTypesetting()&&!w->property("researchBuildPreparing").toBool()&&!w->property("researchBuildFinishing").toBool()&&!w->property("researchPublishing").toBool();},150000);timings.append(QJsonObject{{"name",label},{"milliseconds",t.elapsed()}});return finished&&pdfPairValid(previewFile(w->fileName()))&&w->property("researchBuildErrors").toInt()==0;};
 check("Chinese created book typesets in ordinary editor",compile(book,"book-first-build"));capture(book,"book-editor");
 bool bibliography=false;QDirIterator outputs(buildCacheDirectory(created),QStringList{"*.bbl"},QDir::Files,QDirIterator::Subdirectories);while(outputs.hasNext())bibliography|=bytes(outputs.next()).contains("\\entry{");check("Biber produced resolved bibliography",bibliography);
 const auto school=stageTemplate(qEnvironmentVariable("TEXLINE_TEMPLATE_ZIP"),folder+"/school",std::make_shared<std::atomic_bool>(false));
 for(const auto &name:QStringList{"elegantbook.cls","reference.bib","ustcbeamer.sty"}){
  const auto path=(name.endsWith(".sty")?school.root:root)+"/"+name;const auto original=bytes(path);auto *editor=new TeXDocumentWindow(path);editor->resize(1000,760);editor->show();settle(120);check("template source opens as editable text: "+name,!editor->editor()->isReadOnly()&&!editor->textDoc()->isEmpty());QTextCursor c(editor->textDoc());c.movePosition(QTextCursor::End);c.insertText("\n% TeXline template edit\n");if(name.endsWith(".cls"))c.insertText("\\AtBeginDocument{\\typeout{TEXLINECLASSCHECK}}\n");editor->save();check("template source saves: "+name,bytes(path).contains("% TeXline template edit"));if(name.endsWith(".cls")){check("edited local class participates in main build",compile(book,"book-edited-class"));bool marker=false;QDirIterator logs(buildCacheDirectory(created),QStringList{"*.log"},QDir::Files,QDirIterator::Subdirectories);while(logs.hasNext())marker|=bytes(logs.next()).contains("TEXLINECLASSCHECK");check("main build actually consumed class edit",marker);}while(editor->textDoc()->isUndoAvailable())editor->textDoc()->undo();editor->save();check("template source undo restores content: "+name,QString::fromUtf8(bytes(path)).trimmed()==QString::fromUtf8(original).trimmed());capture(editor,name=="elegantbook.cls"?"class-editor":(name.endsWith(".sty")?"style-editor":"bibliography-editor"));editor->setModified(false);delete editor;
 }
 check("main book rebuilds after class and bibliography saves",compile(book,"book-rebuild"));
 auto *english=new TeXDocumentWindow(staged.root+"/elegantbook-en.tex");english->resize(1000,760);english->show();settle(150);check("ordinary folder opens directly without template registration",!templateInfo(english->fileName()).valid());check("unmodified English main compiles directly from folder",compile(english,"book-english-direct"));capture(english,"book-direct-folder");
 bool intact=true;for(auto it=originals.cbegin();it!=originals.cend();++it)intact&=QCryptographicHash::hash(bytes(it.key()),QCryptographicHash::Sha256)==it.value();check("preview and direct compile preserve original template files",intact);
 for(auto *w:TeXDocumentWindow::documentList())w->setModified(false);flush();app.exit(failures?1:0);
 });return app.exec();}
