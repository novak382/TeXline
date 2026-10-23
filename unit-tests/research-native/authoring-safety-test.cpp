#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "ResearchAuthoring.h"
#include "ResearchAppearance.h"
#include "ResearchCollaboration.h"
#include "ResearchSlideExport.h"
#include "ResearchWorkflow.h"
#include "TeXDocumentWindow.h"
#include "CompletingEdit.h"
#include "TWApp.h"
#include "Settings.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFile>
#include <QDir>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QTableWidget>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <functional>

static bool waitFor(const std::function<bool()> &f,int ms=15000){QElapsedTimer t;t.start();while(!f()&&t.elapsed()<ms){QCoreApplication::processEvents();QThread::msleep(3);}return f();}
static void settle(int ms=250){QElapsedTimer t;t.start();waitFor([&]{return t.elapsed()>=ms;},ms+1000);}
static bool put(const QString &p,const QString &s){QDir().mkpath(QFileInfo(p).absolutePath());QFile f(p);return f.open(QIODevice::WriteOnly)&&f.write(s.toUtf8())==s.toUtf8().size();}
static QString read(const QString &p){QFile f(p);return f.open(QIODevice::ReadOnly)?QString::fromUtf8(f.readAll()).replace("\r\n","\n"):QString();}
int main(int argc,char **argv){const auto report=QString::fromLocal8Bit(argv[1]);argc=1;QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
 QTimer::singleShot(80,&app,[&]{using namespace Research;QJsonArray checks;int failures=0;auto check=[&](const QString &name,bool pass){checks.append(QJsonObject{{"name",name},{"passed",pass}});failures+=!pass;QFile f(report);if(f.open(QIODevice::WriteOnly))f.write(QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures}}).toJson());};
 if(!qEnvironmentVariable("TEXLINE_TEST_DESKTOP").startsWith("TeXlineValidation-")){check("private desktop required",false);app.exit(1);return;}
 Tw::Settings().setValue("openPDFwithTeX",false);const auto folder=QFileInfo(report).absolutePath(),root=folder+"/paper/main.tex";
 const QString picture="\\begin{figure}[htbp]\n\\centering\n\\includegraphics[width=0.333333333\\linewidth]{sample.png}\n\\caption{Original}\\label{fig:one}\n\\end{figure}",original="\\documentclass{article}\n\\usepackage{graphicx}\n\\begin{document}\nOpening.\n"+picture+"\n\\end{document}\n";put(root,original);QImage image(360,150,QImage::Format_RGB32);image.fill(QColor("#aac5d4"));image.save(folder+"/paper/sample.png");QDir().mkpath(folder+"/external");image.save(folder+"/external/实验 图.png");const auto external=folder+"/external/实验 图.png";const auto assetHash=slideExportHash(external);
 auto *w=new TeXDocumentWindow(root);w->resize(qMin(900,w->screen()->availableGeometry().width()-20),qMin(760,w->screen()->availableGeometry().height()-20));w->show();settle(400);
 auto at=[&]{auto c=w->textCursor();c.setPosition(w->textDoc()->editorText().indexOf("includegraphics"));w->editor()->setTextCursor(c);};auto open=[&]{at();w->findChild<QAction*>("researchEditObject")->trigger();settle();return w->findChild<QDialog*>("researchObjectDialog");};auto close=[&]{if(auto *d=w->findChild<QDialog*>("researchObjectDialog"))d->reject();waitFor([&]{return !w->findChild<QDialog*>("researchObjectDialog");});};auto apply=[&](QDialog *d){d->findChild<QDialogButtonBox*>("researchObjectButtons")->button(QDialogButtonBox::Apply)->click();settle();};
 auto *d=open();if(!d){check("figure editor exists",false);w->setModified(false);delete w;app.exit(1);return;}
 d->findChild<QPlainTextEdit*>("researchObjectCaption")->setPlainText("Caption only");apply(d);check("caption editing preserves an exact non-round width",w->textDoc()->editorText().contains("width=0.333333333\\linewidth")&&w->textDoc()->editorText().contains("Caption only"));w->editor()->undo();
 auto choose=[&](QDialog *parent){bool picked=false;QTimer timer,watchdog;timer.setInterval(10);watchdog.setSingleShot(true);QObject::connect(&timer,&QTimer::timeout,&app,[&]{for(auto *widget:QApplication::topLevelWidgets())if(auto *dialog=qobject_cast<QFileDialog*>(widget)){timer.stop();dialog->selectFile(external);QMetaObject::invokeMethod(dialog,"accept",Qt::QueuedConnection);picked=true;}});QObject::connect(&watchdog,&QTimer::timeout,&app,[&]{for(auto *widget:QApplication::topLevelWidgets())if(auto *dialog=qobject_cast<QFileDialog*>(widget))dialog->reject();});timer.start();watchdog.start(6000);parent->findChild<QToolButton*>("researchFigureChoose")->click();timer.stop();watchdog.stop();return picked;};
 d=open();check("image chooser accepts a private Unicode file",choose(d));d->findChild<QPushButton*>("researchObjectPreviewButton")->click();check("external Unicode image previews before applying or copying into the paper",waitFor([&]{return d->findChild<QLabel*>("researchObjectPreview")->property("researchDraftReady").toBool();},45000));close();check("cancelled image replacement copies no resources",!QFileInfo(folder+"/paper/figures").exists()&&w->textDoc()->editorText()==original);
 d=open();check("replacement chooser can be reopened",choose(d));apply(d);const auto imported=QString("figures/figure-")+QString::fromLatin1(assetHash.toHex().left(16))+".png";check("external asset uses safe relative content name",w->textDoc()->editorText().contains(imported)&&slideExportHash(folder+"/paper/"+imported)==assetHash);check("replacement leaves shared originals intact",slideExportHash(external)==assetHash&&slideExportHash(folder+"/paper/sample.png")==assetHash);w->editor()->undo();check("asset reference replacement is one undo",w->textDoc()->editorText()==original);
 d=open();d->findChild<QPlainTextEdit*>("researchObjectCaption")->setPlainText("Retained after disk change");put(root,QString(original).replace("Opening.","New external opening."));apply(d);check("unreceived disk change blocks stale apply and keeps draft",w->findChild<QDialog*>("researchObjectDialog")&&d->findChild<QPlainTextEdit*>("researchObjectSource")->toPlainText().contains("Retained after disk change"));close();w->setModified(false);delete w;settle();
 // Unsaved documents have no resource directory; retain the draft without copying.
 w=new TeXDocumentWindow();w->editor()->setPlainText("\\documentclass{article}\n\\begin{document}\nText.\n\\end{document}\n");w->show();settle(150);w->findChild<QAction*>("researchInsertFigure")->trigger();settle();d=w->findChild<QDialog*>("researchObjectDialog");check("unsaved note can choose an image without silently creating a project",d&&choose(d));if(d){apply(d);check("unsaved image apply retains its draft and requests a real document location",w->untitled()&&w->findChild<QDialog*>("researchObjectDialog")&&d->findChild<QLabel*>("researchObjectNotice")->text().contains(Research::englishUi()?"Save":"保存"));close();}w->setModified(false);delete w;settle();
 // A new figure adds only its required package, in the same undo block.
 const auto tikzRoot=folder+"/drawing.tex";const QString plain="\\documentclass{article}\n\\begin{document}\nText.\n\\end{document}\n";put(tikzRoot,plain);w=new TeXDocumentWindow(tikzRoot);w->show();settle(300);auto c=w->textCursor();c.setPosition(plain.indexOf("\\end{document}"));w->editor()->setTextCursor(c);w->findChild<QAction*>("researchInsertFigure")->trigger();settle();d=w->findChild<QDialog*>("researchObjectDialog");d->findChild<QComboBox*>("researchFigureOrigin")->setCurrentIndex(1);d->findChild<QPushButton*>("researchObjectPreviewButton")->click();check("TikZ draft preview uses actual project engine",waitFor([&]{return d->findChild<QLabel*>("researchObjectPreview")->property("researchDraftReady").toBool()||d->findChild<QPushButton*>("researchObjectPreviewButton")->isEnabled();},120000)&&d->findChild<QLabel*>("researchObjectPreview")->property("researchDraftReady").toBool());settle();d->grab().save(folder+"/tikz-preview.png");apply(d);check("new drawing adds TikZ to original preamble",w->textDoc()->editorText().contains("\\usepackage{tikz}")&&w->textDoc()->editorText().contains("\\begin{tikzpicture}"));w->editor()->undo();check("drawing and package are one undo",w->textDoc()->editorText()==plain);w->setModified(false);delete w;settle();
 for(const auto &style:QStringList{"natbib","biblatex"}){const auto path=folder+"/"+style+".tex";const auto text="\\documentclass{article}\n\\usepackage{"+style+"}\n\\begin{document}\nQuoted text.\n\\bibliography{refs}\n\\end{document}\n";put(path,text);put(folder+"/refs.bib","@book{local, author={Doe, Jane},title={Fictional fixture},year={2026}}\n");w=new TeXDocumentWindow(path);w->show();settle(150);w->findChild<QAction*>("researchInsertCitation")->trigger();auto *cite=w->findChild<QDialog*>("researchCitationDialog");check(style+" local bibliography loads",cite&&waitFor([&]{return cite->findChild<QListWidget*>("researchCitationList")->count()==1;}));if(cite){const auto command=cite->findChild<QComboBox*>("researchCitationStyle")->currentData().toString();check(style+" retains compatible citation conventions",command==(style=="natbib"?"citep":"autocite"));settle();const auto grab=cite->grab().toImage();const auto background=grab.pixelColor(grab.width()-5,grab.height()/2);check(style+" steady dialog uses white background",background.red()>250&&background.green()>250&&background.blue()>250);if(style=="biblatex"){cite->findChild<QLineEdit*>("researchCitationLocator")->setText("42");put(folder+"/refs.bib","@book{changed,title={Changed fixture}}\n");cite->findChild<QDialogButtonBox*>("researchCitationButtons")->button(QDialogButtonBox::Ok)->click();check("bibliography changing during selection retains picker and locator",w->findChild<QDialog*>("researchCitationDialog")&&cite->findChild<QLineEdit*>("researchCitationLocator")->text()=="42"&&w->textDoc()->editorText()==text);}cite->reject();}w->setModified(false);delete w;settle();}
 // Batch project review does not open previously unopened chapters.
 const auto project=folder+"/project/main.tex",child=folder+"/project/chapter.tex";const QString mainText="\\documentclass{article}\n\\begin{document}\nOriginal root.\n\\input{chapter}\n\\end{document}\n",childText="Original child.\n";put(project,mainText);put(child,childText);w=new TeXDocumentWindow(project);w->show();settle(1800);const auto rootRemote=QString(mainText).replace("Original root","External root"),childRemote=QString(childText).replace("Original child","External child");put(project,rootRemote);put(child,childRemote);w->collaboration()->receive(rootRemote);auto *review=w->findChild<QAction*>("researchExternalReview");if(!review)for(auto *a:w->findChildren<QAction*>())if(a->objectName().contains("Review"))review=a;
 // Review text is localized; the count is invariant across UI languages.
 check("multi-file batch discovers both source changes",review&&waitFor([&]{return review->isVisible()&&review->text().contains("2");}));check("batch fixture chapter remains unopened",!TeXDocumentWindow::findDocument(child));w->collaboration()->review();settle();if(auto *all=w->findChild<QPushButton*>("researchUseAllRemote"))all->click();settle(1000);check("batch accepts all files without creating chapter tabs",!TeXDocumentWindow::findDocument(child)&&w->collaboration()->reviewCount()==0&&review&&!review->isVisible()&&read(child)==childRemote);w->setModified(false);delete w;settle();
 app.exit(failures?1:0);
 });return app.exec();}
