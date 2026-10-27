// Physical coordinate-node offsets, source-preserving edits, and native transactions.
#include "ActualAuthoringRecording.h"
#include "ResearchAuthoring.cpp"
using namespace Research;
int main(int argc,char **argv){
  const auto report=argument(1);wchar_t desktop[256]{};DWORD needed=0;
  if(!GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()),UOI_NAME,desktop,sizeof(desktop),&needed)||QString::fromWCharArray(desktop)!=qEnvironmentVariable("TEXLINE_TEST_DESKTOP")||!QString::fromWCharArray(desktop).startsWith("TeXlineValidation-"))return 2;
  argc=1;QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
  QTimer::singleShot(50,&app,[&]{
    const auto folder=QFileInfo(report).absolutePath();QJsonArray checks;int failures=0;
    auto check=[&](const QString &name,bool pass){checks.append(QJsonObject{{"name",name},{"passed",pass}});failures+=!pass;put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"devicePixelRatio",qApp->primaryScreen()->devicePixelRatio()}}).toJson());};
    auto find=[](const Tikz::Scene &s,const QString &id){for(int n=0;n<s.items.size();++n)if(s.items[n].id==id)return n;return -1;};
    auto pos=[&](const Tikz::Scene &s,const QString &id){int n=find(s,id);return n<0?QPointF(qQNaN(),qQNaN()):s.items[n].position;};
    auto pointsClose=[](const QPointF &a,const QPointF &b){return std::isfinite(a.x())&&std::isfinite(b.x())&&QLineF(a,b).length()<.0001;};
    const QString points="\\coordinate[xshift=5mm,xshift=-1mm,yshift=-2mm] (A) at (1,2);\n\\coordinate[xshift=2mm,yshift=1cm] (B) at (A);\n\\coordinate[xshift=1,yshift=2] (U) at (0,0);\n";
    QVector<Tikz::Scene> probes;
    const QStringList settings{"", "scale=2", "xscale=-1.5,yscale=2", "xscale=2,yscale=.7,x={(2cm,1cm)},y={(-1cm,2cm)}"};
    for(const auto &setting:settings){
      const auto s=Tikz::parse("\\begin{tikzpicture}["+setting+"]\n"+points+"\\end{tikzpicture}");
      check("literal coordinate shifts are recognized "+setting,s.valid&&!s.partial&&s.coordinateSafe&&s.coordinateNodeSafe&&find(s,"A")>=0&&find(s,"B")>=0);
      if(find(s,"A")<0||find(s,"B")<0)continue;
      const auto p=s.axisX+2*s.axisY;
      check("literal coordinate uses physical summed shift "+setting,pointsClose(pos(s,"A"),QPointF(p.x()*s.canvasScale.x(),p.y()*s.canvasScale.y())+QPointF(.4,-.2)));
      check("reference coordinate keeps own physical offset "+setting,pointsClose(pos(s,"B"),pos(s,"A")+QPointF(.2,1)));
      check("unitless shifts are TeX points "+setting,pointsClose(pos(s,"U"),{2.54/72.27,2*2.54/72.27}));
      probes.append(s);
      const QPointF delta(.3,-.4);
      const auto movedA=Tikz::parse(Tikz::moved(s,find(s,"A"),delta));
      check("literal drag retains local options and dependent offset "+setting,pointsClose(pos(movedA,"A"),pos(s,"A")+delta)&&pointsClose(pos(movedA,"B"),pos(s,"B")+delta)&&movedA.source.contains("xshift=5mm,xshift=-1mm,yshift=-2mm"));probes.append(movedA);
      const auto movedB=Tikz::parse(Tikz::moved(s,find(s,"B"),delta));
      check("reference drag moves only the selected coordinate "+setting,pointsClose(pos(movedB,"A"),pos(s,"A"))&&pointsClose(pos(movedB,"B"),pos(s,"B")+delta)&&movedB.source.contains("at (A)"));probes.append(movedB);
      const auto target=pos(s,"B")+QPointF(-.2,.3);const auto handle=Tikz::parse(Tikz::pointMoved(s,find(s,"B"),0,target));
      check("point handle moves alias without moving original "+setting,pointsClose(pos(handle,"A"),pos(s,"A"))&&pointsClose(pos(handle,"B"),target));probes.append(handle);
      const auto built=Tikz::parse(Tikz::construct(s,"midpoint",{find(s,"A"),find(s,"B")}));
      check("new midpoint uses shifted world positions "+setting,pointsClose(pos(built,"C"),(pos(s,"A")+pos(s,"B"))/2));probes.append(built);
    }
    const QString inheritance="\\tikzset{off/.style={xshift=2mm,xshift=3mm,yshift=1mm},every path/.style={xshift=1cm,yshift=-4mm},every node/.style={off,xshift=-1mm},every coordinate node/.style={off,xshift=4mm}}";
    const auto inherited=Tikz::parse("\\begin{tikzpicture}[scale=2]\n\\coordinate[off,xshift=5mm] (A) at (1,2);\n\\coordinate[off,xshift=5mm] (B) at (A);\n\\end{tikzpicture}",inheritance);
    check("inherited path and node shifts remain distinct",pointsClose(pos(inherited,"A"),{6.3,3.5})&&pointsClose(pos(inherited,"B"),{8.6,3.8}));probes.append(inherited);
    const auto inheritedMove=Tikz::parse(Tikz::moved(inherited,find(inherited,"B"),{.2,-.1}),inheritance);
    check("reference writeback does not duplicate inherited shifts",pointsClose(pos(inheritedMove,"A"),pos(inherited,"A"))&&pointsClose(pos(inheritedMove,"B"),pos(inherited,"B")+QPointF(.2,-.1))&&inheritedMove.source.contains("[off,"));probes.append(inheritedMove);
    check("uncompensated defaults cannot create a false midpoint",!inherited.coordinateNodeSafe&&Tikz::construct(inherited,"midpoint",{find(inherited,"A"),find(inherited,"B")}).isEmpty());
    check("existing points can still be connected",!Tikz::construct(inherited,"segment",{find(inherited,"A"),find(inherited,"B")}).isEmpty());
    const auto node=Tikz::parse("\\begin{tikzpicture}[xscale=2,yscale=.7,every path/.style={xshift=.5cm,yshift=-.2cm}]\\node[xshift=.5cm,yshift=-.2cm](N)at(1,2){};\\end{tikzpicture}");
    check("standalone node also inherits enclosing path shift",pointsClose(pos(node,"N"),{3.5,1.06}));probes.append(node);
    const auto center=Tikz::parse("\\begin{tikzpicture}[scale=2]\\coordinate[xshift=.5cm,yshift=-.2cm](O)at(0,0);\\draw(O)circle(1cm);\\end{tikzpicture}");
    const auto bound=Tikz::parse(Tikz::pointOnCurve(center,1,30));
    check("shifted imported center can bind a new point",find(bound,"A")>=0&&pointsClose(pos(bound,"A"),{.5+std::sqrt(3.),.8}));probes.append(bound);
    const auto translated=Tikz::parse(Tikz::moved(bound,1,{.3,-.4}));
    check("circle center drag carries bound point",pointsClose(pos(translated,"O"),pos(bound,"O")+QPointF(.3,-.4))&&pointsClose(pos(translated,"A"),pos(bound,"A")+QPointF(.3,-.4)));probes.append(translated);
    for(const auto &opt:QStringList{"xshift=\\unknown","rotate=30","transform shape","shift={(1,2)}","above=1cm,above=2cm"}){const auto unsupported=Tikz::parse("\\begin{tikzpicture}\\coordinate["+opt+"](A)at(0,0);\\end{tikzpicture}");check("unsupported coordinate geometry remains source-only "+opt,unsupported.partial&&find(unsupported,"A")<0);}
    const auto appended=Tikz::parse("\\begin{tikzpicture}\\coordinate(A)at(0,0);\\end{tikzpicture}","\\tikzset{every coordinate node/.append style={xshift=1cm}}");check("unknown node style handler is not silently ignored",appended.partial&&find(appended,"A")<0);
    const auto modifiedAlias=Tikz::parse("\\begin{tikzpicture}\\coordinate[p](A)at(0,0);\\end{tikzpicture}","\\tikzset{p/.style={xshift=1cm},p/.append style={xshift=2cm}}");check("named style append is not flattened",modifiedAlias.partial&&find(modifiedAlias,"A")<0);
    const auto legacy=Tikz::parse("\\begin{tikzpicture}\\coordinate[p](A)at(0,0);\\end{tikzpicture}","\\tikzstyle{p}=[xshift=1cm]\\tikzset{p/.append style={xshift=2cm}}");check("legacy style cannot overwrite unknown modifier guard",legacy.partial&&find(legacy,"A")<0);
    const auto late=Tikz::parse("\\begin{tikzpicture}\\coordinate(A)at(0,0);\\tikzset{every node/.style={xshift=1cm}}\\coordinate(B)at(0,0);\\end{tikzpicture}");check("time-varying style definitions are never silently edited",!late.coordinateSafe&&Tikz::moved(late,0,{1,0}).isEmpty());
    const auto spatial=Tikz::parse("\\begin{tikzpicture}\\coordinate[xshift=.5cm](A)at(1,2,3);\\coordinate(B)at(A);\\coordinate(C)at(2,3,1);\\end{tikzpicture}");bool spatialOk=true;Tikz::spatialPosition(spatial,find(spatial,"A"),&spatialOk);check("physical offset is not invented as a unique XYZ position",!spatialOk);Tikz::spatialPosition(spatial,find(spatial,"B"),&spatialOk);check("dependent aliases retain spatial ambiguity",!spatialOk&&Tikz::sphere(spatial,find(spatial,"A"),1).isEmpty());
    const auto spatialMoved=Tikz::parse(Tikz::moved(spatial,find(spatial,"A"),{.2,.3}));check("source XYZ still drags while keeping physical offset",pointsClose(pos(spatialMoved,"A"),pos(spatial,"A")+QPointF(.2,.3))&&spatialMoved.source.contains("xshift=.5cm"));probes.append(spatial);probes.append(spatialMoved);
    QString tex="\\documentclass{article}\n\\usepackage{tikz}\n\\usetikzlibrary{calc,intersections}\n\\newdimen\\PX\\newdimen\\PY\n\\begin{document}\n";QVector<QPointF> expected;
    for(const auto &s:probes){if(!s.valid)continue;QString measures;for(const auto &i:s.items)if((i.kind==Tikz::Kind::Point||i.kind==Tikz::Kind::Node)&&i.located&&!i.id.startsWith('#')){int n=expected.size();expected.append(i.position);measures+=QString("\\begingroup\\pgftransformreset\\pgfextractx\\PX{\\pgfpointanchor{%1}{center}}\\pgfextracty\\PY{\\pgfpointanchor{%1}{center}}\\typeout{COORD%2=\\the\\PX,\\the\\PY}\\endgroup\n").arg(i.id).arg(n);}auto src=s.source;src.replace("\\end{tikzpicture}",measures+"\\end{tikzpicture}");tex+="{\n"+s.styleContext+"\n"+src+"\n}\\clearpage\n";}
    tex+="\\end{document}";put(folder+"/coordinates.tex",tex.toUtf8());QProcess compile;compile.setWorkingDirectory(folder);compile.start("C:/texlive/2023/bin/windows/pdflatex.exe",{"-interaction=nonstopmode","-halt-on-error","coordinates.tex"});check("real PGF compiles edited sources",compile.waitForFinished(45000)&&compile.exitCode()==0);
    const auto log=QString::fromUtf8(bytes(folder+"/coordinates.log"));QJsonArray measurements;
    for(int n=0;n<expected.size();++n){const auto m=QRegularExpression(QString("COORD%1=([+-]?[0-9.]+)pt,([+-]?[0-9.]+)pt").arg(n)).match(log);QPointF actual(m.captured(1).toDouble()*2.54/72.27,m.captured(2).toDouble()*2.54/72.27);double error=QLineF(actual,expected[n]).length();check(QString("PGF physical anchor %1 within 0.1mm").arg(n),m.hasMatch()&&error<.01);measurements.append(QJsonObject{{"index",n},{"errorCm",error},{"found",m.hasMatch()}});}
    put(folder+"/pgf-measurements.json",QJsonDocument(measurements).toJson());
    Tw::Settings().setValue("openPDFwithTeX",false);Tw::Settings().setValue("researchFocusMode",false);
    const QString path=folder+"/paper/main.tex",text="\\documentclass{article}\n\\usepackage{tikz}\n\\begin{document}\nDiagram.\n\\end{document}\n";
    const QString original="\\begin{tikzpicture}[xscale=1.2,yscale=.8]\n\\coordinate[xshift=5mm,yshift=-2mm](A)at(-2,0);\n\\coordinate[xshift=2cm,yshift=2cm](B)at(A);\n\\coordinate(C)at(2,-1);\n\\draw(A)--(B)--(C)--cycle;\n\\fill(A)circle(1.2pt)node[below left]{$A$};\n\\fill(B)circle(1.2pt)node[above]{$B$};\n\\fill(C)circle(1.2pt)node[below right]{$C$};\n\\end{tikzpicture}";
    put(path,text.toUtf8());auto *w=new TeXDocumentWindow(path);w->resize(qEnvironmentVariableIsSet("TEXLINE_TEST_HALF")?680:1120,760);w->show();settle();if(qEnvironmentVariableIsSet("TEXLINE_TEST_FOCUS")){setFocusMode(true);settle();}
    auto cursor=w->textCursor();cursor.setPosition(text.indexOf("Diagram."));w->editor()->setTextCursor(cursor);openTikz(w,true,{},original);settle();
    auto *dialog=dynamic_cast<TikzDialog*>(w->findChild<QDialog*>("researchTikzDialog"));auto *canvas=dialog?dynamic_cast<TikzCanvas*>(dialog->findChild<QWidget*>("researchTikzCanvas")):nullptr;
    check("native editor opens",canvas);if(!canvas){app.exit(1);return;}
    canvas->choose(find(canvas->model(),"B"));auto *x=dialog->findChild<QDoubleSpinBox*>("researchTikzX");auto *y=dialog->findChild<QDoubleSpinBox*>("researchTikzY");
    check("inspector shows alias actual physical position",pointsClose({x->value(),y->value()},{.1,1.8}));
    x->setFocus();key(x,Qt::Key_Up);settle();check("X edit moves alias and keeps original point",pointsClose(pos(canvas->model(),"A"),{-1.9,-.2})&&pointsClose(pos(canvas->model(),"B"),{.1+x->singleStep(),1.8}));
    canvas->setFocus();key(canvas,Qt::Key_Z,Qt::ControlModifier);check("inspector edit is one undo",canvas->model().source==original);
    auto pointer=[&](QEvent::Type type,const QPointF &p,Qt::MouseButton button,Qt::MouseButtons buttons){QMouseEvent e(type,p,canvas->mapToGlobal(p.toPoint()),button,buttons,Qt::NoModifier);QApplication::sendEvent(canvas,&e);};
    canvas->fit();const QPointF zoomAt=canvas->rect().center();QWheelEvent zoomOut(zoomAt,canvas->mapToGlobal(zoomAt.toPoint()),QPoint(),QPoint(0,-240),Qt::NoButton,Qt::ControlModifier,Qt::NoScrollPhase,false);QApplication::sendEvent(canvas,&zoomOut);settle();dialog->grab().save(folder+"/coordinate-before.png");const auto start=canvas->screen(pos(canvas->model(),"B")),end=canvas->screen({.8,2.3});
    pointer(QEvent::MouseButtonPress,start,Qt::LeftButton,Qt::LeftButton);pointer(QEvent::MouseMove,end,Qt::NoButton,Qt::LeftButton);key(canvas,Qt::Key_Escape);pointer(QEvent::MouseButtonRelease,end,Qt::LeftButton,Qt::NoButton);check("Esc cancels alias drag without moving source",canvas->model().source==original);
    const auto frames=folder+"/actual-coordinate-drag";QDir().mkpath(frames);dialog->grab().save(frames+"/00.png");pointer(QEvent::MouseButtonPress,start,Qt::LeftButton,Qt::LeftButton);
    for(int n=1;n<=8;++n){pointer(QEvent::MouseMove,start+(end-start)*n/8.,Qt::NoButton,Qt::LeftButton);settle(18);dialog->grab().save(frames+QString("/%1.png").arg(n,2,10,QLatin1Char('0')));}pointer(QEvent::MouseButtonRelease,end,Qt::LeftButton,Qt::NoButton);settle();const auto applied=canvas->model().source;
    check("native alias drag moves selected point only",pointsClose(pos(canvas->model(),"A"),{-1.9,-.2})&&pointsClose(pos(canvas->model(),"B"),{.8,2.3}));for(int n=0;n<canvas->model().items.size();++n)if(!canvas->model().items[n].text.isEmpty())check(QString("recorded label %1 stays in canvas").arg(n),canvas->rect().contains(canvas->labelBounds(n).toAlignedRect()));dialog->grab().save(folder+"/coordinate-after.png");key(canvas,Qt::Key_Z,Qt::ControlModifier);check("one undo restores entire alias drag",canvas->model().source==original);key(canvas,Qt::Key_Y,Qt::ControlModifier);check("redo restores exact alias source",canvas->model().source==applied);
    auto *mode=dialog->findChild<QComboBox*>("researchTikzMode");mode->setCurrentIndex(2);check("native preview compiles current shifted drawing",waitFor([&]{return dialog->findChild<QLabel*>("researchTikzPreview")->property("researchDraftReady").toBool();},45000));dialog->grab().save(folder+"/coordinate-typeset.png");mode->setCurrentIndex(0);
    dialog->findChild<QDialogButtonBox*>("researchTikzButtons")->button(QDialogButtonBox::Apply)->click();settle();check("Apply writes current source",w->textDoc()->editorText().contains(applied));w->editor()->undo();check("one manuscript undo restores document",w->textDoc()->editorText()==text);w->editor()->redo();
    cursor=w->textCursor();cursor.setPosition(w->textDoc()->editorText().indexOf("xshift"));w->editor()->setTextCursor(cursor);editResearchObject(w);settle();dialog=dynamic_cast<TikzDialog*>(w->findChild<QDialog*>("researchTikzDialog"));check("existing drawing reopens",dialog);if(dialog){canvas=dynamic_cast<TikzCanvas*>(dialog->findChild<QWidget*>("researchTikzCanvas"));check("reopen retains source and physical position",canvas&&canvas->model().source==applied&&pointsClose(pos(canvas->model(),"B"),{.8,2.3}));key(dialog,Qt::Key_Escape);settle();}
    check("Cancel restores editor focus",w->editor()->hasFocus());w->editor()->undo();check("Cancel preserves original disk file and document",bytes(path)==text.toUtf8()&&w->textDoc()->editorText()==text);w->textDoc()->setModified(false);w->close();app.exit(failures?1:0);
  });return app.exec();
}
