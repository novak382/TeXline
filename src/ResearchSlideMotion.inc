// Included by ResearchSlides.cpp. Raster movement is confined to the list;
// the model, source edits and undo commit once, before the first moving frame.
class SlideReflow final : public QWidget {
public:
  struct Card {QString key;QRectF from,to;QPixmap image;qreal opacity{1};bool arriving{false},leaving{false};};
  explicit SlideReflow(QListWidget *view):QWidget(view->viewport()),view(view),tick(this) {
    setObjectName(QStringLiteral("researchSlideReflow"));setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);hide();tick.setInterval(16);tick.setTimerType(Qt::PreciseTimer);
    connect(&tick,&QTimer::timeout,this,[this]{if(age.elapsed()>=160)clear();else update();});
    view->installEventFilter(this);view->viewport()->installEventFilter(this);
    connect(view->verticalScrollBar(),&QScrollBar::valueChanged,this,[this]{if(!preparing)clear();});
  }
  void clear() {++generation;preparing=false;tick.stop();hide();cards.clear();before.clear();liveRows.clear();setProperty("researchSnapshotBytes",qint64(0));}
  void prepare(const QStringList &oldKeys,const QStringList &newKeys) {
    auto a=oldKeys,b=newKeys;a.sort();b.sort();
    // A title/body edit is immediate. Reflow only an insertion/deletion or a
    // permutation of otherwise identical cards, including Undo and Redo.
    if(!motionEnabled()||!view->isVisible()||oldKeys.isEmpty()||oldKeys==newKeys||(oldKeys.size()==newKeys.size()&&a!=b)){clear();return;}
    ++generation;preparing=true;QHash<QString,int> occurrences;before.clear();
    if(isVisible()){
      QSet<QString> returning;for(const auto &key:newKeys)returning.insert(key+QLatin1Char(':')+QString::number(occurrences[key]++));
      // Undo can restore a card while its deletion is still visible. Carry its
      // displayed position and opacity back instead of making it vanish and
      // treating it as a completely new arrival. Older departures that remain
      // deleted can finish when this new transaction replaces their animation.
      const qreal t=progress();for(const auto &card:cards)if(!card.leaving||returning.contains(card.key)){auto now=card;now.from=interpolate(card.from,card.to,t);now.opacity=card.leaving?card.opacity*(1-t):card.opacity+(1-card.opacity)*t;before.insert(card.key,now);}
      hide();tick.stop();cards.clear();return;
    }
    const auto raster=opaqueViewport();
    for(int i=0;i<view->count()&&i<oldKeys.size();++i){
      const auto key=oldKeys[i]+QLatin1Char(':')+QString::number(occurrences[oldKeys[i]]++);
      auto *item=view->item(i);if(item->isHidden())continue;const auto rect=view->visualItemRect(item);
      if(!rect.intersects(view->viewport()->rect()))continue;
      Card card;card.key=key;card.from=rect;card.image=crop(raster,rect);before.insert(key,card);
    }
  }
  void complete(const QStringList &keys) {
    if(before.isEmpty()){preparing=false;return;}
    hide();const auto raster=opaqueViewport();const QRect bounds=view->viewport()->rect();
    // Transparent sidebar views inherit their parent's pale surface. Sampling
    // the unused edge preserves that surface instead of flashing palette white.
    const auto pixels=raster.toImage();background=pixels.pixelColor(qMax(0,pixels.width()-1),qMax(0,pixels.height()-1));
    QHash<QString,int> occurrences;cards.clear();liveRows.clear();qint64 memory=0;int arriving=0,moving=0,leaving=0;
    for(int i=0;i<view->count()&&i<keys.size();++i){
      const auto key=keys[i]+QLatin1Char(':')+QString::number(occurrences[keys[i]]++);auto *item=view->item(i);
      liveRows.insert(key,i);
      const auto end=view->visualItemRect(item);const bool retained=before.contains(key);auto old=before.take(key);
      if(item->isHidden()||(!end.intersects(bounds)&&(!retained||!old.from.intersects(bounds))))continue;
      Card card;card.key=key;card.to=end;card.from=retained?old.from:QRectF(end.translated(-10,8));card.arriving=!retained;card.opacity=retained?old.opacity:0;
      card.image=end.intersects(bounds)?crop(raster,end):old.image;
      arriving+=card.arriving;moving+=retained&&card.from!=card.to;memory+=qint64(card.image.width())*card.image.height()*4;cards.append(card);
    }
    for(auto old:before){old.to=old.from.translated(-8,0);old.leaving=true;old.arriving=false;cards.append(old);++leaving;memory+=qint64(old.image.width())*old.image.height()*4;}
    before.clear();preparing=false;if(cards.isEmpty()||(!arriving&&!moving&&!leaving)){clear();return;}
    setGeometry(bounds);setProperty("researchSnapshotBytes",memory);setProperty("researchArrivingCards",arriving);setProperty("researchMovingCards",moving);setProperty("researchLeavingCards",leaving);
    segmentDuration=160;age.start();clock.start();show();raise();tick.start(motionFrameInterval(view));
  }
protected:
  bool eventFilter(QObject *object,QEvent *event) override {
    const auto type=event->type();
    // Model reset temporarily changes scrollbar ranges and viewport size.
    // complete() will sample the final layout; retain the old card positions
    // until that transaction finishes instead of treating it as user scrolling.
    if(preparing&&type==QEvent::Resize)return false;
    if(rebasing&&type==QEvent::Resize&&object==view->viewport())return false;
    if(type==QEvent::Resize&&object==view->viewport()&&isVisible()){rebaseViewport();return false;}
    if(type==QEvent::Resize||type==QEvent::Hide||type==QEvent::KeyPress||type==QEvent::InputMethod||type==QEvent::MouseButtonPress||type==QEvent::MouseMove||type==QEvent::Wheel||type==QEvent::FontChange||type==QEvent::StyleChange||type==QEvent::DevicePixelRatioChange)clear();
    return false;
  }
  void paintEvent(QPaintEvent *) override {
    if(age.elapsed()>=160){clear();return;}const qreal t=progress();QPainter p(this);p.fillRect(rect(),background);
    // Departing cards are behind the survivors, so neighbours never acquire a
    // second ghost edge while closing the gap.
    for(bool departing:{true,false})for(const auto &card:cards)if(card.leaving==departing){
      const auto place=interpolate(card.from,card.to,t);const qreal opacity=card.leaving?card.opacity*(1-t):card.opacity+(1-card.opacity)*t;
      p.setOpacity(opacity);p.drawPixmap(QPoint(qRound(place.x()),qRound(place.y())),card.image);
    }
    setProperty("researchMotionProgress",t);
  }
private:
  void freezeCurrent() {const qreal t=progress();for(auto &card:cards){card.from=interpolate(card.from,card.to,t);card.to=card.from;card.opacity=card.leaving?card.opacity*(1-t):card.opacity+(1-card.opacity)*t;}}
  void rebaseViewport() {
    if(age.elapsed()>=160){clear();return;}freezeCurrent();tick.stop();setGeometry(view->viewport()->rect());segmentDuration=qMax(1,160-int(age.elapsed()));clock.restart();const auto serial=++generation;
    // Scrollbars can resize the viewport after the model commits. Retarget
    // from the displayed positions after Qt lays out the new slots, preserving
    // the original deadline rather than restarting a second full animation.
    QTimer::singleShot(0,this,[this,serial]{
      if(serial!=generation||!isVisible())return;if(age.elapsed()>=160){clear();return;}freezeCurrent();rebasing=true;hide();view->doItemsLayout();const auto raster=opaqueViewport();
      for(auto &card:cards)if(!card.leaving&&liveRows.contains(card.key)){auto *item=view->item(liveRows.value(card.key));card.to=view->visualItemRect(item);if(card.to.intersects(view->viewport()->rect()))card.image=crop(raster,card.to.toRect());}else card.to=card.from.translated(-8,0);
      qint64 memory=0;for(const auto &card:cards)memory+=qint64(card.image.width())*card.image.height()*4;setProperty("researchSnapshotBytes",memory);
      segmentDuration=qMax(1,160-int(age.elapsed()));clock.restart();setGeometry(view->viewport()->rect());rebasing=false;show();raise();tick.start(motionFrameInterval(view));
    });
  }
  QPixmap opaqueViewport() const {
    const auto raw=view->viewport()->grab();QColor surface=raw.toImage().pixelColor(0,0);
    // A transparent viewport returns transparent pixels, not its parent's
    // visible surface. Composite that surface into every moving card so neither
    // final model labels nor neighbouring text can show through it.
    if(surface.alpha()<255){auto *root=view->window();const auto backdrop=root->grab(QRect(view->viewport()->mapTo(root,QPoint()),QSize(1,1))).toImage();surface=backdrop.isNull()?root->palette().color(QPalette::Window):backdrop.pixelColor(0,0);}surface.setAlpha(255);
    QPixmap result(raw.size());result.setDevicePixelRatio(raw.devicePixelRatio());result.fill(surface);QPainter painter(&result);painter.drawPixmap(QPoint(),raw);return result;
  }
  static QRectF interpolate(const QRectF &a,const QRectF &b,qreal t){return {a.topLeft()+(b.topLeft()-a.topLeft())*t,b.size()};}
  static QPixmap crop(const QPixmap &raster,const QRect &rect){const qreal d=raster.devicePixelRatio();auto p=raster.copy(QRect(qRound(rect.x()*d),qRound(rect.y()*d),qRound(rect.width()*d),qRound(rect.height()*d)));p.setDevicePixelRatio(d);return p;}
  qreal progress()const{return QEasingCurve(QEasingCurve::OutCubic).valueForProgress(qBound(0.,double(clock.elapsed())/segmentDuration,1.));}
  QListWidget *view;QTimer tick;QElapsedTimer clock,age;QVector<Card> cards;QHash<QString,Card> before;QHash<QString,int> liveRows;QColor background;int segmentDuration{160};quint64 generation{0};bool rebasing{false},preparing{false};
};
QStringList slideMotionKeys(const SlideDeck &deck) {
  QStringList keys;for(const auto &frame:deck.frames){const auto text=deck.sources.value(frame.file).mid(frame.start,frame.end-frame.start);keys<<QString::fromLatin1(QCryptographicHash::hash((frame.file+QLatin1Char('\n')+text).toUtf8(),QCryptographicHash::Sha256).toHex());}return keys;
}
