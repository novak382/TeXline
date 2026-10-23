#!/usr/bin/env python3
from pathlib import Path
import subprocess,hashlib,json,shutil,argparse
p=argparse.ArgumentParser(description="Preserve shaping only during scoped source-width changes; never installs dependencies.")
p.add_argument("--source",type=Path,required=True);p.add_argument("--prefix",type=Path,required=True);p.add_argument("--output",type=Path,required=True);p.add_argument("--compiler",required=True);p.add_argument("--archiver",required=True)
p.add_argument("--base-archive",type=Path,help="Preserved pre-width-cache QtGui; defaults to the prefix archive")
a=p.parse_args()
source=a.source.resolve()
prefix=a.prefix.resolve()
output=a.output.resolve();output.mkdir(parents=True,exist_ok=True)
original=a.base_archive.resolve() if a.base_archive else prefix/'lib/libQt6Gui.a'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
assert sha(original)=='305e89466a1db119eba26633b3784d1f3690183c09c102a56bd0ac3f56ed7911'
cpp=source/'src/gui/text/qtextlayout.cpp'
assert sha(cpp)=='df6fc9652eea6f8ff033fefd08e15bb012f5fd68ffb3e9edabb12966ccd41b88'
text=cpp.read_text()
text=text.replace('#include <qthread.h>','#include <qthread.h>\n#include <qcoreapplication.h>\n#include <qvariant.h>')
needle='    d->invalidate();\n    d->clearLineData();\n    d->itemize();\n    d->layoutData->layoutState = QTextEngine::InLayout;'
assert text.count(needle)==1
replacement='''    // TeXline scopes this guard to a synchronous width-only change of its
    // source document. All content, font, format and IME edits retain Qt's
    // normal invalidation path. Never reuse shaping in another document.
    const auto *application = QCoreApplication::instance();
    const bool widthOnly = d->cacheGlyphs && d->layoutData && d->block.isValid()
        && application && application->property("_texlineWidthRelayoutDocument").toULongLong()
            == qulonglong(reinterpret_cast<quintptr>(d->block.document()))
        && d->layoutData->string == d->block.text();
    if (!widthOnly)
        d->invalidate();
    else {
        d->minWidth = 0;
        d->maxWidth = 0;
    }
    d->clearLineData();
    d->itemize();
    d->layoutData->layoutState = QTextEngine::InLayout;'''
text=text.replace(needle,replacement)
needle='void QTextLayout::clearLayout()\n{\n    d->clearLineData();\n}'
assert text.count(needle)==1
text=text.replace(needle,'''void QTextLayout::clearLayout()
{
    // Document font, option and format changes clear every block before Qt's
    // lazy reflow. Discard retained shaping now, including blocks not yet on
    // screen. Only the application's exact width-only scope may retain it.
    const auto *application = QCoreApplication::instance();
    const bool widthOnly = d->block.isValid() && application
        && application->property("_texlineWidthRelayoutDocument").toULongLong()
            == qulonglong(reinterpret_cast<quintptr>(d->block.document()));
    if (d->cacheGlyphs && !widthOnly)
        d->invalidate();
    d->clearLineData();
}''')
patched=output/cpp.name;patched.write_text(text)
includes=[prefix/'include',cpp.parent]
for module in ['QtCore','QtGui']:includes += [prefix/'include'/module,prefix/'include'/module/'6.10.1',prefix/'include'/module/'6.10.1'/module]
includes += [prefix.parent/'include/harfbuzz']
defines=['NDEBUG','QT_NO_DEBUG','QT_STATIC','QT_BUILD_GUI_LIB','QT_NO_CAST_FROM_ASCII','QT_NO_FOREACH','QT_BUILDING_QT','UNICODE','_UNICODE','WIN32','QT_CORE_LIB','QT_GUI_LIB','_GTHREAD_USE_MUTEX_INIT_FUNC','_GTHREAD_USE_RECURSIVE_MUTEX_INIT_FUNC']
obj=output/(cpp.name+'.obj');compiler=a.compiler;ar=a.archiver
command=[compiler,'-std=c++17','-O2',*['-D'+x for x in defines],*['-I'+str(x) for x in includes],'-c',str(patched),'-o',str(obj)]
subprocess.run(command,check=True);archive=output/'libQt6Gui.a';shutil.copy2(original,archive)
members=subprocess.check_output([ar,'t',str(original)],text=True).splitlines();assert members.count(obj.name)==1
subprocess.run([ar,'r',str(archive),str(obj)],check=True)
assert subprocess.check_output([ar,'t',str(archive)],text=True).splitlines()==members
for member in members:
 if member!=obj.name:assert subprocess.check_output([ar,'p',str(original),member])==subprocess.check_output([ar,'p',str(archive),member]),member
record=dict(originalSha256=sha(original),correctedSha256=sha(archive),originalSourceSha256=sha(cpp),patchedSourceSha256=sha(patched),replacedMember=obj.name,unchangedMembers=len(members)-1,compileCommand=command,qtVersion="6.10.1",scopedWidthOnly=True,lazyFontOptionFormatInvalidation=True)
(output/'provenance.json').write_text(json.dumps(record,indent=2));print(json.dumps(record))
