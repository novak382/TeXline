#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "ResearchSlides.cpp"
#include <QElapsedTimer>
#include <QThread>
#include <QJsonArray>
static bool waitFor(const std::function<bool()> &f,int ms=15000){QElapsedTimer t;t.start();while(!f()&&t.elapsed()<ms){QCoreApplication::processEvents();QThread::msleep(3);}return f();}
static void put(const QString &p,const QString &s){QDir().mkpath(QFileInfo(p).absolutePath());QSaveFile f(p);if(f.open(QIODevice::WriteOnly)){f.write(s.toUtf8());f.commit();}}
int main(int argc,char **argv){const auto report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
 QTimer::singleShot(80,&app,[&]{const auto folder=QFileInfo(report).absolutePath();QJsonArray checks;int failures=0;auto check=[&](QString name,bool ok){checks.append(QJsonObject{{"name",name},{"passed",ok}});failures+=!ok;put(report,QString::fromUtf8(QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures}}).toJson()));};
  Tw::Settings().setValue("openPDFwithTeX",false);Tw::Settings().setValue("researchFocusMode",false);Research::setBuildCacheForTests(folder+"/cache");app.setEngineList({Engine("pdfLaTeX","C:/texlive/2023/bin/windows/pdflatex.exe",{"-synctex=1","-interaction=nonstopmode","-file-line-error","$fullname"},true)});
  for(int scenario=0;scenario<3;++scenario){const auto root=folder+QString("/case%1/main.tex").arg(scenario);const QString original="\\documentclass{beamer}\n\\begin{document}\n\\begin{frame}{A}\nBody\n\\end{frame}\n\\end{document}\n";put(root,original);auto *w=new TeXDocumentWindow(root);w->show();
   auto ready=[&]{return waitFor([&]{auto *c=Research::controller(w);return c&&c->enabled&&!c->dirty&&!c->busy;});};check("notes fixture recognized",ready());auto *c=Research::controller(w);if(!c){app.exit(2);return;}c->select(0,true);c->openNotes();auto *edit=c->notesPane->findChild<QPlainTextEdit *>("researchSlideNote0");edit->setPlainText(scenario==2?"Unbalanced {":"Draft speaker note");check("note-only draft starts with clean source",c->notesChanged&&!w->isModified());check("Save action is enabled for a note-only draft",w->findChild<QAction *>("actionSave")->isEnabled());
   if(scenario==0){check("Save All commits and persists note-only draft",w->saveAll()&&Research::read(root).contains("Draft speaker note")&&!c->notesChanged);}
   else {w->typeset();check("compile settles",waitFor([&]{return !w->isTypesetting()&&!w->property("researchBuildPreparing").toBool()&&!w->property("researchBuildFinishing").toBool()&&!w->property("researchPublishing").toBool();},90000));if(scenario==1)check("Compile includes the current note-only draft",Research::read(root).contains("Draft speaker note")&&!c->notesChanged&&Research::pdfPairValid(Research::previewFile(root)));else check("invalid note blocks compile and preserves draft",c->notesChanged&&edit->toPlainText()=="Unbalanced {"&&Research::previewFile(root).isEmpty()&&Research::read(root)==original);}
   w->grab().save(folder+QString("/notes-case%1.png").arg(scenario));if(scenario==2){c->notesPane->findChild<QAction *>("researchDiscardNotes")->trigger();check("discard notes clears Save and unsaved-window state",!c->notesChanged&&!w->isModified()&&!w->isWindowModified()&&!w->findChild<QAction *>("actionSave")->isEnabled());}c->notesChanged=false;w->setModified(false);delete w;
  }
  const auto multi=folder+"/multi/main.tex",part=folder+"/multi/part.tex";
  const QString chapter="% !TeX root = main.tex\n\\begin{frame}{Child}\nBody\n\\end{frame}\n";
  put(multi,"\\documentclass{beamer}\n\\begin{document}\n\\input{part}\n\\end{document}\n");put(part,chapter);
  auto *child=new TeXDocumentWindow(part);child->show();auto *main=new TeXDocumentWindow(multi);main->show();
  check("multifile notes fixture recognized",waitFor([&]{auto *c=Research::controller(main);return c&&c->enabled&&!c->dirty&&!c->busy;}));
  if(auto *c=Research::controller(main)){c->selected=0;c->openNotes();auto *note=c->notesPane?c->notesPane->findChild<QPlainTextEdit *>("researchSlideNote0"):nullptr;check("child note editor available",note);if(note){note->setPlainText("Multifile note draft");check("Save All writes notes to their child file",main->saveAll()&&Research::read(part).contains("Multifile note draft")&&!Research::read(multi).contains("Multifile note draft"));child->editor()->undo();check("child note save retains exact single-step undo",child->editor()->toPlainText()==chapter);}}
  child->setModified(false);main->setModified(false);delete main;delete child;app.exit(failures?1:0);
 });QTimer::singleShot(300000,&app,[&]{app.exit(92);});return app.exec();}
