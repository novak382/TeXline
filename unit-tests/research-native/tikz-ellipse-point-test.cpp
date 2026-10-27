// Ordinary circles and ellipses: dependent geometry, real PGF and native draft transactions.
#include "ActualAuthoringRecording.h"
#include "ResearchAuthoring.cpp"
using namespace Research;
int main(int argc,char **argv){
  const auto report=argument(1);wchar_t desktop[256]{};DWORD needed=0;
  if(!GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()),UOI_NAME,desktop,sizeof(desktop),&needed)||QString::fromWCharArray(desktop)!=qEnvironmentVariable("TEXLINE_TEST_DESKTOP")||!QString::fromWCharArray(desktop).startsWith("TeXlineValidation-"))return 2;
  argc=1;QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
  TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
  QTimer::singleShot(50,&app,[&]{
    const auto folder=QFileInfo(report).absolutePath();QJsonArray checks;int failures=0;
    auto check=[&](const QString &name,bool pass){checks.append(QJsonObject{{"name",name},{"passed",pass}});failures+=!pass;put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"devicePixelRatio",qApp->primaryScreen()->devicePixelRatio()}}).toJson());};
    auto locate=[](const Tikz::Scene &s,const QString &id){for(int n=0;n<s.items.size();++n)if(s.items[n].id==id)return n;return -1;};
    auto point=[&](const Tikz::Scene &s,const QString &id){const int n=locate(s,id);return n>=0?s.items[n].position:QPointF(qQNaN(),qQNaN());};
    auto close=[](const QPointF &a,const QPointF &b){return std::isfinite(a.x())&&std::isfinite(b.x())&&QLineF(a,b).length()<1e-5;};
    auto curve=[](const Tikz::Scene &s){for(int n=0;n<s.items.size();++n)if(s.items[n].kind==Tikz::Kind::Ellipse&&s.items[n].command=="draw")return n;return -1;};
    check("private inactive desktop",qEnvironmentVariable("TEXLINE_TEST_DESKTOP").startsWith("TeXlineValidation-"));
    const QString original="\\begin{tikzpicture}\n% Keep this ordinary ellipse.\n\\draw[black] (0,0) ellipse (2cm and 1cm);\n\\end{tikzpicture}";
    auto scene=Tikz::parse(original);const auto first=Tikz::pointOnCurve(scene,0,30);
    check("ordinary ellipse supports existing Point on curve",Tikz::pointCurve(scene,0)&&!first.isEmpty());if(first.isEmpty()){app.exit(1);return;}
    scene=Tikz::parse(first);check("source keeps ellipse and comment with no hidden data",!scene.partial&&scene.coordinateSafe&&first.contains("ellipse")&&first.contains("% Keep this ordinary ellipse.")&&!first.contains("plot"));
    check("first point has recoverable radius and center relationship",scene.functions.size()==2&&close(point(scene,"A"),{std::sqrt(3.),.5})&&Tikz::curveForPoint(scene,locate(scene,"A"))==curve(scene));
    scene=Tikz::parse(Tikz::pointOnCurve(scene,curve(scene),150));check("second point reuses radius declarations",scene.functions.size()==2&&close(point(scene,"B"),{-std::sqrt(3.),.5}));
    scene=Tikz::parse(Tikz::construct(scene,"segment",{locate(scene,"A"),locate(scene,"B")}));
    scene=Tikz::parse(Tikz::construct(scene,"midpoint",{locate(scene,"A"),locate(scene,"B")}));
    scene=Tikz::parse(Tikz::construct(scene,"point",{},{0,3}));
    scene=Tikz::parse(Tikz::construct(scene,"foot",{locate(scene,"D"),locate(scene,"A"),locate(scene,"B")}));
    check("ordinary constructions use ellipse points",close(point(scene,"C"),{0,.5})&&close(point(scene,"E"),{0,.5}));
    scene=Tikz::parse(Tikz::resized(scene,curve(scene),{6,4}));check("resize updates points, chord midpoint and foot",!scene.partial&&close(point(scene,"A"),{3*std::sqrt(3.)/2,1})&&close(point(scene,"C"),{0,1})&&close(point(scene,"E"),{0,1}));
    const auto resized=scene;scene=Tikz::parse(Tikz::moved(scene,curve(scene),{.4,-.3}));check("ellipse drag carries center and bound points",close(point(scene,"A"),point(resized,"A")+QPointF(.4,-.3))&&close(point(scene,"C"),{.4,.7}));
    const auto chain=scene;
    if(!scene.valid||curve(scene)<0||locate(scene,"A")<0){check("construction chain remains usable",false);app.exit(1);return;}
    check("dependent ellipse cannot be deleted",Tikz::removed(scene,curve(scene)).isEmpty());
    check("dependent center cannot be deleted",Tikz::removed(scene,locate(scene,scene.items[curve(scene)].coordinates[0].reference)).isEmpty());
    const auto seam=Tikz::nearestCurveParameter(scene,curve(scene),QPointF(.4,-.3)+QPointF(3,.01),359);
    check("closed curve drag crosses angle seam continuously",seam>360&&seam<361);
    const auto dragged=Tikz::parse(Tikz::moved(scene,locate(scene,"A"),{.3,.4}));const auto d=point(dragged,"A")-QPointF(.4,-.3);
    check("point drag remains on ellipse",!dragged.source.isEmpty()&&qAbs(d.x()*d.x()/9+d.y()*d.y()/4-1)<1e-8);
    int marker=-1;for(int n=0;n<scene.items.size();++n)if(scene.items[n].coordinates.value(0).reference=="A"&&scene.items[n].labelMovable)marker=n;
    const auto label=Tikz::parse(Tikz::labelMoved(scene,marker,{.3,-.2}));check("letter movement leaves bound geometry unchanged",!label.source.isEmpty()&&close(point(label,"A"),point(scene,"A")));
    const QString circle="\\begin{tikzpicture}\\draw (-1,0) circle (1cm);\\end{tikzpicture}";
    auto onCircle=Tikz::parse(Tikz::pointOnCurve(Tikz::parse(circle),0,0));check("corpus circle center minus one supports moving point",!onCircle.partial&&close(point(onCircle,"A"),{0,0})&&onCircle.functions.size()==1);
    onCircle=Tikz::parse(Tikz::resized(onCircle,curve(onCircle),{4,4}));check("circle uses one shared radius",close(point(onCircle,"A"),{1,0}));
    auto through=Tikz::parse("\\begin{tikzpicture}\\coordinate (O) at (-2,1);\\coordinate (R) at (0,1);\\end{tikzpicture}");through=Tikz::parse(Tikz::construct(through,"circleThrough",{0,1}));
    const auto throughSource=Tikz::pointOnCurve(through,curve(through),90);check("constructed circle accepts bound point",!throughSource.isEmpty());
    through=Tikz::parse(throughSource);check("constructed point uses ordinary calc rotation",!through.partial&&close(point(through,"A"),{-2,3})&&through.functions.isEmpty());
    if(!throughSource.isEmpty()){through=Tikz::parse(Tikz::moved(through,locate(through,"R"),{1,0}));check("moving defining radius point updates attached point",close(point(through,"A"),{-2,4}));}
    check("invalid angle changes nothing",Tikz::curveParameter(chain,locate(chain,"A"),qQNaN()).isEmpty());
    check("unsupported rotated ellipse is not silently flattened",Tikz::pointOnCurve(Tikz::parse(QString(original).replace("black","rotate=30")),0,40).isEmpty());
    QVector<Tikz::Scene> probes{chain,onCircle,through};
    for(const auto &scale:QStringList{"scale=2","xscale=-1,yscale=1","xscale=2,yscale=.7"}){
      auto source=QString("\\begin{tikzpicture}[%1]\\coordinate (O) at (-1,.3);\\coordinate (R) at (1,1.3);\\end{tikzpicture}").arg(scale);auto base=Tikz::parse(source);base=Tikz::parse(Tikz::construct(base,"circleThrough",{0,1}));base=Tikz::parse(Tikz::pointOnCurve(base,curve(base),90));
      check("constructed circle binding supports "+scale,base.valid&&!base.partial&&locate(base,"A")>=0);probes.append(base);
    }
    auto unknown=chain.source;unknown.insert(unknown.indexOf("\\end{tikzpicture}"),"\\draw (0,0) to[out=0,in=180] ({texlineellipse1x(0)},0);\n");const auto partial=Tikz::parse(unknown);check("unknown radius use is never changed by resize",partial.partial&&Tikz::resized(partial,curve(partial),{5,4}).isEmpty());
    auto textUse=chain.source;textUse.insert(textUse.indexOf("\\end{tikzpicture}"),"\\node at (0,0) {\\pgfmathparse{texlineellipse1x(0)}\\pgfmathresult};\n");const auto referenced=Tikz::parse(textUse);check("recognized label with hidden function call protects shared radius",Tikz::resized(referenced,curve(referenced),{5,4}).isEmpty());
    check("local radius options cannot hide transforms",Tikz::pointOnCurve(Tikz::parse("\\begin{tikzpicture}\\draw (0,0) ellipse[x radius=2cm,y radius=1cm,rotate=30];\\end{tikzpicture}"),0,30).isEmpty());
    check("inherited path translation is not duplicated",Tikz::pointOnCurve(Tikz::parse(QString(original).replace("\\begin{tikzpicture}","\\begin{tikzpicture}[every path/.style={xshift=1cm}]")),0,30).isEmpty());
    const auto linkedAxes=Tikz::parse("\\begin{tikzpicture}[declare function={r(\\t)=1;}]\\draw (0,0) ellipse ({r(0)*1cm} and {r(0)*1cm});\\end{tikzpicture}");check("one shared imported radius cannot silently ignore height",Tikz::resized(linkedAxes,0,{2,3}).isEmpty());
    auto transformed=first;transformed.replace("\\draw[black]","\\draw[black,xscale=2]");const auto unsafe=Tikz::parse(transformed);check("imported local transform cannot claim a false binding",Tikz::curveForPoint(unsafe,locate(unsafe,"A"))<0);
    const auto preambleUse=Tikz::parse(first,"\\tikzset{external/.style={minimum width={texlineellipse1x(0)*1cm}}}");check("external style context protects shared radius",Tikz::resized(preambleUse,curve(preambleUse),{6,3}).isEmpty());
    for(const auto &scale:QStringList{"xscale=2,yscale=.7","xscale=-1.5,yscale=2","x={(2cm,1cm)},y={(-1cm,2cm)}"}){
      auto source=QString(original).replace("\\begin{tikzpicture}","\\begin{tikzpicture}["+scale+"]").replace("black","black,xshift=5mm,yshift=-2mm");
      auto base=Tikz::parse(source);const auto expected=QPointF((.5+std::sqrt(3.))*base.canvasScale.x(),(.5-.2)*base.canvasScale.y());
      base=Tikz::parse(Tikz::pointOnCurve(base,0,30));check("physical radius and local shift survive "+scale,!base.partial&&close(point(base,"A"),expected));probes.append(base);
    }
    for(const auto &nodeStyle:QStringList{"every node/.style={xshift=1cm}","every coordinate node/.style={yshift=1cm}","nodes={xshift=1cm}","every node/.style args={#1}{xshift=#1}"}){
      check("inherited coordinate displacement preserves source "+nodeStyle,Tikz::pointOnCurve(Tikz::parse(original,"\\tikzset{"+nodeStyle+"}"),0,30).isEmpty());
    }
    check("pure node font and color keep circle binding available",!Tikz::pointOnCurve(Tikz::parse(original,"\\tikzset{every node/.style={font=\\small,text=black}}"),0,30).isEmpty());
    const auto shiftedCenter=Tikz::parse("\\begin{tikzpicture}[scale=2]\\coordinate[xshift=.5cm](O)at(0,0);\\draw(O)circle(1cm);\\end{tikzpicture}");
    check("modeled imported coordinate shifts retain real circle center",!Tikz::pointOnCurve(shiftedCenter,1,30).isEmpty()&&close(point(shiftedCenter,"O"),{.5,0}));
    auto custom=Tikz::parse("\\begin{tikzpicture}[x={(2cm,1cm)},y={(-1cm,2cm)}]\\draw(0,0)ellipse(2cm and 1cm);\\draw(-3cm,.4cm)--(3cm,.4cm);\\end{tikzpicture}");
    custom=Tikz::parse(Tikz::pointOnCurve(custom,0,30));int line=-1;for(int n=0;n<custom.items.size();++n)if(custom.items[n].kind==Tikz::Kind::Line)line=n;
    check("custom-axis ellipse retains intersection operation after binding",Tikz::intersectionPath(custom,curve(custom)));
    const auto crossed=Tikz::pathIntersection(custom,curve(custom),line,0);check("custom-axis ellipse binding and intersection coexist",!crossed.isEmpty());
    if(!crossed.isEmpty()){custom=Tikz::parse(crossed);const auto enlarged=Tikz::resized(custom,curve(custom),{5,3});check("resizing shared radii updates bound point and intersection",!enlarged.isEmpty());if(!enlarged.isEmpty())probes.append(Tikz::parse(enlarged));}
    QString tex="\\documentclass{article}\n\\usepackage{tikz}\n\\usetikzlibrary{calc,intersections}\n\\newdimen\\ProbeX\\newdimen\\ProbeY\n\\begin{document}\n";QVector<QPointF> expected;
    for(auto &s:probes){if(!s.valid)continue;QString measures;for(const auto &i:s.items)if(i.kind==Tikz::Kind::Point&&i.located){const int n=expected.size();expected.append(i.position);measures+=QString("\\pgftransformreset\\pgfextractx\\ProbeX{\\pgfpointanchor{%1}{center}}\\pgfextracty\\ProbeY{\\pgfpointanchor{%1}{center}}\\typeout{BOUND%2=\\the\\ProbeX,\\the\\ProbeY}\n").arg(i.id).arg(n);}auto code=s.source;code.replace("\\end{tikzpicture}",measures+"\\end{tikzpicture}");tex+=code+"\\clearpage\n";}
    tex+="\\end{document}";put(folder+"/ellipse-points.tex",tex.toUtf8());QProcess compile;compile.setWorkingDirectory(folder);compile.start("C:/texlive/2023/bin/windows/pdflatex.exe",{"-interaction=nonstopmode","-halt-on-error","ellipse-points.tex"});check("ordinary TikZ compiles with real local TeX",compile.waitForFinished(45000)&&compile.exitCode()==0);
    const auto log=QString::fromUtf8(bytes(folder+"/ellipse-points.log"));QJsonArray measures;
    for(int n=0;n<expected.size();++n){const auto m=QRegularExpression(QString("BOUND%1=([+-]?[0-9.]+)pt,([+-]?[0-9.]+)pt").arg(n)).match(log);const QPointF actual(m.captured(1).toDouble()*2.54/72.27,m.captured(2).toDouble()*2.54/72.27);const double error=QLineF(actual,expected[n]).length();check(QString("PGF anchor %1 agrees within 0.1 mm").arg(n),m.hasMatch()&&error<.01);measures.append(QJsonObject{{"index",n},{"errorCm",error},{"found",m.hasMatch()}});}
    put(folder+"/pgf-measurements.json",QJsonDocument(measures).toJson());
    Tw::Settings().setValue("openPDFwithTeX",false);Tw::Settings().setValue("researchFocusMode",false);
    const QString path=folder+"/paper/main.tex",text="\\documentclass{article}\n\\usepackage{tikz}\n\\begin{document}\nA drawing.\n\\end{document}\n";
    put(path,text.toUtf8());auto *w=new TeXDocumentWindow(path);w->resize(qEnvironmentVariableIsSet("TEXLINE_TEST_HALF")?680:1120,760);w->show();settle();
    if(qEnvironmentVariableIsSet("TEXLINE_TEST_FOCUS")){setFocusMode(true);settle();}
    auto cursor=w->textCursor();cursor.setPosition(text.indexOf("A drawing."));w->editor()->setTextCursor(cursor);openTikz(w,true,{},original);settle();
    auto *dialog=dynamic_cast<TikzDialog*>(w->findChild<QDialog*>("researchTikzDialog"));auto *canvas=dialog?dynamic_cast<TikzCanvas*>(dialog->findChild<QWidget*>("researchTikzCanvas")):nullptr;
    check("native ellipse editor opens",canvas);if(!canvas){app.exit(1);return;}
    auto pointer=[&](QEvent::Type type,const QPointF &p,Qt::MouseButton button,Qt::MouseButtons buttons){QMouseEvent e(type,p,canvas->mapToGlobal(p.toPoint()),button,buttons,Qt::NoModifier);QApplication::sendEvent(canvas,&e);};
    auto click=[&](const QPointF &p){pointer(QEvent::MouseButtonPress,p,Qt::LeftButton,Qt::LeftButton);pointer(QEvent::MouseButtonRelease,p,Qt::LeftButton,Qt::NoButton);};
    auto *action=dialog->findChild<QAction*>("researchTikzConstruct_curvePoint");check("existing Point on curve action",action);if(!action){app.exit(1);return;}
    dialog->grab().save(folder+"/ellipse-before.png");action->trigger();key(canvas,Qt::Key_Escape);check("Esc before point is source neutral",canvas->model().source==original);
    action->trigger();click(canvas->screen({std::sqrt(3.),.5}));settle();
    check("canvas click binds and selects newly inserted point",locate(canvas->model(),"A")>=0&&canvas->selected==locate(canvas->model(),"A"));if(locate(canvas->model(),"A")<0){app.exit(1);return;}
    const auto added=canvas->model().source;key(canvas,Qt::Key_Z,Qt::ControlModifier);check("one draft undo removes point and shared radii",canvas->model().source==original);key(canvas,Qt::Key_Y,Qt::ControlModifier);check("redo restores exact source",canvas->model().source==added);
    canvas->choose(locate(canvas->model(),"A"));auto *parameter=dialog->findChild<QLineEdit*>("researchTikzParameter");check("bound point has angle and no free XY",parameter->isVisibleTo(dialog)&&!dialog->findChild<QDoubleSpinBox*>("researchTikzX")->isVisibleTo(dialog));
    parameter->setFocus();parameter->selectAll();key(parameter,Qt::Key_1,Qt::NoModifier,"invalid");key(parameter,Qt::Key_Return);check("invalid angle remains recoverable",parameter->text()=="invalid"&&canvas->model().source==added);
    parameter->selectAll();key(parameter,Qt::Key_1,Qt::NoModifier,"359");key(parameter,Qt::Key_Return);canvas->setFocus();const auto beforeDrag=canvas->model().source;
    canvas->fit();settle();
    const auto start=canvas->screen(point(canvas->model(),"A")),end=canvas->screen({1.98,.12});
    pointer(QEvent::MouseButtonPress,start,Qt::LeftButton,Qt::LeftButton);pointer(QEvent::MouseMove,end,Qt::NoButton,Qt::LeftButton);key(canvas,Qt::Key_Escape);pointer(QEvent::MouseButtonRelease,end,Qt::LeftButton,Qt::NoButton);check("Esc during drag restores angle and source",canvas->model().source==beforeDrag);
    const auto frames=folder+"/actual-ellipse-drag";QDir().mkpath(frames);dialog->grab().save(frames+"/00.png");
    pointer(QEvent::MouseButtonPress,start,Qt::LeftButton,Qt::LeftButton);for(int n=1;n<=8;++n){pointer(QEvent::MouseMove,start+(end-start)*n/8.,Qt::NoButton,Qt::LeftButton);settle(18);dialog->grab().save(frames+QString("/%1.png").arg(n,2,10,QLatin1Char('0')));}pointer(QEvent::MouseButtonRelease,end,Qt::LeftButton,Qt::NoButton);settle();const auto q=point(canvas->model(),"A");check("native drag crosses seam on ellipse",canvas->model().items[locate(canvas->model(),"A")].coordinates[0].parameter>360&&qAbs(q.x()*q.x()/4+q.y()*q.y()-1)<1e-7);dialog->grab().save(folder+"/ellipse-after.png");key(canvas,Qt::Key_Z,Qt::ControlModifier);check("one undo restores point drag",canvas->model().source==beforeDrag);
    auto *mode=dialog->findChild<QComboBox*>("researchTikzMode");mode->setCurrentIndex(2);check("native draft preview compiles",waitFor([&]{return dialog->findChild<QLabel*>("researchTikzPreview")->property("researchDraftReady").toBool();},45000));dialog->grab().save(folder+"/ellipse-typeset.png");mode->setCurrentIndex(0);const auto final=canvas->model().source;
    dialog->findChild<QDialogButtonBox*>("researchTikzButtons")->button(QDialogButtonBox::Apply)->click();settle();check("Apply writes ordinary TikZ",w->textDoc()->editorText().contains(final));w->editor()->undo();check("one manuscript undo restores document",w->textDoc()->editorText()==text);w->editor()->redo();
    cursor=w->textCursor();cursor.setPosition(w->textDoc()->editorText().indexOf("ellipse"));w->editor()->setTextCursor(cursor);editResearchObject(w);settle();dialog=dynamic_cast<TikzDialog*>(w->findChild<QDialog*>("researchTikzDialog"));check("applied ellipse reopens",dialog);if(dialog){canvas=dynamic_cast<TikzCanvas*>(dialog->findChild<QWidget*>("researchTikzCanvas"));check("reopen restores relationship from source",canvas&&Tikz::curveForPoint(canvas->model(),locate(canvas->model(),"A"))>=0);key(dialog,Qt::Key_Escape);settle();}
    w->editor()->undo();check("Cancel preserves original file and text",bytes(path)==text.toUtf8()&&w->textDoc()->editorText()==text);
    openTikz(w,true,{},linkedAxes.source);settle();dialog=dynamic_cast<TikzDialog*>(w->findChild<QDialog*>("researchTikzDialog"));if(dialog){canvas=dynamic_cast<TikzCanvas*>(dialog->findChild<QWidget*>("researchTikzCanvas"));canvas->choose(0);auto *height=dialog->findChild<QDoubleSpinBox*>("researchTikzHeight");height->setFocus();key(height,Qt::Key_Up);settle();check("rejected shared-radius height restores actual displayed value",qAbs(height->value()-2)<1e-8&&canvas->model().source==linkedAxes.source);check("rejected height gives recoverable feedback",!dialog->findChild<QLabel*>("researchTikzNotice")->text().isEmpty());key(dialog,Qt::Key_Escape);settle();}else check("linked-radius editor opens",false);
    const auto shiftedSource=Tikz::pointOnCurve(Tikz::parse(QString(original).replace("black","black,xshift=1cm")),0,30);openTikz(w,true,{},shiftedSource);settle();dialog=dynamic_cast<TikzDialog*>(w->findChild<QDialog*>("researchTikzDialog"));if(dialog){canvas=dynamic_cast<TikzCanvas*>(dialog->findChild<QWidget*>("researchTikzCanvas"));const int selected=curve(canvas->model());if(selected>=0){canvas->choose(selected);auto *x=dialog->findChild<QDoubleSpinBox*>("researchTikzX");check("shifted ellipse displays actual center after binding",qAbs(x->value()-1)<1e-8);const auto start=canvas->screen({3,-1}),end=canvas->screen({3.5,-1.5});pointer(QEvent::MouseButtonPress,start,Qt::LeftButton,Qt::LeftButton);pointer(QEvent::MouseMove,end,Qt::NoButton,Qt::LeftButton);pointer(QEvent::MouseButtonRelease,end,Qt::LeftButton,Qt::NoButton);settle();check("shifted ellipse resize handle remains on drawn boundary",qAbs(canvas->model().items[selected].size.width()-5)<.05&&qAbs(canvas->model().items[selected].size.height()-3)<.05);key(canvas,Qt::Key_Z,Qt::ControlModifier);check("shifted resize is one undo",canvas->model().source==shiftedSource);}else check("shifted ellipse remains parsed",false);key(dialog,Qt::Key_Escape);settle();}else check("shifted ellipse editor opens",false);
    w->textDoc()->setModified(false);w->close();app.exit(failures?1:0);
  });return app.exec();
}
