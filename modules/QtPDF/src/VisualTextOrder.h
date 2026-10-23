// Geometry ordering for interactive selections. GPL-2.0-or-later.
#pragma once
#include <QRectF>
#include <QVector>
#include <QPair>
#include <algorithm>
#include <numeric>
#include <cmath>
namespace QtPDF {
inline QVector<int> visualTextOrder(const QVector<QRectF> &rects,QVector<QPair<QRectF,double>> *columns=nullptr) {
  QVector<int> order(rects.size());std::iota(order.begin(),order.end(),0);
  std::stable_sort(order.begin(),order.end(),[&](int a,int b){return rects[a].center().y()<rects[b].center().y();});
  QVector<double> heights;for(const auto &r:rects)if(r.height()>2&&r.height()<50)heights.append(r.height());
  std::sort(heights.begin(),heights.end());const double height=heights.isEmpty()?10:heights[heights.size()/2];
  struct Row {double y,height;QVector<int> items;};QVector<Row> rows;QVector<int> small;
  // Establish prose baselines first. Small superscripts and fraction parts must
  // not create an earlier reading row before the words to which they belong.
  for(int index:order){const auto &r=rects[index];if(r.height()>0&&r.height()<height*.78){small.append(index);continue;}int best=-1;double distance=height*.48;
    for(int row=qMax(0,int(rows.size())-6);row<rows.size();++row){const double gap=qAbs(r.center().y()-rows[row].y);if(gap<distance){best=row;distance=gap;}}
    if(best<0)rows.append(Row{r.center().y(),r.height(),QVector<int>{index}});else {rows[best].items.append(index);rows[best].height=qMax(rows[best].height,r.height());}
  }
  std::stable_sort(rows.begin(),rows.end(),[](const Row &a,const Row &b){return a.y<b.y;});
  for(int index:small){const auto &r=rects[index];const auto it=std::lower_bound(rows.cbegin(),rows.cend(),r.center().y(),[](const Row &row,double y){return row.y<y;});const int at=int(it-rows.cbegin());int best=-1;double distance=1e20;
    for(int row=qMax(0,at-2);row<qMin(int(rows.size()),at+2);++row){const auto &line=rows[row];const double gap=qAbs(r.center().y()-line.y),verticalGap=qMax(0.,qMax(line.y-line.height/2-r.bottom(),r.top()-line.y-line.height/2));if(gap<qMax(height*.48,line.height*.95)&&verticalGap<=height*.16&&gap<distance){best=row;distance=gap;}}
    if(best<0)rows.insert(at,Row{r.center().y(),r.height(),QVector<int>{index}});else rows[best].items.append(index);
  }
  order.clear();
  struct Edge {double x;int change;};QVector<Edge> edges;QRectF bounds;
  for(auto &row:rows){std::stable_sort(row.items.begin(),row.items.end(),[&](int a,int b){return rects[a].left()<rects[b].left();});for(int index:row.items)bounds|=rects[index];for(int i=1;i<row.items.size();++i){const auto &left=rects[row.items[i-1]],&right=rects[row.items[i]];const double margin=qMax(12.,height*1.25);if(right.left()-left.right()>margin*2){edges.append(Edge{left.right()+margin,1});edges.append(Edge{right.left()-margin,-1});}}}
  // A recurring vertical gutter identifies prose columns. Full-width titles,
  // equations and footers divide the page into bands rather than being moved
  // to an arbitrary column. Short formula fragments cannot define a gutter.
  std::stable_sort(edges.begin(),edges.end(),[](const Edge &a,const Edge &b){return a.x<b.x;});
  int count=0,best=0;double gutter=0,gutterWidth=0;
  for(int i=0;i+1<edges.size();++i){count+=edges[i].change;const double x=(edges[i].x+edges[i+1].x)/2;if((count>best||count==best&&edges[i+1].x-edges[i].x>gutterWidth)&&edges[i+1].x>edges[i].x&&x>bounds.left()+bounds.width()*.25&&x<bounds.right()-bounds.width()*.25){best=count;gutter=x;gutterWidth=edges[i+1].x-edges[i].x;}}
  if(best>=qMax(3,int(std::ceil(rows.size()*.35)))){QRectF leftBounds,rightBounds;for(const auto &r:rects){if(r.right()<gutter)leftBounds|=r;else if(r.left()>gutter)rightBounds|=r;}
    if(leftBounds.width()>75&&rightBounds.width()>75){
      if(columns)columns->append(QPair<QRectF,double>{bounds,gutter});
      QVector<int> band;
      auto flush=[&]{for(bool right:{false,true}){QVector<QRectF> local;QVector<int> indices;for(int index:band)if((rects[index].center().x()>gutter)==right){local.append(rects[index]);indices.append(index);}const auto sorted=visualTextOrder(local,columns);for(int index:sorted)order.append(indices[index]);}band.clear();};
      for(const auto &row:rows){bool spans=false;for(int index:row.items)spans|=rects[index].left()<gutter&&rects[index].right()>gutter;if(spans){flush();order+=row.items;}else band+=row.items;}flush();return order;
    }
  }
  for(const auto &row:rows)order+=row.items;
  return order;
}
}
