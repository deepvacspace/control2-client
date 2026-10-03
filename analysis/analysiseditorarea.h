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
 * (side by side or stacked) to look at experiments at the same time.
 */
class AnalysisEditorArea : public QWidget {
  Q_OBJECT
public:
  explicit AnalysisEditorArea(QWidget *parent = nullptr);

  bool                            focusPage(QString const &key);
  [[nodiscard]] AnalysisRunPage  *page(QString const &key) const;
  void                            addPage(AnalysisRunPage *page);
  [[nodiscard]] int               pageCount() const;
  [[nodiscard]] CrosshairSyncHub *syncHub() const;

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  QTabWidget *makeGroup();
  void        placeMenuButton(QTabWidget *group);
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

} // namespace deepvaccontrol

#endif // ANALYSISEDITORAREA_H
