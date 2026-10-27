// Bounded PGF function declarations. Included inside the parser's private namespace.
// Only independent, single-parameter arithmetic is evaluated; TeX macros are not run.
bool coordinateNodeModifiersKnown(const QString &source){
  return !QRegularExpression(QStringLiteral(R"(every (?:coordinate node|node|path)\s*/\.(?!style\s*=)|\bnodes\s*=)" )).match(source).hasMatch();
}
bool coordinateNodeStylesSafe(const QMap<QString,QString> &defs,const QString &source){
  if(!coordinateNodeModifiersKnown(source))return false;
  const auto path=style(defs.value(QStringLiteral("every path")),defs);
  if(!safeStyle(path)||path.contains(QStringLiteral("xshift"))||path.contains(QStringLiteral("yshift")))return false;
  const QSet<QString> appearanceKeys{QStringLiteral("font"),QStringLiteral("text"),QStringLiteral("color"),QStringLiteral("draw"),QStringLiteral("fill"),QStringLiteral("inner sep"),QStringLiteral("outer sep"),QStringLiteral("opacity"),QStringLiteral("draw opacity"),QStringLiteral("fill opacity")};
  for(const auto &name:{QStringLiteral("every node"),QStringLiteral("every coordinate node")}){
    const auto inherited=style(defs.value(name),defs);
    for(auto i=inherited.begin();i!=inherited.end();++i)if(!appearanceKeys.contains(i.key())&&!color(i.key(),QColor()).isValid())return false;
  }return true;
}
QString parameterExpression(QString expression,const QString &from,const QString &to){
  return expression.replace(QRegularExpression(QRegularExpression::escape(from)+QStringLiteral("(?![A-Za-z])")),to);
}
bool readCurveFunctions(Scene &scene,const Span &picture){
  int offset=picture.a+1;bool found=false;
  for(const auto &raw:options(picture.text)){
    const auto entry=option(raw);
    if(entry.first==QStringLiteral("declare function")){
      if(found)return false;found=true;
      int at=raw.indexOf(QLatin1Char('='))+1;const auto body=group(raw,at,QLatin1Char('{'),QLatin1Char('}'));
      if(!body.valid()||!raw.mid(at).trimmed().isEmpty())return false;
      scene.functionsEnd=offset+body.b-1;
      const QRegularExpression definition(QStringLiteral(R"(\s*([A-Za-z][A-Za-z0-9_]*)\s*\(\s*(\\[A-Za-z]+)\s*\)\s*=\s*([^;{}]+);)"));
      int pos=0;
      while(pos<body.text.size()){
        if(body.text.mid(pos).trimmed().isEmpty())break;
        const auto m=definition.match(body.text,pos);
        if(!m.hasMatch()||m.capturedStart()!=pos||scene.functions.size()>=48)return false;
        const auto name=m.captured(1),variable=m.captured(2),expression=m.captured(3);
        bool builtin=false,binary=false,valid=false;evaluate(name+QStringLiteral("(0)"),0,&builtin);evaluate(name+QStringLiteral("(0,1)"),0,&binary);
        evaluate(parameterExpression(expression,variable,QStringLiteral("\\x")),0,&valid);
        if(builtin||binary||scene.functions.contains(name)||!valid||expression.size()>256)return false;
        const int start=offset+body.a+1+int(m.capturedStart(3));
        scene.functions.insert(name,{variable,expression,start,start+int(expression.size())});pos=int(m.capturedEnd());
      }
    }
    offset+=int(raw.size())+1;
  }
  return true;
}
bool curveCall(const Scene &scene,QString expression,QString &name,QString &argument){
  expression=expression.trimmed();if(expression.startsWith(QLatin1Char('{'))&&expression.endsWith(QLatin1Char('}')))expression=expression.mid(1,expression.size()-2).trimmed();
  const auto m=QRegularExpression(QStringLiteral(R"(^([A-Za-z][A-Za-z0-9_]*)\s*\(\s*([^()]*)\s*\)$)")).match(expression);
  if(!m.hasMatch()||!scene.functions.contains(m.captured(1)))return false;
  name=m.captured(1);argument=m.captured(2).trimmed();return true;
}
double ellipseRadius(const Scene &scene,QString text,QString &function,bool &ok){
  // A physical radius can share an ordinary local PGF declaration with its points.
  const auto m=QRegularExpression(QStringLiteral(R"(^\s*\{([A-Za-z][A-Za-z0-9_]*)\(0\)\*1cm\}\s*$)")).match(text);
  if(!m.hasMatch())return length(text,&ok);
  function=m.captured(1);if(!scene.functions.contains(function)){ok=false;return 0;}
  const auto &f=scene.functions[function];const double value=evaluate(parameterExpression(f.expression,f.variable,QStringLiteral("\\x")),0,&ok);
  ok&=std::isfinite(value)&&value>0&&value<10000;return value;
}
bool ellipseCoordinate(const Coordinate &c){return c.operation==QStringLiteral("ellipsePoint")||c.operation==QStringLiteral("circlePoint");}
bool readEllipseCoordinate(const Scene &scene,const Span &span,Coordinate &c){
  QString raw=span.text;raw.remove(QRegularExpression(QStringLiteral("\\s+")));
  const auto ellipse=QRegularExpression(QStringLiteral(R"(^\$\(([A-Za-z][A-Za-z0-9_-]*)\)\+\(\{([A-Za-z][A-Za-z0-9_]*)\(0\)\*cos\(([+\-0-9.]+)\)\*1cm\},\{([A-Za-z][A-Za-z0-9_]*)\(0\)\*sin\(([+\-0-9.]+)\)\*1cm\}\)\$$)")).match(raw);
  const auto circle=QRegularExpression(QStringLiteral(R"(^\$\(([A-Za-z][A-Za-z0-9_-]*)\)!1!([+\-0-9.]+):\(([A-Za-z][A-Za-z0-9_-]*)\)\$$)")).match(raw);
  if(!ellipse.hasMatch()&&!circle.hasMatch())return false;
  bool ok=false;const double t=(ellipse.hasMatch()?ellipse.captured(3):circle.captured(2)).toDouble(&ok);
  if(!ok||!std::isfinite(t)||qAbs(t)>10000)return false;
  if(ellipse.hasMatch()){
    if(ellipse.captured(3)!=ellipse.captured(5))return false;
    for(int n:{2,4}){QString function;bool valid=false;ellipseRadius(scene,QStringLiteral("{%1(0)*1cm}").arg(ellipse.captured(n)),function,valid);if(!valid)return false;c.curveFunctions.append(function);}
    c.operation=QStringLiteral("ellipsePoint");c.references={ellipse.captured(1)};
  }else{c.operation=QStringLiteral("circlePoint");c.references={circle.captured(1),circle.captured(3)};}
  c.parameter=t;return true;
}
int ellipseForPoint(const Scene &scene,const Coordinate &point){
  int found=-1;
  for(int n=0;n<scene.items.size();++n){const auto &i=scene.items[n];if(i.kind!=Kind::Ellipse)continue;
    const bool matches=point.operation==QStringLiteral("circlePoint")?i.relation==QStringLiteral("through")&&i.coordinates.size()==2&&i.coordinates[0].reference==point.references.value(0)&&i.coordinates[1].reference==point.references.value(1):i.curveFunctions==point.curveFunctions&&!point.curveFunctions.isEmpty()&&i.coordinates.size()==1&&i.coordinates[0].reference==point.references.value(0);
    if(matches&&pointCurve(scene,n)){if(found>=0)return -1;found=n;}
  }return found;
}
QPointF ellipsePointPosition(const Scene &scene,const Coordinate &c,const QVector<QPointF> &p){
  const double a=qDegreesToRadians(c.parameter);
  if(c.operation==QStringLiteral("circlePoint")&&p.size()==2){const auto d=p[1]-p[0];const auto sx=scene.canvasScale.x(),sy=scene.canvasScale.y();return p[0]+QPointF(d.x()*std::cos(a)-d.y()*sx/sy*std::sin(a),d.x()*sy/sx*std::sin(a)+d.y()*std::cos(a));}
  if(c.operation==QStringLiteral("ellipsePoint")&&p.size()==1&&c.curveFunctions.size()==2){
    double radius[2];for(int n=0;n<2;++n){QString function;bool ok=false;radius[n]=ellipseRadius(scene,QStringLiteral("{%1(0)*1cm}").arg(c.curveFunctions[n]),function,ok);if(!ok)return {nan,nan};}
    return p[0]+QPointF(radius[0]*std::cos(a)*scene.canvasScale.x(),radius[1]*std::sin(a)*scene.canvasScale.y());
  }return {nan,nan};
}
bool readTangentCoordinate(const Scene &scene,const Span &span,Coordinate &c){
  auto raw=span.text;raw.remove(QRegularExpression(QStringLiteral("\\s+")));
  const auto m=QRegularExpression(QStringLiteral(R"(^\$\(([A-Za-z][A-Za-z0-9_-]*)\)\+\(\{2\*\(([A-Za-z][A-Za-z0-9_]*)\(([+\-0-9.]+)\)/veclen\(\2\(\3\),([A-Za-z][A-Za-z0-9_]*)\(\3\)\)\)\},\{2\*\(\4\(\3\)/veclen\(\2\(\3\),\4\(\3\)\)\)\}\)\$$)")).match(raw);
  if(!m.hasMatch()||!scene.functions.contains(m.captured(2))||!scene.functions.contains(m.captured(4))||m.captured(2)==m.captured(4))return false;
  bool ok=false;const double t=m.captured(3).toDouble(&ok);if(!ok||!std::isfinite(t)||qAbs(t)>10000)return false;
  c.operation=QStringLiteral("curveTangent");c.references={m.captured(1)};c.curveFunctions={m.captured(2),m.captured(4)};c.parameter=t;return true;
}
bool readCurveCoordinate(const Scene &scene,const Span &span,Coordinate &c){
  if(readTangentCoordinate(scene,span,c))return true;
  if(readEllipseCoordinate(scene,span,c))return true;
  const auto parts=options(span.text);if(parts.size()!=2&&parts.size()!=3)return false;
  QStringList names;std::array<double,3> values{};double parameter=0;
  for(int n=0;n<parts.size();++n){
    QString name,argument;if(!curveCall(scene,parts[n],name,argument))return false;
    bool ok=false;const double t=argument.toDouble(&ok);
    if(!ok||!std::isfinite(t)||qAbs(t)>10000||(n&&t!=parameter))return false;
    parameter=t;const auto &f=scene.functions[name];values[n]=evaluate(parameterExpression(f.expression,f.variable,QStringLiteral("\\x")),t,&ok);
    if(!ok||!std::isfinite(values[n])||qAbs(values[n])>=10000)return false;names.append(name);
  }
  c.curveFunctions=names;c.parameter=parameter;c.values=values;c.numeric=true;c.spatial=parts.size()==3;
  c.point={values[0],values[1]};return true;
}
void expandCurvePlot(const Scene &scene,Item &item){
  QStringList names;QStringList expanded;const QStringList expressions{item.expressionX,item.expression,item.expressionZ};
  for(int n=0;n<(item.spatial?3:2);++n){
    QString name,argument;if(!curveCall(scene,expressions[n],name,argument)||argument!=item.variable)return;
    const auto &f=scene.functions[name];names.append(name);expanded.append(parameterExpression(f.expression,f.variable,item.variable));
  }
  if(QSet<QString>(names.begin(),names.end()).size()!=names.size())return;
  item.curveFunctions=names;item.expressionX=expanded[0];item.expression=expanded[1];if(item.spatial)item.expressionZ=expanded[2];
}
int boundCurve(const Scene &scene,const Coordinate &point){
  if(conicCoordinate(point)||quadraticCoordinate(point))return -1; // Derived contact/helper points have no free angle or parameter.
  if(ellipseCoordinate(point))return ellipseForPoint(scene,point);
  if(point.curveFunctions.isEmpty())return -1;
  int only=-1,inside=-1,count=0,insideCount=0;
  for(int n=0;n<scene.items.size();++n){const auto &curve=scene.items[n];if(curve.kind!=Kind::Plot||curve.curveFunctions!=point.curveFunctions||!curve.shift.isNull())continue;
    only=n;++count;if(point.parameter>=curve.domainMin&&point.parameter<=curve.domainMax){inside=n;++insideCount;}
  }
  return count==1?only:insideCount==1?inside:-1;
}
bool curveValues(const Item &curve,double t,std::array<double,3> &v){
  const QStringList expressions{curve.expressionX,curve.expression,curve.expressionZ};
  for(int n=0;n<(curve.spatial?3:2);++n){bool ok=false;v[n]=evaluate(parameterExpression(expressions[n],curve.variable,QStringLiteral("\\x")),t,&ok);if(!ok||!std::isfinite(v[n])||qAbs(v[n])>=10000)return false;}return true;
}
QPointF tangentDirection(const Scene &scene,const Coordinate &c){
  if(c.references.size()!=1||c.curveFunctions.size()!=2)return {nan,nan};
  for(const auto &point:scene.items)if(point.id==c.references[0]&&point.kind==Kind::Point&&point.coordinates.size()==1){
    const auto &p=point.coordinates[0];const int index=boundCurve(scene,p);
    if(index<0||scene.items[index].kind!=Kind::Plot||scene.items[index].spatial||p.curveFunctions.size()!=2||p.parameter!=c.parameter||!point.shift.isNull()||!point.placementShift.isNull())return {nan,nan};
    QPointF v;for(int n=0;n<2;++n){
      const auto &f=scene.functions[p.curveFunctions[n]],&d=scene.functions[c.curveFunctions[n]];
      const auto expected=derivative(f.expression,f.variable),actual=parameterExpression(d.expression,d.variable,QStringLiteral("x"));
      if(expected.isEmpty()||expected!=actual)return {nan,nan};
      bool ok=false,pgf=false;evaluatePgf(expected,c.parameter,&pgf);evaluatePgf(parameterExpression(f.expression,f.variable,QStringLiteral("x")),c.parameter,&ok);if(!ok||!pgf)return {nan,nan};const double value=evaluate(expected,c.parameter,&ok);if(!ok||!std::isfinite(value)||qAbs(value)>10000)return {nan,nan};if(n)v.setY(value);else v.setX(value);
    }
    const double speed=std::hypot(v.x(),v.y());if(speed<.01||speed>10000||!std::isfinite(speed))return {nan,nan};v*=2/speed;const auto &curve=scene.items[index];const auto d=curve.plotX*v.x()+curve.plotY*v.y();if(!std::isfinite(d.x())||!std::isfinite(d.y())||QPointF::dotProduct(d,d)<1e-12||qAbs(point.position.x())+qAbs(d.x())>=500||qAbs(point.position.y())+qAbs(d.y())>=500)return {nan,nan};return d;
  }return {nan,nan};
}
QString refreshedTangents(const Scene &,const QString &);
QString prunedTangentFunctions(const Scene &,const QString &);
QString checkedCurvePatch(const Scene &scene,const QVector<QPair<Span,QString>> &patches,QString *error=nullptr){
  const auto draft=refreshedTangents(scene,patched(scene.source,patches));if(draft.isEmpty())return {};
  const auto after=parse(draft,scene.styleContext);
  if(!sameQuadraticRelations(scene,after)){if(error)*error=QStringLiteral("quadraticChanged");return {};}
  if(!sameConicRelations(scene,after)){if(error)*error=QStringLiteral("conicChanged");return {};}
  if(!after.valid||!after.coordinateSafe||after.items.size()!=scene.items.size()||after.functions.size()!=scene.functions.size()||!sameIntersectionBranches(scene,after))return {};
  for(int n=0;n<scene.items.size();++n){const auto &before=scene.items[n],&now=after.items[n];
    if(before.kind!=now.kind||before.id!=now.id)return {};
    if((before.kind==Kind::Point||before.kind==Kind::Node)&&before.located&&!now.located)return {};
  }
  return draft;
}
