#include "utils/FilePath.h"
// TeXworks Research workflow extensions. GPL-2.0-or-later.
#include "ResearchWorkflow.h"
#include "ResearchWritingContext.h"
#include "ResearchOutputPolicy.h"
#include "ResearchProject.h"
#include "ResearchArticle.h"
#include "ResearchSlides.h"
#include "ResearchMerge.h"
#include "ResearchDependencies.h"
#include "ResearchAppearance.h"
#include "ResearchCollaborationTests.h"
#include "TeXDocumentWindow.h"
#include "CompletingEdit.h"
#include "Settings.h"
#include <QApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QKeyEvent>
#include <QListWidget>
#include <QMenuBar>
#include <QMenu>
#include <QMap>
#include <QMessageBox>
#include <QMutex>
#include <QPushButton>
#include <QPointer>
#include <QRunnable>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStatusBar>
#include <QTextCodec>
#include <QTextBrowser>
#include <QTimer>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>
#include <QToolBar>
#include <QToolButton>
#include <QWidgetAction>
#include <QUuid>
#include <atomic>
#include <QVBoxLayout>
#include <algorithm>
#include <memory>
#include <utility>

namespace Research {
static QString key(const QString &s) { return QString::fromLatin1(QCryptographicHash::hash(s.toUtf8(), QCryptographicHash::Sha256).toHex()); }
static QString canonical(const QString &p) { return Tw::Utils::canonicalSourcePath(p); }
static QString source(const QString &p) {
  if (auto *w = TeXDocumentWindow::findDocument(p)) return w->textDoc()->sourceText();
  QFile f(p); if (!f.open(QIODevice::ReadOnly) || f.size() > 4 * 1024 * 1024) return {};
  return QString::fromUtf8(f.readAll());
}
static QString uncomment(QString s) {
  // Keep positions and newlines unchanged, including escaped percent signs.
  for (qsizetype i = 0; i < s.size(); ++i) {
    if (s[i] == QLatin1Char('\\') && QStringView(s).mid(i, 5) == QLatin1String("\\verb")) {
      qsizetype slashes = 0;
      for (qsizetype j = i; j > 0 && s[j-1] == QLatin1Char('\\'); --j) ++slashes;
      qsizetype delimiter = i + 5;
      const bool starred = delimiter < s.size() && s[delimiter] == QLatin1Char('*');
      if (starred) ++delimiter;
      if (!(slashes % 2) && delimiter < s.size() && !s[delimiter].isSpace() &&
          (starred || !s[delimiter].isLetter())) {
        const QChar end = s[delimiter];
        qsizetype last = delimiter + 1;
        while (last < s.size() && s[last] != end && s[last] != QLatin1Char('\n') && s[last] != QLatin1Char('\r')) ++last;
        if (last < s.size() && s[last] == end) ++last;
        // Literal percent signs are not comments; keep scanning compiled text afterwards.
        while (i < last) s[i++] = QLatin1Char(' ');
        --i;
        continue;
      }
    }
    if (s[i] == QLatin1Char('%')) {
      qsizetype n = 0; for (qsizetype j = i; j > 0 && s[j-1] == QLatin1Char('\\'); --j) ++n;
      if (n % 2) continue;
      while (i < s.size() && s[i] != QLatin1Char('\n')) s[i++] = QLatin1Char(' ');
    }
  }
  static const QRegularExpression literalBlock(QStringLiteral(R"(\\begin\s*\{(verbatim\*?|Verbatim|lstlisting|minted|comment|filecontents\*?)\}[\s\S]*?\\end\s*\{\1\})"));
  auto blocks = literalBlock.globalMatch(s);
  while (blocks.hasNext()) {
    const auto m = blocks.next();
    for (qsizetype i = m.capturedStart(); i < m.capturedEnd(); ++i) if (s[i] != QLatin1Char('\n')) s[i] = QLatin1Char(' ');
  }
  return s;
}
QString compiledTexText(QString text) { return uncomment(std::move(text)); }

struct SourceAnalysis {
  QString text, engine;
  bool main = false;
  QStringList includes;
};
static SourceAnalysis analyzeSource(const QString &text) {
  // Cache pure facts, not a final project/root decision: included files and
  // manual choices must still be consulted on every request. Keep one input
  // per thread and no masked whole-document copy after the analysis returns.
  static thread_local SourceAnalysis cached;
  static thread_local bool valid = false;
  if (valid && cached.text == text) {
    // An edit followed by undo can produce equal text in a new allocation.
    // Share the document's current snapshot instead of retaining both copies.
    cached.text = text;
    return cached;
  }
  SourceAnalysis result;
  const QString code = uncomment(text);
  static const QRegularExpression documentClass(QStringLiteral(R"(\\documentclass\s*(?:\[[^\]]*\]\s*)?\{([^{}]+)\})"));
  static const QRegularExpression include(QStringLiteral(R"(\\(?:input|include|subfile)(?![A-Za-z@])\s*(?:\{([^{}]+)\}|([^\s%{}]+)))"));
  static const QRegularExpression unicodeClass(QStringLiteral(R"(\\documentclass\s*(?:\[[^\]]*\]\s*)?\{\s*ctex(?:art|book|rep|beamer)\s*\})"));
  static const QRegularExpression unicodePackage(QStringLiteral(R"(\\(?:usepackage|RequirePackage)\s*(?:\[[^\]]*\]\s*)?\{[^{}]*\b(?:ctex|xeCJK|fontspec|unicode-math)\b[^{}]*\})"));
  static const QRegularExpression unicodeCommands(QStringLiteral(R"(\\(?:setmainfont|setsansfont|setmonofont|setCJKmainfont|setmathfont)\b)"));
  static const QRegularExpression cjk(QStringLiteral(R"([\p{Han}\p{Hiragana}\p{Katakana}\p{Hangul}])"));
  const auto classMatch = documentClass.match(code);
  result.main = classMatch.hasMatch() && classMatch.captured(1).trimmed() != QStringLiteral("subfiles");
  result.engine = unicodeClass.match(code).hasMatch() || unicodePackage.match(code).hasMatch() ||
      unicodeCommands.match(code).hasMatch() || cjk.match(code).hasMatch()
    ? QStringLiteral("XeLaTeX") : QStringLiteral("pdfLaTeX");
  auto matches = include.globalMatch(code);
  while (matches.hasNext()) {
    const auto m = matches.next();
    result.includes.append((m.captured(1).isEmpty() ? m.captured(2) : m.captured(1)).trimmed());
  }
  // Bound retained text to 16 MiB. Oversized sources remain correctly parsed.
  valid = text.size() <= 8 * 1024 * 1024;
  cached = valid ? result : SourceAnalysis();
  if (valid) cached.text = text;
  return result;
}
static QStringList children(const QString &file, const QString &rootDir, const QString &text) {
  QStringList result;
  for (const QString &name : analyzeSource(text).includes) {
    if (name.contains(QLatin1Char('\\')) || name.contains(QLatin1Char('#'))) continue;
    const QString p=resolveDependency(name,file,QDir(rootDir).filePath(QStringLiteral("main.tex")),QStringLiteral(".tex"));
    if(!p.isEmpty()&&authoredDependency(p,QDir(rootDir).filePath(QStringLiteral("main.tex")))&&!result.contains(p))result.append(p);
  }
  return result;
}
static QStringList projectFilesWithSource(const QString &root,const std::function<QString(const QString&)> &read,const std::function<bool()> &cancelled) {
  QStringList result, todo{canonical(root)}; QSet<QString> seen;
  const QString dir = QFileInfo(root).absolutePath();
  while (!todo.isEmpty() && result.size() < 250) {
    if(cancelled&&cancelled())return {};
    QString file = todo.takeFirst(); if (seen.contains(file)) continue;
    seen.insert(file); if (!QFileInfo(file).isFile()) continue;
    result.append(file); todo.append(children(file, dir, read(file)));
  }
  return result;
}
QStringList projectFiles(const QString &root){return projectFilesWithSource(root,source,{});}
QStringList projectFiles(const QString &root,const QHash<QString,QString> &sources,const std::function<bool()> &cancelled){
  return projectFilesWithSource(root,[&sources](const QString &path){
    const auto found=sources.constFind(path);if(found!=sources.cend())return found.value();
    QFile file(path);if(!file.open(QIODevice::ReadOnly)||file.size()>4*1024*1024)return QString();return QString::fromUtf8(file.readAll());
  },cancelled);
}

static bool scanFolder(const QDir &dir, QStringList &files, int &visited, int depth = 0,
                       const std::function<bool()> &cancelled = {}) {
  if (cancelled && cancelled()) return false;
  if (depth > 5) return true;
  for (const QFileInfo &f : dir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot | QDir::NoSymLinks)) {
    if (cancelled && cancelled()) return false;
    if (++visited > 2000) return false;
    if (f.isDir()) {
      if (f.fileName().startsWith(QLatin1Char('.')) || QStringList{QStringLiteral("node_modules"), QStringLiteral("build"), QStringLiteral(".git")}.contains(f.fileName())) continue;
      if (!scanFolder(QDir(f.absoluteFilePath()), files, visited, depth + 1, cancelled)) return false;
    } else if (f.suffix().compare(QStringLiteral("tex"), Qt::CaseInsensitive) == 0) {
      files.append(f.canonicalFilePath()); if (files.size() > 250) return false;
    }
  }
  return true;
}
QStringList rootCandidates(const QString &file, const QString &text) {
  if (file.isEmpty()) return {};
  auto isMain = [](const QString &s) { return analyzeSource(s).main; };
  if (isMain(text)) return {canonical(file)};
  QDir dir(QFileInfo(file).absolutePath()); const QString target = canonical(file);
  for (int level = 0; level < 3 && !dir.isRoot(); ++level) {
    QStringList files, roots;
    int visited = 0;
    if (!scanFolder(dir, files, visited)) break; // Never guess from an incomplete scan.
    for (const QString &candidate : files) {
      if (candidate == target || !isMain(source(candidate))) continue;
      if (projectFiles(candidate).contains(target)) roots.append(candidate);
    }
    if (!roots.isEmpty()) { roots.removeDuplicates(); roots.sort(); return roots; }
    if (!dir.cdUp()) break;
  }
  return {};
}
void rememberRoot(const QString &file, const QString &root) {
  Tw::Settings s; s.setValue(QStringLiteral("researchRoots/") + key(canonical(file)), canonical(root));
}
static bool unambiguousMainPreamble(const QString &text) {
  // A first compiled token consisting of a complete, plain class declaration
  // cannot be hidden by a later literal block. Only accept this positive proof;
  // macros, inline literals, complex options and longer headers use full parsing.
  const QString preamble = text.left(4096);
  qsizetype position = 0;
  while (position < preamble.size()) {
    if (preamble.at(position).isSpace()) { ++position; continue; }
    if (preamble.at(position) != QLatin1Char('%')) break;
    position = preamble.indexOf(QLatin1Char('\n'), position);
    if (position < 0) return false;
  }
  if (!preamble.mid(position, 14).startsWith(QStringLiteral("\\documentclass"))) return false;
  static const QRegularExpression declaration(QStringLiteral(R"(\\documentclass\s*(?:\[[^\]]*\]\s*)?\{([^{}]+)\})"));
  const auto match = declaration.match(preamble, position);
  if (!match.hasMatch() || match.capturedStart() != position) return false;
  const QString directive = match.captured().mid(1);
  return !directive.contains(QLatin1Char('%')) && !directive.contains(QLatin1Char('\\')) &&
      match.captured(1).trimmed() != QStringLiteral("subfiles");
}
QString mainRootFromPreamble(const QString &file, const QString &preamble) {
  if (file.isEmpty()) return {};
  const QString identity = canonical(file);
  Tw::Settings s; const QString remembered = s.value(QStringLiteral("researchRoots/") + key(identity)).toString();
  if (QFileInfo(remembered).isFile()) return remembered;
  return unambiguousMainPreamble(preamble) ? identity : QString();
}
QString resolveRoot(const QString &file, const QString &text) {
  if (file.isEmpty()) return {};
  const QString main = mainRootFromPreamble(file, text);
  if (!main.isEmpty()) return main;
  // Root queries happen during cursor/UI updates: cache bounded filesystem scans briefly.
  struct Cached { QString root; qint64 time; QString text; };
  static QHash<QString, Cached> cache;
  auto c = cache.find(file); const auto now = QDateTime::currentMSecsSinceEpoch();
  if (c != cache.end() && now - c->time < 3000 && c->text == text) {
    c->text = text;
    return c->root;
  }
  if (analyzeSource(text).main) return canonical(file);
  const QStringList roots = rootCandidates(file, text);
  const QString root = roots.size() == 1 ? roots.first() : canonical(file);
  // Bound total retained text to 16 MiB, while still caching large subfiles.
  // Changes beyond the preamble also invalidate the complete input snapshot.
  qsizetype retained = text.size();
  for (auto it = cache.cbegin(); it != cache.cend(); ++it)
    if (it.key() != file) retained += it.value().text.size();
  if (cache.size() >= 16 || retained > 8 * 1024 * 1024) cache.clear();
  if (text.size() <= 8 * 1024 * 1024) cache.insert(file, {root, now, text});
  return root;
}

QString resolveCompletionRoot(const QString &file, const QString &text,
    const QHash<QString, QString> &buffers, const std::function<bool()> &cancelled) {
  auto stopped = [&] { return cancelled && cancelled(); };
  if (file.isEmpty() || stopped()) return {};
  const QString main = mainRootFromPreamble(file, text);
  if (!main.isEmpty()) return main;
  if (analyzeSource(text).main) return canonical(file);
  QDir dir(QFileInfo(file).absolutePath()); const QString target = canonical(file);
  for (int level = 0; level < 3 && !dir.isRoot(); ++level) {
    QStringList files, roots; int visited = 0;
    if (!scanFolder(dir, files, visited, 0, cancelled)) break;
    for (const auto &candidate : files) {
      if (stopped()) return {};
      if (candidate == target) continue;
      QString contents;
      if (buffers.contains(candidate)) contents = buffers.value(candidate);
      else { QFile input(candidate); if (!input.open(QIODevice::ReadOnly) || input.size() > 4*1024*1024) continue; contents = QString::fromUtf8(input.readAll()); }
      if (analyzeSource(contents).main && projectFiles(candidate, buffers, cancelled).contains(target)) roots.append(candidate);
    }
    if (!roots.isEmpty()) { roots.removeDuplicates(); return roots.size() == 1 ? roots.first() : target; }
    if (!dir.cdUp()) break;
  }
  return stopped() ? QString() : target;
}

QString canonicalEngineName(const QString &name) {
  if (name == QStringLiteral("Paper · pdfLaTeX")) return QStringLiteral("pdfLaTeX");
  if (name == QStringLiteral("中文 Note · XeLaTeX")) return QStringLiteral("XeLaTeX");
  return name;
}
QString suggestedEngine(const QString &text) {
  return analyzeSource(text).engine;
}
static QString declaredEngine(const QString &text) {
  static const QRegularExpression program(QStringLiteral(R"(^[\t ]*%[\t ]*![\t ]*TEX[\t ]+program[\t ]*=[\t ]*([^\r\n%]+))"),
    QRegularExpression::MultilineOption | QRegularExpression::CaseInsensitiveOption);
  return canonicalEngineName(program.match(text).captured(1).trimmed());
}
void rememberEngine(const QString &file, const QString &engine) {
  if (file.isEmpty() || engine.isEmpty()) return;
  Tw::Settings s;
  s.setValue(QStringLiteral("researchEngines/") + key(canonical(file)), canonicalEngineName(engine));
}
void forgetEngine(const QString &file) {
  if (!file.isEmpty()) Tw::Settings().remove(QStringLiteral("researchEngines/") + key(canonical(file)));
}
QString preferredEngine(const QString &file, const QString &text) {
  QString declared = declaredEngine(text);
  if (!declared.isEmpty()) return declared;
  static const QRegularExpression rootDeclaration(QStringLiteral(R"(^[\t ]*%[\t ]*![\t ]*TEX[\t ]+root[\t ]*=[\t ]*([^\r\n]+))"),
    QRegularExpression::MultilineOption | QRegularExpression::CaseInsensitiveOption);
  const QString rootName = rootDeclaration.match(text).captured(1).trimmed();
  const QString declaredRoot = rootName.isEmpty() ? QString() : canonical(QDir(QFileInfo(file).absolutePath()).filePath(rootName));
  const QString root = QFileInfo(declaredRoot).isFile() ? declaredRoot : resolveRoot(file, text);
  const bool hasRoot = !root.isEmpty() && canonical(file) != canonical(root);
  const QString rootText = hasRoot ? source(root) : text;
  declared = declaredEngine(rootText);
  if (!declared.isEmpty()) return declared;
  Tw::Settings s;
  const QString manual = s.value(QStringLiteral("researchEngines/") + key(canonical(file))).toString();
  if (!manual.isEmpty()) return canonicalEngineName(manual);
  if (hasRoot) {
    const QString rootManual = s.value(QStringLiteral("researchEngines/") + key(canonical(root))).toString();
    if (!rootManual.isEmpty()) return canonicalEngineName(rootManual);
  }
  // Inspect only the already bounded include graph, never unrelated folders.
  if (hasRoot || !file.isEmpty()) {
    const QString main = hasRoot ? root : file;
    for (const auto &part : projectFiles(main)) {
      if (suggestedEngine(canonical(part) == canonical(file) ? text : source(part)) == QStringLiteral("XeLaTeX"))
        return QStringLiteral("XeLaTeX");
    }
  }
  return suggestedEngine(rootText);
}

struct Symbol { QString file; int line; QString title; };
static QList<Symbol> symbols(const QString &root) {
  QList<Symbol> result;
  const auto files = projectFiles(root);
  QMap<QString, QString> texts;
  qsizetype cachedBytes = 0;
  constexpr qsizetype cacheLimit = 8 * 1024 * 1024;
  QStringList environments = QStringLiteral("theorem|thm|lemma|lem|proposition|prop|corollary|cor|definition|defn|assumption|asm|proof|remark|example").split(QLatin1Char('|'));
  const QRegularExpression declaration(QStringLiteral(R"(\\(?:newtheorem\*?|declaretheorem)\s*(?:\[[^\]]*\]\s*)?\{([^{}]+)\})"));
  for (const auto &file : files) {
    const QString text = uncomment(source(file));
    const qsizetype bytes = text.size() * sizeof(QChar);
    if (bytes <= cacheLimit - cachedBytes) { texts.insert(file, text); cachedBytes += bytes; }
    auto matches = declaration.globalMatch(text);
    while (matches.hasNext()) environments.append(QRegularExpression::escape(matches.next().captured(1)));
  }
  environments.removeDuplicates();
  const QRegularExpression re(QStringLiteral(R"(\\(part|chapter|section|subsection|subsubsection)\*?\s*(?:\[[^\]]*\]\s*)?\{()|\\begin\s*\{(%1)\}\s*(?:\[([^\]\n]*)\])?|\\label\s*\{([^{}]+)\})").arg(environments.join(QLatin1Char('|'))));
  for (const auto &file : files) {
    const auto cached = texts.constFind(file);
    const QString text = cached == texts.constEnd() ? uncomment(source(file)) : cached.value();
    auto matches = re.globalMatch(text);
    qsizetype lineOffset = 0;
    int line = 1;
    while (matches.hasNext()) {
      auto m = matches.next();
      line += QStringView(text).mid(lineOffset, m.capturedStart() - lineOffset).count(QLatin1Char('\n'));
      lineOffset = m.capturedStart();
      QString title;
      if (!m.captured(1).isEmpty()) {
        QString heading; int depth = 1;
        for (qsizetype j = m.capturedStart(2); j < text.size() && heading.size() < 512; ++j) {
          if (text[j] == QLatin1Char('{')) ++depth;
          if (text[j] == QLatin1Char('}') && --depth == 0) break;
          heading += text[j];
        }
        title = m.captured(1) + QStringLiteral("  ") + heading.simplified();
      }
      else if (!m.captured(3).isEmpty()) {
        title = m.captured(3); if (!m.captured(4).isEmpty()) title += QStringLiteral("  ") + m.captured(4);
        else {
          QString following = text.mid(m.capturedEnd(), 160).trimmed().section(QLatin1Char('\n'), 0, 0).simplified();
          title += QStringLiteral("  ") + following.left(72);
        }
      } else title = QStringLiteral("label  ") + m.captured(5);
      result.append({file, line, title});
    }
  }
  return result;
}
static QString testStorageOverride;
static QString storage() {
  const QString dir = testStorageOverride.isEmpty() ? QCoreApplication::applicationDirPath() + QStringLiteral("/settings/research-history") : testStorageOverride;
  QDir().mkpath(dir); return dir;
}
QString writingHistoryDirectory(){return storage();}
bool hasPendingRecovery(const QString &file){return !file.isEmpty()&&QFileInfo::exists(storage()+QLatin1Char('/')+key(canonical(file))+QStringLiteral(".pending.json"));}
static QString identity(TeXDocumentWindow *w) {
  if (!w->untitled()) return key(canonical(w->textDoc()->absoluteFilePath()));
  QString id = w->property("researchRecoveryId").toString();
  if (id.isEmpty()) { id = QUuid::createUuid().toString(QUuid::WithoutBraces); w->setProperty("researchRecoveryId", id); }
  return id;
}
static QByteArray diskHash(const QString &path) { QFile f(path); if (!f.open(QIODevice::ReadOnly)) return {}; return QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256).toHex(); }
static QJsonObject readSnapshot(const QString &path) { QFile f(path); if (!f.open(QIODevice::ReadOnly)) return {}; return QJsonDocument::fromJson(f.readAll()).object(); }
struct RecoveryEpoch { QMutex mutex; quint64 value = 0; QString pending; };
// These registries are accessed only on the GUI thread. Workers retain the
// immutable snapshot and a shared commit epoch, never a live document.
static QHash<TeXDocumentWindow *, std::shared_ptr<RecoveryEpoch>> recoveryOwners;
static QHash<QString, std::weak_ptr<RecoveryEpoch>> recoveryPaths;
static void invalidateRecovery(TeXDocumentWindow *w) {
  const auto owner = recoveryOwners.value(w);
  if (owner) { QMutexLocker lock(&owner->mutex); ++owner->value; }
  const QString pending = storage() + QStringLiteral("/") + identity(w) + QStringLiteral(".pending.json");
  const auto shared = recoveryPaths.value(pending).lock();
  if (shared && shared != owner) { QMutexLocker lock(&shared->mutex); ++shared->value; }
}
static std::shared_ptr<RecoveryEpoch> recoveryEpoch(TeXDocumentWindow *w, const QString &pending) {
  auto state = recoveryOwners.value(w);
  if (state && state->pending == pending) return state;
  state = recoveryPaths.value(pending).lock();
  if (!state) { state = std::make_shared<RecoveryEpoch>(); state->pending = pending; recoveryPaths.insert(pending, state); }
  recoveryOwners.insert(w, state);
  if (!w->property("researchAsyncRecoveryTracked").toBool()) {
    w->setProperty("researchAsyncRecoveryTracked", true);
    QObject::connect(w->textDoc(), &QTextDocument::modificationChanged, w, [w](bool modified) { if (!modified) invalidateRecovery(w); });
    QObject::connect(w, &QObject::destroyed, qApp, [w] {
      const auto state = recoveryOwners.take(w);
      if (state) { QMutexLocker lock(&state->mutex); ++state->value; }
    });
  }
  if (recoveryPaths.size() > 64) {
    for (auto it = recoveryPaths.begin(); it != recoveryPaths.end(); ) {
      if (it.value().expired()) it = recoveryPaths.erase(it); else ++it;
    }
  }
  return state;
}
static bool writeSnapshot(const QString &path, const QJsonObject &o, const std::shared_ptr<RecoveryEpoch> &epoch = {}, quint64 serial = 0) {
  QSaveFile f(path); if (!f.open(QIODevice::WriteOnly)) return false;
  const QByteArray data = QJsonDocument(o).toJson(QJsonDocument::Compact);
  if (f.write(data) != data.size()) return false;
  if (epoch) {
    // Encoding and writing the temporary file never hold up GUI cancellation.
    // Only the short atomic commit is serialized against save/discard/close.
    QMutexLocker lock(&epoch->mutex);
    if (epoch->value != serial) return false;
    return f.commit();
  }
  return f.commit();
}
static QJsonObject snapshot(TeXDocumentWindow *w) {
  return {{QStringLiteral("path"), w->untitled() ? QString() : w->textDoc()->absoluteFilePath()},
          {QStringLiteral("text"), w->textDoc()->editorText()}, {QStringLiteral("cursor"), w->cursorPosition()},
          {QStringLiteral("time"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
          {QStringLiteral("diskHash"), w->property("researchDiskHash").toString()}, {QStringLiteral("version"), 1}};
}
static bool history(const QString &id, const QJsonObject &o, const std::shared_ptr<RecoveryEpoch> &epoch = {}, quint64 serial = 0, const QString &storageRoot = {}) {
  const QString dir = (storageRoot.isEmpty() ? storage() : storageRoot) + QStringLiteral("/") + id; QDir().mkpath(dir);
  auto files = QDir(dir).entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
  if (!files.isEmpty() && readSnapshot(QDir(dir).filePath(files.last())).value(QStringLiteral("text")) == o.value(QStringLiteral("text"))) return true;
  static std::atomic<quint64> sequence{0};
  const QString name = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-hhmmss-zzz")) +
    QStringLiteral("-%1.json").arg(sequence.fetch_add(1),20,10,QLatin1Char('0'));
  if (!writeSnapshot(QDir(dir).filePath(name), o, epoch, serial)) return false;
  files = QDir(dir).entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
  while (files.size() > 30) QFile::remove(QDir(dir).filePath(files.takeFirst()));
  return true;
}
void checkpoint(TeXDocumentWindow *w, bool closing) {
  if (!w->property("researchRecoveryReady").toBool()) return;
  invalidateRecovery(w);
  if (w->property("researchPreservePending").toBool() && !w->isModified()) return;
  const QString pending = storage() + QStringLiteral("/") + identity(w) + QStringLiteral(".pending.json");
  if (w->isModified()) {
    const auto now = QDateTime::currentMSecsSinceEpoch();
    const bool historyDue = closing || now - w->property("researchHistoryTime").toLongLong() >= 30000;
    const QVariantList state{pending, w->property("researchRecoveryGeneration"), w->cursorPosition(), w->property("researchDiskHash")};
    const bool unchanged = w->property("researchRecoveryTracked").toBool() &&
        w->property("researchPendingState").toList() == state && QFileInfo::exists(pending);
    if (!closing && unchanged && !historyDue) return;
    const auto o = snapshot(w);
    if (!unchanged || closing) {
      if (!writeSnapshot(pending, o)) { w->statusBar()->showMessage(Research::ui(QStringLiteral("恢复副本保存失败，请手动保存文档。"))); return; }
      w->setProperty("researchPendingState", state);
    }
    if (historyDue) {
      history(identity(w), o); w->setProperty("researchHistoryTime", now);
    }
  }
  // An intentional discard keeps a history revision, but no crash-recovery prompt.
  if (closing || !w->isModified()) { QFile::remove(pending); w->setProperty("researchPendingState", QVariant()); }
}
static void periodicCheckpoint(TeXDocumentWindow *w) {
  if (!w->property("researchRecoveryReady").toBool() || !w->isModified()) return;
  const QString root = storage(), id = identity(w), pending = root + QStringLiteral("/") + id + QStringLiteral(".pending.json");
  const auto now = QDateTime::currentMSecsSinceEpoch();
  const bool historyDue = now - w->property("researchHistoryTime").toLongLong() >= 30000;
  const QVariantList pendingState{pending, w->property("researchRecoveryGeneration"), w->cursorPosition(), w->property("researchDiskHash")};
  const bool unchanged = w->property("researchRecoveryTracked").toBool() && w->property("researchPendingState").toList() == pendingState && QFileInfo::exists(pending);
  if (unchanged && !historyDue) return;
  const auto data = snapshot(w); const auto epoch = recoveryEpoch(w, pending);
  quint64 serial; { QMutexLocker lock(&epoch->mutex); serial = ++epoch->value; }
  const QPointer<TeXDocumentWindow> receiver(w);
  QThreadPool::globalInstance()->start(QRunnable::create([receiver, epoch, serial, root, id, pending, pendingState, data, unchanged, historyDue, now] {
    { QMutexLocker lock(&epoch->mutex); if (epoch->value != serial) return; }
    const bool pendingOk = (unchanged && QFileInfo::exists(pending)) || writeSnapshot(pending, data, epoch, serial);
    const bool historyOk = pendingOk && (!historyDue || history(id, data, epoch, serial, root));
    QMetaObject::invokeMethod(qApp, [receiver, epoch, serial, pendingState, pendingOk, historyOk, historyDue, now] {
      if (!receiver) return;
      { QMutexLocker lock(&epoch->mutex); if (epoch->value != serial) return; }
      if (!pendingOk) {
        Research::showTransientStatusMessage(receiver, Research::ui(QStringLiteral("恢复副本保存失败，请手动保存文档。")), 7000);
        return;
      }
      receiver->setProperty("researchPendingState", pendingState);
      if (historyDue && historyOk) receiver->setProperty("researchHistoryTime", now);
    }, Qt::QueuedConnection);
  }));
}
void beforeSave(TeXDocumentWindow *w, const QString &path) {
  invalidateRecovery(w);
  if (!QFileInfo(path).isFile()) return;
  // Read via the editor's codec for the old disk revision, not the dirty buffer.
  QFile f(path); if (!f.open(QIODevice::ReadOnly)) return;
  const QByteArray bytes = f.readAll();
  QTextCodec *codec = QTextCodec::codecForName(w->getCurrentCodecName().toUtf8());
  QString text = codec ? codec->toUnicode(bytes) : QString::fromUtf8(bytes);
  text.replace(QStringLiteral("\r\n"), QStringLiteral("\n")); text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
  auto o = snapshot(w); o.insert(QStringLiteral("path"), path); o.insert(QStringLiteral("text"), text);
  history(key(canonical(path)), o);
}
void saved(TeXDocumentWindow *w, const QString &previousPath) {
  if(w->property("researchSaveFailure").isValid()&&w->statusBar()->currentMessage()==w->property("researchSaveFailure").toString())w->statusBar()->clearMessage();
  w->setProperty("researchSaveFailure",QVariant());
  if(auto *button=w->findChild<QToolButton *>(QStringLiteral("researchSaveFailureButton")))button->hide();
  invalidateRecovery(w);
  const QString oldId = previousPath.isEmpty() ? w->property("researchRecoveryId").toString() : key(canonical(previousPath));
  if (!oldId.isEmpty()) QFile::remove(storage() + QStringLiteral("/") + oldId + QStringLiteral(".pending.json"));
  w->setProperty("researchDiskHash", QString::fromLatin1(diskHash(w->textDoc()->absoluteFilePath())));
  history(identity(w), snapshot(w)); checkpoint(w);
}
void saveFailure(TeXDocumentWindow *w,const QString &reason) {
  w->setProperty("researchSaveFailure",reason);
  auto *button=w->findChild<QToolButton *>(QStringLiteral("researchSaveFailureButton"));
  if(!button){
    button=new QToolButton(w->statusBar());button->setObjectName(QStringLiteral("researchSaveFailureButton"));button->setText(ui(QStringLiteral("保存未完成")));configureButton(button,ButtonRole::Status);
    auto *menu=new QMenu(button);button->setMenu(menu);button->setPopupMode(QToolButton::InstantPopup);configureButton(button,ButtonRole::Status);w->statusBar()->insertPermanentWidget(0,button);
    QObject::connect(menu,&QMenu::aboutToShow,w,[w,menu]{menu->clear();
      auto *detail=new QWidgetAction(menu);auto *label=new QLabel(w->property("researchSaveFailure").toString(),menu);label->setWordWrap(true);label->setFixedWidth(340);label->setContentsMargins(12,8,12,8);detail->setDefaultWidget(label);menu->addAction(detail);menu->addSeparator();
      if(auto *a=w->findChild<QAction *>(QStringLiteral("actionSave")))menu->addAction(ui(QStringLiteral("重试保存")),a,&QAction::trigger);
      if(auto *a=w->findChild<QAction *>(QStringLiteral("actionSave_As")))menu->addAction(ui(QStringLiteral("另存副本…")),a,&QAction::trigger);
    });
  }
  button->setToolTip(reason);button->show();showTransientStatusMessage(w,reason,7000,NoticePriority::Attention);
}
static void restoreText(TeXDocumentWindow *w, const QJsonObject &o) {
  auto c = w->editor()->textCursor(); c.beginEditBlock(); c.select(QTextCursor::Document); c.insertText(o.value(QStringLiteral("text")).toString()); c.endEditBlock();
  c.setPosition(qBound(0, o.value(QStringLiteral("cursor")).toInt(), int(w->textDoc()->characterCount()) - 1));
  w->editor()->setTextCursor(c); w->editor()->setFocus(); w->textDoc()->setModified(true); checkpoint(w);
}
static void recoveryPrompt(TeXDocumentWindow *w) {
  if (w->untitled()) return;
  const QString path = storage() + QStringLiteral("/") + identity(w) + QStringLiteral(".pending.json");
  const auto o = readSnapshot(path); if (!o.value(QStringLiteral("text")).isString()) return;
  if (o.value(QStringLiteral("text")).toString() == w->textDoc()->editorText()) { QFile::remove(path); return; }
  static QSet<QString> prompted; if (prompted.contains(path)) return; prompted.insert(path);
  const bool changed = o.value(QStringLiteral("diskHash")).toString() != QString::fromLatin1(diskHash(w->textDoc()->absoluteFilePath()));
  QMessageBox box(QMessageBox::Question, Research::ui(QStringLiteral("发现未保存的内容")), changed ? Research::ui(QStringLiteral("文件已更改，恢复内容将作为新文档打开。")) : Research::ui(QStringLiteral("发现未保存的内容。现在恢复，或稍后从“文件”菜单恢复。")), QMessageBox::NoButton, w);
  auto *restore = box.addButton(Research::ui(QStringLiteral("恢复内容")), QMessageBox::AcceptRole);
  box.addButton(Research::ui(QStringLiteral("稍后")), QMessageBox::RejectRole);
  auto *discard = box.addButton(Research::ui(QStringLiteral("删除恢复副本")), QMessageBox::DestructiveRole);
  box.setDefaultButton(restore); box.exec();
  if (box.clickedButton() == restore) {
    TeXDocumentWindow *target = changed ? new TeXDocumentWindow() : w;
    target->setProperty("researchRecoveryReady", true); restoreText(target, o); target->show(); target->raise(); target->activateWindow();
    if (changed && readSnapshot(storage() + QStringLiteral("/") + identity(target) + QStringLiteral(".pending.json")).value(QStringLiteral("text")) == o.value(QStringLiteral("text"))) QFile::remove(path);
  } else if (box.clickedButton() == discard) { history(identity(w), o); QFile::remove(path); }
  else w->setProperty("researchPreservePending", true);
}
static void browseHistory(TeXDocumentWindow *w, bool pendingOnly) {
  QStringList paths;
  if (pendingOnly) {
    for (const auto &f : QDir(storage()).entryList({QStringLiteral("*.pending.json")}, QDir::Files, QDir::Time)) paths.append(QDir(storage()).filePath(f));
  } else {
    const QDir dir(storage() + QStringLiteral("/") + identity(w));
    for (const auto &f : dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name | QDir::Reversed)) paths.append(dir.filePath(f));
    for (const auto &role : {QStringLiteral("original"),QStringLiteral("external")}) {
      const QString path=storage()+QLatin1Char('/')+identity(w)+QStringLiteral(".review-")+role+QStringLiteral(".json");
      if(QFileInfo::exists(path))paths.append(path);
    }
  }
  paths.erase(std::remove_if(paths.begin(), paths.end(), [](const QString &p) { return !readSnapshot(p).value(QStringLiteral("text")).isString(); }), paths.end());
  MaterialDialog d(w); d.setObjectName(QStringLiteral("researchPalette")); d.setWindowTitle(pendingOnly ? Research::ui(QStringLiteral("恢复未保存内容")) : Research::ui(QStringLiteral("本地历史记录"))); d.resize(820, 570);
  auto *layout = new QVBoxLayout(&d); auto *list = new QListWidget(&d); list->setMaximumHeight(145);list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);list->setTextElideMode(Qt::ElideMiddle);
  auto *preview = new QTextBrowser(&d); preview->setObjectName(QStringLiteral("researchHistoryPreview"));
  QFont previewFont(QStringLiteral("JetBrains Mono NL"), 12);
  previewFont.setFamilies({QStringLiteral("JetBrains Mono NL"), QStringLiteral("Microsoft YaHei UI")});
  previewFont = smoothTextFont(previewFont);
  preview->setFont(previewFont); preview->document()->setDefaultFont(previewFont);
  preview->document()->setDefaultStyleSheet(QStringLiteral("pre { font-family: 'JetBrains Mono NL'; } p { font-family: 'Segoe UI', 'Microsoft YaHei UI'; font-size: 14px; }"));
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &d); auto *restore = buttons->addButton(Research::ui(QStringLiteral("恢复到当前文档")), QDialogButtonBox::AcceptRole);
  auto *copy=buttons->addButton(Research::ui(QStringLiteral("打开为副本")),QDialogButtonBox::ActionRole);copy->setObjectName(QStringLiteral("researchHistoryOpenCopy"));bool openCopy=false;
  QObject::connect(copy,&QPushButton::clicked,&d,[&]{openCopy=true;d.accept();});
  buttons->button(QDialogButtonBox::Cancel)->setText(Research::ui(QStringLiteral("取消")));
  for (auto *button : buttons->buttons()) if (auto *push = qobject_cast<QPushButton *>(button)) configureButton(push);
  restore->setEnabled(!paths.isEmpty());copy->setEnabled(!paths.isEmpty());
  layout->addWidget(new QLabel(Research::ui(QStringLiteral("替换当前内容，保存后生效。Ctrl+Z 可撤销。")), &d)); layout->addWidget(list);
  auto *compare=new QToolButton(&d);compare->setObjectName(QStringLiteral("researchHistoryCompare"));compare->setText(Research::ui(QStringLiteral("改动")));compare->setToolTip(Research::ui(QStringLiteral("仅显示改动")));compare->setCheckable(true);compare->setChecked(true);configureButton(compare,ButtonRole::Status);layout->addWidget(compare,0,Qt::AlignLeft);layout->addWidget(preview);layout->addWidget(buttons);
  for (const auto &p : paths) {
    const auto o = readSnapshot(p); auto *item = new QListWidgetItem(QDateTime::fromString(o.value(QStringLiteral("time")).toString(), Qt::ISODateWithMs).toLocalTime().toString(QStringLiteral("yyyy-MM-dd  HH:mm:ss")) + QStringLiteral("   ") + (o.value(QStringLiteral("path")).toString().isEmpty() ? Research::ui(QStringLiteral("未保存文档")) : o.value(QStringLiteral("path")).toString()), list);
    const QString role=o.value(QStringLiteral("reviewRole")).toString();
    if(!role.isEmpty())item->setText(item->text()+QStringLiteral(" · ")+Research::ui(role==QStringLiteral("original")?QStringLiteral("审核前完整版本"):QStringLiteral("外部完整版本")));
    item->setData(Qt::UserRole, p);
    item->setToolTip(item->text());item->setText(QDateTime::fromString(o.value(QStringLiteral("time")).toString(),Qt::ISODateWithMs).toLocalTime().toString(QStringLiteral("yyyy-MM-dd  HH:mm:ss.zzz"))+QStringLiteral("  ·  ")+QFileInfo(o.value(QStringLiteral("path")).toString()).fileName()+(role.isEmpty()?QString():QStringLiteral("  ·  ")+ui(role==QStringLiteral("original")?QStringLiteral("审核前完整版本"):QStringLiteral("外部完整版本"))));
  }
  const QString current=w->textDoc()->editorText();
  auto show=[=]{auto *item=list->currentItem();const QString old=item?readSnapshot(item->data(Qt::UserRole).toString()).value(QStringLiteral("text")).toString():QString();
    if(!compare->isChecked()){preview->setPlainText(old);return;}
    QString html;const auto changes=textEdits(current,old);int offset=0;
    for(const auto &change:changes){const int line=int(QStringView(current).left(change.start).count(QLatin1Char('\n')))+1;
      const int a=change.start?int(current.lastIndexOf(QLatin1Char('\n'),change.start-1))+1:0;int b=int(current.indexOf(QLatin1Char('\n'),change.end));if(b<0)b=int(current.size());const int oldStart=change.start+offset,oldEnd=oldStart+int(change.text.size());const int c=oldStart?int(old.lastIndexOf(QLatin1Char('\n'),oldStart-1))+1:0;int e=int(old.indexOf(QLatin1Char('\n'),oldEnd));if(e<0)e=int(old.size());offset+=int(change.text.size())-(change.end-change.start);
      html+=QStringLiteral("<p>%1 %2 · %3 / %4</p><pre style='white-space:pre-wrap;background:#fff1ef'>− %5</pre><pre style='white-space:pre-wrap;background:#eef6ee'>+ %6</pre>").arg(ui(QStringLiteral("行"))).arg(line).arg(ui(QStringLiteral("当前")),ui(QStringLiteral("历史")),current.mid(a,b-a).toHtmlEscaped(),old.mid(c,e-c).toHtmlEscaped());}
    preview->setHtml(html.isEmpty()?ui(QStringLiteral("与当前内容相同")):html);
  };
  QObject::connect(list,&QListWidget::currentItemChanged,&d,[show]{show();});QObject::connect(compare,&QToolButton::toggled,&d,[show]{show();});
  QObject::connect(buttons, &QDialogButtonBox::accepted, &d, &QDialog::accept); QObject::connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
  list->setCurrentRow(0); if (paths.isEmpty()) preview->setPlainText(Research::ui(QStringLiteral("目前没有可恢复的副本。")));
  if (d.exec() != QDialog::Accepted || !list->currentItem()) return;
  const QString selectedPath = list->currentItem()->data(Qt::UserRole).toString(); const auto o = readSnapshot(selectedPath);
  if (!o.value(QStringLiteral("text")).isString()) {
    QMessageBox::warning(w, Research::ui(QStringLiteral("版本不可用")), Research::ui(QStringLiteral("这个副本已经不可用，请重新打开历史列表选择其他版本。")));
    return;
  }
  TeXDocumentWindow *target = w;
  if(openCopy)target=new TeXDocumentWindow();
  else if (pendingOnly) {
    const QString file = o.value(QStringLiteral("path")).toString();
    // Restore other documents into an unsaved copy, never overwrite a dirty editor.
    if (file != w->textDoc()->absoluteFilePath() || w->isModified()) target = new TeXDocumentWindow();
  }
  target->setProperty("researchRecoveryReady", true); restoreText(target, o); target->show(); target->raise(); target->activateWindow();
  if (target != w && pendingOnly && readSnapshot(storage() + QStringLiteral("/") + identity(target) + QStringLiteral(".pending.json")).value(QStringLiteral("text")) == o.value(QStringLiteral("text"))) QFile::remove(selectedPath);
}
void installWorkflow(TeXDocumentWindow *w) {
  installWritingContexts(w);
  installProjectWriting(w);
  installSlideMode(w);
  installArticleWriting(w);
  auto *nav = new QAction(Research::ui(QStringLiteral("结构…")), w); nav->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+O"))); w->addAction(nav);
  nav->setObjectName(QStringLiteral("researchOutlineAction"));
  QObject::connect(nav, &QAction::triggered, w, [w]() { if(!toggleSlideDirectory(w)&&!toggleArticleStructure(w))openProjectNavigation(w); });
  auto *hist = new QAction(Research::ui(QStringLiteral("本地历史记录…")), w); QObject::connect(hist, &QAction::triggered, w, [w]() { browseHistory(w, false); });
  hist->setObjectName(QStringLiteral("researchHistoryAction"));
  auto *recover = new QAction(Research::ui(QStringLiteral("恢复未保存内容…")), w); QObject::connect(recover, &QAction::triggered, w, [w]() { browseHistory(w, true); });
  recover->setObjectName(QStringLiteral("researchRecoveryAction"));
  auto *setRoot = new QAction(Research::ui(QStringLiteral("设置主文档…")), w);setRoot->setObjectName(QStringLiteral("researchRootAction"));
  QObject::connect(setRoot, &QAction::triggered, w, [w]() {
    if (w->untitled()) { Research::showTransientStatusMessage(w, Research::ui(QStringLiteral("请先保存当前文件。")), 5000); return; }
    if (w->textDoc()->hasModeLine(QStringLiteral("root"))) { Research::showTransientStatusMessage(w, Research::ui(QStringLiteral("主文档已由 % !TEX root 指定，请修改这条注释。")), 6000); return; }
    const QString root = QFileDialog::getOpenFileName(w, Research::ui(QStringLiteral("选择主文档")), QFileInfo(w->textDoc()->absoluteFilePath()).absolutePath(), QStringLiteral("TeX (*.tex)"));
    if (!root.isEmpty()) { rememberRoot(w->textDoc()->absoluteFilePath(), root); refreshBuildDestination(w); Research::showTransientStatusMessage(w, Research::ui(QStringLiteral("主文档已设为 %1")).arg(QFileInfo(root).fileName()), 5000); }
  });
  for (auto *action : w->menuBar()->actions()) if (auto *menu = action->menu()) {
    if (menu->objectName() == QStringLiteral("menuSearch")) { menu->addSeparator(); menu->addAction(nav); }
    if (menu->objectName() == QStringLiteral("menuFile")) { menu->addSeparator(); menu->addAction(setRoot); menu->addAction(hist); menu->addAction(recover); }
  }
}
bool recordRevision(TeXDocumentWindow *w, const QString &text) {
  auto revision = snapshot(w); revision.insert(QStringLiteral("text"), text); return history(identity(w), revision);
}
bool retainReviewVersions(const QString &path,const QString &before,const QString &external,bool keepExisting) {
  const QString id=key(canonical(path));
  for(const auto &role:{QStringLiteral("original"),QStringLiteral("external")}) {
    const QString file=storage()+QLatin1Char('/')+id+QStringLiteral(".review-")+role+QStringLiteral(".json");
    const QString &text=role==QStringLiteral("original")?before:external;
    const auto previous=readSnapshot(file);
    if(keepExisting&&previous.value(QStringLiteral("text")).isString())continue;
    if(previous.value(QStringLiteral("text")).isString()&&previous.value(QStringLiteral("text")).toString()==text)continue;
    const QJsonObject data{{QStringLiteral("path"),canonical(path)},{QStringLiteral("text"),text},{QStringLiteral("cursor"),0},
      {QStringLiteral("time"),QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},{QStringLiteral("version"),1},{QStringLiteral("reviewRole"),role}};
    if(!writeSnapshot(file,data))return false;
  }
  return true;
}
void startRecovery(TeXDocumentWindow *w) {
  if(w->property("researchBackgroundSource").toBool()&&hasPendingRecovery(w->fileName())){w->setProperty("researchRecoveryBlocked",true);return;}
  if (!w->property("researchRecoveryTracked").toBool()) {
    w->setProperty("researchRecoveryTracked", true);
    w->setProperty("researchRecoveryGeneration", qint64(0));
    QObject::connect(w->textDoc(), &QTextDocument::contentsChange, w, [w](int, int removed, int added) {
      if (removed || added) w->setProperty("researchRecoveryGeneration", w->property("researchRecoveryGeneration").toLongLong() + 1);
    });
  }
  if (w->property("researchRecoveryScheduled").toBool()) return;
  w->setProperty("researchRecoveryScheduled", true);
  QTimer::singleShot(0, w, [w]() {
    w->setProperty("researchRecoveryScheduled", false);
    if(w->property("researchBackgroundSource").toBool()&&hasPendingRecovery(w->fileName())){w->setProperty("researchRecoveryBlocked",true);return;}
    w->setProperty("researchRecoveryBlocked",false);
    w->setProperty("researchDiskHash", QString::fromLatin1(diskHash(w->untitled() ? QString() : w->textDoc()->absoluteFilePath())));
    if (!w->untitled()) history(identity(w), snapshot(w));
    recoveryPrompt(w); w->setProperty("researchRecoveryReady", true);
    if (!w->property("researchRecoveryTimer").toBool()) {
      auto *timer = new QTimer(w); timer->setObjectName(QStringLiteral("researchRecoveryCheckpointTimer")); timer->setInterval(5000);
      QObject::connect(timer, &QTimer::timeout, w, [w]() { if (w->isModified()) { w->setProperty("researchPreservePending", false); periodicCheckpoint(w); } }); timer->start();
      w->setProperty("researchRecoveryTimer", true);
    }
    if (!w->untitled()) history(identity(w), snapshot(w));
  });
}

static QPair<int, int> unmatchedDollar(const QString &file, int reportedLine) {
  QString text = uncomment(source(file));
  // Literal inline code is not TeX math. Keep offsets intact for navigation.
  static const QRegularExpression literal(QStringLiteral(R"(\\verb\*?([^\s\w])[^\n]*?\1|\\(?:url|path|nolinkurl)\s*\{[^{}]*\})"));
  auto literals = literal.globalMatch(text);
  while (literals.hasNext()) {
    const auto m = literals.next();
    for (int p = m.capturedStart(); p < m.capturedEnd(); ++p)
      if (text[p] != QLatin1Char('\n')) text[p] = QLatin1Char(' ');
  }
  const auto lines = text.split(QLatin1Char('\n'));
  if (reportedLine < 1 || reportedLine > lines.size()) return {};
  // An inline formula cannot cross a paragraph boundary. Restrict this hint
  // to the immediately preceding paragraph rather than guessing across a paper.
  int first = reportedLine - 1;
  while (first > 0 && reportedLine - first < 120 && !lines[first-1].trimmed().isEmpty()) --first;
  static const QRegularExpression environment(QStringLiteral(R"(^\\(?:begin|end)\s*\{(?:math|displaymath|equation\*?|align\*?|alignat\*?|gather\*?|multline\*?|flalign\*?)\})"));
  QPair<int, int> pending; int dollarWidth = 0;
  for (int n = first; n < reportedLine; ++n) {
    const QString &line = lines[n];
    for (int p = 0; p < line.size(); ++p) {
      if (line[p] == QLatin1Char('\\')) {
        if (p+1 >= line.size()) continue;
        const QChar next = line[p+1];
        if (next == QLatin1Char('$') || next == QLatin1Char('\\')) { ++p; continue; }
        const bool delimiter = next == QLatin1Char('(') || next == QLatin1Char(')') || next == QLatin1Char('[') || next == QLatin1Char(']');
        const auto env = environment.match(line.mid(p));
        if (delimiter || env.hasMatch()) {
          if (pending.first) return pending;
          p += delimiter ? 1 : env.capturedLength() - 1;
        }
        continue;
      }
      if (line[p] != QLatin1Char('$')) continue;
      const int width = p+1 < line.size() && line[p+1] == QLatin1Char('$') ? 2 : 1;
      if (!pending.first) { pending = {n+1, p+1}; dollarWidth = width; }
      else if (width == dollarWidth) { pending = {}; dollarWidth = 0; }
      else return pending;
      p += width - 1;
    }
  }
  return pending;
}

QString issueExplanation(const Issue &issue, bool english) {
  if (issue.suspectLine > 0)
    return (english ? QStringLiteral("The $ on line %1, column %2 may be unmatched. TeX only detected the problem on line %3. Check this delimiter first.")
                    : ui(QStringLiteral("第 %1 行第 %2 列的 $ 可能未闭合；TeX 到第 %3 行才发现。先检查这个符号是否多余或缺少配对。")))
      .arg(issue.suspectLine).arg(issue.suspectColumn).arg(issue.line);
  const QString &s = issue.message;
  if (s.contains(QStringLiteral("not created by biblatex")))
    return english ? QStringLiteral("The bibliography cache uses the previous format. In the Compile menu, rerun your bibliography tool (Biber or BibTeX), then compile the document.")
                   : ui(QStringLiteral("文献缓存仍是旧格式。在编译菜单中重新运行当前文献工具（Biber 或 BibTeX），再编译文稿。"));
  if (s.contains(QStringLiteral("Missing $ inserted")))
    return english ? QStringLiteral("Math mode is missing or was not closed. Check paired $ delimiters; _ and ^ in ordinary text also need math mode or escaping.")
                   : ui(QStringLiteral("数学模式没有正确开始或结束。检查 $ 是否成对；正文中的 _、^ 也可能需要放进公式或转义。"));
  if (s.contains(QStringLiteral("Bad math environment delimiter")) || s.contains(QStringLiteral("Display math should end")))
    return english ? QStringLiteral("Math delimiters do not match. Check for a stray $ before this line, or mixed $...$, \\( ... \\), and \\[ ... \\] delimiters.")
                   : ui(QStringLiteral("公式定界符不匹配。检查前面是否多了 $，或混用了 $…$、\\(…\\)、\\[…\\]。"));
  if (s.contains(QStringLiteral("Undefined control sequence")))
    return english ? QStringLiteral("A command is undefined. Check its spelling and whether the package providing it is loaded.")
                   : ui(QStringLiteral("有命令未定义。检查命令拼写，以及是否加载了提供它的宏包。"));
  if (s.contains(QStringLiteral("Runaway argument")) || s.contains(QStringLiteral("File ended while scanning")))
    return english ? QStringLiteral("An argument or environment was not closed. Check earlier braces and matching \\begin / \\end commands.")
                   : ui(QStringLiteral("前面的参数或环境没有闭合。检查花括号，以及 \\begin、\\end 是否配对。"));
  if (s.contains(QStringLiteral("Extra }, or forgotten $")))
    return english ? QStringLiteral("A closing brace is extra, or math mode is not closed. Check the preceding braces and $ delimiters.")
                   : ui(QStringLiteral("可能多了右花括号，或公式没有闭合。检查前面的花括号和 $。"));
  return {};
}

QList<Issue> compileIssues(const QString &root, const QString &output, bool failed, const QString &logFile) {
  QFile log(logFile.isEmpty() ? QFileInfo(root).absoluteDir().filePath(QFileInfo(root).completeBaseName() + QStringLiteral(".log")) : logFile);
  const QByteArray logBytes = log.open(QIODevice::ReadOnly) ? log.readAll() : QByteArray();
  // Windows TeX engines can write system-encoded paths even when the source
  // document is UTF-8. Decode a complete diagnostic after rejoining its bytes:
  // a TeX line break can also split a multibyte character inside the path.
  auto decode = [](const QByteArray &bytes) {
    QTextCodec::ConverterState state;
    const QString utf8 = QTextCodec::codecForName("UTF-8")->toUnicode(bytes.constData(), bytes.size(), &state);
    return state.invalidChars == 0 && state.remainingChars == 0 ? utf8 : QTextCodec::codecForLocale()->toUnicode(bytes);
  };
  QList<Issue> result; QSet<QString> seen;
  const QDir dir(QFileInfo(root).absolutePath());
  const QStringList sources = projectFiles(root);
  const QRegularExpression located(QStringLiteral(R"(^(.+\.(?:tex|sty|cls|bib)):(\d+):\s*(.*)$)"), QRegularExpression::CaseInsensitiveOption);
  const QRegularExpression locationTail(QStringLiteral(R"((?:^|:)\d+:)"));
  const QRegularExpression lineNumber(QStringLiteral(R"(^l\.(\d+)\s)"));
  auto add = [&](QString file, int line, QString msg, bool error) {
    const QString token = file + QLatin1Char('\n') + QString::number(line) + QLatin1Char('\n') + msg;
    if (seen.contains(token)) return;
    seen.insert(token); result.append({file, line, msg, error});
  };
  auto existingPath = [&](QString name) {
    name = QDir::fromNativeSeparators(name.trimmed());
    if (name.startsWith(QLatin1Char('"')) && name.endsWith(QLatin1Char('"'))) name = name.mid(1, name.size()-2);
    const QFileInfo file(dir, name);
    return file.isFile() ? file.canonicalFilePath() : QString();
  };
  auto parse = [&](const QStringList &lines, const QList<QByteArray> &raw, bool finalLog) {
    QString pendingError;
    auto completeMessage = [&](QString message, int &index) {
      // TeX hard-wraps the entire file:line:message record at max_print_line,
      // including inside words. Rejoin bytes before decoding, but stop before
      // context, help text, or another diagnostic; none of these is the message.
      QByteArray continuation;
      const auto tail = QRegularExpression(QStringLiteral(R"(\s+$)")).match(lines[index]);
      if (tail.hasMatch()) message += tail.captured();
      while (index+1 < lines.size() && message.size()+continuation.size() < 4096) {
        const QString next = lines[index+1], trimmed = next.trimmed();
        if (trimmed.isEmpty() || located.match(trimmed).hasMatch() || lineNumber.match(trimmed).hasMatch()
            || trimmed.startsWith(QLatin1Char('!')) || trimmed.startsWith(QLatin1Char('<'))
            || trimmed.startsWith(QStringLiteral("See the ")) || trimmed.startsWith(QStringLiteral("Type "))
            || trimmed.startsWith(QStringLiteral("For immediate ")) || trimmed.startsWith(QStringLiteral("Runaway argument"))
            || trimmed.startsWith(QStringLiteral("LaTeX Warning:")) || trimmed.startsWith(QStringLiteral("Package "))
            || trimmed.startsWith(QLatin1Char('(')) || trimmed.startsWith(QLatin1Char('['))) break;
        ++index;
        if (raw.isEmpty()) message += next;
        else continuation += raw[index];
      }
      if (!continuation.isEmpty()) message += decode(continuation);
      return message.trimmed();
    };
    for (int i = 0; i < lines.size(); ++i) {
      const QString s = lines[i].trimmed(); auto m = located.match(s);
      // TeX can split the extension or the file/line delimiter itself. Only
      // reconstruct such a header when it resolves to a real source file.
      if (!m.hasMatch() && locationTail.match(s).hasMatch()) {
        QString joined; QByteArray joinedBytes;
        for (int j = i; j >= qMax(0, i-16); --j) {
          if (lines[j].trimmed().isEmpty()) break;
          if (raw.isEmpty()) joined.prepend(lines[j]);
          else { joinedBytes.prepend(raw[j]); joined = decode(joinedBytes); }
          if (joined.size() > 4096) break;
          const auto candidate = located.match(joined.trimmed());
          if (candidate.hasMatch() && !existingPath(candidate.captured(1)).isEmpty()) { m = candidate; break; }
        }
      }
      if (m.hasMatch()) {
        QString path, joined; QByteArray joinedBytes;
        // Prefer a complete, existing path over a coincidentally existing tail
        // filename. Never turn a wrapped filename fragment into a new path.
        for (int j = i; j >= qMax(0, i-16); --j) {
          if (lines[j].trimmed().isEmpty()) break;
          if (raw.isEmpty()) joined.prepend(lines[j]);
          else { joinedBytes.prepend(raw[j]); joined = decode(joinedBytes); }
          if (joined.size() > 4096) break;
          const auto candidate = located.match(joined.trimmed());
          if (!candidate.hasMatch()) continue;
          const QString resolved = existingPath(candidate.captured(1));
          if (!resolved.isEmpty()) { path = resolved; m = candidate; }
        }
        if (path.isEmpty()) {
          // An engine may print a basename. Only use an exact, unambiguous
          // project filename; suffix fragments such as "ial_unified.tex" do
          // not qualify as the name of "signature_polynomial_unified.tex".
          const QString name = QDir::fromNativeSeparators(m.captured(1));
          QStringList matches;
          if (QFileInfo(name).fileName() == name) for (const auto &file : sources)
            if (QFileInfo(file).fileName() == name) matches.append(file);
          if (matches.size() == 1) path = matches.first();
        }
        if (path.isEmpty()) path = canonical(dir.filePath(m.captured(1)));
        const int reportedLine = m.captured(2).toInt();
        add(path, reportedLine, completeMessage(m.captured(3), i), true);
        pendingError.clear(); continue;
      }
      if (s.startsWith(QStringLiteral("ERROR -")) || s.startsWith(QStringLiteral("I couldn't open database file")) || s.startsWith(QStringLiteral("I couldn't open style file"))) { add(root, 0, s, true); continue; }
      if (s.startsWith(QStringLiteral("! "))) { pendingError = completeMessage(s.mid(2), i); continue; }
      m = lineNumber.match(s);
      if (!pendingError.isEmpty() && m.hasMatch()) { add(root, m.captured(1).toInt(), pendingError, true); pendingError.clear(); }
      if (finalLog && s.contains(QStringLiteral("Warning:")) && (s.startsWith(QStringLiteral("LaTeX")) || s.startsWith(QStringLiteral("Package")))) {
        QString msg = s; while (i+1 < lines.size() && !lines[i+1].trimmed().isEmpty() && !lines[i+1].startsWith(QLatin1Char('(')) && !lines[i+1].contains(QStringLiteral("Warning:")) && msg.size() < 350) {
          if (!lines[i+1].startsWith(QLatin1Char(' '))) break;
          msg += QStringLiteral(" ") + lines[++i].trimmed();
        }
        const QRegularExpression lr(QStringLiteral(R"(on input line (\d+))")); auto lm = lr.match(msg);
        add(root, lm.hasMatch() ? lm.captured(1).toInt() : 0, msg, false);
      }
    }
    if (!pendingError.isEmpty()) add(root, 0, pendingError, true);
  };
  const auto raw = logBytes.split('\n'); QStringList lines;
  for (auto line : raw) { if (line.endsWith('\r')) line.chop(1); lines.append(decode(line)); }
  // Remove CR before joining wrapped byte records as well.
  auto rawLines = raw; for (auto &line : rawLines) if (line.endsWith('\r')) line.chop(1);
  parse(lines, rawLines, true);
  // The final engine log has complete bytes and fewer latexmk duplicates. Use
  // console diagnostics when there is no engine error record to work from.
  if (failed && std::none_of(result.cbegin(), result.cend(), [](const Issue &issue) { return issue.error; }))
    parse(output.split(QLatin1Char('\n')), {}, false);
  if (failed && std::none_of(result.cbegin(), result.cend(), [](const Issue &issue) { return issue.error; }))
    add(root, 0, Research::ui(QStringLiteral("编译未完成。请查看日志中的第一条错误。")), true);
  std::stable_sort(result.begin(), result.end(), [](const Issue &a, const Issue &b) { return a.error && !b.error; });
  for (auto &issue : result) {
    if (!issue.error || !(issue.message.contains(QStringLiteral("Missing $ inserted"))
        || issue.message.contains(QStringLiteral("Bad math environment delimiter"))
        || issue.message.contains(QStringLiteral("Display math should end"))
        || issue.message.contains(QStringLiteral("Extra }, or forgotten $")))) continue;
    const auto origin = unmatchedDollar(issue.file, issue.line);
    issue.suspectLine = origin.first; issue.suspectColumn = origin.second;
  }
  return result;
}
int selfTest(const QString &reportPath) {
  QTemporaryDir temp; if (!temp.isValid()) return 2;
  Tw::Settings::setPortableIniPath(temp.path() + QStringLiteral("/settings"));
  testStorageOverride = temp.path() + QStringLiteral("/history");
  const QDir dir(temp.path()); QJsonArray checks; int failures = 0;
  auto check = [&](const QString &name, bool pass) { checks.append(QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("passed"), pass}}); if (!pass) ++failures; };
  auto put = [&](const QString &name, const QString &text) { const QString p = dir.filePath(name); QDir().mkpath(QFileInfo(p).absolutePath()); QFile f(p); if (f.open(QIODevice::WriteOnly)) f.write(text.toUtf8()); return canonical(p); };
  const QString paperEngine = QStringLiteral("pdfLaTeX"), noteEngine = QStringLiteral("XeLaTeX");
  check(QStringLiteral("legacy paper profile remains compatible"), canonicalEngineName(QStringLiteral("Paper · pdfLaTeX")) == paperEngine);
  check(QStringLiteral("legacy note profile remains compatible"), canonicalEngineName(QStringLiteral("中文 Note · XeLaTeX")) == noteEngine);
  check(QStringLiteral("English article selects paper"), suggestedEngine(QStringLiteral("\\documentclass{article}\nPaper text.")) == paperEngine);
  check(QStringLiteral("Chinese comments do not switch engine"), suggestedEngine(QStringLiteral("% 中文说明\n\\documentclass{article}\nEnglish paper.")) == paperEngine);
  check(QStringLiteral("Chinese verbatim does not switch engine"), suggestedEngine(QStringLiteral("\\begin{verbatim}中文\\end{verbatim}\nEnglish")) == paperEngine);
  check(QStringLiteral("Chinese body selects note"), suggestedEngine(QStringLiteral("\\documentclass{article}\n中文笔记")) == noteEngine);
  check(QStringLiteral("ctex class selects note"), suggestedEngine(QStringLiteral("\\documentclass[UTF8]{ctexart}")) == noteEngine);
  check(QStringLiteral("ctex package selects note"), suggestedEngine(QStringLiteral("\\usepackage[UTF8]{amsmath,ctex}")) == noteEngine);
  check(QStringLiteral("fontspec selects Unicode engine"), suggestedEngine(QStringLiteral("\\usepackage{fontspec}")) == noteEngine);
  check(QStringLiteral("inline fontspec example does not select XeLaTeX"), suggestedEngine(QStringLiteral("\\verb|\\usepackage[Ligatures=TeX]{fontspec}|")) == paperEngine);
  check(QStringLiteral("starred inline Unicode example is literal"), suggestedEngine(QStringLiteral("\\verb*+中文 \\setmainfont{Example}+ English")) == paperEngine);
  check(QStringLiteral("literal percent does not hide a later compiled font package"), suggestedEngine(QStringLiteral("\\verb|%| \\usepackage{fontspec}")) == noteEngine);
  check(QStringLiteral("literal percent and fontspec examples remain pdfLaTeX"), suggestedEngine(QStringLiteral("\\verb|% \\usepackage{fontspec}| English")) == paperEngine);
  check(QStringLiteral("unterminated verb cannot hide the following source line"), suggestedEngine(QStringLiteral("\\verb|example\n\\usepackage{fontspec}")) == noteEngine);
  check(QStringLiteral("comments containing verb do not switch the engine"), suggestedEngine(QStringLiteral("% \\verb|example| \\usepackage{fontspec}\nEnglish")) == paperEngine);
  check(QStringLiteral("escaped verb command cannot hide a compiled package"), suggestedEngine(QStringLiteral("\\\\verb|example \\usepackage{fontspec}|")) == noteEngine);
  const QString literalPositions = QStringLiteral("line one\n\\verb|\\label{example}%| \\label{real}\nlast line");
  const QString cleanedPositions = uncomment(literalPositions);
  check(QStringLiteral("literal scrubbing preserves navigation positions and compiled labels"), cleanedPositions.size() == literalPositions.size() && cleanedPositions.count(QLatin1Char('\n')) == literalPositions.count(QLatin1Char('\n')) && !cleanedPositions.contains(QStringLiteral("example")) && cleanedPositions.indexOf(QStringLiteral("\\label{real}")) == literalPositions.indexOf(QStringLiteral("\\label{real}")));
  check(QStringLiteral("Unicode math selects Unicode engine"), suggestedEngine(QStringLiteral("\\usepackage{unicode-math}")) == noteEngine);
  check(QStringLiteral("commented packages ignored"), suggestedEngine(QStringLiteral("% \\usepackage{xeCJK}\nText.")) == paperEngine);
  const QString engineRoot = put(QStringLiteral("engine-tests/paper.tex"), QStringLiteral("\\documentclass{article}\n\\input{child}\nPaper."));
  const QString engineChild = put(QStringLiteral("engine-tests/child.tex"), QStringLiteral("English."));
  check(QStringLiteral("included English source inherits paper"), preferredEngine(engineChild, source(engineChild)) == paperEngine);
  rememberEngine(engineRoot, QStringLiteral("LuaLaTeX"));
  check(QStringLiteral("manual main engine inherited by child"), preferredEngine(engineChild, source(engineChild)) == QStringLiteral("LuaLaTeX"));
  rememberEngine(engineChild, noteEngine);
  check(QStringLiteral("manual file selection persists"), preferredEngine(engineChild, source(engineChild)) == noteEngine);
  forgetEngine(engineChild);
  check(QStringLiteral("automatic selection can resume after manual override"), preferredEngine(engineChild, source(engineChild)) == QStringLiteral("LuaLaTeX"));
  check(QStringLiteral("explicit program overrides manual selection"), preferredEngine(engineChild, QStringLiteral("%!TEX program = pdfLaTeX\n中文")) == QStringLiteral("pdfLaTeX"));
  check(QStringLiteral("explicit Lua declaration preserved"), preferredEngine(engineRoot, QStringLiteral("%!TEX program = LuaLaTeX\n\\documentclass{ctexart}")) == QStringLiteral("LuaLaTeX"));
  const QString mixedRoot = put(QStringLiteral("mixed-tests/paper.tex"), QStringLiteral("\\documentclass{article}\n\\input{child}"));
  put(QStringLiteral("mixed-tests/child.tex"), QStringLiteral("中文正文"));
  check(QStringLiteral("Chinese included content selects XeLaTeX"), preferredEngine(mixedRoot, source(mixedRoot)) == noteEngine);
  const QString declaredRoot = put(QStringLiteral("declared-tests/main.tex"), QStringLiteral("%!TEX program = XeLaTeX\n\\documentclass{article}"));
  const QString declaredChild = put(QStringLiteral("declared-tests/child.tex"), QStringLiteral("%!TEX root = main.tex\nEnglish"));
  check(QStringLiteral("explicit main file engine respected"), preferredEngine(declaredChild, source(declaredChild)) == QStringLiteral("XeLaTeX"));
  const QString child = put(QStringLiteral("chapters/lemma.tex"), QStringLiteral("\\begin{lemma}[一致性]\nText.\\label{lem:one}\n\\end{lemma}\n\\begin{proof}\nProof.\n\\end{proof}\n"));
  const QString leaf = put(QStringLiteral("chapters/leaf.tex"), QStringLiteral("Leaf.\n"));
  const QString root = put(QStringLiteral("main.tex"), QStringLiteral("\\documentclass{article}\n% \\input{ghost}\n\\section{Results}\n\\input{chapters/lemma}\n\\input{chapters/nested}\n"));
  put(QStringLiteral("chapters/nested.tex"), QStringLiteral("\\input{chapters/leaf}\n"));
  check(QStringLiteral("unique root inferred through input"), rootCandidates(child, source(child)) == QStringList{root});
  check(QStringLiteral("nested include resolves relative to main"), rootCandidates(leaf, source(leaf)) == QStringList{root});
  check(QStringLiteral("main file stays its own root"), resolveRoot(root, source(root)) == root);
  const QString simpleMain = QStringLiteral("% 中文 header\n \n\\documentclass[12pt]{article}\nbody\n");
  check(QStringLiteral("a plain first class agrees with complete main classification"), unambiguousMainPreamble(simpleMain) && analyzeSource(simpleMain).main);
  bool complexFallback = true;
  for (const auto &header : QStringList{QStringLiteral("\\verb|\\documentclass{article}|\n"),
       QStringLiteral("\\begin{verbatim}\n\\documentclass{article}\n\\end{verbatim}\n"),
       QStringLiteral("\\documentclass[% option\n12pt]{article}\n"),
       QStringLiteral("\\newcommand{\\x}{a}\n\\documentclass{article}\n"),
       QStringLiteral("\\documentclass{ subfiles }\n")}) complexFallback &= !unambiguousMainPreamble(header);
  check(QStringLiteral("literal macro commented option and subfiles headers retain full parsing"), complexFallback);
  const QString largeHeader = QStringLiteral("% large header\n").repeated(400) + QStringLiteral("\\documentclass{article}\n");
  check(QStringLiteral("long preambles retain complete main classification"), !unambiguousMainPreamble(largeHeader) && analyzeSource(largeHeader).main);
  const QString lateClass = QStringLiteral("body\n\\documentclass{article}\n");
  check(QStringLiteral("a declaration beyond ordinary body still uses complete parsing"), !unambiguousMainPreamble(lateClass) && analyzeSource(lateClass).main);
  const QString chosenMain = put(QStringLiteral("manual-main/main.tex"), simpleMain);
  rememberRoot(chosenMain, root);
  check(QStringLiteral("a chosen root overrides even an unambiguous main class"), resolveRoot(chosenMain, simpleMain) == root);
  check(QStringLiteral("project graph includes all four files"), projectFiles(root).size() == 4);
  check(QStringLiteral("commented includes ignored"), !projectFiles(root).contains(dir.filePath(QStringLiteral("ghost.tex"))));
  const QString graphicRoot=put(QStringLiteral("command-boundary/main.tex"),QStringLiteral("\\documentclass{article}\n\\includegraphics{plot.pdf}\n\\includeonly{chapter}\n\\inputencoding{utf8}\n"));
  put(QStringLiteral("command-boundary/graphics.tex"),QStringLiteral("\\label{phantom:graphics}"));
  put(QStringLiteral("command-boundary/only.tex"),QStringLiteral("\\label{phantom:only}"));
  put(QStringLiteral("command-boundary/encoding.tex"),QStringLiteral("\\label{phantom:encoding}"));
  check(QStringLiteral("longer control words do not create phantom included files"),projectFiles(graphicRoot)==QStringList{graphicRoot});
  check(QStringLiteral("build snapshots do not treat graphic commands as source includes"),buildDependencies(graphicRoot)==QStringList{graphicRoot});
  check(QStringLiteral("navigation does not index phantom graphic and encoding files"),buildProjectIndex(graphicRoot)->files==QStringList{graphicRoot});
  const QString other = put(QStringLiteral("other.tex"), QStringLiteral("\\documentclass{article}\n\\include{chapters/lemma}\n"));
  check(QStringLiteral("ambiguous roots remain explicit candidates"), rootCandidates(child, source(child)).size() == 2);
  rememberRoot(child, other); check(QStringLiteral("remembered root wins"), resolveRoot(child, source(child)) == other);
  const auto nav = symbols(root);
  check(QStringLiteral("navigation indexes sections lemmas proofs labels"), nav.size() == 4);
  check(QStringLiteral("navigation preserves Unicode titles"), std::any_of(nav.begin(), nav.end(), [](const Symbol &e) { return e.title.contains(QStringLiteral("一致性")); }));
  const QString customRoot = put(QStringLiteral("custom.tex"), QStringLiteral("\\documentclass{article}\n\\newtheorem{myclaim}{Claim}\n\\section{A {nested}\nheading}\\label{sec:custom}\n\\begin{myclaim}\nClaim text.\n\\end{myclaim}\n\\begin{verbatim}\n\\input{fake}\n\\label{fake}\n\\end{verbatim}\n"));
  const auto customNav = symbols(customRoot);
  check(QStringLiteral("custom theorem environments indexed"), std::any_of(customNav.begin(), customNav.end(), [](const Symbol &e) { return e.title.startsWith(QStringLiteral("myclaim")); }));
  check(QStringLiteral("multiline nested heading read completely"), std::any_of(customNav.begin(), customNav.end(), [](const Symbol &e) { return e.title.contains(QStringLiteral("A {nested} heading")); }));
  check(QStringLiteral("label after heading stays searchable"), std::any_of(customNav.begin(), customNav.end(), [](const Symbol &e) { return e.title == QStringLiteral("label  sec:custom"); }));
  check(QStringLiteral("verbatim examples excluded from navigation"), !std::any_of(customNav.begin(), customNav.end(), [](const Symbol &e) { return e.title.contains(QStringLiteral("fake")); }));
  const QString subfile = put(QStringLiteral("chapter.part.tex"), QStringLiteral("\\documentclass[main.tex]{subfiles}\nBody."));
  const QString subRoot = put(QStringLiteral("sub-main.tex"), QStringLiteral("\\documentclass{article}\n\\subfile{chapter.part}\n"));
  check(QStringLiteral("subfiles class and dotted filenames infer main"), rootCandidates(subfile, source(subfile)) == QStringList{subRoot});
  put(QStringLiteral("cycle-a.tex"), QStringLiteral("\\input{cycle-b}"));
  const QString cycle = put(QStringLiteral("cycle-b.tex"), QStringLiteral("\\input{cycle-a}"));
  check(QStringLiteral("include cycles terminate"), projectFiles(cycle).size() == 2);
  put(QStringLiteral("main.log"), QStringLiteral("LaTeX Warning: Reference `missing' on input line 9 undefined.\n\nOutput written.\n"));
  auto issues = compileIssues(root, QStringLiteral("LaTeX Warning: Reference `resolved' undefined."), false);
  check(QStringLiteral("successful build ignores intermediate warnings"), issues.size() == 1 && !issues.first().message.contains(QStringLiteral("resolved")));
  put(QStringLiteral("main.log"), QStringLiteral("Output written.\n"));
  const QString diagnostic = child + QStringLiteral(":5: Undefined control sequence.");
  issues = compileIssues(root, diagnostic + QStringLiteral("\n") + diagnostic, true);
  check(QStringLiteral("file-line errors parsed and deduplicated"), issues.size() == 1 && issues.first().file == child && issues.first().line == 5 && issues.first().error);
  issues = compileIssues(root, QStringLiteral("! Missing $ inserted.\nl.17 text\n"), true);
  check(QStringLiteral("traditional TeX error gets line number"), !issues.isEmpty() && issues.first().line == 17);
  const QString dollarFile = put(QStringLiteral("math-errors/SAP.tex"), QStringLiteral("\\documentclass{article}\n\\begin{document}\n\nMultiplying the bounds gives $\n\\[\nx^2\n\\]\n\\end{document}\n"));
  const QString emptyLog = put(QStringLiteral("math-errors/empty.log"), QStringLiteral("Output written.\n"));
  for (const auto &header : QStringList{dollarFile.left(dollarFile.size()-2)+QStringLiteral("\n")+dollarFile.right(2)+QStringLiteral(":5: Undefined control sequence."), dollarFile+QStringLiteral(":\n5: Undefined control sequence."), dollarFile+QStringLiteral(":5:\nUndefined control sequence.")}) {
    const QString errorLog = put(QStringLiteral("math-errors/header-wrap.log"), QStringLiteral("LaTeX Warning: Unused option.\n\n")+header+QStringLiteral("\nl.5 \\bad\n"));
    issues = compileIssues(dollarFile, QString(), true, errorLog);
    check(QStringLiteral("wrapped diagnostic header retains exact file line and first error: ")+header, issues.size()==2&&issues.first().error&&issues.first().file==dollarFile&&issues.first().line==5&&issues.first().message==QStringLiteral("Undefined control sequence."));
  }
  const QString warningLog=put(QStringLiteral("math-errors/warning-only.log"),QStringLiteral("LaTeX Warning: Unused option.\n"));
  issues=compileIssues(dollarFile,QStringLiteral("Unrecognized engine failure"),true,warningLog);
  check(QStringLiteral("unrecognized failure stays ahead of ordinary warnings"),issues.size()==2&&issues.first().error&&issues.first().line==0);
  const QString wrappedMessage = dollarFile + QStringLiteral(":5: LaT\neX Error: Bad math environment delimiter.\n\nSee the LaTeX manual.\nl.5 \\[\n");
  issues = compileIssues(dollarFile, wrappedMessage, true, emptyLog);
  check(QStringLiteral("wrapped error words are restored completely"), issues.size() == 1 && issues[0].message == QStringLiteral("LaTeX Error: Bad math environment delimiter."));
  check(QStringLiteral("a stray dollar before display math has a separate tentative source hint"), issues.size() == 1 && issues[0].line == 5 && issues[0].suspectLine == 4 && issues[0].suspectColumn == 30);
  check(QStringLiteral("math error explains detection versus probable cause in both languages"), issues.size() == 1 && issueExplanation(issues[0], false).contains(QStringLiteral("可能")) && issueExplanation(issues[0], true).contains(QStringLiteral("may be unmatched")));
  const QString wrappedLog = put(QStringLiteral("math-errors/wrapped.log"), wrappedMessage);
  issues = compileIssues(dollarFile, wrappedMessage, true, wrappedLog);
  check(QStringLiteral("final log and console do not duplicate the same wrapped error"), issues.size() == 1);
  issues = compileIssues(dollarFile, dollarFile + QStringLiteral(":7: Mis\nsing $ inserted.\n<inserted text>\n                $\nl.7 \\]\n"), true, emptyLog);
  check(QStringLiteral("a missing-dollar error excludes TeX recovery context"), issues.size() == 1 && issues[0].message == QStringLiteral("Missing $ inserted."));
  issues = compileIssues(dollarFile, dollarFile + QStringLiteral(":5: Undefined control \nsequence.\n\nl.5 text\n"), true, emptyLog);
  check(QStringLiteral("wrapped messages retain spaces at a word boundary"), issues.size() == 1 && issues[0].message == QStringLiteral("Undefined control sequence."));
  put(QStringLiteral("math-errors/SAP.tex"), QStringLiteral("\\begin{document}\n\nText \\$ and $x$. % $ ignored\n\\[\nx^2\n\\]\n"));
  issues = compileIssues(dollarFile, dollarFile + QStringLiteral(":4: Missing $ inserted.\n"), true, emptyLog);
  check(QStringLiteral("escaped dollars and comments cannot create a false origin hint"), issues.size() == 1 && issues[0].suspectLine == 0);
  put(QStringLiteral("math-errors/SAP.tex"), QStringLiteral("\\begin{document}\n\nText \\verb|$| and \\url{https://example.org/$}.\n\\[\nx^2\n\\]\n"));
  issues = compileIssues(dollarFile, dollarFile + QStringLiteral(":4: Missing $ inserted.\n"), true, emptyLog);
  check(QStringLiteral("literal code and URL dollars cannot create a false origin hint"), issues.size() == 1 && issues[0].suspectLine == 0);
  put(QStringLiteral("math-errors/SAP.tex"), QStringLiteral("\\begin{document}\n\nValid $$x^2$$ and $y$.\n\\[\n\\text{value $x$}\n\\]\n"));
  issues = compileIssues(dollarFile, dollarFile + QStringLiteral(":6: Missing $ inserted.\n"), true, emptyLog);
  check(QStringLiteral("balanced display and nested text math are not blamed"), issues.size() == 1 && issues[0].suspectLine == 0);
  put(QStringLiteral("math-errors/SAP.tex"), QStringLiteral("\\begin{document}\n\nText $x\n\n\\end{document}\n"));
  issues = compileIssues(dollarFile, dollarFile + QStringLiteral(":4: Missing $ inserted.\n"), true, emptyLog);
  check(QStringLiteral("paragraph-end detection points back to the unclosed inline formula"), issues.size() == 1 && issues[0].line == 4 && issues[0].suspectLine == 3);
  check(QStringLiteral("unknown failure still produces actionable summary"), !compileIssues(root, QStringLiteral("process failed"), true).isEmpty());
  const QString backup = dir.filePath(QStringLiteral("backup.json"));
  QJsonObject o{{QStringLiteral("text"), QStringLiteral("中文证明\n\\alpha+\\beta\n")}, {QStringLiteral("cursor"), 7}};
  check(QStringLiteral("atomic recovery snapshot round trip"), writeSnapshot(backup, o) && readSnapshot(backup) == o);
  put(QStringLiteral("broken.json"), QStringLiteral("{broken"));
  check(QStringLiteral("corrupted snapshot is rejected"), !readSnapshot(dir.filePath(QStringLiteral("broken.json"))).contains(QStringLiteral("text")));
  history(QStringLiteral("test"), o); history(QStringLiteral("test"), o);
  check(QStringLiteral("identical history revisions deduplicated"), QDir(storage() + QStringLiteral("/test")).entryList({QStringLiteral("*.json")}, QDir::Files).size() == 1);
  for (int i = 0; i < 35; ++i) { o.insert(QStringLiteral("text"), QString::number(i)); QThread::msleep(2); history(QStringLiteral("test"), o); }
  check(QStringLiteral("history bounded at thirty revisions"), QDir(storage() + QStringLiteral("/test")).entryList({QStringLiteral("*.json")}, QDir::Files).size() == 30);
  {
    QMainWindow editorWindow, pdfWindow;
    QTextEdit editor; editorWindow.setCentralWidget(&editor);
    editor.setPlainText(QStringLiteral("Original")); editor.moveCursor(QTextCursor::End); editor.insertPlainText(QStringLiteral(" edit"));
    auto *toolbar = editorWindow.addToolBar(QStringLiteral("Editor"));
    auto *pdfToolbar = pdfWindow.addToolBar(QStringLiteral("PDF"));
    QMenu more(&editorWindow), pdfMore(&pdfWindow);
    auto *search = toolbar->addAction(QStringLiteral("Search")); search->setProperty("researchFocusHidden", true);
    auto *save = editorWindow.menuBar()->addAction(QStringLiteral("Save")); save->setShortcut(QKeySequence::Save);
    installWorkspace(&editorWindow, toolbar, &more); installWorkspace(&pdfWindow, pdfToolbar, &pdfMore);
    check(QStringLiteral("hidden menus retain keyboard shortcuts"), editorWindow.actions().contains(save));
    auto *focus = editorWindow.findChild<QAction *>(QStringLiteral("researchFocusToggle")); focus->trigger();
    check(QStringLiteral("focus toggle hides optional search control"), !search->isVisible());
    check(QStringLiteral("focus toggle follows both independent windows"), pdfWindow.findChild<QAction *>(QStringLiteral("researchFocusToggle"))->isChecked() && editorWindow.isWindow() && pdfWindow.isWindow());
    editor.undo(); check(QStringLiteral("switching UI preserves native undo"), editor.toPlainText() == QStringLiteral("Original"));
    focus->trigger(); check(QStringLiteral("default UI restores search control"), search->isVisible() && !focus->isChecked());
    auto *dots = editorWindow.findChild<QToolButton *>(QStringLiteral("researchMore"));
    check(QStringLiteral("menu has a readable label, accessible name and actions"), dots && (dots->text() == QStringLiteral("菜单") || dots->text() == QStringLiteral("Menu")) && dots->accessibleName() == dots->text() && dots->menu() && !dots->menu()->actions().isEmpty());
    editorWindow.takeCentralWidget();
  }
  collaborationSelfTests(dir, check, put);
  QJsonObject report{{QStringLiteral("failures"), failures}, {QStringLiteral("checks"), checks}};
  writeSnapshot(reportPath, report); testStorageOverride.clear(); return failures ? 1 : 0;
}
} // namespace Research
