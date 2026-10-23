// Native project assistance. No persistent database or external runtime.
#include "ResearchProject.h"
#include "ResearchWritingContext.h"
#include "ResearchTextAnchor.h"
#include "ResearchVersionControl.h"
#include "ResearchProjectOperations.h"
#include "ResearchArticle.h"
#include "ResearchSlides.h"
#include "ResearchReferences.h"
#include "ResearchCommandCompletion.h"
#include "ResearchAppearance.h"
#include "ResearchSettings.h"
#include "ResearchWorkflow.h"
#include "ResearchBuild.h"
#include "ResearchDependencies.h"
#include "TeXDocumentWindow.h"
#include "PDFDocumentWindow.h"
#include "PDFDocumentView.h"
#include "CompletingEdit.h"
#include "BibTeXFile.h"
#include "TWApp.h"
#include "Settings.h"
#include <QFileSystemWatcher>
#include <QDesktopServices>
#include <QUrl>
#include <QFileSystemModel>
#include <QTreeView>
#include <QToolButton>
#include <QScreen>
#include <QHeaderView>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QAbstractListModel>
#include <QApplication>
#include <QDateTime>
#include <QDynamicPropertyChangeEvent>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QInputMethodEvent>
#include <QLabel>
#include <QDialog>
#include <QTextBrowser>
#include <QScrollArea>
#include <QLineEdit>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QListView>
#include <QMenu>
#include <QPointer>
#include <QRegularExpression>
#include <QRunnable>
#include <QScrollBar>
#include <QScopedValueRollback>
#include <QLineF>
#include <QScreen>
#include <QSet>
#include <QTextBlock>
#include <QThreadPool>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>
#include <atomic>
#include <memory>

namespace Research {
namespace {
quint64 projectEpoch = 0;
QString canonicalProject(const QString &p) { QFileInfo f(p); return f.exists() ? f.canonicalFilePath() : f.absoluteFilePath(); }
QString readProject(const QString &p, QByteArray *hash = nullptr) { QFile f(p); if (!f.open(QIODevice::ReadOnly) || f.size() > 4 * 1024 * 1024) return {}; const QByteArray bytes=f.readAll();if(hash)*hash=QCryptographicHash::hash(bytes,QCryptographicHash::Sha256);return QString::fromUtf8(bytes); }
QByteArray indexDependencyHash(const QString &file){QFile f(file);if(!f.open(QIODevice::ReadOnly))return {};QCryptographicHash hash(QCryptographicHash::Sha256);return hash.addData(&f)?hash.result():QByteArray();}
bool escapedProject(const QString &s, int p) { int n=0; while (p>0 && s[--p]==QLatin1Char('\\')) ++n; return n%2; }
QString balanced(const QString &s, int &p) {
  while (p<s.size() && s[p].isSpace()) ++p;
  if (p>=s.size() || s[p]!=QLatin1Char('{')) return {};
  int first=++p, depth=1;
  while (p<s.size()) { if (!escapedProject(s,p)) { if(s[p]==QLatin1Char('{')) ++depth; else if(s[p]==QLatin1Char('}') && --depth==0) { auto value=s.mid(first,p-first); ++p; return value; } } ++p; }
  return {};
}
QVector<QPair<int,int>> protectedProjectRanges(const QString &text,bool comments=false) {
  // Match comments together with literal bodies so a commented opening cannot
  // swallow later source, and percent signs inside a literal stay literal.
  static const QRegularExpression regions(QStringLiteral(R"(%[^\n]*|\\begin\s*\{(verbatim\*?|Verbatim|BVerbatim|LVerbatim|lstlisting|minted|comment|filecontents\*?)\}[\s\S]*?(?:\\end\s*\{\1\}|$)|\\(?:verb\*?(?![A-Za-z@])|lstinline\*?(?![A-Za-z@])(?:\[[^\]\n]*\])?|mintinline(?![A-Za-z@])(?:\[[^\]\n]*\])?\s*\{[^{}\n]*\})[ \t]*([^\s{])[^\n]*?(?:\2|(?=\n|$))|(?<braced>\\(?:lstinline\*?(?![A-Za-z@])(?:\[[^\]\n]*\])?|mintinline(?![A-Za-z@])(?:\[[^\]\n]*\])?\s*\{[^{}\n]*\})\s*\{))"));
  QVector<QPair<int,int>> ranges;int offset=0;
  while(offset<text.size()){
    const auto match=regions.match(text,offset);if(!match.hasMatch())break;
    const int first=int(match.capturedStart());
    if(escapedProject(text,first)){offset=first+1;continue;}
    int end=int(match.capturedEnd());
    if(match.capturedStart(QStringLiteral("braced"))>=0){int position=end-1;balanced(text,position);end=position;}
    if(comments||text[first]!=QLatin1Char('%'))ranges.append({first,end});offset=end;
  }
  return ranges;
}
QString resolved(const QString &name, const QString &file, const QString &root, const QString &suffix) {
  const auto path=resolveDependency(name,file,root,suffix);
  return authoredDependency(path,root)?path:QString();
}
void appendEntry(ProjectIndex &index, ProjectEntry entry) {
  entry.search=(entry.key+QLatin1Char(' ')+entry.number+QLatin1Char(' ')+entry.title+QLatin1Char(' ')+entry.detail+QLatin1Char(' ')+entry.file).toCaseFolded();
  const qsizetype bytes=sizeof(ProjectEntry)+2*(entry.key.size()+entry.title.size()+entry.detail.size()+entry.kind.size()+entry.number.size()+entry.search.size());
  if(index.entries.size()>=80000 || index.indexedBytes+bytes>16*1024*1024) { index.truncated=true; return; }
  index.indexedBytes+=bytes;
  index.entries.append(std::move(entry));
}
bool literalProjectKey(const QString &key) {
  return !key.isEmpty() && !key.contains(QLatin1Char('\\')) && !key.contains(QLatin1Char('#')) &&
      !key.contains(QLatin1Char('*')) && !key.contains(QLatin1Char('?'));
}
void appendUse(ProjectIndex &index, ProjectEntry entry) {
  const qsizetype bytes=sizeof(ProjectEntry)+2*(entry.file.size()+entry.key.size()+entry.detail.size());
  if(index.uses.size()>=80000 || index.indexedBytes+bytes>16*1024*1024){index.truncated=true;return;}
  index.indexedBytes+=bytes;index.uses.append(std::move(entry));
}
void appendIssue(ProjectIndex &index, ProjectEntry issue) {
  if(index.issues.size()>=1000){index.issuesTruncated=true;return;}
  QString words;
  if(issue.kind==QStringLiteral("duplicate-label"))words=QStringLiteral("标签重复定义 duplicate label");
  else if(issue.kind==QStringLiteral("missing-label"))words=QStringLiteral("未找到标签 missing label reference");
  else if(issue.kind==QStringLiteral("uncertain-label"))words=QStringLiteral("需编译确认标签 dynamic label reference");
  else if(issue.kind==QStringLiteral("uncertain-citation"))words=QStringLiteral("需编译确认文献条目 dynamic citation bibliography");
  else if(issue.kind==QStringLiteral("missing-citation"))words=QStringLiteral("未找到文献条目 missing citation bibliography");
  else words=QStringLiteral("文献文件未找到 missing bibliography file");
  issue.search=(words+QLatin1Char(' ')+issue.key+QLatin1Char(' ')+issue.detail+QLatin1Char(' ')+issue.file).toCaseFolded();
  index.issues.append(std::move(issue));
}
void projectDiagnostics(ProjectIndex &index, const std::function<bool()> &cancelled = {}) {
  index.issues.erase(std::remove_if(index.issues.begin(),index.issues.end(),[](const ProjectEntry &e){return e.kind!=QStringLiteral("missing-bibliography");}),index.issues.end());
  QHash<QString,int> labels;QSet<QString> citations;
  for(const auto &e:index.entries){if(e.kind==QStringLiteral("label"))++labels[e.key];else if(e.kind==QStringLiteral("cite"))citations.insert(e.key);}
  for(const auto &e:index.entries){
    if(cancelled&&cancelled()){index.cancelled=true;return;}
    if(e.kind==QStringLiteral("label")&&labels.value(e.key)>1){auto issue=e;issue.kind=QStringLiteral("duplicate-label");issue.number=QString::number(labels.value(e.key));appendIssue(index,std::move(issue));}
  }
  for(const auto &e:index.uses){
    if(cancelled&&cancelled()){index.cancelled=true;return;}
    if(e.kind==QStringLiteral("reference-use")&&!labels.contains(e.key)){auto issue=e;issue.kind=index.dynamicLabels||index.truncated?QStringLiteral("uncertain-label"):QStringLiteral("missing-label");appendIssue(index,std::move(issue));}
    else if(e.kind==QStringLiteral("citation-use")&&index.citationsReady&&!citations.contains(e.key)){auto issue=e;issue.kind=index.citationCoveragePartial||index.truncated?QStringLiteral("uncertain-citation"):QStringLiteral("missing-citation");appendIssue(index,std::move(issue));}
  }
}
void populateCitations(ProjectIndex &index, const QHash<QString,QString> &buffers, const std::function<bool()> &cancelled = {}) {
  for(const auto &file:index.bibliographies){
    if(cancelled&&cancelled()){index.cancelled=true;return;}
    const bool buffered=buffers.contains(file);const auto content=buffered?buffers.value(file).toUtf8():QByteArray();
    if((buffered?content.size():QFileInfo(file).size())>4*1024*1024){index.truncated=true;index.citationCoveragePartial=true;continue;}
    if(buffered)index.bibliographyBufferHashes.insert(file,QCryptographicHash::hash(content,QCryptographicHash::Sha256));else index.parsedHashes.insert(file,indexDependencyHash(file));
    BibTeXFile bib;if(!(buffered?bib.loadData(content):bib.load(file))){index.citationCoveragePartial=true;continue;}
    for(unsigned n=0;n<bib.numEntries();++n){if(cancelled&&cancelled()){index.cancelled=true;return;}const auto &e=bib.entry(n);if(e.type()!=BibTeXFile::Entry::NORMAL)continue;appendEntry(index,{file,e.key(),e.title(),e.author()+QStringLiteral(" · ")+e.year(),QStringLiteral("cite"),{},{},1,0});}
  }
  index.citationsReady=true;
}
QHash<QString,QString> numbers(const QString &first, QStringList &dependencies, bool &truncated, QHash<QString,QByteArray> &hashes, bool &wasCancelled, const std::function<bool()> &cancelled) {
  QHash<QString,QString> result; QStringList todo{first}; QSet<QString> seen; qsizetype bytes=0;
  static const QRegularExpression re(QStringLiteral(R"(\\(newlabel|@input)\s*)"));
  while(!todo.isEmpty() && seen.size()<512 && bytes<16*1024*1024) {
    if(cancelled&&cancelled()){wasCancelled=true;return {}; }
    const QString file=canonicalProject(todo.takeFirst()); if(seen.contains(file))continue;seen.insert(file);dependencies.append(file);
    const QString text=readProject(file,&hashes[file]);bytes+=text.size()*2;auto it=re.globalMatch(text);
    while(it.hasNext()) { if(cancelled&&cancelled()){wasCancelled=true;return {}; }auto m=it.next();int p=m.capturedEnd();const auto key=balanced(text,p);if(m.captured(1)==QStringLiteral("@input")){if(!key.contains(QLatin1Char('\\')))todo.append(QFileInfo(file).absoluteDir().filePath(key));continue;}const auto payload=balanced(text,p);int n=0;const auto value=balanced(payload,n);if(!key.isEmpty())result.insert(key,value); }
  }
  truncated|=!todo.isEmpty();return result;
}
}

QString maskProjectText(QString text) {
  for(const auto &range:protectedProjectRanges(text,true))for(int i=range.first;i<range.second;++i)if(text[i]!=QLatin1Char('\n'))text[i]=QLatin1Char(' ');
  return text;
}

QSharedPointer<const ProjectIndex> buildProjectIndex(const QString &root, const QHash<QString,QString> &buffers, bool citations, const std::function<bool()> &cancelled) {
  QElapsedTimer clock;clock.start();auto index=QSharedPointer<ProjectIndex>::create();
  auto stop=[&]{if(!cancelled||!cancelled())return false;index->cancelled=true;index->buildMilliseconds=clock.elapsed();return true;};
  if(stop())return index;
  QStringList todo{canonicalProject(root)};QSet<QString> seen,bibs;QHash<QString,QString> texts;
  if(QFileInfo(root).suffix().compare(QStringLiteral("bib"),Qt::CaseInsensitive)==0){bibs.insert(canonicalProject(root));todo.clear();}
  for(const auto &path:recordedDependencies(root)) {
    if(QFileInfo(path).suffix().compare(QStringLiteral("tex"),Qt::CaseInsensitive)==0)todo.append(path);
    if(QFileInfo(path).suffix().compare(QStringLiteral("bib"),Qt::CaseInsensitive)==0&&QFileInfo(path).isFile())bibs.insert(path);
  }
  qsizetype bytes=0;QDateTime latestSource;
  static const QRegularExpression includes(QStringLiteral(R"(\\(?:input|include|subfile)(?![A-Za-z@])\s*(?:\{([^{}]+)\}|([^\s%{}]+)))"));
  static const QRegularExpression bibliography(QStringLiteral(R"(\\(?:bibliography|addbibresource)\s*(?:\[[^\]]*\]\s*)?\{([^{}]+)\})"));
  static const QRegularExpression localPackages(QStringLiteral(R"(\\(usepackage|RequirePackage|documentclass|LoadClass)(?![A-Za-z@])\s*(?:\[[^\]]*\]\s*)?\{([^{}]+)\})"));
  auto missing=[&](const QString &name,const QString &file,const QString &suffix){
    for(const auto &folder:QStringList{QFileInfo(root).absolutePath(),QFileInfo(file).absolutePath()}){
      QString path=QDir(folder).absoluteFilePath(name);if(QFileInfo(path).suffix().isEmpty())path+=suffix;path=QDir::cleanPath(path);index->dependencies.append(path);index->dependencies.append(QFileInfo(path).absolutePath());
      auto ancestor=QFileInfo(path).absoluteDir();for(int depth=0;!ancestor.exists()&&depth<8&&ancestor.cdUp();++depth){}if(ancestor.exists())index->dependencies.append(ancestor.absolutePath());
    }
  };
  while(!todo.isEmpty() && seen.size()<512 && bytes<24*1024*1024) {
    if(stop())return index;
    const QString file=canonicalProject(todo.takeFirst());if(seen.contains(file))continue;seen.insert(file);
    if(!buffers.contains(file) && !QFileInfo(file).isFile())continue;
    const QString raw=buffers.contains(file)?buffers.value(file):readProject(file,&index->parsedHashes[file]);const QString text=maskProjectText(raw);bytes+=text.size()*2;
    index->literalRanges[file]=protectedProjectRanges(raw);
    if(bytes>24*1024*1024){index->truncated=true;break;}
    if(!buffers.contains(file)&&QFileInfo(file).size()>4*1024*1024)index->truncated=true;
    texts.insert(file,text);index->files.append(file);index->dependencies.append(file);index->dependencies.append(QFileInfo(file).absolutePath());latestSource=qMax(latestSource,QFileInfo(file).lastModified());
    const auto sourceHash=QCryptographicHash::hash(raw.toUtf8(),QCryptographicHash::Sha256);if(buffers.contains(file))index->sourceBufferHashes.insert(file,sourceHash);
    const auto declarations=completionDeclarations(text,file,sourceHash);
    if(declarations.size()>=2048)index->truncated=true;
    for(const auto &entry:declarations){if(index->macros.size()>=4096){index->truncated=true;break;}index->macros.append(entry);}
    auto it=includes.globalMatch(text);while(it.hasNext()){auto m=it.next();const auto name=(m.captured(1).isEmpty()?m.captured(2):m.captured(1)).trimmed();auto child=resolved(name,file,root,QStringLiteral(".tex"));if(!child.isEmpty())todo.append(child);else if(literalProjectKey(name))missing(name,file,QStringLiteral(".tex"));}
    // Only literal local style/class files; never search or parse the TeX
    // installation. Directory dependencies allow a later-created file to be
    // discovered on the next on-demand query.
    auto packages=localPackages.globalMatch(text);while(packages.hasNext()){
      const auto m=packages.next();const auto suffix=m.captured(1).contains(QStringLiteral("Class"),Qt::CaseInsensitive)?QStringLiteral(".cls"):QStringLiteral(".sty");
      for(const auto &rawName:m.captured(2).split(QLatin1Char(','))){
        const auto name=rawName.trimmed();if(!literalProjectKey(name))continue;
        QStringList names{name};if(!name.endsWith(suffix,Qt::CaseInsensitive))names.append(name+suffix);
        bool found=false;
        for(const auto &folder:QStringList{QFileInfo(root).absolutePath(),QFileInfo(file).absolutePath()}){
          for(const auto &candidate:names){const auto path=QDir(folder).absoluteFilePath(candidate);if(QFileInfo(path).isFile()||buffers.contains(canonicalProject(path))){todo.append(canonicalProject(path));found=true;break;}}
          if(found)break;
        }
        if(!found)missing(name,file,suffix);
      }
    }
    auto refs=bibliography.globalMatch(text);int bibliographyLine=1;qsizetype bibliographyOffset=0;
    while(refs.hasNext()){
      if(stop())return index;const auto match=refs.next();bibliographyLine+=QStringView(text).mid(bibliographyOffset,match.capturedStart()-bibliographyOffset).count(QLatin1Char('\n'));bibliographyOffset=match.capturedStart();
      for(const auto &value:match.captured(1).split(QLatin1Char(','))){
        const QString name=value.trimmed();const QString bib=resolveDependency(name,file,root,QStringLiteral(".bib"));
        if(!bib.isEmpty()){bibs.insert(bib);}
        else if(literalProjectKey(name)){
          index->citationCoveragePartial=true;
          QString path=QFileInfo(name).isAbsolute()?name:QFileInfo(file).absoluteDir().filePath(name);if(QFileInfo(path).suffix().isEmpty())path+=QStringLiteral(".bib");path=QDir::cleanPath(path);
          index->dependencies.append(path);index->dependencies.append(QFileInfo(path).absolutePath());
          QDir ancestor=QFileInfo(path).absoluteDir();for(int depth=0;!ancestor.exists()&&depth<8&&ancestor.cdUp();++depth){}if(ancestor.exists())index->dependencies.append(ancestor.absolutePath());
          appendIssue(*index,{file,name,name,{},QStringLiteral("missing-bibliography"),{},{},bibliographyLine,int(match.capturedStart())});
        }else index->citationCoveragePartial=true;
      }
    }
  }
  index->truncated|=!todo.isEmpty();
  QSet<QString> environments=QSet<QString>{QStringLiteral("theorem"),QStringLiteral("thm"),QStringLiteral("lemma"),QStringLiteral("lem"),QStringLiteral("proposition"),QStringLiteral("prop"),QStringLiteral("corollary"),QStringLiteral("cor"),QStringLiteral("definition"),QStringLiteral("defn"),QStringLiteral("assumption"),QStringLiteral("asm"),QStringLiteral("proof"),QStringLiteral("remark"),QStringLiteral("example")};
  static const QRegularExpression declarations(QStringLiteral(R"(\\(?:newtheorem|declaretheorem)\*?\s*(?:\[[^\]]*\]\s*)?\{([^{}]+)\})"));
  for(const auto &text:texts){if(stop())return index;auto it=declarations.globalMatch(text);while(it.hasNext())environments.insert(it.next().captured(1));}
  static const QRegularExpression markers(QStringLiteral(R"(\\(part|chapter|section|subsection|subsubsection)\*?\s*(?:\[[^\]]*\]\s*)?\{|\\begin\s*\{([^{}]+)\}\s*(?:\[([^\]\n]*)\])?|\\label\s*\{([^{}]+)\}|\\bibitem\s*(?:\[[^\]]*\]\s*)?\{([^{}]+)\}|\\end\s*\{([^{}]+)\})"));
  const QStringList mathEnvironments{QStringLiteral("equation"),QStringLiteral("equation*"),QStringLiteral("align"),QStringLiteral("align*"),QStringLiteral("alignat"),QStringLiteral("gather"),QStringLiteral("multline"),QStringLiteral("eqnarray")};
  for(const auto &file:index->files) {
    if(stop())return index;
    appendEntry(*index,{file,{},QFileInfo(file).fileName(),{},QStringLiteral("file"),{},{},1,0});
    const QString text=texts.value(file);auto it=markers.globalMatch(text);qsizetype previous=0;int line=1;QString heading;QVector<QPair<QString,int>> open;QVector<QPair<int,QString>> headings;
    while(it.hasNext()){if(stop())return index;auto m=it.next();line+=QStringView(text).mid(previous,m.capturedStart()-previous).count(QLatin1Char('\n'));previous=m.capturedStart();
      QString key,title,kind,detail=heading;
      if(!m.captured(6).isEmpty()){if(!open.isEmpty()&&open.last().first==m.captured(6))open.removeLast();continue;}
      if(!m.captured(1).isEmpty()){int p=m.capturedEnd()-1;heading=balanced(text,p).simplified();headings.append({int(m.capturedStart()),heading});title=m.captured(1)+QStringLiteral("  ")+heading;kind=QStringLiteral("heading");}
      else if(!m.captured(2).isEmpty()){open.append({m.captured(2),int(m.capturedEnd())});if(!environments.contains(m.captured(2)))continue;kind=m.captured(2);title=kind+QStringLiteral("  ")+(m.captured(3).isEmpty()?text.mid(m.capturedEnd(),160).trimmed().section(QLatin1Char('\n'),0,0).simplified().left(72):m.captured(3));}
      else if(!m.captured(4).isEmpty()){key=m.captured(4).trimmed();if(key.contains(QLatin1Char('\\'))||key.contains(QLatin1Char('#'))){index->dynamicLabels=true;continue;}title=key;kind=QStringLiteral("label");if(!open.isEmpty()&&(mathEnvironments.contains(open.last().first)||environments.contains(open.last().first))){QString body=text.mid(open.last().second,qMin(220,int(m.capturedStart())-open.last().second)).simplified();if(body.isEmpty())body=text.mid(m.capturedEnd(),220).section(QStringLiteral("\\end"),0,0).simplified();detail=body.left(150);} }
      else {key=m.captured(5).trimmed();title=key;kind=QStringLiteral("cite");detail=text.mid(m.capturedEnd(),220).simplified().left(160);}
      appendEntry(*index,{file,key,title,detail,kind,{},{},line,int(m.capturedStart())});
    }
    // Only literal keys from known reference/citation commands. Macro-created
    // labels are flagged as needing compilation, not declared absent.
    static const QRegularExpression uses(QStringLiteral(R"(\\(ref|eqref|pageref|autoref|cref|Cref|vref|Vref|nameref|cite|Cite|citep|Citep|citet|Citet|citealp|citealt|citeauthor|citeyear|citeyearpar|autocite|parencite|textcite|footcite|smartcite|supercite|fullcite|nocite)(?![A-Za-z@])\*?\s*(?:\[[^\]]*\]\s*){0,2}\{([^{}]*)\})"));
    auto usesIt=uses.globalMatch(text);qsizetype useOffset=0;int useLine=1,headingIndex=0;QString context;
    while(usesIt.hasNext()){
      if(stop())return index;const auto use=usesIt.next();useLine+=QStringView(text).mid(useOffset,use.capturedStart()-useOffset).count(QLatin1Char('\n'));useOffset=use.capturedStart();
      while(headingIndex<headings.size()&&headings[headingIndex].first<=use.capturedStart())context=headings[headingIndex++].second;
      for(const auto &rawKey:use.captured(2).split(QLatin1Char(','))){const QString key=rawKey.trimmed();if(!literalProjectKey(key))continue;
        const QString kind=use.captured(1).contains(QStringLiteral("cite"),Qt::CaseInsensitive)?QStringLiteral("citation-use"):QStringLiteral("reference-use");
        appendUse(*index,{file,key,key,context,kind,{},{},useLine,int(use.capturedStart())});
      }
    }
  }
  for(const auto &file:bibs){index->dependencies.append(file);index->bibliographies.append(file);appendEntry(*index,{file,{},QFileInfo(file).fileName(),{},QStringLiteral("file"),{},{},1,0});}
  if(citations)populateCitations(*index,buffers,cancelled);
  if(index->cancelled||stop())return index;
  projectDiagnostics(*index,cancelled);if(index->cancelled||stop())return index;
  // The numbers are advisory, never guessed. Dirty buffers suppress numbering.
  QString aux=QFileInfo(root).absoluteDir().filePath(QFileInfo(root).completeBaseName()+QStringLiteral(".aux"));
  const QString pdf=previewFile(root),meta=pdfMetadataReadFile(pdf);index->dependencies.append(meta);
  QJsonObject recorded;
  QFile metadata(meta);if(metadata.open(QIODevice::ReadOnly)&&metadata.size()<=2*1024*1024){const QByteArray bytes=metadata.readAll();index->parsedHashes.insert(meta,QCryptographicHash::hash(bytes,QCryptographicHash::Sha256));recorded=pdfMetadata(pdf);const QString out=recorded.value(QStringLiteral("outputDir")).toString();if(!out.isEmpty())aux=QDir(out).filePath(QFileInfo(root).completeBaseName()+QStringLiteral(".aux"));}
  index->numbersCurrent=buffers.isEmpty()&&QFileInfo(aux).isFile()&&QFileInfo(aux).lastModified()>=latestSource;
  // Managed builds carry byte-level provenance; timestamps alone are weaker.
  if(index->numbersCurrent&&!recorded.isEmpty()){
    const auto inputs=recorded.value(QStringLiteral("inputs")).toObject();if(inputs.isEmpty())index->numbersCurrent=false;
    for(auto i=inputs.begin();i!=inputs.end()&&index->numbersCurrent;++i){const QString file=QFileInfo(i.key()).isAbsolute()?i.key():QFileInfo(root).absoluteDir().filePath(i.key());index->dependencies.append(file);QFile f(file);QCryptographicHash h(QCryptographicHash::Sha256);index->numbersCurrent=f.open(QIODevice::ReadOnly)&&h.addData(&f)&&QString::fromLatin1(h.result().toHex())==i.value().toObject().value(QStringLiteral("hash")).toString();}
  }
  const auto values=numbers(aux,index->dependencies,index->truncated,index->parsedHashes,index->cancelled,cancelled);
  if(index->cancelled||stop())return index;
  QHash<QString,int> labelCounts;for(const auto &e:index->entries)if(e.kind==QStringLiteral("label"))++labelCounts[e.key];
  if(index->numbersCurrent)for(auto &e:index->entries)if(e.kind==QStringLiteral("label")&&values.contains(e.key)&&labelCounts.value(e.key)==1){e.number=values.value(e.key);e.title=e.number+QStringLiteral("  ")+e.key;e.search+=QLatin1Char(' ')+e.number.toCaseFolded();}
  index->dependencies.removeDuplicates();index->buildMilliseconds=clock.elapsed();return index;
}

CitationContext projectContext(const QString &block,int cursor) {
  CitationContext result;if(cursor<0||cursor>block.size()||block.size()>8192)return result;
  const QString clean=maskProjectText(block);
  static const QRegularExpression command(QStringLiteral(R"(\\(eqref|ref|pageref|autoref|cref|Cref|vref|Vref|nameref|cite[a-zA-Z]*|Cite[a-zA-Z]*|autocite|parencite|textcite|footcite|smartcite|supercite|fullcite|nocite)\*?\s*(?:\[[^\]]*\]\s*){0,2}\{([^{}]*)$)"));
  auto m=command.match(clean.left(cursor));if(!m.hasMatch()||escapedProject(clean,m.capturedStart())||clean.mid(m.capturedStart(),cursor-m.capturedStart())!=block.mid(m.capturedStart(),cursor-m.capturedStart()))return result;
  int start=m.capturedStart(2);const int comma=clean.lastIndexOf(QLatin1Char(','),cursor-1);if(comma>=start)start=comma+1;
  while(start<cursor&&clean[start].isSpace())++start;
  int end=cursor;while(end<block.size()&&!QStringLiteral(",}\n").contains(block[end]))++end;
  result={true,m.captured(1).contains(QStringLiteral("cite"),Qt::CaseInsensitive),start,end,block.mid(start,cursor-start)};return result;
}

CitationContext projectContext(const QTextCursor &c){if(c.hasSelection())return {};QString text=c.block().text();int offset=c.block().position(),position=c.positionInBlock();auto r=projectContext(text,position);
    // Wrapped citation arguments are common in papers. Look back only a small,
    // bounded prefix; literal ranges are checked by the worker's full index.
    auto previous=c.block().previous();for(int n=0;!r.valid&&n<4&&previous.isValid()&&text.size()+previous.length()<8192;++n){const QString prefix=previous.text()+QLatin1Char('\n');text.prepend(prefix);position+=prefix.size();offset=previous.position();previous=previous.previous();r=projectContext(text,position);}
    if(!r.valid)return r;
    r.query=r.query.simplified();r.start+=offset;r.end+=offset;return r;
  }

QVector<int> searchProject(const ProjectIndex &index,const QString &query,int limit,int completion) {
  QVector<int> result;if(limit<=0)return result;const auto words=query.toCaseFolded().split(QLatin1Char(' '),Qt::SkipEmptyParts);
  if(completion==3){for(int n=0;n<index.issues.size();++n){bool pass=true;for(const auto &word:words)if(!index.issues[n].search.contains(word)){pass=false;break;}if(pass){result.append(int(index.entries.size())+n);if(result.size()>=limit)break;}}return result;}
  if(completion){
    // Four bounded buckets preserve source order without sorting a whole
    // bibliography on the GUI thread. Empty-prefix queries stop at the limit.
    const auto prefix=query.trimmed().toCaseFolded();QVector<int> buckets[4];QSet<QString> seen;
    const auto kind=completion==2?QStringLiteral("cite"):QStringLiteral("label");
    for(int n=0;n<index.entries.size();++n){
      const auto &e=index.entries[n];if(e.kind!=kind||seen.contains(e.key))continue;bool pass=true;for(const auto &word:words)if(!e.search.contains(word)){pass=false;break;}if(!pass)continue;
      if(prefix.isEmpty()){seen.insert(e.key);result.append(n);if(result.size()>=limit)return result;continue;}
      const auto key=e.key.toCaseFolded();const int score=key==prefix?3:key.startsWith(prefix)?2:key.contains(prefix)?1:0;
      if(buckets[score].size()<limit){seen.insert(e.key);buckets[score].append(n);}
    }
    if(prefix.isEmpty())return result;
    for(int score=3;score>=0;--score)for(int n:buckets[score]){result.append(n);if(result.size()>=limit)return result;}
    return result;
  }
  for(int n=0;n<index.entries.size();++n){bool pass=true;for(const auto &word:words)if(!index.entries[n].search.contains(word)){pass=false;break;}if(pass){result.append(n);if(result.size()>=limit)break;}}
  return result;
}

namespace {
// A single expiring worker and a single cached project; idle work consumes no CPU.
QThreadPool &projectPool(){static QThreadPool pool;static const bool initialized=[](){pool.setMaxThreadCount(1);pool.setExpiryTimeout(10000);return true;}();Q_UNUSED(initialized);return pool;}
// Metadata alone misses same-size rewrites with a preserved modification time.
// Hash only files consumed by the bounded index, on its existing worker.

struct DependencyStamp{QString file; qint64 size,time; QByteArray hash;};
QSharedPointer<const ProjectIndex> cachedProject(const QString &root,const QHash<QString,QString> &buffers, bool citations = false, const std::function<bool()> &cancelled = {}) {
  static QString previousRoot;static QHash<QString,QString> previousBuffers;static QSharedPointer<const ProjectIndex> previous;static QVector<DependencyStamp> stamps;
  bool valid=previous&&previousRoot==root&&previousBuffers==buffers;
  if(valid)for(const auto &stamp:stamps){if(cancelled&&cancelled())return {};const QFileInfo f(stamp.file);if(f.size()!=stamp.size||f.lastModified().toMSecsSinceEpoch()!=stamp.time||(!stamp.hash.isEmpty()&&indexDependencyHash(stamp.file)!=stamp.hash)){valid=false;break;}}
  if(valid){if(citations&&!previous->citationsReady){auto enriched=QSharedPointer<ProjectIndex>::create(*previous);populateCitations(*enriched,buffers,cancelled);projectDiagnostics(*enriched,cancelled);if(enriched->cancelled)return {};for(auto &stamp:stamps){const auto hash=enriched->parsedHashes.value(stamp.file);if(!hash.isEmpty()){if(indexDependencyHash(stamp.file)!=hash)return enriched;stamp.hash=hash;}}previous=enriched;}return previous;}
  auto index=buildProjectIndex(root,buffers,citations,cancelled);if(index->cancelled)return {};QVector<DependencyStamp> currentStamps;for(const auto &file:index->dependencies){if(cancelled&&cancelled())return {};QFileInfo f(file);currentStamps.append({file,f.size(),f.lastModified().toMSecsSinceEpoch(),index->parsedHashes.value(file)});}// Never certify an old parse with a hash captured after a concurrent rewrite.
  for(const auto &stamp:currentStamps)if(!stamp.hash.isEmpty()&&indexDependencyHash(stamp.file)!=stamp.hash)return index;
  stamps=std::move(currentStamps);
  previousRoot=root;previousBuffers=buffers;previous=index;return index;
}
QHash<QString,QString> dirtyBuffers(){QHash<QString,QString> result;for(auto *w:TeXDocumentWindow::documentList())if(!w->untitled()&&w->isModified())result.insert(canonicalProject(w->fileName()),w->editor()->toPlainText());return result;}
struct CompletionInput { QString file, hint, text; QHash<QString,QString> buffers; };
CompletionInput completionInput(TeXDocumentWindow *window) {
  CompletionInput input;input.buffers=dirtyBuffers();input.text=window->textDoc()->sourceText();
  if(window->untitled()){input.file=completionProjectRoot(window);input.hint=input.file;input.buffers.insert(input.file,window->editor()->toPlainText());return input;}
  auto *doc=window->textDoc();input.file=canonicalProject(window->fileName());
  // Copy only explicit/session hints here. Full classification and directory
  // discovery run on the existing worker, never in the keystroke path.
  if(doc->property("researchPresentationSource").toString()==doc->absoluteFilePath())input.hint=doc->property("researchPresentationRoot").toString();
  if(input.hint.isEmpty()&&doc->hasModeLine(QStringLiteral("root")))input.hint=QFileInfo(doc->getFileInfo().dir(),doc->getModeLineValue(QStringLiteral("root")).trimmed()).absoluteFilePath();
  if(input.hint.isEmpty()&&doc->property("researchArticleSource").toString()==doc->absoluteFilePath())input.hint=doc->property("researchArticleRoot").toString();
  return input;
}
QSharedPointer<const ProjectIndex> completionIndex(const CompletionInput &input,bool citations,const std::function<bool()> &cancelled){
  if(cancelled&&cancelled())return {};
  const QString root=input.hint.isEmpty()?resolveCompletionRoot(input.file,input.text,input.buffers,cancelled):input.hint;
  if(root.isEmpty()||(cancelled&&cancelled()))return {};
  return cachedProject(root,input.buffers,citations,cancelled);
}
bool completionEntryCurrent(const ProjectIndex &index,const ProjectEntry &entry){
  const auto hash=index.sourceBufferHashes.value(entry.file,index.bibliographyBufferHashes.value(entry.file,index.parsedHashes.value(entry.file)));if(hash.isEmpty())return false;
  for(auto *w:TeXDocumentWindow::documentList())if((w->untitled()?completionProjectRoot(w)==entry.file:w->isModified()&&canonicalProject(w->fileName())==entry.file))return QCryptographicHash::hash(w->editor()->toPlainText().toUtf8(),QCryptographicHash::Sha256)==hash;
  QFile f(entry.file);return f.open(QIODevice::ReadOnly)&&f.size()<=4*1024*1024&&QCryptographicHash::hash(f.readAll(),QCryptographicHash::Sha256)==hash;
}
const ProjectEntry *projectRow(const ProjectIndex &index,int row){if(row<0)return nullptr;if(row<index.entries.size())return &index.entries[row];row-=int(index.entries.size());return row<index.issues.size()?&index.issues[row]:nullptr;}
QString issueTitle(const ProjectEntry &e){
  QString title;
  if(e.kind==QStringLiteral("duplicate-label"))title=ui(QStringLiteral("标签重复定义"));
  else if(e.kind==QStringLiteral("missing-label"))title=ui(QStringLiteral("未找到标签"));
  else if(e.kind==QStringLiteral("uncertain-label"))title=ui(QStringLiteral("需编译确认标签"));
  else if(e.kind==QStringLiteral("missing-citation"))title=ui(QStringLiteral("未找到文献条目"));
  else if(e.kind==QStringLiteral("uncertain-citation"))title=ui(QStringLiteral("需编译确认文献条目"));
  else title=ui(QStringLiteral("文献文件未找到"));
  return title+QStringLiteral(" · ")+e.key+(e.kind==QStringLiteral("duplicate-label")?QStringLiteral(" (%1)").arg(e.number):QString());
}
class ProjectRows : public QAbstractListModel {
public:
  QSharedPointer<const ProjectIndex> project;QVector<int> rows;
  explicit ProjectRows(QObject *p):QAbstractListModel(p){}
  int rowCount(const QModelIndex &p={})const override{return p.isValid()?0:rows.size();}
  QVariant data(const QModelIndex &i,int role)const override{
    if(!i.isValid()||!project||i.row()>=rows.size())return {};const auto *entry=projectRow(*project,rows[i.row()]);if(!entry)return {};const auto &e=*entry;
    if(role==Qt::DisplayRole){const bool issue=rows[i.row()]>=project->entries.size();const QString title=issue?issueTitle(e):e.kind==QStringLiteral("cite")&&!e.key.isEmpty()?e.key+QStringLiteral(" · ")+e.title:e.title;
      const QString location=QFileInfo(e.file).fileName()+QStringLiteral(" : %1").arg(e.line);return title+QLatin1Char('\n')+(issue?location+(e.detail.isEmpty()?QString():QStringLiteral(" · ")+e.detail.left(120)):e.detail.isEmpty()?location:e.detail.left(150));}
    if(role==Qt::ToolTipRole)return QVariant();return {};
  }
  void replace(QSharedPointer<const ProjectIndex> p,QVector<int> r){beginResetModel();project=std::move(p);rows=std::move(r);endResetModel();}
};
struct ReadingPlace{QPointer<TeXDocumentWindow> window;QString file;QTextCursor cursor,viewportCursor;int position=0,anchor=0,x=0,y=0,viewportY=0;QVariantMap article,slides,references,range,viewportRange;QPointer<PDFDocumentWindow> pdf;QString pdfFile;QPointF pdfCenter;qreal zoom=1;bool pdfFocus=false;QPointer<QDialog> readingDialog;int readingScroll=0,pinnedScroll=0;QPointer<QWidget> focused;};
bool restoringReading=false;
QVector<ReadingPlace> backPlaces,forwardPlaces;QVector<QPointer<QAction>> readingActions;
ReadingPlace place(TeXDocumentWindow *w){ReadingPlace p;if(!w)return p;p.window=w;p.file=w->fileName();p.cursor=w->textCursor();const auto source=w->textDoc()->editorText();p.range=textAnchor(source,p.cursor.selectionStart(),p.cursor.selectionEnd());p.viewportCursor=w->editor()->cursorForPosition(QPoint(0,0));p.viewportY=w->editor()->cursorRect(p.viewportCursor).top();p.viewportRange=textAnchor(source,p.viewportCursor.position(),p.viewportCursor.position(),p.range.value(QStringLiteral("hash")).toString());p.position=p.cursor.position();p.anchor=p.cursor.anchor();p.x=w->editor()->horizontalScrollBar()->value();p.y=w->editor()->verticalScrollBar()->value();p.article=articleReadingState(w);p.slides=slideReadingState(w);p.references=referenceReadingState(w);p.focused=QApplication::focusWidget();p.readingDialog=w->findChild<QDialog*>(QStringLiteral("researchRelatedReading"));if(p.readingDialog&&!p.readingDialog->isVisible())p.readingDialog=nullptr;if(!p.readingDialog)if(auto *citation=w->findChild<QDialog*>(QStringLiteral("researchCitationDialog"));citation&&citation->isVisible())p.readingDialog=citation;if(p.readingDialog)if(auto *text=p.readingDialog->findChild<QTextBrowser*>(QStringLiteral("researchRelatedContents")))p.readingScroll=text->verticalScrollBar()->value();if(auto *area=w->findChild<QScrollArea*>(QStringLiteral("researchPinnedContent")))p.pinnedScroll=area->verticalScrollBar()->value();p.pdf=w->pdfDocument();if(p.pdf){p.pdfFile=p.pdf->fileName();p.zoom=p.pdf->widget()->zoomLevel();p.pdfCenter=p.pdf->widget()->mapToScene(p.pdf->widget()->viewport()->rect().center());}return p;}
ReadingPlace place(PDFDocumentWindow *pdf){auto p=place(TeXDocumentWindow::findDocument(pdf->mainSourceFilename()));p.pdf=pdf;p.pdfFile=pdf->fileName();p.zoom=pdf->widget()->zoomLevel();p.pdfCenter=pdf->widget()->mapToScene(pdf->widget()->viewport()->rect().center());p.pdfFocus=true;return p;}
bool sameReadingPlace(const ReadingPlace &a,const ReadingPlace &b){if(a.pdfFocus!=b.pdfFocus)return false;if(a.pdfFocus)return a.pdfFile==b.pdfFile&&qAbs(a.zoom-b.zoom)<.001&&QLineF(a.pdfCenter,b.pdfCenter).length()<2;const int aPosition=a.window&&!a.cursor.isNull()?a.cursor.position():a.position,aAnchor=a.window&&!a.cursor.isNull()?a.cursor.anchor():a.anchor;return a.file==b.file&&aPosition==b.position&&aAnchor==b.anchor&&a.x==b.x&&a.y==b.y&&a.article.value(QStringLiteral("partial"))==b.article.value(QStringLiteral("partial"))&&a.slides==b.slides&&a.readingDialog==b.readingDialog&&a.references==b.references;}
void updateReadingActions(){readingActions.erase(std::remove_if(readingActions.begin(),readingActions.end(),[](auto a){return !a;}),readingActions.end());for(auto action:readingActions)action->setEnabled(action->objectName()==QStringLiteral("researchNavigateForward")?!forwardPlaces.isEmpty():!backPlaces.isEmpty());}
void remember(const ReadingPlace &p){if(restoringReading||p.file.isEmpty()&&p.pdfFile.isEmpty())return;forwardPlaces.clear();updateReadingActions();if(!backPlaces.isEmpty()){const auto &last=backPlaces.last();if(sameReadingPlace(last,p))return;}backPlaces.append(p);while(backPlaces.size()>64)backPlaces.removeFirst();forwardPlaces.clear();updateReadingActions();}
// A new PDF window can receive its final native size after show(). Restore the
// center after that layout, cancelling immediately on any actual user input.
class PendingPdfPlace final:public QObject {
  PDFDocumentWindow *window;QPointF center;qreal zoom;QTimer timer;int turns=0;bool cancelled=false;
public:
  PendingPdfPlace(PDFDocumentWindow *w,QPointF p,qreal z):QObject(w),window(w),center(p),zoom(z){
    setObjectName(QStringLiteral("researchPendingPdfPlace"));qApp->installEventFilter(this);timer.setSingleShot(true);
    connect(&timer,&QTimer::timeout,this,[this]{if(cancelled)return;if(qAbs(window->widget()->zoomLevel()-zoom)>.001){stop();return;}window->widget()->centerOn(center);if(++turns<2)timer.start(0);else stop();});
    connect(w->widget(),&QtPDF::PDFDocumentView::changedDocument,this,[this]{stop();});timer.start(0);
  }
  void stop(){cancelled=true;timer.stop();qApp->removeEventFilter(this);deleteLater();}
  bool eventFilter(QObject *,QEvent *event)override{
    switch(event->type()){case QEvent::KeyPress:case QEvent::InputMethod:case QEvent::Wheel:case QEvent::MouseButtonPress:case QEvent::TouchBegin:case QEvent::NonClientAreaMouseButtonPress:stop();break;default:break;}return false;
  }
};
// Restore a text anchor after the final dock/scope layout, not a pixel offset
// from before edits. The existing asynchronous index completion wakes this
// one-shot operation through its pending-state property; there is no polling.
class PendingSourcePlace final:public QObject {
 TeXDocumentWindow *window;QTextCursor anchor;int y,x,turns=0;QTimer timer;bool stopped=false;
 bool pending()const{return window->property("researchRestoreArticlePlace").isValid()||window->property("researchRestoreSlidePlace").isValid();}
 void schedule(){if(!stopped&&!timer.isActive())timer.start(0);}
public:
 PendingSourcePlace(TeXDocumentWindow *w,const QTextCursor &at,int offset,int horizontal):QObject(w),window(w),anchor(at),y(offset),x(horizontal){
   setObjectName(QStringLiteral("researchPendingSourcePlace"));timer.setSingleShot(true);qApp->installEventFilter(this);
   connect(&timer,&QTimer::timeout,this,[this]{if(stopped||pending())return;if(anchor.isNull()||anchor.document()!=window->textDoc()){stop();return;}
     auto *editor=window->editor();editor->verticalScrollBar()->setValue(editor->verticalScrollBar()->value()+editor->cursorRect(anchor).top()-y);editor->horizontalScrollBar()->setValue(x);
     if(++turns<2)schedule();else stop();});
   connect(w->textDoc(),&QTextDocument::contentsChange,this,[this](int,int removed,int added){if(removed||added)stop(true);});
   schedule();
 }
 ~PendingSourcePlace()override{qApp->removeEventFilter(this);}
 void stop(bool cancelScope=false){if(stopped)return;stopped=true;timer.stop();qApp->removeEventFilter(this);if(cancelScope){window->setProperty("researchRestoreArticlePlace",QVariant());window->setProperty("researchRestoreSlidePlace",QVariant());}deleteLater();}
 bool eventFilter(QObject *object,QEvent *event)override{
   if(stopped)return false;const auto type=event->type();auto *widget=qobject_cast<QWidget*>(object);
   if(widget&&widget->window()==window){switch(type){case QEvent::KeyPress:case QEvent::InputMethod:case QEvent::Wheel:case QEvent::MouseButtonPress:case QEvent::TouchBegin:case QEvent::NonClientAreaMouseButtonPress:stop(true);return false;default:break;}}
   if(object==window&&type==QEvent::DynamicPropertyChange){const auto key=static_cast<QDynamicPropertyChangeEvent*>(event)->propertyName();if(key=="researchRestoreArticlePlace"||key=="researchRestoreSlidePlace")schedule();}
   if((object==window||object==window->editor()->viewport())&&(type==QEvent::LayoutRequest||type==QEvent::Resize)){turns=0;schedule();}
   return false;
 }
};
bool restorePlace(const ReadingPlace &p,bool activate=true){
  auto *w=p.window.data();if(!w&&!p.file.isEmpty()&&QFileInfo(p.file).isFile())w=TeXDocumentWindow::openDocument(p.file,false,false);
  if(w&&activate)cancelRecentWritingPlace(w);
  if(w){auto cursor=p.cursor;if(cursor.isNull()||cursor.document()!=w->textDoc()){int a=0,b=0;if(!locateTextAnchor(w->textDoc()->editorText(),p.range,&a,&b)){showTransientStatusMessage(w,words("原阅读位置已改变，无法唯一定位。","The original reading position changed and cannot be located uniquely."));return false;}cursor=QTextCursor(w->textDoc());if(p.position==p.anchor)cursor.setPosition(b);else{cursor.setPosition(p.position<p.anchor?b:a);cursor.setPosition(p.position<p.anchor?a:b,QTextCursor::KeepAnchor);}}w->editor()->setTextCursor(cursor);auto article=p.article,slides=p.slides;article.insert(QStringLiteral("scroll"),p.y);slides.insert(QStringLiteral("scroll"),p.y);restoreSlideReadingState(w,slides);restoreArticleReadingState(w,article);restoreReferenceReadingState(w,p.references,!activate);if(auto *area=w->findChild<QScrollArea*>(QStringLiteral("researchPinnedContent")))area->verticalScrollBar()->setValue(p.pinnedScroll);w->editor()->horizontalScrollBar()->setValue(p.x);w->editor()->verticalScrollBar()->setValue(p.y);}
  if(w){
    if(auto *pending=w->findChild<QObject*>(QStringLiteral("researchPendingSourcePlace")))delete pending;
    auto anchor=p.viewportCursor;if(anchor.isNull()||anchor.document()!=w->textDoc()){int a=0,b=0;if(locateTextAnchor(w->textDoc()->editorText(),p.viewportRange,&a,&b)){anchor=QTextCursor(w->textDoc());anchor.setPosition(a);}}
    if(!anchor.isNull()&&anchor.document()==w->textDoc())new PendingSourcePlace(w,anchor,p.viewportY,p.x);
  }
  auto *pdf=p.pdf.data();if(!pdf&&p.pdfFocus&&!p.pdfFile.isEmpty()&&QFileInfo(p.pdfFile).isFile())pdf=new PDFDocumentWindow(p.pdfFile,w);
  if(pdf){if(auto *pending=pdf->findChild<QObject*>(QStringLiteral("researchPendingPdfPlace")))delete pending;pdf->setProperty("researchRestoredReadingPosition",true);pdf->widget()->setZoomLevel(p.zoom);pdf->widget()->centerOn(p.pdfCenter);new PendingPdfPlace(pdf,p.pdfCenter,p.zoom);}
  if(!activate)return w||pdf;
  if(p.pdfFocus&&pdf){pdf->selectWindow();pdf->widget()->setFocus();return true;}
  if(w){w->selectWindow();if(p.readingDialog&&p.readingDialog->objectName()==QStringLiteral("researchCitationDialog")){p.readingDialog->show();p.readingDialog->raise();p.readingDialog->activateWindow();if(p.focused)p.focused->setFocus();return true;}if(auto *dialog=w->findChild<QDialog*>(QStringLiteral("researchRelatedReading"));dialog&&p.references.value(QStringLiteral("dialog")).toBool()){dialog->show();dialog->raise();dialog->activateWindow();if(auto *text=dialog->findChild<QTextBrowser*>(QStringLiteral("researchRelatedContents")))text->verticalScrollBar()->setValue(p.readingScroll);if(p.focused)p.focused->setFocus();}else w->editor()->setFocus();return true;}return false;
}
void jumpEntry(TeXDocumentWindow *w,const ProjectEntry &e){remember(place(w));if(e.file.isEmpty())w->goToLine(e.line);else TeXDocumentWindow::openDocument(e.file,true,true,e.line);}
class WritingController : public QObject {
public:
  CitationContext shownContext; int shownRevision{-1};
  TeXDocumentWindow *window;QTimer timer;QListView *popup;ProjectRows *model;quint64 generation=0;bool busy=false,pending=false;std::shared_ptr<std::atomic<quint64>> requestEpoch=std::make_shared<std::atomic<quint64>>(0);int dismissedPosition=-1,expectedPosition=-1;QString dismissedQuery;bool inputTurn=false,composing=false,manualSession=false;
  explicit WritingController(TeXDocumentWindow *w):QObject(w),window(w),popup(new PopupListView(w)),model(new ProjectRows(popup)) {
    setObjectName(QStringLiteral("researchProjectWriting"));popup->setObjectName(QStringLiteral("researchReferenceCompletion"));popup->setWindowFlags(Qt::ToolTip);popup->setFocusPolicy(Qt::NoFocus);popup->setModel(model);popup->setUniformItemSizes(true);popup->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);popup->setStyleSheet(QStringLiteral("QListView{background:white;color:#1d1d1f;border:1px solid #d2d2d7;border-radius:8px;padding:4px;font-size:13px;} QListView::item{padding:5px;border-radius:5px;} QListView::item:selected{background:#e9e9ed;color:#1d1d1f;}"));popup->hide();
    preparePopupSurface(popup);
    timer.setSingleShot(true);timer.setTimerType(Qt::PreciseTimer);timer.setInterval(300);connect(&timer,&QTimer::timeout,this,[this]{refresh();});
    connect(TWApp::instance(),&TWApp::windowListChanged,this,[]{++projectEpoch;});
    connect(w->editor(),&QTextEdit::textChanged,this,[this]{popup->hide();if(!inputTurn)cancel();});
    connect(w->editor(),&QTextEdit::cursorPositionChanged,this,[this]{popup->hide();if(!inputTurn)cancel();});
    connect(w->textDoc(),&QTextDocument::contentsChange,this,[this](int position,int removed,int added){if(inputTurn&&(removed||added))expectedPosition=position+added;});
    for(auto *bar:{w->editor()->verticalScrollBar(),w->editor()->horizontalScrollBar()})connect(bar,&QScrollBar::valueChanged,this,[this]{if(inputTurn)popup->hide();else cancel();});
    w->editor()->installEventFilter(this);w->installEventFilter(this);popup->installEventFilter(this);connect(popup,&QListView::clicked,this,[this](const QModelIndex &i){accept(i.row());});
  }
  ~WritingController() override {requestEpoch->fetch_add(1);}
  int mode()const{return qBound(0,Tw::Settings().value(QStringLiteral("researchCommandCompletionMode"),0).toInt(),2);}
  void invalidate(){requestEpoch->store(++generation);}
  void cancel(){invalidate();inputTurn=false;manualSession=false;pending=false;timer.stop();popup->hide();}
  void inputStarted(){
    invalidate();dismissedPosition=-1;inputTurn=true;expectedPosition=-1;timer.stop();popup->hide();
    const auto ticket=generation;const auto before=window->textDoc()->revision();
    QTimer::singleShot(0,this,[this,ticket,before]{
      if(ticket!=generation)return;inputTurn=false;
      if(composing||!mode()||window->textDoc()->revision()==before||window->cursorPosition()!=expectedPosition||!context().valid){manualSession=false;return;}
      if(manualSession||mode()==2)timer.start(manualSession?0:300);
    });
  }
  CitationContext context(){return window->editor()->isReadOnly()?CitationContext{}:projectContext(window->editor()->textCursor());}

  void refresh(){auto c=context();if(composing||!mode()||!c.valid||!window->editor()->hasFocus()||(dismissedPosition==c.start&&dismissedQuery==c.query)){popup->hide();return;}request([this](QSharedPointer<const ProjectIndex> p){auto c=context();if(composing||!mode()||!c.valid||!window->editor()->hasFocus())return;const QString file=window->untitled()?completionProjectRoot(window):canonicalProject(window->fileName());for(const auto &range:p->literalRanges.value(file))if(range.first<=c.start&&c.start<range.second){popup->hide();return;}auto rows=searchProject(*p,c.query,5,c.citation?2:1);model->replace(p,rows);if(rows.isEmpty()||(rows.size()==1&&p->entries[rows.first()].key==c.query)){popup->hide();return;}popup->setCurrentIndex(model->index(0));const auto anchor=window->editor()->cursorRect();const QPoint top=window->editor()->viewport()->mapToGlobal(anchor.topLeft()),bottom=window->editor()->viewport()->mapToGlobal(anchor.bottomLeft());
      QRect area(window->mapToGlobal(QPoint()),window->size());if(auto *screen=QGuiApplication::screenAt(bottom))area=area.intersected(screen->availableGeometry());area.adjust(8,8,-8,-8);
      const int below=area.bottom()-bottom.y()-4,above=top.y()-area.top()-4;
      const bool underneath=below>=qMin(5,int(rows.size()))*49+12||below>=above;
      const int space=underneath?below:above,shown=qMin(5,qMin(int(rows.size()),(space-12)/49));
      if(shown<1||area.width()<100){popup->hide();return;}
      popup->resize(qMin(area.width(),qMin(540,qMax(320,window->editor()->width()-30))),shown*49+12);
      popup->move(qBound(area.left(),bottom.x(),area.right()-popup->width()+1),underneath?bottom.y()+4:top.y()-popup->height()-4);shownContext=c;shownRevision=window->textDoc()->revision();popup->show();});}
  void request(std::function<void(QSharedPointer<const ProjectIndex>)> done){
    // One task per controller at a time; subsequent edits coalesce into a retry.
    if(busy){pending=true;return;}busy=true;const quint64 ticket=generation;const auto epoch=requestEpoch;QPointer<WritingController> guard(this);
    const auto input=completionInput(window);
    const bool citations=context().citation;
    projectPool().start(QRunnable::create([guard,input,ticket,citations,epoch,done=std::move(done)]{auto cancelled=[epoch,ticket]{return epoch->load()!=ticket;};auto value=completionIndex(input,citations,cancelled);if(!qApp)return;QMetaObject::invokeMethod(qApp,[guard,value,ticket,done]{if(!guard)return;guard->busy=false;if(value&&guard->generation==ticket)done(value);if(guard->pending){guard->pending=false;if(!guard->composing&&guard->mode()&&(guard->manualSession||guard->mode()==2))guard->timer.start(guard->manualSession?0:300);}},Qt::QueuedConnection);}));
  }
  void accept(int row){if(composing||!mode()||!popup->isVisible()||row<0||row>=model->rows.size()||!model->project)return;auto c=context();if(!c.valid||window->textDoc()->revision()!=shownRevision||c.start!=shownContext.start||c.end!=shownContext.end||c.query!=shownContext.query||!completionEntryCurrent(*model->project,model->project->entries[model->rows[row]])){cancel();return;}const auto key=model->project->entries[model->rows[row]].key;cancel();auto cursor=window->editor()->textCursor();cursor.beginEditBlock();cursor.setPosition(c.start);cursor.setPosition(c.end,QTextCursor::KeepAnchor);cursor.insertText(key);cursor.endEditBlock();window->editor()->setTextCursor(cursor);dismissedPosition=c.start;dismissedQuery=key;timer.stop();popup->hide();}
  bool eventFilter(QObject *o,QEvent *event)override{
    if(o==popup&&event->type()==QEvent::Hide)model->replace({},{});
    if(o==window&&(event->type()==QEvent::WindowDeactivate||event->type()==QEvent::Hide||event->type()==QEvent::Resize||event->type()==QEvent::Move||event->type()==QEvent::Close))cancel();
    if(o!=window->editor())return false;
    if(event->type()==QEvent::FocusOut||event->type()==QEvent::MouseButtonPress||event->type()==QEvent::Resize){cancel();return false;}
    if(event->type()==QEvent::InputMethod){auto *ime=static_cast<QInputMethodEvent*>(event);cancel();composing=!ime->preeditString().isEmpty();if(!composing&&!ime->commitString().isEmpty()&&mode())inputStarted();return false;}
    if(event->type()!=QEvent::KeyPress&&event->type()!=QEvent::ShortcutOverride)return false;
    auto *key=static_cast<QKeyEvent*>(event);const auto modifiers=key->modifiers()&~Qt::KeyboardModifiers(Qt::KeypadModifier);
    const bool triggerKey=(key->key()==Qt::Key_Slash&&modifiers==Qt::AltModifier)||(key->key()==Qt::Key_Space&&modifiers==Qt::ControlModifier&&Tw::Settings().value(QStringLiteral("researchCompletionCtrlSpace"),false).toBool());
    const bool tab=key->key()==Qt::Key_Tab&&modifiers==Qt::NoModifier;
    const bool visible=popup->isVisible();const bool confirmKey=visible&&(tab||((key->key()==Qt::Key_Return||key->key()==Qt::Key_Enter)&&modifiers==Qt::ControlModifier));
    const bool shortcutFree=(tab&&visible)||!(triggerKey||confirmKey)||completionShortcutAvailable(window,QKeySequence(int(modifiers)|key->key()));
    const bool trigger=triggerKey&&context().valid&&shortcutFree;const bool confirm=confirmKey&&shortcutFree;
    if(event->type()==QEvent::ShortcutOverride){if(!composing&&(trigger||confirm)){event->accept();return true;}return false;}
    if(composing)return false;
    if(trigger){cancel();if(mode()){dismissedPosition=-1;manualSession=true;refresh();}return true;}
    if(confirm){accept(popup->currentIndex().row());return true;}
    if(visible&&modifiers==Qt::NoModifier&&(key->key()==Qt::Key_Up||key->key()==Qt::Key_Down)){const int row=qBound(0,popup->currentIndex().row()+(key->key()==Qt::Key_Up?-1:1),model->rowCount()-1);popup->setCurrentIndex(model->index(row));popup->scrollTo(model->index(row));return true;}
    if(key->key()==Qt::Key_Escape){if(visible||timer.isActive()||busy){auto c=context();dismissedPosition=c.start;dismissedQuery=c.query;cancel();cancelCommandCompletion(window);return true;}return false;}
    if(!mode())return false;
    const bool editing=(modifiers==Qt::NoModifier||modifiers==Qt::ShiftModifier)&&((!key->text().isEmpty()&&key->key()!=Qt::Key_Return&&key->key()!=Qt::Key_Enter&&key->key()!=Qt::Key_Tab&&key->key()!=Qt::Key_Backtab)||key->key()==Qt::Key_Backspace||key->key()==Qt::Key_Delete);
    if(editing)inputStarted();else cancel();return false;
  }
};
WritingController *controller(TeXDocumentWindow *w){return dynamic_cast<WritingController*>(w->findChild<QObject*>(QStringLiteral("researchProjectWriting")));}
}

void cancelProjectWriting(QMainWindow *w){if(w)if(auto *c=dynamic_cast<WritingController*>(w->findChild<QObject*>(QStringLiteral("researchProjectWriting"))))c->cancel();}
quint64 completionProjectEpoch(){return projectEpoch;}
QString completionProjectRoot(TeXDocumentWindow *window){return window->untitled()?QDir::current().filePath(QStringLiteral("__research_unsaved_%1.tex").arg(quintptr(window),0,16)):window->textDoc()->getRootFilePath();}
bool showProjectCompletion(TeXDocumentWindow *w){auto *c=controller(w);if(!c||!c->context().valid)return false;c->cancel();c->dismissedPosition=-1;c->manualSession=true;c->refresh();return true;}

void requestCompletionProject(TeXDocumentWindow *window,QObject *receiver,std::function<void(QSharedPointer<const ProjectIndex>)> done,const std::function<bool()> &cancelled){
  if(!window||!receiver)return;
  const auto input=completionInput(window);QPointer<QObject> guard(receiver);
  projectPool().start(QRunnable::create([input,guard,done=std::move(done),cancelled]{
    if(cancelled&&cancelled())return;auto value=completionIndex(input,false,cancelled);if(!qApp)return;
    QMetaObject::invokeMethod(qApp,[guard,value,done,cancelled]{if(guard&&value&&(!cancelled||!cancelled()))done(value);},Qt::QueuedConnection);
  }));
}

void rememberReadingPlace(TeXDocumentWindow *w){if(w)remember(place(w));}
void rememberReadingPlace(PDFDocumentWindow *w){if(w)remember(place(w));}
QVariantMap captureWritingPlace(TeXDocumentWindow *w){
 if(!w||w->untitled())return {};const auto p=place(w);auto slides=p.slides;
 if(slides.value(QStringLiteral("enabled")).toBool()&&!slides.value(QStringLiteral("full")).toBool()&&!slides.value(QStringLiteral("preamble")).toBool())slides.insert(QStringLiteral("savedCursor"),true);
 auto references=p.references;references.remove(QStringLiteral("pinAnchor"));
 return {{QStringLiteral("file"),p.file},{QStringLiteral("range"),p.range},
   {QStringLiteral("reverse"),p.position<p.anchor},{QStringLiteral("selected"),p.cursor.hasSelection()},
   {QStringLiteral("excerpt"),w->textDoc()->editorText().mid(p.cursor.hasSelection()?p.cursor.selectionStart():qMax(0,p.position-120),p.cursor.hasSelection()?qMin(800,p.cursor.selectionEnd()-p.cursor.selectionStart()):500)},
   {QStringLiteral("line"),p.cursor.blockNumber()+1},{QStringLiteral("pdfPage"),p.pdf?int(p.pdf->widget()->currentPage())+1:0},
   {QStringLiteral("x"),p.x},{QStringLiteral("y"),p.y},{QStringLiteral("cursorY"),w->editor()->cursorRect().top()},
   {QStringLiteral("viewportRange"),p.viewportRange},{QStringLiteral("viewportY"),p.viewportY},
   {QStringLiteral("article"),p.article},{QStringLiteral("slides"),slides},{QStringLiteral("references"),references},
   {QStringLiteral("pdf"),p.pdfFile},{QStringLiteral("zoom"),p.zoom},{QStringLiteral("pdfX"),p.pdfCenter.x()},{QStringLiteral("pdfY"),p.pdfCenter.y()},
   {QStringLiteral("pdfVisible"),p.pdf&&p.pdf->isVisible()},{QStringLiteral("focus"),w->property("researchFocus").toBool()},
   {QStringLiteral("width"),w->width()},{QStringLiteral("height"),w->height()},{QStringLiteral("layout"),QString::fromLatin1(w->saveState().toBase64())}};
}
static bool restoreWritingPlaceInWindow(TeXDocumentWindow *w,const QVariantMap &state,QString *error,bool activate){
 error->clear();const auto file=state.value(QStringLiteral("file")).toString();
 if(!QFileInfo(file).isFile()){*error=words("写作现场的文件已移动或删除。","The saved document was moved or deleted.");return false;}
 if(!w||w->untitled()||QFileInfo(w->fileName())!=QFileInfo(file))return false;
 const auto text=w->textDoc()->editorText();int start=0,end=0;
 if(!locateTextAnchor(text,state.value(QStringLiteral("range")).toMap(),&start,&end)){*error=words("原位置已改变或存在多处匹配，请先在源码中核对。","The saved position changed or is ambiguous. Review it in source.");return false;}
 if(activate)cancelRecentWritingPlace(w);
 ReadingPlace p;p.window=w;p.file=file;p.cursor=QTextCursor(w->textDoc());
 const bool selected=state.value(QStringLiteral("selected")).toBool(),reverse=state.value(QStringLiteral("reverse")).toBool();
 p.cursor.setPosition(selected?(reverse?end:start):end);if(selected)p.cursor.setPosition(reverse?start:end,QTextCursor::KeepAnchor);
 p.position=p.cursor.position();p.anchor=p.cursor.anchor();p.article=state.value(QStringLiteral("article")).toMap();p.slides=state.value(QStringLiteral("slides")).toMap();p.references=state.value(QStringLiteral("references")).toMap();p.x=state.value(QStringLiteral("x")).toInt();p.y=state.value(QStringLiteral("y")).toInt();p.pinnedScroll=p.references.value(QStringLiteral("pinScroll")).toInt();p.readingScroll=p.references.value(QStringLiteral("scroll")).toInt();
 p.viewportRange=state.value(QStringLiteral("viewportRange")).toMap();p.viewportY=state.value(QStringLiteral("viewportY")).toInt();
 if(p.viewportRange.isEmpty()){p.viewportCursor=p.cursor;p.viewportCursor.clearSelection();p.viewportY=state.value(QStringLiteral("cursorY")).toInt();}
 if(activate){w->resize(QSize(state.value(QStringLiteral("width"),900).toInt(),state.value(QStringLiteral("height"),700).toInt()).boundedTo(w->screen()->availableGeometry().size()-QSize(16,32)).expandedTo(QSize(360,280)));
 if(w->property("researchFocus").toBool()!=state.value(QStringLiteral("focus")).toBool())setFocusMode(state.value(QStringLiteral("focus")).toBool());}
 p.pdfFile=state.value(QStringLiteral("pdf")).toString();p.zoom=state.value(QStringLiteral("zoom"),1).toDouble();p.pdfCenter={state.value(QStringLiteral("pdfX")).toDouble(),state.value(QStringLiteral("pdfY")).toDouble()};
 if(activate&&QFileInfo(p.pdfFile).isFile()){
   if(!w->pdfDocument()&&previewFile(w->textDoc()->getRootFilePath())==p.pdfFile)w->openPdfIfAvailable(false);
   p.pdf=w->pdfDocument()&&w->pdfDocument()->fileName()==p.pdfFile?w->pdfDocument():PDFDocumentWindow::findDocument(p.pdfFile);
   if(!p.pdf)p.pdf=new PDFDocumentWindow(p.pdfFile,w);
   if(state.value(QStringLiteral("pdfVisible")).toBool())p.pdf->show();
 }else if(!activate&&w->pdfDocument()&&QFileInfo(w->pdfDocument()->fileName())==QFileInfo(p.pdfFile))p.pdf=w->pdfDocument();
 if(!restorePlace(p,activate))return false;
 if(activate||!w->property("researchFocus").toBool())w->restoreState(QByteArray::fromBase64(state.value(QStringLiteral("layout")).toByteArray()));
 if(activate){w->selectWindow();w->editor()->setFocus();}return true;
}
bool restoreWritingPlace(const QVariantMap &state,QString *error){
 const auto file=state.value(QStringLiteral("file")).toString();auto *w=TeXDocumentWindow::findDocument(file);
 if(!w&&QFileInfo(file).isFile())w=TeXDocumentWindow::openDocument(file,false,false);
 return restoreWritingPlaceInWindow(w,state,error,true);
}
bool restoreRecentWritingPlace(TeXDocumentWindow *window,const QVariantMap &state,QString *error){return restoreWritingPlaceInWindow(window,state,error,false);}
void installReadingNavigation(QMainWindow *w){
  if(w->findChild<QAction*>(QStringLiteral("researchNavigateBack")))return;
  for(bool forward:{false,true}){auto *action=new QAction(ui(forward?QStringLiteral("前进到阅读位置"):QStringLiteral("返回阅读位置")),w);action->setObjectName(forward?QStringLiteral("researchNavigateForward"):QStringLiteral("researchNavigateBack"));action->setShortcut(QKeySequence(forward?QStringLiteral("Alt+Right"):QStringLiteral("Alt+Left")));w->addAction(action);readingActions.append(action);
    if(auto *menu=w->findChild<QMenu*>(QStringLiteral("researchViewMenu")))menu->addAction(action);
    QObject::connect(action,&QAction::triggered,w,[w,forward]{
      auto &from=forward?forwardPlaces:backPlaces;auto &to=forward?backPlaces:forwardPlaces;if(from.isEmpty())return;
      const auto current=qobject_cast<TeXDocumentWindow*>(w)?place(qobject_cast<TeXDocumentWindow*>(w)):place(qobject_cast<PDFDocumentWindow*>(w));
      QScopedValueRollback<bool> restoring(restoringReading,true);
      while(!from.isEmpty()){
        const auto target=from.takeLast();if(sameReadingPlace(target,current))continue;
        const bool hideDialog=current.readingDialog&&current.readingDialog!=target.readingDialog;
        if(hideDialog)current.readingDialog->hide();
        if(restorePlace(target)){to.append(current);while(to.size()>64)to.removeFirst();break;}
        if(hideDialog&&current.readingDialog)current.readingDialog->show();
      }
      updateReadingActions();
    });
    QObject::connect(TWApp::instance(),&TWApp::updatedTranslators,action,[action,forward]{action->setText(ui(forward?QStringLiteral("前进到阅读位置"):QStringLiteral("返回阅读位置")));});
  }updateReadingActions();
}
#include "ResearchProjectFiles.inc"
void installProjectWriting(TeXDocumentWindow *w){if(controller(w))return;new WritingController(w);installProjectFiles(w);installProjectTools(w);QObject::connect(w->textDoc(),&QTextDocument::contentsChange,w,[w](int,int removed,int added){if(!removed&&!added)return;++projectEpoch;for(auto *other:TeXDocumentWindow::documentList())if(other!=w)if(auto *c=controller(other);c&&(c->busy||c->popup->isVisible())){c->invalidate();c->popup->hide();if(!c->composing&&c->mode()&&(c->manualSession||c->mode()==2))c->timer.start(c->manualSession?0:300);}});installReadingNavigation(w);}

void openProjectNavigation(TeXDocumentWindow *w){
  // Heap ownership allows the document to close during a queued/modal request.
  // Workers retain only immutable input and their independent cancellation epoch.
  QPointer<MaterialDialog> owned(new MaterialDialog(w));auto &dialog=*owned;
  QPointer<TeXDocumentWindow> owner(w);
  dialog.setObjectName(QStringLiteral("researchPalette"));dialog.setWindowTitle(ui(QStringLiteral("结构")));dialog.resize(760,470);
  auto *layout=new QVBoxLayout(&dialog);layout->setContentsMargins(18,18,18,12);layout->setSpacing(12);
  auto *query=new QLineEdit(&dialog);query->setObjectName(QStringLiteral("researchNavigationQuery"));query->setPlaceholderText(ui(QStringLiteral("搜索文件、章节、定理、公式编号或标签…")));
  auto *checks=new QCheckBox(ui(QStringLiteral("检查标签与文献")),&dialog);checks->setObjectName(QStringLiteral("researchNavigationChecks"));
  auto *top=new QHBoxLayout;top->addWidget(query,1);top->addWidget(checks);layout->addLayout(top);
  auto *list=new QListView(&dialog);list->setObjectName(QStringLiteral("researchNavigationResults"));list->setUniformItemSizes(true);auto *model=new ProjectRows(list);list->setModel(model);
  auto *hint=new QLabel(ui(QStringLiteral("正在读取论文结构…")),&dialog);hint->setObjectName(QStringLiteral("researchNavigationHint"));hint->setWordWrap(true);layout->addWidget(list);layout->addWidget(hint);
  QSharedPointer<const ProjectIndex> index;QTimer filter;filter.setSingleShot(true);filter.setInterval(50);quint64 generation=0;
  QFileSystemWatcher watcher;QTimer refresh;refresh.setSingleShot(true);refresh.setInterval(180);QString activeRoot;
  const auto requestEpoch=std::make_shared<std::atomic<quint64>>(0);QPointer<QObject> guard(&dialog);
  QObject::connect(&dialog,&QDialog::finished,&dialog,[requestEpoch]{requestEpoch->fetch_add(1);});
  QObject::connect(&dialog,&QObject::destroyed,qApp,[requestEpoch]{requestEpoch->fetch_add(1);});
  auto apply=[&]{
    if(!index||!guard)return;dialog.setProperty("researchNavigationBusy",false);const auto results=searchProject(*index,query->text(),120,checks->isChecked()?3:0);model->replace(index,results);if(!results.isEmpty())list->setCurrentIndex(model->index(0));
    if(checks->isChecked())hint->setText(ui(index->issuesTruncated||index->truncated?QStringLiteral("部分检查结果。宏和动态内容请编译检查。"):index->issues.isEmpty()?QStringLiteral("未发现标签或文献问题。动态内容请编译检查。"):QStringLiteral("Enter 跳转。宏和动态内容请编译检查。")));
    else hint->setText(ui(index->truncated?QStringLiteral("结果过多，请缩小查找范围。"):QStringLiteral("↑↓ 选择 · Enter 跳转 · Alt+← 返回阅读位置")));
    auto paths=index->dependencies;paths.removeDuplicates();const auto old=watcher.files()+watcher.directories();if(!old.isEmpty())watcher.removePaths(old);QStringList present;
    for(const auto &path:paths)if(QFileInfo(path).exists())present.append(path);if(!present.isEmpty())watcher.addPaths(present);
  };
  auto requestIndex=[&]{
    if(!owner||!guard)return;dialog.setProperty("researchNavigationBusy",true);++generation;index.reset();model->replace({},{});hint->setText(ui(checks->isChecked()?QStringLiteral("正在检查标签与文献…"):QStringLiteral("正在读取论文结构…")));
    const auto epoch=projectEpoch;const auto ticket=requestEpoch->fetch_add(1)+1;const bool citations=checks->isChecked();
    const QString root=completionProjectRoot(owner);activeRoot=root;
    auto current=dirtyBuffers();if(owner->untitled())current.insert(root,owner->editor()->toPlainText());
    projectPool().start(QRunnable::create([root,current,epoch,ticket,citations,requestEpoch,guard,&index,apply]{
      auto cancelled=[requestEpoch,ticket]{return requestEpoch->load()!=ticket;};auto value=cachedProject(root,current,citations,cancelled);if(!qApp)return;
      QMetaObject::invokeMethod(qApp,[guard,value,epoch,ticket,requestEpoch,&index,apply]{if(!guard||!value||projectEpoch!=epoch||requestEpoch->load()!=ticket)return;index=value;apply();},Qt::QueuedConnection);
    }));
  };
  QObject::connect(&refresh,&QTimer::timeout,&dialog,requestIndex);
  auto schedule=[&]{requestEpoch->fetch_add(1);++generation;index.reset();model->replace({},{});dialog.setProperty("researchNavigationBusy",true);hint->setText(ui(checks->isChecked()?QStringLiteral("正在检查标签与文献…"):QStringLiteral("正在读取论文结构…")));refresh.start();};
  QObject::connect(&watcher,&QFileSystemWatcher::fileChanged,&dialog,[&](const QString &){schedule();});
  QObject::connect(&watcher,&QFileSystemWatcher::directoryChanged,&dialog,[&](const QString &){schedule();});
  QSet<QObject*> documents;
  auto watchDocuments=[&]{for(auto *window:TeXDocumentWindow::documentList()){auto *doc=window->textDoc();if(documents.contains(doc))continue;documents.insert(doc);
    QObject::connect(doc,&QTextDocument::contentsChange,&dialog,[&](int,int removed,int added){if(removed||added)schedule();});
    QObject::connect(doc,&QObject::destroyed,&dialog,[&,doc]{documents.remove(doc);schedule();});
  }};
  watchDocuments();QObject::connect(TWApp::instance(),&TWApp::windowListChanged,&dialog,[&]{watchDocuments();schedule();});
  QObject::connect(checks,&QCheckBox::toggled,&dialog,[&]{refresh.stop();requestIndex();});
  QObject::connect(&filter,&QTimer::timeout,&dialog,[&]{
    if(!index)return;const auto ticket=++generation;const auto data=index;const QString search=query->text();const int type=checks->isChecked()?3:0;
    projectPool().start(QRunnable::create([data,search,type,ticket,guard,model,list,&generation]{auto rows=searchProject(*data,search,120,type);if(!qApp)return;QMetaObject::invokeMethod(qApp,[data,rows,ticket,guard,model,list,&generation]{if(!guard||ticket!=generation)return;model->replace(data,rows);if(!rows.isEmpty())list->setCurrentIndex(model->index(0));},Qt::QueuedConnection);}));
  });
  QObject::connect(query,&QLineEdit::textChanged,&dialog,[&]{++generation;model->replace({},{});filter.start();});
  QObject::connect(list,&QListView::activated,&dialog,[&](const QModelIndex &){dialog.accept();});
  QObject::connect(query,&QLineEdit::returnPressed,&dialog,[&]{if(list->currentIndex().isValid())dialog.accept();});
  struct Keys:QObject{QListView *list;Keys(QListView *l,QLineEdit *q):QObject(q),list(l){}bool eventFilter(QObject *,QEvent *e)override{if(e->type()==QEvent::KeyPress){auto *k=static_cast<QKeyEvent*>(e);if(k->key()==Qt::Key_Down||k->key()==Qt::Key_Up){int row=qBound(0,list->currentIndex().row()+(k->key()==Qt::Key_Down?1:-1),list->model()->rowCount()-1);list->setCurrentIndex(list->model()->index(row,0));return true;}}return false;}};
  query->installEventFilter(new Keys(list,query));requestIndex();query->setFocus();
  const int result=dialog.exec();
  if(owned&&owner&&result==QDialog::Accepted&&list->currentIndex().isValid()&&model->project){
    if(const auto *entry=projectRow(*model->project,model->rows[list->currentIndex().row()])){auto e=*entry;if(owner->untitled()&&e.file==activeRoot)e.file.clear();jumpEntry(owner,e);}
  }
  if(owned)delete owned.data();
}
}
