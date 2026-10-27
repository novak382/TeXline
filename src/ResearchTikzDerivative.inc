// Small symbolic differentiator for the existing PGF arithmetic subset.
// Expressions stay inspectable TeX; there is no numerical slope estimate.
class DerivativeExpression {
  struct Term { QString value, derivative; bool constant{false}; };
  QString source; int at{0}, steps{0}, depth{0}; bool valid{true};
  static QString binary(const QString &a,const QString &op,const QString &b){
    if(op==QStringLiteral("+")){if(a==QStringLiteral("0"))return b;if(b==QStringLiteral("0"))return a;}
    if(op==QStringLiteral("-")){if(b==QStringLiteral("0"))return a;if(a==b)return QStringLiteral("0");}
    if(op==QStringLiteral("*")){if(a==QStringLiteral("0")||b==QStringLiteral("0"))return QStringLiteral("0");if(a==QStringLiteral("1"))return b;if(b==QStringLiteral("1"))return a;}
    if(op==QStringLiteral("/")){if(a==QStringLiteral("0"))return QStringLiteral("0");if(b==QStringLiteral("1"))return a;}
    return QLatin1Char('(')+a+op+b+QLatin1Char(')');
  }
  static QString mul(const QString &a,const QString &b){return binary(a,QStringLiteral("*"),b);}
  static QString div(const QString &a,const QString &b){return binary(a,QStringLiteral("/"),b);}
  static QString call(const QString &name,const QString &a){return name+QLatin1Char('(')+a+QLatin1Char(')');}
  void skip(){while(at<source.size()&&source[at].isSpace())++at;}
  bool take(QChar c){skip();if(at<source.size()&&source[at]==c){++at;return true;}return false;}
  Term sum(){auto a=product();for(;;){QString op;if(take(QLatin1Char('+')))op=QStringLiteral("+");else if(take(QLatin1Char('-')))op=QStringLiteral("-");else return a;const auto b=product();a={binary(a.value,op,b.value),binary(a.derivative,op,b.derivative),a.constant&&b.constant};}}
  Term product(){auto a=unary();for(;;){const bool times=take(QLatin1Char('*'));if(!times&&!take(QLatin1Char('/')))return a;const auto b=unary();const auto d=times?binary(mul(a.derivative,b.value),QStringLiteral("+"),mul(a.value,b.derivative)):div(binary(mul(a.derivative,b.value),QStringLiteral("-"),mul(a.value,b.derivative)),binary(b.value,QStringLiteral("^"),QStringLiteral("2")));a={binary(a.value,times?QStringLiteral("*"):QStringLiteral("/"),b.value),d,a.constant&&b.constant};}}
  Term powerOf(const Term &a,const Term &b){
    const auto value=binary(a.value,QStringLiteral("^"),b.value);QString d;
    if(a.constant&&b.constant)d=QStringLiteral("0");
    else if(b.constant){bool ok=false;const double n=evaluate(b.value,0,&ok);if(!ok||!std::isfinite(n)){valid=false;return {};}
      if(n==0)d=QStringLiteral("0");else if(n==1)d=a.derivative;else d=mul(mul(b.value,binary(a.value,QStringLiteral("^"),QString::number(n-1,'g',14))),a.derivative);
    }else d=mul(value,binary(mul(b.derivative,call(QStringLiteral("ln"),a.value)),QStringLiteral("+"),mul(b.value,div(a.derivative,a.value))));
    return {value,d,a.constant&&b.constant};
  }
  Term unary(){
    if(++steps>512||++depth>128){valid=false;--depth;return {};}
    Term a;if(take(QLatin1Char('+')))a=unary();else if(take(QLatin1Char('-'))){a=unary();a.value=QStringLiteral("(-")+a.value+QLatin1Char(')');a.derivative=mul(QStringLiteral("-1"),a.derivative);}else{a=primary();if(take(QLatin1Char('^')))a=powerOf(a,unary());}--depth;return a;
  }
  Term primary(){
    skip();if(++steps>512||at>=source.size()){valid=false;return {};}
    if(take(QLatin1Char('('))){const auto a=sum();if(!take(QLatin1Char(')')))valid=false;return a;}
    const int first=at;
    if(source[at].isDigit()||source[at]==QLatin1Char('.')){while(at<source.size()&&(source[at].isDigit()||source[at]==QLatin1Char('.')))++at;const auto value=source.mid(first,at-first);bool ok=false;value.toDouble(&ok);valid&=ok;return {value,QStringLiteral("0"),true};}
    if(!source[at].isLetter()){valid=false;++at;return {};}
    while(at<source.size()&&source[at].isLetterOrNumber())++at;const auto name=source.mid(first,at-first);
    if(name==QStringLiteral("x"))return {name,QStringLiteral("1"),false};
    if(name==QStringLiteral("pi")||name==QStringLiteral("e"))return {name,QStringLiteral("0"),true};
    if(!take(QLatin1Char('('))){valid=false;return {};}
    const auto a=sum();if(take(QLatin1Char(','))){const auto b=sum();if(!take(QLatin1Char(')'))||name!=QStringLiteral("pow")){valid=false;return {};}return powerOf(a,b);}
    if(!take(QLatin1Char(')'))){valid=false;return {};}
    const auto value=call(name,a.value);QString d;
    if(name==QStringLiteral("sin"))d=mul(QStringLiteral("(pi/180)"),call(QStringLiteral("cos"),a.value));
    else if(name==QStringLiteral("cos"))d=mul(QStringLiteral("(-pi/180)"),call(QStringLiteral("sin"),a.value));
    else if(name==QStringLiteral("tan"))d=div(QStringLiteral("(pi/180)"),binary(call(QStringLiteral("cos"),a.value),QStringLiteral("^"),QStringLiteral("2")));
    else if(name==QStringLiteral("exp"))d=value;
    else if(name==QStringLiteral("ln"))d=div(QStringLiteral("1"),a.value);
    else if(name==QStringLiteral("sqrt"))d=div(QStringLiteral("1"),mul(QStringLiteral("2"),value));
    else if(name==QStringLiteral("sinh"))d=call(QStringLiteral("cosh"),a.value);
    else if(name==QStringLiteral("cosh"))d=call(QStringLiteral("sinh"),a.value);
    else if(name==QStringLiteral("tanh"))d=div(QStringLiteral("1"),binary(call(QStringLiteral("cosh"),a.value),QStringLiteral("^"),QStringLiteral("2")));
    else if(name==QStringLiteral("rad"))d=QStringLiteral("(pi/180)");
    else if(name==QStringLiteral("deg"))d=QStringLiteral("(180/pi)");
    else if(name==QStringLiteral("log2")||name==QStringLiteral("log10"))d=div(QStringLiteral("1"),mul(a.value,call(QStringLiteral("ln"),name==QStringLiteral("log2")?QStringLiteral("2"):QStringLiteral("10"))));
    else if(name==QStringLiteral("asin")||name==QStringLiteral("acos"))d=div(name==QStringLiteral("asin")?QStringLiteral("(180/pi)"):QStringLiteral("(-180/pi)"),call(QStringLiteral("sqrt"),binary(QStringLiteral("1"),QStringLiteral("-"),binary(a.value,QStringLiteral("^"),QStringLiteral("2")))));
    else if(name==QStringLiteral("atan"))d=div(QStringLiteral("(180/pi)"),binary(QStringLiteral("1"),QStringLiteral("+"),binary(a.value,QStringLiteral("^"),QStringLiteral("2"))));
    else {valid=false;return {};}
    return {value,mul(d,a.derivative),a.constant};
  }
public:
  explicit DerivativeExpression(QString s):source(s){source.replace(QStringLiteral("\\x"),QStringLiteral("x"));}
  QString run(){if(source.size()>256)return {};const auto result=sum();skip();if(!valid||at!=source.size()||result.derivative.size()>256)return {};return result.derivative;}
};
QString derivative(const QString &expression,const QString &variable){
  auto normalized=expression;normalized.replace(QRegularExpression(QRegularExpression::escape(variable)+QStringLiteral("(?![A-Za-z])")),QStringLiteral("\\x"));
  return DerivativeExpression(normalized).run();
}
