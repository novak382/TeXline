#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "TWUtils.h"
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTextDocument>
#include <QTimer>

static qsizetype reference(const QString &text,qsizetype pos,QChar delim,int direction) {
  if(pos<0||pos>=text.size())return -1;
  while(text[pos]!=delim) {
    const QChar c=text[pos];
    if(!TWUtils::openerMatching(c).isNull())pos=direction<0?reference(text,pos-1,TWUtils::openerMatching(c),-1):-1;
    else if(!TWUtils::closerMatching(c).isNull())pos=direction>0?reference(text,pos+1,TWUtils::closerMatching(c),1):-1;
    if(pos<0)return -1;
    pos+=direction;if(pos<0||pos>=text.size())return -1;
  }return pos;
}
int main(int argc,char **argv) {
  const QString report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
  QTimer::singleShot(100,&app,[&] {
    QJsonArray checks;int failures=0;auto check=[&](QString n,bool pass){checks.append(QJsonObject{{"name",n},{"passed",pass}});failures+=!pass;};
    QTextDocument doc;quint32 seed=20261010;const QString alphabet="{}[]()\nA\\注";bool identical=true;int comparisons=0;
    for(int n=0;n<2000;++n) {
      QString text;for(int i=0;i<64;++i){seed=seed*1664525u+1013904223u;text+=alphabet[int(seed%quint32(alphabet.size()))];}doc.setPlainText(text);
      for(int p=0;p<text.size();++p) {
        for(int direction:{-1,1}) {
          const QChar delim=direction<0?TWUtils::openerMatching(text[p]):TWUtils::closerMatching(text[p]);if(delim.isNull())continue;
          const auto expected=reference(text,p+direction,delim,direction);identical&=TWUtils::balanceDelim(text,p+direction,delim,direction)==expected&&TWUtils::balanceDelim(&doc,p+direction,delim,direction)==expected;++comparisons;
        }
      }
    }
    check("seeded mixed/malformed delimiters preserve prior matching semantics",identical);
    check("empty and invalid positions fail safely",TWUtils::balanceDelim(QString(),0,'}',1)==-1&&TWUtils::balanceDelim(&doc,-1,'{',-1)==-1&&TWUtils::balanceDelim(&doc,doc.characterCount(),'}',1)==-1&&TWUtils::balanceDelim(static_cast<QTextDocument *>(nullptr),0,'}',1)==-1);
    check("invalid directions fail safely",TWUtils::balanceDelim("{}",1,'}',0)==-1&&TWUtils::balanceDelim(&doc,0,'}',2)==-1);
    const int depth=100000;const QString nested=QString("{").repeated(depth)+QString("}").repeated(depth);doc.setPlainText(nested);
    check("100000 nested delimiters match forward without recursion",TWUtils::balanceDelim(nested,1,'}',1)==nested.size()-1&&TWUtils::balanceDelim(&doc,1,'}',1)==nested.size()-1);
    check("100000 nested delimiters match backward without recursion",TWUtils::balanceDelim(nested,nested.size()-2,'{',-1)==0&&TWUtils::balanceDelim(&doc,int(nested.size())-2,'{',-1)==0);
    const QString multiline="{\n[\n(x)\n]\n}";doc.setPlainText(multiline);check("multiline mixed nesting matches in both directions",TWUtils::balanceDelim(&doc,1,'}',1)==multiline.size()-1&&TWUtils::balanceDelim(&doc,int(multiline.size())-2,'{',-1)==0);
    QSaveFile f(report);if(f.open(QIODevice::WriteOnly)){f.write(QJsonDocument(QJsonObject{{"checks",checks},{"comparisons",comparisons},{"seed",20261010},{"failures",failures}}).toJson());f.commit();}app.exit(failures?1:0);
  });return app.exec();
}
