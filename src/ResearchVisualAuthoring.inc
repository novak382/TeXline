// Source-backed object drafts. Included by ResearchAuthoring.cpp so all three
// editors share the existing source transaction, preview and asset lifecycle.
struct VisualRange { int start{-1}, end{-1}; QString kind; bool valid() const { return start>=0 && end>start; } };
VisualRange enclosingEnvironment(const QString &source, int position, const QStringList &kinds) {
  const auto clean=maskProjectText(source);
  auto matches=QRegularExpression(QStringLiteral(R"(\\(begin|end)\s*\{([^{}]+)\})")).globalMatch(clean);
  QVector<VisualRange> stack; VisualRange best;
  while(matches.hasNext()) {
    const auto m=matches.next();
    if(m.captured(1)==QStringLiteral("begin"))stack.append({int(m.capturedStart()),-1,m.captured(2)});
    else {
      if(stack.isEmpty()||stack.last().kind!=m.captured(2)){stack.clear();continue;}
      auto range=stack.takeLast();range.end=int(m.capturedEnd());
      if(range.start<=position&&position<range.end&&kinds.contains(range.kind)&&(!best.valid()||range.end-range.start<best.end-best.start))best=range;
    }
  }
  return best;
}
const QStringList mathStructures{QStringLiteral("pmatrix"),QStringLiteral("bmatrix"),QStringLiteral("Bmatrix"),QStringLiteral("vmatrix"),QStringLiteral("Vmatrix"),QStringLiteral("matrix"),QStringLiteral("cases"),QStringLiteral("aligned"),QStringLiteral("align"),QStringLiteral("align*")};
struct MathStructure {
  QString kind, original; QVector<QStringList> rows;
  QVector<QVector<QPair<int,int>>> cells; bool valid{false}, comments{false};
};
MathStructure parseMathStructure(const QString &source) {
  MathStructure result;result.original=source;
  const auto clean=maskProjectText(source);
  const auto begin=QRegularExpression(QStringLiteral(R"(^\s*\\begin\s*\{([^{}]+)\})")).match(clean);
  if(!begin.hasMatch()||!mathStructures.contains(begin.captured(1)))return result;
  result.kind=begin.captured(1);
  const auto end=QRegularExpression(QStringLiteral(R"(\\end\s*\{)")+QRegularExpression::escape(result.kind)+QStringLiteral(R"(\}\s*$)")).match(clean);
  if(!end.hasMatch())return result;
  QStringList row;QVector<QPair<int,int>> cells;int first=int(begin.capturedEnd()),depth=0,nesting=0;
  auto addCell=[&](int to){int a=first,b=to;while(a<b&&source[a].isSpace())++a;while(b>a&&source[b-1].isSpace())--b;row.append(source.mid(a,b-a));cells.append({a,b});};
  auto addRow=[&]{result.rows.append(row);result.cells.append(cells);row.clear();cells.clear();};
  for(int i=first;i<end.capturedStart();++i) {
    if(source[i]==QLatin1Char('%')&&!escaped(source,i))result.comments=true;
    if(escaped(clean,i))continue;
    if(clean[i]==QLatin1Char('{'))++depth;
    else if(clean[i]==QLatin1Char('}')){if(--depth<0)return result;}
    if(clean[i]==QLatin1Char('\\')){static const QRegularExpression environment(QStringLiteral(R"(\\(begin|end)\s*\{)"));const auto token=environment.match(clean,i,QRegularExpression::NormalMatch,QRegularExpression::AnchorAtOffsetMatchOption);if(token.hasMatch())nesting+=token.captured(1)==QStringLiteral("begin")?1:-1;}
    if(depth==0&&nesting==0&&clean[i]==QLatin1Char('&')){addCell(i);first=i+1;}
    else if(depth==0&&nesting==0&&clean.mid(i,2)==QStringLiteral("\\\\")) {
      addCell(i);addRow();++i;first=i+1;
      int next=first;while(next<end.capturedStart()&&clean[next].isSpace())++next;
      if(next<end.capturedStart()&&(clean[next]==QLatin1Char('[')||clean[next]==QLatin1Char('*')))return result;
    }
    if(result.rows.size()>80||row.size()>24)return result;
  }
  if(depth||nesting)return result;
  if(first<end.capturedStart()&&!source.mid(first,end.capturedStart()-first).trimmed().isEmpty()||!row.isEmpty()){addCell(int(end.capturedStart()));addRow();}
  if(result.rows.isEmpty()){result.rows.append(QStringList{QString()});result.cells.append(QVector<QPair<int,int>>{{first,first}});}
  int columns=0;for(const auto &r:result.rows)columns=qMax(columns,int(r.size()));
  if(result.kind==QStringLiteral("cases"))columns=qMax(columns,2);
  if(columns>24||result.rows.size()>80)return result;
  // Ragged rows keep their original spans; structural changes deliberately
  // rebuild only this environment, never the enclosing equation or its label.
  for(auto &r:result.rows)while(r.size()<columns)r.append(QString());
  result.valid=true;return result;
}
QString mathStructureSource(const MathStructure &base,const QString &kind,const QVector<QStringList> &rows) {
  if(kind==base.kind&&rows==base.rows)return base.original;
  bool same=kind==base.kind&&rows.size()==base.rows.size();
  for(int r=0;same&&r<rows.size();++r)same=rows[r].size()==base.cells[r].size();
  if(same){QString draft=base.original;for(int r=int(rows.size())-1;r>=0;--r)for(int c=int(rows[r].size())-1;c>=0;--c)if(rows[r][c]!=base.rows[r][c]){const auto span=base.cells[r][c];draft.replace(span.first,span.second-span.first,rows[r][c]);}return draft;}
  QStringList lines;for(const auto &row:rows)lines.append(row.join(QStringLiteral(" & ")));
  return QStringLiteral("\\begin{%1}\n%2\n\\end{%1}").arg(kind,lines.join(QStringLiteral(" \\\\\n")));
}

#include "ResearchVisualWorkbench.inc"

class StructureDialog final:public MaterialDialog {
  SourceEdit edit;MathStructure base;ClipboardTable *grid;QComboBox *kind;QLabel *notice;PreviewImage *image;QPushButton *preview;
  DraftPreview *renderer;std::unique_ptr<VisualWorkbench> surface;MathCanvas *canvas{nullptr};QStackedWidget *views;bool syncing{false},inserting;bool inMath{false};QTimer delay;
  QVector<QStringList> rows()const{QVector<QStringList> out;for(int r=0;r<grid->rowCount();++r){QStringList row;for(int c=0;c<grid->columnCount();++c)row.append(grid->item(r,c)?grid->item(r,c)->text():QString());out.append(row);}return out;}
  QString draft()const{return mathStructureSource(base,kind->currentData().toString(),rows());}
  QString displayDraft()const{auto text=draft();return kind->currentData().toString().startsWith(QStringLiteral("align"))&&kind->currentData().toString()!=QStringLiteral("aligned")?text:QStringLiteral("\\[\n")+text+QStringLiteral("\n\\]");}
  void changed(){if(syncing)return;if(canvas)canvas->arrange();grid->record();renderer->invalidate();notice->setText(words("预览待更新","Preview pending"));delay.start();}
public:
  StructureDialog(TeXDocumentWindow *w,VisualRange range):MaterialDialog(w),edit(w,range.valid()?range.start:w->textCursor().selectionStart(),range.valid()?range.end:w->textCursor().selectionEnd()),inserting(!range.valid()) {
    setAttribute(Qt::WA_DeleteOnClose);setObjectName(QStringLiteral("researchStructureDialog"));setWindowTitle(words("公式结构","Formula structure"));authoringSurface(this);
    base=parseMathStructure(inserting?QStringLiteral("\\begin{pmatrix}\na & b \\\\\nc & d\n\\end{pmatrix}"):edit.original.mid(edit.start,edit.end-edit.start));
    surface=std::make_unique<VisualWorkbench>(this,words("公式结构","Formula structure"));auto &ui=*surface;
    kind=named(new QComboBox(ui.inspector),"researchStructureKind");
    const QStringList captions{words("圆括号矩阵","Parenthesized matrix"),words("方括号矩阵","Bracketed matrix"),words("花括号矩阵","Braced matrix"),words("行列式","Determinant"),words("双竖线矩阵","Double-bar matrix"),words("无括号矩阵","Plain matrix"),words("分段函数","Cases"),words("公式对齐","Aligned equations"),words("逐行编号","Numbered equations"),words("不编号的多行公式","Unnumbered equations")};
    for(int i=0;i<mathStructures.size();++i)kind->addItem(captions[i],mathStructures[i]);kind->setCurrentIndex(qMax(0,kind->findData(base.kind)));ui.field(words("结构","Structure"),kind);
    auto *more=textButton(ui.inspector,"researchStructureMore",words("调整行与列","Rows and columns"));auto *menu=new QMenu(more);more->setMenu(menu);more->setPopupMode(QToolButton::InstantPopup);ui.inspectorLayout->addWidget(more,0,Qt::AlignLeft);ui.inspectorLayout->addStretch();
    auto *canvasLayout=new QVBoxLayout(ui.canvas);canvasLayout->setContentsMargins(0,0,20,0);views=new QStackedWidget(ui.canvas);canvasLayout->addWidget(views);
    grid=named(new ClipboardTable(ui.canvas),"researchStructureGrid");grid->setSelectionMode(QAbstractItemView::ContiguousSelection);grid->setRowCount(base.rows.size());grid->setColumnCount(base.rows.isEmpty()?1:base.rows.first().size());grid->setFont(interfaceFont());
    for(int r=0;r<base.rows.size();++r)for(int c=0;c<base.rows[r].size();++c)grid->setItem(r,c,new QTableWidgetItem(base.rows[r][c]));
    canvas=new MathCanvas(grid,kind,views);views->addWidget(canvas);
    image=named(new PreviewImage(views),"researchStructurePreview");image->setAlignment(Qt::AlignCenter);image->setMinimumSize(120,100);image->setProperty("researchNaturalPreview",true);image->setProperty("researchQuietPreview",true);image->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Ignored);views->addWidget(image);
    auto *editTab=textButton(this,"researchStructureEditView",words("编辑","Edit"));auto *resultTab=textButton(this,"researchStructureResultView",words("排版效果","Typeset"));for(auto *tab:{editTab,resultTab}){tab->setCheckable(true);tab->setProperty("researchArticleScopeControl",true);configureButton(tab,ButtonRole::Text);}editTab->setChecked(true);ui.header->insertWidget(1,editTab);ui.header->insertWidget(2,resultTab);
    notice=ui.notice;notice->setObjectName(QStringLiteral("researchStructureNotice"));preview=ui.refresh;preview->setObjectName(QStringLiteral("researchStructurePreviewButton"));preview->hide();auto *buttons=ui.buttons;buttons->setObjectName(QStringLiteral("researchStructureButtons"));
    connect(editTab,&QToolButton::clicked,this,[=]{views->setCurrentIndex(0);editTab->setChecked(true);resultTab->setChecked(false);preview->hide();canvas->arrange();grid->setFocus();});connect(resultTab,&QToolButton::clicked,this,[=]{views->setCurrentIndex(1);editTab->setChecked(false);resultTab->setChecked(true);if(!image->property("researchDraftReady").toBool())preview->click();});
    renderer=new DraftPreview(image,notice,preview);delay.setSingleShot(true);delay.setInterval(450);
    auto render=[this]{if(base.valid&&edit.target)renderer->start(edit.target,displayDraft(),{QStringLiteral("amsmath")});};connect(&delay,&QTimer::timeout,this,render);connect(preview,&QPushButton::clicked,this,render);
    grid->metadata=[this]{return QVariantMap{{QStringLiteral("kind"),kind->currentData()}};};grid->restoreMetadata=[this](const QVariantMap &m){QSignalBlocker block(kind);kind->setCurrentIndex(kind->findData(m.value(QStringLiteral("kind"))));};grid->changed=[this]{changed();};grid->trackSelection=[this]{return !syncing;};grid->record();
    connect(grid,&QTableWidget::itemChanged,this,[this]{changed();});connect(kind,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{canvas->arrange();changed();});
    connect(grid,&QTableWidget::currentCellChanged,this,[this]{grid->rememberSelection();});connect(grid,&QTableWidget::itemSelectionChanged,this,[this]{grid->rememberSelection();});
    for(bool row:{true,false})for(int op=0;op<3;++op){auto *a=menu->addAction(row?(op==0?words("在上方插入行","Insert row above"):op==1?words("在下方插入行","Insert row below"):words("删除行","Delete row")):(op==0?words("在左侧插入列","Insert column before"):op==1?words("在右侧插入列","Insert column after"):words("删除列","Delete column")));a->setObjectName(QStringLiteral("researchStructure%1%2").arg(row?QStringLiteral("Row"):QStringLiteral("Column")).arg(op));
      connect(a,&QAction::triggered,this,[this,row,op]{const int count=row?grid->rowCount():grid->columnCount(),at=qMax(0,row?grid->currentRow():grid->currentColumn());if(base.comments){notice->setText(words("含注释的结构请在源码中增删行列。","Add or remove rows in source when comments are present."));return;}if(op==2&&count<=1||op<2&&count>=(row?80:24))return;syncing=true;if(row){if(op==2)grid->removeRow(at);else grid->insertRow(at+(op==1));}else{if(op==2)grid->removeColumn(at);else grid->insertColumn(at+(op==1));}syncing=false;changed();});}
    grid->clearCells=[this]{syncing=true;for(const auto &s:grid->selectedRanges())for(int r=s.topRow();r<=s.bottomRow();++r)for(int c=s.leftColumn();c<=s.rightColumn();++c)if(auto *i=grid->item(r,c))i->setText(QString());syncing=false;changed();};
    grid->importCells=[this](const QVector<QStringList> &data,bool){int columns=0;for(const auto &r:data)columns=qMax(columns,int(r.size()));const int row=qMax(0,grid->currentRow()),col=qMax(0,grid->currentColumn());if(row+data.size()>80||col+columns>24)return;if(base.comments&&(row+data.size()>grid->rowCount()||col+columns>grid->columnCount()))return;syncing=true;grid->setRowCount(qMax(grid->rowCount(),row+int(data.size())));grid->setColumnCount(qMax(grid->columnCount(),col+columns));for(int r=0;r<data.size();++r)for(int c=0;c<data[r].size();++c)grid->setItem(row+r,col+c,new QTableWidgetItem(data[r][c]));syncing=false;changed();};
    connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);connect(buttons->button(QDialogButtonBox::Apply),&QPushButton::clicked,this,[this]{auto text=draft();if(kind->currentData().toString()==QStringLiteral("cases")&&grid->columnCount()!=2){notice->setText(words("分段函数使用两列：表达式与条件。","Cases use two columns: expression and condition."));return;}if(inMath&&(kind->currentData().toString()==QStringLiteral("align")||kind->currentData().toString()==QStringLiteral("align*"))&&base.kind!=kind->currentData().toString()){notice->setText(words("已有公式内部请使用“公式对齐”。","Use Aligned equations inside an existing formula."));return;}const auto parsed=parseMathStructure(text);if(!parsed.valid||parsed.rows!=rows()||!balancedText(text)){notice->setText(words("请检查单元格中的括号、& 和换行。","Check cell braces, alignment marks and row breaks."));return;}if(base.comments&&(kind->currentData().toString()!=base.kind||rows().size()!=base.rows.size())){notice->setText(words("请在源码中调整带注释的结构。","Change this commented structure in source."));return;}if(!inserting&&text==base.original){accept();return;}if(!inMath&&(inserting||base.kind==QStringLiteral("align")||base.kind==QStringLiteral("align*")))text=displayDraft();if(edit.apply(text,{QStringLiteral("amsmath")},notice))accept();});
    const auto context=enclosingEnvironment(edit.original,edit.start,{QStringLiteral("equation"),QStringLiteral("equation*"),QStringLiteral("align"),QStringLiteral("align*"),QStringLiteral("gather"),QStringLiteral("gather*"),QStringLiteral("displaymath")});inMath=context.valid()&&(inserting||context.start<edit.start||context.end>edit.end);
    if(!inMath){const auto prefix=maskProjectText(edit.original.left(edit.start));inMath=prefix.lastIndexOf(QStringLiteral("\\["))>prefix.lastIndexOf(QStringLiteral("\\]"))||prefix.lastIndexOf(QStringLiteral("\\("))>prefix.lastIndexOf(QStringLiteral("\\)"));int dollars=0,displayDollars=0;for(int i=0;i<prefix.size();++i)if(prefix[i]==QLatin1Char('$')&&!escaped(prefix,i)){if(i+1<prefix.size()&&prefix[i+1]==QLatin1Char('$')){++displayDollars;++i;}else ++dollars;}inMath|=(dollars%2)||(displayDollars%2);}
    if(!base.valid){grid->setEnabled(false);kind->setEnabled(false);more->setEnabled(false);buttons->button(QDialogButtonBox::Apply)->setEnabled(false);notice->setText(words("此结构包含自定义排版，请返回源码编辑。","This structure has custom layout. Edit it in source."));}
    ui.previewAction(menu);connect(this,&QDialog::finished,w,[w]{w->editor()->setFocus();});fitVisualWorkbench(this,840,460);grid->setCurrentCell(0,0);canvas->arrange();if(base.valid)delay.start();
  }
};

struct PictureTile {
  QString file,asset,caption,label,raw;QByteArray hash;QList<double> trim{0,0,0,0};
  bool clip{false};int start{-1},end{-1};QImage thumbnail;QSizeF points;
};
struct PictureGroup {
  QVector<PictureTile> tiles;QString prefix,suffix,caption,label,original;bool valid{false};int columns{2};double height{0};
};
PictureGroup parsePictureGroup(const QString &source) {
  PictureGroup out;out.original=source;auto clean=maskProjectText(source);
  const auto begin=QRegularExpression(QStringLiteral(R"(^\s*\\begin\s*\{figure\*?\}\s*(?:\[[^\]]*\])?)")).match(clean);
  const auto end=QRegularExpression(QStringLiteral(R"(\\end\s*\{figure\*?\}\s*$)")).match(clean);
  if(!begin.hasMatch()||!end.hasMatch())return out;
  auto candidates=QRegularExpression(QStringLiteral(R"(\\begin\s*\{subfigure\}\s*(?:\[[tbc]\]\s*)?\{([0-9.]+)\\(?:line|text)width\})")).globalMatch(clean,begin.capturedEnd());
  int first=-1,last=-1;
  while(candidates.hasNext()) {
    const auto m=candidates.next();const auto finish=QRegularExpression(QStringLiteral(R"(\\end\s*\{subfigure\})")).match(clean,m.capturedEnd());
    if(!finish.hasMatch()||m.capturedStart()<last||out.tiles.size()>=24)return out;
    auto inside=source.mid(m.capturedEnd(),finish.capturedStart()-m.capturedEnd());
    inside.replace(QStringLiteral("width=\\linewidth"),QStringLiteral("width=1\\linewidth"));
    const auto height=QRegularExpression(QStringLiteral(R"(,height=([0-9.]+)cm,keepaspectratio)")).match(inside);if(height.hasMatch()){if(out.tiles.isEmpty())out.height=height.captured(1).toDouble();else if(qAbs(out.height-height.captured(1).toDouble())>.00001)return out;inside.remove(height.capturedStart(),height.capturedLength());}
    const auto parsed=parseFigure(inside);if(!parsed.valid)return out;
    PictureTile tile;tile.file=parsed.file;tile.caption=parsed.caption;tile.label=parsed.label;tile.trim=parsed.trim;tile.clip=parsed.clip;tile.start=int(m.capturedStart());tile.end=int(finish.capturedEnd());tile.raw=source.mid(tile.start,tile.end-tile.start);out.tiles.append(tile);
    if(first<0){first=tile.start;const double width=m.captured(1).toDouble();if(width<=0||width>1)return out;out.columns=qBound(1,qRound(1.0/width),4);}last=tile.end;
  }
  if(out.tiles.isEmpty()) {
    auto pictures=QRegularExpression(QStringLiteral(R"(\\includegraphics\s*(?:\[[^\]]*\])?\s*\{[^{}]+\})")).globalMatch(clean,begin.capturedEnd());
    while(pictures.hasNext()) {const auto m=pictures.next();if(m.capturedStart()>=end.capturedStart()||out.tiles.size()>=24)return out;const auto parsed=parseFigure(source.mid(m.capturedStart(),m.capturedLength()));if(!parsed.valid)return out;PictureTile tile;tile.file=parsed.file;tile.trim=parsed.trim;tile.clip=parsed.clip;tile.start=int(m.capturedStart());tile.end=int(m.capturedEnd());tile.raw=source.mid(tile.start,tile.end-tile.start);out.tiles.append(tile);if(first<0)first=tile.start;last=tile.end;}
  }
  if(first<0)return out;
  auto between=clean.mid(first,last-first);for(int i=int(out.tiles.size())-1;i>=0;--i)between.remove(out.tiles[i].start-first,out.tiles[i].end-out.tiles[i].start);
  between.remove(QRegularExpression(QStringLiteral(R"(\\(?:hfill|quad|qquad|par|smallskip|medskip|bigskip)\b|\\\\)")));
  if(!between.trimmed().isEmpty())return out;
  // Structural rearrangement cannot silently discard comments between tiles.
  for(int i=first;i<last;++i)if(source[i]==QLatin1Char('%')&&!escaped(source,i))return out;
  out.prefix=source.left(first);out.suffix=source.mid(last);const auto outside=out.prefix+out.suffix;
  out.caption=command(outside,QStringLiteral("caption")).value;out.label=command(outside,QStringLiteral("label")).value;out.valid=true;return out;
}
QString pictureCommand(const PictureTile &tile,const QString &file,const QString &size) {
  QStringList options{size};bool cropped=false;QStringList trims;
  for(double x:tile.trim){cropped|=x>0;trims.append(QString::number(x,'g',12)+QStringLiteral("bp"));}
  if(cropped)options.append(QStringLiteral("trim=")+trims.join(QLatin1Char(' ')));
  if(tile.clip||cropped)options.append(QStringLiteral("clip"));
  return QStringLiteral("\\includegraphics[%1]{%2}").arg(options.join(QLatin1Char(',')),file);
}
void pictureThumbnail(PictureTile &tile) {
  if(!tile.thumbnail.isNull()||tile.asset.isEmpty())return;
  if(QFileInfo(tile.asset).suffix().compare(QStringLiteral("pdf"),Qt::CaseInsensitive)==0) {
    auto doc=QtPDF::Backend::Document::newDocument(tile.asset);if(doc&&doc->numPages()>0)if(auto page=doc->page(0).toStrongRef()){tile.points=page->pageSizeF();const auto scale=qMin(480.0/qMax(1.0,tile.points.width()),360.0/qMax(1.0,tile.points.height()));tile.thumbnail=page->renderToImage(scale*72,scale*72,QRect(),false);}
  }else {QImageReader reader(tile.asset);const auto pixels=reader.size();if(!pixels.isValid()||qint64(pixels.width())*pixels.height()>64000000)return;reader.setScaledSize(pixels.scaled(480,360,Qt::KeepAspectRatio));tile.thumbnail=reader.read();if(!tile.thumbnail.isNull()){const double dpiX=tile.thumbnail.dotsPerMeterX()>0?tile.thumbnail.dotsPerMeterX()*.0254:72,dpiY=tile.thumbnail.dotsPerMeterY()>0?tile.thumbnail.dotsPerMeterY()*.0254:72;tile.points=QSizeF(pixels.width()*72/dpiX,pixels.height()*72/dpiY);}}
}
class CropCanvas final:public QWidget {
  QImage image;QRectF area;QPointF anchor;bool dragging{false};
  QRectF imageRect()const{QSizeF size=image.size();size.scale(contentsRect().size()-QSize(16,16),Qt::KeepAspectRatio);return QRectF(QPointF(width()/2.0-size.width()/2,height()/2.0-size.height()/2),size);}
  QPointF normalized(QPointF p)const{const auto r=imageRect();return {qBound(0.0,(p.x()-r.left())/r.width(),1.0),qBound(0.0,(p.y()-r.top())/r.height(),1.0)};}
public:
  std::function<void(QRectF)> changed;CropCanvas(const QImage &value,QRectF crop,QWidget *p):QWidget(p),image(value),area(crop){setMinimumSize(200,180);setMouseTracking(true);}
  void setArea(QRectF value){area=value;update();}
protected:
  void paintEvent(QPaintEvent *)override{QPainter p(this);p.fillRect(rect(),Qt::white);const auto r=imageRect();p.drawImage(r,image);const QRectF selected(r.left()+area.left()*r.width(),r.top()+area.top()*r.height(),area.width()*r.width(),area.height()*r.height());QPainterPath shade;shade.addRect(r);shade.addRect(selected);p.fillPath(shade,QColor(255,255,255,150));p.setPen(QPen(QColor(QStringLiteral("#667e8c")),1));p.drawRect(selected);}
  void mousePressEvent(QMouseEvent *e)override{if(e->button()!=Qt::LeftButton||!imageRect().contains(e->position()))return;anchor=normalized(e->position());dragging=true;}
  void mouseMoveEvent(QMouseEvent *e)override{if(!dragging)return;const auto end=normalized(e->position());area=QRectF(anchor,end).normalized();update();}
  void mouseReleaseEvent(QMouseEvent *e)override{if(e->button()!=Qt::LeftButton||!dragging)return;dragging=false;if(area.width()<.01||area.height()<.01)area=QRectF(0,0,1,1);if(changed)changed(area);update();}
};
// Same literal graphics-path convention as the existing private export worker.
QStringList pictureDirectories(TeXDocumentWindow *window) {
  const auto root=rootOf(window);const auto directory=QFileInfo(root).absoluteDir();QStringList dirs{directory.absolutePath()};
  const auto text=sourceOf(root,sourceBuffers())+QLatin1Char('\n')+window->textDoc()->editorText();
  auto declarations=QRegularExpression(QStringLiteral(R"(\\graphicspath\s*\{((?:\s*\{[^{}]*\}\s*)+)\})")).globalMatch(maskProjectText(text));
  while(declarations.hasNext()){auto entries=QRegularExpression(QStringLiteral(R"(\{([^{}]*)\})")).globalMatch(declarations.next().captured(1));while(entries.hasNext()){const auto path=entries.next().captured(1);if(!path.contains(QLatin1Char('\\')))dirs.append(directory.absoluteFilePath(path));}}
  dirs.removeDuplicates();return dirs;
}
QString pictureAsset(const QStringList &dirs,const QString &name) {
  QStringList names{name};if(QFileInfo(name).suffix().isEmpty())for(const auto &extension:{QStringLiteral(".pdf"),QStringLiteral(".png"),QStringLiteral(".jpg"),QStringLiteral(".jpeg")})names.append(name+extension);
  for(const auto &candidate:names)for(const auto &path:dirs){QFileInfo file(QDir(path).filePath(candidate));if(file.isFile())return file.canonicalFilePath();}return {};
}
class PictureGroupDialog final:public MaterialDialog {
  QSharedPointer<QTemporaryDir> previewAssets;SourceEdit edit;PictureGroup base;QVector<PictureTile> tiles;PictureCanvas *list;std::unique_ptr<VisualWorkbench> surface;QStackedWidget *views;QComboBox *columns,*sizing;QDoubleSpinBox *height;QLineEdit *caption,*tileCaption;QLabel *notice;PreviewImage *image;QPushButton *preview;DraftPreview *renderer;QTimer delay;bool inserting,dirty{false},syncing{false};
  QVector<int> order()const{QVector<int> result;for(int i=0;i<list->count();++i)result.append(list->item(i)->data(Qt::UserRole).toInt());return result;}
  QString source(const QStringList &paths)const {
    if(!dirty&&!inserting)return base.original;
    QString body;const int count=columns->currentData().toInt();const auto ids=order();
    bool unchanged=!inserting&&ids.size()==base.tiles.size()&&count==base.columns&&caption->text()==base.caption&&sizing->currentIndex()==(base.height>0?1:0)&&(base.height<=0||qAbs(height->value()-base.height)<.00001);
    for(int i=0;unchanged&&i<ids.size();++i)unchanged=ids[i]==i&&tiles[i].caption==base.tiles[i].caption&&tiles[i].trim==base.tiles[i].trim&&tiles[i].clip==base.tiles[i].clip;
    if(unchanged)return base.original;
    const bool keepLayout=!inserting&&count==base.columns&&sizing->currentIndex()==(base.height>0?1:0)&&(base.height<=0||qAbs(height->value()-base.height)<.00001);
    for(int i=0;i<ids.size();++i){const auto &tile=tiles[ids[i]];
      if(keepLayout&&!tile.raw.isEmpty()&&tile.raw.startsWith(QStringLiteral("\\begin"))){
        auto raw=tile.raw;const auto image=QRegularExpression(QStringLiteral(R"(\\includegraphics\s*(?:\[([^\]]*)\])?\s*\{[^{}]+\})")).match(raw);QStringList sizingOptions;for(const auto &option:image.captured(1).split(QLatin1Char(','),Qt::SkipEmptyParts))if(!option.trimmed().startsWith(QStringLiteral("trim="))&&option.trimmed()!=QStringLiteral("clip"))sizingOptions.append(option);
        raw.replace(image.capturedStart(),image.capturedLength(),pictureCommand(tile,paths[ids[i]],sizingOptions.join(QLatin1Char(','))));raw=setCommand(raw,QStringLiteral("caption"),tile.caption);body+=raw;
      }else {const auto width=QString::number((1.0-.025*(count-1))/count,'f',5);body+=QStringLiteral("\\begin{subfigure}[t]{%1\\linewidth}\n\\centering\n").arg(width);
      const auto size=sizing->currentIndex()==0?QStringLiteral("width=\\linewidth"):QStringLiteral("width=\\linewidth,height=%1cm,keepaspectratio").arg(QString::number(height->value(),'g',6));body+=pictureCommand(tile,paths[ids[i]],size)+QLatin1Char('\n');
      if(!tile.caption.isEmpty())body+=QStringLiteral("\\caption{")+tile.caption+QStringLiteral("}\n");if(!tile.label.isEmpty())body+=QStringLiteral("\\label{")+tile.label+QStringLiteral("}\n");body+=QStringLiteral("\\end{subfigure}");}if(i+1<ids.size())body+=(i+1)%count?QStringLiteral("\\hfill\n"):QStringLiteral("\n\\par\\medskip\n");}
    QString prefix=base.prefix,suffix=base.suffix;auto outside=prefix+QStringLiteral("__TEXLINE_PICTURES__")+suffix;outside=setCommand(outside,QStringLiteral("caption"),caption->text());return outside.replace(QStringLiteral("__TEXLINE_PICTURES__"),body);
  }
  void changed(){if(syncing)return;dirty=true;list->columns=columns->currentData().toInt();list->arrange();list->viewport()->update();renderer->invalidate();notice->setText(words("预览待更新","Preview pending"));delay.start();}
  void render(){QStringList paths(tiles.size(),QString());for(int id:order()){const auto &tile=tiles[id];if(tile.asset.isEmpty()){notice->setText(words("未找到图片资源，请重新选择。","Image asset not found. Choose it again."));return;}paths[id]=previewAssetReference(tile.asset,tile.hash,previewAssets,notice);if(paths[id].isEmpty())return;}if(list->count()&&edit.target)renderer->start(edit.target,source(paths),{QStringLiteral("graphicx"),QStringLiteral("subcaption")},false,previewAssets);}
  void addFiles(const QStringList &files){for(const auto &file:files){if(list->count()>=24){notice->setText(words("每组最多 24 张图片。","A group supports up to 24 images."));break;}const auto info=QFileInfo(file);if(!info.isFile()||info.size()>64*1024*1024){notice->setText(words("图片不可读取或超过 64 MB。","The image is unreadable or larger than 64 MB."));continue;}PictureTile tile;tile.asset=info.canonicalFilePath();tile.hash=slideExportHash(tile.asset);pictureThumbnail(tile);if(tile.thumbnail.isNull())continue;const int id=tiles.size();tiles.append(tile);auto *item=new QListWidgetItem(QIcon(QPixmap::fromImage(tile.thumbnail)),info.fileName(),list);item->setData(Qt::UserRole,id);item->setToolTip(file);}if(list->currentRow()<0&&list->count())list->setCurrentRow(0);changed();}
  void cropCurrent(){const int row=list->currentRow();if(row<0)return;const int id=list->item(row)->data(Qt::UserRole).toInt();auto &tile=tiles[id];pictureThumbnail(tile);if(tile.thumbnail.isNull()||tile.points.isEmpty())return;
    MaterialDialog dialog(this);dialog.setObjectName(QStringLiteral("researchPictureCropDialog"));authoringSurface(&dialog);dialog.setWindowTitle(words("裁剪图片","Crop image"));auto *layout=new QVBoxLayout(&dialog);
    auto rectangle=[&]{const auto &t=tile.trim;return QRectF(t[0]/tile.points.width(),t[3]/tile.points.height(),1-(t[0]+t[2])/tile.points.width(),1-(t[1]+t[3])/tile.points.height());};auto *canvas=new CropCanvas(tile.thumbnail,rectangle(),&dialog);canvas->setObjectName(QStringLiteral("researchPictureCropCanvas"));layout->addWidget(canvas,1);
    auto *rowLayout=new QHBoxLayout;QList<QDoubleSpinBox*> fields;const QStringList labels{words("左","Left"),words("下","Bottom"),words("右","Right"),words("上","Top")};bool sync=false;
    for(int i=0;i<4;++i){rowLayout->addWidget(new QLabel(labels[i],&dialog));auto *f=new QDoubleSpinBox(&dialog);f->setObjectName(QStringLiteral("researchPictureTrim%1").arg(i));f->setRange(0,(i%2?tile.points.height():tile.points.width())*.99);f->setDecimals(2);f->setSuffix(QStringLiteral(" bp"));f->setValue(tile.trim[i]);fields.append(f);rowLayout->addWidget(f);connect(f,qOverload<double>(&QDoubleSpinBox::valueChanged),&dialog,[&]{if(sync)return;canvas->setArea(QRectF(fields[0]->value()/tile.points.width(),fields[3]->value()/tile.points.height(),1-(fields[0]->value()+fields[2]->value())/tile.points.width(),1-(fields[1]->value()+fields[3]->value())/tile.points.height()));});}layout->addLayout(rowLayout);
    canvas->changed=[&](QRectF rect){sync=true;fields[0]->setValue(rect.left()*tile.points.width());fields[1]->setValue((1-rect.bottom())*tile.points.height());fields[2]->setValue((1-rect.right())*tile.points.width());fields[3]->setValue(rect.top()*tile.points.height());sync=false;};
    auto *buttons=workspaceFooter(&dialog,layout,words("应用裁剪","Apply crop"));auto *reset=buttons->addButton(words("重置","Reset"),QDialogButtonBox::ResetRole);configureButton(reset);connect(reset,&QPushButton::clicked,&dialog,[&]{canvas->changed(QRectF(0,0,1,1));canvas->setArea(QRectF(0,0,1,1));});connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{if(fields[0]->value()+fields[2]->value()>=tile.points.width()||fields[1]->value()+fields[3]->value()>=tile.points.height())return;dialog.accept();});fitDialog(&dialog,680,500);
    if(dialog.exec()==QDialog::Accepted){for(int i=0;i<4;++i)tile.trim[i]=fields[i]->value();tile.clip=std::any_of(tile.trim.cbegin(),tile.trim.cend(),[](double x){return x>0;});changed();}
  }
public:
  PictureGroupDialog(TeXDocumentWindow *w,VisualRange range):MaterialDialog(w),edit(w,range.valid()?range.start:w->textCursor().selectionEnd(),range.valid()?range.end:w->textCursor().selectionEnd()),inserting(!range.valid()) {
    setAttribute(Qt::WA_DeleteOnClose);setObjectName(QStringLiteral("researchPictureGroupDialog"));setWindowTitle(words("多图排版","Arrange pictures"));authoringSurface(this);base=inserting?PictureGroup{}:parsePictureGroup(edit.original.mid(edit.start,edit.end-edit.start));if(inserting){base.valid=true;base.prefix=QStringLiteral("\\begin{figure}[htbp]\n\\centering\n");base.suffix=QStringLiteral("\n\\end{figure}");}tiles=base.tiles;
    surface=std::make_unique<VisualWorkbench>(this,words("多图排版","Arrange pictures"));auto &ui=*surface;
    auto *add=textButton(this,"researchPicturesAdd",words("添加图片…","Add images…"));ui.header->insertWidget(1,add);
    columns=named(new QComboBox(ui.inspector),"researchPicturesColumns");for(int i=1;i<=4;++i)columns->addItem(words("%1 列","%1 columns").arg(i),i);columns->setCurrentIndex(qBound(0,base.columns-1,3));ui.field(words("排列","Layout"),columns);
    sizing=named(new QComboBox(ui.inspector),"researchPicturesSizing");sizing->addItems({words("统一宽度","Equal widths"),words("限制高度","Limit height")});sizing->setCurrentIndex(base.height>0?1:0);ui.inspectorLayout->addWidget(sizing);
    height=named(new QDoubleSpinBox(ui.inspector),"researchPicturesHeight");height->setRange(.5,15);height->setValue(base.height>0?base.height:4);height->setSuffix(QStringLiteral(" cm"));height->setVisible(base.height>0);ui.inspectorLayout->addWidget(height);
    caption=field(ui.inspector,"researchPicturesCaption");caption->setText(base.caption);ui.field(words("总标题","Caption"),caption);ui.inspectorLayout->addSpacing(12);
    tileCaption=field(ui.inspector,"researchPicturesTileCaption");ui.field(words("选中图片","Selected image"),tileCaption);tileCaption->setPlaceholderText(words("子图标题","Subcaption"));
    auto *more=textButton(ui.inspector,"researchPicturesMore",words("调整图片","Adjust image"));auto *menu=new QMenu(more);more->setMenu(menu);more->setPopupMode(QToolButton::InstantPopup);ui.inspectorLayout->addWidget(more,0,Qt::AlignLeft);ui.inspectorLayout->addStretch();
    auto *canvasLayout=new QVBoxLayout(ui.canvas);canvasLayout->setContentsMargins(0,0,20,0);views=new QStackedWidget(ui.canvas);canvasLayout->addWidget(views);
    list=named(new PictureCanvas(views),"researchPicturesList");list->columns=base.columns;list->setViewMode(QListView::IconMode);list->setWrapping(true);list->setFlow(QListView::LeftToRight);list->setResizeMode(QListView::Adjust);list->setMovement(QListView::Snap);list->setDragDropMode(QAbstractItemView::InternalMove);list->setDefaultDropAction(Qt::MoveAction);list->setSpacing(0);list->setFrameShape(QFrame::NoFrame);list->setFont(interfaceFont());list->setMinimumWidth(140);list->setStyleSheet(QStringLiteral("QListWidget{border:0;outline:0;background:white;}"));views->addWidget(list);
    auto *delegate=new PictureCanvasDelegate(list);delegate->thumbnail=[this](int id){const auto &tile=tiles[id];if(tile.thumbnail.isNull()||tile.points.isEmpty())return tile.thumbnail;const auto &t=tile.trim;const auto size=tile.thumbnail.size();const auto crop=QRectF(t[0]/tile.points.width()*size.width(),t[3]/tile.points.height()*size.height(),(1-(t[0]+t[2])/tile.points.width())*size.width(),(1-(t[1]+t[3])/tile.points.height())*size.height()).toAlignedRect().intersected(tile.thumbnail.rect());return tile.thumbnail.copy(crop);};delegate->caption=[this](int id){const auto &tile=tiles[id];return tile.caption.isEmpty()?QFileInfo(tile.file.isEmpty()?tile.asset:tile.file).fileName():tile.caption;};list->setItemDelegate(delegate);list->reordered=[this]{changed();};list->filesDropped=[this](const QStringList &files){addFiles(files);};
    image=named(new PreviewImage(views),"researchPicturesPreview");image->setMinimumSize(120,100);image->setAlignment(Qt::AlignCenter);image->setProperty("researchQuietPreview",true);image->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Ignored);views->addWidget(image);
    auto *editTab=textButton(this,"researchPicturesEditView",words("排列","Arrange"));auto *resultTab=textButton(this,"researchPicturesResultView",words("排版效果","Typeset"));for(auto *tab:{editTab,resultTab}){tab->setCheckable(true);tab->setProperty("researchArticleScopeControl",true);configureButton(tab,ButtonRole::Text);}editTab->setChecked(true);ui.header->insertWidget(2,editTab);ui.header->insertWidget(3,resultTab);
    notice=ui.notice;notice->setObjectName(QStringLiteral("researchPicturesNotice"));preview=ui.refresh;preview->setObjectName(QStringLiteral("researchPicturesPreviewButton"));preview->hide();auto *buttons=ui.buttons;buttons->setObjectName(QStringLiteral("researchPicturesButtons"));renderer=new DraftPreview(image,notice,preview);delay.setSingleShot(true);delay.setInterval(500);connect(&delay,&QTimer::timeout,this,[this]{render();});connect(preview,&QPushButton::clicked,this,[this]{render();});
    connect(editTab,&QToolButton::clicked,this,[=]{views->setCurrentIndex(0);editTab->setChecked(true);resultTab->setChecked(false);preview->hide();add->show();list->arrange();});connect(resultTab,&QToolButton::clicked,this,[=]{views->setCurrentIndex(1);editTab->setChecked(false);resultTab->setChecked(true);add->hide();if(!image->property("researchDraftReady").toBool())render();});
    const auto assetDirs=pictureDirectories(w);for(int id=0;id<tiles.size();++id){auto &tile=tiles[id];tile.asset=pictureAsset(assetDirs,tile.file);tile.hash=slideExportHash(tile.asset);auto *item=new QListWidgetItem(QFileInfo(tile.file).fileName(),list);item->setData(Qt::UserRole,id);QTimer::singleShot(id*15,this,[this,id]{pictureThumbnail(tiles[id]);for(int i=0;i<list->count();++i)if(list->item(i)->data(Qt::UserRole).toInt()==id)list->item(i)->setIcon(QIcon(QPixmap::fromImage(tiles[id].thumbnail)));});}
    connect(list,&QListWidget::currentRowChanged,this,[this](int row){QSignalBlocker block(tileCaption);tileCaption->setEnabled(row>=0);tileCaption->setText(row<0?QString():tiles[list->item(row)->data(Qt::UserRole).toInt()].caption);});connect(tileCaption,&QLineEdit::textEdited,this,[this](const QString &text){if(list->currentItem())tiles[list->currentItem()->data(Qt::UserRole).toInt()].caption=text;changed();});connect(caption,&QLineEdit::textEdited,this,[this]{changed();});connect(list->model(),&QAbstractItemModel::rowsMoved,this,[this]{changed();});
    connect(add,&QToolButton::clicked,this,[this]{const auto paths=QFileDialog::getOpenFileNames(this,words("添加图片","Add images"),edit.target?QFileInfo(rootOf(edit.target)).absolutePath():QString(),QStringLiteral("Images (*.pdf *.png *.jpg *.jpeg)"));if(!paths.isEmpty())addFiles(paths);});connect(columns,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{changed();});connect(sizing,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int i){height->setVisible(i==1);changed();});connect(height,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this]{changed();});
    auto *crop=menu->addAction(words("裁剪…","Crop…"),this,[this]{cropCurrent();});crop->setObjectName(QStringLiteral("researchPicturesCrop"));
    for(int direction:{-1,1}){auto *a=menu->addAction(direction<0?words("向前移动","Move earlier"):words("向后移动","Move later"),this,[this,direction]{const int at=list->currentRow(),to=at+direction;if(at<0||to<0||to>=list->count())return;auto *item=list->takeItem(at);list->insertItem(to,item);list->setCurrentRow(to);changed();});a->setObjectName(direction<0?QStringLiteral("researchPicturesEarlier"):QStringLiteral("researchPicturesLater"));}
    auto *remove=menu->addAction(words("移除图片","Remove image"),this,[this]{if(list->currentRow()>=0){delete list->takeItem(list->currentRow());changed();}});remove->setObjectName(QStringLiteral("researchPicturesRemove"));
    connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);connect(buttons->button(QDialogButtonBox::Apply),&QPushButton::clicked,this,[this]{if(!base.valid||list->count()==0)return;if(!dirty&&!inserting){accept();return;}if(!edit.current()){notice->setText(words("原稿已变化，草稿仍保留在这里。","Source changed. Your draft is retained here."));return;}if(edit.target->untitled()){notice->setText(words("请先保存文稿，再导入图片。","Save the document before importing images."));return;}
      QStringList paths(tiles.size(),QString());std::vector<std::unique_ptr<AssetImport>> imported;QString dependency;const QStringList packages{QStringLiteral("graphicx"),QStringLiteral("subcaption")};if(!edit.prepare(packages,dependency,notice))return;
      for(int id:order()){const auto &tile=tiles[id];if(tile.hash.isEmpty()||slideExportHash(tile.asset)!=tile.hash){notice->setText(words("图片已变化或不可读取，请重新选择。","An image changed or is unreadable. Choose it again."));return;}if(!balancedText(tile.caption)){notice->setText(words("请检查子图标题的括号。","Check subcaption braces."));return;}auto asset=std::make_unique<AssetImport>();paths[id]=tile.file.isEmpty()?assetReference(tile.asset,rootOf(edit.target),notice,*asset,tile.hash):tile.file;if(paths[id].isEmpty())return;imported.push_back(std::move(asset));}
      const auto text=source(paths);if(!balancedText(text)){notice->setText(words("请检查标题中的括号。","Check caption braces."));return;}if(edit.apply(inserting?QLatin1Char('\n')+text+QLatin1Char('\n'):text,packages,notice)){for(auto &asset:imported)asset->keep();accept();}
    });if(!base.valid){add->setEnabled(false);buttons->button(QDialogButtonBox::Apply)->setEnabled(false);notice->setText(words("此组图包含自定义排版，请返回源码编辑。","This group has custom layout. Edit it in source."));}
    ui.previewAction(menu);connect(this,&QDialog::finished,w,[w]{w->editor()->setFocus();});fitVisualWorkbench(this,960,620);list->arrange();if(list->count())list->setCurrentRow(0);else tileCaption->setEnabled(false);if(base.valid&&list->count())delay.start();
  }
};

struct RevealEntry {int start{-1},end{-1};QString before,after,body,kind,spec;int first{1},last{0};};
struct RevealDraft {QString source;QVector<RevealEntry> entries;QVector<QPair<int,int>> pauses;bool valid{true};};
RevealDraft parseReveals(const QString &source) {
  RevealDraft out;out.source=source;const auto clean=maskProjectText(source);
  // Relative overlays and mode-specific specifications depend on surrounding
  // counters. Do not flatten those into absolute steps.
  if(QRegularExpression(QStringLiteral(R"(\\(?:onslide|alt|temporal|setbeamercovered|againframe)\b|\\begin\s*\{(?:onlyenv|uncoverenv|visibleenv|overprint|overlayarea)\}|\[[^\]]*<(?:[^>]+)>[^\]]*\]|allowframebreaks|\\pause\s*\[)")).match(clean).hasMatch())out.valid=false;
  if(QRegularExpression(QStringLiteral(R"(\\item\s*\[[^\]]*\]\s*<)")).match(clean).hasMatch())out.valid=false;
  auto pauses=QRegularExpression(QStringLiteral(R"(\\pause\b)")).globalMatch(clean);while(pauses.hasNext()){const auto m=pauses.next();out.pauses.append({int(m.capturedStart()),int(m.capturedEnd())});}
  auto matches=QRegularExpression(QStringLiteral(R"(\\(item|uncover|only|visible)(?![A-Za-z@])\s*(<([^>]+)>)?)")).globalMatch(clean);
  int previousEnd=-1;
  while(matches.hasNext()) {
    const auto m=matches.next();if(m.capturedStart()<previousEnd)continue;if(out.entries.size()>=200){out.valid=false;break;}
    RevealEntry e;e.kind=m.captured(1);e.spec=m.captured(3);e.start=int(m.capturedStart());e.end=int(m.capturedEnd());
    if(!e.spec.isEmpty()){const auto range=QRegularExpression(QStringLiteral(R"(^(\d+)(?:-(\d*))?$)")).match(e.spec);if(!range.hasMatch()){out.valid=false;continue;}e.first=range.captured(1).toInt();e.last=e.spec.contains(QLatin1Char('-'))?(range.captured(2).isEmpty()?0:range.captured(2).toInt()):e.first;if(e.first<1||e.first>40||e.last>40||e.last&&e.last<e.first)out.valid=false;}
    else if(!out.pauses.isEmpty()){for(const auto &pause:out.pauses)if(pause.first<e.start)++e.first;}
    e.before=QStringLiteral("\\")+e.kind;
    if(e.kind!=QStringLiteral("item")){int at=e.end;while(at<clean.size()&&clean[at].isSpace())++at;if(at>=clean.size()||clean[at]!=QLatin1Char('{')){out.valid=false;continue;}const int begin=at;group(clean,at);if(at<0){out.valid=false;continue;}e.body=source.mid(begin+1,at-begin-2);e.after=source.mid(e.end,at-e.end);e.end=at;previousEnd=at;if(QRegularExpression(QStringLiteral(R"(\\(?:item|uncover|only|visible|pause)\b)")).match(maskProjectText(e.body)).hasMatch())out.valid=false;}
    else {int last=clean.indexOf(QLatin1Char('\n'),e.end);if(last<0)last=clean.size();e.body=source.mid(e.end,last-e.end).trimmed();e.after=source.mid(e.end,0);}
    // Only the specification is replaced. Whitespace after a bare command is
    // part of its original token range and must remain after the new overlay.
    const int tokenEnd=e.start+1+e.kind.size();const int specEnd=m.captured(2).isEmpty()?tokenEnd:int(m.capturedEnd(2));
    if(e.kind==QStringLiteral("item")){e.end=specEnd;e.after=QString();}else e.after=source.mid(specEnd,e.end-specEnd);
    out.entries.append(e);
  }
  if(!out.pauses.isEmpty()){
    if(out.entries.isEmpty()||std::any_of(out.entries.cbegin(),out.entries.cend(),[](const auto &e){return e.kind!=QStringLiteral("item")||!e.spec.isEmpty();}))out.valid=false;
    for(const auto &pause:out.pauses)if(!enclosingEnvironment(source,pause.first,{QStringLiteral("itemize"),QStringLiteral("enumerate")}).valid())out.valid=false;
    if(!out.entries.isEmpty()&&out.pauses.last().first>out.entries.last().start)out.valid=false;
    const int lastList=qMax(clean.lastIndexOf(QStringLiteral("\\end{itemize}")),clean.lastIndexOf(QStringLiteral("\\end{enumerate}")));
    if(lastList>=0){const int tail=clean.indexOf(QLatin1Char('}'),lastList)+1;auto trailing=clean.mid(tail);trailing.remove(QRegularExpression(QStringLiteral(R"(\\end\s*\{frame\})")));if(!trailing.trimmed().isEmpty())out.valid=false;}
  }
  return out;
}
QString revealSource(const RevealDraft &base,const QVector<QPair<int,int>> &steps) {
  bool changed=false;for(int i=0;i<steps.size();++i)changed|=steps[i]!=qMakePair(base.entries[i].first,base.entries[i].last);if(!changed)return base.source;
  QVector<TextEdit> edits;for(int i=0;i<steps.size();++i){const auto &e=base.entries[i];if(base.pauses.isEmpty()&&steps[i]==qMakePair(e.first,e.last))continue;const auto range=steps[i].second==steps[i].first?QString::number(steps[i].first):QString::number(steps[i].first)+QLatin1Char('-')+(steps[i].second?QString::number(steps[i].second):QString());edits.append({e.start,e.end,e.before+QLatin1Char('<')+range+QLatin1Char('>')+e.after});}
  for(const auto &p:base.pauses)edits.append({p.first,p.second,QString()});std::sort(edits.begin(),edits.end(),[](const auto &a,const auto &b){return a.start>b.start;});auto text=base.source;for(const auto &e:edits)text.replace(e.start,e.end-e.start,e.text);return text;
}
#include "ResearchRevealEditor.inc"

void openStructure(TeXDocumentWindow *w){if(!w||w->editor()->isReadOnly())return;if(auto *d=w->findChild<QDialog*>(QStringLiteral("researchStructureDialog"))){d->show();d->raise();return;}(new StructureDialog(w,enclosingEnvironment(w->textDoc()->editorText(),w->cursorPosition(),mathStructures)))->show();}
void openPictures(TeXDocumentWindow *w,bool insert=false){if(!w||w->editor()->isReadOnly())return;if(auto *d=w->findChild<QDialog*>(QStringLiteral("researchPictureGroupDialog"))){d->show();d->raise();return;}(new PictureGroupDialog(w,insert?VisualRange{}:enclosingEnvironment(w->textDoc()->editorText(),w->cursorPosition(),{QStringLiteral("figure"),QStringLiteral("figure*")})))->show();}
void openReveals(TeXDocumentWindow *w){if(!w||w->editor()->isReadOnly())return;if(auto *d=w->findChild<QDialog*>(QStringLiteral("researchRevealDialog"))){d->show();d->raise();return;}const auto range=enclosingEnvironment(w->textDoc()->editorText(),w->cursorPosition(),{QStringLiteral("frame")});if(!range.valid()){showTransientStatusMessage(w,words("将光标放在要调整的幻灯片中。","Place the cursor inside the slide to edit."));return;}(new RevealDialog(w,range))->show();}
