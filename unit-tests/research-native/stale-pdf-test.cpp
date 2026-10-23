#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "ActualAuthoringRecording.h"
#include "ResearchReferences.cpp"
#include "ResearchSourceMap.h"
#include "ResearchWorkflow.h"
#include "PDFDocumentWindow.h"
#include "TWApp.h"
#include "Settings.h"
#include <QElapsedTimer>
#include <QSaveFile>
#include <QThread>
#include <QGraphicsPathItem>
#include <QGraphicsPathItem>
#include <windows.h>
static bool put(const QString &file,const QString &text){QSaveFile f(file);return f.open(QIODevice::WriteOnly)&&f.write(text.toUtf8())==text.toUtf8().size()&&f.commit();}
int main(int argc,char **argv){const QString report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);QTimer::singleShot(0,&app,[&]{
 using namespace Research;QJsonArray checks;int failures=0;auto check=[&](const QString &name,bool ok){checks.append(QJsonObject{{"name",name},{"passed",ok}});if(!ok)++failures;};
 auto active=OpenInputDesktop(0,FALSE,DESKTOP_READOBJECTS);wchar_t name[256]={};DWORD length=0;if(active){GetUserObjectInformationW(active,UOI_NAME,name,sizeof(name),&length);CloseDesktop(active);}check("runs on an inactive private desktop",!qEnvironmentVariable("TEXLINE_TEST_DESKTOP").isEmpty()&&QString::fromWCharArray(name)!=qEnvironmentVariable("TEXLINE_TEST_DESKTOP"));if(failures){app.exit(1);return;}
 const QString folder=QFileInfo(report).absolutePath()+"/paper";QDir().mkpath(folder);const QString root=folder+"/main.tex",child=folder+"/results.tex";
 const QString original="\\documentclass{article}\n\\usepackage{xcolor,amsmath,hyperref}\n\\begin{document}\n{\\color{blue}\nFirst blue paragraph with a visible beginning.\n\n"+QString("A large blue paragraph remains available for navigation. ").repeated(140)+"\n\nLast blue boundary marker.}\n\\input{results}\n\\end{document}\n";
 const QString chapter="\\section{Results}\n\\begin{equation}\\label{eq:stable}\nx^2+y^2=z^2\n\\end{equation}\nA reference to \\eqref{eq:stable}.\n\\[\na+b=c\n\\]\n";
 check("independent long colour and multi-file formula fixtures",put(root,original)&&put(child,chapter));Tw::Settings().setValue("openPDFwithTeX",true);auto *w=new TeXDocumentWindow(root);w->resize(1000,720);w->show();
 auto build=[&]{w->typeset();return waitFor([&]{return !w->isTypesetting()&&!w->property("researchBuildPreparing").toBool()&&!w->property("researchBuildFinishing").toBool()&&!w->property("researchPublishing").toBool();},45000);};
 check("actual LaTeX compile completes",build());settle(400);const QString pdf=previewFile(root);check("compiled PDF and SyncTeX remain certified",pdfPairValid(pdf)&&pdfCompiledPairValid(pdf));
 auto compiled=pdfCompiledSources(pdf);check("compiled source snapshots include the real child file",compiled.value(path(root))==original&&compiled.value(path(child))==chapter);
 auto doc=QtPDF::Backend::Document::newDocument(pdf);PreviewSync sync(pdf,root);check("actual multi-page PDF is readable",doc&&doc->numPages()>1&&sync.isValid());
 auto *viewer=PDFDocumentWindow::findDocument(pdf);if(!viewer)viewer=new PDFDocumentWindow(pdf,w);viewer->show();check("real PDF viewer has synchronization",viewer&&viewer->hasSyncData());
 const int oldLastLine=original.left(original.indexOf("Last blue boundary")).count('\n')+1;ReferenceBlock boundary;boundary.file=path(root);boundary.firstLine=boundary.lastLine=oldLastLine;auto hits=sync.fromTeX(boundary,oldLastLine,-1);check("compiled boundary is found on the PDF",!hits.isEmpty()&&!hits.first().isEmpty());
 QTextCursor edit(w->textDoc());const int prefix=original.indexOf("{\\color{blue}");edit.setPosition(prefix);edit.setPosition(prefix+QString("{\\color{blue}\n").size(),QTextCursor::KeepAnchor);edit.removeSelectedText();
 check("unsaved colour deletion makes a previous but intact compiled pair",!pdfPairValid(pdf)&&pdfCompiledPairValid(pdf));
 if(viewer&&!hits.isEmpty()&&!hits.first().isEmpty()) {const int page=hits.firstKey();const auto p=doc->page(page).toStrongRef();const auto pos=hits.first().first().center();const QPointF point(pos.x(),p->pageSizeF().height()-pos.y());QMetaObject::invokeMethod(viewer,"syncClick",Qt::DirectConnection,Q_ARG(PDFDocumentWindow::size_type,page),Q_ARG(QPointF,point));settle(450);}
 check("PDF inverse sync still reaches the closing colour boundary after deleting its opening command",w->textCursor().block().text().contains("Last blue boundary marker.}"));
 const auto current=w->textDoc()->editorText();check("source map retains the shifted closing brace",mapSourcePosition(original,current,original.indexOf("marker.}")+7)==current.indexOf("marker.}")+7);
 check("saving the partially edited source is possible without compiling",w->save());settle(250);check("saved edits retain the immutable compilation source",pdfCompiledSources(pdf).value(path(root))==original&&pdfCompiledPairValid(pdf)&&!pdfPairValid(pdf));
 auto *c=TeXDocumentWindow::openDocument(child,false,false);QTextCursor change(c->textDoc());change.setPosition(0);change.insertText("% new draft line\n% another draft line\n");change.setPosition(c->textDoc()->editorText().indexOf("x^2"));change.setPosition(change.position()+3,QTextCursor::KeepAnchor);change.insertText("x^3");
 Request request;request.root=root;request.pdf=pdf;request.buffers={{path(root),w->textDoc()->editorText()},{path(child),c->textDoc()->editorText()}};
 auto index=referenceIndex(root,compiled);readNumbers(index,auxFile(root,pdf),pdfCompiledAux(pdf));ReferenceDefinition equation;for(const auto &d:index.definitions)if(d.key=="eq:stable")equation=d;int page=-1;const auto box=referenceRect(sync,index,equation,doc,page);check("compiled formula has a real PDF region",page>=0&&!box.isEmpty());
 request.body=true;request.page=page;request.point=box.center();const auto hover=work(request);check("PDF formula hover survives preceding and in-formula draft edits",hover.visual()&&hover.previous&&hover.target.block.text.contains("x^2")&&!hover.target.block.text.contains("x^3"));
 request.body=false;request.key="eq:stable";const auto fromSource=work(request);check("source reference hover uses the compiled formula with an explicit previous marker",fromSource.visual()&&fromSource.previous&&fromSource.target.block.text.contains("x^3"));
 if(viewer&&page>=0){const auto p=doc->page(page).toStrongRef();const QPointF point(box.center().x(),p->pageSizeF().height()-box.center().y());QMetaObject::invokeMethod(viewer,"syncClick",Qt::DirectConnection,Q_ARG(PDFDocumentWindow::size_type,page),Q_ARG(QPointF,point));settle(450);}
 check("inverse sync maps into the changed child rather than its obsolete line",c->textCursor().block().text().contains("x^3")||c->textCursor().block().text().contains("equation"));
 const int targetLine=c->textDoc()->editorText().left(c->textDoc()->editorText().indexOf("x^3")).count('\n')+1;if(viewer){viewer->clearSyncHighlight();viewer->syncFromSource(child,targetLine,2,false);}const auto sceneItems=viewer&&viewer->widget()->scene()?viewer->widget()->scene()->items():QList<QGraphicsItem*>();check("forward synchronization remains available for the edited child",std::any_of(sceneItems.cbegin(),sceneItems.cend(),[](auto *item){return item->type()==QGraphicsPathItem::Type;}));

 if(viewer&&hover.visual()){
  c->setGeometry(60,60,640,760);viewer->setGeometry(700,60,640,760);c->show();viewer->show();viewer->widget()->goToPage(page,Qt::AlignCenter);viewer->widget()->zoomFitWindow();settle(500);
  Recording recording;recording.directory=QFileInfo(report).absolutePath()+"/stale-pdf-flow";QDir().mkpath(recording.directory);recording.windows={c,viewer};recording.canvas=QRect(c->mapToGlobal(QPoint()),QSize(1280,760));recording.pointer=recording.canvas.topLeft()+QPoint(60,80);recording.elapsed.start();recording.capture();recording.timer.start();recording.action("The live formula has changed; the previous PDF stays available");settle(600);
  QPoint point;bool located=false;auto *view=viewer->widget();
  for(auto *item:view->items())if(auto *pi=qgraphicsitem_cast<QtPDF::PDFPageGraphicsItem *>(item)){if(pi->pageNum()!=page)continue;const auto size=doc->page(page).toStrongRef()->pageSizeF();point=view->mapFromScene(pi->mapToScene(pi->mapFromPage(QPointF(box.center().x(),size.height()-box.center().y()))));located=view->viewport()->rect().contains(point);break;}
  if(located){recording.action("Hover still shows the formula from the actual previous PDF");recording.aim(view->viewport(),point);check("actual PDF mouse hover shows a previous-revision card",waitFor([&]{auto *card=viewer->findChild<QFrame*>("researchReferenceCard");return card&&card->findChild<QLabel*>("researchReferenceRevision");},8000));settle(1000);}
  else check("actual PDF hover target is visible",false);
  recording.finish();check("previous PDF recording completes",!recording.failures);
 }
 check("child save does not discard the compiled hover",c->save()&&work(request).visual());
 check("unmatched source braces may fail a new compile without destroying the last valid PDF",build()&&pdfCompiledPairValid(pdf)&&!pdfPairValid(pdf)&&work(request).visual());
 w->grab().save(QFileInfo(report).absolutePath()+"/edited-source.png");if(viewer)viewer->grab().save(QFileInfo(report).absolutePath()+"/previous-pdf.png");
 // A corrupted pair is a different condition from an edited source.
 const QString syncFile=pdf.left(pdf.size()-4)+pdfMetadata(pdf).value("syncSuffix").toString();QFile f(syncFile);f.open(QIODevice::ReadOnly);const auto saved=f.readAll();f.close();{QFile corrupt(syncFile);corrupt.open(QIODevice::WriteOnly);corrupt.write("mismatched sync data");}check("mismatched SyncTeX is still rejected",!pdfCompiledPairValid(pdf)&&!work(request).visual());{QFile restore(syncFile);restore.open(QIODevice::WriteOnly);restore.write(saved);}
 QTextCursor childReset(c->textDoc());childReset.select(QTextCursor::Document);childReset.insertText(chapter);c->save();QTextCursor reset(w->textDoc());reset.select(QTextCursor::Document);reset.insertText(original);w->save();check("successful recompile updates certification and snapshots",build()&&pdfPairValid(pdf)&&pdfCompiledSources(pdf).value(path(child))==chapter);
 check("mapping handles repeated edits Unicode undo and inserted paragraphs",mapSourcePosition("α😀\nA\nclosing}","new\nα😀\nA edited\nclosing}",QString("α😀\nA\nclosing").size())==QString("new\nα😀\nA edited\nclosing").size());
 for(auto *v:TeXDocumentWindow::documentList())v->setModified(false);QSaveFile out(report);if(out.open(QIODevice::WriteOnly)){out.write(QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"physicalInputVerified",false}}).toJson());out.commit();}app.exit(failures?1:0);
 });return app.exec();}
