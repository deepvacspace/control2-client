#ifndef AISIMULATORWIDGET_H
#define AISIMULATORWIDGET_H

#include <QLabel>
#include <QTimer>
#include <QWidget>

#include "chambertwinsim.h"
#include "gruonnxmodel.h"
#include "resholder.h"

class QComboBox;
class QCustomPlot;
class QGridLayout;
class QPushButton;

namespace control2 {
class LineEditNum;

// Simulator tab of the AI page: a closed-loop chamber simulator driven by
// the GRU digital-twin plant model exported from the foresight repo (ONNX)
// plus a C++ port of its ChamberPID/CodesysDiff control-loop primitives
// (see chambertwinsim.h). UI/UX mirrors deepvac/insight's Simulator view: a
// PID/scenario form, a Run button, summary stat tiles, and an animated
// trajectory chart.
class AiSimulatorWidget : public QWidget {
  Q_OBJECT
public:
  explicit AiSimulatorWidget(ResHolder *resHolder, QWidget *parent = nullptr);

private:
  void     initGui();
  void     loadModel();
  void     runSimulation();
  void     advanceSimulation();
  void     finishSimulation();
  void     redrawChart();
  void     updateStatTiles(ai::SimMetrics const &metrics);
  void     setStatusText(QString const &text, bool isError);
  QLabel  *addStatTile(QGridLayout *lay, int column, QString const &title);
  QWidget *buildFormCard();
  QWidget *buildStatsCard();
  QWidget *buildChartCard();

  ResHolder         *m_resHolder;
  GruOnnxModel        m_model;
  ai::ChamberTwinSim  m_sim;

  LineEditNum *m_kpEdit{nullptr};
  LineEditNum *m_kiEdit{nullptr};
  LineEditNum *m_kdEdit{nullptr};
  LineEditNum *m_startTempEdit{nullptr};
  LineEditNum *m_targetTempEdit{nullptr};
  LineEditNum *m_durationEdit{nullptr};
  LineEditNum *m_dtEdit{nullptr};
  LineEditNum *m_initialPEdit{nullptr};
  LineEditNum *m_initialIEdit{nullptr};
  LineEditNum *m_initialDEdit{nullptr};

  QPushButton *m_runBtn{nullptr};
  QLabel      *m_statusLabel{nullptr};

  QLabel *m_costTile{nullptr};
  QLabel *m_tailMaeTile{nullptr};
  QLabel *m_endTempTile{nullptr};
  QLabel *m_overshootTile{nullptr};
  QLabel *m_settleTile{nullptr};

  QComboBox   *m_channelCombo{nullptr};
  QCustomPlot *m_plot{nullptr};
  QTimer       m_animTimer;
  int          m_stepsPerTick{1};
};
} // namespace control2

#endif // AISIMULATORWIDGET_H
