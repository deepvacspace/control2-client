#include "analysischart.h"

#include <QDateTime>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

#include "crosshairsynchub.h"
#include "experimentlibrary.h"
#include "theme.h"
#include "utils.h"

namespace deepvaccontrol {

namespace {
QString const CROSSHAIR_LAYER{"crosshair"};
QString const BANDS_LAYER{"bands"};

QString formatValue(double value) {
  if (std::isnan(value)) {
    return QStringLiteral("—");
  }
  double const absValue{std::fabs(value)};
  if ((absValue != 0.0) && ((absValue < 1e-2) || (absValue >= 1e6))) {
    return QString::number(value, 'e', 2);
  }
  return QString::number(value, 'f', absValue >= 100 ? 1 : 3);
}

QColor withAlpha(QColor color, int alpha) {
  color.setAlpha(alpha);
  return color;
}

// index of the valid sample closest to key, or -1
int nearestIndex(QCPGraph const *graph, double key) {
  auto const data = graph->data();
  if (data->isEmpty()) {
    return -1;
  }
  auto const begin = data->constBegin();
  auto const end   = data->constEnd();
  auto const it    = data->findBegin(key, false); // first sample with key >= key

  int    best{-1};
  double bestDist{std::numeric_limits<double>::max()};
  auto   consider = [&](QCPGraphDataContainer::const_iterator probe) {
    double const dist{std::fabs(probe->key - key)};
    if (dist < bestDist) {
      bestDist = dist;
      best     = static_cast<int>(probe - begin);
    }
  };

  // closest valid sample on each side, skipping short NaN gaps
  auto probe = it;
  for (int n{0}; (n < 4) && (probe != begin); ++n) {
    --probe;
    if (!std::isnan(probe->value)) {
      consider(probe);
      break;
    }
  }
  probe = it;
  for (int n{0}; (n < 4) && (probe != end); ++n, ++probe) {
    if (!std::isnan(probe->value)) {
      consider(probe);
      break;
    }
  }
  return best;
}
}

AnalysisChart::AnalysisChart(QWidget *parent) : QWidget{parent}, m_plot{new QCustomPlot(this)} {
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(m_plot);
  setMinimumHeight(220 * utils::ssf());

  m_plot->setNoAntialiasingOnDrag(true);
  m_plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
  m_plot->axisRect()->setRangeDragAxes({m_plot->xAxis}, {m_plot->yAxis, m_plot->yAxis2});
  m_plot->axisRect()->setRangeZoomAxes({m_plot->xAxis}, {m_plot->yAxis, m_plot->yAxis2});

  QSharedPointer<QCPAxisTickerTime> timeTicker(new QCPAxisTickerTime);
  timeTicker->setTimeFormat("%h:%m:%s");
  m_plot->xAxis->setTicker(timeTicker);
  m_plot->xAxis->setLabel(tr("Time elapsed, hh:mm:ss"));

  QSharedPointer<QCPAxisTickerLog> logTicker(new QCPAxisTickerLog);
  logTicker->setSubTickCount(4);
  m_plot->yAxis2->setTicker(logTicker);
  m_plot->yAxis2->setScaleType(QCPAxis::stLogarithmic);
  m_plot->yAxis2->setNumberFormat("eb");
  m_plot->yAxis2->setNumberPrecision(0);
  m_plot->yAxis2->setRange(1e-7, 1e3);
  m_plot->yAxis2->setVisible(false);

  {
    QFont font = m_plot->legend->font();
    font.setPointSizeF(font.pointSizeF() * 0.9);
    m_plot->legend->setFont(font);
  }
  // legend: a box over the top left of the plot (the plot keeps the whole height); can be hidden
  m_plot->axisRect()->insetLayout()->setInsetAlignment(0, Qt::AlignLeft | Qt::AlignTop);
  m_plot->legend->setMargins(QMargins(6, 4, 8, 4));
  m_plot->legend->setWrap(1);
  m_plot->legend->setFillOrder(QCPLegend::foColumnsFirst);
  m_plot->legend->setVisible(true);
  theme::stylePlot(m_plot);
  styleLegendBox();

  m_plot->selectionRect()->setPen(QPen(QColor("#2563eb"), 1, Qt::DashLine));
  m_plot->selectionRect()->setBrush(QBrush(QColor(37, 99, 235, 40)));

  m_plot->addLayer(BANDS_LAYER, m_plot->layer("main"), QCustomPlot::limBelow);
  m_plot->addLayer(CROSSHAIR_LAYER, m_plot->layer("legend"), QCustomPlot::limAbove);
  m_plot->layer(CROSSHAIR_LAYER)->setMode(QCPLayer::lmBuffered);

  QPen const crossPen(QColor("#64748b"), 1, Qt::DashLine);
  m_crossV = new QCPItemStraightLine(m_plot);
  m_crossH = new QCPItemStraightLine(m_plot);
  for (auto *line : {m_crossV, m_crossH}) {
    line->setLayer(CROSSHAIR_LAYER);
    line->setPen(crossPen);
    line->setSelectable(false);
    line->setVisible(false);
  }

  m_hoverText = new QCPItemText(m_plot);
  m_hoverText->setLayer(CROSSHAIR_LAYER);
  m_hoverText->setSelectable(false);
  m_hoverText->setVisible(false);
  m_hoverText->setPadding(QMargins(6, 4, 6, 4));
  m_hoverText->setBrush(QBrush(QColor(255, 255, 255, 235)));
  m_hoverText->setTextAlignment(Qt::AlignLeft);
  m_hoverText->setClipToAxisRect(false);

  // circle on the sample the label refers to
  m_hoverDot = new QCPItemTracer(m_plot);
  m_hoverDot->setLayer(CROSSHAIR_LAYER);
  m_hoverDot->setSelectable(false);
  m_hoverDot->setVisible(false);
  m_hoverDot->setInterpolating(false);
  m_hoverDot->setStyle(QCPItemTracer::tsCircle);
  m_hoverDot->setSize(9 * utils::ssf());

  connect(m_plot->xAxis, qOverload<QCPRange const &>(&QCPAxis::rangeChanged), this, [this](QCPRange const &range) {
    emit viewRangeChanged(range.lower, range.upper);
  });
  connect(m_plot, &QCustomPlot::mousePress, this, &AnalysisChart::onMousePress);
  connect(m_plot, &QCustomPlot::mouseMove, this, &AnalysisChart::onMouseMove);
  connect(m_plot->selectionRect(), &QCPSelectionRect::accepted, this, &AnalysisChart::onSelectionAccepted);
  m_plot->installEventFilter(this);

  rebuild();
}

AnalysisChart::~AnalysisChart() {
  if (m_syncHub) {
    m_syncHub->unregisterChart(this);
  }
}

void AnalysisChart::setSeries(QVector<ChartSeries> const &series, double startEpoch) {
  bool const firstData{m_series.isEmpty()};
  m_series     = series;
  m_startEpoch = startEpoch;
  if (firstData) {
    m_autoRangePending = true;
  }
  rebuild();
}

void AnalysisChart::setMode(Mode mode) {
  m_mode = mode;
  rebuild();
}

void AnalysisChart::setSmoothingWindow(int window) {
  m_smoothingWindow = std::max(1, window);
  rebuild();
}

void AnalysisChart::setOverlays(bool min, bool max, bool avg) {
  m_overlayMin = min;
  m_overlayMax = max;
  m_overlayAvg = avg;
  rebuildDecorations();
}

void AnalysisChart::setRegions(QVector<ChartRegion> const &regions) {
  m_regions = regions;
  rebuildDecorations();
}

void AnalysisChart::setBands(QVector<ChartBand> const &bands) {
  m_bands = bands;
  rebuildDecorations();
}

void AnalysisChart::setEvents(QVector<ChartEvent> const &events) {
  m_events = events;
  rebuildDecorations();
}

void AnalysisChart::beginUpdate() {
  ++m_updateDepth;
}

void AnalysisChart::endUpdate() {
  if ((m_updateDepth > 0) && (0 == --m_updateDepth)) {
    if (m_graphsDirty) {
      rebuild();
    } else if (m_decorationsDirty) {
      rebuildDecorations();
    }
  }
}

void AnalysisChart::setStages(QVector<ChartRegion> const &stages) {
  m_stages = stages;
  rebuildDecorations();
}

void AnalysisChart::addStages() {
  for (ChartRegion const &stage : std::as_const(m_stages)) {
    auto *rect = new QCPItemRect(m_plot);
    for (auto *position : {rect->topLeft, rect->bottomRight}) {
      position->setTypeY(QCPItemPosition::ptAxisRectRatio);
    }
    rect->topLeft->setCoords(stage.x0, 0.0);
    rect->bottomRight->setCoords(stage.x1, 0.035);
    rect->setPen(QPen(Qt::white, 1));
    rect->setBrush(QBrush(withAlpha(stage.color, 150)));
    m_decorations.push_back(rect);

    auto *boundary = new QCPItemStraightLine(m_plot);
    boundary->point1->setCoords(stage.x0, 0);
    boundary->point2->setCoords(stage.x0, 1);
    boundary->setPen(QPen(withAlpha(stage.color, 90), 1, Qt::DotLine));
    m_decorations.push_back(boundary);

    if (!stage.label.isEmpty()) {
      auto *text = new QCPItemText(m_plot);
      text->position->setTypeY(QCPItemPosition::ptAxisRectRatio);
      text->position->setCoords((stage.x0 + stage.x1) / 2.0, 0.0175);
      text->setPositionAlignment(Qt::AlignCenter);
      text->setText(stage.label);
      text->setColor(Qt::white);
      text->setClipToAxisRect(true);
      m_decorations.push_back(text);
    }
  }
}

void AnalysisChart::setAnnotateMode(bool enabled) {
  m_annotateMode = enabled;
  m_plot->setCursor(enabled ? Qt::CrossCursor : Qt::ArrowCursor);
}

void AnalysisChart::setTimeRange(double start, double end) {
  if (end <= start) {
    return;
  }
  m_plot->xAxis->setRange(start, end);

  // fit values to the visible time range
  for (QCPAxis *axis : {m_plot->yAxis, m_plot->yAxis2}) {
    QCPRange range;
    bool     haveRange{false};
    for (int i{0}; i < m_plot->graphCount(); ++i) {
      QCPGraph *graph = m_plot->graph(i);
      if (graph->valueAxis() != axis) {
        continue;
      }
      bool           found{false};
      QCPRange const graphRange
        = graph->getValueRange(found, axis == m_plot->yAxis2 ? QCP::sdPositive : QCP::sdBoth, QCPRange(start, end));
      if (found) {
        range     = haveRange ? QCPRange(std::min(range.lower, graphRange.lower), std::max(range.upper, graphRange.upper))
                              : graphRange;
        haveRange = true;
      }
    }
    if (haveRange) {
      if (axis == m_plot->yAxis2) {
        axis->setRange(range.lower / 1.5, range.upper * 1.5);
      } else {
        double const pad{range.size() > 0 ? range.size() * 0.05 : 1.0};
        axis->setRange(range.lower - pad, range.upper + pad);
      }
    }
  }
  m_plot->replot();
}

void AnalysisChart::resetView() {
  autoRange();
  m_plot->replot();
}

QPair<double, double> AnalysisChart::dataXRange() const {
  double lower{std::numeric_limits<double>::max()};
  double upper{std::numeric_limits<double>::lowest()};
  for (auto const &series : m_series) {
    if (!series.x.isEmpty()) {
      lower = std::min(lower, series.x.first());
      upper = std::max(upper, series.x.last());
    }
  }
  if (lower > upper) {
    return {0.0, 0.0};
  }
  return {lower, upper};
}

QPair<double, double> AnalysisChart::viewXRange() const {
  return {m_plot->xAxis->range().lower, m_plot->xAxis->range().upper};
}

bool AnalysisChart::exportImage(QString const &path, char const *format) {
  hideHover();
  return m_plot->saveRastered(path, 1600, 900, 1.0, format, 95);
}

QImage AnalysisChart::renderImage(int width, int height) {
  hideHover();
  return m_plot->toPixmap(width, height, 1.0).toImage();
}

void AnalysisChart::setSyncHub(CrosshairSyncHub *hub) {
  if (m_syncHub) {
    m_syncHub->unregisterChart(this);
  }
  m_syncHub = hub;
  if (m_syncHub) {
    m_syncHub->registerChart(this);
  }
}

void AnalysisChart::rebuild() {
  if (m_updateDepth > 0) {
    m_graphsDirty = true;
    return;
  }
  m_graphsDirty = false;
  m_hoverDot->setGraph(nullptr); // its graph is deleted
  m_hoverDot->setVisible(false);
  m_plot->clearGraphs();

  bool        anyLog{false};
  QStringList primaryLabels;
  QStringList logLabels;
  for (ChartSeries const &series : std::as_const(m_series)) {
    QCPAxis  *valueAxis = valueAxisFor(series.logAxis);
    QCPGraph *graph     = m_plot->addGraph(m_plot->xAxis, valueAxis);
    graph->setName(series.label);
    graph->setAdaptiveSampling(true);

    QVector<double> values = smoothed(series.y);
    if (series.logAxis) {
      anyLog = true;
      logLabels.push_back(series.label);
      for (double &value : values) {
        if (value <= 0.0) {
          value = qQNaN();
        }
      }
    } else {
      primaryLabels.push_back(series.label);
    }
    graph->setData(series.x, values, true);

    QPen pen(series.color);
    pen.setWidthF(series.width * utils::ssf());
    pen.setStyle(series.penStyle);
    graph->setPen(pen);
    if (Mode::SCATTER == m_mode) {
      graph->setLineStyle(QCPGraph::lsNone);
      graph->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssDisc, series.color, series.color, 4 * utils::ssf()));
    } else {
      graph->setLineStyle(QCPGraph::lsLine);
      graph->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssNone));
    }
  }

  m_plot->yAxis2->setVisible(anyLog);
  m_plot->yAxis->setLabel(primaryLabels.size() == 1 ? primaryLabels.first() : (primaryLabels.isEmpty() ? QString{} : tr("Value")));
  m_plot->yAxis2->setLabel(logLabels.size() == 1 ? logLabels.first() : QString{});

  m_decorationsDirty = false;
  clearDecorations();
  if (m_series.isEmpty()) {
    auto *text = new QCPItemText(m_plot);
    text->position->setType(QCPItemPosition::ptAxisRectRatio);
    text->position->setCoords(0.5, 0.5);
    text->setText(tr("No channels selected"));
    text->setColor(QColor("#64748b"));
    m_decorations.push_back(text);
  }

  addBands();
  addRegions();
  addEvents();
  addStages();
  addSetpoint();
  addOverlays();

  if (m_autoRangePending && !m_series.isEmpty()) {
    autoRange();
    m_autoRangePending = false;
  }
  m_plot->replot();
}

void AnalysisChart::rebuildDecorations() {
  if (m_updateDepth > 0) {
    m_decorationsDirty = true;
    return;
  }
  m_decorationsDirty = false;
  clearDecorations();
  addBands();
  addRegions();
  addEvents();
  addStages();
  addSetpoint();
  addOverlays();
  m_plot->replot();
}

void AnalysisChart::clearDecorations() {
  for (QCPAbstractItem *item : std::as_const(m_decorations)) {
    m_plot->removeItem(item);
  }
  m_decorations.clear();
}

void AnalysisChart::addOverlays() {
  if (!(m_overlayMin || m_overlayMax || m_overlayAvg)) {
    return;
  }

  double minValue{std::numeric_limits<double>::max()};
  double maxValue{std::numeric_limits<double>::lowest()};
  double sum{0.0};
  qint64 count{0};
  for (int i{0}; i < m_plot->graphCount(); ++i) {
    QCPGraph *graph = m_plot->graph(i);
    if (graph->valueAxis() != m_plot->yAxis) {
      continue;
    }
    for (auto it = graph->data()->constBegin(); it != graph->data()->constEnd(); ++it) {
      if (!std::isnan(it->value)) {
        minValue = std::min(minValue, it->value);
        maxValue = std::max(maxValue, it->value);
        sum += it->value;
        ++count;
      }
    }
  }
  if (0 == count) {
    return;
  }

  auto addLine = [this](double value, QString const &label, QColor const &color) {
    auto *line = new QCPItemStraightLine(m_plot);
    line->point1->setCoords(0, value);
    line->point2->setCoords(1, value);
    line->setPen(QPen(color, 1.2, Qt::DashLine));
    m_decorations.push_back(line);

    auto *text = new QCPItemText(m_plot);
    text->position->setTypeX(QCPItemPosition::ptAxisRectRatio);
    text->position->setCoords(0.995, value);
    text->setPositionAlignment(Qt::AlignRight | Qt::AlignBottom);
    text->setText(QStringLiteral("%1 %2").arg(label, formatValue(value)));
    text->setColor(color);
    m_decorations.push_back(text);
  };

  if (m_overlayMin) {
    addLine(minValue, tr("Min"), QColor("#0d9488"));
  }
  if (m_overlayMax) {
    addLine(maxValue, tr("Max"), QColor("#dc2626"));
  }
  if (m_overlayAvg) {
    addLine(sum / static_cast<double>(count), tr("Avg"), QColor("#ca8a04"));
  }
}

void AnalysisChart::setSetpoint(double value, bool logAxis) {
  bool const same{(std::isnan(value) && std::isnan(m_setpoint)) || ((value == m_setpoint) && (logAxis == m_setpointLog))};
  if (same) {
    return;
  }
  m_setpoint    = value;
  m_setpointLog = logAxis;
  rebuildDecorations();
}

void AnalysisChart::addSetpoint() {
  if (std::isnan(m_setpoint) || (m_series.isEmpty())) {
    return;
  }
  QCPAxis *valueAxis = valueAxisFor(m_setpointLog);
  QColor const color("#475569");

  auto *line = new QCPItemStraightLine(m_plot);
  line->point1->setAxes(m_plot->xAxis, valueAxis);
  line->point2->setAxes(m_plot->xAxis, valueAxis);
  line->point1->setCoords(0, m_setpoint);
  line->point2->setCoords(1, m_setpoint);
  line->setPen(QPen(color, 1.5, Qt::DashDotLine));
  m_decorations.push_back(line);

  auto *text = new QCPItemText(m_plot);
  text->position->setAxes(m_plot->xAxis, valueAxis);
  text->position->setTypeX(QCPItemPosition::ptAxisRectRatio);
  text->position->setCoords(0.995, m_setpoint);
  text->setPositionAlignment(Qt::AlignRight | Qt::AlignTop);
  text->setText(tr("Setpoint %1").arg(formatValue(m_setpoint)));
  text->setColor(color);
  m_decorations.push_back(text);
}

void AnalysisChart::addBands() {
  for (ChartBand const &band : std::as_const(m_bands)) {
    QCPAxis   *valueAxis = valueAxisFor(band.logAxis);
    bool const haveLo{!std::isnan(band.lo)};
    bool const haveHi{!std::isnan(band.hi)};

    if (haveLo && haveHi) {
      auto *rect = new QCPItemRect(m_plot);
      rect->setLayer(BANDS_LAYER);
      for (auto *position : {rect->topLeft, rect->bottomRight}) {
        position->setAxes(m_plot->xAxis, valueAxis);
        position->setTypeX(QCPItemPosition::ptAxisRectRatio);
      }
      rect->topLeft->setCoords(0, std::max(band.lo, band.hi));
      rect->bottomRight->setCoords(1, std::min(band.lo, band.hi));
      rect->setPen(QPen(band.color, 1, Qt::DashLine));
      rect->setBrush(QBrush(withAlpha(band.color, 45)));
      m_decorations.push_back(rect);
    } else {
      for (double const value : {band.lo, band.hi}) {
        if (std::isnan(value)) {
          continue;
        }
        auto *line = new QCPItemStraightLine(m_plot);
        line->point1->setAxes(m_plot->xAxis, valueAxis);
        line->point2->setAxes(m_plot->xAxis, valueAxis);
        line->point1->setCoords(0, value);
        line->point2->setCoords(1, value);
        line->setPen(QPen(band.color, 1.2, Qt::DashLine));
        m_decorations.push_back(line);
      }
    }

    if (!band.label.isEmpty()) {
      auto *text = new QCPItemText(m_plot);
      text->position->setAxes(m_plot->xAxis, valueAxis);
      text->position->setTypeX(QCPItemPosition::ptAxisRectRatio);
      text->position->setCoords(0.005, haveHi ? band.hi : band.lo);
      text->setPositionAlignment(Qt::AlignLeft | Qt::AlignBottom);
      text->setText(band.label);
      text->setColor(band.color.darker(120));
      m_decorations.push_back(text);
    }
  }
}

void AnalysisChart::addRegions() {
  for (ChartRegion const &region : std::as_const(m_regions)) {
    auto *rect = new QCPItemRect(m_plot);
    rect->setLayer(BANDS_LAYER);
    for (auto *position : {rect->topLeft, rect->bottomRight}) {
      position->setTypeY(QCPItemPosition::ptAxisRectRatio);
    }
    rect->topLeft->setCoords(region.x0, 0);
    rect->bottomRight->setCoords(region.x1, 1);
    rect->setPen(QPen(region.color, 1, Qt::DashLine));
    rect->setBrush(QBrush(withAlpha(region.color, 40)));
    m_decorations.push_back(rect);

    if (!region.label.isEmpty()) {
      auto *text = new QCPItemText(m_plot);
      text->position->setTypeY(QCPItemPosition::ptAxisRectRatio);
      text->position->setCoords(region.x0, 0.01);
      text->setPositionAlignment(Qt::AlignLeft | Qt::AlignTop);
      text->setPadding(QMargins(4, 2, 4, 2));
      text->setText(region.label);
      text->setColor(region.color.darker(130));
      m_decorations.push_back(text);
    }
  }
}

void AnalysisChart::addEvents() {
  for (ChartEvent const &event : std::as_const(m_events)) {
    auto *line = new QCPItemStraightLine(m_plot);
    line->point1->setCoords(event.x, 0);
    line->point2->setCoords(event.x, 1);
    line->setPen(QPen(withAlpha(event.color, 170), 1, Qt::DotLine));
    m_decorations.push_back(line);
  }
}

void AnalysisChart::autoRange() {
  if (0 == m_plot->graphCount()) {
    return;
  }
  m_plot->xAxis->rescale(true);
  if (m_plot->xAxis->range().size() <= 0) {
    m_plot->xAxis->setRange(m_plot->xAxis->range().lower - 30, m_plot->xAxis->range().upper + 30);
  }
  auto const [start, end] = dataXRange();
  if (end > start) {
    setTimeRange(start, end);
  }
}

QVector<double> AnalysisChart::smoothed(QVector<double> const &y) const {
  int const window{(m_smoothingWindow % 2 == 0) ? m_smoothingWindow + 1 : m_smoothingWindow};
  if ((window <= 1) || (y.size() < window)) {
    return y;
  }

  // centered moving average ignoring NaN, NaN gaps are kept
  qsizetype const n{y.size()};
  QVector<double> sums(n + 1, 0.0);
  QVector<int>    counts(n + 1, 0);
  for (qsizetype i{0}; i < n; ++i) {
    bool const valid{!std::isnan(y[i])};
    sums[i + 1]   = sums[i] + (valid ? y[i] : 0.0);
    counts[i + 1] = counts[i] + (valid ? 1 : 0);
  }

  int const       half{window / 2};
  QVector<double> result(n);
  for (qsizetype i{0}; i < n; ++i) {
    if (std::isnan(y[i])) {
      result[i] = qQNaN();
      continue;
    }
    qsizetype const lo{std::max<qsizetype>(0, i - half)};
    qsizetype const hi{std::min<qsizetype>(n - 1, i + half)};
    result[i] = (sums[hi + 1] - sums[lo]) / (counts[hi + 1] - counts[lo]);
  }
  return result;
}

QString AnalysisChart::timeText(double x) const {
  QString text = QStringLiteral("t = %1").arg(ExperimentLibrary::formatDuration(x));
  if (m_startEpoch > 0) {
    text += QStringLiteral("  (%1)").arg(
      QDateTime::fromSecsSinceEpoch(static_cast<qint64>(std::llround(m_startEpoch + x))).toString("dd.MM HH:mm:ss"));
  }
  return text;
}

QCPAxis *AnalysisChart::valueAxisFor(bool logAxis) const {
  return logAxis ? m_plot->yAxis2 : m_plot->yAxis;
}

void AnalysisChart::onMousePress(QMouseEvent *event) {
  bool const left{event->button() == Qt::LeftButton};
  if (left && m_annotateMode) {
    m_plot->setSelectionRectMode(QCP::srmCustom);
  } else if ((event->button() == Qt::RightButton) || (left && event->modifiers().testFlag(Qt::ControlModifier))) {
    m_plot->setSelectionRectMode(QCP::srmZoom);
  } else {
    m_plot->setSelectionRectMode(QCP::srmNone);
  }
}

void AnalysisChart::onSelectionAccepted(QRect const &rect, QMouseEvent *event) {
  Q_UNUSED(event)
  if (!m_annotateMode || (m_plot->selectionRectMode() != QCP::srmCustom)) {
    return;
  }
  double const x0{m_plot->xAxis->pixelToCoord(rect.left())};
  double const x1{m_plot->xAxis->pixelToCoord(rect.right())};
  if (std::fabs(x1 - x0) > 0.0) {
    emit annotationDrawn(std::min(x0, x1), std::max(x0, x1));
  }
}

void AnalysisChart::onMouseMove(QMouseEvent *event) {
  QPointF const pos = event->position();
  if (!m_plot->axisRect()->rect().contains(pos.toPoint()) || m_series.isEmpty()) {
    hideHover();
    if (m_syncHub) {
      m_syncHub->broadcastHide(this);
    }
    return;
  }

  double const x{m_plot->xAxis->pixelToCoord(pos.x())};
  double const y{m_plot->yAxis->pixelToCoord(pos.y())};
  setCrosshair(x, y, true);

  // nearest sample in pixels
  double    bestDist{18.0 * utils::ssf()};
  QCPGraph *bestGraph{nullptr};
  int       bestIndex{-1};
  for (int i{0}; i < m_plot->graphCount(); ++i) {
    QCPGraph *graph = m_plot->graph(i);
    int const index = nearestIndex(graph, x);
    if (index < 0) {
      continue;
    }
    auto const    point = (graph->data()->constBegin() + index);
    QPointF const pixel = graph->coordsToPixels(point->key, point->value);
    double const  dist{std::hypot(pixel.x() - pos.x(), pixel.y() - pos.y())};
    if (dist < bestDist) {
      bestDist  = dist;
      bestGraph = graph;
      bestIndex = index;
    }
  }

  if (bestGraph) {
    showPointHover(bestGraph, bestIndex);
  } else {
    m_hoverDot->setVisible(false);
    // event marker close to the cursor
    ChartEvent const *nearEvent{nullptr};
    for (ChartEvent const &markerEvent : std::as_const(m_events)) {
      if (std::fabs(m_plot->xAxis->coordToPixel(markerEvent.x) - pos.x()) <= 6.0 * utils::ssf()) {
        nearEvent = &markerEvent;
        break;
      }
    }
    if (nearEvent) {
      m_hoverText->position->setType(QCPItemPosition::ptPlotCoords);
      m_hoverText->position->setAxes(m_plot->xAxis, m_plot->yAxis);
      m_hoverText->position->setCoords(nearEvent->x, y);
      m_hoverText->setText(QStringLiteral("%1\n%2").arg(nearEvent->text, timeText(nearEvent->x)));
      m_hoverText->setColor(nearEvent->color.darker(120));
      m_hoverText->setPen(QPen(nearEvent->color));
      placeHoverText();
    }
    m_hoverText->setVisible(nullptr != nearEvent);
  }

  replotCrosshair();

  if (m_syncHub) {
    m_syncHub->broadcast(this, x, y, bestGraph ? bestGraph->name() : QString{});
  }
}

void AnalysisChart::showPointHover(QCPGraph *graph, int index) {
  auto const   point = graph->data()->constBegin() + index;
  QColor const color = graph->pen().color();
  m_hoverText->position->setType(QCPItemPosition::ptPlotCoords);
  m_hoverText->position->setAxes(m_plot->xAxis, graph->valueAxis());
  m_hoverText->position->setCoords(point->key, point->value);
  m_hoverText->setText(QStringLiteral("%1\n%2\n%3").arg(graph->name(), timeText(point->key), formatValue(point->value)));
  m_hoverText->setColor(color.darker(120));
  m_hoverText->setPen(QPen(color));
  placeHoverText();
  m_hoverText->setVisible(true);

  m_hoverDot->setGraph(graph);
  m_hoverDot->setGraphKey(point->key);
  m_hoverDot->setPen(QPen(color, 2 * utils::ssf()));
  m_hoverDot->setBrush(QBrush(theme::colors().panel));
  m_hoverDot->setVisible(true);
}

void AnalysisChart::placeHoverText() {
  // next to its anchor, towards the middle of the plot
  QPointF const anchor = m_hoverText->position->pixelPosition();
  QRect const   area   = m_plot->axisRect()->rect();
  Qt::Alignment align  = (anchor.x() > area.center().x()) ? Qt::AlignRight : Qt::AlignLeft;
  align |= (anchor.y() < area.top() + area.height() * 0.3) ? Qt::AlignTop : Qt::AlignBottom;
  m_hoverText->setPositionAlignment(align);
}

void AnalysisChart::showSyncCrosshair(double x, double y, QString const &series) {
  if (m_series.isEmpty()) {
    return;
  }
  setCrosshair(x, y, true);

  // the same label as in the source chart: the hovered line's sample at the synced time
  QCPGraph *graph{nullptr};
  for (int i{0}; (i < m_plot->graphCount()) && !series.isEmpty(); ++i) {
    if (m_plot->graph(i)->name() == series) {
      graph = m_plot->graph(i);
      break;
    }
  }
  int const  index{graph ? nearestIndex(graph, x) : -1};
  bool const atPoint{(index >= 0)
                     && (std::fabs((graph->data()->constBegin() + index)->key - x) <= m_plot->xAxis->range().size() * 0.04)};
  if (atPoint) {
    showPointHover(graph, index);
  } else {
    m_hoverText->setVisible(false);
    m_hoverDot->setVisible(false);
  }
  replotCrosshair();
}

void AnalysisChart::hideSyncCrosshair() {
  hideHover();
}

void AnalysisChart::setCrosshair(double x, double y, bool visible) {
  m_crossV->point1->setCoords(x, 0);
  m_crossV->point2->setCoords(x, 1);
  m_crossH->point1->setCoords(0, y);
  m_crossH->point2->setCoords(1, y);
  m_crossV->setVisible(visible);
  m_crossH->setVisible(visible);
}

void AnalysisChart::hideHover() {
  if (!m_crossV->visible() && !m_hoverText->visible() && !m_hoverDot->visible()) {
    return;
  }
  m_crossV->setVisible(false);
  m_crossH->setVisible(false);
  m_hoverText->setVisible(false);
  m_hoverDot->setVisible(false);
  replotCrosshair();
}

void AnalysisChart::replotCrosshair() {
  m_plot->layer(CROSSHAIR_LAYER)->replot();
}

void AnalysisChart::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
}

void AnalysisChart::setLegendVisible(bool visible) {
  m_plot->legend->setVisible(visible);
  m_plot->replot(QCustomPlot::rpQueuedReplot);
}

void AnalysisChart::styleLegendBox() {
  theme::Colors const &c = theme::colors();
  m_plot->legend->setBrush(QColor(c.panel.red(), c.panel.green(), c.panel.blue(), 235));
  m_plot->legend->setBorderPen(QPen(c.border));
}

void AnalysisChart::changeEvent(QEvent *event) {
  QWidget::changeEvent(event);
  if (event->type() == QEvent::PaletteChange) {
    styleLegendBox(); // the box follows the theme
    m_plot->replot(QCustomPlot::rpQueuedReplot);
  }
}

bool AnalysisChart::eventFilter(QObject *watched, QEvent *event) {
  if ((watched == m_plot) && (event->type() == QEvent::Leave)) {
    hideHover();
    if (m_syncHub) {
      m_syncHub->broadcastHide(this);
    }
  }
  return QWidget::eventFilter(watched, event);
}

}
