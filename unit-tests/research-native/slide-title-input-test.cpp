#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "ResearchSlides.cpp"
#include <QJsonArray>
#include <QElapsedTimer>
#include <QThread>
#include <windows.h>
#include <shellapi.h>
static QString argument() { int count=0;auto args=CommandLineToArgvW(GetCommandLineW(),&count);const auto path=count>1?QString::fromWCharArray(args[1]):QString();LocalFree(args);return path; }
static bool waitFor(const std::function<bool()> &f) { QElapsedTimer t;t.start();while(!f()&&t.elapsed()<12000){QCoreApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QThread::msleep(3);}return f(); }
static bool put(const QString &path,const QByteArray &value){QDir().mkpath(QFileInfo(path).absolutePath());QSaveFile f(path);return f.open(QIODevice::WriteOnly)&&f.write(value)==value.size()&&f.commit();}
int main(int argc,char **argv){
 if(!qEnvironmentVariable("TEXLINE_TEST_DESKTOP").startsWith("TeXlineValidation-"))return 2;
 const QString report=argument();argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
 QTimer::singleShot(150,&app,[&]{
  QJsonArray checks;int failures=0;
  auto check=[&](QString name,bool passed){checks.append(QJsonObject{{"name",name},{"passed",passed}});failures+=!passed;put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures}}).toJson());};
  const QString original="\\documentclass{beamer}\n\\begin{document}\n\\begin{frame}{}\nOpening.\n\\end{frame}\n\\begin{frame}{}\nTarget.\n\\end{frame}\n\\begin{frame}{Same}\nClosing.\n\\end{frame}\n\\end{document}\n";
  for(const auto &language:{QString("zh_CN"),QString("en_US")}){
   app.applyTranslation(language);const QString root=QFileInfo(report).absolutePath()+"/"+language+"/main.tex";
   check(language+": private fixture written",put(root,original.toUtf8()));
   auto *w=new TeXDocumentWindow(root);w->resize(1056,620);w->show();QApplication::setActiveWindow(w);w->editor()->setFocus();
   auto ready=[&]{return waitFor([&]{auto *c=Research::controller(w);return c&&c->enabled&&!c->busy&&!c->dirty&&c->deck.sources.value(Research::path(root))==w->textDoc()->editorText();});};
   check(language+": three frames parsed",ready());auto *c=Research::controller(w);if(!c){app.exit(1);return;}c->list->setCurrentRow(1);c->select(1,true);w->editor()->setFocus();
   QTextCursor cursor(w->textDoc());cursor.setPosition(original.indexOf("\\begin{frame}{}",original.indexOf("Opening."))+14);w->editor()->setTextCursor(cursor);
   QString typed;
   for(const auto &ch:QString("ABC")){
    QKeyEvent down(QEvent::KeyPress,int(ch.unicode()),Qt::ShiftModifier,QString(ch)),up(QEvent::KeyRelease,int(ch.unicode()),Qt::ShiftModifier,QString(ch));
    QApplication::sendEvent(w->editor(),&down);QApplication::sendEvent(w->editor(),&up);typed+=ch;
    check(language+": parser retains the edited frame after "+typed,ready()&&c->selected==1&&c->deck.frames[1].title==typed);
    const auto frame=c->deck.frames.value(1);check(language+": caret stays inside the edited title after "+typed,w->cursorPosition()>frame.start&&w->cursorPosition()<frame.end);
   }
   const QString finished=w->textDoc()->editorText();
   check(language+": no typed characters reach another frame",finished==QString(original).replace("\\begin{frame}{}\nTarget.","\\begin{frame}{ABC}\nTarget."));
   int undos=0;while(w->textDoc()->editorText()!=original&&w->textDoc()->isUndoAvailable()&&undos<4){w->editor()->undo();++undos;ready();}
   check(language+": undo restores the complete original source",w->textDoc()->editorText()==original);
   for(int i=0;i<undos;++i)w->editor()->redo();
   check(language+": redo restores exactly the typed source",ready()&&w->textDoc()->editorText()==finished);
   w->grab().save(QFileInfo(report).absolutePath()+"/title-"+language+".png");w->setModified(false);delete w;
  }
  app.exit(failures?1:0);
 });QTimer::singleShot(90000,&app,[&]{app.exit(92);});return app.exec();
}

