// No network, TeX execution, polling or package installation. GPL-2.0-or-later.
#include "ResearchCompletion.h"
#include "ResearchShorthand.h"
#include "ResearchSettings.h"
#include "ResearchAppearance.h"
#include "ResearchCommandCompletion.h"
#include "Settings.h"
#include <QApplication>
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHash>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScreen>
#include <QScrollArea>
#include <QSet>
#include <QShortcut>
#include <QVBoxLayout>
#include <algorithm>
#include <memory>

namespace Research {
namespace {
bool nameLetter(QChar c) { return c.isLetter() || c == QLatin1Char('@'); }
void spaces(const QString &s, int &p) { while (p < s.size() && s[p].isSpace()) ++p; }
bool group(const QString &s, int &p, QChar open, QChar close, QString &value) {
  spaces(s,p); if (p >= s.size() || s[p] != open) return false;
  const int begin = ++p; int depth = 1, braces = 0;
  while (p < s.size() && p-begin < 65536) {
    const auto c = s[p++];
    if (c == QLatin1Char('\\')) { if (p < s.size()) ++p; continue; }
    if (open == QLatin1Char('[')) {
      if (c == QLatin1Char('{')) ++braces;
      else if (c == QLatin1Char('}')) { if (--braces < 0) return false; }
      if (braces) continue;
    }
    if (c == open) ++depth;
    if (c == close && --depth == 0) { value = s.mid(begin,p-begin-1); return true; }
  }
  return false;
}
bool braced(const QString &s, int &p, QString &value) { return group(s,p,QLatin1Char('{'),QLatin1Char('}'),value); }
QString control(const QString &s, int &p) {
  spaces(s,p); if (p >= s.size() || s[p++] != QLatin1Char('\\')) return {};
  const int start = p; while (p < s.size() && nameLetter(s[p])) ++p;
  return s.mid(start,p-start);
}
bool xparseArguments(const QString &s, QVector<CompletionArgument> &args) {
  int p = 0;
  while (p < s.size() && args.size() < 9) {
    spaces(s,p); if (p == s.size()) break;
    const auto c = s[p++];
    if (c == QLatin1Char('+') || c == QLatin1Char('!')) continue;
    if (c == QLatin1Char('m')) args.append(CompletionArgument{false,{}});
    else if (c == QLatin1Char('o')) args.append(CompletionArgument{true,{}});
    else if (c == QLatin1Char('O')) { QString value; if (!braced(s,p,value)) return false; args.append({true,value}); }
    else return false; // s/t/d/r/v/b, processors and embellishments need TeX semantics.
  }
  spaces(s,p); return p == s.size();
}
const QString snippetsKey = QStringLiteral("researchLocalSnippetsV1");
QString tr(const char *zh, const char *en) { return (englishUi()?QString::fromUtf8(en):Research::ui(QString::fromUtf8(zh))); }
bool snippetDigit(QChar c) { return c >= QLatin1Char('0') && c <= QLatin1Char('9'); }
bool snippetMetadata(const QString &body, int p) {
  return p+1 < body.size() && body[p] == QLatin1Char('$') &&
      (snippetDigit(body[p+1]) || body[p+1] == QLatin1Char('{'));
}
}

QVector<MacroCompletion> completionDeclarations(const QString &s, const QString &file, const QByteArray &hash) {
  QVector<MacroCompletion> result;
  static const QSet<QString> commands{QStringLiteral("newcommand"),QStringLiteral("renewcommand"),QStringLiteral("providecommand"),QStringLiteral("DeclareRobustCommand"),QStringLiteral("DeclareMathOperator")};
  static const QSet<QString> environments{QStringLiteral("newenvironment"),QStringLiteral("renewenvironment"),QStringLiteral("newtheorem")};
  static const QSet<QString> documentCommands{QStringLiteral("NewDocumentCommand"),QStringLiteral("RenewDocumentCommand"),QStringLiteral("ProvideDocumentCommand"),QStringLiteral("DeclareDocumentCommand")};
  static const QSet<QString> documentEnvironments{QStringLiteral("NewDocumentEnvironment"),QStringLiteral("RenewDocumentEnvironment"),QStringLiteral("ProvideDocumentEnvironment"),QStringLiteral("DeclareDocumentEnvironment")};
  static const QRegularExpression validName(QStringLiteral(R"(^[A-Za-z@]+$)")), validEnvironment(QStringLiteral(R"(^[A-Za-z@][A-Za-z0-9@*:_-]*$)"));
  int p = 0, line = 1, previous = 0;
  while (p < s.size() && result.size() < 2048) {
    if (s[p] != QLatin1Char('\\')) { ++p; continue; }
    const int start = p; const QString command = control(s,p);
    if (command.isEmpty()) { p = qMin(p+1,int(s.size())); continue; }
    const bool env = environments.contains(command) || documentEnvironments.contains(command);
    const bool doc = documentCommands.contains(command) || documentEnvironments.contains(command);
    const bool primitive = command == QStringLiteral("def") || command == QStringLiteral("gdef") || command == QStringLiteral("edef") || command == QStringLiteral("xdef");
    if (!commands.contains(command) && !env && !doc && !primitive) continue;
    spaces(s,p); if (p < s.size() && s[p] == QLatin1Char('*')) ++p;
    QString name;
    if (primitive) name = control(s,p);
    else {
      spaces(s,p);
      if (p < s.size() && s[p] == QLatin1Char('{')) { if (!braced(s,p,name)) continue; name = name.trimmed(); if (!env && name.startsWith(QLatin1Char('\\'))) name.remove(0,1); }
      else if (!env) name = control(s,p);
    }
    if (!(env ? validEnvironment : validName).match(name).hasMatch()) continue;
    MacroCompletion entry; entry.name = name; entry.file = file; entry.sourceHash = hash; entry.environment = env;
    QString value, body; bool complete = true;
    if (primitive) {
      int n = 0;
      spaces(s,p);
      while (p < s.size() && s[p] != QLatin1Char('{')) {
        if (s[p] == QLatin1Char('#') && p+1 < s.size() && s[p+1].digitValue() == n+1 && n < 9) { entry.arguments.append(CompletionArgument{false,{}}); ++n; p += 2; }
        else { entry.signatureKnown = false; ++p; }
      }
      complete = braced(s,p,body);
    } else if (doc) {
      complete = braced(s,p,value);
      entry.signatureKnown = complete && xparseArguments(value,entry.arguments);
      complete = complete && braced(s,p,body);
      if (env) { QString tail; complete = complete && braced(s,p,tail); }
    } else if (command == QStringLiteral("newtheorem")) {
      spaces(s,p); if (p < s.size() && s[p] == QLatin1Char('[')) complete = group(s,p,QLatin1Char('['),QLatin1Char(']'),value);
      complete = complete && braced(s,p,body);
      spaces(s,p); if (p < s.size() && s[p] == QLatin1Char('[')) complete = complete && group(s,p,QLatin1Char('['),QLatin1Char(']'),value);
      entry.arguments.append(CompletionArgument{true,{}});
    } else {
      int count = 0;
      spaces(s,p);
      if (p < s.size() && s[p] == QLatin1Char('[')) {
        complete = group(s,p,QLatin1Char('['),QLatin1Char(']'),value);
        bool ok = false; count = value.toInt(&ok); complete = complete && ok && count >= 0 && count <= 9;
      }
      spaces(s,p); bool optional = false; QString initial;
      if (count > 0 && p < s.size() && s[p] == QLatin1Char('[')) { optional = true; complete = complete && group(s,p,QLatin1Char('['),QLatin1Char(']'),initial); }
      for (int n = 0; n < count; ++n) entry.arguments.append({n == 0 && optional, n == 0 ? initial : QString()});
      complete = complete && braced(s,p,body);
      if (env) { QString tail; complete = complete && braced(s,p,tail); }
    }
    if (!complete) { p = start+1; continue; }
    line += QStringView(s).mid(previous,start-previous).count(QLatin1Char('\n')); previous = start;
    entry.line = line; entry.definition = body.simplified().left(180);
    for(const auto &arg:entry.arguments)if(arg.value.size()>512)entry.signatureKnown=false;
    if (!entry.signatureKnown) entry.arguments.clear();
    result.append(entry);
    // Skip the complete replacement body. Nested definitions and examples in
    // a macro are not unconditional project declarations.
  }
  return result;
}

CompletionSnippet parseCompletionSnippet(const QString &sourceBody,const QString &selectedText) {
  QString body=sourceBody;body.replace(QStringLiteral("\r\n"),QStringLiteral("\n"));body.replace(QLatin1Char('\r'),QLatin1Char('\n'));
  CompletionSnippet out;
  struct Part {QString text;int number{-1};};QVector<Part> parts;QHash<int,QString> defaults;QSet<int> defined;bool finalSeen=false;int fields=0;
  auto fail=[&]{out.error=tr("使用 $1、${1:默认值}、$0 或 ${TM_SELECTED_TEXT}；同号参数共用一个默认值。","Use $1, ${1:default}, $0 or ${TM_SELECTED_TEXT}; repeated fields share one default.");return out;};
  if(body.size()>16384){out.error=tr("片段不得超过 16384 个字符。","Snippet exceeds 16384 characters.");return out;}
  auto variable=[&](int at){
    if(body.mid(at,19)==QStringLiteral("${TM_SELECTED_TEXT}"))return 19;
    const auto name=QStringLiteral("$TM_SELECTED_TEXT");
    if(body.mid(at,name.size())==name&&(at+name.size()==body.size()||!(body[at+name.size()].isLetterOrNumber()||body[at+name.size()]==QLatin1Char('_'))))return int(name.size());
    return 0;
  };
  auto literal=[&](const QString &value){if(!parts.isEmpty()&&parts.last().number<0)parts.last().text+=value;else parts.append({value,-1});};
  for(int p=0;p<body.size();) {
    if(body[p]==QLatin1Char('\\')&&p+1<body.size()&&body[p+1]==QLatin1Char('$')&&(snippetMetadata(body,p+1)||variable(p+1))){literal(QString(body[p+1]));p+=2;continue;}
    if(const int n=variable(p)){literal(selectedText);out.usesSelection=true;p+=n;continue;}
    if(!snippetMetadata(body,p)){literal(QString(body[p++]));continue;}
    ++p;const bool brace=body[p]==QLatin1Char('{');if(brace)++p;
    const int first=p;while(p<body.size()&&snippetDigit(body[p]))++p;
    if(first==p||p-first>2)return fail();
    const int number=body.mid(first,p-first).toInt();QString value;bool hasDefault=false;
    if(brace) {
      if(p<body.size()&&body[p]==QLatin1Char(':')) {
        hasDefault=true;++p;int braces=0;
        while(p<body.size()&&(body[p]!=QLatin1Char('}')||braces)) {
          if(const int n=variable(p)){value+=selectedText;out.usesSelection=true;p+=n;continue;}
          if(body[p]==QLatin1Char('\\')&&p+1<body.size()&&(body[p+1]==QLatin1Char('{')||body[p+1]==QLatin1Char('}'))){value+=body[p++];value+=body[p++];continue;}
          if(body[p]==QLatin1Char('{'))++braces;else if(body[p]==QLatin1Char('}'))--braces;
          if(body[p]==QLatin1Char('\\')&&(snippetMetadata(body,p+1)||variable(p+1)))++p;
          else if(snippetMetadata(body,p))return fail();
          value+=body[p++];
        }
      }
      if(p==body.size()||body[p++]!=QLatin1Char('}'))return fail();
    }
    if(number==0){if(finalSeen||hasDefault)return fail();finalSeen=true;}
    else {
      if(++fields>128)return fail();
      if(hasDefault){if(defined.contains(number)&&defaults.value(number)!=value)return fail();defined.insert(number);defaults[number]=value;}
    }
    parts.append({{},number});
  }
  for(const auto &part:parts) {
    if(part.number<0)out.text+=part.text;
    else if(part.number==0)out.finalPosition=int(out.text.size());
    else {const auto value=defaults.value(part.number);out.stops.append({part.number,int(out.text.size()),int(value.size())});out.text+=value;}
    if(out.text.size()>8*1024*1024)return fail();
  }
  std::stable_sort(out.stops.begin(),out.stops.end(),[](const SnippetStop &a,const SnippetStop &b){return a.number==b.number?a.start<b.start:a.number<b.number;});
  if(out.finalPosition<0)out.finalPosition=int(out.text.size());return out;
}

QVector<UserCompletion> userCompletions() {
  const auto bytes=Tw::Settings().value(snippetsKey).toByteArray();
  static QByteArray cachedBytes;static QVector<UserCompletion> cached;static bool loaded=false;
  if(loaded&&bytes==cachedBytes)return cached;
  loaded=true;cachedBytes=bytes;cached.clear();if(bytes.size()>1024*1024)return cached;
  for(const auto &item:QJsonDocument::fromJson(bytes).array()){const auto obj=item.toObject();cached.append({obj.value(QStringLiteral("prefix")).toString(),obj.value(QStringLiteral("description")).toString(),obj.value(QStringLiteral("body")).toString(),obj.value(QStringLiteral("scope")).toString(),obj.value(QStringLiteral("context")).toInt(),obj.value(QStringLiteral("trigger")).toInt(1)});}
  if(!validateUserCompletions(cached).isEmpty())cached.clear();return cached;
}

QString validateUserCompletions(const QVector<UserCompletion> &values) {
  if (values.size() > 128) return tr("最多保存 128 个片段。","At most 128 snippets.");
  static const QRegularExpression prefix(QStringLiteral(R"(^(?:\\[A-Za-z@]+|[A-Za-z][A-Za-z0-9_-]*)$)")); QSet<QString> seen; qsizetype bytes = 0;
  for (const auto &s : values) {
    if (!prefix.match(s.prefix).hasMatch() || s.prefix.size() > 64 || seen.contains(s.scope+QLatin1Char('\t')+s.prefix)) return tr("前缀须唯一，例如 ff 或 \\myresult，最多 64 个字符。","Use a unique prefix, e.g. ff or \\myresult (at most 64 characters).");
    if (s.body.isEmpty() || s.description.size() > 200 || s.scope.size()>4096 || s.context<0 || s.context>2 || s.trigger<0 || s.trigger>2) return tr("请填写片段内容；说明最多 200 个字符。","Provide a body and a description of at most 200 characters.");
    auto parsed = parseCompletionSnippet(s.body); if (!parsed.valid()) return s.prefix + QStringLiteral(": ") + parsed.error;
    seen.insert(s.scope+QLatin1Char('\t')+s.prefix); bytes += s.body.toUtf8().size() + s.description.toUtf8().size() + s.prefix.size();
  }
  return bytes > 512*1024 ? tr("片段总量不得超过 512 KiB。","Snippet collection exceeds 512 KiB.") : QString();
}

bool completionShortcutAvailable(QMainWindow *window,const QKeySequence &key,const QAction *ignored){
  if(!window||key.isEmpty())return true;
  auto normalized=[](const QKeySequence &sequence){
    const auto empty=QKeyCombination::fromCombined(0);
    QKeyCombination keys[4]{empty,empty,empty,empty};
    for(int n=0;n<sequence.count();++n){
      const auto combination=sequence[n];
      keys[n]=combination.key()==Qt::Key_Backtab
          ?QKeyCombination(combination.keyboardModifiers()|Qt::ShiftModifier,Qt::Key_Tab):combination;
    }
    return QKeySequence(keys[0],keys[1],keys[2],keys[3]);
  };
  const auto candidate=normalized(key);
  auto conflicts=[&](const QKeySequence &sequence){
    if(sequence.isEmpty())return false;
    const auto existing=normalized(sequence);
    // matches() is directional: a longer registered chord does not match its
    // shorter prefix. Completion must leave that first key to the chord too.
    return existing.matches(candidate)!=QKeySequence::NoMatch||candidate.matches(existing)!=QKeySequence::NoMatch;
  };
  for(auto *a:window->findChildren<QAction *>())if(a!=ignored&&a->isEnabled())for(const auto &s:a->shortcuts())if(conflicts(s))return false;
  for(auto *s:window->findChildren<QShortcut *>())if(s->isEnabled())for(const auto &sequence:s->keys())if(conflicts(sequence))return false;
  return true;
}

QDialog *configureLocalCompletion(QWidget *parent) {
  for(auto *widget:QApplication::topLevelWidgets())if(widget->objectName()==QStringLiteral("researchCompletionConfig")){widget->show();widget->raise();return qobject_cast<QDialog *>(widget);}
  auto *dialog=new MaterialDialog(parent);configureSettingsSurface(dialog);dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setObjectName(QStringLiteral("researchCompletionConfig"));dialog->setWindowTitle(englishUi()?QStringLiteral("Completion & snippets"):Research::ui(QStringLiteral("补全与片段")));
  auto *outer=new QVBoxLayout(dialog);auto *scroll=new QScrollArea(dialog);scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);auto *page=completionSettings(scroll,qobject_cast<QMainWindow *>(parent));page->setProperty("completionQuickSetup",true);scroll->setWidget(page);outer->addWidget(scroll,1);
  auto *buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel,dialog);outer->addWidget(buttons);QObject::connect(buttons,&QDialogButtonBox::rejected,dialog,&QDialog::reject);QObject::connect(buttons,&QDialogButtonBox::accepted,dialog,[=]{if(saveCompletionSettings(page))dialog->accept();});
  dialog->resize(QSize(parent?qMin(680,qMax(360,parent->width()-24)):680,720).boundedTo(dialog->screen()->availableGeometry().size()-QSize(32,48)));dialog->open();return dialog;
}
}
