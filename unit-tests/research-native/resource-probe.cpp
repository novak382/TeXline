#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "PDFDocumentWindow.h"
#include "ResearchAppearance.h"
#include "Settings.h"
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QThread>
#include <QPointer>
#include <QTimer>
#include <windows.h>
#include <psapi.h>
#include <functional>
static QString mode;static QString report;
static void settle(int ms){QElapsedTimer t;t.start();while(t.elapsed()<ms){QCoreApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QThread::msleep(3);}}
static QJsonObject memory(){PROCESS_MEMORY_COUNTERS_EX p{};p.cb=sizeof(p);using F=BOOL(WINAPI*)(HANDLE,PPROCESS_MEMORY_COUNTERS,DWORD);auto fn=reinterpret_cast<F>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"K32GetProcessMemoryInfo"));fn(GetCurrentProcess(),reinterpret_cast<PPROCESS_MEMORY_COUNTERS>(&p),sizeof(p));qint64 committed=0,reserved=0,largestFree=0,images=0,mapped=0,privateCommit=0;quintptr address=0;MEMORY_BASIC_INFORMATION m{};while(VirtualQuery(reinterpret_cast<void*>(address),&m,sizeof(m))){if(m.State==MEM_COMMIT){committed+=m.RegionSize;if(m.Type==MEM_IMAGE)images+=m.RegionSize;if(m.Type==MEM_MAPPED)mapped+=m.RegionSize;if(m.Type==MEM_PRIVATE)privateCommit+=m.RegionSize;}if(m.State==MEM_RESERVE)reserved+=m.RegionSize;if(m.State==MEM_FREE)largestFree=qMax(largestFree,qint64(m.RegionSize));quintptr next=quintptr(m.BaseAddress)+m.RegionSize;if(next<=address)break;address=next;}return {{"privateBytes",double(p.PrivateUsage)},{"committedBytes",double(committed)},{"imageBytes",double(images)},{"mappedBytes",double(mapped)},{"privateCommit",double(privateCommit)},{"reservedBytes",double(reserved)},{"largestFreeBytes",double(largestFree)},{"gdi",int(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS))},{"user",int(GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS))},{"tiles",QtPDF::Backend::Document::pageCache().tiles().size()}};}
int main(int argc,char **argv){report=QString::fromLocal8Bit(argv[1]);QString pdf;mode=qEnvironmentVariable("MATURITY_PROBE_MODE","scroll");QString file=QFileInfo(report).absolutePath();argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);pdf=QCoreApplication::applicationDirPath()+"/fixtures/lshort.pdf";QTimer::singleShot(80,&app,[&]{Tw::Settings().setValue("openPDFwithTeX",false);Research::setFocusMode(true);QJsonArray rounds;int failures=0;auto save=[&]{QSaveFile f(report);if(f.open(QIODevice::WriteOnly)){f.write(QJsonDocument(QJsonObject{{"mode",mode},{"rounds",rounds},{"failures",failures},{"checks",QJsonArray{QJsonObject{{"name","PDF windows and rendering resources close every round"},{"passed",failures==0}}}}}).toJson());f.commit();}};for(int n=0;n<24;++n){QtPDF::PDFDocumentWidget *view;QWidget *v;if(mode=="empty"){auto *wrapper=new QMainWindow;v=wrapper;view=new QtPDF::PDFDocumentWidget(wrapper);view->load(pdf);wrapper->setCentralWidget(view);wrapper->setProperty("researchFocus",true);wrapper->setAttribute(Qt::WA_DeleteOnClose,true);}else if(mode=="bare"){view=new QtPDF::PDFDocumentWidget;view->load(pdf);v=view;v->setAttribute(Qt::WA_DeleteOnClose,true);}else{auto *wrapper=new PDFDocumentWindow(pdf);v=wrapper;view=wrapper->widget();if(mode=="opaque"){v->ensurePolished();v->setAttribute(Qt::WA_TranslucentBackground,false);}}v->resize(960,740);v->show();settle(120);auto opened=memory();if(mode=="scroll"){for(int i=0;i<36;++i){view->goToPage((i*23+n*11)%view->document().toStrongRef()->numPages());if(i%18==0)view->zoomFitContentWidth();if(i%18==6)view->zoomFitWindow();if(i%18==12)view->zoomFitWidth();settle(12);}}auto active=memory();QPointer<QWidget> guard(v);auto document=view->document();v->close();settle(100);auto closed=memory();failures+=bool(guard)||!document.isNull()||!PDFDocumentWindow::documentList().isEmpty();rounds.append(QJsonObject{{"round",n},{"opened",opened},{"active",active},{"closed",closed}});save();}app.exit(failures?1:0);});return app.exec();}
