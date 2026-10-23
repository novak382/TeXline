// Static UI translations only. Never transliterate document text or format arguments.
const QHash<QString,QString> &traditionalStrings() {
  static const auto strings=[] { QHash<QString,QString> result; QFile file(QStringLiteral(":/research/ui-zh_TW.json"));
    if(file.open(QIODevice::ReadOnly)){const auto object=QJsonDocument::fromJson(file.readAll()).object();for(auto it=object.begin();it!=object.end();++it)result.insert(it.key(),it.value().toString());}return result; }();
  return strings;
}
const QHash<QString,QString> &traditionalSources() {
  static const auto values=[] {QHash<QString,QString> out;auto keys=traditionalStrings().keys();std::sort(keys.begin(),keys.end());for(const auto &key:keys)if(!out.contains(traditionalStrings().value(key)))out.insert(traditionalStrings().value(key),key);return out;}();return values;
}

QString foldUiSearch(QString text) {
  text.replace(QStringLiteral("搜尋"),QStringLiteral("搜索")).replace(QStringLiteral("尋找"),QStringLiteral("查找")).replace(QStringLiteral("專案"),QStringLiteral("项目")).replace(QStringLiteral("字元"),QStringLiteral("字符"));
  static const QString pairs=QStringLiteral("來来倉仓個个側侧備备傳传僅仅優优儲储內内兩两冊册別别刪删則则創创動动務务區区協协參参叢丛問问啟启單单圍围圓圆圖图執执報报場场塊块壞坏夾夹學学實实審审寫写寬宽將将專专對对導导層层帶带幫帮幾几庫库張张彎弯徑径從从復复態态憑凭應应戶户換换擇择擊击據据擴扩敗败數数斷断時时暫暂書书會会條条構构標标樣样機机橫横檔档檢检欄栏權权歷历決决沒没沖冲減减測测準准滿满潔洁瀏浏為为無无燈灯狀状獨独獲获獻献現现環环產产畫画異异當当發发盤盘碩硕確确碼码礎础種种稱称筆笔節节篩筛簡简簽签紙纸級级組组結结給给統统經经網网綴缀線线編编緩缓縮缩總总織织繪绘繼继續续義义聯联聲声腳脚臨临與与舊旧著着蓋盖薦荐處处號号術术補补裝装見见規规視视覽览觀观觸触訂订訊讯記记訪访設设詞词試试該该詳详認认語语誤误說说調调請请論论講讲證证識识譯译議议讀读變变讓让豎竖貼贴資资賬账賴赖蹤踪軟软較较載载輔辅輯辑輸输轉转這这連连進进運运過过達达遠远適适選选還还邊边郵邮鄰邻釋释鈕钮錄录錨锚錯错鍵键鏡镜鑰钥長长閉闭開开間间閱阅關关陣阵陳陈際际隨随隱隐雙双雜杂離离響响頁页頂顶項项順顺須须預预頭头題题顏颜類类顯显驗验驟骤體体點点齊齐");
  static const auto map=[] {QHash<QChar,QChar> out;for(int i=0;i<pairs.size();i+=2)out.insert(pairs[i],pairs[i+1]);return out;}();
  for(auto &ch:text)ch=map.value(ch,ch);
  for(const auto &term:QList<QPair<QString,QString>>{{QStringLiteral("原始码"),QStringLiteral("源码")} ,{QStringLiteral("设定"),QStringLiteral("设置")} ,{QStringLiteral("档案"),QStringLiteral("文件")} ,{QStringLiteral("资料夹"),QStringLiteral("文件夹")} ,{QStringLiteral("储存"),QStringLiteral("保存")} ,{QStringLiteral("贴上"),QStringLiteral("粘贴")} ,{QStringLiteral("剪贴簿"),QStringLiteral("剪贴板")} ,{QStringLiteral("复原"),QStringLiteral("撤销")} ,{QStringLiteral("预设"),QStringLiteral("默认")} ,{QStringLiteral("游标"),QStringLiteral("光标")} ,{QStringLiteral("滑鼠"),QStringLiteral("鼠标")} ,{QStringLiteral("套件"),QStringLiteral("宏包")} })text.replace(term.first,term.second);
  return text.toCaseFolded();
}
