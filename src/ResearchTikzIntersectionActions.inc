// Public actions, after the existing source-patch and geometry helpers.
bool intersectionPath(const Scene &scene,int index){QVector<IntersectionSegment> segments;return intersectionSegments(scene,index,segments);}
QVector<QPointF> pathCrossings(const Scene &scene,int first,int second,bool *ok){
  bool valid=false;QVector<QPointF> result;QVector<IntersectionSegment> a,b;
  if(first!=second&&intersectionSegments(scene,first,a)&&intersectionSegments(scene,second,b))result=intersectSegments(a,b,valid);
  if(ok)*ok=valid;return result;
}
bool sameIntersectionBranches(const Scene &before,const Scene &after){
  for(const auto &group:before.intersections){if(!group.resolved)continue;bool used=false;for(const auto &i:before.items)for(const auto &c:i.coordinates)for(const auto &ref:c.references)if(ref.startsWith(group.name+QLatin1Char('-')))used=true;if(!used)continue;
    bool found=false;for(const auto &next:after.intersections)if(next.name==group.name){found=next.resolved&&next.points.size()==group.points.size();if(found)for(int n=0;n<group.points.size();++n)for(int m=0;m<group.points.size();++m)if(n!=m&&QLineF(next.points[n],group.points[m]).length()+.0004<QLineF(next.points[n],group.points[n]).length())return false;break;}if(!found)return false;
  }return true;
}
QString pathIntersection(const Scene &scene,int first,int second,int crossing,QString *error){
  auto fail=[&](const QString &s){if(error)*error=s;return QString();};bool ok=false;const auto points=pathCrossings(scene,first,second,&ok);
  if(!ok||crossing<0||crossing>=points.size()||scene.intersections.size()>=16||scene.items.size()>450)return fail(QStringLiteral("pathIntersection"));
  QSet<QString> used=occupiedNames(scene);QStringList pathNames;QVector<QPair<Span,QString>> changes;
  auto fresh=[&](const QString &prefix){for(int n=1;n<10000;++n){const auto name=prefix+QString::number(n);if(!scene.source.contains(name)&&!scene.styleContext.contains(name)&&!used.contains(name)){used.insert(name);return name;}}return QString();};
  for(int index:{first,second}){const auto &item=scene.items[index];auto name=item.style.value(QStringLiteral("name path"));if(!name.isEmpty()&&(!pathName(name)||namedPath(scene,name)!=index))return fail(QStringLiteral("pathIntersection"));if(name.isEmpty()){name=fresh(QStringLiteral("tlpath"));if(name.isEmpty())return {};const auto options=QLatin1Char('[')+editOption(item.options,QStringLiteral("name path"),name)+QLatin1Char(']');changes.append({{item.optionsStart>=0?item.optionsStart:item.insertOptions,item.optionsStart>=0?item.optionsEnd:item.insertOptions,{}},options});}pathNames.append(name);}
  const auto prefix=fresh(QStringLiteral("tlcross")),id=freshName(scene,used);if(prefix.isEmpty())return {};
  auto base=parse(patched(scene.source,changes),scene.styleContext);if(!base.valid||base.items.size()!=scene.items.size())return {};
  const auto body=QStringLiteral("\\path[name intersections={of=%1 and %2,name=%3,sort by=%1}];\n").arg(pathNames[0],pathNames[1],prefix)+coordinateSource(id,prefix+QLatin1Char('-')+QString::number(crossing+1),true);
  const auto result=appendDrawing(base,body,QStringLiteral("intersections"));const auto after=parse(result,scene.styleContext);
  if(after.items.size()!=scene.items.size()+2||!after.items[scene.items.size()].located||!sameIntersectionBranches(scene,after))return {};
  return result;
}

namespace {
QString checkedIntersectionEdit(const Scene &scene,const QString &draft,QString *error=nullptr){
  const auto refreshed=refreshedTangents(scene,draft);
  if(!draft.isEmpty()&&refreshed.isEmpty()){if(error)*error=QStringLiteral("tangentChanged");return {};}
  if(!refreshed.isEmpty()&&!sameQuadraticRelations(scene,parse(refreshed,scene.styleContext))){if(error)*error=QStringLiteral("quadraticChanged");return {};}
  if(!refreshed.isEmpty()&&!sameConicRelations(scene,parse(refreshed,scene.styleContext))){if(error)*error=QStringLiteral("conicChanged");return {};}
  if(refreshed.isEmpty()||scene.intersections.isEmpty())return refreshed;
  if(!sameIntersectionBranches(scene,parse(refreshed,scene.styleContext))){if(error)*error=QStringLiteral("crossingChanged");return {};}
  return refreshed;
}
}
QString moved(const Scene &s,int n,const QPointF &d){return checkedIntersectionEdit(s,movedUnchecked(s,n,d));}
QString pointMoved(const Scene &s,int n,int point,const QPointF &p){return checkedIntersectionEdit(s,pointMovedUnchecked(s,n,point,p));}
QString resized(const Scene &s,int n,const QSizeF &size){return checkedIntersectionEdit(s,resizedUnchecked(s,n,size));}
QString property(const Scene &s,int n,const QString &key,const QString &value,QString *error){return checkedIntersectionEdit(s,propertyUnchecked(s,n,key,value,error),error);}
QString reordered(const Scene &s,int n,bool front){return checkedIntersectionEdit(s,reorderedUnchecked(s,n,front));}
