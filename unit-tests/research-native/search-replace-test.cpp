#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "TeXDocumentWindow.h"
#include "CompletingEdit.h"
#include "Settings.h"
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTimer>
#include <QTableWidget>

int main(int argc,char **argv) {
 const QString report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
 QTimer::singleShot(80,&app,[&] {
  QJsonArray checks,runs;int failures=0;
  auto save=[&]{QSaveFile f(report);if(f.open(QIODevice::WriteOnly)){f.write(QJsonDocument(QJsonObject{{"checks",checks},{"runs",runs},{"failures",failures}}).toJson());f.commit();}};
  auto check=[&](QString name,bool pass){checks.append(QJsonObject{{"name",name},{"passed",pass}});failures+=!pass;save();};
  Tw::Settings settings;settings.setValue("openPDFwithTeX",false);
  auto *w=new TeXDocumentWindow();auto *e=w->editor();w->resize(850,700);w->show();
  auto findAll=[&](const QString &text,const QString &pattern,int expected) {
   e->setPlainText(text);settings.setValue("searchText",pattern);settings.setValue("searchRegex",true);settings.setValue("searchFlags",0);settings.setValue("searchSelection",false);settings.setValue("searchFindAll",true);settings.setValue("searchAllFiles",false);
   check("find-all probe retains the initial source",e->text()==text);
   check("actual find-all operation returns",QMetaObject::invokeMethod(w,"doFindAgain",Qt::DirectConnection,Q_ARG(bool,true)));
   QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
   auto *table=w->findChild<QTableWidget*>("table");check("find-all lists each original-source match exactly once",table&&table->rowCount()==expected&&e->text()==text);
   runs.append(QJsonObject{{"findAllPattern",pattern},{"expectedRows",expected},{"actualRows",table?table->rowCount():-1}});save();
  };
  if(qEnvironmentVariableIsSet("TEXLINE_TEST_ZERO_FIND")) {
   findAll("a a","(?=a)",2);w->setModified(false);delete w;save();app.exit(failures?1:0);return;
  }
  if(qEnvironmentVariableIsSet("TEXLINE_TEST_RANGE_FIND")) {
   e->setPlainText("abc");auto cursor=e->textCursor();cursor.setPosition(0);cursor.setPosition(2,QTextCursor::KeepAnchor);e->setTextCursor(cursor);
   settings.setValue("searchText","abc");settings.setValue("searchRegex",true);settings.setValue("searchFlags",int(QTextDocument::FindBackward));settings.setValue("searchSelection",true);settings.setValue("searchWrap",false);settings.setValue("searchAllFiles",false);
   check("backward range probe begins with unchanged source",e->text()=="abc");
   check("backward range find returns when the first match crosses its boundary",QMetaObject::invokeMethod(w,"doFindAgain",Qt::DirectConnection)&&e->textCursor().selectionStart()==0&&e->textCursor().selectionEnd()==2);
   w->setModified(false);delete w;save();app.exit(failures?1:0);return;
  }
  auto replace=[&](QString original,QString pattern,QString replacement,QString expected,bool regex,int flags=0,int from=-1,int to=-1){
   e->setPlainText(original);e->document()->clearUndoRedoStacks();auto c=e->textCursor();c.setPosition(from<0?0:from);if(to>=0)c.setPosition(to,QTextCursor::KeepAnchor);e->setTextCursor(c);
   // Populate the shared snapshot before the edit transaction begins.
   check("initial cached editor text equals input",e->text()==original);
   settings.setValue("searchText",pattern);settings.setValue("replaceText",replacement);settings.setValue("searchRegex",regex);settings.setValue("searchFlags",flags);settings.setValue("searchAllFiles",false);settings.setValue("searchSelection",from>=0);settings.setValue("searchWrap",true);
   QElapsedTimer timer;timer.start();bool invoked=QMetaObject::invokeMethod(w,"doReplace",Qt::DirectConnection,Q_ARG(ReplaceDialog::DialogCode,ReplaceDialog::ReplaceAll));
   const double ms=double(timer.nsecsElapsed())/1e6;QString actual=e->text();
   runs.append(QJsonObject{{"pattern",pattern},{"replacement",replacement},{"regex",regex},{"flags",flags},{"chars",original.size()},{"ms",ms},{"actualPrefix",actual.left(150)},{"expectedPrefix",expected.left(150)}});
   check("replace-all invokes the actual editor operation",invoked);check("replace-all produces the exact expected source",actual==expected);
   e->undo();check("one native undo restores all replacements",e->text()==original);e->redo();check("redo restores the exact replacement result",e->text()==expected);
  };
  for(int flags:qEnvironmentVariableIsSet("TEXLINE_TEST_LITERAL_ONLY")?QList<int>{}:QList<int>{0,int(QTextDocument::FindBackward)}) {
   replace("a a a","a","expanded","expanded expanded expanded",true,flags);
   replace("abc abc abc","abc","x","x x x",true,flags);
   replace("a1 a2 a3","a([0-9])","value\\1","value1 value2 value3",true,flags);
   replace("a\nb a\nb","a\\nb","x","x x",true,flags);
   replace("a a a","a","expanded","expanded expanded expanded",false,flags);
   replace("prefix a a suffix","a","expanded","prefix expanded expanded suffix",true,flags,7,10);
  }
  const QString longText=QString("Text bilingual 中文 $a$ alpha beta gamma.\n").repeated(25000);
  replace(longText,"alpha","x",QString(longText).replace("alpha","x"),false);
  if(qEnvironmentVariableIsSet("TEXLINE_TEST_ZERO_LENGTH")) {
   replace("a a","(?=a)","X","Xa Xa",true);
   replace("a a","(?=a)","","a a",true);
   for(QString pattern:{"(?<=prefix )a","a(?= suffix)","^|$","(?=a)|a","(?<=😀)a","(a)(b)(c)(d)(e)(f)(g)(h)(i)(j)(k)(l)"}) {
    QString original="prefix a suffix\n😀a abcdefghijkl";
    QString replacement="X\\12-\\2-\\1-\\0";
    replace(original,pattern,replacement,QString(original).replace(QRegularExpression(pattern),replacement),true);
   }
   replace(longText,"(alpha)","\\1X",QString(longText).replace(QRegularExpression("(alpha)"),"\\1X"),true);
   findAll("a a\n😀a","(?=a)",3);findAll("a a\n😀a","^|$",2);findAll("a a\n😀a","(?=a)|a",6);findAll("a a\n😀a","(?<=😀)a",1);
   settings.setValue("searchFindAll",false);
   e->setPlainText("prefix a suffix");auto cursor=e->textCursor();cursor.setPosition(0);e->setTextCursor(cursor);
   settings.setValue("searchText","(?<=prefix )a(?= suffix)");settings.setValue("replaceText","X");settings.setValue("searchRegex",true);settings.setValue("searchSelection",false);
   QMetaObject::invokeMethod(w,"doReplace",Qt::DirectConnection,Q_ARG(ReplaceDialog::DialogCode,ReplaceDialog::ReplaceOne));
   check("replace-one preserves lookaround context",e->text()=="prefix X suffix");e->undo();check("replace-one native undo restores context",e->text()=="prefix a suffix");
   auto *other=new TeXDocumentWindow();other->editor()->setPlainText("a2 a3");e->setPlainText("a1 a2");
   settings.setValue("searchText","a([0-9])");settings.setValue("replaceText","X\\1");settings.setValue("searchAllFiles",true);
   QMetaObject::invokeMethod(w,"doReplace",Qt::DirectConnection,Q_ARG(ReplaceDialog::DialogCode,ReplaceDialog::ReplaceAll));
   check("replace-all spans both independent open source windows",e->text()=="X1 X2"&&other->editor()->text()=="X2 X3");
   e->undo();other->editor()->undo();check("each source retains its complete native undo",e->text()=="a1 a2"&&other->editor()->text()=="a2 a3");other->setModified(false);delete other;
  }
  w->setModified(false);delete w;save();app.exit(failures?1:0);
 });return app.exec();
}
