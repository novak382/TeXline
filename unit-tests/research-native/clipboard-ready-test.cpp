#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "TeXDocumentWindow.h"
#include "CompletingEdit.h"
#include <QAction>
#include <QClipboard>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QSaveFile>
#include <QTimer>
#include <QUrl>
#include <windows.h>

class MimeEditor : public CompletingEdit {
public:
  using CompletingEdit::canInsertFromMimeData;
  using CompletingEdit::insertFromMimeData;
};
int main(int argc,char **argv) {
  // The clipboard belongs to a window station, not to an individual desktop.
  // Refuse to write anything unless the runner created a separate station.
  wchar_t name[256]{}; DWORD needed=0;
  if(!GetUserObjectInformationW(GetProcessWindowStation(),UOI_NAME,name,sizeof(name),&needed))return 90;
  const bool isolated=QString::fromWCharArray(name).startsWith("TeXlineClipboard-");
  const QString report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
  QTimer::singleShot(100,&app,[&] {
    auto *window=new TeXDocumentWindow();auto *editor=window->editor();auto *paste=window->findChild<QAction*>("actionPaste");
    MimeEditor mimeEditor;QJsonArray checks,runs;int failures=0;
    auto check=[&](const QString &label,bool passed){checks.append(QJsonObject{{"name",label},{"passed",passed}});if(!passed)++failures;};
    for(int kind=0;kind<6;++kind) {
      auto *data=new QMimeData;
      if(kind==0)data->setText("plain 中文\nsecond line");
      if(kind==1)data->setData("text/plain;charset=utf-8",QString("UTF8 中文").toUtf8());
      if(kind==2)data->setUrls({QUrl("https://example.invalid/paper"),QUrl::fromLocalFile("C:/论文/main.tex")});
      if(kind==3)data->setHtml("<b>HTML without plain text</b>");
      if(kind==4)data->setData("application/x-texline-test",QByteArray("opaque"));
      const bool expected=data->hasText();const QString text=data->text();
      QElapsedTimer clock;clock.start();const bool invoked=QMetaObject::invokeMethod(window,"clipboardChanged",Qt::DirectConnection);const double ms=double(clock.nsecsElapsed())/1e6;
      mimeEditor.setPlainText(QString());check("text/URL MIME acceptance "+QString::number(kind),mimeEditor.canInsertFromMimeData(data)==expected);
      mimeEditor.insertFromMimeData(data);check("MIME insertion preserves accepted text "+QString::number(kind),mimeEditor.toPlainText()==(expected?text:QString()));
      if(expected){mimeEditor.undo();check("MIME insertion is one undo "+QString::number(kind),mimeEditor.toPlainText().isEmpty());}
      if(isolated) {
        app.clipboard()->setMimeData(data);QCoreApplication::processEvents();QMetaObject::invokeMethod(window,"clipboardChanged",Qt::DirectConnection);
        check("Windows paste action matches Qt text/URL semantics "+QString::number(kind),invoked&&paste->isEnabled()==expected&&editor->canPaste()==expected);
        editor->setPlainText(QString());if(expected){paste->trigger();check("actual Windows clipboard paste preserves text "+QString::number(kind),editor->toPlainText()==text);editor->undo();}
      } else delete data;
      editor->setReadOnly(true);QMetaObject::invokeMethod(window,"clipboardChanged",Qt::DirectConnection);check("read-only disables paste "+QString::number(kind),!paste->isEnabled());editor->setReadOnly(false);
      runs.append(QJsonObject{{"kind",kind},{"metadataCheckMs",ms},{"textExpected",expected}});
    }
    if(isolated) app.clipboard()->clear();
    window->setModified(false);window->close();
    QSaveFile file(report);if(file.open(QIODevice::WriteOnly)){file.write(QJsonDocument(QJsonObject{{"checks",checks},{"runs",runs},{"failures",failures},{"windowStation",QString::fromWCharArray(name)},{"windowsClipboardRoundtripVerified",isolated},{"userClipboardTouched",false}}).toJson());file.commit();}app.exit(failures?1:0);
  });return app.exec();
}
