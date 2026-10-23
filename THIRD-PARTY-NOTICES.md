# Third-party notices

TeXline is derived from TeXworks (Stefan Löffler, Jonathan Kew, Charlie
Sharpsteen and the contributors named in the source). Original copyright
headers and the GPL-2.0-or-later grant are retained. TeXline additions in this
release are offered under GPL-3.0-or-later by Jieyang Hu. The combined Qt 6
distribution is conveyed under GPLv3. This does not relicense independent
fonts, icons, datasets or images.

The distribution includes unmodified upstream license texts under `LICENSES`.
The RebuildKit contains the cached source archives and MXE recipe/patch snapshot;
`LICENSES/dependencies/source-index.json` maps each archive to its SHA-256 and
extracted license files. That inventory includes build tools and unused SDK
components as well as libraries linked into the application.

| Component | Version/source | Terms and notices |
| --- | --- | --- |
| TeXworks / QtPDF | upstream base 7951fd8 plus this source snapshot | GPL-2.0-or-later; original source headers and COPYING |
| Qt base, declarative, compatibility, SVG, tools, translations, image formats | 6.10.1, with recorded Qt repairs | per-file Qt grants; combined distribution selects GPLv3; complete module LICENSES retained |
| Poppler and Qt wrapper | 26.01.0 with mutex and Unicode path repairs | upstream GPL notices, source and patches in kit |
| Hunspell | 1.7.2 | upstream alternative licenses preserved; GPL-compatible grant used |
| Lua | 5.4.6 | upstream MIT license and copyright |
| FreeType, HarfBuzz, GLib, gettext/libintl, libiconv, PCRE2, brotli | versions identified by the kit archive names | original alternative/per-file grants and notices in dependencies |
| zlib, bzip2, libpng, JPEG, OpenJPEG, TIFF, WebP, lcms2, xz, zstd, JasPer, libmng | versions identified by the kit archive names | original component grants and notices in dependencies |
| GCC runtime / MinGW-w64 | GCC 15.2.0 / MinGW-w64 13.0.0 | original runtime exceptions, headers, copyright and source retained |
| SyncTeX | bundled upstream source | MIT-style notices in modules/synctex |
| JetBrains Mono NL | bundled font files | SIL Open Font License; res/research/JetBrainsMono-OFL.txt |
| Microsoft VS Code Codicons | pinned commit in resource NOTICE | CC BY 4.0, Microsoft attribution and adaptation notice retained |
| Tango and specialized TeXworks/QtPDF icons | bundled original assets | original asset READMEs and notices preserved in source |
| Poppler encoding maps | data 0.4.12 | Adobe and Glyph & Cog notices beside share/poppler |
| URW Dingbats substitute | original PFB renamed d050000l.pfb | independent GPLv2 font, original COPYING and README beside share/fonts |
| Introduction examples and reused materials | research-scenes revision | source/rights notes in resources/onboarding/sources.md and sources.json |
| Inno Setup | 6.7.3 | Jordan Russell / Martijn Laan; original Inno Setup license in LICENSES/Inno-Setup.txt |

Windows system fonts and the TeX distribution are not redistributed. For
individual library choices, the original license texts and per-file grants
control; this table is an inventory, not a replacement license. The captured
SDK includes additional build components whose licenses remain in their source
archives even when they do not appear in this summary table.

## Source delivery

Keep the Source ZIP, base RebuildKit and 1.0.0 SDK Update available with any binary distribution. A
hash or a link to the original unmodified project does not replace the modified
corresponding source. Do not publish the Setup on its own before completing the
release checklist in `docs/LICENSING.md`.
