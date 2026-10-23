// Actual native windows with independent illustrative paper assets.
#include "ActualAuthoringRecording.h"
#include <cmath>
using namespace Research;
int main(int argc,char **argv) {
  const auto report=argument(1);argc=1;
  QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
  TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
  QTimer::singleShot(40,&app,[&] {
    QJsonArray checks;int failures=0;const auto folder=QFileInfo(report).absolutePath();
    auto check=[&](const QString &name,bool ok){checks.append(QJsonObject{{"name",name},{"passed",ok}});failures+=!ok;put(report,QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"devicePixelRatio",qApp->primaryScreen()->devicePixelRatio()}}).toJson());};
    check("inactive private desktop",qEnvironmentVariable("TEXLINE_TEST_DESKTOP").startsWith("TeXlineValidation-"));
    Tw::Settings().setValue("openPDFwithTeX",false);Tw::Settings().setValue("researchFocusMode",false);
    for(int variant=0;variant<2;++variant){
      QImage graph(1200,760,QImage::Format_RGB32);graph.fill(Qt::white);QPainter p(&graph);p.setRenderHint(QPainter::Antialiasing);
      QFont font("Segoe UI");font.setPixelSize(34);p.setFont(font);const QRectF plot(145,60,980,570);
      p.setPen(QPen(QColor("#dfe5e8"),2));for(int i=0;i<5;++i){const auto y=plot.bottom()-i*plot.height()/4;p.drawLine(QPointF(plot.left(),y),QPointF(plot.right(),y));}
      p.setPen(QPen(QColor("#8b989f"),2));p.drawLine(plot.bottomLeft(),plot.topLeft());p.drawLine(plot.bottomLeft(),plot.bottomRight());
      p.setPen(QColor("#596771"));for(int i=0;i<5;++i){const auto x=plot.left()+i*plot.width()/4;p.drawText(QRectF(x-60,plot.bottom()+15,120,48),Qt::AlignCenter,QString::number(variant?i*.25:100+i*100));p.drawText(QRectF(38,plot.bottom()-i*plot.height()/4-24,85,48),Qt::AlignRight|Qt::AlignVCenter,QString::number(i*.2,'f',1));}
      p.drawText(QRectF(145,695,980,50),Qt::AlignCenter,variant?"Noise level":"Sample size");p.save();p.translate(38,345);p.rotate(-90);p.drawText(QRectF(-180,-30,360,60),Qt::AlignCenter,"Estimation error");p.restore();
      for(int series=0;series<2;++series){QPainterPath line;for(int i=0;i<=80;++i){const auto x=i/80.;const auto y=variant?.10+.55*x*x+series*.11:.65*std::exp(-2.4*x)+.06+series*.10;const QPointF point(plot.left()+x*plot.width(),plot.bottom()-y/.8*plot.height());if(i==0)line.moveTo(point);else line.lineTo(point);}p.setPen(QPen(QColor(series?"#b4bdc5":"#5a7e91"),6));p.drawPath(line);}p.end();QDir().mkpath(folder+"/paper");graph.save(folder+QString("/paper/panel-%1.png").arg(variant));
    }
    const QString matrix="\\begin{pmatrix}1&\\rho&\\rho^2\\\\\\rho&1&\\rho\\\\\\rho^2&\\rho&1\\end{pmatrix}";
    const QString figure="\\begin{figure}[htbp]\n\\centering\n\\begin{subfigure}{0.48\\linewidth}\\includegraphics[width=\\linewidth]{panel-0.png}\\caption{Sample size}\\end{subfigure}\\hfill\n\\begin{subfigure}{0.48\\linewidth}\\includegraphics[width=\\linewidth]{panel-1.png}\\caption{Noise level}\\end{subfigure}\n\\caption{Finite-sample behavior}\\end{figure}";
    const QString source="\\documentclass{article}\n\\usepackage{amsmath,graphicx,subcaption}\n\\begin{document}\n\\["+matrix+"\\]\n"+figure+"\n\\end{document}";
    const auto path=folder+"/paper/main.tex";put(path,source.toUtf8());auto *window=new TeXDocumentWindow(path);window->resize(1120,760);window->show();settle();
    auto open=[&](const QString &needle,const char *action,const char *object){auto cursor=window->textCursor();cursor.setPosition(source.indexOf(needle));window->editor()->setTextCursor(cursor);window->findChild<QAction*>(action)->trigger();return window->findChild<QDialog*>(object);};
    auto *dialog=open("\\rho","researchFormulaStructure","researchStructureDialog");check("formula actual preview ready",dialog&&waitFor([&]{return dialog->findChild<QLabel*>("researchStructurePreview")->property("researchDraftReady").toBool();},45000));
    if(dialog){dialog->grab().save(folder+"/formula-edit.png");dialog->findChild<QToolButton*>("researchStructureResultView")->click();settle();dialog->grab().save(folder+"/formula-typeset.png");dialog->reject();settle();}
    dialog=open("panel-0","researchPictureGroup","researchPictureGroupDialog");check("group actual preview ready",dialog&&waitFor([&]{return dialog->findChild<QLabel*>("researchPicturesPreview")->property("researchDraftReady").toBool();},45000));
    if(dialog){dialog->grab().save(folder+"/pictures-edit.png");dialog->findChild<QToolButton*>("researchPicturesResultView")->click();settle();dialog->grab().save(folder+"/pictures-typeset.png");dialog->reject();settle();}
    const QString deck="\\documentclass[aspectratio=169]{beamer}\n\\setbeamertemplate{navigation symbols}{}\n\\setbeamercolor{structure}{fg=black!80}\n\\begin{document}\n\\begin{frame}{A concentration argument}\n\\begin{itemize}\n\\item Control the noise term.\n\\item<2-> Separate signal and remainder.\n\\item<3-> State the resulting bound.\n\\end{itemize}\n\\end{frame}\n\\end{document}";
    const auto slides=folder+"/paper/slides.tex";put(slides,deck.toUtf8());auto *lecture=new TeXDocumentWindow(slides);lecture->resize(1120,760);lecture->show();settle();auto cursor=lecture->textCursor();cursor.setPosition(deck.indexOf("Control"));lecture->editor()->setTextCursor(cursor);lecture->findChild<QAction*>("researchSlideReveal")->trigger();dialog=lecture->findChild<QDialog*>("researchRevealDialog");auto *step=dialog?dialog->findChild<QSpinBox*>("researchRevealStep"):nullptr;check("slide has three actual compiled pages",step&&waitFor([&]{return step->maximum()==3&&step->isEnabled();},45000));
    if(dialog){step->setValue(3);settle();dialog->grab().save(folder+"/reveal.png");dialog->reject();settle();}
    check("demonstration does not modify manuscript files",bytes(path)==source.toUtf8()&&bytes(slides)==deck.toUtf8());for(auto *w:{window,lecture}){w->textDoc()->setModified(false);w->close();}settle();app.exit(failures?1:0);
  });
  return app.exec();
}
