"""Bounded exact HB output cache for pinned TeXline source layouts (GPL-2.0-or-later)."""
HELPER = r"""// Bounded memo of exact HB output. Font references prevent pointer reuse;
// context and all shaping flags are compared again after the hash lookup.
struct TwSourceShape {
 QFontEngine *engine; QString context; int start,length,script,analysis; uint options; bool design,kern,spacing; quintptr language;
 QList<glyph_t> glyphs; QList<QFixed> advances; QList<QFixedPoint> offsets; QList<QGlyphAttributes> attributes; QList<QGlyphJustification> justifications; QList<ushort> clusters;
 explicit TwSourceShape(QFontEngine *e):engine(e){engine->ref.ref();}
 ~TwSourceShape(){if(!engine->ref.deref())delete engine;}
 bool matches(QFontEngine *e,QStringView text,int index,int len,int scriptValue,int analysisValue,uint optionValue,bool designValue,bool kernValue,bool spacingValue,quintptr lang) const {
  return engine==e&&context==text&&start==index&&length==len&&script==scriptValue&&analysis==analysisValue&&options==optionValue&&design==designValue&&kern==kernValue&&spacing==spacingValue&&language==lang;
 }
};
struct TwSourceShapes {QCache<quint64,TwSourceShape> entries{4*1024*1024};QSet<quint64> seen;QPointer<QCoreApplication> application;};
static thread_local TwSourceShapes twSourceShapes;

"""

PREFIX = r"""    auto *application=QCoreApplication::instance();
    const bool memo = application && QThread::currentThread()==application->thread() && block.isValid() && block.document()->property("_texline_lazy_edits").toBool()
        && stringLength <= 4096 && itemLength > 0 && itemLength <= 1024 && fontFeatures.isEmpty()
        && si.analysis.bidiLevel == 0 && (si.analysis.script == QChar::Script_Latin || si.analysis.script == QChar::Script_Common)
        && itemBoundaries.size() == 3 && itemBoundaries[2] == 0 && this->font(si).family() == QStringLiteral("JetBrains Mono NL");
    bool ascii = memo;
    if (ascii) for (int i=0;i<itemLength;++i) if (string[stringBaseIndex+i]<0x20 || string[stringBaseIndex+i]>0x7e){ascii=false;break;}
    // Pinned HB 12.2 keeps at most five Unicode codepoints on each side.
    // Ten UTF-16 units conservatively retain those even with surrogate pairs.
    // Qt stores relative glyph indices; the context-local item offset suffices.
    QStringView memoContext; int memoStart=0;
    quint64 memoKey=0; bool remember=false;
    const quintptr memoLanguage=reinterpret_cast<quintptr>(hb_language_get_default());
    if (ascii) {
        const int begin=qMax(0,stringBaseIndex-10),end=qMin(stringLength,stringBaseIndex+itemLength+10);
        memoContext=QStringView(reinterpret_cast<const QChar*>(string)+begin,end-begin);memoStart=stringBaseIndex-begin;
        if(twSourceShapes.application!=application) {
            twSourceShapes.entries.clear();twSourceShapes.seen.clear();twSourceShapes.application=application;
            QObject::connect(application,&QCoreApplication::aboutToQuit,application,[]{twSourceShapes.entries.clear();twSourceShapes.seen.clear();});
            QObject::connect(application,&QObject::destroyed,[]{twSourceShapes.entries.clear();twSourceShapes.seen.clear();});
        }
        memoKey=qHashMulti(qHashBits(memoContext.data(),memoContext.size()*sizeof(QChar)),reinterpret_cast<quintptr>(fontEngine),memoStart,itemLength,
            int(si.analysis.script),int(si.analysis.flags),uint(option.flags()),option.useDesignMetrics(),kerningEnabled,hasLetterSpacing,memoLanguage);
        auto *cached=twSourceShapes.entries.object(memoKey);
        if(cached&&cached->matches(fontEngine,memoContext,memoStart,itemLength,
                int(si.analysis.script),int(si.analysis.flags),uint(option.flags()),option.useDesignMetrics(),kerningEnabled,hasLetterSpacing,memoLanguage)) {
            const int count=int(cached->glyphs.size()); if(!ensureSpace(count))return 0;QGlyphLayout glyphs=availableGlyphs(&si).mid(0,count);
            std::copy(cached->glyphs.begin(),cached->glyphs.end(),glyphs.glyphs);std::copy(cached->advances.begin(),cached->advances.end(),glyphs.advances);
            std::copy(cached->offsets.begin(),cached->offsets.end(),glyphs.offsets);std::copy(cached->attributes.begin(),cached->attributes.end(),glyphs.attributes);
            std::copy(cached->justifications.begin(),cached->justifications.end(),glyphs.justifications);std::copy(cached->clusters.begin(),cached->clusters.end(),logClusters(&si));
            return count;
        }
        // One-off paragraphs do not allocate a glyph memo. Store only when a
        // full context occurs again; collisions only affect admission, never reuse.
        remember=twSourceShapes.seen.contains(memoKey);if(twSourceShapes.seen.size()>=8192)twSourceShapes.seen.clear();twSourceShapes.seen.insert(memoKey);
    }

"""

STORE = r"""    if(ascii&&remember&&glyphs_shaped>0&&glyphs_shaped<=1024) {
        auto *entry=new TwSourceShape(fontEngine);entry->context=memoContext.toString();
        entry->start=memoStart;entry->length=itemLength;entry->script=int(si.analysis.script);entry->analysis=int(si.analysis.flags);entry->options=uint(option.flags());
        entry->design=option.useDesignMetrics();entry->kern=kerningEnabled;entry->spacing=hasLetterSpacing;entry->language=memoLanguage;
        QGlyphLayout glyphs=availableGlyphs(&si).mid(0,glyphs_shaped);
        entry->glyphs=QList<glyph_t>(glyphs.glyphs,glyphs.glyphs+glyphs_shaped);entry->advances=QList<QFixed>(glyphs.advances,glyphs.advances+glyphs_shaped);
        entry->offsets=QList<QFixedPoint>(glyphs.offsets,glyphs.offsets+glyphs_shaped);entry->attributes=QList<QGlyphAttributes>(glyphs.attributes,glyphs.attributes+glyphs_shaped);
        entry->justifications=QList<QGlyphJustification>(glyphs.justifications,glyphs.justifications+glyphs_shaped);entry->clusters=QList<ushort>(logClusters(&si),logClusters(&si)+itemLength);
        const qsizetype cost=sizeof(TwSourceShape)+128+memoContext.size()*2+glyphs_shaped*QGlyphLayout::SpaceNeeded+itemLength*2;
        twSourceShapes.entries.insert(memoKey,entry,cost);
    }
"""

def patch_source_shape_cache(text):
    text = '#include <hb.h>\n#if HB_VERSION_MAJOR != 12 || HB_VERSION_MINOR != 2 || HB_VERSION_MICRO != 0\n#error Pinned_HarfBuzz_12_2_required_for_context_cache\n#endif\n#include <QCoreApplication>\n#include <QPointer>\n#include <QThread>\n#include <QCache>\n#include <QSet>\n' + text
    marker = 'int QTextEngine::shapeTextWithHarfbuzzNG('
    if text.count(marker) != 1: raise ValueError('Unexpected pinned HB shaper')
    start = text.index(marker)
    text = text[:start] + '\n' + HELPER + text[start:]
    start = text.index(marker)
    body = text.index('{', start) + 1
    text = text[:body] + '\n' + PREFIX + text[body + 1:]
    start = text.index(marker)
    end = text.index('\n}', start)
    function = text[start:end]
    if function.count('    return glyphs_shaped;') != 1: raise ValueError('Unexpected pinned HB return')
    function = function.replace('    return glyphs_shaped;', STORE + '    return glyphs_shaped;')
    return text[:start] + function + text[end:]
