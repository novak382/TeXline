// Named writing places and local alternatives, backed by existing local history.
#pragma once
#include <QVariantMap>
class TeXDocumentWindow;
class QMenu;
namespace Research {
void installWritingContexts(TeXDocumentWindow *window);
void addWritingContextActions(QMenu *menu,TeXDocumentWindow *window,int position);
// Store under researchRecentPlace in the existing bounded recent-file record.
QVariantMap captureRecentWritingPlace(TeXDocumentWindow *window);
void scheduleRecentWritingPlace(TeXDocumentWindow *window,const QVariantMap &state);
void cancelRecentWritingPlace(TeXDocumentWindow *window);
}
