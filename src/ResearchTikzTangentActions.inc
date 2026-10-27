// A tangent is ordinary calc geometry plus inspectable derivative declarations.
// Reopening verifies the derivative; visual upstream edits refresh it atomically.
namespace {
QString tangentTuple(const QString &point,const QStringList &names,double parameter){
  return QStringLiteral("$(%1)+({2*(%2(%4)/veclen(%2(%4),%3(%4)))},{2*(%3(%4)/veclen(%2(%4),%3(%4)))})$").arg(point,names[0],names[1],curveNumber(parameter));
}
QString prunedTangentFunctions(const Scene &before,const QString &draft){
  if(draft.isEmpty()||before.partial)return draft;
  QSet<QString> candidates;for(const auto &i:before.items)for(const auto &c:i.coordinates)if(c.operation==QStringLiteral("curveTangent")&&std::isfinite(tangentDirection(before,c).x()))for(const auto &name:c.curveFunctions)candidates.insert(name);
  if(candidates.isEmpty())return draft;
  const auto after=parse(draft,before.styleContext);if(!after.valid||after.partial)return draft;
  QVector<QPair<Span,QString>> patches;const auto clean=mask(draft);
  for(const auto &name:candidates){
    if(!after.functions.contains(name)||!QRegularExpression(QStringLiteral("^texlinetangent[0-9]+[xy]$")).match(name).hasMatch())continue;
    const QRegularExpression token(QStringLiteral("(?<![A-Za-z0-9_])")+QRegularExpression::escape(name)+QStringLiteral("(?![A-Za-z0-9_])"));
    if(token.match(mask(before.styleContext)).hasMatch())continue;auto hits=token.globalMatch(clean);int count=0;while(hits.hasNext()){hits.next();++count;}if(count!=1)continue;
    const auto &f=after.functions[name];const QRegularExpression declaration(QRegularExpression::escape(name)+QStringLiteral("\\s*\\(\\s*")+QRegularExpression::escape(f.variable)+QStringLiteral("\\s*\\)\\s*=\\s*[^;{}]+;"));
    const auto m=declaration.match(clean);if(m.hasMatch()&&m.capturedStart()<f.start&&m.capturedEnd()>f.end){int start=int(m.capturedStart());while(start>0&&(draft[start-1]==QLatin1Char(' ')||draft[start-1]==QLatin1Char('\t')))--start;if(start>0&&draft[start-1]==QLatin1Char('\n')){bool comment=false;const int line=draft.lastIndexOf(QLatin1Char('\n'),start-2)+1;for(int n=line;n<start-1;++n)if(draft[n]==QLatin1Char('%')&&!escaped(draft,n))comment=true;if(!comment)--start;}patches.append({{start,int(m.capturedEnd()),{}},QString()});}
  }
  auto result=patched(draft,patches);if(patches.isEmpty())return result;
  const auto ready=parse(result,before.styleContext);const auto declaration=QRegularExpression(QStringLiteral("declare\\s+function\\s*=\\s*\\{")).match(mask(result),ready.optionsStart);
  if(declaration.hasMatch()&&declaration.capturedEnd()<=ready.functionsEnd){const int begin=int(declaration.capturedEnd()),end=ready.functionsEnd;auto body=result.mid(begin,end-begin);const QRegularExpression blank(QStringLiteral("\\r?\\n[ \\t]*(?=\\r?\\n)"));while(blank.match(body).hasMatch())body.replace(blank,QString());result.replace(begin,end-begin,body);}
  return result;
}
QString refreshedTangents(const Scene &before,const QString &draft){
  if(draft.isEmpty())return {};
  bool needed=false;for(const auto &i:before.items)for(const auto &c:i.coordinates)needed|=c.operation==QStringLiteral("curveTangent");
  if(!needed)return draft;
  const auto after=parse(draft,before.styleContext);QVector<QPair<Span,QString>> patches;QMap<QString,QString> functions;
  if(!after.valid||!after.coordinateSafe)return {};
  for(const auto &old:before.items)for(const auto &c:old.coordinates)if(c.operation==QStringLiteral("curveTangent")){
    const Item *helper=nullptr,*point=nullptr;
    for(const auto &i:after.items){if(i.id==old.id)helper=&i;if(i.id==c.references.value(0))point=&i;}
    if(!helper)continue; // Removing the dependent object is allowed by the normal removal guard.
    if(old.id.isEmpty()||!point||helper->coordinates.size()!=1||point->coordinates.size()!=1||!point->shift.isNull()||!point->placementShift.isNull())return {};
    const auto &now=helper->coordinates[0],&p=point->coordinates[0];
    if(now.operation!=c.operation||now.references!=c.references||now.curveFunctions!=c.curveFunctions||p.curveFunctions.size()!=2)return {};
    const int owner=boundCurve(after,p);if(owner<0||after.items[owner].kind!=Kind::Plot||after.items[owner].spatial)return {};
    if(!std::isfinite(tangentDirection(before,c).x()))return {};
    for(int n=0;n<2;++n){
      const auto name=c.curveFunctions[n];if(!after.functions.contains(name)||!before.functions.contains(name))return {};
      const auto &f=after.functions[p.curveFunctions[n]],&d=after.functions[name];
      auto expression=derivative(f.expression,f.variable);if(expression.isEmpty())return {};
      expression.replace(QRegularExpression(QStringLiteral("(?<![A-Za-z\\\\])x(?![A-Za-z])")),d.variable);
      if(d.expression!=before.functions[name].expression&&d.expression!=expression)return {}; // Preserve independently edited derivatives; accept an already refreshed patch.
      if(functions.contains(name)&&functions[name]!=expression)return {};functions[name]=expression;
    }
    patches.append({{now.start,now.end,{}},QLatin1Char('(')+tangentTuple(point->id,c.curveFunctions,p.parameter)+QLatin1Char(')')});
  }
  for(auto i=functions.begin();i!=functions.end();++i){const auto &f=after.functions[i.key()];patches.append({{f.start,f.end,{}},i.value()});}
  const auto result=patched(draft,patches);const auto checked=parse(result,before.styleContext);
  if(!checked.valid||!checked.coordinateSafe)return {};
  for(const auto &i:checked.items)for(const auto &c:i.coordinates)if(c.operation==QStringLiteral("curveTangent")&&!i.located)return {};
  return result;
}
}
bool tangentPoint(const Scene &scene,int index){
  if(conicPointEligible(scene,index))return true;
  if(!scene.valid||scene.partial||!scene.coordinateSafe||!scene.coordinateNodeSafe||index<0||index>=scene.items.size()||scene.functions.size()>46)return false;
  const auto &point=scene.items[index];if(point.kind!=Kind::Point||point.id.isEmpty()||point.coordinates.size()!=1||!point.located||!point.shift.isNull()||!point.placementShift.isNull())return false;
  const auto &p=point.coordinates[0];const int curve=boundCurve(scene,p);
  if(curve<0||scene.items[curve].kind!=Kind::Plot||scene.items[curve].spatial||p.curveFunctions.size()!=2)return false;
  QPointF vector;for(int n=0;n<2;++n){const auto &f=scene.functions[p.curveFunctions[n]];const auto d=derivative(f.expression,f.variable);bool ok=false;const double value=d.isEmpty()?nan:evaluate(d,p.parameter,&ok);bool pgf=false,base=false;evaluatePgf(d,p.parameter,&pgf);evaluatePgf(parameterExpression(f.expression,f.variable,QStringLiteral("x")),p.parameter,&base);if(!ok||!pgf||!base||!std::isfinite(value)||qAbs(value)>10000)return false;if(n)vector.setY(value);else vector.setX(value);}
  const double speed=std::hypot(vector.x(),vector.y());if(speed<.01||speed>10000)return false;vector*=2/speed;const auto &c=scene.items[curve];const auto v=c.plotX*vector.x()+c.plotY*vector.y();return std::isfinite(v.x())&&std::isfinite(v.y())&&QPointF::dotProduct(v,v)>1e-12&&qAbs(point.position.x())+qAbs(v.x())<500&&qAbs(point.position.y())+qAbs(v.y())<500;
}
QString tangentAtPoint(const Scene &scene,int index){
  if(conicPointEligible(scene,index))return conicPointTangent(scene,index);
  if(!tangentPoint(scene,index)||scene.items.size()>445||scene.functionsEnd<0)return {};
  const auto &point=scene.items[index];const auto &p=point.coordinates[0];QString prefix;
  for(int n=1;n<10000;++n){prefix=QStringLiteral("texlinetangent%1").arg(n);if(!(scene.source+scene.styleContext).contains(prefix))break;prefix.clear();}if(prefix.isEmpty())return {};
  const QStringList names{prefix+QStringLiteral("x"),prefix+QStringLiteral("y")};QString declaration;
  for(int n=0;n<2;++n){const auto &f=scene.functions[p.curveFunctions[n]];auto d=derivative(f.expression,f.variable);d.replace(QRegularExpression(QStringLiteral("(?<![A-Za-z\\\\])x(?![A-Za-z])")),QStringLiteral("\\t"));declaration+=QStringLiteral("\n  %1(\\t)=%2;").arg(names[n],d);}
  int declarationStart=scene.functionsEnd;while(declarationStart>0&&scene.source[declarationStart-1].isSpace())--declarationStart;
  const auto base=parse(patched(scene.source,{{{declarationStart,scene.functionsEnd,{}},declaration+QLatin1Char('\n')}}),scene.styleContext);
  if(!base.valid||base.partial||!base.coordinateSafe)return {};
  QSet<QString> used;const auto tip=freshName(base,used,true),back=freshName(base,used,true);
  const auto body=coordinateSource(tip,tangentTuple(point.id,names,p.parameter),false)+coordinateSource(back,QStringLiteral("$(%1)!-1!(%2)$").arg(point.id,tip),false)+QStringLiteral("\\draw (%1)--(%2);").arg(back,tip);
  const auto draft=appendDrawing(base,body,QStringLiteral("calc"));const auto after=parse(draft,scene.styleContext);
  if(!after.valid||after.partial||!after.coordinateSafe||after.items.size()!=scene.items.size()+3||!sameIntersectionBranches(scene,after))return {};
  return draft;
}
