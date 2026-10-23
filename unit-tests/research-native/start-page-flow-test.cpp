// Focused native acceptance for the new blank-page workflows. Private desktop only.
#include "ActualAuthoringRecording.h"
#include "ResearchStart.h"
#include "ResearchSettings.h"
#include "ResearchWorkflow.h"
#include "PDFDocumentWindow.h"
#include "document/TeXDocument.h"

int main(int argc,char **argv){
  if(!qEnvironmentVariable("TEXLINE_TEST_DESKTOP").startsWith("TeXlineValidation-"))return 2;
  const auto report=argument(1);argc=1;QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
  QTimer::singleShot(100,&app,[&]{
    const auto folder=QFileInfo(report).absolutePath();QJsonArray checks,frames;int failures=0;
    auto save=[&]{put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"frames",frames},{"devicePixelRatio",qApp->primaryScreen()->devicePixelRatio()},{"physicalInputVerified",false}}).toJson());};
    auto check=[&](const QString &n,bool pass){checks.append(QJsonObject{{"name",n},{"passed",pass}});failures+=!pass;save();};
    auto capture=[&](QWidget *w,const QString &n){const auto image=w->grab();check(n+": native capture saved",image.save(folder+"/"+n+".png"));frames.append(QJsonObject{{"file",n+".png"},{"devicePixelRatio",image.devicePixelRatio()}});save();};
    auto button=[](TeXDocumentWindow *w,const char *id){return w->findChild<QPushButton*>(QString::fromLatin1(id));};
    auto fresh=[&]{auto *w=new TeXDocumentWindow;w->resize(qEnvironmentVariableIsSet("TEXLINE_TEST_HALF")?620:1120,720);w->show();QApplication::setActiveWindow(w);w->editor()->setFocus();settle(100);return w;};
    auto close=[](TeXDocumentWindow *w){w->setModified(false);w->close();settle(80);};
    Tw::Settings().setValue("openPDFwithTeX",false);Tw::Settings().setValue("researchStartRecentFiles",true);app.clearRecentFiles();Research::setBuildCacheForTests(folder+"/build-cache");
    auto choose=[&](TeXDocumentWindow *w,int index,const QString &name){
      bool reached=false;QTimer responder;responder.setInterval(20);QObject::connect(&responder,&QTimer::timeout,&app,[&]{
        if(auto *menu=qobject_cast<QMenu*>(QApplication::activePopupWidget())){responder.stop();reached=true;capture(menu,name+"-menu");if(index<0)key(menu,Qt::Key_Escape);else{menu->setActiveAction(menu->actions()[index]);key(menu,Qt::Key_Return);}}
      });responder.start();button(w,"researchStartNew")->click();responder.stop();settle(100);check(name+": new menu reached",reached);
    };
    auto *w=fresh();const auto windows=app.topLevelWidgets().size();choose(w,-1,"cancel-new");
    check("Escape cancels New without changing source, history, window count or focus",w->textDoc()->isEmpty()&&!w->textDoc()->isUndoAvailable()&&!w->isModified()&&w->editor()->hasFocus()&&button(w,"researchStartNew")->isVisible()&&app.topLevelWidgets().size()==windows);
    close(w);
    const auto a=folder+"/project-a/shared.tex",b=folder+"/project-b/shared.tex";const QByteArray textA("A unique file.\n"),textB("B unique file.\n");
    check("independent same-name samples created",put(a,textA)&&put(b,textB));app.clearRecentFiles();app.addToRecentFiles({{"path",a}});app.addToRecentFiles({{"path",b}});w=fresh();
    auto *r0=button(w,"researchStartRecent0"),*r1=button(w,"researchStartRecent1");
    check("same-name rows are individually identifiable",r0&&r1&&r0->isVisible()&&r1->isVisible()&&r0->accessibleName()!=r1->accessibleName()&&r0->toolTip()==QDir::toNativeSeparators(b)&&r1->toolTip()==QDir::toNativeSeparators(a));
    capture(w,"recent-same-name");if(r0)r0->click();settle(180);
    auto *opened=TeXDocumentWindow::findDocument(b);check("recent row opens its exact file through the ordinary action",opened&&opened->textDoc()->editorText().toUtf8()==textB);
    if(opened&&opened!=w)close(opened);
    close(w);
    const auto missing=folder+"/missing/no-longer-there.tex";app.clearRecentFiles();app.addToRecentFiles({{"path",missing}});w=fresh();
    bool errorShown=false;QTimer dismiss;dismiss.setInterval(20);QObject::connect(&dismiss,&QTimer::timeout,&app,[&]{if(auto *box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())){dismiss.stop();errorShown=!box->text().isEmpty();capture(box,"recent-missing-error");box->accept();}});
    dismiss.start();if(auto *r=button(w,"researchStartRecent0"))r->click();dismiss.stop();settle(80);
    check("missing recent file reports an error and preserves the blank document",errorShown&&w->textDoc()->isEmpty()&&!w->isModified()&&w->editor()->hasFocus());close(w);
    app.clearRecentFiles();app.addToRecentFiles({{"path",a}});w=fresh();
    auto preferences=[&](bool accept){QTimer responder;responder.setInterval(25);bool reached=false;QObject::connect(&responder,&QTimer::timeout,&app,[&]{if(auto *d=qobject_cast<QDialog*>(QApplication::activeModalWidget()))if(auto *c=d->findChild<QCheckBox*>("researchStartRecentFilesSetting")){responder.stop();reached=true;c->setChecked(false);if(accept)d->accept();else key(d,Qt::Key_Escape);}});responder.start();app.preferences();responder.stop();settle(80);check(accept?"visibility preferences Save reached":"visibility preferences Cancel reached",reached);};
    preferences(false);check("settings Cancel leaves recent list and saved preference intact",Research::settingValue("researchStartRecentFiles").toBool()&&button(w,"researchStartRecent0")->isVisible());
    preferences(true);check("settings Save hides recent rows without changing the file",!Research::settingValue("researchStartRecentFiles").toBool()&&!button(w,"researchStartRecent0")->isVisible()&&w->textDoc()->isEmpty());close(w);
    Tw::Settings().setValue("researchStartRecentFiles",true);app.clearRecentFiles();
    for(const auto &language:QStringList{"zh_CN","zh_TW","en"})for(int kind=0;kind<2;++kind){
      app.applyTranslation(language);w=fresh();const QString name=language+(kind?"-slides":"-document");choose(w,kind,name);
      const QString sentence=language=="en"?QStringLiteral("A clear beginning."):language=="zh_TW"?QStringLiteral("從這裡開始寫作。"):QStringLiteral("从这里开始写作。");
      w->editor()->insertPlainText(sentence);const QString source=w->textDoc()->editorText();
      const auto file=folder+"/compile/"+name+"/main.tex";QDir().mkpath(QFileInfo(file).absolutePath());bool saveDialog=false;
      QTimer saver;saver.setInterval(25);QObject::connect(&saver,&QTimer::timeout,&app,[&]{if(auto *dialog=qobject_cast<QFileDialog*>(QApplication::activeModalWidget())){dialog->setDirectory(QFileInfo(file).absolutePath());if(auto *field=dialog->findChild<QLineEdit*>("fileNameEdit")){saver.stop();saveDialog=true;field->setText(QFileInfo(file).fileName());QMetaObject::invokeMethod(dialog,"accept",Qt::QueuedConnection);}}});
      saver.start();const bool saved=w->saveAs();saver.stop();check(name+": normal Save As produces the exact new source",saved&&saveDialog&&QFileInfo(w->textDoc()->absoluteFilePath())==QFileInfo(file)&&QString::fromUtf8(bytes(file)).replace("\r\n","\n")==source);
      QMenu engine;w->populateResearchEngineMenu(&engine);bool selected=false;const auto expected=language=="en"?QStringLiteral("pdfLaTeX"):QStringLiteral("XeLaTeX");for(auto *action:engine.actions())if(action->isChecked()&&Research::canonicalEngineName(action->text())==expected)selected=true;
      check(name+": declared engine is selected",selected);auto *compile=w->findChild<QAction*>("actionTypeset");check(name+": normal Compile enabled",compile&&compile->isEnabled());if(compile)compile->trigger();
      const bool finished=waitFor([&]{return !w->isTypesetting()&&!w->property("researchBuildPreparing").toBool()&&!w->property("researchBuildFinishing").toBool()&&!w->property("researchPublishing").toBool();},120000);
      const auto pdf=Research::previewFile(file);auto doc=QtPDF::Backend::Document::newDocument(pdf);check(name+": generated document compiles into an actual PDF",finished&&Research::pdfPairValid(pdf)&&doc&&doc->isValid());check(name+": actual PDF contains inserted body",contains(doc,sentence));
      put(QFileInfo(file).absolutePath()+"/compile.txt",w->property("consoleOutput").toString().toUtf8());capture(w,name+"-source");
      if(w->pdfDocument()){w->pdfDocument()->close();settle(80);}close(w);
    }
    check("original recent-file samples remain byte-identical",bytes(a)==textA&&bytes(b)==textB);save();app.exit(failures?1:0);
  });QTimer::singleShot(240000,&app,[&]{if(auto *popup=QApplication::activePopupWidget())popup->close();if(auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget()))dialog->reject();app.exit(93);});return app.exec();
}
