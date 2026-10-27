// Exact source-verifiable tangent relations, native transactions and independent PGF anchors.
#include "ActualAuthoringRecording.h"
#include "ResearchAuthoring.cpp"
using namespace Research;
int main(int argc,char **argv){
  const auto report=argument(1);argc=1;QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
  wchar_t desktop[256]{};DWORD needed=0;
  if(!GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()),UOI_NAME,desktop,sizeof(desktop),&needed)||QString::fromWCharArray(desktop)!=qEnvironmentVariable("TEXLINE_TEST_DESKTOP")||!QString::fromWCharArray(desktop).startsWith("TeXlineValidation-"))return 2;
  TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
  QTimer::singleShot(50,&app,[&]{
    const auto folder=QFileInfo(report).absolutePath();QJsonArray checks;int failures=0;
    auto check=[&](const QString &name,bool pass){checks.append(QJsonObject{{"name",name},{"passed",pass}});failures+=!pass;put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"devicePixelRatio",qApp->primaryScreen()->devicePixelRatio()}}).toJson());};
    check("private inactive desktop",qEnvironmentVariable("TEXLINE_TEST_DESKTOP").startsWith("TeXlineValidation-"));
    auto find=[](const Tikz::Scene &s,const QString &id){for(int n=0;n<s.items.size();++n)if(s.items[n].id==id)return n;return -1;};
    auto pos=[&](const Tikz::Scene &s,const QString &id){const int n=find(s,id);return n>=0?s.items[n].position:QPointF(qQNaN(),qQNaN());};
    auto close=[](const QPointF &a,const QPointF &b){return std::isfinite(a.x())&&std::isfinite(a.y())&&QLineF(a,b).length()<.00002;};
    auto unit=[](const QPointF &p){return p*(2/std::hypot(p.x(),p.y()));};
    auto picture=[](const QString &x,const QString &y,const QString &settings=QString()){return QString("\\begin{tikzpicture}[%1]\n\\draw[domain=-3:3,samples=100] plot ({%2},{%3});\n\\end{tikzpicture}").arg(settings,x,y);};
    auto bound=[&](const QString &x,const QString &y,double t,const QString &settings=QString()){return Tikz::parse(Tikz::pointOnCurve(Tikz::parse(picture(x,y,settings)),0,t));};
    QVector<Tikz::Scene> probes;
    for(const auto &settings:QStringList{"","x=1.23cm,y=1.05cm","xscale=-1.5,yscale=.8,x={(1cm,.3cm)},y={(-.2cm,1cm)}"}){
      auto s=bound("\\x*\\x/4","\\x",2.4,settings);check("parabola point supports tangent "+settings,Tikz::tangentPoint(s,find(s,"A")));
      s=Tikz::parse(Tikz::tangentAtPoint(s,find(s,"A")));check("first tangent source reopens "+settings,s.valid&&!s.partial&&s.items.size()==6);
      if(!s.valid||s.partial)continue;
      auto map=[&](const QPointF &p){const auto q=s.axisX*p.x()+s.axisY*p.y();return QPointF(q.x()*s.canvasScale.x(),q.y()*s.canvasScale.y());};
      check("D082 first exact tangent "+settings,close(pos(s,"A"),map({1.44,2.4}))&&close(pos(s,"tlh1")-pos(s,"A"),map(unit({1.2,1}))));probes.append(s);
      s=Tikz::parse(Tikz::pointOnCurve(s,0,-1.2));s=Tikz::parse(Tikz::tangentAtPoint(s,find(s,"B")));
      s=Tikz::parse(Tikz::construct(s,"intersection",{find(s,"tlh1"),find(s,"tlh2"),find(s,"tlh3"),find(s,"tlh4")}));
      check("D082 tangent intersection matches original problem "+settings,s.valid&&!s.partial&&close(pos(s,"C"),map({-.72,.6})));probes.append(s);
      const auto shifted=Tikz::parse(Tikz::curveParameter(s,find(s,"A"),2));
      check("changing parameter refreshes tangent and downstream intersection "+settings,shifted.valid&&!shifted.partial&&close(pos(shifted,"tlh1")-pos(shifted,"A"),map(unit({1,1})))&&!close(pos(shifted,"C"),pos(s,"C")));probes.append(shifted);
      const auto edited=Tikz::parse(Tikz::property(s,0,"expression X","t*t/2"));
      check("changing function refreshes both derivatives atomically "+settings,edited.valid&&!edited.partial&&close(pos(edited,"tlh1")-pos(edited,"A"),map(unit({2.4,1}))));probes.append(edited);
      const auto moved=Tikz::parse(Tikz::moved(s,0,{.3,-.2}));
      check("moving curve carries tangent and intersection "+settings,moved.valid&&!moved.partial&&close(pos(moved,"A"),pos(s,"A")+QPointF(.3,-.2))&&close(pos(moved,"C"),pos(s,"C")+QPointF(.3,-.2)));probes.append(moved);
      check("cannot remove defining point before dependents "+settings,Tikz::removed(s,find(s,"A")).isEmpty());
    }
    auto hyperbolaScene=Tikz::parse(Tikz::pointOnCurve(Tikz::parse(picture("\\x","2*sqrt(\\x*\\x/5-1)","x=1.05cm,y=.92cm").replace("domain=-3:3","domain=2.25:3")),0,2.5));
    hyperbolaScene=Tikz::parse(Tikz::tangentAtPoint(hyperbolaScene,find(hyperbolaScene,"A")));
    check("D119 tangent has analytic direction before axis mapping",hyperbolaScene.valid&&!hyperbolaScene.partial&&close(pos(hyperbolaScene,"A"),{2.625,.92})&&close(pos(hyperbolaScene,"tlh1")-pos(hyperbolaScene,"A"),QPointF(1.05,1.84)*(2/std::sqrt(5.))));probes.append(hyperbolaScene);
    auto vertical=bound("\\x*\\x","\\x",0);vertical=Tikz::parse(Tikz::tangentAtPoint(vertical,find(vertical,"A")));
    check("vertical tangent does not divide by slope",vertical.valid&&!vertical.partial&&close(pos(vertical,"tlh1")-pos(vertical,"A"),{0,2}));probes.append(vertical);
    const auto cusp=bound("\\x*\\x","\\x*\\x*\\x",0),corner=bound("\\x","abs(\\x)",0);
    check("zero velocity rejected",!Tikz::tangentPoint(cusp,find(cusp,"A")));
    check("unsupported nonsmooth derivative is not guessed",!Tikz::tangentPoint(corner,find(corner,"A")));
    auto trig=bound("cos(\\x)","sin(\\x)",0);trig=Tikz::parse(Tikz::tangentAtPoint(trig,find(trig,"A")));
    check("PGF degrees respected in derivatives",trig.valid&&!trig.partial&&close(pos(trig,"tlh1")-pos(trig,"A"),{0,2}));probes.append(trig);
    if(hyperbolaScene.valid){auto stale=hyperbolaScene.source;const auto f=hyperbolaScene.functions.value("texlinetangent1y");stale.replace(f.start,f.end-f.start,"0");check("stale imported derivative is source-only",Tikz::parse(stale).partial);}
    auto steep=Tikz::parse(Tikz::pointOnCurve(Tikz::parse(picture("9000*\\x","\\x").replace("domain=-3:3","domain=-.001:.001")),0,0));
    steep=Tikz::parse(Tikz::tangentAtPoint(steep,find(steep,"A")));check("high speed tangent has bounded length",steep.valid&&!steep.partial&&close(pos(steep,"tlh1"),unit({9000,1})));probes.append(steep);
    const auto slow=bound(".000005*\\x",".000005*\\x",0);check("PGF-unresolvable velocity rejected",!Tikz::tangentPoint(slow,find(slow,"A")));
    for(double speed:{.1,1.,1000.}){const auto raw=picture(QString::number(speed)+"*\\x",QString::number(speed)+"*\\x").replace("domain=-3:3","domain=-.001:.001");auto s=Tikz::parse(Tikz::pointOnCurve(Tikz::parse(raw),0,0));s=Tikz::parse(Tikz::tangentAtPoint(s,find(s,"A")));check(QString("same line at speed %1 has same tangent length").arg(speed),s.valid&&!s.partial&&close(pos(s,"tlh1"),unit({1,1})));probes.append(s);}
    auto overflow=Tikz::parse(Tikz::pointOnCurve(Tikz::parse(picture("\\x/10","\\x^3/(1+\\x^3)").replace("domain=-3:3","domain=1:20")),0,20));
    check("symbolic derivative intermediate overflow rejected",overflow.valid&&!overflow.partial&&!Tikz::tangentPoint(overflow,find(overflow,"A")));probes.append(overflow);
    auto repeated=bound("\\x","\\x*\\x/2",1);bool repeatOk=true;
    for(int n=0;n<32;++n){const auto created=Tikz::parse(Tikz::tangentAtPoint(repeated,find(repeated,"A")));if(!created.valid||created.partial||created.functions.size()!=4){repeatOk=false;break;}repeated=Tikz::parse(Tikz::removed(created,created.items.size()-1));if(!repeated.valid||repeated.partial||repeated.items.size()!=3||repeated.functions.size()!=2){repeatOk=false;break;}}
    check("repeated create delete does not leak derivative functions",repeatOk&&Tikz::tangentPoint(repeated,find(repeated,"A")));probes.append(repeated);
    auto commented=Tikz::parse(Tikz::tangentAtPoint(repeated,find(repeated,"A")));auto commentSource=commented.source;
    const auto derivative=commented.functions["texlinetangent1x"];commentSource.insert(derivative.end+1," keepvalue(\\t)=2;");commentSource.insert(commentSource.indexOf("  texlinetangent1x"),"% keep comment\n");commented=Tikz::parse(commentSource);
    const auto commentDeleted=Tikz::parse(Tikz::removed(commented,commented.items.size()-1));check("delete preserves preceding comment and same-line unrelated declaration",commented.valid&&!commented.partial&&commentDeleted.valid&&!commentDeleted.partial&&commentDeleted.functions.contains("keepvalue")&&commentDeleted.functions.size()==3&&commentDeleted.source.contains("% keep comment"));probes.append(commentDeleted);
    QString tex="\\documentclass{article}\n\\usepackage{tikz}\n\\usetikzlibrary{calc}\n\\newdimen\\PX\\newdimen\\PY\n\\begin{document}\n";QVector<QPointF> expected;
    for(const auto &s:probes){if(!s.valid||s.partial)continue;QString measures;for(const auto &i:s.items)if(i.kind==Tikz::Kind::Point&&i.located){const int n=expected.size();expected.append(i.position);measures+=QString("\\begingroup\\pgftransformreset\\pgfextractx\\PX{\\pgfpointanchor{%1}{center}}\\pgfextracty\\PY{\\pgfpointanchor{%1}{center}}\\typeout{TAN%2=\\the\\PX,\\the\\PY}\\endgroup\n").arg(i.id).arg(n);}auto source=s.source;source.replace("\\end{tikzpicture}",measures+"\\end{tikzpicture}");tex+="{\n"+source+"\n}\\clearpage\n";}
    tex+="\\end{document}";put(folder+"/tangents.tex",tex.toUtf8());QProcess compile;compile.setWorkingDirectory(folder);compile.start("C:/texlive/2023/bin/windows/pdflatex.exe",{"-interaction=nonstopmode","-halt-on-error","tangents.tex"});check("real PGF compiles tangent sources",compile.waitForFinished(45000)&&compile.exitCode()==0);
    const auto log=QString::fromUtf8(bytes(folder+"/tangents.log"));QJsonArray measures;
    for(int n=0;n<expected.size();++n){const auto m=QRegularExpression(QString("TAN%1=([+-]?[0-9.]+)pt,([+-]?[0-9.]+)pt").arg(n)).match(log);const QPointF actual(m.captured(1).toDouble()*2.54/72.27,m.captured(2).toDouble()*2.54/72.27);const double error=QLineF(actual,expected[n]).length();check(QString("PGF tangent anchor %1").arg(n),m.hasMatch()&&error<.01);measures.append(QJsonObject{{"found",m.hasMatch()},{"errorCm",error}});}put(folder+"/pgf-measurements.json",QJsonDocument(measures).toJson());
    Tw::Settings().setValue("openPDFwithTeX",false);Tw::Settings().setValue("researchFocusMode",false);
    const auto original=picture("\\x","\\x*\\x/2");const QString path=folder+"/paper/main.tex",text="\\documentclass{article}\n\\usepackage{tikz}\n\\begin{document}\nDiagram.\n\\end{document}\n";
    put(path,text.toUtf8());auto *w=new TeXDocumentWindow(path);w->resize(qEnvironmentVariableIsSet("TEXLINE_TEST_HALF")?680:1120,760);w->show();settle();if(qEnvironmentVariableIsSet("TEXLINE_TEST_FOCUS")){setFocusMode(true);settle();}
    auto cursor=w->textCursor();cursor.setPosition(text.indexOf("Diagram."));w->editor()->setTextCursor(cursor);openTikz(w,true,{},original);settle();
    auto *dialog=dynamic_cast<TikzDialog*>(w->findChild<QDialog*>("researchTikzDialog"));auto *canvas=dialog?dynamic_cast<TikzCanvas*>(dialog->findChild<QWidget*>("researchTikzCanvas")):nullptr;check("native tangent workbench opens",canvas);if(!canvas){app.exit(1);return;}
    auto pointer=[&](QEvent::Type type,const QPointF &p,Qt::MouseButton button,Qt::MouseButtons buttons){QMouseEvent e(type,p,canvas->mapToGlobal(p.toPoint()),button,buttons,Qt::NoModifier);QApplication::sendEvent(canvas,&e);};
    auto *action=dialog->findChild<QAction*>("researchTikzConstruct_tangent");check("tangent uses existing Geometry menu",action);if(!action){app.exit(1);return;}
    action->trigger();key(canvas,Qt::Key_Escape);check("Escape before construction preserves source and canvas focus",canvas->model().source==original&&canvas->hasFocus());
    action->trigger();const auto onCurve=canvas->screen({1,.5});pointer(QEvent::MouseButtonPress,onCurve,Qt::LeftButton,Qt::LeftButton);pointer(QEvent::MouseButtonRelease,onCurve,Qt::LeftButton,Qt::NoButton);settle();
    check("canvas creates bound point and tangent together",canvas->model().items.size()==6&&!canvas->model().partial);if(canvas->model().items.size()!=6){app.exit(1);return;}
    const auto added=canvas->model().source;canvas->setFocus();key(canvas,Qt::Key_Z,Qt::ControlModifier);check("one draft undo removes entire tangent construction",canvas->model().source==original);key(canvas,Qt::Key_Y,Qt::ControlModifier);check("redo restores tangent",canvas->model().source==added);
    canvas->choose(find(canvas->model(),"A"));auto *parameter=dialog->findChild<QLineEdit*>("researchTikzParameter");parameter->setFocus();parameter->selectAll();key(parameter,Qt::Key_1,Qt::NoModifier,"1.25");key(parameter,Qt::Key_Return);settle();
    check("native parameter edit refreshes tangent",close(pos(canvas->model(),"tlh1")-pos(canvas->model(),"A"),unit({1,1.25})));
    const auto beforeDrag=canvas->model().source;canvas->fit();dialog->grab().save(folder+"/tangent-before.png");const auto from=canvas->screen(pos(canvas->model(),"A")),to=canvas->screen({.7,.245});
    pointer(QEvent::MouseButtonPress,from,Qt::LeftButton,Qt::LeftButton);pointer(QEvent::MouseMove,to,Qt::NoButton,Qt::LeftButton);key(canvas,Qt::Key_Escape);pointer(QEvent::MouseButtonRelease,to,Qt::LeftButton,Qt::NoButton);check("Escape cancels point and tangent drag",canvas->model().source==beforeDrag);
    const auto frames=folder+"/actual-tangent-drag";QDir().mkpath(frames);dialog->grab().save(frames+"/00.png");pointer(QEvent::MouseButtonPress,from,Qt::LeftButton,Qt::LeftButton);
    for(int n=1;n<=8;++n){pointer(QEvent::MouseMove,from+(to-from)*n/8.,Qt::NoButton,Qt::LeftButton);settle(18);dialog->grab().save(frames+QString("/%1.png").arg(n,2,10,QLatin1Char('0')));}pointer(QEvent::MouseButtonRelease,to,Qt::LeftButton,Qt::NoButton);settle();
    check("drag retains analytic tangent",close(pos(canvas->model(),"tlh1")-pos(canvas->model(),"A"),unit({1,pos(canvas->model(),"A").x()})));const auto dragged=canvas->model().source;dialog->grab().save(folder+"/tangent-after.png");key(canvas,Qt::Key_Z,Qt::ControlModifier);check("one undo restores point and tangent",canvas->model().source==beforeDrag);key(canvas,Qt::Key_Y,Qt::ControlModifier);
    canvas->choose(0);auto *expression=dialog->findChild<QLineEdit*>("researchTikzExpression");expression->setFocus();expression->selectAll();key(expression,Qt::Key_1,Qt::NoModifier,"t*t/3");key(expression,Qt::Key_Return);settle();
    check("native curve edit refreshes tangent direction",close(pos(canvas->model(),"tlh1")-pos(canvas->model(),"A"),unit({1,2*pos(canvas->model(),"A").x()/3})));
    const auto applied=canvas->model().source;canvas->setFocus();key(canvas,Qt::Key_Z,Qt::ControlModifier);check("one undo restores curve and tangent together",canvas->model().source==dragged);key(canvas,Qt::Key_Y,Qt::ControlModifier);check("redo restores edited curve and tangent",canvas->model().source==applied);dialog->grab().save(folder+"/tangent-expression.png");
    canvas->choose(canvas->model().items.size()-1);key(canvas,Qt::Key_Delete);settle();check("native delete removes tangent and unused declarations",canvas->model().functions.size()==2&&canvas->model().items.size()==3);key(canvas,Qt::Key_Z,Qt::ControlModifier);check("one undo restores removed tangent relation",canvas->model().source==applied);
    auto *mode=dialog->findChild<QComboBox*>("researchTikzMode");mode->setCurrentIndex(2);check("native tangent preview compiles",waitFor([&]{return dialog->findChild<QLabel*>("researchTikzPreview")->property("researchDraftReady").toBool();},45000));dialog->grab().save(folder+"/tangent-typeset.png");mode->setCurrentIndex(0);
    dialog->findChild<QDialogButtonBox*>("researchTikzButtons")->button(QDialogButtonBox::Apply)->click();settle();check("Apply writes tangent source",w->textDoc()->editorText().contains(applied));w->editor()->undo();check("one manuscript undo restores text",w->textDoc()->editorText()==text);w->editor()->redo();
    cursor=w->textCursor();cursor.setPosition(w->textDoc()->editorText().indexOf("plot"));w->editor()->setTextCursor(cursor);editResearchObject(w);settle();dialog=dynamic_cast<TikzDialog*>(w->findChild<QDialog*>("researchTikzDialog"));check("existing tangent drawing reopens",dialog);if(dialog){canvas=dynamic_cast<TikzCanvas*>(dialog->findChild<QWidget*>("researchTikzCanvas"));check("source recovers verified tangent relation",canvas&&canvas->model().source==applied&&!canvas->model().partial);key(dialog,Qt::Key_Escape);settle();}
    check("Cancel restores editor focus",w->editor()->hasFocus());w->editor()->undo();check("Cancel preserves disk and original text",bytes(path)==text.toUtf8()&&w->textDoc()->editorText()==text);w->textDoc()->setModified(false);w->close();app.exit(failures?1:0);
  });return app.exec();
}
