// Exact polynomial recognition and ordinary calc/let external contacts.
class QuadraticExpression {
  using Polynomial=std::array<double,3>;
  QString source;int at{0},steps{0},depth{0};bool valid{true};
  void skip(){while(at<source.size()&&source[at].isSpace())++at;}
  bool take(char c){skip();if(at<source.size()&&source[at]==QLatin1Char(c)){++at;return true;}return false;}
  Polynomial multiply(const Polynomial &a,const Polynomial &b){Polynomial c{};for(int n=0;n<3;++n)for(int m=0;m<3;++m)if(a[n]!=0&&b[m]!=0){if(n+m>2)valid=false;else c[n+m]+=a[n]*b[m];}return c;}
  Polynomial sum(){auto a=product();for(;;){const bool plus=take('+');if(!plus&&!take('-'))return a;const auto b=product();for(int n=0;n<3;++n)a[n]+=(plus?1:-1)*b[n];}}
  Polynomial product(){auto a=unary();for(;;){const bool times=take('*');if(!times&&!take('/'))return a;const auto b=unary();if(times)a=multiply(a,b);else{if(b[1]!=0||b[2]!=0||qAbs(b[0])<1e-9){valid=false;return {};}for(auto &v:a)v/=b[0];}}}
  Polynomial unary(){if(++steps>512||++depth>96){valid=false;--depth;return {};}
    Polynomial a{};if(take('+'))a=unary();else if(take('-')){a=unary();for(auto &v:a)v=-v;}else{a=primary();if(take('^')){const auto b=unary();if(b[1]!=0||b[2]!=0||b[0]<0||b[0]>2||b[0]!=std::floor(b[0]))valid=false;else{const auto base=a;a={1,0,0};for(int n=0;n<int(b[0]);++n)a=multiply(a,base);}}}--depth;return a;
  }
  Polynomial primary(){skip();if(take('(')){const auto a=sum();if(!take(')'))valid=false;return a;}if(at<source.size()&&source[at]==QLatin1Char('x')){++at;return {0,1,0};}const int start=at;while(at<source.size()&&(source[at].isDigit()||source[at]==QLatin1Char('.')))++at;bool ok=false;const double value=source.mid(start,at-start).toDouble(&ok);valid&=ok;return {value,0,0};}
public:
  QuadraticExpression(QString s,const QString &variable):source(parameterExpression(s,variable,QStringLiteral("x"))){}
  bool run(std::array<double,3> &out){if(source.size()>256)return false;out=sum();skip();if(!valid||at!=source.size())return false;for(double v:out)if(!std::isfinite(v)||qAbs(v)>50)return false;return true;}
};
bool quadraticCoordinate(const Coordinate &c){return c.operation==QStringLiteral("quadraticContact");}
double cross2(const QPointF &a,const QPointF &b){return a.x()*b.y()-a.y()*b.x();}
int quadraticOwner(const Scene &scene,const Coordinate &c){int found=-1;if(!quadraticCoordinate(c)||c.curveFunctions.size()!=2)return -1;for(int n=0;n<scene.items.size();++n)if(scene.items[n].kind==Kind::Plot&&scene.items[n].curveFunctions==c.curveFunctions){if(found>=0)return -1;found=n;}return found;}
bool quadraticCoefficients(const Scene &scene,int index,std::array<QPointF,3> &out){
  if(index<0||index>=scene.items.size())return false;const auto &i=scene.items[index];if(i.kind!=Kind::Plot||i.spatial||!i.movable||!i.shift.isNull()||!pointCurve(scene,index)||qAbs(i.domainMin)>100||qAbs(i.domainMax)>100)return false;
  std::array<double,3> x{},y{};if(!QuadraticExpression(i.expressionX,i.variable).run(x)||!QuadraticExpression(i.expression,i.variable).run(y))return false;
  for(int n=0;n<3;++n)out[n]={x[n],y[n]};const double determinant=cross2(out[1],out[2]),na=std::hypot(out[2].x(),out[2].y()),nb=std::hypot(out[1].x(),out[1].y());if(na<.02||nb<.05||qAbs(determinant)<.02*na*nb||qAbs(determinant)>4000)return false;
  for(double t:{-1.,0.,1.,i.domainMin,i.domainMax})for(const auto &expression:{i.expressionX,i.expression}){bool ok=false;evaluatePgf(parameterExpression(expression,i.variable,QStringLiteral("x")),t,&ok);if(!ok)return false;}return true;
}
QString quadraticPath(const QString &name,const Coordinate &c){
  if(c.references.size()!=1||c.curveFunctions.size()!=2||(c.parameter!=1&&c.parameter!=-1))return {};
  // p2,p3 are the current coordinate basis, not the canvas-scaled screen basis.
  return QStringLiteral(R"(\path let \p1=(%1),\p2=(1,0),\p3=(0,1),
\n1={scalar(\x2/1cm)},\n2={scalar(\y2/1cm)},\n3={scalar(\x3/1cm)},\n4={scalar(\y3/1cm)},\n5={\n1*\n4-\n2*\n3},
\n6={(scalar(\x1/1cm)*\n4-scalar(\y1/1cm)*\n3)/\n5},\n7={(\n1*scalar(\y1/1cm)-\n2*scalar(\x1/1cm))/\n5},
\n8={%2(0)},\n9={%3(0)},\n{10}={((%2(1)-\n8)-(%2(-1)-\n8))/2},\n{11}={((%3(1)-\n9)-(%3(-1)-\n9))/2},\n{12}={((%2(1)-\n8)+(%2(-1)-\n8))/2},\n{13}={((%3(1)-\n9)+(%3(-1)-\n9))/2},
\n{14}={\n{10}*\n{13}-\n{11}*\n{12}},\n{15}={2*((\n8-\n6)*\n{13}-(\n9-\n7)*\n{12})},\n{16}={(\n8-\n6)*\n{11}-(\n9-\n7)*\n{10}},
\n{17}={max(abs(\n{14}),max(abs(\n{15}),abs(\n{16})))},\n{18}={\n{14}/\n{17}},\n{19}={\n{15}/\n{17}},\n{20}={\n{16}/\n{17}},\n{21}={-0.5*(\n{19}+(2*(\n{19}>=0)-1)*sqrt(\n{19}*\n{19}-4*\n{18}*\n{20}))},\n{22}={%4(\n{21}/\n{18},\n{20}/\n{21})} in coordinate (%5) at ({%2(\n{22})},{%3(\n{22})});)").arg(c.references[0],c.curveFunctions[0],c.curveFunctions[1],c.parameter==1?QStringLiteral("min"):QStringLiteral("max"),name);
}
bool readQuadraticPath(const QString &raw,Item &item,const Scene &scene){
  if(!scene.coordinateNodeSafe||!item.options.isEmpty()||!item.shift.isNull())return false;auto compact=raw;compact.remove(QRegularExpression(QStringLiteral("\\s+")));
  const auto head=QRegularExpression(QStringLiteral(R"(^\\pathlet\\p1=\(([A-Za-z][A-Za-z0-9_-]*)\),\\p2=\(1,0\),\\p3=\(0,1\),)")).match(compact);
  const auto functions=QRegularExpression(QStringLiteral(R"(\\n8=\{([A-Za-z][A-Za-z0-9_]*)\(0\)\},\\n9=\{([A-Za-z][A-Za-z0-9_]*)\(0\)\})")).match(compact);
  const auto name=QRegularExpression(QStringLiteral(R"(coordinate\(([A-Za-z][A-Za-z0-9_-]*)\)at)")).match(compact);if(!head.hasMatch()||!functions.hasMatch()||!name.hasMatch())return false;
  Coordinate c;c.operation=QStringLiteral("quadraticContact");c.references={head.captured(1)};c.curveFunctions={functions.captured(1),functions.captured(2)};c.start=item.start;c.end=item.end;
  for(int branch:{1,-1}){c.parameter=branch;auto expected=quadraticPath(name.captured(1),c);expected.remove(QRegularExpression(QStringLiteral("\\s+")));if(expected!=compact)continue;item.kind=Kind::Point;item.id=name.captured(1);item.coordinates={c};item.size={.07,.07};item.movable=false;return true;}return false;
}
QPointF quadraticContact(const Scene &scene,const Coordinate &c,const QPointF &point,double *parameter=nullptr){
  const int owner=quadraticOwner(scene,c);std::array<QPointF,3> v;if(!quadraticCoefficients(scene,owner,v))return {nan,nan};const auto &curve=scene.items[owner];
  if(c.start>=0&&curve.start>=c.start)return {nan,nan};int anchor=-1;for(int n=0;n<scene.items.size();++n)if(scene.items[n].kind==Kind::Point&&scene.items[n].id==c.references.value(0)){anchor=n;break;}
  QSet<int> active,done;int steps=0;if(anchor<0||(c.start>=0&&scene.items[anchor].start>=c.start)||!conicSourceItem(scene,anchor,active,done,steps))return {nan,nan};
  const auto x=curve.plotX,y=curve.plotY;const double determinant=cross2(x,y);if(qAbs(determinant)<1e-8)return {nan,nan};const QPointF p(cross2(point,y)/determinant,cross2(x,point)/determinant);
  const auto sx=scene.canvasScale.x(),sy=scene.canvasScale.y();if(qAbs(sx)<1e-8||qAbs(sy)<1e-8||qAbs(point.x()/sx)>400||qAbs(point.y()/sy)>400||qAbs(p.x())>100||qAbs(p.y())>100)return {nan,nan};
  for(const auto &basis:{scene.axisX,scene.axisY})if(qAbs(basis.x())>20||qAbs(basis.y())>20)return {nan,nan};const double nx=std::hypot(scene.axisX.x(),scene.axisX.y()),ny=std::hypot(scene.axisY.x(),scene.axisY.y());if(nx<.1||ny<.1||nx>20||ny>20||qMax(nx,ny)>100*qMin(nx,ny)||qAbs(cross2(scene.axisX,scene.axisY))<.05*nx*ny)return {nan,nan};
  double a=cross2(v[1],v[2]),b=2*cross2(v[0]-p,v[2]),d=cross2(v[0]-p,v[1]);const double scale=qMax(qAbs(a),qMax(qAbs(b),qAbs(d)));if(!std::isfinite(scale)||scale>4000||scale<.0001)return {nan,nan};a/=scale;b/=scale;d/=scale;
  const double discriminant=b*b-4*a*d;if(qAbs(a)<.005||discriminant<.005||(c.parameter!=1&&c.parameter!=-1))return {nan,nan};
  const double q=-.5*(b+(b>=0?1:-1)*std::sqrt(discriminant)),low=qMin(q/a,d/q),high=qMax(q/a,d/q);const double t=c.parameter==1?low:high;if(!std::isfinite(t)||qAbs(t)>20||t<curve.domainMin+.01||t>curve.domainMax-.01)return {nan,nan};
  for(const auto &expression:{curve.expressionX,curve.expression}){bool ok=false;evaluatePgf(parameterExpression(expression,curve.variable,QStringLiteral("x")),t,&ok);if(!ok)return {nan,nan};}
  const auto value=v[0]+v[1]*t+v[2]*t*t,velocity=v[1]+v[2]*(2*t);if(std::hypot(velocity.x(),velocity.y())<.01)return {nan,nan};const auto result=x*value.x()+y*value.y();
  if(QLineF(result,point).length()<.08||!std::isfinite(result.x())||!std::isfinite(result.y())||qAbs(result.x()/sx)>400||qAbs(result.y()/sy)>400||qAbs(result.x())>400||qAbs(result.y())>400)return {nan,nan};
  const double other=c.parameter==1?high:low;const auto otherValue=v[0]+v[1]*other+v[2]*other*other;if(QLineF(result,x*otherValue.x()+y*otherValue.y()).length()<.08)return {nan,nan};
  if(parameter)*parameter=t;return result;
}
bool sameQuadraticRelations(const Scene &before,const Scene &after){for(const auto &i:before.items)if(i.located)for(const auto &c:i.coordinates)if(quadraticCoordinate(c)){bool found=false;for(const auto &j:after.items)if(j.id==i.id&&j.kind==Kind::Point&&j.located&&j.coordinates.size()==1){const auto &d=j.coordinates[0];found=d.operation==c.operation&&d.references==c.references&&d.curveFunctions==c.curveFunctions&&d.parameter==c.parameter;break;}if(!found)return false;}return true;}
