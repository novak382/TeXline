#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "utils/ResourcesLibrary.h"
#include "utils/FileVersionDatabase.h"
#include <QFile>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTimer>
#include <windows.h>

static QByteArray read(const QString &path){QFile f(path);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray();}
static bool write(const QString &path,const QByteArray &data){QSaveFile f(path);return f.open(QIODevice::WriteOnly)&&f.write(data)==data.size()&&f.commit();}
int main(int argc,char **argv){const QString report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);QTimer::singleShot(100,&app,[&]{
    QTemporaryDir root;const QString relative="scripts/Hooks/logParser.js",path=root.filePath(relative),dbPath=root.filePath("TwFileVersions.db");
    QDir(root.path()).mkpath("scripts/Hooks");const auto old=read(qEnvironmentVariable("TEXLINE_LOG_BASELINE")),builtIn=read(":/resfiles/"+relative);
    QJsonArray checks;int failures=0;auto check=[&](const QString &name,bool ok){checks.append(QJsonObject{{"name",name},{"passed",ok}});failures+=!ok;};
    check("private resource fixture and new embedded parser available",root.isValid()&&!old.isEmpty()&&!builtIn.isEmpty()&&old!=builtIn&&builtIn.contains("FirstCharsRegexSource"));
    check("old stock hook written in private resource tree",write(path,old));
    check("old hook reproduces Windows read-only stock permissions",QFile::setPermissions(path,QFileDevice::ReadOwner|QFileDevice::ReadUser));
    Tw::Utils::FileVersionDatabase db;db.addFileRecord(QFileInfo(path),Tw::Utils::FileVersionDatabase::hashForFile(path),"before");check("old stock hook ownership recorded",db.save(dbPath));
    Tw::Utils::ResourcesLibrary::updateLibraryResources(QDir(":/resfiles"),QDir(root.path()),"scripts");
    check("unmodified stock hook upgrades to exact embedded candidate",read(path)==builtIn);
    const QByteArray custom=builtIn+"\n// preserved private customization\n";
    check("private custom hook written",write(path,custom));
    Tw::Utils::ResourcesLibrary::updateLibraryResources(QDir(":/resfiles"),QDir(root.path()),"scripts");
    check("custom hook remains byte-identical",read(path)==custom);
    check("private hook deletion succeeds",QFile::remove(path));
    Tw::Utils::ResourcesLibrary::updateLibraryResources(QDir(":/resfiles"),QDir(root.path()),"scripts");
    check("deleted hook is not recreated",!QFile::exists(path));
    check("private stock hook restored only for failure experiment",write(path,old));
    db=Tw::Utils::FileVersionDatabase::load(dbPath);db.addFileRecord(QFileInfo(path),Tw::Utils::FileVersionDatabase::hashForFile(path),"before");check("failure experiment old stock ownership recorded",db.save(dbPath));
    const HANDLE lease=CreateFileW(reinterpret_cast<const wchar_t*>(path.utf16()),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    check("private read lease blocks stock replacement",lease!=INVALID_HANDLE_VALUE);
    Tw::Utils::ResourcesLibrary::updateLibraryResources(QDir(":/resfiles"),QDir(root.path()),"scripts");
    check("failed stock update retains exact old bytes and ownership",read(path)==old&&Tw::Utils::FileVersionDatabase::load(dbPath).getFileRecord(QFileInfo(path)).hash==Tw::Utils::FileVersionDatabase::hashForFile(path));
    if(lease!=INVALID_HANDLE_VALUE)CloseHandle(lease);
    Tw::Utils::ResourcesLibrary::updateLibraryResources(QDir(":/resfiles"),QDir(root.path()),"scripts");
    check("stock update retries after lease release",read(path)==builtIn);
    QSaveFile out(report);if(out.open(QIODevice::WriteOnly)){out.write(QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures}}).toJson());out.commit();}app.exit(failures?1:0);
});return app.exec();}
