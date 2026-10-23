// Verify immutable SVG results against repeated renderer loading.
// Independent captured draft fixtures only. GPL-2.0-or-later.
#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include <QtWidgets>
#include <QSvgRenderer>
static QImage paint(QSvgRenderer &renderer){QImage result(640,420,QImage::Format_ARGB32_Premultiplied);result.fill(Qt::white);QPainter painter(&result);painter.setRenderHints(QPainter::Antialiasing|QPainter::TextAntialiasing|QPainter::SmoothPixmapTransform);renderer.render(&painter,QRectF(0,0,640,420));return result;}
int main(int argc,char **argv){const QString report=QString::fromLocal8Bit(argv[1]);argc=1;QApplication app(argc,argv);QJsonArray checks;int failures=0;
 auto check=[&](QString name,bool pass){checks.append(QJsonObject{{"name",name},{"passed",pass}});failures+=!pass;};
 if(!qEnvironmentVariable("TEXLINE_TEST_DESKTOP").startsWith("TeXlineValidation-"))return 2;
 const QDir folder(qEnvironmentVariable("TEXLINE_SVG_FIXTURES"));const auto names=folder.entryList({"*.svg"},QDir::Files,QDir::Name);check("independent draft SVG fixtures exist",names.size()>=4);
 QSvgRenderer reused;
 for(int cycle=0;cycle<3;++cycle)for(const auto &name:names){QFile file(folder.filePath(name));if(!file.open(QIODevice::ReadOnly)){check("fixture opens",false);continue;}auto svg=file.readAll();svg.replace("<symbol","<g").replace("</symbol>","</g>");
  QSvgRenderer fresh;const bool loaded=fresh.load(svg)&&reused.load(svg);check("SVG loads: "+name,loaded);if(!loaded)continue;
  const QRectF bounds(230,110,160,100);fresh.setViewBox(bounds);reused.setViewBox(bounds);const auto actual=paint(reused),expected=paint(fresh);const bool same=actual==expected;check("reloaded SVG matches a fresh renderer: "+name,same);
  if(!same){actual.save(QFileInfo(report).absolutePath()+"/reused-"+QString::number(cycle)+"-"+name+".png");expected.save(QFileInfo(report).absolutePath()+"/fresh-"+QString::number(cycle)+"-"+name+".png");}
 }
 QFile output(report);if(output.open(QIODevice::WriteOnly))output.write(QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures}}).toJson());return failures?1:0;
}
