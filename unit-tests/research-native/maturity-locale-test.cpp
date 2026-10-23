#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "TWApp.h"
#include "TeXDocumentWindow.h"
#include "CompletingEdit.h"
#include "PrefsDialog.h"
#include "ResearchAppearance.h"
#include "ResearchSettings.h"
#include "ResearchCommands.h"
#include "ResearchWorkflow.h"
#include "ResearchSlideExport.h"
#include "Settings.h"
#include <QtWidgets>
#include <QThread>
static void settle(int ms=100){QElapsedTimer t;t.start();while(t.elapsed()<ms){QCoreApplication::processEvents();QThread::msleep(1);}}
static void key(QWidget *w,int value){QKeyEvent e(QEvent::KeyPress,value,Qt::NoModifier);QApplication::sendEvent(w,&e);}
int main(int argc,char **argv){
 if(argc<2||!qEnvironmentVariable("TEXLINE_TEST_DESKTOP").startsWith("TeXlineValidation-"))return 2;
 const QString report=QString::fromLocal8Bit(argv[1]);argc=1;TWApp app(argc,argv);app.setQuitOnLastWindowClosed(false);
 QTimer::singleShot(100,&app,[&]{
  QJsonArray checks,frames,timings;int failures=0;const auto folder=QFileInfo(report).absolutePath();
  auto check=[&](QString name,bool ok){checks.append(QJsonObject{{"name",name},{"passed",ok}});failures+=!ok;};
  auto capture=[&](QWidget *w,QString name){w->grab().save(folder+"/"+name+".png");frames.append(QJsonObject{{"name",name},{"dpr",w->devicePixelRatioF()},{"width",w->width()},{"height",w->height()}});};
  const auto locale=Research::uiLocale();Tw::Settings().setValue("openPDFwithTeX",false);
  const auto file=folder+"/论文 文件 設定.tex";QFile f(file);f.open(QIODevice::WriteOnly);const QByteArray source="\\documentclass{article}\n\\begin{document}\n\\section{文件 设置 儲存}\nOriginal manuscript. 文件 保存 设置.\n\\end{document}\n";f.write(source);f.close();
  auto *w=new TeXDocumentWindow(file);w->resize(qEnvironmentVariableIsSet("TEXLINE_TEST_HALF")?620:1000,760);w->show();settle(300);auto *editor=w->editor();
  check("Traditional language aliases resolve",Research::supportedUiLocale("zh-Hant-HK")=="zh_TW"&&Research::supportedUiLocale("zh_HK")=="zh_TW"&&Research::supportedUiLocale("zh_CN")=="zh_CN"&&Research::supportedUiLocale("de_DE")=="en_US");
  const auto original=editor->toPlainText();auto cursor=editor->textCursor();cursor.setPosition(65);cursor.setPosition(72,QTextCursor::KeepAnchor);editor->setTextCursor(cursor);
  for(const auto &lang:QStringList{"zh_TW","en_US","zh_CN","zh_TW",locale}){
   app.applyTranslation(lang);settle();check("language round trip retains exact document and selection "+lang,editor->toPlainText()==original&&editor->textCursor().selectionStart()==65&&editor->textCursor().selectionEnd()==72);
   check("effective locale matches "+lang,Research::uiLocale()==lang);
   auto *menu=w->findChild<QMenu*>("researchLanguageMenu");QMetaObject::invokeMethod(menu,"aboutToShow");int checked=0;for(auto *a:menu->actions())checked+=a->isChecked();check("language menu has three choices and one selection "+lang,menu->actions().size()==3&&checked==1);
   if(lang=="zh_TW")check("shared terms use Traditional UI vocabulary",Research::words("保存","Save")=="儲存"&&Research::words("设置","Settings")=="設定"&&Research::ui("查阅此处上下文…")=="查閱此處上下文…");
  }
  check("search accepts both writing vocabularies",Research::uiSearchText("儲存寫作現場")==Research::uiSearchText("保存写作现场")&&Research::uiSearchText("原始碼 設定 檔案")==Research::uiSearchText("源码 设置 文件"));
  auto *intent=w->findChild<QAction*>("researchWritingContexts");
  check("Traditional command label can be found in any UI language",intent&&Research::commandMatchScore(intent,"儲存")>=0);
  if(Research::traditionalUi()){
   Research::Issue issue;issue.message="Missing $ inserted";
   check("compiler explanation uses Traditional Chinese",Research::issueExplanation(issue,false).startsWith("數學"));
   const auto path="C:/论文/设置.tex";
   check("export error translates its template and preserves the path",Research::localizedSlideExportError(QString("无法保存：")+path,false)==QString("無法儲存：")+path);
  }
  auto *probe=new QLabel(w);probe->setText("已保存文件：论文 文件 設定.tex");Research::retranslateWorkspace(probe);check("unknown user-bearing text is not transliterated",probe->text()=="已保存文件：论文 文件 設定.tex");delete probe;
  QTimer::singleShot(80,&app,[&]{
   auto *d=qobject_cast<QDialog*>(QApplication::activeModalWidget());check("settings opens",d);if(!d)return;
   d->resize(qEnvironmentVariableIsSet("TEXLINE_TEST_HALF")?520:880,650);settle(200);
   auto *tabs=d->findChild<QTabWidget*>("tabWidget");auto *languages=d->findChild<QComboBox*>("localePopup");
   const QStringList pageIds{"settingsGeneral","settingsEditor","settingsPdf","settingsShortcuts","settingsBuild","settingsAdvanced"};QStringList actualIds;for(int i=0;i<tabs->count();++i)actualIds.append(tabs->widget(i)->objectName());check("six settings categories retain stable identities",actualIds==pageIds);
   check("settings exposes only supported locales",languages&&languages->count()==3&&languages->findData("zh_TW")>=0&&languages->currentData()==locale);
   auto *search=d->findChild<QLineEdit*>("researchSettingsSearch");auto *matches=d->findChild<QListWidget*>("researchSettingsMatches");
   for(int i=0;i<tabs->count();++i){tabs->setCurrentIndex(i);settle(180);capture(d,"settings-"+QString::number(i));bool fit=true;
    for(auto *b:d->findChildren<QAbstractButton*>())if(b->isVisible()&&!b->text().isEmpty())fit&=b->height()>=b->fontMetrics().height()+2;
    check("button text has full vertical bounds page "+QString::number(i),fit);
   }
   search->setFocus();search->setText(Research::englishUi()?"completion":"補全");settle();QSet<QString> targets;bool unique=true;for(int i=0;i<matches->count();++i){const auto id=matches->item(i)->data(Qt::UserRole+1).toString();unique&=!targets.contains(id);targets.insert(id);}check("search results contain each control once",unique&&matches->count()>0);
   if(matches->currentItem())matches->itemClicked(matches->currentItem());check("single click opens a settings result",d->isVisible()&&!matches->isVisible());
   search->setFocus();search->setText(Research::englishUi()?"Encoding":"編碼");settle();
   key(search,Qt::Key_Return);settle();check("Enter navigates without saving or dismissing settings",d->isVisible()&&!matches->isVisible());search->setFocus();key(search,Qt::Key_Escape);check("first Escape clears search and preserves dialog",d->isVisible()&&search->text().isEmpty());
   search->setText(Research::englishUi()?"Encoding":"編碼");settle();key(search,Qt::Key_Return);check("Traditional query finds advanced setting",tabs->currentWidget()==d->findChild<QWidget*>("settingsAdvanced")&&d->isVisible());
   const auto snippetQuery=Research::englishUi()?"Expand snippets":Research::traditionalUi()?"啟用片段展開":"启用片段展开";
   auto *snippetToggle=d->findChild<QToolButton*>("researchSnippetDetailsToggle");check("snippet management has an explicit disclosure",snippetToggle);if(snippetToggle){if(snippetToggle->isChecked())snippetToggle->click();search->setText(snippetQuery);settle();key(search,Qt::Key_Return);settle();auto *snippetEnabled=d->findChild<QCheckBox*>("researchShorthandEnabledSetting");check("search opens and synchronizes snippet disclosure",tabs->currentWidget()==d->findChild<QWidget*>("settingsEditor")&&snippetToggle->isChecked()&&snippetEnabled&&snippetEnabled->isVisible());}
   auto *buildToggle=d->findChild<QToolButton*>("researchBuildDetailsToggle");check("compiler details have an explicit disclosure",buildToggle);if(buildToggle){if(buildToggle->isChecked())buildToggle->click();search->setText(Research::englishUi()?"Program locations":Research::traditionalUi()?"程式位置":"程序位置");settle();key(search,Qt::Key_Return);settle();auto *paths=d->findChild<QListWidget*>("binPathList");check("search opens and synchronizes compiler disclosure",tabs->currentWidget()==d->findChild<QWidget*>("settingsBuild")&&buildToggle->isChecked()&&paths&&paths->isVisible());}
   search->clear();key(search,Qt::Key_Escape);
  });PrefsDialog::doPrefsDialog(w);settle();check("cancel preserves document selection",editor->toPlainText()==original&&editor->textCursor().selectionStart()==65&&editor->textCursor().selectionEnd()==72);
  // Measure repeated visible-label localization separately from captures.
  QJsonArray samples;for(int i=0;i<9;++i){QElapsedTimer t;t.start();Research::retranslateWorkspace(w);w->grab();samples.append(t.nsecsElapsed()/1e6);}timings.append(QJsonObject{{"operation","workspace translation and raster"},{"milliseconds",samples}});
  capture(w,"workspace");QFile out(report);out.open(QIODevice::WriteOnly);out.write(QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"frames",frames},{"timings",timings},{"locale",locale},{"physicalInput",false}}).toJson());out.close();w->setModified(false);delete w;app.exit(failures?1:0);
 });QTimer::singleShot(90000,&app,[&]{app.exit(92);});return app.exec();
}
