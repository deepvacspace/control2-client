#ifndef GRAPHWIDGET_H
#define GRAPHWIDGET_H

#include <QPushButton>
#include <QTimer>
#include <QWidget>

#include "chartpane.h"
#include "dataviewer.h"
#include "experimentlibrary.h"
#include "graphstyleeditor.h"
#include "resholder.h"
#include "runstore.h"

class QCheckBox;
class QDateTimeEdit;
class QSpinBox;
class QSplitter;

namespace deepvaccontrol {
class AnalysisNotesStore;
class ChartStatisticsDialog;
class OverlayDrawer;

/**
 * @brief Chart page: the live data of today and opened recorded data in tabs, optionally split into two panes side by
 * side. The buttons act on the focused pane; with Tooltip on, the panes mirror each other's crosshair (same time
 * from their start, same line).
 */
class GraphWidget : public QWidget {
  Q_OBJECT
public:
  explicit GraphWidget(ResHolder *resHolder, QWidget *parent = nullptr);

  void handleAccessChange(QString const &name, AccessManager::AccessLevel newLevel);
  // the experiment list (drawer) and the annotations, shared with the data analysis page
  void setAnalysis(ExperimentLibrary *library, AnalysisNotesStore *notes);

private:
  using Tab = ChartPane::Tab;

  void exportData();
  void updateDayButton();
  // labels box over the chart and value label at the mouse, independent of each other
  void updateLegendMode();
  // the calendar: experiments of the experiment database, or the live data of today
  void openCalendar();
  void showExperiment(storage::ExperimentSpan const &experiment);
  void showDay(QDate const &day, QVector<storage::ExperimentSpan> const &experiments);
  // an experiment of the experiment list (database or recorded days) or a time range
  void showRun(ExperimentRun const &run, bool splitRight);

  // a chart configured as the page's (styles, log axis, refresh period, labels / tooltip, annotate)
  [[nodiscard]] DataViewer *makeViewer();
  [[nodiscard]] DataViewer *viewer() const; // of the focused pane
  ChartPane                *addPane();
  ChartPane                *rightPane();     // created when the page is not split yet
  void                      removePane(ChartPane *pane);
  void                      setFocusedPane(ChartPane *pane);
  // opens the tab in the focused pane (or the right one), or selects it where it is open already
  void                      openTab(Tab const &tab, bool splitRight = false);
  void                      moveTab(ChartPane *from, int index, ChartPane *to);
  void                      showTabMenu(ChartPane *pane, int index, QPoint const &globalPos);
  void                      syncCursor(ChartPane *source, double x, int graph);
  void                      onAnnotationDrawn(ChartPane *pane, double x0, double x1);
  // the options drawer (right): time range, smoothing, overlays
  QWidget                  *buildOptions();
  void                      updateRangeFields();
  void                      applyViewOptions(DataViewer *chart) const;
  // the statistics window follows the focused pane and its tab
  void                      updateStatisticsSource();

  bool                 m_addLogAxis{false};
  QVector<int>         m_logAxisIndexes;
  QSplitter           *m_split{nullptr};
  QVector<ChartPane *> m_panes; // one, or two side by side
  ChartPane           *m_focused{nullptr};
  QPushButton         *m_labelsBtn{nullptr};
  QPushButton         *m_valuesBtn{nullptr};
  QPushButton         *m_annotateBtn{nullptr};
  QPushButton         *m_listBtn{nullptr};
  QPushButton         *m_dayBtn{nullptr};
  OverlayDrawer       *m_drawer{nullptr};
  OverlayDrawer       *m_optionsDrawer{nullptr};
  QPushButton         *m_statisticsBtn{nullptr};
  QPushButton         *m_optionsBtn{nullptr};
  ChartStatisticsDialog *m_statistics{nullptr};
  QDateTimeEdit       *m_fromEdit{nullptr};
  QDateTimeEdit       *m_toEdit{nullptr};
  QSpinBox            *m_smoothSpin{nullptr};
  QCheckBox           *m_minCheck{nullptr};
  QCheckBox           *m_maxCheck{nullptr};
  QCheckBox           *m_avgCheck{nullptr};
  bool                 m_loading{false};
  ResHolder           *m_resHolder;
  ExperimentLibrary   *m_library{nullptr};
  AnalysisNotesStore  *m_notes{nullptr};
  GraphStyleEditor    *m_styleEditor;
  QPushButton         *m_exportToBtn;
  QPushButton         *m_styleBtn;
};
} // namespace deepvaccontrol

#endif // GRAPHWIDGET_H
