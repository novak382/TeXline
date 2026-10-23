// Bounded asynchronous external tools, scoped to their requesting UI. GPL-2.0-or-later.
#pragma once
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QPointer>
#include <functional>
namespace Research {
struct TaskResult { bool ok{false}; QByteArray output,error; int code{-1}; };
inline QString systemTool(const QString &name) {
#ifdef Q_OS_WIN
  if(name==QStringLiteral("curl"))return QDir(qEnvironmentVariable("SystemRoot",QStringLiteral("C:/Windows"))).filePath(QStringLiteral("System32/curl.exe"));
  if(name==QStringLiteral("git"))for(const auto &base:{qEnvironmentVariable("ProgramFiles"),qEnvironmentVariable("LOCALAPPDATA")})for(const auto &part:{QStringLiteral("Git/cmd/git.exe"),QStringLiteral("Programs/Git/cmd/git.exe")}){const auto path=QDir(base).filePath(part);if(!base.isEmpty()&&QFileInfo::exists(path))return path;}
#endif
  return QStandardPaths::findExecutable(name);
}
class ExternalTask final : public QObject {
  QProcess process; QTimer timeout; QByteArray output,error; bool completed{false},failed{false};
  std::function<void(TaskResult)> done;
  void collect() {output+=process.readAllStandardOutput();error+=process.readAllStandardError();if(output.size()>8*1024*1024||error.size()>1024*1024){failed=true;output.truncate(8*1024*1024);error="The operation produced too much output. Narrow the selection.";process.kill();}}
  void finish(bool ok,int code) {if(completed)return;completed=true;timeout.stop();collect();auto callback=std::move(done);QPointer<ExternalTask> guard(this);if(callback)callback({ok&&!failed,output,error,code});if(guard)deleteLater();}
public:
  ExternalTask(QObject *owner,const QString &program,const QStringList &args,const QString &cwd,std::function<void(TaskResult)> callback,int milliseconds=30000,const QByteArray &input={},QProcessEnvironment env=QProcessEnvironment::systemEnvironment()):QObject(owner),done(std::move(callback)) {
    process.setProgram(program);process.setArguments(args);process.setWorkingDirectory(cwd);process.setProcessEnvironment(env);
#ifdef Q_OS_WIN
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *a){a->flags|=0x08000000;});
#endif
    connect(&process,&QProcess::readyReadStandardOutput,this,[this]{collect();});
    connect(&process,&QProcess::readyReadStandardError,this,[this]{collect();});
    connect(&process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus status){finish(code==0&&status==QProcess::NormalExit,code);});
    connect(&process,&QProcess::errorOccurred,this,[this](QProcess::ProcessError e){if(e==QProcess::FailedToStart){error=process.errorString().toUtf8();finish(false,-1);}});
    timeout.setSingleShot(true);connect(&timeout,&QTimer::timeout,this,[this]{failed=true;error="The operation timed out.";process.kill();});
    connect(&process,&QProcess::started,this,[this,input]{if(!input.isEmpty())process.write(input);process.closeWriteChannel();});
    QTimer::singleShot(0,this,[this,milliseconds]{if(completed)return;timeout.start(milliseconds);process.start();});
  }
  ~ExternalTask() override {done={};if(process.state()!=QProcess::NotRunning){process.kill();process.waitForFinished(1000);}}
  void cancel(){completed=true;timeout.stop();done={};process.kill();deleteLater();}
};
}
