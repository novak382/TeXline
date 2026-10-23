#!/usr/bin/env python3
"""Repair Qt 6.10.1 text layout; optionally bound TeXline source edit work.

Creates a separate QtGui archive, never overwrites the installed dependency.
All archive members other than qtextdocumentlayout.cpp.obj must remain identical.
"""
from pathlib import Path
import argparse, hashlib, json, shutil, subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--source', type=Path, required=True)
p.add_argument('--prefix', type=Path, required=True)
p.add_argument('--compiler', required=True)
p.add_argument('--archiver', required=True)
p.add_argument('--moc', required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--incremental-edits', action='store_true', help='Opt-in bounded relayout of small edits in large source documents')
p.add_argument('--minimum-edit-characters', type=int, default=1000000, help='Source size at which small edits use bounded layout')
p.add_argument('--source-shape-cache', action='store_true', help='Memoize exact ASCII shaper output for the bundled NL source font')
p.add_argument('--harfbuzz-include', type=Path, help='Override the target Harfbuzz include directory')
p.add_argument('--bounded-source-timers', action='store_true', help='Bound background layout batches for large source documents; preserve foreground range requests')
p.add_argument('--display-line-spacing', action='store_true', help='Opt-in source line spacing without changing document formats or undo history')
a = p.parse_args()
if not 16384 <= a.minimum_edit_characters <= 2147483647: p.error('--minimum-edit-characters must be at least 16384')
if a.source_shape_cache and not a.incremental_edits: p.error('--source-shape-cache requires --incremental-edits')
if a.bounded_source_timers and (not a.incremental_edits or a.source_shape_cache): p.error('--bounded-source-timers requires --incremental-edits without --source-shape-cache')
if a.display_line_spacing and not a.bounded_source_timers: p.error('--display-line-spacing requires the existing bounded-source-timers repair')
source, prefix, output = a.source.resolve(), a.prefix.resolve(), a.output.resolve()
original = prefix / 'lib/libQt6Gui.a'
layout = source / 'src/gui/text/qtextdocumentlayout.cpp'
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
expected_archive = 'da639aa199209f24342ae2a4ef52037b5eca877bac04c1599d1386e3fcd80dd3' if a.bounded_source_timers else 'd1e5ad44cf3bb58c6f3f263d03b3674613c0f94f90cb251084f8a7ebcbe3d2ce'
if a.display_line_spacing: expected_archive = 'd663e0e33b94b18406e26aefbbed15b2e496649e9374df1cfbc8af5aee715005'
if sha(original) != expected_archive:
    raise SystemExit('Expected the pinned original QtGui archive; do not re-patch an installed repair')
if sha(layout) != 'cc0f7e16da5e34b09aef15a0ddc40f742478515c36b6e0dc7bfd316061f44dbf':
    raise SystemExit('Expected unmodified Qt 6.10.1 text layout source')
if '6.10.1' not in (prefix / 'include/QtCore/qtcoreversion.h').read_text():
    raise SystemExit('Expected Qt 6.10.1 headers')
output.mkdir(parents=True, exist_ok=True)
needle = '    if (layoutStruct->fullLayout\n        || (blockPosition + blockLength > layoutFrom && blockPosition <= layoutTo)'
replacement = '''    if (layoutStruct->fullLayout
        // A partial change can interrupt the initial lazy layout. Paragraphs
        // outside its range have no reusable lines yet; do not give them zero
        // height and mark the document finished as if they had been laid out.
        || tl->lineCount() == 0
        || (blockPosition + blockLength > layoutFrom && blockPosition <= layoutTo)'''
text = layout.read_text()
if text.count(needle) != 1: raise SystemExit('Unexpected layoutBlock implementation')
patched = output / layout.name
text = text.replace(needle, replacement)
if a.incremental_edits:
    needle = '''    if (fullLayout) {
        d->contentHasAlignment = false;'''
    replacement = '''    // TeXline source documents opt into the same bounded layout steps used
    // for loading. A fully laid-out long document otherwise walks its entire
    // suffix synchronously for every keystroke near the beginning.
    const bool sourceDocument = document()->property("_texline_lazy_edits").toBool();
    const int sourceRevision = document()->revision();
    // Equal-length replacement is still a text edit. Highlight-only dirty
    // notifications leave the document revision unchanged.
    const bool textEdited = oldLength != length
        || (sourceDocument && document()->property("_texline_layout_revision").toInt() != sourceRevision);
    if (sourceDocument)
        document()->setProperty("_texline_layout_revision", sourceRevision);
    if (!fullLayout && smallChange && textEdited && documentLength >= 1000000
        && d->currentLazyLayoutPosition == -1
        && sourceDocument)
        d->currentLazyLayoutPosition = from;

    if (fullLayout) {
        d->contentHasAlignment = false;'''
    if text.count(needle) != 1: raise SystemExit('Unexpected documentChanged implementation')
    replacement = replacement.replace('documentLength >= 1000000', 'documentLength >= ' + str(a.minimum_edit_characters))
    text = text.replace(needle, replacement)
    needle = '''    while (currentLazyLayoutPosition != -1
           && currentLazyLayoutPosition < position) {
        const_cast<QTextDocumentLayout *>(q_func())->doLayout(currentLazyLayoutPosition, 0, INT_MAX - currentLazyLayoutPosition);
    }'''
    replacement = '''    // A requested distant paragraph must be available synchronously. Lay out
    // that requested range in one pass, rather than restarting from a nearby
    // checkpoint for every 1,000 characters. Timer steps keep their own budget.
    const int previousStep = lazyLayoutStepSize;
    const bool reuseUnchangedLines = document->property("_texline_lazy_edits").toBool();
    if (reuseUnchangedLines)
        lazyLayoutStepSize = qMax(lazyLayoutStepSize, position - currentLazyLayoutPosition);
    while (currentLazyLayoutPosition != -1
           && currentLazyLayoutPosition < position) {
        // Changed paragraphs were cleared by documentChanged; the zero-line
        // repair lays them out. Existing suffix lines only need new positions.
        const_cast<QTextDocumentLayout *>(q_func())->doLayout(currentLazyLayoutPosition, 0,
            reuseUnchangedLines ? 1 : INT_MAX - currentLazyLayoutPosition);
    }
    lazyLayoutStepSize = previousStep;'''
    if text.count(needle) != 1: raise SystemExit('Unexpected ensureLayoutedByPosition implementation')
    text = text.replace(needle, replacement)
    needle = '    fd->oldContentsWidth = newContentsWidth;'
    replacement = '''    // A width change invalidates every cached line, including the suffix
    // not reached by this bounded step. Font changes already clear their
    // changed range through documentChanged. Never reuse old-width lines.
    if (layoutStruct.fullLayout && f == document->rootFrame()
        && document->property("_texline_lazy_edits").toBool()) {
        for (QTextBlock block = document->firstBlock(); block.isValid(); block = block.next())
            block.clearLayout();
    }
    fd->oldContentsWidth = newContentsWidth;'''
    if text.count(needle) != 1: raise SystemExit('Unexpected layoutFrame width invalidation')
    text = text.replace(needle, replacement)
if a.bounded_source_timers:
    needle = '''        if (d->currentLazyLayoutPosition != -1)
            d->layoutStep();'''
    replacement = '''        if (d->currentLazyLayoutPosition != -1) {
            // Source reflow must yield to input and animation between batches.
            // Only timer-driven continuation is bounded. A requested cursor,
            // paint range, document size or print layout remains synchronous.
            const bool boundedSource = document()->property("_texline_lazy_edits").toBool()
                && document()->characterCount() >= 1000000;
            const int previousStep = d->lazyLayoutStepSize;
            if (boundedSource)
                d->lazyLayoutStepSize = qMin(d->lazyLayoutStepSize, 8000);
            d->layoutStep();
            if (boundedSource)
                d->lazyLayoutStepSize = qMax(previousStep, d->lazyLayoutStepSize);
        }'''
    if text.count(needle) != 1: raise SystemExit('Unexpected layout timer implementation')
    text = text.replace(needle, replacement)
if a.display_line_spacing:
    needle = '''void QTextDocumentLayoutPrivate::layoutBlock(const QTextBlock &bl, int blockPosition, const QTextBlockFormat &blockFormat,
                                             QTextLayoutStruct *layoutStruct, int layoutFrom, int layoutTo, const QTextBlockFormat *previousBlockFormat)
{
    Q_Q(QTextDocumentLayout);
    if (!bl.isVisible())
        return;'''
    replacement = needle + '''
    QTextBlockFormat displayFormat = blockFormat;
    // A source editor's visual leading is not a text edit. Override only the
    // local layout copy: preserve block formats, revision and undo/redo records.
    const qreal sourceLineSpacing = document->property("_texline_display_line_spacing").toReal();
    if (document->property("_texline_lazy_edits").toBool()
        && qIsFinite(sourceLineSpacing) && sourceLineSpacing >= 50 && sourceLineSpacing <= 500)
        displayFormat.setLineHeight(sourceLineSpacing, QTextBlockFormat::ProportionalHeight);'''
    if text.count(needle) != 1: raise SystemExit('Unexpected source block format layout entry')
    text = text.replace(needle, replacement)
    needle = 'getLineHeightParams(blockFormat, line, scaling,'
    if text.count(needle) != 2: raise SystemExit('Unexpected block line-height paths')
    text = text.replace(needle, 'getLineHeightParams(displayFormat, line, scaling,')
patched.write_text(text)
includes = [prefix / 'include', source / 'src/gui/text']
for module in ['QtCore', 'QtGui']:
    includes += [prefix / 'include' / module, prefix / 'include' / module / '6.10.1',
                 prefix / 'include' / module / '6.10.1' / module]
defines = ['NDEBUG', 'QT_NO_DEBUG', 'QT_STATIC', 'QT_BUILD_GUI_LIB', 'QT_NO_CAST_FROM_ASCII',
    'QT_NO_FOREACH', 'QT_BUILDING_QT', 'UNICODE', '_UNICODE', 'WIN32', 'QT_CORE_LIB', 'QT_GUI_LIB',
           '_GTHREAD_USE_MUTEX_INIT_FUNC', '_GTHREAD_USE_RECURSIVE_MUTEX_INIT_FUNC']
flags = ['-D' + d for d in defines] + ['-I' + str(i) for i in includes]
moc_command = [a.moc, *flags, str(source / 'src/gui/text/qtextdocumentlayout_p.h'),
               '-o', str(output / 'moc_qtextdocumentlayout_p.cpp')]
subprocess.run(moc_command, check=True)
obj = output / 'qtextdocumentlayout.cpp.obj'
command = [a.compiler, '-std=c++17', '-O2', *flags, '-c', str(patched), '-o', str(obj)]
subprocess.run(command, check=True)
extra_objects = []
if a.source_shape_cache:
    from source_shape_cache import patch_source_shape_cache
    engine = source / 'src/gui/text/qtextengine.cpp'
    if sha(engine) != 'dabac335f4ee4b5607148bbb2691502c72960571bba99d148d1e08ca90406e05':
        raise SystemExit('Expected unmodified pinned text engine')
    patched_engine = output / engine.name
    patched_engine.write_text(patch_source_shape_cache(engine.read_text()))
    engine_obj = output / 'qtextengine.cpp.obj'
    hb_include = a.harfbuzz_include or prefix.parent / 'include/harfbuzz'
    hb_archive = hb_include.parent.parent / 'lib/libharfbuzz.a'
    if sha(hb_archive) != '07bbf496e89676dbad3cf40d84faf8ac87553f2299f1d1f6e2c51629976cac82':
        raise SystemExit('Expected the pinned HarfBuzz 12.2.0 archive')
    engine_command = [a.compiler, '-std=c++17', '-O2', *flags, '-I' + str(hb_include), '-c', str(patched_engine), '-o', str(engine_obj)]
    subprocess.run(engine_command, check=True)
    extra_objects.append(engine_obj)
archive = output / original.name
shutil.copy2(original, archive)
members = subprocess.check_output([a.archiver, 't', str(original)], text=True).splitlines()
assert all(members.count(item.name) == 1 for item in [obj, *extra_objects])
subprocess.run([a.archiver, 'r', str(archive), *map(str, [obj, *extra_objects])], check=True)
assert subprocess.check_output([a.archiver, 't', str(archive)], text=True).splitlines() == members
unchanged = 0
for member in members:
    if member in {item.name for item in [obj, *extra_objects]}: continue
    assert subprocess.check_output([a.archiver, 'p', str(original), member]) == subprocess.check_output([a.archiver, 'p', str(archive), member]), member
    unchanged += 1
record = dict(qtVersion='6.10.1', incrementalEdits=a.incremental_edits, minimumEditCharacters=a.minimum_edit_characters, originalSourceSha256=sha(layout), patchedSourceSha256=sha(patched),
              originalSha256=sha(original), correctedSha256=sha(archive), unchangedMembers=unchanged,
              replacedMember=obj.name, compileCommand=command, mocCommand=moc_command)
record['sourceShapeCache'] = a.source_shape_cache
record['boundedSourceTimers'] = a.bounded_source_timers
record['displayLineSpacing'] = a.display_line_spacing
if a.bounded_source_timers: record.update(backgroundBatchCharacters=8000,backgroundMinimumCharacters=1000000,foregroundRangeRequestsUnchanged=True)
if a.source_shape_cache:
    record.update(engineOriginalSourceSha256=sha(engine), enginePatchedSourceSha256=sha(patched_engine),
                  engineCompileCommand=engine_command, replacedMembers=[obj.name, engine_obj.name], cacheMaxCostBytes=4*1024*1024, harfbuzzVersion="12.2.0", harfbuzzArchiveSha256=sha(hb_archive), contextUtf16UnitsPerSide=10)
(output / 'provenance.json').write_text(json.dumps(record, indent=2))
print(json.dumps(record, indent=2))
