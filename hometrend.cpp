#include "hometrend.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QTimer>
#include <QVBoxLayout>
#include <cmath>

#include "dataholder.h"
#include "datamanager.h"
#include "experimentrecorder.h"
#include "qcustomplot.h"
#include "resholder.h"
#include "statusmanager.h"
#include "systemvaluelist.h"
#include "theme.h"
#include "utils.h"

namespace deepvaccontrol {
namespace {
constexpr double IDLE_WINDOW_S{2.0 * 3600.0}; // shown with no job
constexpr int    REFRESH_MS{2000};
}

HomeTrend::HomeTrend(ResHolder *resHolder, QWidget *parent)
    : QFrame{parent}, m_resHolder{resHolder}, m_plot{new QCustomPlot(this)}, m_timer{new QTimer(this)} {
  setObjectName("homeCard");
  double const ssf{utils::ssf()};
  auto        *lay = new QVBoxLayout(this);
  lay->setContentsMargins(14 * ssf, 12 * ssf, 14 * ssf, 10 * ssf);
  lay->setSpacing(4 * ssf);
  m_title = new QLabel(this);
  m_title->setObjectName("cardTitle");
  lay->addWidget(m_title);
  lay->addWidget(m_plot, 1);

  QSharedPointer<QCPAxisTickerDateTime> ticker(new QCPAxisTickerDateTime);
  ticker->setDateTimeFormat("HH:mm");
  m_plot->xAxis->setTicker(ticker);
  m_plot->addGraph(); // setpoint
  m_plot->addGraph(); // temperature
  m_plot->legend->setVisible(true);
  m_plot->axisRect()->insetLayout()->setInsetAlignment(0, Qt::AlignTop | Qt::AlignLeft);
  m_plot->setMinimumHeight(static_cast<int>(160 * ssf));
  m_plot->setInteractions({}); // a glance, not a tool: the Chart page is for exploring
  m_plot->xAxis->setLabel(tr("Time"));
  m_plot->yAxis->setLabel(tr("Temperature, °C"));
  restyle();

  m_timer->setInterval(REFRESH_MS);
  connect(m_timer, &QTimer::timeout, this, &HomeTrend::refresh);
}

void HomeTrend::restyle() {
  theme::Colors const &c = theme::colors();
  m_plot->setBackground(c.panel);
  for (QCPAxis *axis : {m_plot->xAxis, m_plot->yAxis}) {
    axis->setBasePen(QPen(c.border));
    axis->setTickPen(QPen(c.border));
    axis->setSubTickPen(QPen(c.border));
    axis->setTickLabelColor(c.muted);
    axis->setLabelColor(c.muted);
    axis->grid()->setPen(QPen(c.border, 1, Qt::DotLine));
  }
  m_plot->graph(0)->setPen(QPen(c.muted, 1.6 * utils::ssf(), Qt::DashLine));
  m_plot->graph(0)->setName(tr("Setpoint"));
  m_plot->graph(1)->setPen(QPen(c.heat, 2.2 * utils::ssf()));
  m_plot->graph(1)->setName(tr("Temperature"));
  m_plot->legend->setBrush(c.panel);
  m_plot->legend->setBorderPen(QPen(c.border));
  m_plot->legend->setTextColor(c.text);
  m_plot->replot();
}

void HomeTrend::showEvent(QShowEvent *event) {
  QFrame::showEvent(event);
  refresh();
  m_timer->start();
}

void HomeTrend::hideEvent(QHideEvent *event) {
  QFrame::hideEvent(event);
  m_timer->stop();
}

void HomeTrend::refresh() {
  bool const   online{m_resHolder->status && (StatusManager::AppState::ONLINE == m_resHolder->status->getAppState())};
  double const jobStart{m_resHolder->recorder ? m_resHolder->recorder->runningJobStart() : qQNaN()};
  bool const   running{online && std::isfinite(jobStart)};
  double const now{QDateTime::currentMSecsSinceEpoch() / 1000.0};
  double const from{running ? jobStart : now - IDLE_WINDOW_S};
  m_title->setText(running ? tr("Current experiment") : tr("Last 2 hours"));

  QVector<double> times, temp, ref;
  auto const      data = m_resHolder->dataManager ? m_resHolder->dataManager->getIntervalToAdd() : nullptr;
  if (data) {
    QStringList const              names  = m_resHolder->states->getNames();
    qsizetype const                ti     = names.indexOf("temp");
    qsizetype const                ri     = names.indexOf("temp_ref");
    QVector<double> const          all    = data->getTimes();
    QVector<QVector<double>> const states = data->getStates();
    for (qsizetype i{0}; i < all.size(); ++i) {
      if (all[i] < from) {
        continue;
      }
      times << all[i];
      temp << ((ti >= 0) && (ti < states.size()) ? states[ti].value(i, qQNaN()) : qQNaN());
      // the setpoint only means something while a job runs
      ref << ((running && (ri >= 0) && (ri < states.size())) ? states[ri].value(i, qQNaN()) : qQNaN());
    }
  }
  m_plot->graph(0)->setData(times, ref, true);
  m_plot->graph(1)->setData(times, temp, true);
  m_plot->graph(0)->setVisible(running);
  m_plot->legend->setVisible(running);
  m_plot->xAxis->setRange(from, std::max(now, from + 60.0));
  // seconds on the axis while the span is short
  if (auto ticker = m_plot->xAxis->ticker().dynamicCast<QCPAxisTickerDateTime>()) {
    bool const seconds{m_plot->xAxis->range().size() < 600.0};
    ticker->setDateTimeFormat(seconds ? QStringLiteral("HH:mm:ss") : QStringLiteral("HH:mm"));
    m_plot->xAxis->setLabel(seconds ? tr("Time, hh:mm:ss") : tr("Time, hh:mm"));
  }
  bool found{false};
  QCPRange values = m_plot->graph(1)->getValueRange(found);
  if (running) {
    bool           foundRef{false};
    QCPRange const refRange = m_plot->graph(0)->getValueRange(foundRef);
    if (foundRef) {
      values = found ? QCPRange(std::min(values.lower, refRange.lower), std::max(values.upper, refRange.upper)) : refRange;
      found  = true;
    }
  }
  if (found) {
    double const padding{std::max(values.size() * 0.08, 1.0)};
    m_plot->yAxis->setRange(values.lower - padding, values.upper + padding);
  }
  m_plot->replot(QCustomPlot::rpQueuedReplot);
}

}
