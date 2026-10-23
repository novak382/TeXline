// Deterministic checks shared by the application's isolated self-test.
#pragma once
#include "ResearchMerge.h"
#include "ResearchBuild.h"
#include "Engine.h"
#include <QRandomGenerator>
#include <QSaveFile>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QDateTime>

namespace Research {
template <class Check, class Put>
void collaborationSelfTests(const QDir &dir, Check check, Put put) {
  const QString base=QStringLiteral("alpha\nbeta\ngamma\n");
  auto result=mergeText(base,QStringLiteral("LOCAL alpha\nbeta\ngamma\n"),QStringLiteral("alpha\nbeta\nREMOTE gamma\n"));
  check(QStringLiteral("disjoint external edits merge with local draft"),result.conflicts.isEmpty()&&result.text==QStringLiteral("LOCAL alpha\nbeta\nREMOTE gamma\n"));
  result=mergeText(base,QStringLiteral("alpha\nLOCAL\ngamma\n"),QStringLiteral("alpha\nREMOTE\ngamma\n"));
  check(QStringLiteral("overlapping edits retain both alternatives"),result.conflicts.size()==1&&result.text==QStringLiteral("alpha\nLOCAL\ngamma\n")&&result.conflicts[0].remote==QStringLiteral("REMOTE"));
  result=mergeText(base,QStringLiteral("alpha\nSAME\ngamma\n"),QStringLiteral("alpha\nSAME\ngamma\n"));
  check(QStringLiteral("identical concurrent edits are deduplicated"),result.conflicts.isEmpty()&&result.text==QStringLiteral("alpha\nSAME\ngamma\n"));
  result=mergeText(QStringLiteral("abcd\n"),QStringLiteral("Abcd\n"),QStringLiteral("abcD\n"));
  check(QStringLiteral("separate edits within one line merge"),result.conflicts.isEmpty()&&result.text==QStringLiteral("AbcD\n"));
  result=mergeText(base,QStringLiteral("intro\nalpha\nbeta\ngamma\n"),QStringLiteral("alpha\nbeta\ngamma\nend\n"));
  check(QStringLiteral("insertions at separate boundaries merge"),result.conflicts.isEmpty()&&result.text==QStringLiteral("intro\nalpha\nbeta\ngamma\nend\n"));
  result=mergeText(QStringLiteral("a\nb\nc\nd\n"),QStringLiteral("a\nc\nd\n"),QStringLiteral("a\nb\nc\nD\n"));
  check(QStringLiteral("deletion and separate replacement merge"),result.conflicts.isEmpty()&&result.text==QStringLiteral("a\nc\nD\n"));
  result=mergeText(QStringLiteral("x\n"),QStringLiteral("one\nx\n"),QStringLiteral("two\nx\n"));
  check(QStringLiteral("competing insertions keep both versions for review"),result.conflicts.size()==1&&result.text==QStringLiteral("one\nx\n"));
  QRandomGenerator random(719043);bool roundTrips=true,ordered=true,utf16=true;
  const QStringList tokens{QStringLiteral("a"),QStringLiteral("b"),QStringLiteral("\n"),QStringLiteral("中文"),QString::fromUtf8("Σ🙂"),QStringLiteral("\\label{eq:a}")};
  for(int iteration=0;iteration<2000;++iteration) {
    QString before,after;for(int i=0,n=random.bounded(80);i<n;++i)before+=tokens[random.bounded(int(tokens.size()))];
    after=before;
    for(int i=0,n=random.bounded(1,5);i<n;++i){int start=random.bounded(int(after.size())+1);if(start<after.size()&&after[start].isLowSurrogate())--start;int end=qMin(int(after.size()),start+random.bounded(12));if(end<after.size()&&after[end].isLowSurrogate())++end;after.replace(start,end-start,tokens[random.bounded(int(tokens.size()))]);}
    const auto edits=textEdits(before,after);QString reconstructed=before;int end=0;
    for(const auto&e:edits){ordered&=e.start>=end&&e.end>=e.start&&e.end<=before.size();end=e.end;utf16&=!(e.start<before.size()&&before[e.start].isLowSurrogate())&&!(e.end<before.size()&&before[e.end].isLowSurrogate());}
    for(auto i=edits.crbegin();i!=edits.crend();++i)reconstructed.replace(i->start,i->end-i->start,i->text);
    roundTrips&=reconstructed==after;
  }
  check(QStringLiteral("2000 seeded Unicode diff round trips"),roundTrips);
  check(QStringLiteral("diff ranges remain ordered and within the original"),ordered);
  check(QStringLiteral("diff boundaries never split a surrogate pair"),utf16);
  const QString large=QStringLiteral("old line\n").repeated(30000),rewritten=QStringLiteral("new line\n").repeated(30000);
  const auto edits=textEdits(large,rewritten);QString restored=large;for(auto i=edits.crbegin();i!=edits.crend();++i)restored.replace(i->start,i->end-i->start,i->text);
  check(QStringLiteral("large rewrites use bounded diff memory"),restored==rewritten&&edits.size()==1);
  check(QStringLiteral("review navigation identifies mathematical context"),changeCategory(QStringLiteral("\\section{Theory}\n\\begin{proof}\nDetails"),37).contains(QStringLiteral("proof")));

  setBuildCacheForTests(dir.filePath(QStringLiteral("local-build")));
  const QString root=put(QStringLiteral("shared/paper.tex"),QStringLiteral("\\documentclass{article}\n\\input{sections/proof}\n\\bibliography{refs}\n"));
  put(QStringLiteral("shared/sections/proof.tex"),QStringLiteral("Original proof."));
  const QString bib=put(QStringLiteral("shared/refs.bib"),QStringLiteral("@article{key,title={Original}}"));
  Engine engine(QStringLiteral("pdfLaTeX"),QStringLiteral("latexmk.exe"),{QStringLiteral("-pdf"),QStringLiteral("$fullname")},true);
  auto job=prepareBuild(root,engine);
  check(QStringLiteral("build cache is outside the shared manuscript folder"),job.managed&&!job.outputDir.isEmpty()&&!job.outputDir.startsWith(QFileInfo(root).absolutePath()+QLatin1Char('/')));
  check(QStringLiteral("nested includes and bibliography are versioned"),job.inputs.size()==3&&QDir(job.outputDir+QStringLiteral("/sections")).exists());
  const QString stem=job.outputDir+QStringLiteral("/paper");
  put(QDir(dir).relativeFilePath(stem+QStringLiteral(".pdf")),QStringLiteral("%PDF-1.7\nfixture-v1"));
  put(QDir(dir).relativeFilePath(stem+QStringLiteral(".synctex")),QStringLiteral("fixture sync v1"));
  check(QStringLiteral("successful output promotes a complete PDF and sync pair"),finishBuild(job,true)==1&&pdfPairValid(job.previewFile));
  check(QStringLiteral("local preview resolves its actual source root"),previewFile(root)==job.previewFile&&pdfSourceRoot(job.previewFile)==root);
  QString error;check(QStringLiteral("explicit publication creates a versioned shared PDF"),publishPreview(root,&error)&&pdfRevision(QFileInfo(root).absoluteDir().filePath(QStringLiteral("paper.pdf")))==PdfRevision::Current);
  const auto sharedMeta=pdfMetadata(QFileInfo(root).absoluteDir().filePath(QStringLiteral("paper.pdf")));
  check(QStringLiteral("published metadata preserves relative dependencies and certified SyncTeX"),sharedMeta.value(QStringLiteral("source")).toString()==QStringLiteral("paper.tex")&&sharedMeta.value(QStringLiteral("inputs")).toObject().contains(QStringLiteral("refs.bib"))&&sharedMeta.value(QStringLiteral("syncHash")).toString().size()==64&&pdfPairValid(QFileInfo(root).absoluteDir().filePath(QStringLiteral("paper.pdf"))));
  put(QStringLiteral("shared/refs.bib"),QStringLiteral("@article{key,title={Changed}}"));
  check(QStringLiteral("bibliography changes mark the PDF stale and disable old sync"),pdfRevision(job.previewFile)==PdfRevision::Stale&&!pdfPairValid(job.previewFile));
  check(QStringLiteral("source changes during compilation prevent promotion"),finishBuild(job,true)==-1);
  check(QStringLiteral("stale output cannot overwrite the shared PDF"),!publishPreview(root,&error));
  put(QStringLiteral("shared/refs.bib"),QStringLiteral("@article{key,title={Original}}"));
  put(QDir(dir).relativeFilePath(stem+QStringLiteral(".pdf")),QStringLiteral("%PDF-1.7\nfailed-build"));
  QFile preserved(job.previewFile);preserved.open(QIODevice::ReadOnly);const QByteArray lastGood=preserved.readAll();preserved.close();
  check(QStringLiteral("failed compilation preserves the last complete preview"),finishBuild(job,false)==0&&preserved.open(QIODevice::ReadOnly)&&preserved.readAll()==lastGood);preserved.close();
  put(QDir(dir).relativeFilePath(job.previewFile),QStringLiteral("%PDF-1.7\nmismatched"));
  check(QStringLiteral("a replaced PDF cannot use a mismatched sync file"),pdfRevision(job.previewFile)==PdfRevision::Unpaired&&!pdfPairValid(job.previewFile));
  Engine custom(QStringLiteral("Custom"),QStringLiteral("my-builder.exe"),{},true);
  check(QStringLiteral("custom tools retain their configured output behavior"),!prepareBuild(root,custom).managed);
  const QString largeRoot=put(QStringLiteral("large/library.tex"),QStringLiteral("\\documentclass{article}\n\\bibliography{library}\n"));
  const QString largeBib=put(QStringLiteral("large/library.bib"),QStringLiteral("% padding\n").repeated(450000));
  const auto largeJob=prepareBuild(largeRoot,engine);const QString largeStem=largeJob.outputDir+QStringLiteral("/library");
  put(QDir(dir).relativeFilePath(largeStem+QStringLiteral(".pdf")),QStringLiteral("%PDF-1.7\nlarge-library"));
  put(QDir(dir).relativeFilePath(largeStem+QStringLiteral(".synctex")),QStringLiteral("large sync"));
  check(QStringLiteral("a large text dependency has a current verified preview"),finishBuild(largeJob,true)==1&&pdfPairValid(largeJob.previewFile));
  const auto originalTime=QFileInfo(largeBib).lastModified();QFile changed(largeBib);
  bool changedSameStamp=changed.open(QIODevice::ReadWrite)&&changed.seek(changed.size()-1)&&changed.write("X",1)==1&&changed.flush()&&changed.setFileTime(originalTime,QFileDevice::FileModificationTime);changed.close();
  changedSameStamp&=QFileInfo(largeBib).lastModified()==originalTime;
  check(QStringLiteral("large-library fixture preserves size and modification time after changing content"),changedSameStamp);
  check(QStringLiteral("large text changes cannot masquerade as a current PDF or valid source map"),changedSameStamp&&pdfRevision(largeJob.previewFile)==PdfRevision::Stale&&!pdfPairValid(largeJob.previewFile));
  setBuildCacheForTests(QString());
}
}
