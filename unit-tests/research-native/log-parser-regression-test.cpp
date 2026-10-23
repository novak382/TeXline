#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include <QFile>
#include <QJSEngine>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTimer>

static QString read(const QString &path) {
    QFile f(path); return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

static QJSValue parse(QJSEngine &engine, const QString &code, const QString &log,
                     int existence, int sort, const QString &custom) {
    engine.globalObject().setProperty("capturedLog", log);
    engine.globalObject().setProperty("existence", existence);
    engine.globalObject().setProperty("sort", sort);
    engine.evaluate("var justLoad=true; var lookups=[]; var TW={fileExists:function(p){lookups.push(p);return existence;}};");
    const QJSValue loaded=engine.evaluate(code);
    if (loaded.isError()) return loaded;
    engine.evaluate("var parser=new LogParser(); var trace=[]; parser.Settings.SortBy=sort;"
                    "parser.setResultAddedCallback(function(r){trace.push(['result',r.Severity,r.File,r.Row,r.Description]);});"
                    "parser.setFileOpenedCallback(function(f){trace.push(['open',f]);});"
                    "parser.setFileClosedCallback(function(f){trace.push(['close',f]);});"
                    "parser.setParseCompletedCallback(function(f,r){trace.push(['complete',f,r.length]);});");
    if (!custom.isEmpty()) {
        const auto changed=engine.evaluate(custom);
        if (changed.isError()) return changed;
    }
    return engine.evaluate("parser.Parse(capturedLog,'C:/论文 (独立副本)/main.tex');"
                           "JSON.stringify({results:parser.Results,html:parser.GenerateReport(),"
                           "table:parser.GenerateReport(true),trace:trace,lookups:lookups});");
}

int main(int argc, char **argv) {
    const QString report=QString::fromLocal8Bit(argv[1]); argc=1;
    TWApp app(argc,argv); app.setQuitOnLastWindowClosed(false);
    QTimer::singleShot(100,&app,[&] {
        const QString old=read(qEnvironmentVariable("TEXLINE_LOG_BASELINE"));
        const QString candidate=read(qEnvironmentVariable("TEXLINE_LOG_SCRIPT"));
        const QString captured=read(qEnvironmentVariable("TEXLINE_LOG_FILE"));
        QJsonArray checks; int failures=0;
        auto check=[&](const QString &name,bool ok){checks.append(QJsonObject{{"name",name},{"passed",ok}});failures+=!ok;};
        check("frozen and candidate parser sources and private log available",!old.isEmpty()&&!candidate.isEmpty()&&!captured.isEmpty()&&old!=candidate);
        const QStringList cases={
            "", "plain progress\n[1] [2]\nOutput written on main.pdf (2 pages).\n",
            "! Undefined control sequence.\nl.12 \\unknown\n             \nnext\n",
            "! LaTeX Error: Something went wrong.\nl.20 broken\ncontext\n",
            "! Emergency stop.\n", "! File ended while scanning use of \\foo.\n",
            "Package demo Warning: First message\n(demo) continued on input line 18.\n",
            "Class article Warning: Check this on input line 7.\n",
            "LaTeX Font Warning: Font unavailable\n(Font) using another on input line 9.\n",
            "LaTeX Warning: Reference `a' on page 2 undefined on input line 30.\n",
            "LaTeX Warning: A warning with\nmore text.\n",
            "pdfTeX warning (ext4): duplicate destination\nignored\n",
            "p\nd\nf\nT\ne\nX\n \nw\na\nr\nn\ni\nn\ng\n: wrapped\ncontinued\n",
            "middle of line pdfTeX warning (ext4): duplicate\nignored\n",
            "Overfull \\hbox (2.0pt too wide) in paragraph at lines 12--14\ncontext (extra parenthesis)\n",
            "Underfull \\hbox (badness 1000) detected at line 19\n",
            "Overfull \\vbox (2pt too high) in alignment at lines 2--4\n",
            "Underfull \\vbox (badness 1000) has occurred while \\output is active\n",
            "Tight \\hbox (badness 3) in paragraph at lines 6--8\ncontext\n",
            "Loose \\hbox (badness 3) detected at line 6\n",
            "Tight \\vbox (badness 3) has occurred while \\output is active\n",
            "> 123.\nl.11 \\showthe\\count0\n",
            "\\endL or \\endR problem (1 missing, 0 extra) in paragraph at lines 8--9\n",
            "! First error.\nLatexmk: applying rule 'pdflatex'...\n! Second error.\n",
            "(./chapter.tex\nLaTeX Warning: Chapter warning on input line 4.\n)\n",
            "(\"C:/论文 (独立副本)/章节.tex\"\n! Chapter error.\n)\n",
            "(C:/texlive/texmf-dist/demo.sty\n(./nested.tex\n! Nested error.\n) )\n",
            "(/usr/share/texmf/demo.sty\n! Unix error.\n)\n",
            "(\\\\server\\paper\\chapter.tex\n! UNC error.\n)\n",
            "(not a filename (extra))\nLaTeX Warning: Safe <&> text.\n",
            "! truncated", "LaTeX Warning: no closing dot", " \t\n\r\n",
            "(./"+QString(76,'a')+"\nwrapped.tex\n! Wrapped file.\n)\n",
            captured
        };
        auto compare=[&](const QString &name,const QString &log,int status,int sort,const QString &custom=QString()){
            QJSEngine a,b; const auto left=parse(a,old,log,status,sort,custom),right=parse(b,candidate,log,status,sort,custom);
            check(name,!left.isError()&&!right.isError()&&left.toString()==right.toString());
        };
        for(int i=0;i<cases.size();++i) for(int status=0;status<3;++status) for(int sort=0;sort<2;++sort)
            compare(QString("diagnostics, HTML, callbacks and file probes: case %1 permission %2 sort %3").arg(i).arg(status).arg(sort),cases[i],status,sort);
        // Deterministic mixed transcripts exercise file-stack and diagnostic ordering.
        quint32 seed=20261013;
        for(int i=0;i<40;++i){QString mixed;for(int j=0;j<8;++j){seed=seed*1664525u+1013904223u;mixed+=cases[seed% (cases.size()-1)]+"\n";}
            compare(QString("seeded mixed transcript %1").arg(i),mixed,i%3,i%2);}
        const QString callback="function(m,f){return new Result(Severity.Warning,f,42,m[1]);}";
        compare("appended custom pattern without hint","CUSTOM: works\n",0,0,
                "parser.Patterns.push({Regex:/^CUSTOM: (.+)\\n/,Callback:"+callback+"});");
        compare("replacement of built-in regex invalidates old hint","CUSTOM: works\n",0,0,
                "parser.Patterns[0].Regex=/^CUSTOM: (.+)\\n/;parser.Patterns[0].Callback="+callback+";");
        compare("case-insensitive replacement is not filtered","custom: works\n",0,0,
                "parser.Patterns[0].Regex=/^CUSTOM: (.+)\\n/i;parser.Patterns[0].Callback="+callback+";");
        compare("multiline replacement is not filtered","prefix\nCUSTOM: works\n",0,0,
                "parser.Patterns[0].Regex=/^CUSTOM: (.+)\\n/m;parser.Patterns[0].Callback="+callback+";");
        compare("custom exec on a built-in regex is not filtered","CUSTOM: works\n",0,0,
                "parser.Patterns[0].Regex.exec=function(s){return /^CUSTOM: (.+)\\n/.exec(s);};parser.Patterns[0].Callback="+callback+";");
        for(const QString &flags:{QString("g"),QString("y")})
            compare("stateful built-in regex remains compatible: "+flags,cases[2]+"plain progress\n"+cases[2],0,0,
                    "parser.Patterns[0].Regex=new RegExp(parser.Patterns[0].Regex.source,'"+flags+"');parser.Patterns[0].Callback="+callback+";");
        QSaveFile file(report); if(file.open(QIODevice::WriteOnly)){file.write(QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"seed",20261013},{"cases",static_cast<int>(cases.size())}}).toJson());file.commit();}
        app.exit(failures?1:0);
    }); return app.exec();
}
