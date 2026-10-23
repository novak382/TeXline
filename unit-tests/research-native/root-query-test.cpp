#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "TeXDocumentWindow.h"
#include "CompletingEdit.h"
#include "ResearchWorkflow.h"
#include "Settings.h"
#include "document/TeXDocument.h"
#include "TWScriptManager.h"
#include "scripting/ECMAScriptInterface.h"
#include "scripting/ScriptObject.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTextCursor>
#include <QTimer>
class ProbeDocument : public Tw::Document::TeXDocument {
public: bool snapshotCaptured()const{return !_sourceText.isNull()||!_editorText.isNull();}
};
static bool put(const QString &path,const QString &text){QSaveFile f(path);return f.open(QIODevice::WriteOnly)&&f.write(text.toUtf8())==text.toUtf8().size()&&f.commit();}
static QByteArray get(const QString &path){QFile f(path);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray();}
int main(int argc,char **argv){
 const QString report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);
 QTimer::singleShot(0,&app,[&]{
  // The controlled script writes only its private timing report.
  Tw::Settings().setValue("allowScriptFileWriting",true);
  const QString folder=QFileInfo(report).absolutePath()+"/project";QDir().mkpath(folder);QJsonArray checks,runs;int failures=0;
  auto check=[&](const QString &name,bool ok){checks.append(QJsonObject{{"name",name},{"passed",ok}});if(!ok)++failures;};
  const QString probeFile=folder+"/probe.tex",chosen=folder+"/chosen.tex",other=folder+"/other.tex";
  check("independent root fixtures are written",put(probeFile,"\\documentclass{article}\n")&&put(chosen,"\\documentclass{book}\n")&&put(other,"\\documentclass{report}\n"));
  ProbeDocument probe;probe.setLayoutEnabled(false);probe.setFileInfo(QFileInfo(probeFile));probe.setStoredInFilesystem(true);
  probe.setPlainText("% 中文 header\n\\documentclass{article}\n"+QString("% immutable paper body\n").repeated(100000));probe.parseModeLines();probe.invalidateSourceText();const int revision=probe.revision();
  check("ordinary main root is correct without complete source materialization",probe.getRootFilePath()==QFileInfo(probeFile).canonicalFilePath()&&!probe.snapshotCaptured()&&probe.revision()==revision);
  Research::rememberRoot(probeFile,chosen);probe.invalidateSourceText();check("remembered root keeps precedence without complete source materialization",probe.getRootFilePath()==QFileInfo(chosen).canonicalFilePath()&&!probe.snapshotCaptured());
  probe.setPlainText("%!TEX root = other.tex\n\\documentclass{article}\n");probe.parseModeLines();probe.invalidateSourceText();check("explicit modeline retains precedence over the remembered root",QDir::cleanPath(probe.getRootFilePath())==QFileInfo(other).canonicalFilePath());
  const QString fallbackFile=folder+"/fallback.tex";check("fallback fixture is written",put(fallbackFile,"body\n"));probe.setFileInfo(QFileInfo(fallbackFile));
  const QStringList fallbackHeaders{QString("% long header\n").repeated(400)+"\\documentclass{article}\n", "\\newcommand{\\x}{a}\n\\documentclass{article}\n", "\\documentclass[% option\n12pt]{article}\n", "\\verb|\\documentclass{article}|\n", "\\begin{verbatim}\n\\documentclass{article}\n\\end{verbatim}\n"};
  for(int n=0;n<fallbackHeaders.size();++n){probe.setPlainText(fallbackHeaders[n]);probe.parseModeLines();probe.invalidateSourceText();const QString expected=Research::resolveRoot(fallbackFile,probe.toPlainText());check("complex or late header uses complete classification "+QString::number(n),probe.getRootFilePath()==expected&&probe.snapshotCaptured());}
  for(int lines:{2500,100000}){
   const QString path=folder+"/paper-"+QString::number(lines)+".tex";const QString original="\\documentclass{article}\n% edit marker\n"+QString("% independent source and bilingual 注释.\n").repeated(lines)+"\\begin{document}Body.\\end{document}\n";
   check("independent editable paper "+QString::number(lines),put(path,original));auto *window=new TeXDocumentWindow(path);window->show();QCoreApplication::processEvents();auto *doc=window->textDoc();QJsonArray samples;bool correct=true;
   const int position=original.indexOf('\n')+1;
   for(int n=0;n<12;++n){QTextCursor edit(doc);edit.beginEditBlock();edit.setPosition(position+n);edit.insertText("%");edit.endEditBlock();QElapsedTimer clock;clock.start();const QString root=window->property("rootFileName").toString();samples.append(double(clock.nsecsElapsed())/1e6);correct&=root==QFileInfo(path).canonicalFilePath();}
   check("root queries observe every native edit without changing its target "+QString::number(lines),correct&&doc->toPlainText()==original.left(position)+QString(12,'%')+original.mid(position));for(int n=0;n<12;++n)doc->undo();check("root reads preserve native undo and independent disk "+QString::number(lines),doc->toPlainText()==original&&get(path)==original.toUtf8());
   runs.append(QJsonObject{{"case","native-root-query"},{"lines",lines},{"samplesMs",samples},{"physicalTypingMeasured",false}});
   const QString metric=folder+"/script-"+QString::number(lines)+".json",scriptFile=folder+"/root-edits.js";
   const QString quoted=QString::fromUtf8(QJsonDocument(QJsonArray{metric}).toJson(QJsonDocument::Compact)).mid(1);const QString metricLiteral=quoted.left(quoted.size()-1);
   const QString body="var original=TW.target.text;var root=TW.target.rootFileName;var p=original.indexOf('\\n')+1;if(p<1)throw Error('Missing edit paragraph');var correct=true;var samples=[];for(var b=0;b<3;b++){var start=Date.now();for(var i=0;i<12;i++){TW.target.selectRange(p,0);TW.target.insertText('%');correct=correct&&TW.target.rootFileName===root;}TW.yield();samples.push(Date.now()-start);}var expected=original.slice(0,p)+new Array(37).join('%')+original.slice(p);correct=correct&&TW.target.text===expected&&TW.target.modified;TW.target.selectRange(p,36);TW.target.insertText('');TW.writeFile("+metricLiteral+",JSON.stringify({correct:correct,batchesMs:samples,writes:36,rootReads:36}));undefined;";
   bool scripted=put(scriptFile,"// TeXworksScript\n// Title: independent root query benchmark\n// Script-Type: standalone\n"+body);Tw::Scripting::ECMAScriptInterface language(nullptr);Tw::Scripting::ScriptObject object(std::unique_ptr<Tw::Scripting::Script>(language.newScript(scriptFile)));QVariant result;scripted=scripted&&object.getScript()->parseHeader()&&app.getScriptManager()->runScript(&object,window,result,Tw::Scripting::Script::ScriptStandalone);
   const auto observed=QJsonDocument::fromJson(get(metric)).object();check("real JavaScript root reads see all pending writes "+QString::number(lines),scripted&&observed["correct"].toBool()&&doc->toPlainText()==original&&get(path)==original.toUtf8());runs.append(QJsonObject{{"case","script-write-root-query"},{"lines",lines},{"metrics",observed}});window->setModified(false);delete window;
  }
  QSaveFile file(report);if(file.open(QIODevice::WriteOnly)){file.write(QJsonDocument(QJsonObject{{"checks",checks},{"runs",runs},{"failures",failures},{"physicalInputVerified",false}}).toJson());file.commit();}app.exit(failures?1:0);
 });return app.exec();
}
