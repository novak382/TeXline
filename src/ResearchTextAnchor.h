// Explicit, content-aware source ranges; never guess between repeated matches.
// GPL-2.0-or-later.
#pragma once
#include <QCryptographicHash>
#include <QVariantMap>
#include <QString>
namespace Research {
inline QVariantMap textAnchor(const QString &text,int start,int end,const QString &digest={}){
 start=qBound(0,start,int(text.size()));end=qBound(start,end,int(text.size()));
 return {{QStringLiteral("start"),start},{QStringLiteral("end"),end},
   {QStringLiteral("hash"),digest.isEmpty()?QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(),QCryptographicHash::Sha256).toHex()):digest},
   {QStringLiteral("before"),text.mid(qMax(0,start-160),qMin(start,160))},{QStringLiteral("after"),text.mid(end,160)},
   {QStringLiteral("begin"),start==0},{QStringLiteral("finish"),end==text.size()}};
}
inline bool locateTextAnchor(const QString &text,const QVariantMap &anchor,int *start,int *end){
 if(anchor.isEmpty())return false;
 const int a=anchor.value(QStringLiteral("start")).toInt(),b=anchor.value(QStringLiteral("end")).toInt();
 if(a<0||b<a)return false;
 if(b<=text.size()&&anchor.value(QStringLiteral("hash")).toString()==QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(),QCryptographicHash::Sha256).toHex())){*start=a;*end=b;return true;}
 const auto before=anchor.value(QStringLiteral("before")).toString(),after=anchor.value(QStringLiteral("after")).toString();
 auto unique=[&](const QString &part){const int at=int(text.indexOf(part));return !part.isEmpty()&&at>=0&&text.indexOf(part,at+1)<0?at:-1;};
 const int left=anchor.value(QStringLiteral("begin")).toBool()?0:unique(before),right=anchor.value(QStringLiteral("finish")).toBool()?int(text.size()):unique(after);
 if(left<0||right<0)return false;
 const int mapped=anchor.value(QStringLiteral("begin")).toBool()?0:left+int(before.size());
 if(mapped>right)return false;*start=mapped;*end=right;return true;
}
}
