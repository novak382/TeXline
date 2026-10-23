# 1.0.0 SDK update

The base kit `TeXline-2026.10.20-preview.1-RebuildKit.tar.xz` has SHA-256
`aa5a11c6e9a5dffc64650448cdcfddb0a71fe79988eb6c4073c7f4c99c48ec6d`.
It contains cached sources and recipes for all recorded dependencies.

Extract it into a new directory, then extract `TeXline-1.0.0-SDK-Update.tar.xz`
over the same directory. Verify both downloaded archives using SHA256SUMS first.
The update changes only `usr/x86_64-w64-mingw32.static/qt6/lib/libQt6Gui.a`,
from `305e89466a1db119eba26633b3784d1f3690183c09c102a56bd0ac3f56ed7911`
to `89c7117fd080bd5761998392e1a33d294c16618ddf0be271dfc89f9057999d63`.

`texline-sdk-update/` contains the complete modified Qt text sources, pinned
originals, hashes, licenses and a standalone reconstruction script. The base
kit supplies the rest of the Qt module sources and SDK. The two changed archive
members are `qtextdocumentlayout.cpp.obj` and `qtextlayout.cpp.obj`; all other
members are checked against the base. See the update's README for reconstruction.

This delivery supplies sources and fixed SDK inputs; it does not certify an
independent cold rebuild of all dependencies or byte identity across compilers.
