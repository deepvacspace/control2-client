#ifndef CHARTPANE_H
#define CHARTPANE_H

#include <QFrame>
#include <memory>

#include "dataholder.h"

class QTabBar;

namespace deepvaccontrol {
class AnalysisNotesStore;
class DataManager;
class DataViewer;

/**
 * @brief One side of the chart page: a row of small tabs and the chart of the selected one. A tab shows the live data
 * of today (only in the first pane, where it cannot be closed) or recorded data.
 */
class ChartPane : public QFrame {
  Q_OBJECT
public:
  struct Tab {
    QString                           key;    // "live", "db:<key>", "day:<date>" or an experiment library key
    QString                           label;  // short: tab and day button
    QString                           name;   // full: tool tip and title on the chart
    QString                           runKey; // annotations drawn here belong to it
    std::shared_ptr<DataHolder const> data;   // recorded data; the live one comes from the data manager
  };
  static QString const LIVE_KEY;

  // takes the chart (configured by the page); channelNames: state name of each line (variable rules)
  ChartPane(DataViewer *viewer, DataManager *dataManager, QStringList const &channelNames, QWidget *parent = nullptr);

  [[nodiscard]] DataViewer *viewer() const;
  [[nodiscard]] QTabBar    *tabBar() const;
  [[nodiscard]] int         count() const;
  [[nodiscard]] Tab const  *tab(int index) const;
  [[nodiscard]] Tab const  *currentTab() const;
  [[nodiscard]] bool        liveShown() const;
  // epoch s of the first sample shown (NaN: none)
  [[nodiscard]] double      dataStart() const;

  // selects the tab with that key; false when it is not open here
  bool selectTab(QString const &key);
  // opens (and selects) the tab, or selects it when its key is open already
  void addTab(Tab const &tab);
  // removes the tab; emitted emptied() when it was the last one
  Tab  takeTab(int index);

  void setNotes(AnalysisNotesStore *notes);
  // annotations and variable rules of the shown tab
  void refreshRegions();
  // live tab: the data manager started a new interval
  void reloadLive(double key);
  // highlighted as the pane the buttons act on (only while the page is split)
  void setHighlighted(bool highlighted);

signals:
  void activated(); // clicked: the buttons now act on this pane
  void currentTabChanged();
  void tabMenuRequested(int index, QPoint const &globalPos);
  void emptied();

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  void showTab(int index);

  DataViewer         *m_viewer;
  DataManager        *m_dataManager;
  QStringList         m_channelNames;
  QTabBar            *m_tabBar;
  QVector<Tab>        m_tabs;
  AnalysisNotesStore *m_notes{nullptr};
};

inline DataViewer *ChartPane::viewer() const {
  return m_viewer;
}

inline QTabBar *ChartPane::tabBar() const {
  return m_tabBar;
}

inline int ChartPane::count() const {
  return static_cast<int>(m_tabs.size());
}

} // namespace deepvaccontrol

#endif // CHARTPANE_H
