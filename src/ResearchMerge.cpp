#include "ResearchMerge.h"
#include <QRegularExpression>
#include <algorithm>

namespace Research {
namespace {
QStringList lines(const QString &s) {
  QStringList out; int start = 0;
  for (int i = 0; i < s.size(); ++i) if (s[i] == QLatin1Char('\n')) { out.append(s.mid(start, i-start+1)); start = i+1; }
  if (start < s.size()) out.append(s.mid(start));
  return out;
}
TextEdit trimEdit(const QString &before, TextEdit e) {
  const QString old = before.mid(e.start, e.end-e.start);
  int prefix = 0, suffix = 0;
  while (prefix < old.size() && prefix < e.text.size() && old[prefix] == e.text[prefix]) ++prefix;
  if (prefix < old.size() && old[prefix].isLowSurrogate()) --prefix;
  while (suffix < old.size()-prefix && suffix < e.text.size()-prefix && old[old.size()-1-suffix] == e.text[e.text.size()-1-suffix]) ++suffix;
  if (suffix && old[old.size()-suffix].isLowSurrogate()) --suffix;
  e.start += prefix; e.end -= suffix; e.text = e.text.mid(prefix, e.text.size()-prefix-suffix); return e;
}
QString render(const QString &base, int start, int end, const QVector<TextEdit> &edits) {
  QString out; int cursor = start;
  for (const auto &e : edits) { out += base.mid(cursor, e.start-cursor); out += e.text; cursor = e.end; }
  return out + base.mid(cursor, end-cursor);
}
}

QVector<TextEdit> textEdits(const QString &before, const QString &after) {
  if (before == after) return {};
  const auto a = lines(before), b = lines(after);
  int prefix = 0, suffix = 0;
  while (prefix < a.size() && prefix < b.size() && a[prefix] == b[prefix]) ++prefix;
  while (suffix < a.size()-prefix && suffix < b.size()-prefix && a[a.size()-1-suffix] == b[b.size()-1-suffix]) ++suffix;
  const int n = a.size()-prefix-suffix, m = b.size()-prefix-suffix, width = 2*(n+m)+3, offset = n+m+1;
  QVector<int> aPos(a.size()+1), bPos(b.size()+1);
  for (int i=0;i<a.size();++i) aPos[i+1]=aPos[i]+a[i].size();
  for (int i=0;i<b.size();++i) bPos[i+1]=bPos[i]+b[i].size();
  // Bound trace memory and work. Unusually large rewrites become one reviewable
  // replacement, conservatively producing a conflict rather than losing text.
  const int limit = qMin(256, qMax(0, 4*1024*1024/qMax(1,width)));
  QVector<int> v(width, -1); v[offset+1] = 0; QVector<QVector<int>> trace;
  int distance = -1;
  for (int d=0;d<=qMin(n+m,limit);++d) {
    for (int k=-d;k<=d;k+=2) {
      int x = k == -d || (k != d && v[offset+k-1] < v[offset+k+1]) ? v[offset+k+1] : v[offset+k-1]+1;
      int y = x-k;
      while (x<n && y<m && x>=0 && y>=0 && a[prefix+x]==b[prefix+y]) {++x;++y;}
      v[offset+k]=x;
      if (x>=n && y>=m) {distance=d;break;}
    }
    trace.append(v); if (distance>=0) break;
  }
  if (distance<0) return {trimEdit(before,{aPos[prefix],aPos[prefix+n],after.mid(bPos[prefix],bPos[prefix+m]-bPos[prefix])})};
  QVector<char> ops; int x=n,y=m;
  for (int d=distance;d>0;--d) {
    const auto &previous=trace[d-1]; const int k=x-y;
    const int previousK = k == -d || (k != d && previous[offset+k-1] < previous[offset+k+1]) ? k+1 : k-1;
    const int px=previous[offset+previousK], py=px-previousK;
    while(x>px && y>py) {ops.append('=');--x;--y;}
    if(x==px) {ops.append('+');--y;} else {ops.append('-');--x;}
  }
  while(x>0 && y>0) {ops.append('=');--x;--y;}
  std::reverse(ops.begin(),ops.end());
  QVector<TextEdit> result; int ai=prefix,bi=prefix;
  for(int i=0;i<ops.size();) {
    if(ops[i]=='=') {++ai;++bi;++i;continue;}
    const int sa=ai,sb=bi;
    while(i<ops.size() && ops[i]!='=') {if(ops[i]=='-')++ai;else ++bi;++i;}
    result.append(trimEdit(before,{aPos[sa],aPos[ai],after.mid(bPos[sb],bPos[bi]-bPos[sb])}));
  }
  return result;
}

MergeResult mergeText(const QString &base, const QString &local, const QString &remote) {
  MergeResult result;
  if(local==base || local==remote) {result.text=remote;return result;}
  if(remote==base) {result.text=local;return result;}
  const auto left=textEdits(base,local),right=textEdits(base,remote);
  struct Tagged {TextEdit edit; bool local;}; QVector<Tagged> all;
  for(const auto &e:left)all.append({e,true});
  for(const auto &e:right)all.append({e,false});
  std::stable_sort(all.begin(),all.end(),[](const Tagged&a,const Tagged&b){return a.edit.start<b.edit.start || (a.edit.start==b.edit.start && a.edit.end<b.edit.end);});
  int cursor=0;
  for(int i=0;i<all.size();) {
    const int start=all[i].edit.start; int end=all[i].edit.end; QVector<TextEdit> l,r;
    auto add=[&](const Tagged&t){(t.local?l:r).append(t.edit);end=qMax(end,t.edit.end);};
    add(all[i++]);
    while(i<all.size() && (all[i].edit.start<end || all[i].edit.start==start || (all[i].edit.start==end && all[i].edit.start==all[i].edit.end))) add(all[i++]);
    result.text+=base.mid(cursor,start-cursor);
    const QString original=base.mid(start,end-start),mine=render(base,start,end,l),theirs=render(base,start,end,r);
    QString chosen;
    if(l.isEmpty()) chosen=theirs;
    else if(r.isEmpty() || mine==theirs || theirs==original)chosen=mine;
    else if(mine==original)chosen=theirs;
    else {chosen=mine;result.conflicts.append({int(result.text.size()),int(result.text.size()+mine.size()),original,mine,theirs});}
    result.text+=chosen;cursor=end;
  }
  result.text+=base.mid(cursor);return result;
}

QString changeCategory(const QString &text, int position) {
  const QString context=text.left(qBound(0,position,int(text.size())));
  static const QRegularExpression marker(QStringLiteral(R"(\\(?:(?:sub)*section\*?\s*\{([^}\n]+)\}|begin\s*\{(proof|theorem|lemma|proposition|corollary|definition|equation\*?|align\*?)\}))"));
  auto matches=marker.globalMatch(context); QString label=QStringLiteral("正文");
  while(matches.hasNext()) {const auto m=matches.next();label=m.captured(1).isEmpty()?m.captured(2):m.captured(1);}
  return label+QStringLiteral(" · 第 %1 行").arg(context.count(QLatin1Char('\n'))+1);
}
}
