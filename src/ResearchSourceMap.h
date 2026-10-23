// Map positions in the last compiled source to a live draft. GPL-2.0-or-later.
#pragma once
#include "ResearchMerge.h"
#include <QtGlobal>
namespace Research {
inline int mapSourcePosition(const QString &before,const QString &after,int position,bool end=false) {
  position=qBound(0,position,int(before.size()));int shift=0;
  for(const auto &edit:textEdits(before,after)) {
    if(position<edit.start)break;
    if(position>=edit.end){shift+=int(edit.text.size())-(edit.end-edit.start);continue;}
    const int local=position-edit.start;const QString old=before.mid(edit.start,edit.end-edit.start);
    for(int length:{32,16,8,4})for(int left:{0,length/2,length-1}) {
      const int first=qMax(0,local-left),count=qMin(length,int(old.size())-first);
      if(count<4||local>=first+count)continue;const QString context=old.mid(first,count);
      const int found=edit.text.indexOf(context);
      if(found>=0&&edit.text.indexOf(context,found+1)<0)return edit.start+shift+found+local-first;
    }
    // Deleted text has no exact position in the draft. Retain a useful nearest
    // location rather than jumping to an unrelated raw SyncTeX line number.
    return edit.start+shift+(end?int(edit.text.size()):qMin(local,int(edit.text.size())));
  }
  return qBound(0,position+shift,int(after.size()));
}
}
