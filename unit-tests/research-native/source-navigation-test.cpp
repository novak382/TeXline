#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "ResearchSlides.cpp"
#include <QJsonArray>
#include <QElapsedTimer>
#include <QThread>
static bool waitFor(const std::function<bool()> &f,int ms=20000){QElapsedTimer t;t.start();while(!f()&&t.elapsed()<ms){QCoreApplication::processEvents();QThread::msleep(3);}return f();}
int main(int argc,char **argv){const QString report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);QTimer::singleShot(80,&app,[&]{
 QJsonArray checks;int failures=0;auto check=[&](QString name,bool ok){checks.append(QJsonObject{{"name",name},{"passed",ok}});failures+=!ok;QFile out(report);if(out.open(QIODevice::WriteOnly))out.write(QJsonDocument(QJsonObject{{"devicePixelRatio",qApp->primaryScreen()->devicePixelRatio()},{"checks",checks},{"failures",failures}}).toJson());};
 const auto folder=QFileInfo(report).absolutePath(),root=folder+"/main.tex";
 const QString original="\\documentclass{beamer}\n\\title{Talk}\n\\begin{document}\n\\begin{frame}{First}\nAlpha\n\\end{frame}\n\\begin{frame}{Second}\nBeta\n\\end{frame}\n\\input{chapter}\n% End matter\n\\end{document}\n% After document\n";
 {QFile f(root);f.open(QIODevice::WriteOnly);f.write(original.toUtf8());}
 const auto child=folder+"/chapter.tex";{QFile f(child);f.open(QIODevice::WriteOnly);f.write("% Child header\n\\begin{frame}{Child A}\nOne\n\\end{frame}\n\\begin{frame}{Child B}\nTwo\n\\end{frame}\n% Child tail\n");}
 Tw::Settings().setValue("openPDFwithTeX",false);auto *w=new TeXDocumentWindow(root);w->resize(1440,860);w->show();auto ready=[&]{QCoreApplication::processEvents();return waitFor([&]{auto *c=Research::controller(w);return c&&!c->dirty&&!c->busy&&c->deck.sources.value(Research::path(root))==w->textDoc()->editorText();});};
 check("deck ready",ready());auto *c=Research::controller(w);if(!c){app.exit(1);return;}
 auto visible=[&]{QString s;for(auto b=w->textDoc()->begin();b.isValid();b=b.next())if(b.isVisible())s+=b.text()+"\n";return s;};
 auto click=[&](const char *id){auto *menu=w->findChild<QMenu *>("researchSlidePanelMenu");menu->aboutToShow();auto *entry=menu->findChild<QAction *>(QString::fromLatin1(id)+"MenuAction");check(QString(id)+" reachable in menu",entry&&entry->isEnabled()&&entry->isVisible());if(entry)entry->trigger();};
 click("researchSourceFull");check("full source exposes preamble both slides and trailing text",visible().contains("\\title{Talk}")&&visible().contains("Second")&&visible().contains("After document"));
 check("return caption fits",c->fullSourceButton->fontMetrics().horizontalAdvance(c->fullSourceButton->text())+16<=c->fullSourceButton->width());
 c->list->setCurrentRow(1);check("slide navigation keeps full editing",c->fullSourceSelected&&visible().contains("First")&&visible().contains("\\title"));
 auto cursor=w->textCursor();cursor.setPosition(0);w->editor()->setTextCursor(cursor);w->editor()->insertPlainText("% Edit preamble\n");check("preamble typing never collapses full source",ready()&&c->fullSourceSelected&&visible().contains("After document"));w->editor()->undo();check("full edit undo exact",ready()&&w->textDoc()->editorText()==original);
 click("researchSourcePreamble");check("preamble opens directly",c->inPreamble&&!visible().contains("Alpha")&&visible().contains("\\title"));
 click("researchSourceFull");cursor=w->textCursor();cursor.movePosition(QTextCursor::End);w->editor()->setTextCursor(cursor);check("end opens full source and reaches actual file end",c->fullSourceSelected&&w->cursorPosition()==w->textDoc()->characterCount()-1&&visible().contains("After document"));
 w->editor()->insertPlainText("% Tail edit\n");check("tail edit remains visible after parsing",ready()&&visible().contains("Tail edit")&&visible().contains("\\title"));w->editor()->undo();check("tail undo exact",ready()&&w->textDoc()->editorText()==original);
 click("researchSourceFull");check("explicit single restores focused slide",!c->fullSourceSelected&&!c->inPreamble&&visible().count("\\begin{frame}")==1&&!visible().contains("After document"));
 check("source navigation stays in the overflow menu",c->scopeBar->isHidden()&&w->findChild<QAction *>("researchSourceFullMenuAction")&&w->findChild<QAction *>("researchSourcePreambleMenuAction"));
 w->grab().save(folder+"/single.png");click("researchSourceFull");w->grab().save(folder+"/full.png");
 ready();c->list->setCurrentRow(2);const bool dirtyAfterSelect=c->dirty;TeXDocumentWindow *childWindow=nullptr;const bool childReady=waitFor([&]{childWindow=TeXDocumentWindow::findDocument(child);auto *cc=childWindow?Research::controller(childWindow):nullptr;return cc&&!cc->busy&&!cc->dirty&&cc->fullSourceSelected;});
 QJsonObject trace{{"rootFull",c->fullSourceSelected},{"rootDirty",c->dirty},{"rootBusy",c->busy},{"dirtyAfterSelect",dirtyAfterSelect},{"selected",c->selected},{"childOpened",bool(childWindow)}};
 if(childWindow){auto *cc=Research::controller(childWindow);trace.insert("controller",bool(cc));if(cc){trace.insert("full",cc->fullSourceSelected);trace.insert("busy",cc->busy);trace.insert("dirty",cc->dirty);trace.insert("enabled",cc->enabled);trace.insert("root",cc->root);}trace.insert("firstVisible",childWindow->textDoc()->firstBlock().isVisible());trace.insert("lastVisible",childWindow->textDoc()->lastBlock().isVisible());}
 {QFile f(folder+"/cross-file-state.json");if(f.open(QIODevice::WriteOnly))f.write(QJsonDocument(trace).toJson());}
 check("full view follows cross-file slide navigation",childReady&&childWindow->textDoc()->firstBlock().isVisible()&&childWindow->textDoc()->lastBlock().isVisible());
 check("cross-file navigation lands at the selected frame",childWindow&&childWindow->textCursor().blockNumber()+1==c->deck.frames[2].line);
 if(childWindow){auto *cc=Research::controller(childWindow);cc->list->setCurrentRow(0);check("return to root keeps full source",ready()&&c->fullSourceSelected&&visible().contains("After document"));childWindow->setModified(false);}
 w->findChild<QAction *>("researchSlideModeAction")->setChecked(false);check("exit restores full document",visible().contains("After document")&&visible().contains("\\title"));check("navigation never changes disk",Research::read(root)==original);w->setModified(false);app.exit(failures?1:0);
 });QTimer::singleShot(120000,&app,[&]{app.exit(92);});return app.exec();}
