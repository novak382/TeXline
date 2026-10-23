// TeXworks Research external-edit reception and review. GPL-2.0-or-later.
#pragma once
#include "ResearchMerge.h"
#include <QObject>
#include <QTextCursor>
#include <QTimer>
class QAction;
class QDockWidget;
class QLabel;
class QTextBrowser;
class QPushButton;
class TeXDocumentWindow;
namespace Research {
class ProjectUpdates;
class Collaboration : public QObject {
  Q_OBJECT
public:
  explicit Collaboration(TeXDocumentWindow *window);
  ~Collaboration() override;
  void acceptedDisk(const QString &text);
  void fileChanged();
  void buildFinished();
  bool beforeSave(const QString &path);
  bool verifySave(const QString &path);
  void review();
  void receive(const QString &text); // Also used by deterministic native tests.
  int conflictCount() const;
  int reviewCount() const {return items.size();}
  void resolveCurrent(bool useExternal);
private:
  friend class ProjectUpdates;
  struct Item {QTextCursor range;QString before,after,category;bool conflict{false};QString kind;};
  ProjectUpdates *project{nullptr};
  TeXDocumentWindow *window;
  QString baseline, conflictBase, pendingRemote, stableCandidate;
  QByteArray expectedSaveHash;
  bool hasBaseline{false},reading{false},applying{false},hasCandidate{false};
  int generation{0},readRetries{0},index{0};
  QVector<Item> items;
  QTimer debounce,revisionTimer;
  QAction *reviewAction{nullptr};
  QDockWidget *panel{nullptr};
  QLabel *summary{nullptr},*heading{nullptr};QTextBrowser *preview{nullptr};
  QPushButton *theirs{nullptr};QAction *mine{nullptr},*merge{nullptr};
  quint64 reviewEpoch{0};
  QString reviewOriginal;
  bool hasReviewOriginal{false};
  void readAsync();
  void applyText(const QString &text,bool dirty);
  void updateActions();
  void showItem(bool navigate = true);
  void refreshPdfState();
  void attachProject();
  void stepReview(int direction);
  void finishReview();
  void useAllExternal();
  Collaboration *selectedOwner(int &itemIndex);
  bool applyChoice(int itemIndex, const QString &text);
  void editMerge();
};
}
