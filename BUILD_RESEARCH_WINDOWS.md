# TeXworks Research: validated Windows dependency fixes

## Background source layout batches (2026-10-23)

Run `rebuild-qt-text-layout.py --incremental-edits --bounded-source-timers`
against the pinned Qt 6.10.1 source and the corrected width-cache archive below.
The recipe replaces only `qtextdocumentlayout.cpp.obj`, retaining the other 248
archive members byte for byte, including neutral text and width shaping fixes.
For opted-in source documents of at least one million characters, background
timer continuations process at most an 8,000-character target batch, instead of
growing to 200,000. A single paragraph may exceed this target. Foreground cursor,
paint and document-size requests still complete the required range immediately.
This uses Qt's existing layout timer; it introduces no new polling.

The archive built at `/tmp/texworks-x64/motion-flow/qt-bounded` has SHA-256
`d663e0e33b94b18406e26aefbbed15b2e496649e9374df1cfbc8af5aee715005`.
Both source-layout capability guards recognize it. Keep the previous corrected
archive when adopting this one and record the recipe provenance in
`docs/dependency-provenance.json`.

## Capability guards after text and width-cache repairs (2026-10-23)

The neutral-text (`305e8946...`) and width-cache (`da639aa1...`) archives preserve
the validated `qtextdocumentlayout.cpp.obj`. Both must be recognized by the
`TW_QT_SAFE_LAZY_LAYOUT` and `TW_QT_LAZY_SOURCE_EDITS` CMake guards. A missing
identity silently restores the full-layout workaround and disables source edit
continuation despite linking the corrected library. Check the actual compiler
definitions as well as archive hashes after adopting a derived archive.
This correction does not replace the SDK or change the earlier library repairs.

## Bounded source-width shaping reuse (2026-10-22)

After the neutral-text repair below, run `rebuild-qt-width-cache.py` with the same
source, prefix, compiler and archiver arguments and a separate output directory.
It requires the corrected `305e8946...` QtGui and the pinned original Qt 6.10.1
`qtextlayout.cpp`. Only `qtextlayout.cpp.obj` changes; the other 248 members remain
byte-identical, including the earlier DirectWrite and lazy-layout repairs.
The resulting archive SHA-256 is
`da639aa199209f24342ae2a4ef52037b5eca877bac04c1599d1386e3fcd80dd3`
when built at `/tmp/texworks-x64/writing-polish/qt-width-cache-v2`.

The application enables glyph retention only for source documents up to 2,097,152
UTF-16 units. Reuse is scoped to the exact document during synchronous sidebar
width changes and requires unchanged block text; content, format, font and IME
changes invalidate retained shaping before lazy reflow, including off-screen blocks.
Larger documents keep normal Qt layout.
Use the native builder's archive override for pixel, navigation and cadence
validation before adoption. Preserve the prior archive and all earlier repairs.
The active SDK hash and exact recipe are recorded in `docs/dependency-provenance.json`.

## Rendering policy update (2026-10-19)

Opaque source and UI surfaces no longer force `NoSubpixelAntialias`; they keep
Windows subpixel coverage and vertical hinting. Translucent popup windows retain
the explicit grayscale request. This changes the application's font policy, not
the validated QtGui archive below, and never changes system ClearType settings.

## Neutral text edges on Windows (2026-10-18)

Keep the established Windows DirectWrite backend. Qt 6.10.1 otherwise ignores
`QFont::NoSubpixelAntialias` when choosing the raster glyph cache. After the
existing x64 text-layout repairs, `rebuild-qt-neutral-text.py` selects the existing
A8 path for this request and preserves it in size/DPI clones. It preserves ARGB
color fonts and explicit `NoAntialias`; it does not change system ClearType.
The FreeType platform alternative failed isolated exit validation and is not used.

```text
python3 .github/actions/setup-windows/rebuild-qt-neutral-text.py \
 --source /tmp/maturity-qt/qt-qtbase-7f6d629 \
 --prefix /usr/lib/mxe/usr/x86_64-w64-mingw32.static/qt6 \
 --compiler /usr/lib/mxe/usr/bin/x86_64-w64-mingw32.static-g++ \
 --archiver /usr/lib/mxe/usr/bin/x86_64-w64-mingw32.static-ar \
 --output /tmp/texworks-x64/neutral-text
```

The tool requires the prior corrected QtGui (`f8c846b6...`) and pinned original
DirectWrite source, handles the MinGW COM exception-specification compatibility,
and produces a separate archive. Only `qwindowsfontenginedirectwrite.cpp.obj`
changes; the other 248 members are byte-identical. Test through the native builder's
`--qtgui-archive` override before adopting it, preserving the prior archive.
The corrected archive SHA-256 is
`305e89466a1db119eba26633b3784d1f3690183c09c102a56bd0ac3f56ed7911`.
Current adoption, exact compile arguments, source hashes and all other dependency
hashes are recorded in `docs/dependency-provenance.json`. Do not replace this
archive with the prior QtGui after adopting the neutral-text repair.

## x64 migration candidate

The stable x86 release was frozen on 2026-10-09 before changing the target.
Build the x64 candidate with **x86_64-w64-mingw32.static**, retaining Qt
**6.10.1**, Poppler **26.01.0**, Hunspell **1.7.2**, Lua **5.4.6**, and GCC
**15.2.0**. Do not replace either corrected dependency with an unpatched build.
The mutex definitions below apply to both Poppler archives on x64 as well;
the window-object repair script accepts the x64 compiler, archiver and prefix.

`.github/actions/setup-windows/build-research-x64.py` configures the existing
Release profile and static components, checks the pointer size at configure
time and verifies the stripped executable's AMD64/PE32+ header. It creates a
candidate only: it never installs the binary, changes associations or writes
user settings. Keep the static PDF encoding/font data beside the executable.
Native x64 tests include `architecture-native-test`, which reserves 3 GiB of
virtual address space but commits only one page, plus the existing data-safety,
compilation, collaboration, references and UI cases. Use the same independent
documents and idle system for repeated x86/x64 performance comparisons.

Final hashes, measurements and installation status belong in the migration
validation report; a successful build alone does not establish a stable release.

Use the pinned 2026-01-09 MXE environment rather than a moving `latest` bundle.
Build its separate target prefix, without overwriting the frozen x86 archives:

```text
make MXE_TARGETS=x86_64-w64-mingw32.static JOBS=6 -j1 qt6-qtbase qt6-qttools qt6-qtsvg qt6-qttranslations qt6-qt5compat poppler-qt6 hunspell lua
```

Install the corrected Poppler recipe before this command. After Qt has built,
use `rebuild-qt-window.py` with the x64 prefix/compiler/archiver, preserve the
original archive and adopt the verified corrected archive. Then build with:

```text
python .github/actions/setup-windows/build-research-x64.py --source <source> --build <separate-x64-build> --output <candidate-folder>
```

The existing x86 dependency hashes below identify the frozen release; hashes
for the independently compiled x64 archives must be recorded separately.

The 2026-10-09 maturity build keeps Qt **6.10.1**, Poppler **26.01.0**,
GCC **15.2.0**, and the existing **i686-w64-mingw32.static** target. It adds no
runtime dependency. Do not rebuild this release with the uncorrected dependency
archives: doing so reintroduces the long-PDF memory leak and native-window leak.

## Poppler

Use the existing `.github/actions/setup-windows/mxe/poppler*.mk` recipes and
patches. The common recipe now passes both of these definitions when compiling
Poppler and its Qt wrapper:

```
-D_GTHREAD_USE_MUTEX_INIT_FUNC -D_GTHREAD_USE_RECURSIVE_MUTEX_INIT_FUNC
```

MinGW's statically initialized ordinary and recursive C++ mutexes otherwise
retain their native winpthread storage after destruction in this toolchain.
The definitions select explicit initialization and destruction. Both versions
are needed because Poppler's dictionaries also use recursive mutexes.

The validated archives have SHA-256:

| Archive | SHA-256 |
| --- | --- |
| libpoppler.a | e8b8a3c6181197f196c98b1b8f4a683b35fac40f61512a990c573d6db04edfa6 |
| libpoppler-qt6.a | ad63cc0ac016896b4ed04245d5c545903615e36282bf2de7fbfdd1072003e6e3 |

## Unicode Poppler resources (x64)

The x64 archive also requires `mxe/poppler-2-utf8-paths.patch`. Poppler 26.01.0
used an ANSI module filename, locale-dependent filesystem conversions, and a
three-byte-only UTF-8 file decoder on Windows. Chinese executable/resource
directories could abort during initialization; supplementary characters could
prevent resource files from opening.

`.github/actions/setup-windows/rebuild-poppler-paths.py` repairs the already
mutex-corrected archive in a separate directory. Pass the unmodified 26.01.0
source, its configured Ninja build, x64 prefix, x64 compiler/archiver and an
output directory. It verifies the pinned source/archive hashes and both mutex
definitions, replaces only `GlobalParams.cc.obj` and `gfile.cc.obj`, and checks
every other archive member byte for byte. Keep the original archive before
adopting a candidate verified with `build.py --poppler-archive` and the Unicode
path, CJK, Dingbats, extraction and rendering regressions. Exact hashes and
compiler arguments are in `docs/dependency-provenance.json`.

## Qt Windows platform plugin

Download the unmodified official [qtbase v6.10.1 source](https://github.com/qt/qtbase/tree/v6.10.1).
The GitHub tag archive used in validation has SHA-256
`f49407de8e1d879fa2b3517eec0aa1bd89abcee621970237706c62511b1291a4`.
Use `.github/actions/setup-windows/rebuild-qt-window.py` with these arguments:

```
--source <unmodified qtbase source directory>
--prefix /usr/lib/mxe/usr/i686-w64-mingw32.static/qt6
--compiler /usr/lib/mxe/usr/bin/i686-w64-mingw32.static-g++
--archiver /usr/lib/mxe/usr/bin/i686-w64-mingw32.static-ar
--output <separate output directory>
```

The script verifies the exact source and installed header version, compiles
only `qwindowswindow.cpp`, and replaces only its member in a copy of the
existing static `libqwindows.a`. It explicitly destroys `hwndTitlebar` before
destroying the owner HWND. Without this release, ordinary windows leave an
unparented `_q_titlebar` window behind; changing the expanded-client-area flag
can also detach that helper. The installed library is never overwritten by
the script. Back it up before adopting the verified output.

The reproduced archive has SHA-256
`3517e664f9260527cf95851e3ad038f3e4badacce586eb40e772765287b29315`.
The original archive was
`caad6bda2905c41ca71ff66f01dd8c3da7cbe29ff7cb603055c92a253d2ff4a8`.
The validated source patch and compiler arguments are recorded by the script.

## Application build and validation

The Windows install must also retain `share/poppler` and `share/fonts` from
`res/research/pdf-support`. Poppler resolves these relative to the executable.
The official Poppler 0.4.12 encoding data supplies CJK character maps; the
single URW Dingbats substitute supplies glyphs that Windows Wingdings cannot
render correctly. These are static data, not additional runtime libraries.
Their source hashes and redistribution notices are included alongside them.
Omitting them reproduces four existing backend regressions: missing CID font
names, blank legacy CJK text/rendering, and blank unembedded Dingbats glyphs.

Continue using the existing CMake/Ninja application build. Corrected Poppler
archives belong in the target's `lib` directory; the Qt plugin belongs in
`qt6/plugins/platforms`. A PE link with `--no-insert-timestamp` followed by
`strip --strip-unneeded` permits repeatable comparison of unchanged candidates.

Native regression test sources, measurements, screenshots, build hashes and
both dependency versions are retained separately in the maturity validation
directory. The reusable native cases now live in `unit-tests/research-native`;
their README describes isolated execution. `WITH_TESTS=ON` also registers
`test_ResearchWorkflow` and `test_ResearchDependencies` with CTest. They are not
shipped as application components. The original
installed executable and settings are kept in a rollback directory. Source,
candidate and installed executable are checked independently before delivery.

### Qt 6.10.1 text layout repair (2026-10-12)

`rebuild-qt-text-layout.py` repairs the pinned original QtGui archive separately.
In `layoutBlock`, a paragraph with zero layout lines must be laid out even if it
is outside a partial change range. Otherwise syntax highlighting during initial
lazy layout can mark unseen suffix paragraphs finished with zero height. Only
`qtextdocumentlayout.cpp.obj` changes; 248 other members are byte-identical.
The original/corrected archive hashes and exact compiler/moc commands are in
`docs/dependency-provenance.json`. The existing Windows-platform and Poppler
repairs remain required. Preserve the original archive before adopting the repair.
CMake enables `TW_QT_SAFE_LAZY_LAYOUT` only for the recorded corrected QtGui hash;
an unknown archive retains the synchronous full-layout workaround.

Example (using a prefix with the pinned original QtGui archive):

```sh
python3 .github/actions/setup-windows/rebuild-qt-text-layout.py \
  --source /tmp/maturity-qt/qt-qtbase-7f6d629 \
  --prefix /usr/lib/mxe/usr/x86_64-w64-mingw32.static/qt6 \
  --compiler /usr/lib/mxe/usr/bin/x86_64-w64-mingw32.static-g++ \
  --archiver /usr/lib/mxe/usr/bin/x86_64-w64-mingw32.static-ar \
  --moc /usr/lib/mxe/usr/x86_64-pc-linux-gnu/qt6/libexec/moc \
  --output /tmp/texline-qt-layout-repair
```

For a controlled negative regression, `research-native/build.py --qtgui-archive`
can link a private copy of the original archive without replacing dependencies.
This intentionally bypasses the CMake hash guard and must not be installed.

`--incremental-edits` retains that repair and adds opt-in bounded layout for
small edits in source documents of at least 1,000,000 UTF-16 characters. Text
revision distinguishes equal-length replacements from highlight-only updates.
Continuation reuses unchanged line geometry; width changes invalidate cached
lines, and a nonempty dirty range ensures progress at position zero. A distant
cursor request still lays out the requested range synchronously. PDF/rich-text
documents retain the existing behavior. CMake enables `TW_QT_LAZY_SOURCE_EDITS`
only for archive `f8c846b6eb56123a4f7eb3b4be4e7c986b150650514fe117d195903a34af5690`;
the earlier archive remains supported for initial-load repair only. Rebuild
from the pinned original archive in a separate prefix and retain a rollback
before adopting it. Exact provenance is in `qtSourceEditLayoutRepair` in the
dependency manifest. Only the same one archive member changes.

`--incremental-edits --source-shape-cache` additionally memoizes exact HarfBuzz
results for eligible ASCII runs in JetBrains Mono NL source documents.
This remains experimental: extended-paper layout and persistent script editing
regressed in the paired acceptance run. Current builds use the previously
validated `f8c846b6eb56123a4f7eb3b4be4e7c986b150650514fe117d195903a34af5690`
incremental-layout archive, retaining the cache reproducer and its evidence.
It preserves shaping, retains all context consumed by pinned HB 12.2 and verifies keys, retains font-engine
identity, and bounds the GUI-thread cache to 4 MiB. Complex text, other fonts
and explicit font features take the original path. The pinned archive is
`d4a3ee2512fbe91d03d3b8d46900a8490f2f4ffb31bcd383fe897da3d2745299`; both
lazy-layout guards accept it. Rebuild from the pinned original archive using
the host moc above and `--harfbuzz-include` pointing to the MXE HarfBuzz headers.
Two members change; 247 other members remain identical. Full provenance is
`qtSourceShapeCacheRepair`. Keep the previous archive for rollback.

The cache keeps ten UTF-16 units per side (covering HB 12.2's five codepoints),
uses a context-local item offset, and replays only relative glyph indices. Both
the generator and application CMake check the pinned HarfBuzz archive; the
patched engine also checks the header version. Context audits include varying
paragraph offsets and surrogate pairs. Rapid focus reversals cancel the current
snapshot and expose the final layout immediately; isolated changes retain the
180 ms animation.

Requests within 500 ms of the last mode change expose the final layout without
starting another snapshot, even if the old veil has already cleared. Tests
for an isolated animation wait beyond that interval before triggering it.
