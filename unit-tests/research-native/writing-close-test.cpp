// Exercise new reading/project workflows while their asynchronous work is pending.
#include "ActualAuthoringRecording.h"
#include "ResearchProject.h"
using namespace Research;
static FILE *trace=nullptr;
static LONG CALLBACK recordFault(EXCEPTION_POINTERS *info){
 if(trace&&info->ExceptionRecord->ExceptionCode==EXCEPTION_ACCESS_VIOLATION){
  fprintf(trace,"access-violation pc=%p base=%p address=%llx\n",info->ExceptionRecord->ExceptionAddress,GetModuleHandleW(nullptr),static_cast<unsigned long long>(info->ExceptionRecord->ExceptionInformation[1]));
  void *frames[64];const auto count=CaptureStackBackTrace(0,64,frames,nullptr);for(USHORT i=0;i<count;++i)fprintf(trace,"%p\n",frames[i]);fflush(trace);
 }
 return EXCEPTION_CONTINUE_SEARCH;
}
int main(int argc,char **argv){
 const auto report=argument(1),folder=QFileInfo(report).absolutePath(),tracePath=folder+"/exit-trace.txt";
 trace=_wfopen(reinterpret_cast<const wchar_t*>(tracePath.utf16()),L"w");AddVectoredExceptionHandler(1,recordFault);
 argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
 QTimer::singleShot(60,&app,[&]{
  QJsonArray checks;int failures=0;
  auto check=[&](const QString &name,bool pass){checks.append(QJsonObject{{"name",name},{"passed",pass}});failures+=!pass;put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"devicePixelRatio",qApp->primaryScreen()->devicePixelRatio()}}).toJson());};
  Tw::Settings().setValue("openPDFwithTeX",false);Tw::Settings().setValue("researchFocusMode",false);
  for(int repetition=0;repetition<8;++repetition)for(int mode=0;mode<4;++mode){
   const auto project=folder+QString("/sample-%1-%2").arg(repetition).arg(mode),root=project+"/main.tex",child=project+"/method.tex";
   put(root,"\\documentclass{article}\n\\begin{document}\n\\section{Writing}\nText \\cite{paper}.\n\\input{method}\n\\bibliography{refs}\n\\end{document}\n");
   put(child,"\\begin{equation}\nx=y+z\\label{eq:a}\n\\end{equation}\n");put(project+"/refs.bib","@article{paper,title={Evidence},author={A. Author},year={2024}}\n");
   auto *w=new TeXDocumentWindow(root);w->resize(700,650);w->show();
   check(QString("%1/%2 source ready").arg(repetition).arg(mode),waitFor([&]{return w->property("researchArticleReady").toBool();}));
   if(trace){fprintf(trace,"case %d/%d\n",repetition,mode);fflush(trace);}
   if(mode==0)w->findChild<QAction*>("researchProjectFilesAction")->trigger();
   else if(mode==2)pinSourceObject(w,child,0);
   else {
    auto c=w->textCursor();c.setPosition(w->textDoc()->editorText().indexOf("\\cite")+8);w->editor()->setTextCursor(c);w->findChild<QAction*>("researchInsertCitation")->trigger();
    if(mode==3){auto *dialog=w->findChild<QDialog*>("researchCitationDialog");auto *list=dialog?dialog->findChild<QListWidget*>("researchCitationList"):nullptr;
     check("citation ready for immediate edit/return/close",list&&waitFor([&]{return list->count()==1;}));
     if(dialog&&list&&list->count()){dialog->findChild<QToolButton*>("researchCitationEditSource")->click();if(auto *bib=TeXDocumentWindow::findDocument(project+"/refs.bib"))bib->findChild<QAction*>("researchNavigateBack")->trigger();}
    }
   }
   QList<QPointer<TeXDocumentWindow>> closing;for(auto *source:TeXDocumentWindow::documentList()){source->textDoc()->setModified(false);closing.append(source);}
   for(auto source:closing)if(source)source->close();
   check(QString("%1/%2 pending workflow closes without retaining source windows").arg(repetition).arg(mode),waitFor([&]{return std::all_of(closing.begin(),closing.end(),[](auto source){return source.isNull();});},4000));
  }
  settle(500);app.exit(failures?1:0);
 });return app.exec();
}
