#!/usr/bin/env python3
"""Rebuild only the leaking window object in the existing Qt 6.10.1 static plugin.

No installed library is overwritten. Pass an unmodified official qtbase v6.10.1
source tree and the matching MXE prefix; review the output archive before use.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', type=Path, required=True)
parser.add_argument('--prefix', type=Path, required=True)
parser.add_argument('--compiler', required=True)
parser.add_argument('--archiver', required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
source = args.source.resolve()
prefix = args.prefix.resolve()
output = args.output.resolve()
windows = source / 'src/plugins/platforms/windows'
source_file = windows / 'qwindowswindow.cpp'
expected = 'fb12d7a9ad6bc2525997b8d95f6d7bac7124049018c721f6aa6bb330737c7454'
if hashlib.sha256(source_file.read_bytes()).hexdigest() != expected:
    raise SystemExit('Expected the unmodified official Qt 6.10.1 window source.')
if '6.10.1' not in (prefix / 'include/QtCore/qtcoreversion.h').read_text():
    raise SystemExit('The installed Qt headers must also be version 6.10.1.')
output.mkdir(parents=True, exist_ok=True)
needle = '        DestroyWindow(m_data.hwnd);\n        context->removeWindow(m_data.hwnd);'
replacement = '''        // The titlebar helper is unparented unless ExpandedClientAreaHint is set.
        // Destroy it explicitly before the owner window, including flag changes.
        if (m_data.hwndTitlebar) {
            DestroyWindow(m_data.hwndTitlebar);
            m_data.hwndTitlebar = nullptr;
        }
        DestroyWindow(m_data.hwnd);
        context->removeWindow(m_data.hwnd);'''
text = source_file.read_text()
if text.count(needle) != 1:
    raise SystemExit('Unexpected destroyWindow implementation.')
patched = output / 'qwindowswindow.cpp'
patched.write_text(text.replace(needle, replacement))
includes = [prefix / 'include', windows, source / 'src/3rdparty/wintab']
for module in ('QtCore', 'QtGui', 'QtOpenGL'):
    includes += [prefix / 'include' / module,
                 prefix / 'include' / module / '6.10.1',
                 prefix / 'include' / module / '6.10.1' / module]
defines = ('NDEBUG', 'QT_NO_DEBUG', 'QT_STATIC', 'QT_STATICPLUGIN', 'QT_PLUGIN',
           'QT_NO_CAST_FROM_ASCII', 'QT_NO_FOREACH', 'UNICODE', '_UNICODE',
           'WIN32', 'WINVER=0x0A00', '_WIN32_WINNT=0x0A00',
           'QT_CORE_LIB', 'QT_GUI_LIB', 'QT_OPENGL_LIB')
obj = output / 'qwindowswindow.cpp.obj'
command = [args.compiler, '-std=c++17', '-O2']
command += ['-D' + define for define in defines]
command += ['-I' + str(include) for include in includes]
command += ['-c', str(patched), '-o', str(obj)]
subprocess.run(command, check=True)
original = prefix / 'plugins/platforms/libqwindows.a'
archive = output / 'libqwindows.a'
shutil.copy2(original, archive)
subprocess.run([args.archiver, 'r', str(archive), str(obj)], check=True)
metadata = {'qtVersion': '6.10.1', 'compileCommand': command,
            'originalSha256': hashlib.sha256(original.read_bytes()).hexdigest(),
            'correctedSha256': hashlib.sha256(archive.read_bytes()).hexdigest(),
            'replacedMember': obj.name}
(output / 'provenance.json').write_text(json.dumps(metadata, indent=2))
print(json.dumps(metadata, indent=2))
