#ifndef CHARTSTATISTICSDIALOG_H
#define CHARTSTATISTICSDIALOG_H

#include <QDialog>
#include <QPointer>

#include "runinsight.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTabWidget;
class QTableWidget;
class QTimer;

namespace deepvaccontrol {
class AnalysisNotesStore;
class ComparisonPanel;
class DataViewer;
struct ResHolder;

/**
 * @brief Chart page statistics, as tabs of one window: statistics and control KPIs of the visible range, the
 * experiment's KPIs and stages, anomalies and events (tap a row to zoom to it), the annotations of the shown data and
 * its variable rules; for a comparison tab, the comparison of its experiments. Each tab's buttons show only while it
 * is selected. It follows the chart the page's buttons act on.
 */
class ChartStatisticsDialog : public QDialog {
  Q_OBJECT
public:
  ChartStatisticsDialog(ResHolder *resHolder, AnalysisNotesStore *notes, QWidget *parent = nullptr);

  // the chart shown (runKey: its annotations and rules, empty: none; channelNames: state name of each line, empty:
  // lines that are not channels, e.g. a comparison)
  void setSource(DataViewer *viewer, QString const &runKey, QString const &title, QStringList const &channelNames);
  // the analysis of the experiment shown (empty analysis: being computed / not available)
  void setInsight(RunInsight const &insight, bool computing);
  // a comparison tab: its own tab instead of the experiment ones
  void setComparisonMode(bool comparison);
  [[nodiscard]] ComparisonPanel *comparisonPanel() const;

signals:
  // show this range (epoch s) in the chart
  void zoomRequested(double from, double to);

private:
  QWidget *buildRangePage();
  QWidget *buildAnnotationsPage();
  QWidget *buildRulesPage();
  QWidget *buildKpiPage();
  QWidget *buildAnomaliesPage();
  QWidget *buildEventsPage();
  void     updateButtons();
  void     refreshRange();
  void     refreshAnnotations();
  void     refreshRules();
  void     editAnnotation(int row);
  void     addRule();
  [[nodiscard]] int selectedRow(QTableWidget *table) const;

  ResHolder          *m_resHolder;
  AnalysisNotesStore *m_notes;
  QPointer<DataViewer> m_viewer;
  QString             m_runKey;
  QStringList         m_channelNames;
  QTimer             *m_rangeTimer; // the visible range changed: statistics again (throttled)

  QTabWidget   *m_tabs{nullptr};
  QWidget      *m_rangePage{nullptr};
  QWidget      *m_kpiPage{nullptr};
  QWidget      *m_anomaliesPage{nullptr};
  QWidget      *m_eventsPage{nullptr};
  QWidget      *m_annotationsPage{nullptr};
  QWidget      *m_rulesPage{nullptr};
  ComparisonPanel *m_comparison{nullptr};
  RunInsight    m_insight;
  QVector<QLabel *> m_kpiTiles;
  QLabel       *m_kpiLabel2{nullptr};
  QTableWidget *m_stagesTable{nullptr};
  QLabel       *m_anomalyLabel{nullptr};
  QTableWidget *m_anomalyTable{nullptr};
  QLabel       *m_eventsLabel{nullptr};
  QTableWidget *m_eventsTable{nullptr};
  QLabel       *m_rangeLabel{nullptr};
  QTableWidget *m_rangeTable{nullptr};
  QTableWidget *m_annotationsTable{nullptr};
  QComboBox    *m_ruleChannelCombo{nullptr};
  QLineEdit    *m_ruleLoEdit{nullptr};
  QLineEdit    *m_ruleHiEdit{nullptr};
  QLineEdit    *m_ruleLabelEdit{nullptr};
  QComboBox    *m_ruleColorCombo{nullptr};
  QListWidget  *m_rulesList{nullptr};

  // bottom bar: the buttons of each tab
  QVector<QPushButton *> m_annotationButtons;
  QVector<QPushButton *> m_ruleButtons;
  QPushButton           *m_showBtn{nullptr};
  QPushButton           *m_editBtn{nullptr};
  QPushButton           *m_removeAnnotationBtn{nullptr};
  QPushButton           *m_removeRuleBtn{nullptr};
};

}

#endif // CHARTSTATISTICSDIALOG_H
