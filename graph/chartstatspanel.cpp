#include "chartstatspanel.h"

#include <QComboBox>
#include <QDateTime>
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <cmath>

#include "dataviewer.h"
#include "runanalysis.h"
#include "seriesstats.h"
#include "utils.h"

namespace deepvaccontrol {

namespace {
int px(double value) {
  return qMax(1, static_cast<int>(value * utils::ssf()));
}

// "−69.640 °C"; "—" when there is no value
QString valueText(double value, QString const &unit, int decimals = 3) {
  if (!std::isfinite(value)) {
    return QStringLiteral("—");
  }
  return QString::number(value, 'f', decimals).replace(QLatin1Char('-'), QChar(0x2212)) + (unit.isEmpty() ? QString{} : QStringLiteral(" ") + unit);
}

QString clockText(double seconds) {
  if (!std::isfinite(seconds)) {
    return QStringLiteral("—");
  }
  auto const total = static_cast<qint64>(std::llround(std::max(0.0, seconds)));
  return QStringLiteral("%1:%2:%3")
    .arg(total / 3600, 2, 10, QLatin1Char('0'))
    .arg((total / 60) % 60, 2, 10, QLatin1Char('0'))
    .arg(total % 60, 2, 10, QLatin1Char('0'));
}

// the unit of a channel label ("Temperature, °C" -> "°C")
QString unitOf(QString const &label) {
  qsizetype const comma = label.lastIndexOf(QLatin1Char(','));
  return (comma >= 0) ? label.mid(comma + 1).trimmed() : QString{};
}

QLabel *caption(QString const &text, QWidget *parent) {
  auto *label = new QLabel(text.toUpper(), parent);
  label->setObjectName("sectionCaption");
  return label;
}
}

ChartStatsPanel::ChartStatsPanel(QStringList const &names, QVariantList const &styles, QWidget *parent)
    : QWidget{parent}, m_names{names}, m_styles{styles}, m_timer{new QTimer(this)} {
  auto *lay = new QVBoxLayout(this);
  lay->setContentsMargins(0, 0, px(8), 0);
  lay->setSpacing(px(6));

  m_rangeLabel = new QLabel(this);
  m_rangeLabel->setProperty("muted", true);
  m_rangeLabel->setWordWrap(true);
  lay->addWidget(m_rangeLabel);
  lay->addSpacing(px(4));

  // one channel of the visible range, temperature unless chosen otherwise
  m_channel = new QComboBox(this);
  m_channel->setProperty("soft", true);
  m_channel->setMinimumHeight(px(30));
  for (int i{0}; i < names.size(); ++i) {
    QString const label = (i < styles.size()) ? styles[i].toMap().value("label").toString() : QString{};
    m_channel->addItem(label.isEmpty() ? names[i] : label, i);
  }
  m_channel->setCurrentIndex(std::max(0, static_cast<int>(names.indexOf(QStringLiteral("temp")))));
  lay->addWidget(m_channel);
  m_channelBox = makeBox({tr("Min"), tr("Max"), tr("Mean"), tr("Std"), tr("Slope"), tr("P95")}, m_channelValues);
  lay->addWidget(m_channelBox);
  lay->addSpacing(px(8));

  m_controlCaption = caption(tr("Control vs setpoint"), this);
  lay->addWidget(m_controlCaption);
  m_controlBox = makeBox({tr("MAE"), tr("RMSE"), tr("Settling time"), tr("Steady-state error"), tr("Tail MAE / std")}, m_controlValues);
  lay->addWidget(m_controlBox);

  m_note = new QLabel(this);
  m_note->setProperty("muted", true);
  m_note->setWordWrap(true);
  lay->addWidget(m_note);
  lay->addSpacing(px(8));

  auto *fullBtn = new QPushButton(tr("Full report: stages, anomalies, rules"), this);
  fullBtn->setMinimumHeight(px(32));
  fullBtn->setFocusPolicy(Qt::NoFocus);
  lay->addWidget(fullBtn);
  lay->addStretch();

  m_timer->setSingleShot(true);
  m_timer->setInterval(250); // while zooming and panning: once it stops
  connect(m_timer, &QTimer::timeout, this, &ChartStatsPanel::refresh);
  connect(m_channel, &QComboBox::currentIndexChanged, this, &ChartStatsPanel::refresh);
  connect(fullBtn, &QPushButton::clicked, this, &ChartStatsPanel::fullReportRequested);
}

QFrame *ChartStatsPanel::makeBox(QStringList const &names, QVector<QLabel *> &values) {
  auto *box  = new QFrame(this);
  box->setObjectName("statsBox");
  auto *grid = new QGridLayout(box);
  grid->setContentsMargins(px(12), px(4), px(12), px(4));
  grid->setHorizontalSpacing(px(12));
  grid->setVerticalSpacing(0);
  for (int i{0}; i < names.size(); ++i) {
    if (i > 0) {
      auto *line = new QFrame(box);
      line->setObjectName("cardDivider");
      line->setFixedHeight(1);
      grid->addWidget(line, 2 * i - 1, 0, 1, 2);
    }
    auto *name  = new QLabel(names[i], box);
    auto *value = new QLabel(QStringLiteral("—"), box);
    value->setObjectName("statsValue");
    value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    value->setTextInteractionFlags(Qt::TextSelectableByMouse);
    name->setMinimumHeight(px(30));
    grid->addWidget(name, 2 * i, 0);
    grid->addWidget(value, 2 * i, 1);
    values << value;
  }
  grid->setColumnStretch(1, 1);
  return box;
}

void ChartStatsPanel::setSource(DataViewer *viewer, bool comparison) {
  if (m_rangeConnection) {
    disconnect(m_rangeConnection);
  }
  if (m_dataConnection) {
    disconnect(m_dataConnection);
  }
  m_viewer     = viewer;
  m_comparison = comparison;
  if (viewer) {
    m_rangeConnection = connect(viewer->getPlot()->xAxis, qOverload<QCPRange const &>(&QCPAxis::rangeChanged), m_timer, qOverload<>(&QTimer::start));
    m_dataConnection  = connect(viewer, &DataViewer::dataUpdated, m_timer, qOverload<>(&QTimer::start));
  }
  refresh();
}

void ChartStatsPanel::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);
  refresh();
}

void ChartStatsPanel::refresh() {
  if (!isVisible()) {
    return; // computed when shown
  }
  auto const data = m_viewer ? m_viewer->getData() : nullptr;
  QVector<double> const times = data ? data->getTimes() : QVector<double>{};
  bool const            any{!times.isEmpty() && !m_comparison};
  m_channel->setVisible(any);
  m_channelBox->setVisible(any);
  m_controlCaption->setVisible(any);
  m_controlBox->setVisible(any);
  if (m_comparison) {
    m_rangeLabel->setText(tr("A comparison: its table, per experiment, is in the full report."));
    m_note->clear();
    return;
  }
  if (!any) {
    m_rangeLabel->setText(tr("No data shown."));
    m_note->clear();
    return;
  }
  QCPRange const view = m_viewer->getPlot()->xAxis->range();
  double const   from{std::max(view.lower, times.first())};
  double const   to{std::min(view.upper, times.last())};
  auto clock = [](double t) { return QDateTime::fromSecsSinceEpoch(static_cast<qint64>(t)).toString("HH:mm"); };
  m_rangeLabel->setText(tr("Visible range %1 – %2").arg(clock(from), clock(to)));

  // the channel chosen
  QVector<QVector<double>> const states = data->getStates();
  int const                      index{m_channel->currentData().toInt()};
  QString const                  unit = unitOf(m_channel->currentText());
  if ((index >= 0) && (index < states.size())) {
    core::SeriesStats const stats = core::computeStats(times, states[index], from, to);
    QStringList const       texts{valueText(stats.min, unit),  valueText(stats.max, unit), valueText(stats.mean, unit),
                            valueText(stats.std, unit),  valueText(stats.slopePerS * 60.0, unit.isEmpty() ? tr("/min") : unit + tr("/min")),
                            valueText(stats.p95, unit)};
    for (int i{0}; i < m_channelValues.size(); ++i) {
      m_channelValues[i]->setText(texts.value(i));
    }
  }

  // temperature against its setpoint in the visible range
  int const temp{static_cast<int>(m_names.indexOf(QStringLiteral("temp")))};
  int const reference{static_cast<int>(m_names.indexOf(QStringLiteral("temp_ref")))};
  int const interval{static_cast<int>(m_names.indexOf(QStringLiteral("interval")))};
  int const cycle{static_cast<int>(m_names.indexOf(QStringLiteral("cycle_cur")))};
  bool      control{(temp >= 0) && (reference >= 0) && (temp < states.size()) && (reference < states.size())};
  bool      tracking{true}; // the setpoint ramps in the range: MAE / RMSE mean something
  if (control) {
    QVector<double> slice;
    QVector<double> columns[4];
    int const       indexes[4]{temp, reference, interval, cycle};
    for (int i{0}; i < times.size(); ++i) {
      if ((times[i] < from) || (times[i] > to)) {
        continue;
      }
      slice.push_back(times[i] - from);
      for (int c{0}; c < 4; ++c) {
        bool const have{(indexes[c] >= 0) && (indexes[c] < states.size()) && (i < states[indexes[c]].size())};
        columns[c].push_back(have ? states[indexes[c]][i] : qQNaN());
      }
    }
    if (interval < 0) {
      columns[2].clear();
    }
    if (cycle < 0) {
      columns[3].clear();
    }
    core::RunAnalysis const analysis = core::analyseRun(slice, columns[0], columns[1], columns[2], columns[3]);
    control                          = analysis.overall.samples > 0;
    if (control) {
      core::ControlKpis const &k = analysis.overall;
      // without a ramp the temperature starts away from a step setpoint: MAE / RMSE measure that, not the control
      tracking                   = analysis.hasRamp();
      QStringList const        texts{tracking ? valueText(k.mae, QStringLiteral("°C")) : QStringLiteral("—"),
                              tracking ? valueText(k.rmse, QStringLiteral("°C")) : QStringLiteral("—"),
                              clockText(analysis.settlingTimeS), valueText(k.steadyStateError, QStringLiteral("°C")),
                              QStringLiteral("%1 / %2").arg(valueText(k.tailMae, QString{}), valueText(k.tailStd, QStringLiteral("°C")))};
      for (int i{0}; i < m_controlValues.size(); ++i) {
        m_controlValues[i]->setText(texts.value(i));
      }
    }
  }
  m_controlCaption->setVisible(control);
  m_controlBox->setVisible(control);
  m_note->setText(control ? QString{} : tr("No setpoint in the visible range, so no control figures."));
  m_note->setVisible(!m_note->text().isEmpty());
}

}
