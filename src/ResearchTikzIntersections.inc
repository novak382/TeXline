// Native TikZ named-path intersections. Coordinates stay executable in ordinary TeX.
// Included in the parser's private namespace; no user code is executed here.
bool pathName(const QString &s){return QRegularExpression(QStringLiteral("^[A-Za-z][A-Za-z0-9_-]*$")).match(s).hasMatch();}
bool readPathIntersection(const Item &item,const QString &tail,Scene &scene){
  if(item.command!=QStringLiteral("path")||tail.trimmed()!=QStringLiteral(";")||scene.intersections.size()>=16)return false;
  const auto raw=options(item.options);if(raw.size()!=1||option(raw[0]).first!=QStringLiteral("name intersections"))return false;
  auto value=option(raw[0]).second;int at=0;const auto body=group(value,at,QLatin1Char('{'),QLatin1Char('}'));
  if(!body.valid()||!value.mid(at).trimmed().isEmpty())return false;
  QMap<QString,QString> keys;for(const auto &s:options(body.text)){const auto kv=option(s);if(keys.contains(kv.first))return false;keys[kv.first]=kv.second;}
  if(keys.size()!=3||!keys.contains(QStringLiteral("of"))||!keys.contains(QStringLiteral("name"))||!keys.contains(QStringLiteral("sort by")))return false;
  const auto pair=keys.value(QStringLiteral("of")).split(QRegularExpression(QStringLiteral("\\s+and\\s+")));
  if(pair.size()!=2||!pathName(pair[0])||!pathName(pair[1])||pair[0]==pair[1]||keys.value(QStringLiteral("sort by"))!=pair[0]||!pathName(keys.value(QStringLiteral("name"))))return false;
  for(const auto &p:scene.intersections)if(p.name==keys.value(QStringLiteral("name")))return false;
  PathIntersection p;p.first=pair[0];p.second=pair[1];p.name=keys.value(QStringLiteral("name"));p.start=item.start;p.end=item.end;scene.intersections.append(p);return true;
}
struct IntersectionSegment {QPointF a,b;};
bool finitePoint(const QPointF &p){return std::isfinite(p.x())&&std::isfinite(p.y())&&qAbs(p.x())<100000&&qAbs(p.y())<100000;}
double crossProduct(const QPointF &a,const QPointF &b){return a.x()*b.y()-a.y()*b.x();}
bool intersectionSegments(const Scene &scene,int index,QVector<IntersectionSegment> &out){
  if(index<0||index>=scene.items.size()||!scene.coordinateSafe||scene.partial||scene.spatial)return false;const auto &i=scene.items[index];
  if((scene.source+scene.styleContext).contains(QStringLiteral("\\pgfintersection"))||(scene.source+scene.styleContext).contains(QStringLiteral("\\pgfsetplottension")))return false;
  auto appearance=i.style;for(auto it=appearance.begin();it!=appearance.end();)if(QRegularExpression(QStringLiteral("^(?:route|close)[0-9]+$")).match(it.key()).hasMatch())it=appearance.erase(it);else ++it;
  if(i.spatial||i.clipping||!safeStyle(appearance)||!understoodStyle(appearance)||i.style.contains(QStringLiteral("rounded corners")))return false;
  auto line=[&](const QPointF &a,const QPointF &b){if(!finitePoint(a)||!finitePoint(b)||out.size()>=512)return false;if(QLineF(a,b).length()>1e-10)out.append({a,b});return true;};
  std::function<bool(QPointF,QPointF,QPointF,QPointF,int)> cubic;
  cubic=[&](QPointF a,QPointF b,QPointF c,QPointF d,int depth){
    if(!finitePoint(a)||!finitePoint(b)||!finitePoint(c)||!finitePoint(d))return false;
    const double chord=QLineF(a,d).length(),polygon=QLineF(a,b).length()+QLineF(b,c).length()+QLineF(c,d).length();
    if(polygon-chord<.00005&&qMax(qAbs(crossProduct(b-a,d-a)),qAbs(crossProduct(c-a,d-a)))<.0001*qMax(chord,.0001))return line(a,d);
    if(depth>=12)return false;const auto ab=(a+b)/2,bc=(b+c)/2,cd=(c+d)/2,abc=(ab+bc)/2,bcd=(bc+cd)/2,m=(abc+bcd)/2;
    return cubic(a,ab,abc,m,depth+1)&&cubic(m,bcd,cd,d,depth+1);
  };
  QVector<QPointF> p;
  if(i.kind==Kind::Plot){
    bool ok=false;int count=i.style.value(QStringLiteral("samples"),QStringLiteral("25")).toInt(&ok);if(!ok||count<2||count>256)return false;
    for(int n=0;n<count;++n){std::array<double,3> v{};if(!curveValues(i,i.domainMin+(i.domainMax-i.domainMin)*n/(count-1),v))return false;p.append(i.plotX*v[0]+i.plotY*v[1]+i.shift);}
    if(i.style.contains(QStringLiteral("smooth"))){
      // PGF smooth handler: default tension .5 times its .2775 scale.
      for(int n=1;n<p.size();++n){const auto a=p[n-1],d=p[n],b=n==1?a:a+(p[n]-p[n-2])*.13875,c=n+1==p.size()?d:d-(p[n+1]-p[n-1])*.13875;if(!cubic(a,b,c,d,0))return false;}
    }else for(int n=1;n<p.size();++n)if(!line(p[n-1],p[n]))return false;
    if(i.closed&&!line(p.last(),p.first()))return false;return !out.isEmpty();
  }
  for(int n=0;n<i.coordinates.size();++n){QPointF toward(nan,nan);if(i.kind==Kind::Line||i.kind==Kind::Curve){if(n+1<i.coordinates.size())toward=endpoint(scene,i.coordinates[n+1]);else if(n>0)toward=endpoint(scene,i.coordinates[n-1]);}const auto &coord=i.coordinates[n];for(const auto &ref:coord.references)for(const auto &owner:scene.items)if(owner.id==ref&&(owner.kind==Kind::Node||owner.start>=i.start))return false;const auto q=endpoint(scene,coord,toward)+(coord.numeric?i.shift:QPointF());if(!finitePoint(q))return false;p.append(q);}
  if(p.isEmpty())return false;
  if(i.kind==Kind::Ellipse){
    if(i.relation!=QStringLiteral("through")&&(scene.axisX!=QPointF(1,0)||scene.axisY!=QPointF(0,1))){
      const auto raw=scene.source.mid(i.radiusStart+1,i.radiusEnd-i.radiusStart-2);QStringList radii;
      if(i.radiusAsOptions){const auto values=style(raw,{});radii={values.value(QStringLiteral("radius"),values.value(QStringLiteral("x radius"))),values.value(QStringLiteral("radius"),values.value(QStringLiteral("y radius")))};}
      else radii=raw.split(QRegularExpression(QStringLiteral("\\s+and\\s+")));
      for(const auto &r:radii)if(!QRegularExpression(QStringLiteral("(?:cm|mm|pt|bp|in)\\s*}?[ \t]*$")).match(r).hasMatch())return false;
    }
    double x=i.size.width()/2,y=i.size.height()/2;if(i.relation==QStringLiteral("through")){if(p.size()!=2)return false;const auto d=p[1]-p[0];const double r=std::hypot(d.x()/scene.canvasScale.x(),d.y()/scene.canvasScale.y());x=r*qAbs(scene.canvasScale.x());y=r*qAbs(scene.canvasScale.y());}
    if(x<1e-7||y<1e-7)return false;x*=scene.canvasScale.x()<0?-1:1;y*=scene.canvasScale.y()<0?-1:1;const double k=.55228475;const auto o=p[0];
    return cubic(o+QPointF(x,0),o+QPointF(x,k*y),o+QPointF(k*x,y),o+QPointF(0,y),0)&&cubic(o+QPointF(0,y),o+QPointF(-k*x,y),o+QPointF(-x,k*y),o+QPointF(-x,0),0)&&cubic(o+QPointF(-x,0),o+QPointF(-x,-k*y),o+QPointF(-k*x,-y),o+QPointF(0,-y),0)&&cubic(o+QPointF(0,-y),o+QPointF(k*x,-y),o+QPointF(x,-k*y),o+QPointF(x,0),0);
  }
  if(i.kind==Kind::Curve&&p.size()==4)return cubic(p[0],p[1],p[2],p[3],0);
  if(i.kind==Kind::Rectangle&&p.size()==2){const QVector<QPointF> q{p[0],{p[1].x(),p[0].y()},p[1],{p[0].x(),p[1].y()},p[0]};for(int n=1;n<q.size();++n)if(!line(q[n-1],q[n]))return false;return true;}
  if(i.kind!=Kind::Line)return false;QPointF first=p[0];
  for(int n=1;n<p.size();++n){if(i.style.contains(QStringLiteral("close%1").arg(n-1))&&!line(p[n-1],first))return false;const auto route=i.style.value(QStringLiteral("route%1").arg(n));if(route==QStringLiteral("move")){first=p[n];continue;}auto prior=p[n-1];if(route==QStringLiteral("-|")||route==QStringLiteral("|-")){const auto bend=route==QStringLiteral("-|")?QPointF(p[n].x(),prior.y()):QPointF(prior.x(),p[n].y());if(!line(prior,bend))return false;prior=bend;}if(!line(prior,p[n]))return false;}
  return (!i.closed||line(p.last(),first))&&!out.isEmpty();
}
QVector<QPointF> intersectSegments(const QVector<IntersectionSegment> &a,const QVector<IntersectionSegment> &b,bool &ok){
  struct Hit {double order;QPointF point;};QVector<Hit> hits;QVector<QPointF> uncertain;ok=false;
  for(int n=0;n<a.size();++n)for(const auto &s:b){const auto &r=a[n];
    if(qMax(r.a.x(),r.b.x())+.004<qMin(s.a.x(),s.b.x())||qMax(s.a.x(),s.b.x())+.004<qMin(r.a.x(),r.b.x())||qMax(r.a.y(),r.b.y())+.004<qMin(s.a.y(),s.b.y())||qMax(s.a.y(),s.b.y())+.004<qMin(r.a.y(),r.b.y()))continue;
    const auto u=r.b-r.a,v=s.b-s.a,w=s.a-r.a;const double det=crossProduct(u,v),norm=QLineF(r.a,r.b).length()*QLineF(s.a,s.b).length();
    if(qAbs(det)<=1e-10*norm){
      if(qAbs(crossProduct(w,u))<1e-9*QLineF(r.a,r.b).length()){
        return {}; // PGF's singular line solver does not define collinear endpoint intersections either.
      }continue;
    }
    const double t=crossProduct(w,v)/det,q=crossProduct(w,u)/det;
    if(t< -1e-8||t>1+1e-8||q< -1e-8||q>1+1e-8){
      const double da=qMax(0.,qMax(-t,t-1))*(qAbs(u.x())+qAbs(u.y())),db=qMax(0.,qMax(-q,q-1))*(qAbs(v.x())+qAbs(v.y()));
      if(da<.004&&db<.004){if(uncertain.size()>=512)return {};uncertain.append(r.a+u*t);}continue;
    }
    if(qAbs(det)<.04*norm)return {}; // Near tangencies are not stable indexed crossings.
    const auto at=r.a+u*qBound(0.,t,1.);
    bool duplicate=false;for(const auto &h:hits)if(QLineF(h.point,at).length()<.0004){duplicate=true;break;}if(!duplicate){if(hits.size()>=32)return {};hits.append({n+t,at});}
  }
  // PGF accepts points just outside a segment (0.1pt). A separate such point
  // would change every subsequent index. Subdivision endpoints near a confirmed
  // crossing are harmless because PGF merges them into that same solution.
  for(const auto &p:uncertain){bool represented=false;for(const auto &h:hits)if(QLineF(p,h.point).length()<.01)represented=true;if(!represented)return {};}
  // PGF merges solutions within 1pt in both axes by default. Reject close pairs
  // with a margin instead of offering a coordinate that TeX might not create.
  for(int n=0;n<hits.size();++n)for(int m=0;m<n;++m)if(qAbs(hits[n].point.x()-hits[m].point.x())<.05&&qAbs(hits[n].point.y()-hits[m].point.y())<.05)return {};
  std::sort(hits.begin(),hits.end(),[](const Hit &a,const Hit &b){return a.order<b.order;});QVector<QPointF> points;for(const auto &h:hits)points.append(h.point);ok=true;return points;
}
int namedPath(const Scene &scene,const QString &name){int found=-1;for(int n=0;n<scene.items.size();++n)if(scene.items[n].style.value(QStringLiteral("name path"))==name){if(found>=0)return -1;found=n;}return found;}
bool resolvePathIntersections(Scene &scene){bool changed=false;for(auto &group:scene.intersections)if(!group.resolved){const int a=namedPath(scene,group.first),b=namedPath(scene,group.second);if(a<0||b<0||scene.items[a].end>group.start||scene.items[b].end>group.start)continue;QVector<IntersectionSegment> first,second;if(!intersectionSegments(scene,a,first)||!intersectionSegments(scene,b,second))continue;bool ok=false;const auto points=intersectSegments(first,second,ok);if(ok){group.points=points;group.resolved=true;changed=true;}}return changed;}
QPointF intersectionReference(const Scene &scene,const QString &name){for(const auto &group:scene.intersections)if(name.startsWith(group.name+QLatin1Char('-'))){bool ok=false;const int at=name.mid(group.name.size()+1).toInt(&ok);if(ok&&at>0&&at<=group.points.size()&&name==group.name+QLatin1Char('-')+QString::number(at))return group.points[at-1];}return {nan,nan};}
