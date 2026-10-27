// Bounded, source-preserving TikZ editing. GPL-2.0-or-later.
#pragma once
#include <QColor>
#include <QMap>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>
#include <array>
#include <limits>

namespace Research { namespace Tikz {
enum class Kind { Node, Point, Line, Rectangle, Ellipse, Plot, Angle, Curve };
struct Coordinate {
  int start{-1},end{-1};
  QPointF point;
  QString reference,anchor;
  QString operation;
  QStringList references;
  QStringList curveFunctions;
  double parameter{0};
  double fraction{.5};
  bool numeric{false};
  QPointF outputScale{1,1};
  bool spatial{false};
  std::array<double,3> values{{0,0,0}};
};
struct Item {
  Kind kind{Kind::Node};
  int start{-1},end{-1},optionsStart{-1},optionsEnd{-1},insertOptions{-1},textStart{-1},textEnd{-1},radiusStart{-1},radiusEnd{-1};
  QString id,text,options,shape,expression,expressionX,expressionZ,relation,relativeTo,command;
  int xStart{-1},xEnd{-1},zStart{-1},zEnd{-1};
  int labelOptionsStart{-1},labelOptionsEnd{-1},insertLabelOptions{-1};
  int labelTextStart{-1},labelTextEnd{-1};
  QString labelOptions;
  QMap<QString,QString> labelStyle;
  QPointF labelShift;
  bool labelMovable{false};
  QString variable{QStringLiteral("\\x")};
  QStringList curveFunctions;
  QMap<QString,QString> style;
  QVector<Coordinate> coordinates;
  QPointF position,shift,placementShift;
  QPointF plotScale{1,1};
  QPointF plotX{1,0},plotY{0,1},plotZ{-.385,-.385};
  bool spatial{false},resizable{false},inheritedShift{false};
  QSizeF size{2.4,.8};
  double strokeWidth{.4};
  QVector<qreal> dashPattern; // Physical TeX points, independent of stroke width.
  double dashPhase{0};
  double distance{1.2},domainMin{-3},domainMax{3};
  bool located{false},movable{true},arrow{false},bothArrows{false},reverseArrow{false},closed{false},radiusAsOptions{false},clipping{false};
};
struct CurveFunction {
  QString variable,expression;
  int start{-1},end{-1};
};
struct PathIntersection {
  QString first,second,name;
  int start{-1},end{-1};
  QVector<QPointF> points;
  bool resolved{false};
};
struct Scene {
  QString source,problem,styleContext;
  int bodyStart{-1},bodyEnd{-1};
  QVector<Item> items;
  bool valid{false},partial{false},coordinateSafe{true},coordinateNodeSafe{true};
  QPointF canvasScale{1,1},unitScale{1,1};
  QPointF axisX{1,0},axisY{0,1},axisZ{-.385,-.385},camera{-45,25};
  bool spatial{false};
  double spaceUnit{1};
  int optionsStart{-1},optionsEnd{-1},insertOptions{-1};
  QString pictureOptions;
  QMap<QString,CurveFunction> functions;
  int functionsEnd{-1};
  QVector<PathIntersection> intersections;
};
Scene parse(const QString &source,const QString &styleContext=QString());
QString moved(const Scene &,int item,const QPointF &delta);
QString labelMoved(const Scene &,int item,const QPointF &delta);
QString pointMoved(const Scene &,int item,int coordinate,const QPointF &position);
QString resized(const Scene &,int item,const QSizeF &size);
QString connected(const Scene &,int from,int to);
QString construct(const Scene &,const QString &tool,const QVector<int> &points,const QPointF &at={},QString *error=nullptr);
QString pointLabel(const Scene &,int item);
QString triangle(const Scene &,int mode,const QVector<double> &values,const QPointF &at,QString *error=nullptr);
QString clipped(const Scene &,const QRectF &);
QPointF projected(const Scene &,const std::array<double,3> &);
QString spatialCoordinates(const Scene &,int item,const std::array<double,3> &);
QString spatialPoint(const Scene &,const std::array<double,3> &,const QString &name=QString());
QString projectionView(const Scene &,double azimuth,double elevation,double unit=0);
QString sphere(const Scene &,int center,double radius);
QString spatialCurve(const Scene &,const QStringList &expressions,double from,double to);
int curveForPoint(const Scene &,int point);
bool tangentPoint(const Scene &,int point);
QString tangentAtPoint(const Scene &,int point);
QVector<QPointF> externalTangencies(const Scene &,int curve,int point,QVector<int> *branches=nullptr,QString *error=nullptr);
QVector<QPointF> conicTangencies(const Scene &,int curve,int point,QString *error=nullptr);
QString tangentFromPoint(const Scene &,int curve,int point,int branch,QString *error=nullptr,int *line=nullptr);
bool pointCurve(const Scene &,int curve);
QString pointOnCurve(const Scene &,int curve,double parameter);
QString curveParameter(const Scene &,int point,double parameter,QString *error=nullptr);
double nearestCurveParameter(const Scene &,int curve,const QPointF &,double previous=std::numeric_limits<double>::quiet_NaN());
bool intersectionPath(const Scene &,int item);
QVector<QPointF> pathCrossings(const Scene &,int first,int second,bool *ok=nullptr);
QString pathIntersection(const Scene &,int first,int second,int crossing,QString *error=nullptr);
bool sameIntersectionBranches(const Scene &,const Scene &);
bool isSphere(const Scene &,int item);
QString reordered(const Scene &,int item,bool front);
std::array<double,3> spatialPosition(const Scene &,int item,bool *ok=nullptr);
QString property(const Scene &,int item,const QString &key,const QString &value,QString *error=nullptr);
QString inserted(const Scene &,const QString &preset,const QPointF &at);
QString removed(const Scene &,int item,QString *error=nullptr);
QString emptyPicture();
QPointF endpoint(const Scene &,const Coordinate &);
QPointF endpoint(const Scene &,const Coordinate &,const QPointF &toward);
QVector<QPointF> samples(const Item &,int count=120);
double evaluate(const QString &,double x,bool *ok);
QColor color(const QString &,const QColor &fallback);
QString number(double value);
} }
