// Developer-only x64 migration checks. GPL-2.0-or-later.
#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMainWindow>
#include <QSaveFile>
#include <QSettings>
#include <QTemporaryDir>
#include <windows.h>
#include <cstdint>
#include <limits>

int main(int argc, char **argv) {
  if (argc < 2) return 2;
  const QString report = QString::fromLocal8Bit(argv[1]);
  QApplication app(argc, argv);
  QJsonArray checks;
  int failures = 0;
  auto check = [&](const QString &name, bool passed) {
    checks.append(QJsonObject{{"name", name}, {"passed", passed}});
    failures += !passed;
  };
  check("native pointer and Windows user-data slots retain 64 bits",
        sizeof(void *) == 8 && sizeof(LONG_PTR) == 8 && sizeof(quintptr) == 8);
  BOOL wow64 = TRUE;
  check("process runs natively without WOW64",
        IsWow64Process(GetCurrentProcess(), &wow64) && !wow64);
  SYSTEM_INFO info{};
  GetSystemInfo(&info);
  check("usable address range extends above 4 GiB",
        reinterpret_cast<std::uintptr_t>(info.lpMaximumApplicationAddress) >
          std::numeric_limits<std::uint32_t>::max());

  // Reserve address space only. Commit one page beyond the old x86 ceiling;
  // do not consume 3 GiB of physical memory for this capability check.
  const SIZE_T bytes = SIZE_T(3) * 1024 * 1024 * 1024;
  auto *region = static_cast<unsigned char *>(
      VirtualAlloc(nullptr, bytes, MEM_RESERVE, PAGE_NOACCESS));
  check("one contiguous 3 GiB virtual reservation succeeds", region != nullptr);
  if (region) {
    auto *page = static_cast<std::uint64_t *>(VirtualAlloc(
        region + SIZE_T(2560) * 1024 * 1024, 4096,
        MEM_COMMIT, PAGE_READWRITE));
    if (page) *page = UINT64_C(0x12345678abcdef01);
    check("a page past 2 GiB within the reservation can be written and read",
          page && *page == UINT64_C(0x12345678abcdef01));
    check("large reservation releases immediately", VirtualFree(region, 0, MEM_RELEASE));
  }

  QMainWindow window;
  const auto hwnd = reinterpret_cast<HWND>(window.winId());
  const LONG_PTR previous = GetWindowLongPtrW(hwnd, GWLP_USERDATA);
  const LONG_PTR marker = LONG_PTR(UINT64_C(0x12345678abcdef01));
  SetWindowLongPtrW(hwnd, GWLP_USERDATA, marker);
  const bool retained = GetWindowLongPtrW(hwnd, GWLP_USERDATA) == marker;
  SetWindowLongPtrW(hwnd, GWLP_USERDATA, previous);
  check("native window roundtrip preserves all pointer bits", retained && IsWindow(hwnd));

  QTemporaryDir temporary;
  check("independent temporary directory is available", temporary.isValid());
  if (temporary.isValid()) {
    const QString ini = temporary.filePath("settings.ini");
    const QByteArray legacy("[General]\nresearchFocusMode=true\nresearchLanguage=en\n");
    QFile file(ini);
    bool written = file.open(QIODevice::WriteOnly) && file.write(legacy) == legacy.size();
    file.close();
    QSettings settings(ini, QSettings::IniFormat);
    check("existing architecture-independent INI settings remain readable",
          written && settings.value("researchFocusMode").toBool() &&
          settings.value("researchLanguage").toString() == "en");
  }
  QSaveFile output(report);
  if (!output.open(QIODevice::WriteOnly)) return 2;
  output.write(QJsonDocument(QJsonObject{{"checks", checks}, {"failures", failures},
       {"pointerBits", int(sizeof(void *) * 8)}, {"reservedBytes", double(bytes)}}).toJson());
  if (!output.commit()) return 2;
  return failures ? 1 : 0;
}
