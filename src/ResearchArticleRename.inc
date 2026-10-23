// Included inside ArticleController. Inline titles are plain text; custom TeX
// headings keep their exact source and open at the title instead.
  void renameSection(QTreeWidgetItem *item,bool sourceOnly=false) {
    if(!item||busy||!window->property("researchArticleReady").toBool()||item->data(0,Qt::UserRole+1).toInt()!=0)return;
    const int index=item->data(0,Qt::UserRole).toInt();if(index<0||index>=document.sections.size())return;
    const auto entry=document.sections[index];auto *target=TeXDocumentWindow::findDocument(entry.file);if(!target)target=TeXDocumentWindow::openDocument(entry.file,false,false);if(!target)return;
    const auto original=target->textDoc()->editorText();if(original!=document.sources.value(entry.file))return;
    const auto match=QRegularExpression(QStringLiteral(R"(^\\(?:part|chapter|section|subsection|subsubsection)\*?)")).match(original.mid(entry.start));if(!match.hasMatch())return;
    int at=entry.start+int(match.capturedEnd());articleGroup(original,at,QLatin1Char('['),QLatin1Char(']'));while(at<original.size()&&original[at].isSpace())++at;
    const int first=at+1;const auto title=articleGroup(original,at);if(at<=first)return;const int last=at-1;
    if(sourceOnly||title.contains(QRegularExpression(QStringLiteral(R"([\\{}%\n])")))){rememberReadingPlace(window);navigate(item);auto cursor=target->textCursor();cursor.setPosition(first);cursor.setPosition(last,QTextCursor::KeepAnchor);target->editor()->setTextCursor(cursor);target->editor()->ensureCursorVisible();target->selectWindow();target->editor()->setFocus();return;}
    if(target->editor()->isReadOnly())return;
    if(auto *previous=tree->findChild<QLineEdit*>(QStringLiteral("researchArticleTitleEditor")))previous->deleteLater();
    auto *field=new QLineEdit(title,tree->viewport());field->setObjectName(QStringLiteral("researchArticleTitleEditor"));configureField(field);field->setGeometry(tree->visualItemRect(item).adjusted(0,0,-2,0));field->setMinimumHeight(qMax(30,field->sizeHint().height()));
    QPointer<TeXDocumentWindow> guard(target);QTextCursor place=target->textCursor();field->installEventFilter(this);
    connect(field,&QLineEdit::returnPressed,field,[this,field,guard,original,first,last,place]{
      if(!guard||guard->editor()->isReadOnly()||guard->textDoc()->editorText()!=original){showTransientStatusMessage(window,articleWords(QStringLiteral("标题来源已更新，请重新修改。"),QStringLiteral("The title source changed. Edit it again.")));return;}
      auto title=field->text().trimmed();if(title.isEmpty())return;QString escaped;
      for(const auto ch:title){if(ch==QLatin1Char('\\')){escaped+=QStringLiteral("\\textbackslash{}");continue;}if(QStringLiteral("\\{}%&#_$").contains(ch))escaped+=QLatin1Char('\\');if(ch==QLatin1Char('~'))escaped+=QStringLiteral("\\textasciitilde{}");else if(ch==QLatin1Char('^'))escaped+=QStringLiteral("\\textasciicircum{}");else escaped+=ch;}
      if(escaped!=original.mid(first,last-first)){auto cursor=guard->textCursor();cursor.beginEditBlock();cursor.setPosition(first);cursor.setPosition(last,QTextCursor::KeepAnchor);cursor.insertText(escaped);cursor.endEditBlock();guard->editor()->setTextCursor(place);}
      field->hide();field->deleteLater();tree->setFocus();
    });field->show();field->setFocus();field->selectAll();
  }
