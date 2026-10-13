#ifndef COMPARISONPANEL_H
#define COMPARISONPANEL_H

#include <QJsonObject>
#include <QVector>
#include <QWidget>

#include "comparison.h"
#include "experimentlibrary.h"

class QComboBox;
class QLabel;
class QSpinBox;
class QStackedWidget;
class QTableWidget;

namespace deepvaccontrol {

// One run of a comparison (the first one is the reference the others are measured against).
struct ComparedRun {
  QString                         title;
  ExperimentRun                   run;
  core::ExperimentAnalysis const *analysis{nullptr};
  QVector<double>                 times; // elapsed s
  QVector<double>                 temp;
  QVector<double>                 reference;
  QVector<double>                 signal; // the compared channel, for the signal differences
  QJsonObject                     manifest; // core::RunManifest JSON of stored runs (configuration snapshot, run details)
};

/**
 * @brief Comparison of experiments: alignment (run start, n-th setpoint change, target crossing,
 * stage n) and the differences in KPIs, configuration, step responses and signal behaviour.
 */
class ComparisonPanel : public QWidget {
  Q_OBJECT
public:
  explicit ComparisonPanel(QWidget *parent = nullptr);

  // runs[0] is the reference; fewer than two runs shows a hint instead
  void setRuns(QVector<ComparedRun> const &runs, QString const &signalLabel);

  // time to add to each run's elapsed time so that its anchor falls on the reference run's anchor
  // (0 for runs without the anchor)
  [[nodiscard]] QVector<double> offsets() const;
  [[nodiscard]] QString         alignmentText() const;
  // the alignment (core::AlignMode, n-th): set from outside without alignmentChanged()
  void                          setAlignment(int mode, int n);
  [[nodiscard]] int             alignMode() const;
  [[nodiscard]] int             alignN() const;
  // the comparison as HTML (experiment report)
  [[nodiscard]] QString html() const;

signals:
  void alignmentChanged();

private:
  void recompute();
  void fillKpis();
  void fillConfiguration();
  void fillSteps();
  void fillSignals();

  QVector<ComparedRun> m_runs;
  QString              m_signalLabel;
  QVector<double>      m_anchors;
  QVector<double>      m_offsets;

  QStackedWidget *m_stack{nullptr};
  QComboBox      *m_alignCombo{nullptr};
  QSpinBox       *m_alignSpin{nullptr};
  QLabel         *m_anchorLabel{nullptr};
  QTableWidget   *m_kpiTable{nullptr};
  QTableWidget   *m_configTable{nullptr};
  QTableWidget   *m_stepsTable{nullptr};
  QTableWidget   *m_signalTable{nullptr};
};

} // namespace deepvaccontrol

#endif // COMPARISONPANEL_H
