namespace {
bool prepareQuadratic(const Scene &scene,int index,Scene &base){
  if(index<0||index>=scene.items.size()||scene.items.size()>440||scene.items[index].kind!=Kind::Plot||scene.items[index].spatial)return false;
  const auto &curve=scene.items[index];Coordinate relation;relation.operation=QStringLiteral("quadraticContact");relation.curveFunctions=curve.curveFunctions;std::array<QPointF,3> existing;if(quadraticOwner(scene,relation)==index&&quadraticCoefficients(scene,index,existing)){base=scene;return true;}const auto promoted=parse(pointOnCurve(scene,index,(curve.domainMin+curve.domainMax)/2),scene.styleContext);if(!promoted.valid||promoted.partial||promoted.items.size()!=scene.items.size()+2)return false;
  base=parse(removed(promoted,promoted.items.size()-2),scene.styleContext);std::array<QPointF,3> v;return base.valid&&!base.partial&&quadraticCoefficients(base,index,v)&&base.items[index].curveFunctions.size()==2;
}
QString quadraticTangent(const Scene &scene,int index,int point,int branch,QString *error,int *line){
  if(line)*line=-1;auto fail=[&]{if(error)*error=QStringLiteral("quadratic");return QString();};if(point<0||point>=scene.items.size()||scene.items[point].kind!=Kind::Point||!scene.items[point].located||scene.items[point].spatial)return fail();
  Scene base;if(!prepareQuadratic(scene,index,base))return fail();Coordinate c;c.operation=QStringLiteral("quadraticContact");c.references={scene.items[point].id};c.curveFunctions=base.items[index].curveFunctions;c.parameter=branch;if(!std::isfinite(quadraticContact(base,c,base.items[point].position).x()))return fail();
  QString name;for(const auto &i:base.items)if(i.kind==Kind::Point&&i.coordinates.size()==1){const auto &d=i.coordinates[0];if(quadraticCoordinate(d)&&d.references==c.references&&d.curveFunctions==c.curveFunctions&&d.parameter==branch){name=i.id;const int existing=conicLine(base,d,name);if(existing>=0){if(line)*line=existing;return base.source;}break;}}
  QSet<QString> used;QString body;if(name.isEmpty()){name=freshName(base,used);body=quadraticPath(name,c)+QStringLiteral("\n\\fill (%1) circle (1.2pt) node[above right] {$%1$};\n").arg(name);}body+=QStringLiteral("\\draw (%1)--(%2);").arg(c.references[0],name);
  const auto draft=appendDrawing(base,body,QStringLiteral("calc"));const auto after=parse(draft,scene.styleContext);if(!after.valid||after.partial||!after.coordinateSafe||!sameQuadraticRelations(scene,after)||!sameConicRelations(scene,after)||!sameIntersectionBranches(scene,after))return fail();if(line)*line=after.items.size()-1;return draft;
}
}
QVector<QPointF> externalTangencies(const Scene &scene,int curve,int point,QVector<int> *branches,QString *error){
  if(branches)branches->clear();if(curve>=0&&curve<scene.items.size()&&scene.items[curve].kind==Kind::Ellipse){const auto points=conicTangencies(scene,curve,point,error);if(branches)*branches=points.size()==1?QVector<int>{0}:points.size()==2?QVector<int>{1,-1}:QVector<int>{};return points;}
  auto fail=[&]{if(error)*error=QStringLiteral("quadratic");return QVector<QPointF>();};if(!scene.valid||scene.partial||!scene.coordinateSafe||!scene.coordinateNodeSafe||point<0||point>=scene.items.size())return fail();const auto &p=scene.items[point];if(p.kind!=Kind::Point||!p.located||p.spatial||p.id.isEmpty())return fail();
  Scene base;if(!prepareQuadratic(scene,curve,base))return fail();Coordinate c;c.operation=QStringLiteral("quadraticContact");c.references={p.id};c.curveFunctions=base.items[curve].curveFunctions;QVector<QPointF> result;
  for(int branch:{1,-1}){c.parameter=branch;const auto contact=quadraticContact(base,c,p.position);if(std::isfinite(contact.x())){result.append(contact);if(branches)branches->append(branch);}}
  if(result.isEmpty()&&error)*error=QStringLiteral("quadraticContacts");return result;
}
