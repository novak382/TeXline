// Bounded ordinary calc/let constructions. Their source contains the relationship.
bool conicCoordinate(const Coordinate &c){return c.operation==QStringLiteral("conicTangent")||c.operation==QStringLiteral("conicContact");}
QString conicPath(const QString &name,const Coordinate &c){
  if(c.references.size()<2||c.references.size()>3)return {};
  QString radii;
  if(c.references.size()==3)radii=QStringLiteral(R"(\p2=($(%1)-(%2)$),\n1={scalar(veclen(\x2,\y2)/1cm)},\n2={\n1},)").arg(c.references[2],c.references[1]);
  else if(c.curveFunctions.size()==2)radii=QStringLiteral(R"(\n1={%1(0)},\n2={%2(0)},)").arg(c.curveFunctions[0],c.curveFunctions[1]);else return {};
  auto source=QStringLiteral(R"(\path let \p1=($(%1)-(%2)$),)").arg(c.references[0],c.references[1])+radii+QStringLiteral(R"(\n3={scalar(\x1/1cm)/\n1},\n4={scalar(\y1/1cm)/\n2},)");
  if(c.operation==QStringLiteral("conicTangent"))return source+QStringLiteral(R"(\n5={-\n1*\n4},\n6={\n2*\n3},\n7={veclen(\n5,\n6)} in coordinate (%1) at ($(%2)+({2*(\n5/\n7)*1cm},{2*(\n6/\n7)*1cm})$);)").arg(name,c.references[0]);
  if(c.operation!=QStringLiteral("conicContact")||(c.parameter!=1&&c.parameter!=-1))return {};
  return source+QStringLiteral(R"(\n5={veclen(\n3,\n4)},\n6={1/\n5},\n7={sqrt(1-\n6*\n6)} in coordinate (%1) at ($(%2)+({\n1*(\n3/\n5*\n6%3\n4/\n5*\n7)*1cm},{\n2*(\n4/\n5*\n6%4\n3/\n5*\n7)*1cm})$);)").arg(name,c.references[1],c.parameter==1?QStringLiteral("-"):QStringLiteral("+"),c.parameter==1?QStringLiteral("+"):QStringLiteral("-"));
}
bool readConicPath(const QString &raw,Item &item,const Scene &scene){
  if(!scene.coordinateNodeSafe||!item.options.isEmpty()||!item.shift.isNull())return false;
  auto compact=raw;compact.remove(QRegularExpression(QStringLiteral("\\s+")));
  const auto header=QRegularExpression(QStringLiteral(R"(^\\pathlet\\p1=\(\$\(([A-Za-z][A-Za-z0-9_-]*)\)-\(([A-Za-z][A-Za-z0-9_-]*)\)\$\),)")).match(compact);
  const auto name=QRegularExpression(QStringLiteral(R"(coordinate\(([A-Za-z][A-Za-z0-9_-]*)\)at)")).match(compact);
  if(!header.hasMatch()||!name.hasMatch())return false;
  Coordinate c;c.references={header.captured(1),header.captured(2)};c.start=item.start;c.end=item.end;
  const auto rest=compact.mid(header.capturedLength());
  const auto through=QRegularExpression(QStringLiteral(R"(^\\p2=\(\$\(([A-Za-z][A-Za-z0-9_-]*)\)-\(([A-Za-z][A-Za-z0-9_-]*)\)\$\),)")).match(rest);
  if(through.hasMatch()){if(through.captured(2)!=c.references[1])return false;c.references.append(through.captured(1));}
  else{const auto radii=QRegularExpression(QStringLiteral(R"(^\\n1=\{([A-Za-z][A-Za-z0-9_]*)\(0\)\},\\n2=\{([A-Za-z][A-Za-z0-9_]*)\(0\)\},)")).match(rest);if(!radii.hasMatch()||!scene.functions.contains(radii.captured(1))||!scene.functions.contains(radii.captured(2)))return false;c.curveFunctions={radii.captured(1),radii.captured(2)};}
  for(int branch:{0,1,-1}){c.operation=branch?QStringLiteral("conicContact"):QStringLiteral("conicTangent");c.parameter=branch;auto expected=conicPath(name.captured(1),c);expected.remove(QRegularExpression(QStringLiteral("\\s+")));if(expected!=compact)continue;
    item.kind=Kind::Point;item.id=name.captured(1);item.coordinates={c};item.size={.07,.07};item.movable=false;return true;
  }return false;
}
int conicOwner(const Scene &scene,const Coordinate &c){
  if(!conicCoordinate(c)||c.references.size()<2)return -1;int found=-1;
  for(int n=0;n<scene.items.size();++n){const auto &i=scene.items[n];if(i.kind!=Kind::Ellipse||i.coordinates.isEmpty()||i.coordinates[0].reference!=c.references[1])continue;
    const bool matches=c.references.size()==3?i.relation==QStringLiteral("through")&&i.coordinates.size()==2&&i.coordinates[1].reference==c.references[2]:i.relation!=QStringLiteral("through")&&i.curveFunctions==c.curveFunctions&&!c.curveFunctions.isEmpty();
    if(matches){if(found>=0)return -1;found=n;}
  }return found;
}
bool conicSourceItem(const Scene &scene,int index,QSet<int> &active,QSet<int> &done,int &steps){
  if(done.contains(index))return true;if(index<0||index>=scene.items.size()||active.contains(index)||++steps>1024)return false;
  active.insert(index);const auto &item=scene.items[index];QStringList references;if(!item.relativeTo.isEmpty())references.append(item.relativeTo);
  for(const auto &c:item.coordinates){references+=c.references;if(!c.reference.isEmpty())references.append(c.reference);}
  references.removeDuplicates();
  for(const auto &ref:references){int owner=-1;for(int n=0;n<scene.items.size();++n)if((scene.items[n].kind==Kind::Point||scene.items[n].kind==Kind::Node)&&scene.items[n].id==ref){owner=n;break;}
    if(owner>=0){if(scene.items[owner].start>=item.start||!conicSourceItem(scene,owner,active,done,steps))return false;continue;}
    bool found=false;for(const auto &g:scene.intersections)if(g.resolved&&ref.startsWith(g.name+QLatin1Char('-'))&&g.end<=item.start){found=true;for(const auto &name:{g.first,g.second}){int path=-1;for(int n=0;n<scene.items.size();++n)if(scene.items[n].style.value(QStringLiteral("name path"))==name){path=n;break;}if(path<0||scene.items[path].start>=g.start||!conicSourceItem(scene,path,active,done,steps))return false;}break;}if(!found)return false;
  }
  active.remove(index);done.insert(index);return true;
}
bool conicLocal(const Scene &scene,const Coordinate &c,const QVector<QPointF> &p,double &a,double &b,QPointF &u){
  const int owner=conicOwner(scene,c);if(owner<0||!pointCurve(scene,owner)||p.size()!=c.references.size())return false;
  QSet<int> active,done;int steps=0;for(const auto &ref:c.references){int n=-1;for(int k=0;k<scene.items.size();++k)if(scene.items[k].id==ref&&(scene.items[k].kind==Kind::Point||scene.items[k].kind==Kind::Node)){n=k;break;}if(n<0||(c.start>=0&&scene.items[n].start>=c.start)||!conicSourceItem(scene,n,active,done,steps))return false;}
  const double sx=scene.canvasScale.x(),sy=scene.canvasScale.y();if(qAbs(sx)<1e-8||qAbs(sy)<1e-8)return false;
  for(const auto &point:p)if(!std::isfinite(point.x())||!std::isfinite(point.y())||qAbs(point.x())>=500||qAbs(point.y())>=500||qAbs(point.x()/sx)>=500||qAbs(point.y()/sy)>=500)return false;
  if(p.size()==3){const auto d=p[2]-p[1];a=b=std::hypot(d.x()/sx,d.y()/sy);}
  else{for(int n=0;n<2;++n){QString function;bool ok=false;const auto radius=ellipseRadius(scene,QStringLiteral("{%1(0)*1cm}").arg(c.curveFunctions[n]),function,ok);bool pgf=false;const auto &definition=scene.functions[function];evaluatePgf(parameterExpression(definition.expression,definition.variable,QStringLiteral("x")),0,&pgf);if(!ok||!pgf)return false;if(n)b=radius;else a=radius;}}
  if(a<.01||b<.01||a>100||b>100||!std::isfinite(a)||!std::isfinite(b))return false;
  const auto d=p[0]-p[1];const QPointF local(d.x()/sx,d.y()/sy);
  if(std::hypot(local.x(),local.y())>400)return false;
  u={local.x()/a,local.y()/b};return std::isfinite(u.x())&&std::isfinite(u.y())&&std::hypot(u.x(),u.y())<=10000;
}
QPointF conicPosition(const Scene &scene,const Coordinate &c,const QVector<QPointF> &p){
  double a=0,b=0;QPointF u;if(!conicLocal(scene,c,p,a,b,u))return {nan,nan};const double distance=std::hypot(u.x(),u.y());QPointF result;
  if(c.operation==QStringLiteral("conicTangent")){
    if(qAbs(distance-1)>1e-6)return {nan,nan};const QPointF v(-a*u.y(),b*u.x());const double speed=std::hypot(v.x(),v.y());if(speed<.01||speed>10000)return {nan,nan};result=p[0]+QPointF(2*v.x()/speed*scene.canvasScale.x(),2*v.y()/speed*scene.canvasScale.y());
  }else{
    if(distance<1.001||(c.parameter!=1&&c.parameter!=-1))return {nan,nan};const double k=1/distance,h=std::sqrt(1-k*k);
    if(std::hypot(2*a*u.y()/distance*h*scene.canvasScale.x(),2*b*u.x()/distance*h*scene.canvasScale.y())<.08)return {nan,nan};
    result=p[1]+QPointF(a*(u.x()/distance*k-c.parameter*u.y()/distance*h)*scene.canvasScale.x(),b*(u.y()/distance*k+c.parameter*u.x()/distance*h)*scene.canvasScale.y());
  }
  if(!std::isfinite(result.x())||!std::isfinite(result.y())||qAbs(result.x())>=500||qAbs(result.y())>=500||qAbs(result.x()/scene.canvasScale.x())>=500||qAbs(result.y()/scene.canvasScale.y())>=500)return {nan,nan};return result;
}
bool sameConicRelations(const Scene &before,const Scene &after){
  for(const auto &i:before.items)if(i.located)for(const auto &c:i.coordinates)if(conicCoordinate(c)){
    const Item *found=nullptr;for(const auto &next:after.items)if(next.id==i.id){found=&next;break;}
    if(!found)continue;if(!found->located||found->coordinates.size()!=1)return false;const auto &d=found->coordinates[0];if(d.operation!=c.operation||d.references!=c.references||d.curveFunctions!=c.curveFunctions||d.parameter!=c.parameter)return false;
  }return true;
}
