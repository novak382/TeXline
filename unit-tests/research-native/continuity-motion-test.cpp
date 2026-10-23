// Native event costs and language-entry keyboard checks. No physical-input claim.
#include "ActualAuthoringRecording.h"
#include "PrefsDialog.h"
#include "ResearchMotion.h"

class MeasuredApp : public TWApp {
public:
  using TWApp::TWApp;
  bool measuring{false}; int depth{0}; QJsonArray events,phases;
  bool notify(QObject *receiver,QEvent *event) override {
    if(!measuring)return TWApp::notify(receiver,event);
    const bool outer=depth++==0;QElapsedTimer clock;clock.start();
    const bool result=TWApp::notify(receiver,event);--depth;
    const double ms=double(clock.nsecsElapsed())/1e6;
    if(outer)events.append(QJsonObject{{"type",int(event->type())},{"ms",ms}});
    if(event->type()==QEvent::Paint||event->type()==QEvent::Resize||event->type()==QEvent::LayoutRequest)phases.append(QJsonObject{{"type",int(event->type())},{"ms",ms}});
    return result;
  }
};
int main(int argc,char **argv){
  if(!qEnvironmentVariable("TEXLINE_TEST_DESKTOP").startsWith("TeXlineValidation-"))return 2;
  const auto report=argument(1);argc=1;MeasuredApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
  QTimer::singleShot(100,&app,[&]{
    QJsonArray checks,samples;int failures=0;const auto folder=QFileInfo(report).absolutePath();
    auto check=[&](QString name,bool pass){checks.append(QJsonObject{{"name",name},{"passed",pass}});failures+=!pass;};
    Tw::Settings().setValue("openPDFwithTeX",false);Tw::Settings().setValue("researchFocusMode",false);Tw::Settings().setValue("researchReduceMotion",false);
    const auto file=qEnvironmentVariable("TEXLINE_MOTION_PERF_ROOT")+"/continuity.tex";
    QString source="\\documentclass{article}\n\\begin{document}\n";
    for(int section=0;section<160;++section){source+="\\section{Transport and diffusion "+QString::number(section)+"}\n";for(int line=0;line<65;++line)source+="The concentration evolves under a conserved flux. We compare the boundary conditions with the stationary solution and retain the spatial discretization in the error estimate.\n";}
    source+="\\end{document}\n";check("independent manuscript saved",put(file,source.toUtf8()));
    auto *w=new TeXDocumentWindow(file);w->resize(qEnvironmentVariableIsSet("TEXLINE_TEST_HALF")?520:1160,760);w->show();
    check("long article ready",waitFor([&]{return w->property("researchArticleReady").toBool();}));settle(700);
    auto cursor=w->textCursor();cursor.setPosition(source.size()/2);cursor.setPosition(cursor.position()+12,QTextCursor::KeepAnchor);w->editor()->setTextCursor(cursor);w->editor()->ensureCursorVisible();settle(500);
    auto measure=[&](QString mode,int iteration,const std::function<void()> &action,QWidget *surface,const QStringList &phases){
      QJsonArray gaps;QElapsedTimer clock;QTimer beat;double previous=0;
      beat.setTimerType(Qt::PreciseTimer);beat.setInterval(4);QObject::connect(&beat,&QTimer::timeout,&app,[&]{const double now=double(clock.nsecsElapsed())/1e6;gaps.append(now-previous);previous=now;});
      app.events={};app.phases={};clock.start();app.measuring=true;beat.start();QElapsedTimer call;call.start();action();const double callMs=double(call.nsecsElapsed())/1e6;settle(215);beat.stop();app.measuring=false;
      QJsonObject costs;for(const auto &name:phases)if(surface->property(name.toUtf8()).isValid())costs[name]=surface->property(name.toUtf8()).toDouble();
      QJsonObject row{{"mode",mode},{"iteration",iteration},{"callMs",callMs},{"heartbeatGapMs",gaps},{"events",app.events},{"phaseEvents",app.phases},{"phases",costs},{"dpr",w->devicePixelRatioF()},{"width",w->width()}};
      samples.append(row);
    };
    auto *outline=w->findChild<QAction*>("researchOutlineAction");
    check("outline command exists",outline);
    if(outline)for(int i=0;i<12;++i){settle(250);measure("long-panel",i,[&]{outline->trigger();},w,{"captures-before","anchor-before","visibility-and-resize","layouts","anchor-after","captures-after","done"});}
    if(outline&&!outline->isChecked()){outline->trigger();settle(400);}
    for(int i=0;i<12;++i){settle(250);measure("long-focus-open",i,[&]{Research::setFocusMode(i%2==0);},w,{"focus-capture","focus-apply","focus-layout","focus-anchor","focus-done"});}
    check("transitions preserve the selected source",w->textCursor().selectedText()==cursor.selectedText()&&w->textDoc()->editorText()==source);
    QTimer::singleShot(80,&app,[&]{
      auto *d=qobject_cast<QDialog*>(QApplication::activeModalWidget());check("preferences opens",d);if(!d)return;
      d->resize(qEnvironmentVariableIsSet("TEXLINE_TEST_HALF")?520:880,650);settle(300);auto *tabs=d->findChild<QTabWidget*>("tabWidget");
      for(int i=0;i<16;++i){const int index=i%2?0:1;auto *area=qobject_cast<QScrollArea*>(tabs->widget(index));measure("settings",i,[&]{tabs->setCurrentIndex(index);},area->viewport(),{"arrival-capture"});}
      d->grab().save(folder+"/settings.png");key(d,Qt::Key_Escape);
    });PrefsDialog::doPrefsDialog(w,0);settle(300);
    for(int i=0;i<12;++i){
      QPointer<QDialog> draft;
      measure("dialog",i,[&]{w->findChild<QAction*>("researchSaveSnippet")->trigger();QCoreApplication::processEvents();for(auto *d:w->findChildren<QDialog*>())if(d->isVisible()&&d->findChild<QLineEdit*>("completionSnippetPrefix"))draft=d;},w,{"snippet-controls","snippet-root","snippet-before-trial","snippet-after-trial","snippet-before-context","snippet-after-context","snippet-done"});
      check("snippet draft opens",draft);
      if(draft){auto row=samples.last().toObject();row["captureMs"]=draft->property("arrival-capture").toDouble();samples.removeLast();samples.append(row);if(i==0)draft->grab().save(folder+"/dialog.png");key(draft,Qt::Key_Escape);settle(250);}
    }
    check("cancelled tools preserve document and selection",w->textDoc()->editorText()==source&&w->textCursor().selectedText()==cursor.selectedText());
    QApplication::setActiveWindow(w);w->editor()->setFocus();settle(50);check("editor owns focus before opening languages",w->editor()->hasFocus());
    Research::showOnboarding(w);settle(200);QPointer<QDialog> intro;
    for(auto *top:QApplication::topLevelWidgets())if(top->objectName()=="researchOnboarding"&&top->isVisible())intro=qobject_cast<QDialog*>(top);
    check("language opener shown",intro);
    if(intro){
      auto *cn=intro->findChild<QPushButton*>("onboardingChinese"),*tw=intro->findChild<QPushButton*>("onboardingTraditional"),*en=intro->findChild<QPushButton*>("onboardingEnglish");
      QApplication::setActiveWindow(intro);cn->setFocus();key(cn,Qt::Key_Right);check("Right moves language focus",tw->hasFocus());
      tw->setFocus();key(tw,Qt::Key_Return);check("Enter selects focused Traditional Chinese",Tw::Settings().value("locale").toString()=="zh_TW"&&intro->property("onboardingPageIndex").toInt()==0);
      intro->findChild<QPushButton*>("onboardingBack")->click();settle();check("returning to language choices restores selected-language focus",tw->hasFocus());
      en->setFocus();key(en,Qt::Key_Left);check("Left moves language focus",tw->hasFocus());
      cn->setFocus();key(cn,Qt::Key_Tab);check("Tab reaches next language",tw->hasFocus());
      en->setFocus();key(en,Qt::Key_Space);check("Space selects English",Tw::Settings().value("locale").toString()=="en_US"&&intro->property("onboardingPageIndex").toInt()==0);
      intro->findChild<QPushButton*>("onboardingBack")->click();settle();en->setFocus();key(en,Qt::Key_Enter,Qt::KeypadModifier);check("keypad Enter selects the focused language",intro->property("onboardingPageIndex").toInt()==0);
      intro->findChild<QPushButton*>("onboardingBack")->click();settle();intro->grab().save(folder+"/language.png");
      check("three choices have equal width and complete labels",qAbs(cn->width()-tw->width())<=1&&qAbs(tw->width()-en->width())<=1&&cn->width()>cn->fontMetrics().horizontalAdvance(cn->text())+20);
      if(qEnvironmentVariableIsSet("TEXLINE_TEST_HALF"))check("language opener fits narrow writing window",intro->width()<=w->width());
      key(intro,Qt::Key_Escape);settle();check("Escape closes opener and restores editor focus",!intro&&w->editor()->hasFocus());
    }
    put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"samples",samples},{"devicePixelRatio",w->devicePixelRatioF()},{"captureDuringTiming",false},{"physicalInputVerified",false},{"eventCosts","events: outermost dispatch; phaseEvents: inclusive Paint=12, Resize=14, LayoutRequest=76 costs. Nested costs overlap outer events; do not sum them together."}}).toJson());w->setModified(false);delete w;app.exit(failures?1:0);
  });QTimer::singleShot(180000,&app,[&]{app.exit(93);});return app.exec();
}
