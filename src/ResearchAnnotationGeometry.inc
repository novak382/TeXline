// PDF-point geometry, kept separate from authored TeX and from screen scale.
namespace {
QVector<QRectF> annotationLines(QVector<QRectF> boxes) {
  boxes.erase(std::remove_if(boxes.begin(),boxes.end(),[](const QRectF &b){
    return b.isEmpty()||!std::isfinite(b.x())||!std::isfinite(b.y())||!std::isfinite(b.width())||!std::isfinite(b.height());
  }),boxes.end());
  std::sort(boxes.begin(),boxes.end(),[](const QRectF &a,const QRectF &b){return a.top()==b.top()?a.left()<b.left():a.top()<b.top();});
  QVector<QRectF> lines;
  for(const auto &box:boxes) {
    bool joined=false;
    for(auto &line:lines)if(qAbs(line.center().y()-box.center().y())<qMax(line.height(),box.height())*.6&&
      box.left()<=line.right()+qMax(4.,line.height())&&box.right()>=line.left()-qMax(4.,line.height())) {
      line=line.united(box);joined=true;break;
    }
    if(!joined)lines.append(box);
  }
  return lines;
}
QRectF annotationBounds(const QVector<QRectF> &rects) {QRectF result;for(const auto &r:rects)result=result.united(r);return result;}
QString annotationPlain(const QString &text,QVector<int> *positions=nullptr) {
  QString result;
  for(int i=0;i<text.size();++i) {
    if(text[i]==QChar(0x00ad))continue;
    if(text[i]==QLatin1Char('-')&&i+1<text.size()&&(text[i+1]==QLatin1Char('\n')||text[i+1]==QLatin1Char('\r'))) {
      while(i+1<text.size()&&text[i+1].isSpace())++i;continue;
    }
    if(text[i].isSpace()) {if(result.isEmpty()||result.endsWith(QLatin1Char(' ')))continue;result+=QLatin1Char(' ');}
    else result+=text[i];
    if(positions)positions->append(i);
  }
  if(result.endsWith(QLatin1Char(' '))){result.chop(1);if(positions)positions->removeLast();}
  return result;
}
QVector<AnnotationLocation> annotationLocations(const Annotation &note,const QSharedPointer<QtPDF::Backend::Document> &doc,
    int expectedPage,const QVector<QRectF> &expected,const QString &pdfHash={},bool reviewCopy=false,bool sameSource=true) {
  if(!doc||expectedPage<0||expectedPage>=doc->numPages())return {};
  if(!reviewCopy&&!pdfHash.isEmpty()&&note.geometryHash==pdfHash&&!note.selectionRects.isEmpty()&&note.page>=0&&note.page<doc->numPages())
    return {{note.page,annotationLines(note.selectionRects)}};
  if(note.quote.trimmed().isEmpty()) {
    if(reviewCopy&&sameSource&&!note.referenceBox.isEmpty()&&!note.selectionRects.isEmpty()) {
      auto rects=note.selectionRects;const auto delta=annotationBounds(expected).topLeft()-note.referenceBox.topLeft();
      for(auto &box:rects)box.translate(delta);
      return {{expectedPage,annotationLines(rects)}};
    }
    return expected.isEmpty()?QVector<AnnotationLocation>():QVector<AnnotationLocation>{{expectedPage,annotationLines(expected)}};
  }
  QString text;QVector<QRectF> characters;QVector<int> pages;
  for(int n=qMax(0,expectedPage-1);n<qMin(int(doc->numPages()),expectedPage+3);++n)if(auto page=doc->page(n).toStrongRef()) {
    QtPDF::Backend::Page::BoxBoundaryList boxes;
    const auto size=page->pageSizeF();const auto content=page->selectedText({QPolygonF(QRectF(QPointF(),size))},nullptr,&boxes,true);
    if(content.size()!=boxes.size())continue;
    text+=content;characters+=boxes;for(int i=0;i<content.size();++i)pages.append(n);
    text+=QLatin1Char('\n');characters.append(QRectF());pages.append(n);
  }
  QVector<int> positions;const auto plain=annotationPlain(text,&positions),quote=annotationPlain(note.quote);
  if(quote.isEmpty())return {};
  int found=-1;double best=1e20,second=1e20;auto reference=annotationBounds(expected).center();
  // Two identical phrases can occur inside one source paragraph. Retain the
  // selected occurrence relative to that paragraph when only opinions moved it.
  if(reviewCopy&&sameSource&&!note.referenceBox.isEmpty()&&!note.selectionRects.isEmpty())
    reference=note.selectionRects.first().center()+annotationBounds(expected).topLeft()-note.referenceBox.topLeft();
  for(int at=0;(at=plain.indexOf(quote,at))>=0;++at) {
    const auto index=positions[at];const auto point=characters[index].center();
    const double score=qAbs(pages[index]-expectedPage)*10000.+QLineF(reference,point).length();
    if(score<best){second=best;best=score;found=at;}else second=qMin(second,score);
  }
  if(found<0||second-best<1.)return {}; // An ambiguous quote is never marked elsewhere.
  QMap<int,QVector<QRectF>> selected;
  for(int i=found;i<found+quote.size();++i){const int at=positions[i];if(!characters[at].isEmpty()&&!text[at].isSpace())selected[pages[at]].append(characters[at]);}
  QVector<AnnotationLocation> result;
  for(auto it=selected.cbegin();it!=selected.cend();++it)result.append({it.key(),annotationLines(it.value())});
  return result;
}
QString annotationOverlay(const QHash<QString,QVector<AnnotationLocation>> &locations,const QSharedPointer<QtPDF::Backend::Document> &doc) {
  QString commands;QMap<int,QVector<QRectF>> pages;
  for(auto it=locations.cbegin();it!=locations.cend();++it)for(const auto &location:it.value())pages[location.page]+=location.rects;
  for(auto it=pages.cbegin();it!=pages.cend();++it) {
    const auto page=doc->page(it.key()).toStrongRef();if(!page)continue;
    const auto size=page->pageSizeF();commands+=QStringLiteral("\\ifnum\\TeXlineReviewSheet=%1\n").arg(it.key()+1);
    for(const auto &box:annotationLines(it.value())) {
      if(!QRectF(QPointF(),size).contains(box))continue;
      const double y=qMin(size.height()-.5,box.bottom()+.8);
      // Kernel picture primitives keep this vector overlay independent of
      // packages, line breaking, page counters and all authored math/macros.
      for(double x=box.left();x<box.right();x+=4.) {
        const auto width=qMin(2.,box.right()-x);if(width<.15)break;
        commands+=QStringLiteral("\\put(%1,-%2){\\rule{%3bp}{.4bp}}\n").arg(x,0,'f',3).arg(y,0,'f',3).arg(width,0,'f',3);
      }
    }
    commands+=QStringLiteral("\\fi\n");
  }
  if(commands.isEmpty())return {};
  return QStringLiteral("\n% TeXline temporary annotation underlines\n\\newcount\\TeXlineReviewSheet\n"
    "\\AddToHook{shipout/foreground}{\\global\\advance\\TeXlineReviewSheet by 1\\relax\n"
    "\\begingroup\\setlength{\\unitlength}{1bp}\\definecolor{TeXlineReviewMark}{RGB}{146,130,112}\\color{TeXlineReviewMark}\n")+
    commands+QStringLiteral("\\endgroup}\n");
}
}
