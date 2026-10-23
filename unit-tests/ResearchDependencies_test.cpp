// Dependency boundary and path-precedence checks. GPL-2.0-or-later.
#include "ResearchDependencies.h"
#include <QtTest>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

class ResearchDependenciesTest : public QObject {
  Q_OBJECT
private slots:
  void lookupAndBoundaries() {
    QTemporaryDir temporary;QVERIFY(temporary.isValid());const QDir dir(temporary.path());
    auto put=[&](const QString &name){const auto path=dir.filePath(name);QDir().mkpath(QFileInfo(path).absolutePath());QFile f(path);if(!f.open(QIODevice::WriteOnly))return QString();f.write("fixture");f.close();return QFileInfo(path).canonicalFilePath();};
    const auto root=put(QStringLiteral("paper/main.tex")),child=put(QStringLiteral("paper/chapters/child.tex"));
    const auto local=put(QStringLiteral("paper/refs.bib"));put(QStringLiteral("paper/chapters/refs.bib"));
    QCOMPARE(Research::resolveDependency(QStringLiteral("refs"),child,root,QStringLiteral(".bib")),local);
    QCOMPARE(Research::resolveDependency(QStringLiteral("refs.bib"),child,root,QStringLiteral(".bib")),local);
    const auto outside=put(QString::fromUtf8("shared library 中文/library.bib"));
    const QByteArray previous=qgetenv("BIBINPUTS");const bool hadPrevious=qEnvironmentVariableIsSet("BIBINPUTS");
    qputenv("BIBINPUTS",(QFileInfo(outside).absolutePath()+QDir::listSeparator()).toLocal8Bit());
    QCOMPARE(Research::resolveDependency(QStringLiteral("library"),child,root,QStringLiteral(".bib")),outside);
    QCOMPARE(Research::resolveDependency(outside,child,root,QStringLiteral(".bib")),outside);
    const auto replacement=put(QStringLiteral("other library/library.bib"));
    qputenv("BIBINPUTS",QFileInfo(replacement).absolutePath().toLocal8Bit());
    QCOMPARE(Research::resolveDependency(QStringLiteral("library"),child,root,QStringLiteral(".bib")),replacement);
    if(hadPrevious)qputenv("BIBINPUTS",previous);else qunsetenv("BIBINPUTS");
    QVERIFY(Research::resolveDependency(QStringLiteral("\\macro/path"),child,root,QStringLiteral(".tex")).isEmpty());
    QVERIFY(Research::resolveDependency(QStringLiteral("#1"),child,root,QStringLiteral(".tex")).isEmpty());
    QVERIFY(Research::resolveDependency(QStringLiteral(""),child,root,QStringLiteral(".tex")).isEmpty());
    QVERIFY(Research::authoredDependency(outside,root));
    QVERIFY(!Research::authoredDependency(QString(),root));
    const auto generated=put(QStringLiteral("cache/main.bbl"));
    QVERIFY(!Research::authoredDependency(generated,root,dir.filePath(QStringLiteral("cache"))));
    const auto installed=put(QStringLiteral("texmf-dist/tex/latex/base/article.cls"));
    QVERIFY(!Research::authoredDependency(installed,root));
    const auto authorClass=put(QStringLiteral("shared/local.cls"));QVERIFY(Research::authoredDependency(authorClass,root));
    const auto privateData=put(QStringLiteral("shared/unrelated.sqlite"));QVERIFY(!Research::authoredDependency(privateData,root));
  }
};
QTEST_GUILESS_MAIN(ResearchDependenciesTest)
#include "ResearchDependencies_test.moc"
