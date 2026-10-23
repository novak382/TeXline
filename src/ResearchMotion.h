// Short, interruptible visual feedback. GPL-2.0-or-later.
#pragma once
class QWidget;
class QDockWidget;
class QTextDocument;
namespace Research {
namespace MotionMetrics { inline constexpr int TransitionMs = 160; inline constexpr int ArrivalDistance = 4; }
// Match active raster transitions to the current display, without an idle timer.
int motionFrameInterval(const QWidget *surface);
void installSourceLayoutCache(QTextDocument *document);
// Commit layout once. Only a temporary raster reveal moves; editing stays live.
void setPanelVisible(QDockWidget *panel, bool visible, int extent = 0, bool animate = true);
// Replace a revealed panel's raster immediately when its data becomes stale.
bool refreshPanelMotion(QDockWidget *panel);
void animateArrival(QWidget *content, int distance = MotionMetrics::ArrivalDistance);
void cancelArrival(QWidget *content);
}
