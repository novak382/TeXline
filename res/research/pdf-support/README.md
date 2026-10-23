# Portable PDF compatibility data

These static assets repair rendering and text extraction for legacy PDFs whose
fonts or Asian character maps are not embedded. They add no runtime library,
network access, background process, preference, or toolbar control. Standard
LaTeX-generated embedded-font PDFs continue to use their own fonts.

Install `poppler/` as `<exe directory>/share/poppler/` and `fonts/` as
`<exe directory>/share/fonts/`. Poppler 26.01.0 already searches these paths.
The Windows CMake install includes both directories. No machine-wide font is
installed, and the editor's JetBrains Mono / Microsoft YaHei UI is unchanged.

## Encoding maps

Official Poppler data 0.4.12:
https://poppler.freedesktop.org/poppler-data-0.4.12.tar.gz

Archive SHA-256:
`c835b640a40ce357e1b83666aabd95edffa24ddddd49b8daff63adb851cdab74`

The cMap data retains the Adobe redistribution notices in `COPYING.adobe`.
The other encoding tables retain the Glyph & Cog GPLv2 notice and `COPYING.gpl2`.
The source download is retained in the validation directory. Tables are copied
without alteration, and all character-map collections are included; this does
not special-case the PDF used by the regression test.

## Single base-symbol font

The URW Base 35 original `pfb/uzdr.pfb` is copied byte-for-byte as
`fonts/d050000l.pfb`, the standard substitute name Poppler searches for.
It fixes blank Zapf Dingbats glyphs when Windows Wingdings is not a compatible
substitute. The source archive is available at:
https://mirrors.ctan.org/fonts/urw/base35.zip

Source archive SHA-256:
`07abaf13d6dbff68b884a877c91c9692283562273adc2d52a4bf9bfdd90668fd`

Font SHA-256:
`1ed716566691399aaee420b07fb18ce0affbd883091862c68184997a8d6f1a7a`

The original GPLv2 `COPYING` and `README.base35` accompany the font. Only this
45,955-byte substitute is redistributed, not the other Base 35 fonts.
