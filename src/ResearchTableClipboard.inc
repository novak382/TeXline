// Included in the authoring namespace. Clipboard cells retain raw TeX internally;
// spreadsheet text is escaped once at the import boundary.
QVector<QStringList> spreadsheetRows(QString text) {
  text.replace(QStringLiteral("\r\n"),QStringLiteral("\n"));text.replace(QLatin1Char('\r'),QLatin1Char('\n'));
  QVector<QStringList> rows;QStringList row;QString cell;bool quoted=false,atStart=true;
  for(int i=0;i<text.size();++i) {
    const auto ch=text[i];
    if(ch==QLatin1Char('"')&&(quoted||atStart)) {
      if(quoted&&i+1<text.size()&&text[i+1]==ch){cell+=ch;++i;}else quoted=!quoted;
      atStart=false;
    } else if(!quoted&&(ch==QLatin1Char('\t')||ch==QLatin1Char('\n'))) {
      row.append(cell);cell.clear();atStart=true;if(row.size()>40)return {QStringList(41,QString())};
      if(ch==QLatin1Char('\n')){rows.append(row);row.clear();if(rows.size()>200)return rows;}
    } else {cell+=ch;atStart=false;}
  }
  if(quoted)return {};
  if(!cell.isEmpty()||!row.isEmpty()||!text.endsWith(QLatin1Char('\n'))){row.append(cell);rows.append(row);}
  return rows;
}
class ClipboardTable final:public QTableWidget {
public:
  using QTableWidget::QTableWidget;
  std::function<const QMimeData*()> readClipboard{[]{return QApplication::clipboard()->mimeData();}};
  std::function<void(QMimeData*)> writeClipboard{[](QMimeData *mime){QApplication::clipboard()->setMimeData(mime);}};
  std::function<void(const QVector<QStringList>&,bool)> importCells;
  std::function<void()> clearCells;
  std::function<QVariantMap()> metadata;
  std::function<void(const QVariantMap&)> restoreMetadata;
  std::function<void()> changed;
  std::function<bool()> trackSelection;
  struct State {QVector<QStringList> rows;int columns;QVariantMap meta;int row{-1},column{-1};QList<QTableWidgetSelectionRange> selection;bool operator==(const State &other)const{return rows==other.rows&&columns==other.columns&&meta==other.meta;}};
  QVector<State> history;int historyPosition{-1};bool restoring{false};
  State state()const{State value;value.columns=columnCount();value.row=currentRow();value.column=currentColumn();value.selection=selectedRanges();if(metadata)value.meta=metadata();for(int r=0;r<rowCount();++r){QStringList row;for(int c=0;c<columnCount();++c)row.append(item(r,c)?item(r,c)->text():QString());value.rows.append(row);}return value;}
  void rememberSelection(){if(restoring||historyPosition<0||trackSelection&&!trackSelection())return;auto &value=history[historyPosition];value.row=currentRow();value.column=currentColumn();value.selection=selectedRanges();}
  void record(){if(restoring)return;const auto value=state();if(historyPosition>=0&&history[historyPosition]==value)return;history.resize(historyPosition+1);history.append(value);if(history.size()>32)history.removeFirst();historyPosition=int(history.size())-1;}
  void undoCells(bool redo=false){const int next=historyPosition+(redo?1:-1);if(next<0||next>=history.size())return;const auto value=history[next];restoring=true;{QSignalBlocker block(this);setRowCount(int(value.rows.size()));setColumnCount(value.columns);for(int r=0;r<rowCount();++r)for(int c=0;c<columnCount();++c)setItem(r,c,new QTableWidgetItem(value.rows[r][c]));if(restoreMetadata)restoreMetadata(value.meta);setCurrentCell(qBound(0,value.row,rowCount()-1),qBound(0,value.column,columnCount()-1));clearSelection();for(const auto &range:value.selection)setRangeSelected(range,true);}historyPosition=next;if(changed)changed();restoring=false;}
  static QString format(){return QStringLiteral("application/x-texline-table-cells");}
  QMimeData *selectionData()const {
    const auto ranges=selectedRanges();if(ranges.isEmpty())return nullptr;
    const auto range=ranges.first();QJsonArray rows;QStringList lines;
    for(int r=range.topRow();r<=range.bottomRow();++r){QJsonArray row;QStringList cells;
      for(int c=range.leftColumn();c<=range.rightColumn();++c){const auto value=item(r,c)?item(r,c)->text():QString();row.append(value);auto escaped=value;escaped.replace(QLatin1Char('"'),QStringLiteral("\"\""));if(escaped.contains(QLatin1Char('\t'))||escaped.contains(QLatin1Char('\n'))||escaped.contains(QLatin1Char('"')))escaped=QLatin1Char('"')+escaped+QLatin1Char('"');cells.append(escaped);}rows.append(row);lines.append(cells.join(QLatin1Char('\t')));}
    auto *mime=new QMimeData;mime->setText(lines.join(QStringLiteral("\r\n")));mime->setData(format(),QJsonDocument(rows).toJson(QJsonDocument::Compact));return mime;
  }
  bool pasteCells(const QMimeData *mime) {
    if(!mime||!importCells)return false;
    QVector<QStringList> rows;const bool raw=mime->hasFormat(format());
    if(raw){const auto payload=mime->data(format());if(payload.size()>4*1024*1024)return false;const auto array=QJsonDocument::fromJson(payload).array();for(const auto &line:array){QStringList row;for(const auto &cell:line.toArray())row.append(cell.toString());rows.append(row);}}
    else if(mime->hasText()){const auto text=mime->text();if(text.size()<=4*1024*1024)rows=spreadsheetRows(text);}
    if(rows.isEmpty()||std::any_of(rows.cbegin(),rows.cend(),[](const QStringList &row){return row.isEmpty();}))return false;importCells(rows,raw);return true;
  }
  void copyCells(bool cut=false){if(auto *data=selectionData()){writeClipboard(data);if(cut&&clearCells)clearCells();}}
protected:
  bool event(QEvent *event)override {
    if(event->type()==QEvent::ShortcutOverride){auto *key=static_cast<QKeyEvent*>(event);if(key->matches(QKeySequence::Undo)||key->matches(QKeySequence::Redo)||key->matches(QKeySequence::Copy)||key->matches(QKeySequence::Cut)||key->matches(QKeySequence::Paste)||key->key()==Qt::Key_Delete){event->accept();return true;}}
    return QTableWidget::event(event);
  }
  void keyPressEvent(QKeyEvent *key)override {
    if(key->matches(QKeySequence::Undo)){undoCells();return;}
    if(key->matches(QKeySequence::Redo)){undoCells(true);return;}
    if(key->matches(QKeySequence::Copy)){copyCells();return;}
    if(key->matches(QKeySequence::Cut)){copyCells(true);return;}
    if(key->matches(QKeySequence::Paste)){pasteCells(readClipboard());return;}
    if(key->key()==Qt::Key_Delete&&key->modifiers()==Qt::NoModifier){if(clearCells)clearCells();return;}
    QTableWidget::keyPressEvent(key);
  }
  void contextMenuEvent(QContextMenuEvent *event)override {
    QMenu menu(this);auto add=[&](const QString &caption,const QKeySequence &key,const std::function<void()> &run){auto *a=menu.addAction(caption,run);a->setShortcut(key);return a;};
    add(words("复制","Copy"),QKeySequence::Copy,[this]{copyCells();})->setEnabled(!selectedRanges().isEmpty());
    add(words("剪切","Cut"),QKeySequence::Cut,[this]{copyCells(true);})->setEnabled(!selectedRanges().isEmpty());
    add(words("粘贴","Paste"),QKeySequence::Paste,[this]{pasteCells(readClipboard());});
    add(words("清空内容","Clear contents"),QKeySequence(Qt::Key_Delete),[this]{if(clearCells)clearCells();});
    menu.exec(event->globalPos());
  }
};
