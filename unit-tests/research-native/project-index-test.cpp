#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "TeXDocumentWindow.h"
#include "CompletingEdit.h"
#include "ResearchProject.h"
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QListView>
#include <QSaveFile>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <functional>
using namespace Research;
static bool put(const QString &path,const QByteArray &bytes){QDir().mkpath(QFileInfo(path).absolutePath());QFile f(path);return f.open(QIODevice::WriteOnly|QIODevice::Truncate)&&f.write(bytes)==bytes.size();}
static QByteArray get(const QString &path){QFile f(path);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray();}
static QStringList palette(TeXDocumentWindow *window,const QString &query,bool *completed){
  QStringList result;*completed=false;QElapsedTimer clock;clock.start();QTimer observer;observer.setInterval(10);
  QObject::connect(&observer,&QTimer::timeout,window,[&]{
    auto *dialog=window->findChild<QDialog*>("researchPalette");if(!dialog)return;
    auto *input=dialog->findChild<QLineEdit*>("researchNavigationQuery");auto *list=dialog->findChild<QListView*>("researchNavigationResults");
    if(input&&input->text()!=query){input->setText(query);return;}
    if(list&&list->model()->rowCount()>0){for(int n=0;n<list->model()->rowCount();++n)result.append(list->model()->index(n,0).data().toString());*completed=true;dialog->reject();}
    else if(clock.elapsed()>5000)dialog->reject();
  });observer.start();openProjectNavigation(window);observer.stop();return result;
}
int main(int argc,char **argv){
  const QString report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);
  QTimer::singleShot(0,&app,[&]{
    const QString folder=QFileInfo(report).absolutePath()+"/project";QDir().mkpath(folder);QJsonArray checks,runs;int failed=0;
    auto check=[&](const QString &name,bool ok){checks.append(QJsonObject{{"name",name},{"passed",ok}});if(!ok)++failed;};
    QByteArray body="\\documentclass{article}\n\\begin{document}\n";
    bool written=true;
    for(int n=0;n<12;++n){const QString leaf="chapter"+QString::number(n)+".tex";body+="\\input{"+leaf.toUtf8()+"}\n";QByteArray chapter;for(int k=0;k<500;++k)chapter+="\\section{Context "+QByteArray::number(k)+"}\\label{key"+QByteArray::number(n)+"_"+QByteArray::number(k)+"}\n";written&=put(folder+"/"+leaf,chapter);}
    body+="\\end{document}\n";const QString root=folder+"/main.tex";written&=put(root,body);check("independent multi-chapter project is written",written);
    for(int pair=0;pair<3;++pair){
      auto run=[&](bool cancel){int polls=0;QElapsedTimer time;time.start();auto index=buildProjectIndex(root,{},false,[&]{return cancel&&++polls>=64;});const double ms=time.nsecsElapsed()/1e6;
        check(cancel?"abandoned scan stops at a bounded entry boundary":"complete scan retains all chapters and labels",cancel?(index->cancelled&&index->entries.size()<1000):(!index->cancelled&&!index->truncated&&index->files.size()==13&&searchProject(*index,"key11_499").size()==1));
        runs.append(QJsonObject{{"pair",pair},{"cancelRequested",cancel},{"milliseconds",ms},{"entries",index->entries.size()},{"cancelled",index->cancelled}});};
      if(pair%2){run(true);run(false);}else{run(false);run(true);}
    }
    auto immediate=buildProjectIndex(root,{},true,[]{return true;});check("already cancelled request reads no project or bibliography",immediate->cancelled&&immediate->files.isEmpty()&&immediate->entries.isEmpty());
    const QString small=folder+"/paper/main.tex",external=folder+"/章节/part.tex";
    const QByteArray main="\\documentclass{article}\n\\begin{document}\n\\input{../章节/part}\n\\end{document}\n",old="\\section{Same context}\\label{key_old}\n",fresh="\\section{Same context}\\label{key_new}\n";
    check("outside chapter fixture is written",put(small,main)&&put(external,old));
    auto *window=new TeXDocumentWindow(small);window->show();window->activateWindow();QCoreApplication::processEvents();bool ready=false;
    const auto before=palette(window,"key_",&ready);check("first navigation reads outside chapter label",ready&&std::any_of(before.cbegin(),before.cend(),[](const QString &s){return s.contains("key_old");}));
    const QDateTime modified=QFileInfo(external).lastModified(),directoryModified=QFileInfo(QFileInfo(external).absolutePath()).lastModified();const qint64 size=QFileInfo(external).size();
    const bool replaced=put(external,fresh);QFile stamp(external);const bool restored=stamp.open(QIODevice::ReadWrite)&&stamp.setFileTime(modified,QFileDevice::FileModificationTime);stamp.close();
    check("external rewrite preserves file size time and parent-directory time",replaced&&restored&&get(external)==fresh&&QFileInfo(external).size()==size&&QFileInfo(external).lastModified()==modified&&QFileInfo(QFileInfo(external).absolutePath()).lastModified()==directoryModified);
    const auto after=palette(window,"key_",&ready);check("cached navigation observes same-metadata external bytes",ready&&std::any_of(after.cbegin(),after.cend(),[](const QString &s){return s.contains("key_new");})&&std::none_of(after.cbegin(),after.cend(),[](const QString &s){return s.contains("key_old");}));
    check("navigation and cancellation do not edit root source or its buffer",get(small)==main&&window->editor()->toPlainText()==QString::fromUtf8(main)&&!window->isModified());
    window->setModified(false);window->close();QSaveFile output(report);if(output.open(QIODevice::WriteOnly)){output.write(QJsonDocument(QJsonObject{{"checks",checks},{"runs",runs},{"failures",failed},{"physicalInputVerified",false}}).toJson());output.commit();}app.exit(failed?1:0);
  });return app.exec();
}
