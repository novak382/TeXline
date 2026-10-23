#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "TeXDocumentWindow.h"
#include "CompletingEdit.h"
#include "ResearchAppearance.h"
#include "Settings.h"
#include <QAction>
#include <QMenu>
#include <QComboBox>
#include <QAbstractItemView>
#include <QToolTip>
#include <QCryptographicHash>
#include <QFile>
#include <QToolButton>
#include <QToolBar>
#include <QLabel>
#include <QPainter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTimer>
#include <QThread>
#include <QElapsedTimer>
#include <windows.h>
#include <dwmapi.h>
static void settle(int ms=200){QElapsedTimer timer;timer.start();while(timer.elapsed()<ms){QCoreApplication::processEvents();QThread::msleep(2);}}
int main(int argc,char **argv){const QString report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);QTimer::singleShot(100,&app,[&]{
 QJsonArray checks,samples;int failures=0;auto check=[&](const QString &name,bool ok){checks.append(QJsonObject{{"name",name},{"passed",ok}});failures+=!ok;};
 Research::setFocusMode(false);Tw::Settings().setValue("researchDetectBeamer",false);auto *w=new TeXDocumentWindow();w->resize(1000,720);w->show();w->editor()->setPlainText("% Independent text rendering sample 中文注释\n\\documentclass{article}\n\\begin{document}\nA small bilingual document.\\end{document}\n");w->setModified(false);settle();w->showNormal();w->resize(1000,720);settle();
 const QString folder=QFileInfo(report).absolutePath();w->grab().save(folder+"/workspace.png");w->grab(QRect(0,0,w->width(),120)).save(folder+"/header.png");
 auto *min=w->findChild<QToolButton *>("researchWindowMinimize"),*max=w->findChild<QToolButton *>("researchWindowMaximize"),*close=w->findChild<QToolButton *>("researchWindowClose");
 check("all caption controls remain accessible",min&&max&&close&&!min->accessibleName().isEmpty()&&!max->accessibleName().isEmpty()&&!close->accessibleName().isEmpty());
 if(min&&max&&close){check("caption controls form a continuous right-aligned strip",min->size()==QSize(46,32)&&max->x()==min->x()+min->width()&&close->x()==max->x()+max->width()&&close->parentWidget()->mapTo(w,QPoint(close->geometry().right()+1,0)).x()==w->width());
 const auto image=min->grab().toImage().convertToFormat(QImage::Format_ARGB32);int top=image.height(),bottom=-1,left=image.width(),right=-1;
 for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x){const auto c=image.pixelColor(x,y);if(c.alpha()>180&&c.red()<90&&c.green()<90&&c.blue()<90){top=qMin(top,y);bottom=qMax(bottom,y);left=qMin(left,x);right=qMax(right,x);}}
 check("minimize stroke is centred in physical pixels",bottom>=top&&qAbs((left+right)-(image.width()-1))<=2&&qAbs((top+bottom)-(image.height()-1))<=2);
 auto *title=w->findChild<QLabel *>("researchPaperTitle");auto *toolbar=w->findChild<QToolBar *>("toolBar_run");
 check("normal title row is 32 pixels and no taller than the toolbar",title&&toolbar&&title->geometry().top()==0&&title->height()==32&&toolbar->mapTo(w,QPoint()).y()==32&&toolbar->height()>=32);
 min->grab().save(folder+"/minimize.png");max->grab().save(folder+"/maximize.png");close->grab().save(folder+"/close.png");
 }
 {QFile asset(":/research/icons/codicons/chrome-minimize.svg");check("original Microsoft chrome-minimize.svg is embedded unchanged",asset.open(QIODevice::ReadOnly)&&QCryptographicHash::hash(asset.readAll(),QCryptographicHash::Sha256).toHex()=="61970afb22ba6828f4e06ddd665dbd4cd55ed4fc05ae33815ee03c07e929c37e");}
 {QFile asset(":/research/icons/codicons/chrome-maximize.svg");check("original Microsoft chrome-maximize.svg is embedded unchanged",asset.open(QIODevice::ReadOnly)&&QCryptographicHash::hash(asset.readAll(),QCryptographicHash::Sha256).toHex()=="b2713506aaac02a5cf39531612d142c2752be547ae82f07167a7b07c606f6cf0");}
 {QFile asset(":/research/icons/codicons/chrome-restore.svg");check("original Microsoft chrome-restore.svg is embedded unchanged",asset.open(QIODevice::ReadOnly)&&QCryptographicHash::hash(asset.readAll(),QCryptographicHash::Sha256).toHex()=="451365376e95c99b512f2451d6763caea1f2ad6c82e63a710aede29b5feee294");}
 {QFile asset(":/research/icons/codicons/chrome-close.svg");check("original Microsoft chrome-close.svg is embedded unchanged",asset.open(QIODevice::ReadOnly)&&QCryptographicHash::hash(asset.readAll(),QCryptographicHash::Sha256).toHex()=="5858b07d74cd2e32ca421121b09ff940d3eff82ad563d49addcb98bde879cb03");}
 {QFile manifest(":/research/icons/codicons/manifest.json");const bool opened=manifest.open(QIODevice::ReadOnly);const auto assets=opened?QJsonDocument::fromJson(manifest.readAll()).object():QJsonObject();const auto resources=assets.value("resources").toArray();bool unchanged=!resources.isEmpty(),renderable=unchanged;
  for(const auto &entry:resources){const auto row=entry.toObject();const QString path=row.value("resource").toString();QFile svg(path);unchanged=unchanged&&svg.open(QIODevice::ReadOnly)&&QCryptographicHash::hash(svg.readAll(),QCryptographicHash::Sha256).toHex()==row.value("sha256").toString().toLatin1();renderable=renderable&&!QIcon(path).pixmap(16,16).isNull();}
  bool composites=true;for(const auto &entry:assets.value("composites").toArray()){const auto row=entry.toObject();QFile svg(row.value("resource").toString());composites=composites&&svg.open(QIODevice::ReadOnly)&&QCryptographicHash::hash(svg.readAll(),QCryptographicHash::Sha256).toHex()==row.value("sha256").toString().toLatin1();}
  check("synchronized compile composition matches its attributed manifest",composites);
  check("all imported interface SVGs and aliases retain original Microsoft bytes",unchanged);check("all imported interface SVGs render through Qt",renderable);
  bool themed=QIcon::themeName()=="texline-codicons";
  const auto theme=assets.value("theme").toObject();for(auto it=theme.begin();it!=theme.end();++it)themed=themed&&!QIcon::fromTheme(it.key()).pixmap(16,16).isNull();
  check("editing and navigation controls resolve the shared SVG icon theme",themed);
 }
 const auto hwnd=reinterpret_cast<HWND>(w->winId());RECT frame{},client{};POINT origin{};GetWindowRect(hwnd,&frame);GetClientRect(hwnd,&client);ClientToScreen(hwnd,&origin);
 check("native frame styles and system transitions remain enabled",(GetWindowLongPtrW(hwnd,GWL_STYLE)&(WS_CAPTION|WS_THICKFRAME))==(WS_CAPTION|WS_THICKFRAME)&&w->property("researchDwmAttribute3Succeeded").toBool()&&!w->property("researchDwmAttribute3").toBool());
 BOOL enabled=FALSE;const HRESULT rendered=DwmGetWindowAttribute(hwnd,DWMWA_NCRENDERING_ENABLED,&enabled,sizeof(enabled));
 check("main window keeps an opaque non-layered native surface with compositor rendering",SUCCEEDED(rendered)&&enabled&&!(GetWindowLongPtrW(hwnd,GWL_EXSTYLE)&WS_EX_LAYERED)&&!w->testAttribute(Qt::WA_TranslucentBackground));
 check("normal window retains the native resize frame and one-pixel top edge",origin.x>frame.left&&origin.y-frame.top==1&&frame.right-origin.x-client.right==origin.x-frame.left&&frame.bottom-origin.y-client.bottom==origin.x-frame.left);
 check("normal Qt client dimensions agree with native dimensions",qAbs(qRound(w->width()*w->devicePixelRatioF())-client.right)<=1&&qAbs(qRound(w->height()*w->devicePixelRatioF())-client.bottom)<=1);
 int corner=0;const HRESULT rounded=DwmGetWindowAttribute(hwnd,static_cast<DWMWINDOWATTRIBUTE>(33),&corner,sizeof(corner));check("DWM accepts rounded main-window corners",SUCCEEDED(rounded)&&corner==2);
 QMenu menu;menu.addAction("Open / 打开");menu.addAction("Save / 保存");menu.popup(w->mapToGlobal(QPoint(200,100)));settle();const auto popup=menu.grab().toImage();popup.save(folder+"/menu-alpha.png");
 check("rounded menu corners have zero opacity",menu.testAttribute(Qt::WA_TranslucentBackground)&&popup.pixelColor(0,0).alpha()<16&&popup.pixelColor(popup.width()-1,0).alpha()<16&&popup.pixelColor(0,popup.height()-1).alpha()<16);
 check("menu centre stays opaque",popup.pixelColor(popup.width()/2,popup.height()/2).alpha()==255);
 check("opaque editor uses subpixel smoothing",!(w->editor()->font().styleStrategy()&QFont::NoSubpixelAntialias)&&(w->editor()->font().styleStrategy()&QFont::PreferAntialias));
 check("transparent menu requests grayscale smoothing",menu.font().styleStrategy()&QFont::NoSubpixelAntialias);menu.hide();
 auto cornerCheck=[&](QWidget *surface,const QString &name){
  if(!surface){check(name+" exists",false);return;}
  const auto handle=reinterpret_cast<HWND>(surface->winId());const auto style=GetWindowLongPtrW(handle,GWL_STYLE);RECT frame{},client{};GetWindowRect(handle,&frame);GetClientRect(handle,&client);
  check(name+" uses a layered frameless native surface without a square shadow",surface->windowFlags().testFlag(Qt::FramelessWindowHint)&&surface->windowFlags().testFlag(Qt::NoDropShadowWindowHint)&&(GetWindowLongPtrW(handle,GWL_EXSTYLE)&WS_EX_LAYERED)&&!(style&(WS_CAPTION|WS_THICKFRAME))&&!(GetClassLongPtrW(handle,GCL_STYLE)&CS_DROPSHADOW)&&frame.right-frame.left==client.right&&frame.bottom-frame.top==client.bottom&&surface->property("researchDwmFrameBorder").toInt()==0);
  const auto image=surface->grab().toImage();image.save(folder+"/"+name+"-alpha.png");int partial=0,transparent=0;
  for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x){const int a=image.pixelColor(x,y).alpha();partial+=a>0&&a<255;transparent+=a==0;}
  check(name+" has transparent outer corners",image.pixelColor(0,0).alpha()<16&&image.pixelColor(image.width()-1,0).alpha()<16);
  check(name+" rounds the edge with fractional alpha",partial>0&&transparent>0);
  samples.append(QJsonObject{{"surface",name},{"partialAlphaPixels",partial},{"transparentPixels",transparent},{"translucent",surface->testAttribute(Qt::WA_TranslucentBackground)}});
 };
 cornerCheck(&menu,"menu");
 auto *combo=new QComboBox(w);combo->addItems({"Compiler / 编译器","Preview / 预览"});combo->setGeometry(250,130,210,32);combo->show();combo->showPopup();settle();cornerCheck(combo->view()->window(),"combo");combo->hidePopup();combo->hide();
 QToolTip::showText(w->mapToGlobal(QPoint(300,200)),"Output path / 输出路径",w);settle();QWidget *tip=nullptr;for(auto *v:QApplication::topLevelWidgets())if(v->isVisible()&&v->inherits("QTipLabel"))tip=v;
 cornerCheck(tip,"tooltip");QToolTip::hideText();settle(50);

 {auto *compile=w->findChild<QToolButton *>("researchCompile"),*focus=w->findChild<QToolButton *>("researchFocusCompile");const auto synced=w->property("researchSynchronizedProject"),cache=w->property("researchCacheOnly");bool modes=compile&&focus;
  for(bool synchronized:{false,true})for(bool cacheOnly:{false,true}){w->setProperty("researchSynchronizedProject",synchronized);w->setProperty("researchCacheOnly",cacheOnly);if(compile){Research::refreshCompileButton(compile);const auto expected=QIcon(synchronized&&!cacheOnly?":/research/icons/compile-sync.svg":":/research/icons/focus-play.svg").pixmap(48,48).toImage();modes=modes&&compile->icon().pixmap(48,48).toImage()==expected&&focus->icon().pixmap(48,48).toImage()==expected;}}
  check("cloud badge follows synchronized output policy in both compile buttons",modes);w->setProperty("researchSynchronizedProject",synced);w->setProperty("researchCacheOnly",cache);if(compile)Research::refreshCompileButton(compile);
  const auto plain=QIcon(":/research/icons/focus-play.svg").pixmap(48,48).toImage(),cloud=QIcon(":/research/icons/compile-sync.svg").pixmap(48,48).toImage();int upper=0;for(int y=0;y<cloud.height()/3;++y)for(int x=cloud.width()/2;x<cloud.width();++x)upper+=cloud.pixelColor(x,y).alpha()>100;
  check("synchronized SVG visibly retains the upper-right cloud",cloud!=plain&&upper>12);cloud.save(folder+"/compile-cloud.png");plain.save(folder+"/compile-plain.png");}
 // Render controlled text on an opaque pixmap, independent of widget effects.
 for(int mode=0;mode<3;++mode){QPixmap sample(QSize(800,150)*w->devicePixelRatioF());sample.setDevicePixelRatio(w->devicePixelRatioF());sample.fill(Qt::white);QPainter painter(&sample);QFont font=w->editor()->font();font.setPixelSize(18);font.setStyleStrategy(QFont::StyleStrategy(QFont::PreferAntialias|QFont::PreferOutline|(mode==0?QFont::NoSubpixelAntialias:QFont::PreferDefault)));font.setHintingPreference(mode==2?QFont::PreferDefaultHinting:QFont::PreferVerticalHinting);painter.setFont(font);painter.setPen(Qt::black);painter.drawText(QRect(12,10,760,130),Qt::AlignLeft|Qt::AlignVCenter,QStringLiteral("LaTeX editor — 清晰的中文与英文字形\n\\sum_{i=1}^{n} x_i = 0; 0123456789"));painter.end();const auto image=sample.toImage();int gray=0,colour=0;for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x){const auto c=image.pixelColor(x,y);if(qMax(c.red(),qMax(c.green(),c.blue()))-qMin(c.red(),qMin(c.green(),c.blue()))>3)++colour;else if(c.red()>0&&c.red()<255)++gray;}samples.append(QJsonObject{{"mode",mode},{"grayPixels",gray},{"subpixelPixels",colour},{"dpr",w->devicePixelRatioF()}});sample.save(folder+QString("/text-%1.png").arg(mode));}
 w->setModified(false);delete w;
 QSaveFile out(report);if(out.open(QIODevice::WriteOnly)){out.write(QJsonDocument(QJsonObject{{"checks",checks},{"samples",samples},{"failures",failures},{"physicalCompositorScreenshot",false}}).toJson());out.commit();}app.exit(failures?1:0);
 });return app.exec();}
