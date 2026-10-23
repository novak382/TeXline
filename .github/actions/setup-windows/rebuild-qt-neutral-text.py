#!/usr/bin/env python3
"""Honor NoSubpixelAntialias in pinned Qt DirectWrite, preserving other repairs.

Builds a separate QtGui archive; never installs a library or application.
Only qwindowsfontenginedirectwrite.cpp.obj may change.
"""
from pathlib import Path
import argparse, hashlib, json, shutil, subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--source', type=Path, required=True)
p.add_argument('--prefix', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--compiler', required=True)
p.add_argument('--archiver', required=True)
a = p.parse_args()
source, prefix, output = a.source.resolve(), a.prefix.resolve(), a.output.resolve()
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
original = prefix / 'lib/libQt6Gui.a'
cpp = source / 'src/gui/text/windows/qwindowsfontenginedirectwrite.cpp'
if sha(original) != 'f8c846b6eb56123a4f7eb3b4be4e7c986b150650514fe117d195903a34af5690':
    raise SystemExit('Expected the existing x64 QtGui with its validated text-layout repairs')
if sha(cpp) != '31de02863de82f5f719f7f0357fe0650f4b0324e7528a671ee0c924f56e0f32b':
    raise SystemExit('Expected pinned unmodified Qt 6.10.1 DirectWrite source')
output.mkdir(parents=True, exist_ok=True)
text = cpp.read_text()
# Match MinGW's IFACEMETHOD declarations, which have no C++ noexcept specifier.
# Keep the COM declarations and definitions consistent for this toolchain;
# this signature compatibility is separate from the glyph-cache change below.
text = text.replace(') noexcept\n', ')\n')
needle = '    fontEngine->fontDef.pixelSize = pixelSize;'
assert text.count(needle) == 1
text = text.replace(needle, needle + '''
    // Keep a neutral glyph-cache format when DPI/zoom clones the engine.
    if (glyphFormat == QFontEngine::Format_A8)
        fontEngine->glyphFormat = QFontEngine::Format_A8;''')
needle = '''#endif
}

QString QWindowsFontEngineDirectWrite::fontNameSubstitute'''
assert text.count(needle) == 1
text = text.replace(needle, '''#endif
    // The raster engine otherwise selects its global ClearType A32 cache,
    // ignoring this per-font request. Use the existing DirectWrite A8 path;
    // retain color glyphs and let explicit NoAntialias keep precedence.
    if (glyphFormat != QFontEngine::Format_ARGB
        && (request.styleStrategy & QFont::NoSubpixelAntialias)
        && !(request.styleStrategy & QFont::NoAntialias))
        glyphFormat = QFontEngine::Format_A8;
}

QString QWindowsFontEngineDirectWrite::fontNameSubstitute''')
patched = output / cpp.name
patched.write_text(text)
includes = [prefix / 'include', cpp.parent, source / 'src/gui/text']
for module in ['QtCore', 'QtGui']:
    includes += [prefix/'include'/module, prefix/'include'/module/'6.10.1', prefix/'include'/module/'6.10.1'/module]
defines = ['NDEBUG','QT_NO_DEBUG','QT_STATIC','QT_BUILD_GUI_LIB','QT_NO_CAST_FROM_ASCII','QT_NO_FOREACH',
           'QT_BUILDING_QT','UNICODE','_UNICODE','WIN32','QT_CORE_LIB','QT_GUI_LIB',
           '_GTHREAD_USE_MUTEX_INIT_FUNC','_GTHREAD_USE_RECURSIVE_MUTEX_INIT_FUNC']
obj = output / (cpp.name + '.obj')
command = [a.compiler,'-std=c++17','-O2',*['-D'+v for v in defines],*['-I'+str(v) for v in includes],'-c',str(patched),'-o',str(obj)]
subprocess.run(command, check=True)
archive = output / original.name
shutil.copy2(original, archive)
members = subprocess.check_output([a.archiver,'t',str(original)],text=True).splitlines()
assert members.count(obj.name) == 1
subprocess.run([a.archiver,'r',str(archive),str(obj)],check=True)
assert subprocess.check_output([a.archiver,'t',str(archive)],text=True).splitlines() == members
for member in members:
    if member == obj.name: continue
    assert subprocess.check_output([a.archiver,'p',str(original),member]) == subprocess.check_output([a.archiver,'p',str(archive),member]), member
record = dict(qtVersion='6.10.1', originalSha256=sha(original), correctedSha256=sha(archive),
              originalSourceSha256=sha(cpp), patchedSourceSha256=sha(patched), replacedMember=obj.name,
              unchangedMembers=len(members)-1, compileCommand=command, backend='Windows DirectWrite',
              preservesColorFonts=True, preservesNoAntialias=True)
(output/'provenance.json').write_text(json.dumps(record,indent=2))
print(json.dumps(record,indent=2))
