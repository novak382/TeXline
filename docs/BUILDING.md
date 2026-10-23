# Rebuild TeXline v1.0.0 for Windows x64

The release consists of the public Source ZIP, the base RebuildKit tar.xz,
the 1.0.0 SDK Update tar.xz, Setup, portable ZIP and SHA256SUMS. The kit contains the captured corrected MXE SDK, its recipe
snapshot and cached upstream source archives. Verify SHA-256 before extraction.
Use Ubuntu 24.04 / WSL2 with Python 3, CMake, Ninja, tar, xz and standard build
tools. The SDK is a Linux build toolchain, not a Windows runtime requirement.

## Fixed-SDK application rebuild

Extract the Source ZIP to a new directory. Extract the RebuildKit to a dedicated
MXE directory. Do not overwrite an existing toolchain. The kit has paths such
as `usr/x86_64-w64-mingw32.static` and `pkg` at its root.

```sh
mkdir -p /tmp/texline-mxe
tar -xJf TeXline-2026.10.20-preview.1-RebuildKit.tar.xz -C /tmp/texline-mxe
tar -xJf TeXline-1.0.0-SDK-Update.tar.xz -C /tmp/texline-mxe
python3 tools/relocate-sdk.py /tmp/texline-mxe
python3 tools/build-preview.py --mxe /tmp/texline-mxe \
  --build /tmp/texline-application-build --output /tmp/texline-candidate
```

**The base kit alone is insufficient for 1.0.0.** The SDK Update replaces
QtGui with the exact archive used for this binary and includes the complete
modified text-layout sources and a reconstruction script. See
[SDK-UPDATE](SDK-UPDATE.md).

**The base kit alone is insufficient for 1.0.0.** The SDK Update replaces
QtGui with the exact archive used for this binary and includes the complete
modified text-layout sources and a reconstruction script. See
[SDK-UPDATE](SDK-UPDATE.md).

Some MXE metadata retains its original `/usr/lib/mxe` prefix. Relocate metadata
with `tools/relocate-sdk.py` before building in another path. The archive files
and compiled libraries remain byte-identical. The release report states the
actual tested prefix and whether the relocated SDK was used.

The builder checks the recorded corrected Qt/Poppler/HarfBuzz archives before
configuration. It emits an AMD64 PE32+ executable, an application build record
and the external introduction pack. Use `tools/release.py payload` to assemble
PDF data, notices and examples. Build Setup on Windows with Inno Setup 6.7.3:

```powershell
python tools/release.py payload --exe <candidate.exe> --output out/v1.0.0/payload
ISCC.exe win32/texline.iss
```

ISCC supports `/DPayloadDir=<absolute path>` and `/DReleaseDir=<absolute path>`.
Compiler download: https://github.com/jrsoftware/issrc/releases/tag/is-6_7_3 .
Verify its signature and release hash. Its `/PORTABLE=1` setup mode avoids
system installation of the compiler. An installed TeX distribution is needed
only for the writing examples and functional compilation tests.

## Corresponding sources and dependency rebuilds

`pkg/` in the RebuildKit contains the upstream source archives. `src/`, `ext/`,
`plugins/` and the root makefiles preserve the MXE recipe snapshot. The public
source retains `.github/actions/setup-windows/rebuild-*.py`, the Poppler patches
and `docs/dependency-provenance.json`; these identify the extra Qt and Poppler
repairs. The cached Qt module sources include the pinned original files required
by the repair scripts. No source is replaced by a hash alone.

To experiment with a from-zero dependency build, use an independent prefix and
the saved recipes, selecting `MXE_TARGETS=x86_64-w64-mingw32.static` and the
gcc15 plugin. Apply the documented Poppler recipe definitions and the Qt window,
incremental-layout and DirectWrite repairs. Their scripts intentionally refuse
unrecognized inputs. A rebuilt archive may require comparison and a new audited
fingerprint; do not bypass the guard and install it. This release does not
claim that the complete cold dependency build has been independently reproduced.

`BUILD_RESEARCH_WINDOWS.md` in the Source ZIP contains the
historical repair narrative. The public `dependency-provenance.json`, repair
scripts, complete source archives and recipe snapshot are the portable inputs.
Public automation is manual only and requires both archive URLs and SHA-256 values;
it does not publish releases or invent a download endpoint.
