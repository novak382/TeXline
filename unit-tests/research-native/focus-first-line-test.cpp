#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "TeXDocumentWindow.h"
#include "CompletingEdit.h"
#include "ResearchAppearance.h"
#include "Settings.h"
#include "utils/WindowManager.h"
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMouseEvent>
#include <QAbstractButton>
#include <QPixmap>
#include <QSaveFile>
#include <QThread>
#include <QTimer>
#include <QAbstractNativeEventFilter>
#include <windows.h>

class StationaryPointer final : public QObject, public QAbstractNativeEventFilter {
public:
  bool nativeEventFilter(const QByteArray &type,void *message,qintptr *result) override {
    if (!message || (type!="windows_generic_MSG" && type!="windows_dispatcher_MSG")) return false;
    const auto *msg=static_cast<MSG *>(message);
    if ((msg->message==WM_MOUSEMOVE||msg->message==WM_NCMOUSEMOVE) && QWidget::find(reinterpret_cast<WId>(msg->hwnd))) { if(result)*result=0; return true; }
    return false;
  }
  bool eventFilter(QObject *,QEvent *event) override {
    return (event->type()==QEvent::MouseMove||event->type()==QEvent::NonClientAreaMouseMove) && event->spontaneous();
  }
};

static void settle(int ms) { QElapsedTimer t; t.start(); while(t.elapsed()<ms) { QCoreApplication::processEvents(); QThread::msleep(3); } }
static bool put(const QString &path, const QByteArray &data) { QSaveFile f(path); return f.open(QIODevice::WriteOnly) && f.write(data)==data.size() && f.commit(); }
int main(int argc, char **argv) {
  const QString report=QString::fromLocal8Bit(argv[1]); argc=1;
  TWApp app(argc,argv); app.setQuitOnLastWindowClosed(false);
  StationaryPointer pointer; app.installEventFilter(&pointer); app.installNativeEventFilter(&pointer);
  QTimer::singleShot(100,&app,[&] {
    const QString path=QFileInfo(report).absolutePath()+"/first-line.tex";
    const QByteArray text="\\documentclass{article}\n\\begin{document}\nFirst line remains editable.\n\\end{document}\n";
    put(path,text); Tw::Settings().setValue("openPDFwithTeX",false);
    auto *source=new TeXDocumentWindow(path); source->show(); settle(200);
    QJsonArray checks; int failures=0;
    auto check=[&](const QString &name,bool passed){ checks.append(QJsonObject{{"name",name},{"passed",passed}}); failures+=!passed; put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures}}).toJson()); };
    const HWND initialHandle=reinterpret_cast<HWND>(source->winId());RECT initial{};GetWindowRect(initialHandle,&initial);
    MONITORINFO initialMonitor{};initialMonitor.cbSize=sizeof(initialMonitor);GetMonitorInfoW(MonitorFromWindow(initialHandle,MONITOR_DEFAULTTONEAREST),&initialMonitor);
    check("opening a fresh file fills the left half without an explicit arrange command",initial.left==initialMonitor.rcWork.left&&initial.top==initialMonitor.rcWork.top&&initial.right==initialMonitor.rcWork.left+(initialMonitor.rcWork.right-initialMonitor.rcWork.left)/2&&initial.bottom==initialMonitor.rcWork.bottom);
    auto motion=[&](const QPoint &point){ QMouseEvent event(QEvent::MouseMove,point,source->mapToGlobal(point),Qt::NoButton,Qt::NoButton,Qt::NoModifier); QCoreApplication::sendEvent(source,&event); settle(240); };
    for(const auto &locale:QStringList{"zh_CN","en_US"}) {
      app.applyTranslation(locale);
      for(bool focused:{false,true}) {
        Research::setFocusMode(focused); settle(260);
        for(bool rhs:{false,true}) {
          Tw::Utils::WindowManager::zoomToHalfScreen(source,rhs); settle(100);
          const HWND handle=reinterpret_cast<HWND>(source->winId()); RECT actual{}; GetWindowRect(handle,&actual);
          MONITORINFO m{};m.cbSize=sizeof(m);GetMonitorInfoW(MonitorFromWindow(handle,MONITOR_DEFAULTTONEAREST),&m);
          const LONG middle=m.rcWork.left+(m.rcWork.right-m.rcWork.left)/2;
          check(locale+(focused?" focus":" default")+(rhs?" right":" left")+" fills physical half work area", actual.top==m.rcWork.top && actual.bottom==m.rcWork.bottom && actual.left==(rhs?middle:m.rcWork.left) && actual.right==(rhs?m.rcWork.right:middle));
        }
        if(focused) {
          auto *editor=source->editor(); editor->moveCursor(QTextCursor::Start); settle(50);
          auto *chrome=source->findChild<QWidget *>("researchFocusChrome");
          const QPoint first=editor->viewport()->mapTo(source,editor->cursorRect().topLeft());
          motion(QPoint(220,15));
          put(QFileInfo(report).absolutePath()+"/"+locale+"-geometry.json",QJsonDocument(QJsonObject{{"firstY",first.y()},{"chromeBottom",chrome->geometry().bottom()},{"chromeVisible",chrome->isVisible()},{"viewportTop",editor->viewport()->mapTo(source,QPoint()).y()}}).toJson());
          check(locale+" top margin reveals controls without covering first line",chrome && chrome->isVisible() && first.y()>chrome->geometry().bottom());
          bool writingTargets=true;for(int offset:{15,45,100}){auto *hit=source->childAt(first+QPoint(offset,5));bool text=false;for(auto *p=hit;p;p=p->parentWidget()){if(qobject_cast<QAbstractButton *>(p)){text=false;break;}if(p==editor){text=true;break;}}writingTargets&=text;}
          check(locale+" visible controls leave the first source words as writing targets",writingTargets);
          source->grab().save(QFileInfo(report).absolutePath()+"/"+locale+"-first-line-visible.png");
          motion(first+QPoint(15,5));
          // Check the completed asynchronous fade, rather than assuming a
          // busy Windows event loop delivered its last frame within 240 ms.
          QElapsedTimer hidden;hidden.start();
          while(!chrome->isHidden()&&hidden.elapsed()<1000)settle(10);
          check(locale+" moving over first line hides chrome",chrome->isHidden());
          QTextCursor cursor(editor->document());cursor.movePosition(QTextCursor::EndOfBlock,QTextCursor::KeepAnchor);editor->setTextCursor(cursor);
          check(locale+" first line remains fully selectable",editor->textCursor().selectedText()=="\\documentclass{article}");
          check(locale+" view changes preserve source",editor->toPlainText().toUtf8()==text && !source->isModified());
        }
      }
    }
    Research::setFocusMode(false); settle(260);
    Tw::Utils::WindowManager::zoomToHalfScreen(source,false); settle(100);
    RECT before{};GetWindowRect(reinterpret_cast<HWND>(source->winId()),&before);
    source->close(); settle(80);
    source=new TeXDocumentWindow(path);source->show();settle(260);
    RECT after{};GetWindowRect(reinterpret_cast<HWND>(source->winId()),&after);
    auto rectJson=[](const RECT &r){return QJsonArray{int(r.left),int(r.top),int(r.right),int(r.bottom)};};
    put(QFileInfo(report).absolutePath()+"/reopen-geometry.json",QJsonDocument(QJsonObject{{"before",rectJson(before)},{"after",rectJson(after)}}).toJson());
    check("reopening a saved half-screen window preserves its full outer rectangle",EqualRect(&before,&after));
    source->close();settle(80);app.exit(failures?1:0);
  }); return app.exec();
}
