#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "TeXDocumentWindow.h"
#include "CompletingEdit.h"
#include "ResearchProject.h"
#include "ResearchAppearance.h"
#include <QAction>
#include <QCheckBox>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QSaveFile>
#include <QTextCursor>
#include <QTimer>
#include <algorithm>
using namespace Research;
static QString writeError;
static bool put(const QString &path,const QString &text){QDir().mkpath(QFileInfo(path).absolutePath());QSaveFile f(path);const bool ok=f.open(QIODevice::WriteOnly)&&f.write(text.toUtf8())==text.toUtf8().size()&&f.commit();writeError=ok?QString():f.errorString();return ok;}
static QByteArray get(const QString &path){QFile f(path);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray();}
static int count(const ProjectIndex &index,const QString &kind,const QString &key={}){return int(std::count_if(index.issues.cbegin(),index.issues.cend(),[&](const auto &e){return e.kind==kind&&(key.isEmpty()||e.key==key);}));}
int main(int argc,char **argv){const QString report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);QTimer::singleShot(0,&app,[&]{
 const QString folder=QFileInfo(report).absolutePath()+"/project",root=folder+"/main.tex",child=folder+"/章节/child.tex",bib=folder+"/refs.bib";
 QJsonArray checks;int failures=0;auto check=[&](const QString &name,bool ok){checks.append(QJsonObject{{"name",name},{"passed",ok}});if(!ok)++failures;};
 const QString source="\\documentclass{article}\n\\newtheorem{claim}{Claim}\n\\begin{document}\n\\section{Setup}\n\\label{sec:intro}\n\\ref{sec:intro} \\eqref{eq:missing}\n\\cref{dup,missing2} \\citep[see][p.~1]{good,absent}\n\\nocite{*} \\bibliography{refs}\n\\input{章节/child}\n% \\ref{commented}\n\\begin{verbatim}\\ref{fake}\\cite{fake}\\end{verbatim}\n\\refsymbol{not-a-reference}\n\\end{document}\n";
 const QString chapter="\\begin{claim}[Stable source]\n\\label{dup} Claim body.\n\\begin{proof}Proof body.\\end{proof}\n\\end{claim}\n\\label{dup}\n\\begin{equation}a=b\\label{eq:unicode😀}\\end{equation}\n";
 const QString bibliography="@article{good, title={Good source}, author={Alpha}, year={2025}}\n";
 check("independent paper chapters and bibliography are written",put(root,source)&&put(child,chapter)&&put(bib,bibliography));
 auto index=buildProjectIndex(root);check("literal missing labels are located across chapters",count(*index,"missing-label","eq:missing")==1&&count(*index,"missing-label","missing2")==1&&count(*index,"missing-label","sec:intro")==0);
 check("both duplicate definitions retain their actual chapter locations",count(*index,"duplicate-label","dup")==2&&std::all_of(index->issues.cbegin(),index->issues.cend(),[&](const auto &e){return e.kind!="duplicate-label"||(e.file==QFileInfo(child).canonicalFilePath()&&e.line>1);}));
 check("missing citation is distinguished from a valid bibliography key",count(*index,"missing-citation","absent")==1&&count(*index,"missing-citation","good")==0);
 check("comments literals wildcard nocite and longer commands are ignored",std::none_of(index->issues.cbegin(),index->issues.cend(),[](const auto &e){return e.key=="fake"||e.key=="commented"||e.key=="*"||e.key=="not-a-reference";}));
 check("custom theorem proof and Unicode equation labels remain navigable",std::any_of(index->entries.cbegin(),index->entries.cend(),[](const auto &e){return e.kind=="claim";})&&std::any_of(index->entries.cbegin(),index->entries.cend(),[](const auto &e){return e.kind=="proof";})&&std::any_of(index->entries.cbegin(),index->entries.cend(),[](const auto &e){return e.key=="eq:unicode😀";}));
 const auto issues=searchProject(*index,"missing",120,3),labelRows=searchProject(*index,"dup",120,1);check("diagnostic search is separate from citation or label completion",!issues.isEmpty()&&std::all_of(issues.cbegin(),issues.cend(),[&](int row){return row>=index->entries.size();})&&std::all_of(labelRows.cbegin(),labelRows.cend(),[&](int row){return row<index->entries.size();}));
 QString dirty=chapter;dirty.replace("\\label{dup}\n", "\\label{missing2}\n");auto modified=buildProjectIndex(root,{{QFileInfo(child).canonicalFilePath(),dirty}});check("dirty buffers update checks without rewriting the external chapter",count(*modified,"duplicate-label")==0&&count(*modified,"missing-label","missing2")==0&&get(child)==chapter.toUtf8());
 const QString dynamic=folder+"/dynamic/main.tex",dynamicSource="\\documentclass{article}\n\\newcommand{\\makeLabel}[1]{\\label{#1}}\n\\makeLabel{generated} \\ref{generated}\n";check("dynamic-label fixture is written",put(dynamic,dynamicSource));auto generated=buildProjectIndex(dynamic);check("macro-generated labels explicitly require compilation",count(*generated,"uncertain-label","generated")==1&&count(*generated,"missing-label","generated")==0);
 const QString missing=folder+"/missing/main.tex";check("missing bibliography fixture is written",put(missing,"\\documentclass{article}\n\\bibliography{new/absent}\\cite{unknown}\n"));auto unavailable=buildProjectIndex(missing);check("missing bibliography records its declaration and uncertain citation",count(*unavailable,"missing-bibliography","new/absent")==1&&count(*unavailable,"uncertain-citation","unknown")==1&&count(*unavailable,"missing-citation","unknown")==0);
 check("English diagnostics translate interface text only",ui("检查标签与文献",true)=="Check labels and references"&&ui("未找到标签",true)=="Label not found");
 auto *window=new TeXDocumentWindow(root);window->show();window->activateWindow();QCoreApplication::processEvents();QTextCursor cursor(window->textDoc());cursor.setPosition(source.indexOf("\\eqref"));window->editor()->setTextCursor(cursor);const int reading=window->cursorPosition();
 int stage=0;bool modeAvailable=false,repairObserved=false,repairWritten=false;int repairAttempts=0;QJsonArray trace;QElapsedTimer deadline;deadline.start();QTimer observer;observer.setInterval(20);
 QObject::connect(&observer,&QTimer::timeout,window,[&]{auto *dialog=window->findChild<QDialog*>("researchPalette");if(!dialog)return;auto *toggle=dialog->findChild<QCheckBox*>("researchNavigationChecks");auto *query=dialog->findChild<QLineEdit*>("researchNavigationQuery");auto *list=dialog->findChild<QListView*>("researchNavigationResults");if(!toggle||!query||!list){dialog->reject();return;}modeAvailable=true;
  if(stage==0){trace.append(QJsonObject{{"event","modal-found"},{"elapsedMs",deadline.elapsed()}});toggle->setChecked(true);query->setText("eq:missing");stage=1;return;}
  if(stage==1&&!dialog->property("researchNavigationBusy").toBool()&&list->model()->rowCount()>0){trace.append(QJsonObject{{"event","missing-row-visible"},{"elapsedMs",deadline.elapsed()}});dialog->grab().save(QFileInfo(report).absolutePath()+"/checks.png");stage=2;return;}
  if(stage==2&&!repairWritten){repairWritten=put(child,chapter+"\\label{eq:missing}\n");++repairAttempts;trace.append(QJsonObject{{"event","atomic-repair-write"},{"written",repairWritten},{"error",writeError},{"attempt",repairAttempts},{"elapsedMs",deadline.elapsed()}});if(!repairWritten&&repairAttempts>=10){dialog->reject();return;}return;}
  if(stage==2&&!dialog->property("researchNavigationBusy").toBool()&&list->model()->rowCount()==0){trace.append(QJsonObject{{"event","repair-visible"},{"elapsedMs",deadline.elapsed()}});repairObserved=true;dialog->reject();return;}
  if(deadline.elapsed()>6000){trace.append(QJsonObject{{"event","deadline"},{"stage",stage},{"busy",dialog->property("researchNavigationBusy").toBool()},{"rows",list->model()->rowCount()},{"elapsedMs",deadline.elapsed()}});dialog->grab().save(QFileInfo(report).absolutePath()+"/deadline.png");dialog->reject();}
 });observer.start();openProjectNavigation(window);observer.stop();check("independent external chapter repair is atomically committed",repairWritten&&get(child).contains("\\label{eq:missing}"));check("navigation offers read-only checks and refreshes an outside repair",modeAvailable&&repairObserved);check("checking never edits source buffer or root file",window->editor()->toPlainText()==source&&get(root)==source.toUtf8()&&!window->isModified());
 stage=0;bool jumped=false;deadline.restart();QObject::disconnect(&observer,nullptr,window,nullptr);
 QObject::connect(&observer,&QTimer::timeout,window,[&]{auto *dialog=window->findChild<QDialog*>("researchPalette");if(!dialog)return;auto *toggle=dialog->findChild<QCheckBox*>("researchNavigationChecks");auto *query=dialog->findChild<QLineEdit*>("researchNavigationQuery");auto *list=dialog->findChild<QListView*>("researchNavigationResults");if(!toggle||!query||!list){dialog->reject();return;}if(stage==0){toggle->setChecked(true);query->setText("dup");stage=1;return;}if(!dialog->property("researchNavigationBusy").toBool()&&list->model()->rowCount()>0){list->setCurrentIndex(list->model()->index(0,0));jumped=true;dialog->accept();}else if(deadline.elapsed()>6000){trace.append(QJsonObject{{"event","deadline"},{"stage",stage},{"busy",dialog->property("researchNavigationBusy").toBool()},{"rows",list->model()->rowCount()},{"elapsedMs",deadline.elapsed()}});dialog->grab().save(QFileInfo(report).absolutePath()+"/deadline.png");dialog->reject();}});observer.start();openProjectNavigation(window);observer.stop();
 auto *target=TeXDocumentWindow::findDocument(child);check("a diagnostic jumps to the actual chapter",jumped&&target&&target->cursorPosition()>0);if(target){target->findChild<QAction*>("researchNavigateBack")->trigger();QCoreApplication::processEvents();}check("back navigation restores the original source reading position",window->cursorPosition()==reading);
 if(qEnvironmentVariableIsSet("TEXLINE_TEST_NAV_OWNER_DESTROY")){auto *temporary=new TeXDocumentWindow(root);temporary->show();QTimer::singleShot(30,&app,[temporary]{temporary->setModified(false);delete temporary;});openProjectNavigation(temporary);check("closing the owning document safely retires modal navigation",true);}
 for(auto *doc:TeXDocumentWindow::documentList()){doc->setModified(false);doc->close();}QSaveFile output(report);if(output.open(QIODevice::WriteOnly)){output.write(QJsonDocument(QJsonObject{{"checks",checks},{"trace",trace},{"failures",failures},{"physicalInputVerified",false}}).toJson());output.commit();}app.exit(failures?1:0);
 });return app.exec();}
