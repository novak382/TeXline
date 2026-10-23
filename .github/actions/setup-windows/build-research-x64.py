#!/usr/bin/env python3
"""Build an x64 static Research candidate; never install or alter user settings."""
from pathlib import Path
import argparse, hashlib, json, shutil, struct, subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--source', type=Path, required=True)
p.add_argument('--build', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--mxe', type=Path, default=Path('/usr/lib/mxe'))
p.add_argument('--jobs', type=int, default=6)
a = p.parse_args()
target = 'x86_64-w64-mingw32.static'
cmake = a.mxe / 'usr/bin' / (target + '-cmake')
strip = a.mxe / 'usr/bin' / (target + '-strip')
source, build, output = a.source.resolve(), a.build.resolve(), a.output.resolve()
output.mkdir(parents=True, exist_ok=True)
subprocess.run([str(cmake), '-S', str(source), '-B', str(build), '-G', 'Ninja',
    '-DTW_BUILD_ID=research', '-DTW_REQUIRE_64BIT=ON', '-DQT_DEFAULT_MAJOR_VERSION=6',
    '-DWITH_TESTS=OFF', '-DCMAKE_BUILD_TYPE=Release', '-DBUILD_SHARED_LIBS=OFF',
    '-DBUILD_SHARED_PLUGINS=OFF', '-DWITH_PYTHON=OFF', '-DWITH_LUA=ON',
    '-DWITH_POPPLERQT=ON', '-DWITH_MUPDF=OFF',
    '-DCMAKE_EXE_LINKER_FLAGS=-Wl,--no-insert-timestamp',
    '-DPLATFORM_DEPENDENCIES=freetype;harfbuzz;freetype_too;bz2;iphlpapi;ssl;crypto;crypt32;ws2_32;sharpyuv',
    '-DPoppler_ADDITIONAL_DEPENDENCIES=freetype;harfbuzz;freetype_too;glib-2.0;intl;iconv;ws2_32;winmm;tiff;webp;jpeg;openjp2;png;lcms2;lzma;bz2;wtsapi32;sharpyuv;zstd;z',
    '-DTEXWORKS_ADDITIONAL_LIBS=freetype;harfbuzz;freetype_too;bz2;wtsapi32;opengl32;imm32;shlwapi;dwmapi;uxtheme',
    '-Dgp_tool=none'], check=True)
subprocess.run(['cmake', '--build', str(build), '--target', 'TeXworks', '-j', str(a.jobs)], check=True)
candidate = output / 'candidate.exe'
shutil.copy2(build / 'TeXworks.exe', candidate)
shutil.copytree(source / 'res/research/onboarding', output / 'resources/onboarding', dirs_exist_ok=True)
subprocess.run([str(strip), '--strip-unneeded', str(candidate)], check=True)
data = candidate.read_bytes()
pe = struct.unpack_from('<I', data, 0x3c)[0]
machine = struct.unpack_from('<H', data, pe + 4)[0]
magic = struct.unpack_from('<H', data, pe + 24)[0]
characteristics = struct.unpack_from('<H', data, pe + 22)[0]
if machine != 0x8664 or magic != 0x20b or not characteristics & 0x20:
    raise SystemExit('Candidate is not an AMD64 PE32+ executable')
record = {'architecture': 'x64', 'machine': hex(machine), 'peMagic': hex(magic),
          'largeAddressAware': bool(characteristics & 0x20),
          'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest(),
          'buildProfile': 'Release', 'toolchain': target}
(output / 'candidate-build.json').write_text(json.dumps(record, indent=2))
print(json.dumps(record, indent=2))
