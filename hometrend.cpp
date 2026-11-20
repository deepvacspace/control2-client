#include "hometrend.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

#include "dataholder.h"
#include "datamanager.h"
#include "experimentrecorder.h"
#include "pressureunits.h"
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
  auto        *lay = new QVBoxLayout(this);
  lay->setContentsMargins(theme::dp(28), theme::dp(24), theme::dp(28), theme::dp(20));
  lay->setSpacing(theme::dp(8));
  m_title = new QLabel(this);
  m_title->setObjectName("cardTitle");
  lay->addWidget(m_title);
  lay->addWidget(m_plot, 1);

  QSharedPointer<QCPAxisTickerDateTime> ticker(new QCPAxisTickerDateTime);
  ticker->setDateTimeFormat("HH:mm");
  m_plot->xAxis->setTicker(ticker);
  m_plot->legend->setVisible(true);
  m_plot->axisRect()->insetLayout()->setInsetAlignment(0, Qt::AlignTop | Qt::AlignLeft);
  m_plot->setMinimumHeight(theme::dp(300));
  m_plot->setInteractions({}); // a glance, not a tool: the Analysis page is for exploring
  m_plot->xAxis->setLabel(tr("Time"));
  m_plot->yAxis->setLabel(tr("Temperature, °C"));
  theme::setupPressureAxis(m_plot->yAxis2);
  m_plot->yAxis2->setVisible(false);
  m_otherAxis = m_plot->axisRect()->addAxis(QCPAxis::atRight); // outside the pressure's
  m_otherAxis->setVisible(false);
  restyle();

  m_timer->setInterval(REFRESH_MS);
  connect(m_timer, &QTimer::timeout, this, &HomeTrend::refresh);
}

void HomeTrend::restyle() {
  theme::Colors const &c = theme::colors();
  QFont titleFont = m_title->font();
  titleFont.setPointSizeF(font().pointSizeF() * 1.25);
  m_title->setFont(titleFont);
  m_plot->setBackground(c.panel);
  for (QCPAxis *axis : {m_plot->xAxis, m_plot->yAxis, m_plot->yAxis2, m_otherAxis}) {
    axis->setBasePen(QPen(c.border));
    axis->setTickPen(QPen(c.border));
    axis->setSubTickPen(QPen(c.border));
    axis->setTickLabelColor(c.muted);
    axis->setLabelColor(c.muted);
    axis->grid()->setPen(QPen(c.border, 1, Qt::DotLine));
  }
  m_plot->yAxis2->grid()->setVisible(false); // the temperature's grid only
  m_otherAxis->grid()->setVisible(false);
  m_plot->legend->setBrush(c.panel);
  m_plot->legend->setBorderPen(QPen(c.border));
  m_plot->legend->setTextColor(c.text);
  m_plot->replot();
}

void HomeTrend::setStyles(QVariantList const &styles) {
  m_styles = styles;
  m_plot->clearGraphs();
  m_graphs.clear();
  m_drawnCount = 0;
  for (int i{0}; i < m_styles.size(); ++i) {
    QVariantMap const style = m_styles[i].toMap();
    if (!style.contains(QStringLiteral("visible"))) {
      continue; // not a line
    }
    QString const name = style.value(QStringLiteral("name")).toString();
    QString const unit = pressure::unitOf(style.value(QStringLiteral("label")).toString());
    QCPAxis      *axis = pressure::isPressureChannel(name) ? m_plot->yAxis2 : (unit == QStringLiteral("°c")) ? m_plot->yAxis : m_otherAxis;
    m_graphs.insert(i, m_plot->addGraph(m_plot->xAxis, axis));
    applyStyle(i);
  }
  refresh();
}

void HomeTrend::setStyle(int index, QVariantMap const &style) {
  if ((index < 0) || (index >= m_styles.size())) {
    return;
  }
  m_styles[index] = style;
  applyStyle(index);
  m_drawnCount = 0; // its unit may have changed: all of it again
  refresh();
}

// as on the Analysis page: the channel's colour, line style and width
void HomeTrend::applyStyle(int index) {
  QCPGraph *graph = m_graphs.value(index);
  if (!graph) {
    return;
  }
  QVariantMap const style = m_styles.value(index).toMap();
  QPen              pen{QColor(style.value(QStringLiteral("color")).toString())};
  pen.setWidth(style.value(QStringLiteral("width")).toInt());
  pen.setStyle(static_cast<Qt::PenStyle>(style.value(QStringLiteral("pen")).toInt()));
  graph->setPen(pen);
  QString const label = style.value(QStringLiteral("label")).toString();
  graph->setName(pressure::isPressureChannel(style.value(QStringLiteral("name")).toString()) ? pressure::torrLabel(label) : label);
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

  auto const                     data   = m_resHolder->dataManager ? m_resHolder->dataManager->getIntervalToAdd() : nullptr;
  QVector<double> const          all    = data ? data->getTimes() : QVector<double>{};
  QVector<QVector<double>> const states = data ? data->getStates() : QVector<QVector<double>>{};
  qsizetype const                n{all.size()};
  qsizetype const                first{std::lower_bound(all.cbegin(), all.cend(), from) - all.cbegin()}; // the times are in order

  // the lines drawn: a setpoint only means something while a job runs
  QMap<int, bool> drawnLines;
  for (auto it = m_graphs.cbegin(); it != m_graphs.cend(); ++it) {
    QVariantMap const style = m_styles.value(it.key()).toMap();
    bool const        setpoint{style.value(QStringLiteral("name")).toString().endsWith(QStringLiteral("_ref"))};
    drawnLines.insert(it.key(), style.value(QStringLiteral("visible")).toBool() && (!setpoint || running) && (it.key() < states.size()));
  }
  // only the samples newer than the last drawing (and the last one drawn, replaced when its time came again), the
  // older ones out of the window dropped; all of it for another recording, job or choice of lines
  bool const continues{data && (data.get() == m_drawnHolder) && (running == m_drawnRunning) && (!running || (jobStart == m_drawnJobStart))
                       && (drawnLines == m_drawnLines) && (m_drawnCount > 1) && (m_drawnCount <= n) && (all[m_drawnCount - 1] == m_drawnLast)};
  qsizetype const       start{continues ? std::max(first, m_drawnCount - 1) : first};
  QVector<double> const times = all.mid(start);
  m_drawnHolder               = data.get();
  m_drawnCount                = n;
  m_drawnLast                 = (n > 0) ? all.last() : qQNaN();
  m_drawnRunning              = running;
  m_drawnJobStart             = jobStart;
  m_drawnLines                = drawnLines;

  int         shown{0};
  QStringList otherUnits;
  for (auto it = m_graphs.cbegin(); it != m_graphs.cend(); ++it) {
    QVariantMap const style = m_styles.value(it.key()).toMap();
    QString const     name  = style.value(QStringLiteral("name")).toString();
    QString const     label = style.value(QStringLiteral("label")).toString();
    bool const        drawn{drawnLines.value(it.key())};
    QCPGraph         *graph = it.value();
    if (!drawn) {
      graph->data()->clear();
    } else {
      // the pressure in Torr: its unit's factor once (pressure::toTorr reads the unit from the label)
      bool const             torr{pressure::isPressureChannel(name)};
      double const           factor{torr ? pressure::toTorr(1.0, label) : 1.0};
      QVector<double> const &channel = states[it.key()];
      QVector<double>        values;
      values.reserve(times.size());
      for (qsizetype i{start}; i < n; ++i) {
        double value{channel.value(i, qQNaN())};
        if (torr) {
          value = (std::isfinite(value) && (std::fabs(value) <= 1e9)) ? value * factor : qQNaN(); // as pressure::toTorr
          value = (value > 0.0) ? value : qQNaN(); // none at or below zero on the logarithmic axis
        }
        values << value;
      }
      if (continues) {
        graph->data()->removeBefore(from);
        graph->data()->removeAfter(all[start - 1]);
        graph->addData(times, values, true);
      } else {
        graph->setData(times, values, true);
      }
    }
    graph->setVisible(drawn);
    if (drawn) {
      graph->addToLegend();
      ++shown;
      if (graph->valueAxis() == m_otherAxis) {
        otherUnits << pressure::unitOf(label);
      }
    } else {
      it.value()->removeFromLegend();
    }
  }
  m_plot->legend->setVisible(shown > 1);
  m_plot->xAxis->setRange(from, std::max(now, from + 60.0));
  // seconds on the axis while the span is short
  if (auto ticker = m_plot->xAxis->ticker().dynamicCast<QCPAxisTickerDateTime>()) {
    bool const seconds{m_plot->xAxis->range().size() < 600.0};
    ticker->setDateTimeFormat(seconds ? QStringLiteral("HH:mm:ss") : QStringLiteral("HH:mm"));
    m_plot->xAxis->setLabel(seconds ? tr("Time, hh:mm:ss") : tr("Time, hh:mm"));
  }
  // each axis around the lines drawn on it; the ones at the right only while they have some
  auto valueRange = [this](QCPAxis *axis, QCP::SignDomain domain, bool &found) {
    QCPRange range;
    found = false;
    for (QCPGraph *graph : std::as_const(m_graphs)) {
      bool     any{false};
      QCPRange own;
      if (graph->visible() && (graph->valueAxis() == axis)) {
        own = graph->getValueRange(any, domain);
      }
      if (any) {
        range = found ? QCPRange(std::min(range.lower, own.lower), std::max(range.upper, own.upper)) : own;
        found = true;
      }
    }
    return range;
  };
  auto hasLines = [this](QCPAxis *axis) {
    return std::any_of(m_graphs.cbegin(), m_graphs.cend(), [axis](QCPGraph *graph) { return graph->visible() && (graph->valueAxis() == axis); });
  };
  bool found{false};
  for (QCPAxis *axis : {m_plot->yAxis, m_otherAxis}) {
    QCPRange const values = valueRange(axis, QCP::sdBoth, found);
    if (found) {
      double const padding{std::max(values.size() * 0.08, 1.0)};
      axis->setRange(values.lower - padding, values.upper + padding);
    }
  }
  m_otherAxis->setVisible(hasLines(m_otherAxis));
  otherUnits.removeDuplicates();
  m_otherAxis->setLabel((otherUnits.size() == 1) ? otherUnits.first() : QString{});
  m_plot->yAxis2->setVisible(hasLines(m_plot->yAxis2));
  QCPRange const presRange      = valueRange(m_plot->yAxis2, QCP::sdPositive, found);
  auto const     [lower, upper] = pressure::decadeRange(found ? presRange.lower : qQNaN(), found ? presRange.upper : qQNaN());
  if (std::isfinite(lower)) {
    m_plot->yAxis2->setRange(lower, upper);
  }
  m_plot->replot(QCustomPlot::rpQueuedReplot);
}

}
