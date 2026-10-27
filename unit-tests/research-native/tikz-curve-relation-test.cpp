// Standard TikZ curve functions, dependency preservation and native draft transactions.
#include "ActualAuthoringRecording.h"
#include "ResearchAuthoring.cpp"
using namespace Research;
int main(int argc,char **argv){
  const auto report=argument(1);argc=1;QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
  TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
  QTimer::singleShot(50,&app,[&]{
    const auto folder=QFileInfo(report).absolutePath();QJsonArray checks;int failures=0;
    auto check=[&](const QString &name,bool pass){checks.append(QJsonObject{{"name",name},{"passed",pass}});failures+=!pass;put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"devicePixelRatio",qApp->primaryScreen()->devicePixelRatio()}}).toJson());};
    check("private inactive desktop",qEnvironmentVariable("TEXLINE_TEST_DESKTOP").startsWith("TeXlineValidation-"));
    const QString originalPicture=QStringLiteral("\\begin{tikzpicture}\n% Original parametric parabola.\n\\draw[black,domain=-3:3,samples=100] plot (\\x,{\\x*\\x/2});\n\\end{tikzpicture}");
    auto locate=[](const Tikz::Scene &s,const QString &id){for(int n=0;n<s.items.size();++n)if(s.items[n].id==id)return n;return -1;};
    auto point=[&](const Tikz::Scene &s,const QString &id){const int n=locate(s,id);return n>=0?s.items[n].position:QPointF(qQNaN(),qQNaN());};
    auto pointsClose=[](const QPointF &a,const QPointF &b){return QLineF(a,b).length()<1e-5;};
    auto scene=Tikz::parse(originalPicture);const auto first=Tikz::pointOnCurve(scene,0,-1);
    check("first binding promotes only selected plot",!first.isEmpty()&&first.contains("declare function")&&first.contains("% Original parametric parabola."));
    if(first.isEmpty()){app.exit(1);return;}
    scene=Tikz::parse(first);check("source reopens with shared functions and attached point",scene.valid&&!scene.partial&&scene.functions.size()==2&&scene.items.size()==3&&pointsClose(point(scene,"A"),{-1,.5}));
    scene=Tikz::parse(Tikz::pointOnCurve(scene,0,2));check("second point reuses declarations",scene.functions.size()==2&&scene.items.size()==5&&pointsClose(point(scene,"B"),{2,2}));
    scene=Tikz::parse(Tikz::construct(scene,"segment",{locate(scene,"A"),locate(scene,"B")}));
    scene=Tikz::parse(Tikz::construct(scene,"midpoint",{locate(scene,"A"),locate(scene,"B")}));
    check("existing midpoint uses curve endpoints",pointsClose(point(scene,"C"),{.5,1.25}));
    scene=Tikz::parse(Tikz::construct(scene,"point",{},{0,3}));
    scene=Tikz::parse(Tikz::construct(scene,"foot",{locate(scene,"D"),locate(scene,"A"),locate(scene,"B")}));
    const auto chain=scene;
    scene=Tikz::parse(Tikz::curveParameter(scene,locate(scene,"B"),1));
    check("parameter edit retains attached A/B and updates midpoint",pointsClose(point(scene,"A"),{-1,.5})&&pointsClose(point(scene,"B"),{1,.5})&&pointsClose(point(scene,"C"),{0,.5}));
    check("foot follows changed chord",pointsClose(point(scene,"E"),{0,.5}));
    auto changed=Tikz::property(scene,0,"expression","t*t");scene=Tikz::parse(changed);
    check("expression edit patches shared definition and both endpoints",!changed.isEmpty()&&scene.functions.size()==2&&pointsClose(point(scene,"A"),{-1,1})&&pointsClose(point(scene,"B"),{1,1}));
    check("downstream midpoint and foot follow curve expression",pointsClose(point(scene,"C"),{0,1})&&pointsClose(point(scene,"E"),{0,1}));
    const auto beforeMove=scene;scene=Tikz::parse(Tikz::moved(scene,0,{.4,-.7}));
    check("moving shared curve updates attached points",pointsClose(point(scene,"A"),point(beforeMove,"A")+QPointF(.4,-.7))&&pointsClose(point(scene,"B"),point(beforeMove,"B")+QPointF(.4,-.7)));
    check("curve drag keeps ordinary source functions without local shift",!scene.source.contains("xshift")&&!scene.source.contains("yshift")&&!scene.partial);
    check("deleting a curve with bound points is rejected",Tikz::removed(scene,0).isEmpty());
    check("deleting endpoint used by midpoint is rejected",Tikz::removed(scene,locate(scene,"A")).isEmpty());
    check("invalid parameter preserves draft",Tikz::curveParameter(scene,locate(scene,"A"),qQNaN()).isEmpty());
    check("undefined endpoint after function edit rejects transaction",Tikz::property(scene,0,"expression","sqrt(t)").isEmpty());
    check("unknown functions remain source-only",Tikz::parse(QString(first).replace("\\t*\\t/2","mystery(\\t)")).partial||!Tikz::parse(QString(first).replace("\\t*\\t/2","mystery(\\t)")).coordinateSafe);
    check("recursive declarations are not evaluated",!Tikz::parse(QString(first).replace("texlinecurve1x(\\t)=\\t;","texlinecurve1x(\\t)=texlinecurve1x(\\t);")).coordinateSafe);
    const auto shortened=Tikz::parse(Tikz::property(chain,0,"domain","0:1"));
    check("shortening visible domain does not clamp saved parameters",pointsClose(point(shortened,"A"),point(chain,"A"))&&pointsClose(point(shortened,"B"),point(chain,"B")));
    check("new point outside drawn domain is rejected",Tikz::pointOnCurve(shortened,0,2).isEmpty());
    check("explicit parameter remains editable outside visible plot",!Tikz::curveParameter(shortened,locate(shortened,"B"),1.5).isEmpty());
    check("retained out-of-domain point can continue dragging",!Tikz::moved(shortened,locate(shortened,"B"),{.1,.1}).isEmpty());
    for(const auto &builtin:QStringList{"sin","pow","min","atan2","veclen"})check("existing PGF function cannot be redeclared: "+builtin,!Tikz::parse("\\begin{tikzpicture}[declare function={"+builtin+"(\\t)=\\t;}]\\end{tikzpicture}").coordinateSafe);
    const auto emptyFunctions=Tikz::parse(QString(originalPicture).replace(QStringLiteral("\\begin{tikzpicture}"),QStringLiteral("\\begin{tikzpicture}[declare function={}]")));
    check("empty declaration can receive functions",!Tikz::pointOnCurve(emptyFunctions,0,1).isEmpty());
    auto twoPlots=first;twoPlots.replace("domain=-3:3","domain=-3:-1");twoPlots.insert(twoPlots.indexOf("\\end{tikzpicture}"),"\\draw[domain=1:3] plot ({texlinecurve1x(\\x)},{texlinecurve1y(\\x)});\n\\coordinate (P) at ({texlinecurve1x(2)},{texlinecurve1y(2)});\n");
    const auto splitFamily=Tikz::parse(Tikz::pointOnCurve(Tikz::parse(twoPlots),3,2));
    check("new binding on shared Plot gets its own family",splitFamily.functions.size()==4&&splitFamily.items.size()>3&&splitFamily.items[0].curveFunctions!=splitFamily.items[3].curveFunctions);
    check("new point on second Plot drags along correct domain",!Tikz::moved(splitFamily,locate(splitFamily,"B"),{.1,.1}).isEmpty());
    const auto splitChanged=Tikz::parse(Tikz::property(splitFamily,3,"expression","t*t"));
    check("splitting shared family retains earlier point on selected segment",pointsClose(point(splitChanged,"P"),{2,4})&&pointsClose(point(splitChanged,"B"),{2,4})&&pointsClose(point(splitChanged,"A"),{-1,.5}));
    check("ambiguous shared-point ownership is not silently split",Tikz::pointOnCurve(Tikz::parse(QString(twoPlots).replace("domain=-3:-1","domain=-3:3")),3,2).isEmpty());
    auto precisePoint=twoPlots;precisePoint.replace("texlinecurve1x(2)","texlinecurve1x( 1.123456789123 )");precisePoint.replace("texlinecurve1y(2)","texlinecurve1y( 1.123456789123 )");
    const auto preciseSplit=Tikz::pointOnCurve(Tikz::parse(precisePoint),3,2);
    check("family migration preserves original parameter precision and spaces",preciseSplit.contains("texlinecurve2x( 1.123456789123 )")&&preciseSplit.contains("texlinecurve2y( 1.123456789123 )"));
    auto nodeSource=first;nodeSource.replace("\\coordinate (A) at", "\\node (A) at");nodeSource.replace("{texlinecurve1y(-1)});","{texlinecurve1y(-1)}) {N};");
    const auto nodeScene=Tikz::parse(nodeSource);const auto nodeChanged=Tikz::parse(Tikz::curveParameter(nodeScene,locate(nodeScene,"A"),2));
    check("imported single-coordinate Node accepts parameter edit",pointsClose(point(nodeChanged,"A"),{2,2}));
    for(const auto &option:QStringList{"xscale=2,yscale=.7","x={(2cm,1cm)},y={(-1cm,2cm)}","xscale=-1.5,yscale=2"}){
      auto input=originalPicture;input.replace("\\begin{tikzpicture}","\\begin{tikzpicture}["+option+"]");input.replace("black,domain","black,xshift=5mm,yshift=-2mm,domain");
      const auto base=Tikz::parse(input);const auto plot=base.items.value(0);const auto expected=plot.plotX+plot.plotY*.5+plot.shift;
      const auto lifted=Tikz::parse(Tikz::pointOnCurve(base,0,1));
      check("shift and transformed axes retained: "+option,lifted.valid&&!lifted.partial&&pointsClose(point(lifted,"A"),expected));
      const auto shifted=Tikz::parse(Tikz::moved(lifted,0,{.5,.2}));check("shared translation follows transformed axes: "+option,pointsClose(point(shifted,"A"),expected+QPointF(.5,.2)));
    }
    const auto xyz=Tikz::parse(Tikz::spatialCurve(Tikz::parse(Tikz::emptyPicture()),{"cos(t)","sin(t)","t/90"},0,360));
    const auto attached3=Tikz::parse(Tikz::pointOnCurve(xyz,0,90));bool xyzOk=false;auto xyzPoint=Tikz::spatialPosition(attached3,locate(attached3,"A"),&xyzOk);
    check("3D point preserves true XYZ",xyzOk&&qAbs(xyzPoint[0])<1e-6&&qAbs(xyzPoint[1]-1)<1e-6&&qAbs(xyzPoint[2]-1)<1e-6);
    check("XYZ editor cannot detach bound point",Tikz::spatialCoordinates(attached3,locate(attached3,"A"),{1,2,3}).isEmpty());
    auto view3=Tikz::parse(Tikz::projectionView(attached3,-25,35));check("projection change retains bound parameter",pointsClose(point(view3,"A"),Tikz::projected(view3,{0,1,1})));
    const auto moved3=Tikz::parse(Tikz::moved(attached3,0,{.2,-.4}));check("3D curve drag carries its bound point",pointsClose(point(moved3,"A"),point(attached3,"A")+QPointF(.2,-.4)));
    QString crossing="\\begin{tikzpicture}\\draw[domain=-180:180] plot ({sin(\\x)},{sin(2*\\x)});\\end{tikzpicture}";
    const auto crossed=Tikz::parse(crossing);const auto branch=Tikz::nearestCurveParameter(crossed,0,{.01,.01},179.);
    check("drag at crossing keeps nearby parameter branch",std::isfinite(branch)&&branch>165);
    check("fresh point finds nearest parameter",qAbs(Tikz::nearestCurveParameter(Tikz::parse(originalPicture),0,{1,.5})-1)<1e-4);
    const auto colliding=Tikz::parse(originalPicture,"\\pgfmathdeclarefunction{texlinecurve1x}{1}{}");
    check("new function names avoid preamble declarations",Tikz::pointOnCurve(colliding,0,0).contains("texlinecurve2x"));
    const QString external="\\begin{tikzpicture}[declare function={custom(\\s)=sin(\\s);}]\\draw[domain=-3:3] plot ({\\x},{\\x*\\x/2});\\end{tikzpicture}";
    const auto appended=Tikz::pointOnCurve(Tikz::parse(external),0,1);
    check("existing local declarations kept byte for byte",appended.contains("custom(\\s)=sin(\\s);")&&Tikz::parse(appended).functions.size()==3);
    Tw::Settings().setValue("openPDFwithTeX",false);Tw::Settings().setValue("researchFocusMode",false);
    const QString root=folder+"/paper/main.tex",text="\\documentclass{article}\n\\usepackage{tikz}\n\\begin{document}\nA drawing.\n\\end{document}\n";
    put(root,text.toUtf8());auto *w=new TeXDocumentWindow(root);w->resize(qEnvironmentVariableIsSet("TEXLINE_TEST_HALF")?680:1120,760);w->show();settle();
    if(qEnvironmentVariableIsSet("TEXLINE_TEST_FOCUS")){setFocusMode(true);settle();}
    auto cursor=w->textCursor();cursor.setPosition(text.indexOf("A drawing."));w->editor()->setTextCursor(cursor);
    openTikz(w,true,{},originalPicture);settle();
    auto *dialog=dynamic_cast<TikzDialog*>(w->findChild<QDialog*>("researchTikzDialog"));auto *canvas=dialog?dynamic_cast<TikzCanvas*>(dialog->findChild<QWidget*>("researchTikzCanvas")):nullptr;
    check("native curve workbench opens",canvas);if(!canvas){app.exit(1);return;}
    auto pointer=[&](QEvent::Type type,const QPointF &p,Qt::MouseButton button,Qt::MouseButtons buttons){QMouseEvent e(type,p,canvas->mapToGlobal(p.toPoint()),button,buttons,Qt::NoModifier);QApplication::sendEvent(canvas,&e);};
    auto click=[&](const QPointF &p){pointer(QEvent::MouseButtonPress,p,Qt::LeftButton,Qt::LeftButton);pointer(QEvent::MouseButtonRelease,p,Qt::LeftButton,Qt::NoButton);};
    auto *action=dialog->findChild<QAction*>("researchTikzConstruct_curvePoint");check("point-on-curve action reuses Add menu",action);
    if(!action){key(dialog,Qt::Key_Escape);w->close();app.exit(1);return;}
    action->trigger();key(canvas,Qt::Key_Escape);check("Escape before picking leaves exact original source",canvas->model().source==originalPicture&&!canvas->activeDrag());
    action->trigger();click(canvas->screen({1,.5}));settle();
    check("canvas click creates bound point without AI or source input",canvas->model().functions.size()==2&&locate(canvas->model(),"A")>=0);
    if(locate(canvas->model(),"A")<0){app.exit(1);return;}
    const auto added=canvas->model().source;key(canvas,Qt::Key_Z,Qt::ControlModifier);check("one draft undo removes point and declarations",canvas->model().source==originalPicture);key(canvas,Qt::Key_Y,Qt::ControlModifier);check("redo restores point relation",canvas->model().source==added);
    canvas->choose(locate(canvas->model(),"A"));auto *parameter=dialog->findChild<QLineEdit*>("researchTikzParameter");
    check("bound point shows parameter instead of free XYZ",parameter->isVisibleTo(dialog)&&!dialog->findChild<QDoubleSpinBox*>("researchTikzX")->isVisibleTo(dialog));
    parameter->setFocus();parameter->selectAll();key(parameter,Qt::Key_1,Qt::NoModifier,"invalid");key(parameter,Qt::Key_Return);settle();
    check("invalid parameter retains input and source",parameter->text()=="invalid"&&canvas->model().source==added);
    parameter->selectAll();key(parameter,Qt::Key_1,Qt::NoModifier,"-1.25");key(parameter,Qt::Key_Return);settle();
    check("native parameter editing moves bound point",pointsClose(point(canvas->model(),"A"),{-1.25,.78125}));
    const auto changedPoint=canvas->model().source;canvas->setFocus();canvas->choose(locate(canvas->model(),"A"));
    const auto start=canvas->screen(point(canvas->model(),"A")),finish=start+QPointF(20,25);
    pointer(QEvent::MouseButtonPress,start,Qt::LeftButton,Qt::LeftButton);pointer(QEvent::MouseMove,finish,Qt::NoButton,Qt::LeftButton);key(canvas,Qt::Key_Escape);pointer(QEvent::MouseButtonRelease,finish,Qt::LeftButton,Qt::NoButton);
    check("Escape during point drag restores relation and parameter",canvas->model().source==changedPoint);
    pointer(QEvent::MouseButtonPress,start,Qt::LeftButton,Qt::LeftButton);pointer(QEvent::MouseMove,finish,Qt::NoButton,Qt::LeftButton);pointer(QEvent::MouseButtonRelease,finish,Qt::LeftButton,Qt::NoButton);
    const auto draggedPoint=point(canvas->model(),"A");check("native point drag stays on parabola",canvas->model().source!=changedPoint&&qAbs(draggedPoint.y()-draggedPoint.x()*draggedPoint.x()/2)<1e-6);
    key(canvas,Qt::Key_Z,Qt::ControlModifier);check("one undo restores drag",canvas->model().source==changedPoint);
    canvas->choose(0);auto *expression=dialog->findChild<QLineEdit*>("researchTikzExpression");expression->setFocus();expression->selectAll();key(expression,Qt::Key_1,Qt::NoModifier,"t*t/3");key(expression,Qt::Key_Return);settle();
    check("curve inspector updates the shared expression",qAbs(point(canvas->model(),"A").y()-1.5625/3)<1e-5);
    dialog->grab().save(folder+"/curve-inspector.png");
    canvas->choose(locate(canvas->model(),"A"));dialog->grab().save(folder+"/point-parameter.png");
    auto *mode=dialog->findChild<QComboBox*>("researchTikzMode");mode->setCurrentIndex(2);
    check("shared functions compile through actual local TeX preview",waitFor([&]{return dialog->findChild<QLabel*>("researchTikzPreview")->property("researchDraftReady").toBool();},45000));
    dialog->grab().save(folder+"/curve-typeset.png");mode->setCurrentIndex(0);const auto finalDraft=canvas->model().source;
    dialog->findChild<QDialogButtonBox*>("researchTikzButtons")->button(QDialogButtonBox::Apply)->click();settle();
    check("Apply inserts standard editable TikZ",w->textDoc()->editorText().contains(finalDraft));
    w->editor()->undo();check("one manuscript undo restores original",w->textDoc()->editorText()==text);w->editor()->redo();
    cursor=w->textCursor();cursor.setPosition(w->textDoc()->editorText().indexOf("plot"));w->editor()->setTextCursor(cursor);editResearchObject(w);settle();
    dialog=dynamic_cast<TikzDialog*>(w->findChild<QDialog*>("researchTikzDialog"));check("applied bound curve reopens",dialog);
    if(dialog){canvas=dynamic_cast<TikzCanvas*>(dialog->findChild<QWidget*>("researchTikzCanvas"));check("reopening recovers dependency from source",canvas&&canvas->model().functions.size()==2&&canvas->model().source==finalDraft);key(dialog,Qt::Key_Escape);settle();}
    w->editor()->undo();check("Cancel and undo keep original disk and text",bytes(root)==text.toUtf8()&&w->textDoc()->editorText()==text);w->textDoc()->setModified(false);w->close();app.exit(failures?1:0);
  });return app.exec();
}
