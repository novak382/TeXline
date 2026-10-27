namespace {QString quadraticTangent(const Scene &,int,int,int,QString *,int *);}
// Exact conic contacts and tangents share the existing circle/ellipse radii.
namespace {
int namedPoint(const Scene &scene,const QString &name){for(int n=0;n<scene.items.size();++n)if(scene.items[n].kind==Kind::Point&&scene.items[n].id==name)return n;return -1;}
bool prepareConic(const Scene &scene,int index,const QString &anchor,Scene &base,Coordinate &c){
  if(index<0||index>=scene.items.size()||scene.items.size()>440||scene.items[index].kind!=Kind::Ellipse||!pointCurve(scene,index))return false;
  base=scene;const auto &original=scene.items[index];Coordinate binding;
  if(original.relation==QStringLiteral("through")){c.references={anchor,original.coordinates[0].reference,original.coordinates[1].reference};}
  else{
    if(original.curveFunctions.size()!=2||original.coordinates[0].reference.isEmpty()){
      const auto promoted=parse(pointOnEllipse(scene,index,0),scene.styleContext);if(!promoted.valid||promoted.partial||promoted.items.size()<2)return false;
      const int temporary=promoted.items.size()-2;binding=promoted.items[temporary].coordinates.value(0);
      base=parse(removed(promoted,temporary),scene.styleContext);if(!base.valid||base.partial||boundCurve(base,binding)<0)return false;
    }else{binding.references={original.coordinates[0].reference};binding.curveFunctions=original.curveFunctions;}
    c.references={anchor,binding.references.value(0)};c.curveFunctions=binding.curveFunctions;
  }
  c.operation=QStringLiteral("conicTangent");return conicOwner(base,c)>=0;
}
QVector<QPointF> conicReferencePoints(const Scene &scene,const Coordinate &c){
  QVector<QPointF> p;for(const auto &name:c.references){const int n=namedPoint(scene,name);if(n<0||!scene.items[n].located)return {};p.append(scene.items[n].position);}return p;
}
bool conicPointEligible(const Scene &scene,int point){
  if(point<0||point>=scene.items.size())return false;const auto &p=scene.items[point];const int curve=curveForPoint(scene,point);
  if(p.kind!=Kind::Point||!p.located||!p.shift.isNull()||!p.placementShift.isNull()||curve<0||scene.items[curve].kind!=Kind::Ellipse)return false;
  Scene base;Coordinate c;if(!prepareConic(scene,curve,p.id,base,c))return false;return std::isfinite(conicPosition(base,c,conicReferencePoints(base,c)).x());
}
int conicLine(const Scene &scene,const Coordinate &relation,const QString &point){
  QString back;
  if(relation.operation==QStringLiteral("conicTangent"))for(const auto &i:scene.items)if(i.kind==Kind::Point&&i.coordinates.size()==1){const auto &c=i.coordinates[0];if(c.operation==QStringLiteral("mix")&&c.references==QStringList{relation.references[0],point}&&c.fraction==-1)back=i.id;}
  const auto start=(relation.operation==QStringLiteral("conicContact")||relation.operation==QStringLiteral("quadraticContact"))?relation.references[0]:back;
  if(start.isEmpty())return -1;for(int n=0;n<scene.items.size();++n){const auto &i=scene.items[n];if(i.kind!=Kind::Line||i.coordinates.size()!=2||i.closed||i.style.value(QStringLiteral("route1"))!=QStringLiteral("--"))continue;const auto a=i.coordinates[0].reference,b=i.coordinates[1].reference;if((a==start&&b==point)||(a==point&&b==start))return n;}return -1;
}
QString conicPointTangent(const Scene &scene,int point){
  if(!conicPointEligible(scene,point))return {};const auto &p=scene.items[point];return tangentFromPoint(scene,curveForPoint(scene,point),point,0);
}
}
QVector<QPointF> conicTangencies(const Scene &scene,int curve,int point,QString *error){
  auto fail=[&](const QString &why){if(error)*error=why;return QVector<QPointF>();};
  if(!scene.valid||scene.partial||!scene.coordinateSafe||!scene.coordinateNodeSafe||point<0||point>=scene.items.size())return fail(QStringLiteral("conic"));
  const auto &p=scene.items[point];if(p.kind!=Kind::Point||!p.located||p.spatial||p.id.isEmpty())return fail(QStringLiteral("conic"));
  Scene base;Coordinate c;if(!prepareConic(scene,curve,p.id,base,c))return fail(QStringLiteral("conic"));const auto positions=conicReferencePoints(base,c);double a=0,b=0;QPointF u;if(!conicLocal(base,c,positions,a,b,u))return fail(QStringLiteral("conic"));
  const double distance=std::hypot(u.x(),u.y());if(qAbs(distance-1)<=1e-6){const auto tangent=conicPosition(base,c,positions);if(!std::isfinite(tangent.x()))return fail(QStringLiteral("conic"));return {positions[0]};}
  if(distance<1)return fail(QStringLiteral("insideConic"));if(distance<1.001)return fail(QStringLiteral("nearConic"));
  const double h=std::sqrt(1-1/(distance*distance));if(std::hypot(2*a*u.y()/distance*h*base.canvasScale.x(),2*b*u.x()/distance*h*base.canvasScale.y())<.08)return fail(QStringLiteral("nearConic"));
  QVector<QPointF> points;for(int branch:{1,-1}){c.operation=QStringLiteral("conicContact");c.parameter=branch;const auto contact=conicPosition(base,c,positions);if(!std::isfinite(contact.x()))return fail(QStringLiteral("conic"));points.append(contact);}return points;
}
QString tangentFromPoint(const Scene &scene,int curve,int point,int branch,QString *error,int *line){
  if(curve>=0&&curve<scene.items.size()&&scene.items[curve].kind==Kind::Plot)return quadraticTangent(scene,curve,point,branch,error,line);
  if(line)*line=-1;auto fail=[&](const QString &why){if(error)*error=why;return QString();};const auto choices=conicTangencies(scene,curve,point,error);
  if(choices.isEmpty()||(branch!=0&&branch!=1&&branch!=-1)||(choices.size()==1)!=(branch==0))return {};
  Scene base;Coordinate c;if(!prepareConic(scene,curve,scene.items[point].id,base,c))return fail(QStringLiteral("conic"));c.operation=branch?QStringLiteral("conicContact"):QStringLiteral("conicTangent");c.parameter=branch;
  QString existing;for(const auto &i:base.items)if(i.kind==Kind::Point&&i.coordinates.size()==1){const auto &d=i.coordinates[0];if(d.operation==c.operation&&d.references==c.references&&d.curveFunctions==c.curveFunctions&&d.parameter==c.parameter){existing=i.id;const int n=conicLine(base,d,existing);if(n>=0){if(line)*line=n;return base.source;}break;}}
  QSet<QString> used;QString body;
  if(branch){const auto name=existing.isEmpty()?freshName(base,used):existing;if(existing.isEmpty())body=conicPath(name,c)+QLatin1Char('\n')+QStringLiteral("\\fill (%1) circle (1.2pt) node[above right] {$%1$};\n").arg(name);body+=QStringLiteral("\\draw (%1)--(%2);").arg(c.references[0],name);}
  else{const auto tip=existing.isEmpty()?freshName(base,used,true):existing,back=freshName(base,used,true);if(existing.isEmpty())body=conicPath(tip,c)+QLatin1Char('\n');body+=coordinateSource(back,QStringLiteral("$(%1)!-1!(%2)$").arg(c.references[0],tip),false)+QStringLiteral("\\draw (%1)--(%2);").arg(back,tip);}
  const auto draft=appendDrawing(base,body,QStringLiteral("calc"));const auto after=parse(draft,scene.styleContext);if(!after.valid||after.partial||!after.coordinateSafe||!sameConicRelations(scene,after)||!sameIntersectionBranches(scene,after))return fail(QStringLiteral("conic"));if(line)*line=after.items.size()-1;return draft;
}
