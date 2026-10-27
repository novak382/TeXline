// Parameter-bound points use ordinary local PGF functions, without hidden metadata.
namespace {
QString curveNumber(double value){return QString::number(value,'f',8).remove(QRegularExpression(QStringLiteral(R"(\.?0+$)")));}
QString curveTuple(const QStringList &names,double t){
  QStringList parts;for(const auto &name:names)parts.append(QStringLiteral("{%1(%2)}").arg(name,curveNumber(t)));
  return parts.join(QLatin1Char(','));
}
bool curveOffset(const Scene &scene,const Item &curve,const QPointF &delta,std::array<double,3> &v){
  if(curve.spatial){Coordinate zero;v=shiftedSpatial(scene,zero,delta);return QLineF(projected(scene,v),delta).length()<1e-6;}
  const auto a=curve.plotX,b=curve.plotY;const double determinant=a.x()*b.y()-a.y()*b.x();
  if(qAbs(determinant)<1e-10)return false;
  v[0]=(delta.x()*b.y()-delta.y()*b.x())/determinant;v[1]=(a.x()*delta.y()-a.y()*delta.x())/determinant;return true;
}
QString movedCurve(const Scene &scene,int index,const QPointF &delta){
  const auto &curve=scene.items[index];std::array<double,3> v{};if(!curveOffset(scene,curve,delta,v))return {};
  QVector<QPair<Span,QString>> patches;
  for(int n=0;n<curve.curveFunctions.size();++n)if(qAbs(v[n])>1e-10){const auto &f=scene.functions[curve.curveFunctions[n]];
    const auto expression=QStringLiteral("(%1)+(%2)").arg(f.expression,curveNumber(v[n]));if(expression.size()>256)return {};
    patches.append({{f.start,f.end,{}},expression});
  }
  return checkedCurvePatch(scene,patches);
}
}
double nearestCurveParameter(const Scene &scene,int index,const QPointF &point,double previous){
  if(index<0||index>=scene.items.size()||!pointCurve(scene,index)||!std::isfinite(point.x())||!std::isfinite(point.y()))return nan;
  if(scene.items[index].kind==Kind::Ellipse)return nearestEllipseParameter(scene,index,point,previous);
  const auto &curve=scene.items[index];double lo=curve.domainMin,hi=curve.domainMax;
  // A dragged point stays on its nearby parameter branch at crossings.
  if(std::isfinite(previous)){const double window=(hi-lo)/24;if(previous<lo||previous>hi){lo=qMax(-10000.,previous-window);hi=qMin(10000.,previous+window);}else{lo=qMax(lo,previous-window);hi=qMin(hi,previous+window);}}
  auto distance=[&](double t){std::array<double,3> v{};if(!curveValues(curve,t,v))return std::numeric_limits<double>::infinity();const auto d=curve.plotX*v[0]+curve.plotY*v[1]+curve.plotZ*v[2]+curve.shift-point;return QPointF::dotProduct(d,d);};
  constexpr int count=192;const double step=(hi-lo)/count;double best=std::numeric_limits<double>::infinity(),parameter=nan;
  for(int n=0;n<=count;++n){const double t=lo+n*step,d=distance(t);if(d<best-1e-12||(qAbs(d-best)<=1e-12&&std::isfinite(previous)&&qAbs(t-previous)<qAbs(parameter-previous))){best=d;parameter=t;}}
  if(!std::isfinite(parameter))return nan;
  double left=qMax(lo,parameter-step),right=qMin(hi,parameter+step);
  for(int n=0;n<24;++n){const double a=left+(right-left)*.38196601125,b=left+(right-left)*.61803398875;if(distance(a)<distance(b))right=b;else left=a;}
  const double refined=(left+right)/2;if(distance(refined)<best)parameter=refined;
  return parameter;
}
QString pointOnCurve(const Scene &scene,int index,double parameter){
  if(!scene.valid||!scene.coordinateSafe||scene.items.size()>450||index<0||index>=scene.items.size()||!std::isfinite(parameter)||qAbs(parameter)>10000)return {};
  if(scene.items[index].kind==Kind::Ellipse)return pointOnEllipse(scene,index,parameter);
  const auto &curve=scene.items[index];std::array<double,3> value{};
  if(curve.kind!=Kind::Plot||!curve.movable||parameter<curve.domainMin||parameter>curve.domainMax||!curveValues(curve,parameter,value))return {};
  auto names=curve.curveFunctions;Scene base=scene;
  int uses=0;for(const auto &item:scene.items)if(!names.isEmpty()&&item.curveFunctions==names)++uses;
  if(uses>1&&scene.partial)return {};
  if(uses>1)names.clear(); // The selected path gets its own family; other paths retain their source.
  if(names.isEmpty()){
    if(scene.functions.size()+(curve.spatial?3:2)>48)return {};
    std::array<double,3> offset{};if(!curveOffset(scene,curve,curve.shift,offset))return {};
    const auto occupied=scene.source+scene.styleContext;QString prefix;
    for(int n=1;n<10000;++n){prefix=QStringLiteral("texlinecurve%1").arg(n);if(!occupied.contains(prefix))break;prefix.clear();}if(prefix.isEmpty())return {};
    QString declaration;QVector<QPair<Span,QString>> patches;
    const QStringList expressions{curve.expressionX,curve.expression,curve.expressionZ};
    const int starts[]{curve.xStart,curve.textStart,curve.zStart},ends[]{curve.xEnd,curve.textEnd,curve.zEnd};
    for(int n=0;n<(curve.spatial?3:2);++n){
      const auto name=prefix+QChar(QLatin1Char('x').unicode()+n);names.append(name);
      auto expression=parameterExpression(expressions[n],curve.variable,QStringLiteral("\\t"));
      if(qAbs(offset[n])>1e-10)expression=QStringLiteral("(%1)+(%2)").arg(expression,curveNumber(offset[n]));
      if(expression.size()>256)return {};
      declaration+=QStringLiteral("\n  %1(\\t)=%2;").arg(name,expression);
      auto call=QStringLiteral("%1(%2)").arg(name,curve.variable);
      if(starts[n]<=0||scene.source[starts[n]-1]!=QLatin1Char('{')||scene.source[ends[n]]!=QLatin1Char('}'))call=QLatin1Char('{')+call+QLatin1Char('}');
      patches.append({{starts[n],ends[n],{}},call});
    }
    if(uses>1)for(const auto &item:scene.items)for(const auto &point:item.coordinates)if(point.curveFunctions==curve.curveFunctions){
      const int owner=boundCurve(scene,point);if(owner<0)return {};
      if(owner==index){
        auto original=scene.source.mid(point.start,point.end-point.start);
        for(int n=0;n<names.size();++n)original.replace(QRegularExpression(QStringLiteral("(?<![A-Za-z0-9_])")+QRegularExpression::escape(curve.curveFunctions[n])+QStringLiteral("(?=\\s*\\()")),names[n]);
        patches.append({{point.start,point.end,{}},original});
      }
    }
    if(scene.functionsEnd>=0){int start=scene.functionsEnd;while(start>0&&scene.source[start-1].isSpace())--start;patches.append({{start,scene.functionsEnd,{}},declaration+QLatin1Char('\n')});}
    else if(scene.optionsStart>=0){const auto separator=scene.pictureOptions.trimmed().isEmpty()||scene.pictureOptions.trimmed().endsWith(QLatin1Char(','))?QString():QStringLiteral(",");patches.append({{scene.optionsEnd-1,scene.optionsEnd-1,{}},separator+QStringLiteral("declare function={")+declaration+QStringLiteral("\n}")});}
    else patches.append({{scene.insertOptions,scene.insertOptions,{}},QStringLiteral("[declare function={")+declaration+QStringLiteral("\n}]")});
    if(!curve.shift.isNull()){
      if(curve.optionsStart<0)return {};
      const auto raw=editOption(editOption(curve.options,QStringLiteral("xshift"),QStringLiteral("__remove__")),QStringLiteral("yshift"),QStringLiteral("__remove__"));
      patches.append({{curve.optionsStart,curve.optionsEnd,{}},QLatin1Char('[')+raw+QLatin1Char(']')});
    }
    base=parse(patched(scene.source,patches),scene.styleContext);
    if(!base.valid||!base.coordinateSafe||base.items.size()!=scene.items.size()||base.items[index].curveFunctions!=names||!base.items[index].shift.isNull())return {};
  }else if(!curve.shift.isNull())return {};
  QSet<QString> used;const auto name=freshName(base,used);
  const auto draft=appendDrawing(base,coordinateSource(name,curveTuple(names,parameter),true));
  const auto after=parse(draft,scene.styleContext);
  if(after.items.size()!=base.items.size()+2||!after.items[base.items.size()].located)return {};
  return draft;
}
QString curveParameter(const Scene &scene,int index,double parameter,QString *error){
  if(!scene.valid||!scene.coordinateSafe||index<0||index>=scene.items.size()||!std::isfinite(parameter)||qAbs(parameter)>10000)return {};
  const auto &item=scene.items[index];if((item.kind!=Kind::Point&&item.kind!=Kind::Node)||item.coordinates.size()!=1)return {};
  const auto &point=item.coordinates[0];if(conicCoordinate(point)||quadraticCoordinate(point)||point.curveFunctions.isEmpty()&&!ellipseCoordinate(point))return {};
  if(parameter==point.parameter)return scene.source;
  return checkedCurvePatch(scene,{{{point.start,point.end,{}},QLatin1Char('(')+(ellipseCoordinate(point)?ellipseTuple(point,parameter):curveTuple(point.curveFunctions,parameter))+QLatin1Char(')')}},error);
}
int curveForPoint(const Scene &scene,int index){
  return index>=0&&index<scene.items.size()&&!scene.items[index].coordinates.isEmpty()?boundCurve(scene,scene.items[index].coordinates[0]):-1;
}
