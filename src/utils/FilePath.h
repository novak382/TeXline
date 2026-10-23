#pragma once

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstring>
#endif

namespace Tw::Utils {
#ifdef Q_OS_WIN
inline QString directoryJunctionTarget(const QString &directory)
{
  QString native = QDir::toNativeSeparators(QFileInfo(directory).absoluteFilePath());
  if (!native.startsWith(QStringLiteral("\\\\?\\"))) native.prepend(QStringLiteral("\\\\?\\"));
  // Read the actual mount-point target, including beyond MAX_PATH. Qt's
  // junction metadata path cannot reliably classify these long cache aliases.
  const auto path = native.toStdWString();
  HANDLE handle = CreateFileW(path.c_str(), FILE_READ_EA,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
      FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  if (handle == INVALID_HANDLE_VALUE) return {};
  QByteArray buffer(16 * 1024, '\0');
  DWORD bytes = 0;
  const bool read = DeviceIoControl(handle, FSCTL_GET_REPARSE_POINT, nullptr, 0,
      buffer.data(), DWORD(buffer.size()), &bytes, nullptr);
  CloseHandle(handle);
  struct Header { DWORD tag; WORD length, reserved, offset, nameLength, printOffset, printLength; };
  static_assert(sizeof(Header) == 16);
  if (!read || bytes < sizeof(Header)) return {};
  Header header{}; std::memcpy(&header, buffer.constData(), sizeof(header));
  if (header.tag != IO_REPARSE_TAG_MOUNT_POINT || header.nameLength % sizeof(wchar_t) ||
      header.offset % sizeof(wchar_t) || sizeof(Header) + header.offset + header.nameLength > bytes ||
      header.length + 8u > bytes) return {};
  QString target = QString::fromWCharArray(reinterpret_cast<const wchar_t *>(
      buffer.constData() + sizeof(Header) + header.offset), header.nameLength / sizeof(wchar_t));
  if (!target.startsWith(QStringLiteral("\\??\\"))) return {};
  target.remove(0, 4);
  if (target.startsWith(QStringLiteral("UNC\\"), Qt::CaseInsensitive)) return {};
  return QFileInfo(target).canonicalFilePath();
}
#endif
// Only resolve the hashed search-directory aliases owned by the build cache.
// QFileInfo::canonicalFilePath on Windows does not resolve directory junctions.
inline QString resolveSearchAlias(const QString &absolutePath)
{
#ifdef Q_OS_WIN
  const QString normalized = QDir::fromNativeSeparators(absolutePath);
  QString marker = QStringLiteral("/search-paths/");
  qsizetype start = normalized.indexOf(marker, 0, Qt::CaseInsensitive);
  if (start < 0) {
    marker = QStringLiteral("/research-search-paths/");
    start = normalized.indexOf(marker, 0, Qt::CaseInsensitive);
  }
  if (start >= 0) {
    const qsizetype keyStart = start + marker.size();
    const qsizetype end = normalized.indexOf(QLatin1Char('/'), keyStart);
    const QString key = normalized.mid(keyStart, end < 0 ? -1 : end - keyStart);
    if (key.size() == 32 || key.size() == 64) {
      const QString target = directoryJunctionTarget(normalized.left(keyStart) + key);
      if (!target.isEmpty()) {
        const QString expected = QString::fromLatin1(QCryptographicHash::hash(target.toCaseFolded().toUtf8(), QCryptographicHash::Sha256).toHex());
        if (!target.isEmpty() && key == expected.left(key.size()))
          return end < 0 ? target : QDir(target).absoluteFilePath(normalized.mid(end + 1));
      }
    }
  }
#endif
  return absolutePath;
}

inline QString canonicalSourcePath(const QString &path)
{
  const QFileInfo file(resolveSearchAlias(QFileInfo(path).absoluteFilePath()));
  return file.exists() ? file.canonicalFilePath() : file.absoluteFilePath();
}
}
