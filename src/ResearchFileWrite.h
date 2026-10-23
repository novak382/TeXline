// Shared atomic file replacement. GPL-2.0-or-later.
#pragma once
#include "ResearchWorkspaceUi.h"
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QTemporaryFile>
#include <QThread>
#include <functional>
#include <memory>
#include <cstring>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif
namespace Research {
#ifdef Q_OS_WIN
inline bool replaceGuardedFile(const QString &path,const QByteArray &data,QString *error){
  // Qt's ordinary FileRenameInfo cannot replace a destination held open by
  // our validation lease. FileRenameInfoEx preserves that lease through the
  // rename; no direct-write fallback or gap before the commit is necessary.
  struct RemoveTemporary {QString path;~RemoveTemporary(){if(!path.isEmpty())QFile::remove(path);}} cleanup;
  {
    QTemporaryFile staged(path+QStringLiteral(".texline-XXXXXX"));
    if(!staged.open()||staged.write(data)!=data.size()||!staged.flush()||!staged.setPermissions(QFileInfo(path).permissions())){
      *error=path+QStringLiteral(": ")+staged.errorString();return false;
    }
    cleanup.path=staged.fileName();staged.setAutoRemove(false);
  }
  const auto temporary=QDir::toNativeSeparators(cleanup.path).toStdWString();
  HANDLE raw=CreateFileW(temporary.c_str(),GENERIC_WRITE|DELETE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(raw==INVALID_HANDLE_VALUE){*error=words("无法准备文件替换：","Cannot prepare file replacement: ")+path;return false;}
  std::unique_ptr<void,decltype(&CloseHandle)> handle(raw,&CloseHandle);
  if(!FlushFileBuffers(raw)){*error=words("无法写入文件：","Cannot write file: ")+path;return false;}
  struct RenameInfo {DWORD flags;HANDLE root;DWORD length;WCHAR name[1];};
  const auto destination=QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath()).toStdWString();
  const auto length=destination.size()*sizeof(wchar_t);QByteArray storage(sizeof(RenameInfo)+length+sizeof(wchar_t),char(0));
  auto *info=new(storage.data()) RenameInfo{};info->flags=0x1|0x2;info->length=DWORD(length);std::memcpy(info->name,destination.data(),length);
  const bool renamed=SetFileInformationByHandle(raw,static_cast<FILE_INFO_BY_HANDLE_CLASS>(22),info,DWORD(storage.size()));
  if(!renamed){*error=words("无法安全替换此文件，原文已保留：","Cannot safely replace this file. Original retained: ")+path;return false;}
  cleanup.path.clear();error->clear();return true;
}
#endif
inline bool writeNewFile(const QString &path,const QByteArray &data,QString *error);
inline bool writeObservedFile(const QString &path,const QByteArray &data,QString *error,const std::function<bool()> &retryAllowed={}){
  if(!QDir().mkpath(QFileInfo(path).absolutePath())){*error=words("无法创建目标文件夹。","Cannot create the destination folder.");return false;}
  for(int attempt=0;;++attempt){
    {
      bool writerBusy=false;const bool creating=retryAllowed&&!QFileInfo::exists(path);
#ifdef Q_OS_WIN
      // Hold a read handle which excludes in-place writers while validating
      // and atomically replacing the destination. A check before a retry alone
      // can become stale when an existing writer releases its handle.
      std::unique_ptr<void,decltype(&CloseHandle)> lease(nullptr,&CloseHandle);
      if(retryAllowed&&!creating){
        const auto native=QDir::toNativeSeparators(path).toStdWString();
        HANDLE handle=CreateFileW(native.c_str(),GENERIC_READ|DELETE,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(handle!=INVALID_HANDLE_VALUE){
          // Qt readers do not share DELETE access. Probe it for the bounded
          // reader-lease retry, then retain only READ while checking contents.
          // A writer entering between the two opens is rejected or revalidated.
          CloseHandle(handle);
          handle=CreateFileW(native.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        }
        if(handle==INVALID_HANDLE_VALUE){
          const auto code=GetLastError();
          if((code!=ERROR_SHARING_VIOLATION&&code!=ERROR_LOCK_VIOLATION)||attempt>=10){*error=words("文件正在使用或已变化，请重新预览：","File is in use or has changed. Preview again: ")+path;return false;}
          writerBusy=true;
        }else lease.reset(handle);
      }
#endif
      if(!writerBusy){
      if(retryAllowed&&!retryAllowed()){*error=words("文件已变化，请重新预览：","File changed. Preview again: ")+path;return false;}
      if(creating)return writeNewFile(path,data,error);
#ifdef Q_OS_WIN
      if(lease)return replaceGuardedFile(path,data,error);
#endif
      QSaveFile f(path);if(!f.open(QIODevice::WriteOnly)||f.write(data)!=data.size()){*error=path+QStringLiteral(": ")+f.errorString();return false;}
      if(f.commit()){error->clear();return true;}*error=path+QStringLiteral(": ")+f.errorString();
#ifdef Q_OS_WIN
      // Same bounded atomic-rename policy as build metadata: a short reader
      // lease must not force direct overwrite or bypass the content snapshot.
      if(f.error()!=QFileDevice::RenameError||attempt>=10||!retryAllowed)return false;
#else
      return false;
#endif
      }
    }
    QThread::msleep(10);if(retryAllowed&&!retryAllowed()){*error=words("文件已变化，请重新预览：","File changed. Preview again: ")+path;return false;}
  }
}

// Publishing a new bibliography must never replace a file created while its
// import preview was open. QFile::rename does not replace an existing target.
inline bool writeNewFile(const QString &path,const QByteArray &data,QString *error) {
 QTemporaryFile staged(path+QStringLiteral(".texline-XXXXXX"));
 if(!staged.open()||staged.write(data)!=data.size()||!staged.flush()) {*error=staged.errorString();return false;}
 staged.close();
 if(!staged.rename(path)){*error=staged.errorString();return false;}
 staged.setAutoRemove(false);error->clear();return true;
}
}
