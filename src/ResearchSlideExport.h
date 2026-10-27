// Independent slide exports. No document, preview or user setting mutation.
#pragma once
#include "Engine.h"
#include <QHash>
#include <QStringList>
#include <atomic>
#include <memory>
namespace Research {
struct SlideExportResult;
struct SlideExportRequest;
// One transient editor owns a session; no global or on-disk persistent cache.
class SlidePreviewSession {
public:
 SlidePreviewSession();
 ~SlidePreviewSession();
 SlidePreviewSession(const SlidePreviewSession &)=delete;
 SlidePreviewSession &operator=(const SlidePreviewSession &)=delete;
private:
 struct State;std::unique_ptr<State> state;
 friend SlideExportResult exportSlideCopies(const SlideExportRequest &,const std::shared_ptr<std::atomic_bool> &);
};
struct SlideExportRequest {
 QString root;
 // Complete template resources, including files loaded through class macros.
 QStringList extraDependencies;
 QHash<QString,QString> sources;
 Engine engine;
 QList<int> profiles;
 QStringList destinations;
 QHash<QString,QByteArray> destinationHashes;
 // Only isolated profile-0 previews opt in; exports keep their existing path.
 std::shared_ptr<SlidePreviewSession> previewSession;
};
struct SlideExportResult {
 QStringList written;QString error,log;bool cancelled{false};
 // Worker timings exclude the editor debounce and PDF rendering.
 double preparationMs{0},compilationMs{0},publicationMs{0};int enginePasses{0};
 bool reusedWorkspace{false};
};
QByteArray slideExportHash(const QString &file);
QString slideProfileSource(QString source,int profile);
QString localizedSlideExportError(const QString &error,bool english);
SlideExportResult exportSlideCopies(const SlideExportRequest &request,const std::shared_ptr<std::atomic_bool> &cancel);
}
