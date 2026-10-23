// Independent slide exports. No document, preview or user setting mutation.
#pragma once
#include "Engine.h"
#include <QHash>
#include <QStringList>
#include <atomic>
#include <memory>
namespace Research {
struct SlideExportRequest {
 QString root;
 // Complete template resources, including files loaded through class macros.
 QStringList extraDependencies;
 QHash<QString,QString> sources;
 Engine engine;
 QList<int> profiles;
 QStringList destinations;
 QHash<QString,QByteArray> destinationHashes;
};
struct SlideExportResult {QStringList written;QString error,log;bool cancelled{false};};
QByteArray slideExportHash(const QString &file);
QString slideProfileSource(QString source,int profile);
QString localizedSlideExportError(const QString &error,bool english);
SlideExportResult exportSlideCopies(const SlideExportRequest &request,const std::shared_ptr<std::atomic_bool> &cancel);
}
