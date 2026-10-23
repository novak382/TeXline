// Optional first-use introduction. GPL-2.0-or-later.
#pragma once
class QWidget;
class QMainWindow;
class QMenu;
class QString;
namespace Research {
// The content pack is reread on each opening; seen state is independent of it.
void showOnboarding(QWidget *parent = nullptr, bool firstUse = false);
void showOnboardingTopic(QWidget *parent,const QString &topic);
void scheduleOnboarding();
void installOnboardingMenu(QMainWindow *window, QMenu *normal, QMenu *focused);
}
