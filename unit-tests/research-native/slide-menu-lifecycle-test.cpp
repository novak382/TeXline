// Repeated model refreshes must not retain obsolete menu widgets.
#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "ResearchSlides.cpp"
#include <QJsonArray>
#include <QThread>
#include <windows.h>
#include <psapi.h>
#include <shellapi.h>
static bool await(const std::function<bool()> &f,int ms=20000){QElapsedTimer t;t.start();while(!f()&&t.elapsed()<ms){QCoreApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QThread::msleep(1);}return f();}
static void settle(int ms=15){QElapsedTimer t;t.start();await([&]{return t.elapsed()>=ms;},ms+1000);}
static void put(const QString &p,const QByteArray &v){QDir().mkpath(QFileInfo(p).absolutePath());QSaveFile f(p);if(f.open(QIODevice::WriteOnly)&&f.write(v)==v.size())f.commit();}
static qint64 memory(){PROCESS_MEMORY_COUNTERS_EX s{};s.cb=sizeof(s);using F=BOOL(WINAPI*)(HANDLE,PPROCESS_MEMORY_COUNTERS,DWORD);auto f=reinterpret_cast<F>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"K32GetProcessMemoryInfo"));return f&&f(GetCurrentProcess(),reinterpret_cast<PPROCESS_MEMORY_COUNTERS>(&s),sizeof(s))?qint64(s.PrivateUsage):0;}
int main(int argc,char **argv){if(!qEnvironmentVariable("TEXLINE_TEST_DESKTOP").startsWith("TeXlineValidation-"))return 2;int n=0;auto **args=CommandLineToArgvW(GetCommandLineW(),&n);const QString report=QString::fromWCharArray(args[1]);LocalFree(args);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
 QTimer::singleShot(80,&app,[&]{const QString folder=QFileInfo(report).absolutePath(),path=folder+"/project/talk.tex";QString source="\\documentclass{beamer}\n\\begin{document}\n";for(int i=0;i<24;++i)source+="\\begin{frame}{Conserved flux "+QString::number(i)+"}\nThe normal flux vanishes at the boundary.\n\\end{frame}\n";source+="\\end{document}\n";put(path,source.toUtf8());QJsonArray checks,resources;int failures=0;auto save=[&]{put(report,QJsonDocument(QJsonObject{{"checks",checks},{"resources",resources},{"failures",failures}}).toJson());};auto check=[&](QString name,bool good){failures+=!good;checks.append(QJsonObject{{"name",name},{"passed",good}});save();};Tw::Settings().setValue("openPDFwithTeX",false);Tw::Settings().setValue("researchFocusMode",false);auto *w=new TeXDocumentWindow(path);w->resize(1100,760);w->show();check("deck ready",await([&]{auto *c=Research::controller(w);return c&&c->enabled&&!c->busy&&!c->dirty&&c->deck.sources.value(Research::path(path))==source;}));auto *c=Research::controller(w);if(!c){w->setModified(false);delete w;app.exit(1);return;}
  c->accept(c->deck,{});settle(100);const int initialMenus=w->findChildren<QMenu*>().size(),initialObjects=w->findChildren<QObject*>().size();const auto before=memory();
  for(int i=0;i<100;++i){c->accept(c->deck,{});settle();if(i%10==0){DWORD handles=0;GetProcessHandleCount(GetCurrentProcess(),&handles);resources.append(QJsonObject{{"refresh",i+1},{"menus",w->findChildren<QMenu*>().size()},{"objects",w->findChildren<QObject*>().size()},{"privateBytes",double(memory())},{"kernelHandles",int(handles)},{"gdi",int(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS))},{"user",int(GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS))}});save();}}
  check("unchanged model refresh retains a bounded number of menus",w->findChildren<QMenu*>().size()<=initialMenus+2);check("obsolete menu actions do not accumulate",w->findChildren<QObject*>().size()<=initialObjects+25);check("repeated refresh keeps exact source",w->textDoc()->editorText()==source);check("refresh-only retained memory is bounded",memory()<=before+24*1024*1024);w->setModified(false);delete w;settle(100);save();app.exit(failures?1:0);
 });return app.exec();}
