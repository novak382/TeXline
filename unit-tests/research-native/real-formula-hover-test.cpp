#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "ResearchReferences.cpp"
#include "Settings.h"
#include <QElapsedTimer>
#include <QThread>
#include <QJsonArray>
#include <windows.h>
static bool waitFor(const std::function<bool()> &f,int ms=20000){QElapsedTimer t;t.start();while(!f()&&t.elapsed()<ms){QCoreApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QThread::msleep(3);}return f();}
static void settle(int ms=250){QElapsedTimer t;t.start();waitFor([&]{return t.elapsed()>=ms;},ms+1000);}
static void put(const QString &p,const QString &s){QDir().mkpath(QFileInfo(p).absolutePath());QSaveFile f(p);if(f.open(QIODevice::WriteOnly)){f.write(s.toUtf8());f.commit();}}
int main(int argc,char **argv){
 const auto report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
 QTimer::singleShot(80,&app,[&]{
  const auto folder=QFileInfo(report).absolutePath(),root=QString::fromLocal8Bit(qgetenv("TEXLINE_AUDIT_PROJECT"))+"/main.tex";QJsonArray checks,diagnostics;int failures=0;
  auto check=[&](QString n,bool ok){checks.append(QJsonObject{{"name",n},{"passed",ok}});failures+=!ok;put(report,QString::fromUtf8(QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"diagnostics",diagnostics}}).toJson()));};
  auto surface=[&](QFrame *card,const QString &name){const auto image=card->grab().toImage();image.save(folder+"/"+name+"-surface.png");check(name+" card has transparent corners and an opaque interior",image.pixelColor(0,0).alpha()<16&&image.pixelColor(image.width()-1,0).alpha()<16&&image.pixelColor(0,image.height()-1).alpha()<16&&image.pixelColor(image.width()/2,image.height()-5).alpha()==255);check(name+" card uses a native transparent window",GetWindowLongPtrW(reinterpret_cast<HWND>(card->winId()),GWL_EXSTYLE)&WS_EX_LAYERED);};
  const QString source=Research::read(root);check("independent real lecture is present",source.contains("From Coin Tosses"));
  Research::setBuildCacheForTests(folder+"/cache");Tw::Settings().setValue("openPDFwithTeX",false);Tw::Settings().setValue("researchFocusMode",false);Tw::Settings().setValue("researchBuildDestinationPolicy","local-cache");app.setEngineList({Engine("XeLaTeX","C:/texlive/2023/bin/windows/xelatex.exe",{"-synctex=1","-interaction=nonstopmode","-file-line-error","$fullname"},true)});
  auto compile=[&](TeXDocumentWindow *window){for(int pass=0;pass<2;++pass){window->typeset();if(!waitFor([&]{return !window->isTypesetting()&&!window->property("researchBuildPreparing").toBool()&&!window->property("researchBuildFinishing").toBool()&&!window->property("researchPublishing").toBool();},150000))return false;settle(500);}return QFileInfo::exists(Research::previewFile(window->fileName()));};
  auto *w=new TeXDocumentWindow(root);w->resize(1440,860);w->show();settle(600);check("real lecture compiles",compile(w));
  auto *view=w->findChild<QtPDF::PDFDocumentView *>("researchSlidePreview");auto *refs=view?dynamic_cast<Research::References *>(view->findChild<QObject *>("researchSlideReferences")):nullptr;check("real lecture preview controller exists",refs);if(!refs){app.exit(1);return;}
  auto request=refs->context();const auto index=Research::referenceIndex(root,request.buffers);const auto pdf=QtPDF::Backend::Document::newDocument(request.pdf);Research::PreviewSync sync(request.pdf,root);check("real lecture source/PDF pair is current",pdf&&Research::validSourcePair(request));
  int total=0,resolved=0;for(const auto &block:index.blocks)if(block.equation){++total;int page=-1;const auto rect=Research::blockRect(sync,block,page);if(!rect.isEmpty())++resolved;diagnostics.append(QJsonObject{{"line",block.firstLine},{"last",block.lastLine},{"page",page},{"rect",QString("%1,%2,%3,%4").arg(rect.x()).arg(rect.y()).arg(rect.width()).arg(rect.height())}});}
  put(folder+"/all-formulas.json",QString::fromUtf8(QJsonDocument(QJsonObject{{"total",total},{"resolved",resolved},{"formulas",diagnostics}}).toJson()));
  const auto deck=Research::parseSlideDeck(root,request.buffers);
  auto locate=[&](const Research::ReferenceBlock &block,int &page){
    for(const auto &frame:deck.frames)if(frame.file==block.file&&frame.start<=block.start&&frame.end>=block.end){
      Research::ReferenceBlock bounds=block;bounds.firstLine=frame.line;bounds.lastLine=frame.endLine;
      const auto pages=sync.fromTeX(bounds,frame.endLine,-1);for(auto it=pages.begin();it!=pages.end();++it)for(const auto &entry:Research::beamerFormulaRects(sync,index,frame,it.key(),pdf))if(entry.first.start==block.start){page=it.key();return entry.second;}
    }page=-1;return QRectF();
  };
  auto titleRequest=request;titleRequest.body=true;titleRequest.page=3;titleRequest.point=QPointF(150,25);check("slide title never becomes a guessed formula preview",!Research::work(titleRequest).visual());
  const QList<int> lines{127,307,582,956,1108,1408};
  for(int line:lines){for(const auto &block:index.blocks)if(block.equation&&block.firstLine==line){
    int page=-1;const auto rect=locate(block,page);auto req=request;req.body=true;req.page=page;req.point=rect.center();const auto result=Research::work(req);const QString name=QString("line-%1").arg(line);auto inverse=sync.fromPDF(page,rect.center());put(folder+"/"+name+"-diagnostic.txt",QString("page=%1 rect=%2,%3,%4,%5 inverse=%6 result=%7\n").arg(page).arg(rect.x()).arg(rect.y()).arg(rect.width()).arg(rect.height()).arg(inverse.line).arg(result.target.block.firstLine));
    check(name+" resolves complete formula",result.visual()&&result.target.block.firstLine==line);check(name+" keeps all TeX display bounds",result.viewBox.contains(rect));check(name+" is vector",!result.vector.isEmpty()&&QSvgRenderer(result.vector).isValid());
    if(result.visual()){Research::VectorPreview widget(result,nullptr);widget.grab().save(folder+"/"+name+"-crop.png");auto p=pdf->page(page).toStrongRef();if(p)p->renderToImage(144,144).save(folder+"/"+name+"-page.png");
      view->goToPage(page,Qt::AlignCenter);settle(350);
      // Locate the same PDF point through the page item's coordinate mapping.
      QPoint point;bool located=false;for(auto *item:view->items())if(auto *pageItem=qgraphicsitem_cast<QtPDF::PDFPageGraphicsItem *>(item)){if(pageItem->pageNum()!=page)continue;point=view->mapFromScene(pageItem->mapToScene(pageItem->mapFromPage(QPointF(req.point.x(),pdf->page(page).toStrongRef()->pageSizeF().height()-req.point.y()))));located=view->viewport()->rect().contains(point);break;}
      if(located){QMouseEvent e(QEvent::MouseMove,QPointF(point),QPointF(view->viewport()->mapToGlobal(point)),Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view->viewport(),&e);check(name+" native body hover",waitFor([&]{return w->findChild<QFrame *>("researchReferenceCard")!=nullptr;}));if(auto *card=w->findChild<QFrame *>("researchReferenceCard")){surface(card,name);const QRect bounds(view->viewport()->mapToGlobal(QPoint()),view->viewport()->size());check(name+" card is bounded and protects the whole formula",bounds.contains(card->geometry())&&!card->geometry().intersects(card->property("researchReferenceProtectedRect").toRect()));const auto pageArea=card->property("researchReferencePageRect").toRect();check(name+" protects a complete display rather than a pointer row",!pageArea.isEmpty()&&card->property("researchReferenceProtectedRect").toRect().height()>rect.height());if(pageArea.top()-bounds.top()>card->height()+12)check(name+" uses blank canvas rather than covering slide text",!card->geometry().intersects(pageArea));check(name+" hover remains vector",card->findChild<QWidget *>("researchReferenceImage")->property("researchVectorPreview").toBool());auto shot=w->grab();QPainter painter(&shot);painter.drawPixmap(card->pos()-w->mapToGlobal(QPoint()),card->grab());painter.end();shot.save(folder+"/"+name+"-hover.png");}refs->cancel();}
    }
  }}
  check("real source is unchanged",Research::read(root)==source&&w->textDoc()->editorText()==source);w->setModified(false);
  const QString supplement=folder+"/matrix/main.tex";
  const QString sample=source.left(source.indexOf("\\begin{document}"))+QString::fromLatin1(R"TEX(\begin{document}
\begin{frame}{Matrix boundary check}
\begin{equation}\label{audit:matrix}
A=\begin{pmatrix}
\frac{a_{11}}{1+x^2} & a_{12} & a_{13} & a_{14}\\
a_{21} & \sqrt{1+a_{22}^2} & a_{23} & a_{24}\\
a_{31} & a_{32} & \sum_{k=1}^n k^{-2} & a_{34}\\
a_{41} & a_{42} & a_{43} & \frac{a_{44}}{1+y^2}
\end{pmatrix}
\end{equation}
\end{frame}
\begin{frame}{Multi-line boundary check}
\begin{equation}\label{audit:multiline}
\begin{aligned}
\E[(X+Y)^2]&=\E[X^2]+2\E[XY]+\E[Y^2]\\
&=\operatorname{Var}(X)+\operatorname{Var}(Y)+2\operatorname{Cov}(X,Y)\\
&\quad +(\E[X]+\E[Y])^2.
\end{aligned}
\end{equation}
\end{frame}
\begin{frame}{Reference check}
Matrix~\eqref{audit:matrix}; expansion~\eqref{audit:multiline}.
\end{frame}
\end{document}
)TEX");
  put(supplement,sample);auto *matrix=new TeXDocumentWindow(supplement);matrix->resize(1440,860);matrix->show();settle(500);check("same-preamble matrix supplement compiles",compile(matrix));
  auto *mv=matrix->findChild<QtPDF::PDFDocumentView *>("researchSlidePreview");auto *mr=mv?dynamic_cast<Research::References *>(mv->findChild<QObject *>("researchSlideReferences")):nullptr;check("matrix preview controller exists",mr);
  if(mr)for(const auto &key:QStringList{"audit:matrix","audit:multiline"}){auto req=mr->context();req.key=key;const auto result=Research::work(req);check(key+" resolves as valid vector",!result.vector.isEmpty()&&QSvgRenderer(result.vector).isValid());if(result.visual()){
    auto doc=QtPDF::Backend::Document::newDocument(req.pdf);auto page=doc->page(result.page).toStrongRef();Research::PreviewSync ms(req.pdf,supplement);const auto numbers=page->search("("+result.target.number+")",{});QRectF expected;if(numbers.size()==1)expected=ms.numberedDisplay(result.page,numbers.first().bbox,page->pageSizeF().width());check(key+" includes all rows and delimiter height",!expected.isEmpty()&&result.viewBox.top()<=expected.top()&&result.viewBox.bottom()>=expected.bottom());
    check(key+" excludes the slide title and footer",result.viewBox.height()<page->pageSizeF().height()*.7&&!page->selectedText({QPolygonF(result.viewBox)}).contains("boundary check"));Research::VectorPreview widget(result,nullptr);widget.grab().save(folder+"/"+key.mid(6)+"-crop.png");page->renderToImage(144,144).save(folder+"/"+key.mid(6)+"-page.png");
    mv->goToPage(result.page,Qt::AlignCenter);settle(350);QPoint point;bool located=false;
    for(auto *item:mv->items())if(auto *pi=qgraphicsitem_cast<QtPDF::PDFPageGraphicsItem *>(item)){if(pi->pageNum()!=result.page)continue;const auto center=result.viewBox.center();point=mv->mapFromScene(pi->mapToScene(pi->mapFromPage(QPointF(center.x(),page->pageSizeF().height()-center.y()))));located=mv->viewport()->rect().contains(point);break;}
    check(key+" matrix point is visible",located);
    if(located){QMouseEvent e(QEvent::MouseMove,QPointF(point),QPointF(mv->viewport()->mapToGlobal(point)),Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(mv->viewport(),&e);check(key+" native hover is visible",waitFor([&]{return matrix->findChild<QFrame *>("researchReferenceCard")!=nullptr;}));if(auto *card=matrix->findChild<QFrame *>("researchReferenceCard")){surface(card,key.mid(6));check(key+" hover is vector and does not overlap the matrix",card->findChild<QWidget *>("researchReferenceImage")&&card->findChild<QWidget *>("researchReferenceImage")->property("researchVectorPreview").toBool()&&!card->geometry().intersects(card->property("researchReferenceProtectedRect").toRect()));auto shot=matrix->grab();QPainter painter(&shot);painter.drawPixmap(card->pos()-matrix->mapToGlobal(QPoint()),card->grab());painter.end();shot.save(folder+"/"+key.mid(6)+"-hover.png");}mr->cancel();}

  }}matrix->setModified(false);check("supplement never changes real source",Research::read(root)==source);app.exit(failures?1:0);
 });QTimer::singleShot(480000,&app,[&]{app.exit(92);});return app.exec();
}

