#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "TeXDocumentWindow.h"
#include "PDFDocumentWindow.h"
#include "Settings.h"
#include <QDir>
#include <QFile>
#include <QTimer>
#include <QDebug>
#include <csignal>
#include <exception>
#include <windows.h>
static HANDLE logFile;
static void log(const char *s) { DWORD bytes; WriteFile(logFile,s,DWORD(strlen(s)),&bytes,nullptr); FlushFileBuffers(logFile); }
static void stack() { void *frames[32]; const auto n=CaptureStackBackTrace(0,32,frames,nullptr); char line[80]; for(USHORT i=0;i<n;++i){snprintf(line,sizeof(line),"frame %p module-offset %llx\n",frames[i],(unsigned long long)((uintptr_t)frames[i]-(uintptr_t)GetModuleHandleW(nullptr)));log(line);} }
int main(int argc,char **argv) {
  const QString folder=QString::fromLocal8Bit(argv[1]),pdf=QString::fromLocal8Bit(argv[2]);
  QDir().mkpath(folder); const QString diag=folder+"/diagnostic.txt";
  logFile=CreateFileW(reinterpret_cast<LPCWSTR>(diag.utf16()),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
  std::set_terminate([]{log("TERMINATE\n");try{if(auto e=std::current_exception())std::rethrow_exception(e);}catch(const std::exception &e){log(e.what());log("\n");}catch(...){log("non-std exception\n");}stack();std::_Exit(111);});
  std::signal(SIGABRT,[](int){log("ABORT\n");stack();std::_Exit(112);});
  qInstallMessageHandler([](QtMsgType,const QMessageLogContext &,const QString &text){log(text.toUtf8().constData());log("\n");});
  argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
  QTimer::singleShot(100,&app,[&]{
    const QString path=folder+"/main.tex";
    QFile file(path);file.open(QIODevice::WriteOnly);file.write("\\documentclass{article}\n\\begin{document}Probe.\\end{document}\n");file.close();
    QFile::copy(pdf,folder+"/main.pdf");Tw::Settings().setValue("openPDFwithTeX",false);Tw::Settings().setValue("researchFocusMode",false);
    log("create source\n");auto *source=new TeXDocumentWindow(path);source->show();
    log("create PDF\n");auto *viewer=new PDFDocumentWindow(folder+"/main.pdf",source);viewer->show();
    log("created PDF\n");QTimer::singleShot(1200,&app,[&]{log("finished\n");app.exit(0);});
  });return app.exec();
}
