#ifndef CHARTSTATISTICSDIALOG_H
#define CHARTSTATISTICSDIALOG_H

#include <QDialog>
#include <QPointer>

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
class DataViewer;
struct ResHolder;

/**
 * @brief Chart page statistics, as tabs of one window: statistics and control KPIs of the visible range, the
 * annotations of the shown data and its variable rules. Each tab's buttons show only while it is selected.
 * It follows the chart the page's buttons act on.
 */
class ChartStatisticsDialog : public QDialog {
  Q_OBJECT
public:
  ChartStatisticsDialog(ResHolder *resHolder, AnalysisNotesStore *notes, QWidget *parent = nullptr);

  // the chart shown (runKey: its annotations and rules, empty: none; channelNames: state name of each line, empty:
  // lines that are not channels, e.g. a comparison)
  void setSource(DataViewer *viewer, QString const &runKey, QString const &title, QStringList const &channelNames);

signals:
  // show this range (epoch s) in the chart
  void zoomRequested(double from, double to);

private:
  QWidget *buildRangePage();
  QWidget *buildAnnotationsPage();
  QWidget *buildRulesPage();
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
  QLabel       *m_rangeLabel{nullptr};
  QTableWidget *m_rangeTable{nullptr};
  QLabel       *m_kpiLabel{nullptr};
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

} // namespace deepvaccontrol

#endif // CHARTSTATISTICSDIALOG_H
