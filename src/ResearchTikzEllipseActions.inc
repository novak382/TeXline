// Shared physical radii and calc coordinates keep ellipse points editable in ordinary TikZ.
namespace {
QString ellipseTuple(const Coordinate &p,double t){
  const auto angle=QString::number(t,'f',8).remove(QRegularExpression(QStringLiteral(R"(\.?0+$)")));
  if(p.operation==QStringLiteral("circlePoint"))return QStringLiteral("$(%1)!1!%2:(%3)$").arg(p.references[0],angle,p.references[1]);
  return QStringLiteral("$(%1)+({%2(0)*cos(%3)*1cm},{%4(0)*sin(%3)*1cm})$").arg(p.references[0],p.curveFunctions[0],angle,p.curveFunctions[1]);
}
bool ellipseFrame(const Scene &scene,const Item &i,QPointF &center,QPointF &x,QPointF &y){
  if(i.kind!=Kind::Ellipse||i.coordinates.isEmpty()||i.spatial||!scene.coordinateSafe||scene.spatial)return false;
  center=endpoint(scene,i.coordinates[0])+(i.coordinates[0].numeric?i.shift:QPointF());
  if(i.relation==QStringLiteral("through")){
    if(i.coordinates.size()!=2)return false;
    x=endpoint(scene,i.coordinates[1])-center;y={-x.y()*scene.canvasScale.x()/scene.canvasScale.y(),x.x()*scene.canvasScale.y()/scene.canvasScale.x()};
  }else{x={i.size.width()/2*(scene.canvasScale.x()<0?-1:1),0};y={0,i.size.height()/2*(scene.canvasScale.y()<0?-1:1)};}
  return std::isfinite(center.x())&&std::isfinite(center.y())&&std::hypot(x.x(),x.y())>1e-8&&std::hypot(y.x(),y.y())>1e-8;
}
bool independentRadii(const Scene &scene,int index){
  const auto &curve=scene.items[index];if(curve.curveFunctions.isEmpty())return true;
  const QSet<QString> names(curve.curveFunctions.begin(),curve.curveFunctions.end());
  QVector<QPair<int,int>> spans{{curve.radiusStart,curve.radiusEnd}};
  for(int n=0;n<scene.items.size();++n){const auto &i=scene.items[n];
    if(n!=index)for(const auto &name:i.curveFunctions)if(names.contains(name))return false;
    for(const auto &c:i.coordinates)for(const auto &name:c.curveFunctions)if(names.contains(name)){if(!(ellipseCoordinate(c)&&ellipseForPoint(scene,c)==index)&&!(conicCoordinate(c)&&conicOwner(scene,c)==index))return false;spans.append({c.start,c.end});}
  }
  const auto clean=mask(scene.source);
  for(const auto &name:names){const auto &f=scene.functions[name];const QRegularExpression call(QStringLiteral("(?<![A-Za-z0-9_])")+QRegularExpression::escape(name)+QStringLiteral("\\s*\\("));if(call.match(mask(scene.styleContext)).hasMatch())return false;auto calls=call.globalMatch(clean);
    while(calls.hasNext()){const auto m=calls.next();const int at=int(m.capturedStart());bool known=false;for(const auto &span:spans)if(at>=span.first&&at<span.second)known=true;
      if(at<f.start){const auto prefix=clean.mid(at,f.start-at);const auto definition=QRegularExpression(QStringLiteral("^")+QRegularExpression::escape(name)+QStringLiteral("\\s*\\(\\s*")+QRegularExpression::escape(f.variable)+QStringLiteral("\\s*\\)\\s*=\\s*$"));known|=definition.match(prefix).hasMatch();}
      if(!known)return false;
    }
  }return true;
}
QString resizedEllipse(const Scene &scene,int index,const QSizeF &size){
  const auto &i=scene.items[index];if(scene.partial||!pointCurve(scene,index)||i.curveFunctions.size()!=2||!independentRadii(scene,index))return {};
  if(i.shape!=QStringLiteral("circle")&&i.curveFunctions[0]==i.curveFunctions[1]&&qAbs(size.width()/qAbs(scene.canvasScale.x())-size.height()/qAbs(scene.canvasScale.y()))>1e-8)return {};
  QVector<QPair<Span,QString>> patches;QSet<QString> used;
  for(int n=0;n<2;++n){const auto name=i.curveFunctions[n];if(used.contains(name))continue;used.insert(name);const auto &f=scene.functions[name];
    const double v=(n?size.height()/qAbs(scene.canvasScale.y()):size.width()/qAbs(scene.canvasScale.x()))/2;
    patches.append({{f.start,f.end,{}},QString::number(v,'g',14)});
  }return checkedCurvePatch(scene,patches);
}
double nearestEllipseParameter(const Scene &scene,int index,const QPointF &point,double previous){
  QPointF center,x,y;if(!ellipseFrame(scene,scene.items[index],center,x,y))return nan;
  const auto d=point-center;const double determinant=x.x()*y.y()-x.y()*y.x();if(qAbs(determinant)<1e-12)return nan;
  if(QPointF::dotProduct(d,d)<1e-14)return std::isfinite(previous)?previous:0;
  // Radial angle follows the pointer continuously, including the 0/360 seam.
  double t=qRadiansToDegrees(std::atan2((x.x()*d.y()-x.y()*d.x())/determinant,(d.x()*y.y()-d.y()*y.x())/determinant));
  if(std::isfinite(previous))t+=360*std::round((previous-t)/360);else if(t<0)t+=360;
  if(qAbs(t)>9900)t=std::remainder(t,360);return t;
}
QString pointOnEllipse(const Scene &scene,int index,double parameter){
  if(!pointCurve(scene,index)||scene.items[index].kind!=Kind::Ellipse)return {};
  const auto &original=scene.items[index];Scene base=scene;Coordinate point;point.parameter=parameter;
  if(original.relation==QStringLiteral("through")){
    point.operation=QStringLiteral("circlePoint");point.references={original.coordinates[0].reference,original.coordinates[1].reference};
  }else{
    auto names=original.curveFunctions;QVector<QPair<Span,QString>> patches;
    if(!names.isEmpty()&&!independentRadii(scene,index))return {};
    if(names.isEmpty()){
      if(scene.functions.size()+(original.shape==QStringLiteral("circle")?1:2)>48)return {};
      QString prefix;for(int n=1;n<10000;++n){prefix=QStringLiteral("texlineellipse%1").arg(n);if(!(scene.source+scene.styleContext).contains(prefix))break;prefix.clear();}if(prefix.isEmpty())return {};
      names={prefix+QStringLiteral("x"),prefix+(original.shape==QStringLiteral("circle")?QStringLiteral("x"):QStringLiteral("y"))};
      QString declaration;QSet<QString> written;
      for(int n=0;n<2;++n)if(!written.contains(names[n])){written.insert(names[n]);const double radius=(n?original.size.height()/qAbs(scene.canvasScale.y()):original.size.width()/qAbs(scene.canvasScale.x()))/2;declaration+=QStringLiteral("\n  %1(\\t)=%2;").arg(names[n],QString::number(radius,'g',14));}
      if(scene.functionsEnd>=0){int first=scene.functionsEnd;while(first>0&&scene.source[first-1].isSpace())--first;patches.append({{first,scene.functionsEnd,{}},declaration+QLatin1Char('\n')});}
      else if(scene.optionsStart>=0){const auto sep=scene.pictureOptions.trimmed().isEmpty()||scene.pictureOptions.trimmed().endsWith(QLatin1Char(','))?QString():QStringLiteral(",");patches.append({{scene.optionsEnd-1,scene.optionsEnd-1,{}},sep+QStringLiteral("declare function={")+declaration+QStringLiteral("\n}")});}
      else patches.append({{scene.insertOptions,scene.insertOptions,{}},QStringLiteral("[declare function={")+declaration+QStringLiteral("\n}]")});
      const auto rx=QStringLiteral("{%1(0)*1cm}").arg(names[0]),ry=QStringLiteral("{%1(0)*1cm}").arg(names[1]);QString radius;
      if(original.radiusAsOptions){auto raw=scene.source.mid(original.radiusStart+1,original.radiusEnd-original.radiusStart-2);raw=editOption(raw,original.shape==QStringLiteral("circle")?QStringLiteral("radius"):QStringLiteral("x radius"),rx);if(original.shape!=QStringLiteral("circle"))raw=editOption(raw,QStringLiteral("y radius"),ry);radius=QLatin1Char('[')+raw+QLatin1Char(']');}
      else radius=QLatin1Char('(')+rx+(original.shape==QStringLiteral("circle")?QString():QStringLiteral(" and ")+ry)+QLatin1Char(')');
      patches.append({{original.radiusStart,original.radiusEnd,{}},radius});
    }
    const auto &c=original.coordinates[0];auto center=c.reference;
    if(c.numeric){
      QSet<QString> used;center=freshName(scene,used,true);
      auto raw=scene.source.mid(c.start+1,c.end-c.start-2);
      // Coordinate-node shifts are physical; path shifts are transformed. Bake the
      // path's translated center into its new coordinate instead of copying options.
      if(!original.shift.isNull()){const auto p=c.point+original.shift;raw=QStringLiteral("%1cm,%2cm").arg(QString::number(p.x()/scene.canvasScale.x(),'g',14),QString::number(p.y()/scene.canvasScale.y(),'g',14));}
      const auto helper=coordinateSource(center,raw,false);
      patches.append({{original.start,original.start,{}},helper});
      patches.append({{c.start,c.end,{}},QLatin1Char('(')+center+QLatin1Char(')')});
    }
    // A one-line picture may place its new options and helper at the same offset.
    for(int n=0;n<patches.size();++n)if(patches[n].first.a==patches[n].first.b)for(int m=patches.size()-1;m>n;--m)if(patches[m].first.a==patches[n].first.a&&patches[m].first.b==patches[n].first.b){patches[n].second+=patches[m].second;patches.removeAt(m);}
    base=parse(patched(scene.source,patches),scene.styleContext);if(!base.valid||base.partial||!base.coordinateSafe)return {};
    point.operation=QStringLiteral("ellipsePoint");point.references={center};point.curveFunctions=names;
  }
  QSet<QString> used;const auto name=freshName(base,used);const auto draft=appendDrawing(base,coordinateSource(name,ellipseTuple(point,parameter),true),base.source.contains(QStringLiteral("\\usetikzlibrary{calc}"))?QString():QStringLiteral("calc"));
  const auto after=parse(draft,scene.styleContext);
  if(!after.valid||after.partial||!after.coordinateSafe||after.items.size()!=base.items.size()+2||!after.items[base.items.size()].located||boundCurve(after,after.items[base.items.size()].coordinates[0])<0||!sameIntersectionBranches(scene,after))return {};
  return draft;
}
}
bool pointCurve(const Scene &scene,int index){
  if(!scene.valid||!scene.coordinateSafe||index<0||index>=scene.items.size())return false;const auto &i=scene.items[index];
  if(i.kind==Kind::Plot)return i.movable&&scene.coordinateNodeSafe;
  if(!scene.coordinateNodeSafe)return false;
  QPointF c,x,y;if(scene.partial||!ellipseFrame(scene,i,c,x,y)||i.clipping||i.inheritedShift||!safeStyle(i.style)||!understoodStyle(i.style))return false;
  for(const auto &p:i.coordinates){if(p.numeric){if(p.spatial||!p.curveFunctions.isEmpty())return false;}else{if(p.reference.isEmpty()||!p.anchor.isEmpty()||!p.operation.isEmpty())return false;bool found=false;for(const auto &owner:scene.items)if(owner.id==p.reference&&owner.kind==Kind::Point&&owner.located&&owner.start<i.start)found=true;if(!found)return false;}}
  if(i.relation==QStringLiteral("through"))return i.coordinates.size()==2&&i.shift.isNull();
  if(i.coordinates.size()!=1||i.radiusStart<0)return false;
  if(i.radiusAsOptions){QSet<QString> keys;for(const auto &raw:options(scene.source.mid(i.radiusStart+1,i.radiusEnd-i.radiusStart-2))){const auto key=option(raw).first;if(keys.contains(key)||!QStringList{QStringLiteral("radius"),QStringLiteral("x radius"),QStringLiteral("y radius")}.contains(key))return false;keys.insert(key);}if(i.shape==QStringLiteral("circle")?keys!=QSet<QString>{QStringLiteral("radius")}:keys!=QSet<QString>{QStringLiteral("x radius"),QStringLiteral("y radius")})return false;}
  if(scene.axisX!=QPointF(1,0)||scene.axisY!=QPointF(0,1)){
    auto raw=scene.source.mid(i.radiusStart+1,i.radiusEnd-i.radiusStart-2);QStringList values;
    if(i.radiusAsOptions){const auto s=style(raw,{});values={s.value(QStringLiteral("radius"),s.value(QStringLiteral("x radius"))),s.value(QStringLiteral("radius"),s.value(QStringLiteral("y radius")))};}else values=raw.split(QRegularExpression(QStringLiteral("\\s+and\\s+")));
    for(const auto &v:values)if(!QRegularExpression(QStringLiteral("(?:cm|mm|pt|bp|in)\\s*}?[ \t]*$")).match(v).hasMatch())return false;
  }return true;
}
