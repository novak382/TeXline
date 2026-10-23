// On-demand project files; no permanent sidebar or recursive GUI-thread scan.
static void openProjectFiles(TeXDocumentWindow *window) {
  if(window->untitled()){showTransientStatusMessage(window,words("请先保存文稿。","Save the document first."));return;}
  if(auto *existing=window->findChild<QDialog*>(QStringLiteral("researchProjectFiles"))){existing->show();existing->raise();return;}
  const auto root=completionProjectRoot(window),folder=QFileInfo(root).absolutePath();
  auto *dialog=new MaterialDialog(window);configureSettingsSurface(dialog);dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setObjectName(QStringLiteral("researchProjectFiles"));dialog->setWindowTitle(words("项目文件","Project files"));
  auto *layout=new QVBoxLayout(dialog);layout->setContentsMargins(16,16,16,12);layout->setSpacing(8);
  auto *search=new QLineEdit(dialog);search->setObjectName(QStringLiteral("researchProjectFileSearch"));search->setPlaceholderText(words("筛选文件名","Filter filenames"));search->setClearButtonEnabled(true);configureField(search);auto *top=new QHBoxLayout;top->addWidget(search,1);auto *operations=new QToolButton(dialog);operations->setObjectName(QStringLiteral("researchProjectOperations"));operations->setText(words("操作…","Actions…"));configureButton(operations,ButtonRole::Text);top->addWidget(operations);layout->addLayout(top);
  auto *tree=new QTreeView(dialog);tree->setObjectName(QStringLiteral("researchProjectFileTree"));tree->setHeaderHidden(true);tree->setUniformRowHeights(true);tree->setFrameShape(QFrame::NoFrame);tree->setTextElideMode(Qt::ElideMiddle);
  tree->setStyleSheet(QStringLiteral("QTreeView {background:white;border:0;} QTreeView::item {padding:6px 4px;} QTreeView::item:selected {background:#e6e9ec;color:#20252a;}"));
  auto *model=new QFileSystemModel(tree);model->setReadOnly(true);model->setFilter(QDir::AllDirs|QDir::Files|QDir::NoDotAndDotDot|QDir::NoSymLinks);
  const QStringList extensions{QStringLiteral("*.tex"),QStringLiteral("*.bib"),QStringLiteral("*.cls"),QStringLiteral("*.sty"),QStringLiteral("*.bst"),QStringLiteral("*.bbx"),QStringLiteral("*.cbx"),QStringLiteral("*.cfg"),QStringLiteral("*.def"),QStringLiteral("*.ltx"),QStringLiteral("*.txt"),QStringLiteral("*.md"),QStringLiteral("*.pdf"),QStringLiteral("*.png"),QStringLiteral("*.jpg"),QStringLiteral("*.jpeg"),QStringLiteral("*.svg"),QStringLiteral("*.eps"),QStringLiteral("*.csv"),QStringLiteral("*.dat")};
  model->setNameFilters(extensions);model->setNameFilterDisables(false);tree->setModel(model);tree->setRootIndex(model->setRootPath(folder));for(int i=1;i<4;++i)tree->hideColumn(i);tree->header()->setSectionResizeMode(0,QHeaderView::Stretch);layout->addWidget(tree,1);
  auto *path=new QLabel(QFileInfo(root).fileName(),dialog);path->setObjectName(QStringLiteral("researchProjectFilePath"));path->setTextFormat(Qt::PlainText);path->setWordWrap(true);layout->addWidget(path);
  auto *buttons=new QDialogButtonBox(QDialogButtonBox::Open|QDialogButtonBox::Cancel,dialog);layout->addWidget(buttons);auto *open=buttons->button(QDialogButtonBox::Open);open->setEnabled(false);open->setText(words("打开","Open"));buttons->button(QDialogButtonBox::Cancel)->setText(words("取消","Cancel"));for(auto *button:buttons->buttons())configureButton(qobject_cast<QPushButton*>(button));
  QObject::connect(tree->selectionModel(),&QItemSelectionModel::currentChanged,dialog,[=](const QModelIndex &index){const auto file=model->filePath(index);open->setEnabled(QFileInfo(file).isFile());path->setText(QDir(folder).relativeFilePath(file));});
  QObject::connect(search,&QLineEdit::textChanged,dialog,[=](const QString &query){QStringList filters;for(const auto &extension:extensions)filters.append(QStringLiteral("*")+query+(query.endsWith(extension.mid(1),Qt::CaseInsensitive)?QString():extension));model->setNameFilters(query.isEmpty()?extensions:filters);});
  auto activate=[=]{const auto file=model->filePath(tree->currentIndex());if(!QFileInfo(file).isFile())return;rememberReadingPlace(window);rememberRoot(file,root);dialog->accept();const auto suffix=QFileInfo(file).suffix().toLower();if(suffix==QStringLiteral("pdf")){TWApp::instance()->openFile(file);return;}if(QStringList{QStringLiteral("png"),QStringLiteral("jpg"),QStringLiteral("jpeg"),QStringLiteral("svg"),QStringLiteral("eps")}.contains(suffix)){QDesktopServices::openUrl(QUrl::fromLocalFile(file));return;}if(auto *target=TeXDocumentWindow::openDocument(file,true,true))target->editor()->setFocus();};
  QObject::connect(buttons,&QDialogButtonBox::accepted,dialog,activate);QObject::connect(tree,&QTreeView::activated,dialog,[=](const QModelIndex &index){if(!model->isDir(index))activate();});QObject::connect(buttons,&QDialogButtonBox::rejected,dialog,&QDialog::reject);
  QObject::connect(dialog,&QDialog::rejected,window,[window]{QTimer::singleShot(0,window,[window]{window->selectWindow();window->editor()->setFocus();});});
  installProjectFileOperations(window,dialog,tree,model,operations,folder,root);
  dialog->resize(QSize(qMin(650,qMax(360,window->width()-24)),510).boundedTo(window->screen()->availableGeometry().size()-QSize(32,48)));dialog->show();search->setFocus();
}
static void installProjectFiles(TeXDocumentWindow *window) {
  auto *action=new QAction(words("项目文件…","Project files…"),window);action->setObjectName(QStringLiteral("researchProjectFilesAction"));window->addAction(action);
  QObject::connect(action,&QAction::triggered,window,[window]{openProjectFiles(window);});
  QObject::connect(TWApp::instance(),&TWApp::updatedTranslators,window,[action]{action->setText(words("项目文件…","Project files…"));});
}
