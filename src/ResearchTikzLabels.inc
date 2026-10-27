// Ordinary node offsets. Included inside Tikz's private namespace.
// PGF resets the inherited linear transform before applying local node shifts.
double shiftLength(const QString &raw,bool *ok){
  const double value=length(raw,ok);
  // A bare PGF dimension is in points; a bare Cartesian coordinate is in cm.
  return QRegularExpression(QStringLiteral("(?:cm|mm|pt|bp|in)\\s*}?\\s*$")).match(raw).hasMatch()?value:value*2.54/72.27;
}
bool translationOptions(const QString &raw,const QMap<QString,QString> &defs,QPointF &shift,int depth=0){
  if(depth>8)return false;
  for(const auto &entry:options(mask(raw))){const auto kv=option(entry);if(kv.first.isEmpty())continue;
    if(defs.contains(kv.first)){if(!kv.second.isEmpty()||!translationOptions(defs.value(kv.first),defs,shift,depth+1))return false;continue;}
    if(!safeStyle({{kv.first,kv.second}})||!understoodStyle({{kv.first,kv.second}}))return false;
    if(kv.first==QStringLiteral("xshift")||kv.first==QStringLiteral("yshift")){bool ok=false;const double value=shiftLength(kv.second,&ok);if(!ok)return false;if(kv.first==QStringLiteral("xshift"))shift.rx()+=value;else shift.ry()+=value;}
  }
  return finitePoint(shift)&&qAbs(shift.x())<10000&&qAbs(shift.y())<10000;
}
bool labelOptions(const QString &raw,const QMap<QString,QString> &defs,
                  QMap<QString,QString> &out,QPointF &shift,int depth=0){
  if(depth>8)return false;
  static const QSet<QString> allowed{
    QStringLiteral("above"),QStringLiteral("below"),QStringLiteral("left"),QStringLiteral("right"),
    QStringLiteral("above left"),QStringLiteral("above right"),QStringLiteral("below left"),QStringLiteral("below right"),
    QStringLiteral("anchor"),QStringLiteral("font"),QStringLiteral("text"),QStringLiteral("color"),
    QStringLiteral("inner sep"),QStringLiteral("outer sep"),QStringLiteral("align"),
    QStringLiteral("opacity"),QStringLiteral("text opacity"),QStringLiteral("pos"),
    QStringLiteral("midway"),QStringLiteral("at start"),QStringLiteral("at end"),
    QStringLiteral("near start"),QStringLiteral("near end"),QStringLiteral("very near start"),QStringLiteral("very near end"),
    QStringLiteral("draw"),QStringLiteral("fill"),QStringLiteral("rectangle"),QStringLiteral("circle"),
    QStringLiteral("ellipse"),QStringLiteral("diamond"),QStringLiteral("shape"),QStringLiteral("rounded corners"),
    QStringLiteral("minimum size"),QStringLiteral("minimum width"),QStringLiteral("minimum height"),
    QStringLiteral("text width"),QStringLiteral("line width"),QStringLiteral("node distance")};
  for(const auto &entry:options(mask(raw))){const auto kv=option(entry);if(kv.first.isEmpty())continue;
    if(defs.contains(kv.first)){if(!kv.second.isEmpty()||!labelOptions(defs.value(kv.first),defs,out,shift,depth+1))return false;continue;}
    if(kv.first==QStringLiteral("xshift")||kv.first==QStringLiteral("yshift")){
      bool ok=false;const double value=shiftLength(kv.second,&ok);if(!ok)return false;
      if(kv.first==QStringLiteral("xshift"))shift.rx()+=value;else shift.ry()+=value;
    }else if(!allowed.contains(kv.first)&&!color(kv.first,QColor()).isValid())return false;
    const QStringList directions{QStringLiteral("above"),QStringLiteral("below"),QStringLiteral("left"),QStringLiteral("right"),QStringLiteral("above left"),QStringLiteral("above right"),QStringLiteral("below left"),QStringLiteral("below right")};
    if(directions.contains(kv.first)){
      auto anchor=kv.first;anchor.replace(QStringLiteral("above"),QStringLiteral("south")).replace(QStringLiteral("below"),QStringLiteral("north")).replace(QStringLiteral("left"),QStringLiteral("east")).replace(QStringLiteral("right"),QStringLiteral("west"));out[QStringLiteral("__anchor")]=anchor;
      const double d=length(kv.second);double x=out.value(QStringLiteral("__gapx")).toDouble(),y=out.value(QStringLiteral("__gapy")).toDouble();
      if(kv.first.contains(QStringLiteral("right")))x+=d;if(kv.first.contains(QStringLiteral("left")))x-=d;if(kv.first.contains(QStringLiteral("above")))y+=d;if(kv.first.contains(QStringLiteral("below")))y-=d;
      out[QStringLiteral("__gapx")]=QString::number(x,'g',16);out[QStringLiteral("__gapy")]=QString::number(y,'g',16);
    }
    if(kv.first==QStringLiteral("anchor"))out[QStringLiteral("__anchor")]=kv.second;
    const QMap<QString,QString> times{{QStringLiteral("at start"),QStringLiteral("0")},{QStringLiteral("very near start"),QStringLiteral(".125")},{QStringLiteral("near start"),QStringLiteral(".25")},{QStringLiteral("midway"),QStringLiteral(".5")},{QStringLiteral("near end"),QStringLiteral(".75")},{QStringLiteral("very near end"),QStringLiteral(".875")},{QStringLiteral("at end"),QStringLiteral("1")}};
    if(times.contains(kv.first))out[QStringLiteral("__pos")]=times.value(kv.first);if(kv.first==QStringLiteral("pos"))out[QStringLiteral("__pos")]=kv.second;
    out[kv.first]=kv.second;
  }
  return std::isfinite(shift.x())&&std::isfinite(shift.y())&&qAbs(shift.x())<10000&&qAbs(shift.y())<10000;
}
void readLabel(Item &item,const QMap<QString,QString> &defs){
  if(item.kind!=Kind::Node&&item.insertLabelOptions<0)return;
  auto &s=item.labelStyle;auto &offset=item.labelShift;
  item.labelMovable=labelOptions(defs.value(QStringLiteral("every node")),defs,s,offset)
    &&labelOptions(item.kind==Kind::Node?item.options:item.labelOptions,defs,s,offset);
  const QStringList directions{QStringLiteral("above"),QStringLiteral("below"),QStringLiteral("left"),QStringLiteral("right"),QStringLiteral("above left"),QStringLiteral("above right"),QStringLiteral("below left"),QStringLiteral("below right")};
  for(const auto &key:directions)if(s.contains(key)&&!s.value(key).isEmpty()){
    bool ok=false;length(s.value(key),&ok);if(item.kind==Kind::Node&&(s.value(key).startsWith(QStringLiteral("of "))||s.value(key).contains(QStringLiteral(" of "))))ok=true;item.labelMovable&=ok;
  }
  if(item.kind!=Kind::Node&&s.contains(QStringLiteral("__pos"))&&(item.closed||item.kind!=Kind::Line&&item.kind!=Kind::Curve))item.labelMovable=false;
  if(s.contains(QStringLiteral("pos"))){bool ok=false;const double t=s.value(QStringLiteral("pos")).toDouble(&ok);item.labelMovable&=ok&&t>=0&&t<=1;}
  for(const auto &key:{QStringLiteral("inner sep"),QStringLiteral("outer sep")})if(s.contains(key)){bool ok=false;length(s.value(key),&ok);item.labelMovable&=ok;}
  if(s.contains(QStringLiteral("anchor")))item.labelMovable&=QStringList{QStringLiteral("center"),QStringLiteral("north"),QStringLiteral("south"),QStringLiteral("east"),QStringLiteral("west"),QStringLiteral("north east"),QStringLiteral("north west"),QStringLiteral("south east"),QStringLiteral("south west")}.contains(s.value(QStringLiteral("anchor")));
}
// Change only the final literal shift on each axis; earlier shifts and styles remain byte-identical.
QString offsetLabelOptions(QString raw,const QPointF &delta){
  for(int axis=0;axis<2;++axis){const double change=axis?delta.y():delta.x();if(qAbs(change)<1e-10)continue;
    const auto key=axis?QStringLiteral("yshift"):QStringLiteral("xshift");int start=0,valueStart=-1,valueEnd=-1;double old=0;
    for(const auto &entry:options(mask(raw))){const auto kv=option(entry);if(kv.first==key){bool ok=false;old=shiftLength(kv.second,&ok);if(!ok)return {};const int equal=entry.indexOf(QLatin1Char('='));int a=equal+1,b=int(entry.size());while(a<b&&entry[a].isSpace())++a;while(b>a&&entry[b-1].isSpace())--b;valueStart=start+a;valueEnd=start+b;}start+=int(entry.size())+1;}
    const double value=old+change;if(!std::isfinite(value)||qAbs(value)>10000)return {};
    const auto text=QString::number(value,'f',8).remove(QRegularExpression(QStringLiteral(R"(\.?0+$)")))+QStringLiteral("cm");
    if(valueStart>=0)raw.replace(valueStart,valueEnd-valueStart,text);
    else raw+=(raw.trimmed().isEmpty()?QString():QStringLiteral(","))+key+QLatin1Char('=')+text;
  }return raw;
}
