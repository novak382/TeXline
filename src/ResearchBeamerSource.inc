// Parser members: declarations are source, not executed frame occurrences.
struct FrameMacro {bool supported{false},titlePage{false},literal{false};QString title,content,file,frameOptions;int frameStart{-1},frameEnd{-1};};
QHash<QString,FrameMacro> frameMacros;
QSet<QString> definedMacros,inlineActive,inlineDone;
bool templateCommand(const QString &cmd,const QString &s,int &p){
  const int count=cmd==QStringLiteral("usebackgroundtemplate")?1:
    cmd==QStringLiteral("setbeamertemplate")?2:cmd==QStringLiteral("addtobeamertemplate")?3:
    cmd==QStringLiteral("setbeamercolor")||cmd==QStringLiteral("setbeamerfont")?2:0;
  if(!count)return false;
  if(p<s.size()&&s[p]==QLatin1Char('*'))++p;
  int at=p;group(s,p);if(p==at)return false;
  QString opts;options(s,p,opts);
  for(int n=1;n<count;++n){spaces(s,p);if(p>=s.size()||s[p]!=QLatin1Char('{'))return cmd==QStringLiteral("setbeamertemplate")&&!opts.isEmpty();at=p;group(s,p);if(p==at)return false;}
  return true;
}
FrameMacro frameMacro(const QString &body,const QString &filename){
  FrameMacro result;result.file=filename;int p=0,frames=0;
  while(p<body.size()){
    spaces(body,p);if(p==body.size())break;if(body[p]!=QLatin1Char('\\'))return result;
    const int commandStart=p;const auto cmd=command(body,p);
    if(cmd==QStringLiteral("relax")||cmd==QStringLiteral("par"))continue;
    if(templateCommand(cmd,body,p))continue;
    if(cmd!=QStringLiteral("begin")&&cmd!=QStringLiteral("frame"))return result;
    if(++frames>1)return result;
    if(cmd==QStringLiteral("begin")&&group(body,p)!=QStringLiteral("frame"))return result;
    QString opts;options(body,p,opts);
    result.literal=cmd==QStringLiteral("begin");result.frameStart=commandStart;result.frameOptions=opts;
    QString content;
    if(cmd==QStringLiteral("frame")){const int before=p;content=group(body,p);if(p==before)return result;}
    else{
      const auto end=QRegularExpression(QStringLiteral(R"(\\(begin|end)\s*\{frame\})")).match(body,p);
      if(!end.hasMatch()||end.captured(1)!=QStringLiteral("end"))return result;
      int titleAt=p;result.title=pretty(group(body,titleAt));content=body.mid(p,int(end.capturedStart())-p);p=int(end.capturedEnd());
    }
    result.frameEnd=p;
    // No conditional execution, loop or nested frame-producing macro is inferred.
    if(QRegularExpression(QStringLiteral(R"(\\(?:if[A-Za-z@]*|else|fi|loop|repeat|foreach|csname|catcode|againframe)(?![A-Za-z@]))")).match(content).hasMatch())return result;
    for(auto m=frameMacros.begin();m!=frameMacros.end();++m)if(QRegularExpression(QStringLiteral("\\\\")+QRegularExpression::escape(m.key())+QStringLiteral("(?![A-Za-z@])")).match(content).hasMatch())return result;
    result.content=content;result.titlePage=QRegularExpression(QStringLiteral(R"(\\(?:maketitle|titlepage)(?![A-Za-z@]))")).match(content).hasMatch();
    if(result.title.isEmpty()){const auto title=QRegularExpression(QStringLiteral(R"(\\frametitle\*?\s*\{)")).match(content);if(title.hasMatch()){int at=int(title.capturedEnd())-1;result.title=pretty(group(content,at));}}
  }
  result.supported=frames==1;return result;
}
bool declaration(const QString &cmd,const QString &s,int &p,const QString &filename,bool top,bool record=true,bool *frameDefinition=nullptr){
  const bool environment=cmd==QStringLiteral("newenvironment")||cmd==QStringLiteral("renewenvironment");
  const bool tex=cmd==QStringLiteral("def")||cmd==QStringLiteral("gdef")||cmd==QStringLiteral("edef")||cmd==QStringLiteral("xdef");
  if(!environment&&!tex&&cmd!=QStringLiteral("newcommand")&&cmd!=QStringLiteral("renewcommand")&&cmd!=QStringLiteral("providecommand")&&cmd!=QStringLiteral("DeclareRobustCommand"))return false;
  if(p<s.size()&&s[p]==QLatin1Char('*'))++p;spaces(s,p);QString name;
  if(p<s.size()&&s[p]==QLatin1Char('{'))name=group(s,p).trimmed();else if(p<s.size()&&s[p]==QLatin1Char('\\'))name=QLatin1Char('\\')+command(s,p);
  bool zero=true;
  if(tex){const int at=p;while(p<s.size()&&s[p]!=QLatin1Char('{'))++p;zero=s.mid(at,p-at).trimmed().isEmpty();}
  else{spaces(s,p);if(p<s.size()&&s[p]==QLatin1Char('[')){bool ok=false;const auto args=group(s,p,QLatin1Char('['),QLatin1Char(']'));zero=args.toInt(&ok)==0&&ok;spaces(s,p);if(p<s.size()&&s[p]==QLatin1Char('[')){group(s,p,QLatin1Char('['),QLatin1Char(']'));zero=false;}}}
  const int before=p;const auto body=group(s,p);if(p==before||p>s.size()){unsure();return true;}
  if(environment){const int at=p;const auto ending=group(s,p);if(p==at)unsure();if(frameDefinition)*frameDefinition=QRegularExpression(QStringLiteral(R"(\\(?:begin|end)\s*\{frame\})")).match(body+ending).hasMatch();return true;}
  const auto matched=QRegularExpression(QStringLiteral(R"(^\\([A-Za-z@]+)$)")).match(name);if(!matched.hasMatch())return true;name=matched.captured(1);
  const bool already=definedMacros.contains(name),wasFrame=frameMacros.contains(name);
  if(cmd==QStringLiteral("providecommand")&&already)return true;
  const bool frames=QRegularExpression(QStringLiteral(R"(\\(?:begin\s*\{frame\}|frame(?![A-Za-z@])|againframe(?![A-Za-z@])))")).match(body).hasMatch();
  if(frameDefinition)*frameDefinition=frames||wasFrame;
  if(!record)return true;definedMacros.insert(name);
  // Local/conditional redefinitions must revoke, never inherit, an older proof.
  if(!top||conditionalDepth){if(wasFrame||frames)frameMacros.insert(name,FrameMacro{});return true;}
  if(frames){auto value=zero&&(cmd!=QStringLiteral("edef")&&cmd!=QStringLiteral("xdef"))?frameMacro(body,filename):FrameMacro{};frameMacros.insert(name,value);}
  else frameMacros.remove(name);
  return true;
}
bool inlineDependencies(const QString &text,const QString &filename,int depth){
  if(stop()||depth>32)return false;const auto clean=maskProjectText(text);int p=0;
  while(p<clean.size()){
    if(clean[p]!=QLatin1Char('\\')){++p;continue;}const auto cmd=command(clean,p);
    bool structuralDefinition=false;if(declaration(cmd,clean,p,filename,false,false,&structuralDefinition)){if(structuralDefinition)return false;continue;}
    if(frameMacros.contains(cmd)||QStringList{QStringLiteral("frame"),QStringLiteral("againframe"),QStringLiteral("documentclass"),QStringLiteral("includeonly"),QStringLiteral("includeonlyframes"),QStringLiteral("mode"),QStringLiteral("endinput"),QStringLiteral("catcode"),QStringLiteral("csname")}.contains(cmd))return false;
    if(cmd==QStringLiteral("begin")||cmd==QStringLiteral("end")){const auto env=group(clean,p);if(env==QStringLiteral("frame")||env==QStringLiteral("document"))return false;continue;}
    if(cmd!=QStringLiteral("input")&&cmd!=QStringLiteral("include")&&cmd!=QStringLiteral("subfile"))continue;
    spaces(clean,p);QString child;if(p<clean.size()&&clean[p]==QLatin1Char('{'))child=group(clean,p);else{const int at=p;while(p<clean.size()&&!clean[p].isSpace()&&clean[p]!=QLatin1Char('}'))++p;child=clean.mid(at,p-at);}
    if(child.isEmpty()||child.contains(QLatin1Char('\\'))||child.contains(QLatin1Char('#')))return false;
    const auto resolved=resolveDependency(child,filename,root,QStringLiteral(".tex"));
    if(resolved.isEmpty()||!authoredDependency(resolved,root)||active.contains(resolved)||inlineActive.contains(resolved))return false;
    if(inlineDone.contains(resolved))continue;
    const auto raw=buffers.contains(resolved)?buffers.value(resolved):read(resolved);if(raw.isEmpty()||total+raw.size()>12*1024*1024)return false;
    total+=int(raw.size());deck.sources.insert(resolved,raw);inlineActive.insert(resolved);const bool safe=inlineDependencies(raw,resolved,depth+1);inlineActive.remove(resolved);if(!safe)return false;inlineDone.insert(resolved);
  }return true;
}
void macroFrame(const FrameMacro &macro,const QString &filename,const QString &raw,const QString &s,int start,int end,int depth){
  if(!macro.supported||!inlineDependencies(macro.content,macro.file,depth)){unsure();return;}
  const int lineStart=start==0?0:raw.lastIndexOf(QLatin1Char('\n'),start-1)+1,nl=raw.indexOf(QLatin1Char('\n'),end),lineEnd=nl<0?int(raw.size()):nl;
  const bool isolated=s.mid(lineStart,start-lineStart).trimmed().isEmpty()&&s.mid(end,lineEnd-end).trimmed().isEmpty();
  SlideFrame f;f.file=filename;f.kind=QStringLiteral("macroframe");f.title=macro.title;f.start=isolated?lineStart:start;f.end=isolated?(nl<0?lineEnd:nl+1):end;f.line=int(QStringView(raw).left(start).count(QLatin1Char('\n')))+1;f.endLine=int(QStringView(raw).left(end-1).count(QLatin1Char('\n')))+1;f.titlePage=macro.titlePage;f.backup=backup;f.section=section+(subsection.isEmpty()?QString():QStringLiteral(" / ")+subsection);f.sliceable=isolated;f.editable=false;deck.frames.append(f);
}
bool groupedFrame(const QString &filename,const QString &raw,const QString &s,int &p,int depth){
  if(conditionalDepth||deck.frames.isEmpty())return false;
  const auto &previous=deck.frames.last();
  if(previous.file!=filename||previous.end>p||!s.mid(previous.end,p-previous.end).trimmed().isEmpty())return false;
  const int start=p,lineStart=start==0?0:raw.lastIndexOf(QLatin1Char('\n'),start-1)+1;
  if(!s.mid(lineStart,start-lineStart).trimmed().isEmpty())return false;
  int end=p;const auto body=group(s,end);
  if(end<=start+1||end>s.size()||s[end-1]!=QLatin1Char('}'))return false;
  const int nl=raw.indexOf(QLatin1Char('\n'),end),lineEnd=nl<0?int(raw.size()):nl;
  if(!s.mid(end,lineEnd-end).trimmed().isEmpty())return false;
  const auto frame=frameMacro(body,filename);
  if(!frame.supported||!frame.literal||frame.frameStart<0||frame.frameEnd<=frame.frameStart||frame.frameOptions.contains(QStringLiteral("allowframebreaks")))return false;
  const int count=deck.frames.size();macroFrame(frame,filename,raw,s,start,end,depth);p=end;
  if(deck.frames.size()==count)return true; // An unsafe input already marked this deck uncertain.
  auto &f=deck.frames.last();f.kind=QStringLiteral("groupframe");
  // Show the complete style group, but certify its actual literal frame close.
  f.line=int(QStringView(raw).left(start+1+frame.frameStart).count(QLatin1Char('\n')))+1;
  f.endLine=int(QStringView(raw).left(start+frame.frameEnd).count(QLatin1Char('\n')))+1;
  return true;
}
