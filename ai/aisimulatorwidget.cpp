#include "aisimulatorwidget.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

#include "analysischart.h"
#include "constants.h"
#include "lineeditnum.h"
#include "utils.h"

namespace deepvaccontrol {

namespace {
constexpr int    ANIM_INTERVAL_MS{16};
constexpr int    ANIM_TARGET_TICKS{120};
constexpr double INITIAL_TERM_MIN{-1.0};
constexpr double INITIAL_TERM_MAX{1.0};

QString formatNumber(double value, int prec = 2) {
  return std::isfinite(value) ? QString::number(value, 'f', prec) : QStringLiteral("—");
}
} // namespace

AiSimulatorWidget::AiSimulatorWidget(ResHolder *resHolder, QWidget *parent)
    : QWidget{parent}, m_resHolder{resHolder}, m_sim{&m_model} {
  initGui();
  loadModel();

  connect(&m_animTimer, &QTimer::timeout, this, &AiSimulatorWidget::advanceSimulation);
}

void AiSimulatorWidget::loadModel() {
  QDir const modelDir(QDir(QCoreApplication::applicationDirPath()).filePath("data/model"));
  bool const ok =
    m_model.load(modelDir.filePath("gru_plant_model.onnx"), modelDir.filePath("gru_plant_model.json"));

  setStatusText(m_model.statusMessage(), !ok);
  m_runBtn->setEnabled(ok);
}

void AiSimulatorWidget::initGui() {
  auto *mainLay = new QVBoxLayout(this);
  mainLay->setContentsMargins(4 * utils::ssf(), 4 * utils::ssf(), 4 * utils::ssf(), 4 * utils::ssf());
  mainLay->setSpacing(4 * utils::ssf());

  mainLay->addWidget(buildFormCard());
  mainLay->addWidget(buildStatsCard());
  mainLay->addWidget(buildChartCard(), 1);
}

QWidget *AiSimulatorWidget::buildFormCard() {
  auto *frame = new QFrame(this);
  frame->setFrameStyle(QFrame::Box | QFrame::Plain);
  frame->setStyleSheet("QFrame {border: 1px solid #b9b9b9;}");

  auto *lay = new QGridLayout(frame);
  lay->setSpacing(3 * utils::ssf());

  auto addField = [this, lay](int col, QString const &title, double min, double max, int prec, float defaultValue) {
    auto *label = new QLabel(title, lay->parentWidget());
    auto *edit  = new LineEditNum(min, max, prec, false, lay->parentWidget());
    edit->setFixedWidth(110 * utils::ssf());
    edit->setValue(defaultValue);
    lay->addWidget(label, 0, col, Qt::AlignHCenter);
    lay->addWidget(edit, 1, col);
    return edit;
  };

  m_kpEdit = addField(0, tr("Kp"), ai::KP_MIN, ai::KP_MAX, 2, ai::DEFAULT_KP);
  m_kiEdit = addField(1, tr("Ki"), ai::KI_MIN, ai::KI_MAX, 1, ai::DEFAULT_KI);
  m_kdEdit = addField(2, tr("Kd"), ai::KD_MIN, ai::KD_MAX, 2, ai::DEFAULT_KD);
  m_startTempEdit  = addField(3, tr("Start temp, °C"), -100, 150, 1, ai::DEFAULT_START_TEMP);
  m_targetTempEdit = addField(4, tr("Target, °C"), -100, 150, 1, ai::DEFAULT_TARGET_TEMP);
  m_durationEdit   = addField(5, tr("Duration, s"), 1, 100000, 0, ai::DEFAULT_DURATION_S);
  m_dtEdit         = addField(6, tr("Step, s"), 0.1, 60, 2, ai::DEFAULT_DT_S);
  m_initialPEdit   = addField(7, tr("Initial P"), INITIAL_TERM_MIN, INITIAL_TERM_MAX, 3, 0.0f);
  m_initialIEdit   = addField(8, tr("Initial I"), INITIAL_TERM_MIN, INITIAL_TERM_MAX, 3, 0.0f);
  m_initialDEdit   = addField(9, tr("Initial D"), INITIAL_TERM_MIN, INITIAL_TERM_MAX, 3, 0.0f);

  m_runBtn = new QPushButton(tr("Run simulation"), frame);
  m_runBtn->setFixedHeight(deepvaccontrol::UI_BTN_HEIGHT * utils::ssf());
  lay->addWidget(m_runBtn, 2, 0, 1, 5);

  m_statusLabel = new QLabel(frame);
  m_statusLabel->setWordWrap(true);
  lay->addWidget(m_statusLabel, 2, 5, 1, 5);

  lay->setColumnStretch(10, 1);

  connect(m_runBtn, &QPushButton::clicked, this, &AiSimulatorWidget::runSimulation);

  return frame;
}

QLabel *AiSimulatorWidget::addStatTile(QGridLayout *lay, int column, QString const &title) {
  auto *titleLabel = new QLabel(title, lay->parentWidget());
  auto  titleFont   = titleLabel->font();
  titleFont.setPointSize(qMax(7, titleFont.pointSize() - 1));
  titleLabel->setFont(titleFont);

  auto *valueLabel = new QLabel(QStringLiteral("—"), lay->parentWidget());
  auto  valueFont   = valueLabel->font();
  valueFont.setBold(true);
  valueFont.setPointSize(valueFont.pointSize() + 2);
  valueLabel->setFont(valueFont);

  lay->addWidget(titleLabel, 0, column, Qt::AlignHCenter);
  lay->addWidget(valueLabel, 1, column, Qt::AlignHCenter);

  return valueLabel;
}

QWidget *AiSimulatorWidget::buildStatsCard() {
  auto *frame = new QFrame(this);
  frame->setFrameStyle(QFrame::Box | QFrame::Plain);
  frame->setStyleSheet("QFrame {border: 1px solid #b9b9b9;}");

  auto *lay = new QGridLayout(frame);
  lay->setSpacing(3 * utils::ssf());

  m_costTile      = addStatTile(lay, 0, tr("Cost"));
  m_tailMaeTile    = addStatTile(lay, 1, tr("Tail MAE"));
  m_endTempTile    = addStatTile(lay, 2, tr("End temp, °C"));
  m_overshootTile  = addStatTile(lay, 3, tr("Overshoot, °C"));
  m_settleTile     = addStatTile(lay, 4, tr("Settle time, s"));

  lay->setColumnStretch(5, 1);

  return frame;
}

QWidget *AiSimulatorWidget::buildChartCard() {
  auto *frame = new QFrame(this);
  frame->setFrameStyle(QFrame::Box | QFrame::Plain);
  frame->setStyleSheet("QFrame {border: 1px solid #b9b9b9;}");

  auto *lay = new QVBoxLayout(frame);
  lay->setContentsMargins(2 * utils::ssf(), 2 * utils::ssf(), 2 * utils::ssf(), 2 * utils::ssf());

  auto *topLay = new QHBoxLayout();
  topLay->addWidget(new QLabel(tr("Channel:"), frame));

  // same channels as Deepvac Insight's simulator
  m_channelCombo = new QComboBox(frame);
  m_channelCombo->addItem(tr("Temperature"));
  m_channelCombo->addItem(tr("Error (setpoint − temp)"));
  m_channelCombo->addItem(tr("Control (u)"));
  m_channelCombo->addItem(tr("Predicted ΔT per step"));
  topLay->addWidget(m_channelCombo);
  topLay->addStretch();
  lay->addLayout(topLay);

  // shared analysis chart: crosshair, hover values, pan / zoom, box zoom
  m_chart = new AnalysisChart(frame);
  m_chart->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  lay->addWidget(m_chart, 1);

  connect(m_channelCombo, &QComboBox::currentIndexChanged, this, &AiSimulatorWidget::redrawChart);

  return frame;
}

void AiSimulatorWidget::setStatusText(QString const &text, bool isError) {
  m_statusLabel->setText(text);
  m_statusLabel->setStyleSheet(isError ? "color: #b03030;" : "");
}

void AiSimulatorWidget::runSimulation() {
  if (!m_model.isLoaded()) {
    setStatusText(m_model.statusMessage(), true);
    return;
  }

  ai::SimParams params;
  params.kp         = m_kpEdit->text().toDouble();
  params.ki         = m_kiEdit->text().toDouble();
  params.kd         = m_kdEdit->text().toDouble();
  params.startTemp  = m_startTempEdit->text().toDouble();
  params.targetTemp = m_targetTempEdit->text().toDouble();
  params.durationS  = m_durationEdit->text().toDouble();
  params.dtS        = m_dtEdit->text().toDouble();
  params.initialP   = m_initialPEdit->text().toDouble();
  params.initialI   = m_initialIEdit->text().toDouble();
  params.initialD   = m_initialDEdit->text().toDouble();

  if ((params.dtS <= 0.0) || (params.durationS <= 0.0)) {
    setStatusText(tr("Invalid scenario parameters"), true);
    return;
  }

  QString errorMsg;
  if (!m_sim.begin(params, &errorMsg)) {
    setStatusText(errorMsg, true);
    return;
  }

  m_runBtn->setEnabled(false);
  setStatusText(tr("Running simulation..."), false);

  m_runTitle = tr("Kp %1 / Ki %2 / Kd %3").arg(params.kp).arg(params.ki).arg(params.kd);
  m_chart->setSeries({}, 0.0);

  m_stepsPerTick = std::max(1, static_cast<int>(std::ceil(m_sim.totalSteps() / static_cast<double>(ANIM_TARGET_TICKS))));
  m_animTimer.start(ANIM_INTERVAL_MS);
}

void AiSimulatorWidget::advanceSimulation() {
  ai::TrajectoryPoint point;
  QString              errorMsg;

  for (int i{0}; (i < m_stepsPerTick) && !m_sim.isFinished(); ++i) {
    if (!m_sim.stepOnce(point, &errorMsg)) {
      m_animTimer.stop();
      setStatusText(errorMsg, true);
      m_runBtn->setEnabled(true);
      return;
    }
  }

  redrawChart();

  if (m_sim.isFinished()) {
    m_animTimer.stop();
    finishSimulation();
  }
}

void AiSimulatorWidget::finishSimulation() {
  ai::SimMetrics const metrics = m_sim.metrics();
  updateStatTiles(metrics);
  m_runBtn->setEnabled(true);

  if (metrics.valid) {
    setStatusText(m_runTitle, false);
  } else {
    setStatusText(tr("%1 (warning: %2)").arg(m_runTitle, metrics.invalidReason), true);
  }
}

void AiSimulatorWidget::redrawChart() {
  auto const &traj = m_sim.trajectory();

  QVector<double> xs;
  xs.reserve(traj.size());
  for (auto const &p : traj) {
    xs << p.elapsedS;
  }

  auto makeSeries = [&](QString const &label, QColor const &color, int width, auto value) {
    ChartSeries series;
    series.label = label;
    series.color = color;
    series.width = width;
    series.x     = xs;
    series.y.reserve(traj.size());
    for (auto const &p : traj) {
      series.y << value(p);
    }
    return series;
  };

  QVector<ChartSeries> series;
  switch (m_channelCombo->currentIndex()) {
    case 0:
      series << makeSeries(tr("Temperature, °C"), QColor(40, 100, 200), 2, [](auto const &p) { return p.temp; });
      series << makeSeries(tr("Setpoint, °C"), QColor(140, 140, 140), 1, [](auto const &p) { return p.tempRef; });
      break;
    case 1:
      series << makeSeries(tr("Error, °C"), QColor(220, 120, 0), 2, [](auto const &p) { return p.error; });
      break;
    case 2:
      series << makeSeries(tr("Control (u)"), QColor(200, 60, 60), 2, [](auto const &p) { return p.u; });
      break;
    default:
      series << makeSeries(tr("Predicted ΔT, °C"), QColor(130, 60, 200), 2, [](auto const &p) { return p.predDelta; });
      break;
  }

  m_chart->setSeries(series, 0.0);
  m_chart->resetView(); // follow the growing trajectory while animating
}

void AiSimulatorWidget::updateStatTiles(ai::SimMetrics const &metrics) {
  m_costTile->setText(formatNumber(metrics.cost, 2));
  m_tailMaeTile->setText(formatNumber(metrics.tailMae, 3));
  m_endTempTile->setText(formatNumber(metrics.endTemp, 2));
  m_overshootTile->setText(formatNumber(metrics.overshootMax, 2));
  m_settleTile->setText(formatNumber(metrics.timeToSettleS, 0));
}

} // namespace deepvaccontrol
