// Local, reusable multi-file templates. GPL-2.0-or-later.
#pragma once
#include <QStringList>
#include <QImage>
#include <atomic>
class QWidget;
#include <memory>
namespace Research {
struct TemplateBundle {QString root,error;QStringList mains;int files{0};qint64 bytes{0};};
// The destination must not exist. Only this new directory is removed on failure.
TemplateBundle stageTemplate(const QString &input,const QString &destination,const std::shared_ptr<std::atomic_bool> &cancel,bool project=false);
struct TemplateInfo {QString root,main,name,engine,preview;bool valid()const{return !root.isEmpty();}};
TemplateInfo templateInfo(const QString &main);
QString saveTemplate(const TemplateBundle &bundle,const QString &main,const QString &name,const QString &engine,const QImage &preview,const QString &library,QString *error);
QString instantiateTemplate(const QString &main,const QString &destination,QString *error);
QString importTemplateDialog(QWidget *parent,const QString &library);
QString createTemplateProjectDialog(QWidget *parent,const QString &main);
QString importProjectDialog(QWidget *parent,const QString &archive);
}
