#include "aisimulatorwidget.h"

#include <QButtonGroup>
#include <QLabel>
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
#include "theme.h"
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
    m_model.load(modelDir.filePath("twin_model.onnx"), modelDir.filePath("twin_model.json"));

  setStatusText(ok ? QString() : m_model.statusMessage(), !ok); // only problems are shown
  m_runBtn->setEnabled(ok);
}

namespace {
// a card with an optional title
QFrame *makeCard(QWidget *parent, QString const &title) {
  double const ssf{utils::ssf()};
  auto        *card = new QFrame(parent);
  card->setObjectName("pageCard");
  auto *lay = new QVBoxLayout(card);
  lay->setContentsMargins(14 * ssf, 10 * ssf, 14 * ssf, 12 * ssf);
  lay->setSpacing(8 * ssf);
  if (!title.isEmpty()) {
    auto *label = new QLabel(title, card);
    label->setObjectName("cardTitle");
    lay->addWidget(label);
  }
  return card;
}

// small uppercase caption over a group of fields
QLabel *groupCaption(QString const &text, QWidget *parent) {
  auto *label = new QLabel(text.toUpper(), parent);
  label->setProperty("muted", true);
  QFont font = label->font();
  font.setPointSizeF(font.pointSizeF() * 0.85);
  font.setLetterSpacing(QFont::PercentageSpacing, 106);
  label->setFont(font);
  return label;
}
} // namespace

void AiSimulatorWidget::initGui() {
  double const ssf{utils::ssf()};
  auto        *mainLay = new QVBoxLayout(this);
  mainLay->setContentsMargins(0, 8 * ssf, 0, 0);
  mainLay->setSpacing(10 * ssf);

  // scenario and results in one row (can be hidden from the chart header), the chart below
  m_scenarioRow = makeCard(this, {});
  auto *rowLay  = new QHBoxLayout();
  rowLay->setSpacing(10 * ssf);
  static_cast<QVBoxLayout *>(m_scenarioRow->layout())->addLayout(rowLay);
  rowLay->addWidget(buildFormCard());
  auto *divider = new QFrame(m_scenarioRow);
  divider->setObjectName("cardDivider");
  divider->setFixedWidth(qMax(1, static_cast<int>(1 * ssf)));
  rowLay->addSpacing(6 * ssf);
  rowLay->addWidget(divider);
  rowLay->addSpacing(6 * ssf);
  rowLay->addWidget(buildStatsCard(), 1);
  mainLay->addWidget(m_scenarioRow);
  mainLay->addWidget(buildChartCard(), 1);
}

QWidget *AiSimulatorWidget::buildFormCard() {
  double const ssf{utils::ssf()};
  auto        *box = new QWidget(this);
  auto        *row = new QHBoxLayout(box);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(8 * ssf);

  // label over field, all in one row
  auto addField = [this, box, row, ssf](QString const &title, double min, double max, int prec, float defaultValue, int width) {
    auto *column = new QVBoxLayout();
    column->setSpacing(2 * ssf);
    auto *label = new QLabel(title, box);
    label->setProperty("muted", true);
    auto *edit = new LineEditNum(min, max, prec, false, box);
    edit->setFont(font());
    edit->setFixedSize(static_cast<int>(width * ssf), static_cast<int>(32 * ssf));
    edit->setValue(defaultValue);
    column->addWidget(label);
    column->addWidget(edit);
    row->addLayout(column);
    return edit;
  };

  auto caption = [box](QString const &text) {
    auto *label = groupCaption(text, box);
    label->setAlignment(Qt::AlignBottom);
    return label;
  };
  auto *gainsBox = new QVBoxLayout();
  gainsBox->addWidget(caption(tr("PID coefficients")));
  auto *gainsRow = new QHBoxLayout();
  gainsRow->setSpacing(8 * ssf);
  gainsBox->addLayout(gainsRow);
  row->addLayout(gainsBox);
  auto gainField = [this, box, gainsRow, ssf](QString const &title, double min, double max, int prec, float value) {
    auto *column = new QVBoxLayout();
    column->setSpacing(2 * ssf);
    auto *label = new QLabel(title, box);
    label->setProperty("muted", true);
    auto *edit = new LineEditNum(min, max, prec, false, box);
    edit->setFont(font());
    edit->setFixedSize(static_cast<int>(80 * ssf), static_cast<int>(32 * ssf));
    edit->setValue(value);
    column->addWidget(label);
    column->addWidget(edit);
    gainsRow->addLayout(column);
    return edit;
  };
  m_kpEdit = gainField(tr("P"), ai::KP_MIN, ai::KP_MAX, 2, ai::DEFAULT_KP);
  m_kiEdit = gainField(tr("I"), ai::KI_MIN, ai::KI_MAX, 1, ai::DEFAULT_KI);
  m_kdEdit = gainField(tr("D"), ai::KD_MIN, ai::KD_MAX, 2, ai::DEFAULT_KD);
  row->addSpacing(10 * ssf);

  m_startTempEdit  = addField(tr("Start, °C"), -100, 150, 1, ai::DEFAULT_START_TEMP, 90);
  m_targetTempEdit = addField(tr("Target, °C"), -100, 150, 1, ai::DEFAULT_TARGET_TEMP, 90);
  m_durationEdit   = addField(tr("Duration, s"), 1, 100000, 0, ai::DEFAULT_DURATION_S, 90);

  // not shown: time step and the initial P / I / D terms keep their defaults
  m_dtEdit       = new LineEditNum(0.1, 60, 2, false, box);
  m_initialPEdit = new LineEditNum(INITIAL_TERM_MIN, INITIAL_TERM_MAX, 3, false, box);
  m_initialIEdit = new LineEditNum(INITIAL_TERM_MIN, INITIAL_TERM_MAX, 3, false, box);
  m_initialDEdit = new LineEditNum(INITIAL_TERM_MIN, INITIAL_TERM_MAX, 3, false, box);
  m_dtEdit->setValue(ai::DEFAULT_DT_S);
  for (LineEditNum *hidden : {m_dtEdit, m_initialPEdit, m_initialIEdit, m_initialDEdit}) {
    if (hidden != m_dtEdit) {
      hidden->setValue(0.0f);
    }
    hidden->setVisible(false);
  }

  auto *runBox = new QVBoxLayout();
  runBox->setSpacing(2 * ssf);
  m_statusLabel = new QLabel(box);
  m_statusLabel->setProperty("muted", true);
  m_statusLabel->setMaximumWidth(static_cast<int>(260 * ssf));
  runBox->addWidget(m_statusLabel);
  m_runBtn = new QPushButton(tr("Run simulation"), box);
  m_runBtn->setObjectName("primaryButton");
  m_runBtn->setFixedHeight(static_cast<int>(32 * ssf));
  m_runBtn->setMinimumWidth(static_cast<int>(140 * ssf));
  runBox->addWidget(m_runBtn);
  row->addSpacing(6 * ssf);
  row->addLayout(runBox);

  connect(m_runBtn, &QPushButton::clicked, this, &AiSimulatorWidget::runSimulation);

  return box;
}

QLabel *AiSimulatorWidget::addStatTile(QGridLayout *lay, int column, QString const &title) {
  auto *titleLabel = new QLabel(title, lay->parentWidget());
  titleLabel->setProperty("muted", true);

  auto *valueLabel = new QLabel(QStringLiteral("—"), lay->parentWidget());
  auto  valueFont  = valueLabel->font();
  valueFont.setBold(true);
  valueFont.setPointSizeF(valueFont.pointSizeF() * 1.5);
  valueLabel->setFont(valueFont);

  lay->addWidget(titleLabel, 0, column);
  lay->addWidget(valueLabel, 1, column);

  return valueLabel;
}

QWidget *AiSimulatorWidget::buildStatsCard() {
  auto *box  = new QWidget(this);
  auto *grid = new QGridLayout(box);
  grid->setContentsMargins(0, 0, 0, 0);
  grid->setHorizontalSpacing(18 * utils::ssf());
  grid->setVerticalSpacing(0);

  m_costTile      = addStatTile(grid, 0, tr("Cost"));
  m_tailMaeTile   = addStatTile(grid, 1, tr("Tail MAE, °C"));
  m_endTempTile   = addStatTile(grid, 2, tr("End, °C"));
  m_overshootTile = addStatTile(grid, 3, tr("Overshoot, °C"));
  m_settleTile    = addStatTile(grid, 4, tr("Settle, s"));
  for (int column{0}; column < 5; ++column) {
    grid->setColumnStretch(column, 1);
  }

  return box;
}

QWidget *AiSimulatorWidget::buildChartCard() {
  double const ssf{utils::ssf()};
  QFrame      *frame = makeCard(this, {});
  auto        *lay   = static_cast<QVBoxLayout *>(frame->layout());

  // header: title and the channel as a segmented switch
  auto *topLay = new QHBoxLayout();
  auto *title  = new QLabel(tr("Predicted response"), frame);
  title->setObjectName("cardTitle");
  topLay->addWidget(title);
  topLay->addStretch();
  auto *scenarioBtn = new QPushButton(tr("Scenario"), frame);
  scenarioBtn->setCheckable(true);
  scenarioBtn->setChecked(true);
  scenarioBtn->setFixedHeight(static_cast<int>(28 * ssf));
  scenarioBtn->setToolTip(tr("Show or hide the scenario and the results above the chart"));
  connect(scenarioBtn, &QPushButton::toggled, this, [this](bool shown) { m_scenarioRow->setVisible(shown); });
  topLay->addWidget(scenarioBtn);
  topLay->addSpacing(8 * ssf);

  // same channels as Deepvac Insight's simulator (the combo stays as the model of the switch)
  m_channelCombo = new QComboBox(frame);
  m_channelCombo->addItem(tr("Temperature"));
  m_channelCombo->addItem(tr("Error"));
  m_channelCombo->addItem(tr("Control (u)"));
  m_channelCombo->addItem(tr("Predicted ΔT"));
  m_channelCombo->setVisible(false);
  auto *group    = new QButtonGroup(frame);
  auto *segments = new QHBoxLayout();
  segments->setSpacing(0);
  for (int i{0}; i < m_channelCombo->count(); ++i) {
    auto *button = new QPushButton(m_channelCombo->itemText(i), frame);
    button->setCheckable(true);
    button->setProperty("segment", true);
    button->setProperty("segmentFirst", i == 0);
    button->setProperty("segmentLast", i == m_channelCombo->count() - 1);
    button->setFixedHeight(static_cast<int>(28 * ssf));
    button->setMinimumWidth(static_cast<int>(100 * ssf));
    group->addButton(button, i);
    segments->addWidget(button);
  }
  group->button(0)->setChecked(true);
  group->setExclusive(true);
  connect(group, &QButtonGroup::idClicked, m_channelCombo, &QComboBox::setCurrentIndex);
  topLay->addLayout(segments);
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
  m_statusLabel->setStyleSheet(isError ? QStringLiteral("color: %1;").arg(theme::colors().danger.name()) : QString());
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
  setStatusText(tr("Running…"), false);

  m_runTitle = tr("P %1 / I %2 / D %3").arg(params.kp).arg(params.ki).arg(params.kd);
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
