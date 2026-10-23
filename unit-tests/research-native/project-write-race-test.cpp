#include "ActualAuthoringRecording.h"
#include "ResearchProjectOperations.cpp"
#include <condition_variable>
#include <mutex>
#include <thread>
using namespace Research;
int main(int argc,char **argv){const auto report=argument(1);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);QTimer::singleShot(60,&app,[&]{
 const auto folder=QFileInfo(report).absolutePath();QJsonArray checks,details;int failures=0;
 for(int i=0;i<12;++i){
  const auto file=folder+QString("/paper/race-%1.txt").arg(i);put(file,"before");const auto native=QDir::toNativeSeparators(file).toStdWString();
  HANDLE lease=CreateFileW(native.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
  std::mutex mutex;std::condition_variable cv;bool released=false,done=false;DWORD written=0;bool writerOk=false;
  std::thread writer([&]{std::unique_lock<std::mutex> lock(mutex);cv.wait(lock,[&]{return released;});lock.unlock();if(lease!=INVALID_HANDLE_VALUE){writerOk=WriteFile(lease,"newer!",6,&written,nullptr)&&FlushFileBuffers(lease);CloseHandle(lease);}lock.lock();done=true;lock.unlock();cv.notify_all();});
  int validations=0;QString error;
  auto unchanged=[&]{const bool same=bytes(file)=="before";if(++validations==1){std::unique_lock<std::mutex> lock(mutex);released=true;cv.notify_all();cv.wait(lock,[&]{return done;});}return same;};
  const bool applied=writeBytes(file,"after!",&error,unchanged);
  {std::lock_guard<std::mutex> lock(mutex);released=true;}cv.notify_all();writer.join();
  const auto content=bytes(file);const bool pass=lease!=INVALID_HANDLE_VALUE&&writerOk&&written==6&&!applied&&content=="newer!";
  checks.append(QJsonObject{{"name",QString("snapshot/rename race preserves newer writer %1").arg(i)},{"passed",pass}});failures+=!pass;
  details.append(QJsonObject{{"applied",applied},{"writerOk",writerOk},{"written",int(written)},{"validations",validations},{"content",QString::fromUtf8(content)},{"error",error}});
 }
 for(const auto &name:{QString("normal.txt"),QString("中文 draft.txt")}){
  const auto file=folder+"/paper/"+name;put(file,"before");QString error;const auto permissions=QFileInfo(file).permissions();
  const bool applied=writeBytes(file,"after!",&error,[&]{return bytes(file)=="before";});
  const bool pass=applied&&bytes(file)=="after!"&&QFileInfo(file).permissions()==permissions;
  checks.append(QJsonObject{{"name","uncontended replacement and permissions "+name},{"passed",pass}});failures+=!pass;
  details.append(QJsonObject{{"file",name},{"applied",applied},{"error",error}});
 }
 const bool clean=QDir(folder+"/paper").entryList({"*.texline-*"},QDir::Files).isEmpty();checks.append(QJsonObject{{"name","success and cancellation leave no staged files"},{"passed",clean}});failures+=!clean;
 put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"details",details}}).toJson());app.exit(failures?1:0);
 });return app.exec();}
