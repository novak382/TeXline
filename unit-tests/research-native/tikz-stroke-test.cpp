// Original numeric TikZ dash patterns, native widgets and real TeX output.
#include "ActualAuthoringRecording.h"
#include "ResearchAuthoring.cpp"
using namespace Research;
int main(int argc,char **argv) {
  const auto report=argument(1);argc=1;
  QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
  TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
  QTimer::singleShot(50,&app,[&]{
    QJsonArray checks;int failures=0;const auto folder=QFileInfo(report).absolutePath();
    auto check=[&](const QString &name,bool ok){checks.append(QJsonObject{{"name",name},{"passed",ok}});failures+=!ok;put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"devicePixelRatio",qApp->primaryScreen()->devicePixelRatio()}}).toJson());};
    check("inactive private desktop",qEnvironmentVariable("TEXLINE_TEST_DESKTOP").startsWith("TeXlineValidation-"));
    Tw::Settings().setValue("openPDFwithTeX",false);Tw::Settings().setValue("researchFocusMode",false);
    auto picture=[](const QString &options){return QStringLiteral("\\begin{tikzpicture}\n% Preserve original source.\n\\draw[black,line width=.8pt,")+options+QStringLiteral("] (-2,0)--(2,0);\n\\end{tikzpicture}");};
    const QString sample=picture("dash pattern=on 4pt off 2pt");
    for(const auto &pattern:QStringList{"on 1pt off 1pt","on 2pt off 2pt","on 3.8pt off 3.8pt","on 4pt off 2pt","on 6pt off 6pt"}) {
      const auto s=Tikz::parse(picture("dash pattern="+pattern));
      check("original corpus dash is editable: "+pattern,s.valid&&!s.partial&&s.items.size()==1&&s.items[0].movable);
      check("dash source survives coordinate edit: "+pattern,Tikz::moved(s,0,{0,1})==QString(s.source).replace("(-2,0)--(2,0)","(-2cm,1cm)--(2cm,1cm)"));
    }
    auto metric=Tikz::parse(picture("dash pattern=on 1mm off .1cm,dash phase=-2pt"));
    check("dash lengths normalize physical units without changing source",!metric.partial&&metric.items.size()==1&&metric.items[0].dashPattern.size()==2&&qAbs(metric.items[0].dashPattern[0]-72.27/25.4)<1e-10&&qAbs(metric.items[0].dashPattern[1]-72.27/25.4)<1e-10&&qAbs(metric.items[0].dashPhase+2)<1e-10);
    for(const auto &bad:QStringList{"on 3pt off -2pt","on 3pt off 0pt","off 2pt on 3pt","on 3pt","on \\mydash off 2pt","on 3pt off 2pt trailing","on 1001pt off 1pt"}) {
      const auto s=Tikz::parse(picture("dash pattern="+bad));check("unsupported pattern retained source-only: "+bad,s.partial&&s.items.size()==1&&!s.items[0].movable&&s.items[0].dashPattern.isEmpty()&&s.source==picture("dash pattern="+bad));
      check("invalid pattern is never written by properties",Tikz::property(Tikz::parse(sample),0,"dash pattern",bad).isEmpty());
    }
    const QString inherited="\\begin{tikzpicture}[dash pattern=on 6pt off 2pt]\\draw[solid] (0,0)--(1,0);\\end{tikzpicture}";
    check("explicit solid overrides inherited pattern",Tikz::parse(inherited).items[0].dashPattern.isEmpty());
    const QString named="\\begin{tikzpicture}[hidden/.style={dash pattern=on 6pt off 2pt}]\\draw[hidden] (0,0)--(1,0);\\end{tikzpicture}";
    const auto namedScene=Tikz::parse(named);auto namedSolid=Tikz::parse(Tikz::property(namedScene,0,"line style","solid"));
    check("switching named dash style to solid writes an explicit final override",namedSolid.source.contains("[hidden,solid]")&&namedSolid.items[0].dashPattern.isEmpty());
    auto namedCustom=Tikz::parse(Tikz::property(namedScene,0,"dash pattern","on 2pt off 1pt"));
    check("custom override leaves named style definition intact",namedCustom.source.contains("hidden/.style={dash pattern=on 6pt off 2pt}")&&namedCustom.items[0].dashPattern==QVector<qreal>({2,1}));
    for(const auto &pair:QList<QPair<QString,int>>{{"dash pattern=on 4pt off 2pt,dashed",0},{"dashed,dash pattern=on 4pt off 2pt",1}}){const auto item=Tikz::parse(picture(pair.first)).items[0];check("last explicit dash option wins: "+pair.first,item.dashPattern==QVector<qreal>(pair.second?QVector<qreal>{4,2}:QVector<qreal>{3,3}));}
    const auto phased=Tikz::parse(picture("dash pattern=on 4pt off 2pt,dash phase=0.123456789pt"));
    check("unrelated width edit preserves precise phase bytes",Tikz::property(phased,0,"line width","1.2pt").contains("dash phase=0.123456789pt"));
    const auto defaults=Tikz::parse("\\begin{tikzpicture}[dash pattern=on 6pt off 6pt,every path/.style={solid}]\\draw (0,0)--(1,0);\\end{tikzpicture}");
    check("every-path style overrides picture dash",defaults.items.size()==1&&defaults.items[0].dashPattern.isEmpty());
    const QString root=folder+"/paper/main.tex",original="\\documentclass{article}\n\\usepackage{tikz}\n\\begin{document}\nA drawing.\n\\end{document}\n";
    put(root,original.toUtf8());auto *w=new TeXDocumentWindow(root);w->resize(qEnvironmentVariableIsSet("TEXLINE_TEST_HALF")?680:1120,760);w->show();settle();
    if(qEnvironmentVariableIsSet("TEXLINE_TEST_FOCUS")){setFocusMode(true);settle();}
    auto c=w->textCursor();c.setPosition(original.indexOf("A drawing."));w->editor()->setTextCursor(c);
    openTikz(w,true,{},sample);settle();
    auto *d=dynamic_cast<TikzDialog*>(w->findChild<QDialog*>("researchTikzDialog"));auto *canvas=d?dynamic_cast<TikzCanvas*>(d->findChild<QWidget*>("researchTikzCanvas")):nullptr;
    check("existing custom dash opens",canvas);if(!canvas){app.exit(1);return;}
    canvas->choose(-1);canvas->fit();settle();d->grab().save(folder+"/dash-drawing.png");
    const auto raster=canvas->grab().toImage();const double dpr=raster.devicePixelRatio();
    auto left=canvas->screen({-1.5,0})*dpr,right=canvas->screen({1.5,0})*dpr;int runs=0;bool ink=false;
    for(int x=qRound(left.x());x<=qRound(right.x());++x){const auto pixel=raster.pixelColor(x,qRound(left.y()));bool now=pixel.red()<160&&pixel.green()<160&&pixel.blue()<160;if(now&&!ink)++runs;ink=now;}
    check("actual canvas raster contains separated dashes",runs>=6);
    // Sample the actual native painter, independently of the data-only checks.
    TikzCanvas probe(nullptr);probe.resize(700,180);
    auto wave=[&](double width,double phase){auto s=Tikz::parse(picture(QStringLiteral("dash pattern=on 4pt off 2pt,dash phase=%1pt").arg(phase)));s=Tikz::parse(Tikz::property(s,0,"line width",QString::number(width)+"pt"));probe.setScene(s,true);auto image=probe.grab().toImage();const auto ratio=image.devicePixelRatio();const auto a=probe.screen({-2,0})*ratio,b=probe.screen({2,0})*ratio;QVector<bool> row;for(int x=qRound(a.x())+1;x<qRound(b.x())-1;++x){const auto p=image.pixelColor(x,qRound(a.y()));row.append(p.red()<160&&p.green()<160&&p.blue()<160);}return row;};
    auto starts=[](const QVector<bool> &row){QVector<int> out;bool old=false;for(int i=0;i<row.size();++i){if(row[i]&&!old)out.append(i);old=row[i];}return out;};
    const auto narrow=starts(wave(.8,0)),thick=starts(wave(2,0));bool same=narrow.size()==thick.size();for(int i=0;i<qMin(narrow.size(),thick.size());++i)same&=qAbs(narrow[i]-thick[i])<=1;
    check("dash physical period stays fixed when line width changes",same&&narrow.size()>8);
    const auto zeroWave=wave(.8,0),positive=wave(.8,4),negative=wave(.8,-2);check("positive phase begins inside the gap",zeroWave.value(1)&&!positive.value(1));
    check("negative phase wraps to the same pattern",positive==negative);
    auto *mode=d->findChild<QComboBox*>("researchTikzMode");mode->setCurrentIndex(2);
    check("original dash compiles in actual local TeX",waitFor([&]{return d->findChild<QLabel*>("researchTikzPreview")->property("researchDraftReady").toBool();},45000));
    d->grab().save(folder+"/dash-typeset.png");mode->setCurrentIndex(0);canvas->choose(0);
    auto *style=d->findChild<QComboBox*>("researchTikzLineStyle");int custom=style->findData("custom");
    check("custom dash reuses existing line-style menu",custom>=0);
    if(custom>=0){
      auto openForm=[&]{style->setCurrentIndex(custom);QMetaObject::invokeMethod(style,"activated",Qt::DirectConnection,Q_ARG(int,custom));settle();return d->findChild<QDialog*>("researchTikzDashDialog");};
      auto *form=openForm();check("custom dash form opens on demand",form);
      if(form){
        auto *pattern=form->findChild<QLineEdit*>("researchTikzDashPattern");auto *buttons=form->findChild<QDialogButtonBox*>("researchTikzDashButtons");
        check("original lengths populate editable form",pattern&&pattern->text()=="4 2");
        const auto draft=canvas->model().source;pattern->setFocus();pattern->selectAll();key(pattern,Qt::Key_1,Qt::NoModifier,"4 -2");buttons->button(QDialogButtonBox::Ok)->click();settle();
        check("invalid dash retains input and draft",form->isVisible()&&pattern->text()=="4 -2"&&canvas->model().source==draft);
        pattern->selectAll();key(pattern,Qt::Key_1,Qt::NoModifier,"6 2 1 2");d->grab().save(folder+"/dash-form-owner.png");form->grab().save(folder+"/dash-form.png");key(form,Qt::Key_Escape);settle();
        check("form Escape restores source and canvas focus",canvas->model().source==draft&&!d->findChild<QDialog*>("researchTikzDashDialog")&&canvas->hasFocus());
        form=openForm();pattern=form->findChild<QLineEdit*>("researchTikzDashPattern");pattern->setFocus();pattern->selectAll();key(pattern,Qt::Key_1,Qt::NoModifier,"6 2 1 2");form->findChild<QDialogButtonBox*>("researchTikzDashButtons")->button(QDialogButtonBox::Ok)->click();settle();
        check("compound pattern commits one draft change",canvas->model().source.contains("dash pattern=on 6pt off 2pt on 1pt off 2pt")&&!canvas->model().partial);
        const auto compound=canvas->model().source;key(canvas,Qt::Key_Z,Qt::ControlModifier);check("one draft undo restores exact imported stroke",canvas->model().source==draft);key(canvas,Qt::Key_Y,Qt::ControlModifier);check("redo restores compound dash",canvas->model().source==compound);
        canvas->choose(0);const auto start=canvas->screen({0,0}),end=start+QPointF(0,24);
        auto pointer=[&](QEvent::Type type,const QPointF &p,Qt::MouseButton button,Qt::MouseButtons buttons){QMouseEvent e(type,p,canvas->mapToGlobal(p.toPoint()),button,buttons,Qt::NoModifier);QApplication::sendEvent(canvas,&e);};
        pointer(QEvent::MouseButtonPress,start,Qt::LeftButton,Qt::LeftButton);pointer(QEvent::MouseMove,end,Qt::NoButton,Qt::LeftButton);key(canvas,Qt::Key_Escape);pointer(QEvent::MouseButtonRelease,end,Qt::LeftButton,Qt::NoButton);check("Escape cancels an active custom-dash drag",canvas->model().source==compound);
        pointer(QEvent::MouseButtonPress,start,Qt::LeftButton,Qt::LeftButton);pointer(QEvent::MouseMove,end,Qt::NoButton,Qt::LeftButton);pointer(QEvent::MouseButtonRelease,end,Qt::LeftButton,Qt::NoButton);check("actual pointer events drag the custom line and retain its pattern",canvas->model().source!=compound&&canvas->model().source.contains("on 6pt off 2pt on 1pt off 2pt"));key(canvas,Qt::Key_Z,Qt::ControlModifier);check("one draft undo restores custom-line drag",canvas->model().source==compound);
        canvas->choose(0);style->setCurrentIndex(0);QMetaObject::invokeMethod(style,"activated",Qt::DirectConnection,Q_ARG(int,0));check("switch to solid removes effective custom dashes",!canvas->model().items[0].style.contains("dash pattern"));key(canvas,Qt::Key_Z,Qt::ControlModifier);
        mode->setCurrentIndex(2);check("new compound pattern compiles",waitFor([&]{return d->findChild<QLabel*>("researchTikzPreview")->property("researchDraftReady").toBool();},45000));d->grab().save(folder+"/compound-typeset.png");mode->setCurrentIndex(0);
        d->findChild<QDialogButtonBox*>("researchTikzButtons")->button(QDialogButtonBox::Apply)->click();settle();const auto applied=w->textDoc()->editorText();check("Apply inserts complete custom drawing",applied.contains(compound));w->editor()->undo();check("one manuscript undo restores original",w->textDoc()->editorText()==original);w->editor()->redo();check("manuscript redo restores exact drawing",w->textDoc()->editorText()==applied);
        auto cursor=w->textCursor();cursor.setPosition(applied.indexOf("dash pattern"));w->editor()->setTextCursor(cursor);editResearchObject(w);settle();d=dynamic_cast<TikzDialog*>(w->findChild<QDialog*>("researchTikzDialog"));check("applied custom drawing reopens",d);if(d){auto *reopened=dynamic_cast<TikzCanvas*>(d->findChild<QWidget*>("researchTikzCanvas"));check("reopening retains compound pattern",reopened&&!reopened->model().partial&&reopened->model().source==compound);key(d,Qt::Key_Escape);settle();}w->editor()->undo();
      }
    }
    if(auto *open=w->findChild<QDialog*>("researchTikzDialog")){key(open,Qt::Key_Escape);settle();}
    // Both fields preserve the untouched original representation, including units.
    QStringList longPattern;for(int n=0;n<32;++n)longPattern.append((n%2?QStringLiteral("off "):QStringLiteral("on "))+"999.9999pt");
    for(int scenario=0;scenario<3;++scenario){
      const auto exact=picture(scenario==0?"dash pattern="+longPattern.join(' '):"dash pattern=on .123456789cm off 2bp,dash phase=.123456789pt");
      openTikz(w,true,{},exact);settle();d=dynamic_cast<TikzDialog*>(w->findChild<QDialog*>("researchTikzDialog"));canvas=d?dynamic_cast<TikzCanvas*>(d->findChild<QWidget*>("researchTikzCanvas")):nullptr;check("precision drawing opens",canvas);if(!canvas){app.exit(1);return;}canvas->choose(0);style=d->findChild<QComboBox*>("researchTikzLineStyle");custom=style->findData("custom");style->setCurrentIndex(custom);QMetaObject::invokeMethod(style,"activated",Qt::DirectConnection,Q_ARG(int,custom));settle();auto *form=d->findChild<QDialog*>("researchTikzDashDialog");check("precision form opens",form);if(!form){key(d,Qt::Key_Escape);settle();continue;}
      auto *pattern=form->findChild<QLineEdit*>("researchTikzDashPattern");if(scenario==0)check("all32 long dash lengths fit without truncation",pattern->text().split(' ',Qt::SkipEmptyParts).size()==32&&pattern->text().size()==287);
      if(scenario==1){pattern->setFocus();pattern->selectAll();key(pattern,Qt::Key_1,Qt::NoModifier,"4 2");}
      if(scenario==2)form->findChild<QDoubleSpinBox*>("researchTikzDashPhase")->setValue(3);
      form->findChild<QDialogButtonBox*>("researchTikzDashButtons")->button(QDialogButtonBox::Ok)->click();settle();
      check("precision form confirms and closes",!d->findChild<QDialog*>("researchTikzDashDialog"));
      check(scenario==0?"unchanged long dash confirms without source changes":scenario==1?"pattern-only edit retains precise original phase":"phase-only edit retains exact pattern units and precision",scenario==0?canvas->model().source==exact:scenario==1?canvas->model().source!=exact&&canvas->model().source.contains("dash pattern=on 4pt off 2pt")&&canvas->model().source.contains("dash phase=.123456789pt"):canvas->model().source!=exact&&canvas->model().source.contains("dash phase=3pt")&&canvas->model().source.contains("dash pattern=on .123456789cm off 2bp"));
      if(scenario){key(canvas,Qt::Key_Z,Qt::ControlModifier);check("precision edit undo restores all original bytes",canvas->model().source==exact);}key(d,Qt::Key_Escape);settle();
    }
    check("Cancel and tests leave manuscript and original disk unchanged",w->textDoc()->editorText()==original&&bytes(root)==original.toUtf8());w->textDoc()->setModified(false);w->close();settle();app.exit(failures?1:0);
  });return app.exec();
}
