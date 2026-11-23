#ifndef ANALYSISEDITORAREA_H
#define ANALYSISEDITORAREA_H

#include <QList>
#include <QWidget>

class QSplitter;
class QStackedWidget;
class QTabWidget;

namespace deepvaccontrol {

class AnalysisRunPage;
class CrosshairSyncHub;

/**
 * @brief Tabbed area for opened experiments. Tabs can be split into several panes
 * side by side to look at experiments at the same time.
 */
class AnalysisEditorArea : public QWidget {
  Q_OBJECT
public:
  explicit AnalysisEditorArea(QWidget *parent = nullptr);

  bool                            focusPage(QString const &key);
  [[nodiscard]] AnalysisRunPage  *page(QString const &key) const;
  // splitRight: in a new pane to the right of the active one (when that one is not empty)
  void                            addPage(AnalysisRunPage *page, bool splitRight = false);
  // moves an opened experiment into a new pane to the right; false when it is not opened
  bool                            splitPageRight(QString const &key);
  [[nodiscard]] int               pageCount() const;
  [[nodiscard]] CrosshairSyncHub *syncHub() const;

private:
  QTabWidget *makeGroup();
  void        insertGroupNextTo(QTabWidget *existing, QTabWidget *group, Qt::Orientation orientation);
  void        showTabMenu(QTabWidget *group, int index, QPoint const &globalPos);
  void        splitPage(QTabWidget *source, int index, Qt::Orientation orientation);
  void        movePage(QTabWidget *source, int index, QTabWidget *target);
  void        closePage(QTabWidget *group, int index);
  void        removeGroupIfEmpty(QTabWidget *group);
  void        updatePlaceholder();

  QStackedWidget    *m_stack;
  QSplitter         *m_root;
  QList<QTabWidget *> m_groups;
  QTabWidget        *m_activeGroup{nullptr};
  CrosshairSyncHub  *m_syncHub;
};

inline CrosshairSyncHub *AnalysisEditorArea::syncHub() const {
  return m_syncHub;
}

}

#endif // ANALYSISEDITORAREA_H
