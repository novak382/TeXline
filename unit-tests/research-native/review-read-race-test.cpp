#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "TeXDocumentWindow.h"
#include "CompletingEdit.h"
#include "Settings.h"
#include <QTextCursor>
#include <QTimer>
#define private public
#include "ResearchCollaboration.h"
#undef private
#include <QThreadPool>
#include <QSaveFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QDir>
#include <QElapsedTimer>
#include <QThread>
static bool put(const QString &path,const QString &text){QDir().mkpath(QFileInfo(path).absolutePath());QSaveFile f(path);const auto bytes=text.toUtf8();return f.open(QIODevice::WriteOnly)&&f.write(bytes)==bytes.size()&&f.commit();}
int main(int argc,char **argv){const QString report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);QTimer::singleShot(100,&app,[&]{
    QJsonArray checks;int failures=0;auto check=[&](QString name,bool ok){checks.append(QJsonObject{{"name",name},{"passed",ok}});failures+=!ok;};Tw::Settings().setValue("openPDFwithTeX",false);
    const QString root=QFileInfo(report).absolutePath()+"/queued-read/main.tex",a="\\documentclass{article}\n\\begin{document}Version A.\\end{document}\n",b="\\documentclass{article}\n\\begin{document}Version B.\\end{document}\n";
    check("independent earlier source is written",put(root,a));auto *w=new TeXDocumentWindow(root);auto *c=w->collaboration();
    // Model the preceding stable read, then let a real worker queue its second
    // result without delivering GUI callbacks. No file/GUI work is mocked.
    c->hasCandidate=true;c->stableCandidate=a;c->readAsync();check("the earlier real read completes and queues its callback",QThreadPool::globalInstance()->waitForDone(5000)&&c->reading);
    check("newer source is actually committed",put(root,b));c->receive(b);check("newer version is present before the old callback",w->editor()->toPlainText()==b);
    QCoreApplication::processEvents();check("queued old read cannot revert the received newer buffer",w->editor()->toPlainText()==b&&c->baseline==b&&!c->reading);
    w->setModified(false);delete w;QSaveFile f(report);if(f.open(QIODevice::WriteOnly)){f.write(QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures}}).toJson());f.commit();}app.exit(failures?1:0);
});return app.exec();}
