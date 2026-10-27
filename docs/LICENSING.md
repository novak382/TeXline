# License and source delivery

This distribution preserves upstream attribution and the GPL-2.0-or-later grant in
TeXworks source. The Qt 6 combined application is distributed using the GPLv3
option. COPYING retains GPLv2; LICENSES/GPL-3.0.txt supplies GPLv3. New TeXline
additions are GPL-3.0-or-later. Asset-specific terms remain attached to their
respective files. See THIRD-PARTY-NOTICES for the inventory.

## Materials to keep together

1. Source ZIP: exact application source, Qt/Poppler repair generators, patches,
   build scripts, resources, source manifest and notices.
2. Base RebuildKit plus 1.0.0 SDK Update: corrected x64 static SDK, original
   cached dependency sources, MXE recipes, complete changed Qt text sources,
   reconstruction script, and archive/license indexes. Both archives are required.
3. Portable/Setup payload: GPL texts, component license texts, original font
   and map notices, icon attribution and introduction material attributions.
4. SHA256SUMS and release report: identify what was actually delivered and tested.

The source index is extracted from actual archives, not generated from guessed
package licenses. Qt's internal third-party notices remain in its module source
and LICENSES directories. Original TeXworks, SyncTeX and asset notices are not
removed. The cached SDK may contain additional components; the complete cached
source set is included to avoid silently omitting transitive dependencies.

The provided build is an application rebuild against captured corrected
dependencies. A full independent cold rebuild of the toolchain and dependency
archives has not been certified. This is stated as a verification limit; the
source archives, recorded recipes, compiler inputs and modification scripts
are still supplied for inspection and further rebuilding.

## Before any public upload

- Publish Source and RebuildKit with equivalent access alongside binaries.
- Verify the delivered archive hashes, all compiled library source coverage,
  runtime exceptions, independent-asset terms and extracted notice inventory.
- Preserve the identity of modifications and the original authors' notices.
- Check introduction materials against their attached attribution records.
- Choose a real project/support endpoint; no tracker or email is fabricated.
- Resolve any uncovered corresponding-source or dependency-build issue before
  describing the package as ready for unrestricted public distribution.

The [public release](https://github.com/novak382/TeXline/releases/tag/v1.0.0-20261028)
keeps these materials together. The release report records actual checks and
remaining gaps; it is not a blanket legal certification.

References: [GNU GPLv3](https://www.gnu.org/licenses/gpl-3.0.html),
[GNU source distribution FAQ](https://www.gnu.org/licenses/gpl-faq.html),
[Qt 6.10 licensing](https://doc.qt.io/qt-6.10/licensing.html).
