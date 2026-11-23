#include "manualgraph.h"

#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

#include "pressureunits.h"
#include "qcustomplot.h"
#include "theme.h"
#include "utils.h"

namespace deepvaccontrol {
namespace {
// minutes to the setpoint at the line's rate; none: full speed, or not known
std::optional<double> rampMinutes(ManualGraph::Line const &line) {
  if (!line.on || !line.now || !line.rate || !(*line.rate > 0.0) || !std::isfinite(line.target)) {
    return std::nullopt;
  }
  return std::abs(line.target - *line.now) / *line.rate;
}

bool sameLine(ManualGraph::Line const &a, ManualGraph::Line const &b, double tolerance) {
  bool const sameNow{(a.now.has_value() == b.now.has_value()) && (!a.now || (std::abs(*a.now - *b.now) <= tolerance))};
  return (a.on == b.on) && sameNow && (a.target == b.target) && (a.rate == b.rate);
}

// minutes from the start: the stretch drawn before it (the chamber as it is now) has no times
class FromStartTicker : public QCPAxisTicker {
protected:
  QString getTickLabel(double tick, QLocale const &locale, QChar formatChar, int precision) override {
    return (tick < -1e-9) ? QString{} : QCPAxisTicker::getTickLabel(tick, locale, formatChar, precision);
  }
};
}

ManualGraph::ManualGraph(QWidget *parent) : QFrame{parent}, m_plot{new QCustomPlot(this)} {
  setFrameStyle(QFrame::Box | QFrame::Plain);
  setStyleSheet("QFrame {border: 1px solid #d9dee5;}");
  auto *lay = new QVBoxLayout(this);
  lay->setContentsMargins(0, 0, 0, 0);
  lay->addWidget(m_plot);
  QTimer::singleShot(0, m_plot, [plot = m_plot] { theme::stylePlot(plot); });
  theme::setupPressureAxis(m_plot->yAxis2);
  m_tempGraph = m_plot->addGraph(m_plot->xAxis, m_plot->yAxis);
  m_presGraph = m_plot->addGraph(m_plot->xAxis, m_plot->yAxis2);
  setColors(QColor(QStringLiteral("#d4122e")), QColor(QStringLiteral("#1f5fff")));
  m_plot->yAxis->setLabel(tr("Temperature, °C"));
  m_plot->xAxis->setSubTicks(false);
  m_plot->xAxis->setTicker(QSharedPointer<QCPAxisTicker>(new FromStartTicker));
}

void ManualGraph::setPressureUnit(QString const &unit) {
  m_plot->yAxis2->setLabel(tr("Pressure, %1").arg(unit));
}

void ManualGraph::setColors(QColor const &temperature, QColor const &pressure) {
  for (auto const &[graph, color] : {std::pair{m_tempGraph, temperature}, std::pair{m_presGraph, pressure}}) {
    graph->setPen(QPen(color, 3.0 * utils::ssf()));
  }
}

void ManualGraph::setLines(Line const &temperature, Line const &pressure) {
  // small moves of the chamber's values are not worth a redraw
  double const presTolerance{(pressure.now ? std::abs(*pressure.now) : 0.0) * 0.01};
  if (m_drawn && sameLine(temperature, m_temp, 0.1) && sameLine(pressure, m_pres, presTolerance)) {
    return;
  }
  m_temp  = temperature;
  m_pres  = pressure;
  m_drawn = true;
  // hidden (another page or tab): drawn when shown
  if (!isVisible()) {
    m_stale = true;
    return;
  }
  redraw();
}

void ManualGraph::showEvent(QShowEvent *event) {
  QFrame::showEvent(event);
  if (m_stale) {
    m_stale = false;
    redraw();
  }
}

void ManualGraph::redraw() {
  // the change in the middle of the plot (it is the only one): before it the chamber as it is now, after it the hold.
  // With a ramp at a rate the time is in minutes from the start (0: where the change begins) and the longest ramp
  // takes the middle half; at full speed there are no times (how long is not known): a short slope in the middle
  std::optional<double> const tempRamp = rampMinutes(m_temp);
  std::optional<double> const presRamp = rampMinutes(m_pres);
  double const                longest{std::max(tempRamp.value_or(0.0), presRamp.value_or(0.0))};
  bool const                  ramps{longest > 0.0};
  double const                lead{ramps ? longest * 0.5 : 5.0}; // shown before the start
  double const                end{ramps ? longest * 1.5 : 5.0};  // the longest ramp, then as long of the hold as before it
  double const                fullSpeed{(lead + end) * 0.04};    // how wide a setpoint reached at full speed is drawn
  double const                begin{ramps ? 0.0 : -fullSpeed / 2.0};

  auto points = [&](Line const &line, std::optional<double> ramp, bool logarithmic, QVector<double> &keys, QVector<double> &values) {
    keys.clear();
    values.clear();
    if (!line.on || !std::isfinite(line.target)) {
      return;
    }
    if (line.now && std::isfinite(*line.now)) {
      keys << -lead;
      values << *line.now;
      double const reached{(ramp && (*ramp > 0.0)) ? *ramp : fullSpeed};
      // linear in the value: on the logarithmic axis a pressure ramp bends, drawn with enough points to follow it
      int const steps{(logarithmic && ramp && (*ramp > 0.0)) ? 60 : 1};
      for (int i{0}; i <= steps; ++i) {
        double const f{static_cast<double>(i) / steps};
        keys << begin + reached * f;
        values << *line.now + (line.target - *line.now) * f;
      }
    } else {
      keys << -lead;
      values << line.target;
    }
    keys << end;
    values << line.target;
  };
  QVector<double> keys;
  QVector<double> values;
  points(m_temp, tempRamp, false, keys, values);
  m_tempGraph->setData(keys, values, true);
  if (!values.isEmpty()) {
    auto const [low, high] = std::minmax_element(values.cbegin(), values.cend());
    double const margin{std::max(2.0, (*high - *low) * 0.1)};
    m_plot->yAxis->setRange(*low - margin, *high + margin);
  }
  m_plot->yAxis->setVisible(m_temp.on);
  m_plot->yAxis->grid()->setVisible(m_temp.on);

  points(m_pres, presRamp, true, keys, values);
  m_presGraph->setData(keys, values, true);
  if (!values.isEmpty()) {
    auto const [low, high] = std::minmax_element(values.cbegin(), values.cend());
    auto const [lower, upper] = pressure::decadeRange(std::max(*low, 1e-12), std::max(*high, 1e-12));
    if (std::isfinite(lower)) {
      m_plot->yAxis2->setRange(lower, upper);
    }
  }
  m_plot->yAxis2->setVisible(m_pres.on);

  // minutes from the start when something ramps; otherwise no times (full speed: how long is not known)
  m_plot->xAxis->setRange(-lead, end);
  m_plot->xAxis->setTickLabels(ramps);
  m_plot->xAxis->setLabel(ramps ? tr("Time from the start, min") : tr("Time (not to scale)"));
  m_plot->replot();
}
}
