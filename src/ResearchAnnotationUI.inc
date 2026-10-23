namespace {
QString annotationStorePath(const QString &root) {
  QString identity=canonical(root);
#ifdef Q_OS_WIN
  identity=identity.toCaseFolded();
#endif
  const auto base=QFileInfo(Tw::Settings().fileName()).absolutePath()+QStringLiteral("/research-annotations");
  QDir().mkpath(base);return base+QLatin1Char('/')+digest(identity)+QStringLiteral(".json");
}
QString noteFingerprint(const QVector<Annotation> &notes) {
  QJsonArray rows;for(const auto &note:notes)rows.append(annotationJson(note));
  return digest(QString::fromUtf8(QJsonDocument(rows).toJson(QJsonDocument::Compact)));
}
QString annotationPdfSignature(const QString &pdf) {
  const QFileInfo file(pdf),metadata(pdfMetadataReadFile(pdf));
  return QStringLiteral("%1:%2:%3:%4").arg(file.size()).arg(file.lastModified().toMSecsSinceEpoch()).arg(metadata.size()).arg(metadata.lastModified().toMSecsSinceEpoch());
}
class AnnotationSync final:public TWSyncTeXSynchronizer {
  AnnotationBuild build;
  QString originalPdf,copyRoot;
  QString currentText(const QString &file)const {
    if(auto *window=TeXDocumentWindow::findDocument(file))return window->textDoc()->editorText();
    QFile input(file);if(!input.open(QIODevice::ReadOnly)||input.size()>4*1024*1024)return build.original.value(file);
    auto text=QString::fromUtf8(input.readAll());text.replace(QStringLiteral("\r\n"),QStringLiteral("\n"));return text;
  }
public:
  AnnotationSync(const AnnotationBuild &result,const QString &pdf,const QString &root):
    TWSyncTeXSynchronizer(result.pdf,{}, {},QFileInfo(result.copyToOriginal.key(root)).absolutePath()),
    build(result),originalPdf(pdf),copyRoot(QFileInfo(result.copyToOriginal.key(root)).absolutePath()){}
  TeXSyncPoint syncFromPDF(const PDFSyncPoint &point,Resolution)const override {
    auto hit=TWSyncTeXSynchronizer::syncFromPDF(point,LineResolution);
    const auto copy=canonical(QDir(copyRoot).absoluteFilePath(hit.filename));
    const auto file=build.copyToOriginal.value(copy);if(file.isEmpty()||hit.line<1)return {{},-1,-1,-1};
    const auto modified=build.modified.value(file),original=build.original.value(file),current=currentText(file);
    const int offset=lineOffset(modified,hit.line);if(offset<0)return {{},-1,-1,-1};
    int mapped=mapSourcePosition(modified,original,offset);
    mapped=mapSourcePosition(original,current,mapped);
    const int start=current.lastIndexOf(QLatin1Char('\n'),qMax(0,mapped-1))+1;
    return {file,current.left(mapped).count(QLatin1Char('\n'))+1,mapped-start,1};
  }
  PDFSyncPoint syncFromTeX(const TeXSyncPoint &point,Resolution)const override {
    const auto file=canonical(point.filename);if(!build.original.contains(file))return {{},-1,{}};
    const auto original=build.original.value(file),modified=build.modified.value(file),current=currentText(file);
    const int start=lineOffset(current,point.line);if(start<0)return {{},-1,{}};
    int offset=mapSourcePosition(current,original,qMin(start+qMax(0,int(point.col)),int(current.size())));
    offset=mapSourcePosition(original,modified,offset);
    auto result=TWSyncTeXSynchronizer::syncFromTeX({build.copyToOriginal.key(file),modified.left(offset).count(QLatin1Char('\n'))+1,0,-1},LineResolution);
    result.filename=originalPdf;return result;
  }
};
class AnnotationMark final:public QGraphicsObject {
  QPainterPath line;
  bool visibleLine;
  std::function<void()> clicked;
public:
  AnnotationMark(const QLineF &underline,bool paintLine,QGraphicsItem *parent,std::function<void()> callback):
    QGraphicsObject(parent),visibleLine(paintLine),clicked(std::move(callback)) {
    line.moveTo(underline.p1());line.lineTo(underline.p2());
    setCursor(Qt::PointingHandCursor);setZValue(40);
  }
  QRectF boundingRect()const override{return line.boundingRect().adjusted(-2,-2,2,2);}
  QPainterPath shape()const override{QPainterPathStroker hit;hit.setWidth(4);return hit.createStroke(line);}
  void paint(QPainter *p,const QStyleOptionGraphicsItem *,QWidget *)override {
    if(!visibleLine)return;
    p->setRenderHint(QPainter::Antialiasing);
    QPen pen(QColor(146,130,112),.4,Qt::CustomDashLine,Qt::FlatCap);pen.setDashPattern({5,5});
    p->setPen(pen);p->drawPath(line);
  }
  void mousePressEvent(QGraphicsSceneMouseEvent *event)override {event->accept();}
  void mouseReleaseEvent(QGraphicsSceneMouseEvent *event)override {
    event->accept();const auto callback=clicked;
    // Returning from the typeset PDF replaces the scene and its active tool.
    // Let the current mouse dispatch finish before either can be released.
    QTimer::singleShot(0,this,[callback]{if(callback)callback();});
  }
};
struct AnnotationViewState {
  qreal zoom{1};QPointF center;bool valid{false};
};
}
class AnnotationController final:public QObject {
  PDFDocumentWindow *window;
  QToolButton *pill,*originalButton,*reviewButton;
  QWidget *switcher,*switchContent;
  QAction *switchAction,*annotateAction,*regionAction;
  QMenu *reviewMenu;
  QVariantAnimation reveal;
  QPointer<PopupFrame> editor;
  QPointer<QPlainTextEdit> field;
  QPointer<QRubberBand> band;
  QVector<QPointer<AnnotationMark>> marks;
  QVector<Annotation> notes;
  QHash<QString,int> notePositions;
  struct NoteChange {QVector<Annotation> before,after;};
  QVector<NoteChange> noteHistory;int historyPosition{0};
  QAction *undoNotes{nullptr},*redoNotes{nullptr};
  Annotation pending,removed,relocating;
  int nextNumber{1};
  QString root,store,storedHash,compiledFingerprint,compiledPdfHash,errorLog;
  QHash<QString,QString> sources;
  AnnotationBuild compiled;
  AnnotationViewState originalState,reviewState;
  QSharedPointer<QtPDF::Backend::Document> originalDocument;
  std::shared_ptr<TWSyncTeXSynchronizer> originalSync;
  QString originalSignature;
  std::shared_ptr<std::atomic_bool> cancellation;
  quint64 generation{0};
  quint64 viewGeneration{0};
  bool review{false},wantReview{false},busy{false},exportPending{false},loading{false},pressing{false},restoreAfterReload{false},storeInvalid{false};
  QString focusId;
  QPoint pressed,released;
  bool regionSelecting{false},originalWatch{false};
  QTimer draftTimer;
  Annotation draft;
  AnnotationViewState state()const {
    auto *view=window->widget();
    return {view->zoomLevel(),view->mapToScene(view->viewport()->rect().center()),true};
  }
  void restoreState(const AnnotationViewState &value) {
    if(!value.valid)return;
    auto *view=window->widget();view->setZoomLevel(value.zoom);
    auto center=[this,value] {
      auto *v=window->widget();v->centerOn(value.center);
      // QGraphicsView::centerOn rounds differently from QRect::center.
      // Correct the final pixel offset without accumulating drift on toggles.
      const auto delta=v->mapFromScene(value.center)-v->viewport()->rect().center();
      v->horizontalScrollBar()->setValue(v->horizontalScrollBar()->value()+delta.x());
      v->verticalScrollBar()->setValue(v->verticalScrollBar()->value()+delta.y());
    };
    center();const auto ticket=++viewGeneration;
    // A newly loaded scene may show/hide a scrollbar on the next layout turn.
    // Restore after that turn too, unless the reader already interacted again.
    QTimer::singleShot(0,this,[this,ticket,center]{if(ticket==viewGeneration)center();});
  }
  void notice(const QString &zh,const QString &en) {
    showTransientStatusMessage(window,englishUi()?en:zh,5000,NoticePriority::Attention);
  }
  void dismiss() {pill->hide();if(editor)editor->close();}
  void refreshNotePositions() {
    notePositions.clear();
    for(const auto &note:notes) {
      if(note.opinion.trimmed().isEmpty()||!sources.contains(note.file))continue;
      int position=-1;
      if(resolvedAnnotationSpan(note,sources.value(note.file),&position).valid())notePositions.insert(note.id,position);
    }
  }
  QVector<Annotation> locatedNotes()const {
    QVector<Annotation> result;
    for(const auto &note:notes)if(notePositions.contains(note.id))result.append(note);
    return result;
  }
  bool persist(const QVector<Annotation> &next) {
    if(store.isEmpty())return false;
    if(storeInvalid){notice(QStringLiteral("现有批注记录无法读取，已保留原文件；当前输入仍保留。"),QStringLiteral("Existing annotation records cannot be read. The file and your current input are retained."));return false;}
    QLockFile lock(store+QStringLiteral(".lock"));lock.setStaleLockTime(10000);
    if(!lock.tryLock(0)){notice(QStringLiteral("批注正在另一窗口保存，请稍后重试。"),QStringLiteral("Annotations are being saved in another window. Try again shortly."));return false;}
    QFile previous(store);QByteArray bytes;if(previous.exists()&&(!previous.open(QIODevice::ReadOnly)||previous.size()>32*1024*1024))return false;
    if(previous.isOpen()){bytes=previous.readAll();previous.close();}
    if(digest(QString::fromUtf8(bytes))!=storedHash) {
      notice(QStringLiteral("批注已在另一窗口更新，请重新打开 PDF 后重试；当前输入仍保留。"),QStringLiteral("Annotations changed in another window. Reopen the PDF and retry; your input is retained."));return false;
    }
    QJsonArray rows;for(const auto &note:next)rows.append(annotationJson(note));
    int nextId=nextNumber;for(const auto &note:next)nextId=qMax(nextId,note.id.mid(1).toInt()+1);
    const QJsonObject object{{QStringLiteral("format"),1},{QStringLiteral("root"),root},{QStringLiteral("notes"),rows},
                             {QStringLiteral("nextId"),nextId},
                             {QStringLiteral("draft"),draft.opinion.isEmpty()?QJsonObject():annotationJson(draft)}};
    const auto data=QJsonDocument(object).toJson();
    QSaveFile output(store);
    if(!output.open(QIODevice::WriteOnly)||output.write(data)!=data.size()||!output.commit()) {
      notice(QStringLiteral("批注保存失败，请保留当前输入后重试。"),QStringLiteral("Annotations could not be saved. Keep this input and retry."));return false;
    }
    storedHash=digest(QString::fromUtf8(data));notes=next;nextNumber=nextId;refreshNotePositions();return true;
  }
  void historyControls() {if(undoNotes)undoNotes->setEnabled(historyPosition>0);if(redoNotes)redoNotes->setEnabled(historyPosition<noteHistory.size());}
  bool commitNotes(const QVector<Annotation> &next) {
    const auto before=notes;if(noteFingerprint(before)==noteFingerprint(next))return persist(next);
    if(!persist(next))return false;
    noteHistory.resize(historyPosition);noteHistory.append({before,next});
    if(noteHistory.size()>64)noteHistory.removeFirst();historyPosition=int(noteHistory.size());historyControls();return true;
  }
  void stepHistory(bool forward) {
    if(editor)return; // The opinion field owns its ordinary text undo.
    if(forward?historyPosition>=noteHistory.size():historyPosition<=0)return;
    const auto next=forward?noteHistory[historyPosition].after:noteHistory[historyPosition-1].before;
    if(!persist(next))return;historyPosition+=forward?1:-1;relocating={};regionAction->setChecked(false);
    invalidate();showOriginal();updateControls();drawMarks();historyControls();window->widget()->setFocus();
  }
  void hydrate() {
    if(loading)return;
    const auto next=window->getMainSourceFilename();
    if(next.isEmpty()) {
      draftTimer.stop();invalidate();root.clear();store.clear();notes.clear();notePositions.clear();sources.clear();draft={};relocating={};
      noteHistory.clear();historyPosition=0;historyControls();updateControls(false);clearMarks();return;
    }
    const bool changedRoot=root!=canonical(next);
    if(changedRoot){root=canonical(next);store=annotationStorePath(root);originalState={};reviewState={};}
    QFile input(store);QByteArray bytes;
    if(input.open(QIODevice::ReadOnly)&&input.size()<=32*1024*1024)bytes=input.readAll();
    const auto diskHash=digest(QString::fromUtf8(bytes));
    if(changedRoot||diskHash!=storedHash) {
      // A reload must observe deletion made in another window, including an
      // empty store. Never resurrect its old in-memory records on the next save.
      draftTimer.stop();noteHistory.clear();historyPosition=0;historyControls();notes.clear();draft={};removed={};relocating={};nextNumber=1;
      storedHash=diskHash;
      const auto object=QJsonDocument::fromJson(bytes).object();
      storeInvalid=input.exists()&&(bytes.isEmpty()||object.value(QStringLiteral("format")).toInt()!=1||canonical(object.value(QStringLiteral("root")).toString())!=root);
      if(object.value(QStringLiteral("format")).toInt()==1&&canonical(object.value(QStringLiteral("root")).toString())==root) {
        nextNumber=qMax(1,object.value(QStringLiteral("nextId")).toInt(1));
        const auto rows=object.value(QStringLiteral("notes")).toArray();
        if(rows.size()<=2048)for(const auto &row:rows) {
          auto note=annotationFromJson(row.toObject());
          if(!note.anchor.isEmpty()&&!note.opinion.trimmed().isEmpty()&&!note.id.isEmpty()){notes.append(note);nextNumber=qMax(nextNumber,note.id.mid(1).toInt()+1);}
        }
        draft=annotationFromJson(object.value(QStringLiteral("draft")).toObject());
      }
    }
    sources=pdfCompiledSources(window->fileName());refreshNotePositions();updateControls();drawMarks();
  }
  bool compact()const{return window->width()<(englishUi()?760:600);}
  void revealControls(bool animate=true) {
    const bool visible=!notePositions.isEmpty();
    // configureButton fixes the actual control metrics; its sizeHint can be
    // smaller than those limits and would make the first child spill left.
    const int width=visible?(originalButton->isHidden()?0:originalButton->minimumWidth())+reviewButton->minimumWidth()+4:0;
    // Animate only the clipping container. Laying out fixed buttons inside a
    // shrinking row makes Qt compress spacing until their captions overlap.
    if(visible)switchContent->setFixedSize(width,ControlMetrics::Height);
    const int start=switcher->maximumWidth();reveal.stop();
    if(visible){switchAction->setVisible(true);switcher->show();}
    if(!animate||!motionEnabled()||!window->isVisible()) {
      switcher->setFixedWidth(width);switchAction->setVisible(visible);return;
    }
    reveal.setStartValue(qMin(start,600));reveal.setEndValue(width);reveal.start();
  }
  void updateControls(bool animate=true) {
    originalButton->setVisible(!compact());
    originalButton->setText(words("原稿","Original"));reviewButton->setText(words("批注","Annotations"));
    for(auto *button:{originalButton,reviewButton}) {
      button->setProperty("researchEqualWidthCaption",words("批注","Annotations"));
      configureButton(button,ButtonRole::Text);
    }
    originalButton->setChecked(!review);reviewButton->setChecked(review);
    reviewButton->setToolTip(busy?words("正在编译批注稿；可继续编辑原稿","Typesetting annotations; source editing remains available"):
                                     compact()&&review?words("返回原稿","Return to original"):words("查看由 LaTeX 排版的批注","View annotations typeset by LaTeX"));
    reviewMenu->setTitle(words("批注","Annotations"));
    annotateAction->setText(words("添加批注","Add annotation"));
    if(undoNotes)undoNotes->setText(words("撤销批注操作","Undo annotation"));if(redoNotes)redoNotes->setText(words("重做批注操作","Redo annotation"));
    regionAction->setText(words("框选批注","Annotate a region"));
    revealControls(animate);
  }
  void clearMarks(){for(auto mark:marks)if(mark)delete mark;marks.clear();}
  void drawMarks() {
    clearMarks();if(notes.isEmpty()||!window->widget()->scene()||sources.isEmpty())return;
    const auto document=window->widget()->document().toStrongRef();if(!document)return;
    TWSyncTeXSynchronizer sync(window->fileName(),{}, {},QFileInfo(root).absolutePath());
    if(!sync.isValid())return;
    const auto hash=pdfMetadata(window->fileName()).value(QStringLiteral("pdfHash")).toString();
    for(const auto &note:notes) {
      const auto source=sources.value(note.file);const int start=notePositions.value(note.id,-1);if(start<0)continue;
      const int offset=qMin(start+note.selectionOffset,start+int(note.anchor.size())-1);
      auto point=sync.syncFromTeX({note.file,source.left(offset).count(QLatin1Char('\n'))+1,0,-1},TWSynchronizer::LineResolution);
      if(point.page<1||point.rects.isEmpty())continue;
      const auto locations=review?compiled.underlines.value(note.id):annotationLocations(note,document,int(point.page-1),point.rects,hash);
      for(const auto &location:locations)
      for(auto *item:window->widget()->scene()->items()) {
        auto *page=qgraphicsitem_cast<QtPDF::PDFPageGraphicsItem*>(item);
        if(!page||page->pageNum()!=location.page)continue;
        const auto backend=page->page().toStrongRef();if(!backend)break;
        const auto size=backend->pageSizeF();
        for(const auto &box:location.rects)if(QRectF(QPointF(),size).contains(box)) {
          const double y=qMin(size.height()-.5,box.bottom()+.8);
          auto *mark=new AnnotationMark(QLineF(page->mapFromPage(QPointF(box.left(),size.height()-y)),page->mapFromPage(QPointF(box.right(),size.height()-y))),!review,page,[this,id=note.id]{editExisting(id);});
          mark->setToolTip(note.opinion);marks.append(mark);
        }
        break;
      }
    }
  }
  bool capture(const QPoint &first,const QPoint &last,bool region) {
    if(review)return false;
    sources=pdfCompiledSources(window->fileName());
    if(!pdfCompiledPairValid(window->fileName())||sources.isEmpty()) {
      notice(QStringLiteral("请先编译一次原稿，再添加批注。"),QStringLiteral("Compile the original document once before annotating."));return false;
    }
    auto *view=window->widget();auto *scene=qobject_cast<QtPDF::PDFDocumentScene*>(view->scene());if(!scene)return false;
    const auto rectangle=QRect(first,last).normalized();
    auto *page=dynamic_cast<QtPDF::PDFPageGraphicsItem*>(scene->pageAt(view->mapToScene(region?rectangle.center():last)));
    if(!page)return false;const auto backend=page->page().toStrongRef();if(!backend)return false;
    const auto size=backend->pageSizeF();
    auto topPoint=[&](const QPoint &pos){const auto p=page->mapToPage(page->mapFromScene(view->mapToScene(pos)));return QPointF(p.x(),size.height()-p.y());};
    const auto start=topPoint(first),end=topPoint(last);
    QRectF bounds(start,end);bounds=bounds.normalized();
    bounds=bounds.intersected(QRectF(QPointF(),size));if(bounds.isEmpty())bounds=QRectF(end,QSizeF(1,1));
    TWSyncTeXSynchronizer sync(window->fileName(),{}, {},QFileInfo(root).absolutePath());
    const auto point=region?bounds.center():start;
    const auto hit=sync.syncFromPDF({window->fileName(),page->pageNum()+1,{QRectF(point,QSizeF())}},TWSynchronizer::LineResolution);
    const auto file=canonical(QDir(QFileInfo(root).absolutePath()).absoluteFilePath(hit.filename));
    if(hit.line<1||!sources.contains(file)) {
      notice(QStringLiteral("此处未找到可核对的源码位置，请靠近正文、公式或图注重新选择。"),QStringLiteral("No verified source position here. Select nearer the text, equation or caption."));return false;
    }
    const auto text=sources.value(file);int offset=lineOffset(text,hit.line);
    while(offset>=0&&offset<text.size()&&(text[offset]==QLatin1Char(' ')||text[offset]==QLatin1Char('\t')))++offset;
    int endOffset=offset;
    if(!region) {
      const auto lastHit=sync.syncFromPDF({window->fileName(),page->pageNum()+1,{QRectF(end,QSizeF())}},TWSynchronizer::LineResolution);
      if(lastHit.line<1||canonical(QDir(QFileInfo(root).absolutePath()).absoluteFilePath(lastHit.filename))!=file) {
        notice(QStringLiteral("所选内容跨越源码文件或排版边界，请按段落或对象分别批注。"),QStringLiteral("This selection crosses source files or layout boundaries. Annotate each paragraph or object separately."));return false;
      }
      endOffset=lineOffset(text,lastHit.line);
      while(endOffset>=0&&endOffset<text.size()&&(text[endOffset]==QLatin1Char(' ')||text[endOffset]==QLatin1Char('\t')))++endOffset;
    }
    pending=makeAnnotation(file,text,offset,region?QString():view->selectedText(),page->pageNum(),bounds,endOffset);
    pending.geometryHash=pdfMetadata(window->fileName()).value(QStringLiteral("pdfHash")).toString();
    pending.selectionOffset=qMax(0,offset-pending.offset);
    pending.selectionRects=annotationLines(region?QVector<QRectF>{bounds}:view->selectedTextRects());
    const auto reference=sync.syncFromTeX({file,hit.line,0,-1},TWSynchronizer::LineResolution);
    if(reference.page==page->pageNum()+1)pending.referenceBox=annotationBounds(reference.rects);
    if(pending.anchor.isEmpty()) {
      notice(QStringLiteral("此处的源码边界尚不明确，请选择完整段落或对象附近。"),QStringLiteral("The source boundary is unclear here. Select near a complete paragraph or object."));return false;
    }
    if(!relocating.id.isEmpty()){pending.id=relocating.id;pending.opinion=relocating.opinion;}
    return true;
  }
  QPoint popupPosition(const QSize &size)const {
    auto *viewport=window->widget()->viewport();const auto area=QRect(viewport->mapToGlobal(QPoint(8,8)),viewport->size()-QSize(16,16));
    auto pos=viewport->mapToGlobal(QPoint(released.x(),qMax(pressed.y(),released.y())+10));
    if(pos.y()+size.height()>area.bottom())pos.setY(viewport->mapToGlobal(QPoint(pressed.x(),qMin(pressed.y(),released.y()))).y()-size.height()-10);
    pos.setX(qBound(area.left(),pos.x(),qMax(area.left(),area.right()-size.width()+1)));
    pos.setY(qBound(area.top(),pos.y(),qMax(area.top(),area.bottom()-size.height()+1)));return pos;
  }
  void showPill() {
    if(review||window->widget()->selectedText().trimmed().isEmpty())return;
    pill->setText(words("批注","Annotation"));configureButton(pill,ButtonRole::Text);
    pill->setProperty("researchControl",false);
    pill->setStyleSheet(pill->styleSheet()+QStringLiteral("QToolButton { background: white; border: 1px solid #d9dfe2; border-radius: 12px; } QToolButton:hover { background: #f1f4f5; }"));
    pill->adjustSize();
    auto pos=QPoint(released.x()+6,qMax(pressed.y(),released.y())+8);auto *viewport=window->widget()->viewport();
    if(pos.y()+pill->height()>viewport->height()-8)pos.setY(qMin(pressed.y(),released.y())-pill->height()-8);
    pos.setX(qBound(8,pos.x(),qMax(8,viewport->width()-pill->width()-8)));
    pos.setY(qBound(8,pos.y(),qMax(8,viewport->height()-pill->height()-8)));
    pill->move(pos);pill->show();pill->raise();
  }
  void showEditor(Annotation note) {
    pill->hide();if(editor)editor->close();
    auto *popup=new PopupFrame(window,Qt::Popup);editor=popup;popup->setObjectName(QStringLiteral("researchAnnotationEditor"));
    popup->setAttribute(Qt::WA_DeleteOnClose);preparePopupSurface(popup);
    popup->setStyleSheet(QStringLiteral("QFrame#researchAnnotationEditor { background: white; border: 1px solid #d8e0e4; border-radius: 10px; } QPlainTextEdit { border: none; background: white; color: #30353a; }"));
    auto *layout=new QVBoxLayout(popup);layout->setContentsMargins(10,10,10,8);layout->setSpacing(4);
    auto *text=new QPlainTextEdit(popup);field=text;text->setObjectName(QStringLiteral("researchAnnotationText"));
    auto font=smoothTextFont(QApplication::font());font.setPixelSize(ControlMetrics::FontPixels);text->setFont(font);
    text->setPlaceholderText(words("写下批注意见…","Write an annotation…"));text->setTabChangesFocus(true);
    text->setToolTip(words("公式可用 $…$、\\(…\\)","Math: $…$ or \\(…\\)"));
    text->setPlainText(!note.opinion.isEmpty()?note.opinion:(draft.file==note.file&&draft.anchor==note.anchor?draft.opinion:QString()));
    layout->addWidget(text,1);
    auto *row=new QHBoxLayout;row->setContentsMargins(0,0,0,0);row->addStretch();
    auto *more=new QToolButton(popup);more->setObjectName(QStringLiteral("researchAnnotationMore"));
    more->setIcon(QIcon(QStringLiteral(":/research/icons/more.svg")));configureButton(more,ButtonRole::Icon);
    more->setToolTip(words("更多批注操作","More annotation actions"));
    auto *menu=new QMenu(more);
    if(!note.id.isEmpty()) {
      auto *relocate=menu->addAction(words("重新定位批注","Relocate annotation"));relocate->setObjectName(QStringLiteral("researchAnnotationRelocate"));
      connect(relocate,&QAction::triggered,this,[this,note,text,popup] {
        relocating=note;relocating.opinion=text->toPlainText();popup->close();regionAction->setChecked(true);
        showTransientStatusMessage(window,words("请在原稿中框选新的批注位置。","Select the new annotation location in the original PDF."));
      });
      menu->addSeparator();auto *erase=menu->addAction(words("删除批注","Delete annotation"));
      erase->setObjectName(QStringLiteral("researchAnnotationDelete"));
      connect(erase,&QAction::triggered,this,[this,note,popup]{
        auto next=notes;next.erase(std::remove_if(next.begin(),next.end(),[&](const Annotation &n){return n.id==note.id;}),next.end());
        const auto previousDraft=draft;draft={};
        if(commitNotes(next)){removed=note;popup->close();invalidate();if(notePositions.isEmpty())showOriginal();updateControls();drawMarks();}
        else draft=previousDraft;
      });
    }
    more->setMenu(menu);more->setPopupMode(QToolButton::InstantPopup);row->addWidget(more);
    more->setVisible(!note.id.isEmpty());
    auto *save=new QToolButton(popup);save->setObjectName(QStringLiteral("researchAnnotationSave"));
    configureButton(save,ButtonRole::Icon);save->setText(QStringLiteral("✓"));
    save->setToolTip(words("保存批注 · Ctrl+Enter","Save annotation · Ctrl+Enter"));
    save->setAccessibleName(words("保存批注","Save annotation"));
    save->setProperty("researchControl",false);
    save->setStyleSheet(save->styleSheet()+QStringLiteral("QToolButton { background: #edf3f6; color: #30363c; font-size: 20px; border: 1px solid #bccbd3; border-radius: 15px; } QToolButton:disabled { background: #d8dcde; border-color: #d8dcde; }"));
    row->addWidget(save);layout->addLayout(row);
    save->setEnabled(!text->toPlainText().trimmed().isEmpty());
    connect(text,&QPlainTextEdit::textChanged,this,[this,text,save,note] {
      save->setEnabled(!text->toPlainText().trimmed().isEmpty());
      if(note.id.isEmpty()){draft=note;draft.opinion=text->toPlainText();draftTimer.start();}
    });
    connect(save,&QToolButton::clicked,this,[this,note,text,popup]() mutable {
      auto updated=note;updated.opinion=text->toPlainText().trimmed();if(updated.opinion.isEmpty())return;
      if(updated.opinion.size()>20000){notice(QStringLiteral("请将长意见拆成多条批注。"),QStringLiteral("Split this long opinion into separate annotations."));return;}
      QString error;annotationTeX(updated.opinion,&error);if(!error.isEmpty()){notice(error,QStringLiteral("Keep only mathematical expressions inside math delimiters."));return;}
      auto next=notes;
      if(updated.id.isEmpty()) {
        updated.id=QStringLiteral("A%1").arg(nextNumber,2,10,QLatin1Char('0'));next.append(updated);
      } else for(auto &n:next)if(n.id==updated.id)n=updated;
      const auto previousDraft=draft;draft={};draftTimer.stop();
      if(commitNotes(next)){focusId=updated.id;if(relocating.id==updated.id)relocating={};popup->close();invalidate();updateControls();drawMarks();}
      else draft=previousDraft;
    });
    auto *submit=new QShortcut(QKeySequence(Qt::CTRL|Qt::Key_Return),popup);
    connect(submit,&QShortcut::activated,save,&QToolButton::click);
    auto *escape=new QShortcut(QKeySequence(Qt::Key_Escape),popup);connect(escape,&QShortcut::activated,popup,&QWidget::close);
    connect(popup,&QObject::destroyed,this,[this]{if(draftTimer.isActive()){draftTimer.stop();persist(notes);}window->widget()->setFocus();});
    popup->resize(qMax(180,qMin(360,window->widget()->viewport()->width()-24)),110);
    popup->move(popupPosition(popup->size()));
    const auto viewport=window->widget()->viewport();
    const auto area=QRect(viewport->mapToGlobal(QPoint(8,8)),viewport->size()-QSize(16,16));
    const bool below=popup->y()>=viewport->mapToGlobal(QPoint(0,qMax(pressed.y(),released.y()))).y();
    const int edge=below?popup->y():popup->y()+popup->height();
    const auto fit=[popup,text,area,below,edge] {
      const QFontMetrics metrics(text->font());int lines=0;
      for(const auto &line:text->toPlainText().split(QLatin1Char('\n')))
        lines+=qMax(1,int(std::ceil(double(metrics.horizontalAdvance(line))/qMax(80,popup->width()-28))));
      const int maximum=qMax(80,qMin(240,below?area.bottom()-edge+1:edge-area.top()));
      const int height=qBound(qMin(110,maximum),lines*metrics.lineSpacing()+54,maximum);
      popup->resize(popup->width(),height);popup->move(popup->x(),below?edge:edge-height);
    };
    connect(text,&QPlainTextEdit::textChanged,popup,fit);fit();
    popup->show();text->setFocus();
  }
  void editExisting(const QString &id) {
    showOriginal();
    for(const auto &note:notes)if(note.id==id) {
      const auto source=sources.value(note.file);const int offset=resolveAnnotation(note,source);
      if(offset>=0)window->syncFromSource(note.file,source.left(offset).count(QLatin1Char('\n'))+1,0,false);
      const auto ticket=viewGeneration;
      // The restored scene can change scrollbars on the next layout turn.
      // Open after that resize, which would otherwise dismiss a fresh popup.
      QTimer::singleShot(0,this,[this,note,ticket]{
        if(ticket!=viewGeneration||review||!window->isVisible())return;
        released=QPoint(window->widget()->viewport()->width()/2,window->widget()->viewport()->height()/2);pressed=released;
        showEditor(note);
      });return;
    }
  }
  void invalidate() {
    ++generation;if(cancellation)cancellation->store(true);busy=false;wantReview=false;exportPending=false;
    window->setProperty("researchAnnotationBusy",false);
    compiledFingerprint.clear();
  }
  void showOriginal() {
    ++viewGeneration;
    wantReview=false;if(!review){updateControls();return;}
    dismiss();reviewState=state();loading=true;
    window->clearSyncHighlight();window->setProperty("researchAnnotationReview",false);
    review=false;
    if(originalDocument&&originalSignature==annotationPdfSignature(window->fileName())) {
      window->widget()->loadDocument(originalDocument);window->_synchronizer=originalSync;
    } else {
      window->widget()->load(window->fileName());window->loadSyncData();
    }
    window->widget()->setWatchForDocumentChangesOnDisk(originalWatch);
    if(auto doc=window->widget()->document().toStrongRef())doc->setPaperColor(Tw::Settings().value(QStringLiteral("pdfPaperColor"),QColor(Qt::white)).value<QColor>());
    restoreState(originalState);loading=false;updateControls();drawMarks();
  }
  void displayReview() {
    ++viewGeneration;
    if(compiled.pdf.isEmpty()||notePositions.isEmpty())return;
    if(!review){originalState=state();originalDocument=window->widget()->document().toStrongRef();originalSync=window->_synchronizer;originalSignature=annotationPdfSignature(window->fileName());originalWatch=window->widget()->watchForDocumentChangesOnDisk();}
    dismiss();clearMarks();loading=true;window->clearSyncHighlight();
    if(!window->widget()->loadDocument(compiled.document)){loading=false;return;}
    review=true;window->setProperty("researchAnnotationReview",true);
    window->widget()->setWatchForDocumentChangesOnDisk(false);
    if(auto doc=window->widget()->document().toStrongRef())doc->setPaperColor(Tw::Settings().value(QStringLiteral("pdfPaperColor"),QColor(Qt::white)).value<QColor>());
    window->_synchronizer=std::make_shared<AnnotationSync>(compiled,window->fileName(),root);
    window->setProperty("researchAnnotationPdf",compiled.pdf);
    if(reviewState.valid)restoreState(reviewState);
    else {
      window->widget()->setZoomLevel(originalState.zoom);
      auto *view=window->widget();view->goToPage(compiled.focusPage);
      if(!compiled.focusBox.isEmpty())for(auto *item:view->scene()->items()) {
        auto *page=qgraphicsitem_cast<QtPDF::PDFPageGraphicsItem*>(item);
        if(!page||page->pageNum()!=compiled.focusPage)continue;
        if(auto backend=page->page().toStrongRef()) {
          const auto p=compiled.focusBox.center();
          const auto target=page->mapToScene(page->mapFromPage(QPointF(p.x(),backend->pageSizeF().height()-p.y())));
          view->ensureVisible(QRectF(target-QPointF(30,45),QSizeF(100,100)),20,20);
        }
        break;
      }
    }
    loading=false;updateControls();drawMarks();
  }
  void showErrorLog() {
    auto *dialog=new MaterialDialog(window);dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(words("批注编译信息","Annotation compiler output"));auto *layout=new QVBoxLayout(dialog);
    auto *text=new QPlainTextEdit(dialog);text->setReadOnly(true);text->setPlainText(errorLog);layout->addWidget(text);
    dialog->resize(qMin(720,window->width()-20),qMin(460,window->height()-40));dialog->show();
  }
  void requestReview(bool exporting=false) {
    sources=pdfCompiledSources(window->fileName());refreshNotePositions();updateControls();
    const auto currentNotes=locatedNotes();
    if(currentNotes.isEmpty())return;
    wantReview=!exporting||wantReview;exportPending|=exporting;
    const auto hash=pdfMetadata(window->fileName()).value(QStringLiteral("pdfHash")).toString();
    if(compiledFingerprint==noteFingerprint(currentNotes)&&compiledPdfHash==hash&&!compiled.pdf.isEmpty()) {
      if(wantReview)displayReview();if(exportPending){exportPending=false;exportPdf();}return;
    }
    if(busy)return;
    AnnotationRequest request;
    request.root=root;request.pdf=window->fileName();request.sources=sources;
    request.auxiliaries=pdfCompiledAux(request.pdf);request.notes=currentNotes;
    request.focusId=focusId;
    request.paperColor=Tw::Settings().value(QStringLiteral("pdfPaperColor"),QColor(Qt::white)).value<QColor>();
    auto *view=window->widget();
    for(auto *item:view->scene()->items())if(auto *page=qgraphicsitem_cast<QtPDF::PDFPageGraphicsItem*>(item)) {
      const auto transform=view->viewportTransform();
      const auto first=transform.map(page->mapToScene(page->mapFromPage(QPointF())));
      const auto last=transform.map(page->mapToScene(page->mapFromPage(QPointF(72,0))));
      request.renderResolution=QLineF(first,last).length()*view->devicePixelRatioF();break;
    }
    request.engine=TWApp::instance()->getNamedEngine(preferredEngine(root,sources.value(root)));
    if(!request.engine.supportsLocalBuild()||!request.engine.isAvailable())request.engine=TWApp::instance()->getDefaultEngine();
    cancellation=std::make_shared<std::atomic_bool>(false);const auto token=cancellation;
    const auto ticket=++generation;const auto fingerprint=noteFingerprint(currentNotes);QPointer<AnnotationController> guard(this);
    busy=true;updateControls();window->setProperty("researchAnnotationBusy",true);
    showTransientStatusMessage(window,words("正在排版批注…","Typesetting annotations…"),5000);
    QThreadPool::globalInstance()->start(QRunnable::create([guard,token,ticket,request,fingerprint,hash] {
      const auto result=compileAnnotations(request,token);
      if(qApp)QMetaObject::invokeMethod(qApp,[guard,token,ticket,result,fingerprint,hash] {
        if(!guard||token->load()||ticket!=guard->generation)return;
        guard->busy=false;guard->window->setProperty("researchAnnotationBusy",false);
        if(guard->window->statusBar()->currentMessage()==words("正在排版批注…","Typesetting annotations…"))guard->window->statusBar()->clearMessage();
        if(!result.error.isEmpty()) {
          guard->errorLog=result.error+QLatin1Char('\n')+result.log;
          guard->notice(result.error,QStringLiteral("Could not typeset annotations. Open More > Annotations > Compiler output for details."));
          guard->wantReview=false;guard->exportPending=false;guard->updateControls();return;
        }
        const bool show=guard->wantReview;
        if(guard->review)guard->showOriginal();
        guard->wantReview=show;
        guard->compiled=result;guard->compiledFingerprint=fingerprint;guard->compiledPdfHash=hash;guard->reviewState={};
        if(guard->wantReview)guard->displayReview();
        if(guard->exportPending){guard->exportPending=false;guard->exportPdf();}
        guard->updateControls();
      },Qt::QueuedConnection);
    }));
  }
  bool safeExport(const QString &target,const QByteArray &bytes) {
    if(target.isEmpty())return false;const auto file=canonical(target);
    if(file==canonical(window->fileName())||sources.contains(file)||recordedDependencies(root).contains(file)) {
      notice(QStringLiteral("请选择独立文件名，保留论文原稿与资源。"),QStringLiteral("Choose a separate name to preserve the manuscript and its resources."));return false;
    }
    QSaveFile output(target);
    if(!output.open(QIODevice::WriteOnly)||output.write(bytes)!=bytes.size()||!output.commit()) {
      notice(QStringLiteral("无法保存批注导出文件。"),QStringLiteral("Could not save the annotation export."));return false;
    }
    return true;
  }
  void exportPdf() {
    if(compiled.pdf.isEmpty())return;
    const auto target=QFileDialog::getSaveFileName(window,words("导出批注稿","Export annotated PDF"),
      QFileInfo(root).absoluteDir().filePath(QFileInfo(root).completeBaseName()+words("-批注稿.pdf","-annotated.pdf")),QStringLiteral("PDF (*.pdf)"));
    QFile input(compiled.pdf);if(input.open(QIODevice::ReadOnly))safeExport(target,input.readAll());
  }
  void exportOpinions() {
    const auto target=QFileDialog::getSaveFileName(window,words("导出批注意见","Export annotations"),
      QFileInfo(root).absoluteDir().filePath(QFileInfo(root).completeBaseName()+words("-批注意见.md","-annotations.md")),QStringLiteral("Markdown (*.md)"));
    QString text=words("# 批注意见\n\n","# Annotations\n\n");
    text+=words("以下意见由作者逐条指定，请按其范围处理，并保留未要求修改的内容。\n\n","The author specified these opinions. Follow their scope and preserve unrelated content.\n\n");
    for(const auto &note:notes) {
      const auto source=sources.value(note.file);const int offset=resolveAnnotation(note,source);
      text+=QStringLiteral("## %1\n\n%2:%3\n\n").arg(note.id,QDir(QFileInfo(root).absolutePath()).relativeFilePath(note.file),offset<0?words("待核对","needs verification"):QString::number(source.left(offset).count(QLatin1Char('\n'))+1));
      text+=words("原文：\n\n","Original:\n\n")+(!note.quote.isEmpty()?note.quote:note.anchor)+QStringLiteral("\n\n");
      text+=words("意见：\n\n","Annotation:\n\n")+note.opinion+QStringLiteral("\n\n");
    }
    safeExport(target,text.toUtf8());
  }
  void populateMenu() {
    reviewMenu->clear();
    reviewMenu->addAction(annotateAction);
    if(!draft.opinion.isEmpty()) {
      auto *resume=reviewMenu->addAction(words("继续未完成批注","Resume annotation"));
      connect(resume,&QAction::triggered,this,[this]{showOriginal();pending=draft;released=QPoint(window->widget()->viewport()->width()/2,80);pressed=released;showEditor(pending);});
    }
    if(window->property("researchFocus").toBool()&&!notePositions.isEmpty()) {
      auto *toggle=reviewMenu->addAction(review?words("返回原稿","Return to original"):words("查看批注","View annotations"));
      connect(toggle,&QAction::triggered,this,[this]{if(review||(busy&&wantReview))showOriginal();else requestReview();});
    }
    if(busy) {
      auto *stop=reviewMenu->addAction(words("取消编译","Cancel typesetting"));
      connect(stop,&QAction::triggered,this,[this]{invalidate();window->setProperty("researchAnnotationBusy",false);updateControls();});
    } else if(!notePositions.isEmpty()) {
      auto *pdf=reviewMenu->addAction(words("导出批注 PDF…","Export annotated PDF…"));pdf->setObjectName(QStringLiteral("researchAnnotationExportPdf"));connect(pdf,&QAction::triggered,this,[this]{requestReview(true);});
      auto *opinions=reviewMenu->addAction(words("导出批注意见…","Export annotations…"));opinions->setObjectName(QStringLiteral("researchAnnotationExportOpinions"));connect(opinions,&QAction::triggered,this,[this]{exportOpinions();});
    }
    if(!errorLog.isEmpty()){auto *log=reviewMenu->addAction(words("编译信息…","Compiler output…"));connect(log,&QAction::triggered,this,[this]{showErrorLog();});}
    if(!notes.isEmpty())reviewMenu->addSeparator();
    for(const auto &note:notes) {
      const auto caption=QStringLiteral("[%1]  %2").arg(note.id,note.opinion.simplified().left(46));
      auto *action=reviewMenu->addAction(notePositions.contains(note.id)?caption:caption+words(" · 待重新定位"," · Relocate"));
      action->setToolTip(note.opinion);connect(action,&QAction::triggered,this,[this,id=note.id]{editExisting(id);});
    }
    if(!noteHistory.isEmpty()){reviewMenu->addSeparator();reviewMenu->addAction(undoNotes);reviewMenu->addAction(redoNotes);historyControls();}

  }
public:
  AnnotationController(PDFDocumentWindow *w,QToolBar *bar,QMenu *more):QObject(w),window(w),reveal(this) {
    setObjectName(QStringLiteral("researchAnnotations"));
    switcher=new QWidget(bar);switcher->setObjectName(QStringLiteral("researchAnnotationSwitch"));switcher->setMaximumWidth(0);
    switcher->setFixedHeight(ControlMetrics::Height);
    // Each animation tick sets both limits. Fixed lets QToolBar reserve that
    // width; Ignored would allocate zero while the children still paint.
    switcher->setSizePolicy(QSizePolicy::Fixed,QSizePolicy::Preferred);switcher->setMinimumWidth(0);
    switchContent=new QWidget(switcher);switchContent->setObjectName(QStringLiteral("researchAnnotationContent"));
    auto *row=new QHBoxLayout(switchContent);row->setSizeConstraint(QLayout::SetNoConstraint);row->setContentsMargins(0,0,0,0);row->setSpacing(2);
    originalButton=new QToolButton(switchContent);originalButton->setObjectName(QStringLiteral("researchAnnotationOriginal"));
    reviewButton=new QToolButton(switchContent);reviewButton->setObjectName(QStringLiteral("researchAnnotationReview"));
    for(auto *button:{originalButton,reviewButton}){configureButton(button,ButtonRole::Text);button->setCheckable(true);row->addWidget(button);}
    reviewMenu=new QMenu(words("批注","Annotations"),window);reviewMenu->setObjectName(QStringLiteral("researchAnnotationMenu"));
    more->addMenu(reviewMenu);
    if(auto *focus=window->findChild<QMenu*>(QStringLiteral("researchFocusMenu")))focus->addMenu(reviewMenu);
    const auto actions=bar->actions();switchAction=bar->addWidget(switcher);
    if(!actions.isEmpty())bar->insertAction(actions[qMin(3,int(actions.size())-1)],switchAction);
    switchAction->setVisible(false);
    reveal.setDuration(150);reveal.setEasingCurve(QEasingCurve::OutCubic);
    connect(&reveal,&QVariantAnimation::valueChanged,this,[this](const QVariant &value){switcher->setFixedWidth(qMax(0,value.toInt()));});
    connect(&reveal,&QAbstractAnimation::finished,this,[this]{if(notePositions.isEmpty())switchAction->setVisible(false);});
    connect(originalButton,&QToolButton::clicked,this,[this]{showOriginal();});
    connect(reviewButton,&QToolButton::clicked,this,[this]{if(compact()&&(review||(busy&&wantReview)))showOriginal();else requestReview();});
    connect(reviewMenu,&QMenu::aboutToShow,this,[this]{populateMenu();});
    pill=new QToolButton(w->widget()->viewport());pill->setObjectName(QStringLiteral("researchAnnotationPill"));
    configureButton(pill,ButtonRole::Text);pill->setFocusPolicy(Qt::NoFocus);
    pill->setStyleSheet(QStringLiteral("QToolButton { background: white; border: 1px solid #d9dfe2; border-radius: 12px; padding: 0 10px; } QToolButton:hover { background: #f1f4f5; }"));
    pill->setToolTip(words("批注所选内容 · Ctrl+Alt+M","Annotate selection · Ctrl+Alt+M"));pill->hide();
    connect(pill,&QToolButton::clicked,this,[this]{if(!window->widget()->selectedText().trimmed().isEmpty()&&capture(pressed,released,false))showEditor(pending);});
    annotateAction=new QAction(words("添加批注","Add annotation"),window);annotateAction->setObjectName(QStringLiteral("researchAnnotateSelection"));
    annotateAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+M")));window->addAction(annotateAction);
    connect(annotateAction,&QAction::triggered,this,[this]{
      if(!review&&!window->widget()->selectedText().trimmed().isEmpty())pill->click();
      else {showOriginal();regionAction->setChecked(true);}
    });
    regionAction=new QAction(window);regionAction->setObjectName(QStringLiteral("researchAnnotateRegion"));regionAction->setCheckable(true);
    connect(regionAction,&QAction::toggled,this,[this](bool checked){if(checked)showOriginal();dismiss();regionSelecting=false;if(band)band->hide();window->widget()->viewport()->setCursor(checked?Qt::CrossCursor:Qt::ArrowCursor);});
    undoNotes=new QAction(words("撤销批注操作","Undo annotation"),window);undoNotes->setObjectName(QStringLiteral("researchAnnotationUndo"));
    redoNotes=new QAction(words("重做批注操作","Redo annotation"),window);redoNotes->setObjectName(QStringLiteral("researchAnnotationRedo"));
    undoNotes->setShortcut(QKeySequence::Undo);redoNotes->setShortcuts(QKeySequence::keyBindings(QKeySequence::Redo));
    for(auto *a:{undoNotes,redoNotes}){a->setShortcutContext(Qt::WidgetWithChildrenShortcut);window->widget()->addAction(a);}
    connect(undoNotes,&QAction::triggered,this,[this]{stepHistory(false);});connect(redoNotes,&QAction::triggered,this,[this]{stepHistory(true);});historyControls();
    draftTimer.setSingleShot(true);draftTimer.setInterval(350);
    connect(&draftTimer,&QTimer::timeout,this,[this]{persist(notes);});
    w->widget()->viewport()->installEventFilter(this);w->widget()->installEventFilter(this);w->installEventFilter(this);
    connect(w,&PDFDocumentWindow::reloaded,this,[this]{if(!loading){hydrate();if(restoreAfterReload&&originalState.valid)restoreState(originalState);restoreAfterReload=false;originalDocument.clear();originalSync.reset();}});
    connect(w->widget(),&QtPDF::PDFDocumentView::textSelectionChanged,this,[this](bool selected){if(!selected)pill->hide();});
    hydrate();
  }
  ~AnnotationController()override{
    if(cancellation)cancellation->store(true);
    if(editor){disconnect(editor,nullptr,this,nullptr);editor->close();}
  }
  void beforeReload() {
    if(loading)return;
    ++viewGeneration;
    invalidate();dismiss();clearMarks();window->setProperty("researchAnnotationBusy",false);
    restoreAfterReload=review;
    if(review){reviewState=state();review=false;window->setProperty("researchAnnotationReview",false);}
  }
  bool eventFilter(QObject *watched,QEvent *event)override {
    if(watched==window&&event->type()==QEvent::Resize)++viewGeneration;
    if(watched==window&&(event->type()==QEvent::Resize||event->type()==QEvent::LanguageChange))updateControls(false);
    if(watched==window&&event->type()==QEvent::Close){
      if(draftTimer.isActive()){draftTimer.stop();persist(notes);}
      if(review)showOriginal();invalidate();
    }
    if(watched==window&&event->type()==QEvent::DynamicPropertyChange) {
      if(static_cast<QDynamicPropertyChangeEvent*>(event)->propertyName()=="researchFocus")dismiss();
    }
    if(watched==window->widget()||watched==window->widget()->viewport())if(event->type()==QEvent::ShortcutOverride||event->type()==QEvent::KeyPress){auto *key=static_cast<QKeyEvent*>(event);if(key->matches(QKeySequence::Undo)||key->matches(QKeySequence::Redo)){if(event->type()==QEvent::ShortcutOverride)event->accept();else stepHistory(key->matches(QKeySequence::Redo));return true;}}
    if(watched!=window->widget()->viewport())return false;
    if(event->type()==QEvent::MouseButtonPress||event->type()==QEvent::Wheel||event->type()==QEvent::KeyPress)++viewGeneration;
    // A user who resumes reading/selecting keeps that view when the worker ends.
    if(busy&&(event->type()==QEvent::MouseButtonPress||event->type()==QEvent::Wheel||event->type()==QEvent::KeyPress))wantReview=false;
    if(event->type()==QEvent::KeyPress) {
      auto *key=static_cast<QKeyEvent*>(event);
      if(key->key()==Qt::Key_Escape){relocating={};regionAction->setChecked(false);dismiss();return false;}
    }
    if(event->type()==QEvent::Wheel||event->type()==QEvent::Resize||event->type()==QEvent::Hide) {dismiss();pressing=false;}
    if(event->type()==QEvent::MouseButtonPress) {
      auto *mouse=static_cast<QMouseEvent*>(event);pill->hide();
      if(mouse->button()!=Qt::LeftButton)return false;
      pressed=released=mouse->position().toPoint();pressing=mouse->modifiers()==Qt::NoModifier;
      if(regionAction->isChecked()&&pressing) {
        regionSelecting=true;if(!band)band=new QRubberBand(QRubberBand::Rectangle,window->widget()->viewport());
        band->setGeometry(QRect(pressed,QSize()));band->show();return true;
      }
    } else if(event->type()==QEvent::MouseMove&&regionSelecting) {
      band->setGeometry(QRect(pressed,static_cast<QMouseEvent*>(event)->position().toPoint()).normalized());return true;
    } else if(event->type()==QEvent::MouseButtonRelease) {
      auto *mouse=static_cast<QMouseEvent*>(event);released=mouse->position().toPoint();
      if(regionSelecting) {
        regionSelecting=false;if(band)band->hide();regionAction->setChecked(false);
        if((released-pressed).manhattanLength()>10&&capture(pressed,released,true))showEditor(pending);
        pressing=false;return true;
      }
      if(pressing&&mouse->button()==Qt::LeftButton)QTimer::singleShot(0,this,[this]{showPill();});
      pressing=false;
    }
    return false;
  }
};
void installAnnotations(PDFDocumentWindow *window,QToolBar *bar,QMenu *more) {
  if(!window->findChild<QObject*>(QStringLiteral("researchAnnotations")))new AnnotationController(window,bar,more);
}
void beforeAnnotationReload(PDFDocumentWindow *window) {
  if(auto *controller=dynamic_cast<AnnotationController*>(window->findChild<QObject*>(QStringLiteral("researchAnnotations"))))controller->beforeReload();
}
QString annotationDisplayedFile(PDFDocumentWindow *window) {
  return window->property("researchAnnotationReview").toBool()?window->property("researchAnnotationPdf").toString():window->fileName();
}
