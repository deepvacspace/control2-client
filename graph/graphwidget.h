#ifndef GRAPHWIDGET_H
#define GRAPHWIDGET_H

#include <QPushButton>
#include <QTimer>
#include <QWidget>
#include <functional>

#include "chartpane.h"
#include "comparisonpanel.h"
#include "derivedvariablesstore.h"
#include "runinsight.h"
#include "dataviewer.h"
#include "experimentlibrary.h"
#include "graphstyleeditor.h"
#include "resholder.h"
#include "runstore.h"

class QAction;
class QAbstractButton;
class QCheckBox;
class QAbstractButton;
class QComboBox;
class QRadioButton;
class QJsonObject;
class QGridLayout;
class QSpinBox;
class QSplitter;

namespace deepvaccontrol {
class AnalysisNotesStore;
class AnalysisServices;
class ChartStatisticsDialog;
class DateTimeField;
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
  ~GraphWidget() override; // the session is saved

  void handleAccessChange(QString const &name, AccessManager::AccessLevel newLevel);

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
  // the experiment list (drawer), notes and reports of the shared analysis services
  void initAnalysis();
  // an experiment of the experiment list (database or recorded days) or a time range; from / to: elapsed s to show
  void showRun(ExperimentRun const &run, bool splitRight, double from = qQNaN(), double to = qQNaN());
  // what the assistant asks to see (other runs: a comparison)
  QString showFromServices(ExperimentRun const &run, QStringList const &channels, double from, double to,
                           QVector<ExperimentRun> const &compare);
  // ---- comparisons: a tab with its own chart, the channels of several experiments on an aligned time axis
  // editKey: the comparison tab to change; empty: a new one
  void openCompareDialog(QString const &editKey = {});
  // returns the new tab's key
  QString createComparison(ChartComparison const &comparison, bool splitRight = false);
  // reads, aligns and puts the experiments on a common time axis (in the background), then fills the tab
  void computeComparison(QString const &key, ChartComparison const &comparison);
  // labels and line styles of a comparison's chart
  void styleComparison(DataViewer *chart, ChartComparison const &comparison) const;
  [[nodiscard]] QPair<QString, QString> comparisonTitles(ChartComparison const &comparison) const; // tab label, full name
  [[nodiscard]] DataViewer *makeComparisonViewer();
  // the experiment a tab shows, as the library knows it (a time range for the live data and days)
  [[nodiscard]] ExperimentRun runOfTab(Tab const &tab) const;

  // a chart configured as the page's (styles, log axis, refresh period, labels / tooltip, annotate)
  [[nodiscard]] DataViewer *makeViewer();
  [[nodiscard]] DataViewer *viewer() const; // of the focused pane
  ChartPane                *addPane();
  ChartPane                *rightPane();     // created when the page is not split yet
  void                      removePane(ChartPane *pane);
  void                      setFocusedPane(ChartPane *pane);
  // opens the tab in the focused pane (or the right one), or selects it where it is open already
  void                      openTab(Tab const &tab, bool splitRight = false);
  void                      moveTab(ChartPane *from, int index, ChartPane *to, int at = -1);
  void                      onTabDropped(ChartPane *target, ChartPane *source, QString const &key, int at, bool rightHalf);
  // samples read in the background: named columns (empty: in the order of the states)
  struct Samples {
    QVector<double>          times;
    QStringList              columns;
    QVector<QVector<double>> values;
  };
  // opens a tab with a spinner and fills it when `read` (run in another thread) is done
  void                      openLoading(Tab tab, bool splitRight, std::function<bool(Samples &out, QString &error)> read);
  [[nodiscard]] ChartPane  *paneWith(QString const &key) const;
  // the states of the chart from named columns (empty names: in the order of the states)
  [[nodiscard]] std::shared_ptr<DataHolder const> holderFrom(QVector<double> const &times, QStringList const &columns,
                                                             QVector<QVector<double>> const &values) const;
  void                      editExperimentDetails();
  // an experiment of the experiment database
  void                      openStored(QString const &storeKey, QString const &label, QString const &name);
  // ---- the session (open tabs, split, their zoom, options) kept across restarts
  void                      scheduleSessionSave();
  void                      saveSession() const;
  void                      restoreSession();
  [[nodiscard]] QJsonObject tabJson(ChartPane *pane, int index) const;
  void                      updateOptions();
  void                      showTabMenu(ChartPane *pane, int index, QPoint const &globalPos);
  void                      syncCursor(ChartPane *source, double x, int graph);
  void                      onAnnotationDrawn(ChartPane *pane, double x0, double x1);
  // the options drawer (right): time range, smoothing, overlays
  QWidget                  *buildOptions();
  void                      updateRangeFields();
  void                      applyViewOptions(DataViewer *chart) const;
  // the statistics window follows the focused pane and its tab
  void                      updateStatisticsSource();
  // ---- the analysis of the experiment a pane shows (KPIs, stages, anomalies, events): computed in the background
  // when the statistics window or a marker toggle needs it, kept per tab
  [[nodiscard]] bool        needInsight() const;
  void                      requestInsights();
  void                      requestInsight(ChartPane *pane);
  void                      applyInsight(ChartPane *pane);
  // the setpoint line of every chart (Actions ▸ Setpoint)
  void                      applySetpointLine();
  // the experiment shown: number, DUT, purpose, operator, duration, samples
  void                      showInfo();
  // the comparison tab of the statistics window
  void                      refreshComparisonStatistics();
  // ---- calculated channels (Options): which ones are drawn, computed from the data shown
  void                      refreshDerivedList();
  // the live tab follows the chamber: drawn while a job runs, blank otherwise
  void                      updateLiveActivity();
  // smoothing of the Options drawer, in samples (1: off)
  [[nodiscard]] int         smoothing() const;
  void                      setSmoothing(int samples);
  // "Comparison", then "Comparison 2", 3, ... while several are open
  [[nodiscard]] QString     nextComparisonLabel() const;
  void                      refreshDerived(ChartPane *pane);
  void                      manageDerived();
  // the comparison as CSV: one row per experiment and channel
  bool                      writeComparisonCsv(Tab const &tab, QString const &path) const;

  bool                 m_addLogAxis{false};
  QVector<int>         m_logAxisIndexes;
  QSplitter           *m_split{nullptr};
  QVector<ChartPane *> m_panes; // one, or two side by side
  ChartPane           *m_focused{nullptr};
  QPushButton         *m_actionsBtn{nullptr}; // labels, tooltip, markers, setpoint, export
  QPushButton         *m_infoBtn{nullptr};
  QAction             *m_labelsBtn{nullptr};
  QAction             *m_valuesBtn{nullptr};
  QAction             *m_eventsAct{nullptr};
  QAction             *m_anomaliesAct{nullptr};
  QAction             *m_stagesAct{nullptr};
  QAction             *m_setpointAct{nullptr};
  QHash<QString, RunInsight> m_insights; // by tab key
  QHash<QString, qint64>     m_insightTimes; // when computed (ms): the live tab is computed again now and then
  QSet<QString>              m_insightPending;
  QTimer                    *m_insightTimer{nullptr};
  // comparison tabs: the analyses of their experiments (comparison tables)
  struct ComparisonData {
    QVector<std::shared_ptr<core::ExperimentAnalysis const>> analyses;
    QVector<ComparedRun>                                     runs;
    QString                                                  signalLabel;
  };
  QHash<QString, ComparisonData> m_comparisonData;
  QVector<storage::DerivedVariable> m_derived;      // definitions
  QStringList                       m_derivedShown; // names drawn
  QWidget                          *m_derivedBox{nullptr};
  bool                              m_liveActive{false};
  QString                           m_liveIdleText;
  QPushButton         *m_annotateBtn{nullptr};
  QPushButton         *m_listBtn{nullptr};
  QPushButton         *m_dayBtn{nullptr};
  OverlayDrawer       *m_drawer{nullptr};
  OverlayDrawer       *m_optionsDrawer{nullptr};
  QPushButton         *m_statisticsBtn{nullptr};
  QPushButton         *m_optionsBtn{nullptr};
  ChartStatisticsDialog *m_statistics{nullptr};
  DateTimeField       *m_fromEdit{nullptr};
  DateTimeField       *m_toEdit{nullptr};
  QComboBox           *m_smoothCombo{nullptr}; // samples averaged (1: off)
  QComboBox           *m_overlayChannel{nullptr};
  QAbstractButton     *m_minCheck{nullptr};
  QAbstractButton     *m_maxCheck{nullptr};
  QAbstractButton     *m_avgCheck{nullptr};
  QAbstractButton     *m_relativeRadio{nullptr};
  QPushButton         *m_detailsBtn{nullptr};
  AnalysisServices    *m_services{nullptr};
  QPushButton         *m_compareBtn{nullptr};
  int                  m_comparisons{0}; // comparison tabs made (their keys)
  bool                 m_canAnnotate{false};
  int                  m_dragDepth{0}; // tabs being dragged: panes stay until it ends
  QAbstractButton     *m_scatterRadio{nullptr};
  QTimer              *m_sessionTimer{nullptr};
  bool                 m_restoring{false};
  ChartPane           *m_restoreTarget{nullptr}; // while restoring: the pane tabs open in
  QJsonObject         *m_restoreView{nullptr};   // and the zoom they get
  ResHolder           *m_resHolder;
  ExperimentLibrary   *m_library{nullptr};
  AnalysisNotesStore  *m_notes{nullptr};
  GraphStyleEditor    *m_styleEditor;
  QAction             *m_exportToBtn;
  QPushButton         *m_styleBtn;
};
} // namespace deepvaccontrol

#endif // GRAPHWIDGET_H
