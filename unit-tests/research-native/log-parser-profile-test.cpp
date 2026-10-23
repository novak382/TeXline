#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "TeXDocumentWindow.h"
#include "Settings.h"
#include "scripting/ScriptAPI.h"
#include "scripting/ScriptObject.h"
#include "scripting/ECMAScriptInterface.h"
#include <QElapsedTimer>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QTextEdit>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTimer>
#include <QJSEngine>
#include <QQmlEngine>
#include <memory>
#include <array>
struct AccessStats{int calls=0;double ms=0;QHash<QString,int> paths;};
class MeasuredAPI : public Tw::Scripting::ScriptAPI {
 std::shared_ptr<AccessStats> stats;
public:
 MeasuredAPI(Tw::Scripting::ScriptObject *script,QObject *app,QObject *target,QVariant &result,std::shared_ptr<AccessStats> s):ScriptAPI(script,app,target,result),stats(std::move(s)){}
 QObject *clone()const override{return new MeasuredAPI(m_script,m_app,m_target,m_result,stats);}
 int fileExists(const QString &path)const override{QElapsedTimer t;t.start();const int result=ScriptAPI::fileExists(path);++stats->calls;++stats->paths[path];stats->ms+=t.nsecsElapsed()/1e6;return result;}
};
int main(int argc,char **argv){const QString report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);QTimer::singleShot(100,&app,[&]{
 const QString root=qEnvironmentVariable("TEXLINE_LOG_ROOT"),log=qEnvironmentVariable("TEXLINE_LOG_FILE"),scriptPath=qEnvironmentVariable("TEXLINE_LOG_SCRIPT");QFile file(log);const bool loaded=file.open(QIODevice::ReadOnly);QString output=QString::fromUtf8(file.readAll());output.replace("\r\n","\n");output.replace(QLatin1Char('\r'),QLatin1Char('\n'));file.close();Tw::Settings().setValue("openPDFwithTeX",false);Tw::Settings().setValue("allowScriptFileReading",false);
 auto *window=new TeXDocumentWindow(root);window->show();QObject snapshot;QQmlEngine::setObjectOwnership(window,QQmlEngine::CppOwnership);QQmlEngine::setObjectOwnership(&snapshot,QQmlEngine::CppOwnership);snapshot.setProperty("fileName",window->fileName());snapshot.setProperty("rootFileName",window->property("rootFileName"));snapshot.setProperty("consoleOutput",output);
 // Use the same captured log with the real target; no compiler is started.
 window->findChild<QTextEdit*>("textEdit_console")->setPlainText(output);
 Tw::Scripting::ECMAScriptInterface language(nullptr);Tw::Scripting::ScriptObject script(std::unique_ptr<Tw::Scripting::Script>(language.newScript(scriptPath)));QJsonArray rows,checks;int failures=0;auto check=[&](const QString &name,bool ok){checks.append(QJsonObject{{"name",name},{"passed",ok}});failures+=!ok;};check("captured private log and parser source are available",loaded&&!output.isEmpty()&&QFileInfo::exists(scriptPath));
 QFile parserFile(scriptPath);parserFile.open(QIODevice::ReadOnly);const QString parserCode=QString::fromUtf8(parserFile.readAll());parserFile.close();QString expected;
 QFile baselineFile(qEnvironmentVariable("TEXLINE_LOG_BASELINE"));const bool paired=baselineFile.open(QIODevice::ReadOnly);const QString baselineCode=paired?QString::fromUtf8(baselineFile.readAll()):parserCode;baselineFile.close();
 const bool profile=qEnvironmentVariableIntValue("TEXLINE_LOG_REGEX_PROFILE");
 for(int pair=0;pair<3;++pair)for(bool frozen:(pair%2?std::array<bool,2>{true,false}:std::array<bool,2>{false,true})){const QString code=paired&&!frozen?baselineCode:parserCode;const bool snapshotTarget=frozen&&!paired;QVariant result;auto stats=std::make_shared<AccessStats>();MeasuredAPI api(&script,&app,snapshotTarget?&snapshot:window,result,stats);QElapsedTimer t;t.start();QJSEngine engine;engine.installExtensions(QJSEngine::AllExtensions);engine.globalObject().setProperty("__api",engine.newQObject(api.clone()));QJSValue target=snapshotTarget?engine.newObject():engine.newQObject(window);if(snapshotTarget){target.setProperty("fileName",snapshot.property("fileName").toString());target.setProperty("rootFileName",snapshot.property("rootFileName").toString());target.setProperty("consoleOutput",output);}engine.globalObject().setProperty("__target",target);engine.evaluate("var TW={target:__target,fileExists:function(p){return __api.fileExists(p);},question:function(a,b,c,d){return __api.question(a,b,c,d);}};");if(profile)engine.evaluate("var regexStats={};var originalExec=RegExp.prototype.exec;RegExp.prototype.exec=function(s){var k=this.source;var r=regexStats[k]||(regexStats[k]={calls:0,ms:0});var start=Date.now();var v=originalExec.call(this,s);r.calls++;r.ms+=Date.now()-start;return v;};");const auto evaluated=engine.evaluate(code,scriptPath);const bool ran=!evaluated.isError();const QString html=engine.globalObject().property("TW").property("result").toString();const double duration=t.nsecsElapsed()/1e6;
  if(pair==0&&!frozen)expected=html;check(QString("%1 target retains exact diagnostic HTML in pair %2").arg(frozen?"snapshot":"real").arg(pair),ran&&html==expected);
  int repeated=0;for(int n:stats->paths)repeated+=qMax(0,n-1);rows.append(QJsonObject{{"pair",pair},{"target",paired?(frozen?"candidate":"baseline"):(frozen?"snapshot":"real")},{"durationMs",duration},{"fileExistsCalls",stats->calls},{"uniquePaths",stats->paths.size()},{"repeatLookups",repeated},{"fileExistsMs",stats->ms},{"htmlChars",html.size()},{"htmlSha256",QString::fromLatin1(QCryptographicHash::hash(html.toUtf8(),QCryptographicHash::Sha256).toHex())},{"scriptSha256",QString::fromLatin1(QCryptographicHash::hash(code.toUtf8(),QCryptographicHash::Sha256).toHex())},{"regexStats",QJsonValue::fromVariant(engine.globalObject().property("regexStats").toVariant())}});QSaveFile checkpoint(report);if(checkpoint.open(QIODevice::WriteOnly)){checkpoint.write(QJsonDocument(QJsonObject{{"checks",checks},{"rows",rows},{"logChars",output.size()},{"failures",failures}}).toJson());checkpoint.commit();}
 }
 window->setModified(false);delete window;QSaveFile out(report);if(out.open(QIODevice::WriteOnly)){out.write(QJsonDocument(QJsonObject{{"checks",checks},{"rows",rows},{"logChars",output.size()},{"failures",failures}}).toJson());out.commit();}app.exit(failures?1:0);
});return app.exec();}
