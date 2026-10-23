// Cross-discipline authoring walkthroughs. Independent fixtures only.
// GPL-2.0-or-later.
#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "ResearchAuthoring.h"
#include "ResearchArticle.h"
#include "ResearchAppearance.h"
#include "ResearchProject.h"
#include "ResearchSlideExport.h"
#include "TeXDocumentWindow.h"
#include "CompletingEdit.h"
#include "TWApp.h"
#include "Settings.h"
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextBlock>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <functional>
#include <algorithm>
#include <memory>

namespace {
bool waitFor(const std::function<bool()> &predicate,int limit=15000) {
  QElapsedTimer timer;timer.start();
  while(!predicate()&&timer.elapsed()<limit){QCoreApplication::processEvents();QThread::msleep(3);}
  return predicate();
}
void settle(int duration=160){QElapsedTimer timer;timer.start();waitFor([&]{return timer.elapsed()>=duration;},duration+1000);}
bool put(const QString &file,const QString &text){QDir().mkpath(QFileInfo(file).absolutePath());QFile output(file);return output.open(QIODevice::WriteOnly)&&output.write(text.toUtf8())==text.toUtf8().size();}
QString paper(const QString &body,const QString &preamble=QString()) {return "\\documentclass{article}\n"+preamble+"\\begin{document}\nOpening paragraph.\n"+body+"\n\\end{document}\n";}
class Walkthrough {
public:
  QString report,folder,workflow;
  QJsonArray checks;
  int failures{0};
  QPointer<TeXDocumentWindow> window;
  explicit Walkthrough(const QString &path):report(path),folder(QFileInfo(path).absolutePath()){}
  void check(const QString &name,bool passed) {
    checks.append(QJsonObject{{"workflow",workflow},{"name",name},{"passed",passed}});failures+=!passed;
    QFile output(report);if(output.open(QIODevice::WriteOnly))output.write(QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"physicalInput",false},{ "clipboardIntegrationSkipped",qEnvironmentVariableIsSet("TEXLINE_SKIP_CLIPBOARD") }}).toJson());
    if(!passed&&window){const auto prefix=folder+"/failure-"+QString::number(checks.size());QWidget *surface=window;for(auto *dialog:window->findChildren<QDialog*>())if(dialog->isVisible()){surface=dialog;break;}surface->grab().save(prefix+".png");if(auto *source=surface->findChild<QPlainTextEdit*>("researchObjectSource"))put(prefix+"-draft.tex",source->toPlainText());}
  }
  void close() {
    if(!window)return;
    for(auto *dialog:window->findChildren<QDialog*>())if(dialog->isVisible())dialog->reject();
    settle(50);window->setModified(false);delete window.data();settle(80);
  }
  void open(const QString &name,const QString &text) {
    close();const QString file=folder+"/"+name+"/main.tex";check("independent manuscript created",put(file,text));
    window=new TeXDocumentWindow(file);window->resize(1100,800);window->show();settle(350);
  }
  QAction *action(const char *name){return window->findChild<QAction*>(QString::fromLatin1(name));}
  void at(const QString &needle){auto cursor=window->textCursor();cursor.setPosition(window->textDoc()->editorText().indexOf(needle));window->editor()->setTextCursor(cursor);}
  QDialog *object(){return window->findChild<QDialog*>("researchObjectDialog");}
  QDialog *edit(const QString &needle){at(needle);action("researchEditObject")->trigger();settle();return object();}
  void apply(QDialog *dialog){dialog->findChild<QDialogButtonBox*>("researchObjectButtons")->button(QDialogButtonBox::Apply)->click();settle();}
  void dismiss(){if(auto *dialog=object())dialog->reject();settle();}
  QString draft(QDialog *dialog){return dialog->findChild<QPlainTextEdit*>("researchObjectSource")->toPlainText();}
  bool choose(QDialog *parent,const QString &file,const char *button="researchFigureChoose") {
    bool chosen=false;QTimer timer,watchdog;timer.setInterval(10);watchdog.setSingleShot(true);
    QObject::connect(&timer,&QTimer::timeout,qApp,[&]{for(auto *widget:QApplication::topLevelWidgets())if(auto *dialog=qobject_cast<QFileDialog*>(widget)){timer.stop();dialog->selectFile(file);QMetaObject::invokeMethod(dialog,"accept",Qt::QueuedConnection);chosen=true;}});
    QObject::connect(&watchdog,&QTimer::timeout,qApp,[&]{for(auto *widget:QApplication::topLevelWidgets())if(auto *dialog=qobject_cast<QFileDialog*>(widget))dialog->reject();});
    timer.start();watchdog.start(5000);parent->findChild<QToolButton*>(QString::fromLatin1(button))->click();timer.stop();watchdog.stop();return chosen;
  }
  QDialog *citations() {action("researchInsertCitation")->trigger();settle();auto *dialog=window->findChild<QDialog*>("researchCitationDialog");if(dialog)waitFor([&]{return dialog->findChild<QListWidget*>("researchCitationList")->count()>0;},5000);return dialog;}
  ~Walkthrough(){close();}
};
}

int main(int argc,char **argv) {
  const QString report=QString::fromLocal8Bit(argv[1]);argc=1;
  QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
  QTimer::singleShot(80,&app,[&]{
    using namespace Research;Walkthrough audit(report);audit.workflow="environment";
    if(!qEnvironmentVariable("TEXLINE_TEST_DESKTOP").startsWith("TeXlineValidation-")){audit.check("private inactive desktop required",false);app.exit(1);return;}
    Tw::Settings().setValue("openPDFwithTeX",false);
    QTimer unexpectedMessages;unexpectedMessages.setInterval(40);
    QObject::connect(&unexpectedMessages,&QTimer::timeout,&app,[&]{for(auto *widget:QApplication::topLevelWidgets())if(auto *message=qobject_cast<QMessageBox*>(widget);message&&message->isVisible()&&!message->property("maturityRecorded").toBool()){message->setProperty("maturityRecorded",true);message->grab().save(audit.folder+"/unexpected-message.png");audit.check("unexpected message: "+message->text(),false);message->reject();}});unexpectedMessages.start();
    const QString table="\\begin{table}[htbp]\n\\centering\n\\begin{tabular}{lr}\nGroup & Response \\\\\nControl & 1.23000 \\\\\nTreatment & 2.4000 \\\\\n\\end{tabular}\n\\caption[Short]{Original caption}\\label{tab:data}\n\\end{table}";
    const QString figure="\\begin{figure}[htbp]\n\\centering\n\\includegraphics[width=0.60\\linewidth]{sample.png}\n\\caption[Short]{Original caption}\\label{fig:data}\n\\end{figure}";

    audit.workflow="experimental-science-table";
    const auto handTuned=QString(table).replace("Group & Response", "  Group   &  Response").replace("Control & 1.23000", "\tControl &1.23000");
    audit.open("biology",paper(handTuned));auto *dialog=audit.edit("begin{tabular}");
    audit.check("table opens as an editable grid",dialog&&dialog->findChild<QTableWidget*>("researchTableGrid")->isEnabled());
    if(dialog){
      dialog->findChild<QLineEdit*>("researchObjectCaption")->setText("Measured response (95\\% CI)");
      const QString source=audit.draft(dialog),originalTabular=handTuned.mid(handTuned.indexOf("\\begin{tabular}"),handTuned.indexOf("\\end{tabular}")+13-handTuned.indexOf("\\begin{tabular}"));
      audit.check("caption-only edit preserves every byte of the cell layout",source.contains(originalTabular));
      audit.check("short caption and label survive a long-caption edit",source.contains("\\caption[Short]{Measured response (95\\% CI)}\\label{tab:data}"));audit.dismiss();
    }
    const QString ruled="\\begin{table}\n\\begin{tabular}{lr}\n\\hline\nGroup & Value \\\\\n\\hline\nA & 1.20 \\\\\n\\hline\nB & 2.40 \\\\\n\\hline\n\\end{tabular}\n\\caption{Grouped observations}\n\\end{table}";
    audit.open("chemistry",paper(ruled));dialog=audit.edit("begin{tabular}");
    if(dialog){auto *grid=dialog->findChild<QTableWidget*>("researchTableGrid");grid->item(1,1)->setText("1.25");audit.check("editing one result preserves all four horizontal separators",audit.draft(dialog).count("\\hline")==4);audit.check("other exact numeric strings survive cell editing",audit.draft(dialog).contains("2.40"));audit.dismiss();}
    const QString tuned="\\begin{tabular}{lr}\n\\toprule[1pt]\nGroup & Value \\\\\n\\midrule[.4pt]\nA & 1.2 \\\\\n\\bottomrule[1pt]\n\\end{tabular}";
    const auto tunedTable=parseSimpleTable(tuned);
    audit.check("optional rule widths never become cell data",!tunedTable.valid||(tunedTable.rows[0][0]=="Group"&&simpleTableSource(tunedTable).contains("\\toprule[1pt]")));

    audit.workflow="finance-spreadsheet-data";
    audit.open("finance",paper(table));dialog=audit.edit("begin{tabular}");
    if(dialog&&!qEnvironmentVariableIsSet("TEXLINE_SKIP_CLIPBOARD")){
      auto saved=std::make_unique<QMimeData>();if(const auto *mime=QApplication::clipboard()->mimeData())for(const auto &format:mime->formats())saved->setData(format,mime->data(format));
      const QString pasted="R&D\t5%\nA_B\t$12";QApplication::clipboard()->setText(pasted);dialog->findChild<QTableWidget*>("researchTableGrid")->setCurrentCell(0,0);dialog->findChild<QToolButton*>("researchTablePaste")->click();
      if(QApplication::clipboard()->text()==pasted)QApplication::clipboard()->setMimeData(saved.release());
      const auto source=audit.draft(dialog);audit.check("plain spreadsheet text escapes reserved TeX characters",source.contains("R\\&D")&&source.contains("5\\%")&&source.contains("A\\_B")&&source.contains("\\$12"));
      audit.check("pasted data remain a valid structured table",parseSimpleTable(source).valid);audit.dismiss();
    }

    audit.workflow="table-controls-and-compact-source";
    audit.open("statistics",paper(QString(table).replace("{lr}","{rl}")));dialog=audit.edit("begin{tabular}");
    if(dialog){
      auto *grid=dialog->findChild<QTableWidget*>("researchTableGrid");auto *alignment=dialog->findChild<QComboBox*>("researchTableAlignment");
      audit.check("initial alignment describes the first editable column",alignment->currentData().toString()=="r");
      while(grid->columnCount()>1)dialog->findChild<QAction*>("researchTableColumn2")->trigger();
      auto *columns=dialog->findChild<QToolButton*>("researchTableColumns");columns->menu()->popup(columns->mapToGlobal(QPoint(0,columns->height())));settle(40);
      audit.check("deleting the final column is visibly disabled",!dialog->findChild<QAction*>("researchTableColumn2")->isEnabled());columns->menu()->hide();
      dialog->resize(640,550);settle();audit.check("table dialog can fit a compact working width",dialog->width()<740);
      dialog->findChild<QToolButton*>("researchObjectSourceTab")->click();settle();
      audit.check("choosing Source in a compact table immediately shows source",dialog->findChild<QPlainTextEdit*>("researchObjectSource")->isVisible());
      dialog->findChild<QToolButton*>("researchObjectLayoutTab")->click();settle();
      audit.check("choosing Table in a compact dialog immediately shows cells",grid->isVisible());audit.dismiss();
    }

    audit.workflow="figure-roundtrip-and-reading-space";
    audit.open("archaeology",paper(QString(figure).replace("\\begin{figure}","\\begin {figure}"),"\\usepackage{graphicx}\n"));
    QImage image(360,150,QImage::Format_RGB32);image.fill(QColor("#abc7d3"));image.save(QFileInfo(audit.window->fileName()).absolutePath()+"/sample.png");
    dialog=audit.edit("includegraphics");if(dialog){dialog->findChild<QPlainTextEdit*>("researchObjectCaption")->setPlainText("Excavation context");audit.check("legal spacing in begin does not create nested figures",audit.draft(dialog).count(QRegularExpression("\\\\begin\\s*\\{figure\\}"))==1);audit.dismiss();}
    audit.open("image-layout",paper(figure,"\\usepackage{graphicx}\n"));image.save(QFileInfo(audit.window->fileName()).absolutePath()+"/sample.png");dialog=audit.edit("includegraphics");
    if(dialog){auto *split=dialog->findChild<QSplitter*>("researchObjectLayoutSplit");split->setSizes({330,650});settle(50);const auto before=split->sizes();const double ratio=double(before[0])/(before[0]+before[1]);dialog->resize(dialog->width()+70,dialog->height());settle();const auto after=split->sizes();audit.check("resizing preserves the writer's chosen preview proportion",qAbs(double(after[0])/(after[0]+after[1])-ratio)<.08);audit.dismiss();}

    audit.workflow="source-remains-authoritative";
    const auto badLabel=paper(QString(figure).replace("fig:data","bad key"),"\\usepackage{graphicx}\n");audit.open("source-editor",badLabel);dialog=audit.edit("includegraphics");
    if(dialog){dialog->findChild<QToolButton*>("researchObjectSourceTab")->click();auto *source=dialog->findChild<QPlainTextEdit*>("researchObjectSource");source->setPlainText(source->toPlainText().replace("bad key","fig:repaired"));audit.apply(dialog);audit.check("repairing a label in source is not blocked by a stale form value",!audit.object()&&audit.window->textDoc()->editorText().contains("fig:repaired"));audit.dismiss();}

    audit.workflow="resource-failure-is-transactional";
    audit.close();const auto project=audit.folder+"/multifile";put(project+"/main.tex",paper("\\input{chapter}"));const QString child="% !TeX root = main.tex\nChild paragraph.\n";put(project+"/chapter.tex",child);image.save(audit.folder+"/external.png");
    audit.window=new TeXDocumentWindow(project+"/chapter.tex");audit.window->show();settle(350);audit.action("researchInsertFigure")->trigger();settle();dialog=audit.object();
    if(dialog){audit.check("child draft chooses an external image",audit.choose(dialog,audit.folder+"/external.png"));audit.apply(dialog);audit.check("missing main-file dependency retains draft and original text",audit.object()&&audit.window->textDoc()->editorText()==child);audit.check("failed application creates no image resource directory",!QFileInfo(project+"/figures").exists());audit.dismiss();}

    audit.workflow="insertion-and-undo";
    const auto simple=paper("Write here.\nContinue later.");audit.open("short-note",simple);audit.at("Continue later.");const int insertion=audit.window->cursorPosition();audit.action("researchInsertTable")->trigger();settle();dialog=audit.object();
    if(dialog){audit.apply(dialog);const auto text=audit.window->textDoc()->editorText();audit.check("new table leaves an unselected caret after its source",!audit.object()&&!audit.window->textCursor().hasSelection()&&audit.window->cursorPosition()>text.indexOf("\\end{table}")+QString("\\end{table}").size());audit.window->editor()->undo();audit.check("new table is one undo with the original editing position",audit.window->textDoc()->editorText()==simple&&audit.window->cursorPosition()==insertion);}
    audit.open("drawing-note",simple);audit.at("Continue later.");const int drawingInsertion=audit.window->cursorPosition();audit.action("researchInsertFigure")->trigger();settle();dialog=audit.object();
    if(dialog){dialog->findChild<QComboBox*>("researchFigureOrigin")->setCurrentIndex(1);audit.apply(dialog);audit.check("drawing inserts its dependency and content in one edit",audit.window->textDoc()->editorText().contains("\\usepackage{tikz}")&&audit.window->textDoc()->editorText().contains("\\begin{tikzpicture}"));audit.window->editor()->undo();audit.check("undoing a drawing does not jump to the preamble",audit.window->textDoc()->editorText()==simple&&audit.window->cursorPosition()==drawingInsertion);}

    audit.workflow="humanities-local-citations";
    const QString bibliography="@book{one,author={Doe, Jane},title={Fictional archive A},year={2025}}\n@book{two,author={Roe, Alex},title={Fictional archive B},year={2026}}\n";
    audit.open("literature",paper("Text.\n\\bibliography{refs}","\\usepackage[\n  round\n]{natbib}\n"));put(QFileInfo(audit.window->fileName()).absolutePath()+"/refs.bib",bibliography);dialog=audit.citations();
    if(dialog){
      audit.check("multiline package options expose the compatible citation style",dialog->findChild<QComboBox*>("researchCitationStyle")->currentData().toString()=="citep");
      auto *list=dialog->findChild<QListWidget*>("researchCitationList");list->setCurrentRow(1);const auto selected=list->currentItem()?list->currentItem()->data(Qt::UserRole).toString():QString();
      dialog->findChild<QLineEdit*>("researchCitationSearch")->setText("Fictional");audit.check("refining search preserves an entry that still matches",list->currentItem()&&list->currentItem()->data(Qt::UserRole).toString()==selected);
      dialog->findChild<QToolButton*>("researchCitationViewSource")->click();auto *metadata=dialog->findChild<QPlainTextEdit*>("researchCitationMetadata");auto cursor=metadata->textCursor();cursor.setPosition(qMin(40,int(metadata->toPlainText().size())));metadata->setTextCursor(cursor);const int reading=cursor.position();
      dialog->findChild<QLineEdit*>("researchCitationLocator")->setText("42--45");audit.check("typing page numbers preserves the entry-source reading position",metadata->textCursor().position()==reading);
      dialog->findChild<QLineEdit*>("researchCitationSearch")->setText("no-such-archive");audit.check("no-result search explains the empty state",!dialog->findChild<QLabel*>("researchCitationNotice")->text().isEmpty());
      dialog->findChild<QLineEdit*>("researchCitationSearch")->clear();audit.check("clearing search restores references and retains the locator",list->count()==2&&dialog->findChild<QLineEdit*>("researchCitationLocator")->text()=="42--45");dialog->reject();settle();
    }

    audit.workflow="standard-bibtex-parentheses";
    audit.open("sociology",paper("Text.\n\\bibliography{refs}"));const auto bibDir=QFileInfo(audit.window->fileName()).absolutePath();
    const QString parenthesized="@book(paren,\n author={Doe, Jane},\n title=\"Fictional (field) notes\",\n year={2026}\n)\n@book{next,author={Roe, Alex},title={Next fixture},year={2025}}\n";put(bibDir+"/refs.bib",parenthesized);
    put(bibDir+"/check.aux","\\relax\n\\citation{paren,next}\n\\bibstyle{plain}\n\\bibdata{refs}\n");QProcess bibtex;bibtex.setProgram(TWApp::instance()->findProgram("bibtex",Engine::binPaths()));bibtex.setWorkingDirectory(bibDir);bibtex.setArguments({"check"});
#ifdef Q_OS_WIN
    bibtex.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args){args->flags|=0x08000000;});
#endif
    bibtex.start();const bool bibliographyValid=bibtex.waitForFinished(15000)&&bibtex.exitCode()==0;audit.check("the independent bibliography is accepted by actual BibTeX",bibliographyValid);
    dialog=audit.citations();if(dialog){auto *list=dialog->findChild<QListWidget*>("researchCitationList");bool found=false;for(int row=0;row<list->count();++row)if(list->item(row)->data(Qt::UserRole).toString()=="paren"){list->setCurrentRow(row);found=true;break;}audit.check("standard parenthesized entries remain discoverable",found);if(found){dialog->findChild<QToolButton*>("researchCitationViewSource")->click();audit.check("entry source includes its real parenthesized record",dialog->findChild<QPlainTextEdit*>("researchCitationMetadata")->toPlainText().contains("@book(paren,"));}dialog->reject();settle();}

    audit.workflow="short-unsaved-note";audit.close();audit.window=new TeXDocumentWindow();audit.window->editor()->setPlainText(simple);audit.window->show();settle();audit.action("researchInsertCitation")->trigger();settle();dialog=audit.window->findChild<QDialog*>("researchCitationDialog");
    if(dialog){settle(300);bool openedFilePicker=false;QTimer dismiss;dismiss.setInterval(10);QObject::connect(&dismiss,&QTimer::timeout,&app,[&]{for(auto *widget:QApplication::topLevelWidgets())if(auto *picker=qobject_cast<QFileDialog*>(widget)){openedFilePicker=true;picker->reject();}});dismiss.start();dialog->findChild<QToolButton*>("researchCitationAttach")->click();dismiss.stop();audit.check("unsaved bibliography linking asks to save before selecting files",!openedFilePicker&&dialog->findChild<QLabel*>("researchCitationNotice")->text().contains(englishUi()?"Save":"保存"));audit.check("unsaved note contents stay intact",audit.window->textDoc()->editorText()==simple);dialog->reject();settle();}

    audit.workflow="citation-boundaries";
    audit.open("large-library-selection",paper("Text.\n\\bibliography{refs}"));QString library;for(int n=0;n<410;++n)library+="@book{volume"+QString::number(n)+",title={Fictional volume "+QString("%1").arg(n,4,10,QLatin1Char('0'))+"},author={Doe, Jane},year={2026}}\n";put(QFileInfo(audit.window->fileName()).absolutePath()+"/refs.bib",library);dialog=audit.citations();
    if(dialog){auto *search=dialog->findChild<QLineEdit*>("researchCitationSearch");auto *list=dialog->findChild<QListWidget*>("researchCitationList");search->setText("0399");audit.check("a citation beyond the first result page can be found",list->count()==1&&list->currentItem()->data(Qt::UserRole).toString()=="volume399");dialog->findChild<QLineEdit*>("researchCitationLocator")->setText("42--45");search->clear();audit.check("broadening a capped search retains the selected distant citation",list->currentItem()&&list->currentItem()->data(Qt::UserRole).toString()=="volume399"&&dialog->findChild<QLabel*>("researchCitationInsertion")->text()=="\\cite[42--45]{volume399}");audit.check("retained distant selection stays within the bounded result list",list->count()==200);dialog->reject();settle();}
    audit.open("long-bibliography",paper("Text.\n\\bibliography{refs}"));const auto longDir=QFileInfo(audit.window->fileName()).absolutePath();
    const QString longAuthors=QString("Doe, Jane and Roe, Alex and Smith, Jordan and Lee, Chris and ").repeated(45);
    put(longDir+"/refs.bib","@article{long,title={A deliberately long interdisciplinary study of "+QString("measurements and archives ").repeated(35)+"},author={"+longAuthors+"},year={2026}}\n");
    dialog=audit.citations();if(dialog){settle();const auto area=dialog->screen()->availableGeometry();auto *buttons=dialog->findChild<QDialogButtonBox*>("researchCitationButtons");audit.check("long metadata keeps dialog and insertion buttons inside the work area",dialog->height()<=area.height()&&buttons->mapTo(dialog,QPoint()).y()+buttons->height()<=dialog->height());auto *longTitle=dialog->findChild<QLabel*>("researchCitationTitle");auto *longAuthor=dialog->findChild<QLabel*>("researchCitationAuthor");audit.check("long title and author list remain fully readable",longTitle->height()>=longTitle->heightForWidth(longTitle->width())&&longAuthor->height()>=longAuthor->heightForWidth(longAuthor->width()));dialog->grab().save(audit.folder+"/long-citation.png");dialog->reject();settle();}
    const auto bufferBib=QFileInfo(longDir+"/refs.bib").canonicalFilePath();const auto bufferRoot=QFileInfo(audit.window->fileName()).canonicalFilePath();
    const auto dirtyIndex=buildProjectIndex(bufferRoot,{{bufferBib,"@book{unsaved,title={Current unsaved bibliography},author={Doe, Jane},year={2026}}"}},true);
    audit.check("citation indexing reads an explicitly supplied unsaved bibliography buffer",std::any_of(dirtyIndex->entries.begin(),dirtyIndex->entries.end(),[](const auto &entry){return entry.kind=="cite"&&entry.key=="unsaved";}));
    audit.open("duplicate-bibliography",paper("Text.\n\\bibliography{refs}"));put(QFileInfo(audit.window->fileName()).absolutePath()+"/refs.bib",bibliography+"@book{one,title={Conflicting fixture},year={2020}}\n");dialog=audit.citations();
    if(dialog){auto *list=dialog->findChild<QListWidget*>("researchCitationList");for(int row=0;row<list->count();++row)if(list->item(row)->data(Qt::UserRole).toString()=="one")list->setCurrentRow(row);audit.check("duplicate citation keys disable ambiguous insertion",!dialog->findChild<QDialogButtonBox*>("researchCitationButtons")->button(QDialogButtonBox::Ok)->isEnabled()&&!dialog->findChild<QLabel*>("researchCitationNotice")->text().isEmpty());dialog->reject();settle();}
    const auto linked=paper("Text.","\\usepackage{biblatex}\n\\addbibresource{first.bib}\n\\addbibresource{second.bib}\n");audit.open("already-linked-bibliography",linked);const auto linkedDir=QFileInfo(audit.window->fileName()).absolutePath();put(linkedDir+"/first.bib",bibliography);put(linkedDir+"/second.bib","@book{three,title={Second bibliography}}\n");dialog=audit.citations();
    if(dialog){dialog->findChild<QToolButton*>("researchCitationMore")->click();audit.check("an existing secondary bibliography can be chosen",audit.choose(dialog,linkedDir+"/second.bib","researchCitationAttach"));audit.check("linking an already declared bibliography is a source no-op",audit.window->textDoc()->editorText()==linked);if(dialog->isVisible())dialog->reject();settle();}

    audit.workflow="live-bibliography-buffer";
    audit.open("live-bib",paper("Text.\n\\bibliography{refs}"));const auto liveBib=QFileInfo(audit.window->fileName()).absolutePath()+"/refs.bib";put(liveBib,bibliography);
    auto *bibWindow=new TeXDocumentWindow(liveBib);bibWindow->editor()->setPlainText("@book{live,title={Currently edited archive},author={Doe, Jane},year={2026}}\n");bibWindow->show();settle();dialog=audit.citations();
    if(dialog){auto *list=dialog->findChild<QListWidget*>("researchCitationList");audit.check("citation picker discovers the currently edited bibliography",list->count()==1&&list->item(0)->data(Qt::UserRole).toString()=="live");dialog->findChild<QLineEdit*>("researchCitationLocator")->setText("42");bibWindow->editor()->insertPlainText("\n% changed after picker opened\n");dialog->findChild<QDialogButtonBox*>("researchCitationButtons")->button(QDialogButtonBox::Ok)->click();settle();audit.check("bibliography buffer changes reload in the same picker and retain the page locator",waitFor([&]{return audit.window->findChild<QDialog*>("researchCitationDialog")==dialog&&dialog->findChild<QDialogButtonBox*>("researchCitationButtons")->button(QDialogButtonBox::Ok)->isEnabled();})&&dialog->findChild<QLineEdit*>("researchCitationLocator")->text()=="42"&&list->count()==1&&list->item(0)->data(Qt::UserRole).toString()=="live"&&!audit.window->textDoc()->editorText().contains("\\cite"));dialog->reject();settle();}
    dialog=audit.citations();if(dialog){const auto before=audit.window->textDoc()->editorText();dialog->findChild<QDialogButtonBox*>("researchCitationButtons")->button(QDialogButtonBox::Ok)->click();settle();audit.check("a fresh citation can use an unchanged unsaved bibliography buffer",!audit.window->findChild<QDialog*>("researchCitationDialog")&&audit.window->textDoc()->editorText().contains("\\cite{live}"));audit.window->editor()->undo();audit.check("live-buffer citation is still one exact undo",audit.window->textDoc()->editorText()==before);}
    audit.check("bibliography browsing never saves the edited bibliography",[&]{QFile original(liveBib);return original.open(QIODevice::ReadOnly)&&!original.readAll().contains("Currently edited archive");}());bibWindow->setModified(false);delete bibWindow;settle();

    audit.workflow="form-source-boundaries";
    const auto scopedFigure=QString(figure).replace("Original caption","A {\\raggedright local} caption");audit.open("caption-scope",paper(scopedFigure,"\\usepackage{graphicx}\n"));image.save(QFileInfo(audit.window->fileName()).absolutePath()+"/sample.png");dialog=audit.edit("includegraphics");
    if(dialog){dialog->findChild<QComboBox*>("researchFigureAlignment")->setCurrentIndex(2);audit.check("figure alignment preserves commands scoped inside its caption",audit.draft(dialog).contains("A {\\raggedright local} caption"));audit.dismiss();}
    audit.open("bare-tabular",paper("\\begin{tabular}{lr}\nA & 1.2300 \\\\\nB & 2.0 \\\\\n\\end{tabular}"));dialog=audit.edit("A & 1.2300");
    if(dialog){const auto before=audit.window->textDoc()->editorText();audit.apply(dialog);audit.check("a bare tabular form no-op preserves every source byte",audit.window->textDoc()->editorText()==before);dialog=audit.edit("A & 1.2300");dialog->findChild<QLineEdit*>("researchObjectCaption")->setText("Observed measurements");audit.check("captioning a bare tabular creates exactly one table float",audit.draft(dialog).count("\\begin{table}")==1&&audit.draft(dialog).count("\\begin{tabular}")==1);audit.dismiss();}
    const auto complex=paper("\\begin{table}\n\\begin{tabular}{p{3cm}r}\n\\multicolumn{2}{c}{Grouped measurements}\\\\\nA & 1.2300 \\\\\n\\end{tabular}\n\\end{table}");audit.open("complex-table",complex);dialog=audit.edit("multicolumn");
    if(dialog){audit.check("custom table source is visible and cell tools are unavailable",dialog->findChild<QPlainTextEdit*>("researchObjectSource")->isVisible()&&!dialog->findChild<QTableWidget*>("researchTableGrid")->isEnabled());audit.apply(dialog);audit.check("applying unchanged custom source preserves the entire manuscript",audit.window->textDoc()->editorText()==complex);}

    audit.workflow="cross-discipline-object-discovery";
    const QString mixed=paper("\\section{Methods}\n\\begin{algorithm}\\begin{algorithmic}Step.\\end{algorithmic}\\caption{Method}\\end{algorithm}\n\\begin{figure}\\begin{tikzpicture}\\draw (0,0)--(1,1);\\end{tikzpicture}\\caption{Drawing}\\end{figure}\n\\begin{eqnarray}x&=&1\\label{eq:legacy}\\end{eqnarray}\n\\begin{tabular}{lr}A&B\\\\ C&D\\\\\\end{tabular}\n\\section{Results}\n\\begin{verbatim}\n\\begin{table}fake\\end{table}\n\\end{verbatim}\n");
    const auto mixedFile=audit.folder+"/mixed.tex";put(mixedFile,mixed);const auto parsed=parseArticleDocument(mixedFile);
    int algorithms=0,figures=0;bool legacy=false,bare=false;for(const auto &object:parsed.objects){algorithms+=object.kind=="algorithm";figures+=object.kind=="figure";legacy|=object.kind=="eqnarray";bare|=object.kind=="tabular";}
    audit.check("article discovery groups nested algorithm and drawing internals",parsed.article&&algorithms==1&&figures==1);
    audit.check("legacy physics equations appear in content-driven lookup",legacy);
    audit.check("a standalone tabular is discoverable as a table",bare&&authoringObjectAt(mixed,mixed.indexOf("A&B")).valid());
    audit.check("literal code does not create a fake table",std::none_of(parsed.objects.begin(),parsed.objects.end(),[](const auto &object){return object.kind=="table";}));
    audit.close();app.exit(audit.failures?1:0);
  });return app.exec();
}
