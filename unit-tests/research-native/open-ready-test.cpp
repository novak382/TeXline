#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "TeXDocumentWindow.h"
#include "CompletingEdit.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QTextBlock>
#include <QTimer>
#include <windows.h>

static double ticks() { LARGE_INTEGER value; QueryPerformanceCounter(&value); return double(value.QuadPart); }
static double frequency() { LARGE_INTEGER value; QueryPerformanceFrequency(&value); return double(value.QuadPart); }
static QJsonObject read(const QString &path) { QFile f(path); if (!f.open(QIODevice::ReadOnly)) return {}; return QJsonDocument::fromJson(f.readAll()).object(); }
static void write(const QString &path, const QJsonObject &data) { QSaveFile f(path); if (f.open(QIODevice::WriteOnly)) { f.write(QJsonDocument(data).toJson()); f.commit(); } }

// Observe completed Qt paints and the actual changed raster, rather than
// treating show(), LoadFile hooks or a buffer change as an editable frame.
class ReadyProbe : public QObject {
public:
  QString root; QJsonArray paths, rows, checks; int index=0, stage=0, failures=0;
  double requestTicks=0, keyTicks=0, paintTicks=0; QString original; QImage before; QPointer<TeXDocumentWindow> window;
  explicit ReadyProbe(QString directory):root(directory) {
    const auto config=read(root+"/request.json"); paths=config["paths"].toArray(); requestTicks=config["requestTicks"].toDouble();
  }
  void save() { write(root+"/report.json",{{"runs",rows},{"checks",checks},{"failures",failures},{"physicalInputVerified",false},{"clockFrequency",frequency()}}); }
  void check(const QString &name,bool ok) { checks.append(QJsonObject{{"name",name},{"passed",ok}}); if(!ok)++failures; save(); }
  bool eventFilter(QObject *object,QEvent *event) override {
    if (event->type()!=QEvent::Paint || stage==1 || stage==3 || stage==4 || index>=paths.size()) return false;
    auto *viewport=qobject_cast<QWidget*>(object); if(!viewport)return false;
    auto *editor=qobject_cast<CompletingEdit*>(viewport->parentWidget()); if(!editor || editor->viewport()!=viewport)return false;
    auto *target=qobject_cast<TeXDocumentWindow*>(editor->window());
    if(!target || target->textDoc()->absoluteFilePath()!=paths[index].toString())return false;
    if(stage==0) {
      stage=1; window=target; paintTicks=ticks();
      QTimer::singleShot(0,this,[this,editor] {
        if(!window)return;
        original=editor->toPlainText(); const auto cursor=editor->cursorForPosition(QPoint(10,10));
        const bool firstVisible=cursor.blockNumber()<3 && !original.isEmpty() && editor->isVisible();
        const double completed=ticks();
        before=editor->viewport()->grab(QRect(0,0,qMin(700,editor->viewport()->width()),120)).toImage().convertToFormat(QImage::Format_RGB32);
        int dark=0; for(int y=0;y<qMin(120,before.height());++y)for(int x=0;x<qMin(700,before.width());++x)if(qGray(before.pixel(x,y))<180)++dark;
        check("first screen contains real source glyphs "+QString::number(index),firstVisible&&dark>100);
        QJsonObject row{{"file",paths[index]},{"kind",index==0?"cold-process":"running-launcher"},{"requestToFirstPaintMs",(paintTicks-requestTicks)*1000/frequency()},{"requestToPaintCompletedMs",(completed-requestTicks)*1000/frequency()},{"requestToVerifiedRasterMs",(ticks()-requestTicks)*1000/frequency()},{"chars",original.size()},{"blocks",editor->document()->blockCount()},{"font",editor->font().toString()},{"dpr",editor->devicePixelRatioF()},{"firstRasterDarkPixels",dark}};
        rows.append(row);
        // Send immediately after the first completed frame. No settling delay
        // or forced whole-document layout hides an early editing stall.
        QTextCursor at(editor->document()); at.setPosition(0); editor->setTextCursor(at);
        stage=2; keyTicks=ticks(); QKeyEvent key(QEvent::KeyPress,Qt::Key_Z,Qt::NoModifier,"Z"); QApplication::sendEvent(editor,&key);
        editor->viewport()->update();
      });
    } else if(stage==2) {
      stage=3; const double displayed=ticks();
      QTimer::singleShot(0,this,[this,editor,displayed] {
        if(!window)return;
        const double completed=ticks();
        QImage after=editor->viewport()->grab(QRect(0,0,qMin(700,editor->viewport()->width()),120)).toImage().convertToFormat(QImage::Format_RGB32);
        int changed=0; if(after.size()==before.size())for(int y=0;y<qMin(80,after.height());++y)for(int x=0;x<qMin(600,after.width());++x)if(after.pixel(x,y)!=before.pixel(x,y))++changed;
        const bool textOK=editor->toPlainText()=="Z"+original;
        check("first input changes source and painted glyphs "+QString::number(index),textOK&&changed>25);
        auto row=rows.last().toObject(); row["inputToPaintMs"]=(displayed-keyTicks)*1000/frequency(); row["inputToPaintCompletedMs"]=(completed-keyTicks)*1000/frequency(); row["requestToInputPaintMs"]=(displayed-requestTicks)*1000/frequency(); row["requestToInputPaintCompletedMs"]=(completed-requestTicks)*1000/frequency(); row["inputToVerifiedRasterMs"]=(ticks()-keyTicks)*1000/frequency(); row["inputRasterChangedPixels"]=changed; rows[rows.size()-1]=row; before.save(root+"/screen-"+QString::number(index)+"-before.png"); after.save(root+"/screen-"+QString::number(index)+"-input.png"); save();
        editor->undo(); check("first input is one undo and restores independent source "+QString::number(index),editor->toPlainText()==original&&!window->isModified());
        ++index; stage=4;
        QTimer::singleShot(350,this,[this] {
          if(index>=paths.size()) { for(auto *doc:TeXDocumentWindow::documentList()){doc->setModified(false);doc->close();} TWApp::instance()->exit(failures?1:0);return; }
          stage=0; requestTicks=ticks(); auto *sender=new QProcess(this); auto environment=QProcessEnvironment::systemEnvironment();environment.insert("TEXLINE_OPEN_READY_SENDER","1"); sender->setProcessEnvironment(environment);
          QObject::connect(sender,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this,sender](int code,QProcess::ExitStatus status){check("secondary launcher exits successfully",code==0&&status==QProcess::NormalExit);sender->deleteLater();});
          sender->start(QCoreApplication::applicationFilePath(),{paths[index].toString()});
        });
      });
    }
    return false;
  }
};
int main(int argc,char **argv) {
  const bool sender=qEnvironmentVariableIsSet("TEXLINE_OPEN_READY_SENDER");
  const QString root=qEnvironmentVariable("TEXLINE_OPEN_READY_ROOT"); const double entered=ticks();
  TWApp app(argc,argv);
  if(sender)return app.exec();
  ReadyProbe probe(root); app.installEventFilter(&probe);
  QFile::copy(qEnvironmentVariable("TEXLINE_STARTUP_PROFILE"),root+"/receiver-startup.json");
  write(root+"/constructor.json",{{"mainEntryTicks",entered},{"applicationConstructedTicks",ticks()},{"frequency",frequency()}});
  QTimer::singleShot(60000,&app,[&]{probe.check("owned open/paint experiment completes within timeout",false);app.exit(2);});
  return app.exec();
}
