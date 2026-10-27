// Independent labels: native pointer transactions, source fidelity and real TeX units.
#include "ActualAuthoringRecording.h"
#include "ResearchAuthoring.cpp"
using namespace Research;
int main(int argc,char **argv){
  const auto report=argument(1);argc=1;QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
  TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
  QTimer::singleShot(50,&app,[&]{
    const auto folder=QFileInfo(report).absolutePath();QJsonArray checks;int failures=0;
    auto check=[&](const QString &name,bool pass){checks.append(QJsonObject{{"name",name},{"passed",pass}});failures+=!pass;put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"devicePixelRatio",qApp->primaryScreen()->devicePixelRatio()}}).toJson());};
    auto close=[](const QPointF &a,const QPointF &b){return QLineF(a,b).length()<1e-6;};
    check("private inactive desktop",qEnvironmentVariable("TEXLINE_TEST_DESKTOP").startsWith("TeXlineValidation-"));
    const QString picture=R"(\begin{tikzpicture}
% Letters are attached to the triangle, not independent geometry.
\coordinate (A) at (0,0);
\coordinate (B) at (3,0);
\coordinate (C) at (1,2);
\draw[black] (A)--(B)--(C)--cycle;
\fill (A) circle (1.2pt) node[above right] {$A$};
\fill (B) circle (1.2pt) node[below right] {$B$};
\fill (C) circle (1.2pt) node[above] {$C$};
\end{tikzpicture})";
    auto s=Tikz::parse(picture);check("original labels are editable",s.valid&&!s.partial&&s.items.size()==7&&s.items[4].labelMovable);
    const auto moved=Tikz::labelMoved(s,4,{-.6,.4});const auto next=Tikz::parse(moved);
    check("label moves without changing any point",!moved.isEmpty()&&close(next.items[0].position,s.items[0].position)&&close(next.items[1].position,s.items[1].position)&&close(next.items[2].position,s.items[2].position));
    check("label shift survives parsing",close(next.items[4].labelShift,{-.6,.4}));
    check("unrelated source preserved exactly",moved.left(s.items[4].labelOptionsStart)==picture.left(s.items[4].labelOptionsStart)&&moved.mid(moved.indexOf("] {$A$}"))==picture.mid(picture.indexOf("] {$A$}")));
    check("zero drag does not rewrite source",Tikz::labelMoved(s,4,{})==picture);
    check("nonfinite drag rejected",Tikz::labelMoved(s,4,{qQNaN(),0}).isEmpty());
    auto following=Tikz::parse(Tikz::moved(next,0,{.3,-.2}));check("moving point keeps offset",close(following.items[4].labelShift,{-.6,.4})&&close(Tikz::endpoint(following,following.items[4].coordinates[0]),{.3,-.2}));
    const QString imported=R"(\begin{tikzpicture}[scale=2,x={(2cm,.3cm)},y={(.4cm,1cm)},every node/.style={xshift=1mm,font=\small}]
\coordinate(A) at (1,1);
\fill (A) circle (1pt) node[above right, xshift=2mm, xshift=3mm,yshift=-4pt,font=\fontsize{12}{14}\selectfont] {$A_1$};
\end{tikzpicture})";
    auto original=Tikz::parse(imported);const auto shifted=Tikz::labelMoved(original,1,{.2,-.3});auto shiftedScene=Tikz::parse(shifted);
    check("duplicate shifts and inherited shift accumulate",close(original.items[1].labelShift,{.6,-4*2.54/72.27}));
    check("physical label shift ignores picture scale and axes",!shifted.isEmpty()&&close(shiftedScene.items[1].labelShift,original.items[1].labelShift+QPointF(.2,-.3)));
    check("unmodified shift and font bytes retained",shifted.contains(" xshift=2mm, xshift=0.5cm")&&shifted.contains("font=\\fontsize{12}{14}\\selectfont")&&shifted.contains("xshift=1mm,font=\\small"));
    for(const auto &option:QStringList{"xshift=\\gap","rotate=20","transform shape","sloped","mystery style"}){auto bad=Tikz::parse(QString(picture).replace("above right",option));check("unsupported label retains source: "+option,!bad.items[4].labelMovable&&Tikz::labelMoved(bad,4,{1,0}).isEmpty()&&bad.source.contains(option));}
    auto noOptions=Tikz::parse(QString(picture).replace("node[above right]","node"));check("label with no options accepts offset",Tikz::labelMoved(noOptions,4,{.1,0}).contains("node[xshift=0.1cm]"));
    const auto nodeSource=QStringLiteral("\\begin{tikzpicture}[scale=2]\\coordinate(A) at (1,2);\\node[above,xshift=1mm,xshift=2mm] at(A){$A$};\\end{tikzpicture}");
    const auto node=Tikz::parse(nodeSource);const auto nodeMoved=Tikz::parse(Tikz::moved(node,1,{.5,.2}));
    check("separate label node keeps reference and point",nodeMoved.source.contains("at(A)")&&close(nodeMoved.items[0].position,node.items[0].position));
    check("separate node moves in physical screen axes",close(nodeMoved.items[1].position,node.items[1].position+QPointF(.5,.2)));
    auto spatial=Tikz::parse(QString(picture).replace("\\begin{tikzpicture}","\\begin{tikzpicture}[x={(.8cm,-.4cm)},y={(.6cm,.5cm)},z={(0cm,1cm)}]").replace("at (0,0)","at (0,0,1)"));
    const auto spatialAfter=Tikz::parse(Tikz::labelMoved(spatial,4,{.2,.3}));check("XYZ point coordinates unchanged by label drag",spatialAfter.items[0].coordinates[0].values==spatial.items[0].coordinates[0].values&&close(spatialAfter.items[4].labelShift,{.2,.3}));
    auto curve=Tikz::parse("\\begin{tikzpicture}\\draw[domain=-2:2] plot (\\x,{\\x*\\x});\\end{tikzpicture}");curve=Tikz::parse(Tikz::pointOnCurve(curve,0,1));
    auto curveAfter=Tikz::parse(Tikz::labelMoved(curve,2,{.4,.3}));check("curve bound point parameter untouched",curveAfter.items[1].coordinates[0].parameter==1&&close(curveAfter.items[2].labelShift,{.4,.3}));
    auto withText=Tikz::parse("\\begin{tikzpicture}\\draw[domain=-2:2] plot (\\x,{\\x*\\x}) node[above] {$f$};\\end{tikzpicture}");
    check("plot label keeps expression span",withText.items.size()==1&&withText.items[0].expression=="\\x*\\x"&&withText.source.mid(withText.items[0].textStart,withText.items[0].textEnd-withText.items[0].textStart)=="\\x*\\x");
    check("changing plot expression keeps label",Tikz::property(withText,0,"expression","t*t/2").contains("{$f$}"));
    const auto anchorLast=Tikz::parse(QString(picture).replace("above right","above,anchor=west")),directionLast=Tikz::parse(QString(picture).replace("above right","anchor=west,above"));
    check("label anchor follows option order",anchorLast.items[4].labelStyle.value("__anchor")=="west"&&directionLast.items[4].labelStyle.value("__anchor")=="south");
    // Let PGF itself measure the resulting label centers in a separate TeX file.
    QString probe="\\documentclass{article}\n\\usepackage{tikz}\n\\newdimen\\LabelX\\newdimen\\LabelY\n\\begin{document}\n";
    for(int n=0;n<2;++n){auto p=n?shifted:imported;p.replace(" {$A_1$}"," (Letter) {$A_1$}");p.replace("\\end{tikzpicture}",QString("\\pgftransformreset\\pgfextractx\\LabelX{\\pgfpointanchor{Letter}{center}}\\pgfextracty\\LabelY{\\pgfpointanchor{Letter}{center}}\\typeout{LABEL%1=\\the\\LabelX,\\the\\LabelY}\n\\end{tikzpicture}").arg(n));probe+=p+"\n";}
    probe+="\\end{document}";put(folder+"/units.tex",probe.toUtf8());QProcess tex;tex.setWorkingDirectory(folder);tex.start("C:/texlive/2023/bin/windows/pdflatex.exe",{"-interaction=nonstopmode","-halt-on-error","units.tex"});
    check("real TeX unit probe compiles",tex.waitForFinished(30000)&&tex.exitCode()==0);const auto log=QString::fromUtf8(bytes(folder+"/units.log"));QPointF positions[2];bool measured=true;
    for(int n=0;n<2;++n){auto m=QRegularExpression(QString("LABEL%1=([-.0-9]+)pt,([-.0-9]+)pt").arg(n)).match(log);measured&=m.hasMatch();positions[n]={m.captured(1).toDouble(),m.captured(2).toDouble()};}
    check("real PGF displacement matches canvas physical offsets",measured&&QLineF(positions[1]-positions[0],QPointF(.2,-.3)*(72.27/2.54)).length()<.003);
    {TikzCanvas isolated(nullptr);isolated.resize(700,500);auto line=Tikz::parse("\\begin{tikzpicture}\\draw[xshift=1cm] (0,0)--(2,0) node[midway] {M};\\end{tikzpicture}");isolated.setScene(line,true);check("midway label includes path shift",QLineF(isolated.labelBounds(0).center(),isolated.screen({2,0})).length()<1);
      auto corner=Tikz::parse("\\begin{tikzpicture}\\draw (0,0)-|(2,2) node[midway] {M};\\end{tikzpicture}");isolated.setScene(corner,true);check("orthogonal midway follows corner",QLineF(isolated.labelBounds(0).center(),isolated.screen({2,0})).length()<1);
      isolated.setScene(withText,true);check("plot label anchored to final point",isolated.labelBounds(0).center().x()>isolated.screen({1.9,0}).x());
    }
    Tw::Settings().setValue("openPDFwithTeX",false);Tw::Settings().setValue("researchFocusMode",false);
    const QString root=folder+"/paper/main.tex",text="\\documentclass{article}\n\\usepackage{tikz}\n\\begin{document}\nA drawing.\n\\end{document}\n";put(root,text.toUtf8());auto *w=new TeXDocumentWindow(root);w->resize(qEnvironmentVariableIsSet("TEXLINE_TEST_HALF")?680:1120,760);w->show();settle();
    if(qEnvironmentVariableIsSet("TEXLINE_TEST_FOCUS")){setFocusMode(true);settle();}
    auto cursor=w->textCursor();cursor.setPosition(text.indexOf("A drawing."));w->editor()->setTextCursor(cursor);openTikz(w,true,{},picture);settle();
    auto *dialog=dynamic_cast<TikzDialog*>(w->findChild<QDialog*>("researchTikzDialog"));auto *canvas=dialog?dynamic_cast<TikzCanvas*>(dialog->findChild<QWidget*>("researchTikzCanvas")):nullptr;check("native label editor opens",canvas);if(!canvas){app.exit(1);return;}
    auto pointer=[&](QEvent::Type type,const QPointF &p,Qt::MouseButton button,Qt::MouseButtons buttons){QMouseEvent e(type,p,canvas->mapToGlobal(p.toPoint()),button,buttons,Qt::NoModifier);QApplication::sendEvent(canvas,&e);};
    const auto start=canvas->labelBounds(4).center(),finish=start+QPointF(-44,-20);check("label hit targets visible text",canvas->labelHit(start)==4&&canvas->labelBounds(4).width()<40&&canvas->rect().contains(start.toPoint())&&canvas->rect().contains(finish.toPoint()));
    dialog->grab().save(folder+"/labels-before.png");
    pointer(QEvent::MouseButtonPress,start,Qt::LeftButton,Qt::LeftButton);pointer(QEvent::MouseMove,finish,Qt::NoButton,Qt::LeftButton);key(canvas,Qt::Key_Escape);pointer(QEvent::MouseButtonRelease,finish,Qt::LeftButton,Qt::NoButton);
    check("Escape during label drag restores exact draft",canvas->model().source==picture&&!canvas->activeDrag());
    const auto frames=folder+"/actual-label-drag";QDir().mkpath(frames);dialog->grab().save(frames+"/00.png");pointer(QEvent::MouseButtonPress,start,Qt::LeftButton,Qt::LeftButton);
    for(int n=1;n<=8;++n){pointer(QEvent::MouseMove,start+(finish-start)*n/8.,Qt::NoButton,Qt::LeftButton);settle(18);dialog->grab().save(frames+QString("/%1.png").arg(n,2,10,QLatin1Char('0')));}
    pointer(QEvent::MouseButtonRelease,finish,Qt::LeftButton,Qt::NoButton);settle();const auto dragged=canvas->model().source;
    check("pointer drags label only",dragged!=picture&&close(canvas->model().items[0].position,{0,0})&&QLineF(canvas->labelBounds(4).center(),finish).length()<1);
    check("label release restores canvas focus",canvas->hasFocus());dialog->grab().save(folder+"/labels-after.png");dialog->grab().save(frames+"/09.png");
    key(canvas,Qt::Key_Z,Qt::ControlModifier);check("one draft undo restores label",canvas->model().source==picture);key(canvas,Qt::Key_Y,Qt::ControlModifier);check("redo restores offset",canvas->model().source==dragged);
    key(canvas,Qt::Key_Right);check("arrow key nudges selected label not point",close(canvas->model().items[0].position,{0,0})&&canvas->model().source!=dragged);key(canvas,Qt::Key_Z,Qt::ControlModifier);
    key(canvas,Qt::Key_Delete);check("delete selected label preserves point and triangle",canvas->model().items.size()==7&&canvas->model().items[4].text.isEmpty()&&close(canvas->model().items[0].position,{0,0}));key(canvas,Qt::Key_Z,Qt::ControlModifier);
    // Menu and keyboard must act on the same selected label, not its owner point.
    const auto letter=canvas->labelBounds(4).center();pointer(QEvent::MouseButtonPress,letter,Qt::LeftButton,Qt::LeftButton);pointer(QEvent::MouseButtonRelease,letter,Qt::LeftButton,Qt::NoButton);
    dialog->findChild<QAction*>("researchTikzDeleteSelected")->trigger();check("More Delete clears label and retains point and edges",canvas->model().items.size()==7&&canvas->model().items[4].text.isEmpty()&&canvas->model().source.contains("(A)--(B)--(C)--cycle"));key(canvas,Qt::Key_Z,Qt::ControlModifier);
    const auto center=canvas->screen({0,0});pointer(QEvent::MouseButtonPress,center,Qt::LeftButton,Qt::LeftButton);pointer(QEvent::MouseMove,center+QPointF(10,-10),Qt::NoButton,Qt::LeftButton);pointer(QEvent::MouseButtonRelease,center+QPointF(10,-10),Qt::LeftButton,Qt::NoButton);
    check("point remains directly draggable after label",!close(canvas->model().items[0].position,{0,0})&&close(canvas->model().items[4].labelShift,Tikz::parse(dragged).items[4].labelShift));key(canvas,Qt::Key_Z,Qt::ControlModifier);
    auto *mode=dialog->findChild<QComboBox*>("researchTikzMode");mode->setCurrentIndex(2);check("moved labels compile in actual preview",waitFor([&]{return dialog->findChild<QLabel*>("researchTikzPreview")->property("researchDraftReady").toBool();},45000));dialog->grab().save(folder+"/labels-typeset.png");mode->setCurrentIndex(0);const auto finalDraft=canvas->model().source;
    dialog->findChild<QDialogButtonBox*>("researchTikzButtons")->button(QDialogButtonBox::Apply)->click();settle();check("Apply writes offset into ordinary TikZ",w->textDoc()->editorText().contains(finalDraft));w->editor()->undo();check("one manuscript undo restores original",w->textDoc()->editorText()==text);w->editor()->redo();
    cursor=w->textCursor();cursor.setPosition(w->textDoc()->editorText().indexOf("\\coordinate"));w->editor()->setTextCursor(cursor);editResearchObject(w);settle();dialog=dynamic_cast<TikzDialog*>(w->findChild<QDialog*>("researchTikzDialog"));check("saved drawing reopens",dialog);
    if(dialog){canvas=dynamic_cast<TikzCanvas*>(dialog->findChild<QWidget*>("researchTikzCanvas"));check("reopen recovers exact label offsets",canvas&&canvas->model().source==finalDraft);key(dialog,Qt::Key_Escape);settle();}
    check("Cancel keeps manuscript and original disk",w->textDoc()->editorText().contains(finalDraft)&&bytes(root)==text.toUtf8());w->editor()->undo();
    const auto twoLabels=QString(picture).replace("\\end{tikzpicture}","\\fill (A) circle(1.2pt) node[below left,font=\\fontsize{12}{14}\\selectfont] {$A_2$};\n\\end{tikzpicture}");
    openTikz(w,true,{},twoLabels);settle();dialog=dynamic_cast<TikzDialog*>(w->findChild<QDialog*>("researchTikzDialog"));canvas=dialog?dynamic_cast<TikzCanvas*>(dialog->findChild<QWidget*>("researchTikzCanvas")):nullptr;
    if(canvas){const auto second=canvas->labelBounds(7).center();pointer(QEvent::MouseButtonPress,second,Qt::LeftButton,Qt::LeftButton);pointer(QEvent::MouseButtonRelease,second,Qt::LeftButton,Qt::NoButton);auto *field=dialog->findChild<QPlainTextEdit*>("researchTikzLabel");
      check("second label populates its own inspector",field->toPlainText()=="A_2"&&canvas->labelSelection()==7&&!dialog->findChild<QDoubleSpinBox*>("researchTikzX")->isVisibleTo(dialog));
      field->setFocus();field->selectAll();key(field,Qt::Key_A,Qt::NoModifier,"A_3");auto *position=dialog->findChild<QComboBox*>("researchTikzLabelPosition");const int which=position->findData("left");position->setCurrentIndex(which);QMetaObject::invokeMethod(position,"activated",Qt::DirectConnection,Q_ARG(int,which));settle();
      check("editing second label retains first and point",canvas->model().items[4].text=="$A$"&&canvas->model().items[7].text=="$A_3$"&&canvas->model().items[7].labelOptions.contains("left")&&close(canvas->model().items[0].position,{0,0}));
      const auto firstOptions=canvas->model().items[4].labelOptions;dialog->findChild<QAction*>("researchTikzDeleteSelected")->trigger();const int below=position->findData("below");position->setCurrentIndex(below);QMetaObject::invokeMethod(position,"activated",Qt::DirectConnection,Q_ARG(int,below));
      check("empty second label keeps exact target for placement",canvas->labelSelection()==7&&canvas->model().items[4].labelOptions==firstOptions&&canvas->model().items[7].labelOptions.contains("below"));key(canvas,Qt::Key_Z,Qt::ControlModifier);key(canvas,Qt::Key_Z,Qt::ControlModifier);
      auto *style=dialog->findChild<QAction*>("researchTikzLabelStyle");style->trigger();settle();auto *form=dialog->findChild<QDialog*>("researchTikzLabelStyleDialog");auto *size=form?form->findChild<QDoubleSpinBox*>("researchTikzLabelSize"):nullptr;
      check("label style reads selected label size and hides dot radius",size&&size->value()==12&&!form->findChild<QDoubleSpinBox*>("researchTikzPointRadius"));const auto beforeSize=canvas->model().source;
      if(size){size->setValue(14);key(form,Qt::Key_Escape);settle();check("label style Cancel preserves draft",canvas->model().source==beforeSize);style->trigger();settle();form=dialog->findChild<QDialog*>("researchTikzLabelStyleDialog");size=form->findChild<QDoubleSpinBox*>("researchTikzLabelSize");size->setValue(14);form->findChild<QDialogButtonBox*>("researchTikzLabelStyleButtons")->button(QDialogButtonBox::Ok)->click();settle();check("font edit affects only second label",canvas->model().items[7].labelOptions.contains("{14}")&&!canvas->model().items[4].labelOptions.contains("font="));key(canvas,Qt::Key_Z,Qt::ControlModifier);check("one undo restores selected label font",canvas->model().source==beforeSize);}
      const auto beforeTool=canvas->model().source;canvas->startTool("segment");check("constructing after selecting text clears label target",canvas->labelSelection()<0);key(canvas,Qt::Key_Escape);check("construction Cancel keeps label and source",canvas->model().source==beforeTool&&canvas->labelSelection()<0);
      key(dialog,Qt::Key_Escape);settle();
    }else check("multiple label dialog opens",false);
    openTikz(w,true,{},withText.source);settle();dialog=dynamic_cast<TikzDialog*>(w->findChild<QDialog*>("researchTikzDialog"));canvas=dialog?dynamic_cast<TikzCanvas*>(dialog->findChild<QWidget*>("researchTikzCanvas")):nullptr;
    if(canvas){const auto textPosition=canvas->labelBounds(0).center();pointer(QEvent::MouseButtonDblClick,textPosition,Qt::LeftButton,Qt::LeftButton);settle();auto *field=dialog->findChild<QPlainTextEdit*>("researchTikzLabel");check("double-click plot label focuses text rather than formula",field&&field->hasFocus()&&canvas->model().items[0].expression=="\\x*\\x");key(dialog,Qt::Key_Escape);settle();}
    else check("plot label dialog opens",false);
    w->textDoc()->setModified(false);w->close();app.exit(failures?1:0);
  });return app.exec();
}
