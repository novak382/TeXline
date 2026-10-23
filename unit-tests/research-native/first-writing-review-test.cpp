// A bounded first-document walkthrough, using the actual template/save/compile UI.
#include "ActualAuthoringRecording.h"
#include "ResearchVersionControl.h"
#include "ResearchSettings.h"
#include "TemplateDialog.h"
#include <QFileSystemModel>
using namespace Research;
int main(int argc,char **argv){const auto report=argument(1);argc=1;QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
QTimer::singleShot(60,&app,[&]{
  const auto folder=QFileInfo(report).absolutePath();QJsonArray checks;int failures=0;
  auto check=[&](const QString &name,bool pass){checks.append(QJsonObject{{"name",name},{"passed",pass}});failures+=!pass;put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures}}).toJson());};
  Tw::Settings().setValue("openPDFwithTeX",false);Tw::Settings().setValue("researchFocusMode",false);setBuildCacheForTests(folder+"/build-cache");
  const QStringList templates{"Basic LaTeX documents/minimal-research.tex","Basic LaTeX documents/chinese-article.tex","Beamer presentations/classic-blank.tex"};
  for(int i=0;i<templates.size();++i){
    QTimer::singleShot(160,&app,[&,i]{
      auto *d=qobject_cast<QDialog*>(QApplication::activeModalWidget());auto *tree=d?d->findChild<QTreeView*>("treeView"):nullptr;
      check(QString("template chooser %1 opens").arg(i),tree!=nullptr);if(!tree){if(d)d->reject();return;}
      auto *model=qobject_cast<QFileSystemModel*>(tree->model());const auto path=QDir(model->rootPath()).filePath(templates[i]);
      check(QString("template %1 is in library").arg(i),waitFor([&]{return model->index(path).isValid();},3000));
      const auto index=model->index(path);tree->setCurrentIndex(index);tree->scrollTo(index);settle(100);d->grab().save(folder+QString("/template-%1.png").arg(i));
      auto *open=d->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Open);check(QString("template %1 can be opened").arg(i),open&&open->isEnabled());if(open&&open->isEnabled())open->click();else d->reject();
    });
    auto *w=qobject_cast<TeXDocumentWindow*>(TWApp::instance()->newFromTemplate());check(QString("new document %1 is editable").arg(i),w&&w->untitled());if(!w)continue;
    w->resize(900,720);w->show();settle(120);const auto destination=folder+QString("/document-%1.tex").arg(i);
    QTimer::singleShot(120,&app,[&,destination]{auto *d=qobject_cast<QFileDialog*>(QApplication::activeModalWidget());check("first save uses a destination draft",d!=nullptr);if(d){d->setDirectory(QFileInfo(destination).absolutePath());d->selectFile(QFileInfo(destination).fileName());if(auto *name=d->findChild<QLineEdit*>("fileNameEdit"))name->setText(QFileInfo(destination).fileName());QMetaObject::invokeMethod(d,"accept",Qt::QueuedConnection);}});
    const bool saved=w->saveAs();put(folder+QString("/save-%1.json").arg(i),QJsonDocument(QJsonObject{{"saved",saved},{"actual",w->fileName()},{"expected",destination}}).toJson());
    check(QString("first save %1 succeeds").arg(i),saved&&QFileInfo(w->fileName())==QFileInfo(destination));
    if(!saved||QFileInfo(w->fileName())!=QFileInfo(destination))continue;
    const auto started=QDateTime::currentMSecsSinceEpoch();w->findChild<QAction*>("actionTypeset")->trigger();
    const bool built=waitFor([&]{return !w->isTypesetting()&&!w->property("researchBuildPreparing").toBool()&&!w->property("researchBuildFinishing").toBool()&&pdfPairValid(previewFile(destination));},70000);
    check(QString("first compile %1 produces matching PDF").arg(i),built);
    put(folder+QString("/compile-%1.json").arg(i),QJsonDocument(QJsonObject{{"milliseconds",double(QDateTime::currentMSecsSinceEpoch()-started)},{"pdf",previewFile(destination)}}).toJson());
    w->grab().save(folder+QString("/first-writing-%1.png").arg(i));
    if(i==0){
      openEnvironmentCheck(w);auto *generic=w->findChild<QDialog*>("researchEnvironmentDialog");check("generic tools dialog opens",generic!=nullptr);
      openEnvironmentCheck(w,"texline-missing-engine");auto *missing=w->findChild<QDialog*>("researchEnvironmentDialog");
      check("missing engine updates existing tools dialog",missing&&missing!=generic&&missing->property("missingTool").toString()=="texline-missing-engine");
      QPushButton *retry=nullptr;if(missing)for(auto *button:missing->findChildren<QPushButton*>())if(button->text()==words("重新编译","Retry compilation"))retry=button;
      check("missing engine cannot retry prematurely",retry&&!retry->isEnabled());if(missing){missing->grab().save(folder+"/missing-engine.png");key(missing,Qt::Key_Escape);settle();}
      openEnvironmentCheck(w,"pdflatex");auto *available=w->findChild<QDialog*>("researchEnvironmentDialog");retry=nullptr;if(available)for(auto *button:available->findChildren<QPushButton*>())if(button->text()==words("重新编译","Retry compilation"))retry=button;
      check("available engine offers direct retry",retry&&waitFor([&]{return retry->isEnabled();},12000));if(available){available->grab().save(folder+"/engine-recovered.png");key(available,Qt::Key_Escape);settle();}
      check("environment Esc restores manuscript focus",w->editor()->hasFocus());
    }
  }
  for(auto *w:TeXDocumentWindow::documentList())w->textDoc()->setModified(false);for(auto *w:TeXDocumentWindow::documentList())w->close();settle(300);check("first-writing review completed",true);app.exit(failures?1:0);
});return app.exec();}
