#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "ResearchArticle.h"
#include "ResearchReferences.h"
#include "TeXDocumentWindow.h"
#include "CompletingEdit.h"
#include "TWApp.h"
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QScreen>
#include <QTextBlock>
#include <QTextBrowser>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <algorithm>

static bool waitFor(const std::function<bool()> &ready,int ms=15000){QElapsedTimer t;t.start();while(!ready()&&t.elapsed()<ms){QCoreApplication::processEvents();QThread::msleep(2);}return ready();}
static void put(const QString &p,const QByteArray &s){QFile f(p);if(f.open(QIODevice::WriteOnly))f.write(s);}
static QString visible(TeXDocumentWindow *w){QString out;for(auto b=w->textDoc()->begin();b.isValid();b=b.next())if(b.isVisible())out+=b.text()+"\n";return out;}
int main(int argc,char **argv){const QString report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
 QTimer::singleShot(50,&app,[&]{QJsonArray checks,timings;int failures=0;const auto folder=QFileInfo(report).absolutePath();
 auto save=[&]{put(report,QJsonDocument(QJsonObject{{"checks",checks},{"timings",timings},{"failures",failures},{"physicalInput",false}}).toJson());};
 auto check=[&](const QString &name,bool ok){checks.append(QJsonObject{{"name",name},{"passed",ok}});failures+=!ok;save();};
 if(!qEnvironmentVariable("TEXLINE_TEST_DESKTOP").startsWith("TeXlineValidation-")){check("requires an inactive private desktop",false);app.exit(2);return;}
 QString text="\\documentclass{article}\n\\usepackage{amsthm}\n\\newtheorem{claim}{Claim}\n\\begin{document}\n";
 for(int j=0;j<80;++j){text+="\\section{Section "+QString::number(j)+"}\n";for(int k=0;k<60;++k)text+="Probability and statistics paragraph "+QString::number(k)+".\n";if(j==0)text+="\\begin{claim}\\label{thm:main}\nA useful theorem.\n\\end{claim}\n";}
 text+="\\end{document}\n";const auto path=folder+"/response.tex";put(path,text.toUtf8());auto *w=new TeXDocumentWindow(path);const auto area=app.primaryScreen()->availableGeometry();w->resize(qMin(1400,area.width()-40),qMin(850,area.height()-40));w->show();
 auto ready=[&]{return waitFor([&]{return w->property("researchArticleReady").toBool();});};
 check("large article is detected",ready()&&w->property("researchArticleMode").toBool());
 const int at=text.indexOf("paragraph")+9;QTextCursor c(w->textDoc());c.setPosition(at);w->editor()->setTextCursor(c);w->editor()->ensureCursorVisible();QCoreApplication::processEvents();
 auto sample=[&](const QString &mode){QJsonArray values;const auto before=w->textDoc()->editorText();const int position=w->cursorPosition();const QRect region(0,qMax(0,w->editor()->cursorRect().top()-20),qMin(900,w->editor()->viewport()->width()),qMin(240,w->editor()->viewport()->height()));QImage first=w->editor()->viewport()->grab(region).toImage();
  for(int j=0;j<24;++j){QElapsedTimer t;t.start();QKeyEvent event(QEvent::KeyPress,Qt::Key_X,Qt::NoModifier,"x");QApplication::sendEvent(w->editor(),&event);QCoreApplication::processEvents();auto raster=w->editor()->viewport()->grab(region).toImage();values.append(double(t.nsecsElapsed())/1e6);check(mode+": input raster exists "+QString::number(j),!raster.isNull());}
  auto expected=before;expected.insert(position,QString(24,'x'));check(mode+": exact source after continuous typing",w->textDoc()->editorText()==expected);auto raster=w->editor()->viewport()->grab(region).toImage();check(mode+": source glyph raster changes",raster!=first);
  timings.append(QJsonObject{{"mode",mode},{"eventProcessAndVerifiedRasterMs",values},{"lines",w->textDoc()->blockCount()},{"dpr",w->devicePixelRatioF()}});save();w->editor()->undo();check(mode+": one grouped typing undo restores source",w->textDoc()->editorText()==before);ready();};
 sample("full");c=QTextCursor(w->textDoc());c.setPosition(at);w->editor()->setTextCursor(c);auto *section=w->findChild<QToolButton*>("researchArticleSection");if(section)section->click();
 check("local section can be entered",w->editor()->property("researchSourceSlice").toBool());sample("section");
 const auto slice=visible(w);c=w->textCursor();c.insertText("\nNew line\nAnother line\n");QCoreApplication::processEvents();check("inserted paragraphs stay inside the slice",visible(w).contains("Another line")&&!visible(w).contains("Section 1"));w->editor()->undo();ready();check("multiline undo restores visibility",visible(w)==slice&&w->textDoc()->editorText()==text);w->editor()->redo();ready();check("multiline redo retains the section boundary",visible(w).contains("Another line")&&!visible(w).contains("Section 1"));w->editor()->undo();ready();
 Research::pinSourceReference(w,"thm:main");check("source reference resolves",waitFor([&]{auto *dock=w->findChild<QWidget*>("researchPinnedReference");return dock&&dock->property("researchPinnedReady").toBool();}));
 auto *pin=w->findChild<QTextBrowser*>("researchPinnedSource");if(pin){QCoreApplication::processEvents();const auto a=w->editor()->font(),b=pin->font();const auto sourceFont=w->editor()->fontMetrics(),referenceFont=pin->fontMetrics();check("reference has the rendered source font family and reading size",a.family()==b.family()&&qAbs(sourceFont.height()-referenceFont.height())<=1&&qAbs(referenceFont.horizontalAdvance("WWWW")-referenceFont.horizontalAdvance("iiii"))<=1);w->grab().save(folder+"/reference-font.png");}
 else check("source reference is available for font comparison",false);
 check("original disk source is unchanged",[&]{QFile f(path);return f.open(QIODevice::ReadOnly)&&f.readAll()==text.toUtf8();}());w->setModified(false);delete w;app.exit(failures?1:0);
 });QTimer::singleShot(180000,&app,[&]{app.exit(92);});return app.exec();}
