# Contributing to TeXline

Start with [BUILDING](docs/BUILDING.md). The supported preview target is Windows
x64 with the recorded corrected MXE/Qt/Poppler SDK. Other platforms inherited
from TeXworks are not certified by this release.

Keep changes small and include a reproducer for a functional defect. Preserve
the original TeX document, undo/redo, recovery, external-edit review, complete
build publication, SyncTeX, and source/PDF reading positions. Dynamic TeX
structures must fall back to ordinary source editing when uncertain.

Do not submit manuscripts, private settings, build caches or executables. Native
regressions must use independent fixtures, private settings and an inactive
desktop. Synthetic input does not certify physical IME or multi-monitor use.
See `unit-tests/research-native/README.md` for the relevant cases.

Include what changed, why, and what was actually tested. Keep copyright and
asset attribution intact. Contributions intended for inclusion must be under
terms compatible with this GPL distribution. Use [Issues](https://github.com/novak382/TeXline/issues) for reproducible bugs
and pull requests for reviewed patches.
