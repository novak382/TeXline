# TeXline

[简体中文](README.zh-CN.md) · [Download 1.0](https://github.com/novak382/TeXline/releases/tag/v1.0.0) · [First session](docs/FIRST-SESSION.md) · [Build](docs/BUILDING.md)

A light, focused Windows x64 LaTeX editor, derived from
[TeXworks](https://github.com/TeXworks/texworks). Write ordinary `.tex` files,
read the PDF beside your text, and move between source and output with SyncTeX.

- **Writing:** multi-file structure, section editing, pinned references and focus mode.
- **Research:** local bibliography search, citation editing, explicit reference context,
  named writing contexts and recoverable local text versions.
- **Beamer:** frame navigation and reordering, notes, expanded preview, and visual
  editing of figures, tables, formula structures and overlay steps.
- **Projects:** review external changes, use local Git, and work with ordinary project files.

The interface supports 简体中文, 繁體中文 and English. Editing is local;
an AI service or cloud account is not required. Compilation needs a separately
installed TeX distribution, such as TeX Live or MiKTeX.

## Start writing

Download `TeXline-1.0.0-Setup-x64.exe` from the
[release page](https://github.com/novak382/TeXline/releases/tag/v1.0.0),
or extract `TeXline-1.0.0-Portable-x64.zip` into a writable folder.
Setup installs for the current user without creating shortcuts or changing
default file associations. Open `TeXline.exe` in the installation folder.

See [installation](docs/INSTALL.md) and try the supplied
[first writing session](docs/FIRST-SESSION.md).
The Windows package is unsigned. Physical IME, foreground animation and
multi-monitor acceptance still need real-user testing; see
[known limitations](docs/KNOWN-LIMITATIONS.md).

## Feedback and contributions

Report reproducible problems in [Issues](https://github.com/novak382/TeXline/issues).
Include the version, Windows display scale and a small shareable example;
remove private text and personal paths. See [feedback](docs/FEEDBACK.md) and
[contributing](CONTRIBUTING.md).

## License and sources

TeXline is an independent TeXworks derivative. Original authors' notices and
the GPL-2.0-or-later source grant are preserved; this combined Qt 6 distribution
uses the GPLv3 option. TeXline additions are GPL-3.0-or-later. Independent assets
and dependencies retain their own terms. See [LICENSE](LICENSE), [COPYING](COPYING),
[third-party notices](THIRD-PARTY-NOTICES.md) and [source delivery](docs/LICENSING.md).

The release includes application source, the base RebuildKit and the 1.0.0 SDK
update with modified Qt sources. **Use both SDK archives** when rebuilding this
version. [Build instructions](docs/BUILDING.md) describe their identities and
the limits of the recorded rebuild verification.
