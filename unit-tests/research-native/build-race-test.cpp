#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "Engine.h"
#include "ResearchBuild.h"
#include "PDFBackend.h"
#include <QDir>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QThread>
#include <QTimer>
#include <QCryptographicHash>
#include <functional>
static bool waitFor(const std::function<bool()> &ready,int ms=60000){QElapsedTimer t;t.start();while(!ready()&&t.elapsed()<ms){QCoreApplication::processEvents();QThread::msleep(2);}return ready();}
static bool put(QString p,QString s){QDir().mkpath(QFileInfo(p).absolutePath());QSaveFile f(p);auto b=s.toUtf8();return f.open(QIODevice::WriteOnly)&&f.write(b)==b.size()&&f.commit();}
// Disposable control files are created once. Atomic replacement races with
// TeX's repeated openin probes on Windows; manuscript updates stay atomic.
static bool signalGate(const QString &path){QFile file(path);return file.open(QIODevice::WriteOnly)&&file.write("go\n")==3;}
static QByteArray hash(QString p){QFile f(p);if(!f.open(QIODevice::ReadOnly))return {};return QCryptographicHash::hash(f.readAll(),QCryptographicHash::Sha256);}
int main(int argc,char **argv){const QString report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
QTimer::singleShot(80,&app,[&]{QJsonArray checks;int failures=0;QJsonObject measures;auto check=[&](QString name,bool pass){checks.append(QJsonObject{{"name",name},{"passed",pass}});failures+=!pass;put(report,QString::fromUtf8(QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"measures",measures}}).toJson()));};
QString folder=QFileInfo(report).absolutePath()+"/build-race 研究",root=folder+"/main.tex",child=folder+"/chapter.tex";
QString document="\\documentclass{article}\n\\newcount\\stress\\newcount\\ready\\newread\\readgate\\newwrite\\checkpoint\n\\def\\waitgate#1{\\ready=0\\stress=0\\loop\\openin\\readgate=#1\\relax\\ifeof\\readgate\\else\\ready=1\\fi\\closein\\readgate\\advance\\stress by1\\ifnum\\stress>5000000\\ready=1\\fi\\ifnum\\ready=0\\repeat}\n\\begin{document}\n\\immediate\\openout\\checkpoint=maturity-ready.txt\\immediate\\write\\checkpoint{ready}\\immediate\\closeout\\checkpoint\n\\waitgate{gate-change.tex}\n\\input{chapter}\n\\immediate\\openout\\checkpoint=maturity-read.txt\\immediate\\write\\checkpoint{read}\\immediate\\closeout\\checkpoint\n\\waitgate{gate-restore.tex}\n\\end{document}\n";
document.replace("gate-change.tex","\\detokenize{"+QFileInfo(report).absolutePath()+"/gate-change.tex}");document.replace("gate-restore.tex","\\detokenize{"+QFileInfo(report).absolutePath()+"/gate-restore.tex}");put(root,document);put(child,"Stable version A.\n");signalGate(QFileInfo(report).absolutePath()+"/gate-change.tex");signalGate(QFileInfo(report).absolutePath()+"/gate-restore.tex");const auto sourceHash=hash(child);
Engine engine("Direct pdfLaTeX","C:/texlive/2023/bin/windows/pdflatex.exe",{"-synctex=1","-interaction=nonstopmode","-file-line-error","$fullname"},true);
auto initial=Research::prepareBuild(root,engine);QProcess *first=engine.run(QFileInfo(root),&app,initial.outputDir);check("real direct engine finishes the stable warm-up build",first&&waitFor([&]{return first->state()==QProcess::NotRunning;})&&first->exitCode()==0);
check("warm-up output has a valid PDF and provenance",Research::finishBuild(initial,true)==1&&Research::pdfPairValid(initial.previewFile));const auto good=hash(initial.previewFile);if(first)delete first;
QFile::remove(QFileInfo(report).absolutePath()+"/gate-change.tex");QFile::remove(QFileInfo(report).absolutePath()+"/gate-restore.tex");QFile::remove(initial.outputDir+"/maturity-ready.txt");QFile::remove(initial.outputDir+"/maturity-read.txt");
auto job=Research::prepareBuild(root,engine);QProcess *process=engine.run(QFileInfo(root),&app,job.outputDir);QByteArray output;bool changed=false,restored=false;QElapsedTimer timing;timing.start();
measures.insert("initialInputs",job.inputs);
bool finished=waitFor([&]{output+=process->readAllStandardOutput();if(!changed&&QFileInfo::exists(job.outputDir+"/maturity-ready.txt")){changed=put(child,"Transient version B.\n");measures.insert("changedAtMs",double(timing.elapsed()));signalGate(QFileInfo(report).absolutePath()+"/gate-change.tex");}if(changed&&!restored&&QFileInfo::exists(job.outputDir+"/maturity-read.txt")){restored=put(child,"Stable version A.\n");measures.insert("restoredAtMs",double(timing.elapsed()));signalGate(QFileInfo(report).absolutePath()+"/gate-restore.tex");}return process->state()==QProcess::NotRunning;});output+=process->readAllStandardOutput();put(QFileInfo(report).absolutePath()+"/build-race-console.txt",QString::fromLocal8Bit(output));
check("both controlled updates occur before the real TeX process finishes",finished&&changed&&restored&&process->exitCode()==0&&hash(child)==sourceHash);
QString text;auto rendered=QtPDF::Backend::Document::newDocument(job.outputDir+"/main.pdf");if(rendered)if(auto p=rendered->page(0).toStrongRef())text=p->selectedText({QPolygonF(QRectF(QPointF(),p->pageSizeF()))});
measures.insert("outputText",text);check("the generated PDF demonstrably contains the intermediate revision",text.contains("Transient version B"));
const int promoted=Research::finishBuild(job,true);measures.insert("promotionResult",promoted);check("a changed-and-restored source must not promote its intermediate PDF",promoted==-1);
QFile::remove(QFileInfo(report).absolutePath()+"/build-race.fls");QFile::copy(job.outputDir+"/main.fls",QFileInfo(report).absolutePath()+"/build-race.fls");
check("rejecting the intermediate build preserves the complete stable preview",hash(job.previewFile)==good&&Research::pdfPairValid(job.previewFile));
check("the last source-matching preview retains its current provenance",Research::pdfRevision(job.previewFile,root)==Research::PdfRevision::Current);
delete process;app.exit(failures?1:0);
});return app.exec();}
