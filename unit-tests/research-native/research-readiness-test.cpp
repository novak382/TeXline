// Review the changed workflows before broad regression; private desktop only.
#include "ActualAuthoringRecording.h"
#include "ResearchBibliography.h"
#include "ResearchTemplates.h"
#include "ResearchFileWrite.h"
#include "ResearchSettings.h"
using namespace Research;
static void when(QWidget *parent,const QString &name,std::function<void(QDialog*)> fn){
 auto *t=new QTimer(parent);QObject::connect(t,&QTimer::timeout,t,[=]{auto *d=parent->findChild<QDialog*>(name);if(!d||!d->isVisible())return;t->stop();t->deleteLater();QTimer::singleShot(20000,d,&QDialog::reject);fn(d);});t->start(15);
}
int main(int argc,char **argv){const auto report=argument(1);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
QTimer::singleShot(60,&app,[&]{
 const auto folder=QFileInfo(report).absolutePath();QJsonArray checks,measurements;int failures=0;
 auto check=[&](const QString &name,bool pass){checks.append(QJsonObject{{"name",name},{"passed",pass}});failures+=!pass;put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"measurements",measurements},{"devicePixelRatio",qApp->primaryScreen()->devicePixelRatio()},{"realUserTrial",false}}).toJson());};
 const QString original="% preserved comment\n@string{local={Local publisher}}\n@misc{stable,title={旧标题 {α}},author={胡, 洁洋},doi={10.1000/example},x-local={Private label},publisher=local}\n@book{other,title={Other},year=2024}\n";
 const QString incoming="@article{exportKey,title={Updated {α}},author={Hu, Jieyang},doi={https://doi.org/10.1000/EXAMPLE},year=2026}";
 QString citation,error;const auto updated=updateBibliographyEntry(original,incoming,&citation,&error);
 check("refresh retains stable citation key and local fields",error.isEmpty()&&citation=="stable"&&updated.contains("@article{stable,")&&updated.contains("x-local={Private label}")&&updated.contains("publisher=local")&&updated.startsWith("% preserved comment\n@string"));
 check("refresh adds literal numeric metadata",updated.contains("year = {2026}")&&parseBibliography(updated).records.size()==2);
 const auto unrelated=updateBibliographyEntry(original,"@article{stable,title={Unrelated},doi={10.1000/other}}",&citation,&error);
 check("same-key different-DOI collision is rejected",unrelated.isEmpty()&&!error.isEmpty());
 updateBibliographyEntry(original+"\n@misc{duplicate,doi={10.1000/example}}",incoming,&citation,&error);
 check("ambiguous identity is rejected",!error.isEmpty());
 updateBibliographyEntry(original,"@article{exportKey,title=undefinedMacro,doi={10.1000/example}}",&citation,&error);
 check("unbound macro update is rejected",!error.isEmpty());
 const auto unchanged=updateBibliographyEntry(updated,incoming,&citation,&error);
 check("repeated refresh is idempotent",error.isEmpty()&&unchanged==updated);
 check("custom entry type grammar matches bibliography indexing",parseBibliography("@data.archive-v2{dataset,title={Research}}").error.isEmpty());
 for(const auto &raw:QStringList{"@book{a,title={A}}","@book{a,title={A},}","@book{a,title={A} % local comment\n}","@book(a,title={A})","@book{a}"}){
   const auto result=patchBibliography(raw,"a",{{"year","2026"}},&error);
   check("append field preserves valid delimiters: "+raw,error.isEmpty()&&parseBibliography(result).records.size()==1&&result.contains("2026"));
 }
 QString huge;for(int i=0;i<1500;++i)huge+=QString("@article{k%1,title={Evidence %1},doi={10.1000/%1},year={2026}}\n").arg(i);
 QElapsedTimer clock;clock.start();QStringList added;const auto merged=mergeBibliography({},huge,&added,&error);const auto ms=clock.elapsed();
 measurements.append(QJsonObject{{"operation","merge 1500 references"},{"milliseconds",ms}});
 check("large bibliography merge preserves all records",error.isEmpty()&&added.size()==1500&&parseBibliography(merged).records.size()==1500);
 const auto publicFolder=qEnvironmentVariable("TEXLINE_PUBLIC_BIB");
 for(const auto &file:QDir(publicFolder).entryInfoList({"*.bib"},QDir::Files)){
   const auto source=QString::fromUtf8(bytes(file.absoluteFilePath()));const auto parsed=parseBibliography(source);
   check("public BBT sample parses: "+file.fileName(),parsed.error.isEmpty()&&!parsed.records.isEmpty());
   if(parsed.error.isEmpty()&&!parsed.records.isEmpty()){
     const auto key=parsed.records.first().key;const auto patched=patchBibliography(source,key,{{"texlinecheck","Independent verification"}},&error);
     check("public sample edit preserves record count: "+file.fileName(),error.isEmpty()&&parseBibliography(patched).records.size()==parsed.records.size());
   }
 }
 const auto main=folder+"/main.tex",bib=folder+"/refs.bib";put(main,"\\documentclass{article}\n\\begin{document}Test\\end{document}\n");put(bib,original.toUtf8());
 Tw::Settings().setValue("openPDFwithTeX",false);auto *w=new TeXDocumentWindow(main);w->resize(qEnvironmentVariableIsSet("TEXLINE_TEST_HALF")?620:1040,760);w->show();settle();
 clock.restart();when(w,"researchBibImportPreview",[&](QDialog *d){measurements.append(QJsonObject{{"operation","open import preview 1500 references"},{"milliseconds",clock.elapsed()}});key(d,Qt::Key_Escape);});appendBibliography(w,w,bib,huge,&added);
 when(w,"researchBibImportPreview",[&](QDialog *d){auto *refresh=d->findChild<QCheckBox*>("researchBibRefreshExisting");check("refresh is explicit and off by default",refresh&&!refresh->isChecked());refresh->setChecked(true);d->grab().save(folder+"/reference-update.png");key(d,Qt::Key_Escape);});
 check("refresh cancel preserves bibliography disk",!appendBibliography(w,w,bib,incoming,&added)&&bytes(bib)==original.toUtf8());
 when(w,"researchBibImportPreview",[&](QDialog *d){d->findChild<QCheckBox*>("researchBibRefreshExisting")->setChecked(true);d->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();});
 check("refresh applies under original citation key",appendBibliography(w,w,bib,incoming,&added)&&added==QStringList{"stable"});
 auto *bw=TeXDocumentWindow::findDocument(bib);check("refresh stays in editor until save",bw&&bw->textDoc()->editorText()==updated&&bytes(bib)==original.toUtf8());
 if(bw){bw->editor()->undo();check("one undo restores exact bibliography",bw->textDoc()->editorText()==original);bw->editor()->redo();check("redo restores refresh",bw->textDoc()->editorText()==updated);bw->editor()->undo();}
 const auto newFile=folder+"/created.bib";check("new file publish succeeds",writeNewFile(newFile,"original",&error));check("new file publish cannot overwrite a later destination",!writeNewFile(newFile,"replacement",&error)&&bytes(newFile)=="original");
 const auto competing=folder+"/competing.tex";const bool wrote=writeObservedFile(competing,"planned",&error,[&]{put(competing,"later writer");return true;});
 check("new project file creation race retains external contents",!wrote&&bytes(competing)=="later writer");
 const auto archive=qEnvironmentVariable("TEXLINE_PROJECT_ZIP");
 check("private project ZIP provided",QFileInfo::exists(archive));
 if(QFileInfo::exists(archive)){
   when(w,"researchOpenProject",[&](QDialog *d){check("project is read asynchronously",waitFor([&]{return d->findChild<QComboBox*>("researchProjectMain")->count()>0;}));d->grab().save(folder+"/open-project.png");key(d,Qt::Key_Escape);});
   check("project cancel creates no destination",importProjectDialog(w,archive).isEmpty());
   when(w,"researchOpenProject",[&](QDialog *d){auto *m=d->findChild<QComboBox*>("researchProjectMain");check("multiple mains require explicit selection",waitFor([&]{return m->count()==2;})&&m->currentIndex()==-1);d->findChild<QLineEdit*>("researchProjectName")->setText("Imported paper");d->findChild<QLineEdit*>("researchProjectDestination")->setText(folder);m->setCurrentText("main.tex");d->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Open)->click();});
   const auto opened=importProjectDialog(w,archive);check("ZIP opens selected main in new project",opened==folder+"/Imported paper/main.tex"&&QFileInfo::exists(opened));
   check("project import preserves hidden configuration and resources",bytes(folder+"/Imported paper/.latexmkrc")=="# independent config\n"&&bytes(folder+"/Imported paper/sections/body.tex")=="Project content\n");
   check("archive bytes are unchanged",QFileInfo::exists(archive));
 }
 check("atomic writes leave no temporary file",QDir(folder).entryList({"*.texline-*"},QDir::Files|QDir::Hidden).isEmpty());
 for(auto *doc:TeXDocumentWindow::documentList()){doc->textDoc()->setModified(false);doc->close();}settle();check("review completed",true);app.exit(failures?1:0);
});return app.exec();}
