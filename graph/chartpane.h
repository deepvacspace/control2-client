#ifndef CHARTPANE_H
#define CHARTPANE_H

#include <QFrame>
#include <QJsonObject>
#include <memory>

#include "chartcomparison.h"
#include "charttoolbar.h"
#include "dataholder.h"

class QHBoxLayout;
class QStackedWidget;
class QToolButton;
class QTabBar;

namespace deepvaccontrol {
class AnalysisNotesStore;
class DataManager;
class DataViewer;

/**
 * @brief One side of the chart page: a row of small tabs and the chart of the selected one. A tab shows the live data
 * of today (only in the first pane, where it stays first and cannot be closed) or recorded data. Tabs are reordered
 * by dragging them along the row, and dragged to the other pane (or to the right half of the chart to split).
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
    QString                           storeKey; // experiment database key (experiment details, bundle), if any
    bool                              loading{false}; // data still being read (a spinner in the tab)
    std::shared_ptr<ChartComparison const> comparison; // set: a comparison tab
    DataViewer                       *ownViewer{nullptr}; // comparison tabs have their own chart (in the pane holding them)
    double                            zoomFrom{qQNaN()}; // elapsed s: shown once the data is there
    double                            zoomTo{qQNaN()};
    QJsonObject                       view; // its zoom while another tab is shown (DataViewer::viewState)
    ExperimentRun                     run;  // an experiment of the experiment list: how to read it again
  };
  static QString const LIVE_KEY;

  // takes the chart (configured by the page); channelNames: state name of each line (variable rules)
  ChartPane(DataViewer *viewer, DataManager *dataManager, QStringList const &channelNames, QWidget *parent = nullptr);

  // the chart shown (a comparison tab's own one, or the pane's)
  [[nodiscard]] DataViewer *viewer() const;
  // the pane's chart (live and recorded tabs) and all of its charts
  [[nodiscard]] DataViewer *mainViewer() const;
  [[nodiscard]] QVector<DataViewer *> viewers() const;
  [[nodiscard]] QTabBar    *tabBar() const;
  [[nodiscard]] int         count() const;
  [[nodiscard]] Tab const  *tab(int index) const;
  [[nodiscard]] Tab const  *currentTab() const;
  [[nodiscard]] int         indexOf(QString const &key) const;
  // the zoom of a tab (the chart's own while it is shown)
  [[nodiscard]] QJsonObject viewOf(int index) const;
  void                      setTabView(QString const &key, QJsonObject const &view);
  [[nodiscard]] bool        liveShown() const;
  // epoch s of the first sample shown (NaN: none)
  [[nodiscard]] double      dataStart() const;

  // selects the tab with that key; false when it is not open here
  bool selectTab(QString const &key);
  // opens (and selects) the tab at `at` (-1: at the end), or selects it when its key is open already
  void addTab(Tab const &tab, int at = -1);
  // removes the tab (its own chart, if any, is the caller's now); emits emptied() when it was the last one
  Tab  takeTab(int index);
  // removes the tab for good
  void closeTab(int index);
  // a comparison tab gets new settings: it shows a spinner until setTabData()
  void updateTab(QString const &key, QString const &label, QString const &name, std::shared_ptr<ChartComparison const> const &comparison);
  // the data of a loading tab arrived
  void setTabData(QString const &key, std::shared_ptr<DataHolder const> const &data);

  void setNotes(AnalysisNotesStore *notes);
  // annotations and variable rules of the shown tab
  void refreshRegions();
  // live tab: the data manager started a new interval
  void reloadLive(double key);
  // live tab: drawn only while the chamber runs a job (else blank, with `idleText`)
  void setLiveActive(bool active, QString const &idleText);
  // highlighted as the pane the buttons act on (only while the page is split)
  void setHighlighted(bool highlighted);
  // the left pane: a tab dropped on the right half of its own chart splits the page
  void setPrimary(bool primary);
  // ---- the chart's tools (top left of the plot) and the zoom / pan pad (bottom left)
  [[nodiscard]] ChartToolbar *toolbar() const;
  void setToolbarPinned(bool pinned);
  // only the left pane shows the tools; they act on whichever pane is selected
  void setToolbarShown(bool shown);
  // the tool in use on this pane's charts; pad: the zoom / pan pad here (the selected pane)
  void setTool(ChartToolbar::Tool tool, bool pad);

signals:
  void activated(); // clicked: the buttons now act on this pane
  // from the chart shown
  void annotationDrawn(double x0, double x1);
  void cursorMoved(double x, int graph);
  void cursorHidden();
  void currentTabChanged();
  void tabsChanged(); // opened, closed, moved or filled
  void tabMenuRequested(int index, QPoint const &globalPos);
  void emptied();
  // a tab of `source` dropped here: at `at` in the tab row (-1: at the end); rightHalf: on the right half of the chart
  void tabDropped(ChartPane *source, QString const &key, int at, bool rightHalf);
  // a tab is being dragged out of this pane (no pane may go away until it ends)
  void tabDragStarted();
  void tabDragFinished();
  // the + after the tabs: open an experiment or a comparison here
  void addRequested(QPoint const &globalPos);
  // the pad's middle icon: back to Inspect
  void toolResetRequested();

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;
  void dragEnterEvent(QDragEnterEvent *event) override;
  void dragMoveEvent(QDragMoveEvent *event) override;
  void dragLeaveEvent(QDragLeaveEvent *event) override;
  void dropEvent(QDropEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;

private:
  // the tool in use on the charts of the pane, the pad shown or not
  void applyTool();
  // the floating tools and the pad in the corners of the plot area
  void placeOverlays();

private:
  void showTab(int index);
  void attachViewer(DataViewer *viewer);
  void detachViewer(DataViewer *viewer);
  void startTabDrag(QString const &key);
  // where a drop at pos would go; false when it would do nothing
  bool dropTarget(ChartPane *source, QPoint const &pos, int &at, bool &rightHalf, QRect &hint) const;

  DataViewer         *m_viewer;
  DataManager        *m_dataManager;
  QStringList         m_channelNames;
  QTabBar            *m_tabBar;
  QStackedWidget     *m_stack;
  QHBoxLayout        *m_chartRow{nullptr}; // the pinned tools, then the chart
  ChartToolbar       *m_toolbar{nullptr};
  ChartPad           *m_pad{nullptr};
  QToolButton        *m_addBtn{nullptr};
  ChartToolbar::Tool  m_tool{ChartToolbar::Tool::INSPECT};
  bool                m_padShown{false};
  // the floating tools moved by the user: where, from the chart's top left corner
  bool                m_toolbarMoved{false};
  QPoint              m_toolbarOffset;
  QMetaObject::Connection m_replotConnection; // the overlays follow the plot's area
  QWidget            *m_dropHint;
  QVector<Tab>        m_tabs;
  AnalysisNotesStore *m_notes{nullptr};
  bool                m_primary{false};
  bool                m_liveActive{false};
  QString             m_liveIdleText;
  // tab pressed in the row (drag out of it)
  QString             m_shownKey; // the tab the pane's chart shows now (its zoom is kept when another is shown)
  QString             m_pressKey;
  QPoint              m_pressPos;
};

inline DataViewer *ChartPane::mainViewer() const {
  return m_viewer;
}

inline QTabBar *ChartPane::tabBar() const {
  return m_tabBar;
}

inline int ChartPane::count() const {
  return static_cast<int>(m_tabs.size());
}

}

#endif // CHARTPANE_H
