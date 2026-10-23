// Shared composition for transient visual editors. Controls use TeXline's
// existing metrics; the edited object, not a form, owns the main surface.
void fitVisualWorkbench(QDialog *dialog,int width,int height) {
  // A transient editor stays within the writing window, including half-screen.
  const auto parent=dialog->parentWidget();
  fitDialog(dialog,qMin(width,qMax(540,parent->width())),qMin(height,qMax(420,parent->height()-48)));
}
struct VisualWorkbench {
  QVBoxLayout *layout,*inspectorLayout;QHBoxLayout *header,*footer;QSplitter *split;
  QWidget *canvas,*inspector;QLabel *notice;QPushButton *refresh;QDialogButtonBox *buttons;
  VisualWorkbench(MaterialDialog *dialog,const QString &title) {
    dialog->setFont(interfaceFont());authoringSurface(dialog);
    layout=new QVBoxLayout(dialog);layout->setContentsMargins(24,20,24,16);layout->setSpacing(18);
    header=new QHBoxLayout;header->setSpacing(12);auto *name=new QLabel(title,dialog);auto font=interfaceFont();font.setWeight(QFont::DemiBold);name->setFont(font);header->addWidget(name);header->addStretch();layout->addLayout(header);
    split=new QSplitter(Qt::Horizontal,dialog);split->setHandleWidth(1);split->setChildrenCollapsible(false);split->setStyleSheet(QStringLiteral("QSplitter::handle {background:#e5e9ec;}"));
    canvas=new QWidget(split);inspector=new QWidget(split);inspector->setMinimumWidth(210);inspector->setMaximumWidth(244);inspectorLayout=new QVBoxLayout(inspector);inspectorLayout->setContentsMargins(20,0,0,0);inspectorLayout->setSpacing(12);split->addWidget(canvas);split->addWidget(inspector);split->setStretchFactor(0,1);split->setStretchFactor(1,0);layout->addWidget(split,1);
    footer=new QHBoxLayout;footer->setSpacing(8);notice=new QLabel(dialog);notice->setFont(interfaceFont());notice->setWordWrap(true);notice->setStyleSheet(QStringLiteral("color:#687780;"));notice->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);footer->addWidget(notice,1);
    refresh=new QPushButton(words("更新预览","Update preview"),dialog);configureButton(refresh);refresh->setAutoDefault(false);refresh->hide();
    buttons=new QDialogButtonBox(QDialogButtonBox::Apply|QDialogButtonBox::Cancel,dialog);buttons->button(QDialogButtonBox::Apply)->setText(words("应用","Apply"));buttons->button(QDialogButtonBox::Cancel)->setText(words("取消","Cancel"));for(auto *b:buttons->buttons()){configureButton(qobject_cast<QPushButton*>(b));qobject_cast<QPushButton*>(b)->setAutoDefault(false);}footer->addWidget(buttons);layout->addLayout(footer);
  }
  QLabel *section(const QString &text){auto *l=new QLabel(text,inspector);l->setFont(interfaceFont());l->setStyleSheet(QStringLiteral("color:#687780;"));inspectorLayout->addWidget(l);return l;}
  void field(const QString &caption,QWidget *widget){section(caption);inspectorLayout->addWidget(widget);}
  void previewAction(QMenu *menu){menu->addSeparator();auto *action=menu->addAction(words("更新预览","Update preview"));QObject::connect(action,&QAction::triggered,refresh,&QPushButton::click);QObject::connect(menu,&QMenu::aboutToShow,refresh,[this,action]{action->setEnabled(refresh->isEnabled());});}
};
class MathCellDelegate final:public QStyledItemDelegate {
public:using QStyledItemDelegate::QStyledItemDelegate;
  void paint(QPainter *p,const QStyleOptionViewItem &option,const QModelIndex &index)const override {
    p->save();p->setRenderHint(QPainter::Antialiasing);const auto rect=QRectF(option.rect).adjusted(5,5,-5,-5);const bool selected=option.state&QStyle::State_Selected;
    p->setBrush(selected?QColor(QStringLiteral("#edf2f5")):QColor(QStringLiteral("#f7f9fa")));p->setPen(selected?QPen(QColor(QStringLiteral("#b9c8d1")),1):Qt::NoPen);p->drawRoundedRect(rect,4,4);
    auto font=interfaceFont();p->setFont(font);p->setPen(QColor(QStringLiteral("#30363c")));const auto text=index.data().toString();p->drawText(rect.adjusted(10,0,-10,0),Qt::AlignCenter,p->fontMetrics().elidedText(text.isEmpty()?QStringLiteral("·"):text,Qt::ElideRight,qRound(rect.width())-20));p->restore();
  }
  QWidget *createEditor(QWidget *parent,const QStyleOptionViewItem &,const QModelIndex &)const override{auto *edit=new QLineEdit(parent);configureField(edit);edit->setAlignment(Qt::AlignCenter);return edit;}
  void updateEditorGeometry(QWidget *editor,const QStyleOptionViewItem &option,const QModelIndex &)const override{editor->setGeometry(option.rect.adjusted(5,5,-5,-5));}
};
class MathCanvas final:public QWidget {
public:
  ClipboardTable *grid;QComboBox *kind;
  MathCanvas(ClipboardTable *g,QComboBox *k,QWidget *p):QWidget(p),grid(g),kind(k){grid->setParent(this);grid->horizontalHeader()->hide();grid->verticalHeader()->hide();grid->setShowGrid(false);grid->setFrameShape(QFrame::NoFrame);grid->setItemDelegate(new MathCellDelegate(grid));grid->setStyleSheet(QStringLiteral("QTableWidget {background:white;border:0;outline:0;}"));}
  void arrange(){const int cols=qMax(1,grid->columnCount());const int cell=qBound(88,(width()-72)/cols,160);const int w=qMin(width()-56,cols*cell),h=qMin(qMax(80,height()-40),grid->rowCount()*54+2);grid->setMaximumSize(QWIDGETSIZE_MAX,QWIDGETSIZE_MAX);grid->setMinimumSize(0,0);grid->setGeometry((width()-w)/2,(height()-h)/2,w,h);grid->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);for(int c=0;c<cols;++c)grid->setColumnWidth(c,cell);grid->verticalHeader()->setDefaultSectionSize(54);for(int r=0;r<grid->rowCount();++r)grid->setRowHeight(r,54);update();}
protected:
  void resizeEvent(QResizeEvent *)override{arrange();}
  void paintEvent(QPaintEvent *)override{
    QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.setPen(QPen(QColor(QStringLiteral("#596771")),1.5));const auto box=QRectF(grid->geometry()).adjusted(-10,3,10,-3);const auto type=kind->currentData().toString();
    for(int side:{-1,1}){const qreal x=side<0?box.left():box.right(),inside=x-side*10;QPainterPath path;
      if(type==QStringLiteral("pmatrix")){path.moveTo(inside,box.top());path.cubicTo(x,box.top()+8,x,box.bottom()-8,inside,box.bottom());}
      else if(type==QStringLiteral("bmatrix")){path.moveTo(inside,box.top());path.lineTo(x,box.top());path.lineTo(x,box.bottom());path.lineTo(inside,box.bottom());}
      else if(type==QStringLiteral("Bmatrix")||type==QStringLiteral("cases")&&side<0){path.moveTo(inside,box.top());path.cubicTo(x,box.top(),inside,box.center().y()-6,x,box.center().y());path.cubicTo(inside,box.center().y()+6,x,box.bottom(),inside,box.bottom());}
      else if(type==QStringLiteral("vmatrix")||type==QStringLiteral("Vmatrix")){p.drawLine(QPointF(x,box.top()),QPointF(x,box.bottom()));if(type==QStringLiteral("Vmatrix"))p.drawLine(QPointF(x-side*4,box.top()),QPointF(x-side*4,box.bottom()));}
      p.drawPath(path);
    }
  }
};
class PictureCanvas final:public QListWidget {
  int dragged{-1};
public:
  int columns{2};std::function<void()> reordered;std::function<void(QStringList)> filesDropped;
  using QListWidget::QListWidget;
  void arrange(){const int width=qMax(60,(viewport()->width()-12)/qMax(1,columns));setGridSize(QSize(width,qBound(150,qRound(width*.72)+50,290)));doItemsLayout();}
protected:
  void resizeEvent(QResizeEvent *e)override{QListWidget::resizeEvent(e);arrange();}
  void paintEvent(QPaintEvent *event)override{QListWidget::paintEvent(event);if(count())return;QPainter p(viewport());p.setFont(interfaceFont());p.setPen(QColor(QStringLiteral("#687780")));p.drawText(viewport()->rect().adjusted(20,20,-20,-20),Qt::AlignCenter|Qt::TextWordWrap,words("拖入图片，开始排列","Drop images here to arrange them"));}
  void startDrag(Qt::DropActions)override{dragged=currentRow();if(dragged<0)return;auto *drag=new QDrag(this);auto *mime=new QMimeData;mime->setData(QStringLiteral("application/x-texline-picture-order"),QByteArray::number(dragged));drag->setMimeData(mime);drag->setPixmap(currentItem()->icon().pixmap(130,90));drag->exec(Qt::MoveAction);drag->deleteLater();dragged=-1;}
  void dragEnterEvent(QDragEnterEvent *event)override{if(event->source()==this||event->mimeData()->hasUrls())event->acceptProposedAction();else event->ignore();}
  void dragMoveEvent(QDragMoveEvent *event)override{if(event->source()==this||event->mimeData()->hasUrls())event->acceptProposedAction();else event->ignore();}
  void dropEvent(QDropEvent *event)override{if(event->source()!=this){QStringList files;for(const auto &url:event->mimeData()->urls())if(url.isLocalFile())files.append(url.toLocalFile());if(!files.isEmpty()&&filesDropped){filesDropped(files);event->acceptProposedAction();}else event->ignore();return;}if(dragged<0){event->ignore();return;}const auto target=indexAt(event->position().toPoint());const int to=target.isValid()?target.row():count()-1;if(to!=dragged&&to>=0){auto *item=takeItem(dragged);insertItem(to,item);setCurrentRow(to);if(reordered)reordered();}event->accept();arrange();}
};
class PictureCanvasDelegate final:public QStyledItemDelegate {
public:
  std::function<QImage(int)> thumbnail;std::function<QString(int)> caption;
  using QStyledItemDelegate::QStyledItemDelegate;
  QSize sizeHint(const QStyleOptionViewItem &,const QModelIndex &)const override{const auto *view=qobject_cast<QListView*>(parent());return view?view->gridSize():QSize(240,210);}
  void paint(QPainter *p,const QStyleOptionViewItem &o,const QModelIndex &index)const override{
    p->save();p->setClipRect(o.rect);p->setRenderHint(QPainter::Antialiasing);p->setRenderHint(QPainter::SmoothPixmapTransform);const auto rect=QRectF(o.rect).adjusted(8,8,-8,-8);const int id=index.data(Qt::UserRole).toInt();const bool selected=o.state&QStyle::State_Selected;
    const auto raster=thumbnail?thumbnail(id):QImage();const auto imageArea=rect.adjusted(12,12,-12,-38);QSizeF size=raster.size();size.scale(imageArea.size(),Qt::KeepAspectRatio);const QRectF destination(imageArea.center()-QPointF(size.width()/2,size.height()/2),size);if(!raster.isNull())p->drawImage(destination,raster);
    p->setPen(QColor(QStringLiteral("#596771")));p->setFont(interfaceFont());const auto text=caption?caption(id):index.data().toString();p->drawText(rect.adjusted(12,rect.height()-32,-12,-6),Qt::AlignCenter,p->fontMetrics().elidedText(text,Qt::ElideRight,qRound(rect.width())-24));
    if(selected){p->setBrush(Qt::NoBrush);p->setPen(QPen(QColor(QStringLiteral("#bccbd4")),1));p->drawRoundedRect(rect,4,4);}p->restore();
  }
};
