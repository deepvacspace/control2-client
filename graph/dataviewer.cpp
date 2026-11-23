#include "dataviewer.h"

#include <numeric>

#include "theme.h"

#include <QHBoxLayout>
#include <algorithm>
#include <cmath>

#include "pressureunits.h"
#include "utils.h"
namespace deepvaccontrol {

static QString const textLayerName{"text_layer"};

namespace {
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

// time from an origin (the first sample shown): ticks at round elapsed times
class ElapsedTicker : public QCPAxisTickerTime {
public:
  double origin{0.0};

  void generate(QCPRange const &range, QLocale const &locale, QChar formatChar, int precision, QVector<double> &ticks,
                QVector<double> *subTicks, QVector<QString> *tickLabels) override {
    QCPAxisTickerTime::generate(QCPRange(range.lower - origin, range.upper - origin), locale, formatChar, precision, ticks, subTicks,
                                tickLabels);
    for (double &tick : ticks) {
      tick += origin;
    }
    if (subTicks) {
      for (double &tick : *subTicks) {
        tick += origin;
      }
    }
  }
};

// "1:02:03" (h:mm:ss) of a duration in s
QString durationText(double seconds) {
  qint64 const total{static_cast<qint64>(std::llround(std::fabs(seconds)))};
  return QStringLiteral("%1%2:%3:%4")
    .arg(seconds < 0 ? QStringLiteral("−") : QString{})
    .arg(total / 3600)
    .arg((total / 60) % 60, 2, 10, QLatin1Char('0'))
    .arg(total % 60, 2, 10, QLatin1Char('0'));
}

// the samples a centred moving average spans (odd)
int averageWindow(int samples) {
  return (samples % 2 == 0) ? samples + 1 : samples;
}

// centred moving average ignoring NaN, NaN gaps are kept
QVector<double> smoothed(QVector<double> const &y, int samples) {
  int const window{averageWindow(samples)};
  if ((window <= 1) || (y.size() < window)) {
    return y;
  }
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

// a line's style as shown: the pressure's label in Torr, the unit it is drawn in
QVariantMap shownStyle(QVariantMap style) {
  if (pressure::isPressureChannel(style.value(QStringLiteral("name")).toString())) {
    style[QStringLiteral("label")] = pressure::torrLabel(style.value(QStringLiteral("label")).toString());
    if (style.contains(QStringLiteral("axis_label"))) {
      style[QStringLiteral("axis_label")] = pressure::torrLabel(style.value(QStringLiteral("axis_label")).toString());
    }
  }
  return style;
}

QString formatValue(double value) {
  double const absValue{std::fabs(value)};
  if ((absValue != 0.0) && ((absValue < 1e-2) || (absValue >= 1e6))) {
    return utils::formatScientific(value, 2, false);
  }
  return QString::number(value, 'f', absValue >= 100 ? 1 : 3);
}
}

DataViewer::DataViewer(Logger *logger, bool addSecondLogAxis, QWidget *parent)
    : QWidget(parent)
    , m_logger{logger}
    , m_plot(new QCustomPlot())
    , m_legendController(m_plot)
    , m_useLogAxis{addSecondLogAxis} {
  m_plot->setAutoAddPlottableToLegend(false);
  m_plot->setNoAntialiasingOnDrag(true); // dragged: drawn plainer, so it follows the finger
  auto mainLay = new QHBoxLayout(this);
  mainLay->setContentsMargins(0, 0, 0, 0);
  mainLay->addWidget(m_plot);

  QSharedPointer<QCPAxisTickerDateTime> timeTicker(new QCPAxisTickerDateTime);
  m_plot->xAxis->setTicker(timeTicker);
  timeTicker->setDateTimeFormat("hh:mm:ss");
  m_clockTicker = timeTicker;
  QSharedPointer<ElapsedTicker> elapsedTicker(new ElapsedTicker);
  elapsedTicker->setTimeFormat("%h:%m:%s");
  m_elapsedTicker = elapsedTicker;

  if (m_useLogAxis) { // the pressure's (titled by the lines on it, see updateAxisTitles)
    theme::setupPressureAxis(m_plot->yAxis2);
    m_plot->yAxis2->setRange(1e-7, 1e3);
    // dragging and box zoom move it with the values (QCustomPlot keeps it logarithmic)
    m_plot->axisRect()->setRangeDragAxes({m_plot->xAxis}, {m_plot->yAxis, m_plot->yAxis2});
    m_plot->axisRect()->setRangeZoomAxes({m_plot->xAxis}, {m_plot->yAxis, m_plot->yAxis2});
  }

  connect(m_plot, &QCustomPlot::mousePress, this, &DataViewer::mouseEventHeadler);
  connect(m_plot, &QCustomPlot::mouseRelease, this, &DataViewer::mouseEventHeadler);
  connect(m_plot, &QCustomPlot::mouseMove, this, &DataViewer::mouseEventHeadler);
  connect(m_plot, &QCustomPlot::mouseWheel, this, &DataViewer::wheelEventHandler);
  connect(m_plot->selectionRect(), &QCPSelectionRect::accepted, this, [this](QRect const &rect, QMouseEvent *event) {
    (void)event;
    if (m_plot->selectionRectMode() == QCP::srmCustom) { // annotate: a time range
      double const x0{m_plot->xAxis->pixelToCoord(rect.left())};
      double const x1{m_plot->xAxis->pixelToCoord(rect.right())};
      if (std::fabs(x1 - x0) > 0.0) {
        emit annotationDrawn(std::min(x0, x1), std::max(x0, x1));
      }
      return;
    }
    // rectangle zoom: update internal variable
    m_currentView = mouseToCoord(rect);
  });
  m_plot->addLayer("regions", m_plot->layer("main"), QCustomPlot::limBelow);
  m_setpointCurve = new QCPCurve(m_plot->xAxis, m_plot->yAxis);
  m_setpointCurve->setPen(QPen(theme::colors().muted, 1.6, Qt::DashLine));
  m_setpointCurve->setSelectable(QCP::stNone);
  m_setpointCurve->setVisible(false);

  // legend position: a box over the top left of the chart, small labels
  m_plot->axisRect()->insetLayout()->setInsetAlignment(0, Qt::AlignLeft | Qt::AlignTop);
  m_plot->legend->setBorderPen(QPen(theme::colors().border));
  {
    QFont font = m_plot->legend->font();
    font.setPointSizeF(font.pointSizeF() * 0.8);
    m_plot->legend->setFont(font);
  }
  m_plot->setMouseTracking(true); // the value label follows the mouse without a button pressed
  m_plot->setAttribute(Qt::WA_AcceptTouchEvents); // fingers: see handleTouch()
  m_plot->installEventFilter(this);

  // value label: over the legend box
  m_plot->addLayer("cursor", m_plot->layer("legend"), QCustomPlot::limAbove);
  m_plot->layer("cursor")->setMode(QCPLayer::lmBuffered);
  m_plot->layer("legend")->setMode(QCPLayer::lmBuffered); // its values follow the cursor without redrawing the lines

  m_cursor = new QCPItemStraightLine(m_plot);
  m_cursor->setLayer("cursor");
  m_cursor->setAntialiased(false);
  m_cursor->setSelectable(false);
  m_cursor->point1->setTypeY(QCPItemPosition::ptAxisRectRatio);
  m_cursor->point2->setTypeY(QCPItemPosition::ptAxisRectRatio);
  m_cursor->setPen(QPen(theme::colors().muted, 1, Qt::DashLine));
  m_cursor->setVisible(false);

  m_cursorH = new QCPItemStraightLine(m_plot);
  m_cursorH->setLayer("cursor");
  m_cursorH->setAntialiased(false);
  m_cursorH->setSelectable(false);
  m_cursorH->point1->setTypeX(QCPItemPosition::ptAxisRectRatio);
  m_cursorH->point2->setTypeX(QCPItemPosition::ptAxisRectRatio);
  m_cursorH->setPen(QPen(theme::colors().muted, 1, Qt::DashLine));
  m_cursorH->setVisible(false);

  m_tracer = new QCPItemTracer(m_plot);
  m_tracer->setLayer("cursor");
  m_tracer->setSelectable(false);
  m_tracer->setInterpolating(false);
  m_tracer->setStyle(QCPItemTracer::tsCircle);
  m_tracer->setSize(5 * utils::ssf());
  m_tracer->setVisible(false);

  m_valueText = new QCPItemText(m_plot);
  m_valueText->setLayer("cursor");
  m_valueText->setSelectable(false);
  m_valueText->setPadding(QMargins(6, 4, 6, 4));
  m_valueText->setTextAlignment(Qt::AlignLeft);
  m_valueText->setClipToAxisRect(false);
  m_valueText->setVisible(false);

  m_plot->addLayer(textLayerName, m_plot->layer("legend"), QCustomPlot::limBelow);
  m_plot->layer(textLayerName)->setMode(QCPLayer::lmBuffered);

  m_textItem = new QCPItemText(m_plot);
  m_textItem->setLayer(m_plot->layer(textLayerName));
  m_textItem->setPositionAlignment(Qt::AlignRight | Qt::AlignTop);
  m_textItem->position->setType(QCPItemPosition::ptAxisRectRatio);
  m_textItem->position->setCoords(0.98, 0.0);
  m_textItem->setProperty("themedLabel", true); // colours from theme::stylePlot(), again when the theme changes
  theme::stylePlot(m_plot);
  {
    QFont font;
    font.setBold(true);
    font.setPointSize(10 * utils::ssf());
    m_textItem->setFont(font);
  }
  updateTextItem(QString{});
  m_legendController.setValueVisible(false); // values are shown in the label at the mouse
  setLabelsVisible(false);

  m_renderTimer.setTimerType(Qt::CoarseTimer);
  connect(&m_renderTimer, &QTimer::timeout, this, &DataViewer::onRenderTick);
  m_renderTimer.start(5000);
}

void DataViewer::setData(std::shared_ptr<DataHolder const> data) {
  if (data == m_data) {
    return;
  }

  if (nullptr == data) {
    return;
  }

  if (true == static_cast<bool>(m_updateDataConnection)) {
    if (disconnect(m_updateDataConnection)) {
      m_logger->stream(Logger::Level::DEB)
        << "dataChanged signal has been successfully disconeted from prev DataHolder";
    } else {
      m_logger->stream(Logger::Level::DEB)
        << "dataChanged signal has been UNSUCCESSFULLY disconeted from prev DataHolder";
    }
  }

  if (m_graphsStale || (data->getNumberOfStates() != m_plot->graphCount())) {
    m_graphsStale = false;
    // [re]init graphs
    initGraphs(data->getNumberOfStates());

    if (m_data) { // previous dataInterval not empty
      m_logger->stream(Logger::Level::WAR) << "New DataHolder has another number of states";
    }
  }

  m_data     = data;
  m_fitStart = qQNaN();
  m_updateDataConnection
    = connect(m_data.get(), &DataHolder::dataChanged, this, &DataViewer::onDataChanged, Qt::QueuedConnection);
  if (true == static_cast<bool>(m_updateDataConnection)) {
    m_logger->stream(Logger::Level::DEB) << "dataChanged signal has been successfully connected for new DataHolder";
  } else {
    m_logger->stream(Logger::Level::DEB) << "dataChanged signal has been UNSUCCESSFULLY connected for new DataHolder";
  }
  setControlMode(ControlMode::LEAD_FULL);

  // Force immediate redraw on switch:
  m_dirtyForUi.store(false, std::memory_order_relaxed);
  updateData();

  // if (static_cast<qint64>(data->getIntervalKey()) != QDateTime::currentSecsSinceEpoch()) {
  updateTextItem(QDateTime::fromSecsSinceEpoch(data->getIntervalKey()).toString("yyyy-MM-dd"));
  // }
}

std::shared_ptr<DataHolder const> DataViewer::getData() const {
  return m_data;
}

void DataViewer::setRefreshPeriodMs(size_t ms) {
  m_renderTimer.setInterval(qMax(static_cast<size_t>(1000), ms)); // protect from too small
}

void DataViewer::onDataChanged() {
  // Keep it cheap: just mark dirty.
  m_dirtyForUi.store(true, std::memory_order_relaxed);
}

void DataViewer::onRenderTick() {
  // hidden (another page or tab): kept for when it shows again
  if (!m_data || !isVisible()) {
    return;
  }

  if (m_dirtyForUi.exchange(false, std::memory_order_relaxed)) {
    updateData();
  }
}

void DataViewer::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);
  if (m_data && m_dirtyForUi.exchange(false, std::memory_order_relaxed)) {
    updateData();
  }
}

void DataViewer::setStyles(QVariantList const &stateListVariant) {
  // the pressure drawn in Torr: its factor from the unit of its label (the styles keep the label, e.g. for exports)
  QVariantList    shown;
  QVector<double> scales;
  for (QVariant const &style : stateListVariant) {
    QVariantMap const map = style.toMap();
    shown << shownStyle(map);
    scales << (pressure::isPressureChannel(map.value(QStringLiteral("name")).toString())
                 ? pressure::toTorr(1.0, map.value(QStringLiteral("label")).toString())
                 : 1.0);
  }
  if (scales != m_torrScale) {
    m_torrScale  = scales;
    m_drawnCount = 0; // the lines in other units: all of them again with the next data
  }
  m_legendController.setStyles(shown);
  if (m_compactLegend) {
    m_legendController.compactLayout(3);
  }
  m_stateListVariant = stateListVariant;
  applyStyles();
  updateAxisTitles(); // also before there are lines (applyStyles needs them)
  rebuildDecorations(); // bands and overlays follow the visible channels
}

void DataViewer::setStyle(int index, QVariantMap stateMapVariant) {
  m_legendController.setStyle(index, shownStyle(stateMapVariant));
  if (m_compactLegend) {
    m_legendController.compactLayout(3);
  }
  applyStyle(index, stateMapVariant, false); // this replot graphs and legend
  rebuildDecorations();
}

void DataViewer::updateTextItem(QString const &text) {
  m_textItem->setText(text);
  m_plot->layer(textLayerName)->setVisible(!text.isEmpty());
  m_plot->layer(textLayerName)->replot();
}

void DataViewer::setView(QRectF const &view) {
  setPlotRange(view);
  m_plot->replot();
}

void DataViewer::switchControlMode() {
  switch (m_controlMode) {
    case DataViewer::ControlMode::ZOOM:
      setControlMode(DataViewer::ControlMode::LEAD_X);
      break;
    case DataViewer::ControlMode::LEAD_X:
      setControlMode(DataViewer::ControlMode::LEAD_FULL);
      break;
    case DataViewer::ControlMode::LEAD_FULL:
      setControlMode(DataViewer::ControlMode::LEAD_X);
      break;
    default:
      break;
  }
  updateRange();
  m_plot->replot();
}

// The time shown stays on the data: at most a few times its duration, never beside it.
void DataViewer::limitView(QRectF &view) const {
  QVector<double> const times = m_data ? m_data->getTimes() : QVector<double>{};
  if (times.isEmpty()) {
    return;
  }
  double const first{times.first()};
  double const last{times.last()};
  double const maxWidth{std::max(4.0 * (last - first), 600.0)};
  if (view.width() > maxWidth) {
    double const center{std::clamp(view.center().x(), first, last)};
    view.setLeft(center - maxWidth / 2.0);
    view.setRight(center + maxWidth / 2.0);
  }
  if (view.right() < first) {
    view.moveLeft(view.left() + (first - view.right()));
  } else if (view.left() > last) {
    view.moveLeft(last);
  }

  // the values likewise: at most ten times the range of the lines shown
  QCPRange values;
  bool     haveValues{false};
  for (int i{0}; i < m_plot->graphCount(); ++i) {
    QCPGraph *graph = m_plot->graph(i);
    bool      found{false};
    QCPRange const range = graph->visible() && (graph->valueAxis() == m_plot->yAxis) ? graph->getValueRange(found) : QCPRange{};
    if (found) {
      values     = haveValues ? QCPRange(std::min(values.lower, range.lower), std::max(values.upper, range.upper)) : range;
      haveValues = true;
    }
  }
  if (!haveValues) {
    return;
  }
  double const lower{std::min(view.top(), view.bottom())};
  double const upper{std::max(view.top(), view.bottom())};
  double const maxHeight{std::max(10.0 * values.size(), 100.0)};
  if (upper - lower > maxHeight) {
    double const center{std::clamp((lower + upper) / 2.0, values.lower, values.upper)};
    bool const   upward{view.top() > view.bottom()}; // getRange(): top is the upper value
    view.setTop(center + (upward ? 0.5 : -0.5) * maxHeight);
    view.setBottom(center - (upward ? 0.5 : -0.5) * maxHeight);
  }
}

bool DataViewer::viewFitsData(QJsonObject const &state) const {
  QVector<double> const times = m_data ? m_data->getTimes() : QVector<double>{};
  if (times.isEmpty()) {
    return true; // nothing to judge it by yet
  }
  QRectF view(QPointF(state.value("x0").toDouble(), 0.0), QPointF(state.value("x1").toDouble(), 1.0));
  QRectF limited = view;
  limitView(limited);
  return limited == view;
}

void DataViewer::moveView(double addX, double addY) {
  m_currentView = getRange(); // the axes may have been dragged since
  QPointF const addition(fabs(m_currentView.width()) * addX, fabs(m_currentView.height()) * addY);
  m_currentView.moveCenter(m_currentView.center() + addition);
  limitView(m_currentView);
  moveLogAxis(addY);

  if ((DataViewer::ControlMode::LEAD_FULL == m_controlMode) || (addX != 0.0)) {
    setControlMode(DataViewer::ControlMode::ZOOM);
  }

  setPlotRange(m_currentView);
  updateRange();
  m_plot->replot();
}

void DataViewer::zoomView(double mulX, double mulY) {
  m_currentView = getRange(); // the axes may have been dragged since
  QPointF const center{m_currentView.center()};
  double const  halfWidth{qAbs(m_currentView.width()) * 0.5};
  double const  halfHeight{qAbs(m_currentView.height()) * 0.5};

  m_currentView.setLeft(center.x() - halfWidth * mulX);
  m_currentView.setRight(center.x() + halfWidth * mulX);

  m_currentView.setTop(center.y() + halfHeight * mulY);
  m_currentView.setBottom(center.y() - halfHeight * mulY);
  limitView(m_currentView);
  zoomLogAxis(qQNaN(), mulY);

  if (DataViewer::ControlMode::LEAD_FULL == m_controlMode) {
    setControlMode(DataViewer::ControlMode::ZOOM);
  }

  setPlotRange(m_currentView);
  updateRange();
  m_plot->replot();
}

void DataViewer::setControlMode(ControlMode const mode) {
  m_controlMode = mode;
  if (ControlMode::LEAD_FULL != mode) {
    m_fitStart = qQNaN();
  }
  emit controlModeChanged(m_controlMode);
  // fit / follow right away (not only when new data arrives: a saved experiment never changes)
  if ((ControlMode::ZOOM != mode) && m_data && (m_plot->graphCount() > 0)) {
    updateRange();
    m_plot->replot();
  }
}

void DataViewer::setCompactLegend(bool compact) {
  m_compactLegend = compact;
  QCPLegend *legend = m_plot->legend;
  if (compact && (legend->layout() == m_plot->axisRect()->insetLayout())) {
    m_plot->axisRect()->insetLayout()->take(legend);
    m_plot->axisRect()->insetLayout()->simplify();
    m_plot->plotLayout()->addElement(1, 0, legend);
    m_plot->plotLayout()->setRowStretchFactor(1, 0.001);
    legend->setBrush(Qt::NoBrush);
    legend->setBorderPen(Qt::NoPen);
    legend->setLayer("legend");
  }
  if (compact) {
    m_legendController.compactLayout(3);
  }
  setLabelsVisible(m_labelsVisible);
  m_plot->replot();
}

void DataViewer::setRegions(QVector<Region> const &regions) {
  for (QCPAbstractItem *item : std::as_const(m_regionItems)) {
    m_plot->removeItem(item);
  }
  m_regionItems.clear();
  for (Region const &region : regions) {
    auto *rect = new QCPItemRect(m_plot);
    rect->setLayer("regions");
    rect->setSelectable(false);
    for (auto *position : {rect->topLeft, rect->bottomRight}) {
      position->setTypeY(QCPItemPosition::ptAxisRectRatio);
    }
    rect->topLeft->setCoords(region.x0, 0);
    rect->bottomRight->setCoords(region.x1, 1);
    QColor fill = region.color;
    fill.setAlpha(40);
    rect->setPen(QPen(region.color, 1, Qt::DashLine));
    rect->setBrush(fill);
    m_regionItems.push_back(rect);

    if (!region.label.isEmpty()) {
      auto *text = new QCPItemText(m_plot);
      text->setLayer("regions");
      text->setSelectable(false);
      text->position->setTypeY(QCPItemPosition::ptAxisRectRatio);
      text->position->setCoords(region.x0, 0.01);
      text->setPositionAlignment(Qt::AlignLeft | Qt::AlignTop);
      text->setPadding(QMargins(4, 2, 4, 2));
      text->setText(region.label);
      text->setColor(region.color.darker(130));
      m_regionItems.push_back(text);
    }
  }
  m_plot->replot(QCustomPlot::rpQueuedReplot);
}

void DataViewer::setAnnotateMode(bool enabled) {
  m_annotateMode = enabled;
  m_plot->setCursor((m_annotateMode || m_boxZoomMode) ? Qt::CrossCursor : Qt::ArrowCursor);
}

void DataViewer::setBoxZoomMode(bool enabled) {
  m_boxZoomMode = enabled;
  m_plot->setCursor((m_annotateMode || m_boxZoomMode) ? Qt::CrossCursor : Qt::ArrowCursor);
}

void DataViewer::setLabelsVisible(bool visible) {
  m_labelsVisible = visible;
  m_legendController.setVisible(visible || m_compactLegend);
  updateLegendValueMode();
}

void DataViewer::setValuesVisible(bool visible) {
  m_valuesVisible = visible;
  if (!visible) {
    hideValueCursor();
  }
  updateLegendValueMode();
}

void DataViewer::updateLegendValueMode() {
  // Labels and Tooltip both on: each label also tells its line's value at the cursor
  bool const on{m_labelsVisible && m_valuesVisible};
  if (on == m_legendValues) {
    return;
  }
  m_legendValues = on;
  m_legendController.setValueTexts({});
  m_legendController.setItemValuesVisible(on); // room for the values: the labels laid out again
  m_plot->replot(QCustomPlot::rpQueuedReplot);
}

void DataViewer::showLegendValues(double x) {
  if (!m_legendValues || !m_data) {
    return;
  }
  // each visible line's sample closest to that time, if it has one close by
  double const closeBy{m_plot->xAxis->range().size() * 0.04};
  QStringList  texts;
  for (int i{0}; i < m_plot->graphCount(); ++i) {
    QCPGraph const *graph = m_plot->graph(i);
    int const       index{graph->visible() ? nearestIndex(graph, x) : -1};
    auto const      point = graph->data()->constBegin() + std::max(index, 0);
    if ((index < 0) || (std::fabs(point->key - x) > closeBy)) {
      texts << QStringLiteral("—");
      continue;
    }
    texts << (m_logAxisDataIndexes.contains(i) ? utils::formatScientific(point->value, 2) : formatValue(point->value));
  }
  m_legendController.setValueTexts(texts);
  m_plot->layer("legend")->replot();
}

void DataViewer::updateValueCursor(QPointF const &pos) {
  if (!m_data || (0 == m_plot->graphCount()) || !m_plot->axisRect()->rect().contains(pos.toPoint())) {
    hideValueCursor();
    emit valueCursorHidden();
    return;
  }

  double const x{m_plot->xAxis->pixelToCoord(pos.x())};
  if (!m_valuesVisible) { // Tooltip off: the crosshair alone, always
    showCursor(x, m_plot->yAxis->pixelToCoord(pos.y()), -1, -1);
    return;
  }

  // an event / anomaly marker under the mouse: its text
  for (Marker const &marker : std::as_const(m_markers)) {
    if (std::fabs(m_plot->xAxis->coordToPixel(marker.x) - pos.x()) <= 6.0 * utils::ssf()) {
      showCursor(marker.x, qQNaN(), -1, -1);
      theme::Colors const &c = theme::colors();
      m_valueText->position->setType(QCPItemPosition::ptPlotCoords);
      m_valueText->position->setAxes(m_plot->xAxis, m_plot->yAxis);
      m_valueText->position->setCoords(marker.x, m_plot->yAxis->pixelToCoord(pos.y()));
      m_valueText->setText(QStringLiteral("%1\n%2").arg(marker.text,
                                                        QDateTime::fromSecsSinceEpoch(static_cast<qint64>(std::llround(marker.x))).toString("hh:mm:ss")));
      m_valueText->setColor(marker.color.darker(120));
      m_valueText->setPen(QPen(marker.color));
      m_valueText->setBrush(QBrush(QColor(c.panel.red(), c.panel.green(), c.panel.blue(), 235)));
      m_valueText->setPositionAlignment(((m_plot->xAxis->coordToPixel(marker.x) > m_plot->axisRect()->rect().center().x()) ? Qt::AlignRight : Qt::AlignLeft)
                                        | Qt::AlignVCenter);
      m_valueText->setVisible(true);
      m_plot->layer("cursor")->replot();
      emit valueCursorMoved(marker.x, -1);
      return;
    }
  }

  // the visible line closest to the mouse at the time pointed at
  double       bestDist{std::numeric_limits<double>::max()};
  int          bestGraph{-1};
  int          bestIndex{-1};
  for (int i{0}; i < m_plot->graphCount(); ++i) {
    QCPGraph *graph = m_plot->graph(i);
    if (!graph->visible()) {
      continue;
    }
    int const index{nearestIndex(graph, x)};
    if (index < 0) {
      continue;
    }
    auto const    point = graph->data()->constBegin() + index;
    QPointF const pixel = graph->coordsToPixels(point->key, point->value);
    double const  dist{std::hypot(pixel.x() - pos.x(), pixel.y() - pos.y())};
    if (dist < bestDist) {
      bestDist  = dist;
      bestGraph = i;
      bestIndex = index;
    }
  }
  if (bestGraph < 0) {
    hideValueCursor();
    emit valueCursorHidden();
    return;
  }

  // crosshair at the mouse, as in the data analysis charts
  showCursor(x, m_plot->yAxis->pixelToCoord(pos.y()), bestGraph, bestIndex);
  emit valueCursorMoved(x, bestGraph);
}

void DataViewer::showSyncCursor(double x, int graphIndex) {
  if (!m_data || (0 == m_plot->graphCount())) {
    return;
  }
  // the same line's sample close to that time, if it has one
  QCPGraph *graph = ((graphIndex >= 0) && (graphIndex < m_plot->graphCount())) ? m_plot->graph(graphIndex) : nullptr;
  int const index{(graph && graph->visible()) ? nearestIndex(graph, x) : -1};
  bool const atPoint{(index >= 0)
                     && (std::fabs((graph->data()->constBegin() + index)->key - x) <= m_plot->xAxis->range().size() * 0.04)};
  if (!atPoint) {
    showCursor(x, qQNaN(), -1, -1);
    return;
  }
  auto const   point = graph->data()->constBegin() + index;
  double const y{m_plot->yAxis->pixelToCoord(graph->valueAxis()->coordToPixel(point->value))};
  showCursor(x, y, graphIndex, index);
}

void DataViewer::showCursor(double x, double y, int graphIndex, int index) {
  m_cursor->point1->setCoords(x, 0);
  m_cursor->point2->setCoords(x, 1);
  m_cursor->setVisible(true);
  m_cursorH->point1->setCoords(0, y);
  m_cursorH->point2->setCoords(1, y);
  m_cursorH->setVisible(!std::isnan(y));
  showLegendValues(x);
  if (index < 0) {
    m_tracer->setVisible(false);
    m_valueText->setVisible(false);
    m_plot->layer("cursor")->replot();
    return;
  }

  QCPGraph     *graph = m_plot->graph(graphIndex);
  auto const    point = graph->data()->constBegin() + index;
  QColor const  color = graph->pen().color();
  QVariantMap const style = (graphIndex < m_stateListVariant.size()) ? m_stateListVariant[graphIndex].toMap() : QVariantMap{};
  QString const     label = shownStyle(style).value(QStringLiteral("label")).toString();
  QString           value = m_logAxisDataIndexes.contains(graphIndex) ? utils::formatScientific(point->value, 2) : formatValue(point->value);
  if (pressure::isPressureChannel(style.value(QStringLiteral("name")).toString())) {
    value += QStringLiteral(" Torr");
  }
  QString const clock = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(std::llround(point->key))).toString("hh:mm:ss");
  QString const time  = m_relativeTime ? QStringLiteral("t = %1  (%2)").arg(durationText(point->key - m_data->getTimes().first()), clock) : clock;

  m_tracer->setGraph(graph);
  m_tracer->setGraphKey(point->key);
  m_tracer->setPen(QPen(color, 2 * utils::ssf()));
  m_tracer->setBrush(QBrush(theme::colors().panel));
  m_tracer->setVisible(true);

  theme::Colors const &c = theme::colors();
  m_valueText->position->setType(QCPItemPosition::ptPlotCoords);
  m_valueText->position->setAxes(m_plot->xAxis, graph->valueAxis());
  m_valueText->position->setCoords(point->key, point->value);
  m_valueText->setText(QStringLiteral("%1\n%2\n%3").arg(label, time, value));
  m_valueText->setColor(color.darker(120));
  m_valueText->setPen(QPen(color));
  m_valueText->setBrush(QBrush(QColor(c.panel.red(), c.panel.green(), c.panel.blue(), 235)));
  // next to the point, towards the middle of the plot
  QPointF const anchor = m_valueText->position->pixelPosition();
  QRect const   area   = m_plot->axisRect()->rect();
  Qt::Alignment align  = (anchor.x() > area.center().x()) ? Qt::AlignRight : Qt::AlignLeft;
  align |= (anchor.y() < area.top() + area.height() * 0.3) ? Qt::AlignTop : Qt::AlignBottom;
  m_valueText->setPositionAlignment(align);
  m_valueText->setVisible(true);

  m_plot->layer("cursor")->replot();
}

void DataViewer::hideValueCursor() {
  if (!m_cursor->visible() && !m_tracer->visible() && !m_valueText->visible()) {
    return;
  }
  m_cursor->setVisible(false);
  m_cursorH->setVisible(false);
  m_tracer->setVisible(false);
  m_valueText->setVisible(false);
  m_plot->layer("cursor")->replot();
  if (m_legendValues) { // the labels without values until the cursor is back
    m_legendController.setValueTexts({});
    m_plot->layer("legend")->replot();
  }
}

bool DataViewer::eventFilter(QObject *watched, QEvent *event) {
  if (watched == m_plot) {
    switch (event->type()) {
      case QEvent::Leave:
        if (!m_fromTouch) {
          hideValueCursor();
          emit valueCursorHidden();
        }
        break;
      case QEvent::TouchBegin:
      case QEvent::TouchUpdate:
      case QEvent::TouchEnd:
      case QEvent::TouchCancel:
        handleTouch(static_cast<QTouchEvent *>(event));
        return true;
      default:
        break;
    }
  }
  return QWidget::eventFilter(watched, event);
}

void DataViewer::sendMouse(QEvent::Type type, QPointF const &pos, Qt::MouseButtons buttons) {
  Qt::MouseButton const button = (type == QEvent::MouseMove) ? Qt::NoButton : Qt::LeftButton;
  QMouseEvent           mouse(type, pos, m_plot->mapToGlobal(pos), button, buttons, Qt::NoModifier);
  QCoreApplication::sendEvent(m_plot, &mouse);
}

void DataViewer::handleTouch(QTouchEvent *event) {
  event->accept();
  QList<QEventPoint> active;
  for (QEventPoint const &point : event->points()) {
    if (point.state() != QEventPoint::Released) {
      active.push_back(point);
    }
  }
  auto endSingle = [this] {
    if (m_touchSingle) {
      sendMouse(QEvent::MouseButtonRelease, m_touchLast, Qt::NoButton);
      m_touchSingle = false;
    }
  };
  if (event->type() == QEvent::TouchCancel) {
    endSingle();
    m_pinching  = false;
    m_fromTouch = false;
    return;
  }

  // two fingers: pinch to zoom (each axis by how far the fingers spread along it) and move with their middle
  if (active.size() >= 2) {
    endSingle();
    QPointF const a = active[0].position();
    QPointF const b = active[1].position();
    if (!m_pinching) {
      m_pinching = true;
      m_pinchA   = a;
      m_pinchB   = b;
      m_pinchX   = m_plot->xAxis->range();
      m_pinchY   = m_plot->yAxis->range();
      m_pinchY2  = m_plot->yAxis2->range();
      setControlMode(ControlMode::ZOOM);
      return;
    }
    QRect const  area = m_plot->axisRect()->rect();
    double const minSpread{40.0 * utils::ssf()};
    double const startDx{std::max(std::fabs(m_pinchA.x() - m_pinchB.x()), minSpread)};
    double const startDy{std::max(std::fabs(m_pinchA.y() - m_pinchB.y()), minSpread)};
    double const nowDx{std::max(std::fabs(a.x() - b.x()), minSpread)};
    double const nowDy{std::max(std::fabs(a.y() - b.y()), minSpread)};
    double const scaleX{startDx / nowDx};
    // values zoom only when the fingers are spread vertically too
    double const scaleY{(std::fabs(m_pinchA.y() - m_pinchB.y()) > minSpread) ? startDy / nowDy : 1.0};
    QPointF const startMid = (m_pinchA + m_pinchB) / 2.0;
    QPointF const nowMid   = (a + b) / 2.0;
    // the data under the fingers' middle at the start stays under it
    double const xAnchor{m_pinchX.lower + (startMid.x() - area.left()) / area.width() * m_pinchX.size()};
    double const yAnchor{m_pinchY.upper - (startMid.y() - area.top()) / area.height() * m_pinchY.size()};
    double const xSize{m_pinchX.size() * scaleX};
    double const ySize{m_pinchY.size() * scaleY};
    double const xLower{xAnchor - (nowMid.x() - area.left()) / area.width() * xSize};
    double const yUpper{yAnchor + (nowMid.y() - area.top()) / area.height() * ySize};
    m_plot->xAxis->setRange(xLower, xLower + xSize);
    m_plot->yAxis->setRange(yUpper - ySize, yUpper);
    if (logAxisShown() && (m_pinchY2.lower > 0.0)) { // the same in decades
      double const lo{std::log10(m_pinchY2.lower)};
      double const hi{std::log10(m_pinchY2.upper)};
      double const anchor2{hi - (startMid.y() - area.top()) / area.height() * (hi - lo)};
      double const size2{(hi - lo) * scaleY};
      double const upper2{anchor2 + (nowMid.y() - area.top()) / area.height() * size2};
      setLogRange(upper2 - size2, upper2);
    }
    m_currentView = getRange();
    m_plot->replot(QCustomPlot::rpQueuedReplot);
    return;
  }

  if (m_pinching) { // the last finger of a pinch: nothing until it is lifted
    if (active.isEmpty()) {
      m_pinching  = false;
      m_fromTouch = false;
    }
    return;
  }

  // one finger: as the mouse
  if (event->type() == QEvent::TouchBegin && !active.isEmpty()) {
    m_fromTouch   = true;
    m_touchSingle = true;
    m_touchStart  = active[0].position();
    m_touchLast   = m_touchStart;
    sendMouse(QEvent::MouseButtonPress, m_touchStart, Qt::LeftButton);
    return;
  }
  if (!active.isEmpty()) {
    if (m_touchSingle) {
      m_touchLast = active[0].position();
      sendMouse(QEvent::MouseMove, m_touchLast, Qt::LeftButton);
    }
    return;
  }
  // lifted: a tap, or the second of a double tap (fits the whole data)
  bool const tap{m_touchSingle && ((m_touchLast - m_touchStart).manhattanLength() < 12 * utils::ssf())};
  endSingle();
  qint64 const now{QDateTime::currentMSecsSinceEpoch()};
  if (tap) {
    if ((now - m_lastTapMs < 350) && ((m_touchLast - m_lastTapPos).manhattanLength() < 30 * utils::ssf())) {
      setControlMode(ControlMode::LEAD_FULL);
      m_lastTapMs = 0;
    } else {
      m_lastTapMs  = now;
      m_lastTapPos = m_touchLast;
    }
  }
  m_fromTouch = false;
}

QJsonObject DataViewer::viewState() const {
  return QJsonObject{{"mode", static_cast<int>(m_controlMode)},
                     {"x0", m_plot->xAxis->range().lower},
                     {"x1", m_plot->xAxis->range().upper},
                     {"y0", m_plot->yAxis->range().lower},
                     {"y1", m_plot->yAxis->range().upper},
                     {"l0", m_plot->yAxis2->range().lower},
                     {"l1", m_plot->yAxis2->range().upper}};
}

void DataViewer::setViewState(QJsonObject const &state) {
  if (state.isEmpty()) {
    return;
  }
  auto const mode = static_cast<ControlMode>(state.value("mode").toInt(static_cast<int>(ControlMode::LEAD_FULL)));
  if ((mode == ControlMode::ZOOM) && !viewFitsData(state)) {
    setControlMode(ControlMode::LEAD_FULL); // a zoom beside or far wider than the data: the whole data instead
    return;
  }
  if (mode != ControlMode::ZOOM) {
    setControlMode(mode); // follows the data again
    return;
  }
  setControlMode(ControlMode::ZOOM);
  if (state.value("x1").toDouble() > state.value("x0").toDouble()) {
    m_plot->xAxis->setRange(state.value("x0").toDouble(), state.value("x1").toDouble());
  }
  if (state.value("y1").toDouble() > state.value("y0").toDouble()) {
    m_plot->yAxis->setRange(state.value("y0").toDouble(), state.value("y1").toDouble());
  }
  if ((state.value("l0").toDouble() > 0.0) && (state.value("l1").toDouble() > state.value("l0").toDouble())) {
    m_plot->yAxis2->setRange(state.value("l0").toDouble(), state.value("l1").toDouble());
  }
  m_currentView = getRange();
  m_plot->replot();
}

void DataViewer::setScatter(bool scatter) {
  m_scatter = scatter;
  for (int i{0}; i < m_plot->graphCount(); ++i) {
    applyLineStyle(m_plot->graph(i));
  }
  m_plot->replot(QCustomPlot::rpQueuedReplot);
}

void DataViewer::applyLineStyle(QCPGraph *graph) const {
  QColor const color = graph->pen().color();
  graph->setLineStyle(m_scatter ? QCPGraph::lsNone : QCPGraph::lsLine);
  graph->setScatterStyle(m_scatter ? QCPScatterStyle(QCPScatterStyle::ssDisc, color, color, 3 * utils::ssf())
                                   : QCPScatterStyle(QCPScatterStyle::ssNone));
}

void DataViewer::updateAxisTitles() {
  m_plot->xAxis->setLabel(m_relativeTime ? tr("Time from start, h:mm:ss") : tr("Time, hh:mm:ss"));
  for (QCPAxis *axis : {m_plot->yAxis, m_plot->yAxis2}) {
    // the visible channels on this axis: "Name, unit" in their labels
    QStringList names;
    QStringList units;
    // (before any data, e.g. the live chart while nothing is received: from the styles)
    for (int i{0}; i < m_stateListVariant.size(); ++i) {
      bool const      drawn{i < m_plot->graphCount()};
      QCPAxis const  *valueAxis = drawn ? m_plot->graph(i)->valueAxis() : (m_logAxisDataIndexes.contains(i) ? m_plot->yAxis2 : m_plot->yAxis);
      bool const      visible{drawn ? m_plot->graph(i)->visible() : m_stateListVariant[i].toMap().value("visible").toBool()};
      if ((valueAxis != axis) || !visible) {
        continue;
      }
      // "axis_label": what the line measures, when its label names something else (a comparison's experiment)
      QVariantMap const style = shownStyle(m_stateListVariant[i].toMap());
      QString const   label = style.value(QStringLiteral("axis_label"), style.value(QStringLiteral("label"))).toString();
      qsizetype const comma = label.lastIndexOf(QLatin1Char(','));
      QString const   name  = (comma > 0) ? label.left(comma).trimmed() : label.trimmed();
      QString const   unit  = (comma > 0) ? label.mid(comma + 1).trimmed() : QString{};
      names << name;
      if (!unit.isEmpty() && !units.contains(unit)) {
        units << unit;
      }
    }
    QString title;
    if (names.size() == 1) {
      title = units.isEmpty() ? names.first() : QStringLiteral("%1, %2").arg(names.first(), units.first());
    } else if (!names.isEmpty()) {
      // several channels: the words their names start with ("Temperature …"), and the unit(s)
      QStringList common = names.first().split(QLatin1Char(' '), Qt::SkipEmptyParts);
      for (QString const &name : std::as_const(names)) {
        QStringList const words = name.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        int               same{0};
        while ((same < common.size()) && (same < words.size()) && (common[same] == words[same])) {
          ++same;
        }
        common = common.mid(0, same);
      }
      QString const quantity = common.join(QLatin1Char(' '));
      QString const unitText = units.join(QStringLiteral(" | "));
      title = (units.size() <= 1) && !quantity.isEmpty() ? (unitText.isEmpty() ? quantity : QStringLiteral("%1, %2").arg(quantity, unitText))
                                                         : unitText;
    }
    // the pressure's axis while its line is hidden: still what it would show
    if (title.isEmpty() && (axis == m_plot->yAxis2)) {
      for (int i : std::as_const(m_logAxisDataIndexes)) {
        if ((i < m_stateListVariant.size()) && pressure::isPressureChannel(m_stateListVariant[i].toMap().value(QStringLiteral("name")).toString())) {
          title = pressure::torrLabel(QCoreApplication::translate("theme", "Pressure"));
          break;
        }
      }
    }
    axis->setLabel(title);
  }
}

void DataViewer::applyStyles() {
  quint16 statesCount{static_cast<quint16>(m_stateListVariant.size())};

  if (0 == statesCount) {
    m_logger->stream(Logger::Level::WAR) << "There is empty m_paramListVariant to update graph styles";
    return;
  }

  if (0 == m_plot->graphCount()) {
    m_logger->stream(Logger::Level::WAR) << "There is no graphs on plot";
    return;
  }

  if (m_plot->graphCount() != statesCount) {
    m_logger->stream(Logger::Level::WAR)
      << QString("There ara %1 grpahs and %2 graph styles in settings").arg(m_plot->graphCount()).arg(statesCount);
  }

  statesCount = std::min(statesCount, static_cast<quint16>(m_plot->graphCount()));
  for (uint16_t i{0U}; i < statesCount; ++i) {
    QVariantMap stateMap{m_stateListVariant[i].toMap()};
    applyStyle(i, stateMap, true);
  }

  m_plot->replot();
}

void DataViewer::applyStyle(int index, QVariantMap const &stateVariantMap, bool shadow) {
  if (index >= m_plot->graphCount()) {
    m_logger->stream(Logger::Level::WAR) << "There is no graph" << index;
    return;
  }

  quint32 const penWidth{stateVariantMap["width"].toUInt()};
  quint32 const penStyle{stateVariantMap["pen"].toUInt()};
  QString const color{stateVariantMap["color"].toString()};
  bool const    visibility{stateVariantMap["visible"].toBool()};

  QPen pen{QColor(color)};
  pen.setWidth(penWidth);
  pen.setStyle(static_cast<Qt::PenStyle>(penStyle));

  m_plot->graph(index)->setVisible(visibility);
  m_plot->graph(index)->setPen(pen);
  applyLineStyle(m_plot->graph(index)); // points take the line's colour
  updateAxisTitles();

  if (!shadow) {
    m_plot->replot();
  }
}

void DataViewer::updateData() {
  int const graphCount{m_plot->graphCount()};

  QVector<double> const times = m_data->getTimes();
  if (times.empty()) {
    qDebug() << "There are no data in DataHolder";
    return;
  }

  updateTimeOrigin();
  QVector<QVector<double>> const states = m_data->getStates();
  qsizetype const                n{times.size()};
  // only the samples newer than the last drawing (and the last one drawn: its time may have come again with other
  // values); all of them for other data, or data that changed otherwise. Smoothed: also the ones the new samples
  // reach in their window, smoothed with the samples before them (fewer samples than a window are drawn unsmoothed:
  // all of them again once there are enough)
  int const  window{(m_smoothing > 1) ? averageWindow(m_smoothing) : 1};
  int const  half{window / 2};
  bool const continues{(m_drawnHolder == m_data.get()) && (m_drawnCount > 1) && (m_drawnCount >= window) && (m_drawnCount <= n)
                       && (times.first() == m_drawnFirst) && (times[m_drawnCount - 1] == m_drawnLast) && (graphCount > 0)
                       && !m_plot->graph(0)->data()->isEmpty()};
  qsizetype const from{continues ? std::max<qsizetype>(0, m_drawnCount - 1 - half) : 0};
  qsizetype const context{std::max<qsizetype>(0, std::min<qsizetype>(from - half, n - window))};
  QVector<double> const keys = times.mid(from);
  for (int i{0U}; i < graphCount; ++i) {
    if (i >= states.size()) {
      m_plot->graph(i)->data()->clear();
      continue;
    }
    QVector<double> values;
    if (m_smoothing > 1) {
      values = smoothed(states[i].mid(context), m_smoothing).mid(from - context);
    } else {
      values = states[i].mid(from);
    }
    double const scale{m_torrScale.value(i, 1.0)}; // the pressure in Torr
    if (scale != 1.0) {
      for (double &value : values) {
        value *= scale;
      }
    }
    QCPGraph *graph = m_plot->graph(i);
    if (from > 0) {
      graph->data()->removeAfter(times[from - 1]);
      graph->addData(keys, values, /*alreadySorted*/ true);
    } else {
      graph->setData(keys, values, /*alreadySorted*/ true);
    }
  }
  m_drawnHolder = m_data.get();
  m_drawnCount  = n;
  m_drawnFirst  = times.first();
  m_drawnLast   = times.last();

  updateRange();
  if (m_overlayMin || m_overlayMax || m_overlayAvg) {
    rebuildDecorations(); // min / max / average follow the data (the other decorations do not)
  }
  updateSetpointLine(from);
  m_plot->replot(QCustomPlot::rpQueuedReplot);
  emit dataUpdated();
}

void DataViewer::setExtraSeries(QVector<Series> const &series) {
  for (QCPCurve *curve : std::as_const(m_extraCurves)) {
    m_plot->removePlottable(curve);
  }
  m_extraCurves.clear();
  m_extra = series;
  for (Series const &each : std::as_const(m_extra)) {
    auto *curve = new QCPCurve(m_plot->xAxis, m_plot->yAxis);
    curve->setPen(QPen(each.color, 2 * utils::ssf(), Qt::DashDotLine));
    curve->setSelectable(QCP::stNone);
    curve->setData(each.times, each.values);
    m_extraCurves.push_back(curve);
  }
  rebuildDecorations(); // their names
  m_plot->replot(QCustomPlot::rpQueuedReplot);
}

void DataViewer::setMarkers(QVector<Marker> const &markers) {
  m_markers = markers;
  rebuildDecorations();
  m_plot->replot(QCustomPlot::rpQueuedReplot);
}

void DataViewer::setStages(QVector<Region> const &stages) {
  m_stages = stages;
  rebuildDecorations();
  m_plot->replot(QCustomPlot::rpQueuedReplot);
}

void DataViewer::setSetpointLine(bool visible) {
  m_setpointVisible = visible;
  updateSetpointLine();
}

void DataViewer::setSetpointChannel(int index) {
  m_setpointChannel = index;
  m_setpointTimes.clear();
  m_setpointValues.clear();
  updateSetpointLine();
}

void DataViewer::setSetpointSeries(QVector<double> const &times, QVector<double> const &values) {
  m_setpointChannel = -1;
  m_setpointTimes   = times;
  m_setpointValues  = values;
  updateSetpointLine();
}

void DataViewer::updateSetpointLine(qsizetype from) {
  QVector<double> times  = m_setpointTimes;
  QVector<double> values = m_setpointValues;
  bool const      channel{(m_setpointChannel >= 0) && m_data};
  if (channel) {
    times                                 = m_data->getTimes();
    QVector<QVector<double>> const states = m_data->getStates();
    values = (m_setpointChannel < states.size()) ? states[m_setpointChannel] : QVector<double>{};
  }
  bool const show{m_setpointVisible && !times.isEmpty() && (times.size() == values.size())};
  m_setpointCurve->setVisible(show);
  if (!show) {
    m_setpointCurve->data()->clear(); // no stale line of another tab
    m_setpointCount = 0;
  } else if (channel && (from > 0) && (m_setpointCount >= from) && (m_setpointCurve->dataCount() == m_setpointCount)) {
    // the channel's newer samples only (the curve's parameter: the sample's index)
    m_setpointCurve->data()->removeAfter(static_cast<double>(from - 1));
    QVector<double> indexes(times.size() - from);
    std::iota(indexes.begin(), indexes.end(), static_cast<double>(from));
    m_setpointCurve->addData(indexes, times.mid(from), values.mid(from), /*alreadySorted*/ true);
    m_setpointCount = times.size();
  } else {
    m_setpointCurve->setData(times, values);
    m_setpointCount = channel ? times.size() : 0;
  }
  m_plot->replot(QCustomPlot::rpQueuedReplot);
}

void DataViewer::setRelativeTime(bool relative) {
  m_relativeTime = relative;
  m_plot->xAxis->setTicker(relative ? m_elapsedTicker : m_clockTicker);
  updateAxisTitles();
  updateTimeOrigin();
  m_plot->replot(QCustomPlot::rpQueuedReplot);
}

void DataViewer::updateTimeOrigin() {
  if (auto *elapsed = dynamic_cast<ElapsedTicker *>(m_elapsedTicker.data()); elapsed && m_data) {
    QVector<double> const times = m_data->getTimes();
    elapsed->origin             = times.isEmpty() ? 0.0 : times.first();
  }
}

void DataViewer::clear(QString const &text) {
  if (static_cast<bool>(m_updateDataConnection)) {
    disconnect(m_updateDataConnection);
  }
  m_data.reset();
  m_drawnHolder = nullptr;
  m_drawnCount  = 0;
  hideValueCursor();
  for (int i{0}; i < m_plot->graphCount(); ++i) {
    m_plot->graph(i)->data()->clear();
  }
  setRegions({});
  m_bands.clear();
  m_markers.clear();
  m_stages.clear();
  setExtraSeries({});
  rebuildDecorations();
  updateSetpointLine();
  updateAxisTitles();
  updateTextItem(text);
  m_plot->replot(QCustomPlot::rpQueuedReplot);
}

void DataViewer::setBands(QVector<Band> const &bands) {
  m_bands = bands;
  rebuildDecorations();
  m_plot->replot(QCustomPlot::rpQueuedReplot);
}

void DataViewer::setSmoothingWindow(int samples) {
  samples = std::max(1, samples);
  if (samples == m_smoothing) {
    return;
  }
  m_smoothing  = samples;
  m_drawnCount = 0; // every sample smoothed again
  if (m_data) {
    updateData();
  }
}

void DataViewer::setOverlays(bool min, bool max, bool avg, QString const &channel) {
  m_overlayChannel = channel;
  m_overlayMin     = min;
  m_overlayMax = max;
  m_overlayAvg = avg;
  rebuildDecorations();
  m_plot->replot(QCustomPlot::rpQueuedReplot);
}

void DataViewer::setFitStart(double from) {
  m_fitStart = from;
  setControlMode(ControlMode::LEAD_FULL);
}

bool DataViewer::fittedToAll() const {
  return (ControlMode::LEAD_FULL == m_controlMode) && std::isnan(m_fitStart);
}

void DataViewer::setTimeRange(double from, double to) {
  if (!(to > from)) { // also when one of them is no number
    return;
  }
  setControlMode(ControlMode::ZOOM);
  m_plot->xAxis->setRange(from, to);
  fitValues(from, to);
  m_currentView = getRange();
  m_plot->replot();
}

void DataViewer::fitValues(double from, double to) {
  for (QCPAxis *axis : {m_plot->yAxis, m_plot->yAxis2}) {
    fitAxis(axis, from, to);
  }
}

void DataViewer::fitAxis(QCPAxis *axis, double from, double to) {
  QCPRange range;
  bool     haveRange{false};
  for (int i{0}; i < m_plot->graphCount(); ++i) {
    QCPGraph *graph = m_plot->graph(i);
    if ((graph->valueAxis() != axis) || !graph->visible()) {
      continue;
    }
    bool           found{false};
    QCPRange const graphRange = graph->getValueRange(found, axis == m_plot->yAxis2 ? QCP::sdPositive : QCP::sdBoth, QCPRange(from, to));
    if (found) {
      range     = haveRange ? QCPRange(std::min(range.lower, graphRange.lower), std::max(range.upper, graphRange.upper)) : graphRange;
      haveRange = true;
    }
  }
  if (!haveRange) {
    return;
  }
  if (axis == m_plot->yAxis2) {
    axis->setRange(range.lower / 1.5, range.upper * 1.5);
  } else {
    double const pad{range.size() > 0 ? range.size() * 0.05 : 1.0};
    axis->setRange(range.lower - pad, range.upper + pad);
  }
}

bool DataViewer::logAxisShown() const {
  return m_useLogAxis && m_plot->yAxis2->visible() && (m_plot->yAxis2->scaleType() == QCPAxis::stLogarithmic);
}

void DataViewer::setLogRange(double lowerDecade, double upperDecade) {
  // from 1e-15 to 1e10, at least a fiftieth of a decade high
  constexpr double MIN_DECADE{-15.0};
  constexpr double MAX_DECADE{10.0};
  constexpr double MIN_SIZE{0.02};
  if (!std::isfinite(lowerDecade) || !std::isfinite(upperDecade)) {
    return;
  }
  if (upperDecade - lowerDecade < MIN_SIZE) {
    double const middle{(lowerDecade + upperDecade) / 2.0};
    lowerDecade = middle - MIN_SIZE / 2.0;
    upperDecade = middle + MIN_SIZE / 2.0;
  }
  double const size{std::min(upperDecade - lowerDecade, MAX_DECADE - MIN_DECADE)};
  lowerDecade = std::clamp(lowerDecade, MIN_DECADE, MAX_DECADE - size);
  m_plot->yAxis2->setRange(std::pow(10.0, lowerDecade), std::pow(10.0, lowerDecade + size));
}

void DataViewer::zoomLogAxis(double anchorPixelY, double mul) {
  QCPRange const range = m_plot->yAxis2->range();
  if (!logAxisShown() || (range.lower <= 0.0) || qFuzzyCompare(mul, 1.0)) {
    return;
  }
  double const lo{std::log10(range.lower)};
  double const hi{std::log10(range.upper)};
  double const anchor{std::isnan(anchorPixelY) ? (lo + hi) / 2.0 : std::log10(m_plot->yAxis2->pixelToCoord(anchorPixelY))};
  if (!std::isfinite(anchor)) {
    return;
  }
  setLogRange(anchor - (anchor - lo) * mul, anchor + (hi - anchor) * mul);
}

void DataViewer::moveLogAxis(double fraction) {
  QCPRange const range = m_plot->yAxis2->range();
  if (!logAxisShown() || (range.lower <= 0.0) || (fraction == 0.0)) {
    return;
  }
  double const lo{std::log10(range.lower)};
  double const hi{std::log10(range.upper)};
  double const shift{(hi - lo) * fraction};
  setLogRange(lo + shift, hi + shift);
}

void DataViewer::rebuildDecorations() {
  for (QCPAbstractItem *item : std::as_const(m_decorationItems)) {
    m_plot->removeItem(item);
  }
  m_decorationItems.clear();
  auto shown = [this](int channel) {
    return (channel >= 0) && (channel < m_plot->graphCount()) && m_plot->graph(channel)->visible();
  };
  auto addLine = [this](QCPAxis *axis, double value, QPen const &pen) {
    auto *line = new QCPItemStraightLine(m_plot);
    line->setLayer("regions");
    line->setSelectable(false);
    line->point1->setAxes(m_plot->xAxis, axis);
    line->point2->setAxes(m_plot->xAxis, axis);
    line->point1->setTypeX(QCPItemPosition::ptAxisRectRatio);
    line->point2->setTypeX(QCPItemPosition::ptAxisRectRatio);
    line->point1->setCoords(0, value);
    line->point2->setCoords(1, value);
    line->setPen(pen);
    m_decorationItems.push_back(line);
  };
  auto addText = [this](QCPAxis *axis, double x, double value, Qt::Alignment align, QString const &text, QColor const &color) {
    auto *item = new QCPItemText(m_plot);
    item->setLayer("regions");
    item->setSelectable(false);
    item->position->setAxes(m_plot->xAxis, axis);
    item->position->setTypeX(QCPItemPosition::ptAxisRectRatio);
    item->position->setCoords(x, value);
    item->setPositionAlignment(align);
    item->setText(text);
    item->setColor(color);
    m_decorationItems.push_back(item);
  };

  // calculated channels: named at their last value
  for (Series const &each : std::as_const(m_extra)) {
    for (qsizetype i{each.values.size() - 1}; i >= 0; --i) {
      if ((i >= each.times.size()) || std::isnan(each.values[i])) {
        continue;
      }
      auto *text = new QCPItemText(m_plot);
      text->setLayer("regions");
      text->setSelectable(false);
      text->position->setCoords(each.times[i], each.values[i]);
      text->setPositionAlignment(Qt::AlignRight | Qt::AlignBottom);
      text->setText(each.label);
      text->setColor(each.color.darker(120));
      m_decorationItems.push_back(text);
      break;
    }
  }

  // events and anomalies: dotted vertical lines
  for (Marker const &marker : std::as_const(m_markers)) {
    auto *line = new QCPItemStraightLine(m_plot);
    line->setLayer("regions");
    line->setSelectable(false);
    line->point1->setCoords(marker.x, 0);
    line->point2->setCoords(marker.x, 1);
    QColor color = marker.color;
    color.setAlpha(170);
    line->setPen(QPen(color, 1, Qt::DotLine));
    m_decorationItems.push_back(line);
  }
  // stages: a labelled strip along the top
  for (Region const &stage : std::as_const(m_stages)) {
    auto *rect = new QCPItemRect(m_plot);
    rect->setLayer("regions");
    rect->setSelectable(false);
    for (auto *position : {rect->topLeft, rect->bottomRight}) {
      position->setTypeY(QCPItemPosition::ptAxisRectRatio);
    }
    rect->topLeft->setCoords(stage.x0, 0.0);
    rect->bottomRight->setCoords(stage.x1, 0.035);
    QColor fill = stage.color;
    fill.setAlpha(150);
    rect->setPen(QPen(Qt::white, 1));
    rect->setBrush(fill);
    m_decorationItems.push_back(rect);
    if (!stage.label.isEmpty()) {
      auto *text = new QCPItemText(m_plot);
      text->setLayer("regions");
      text->setSelectable(false);
      text->position->setTypeY(QCPItemPosition::ptAxisRectRatio);
      text->position->setCoords((stage.x0 + stage.x1) / 2.0, 0.0175);
      text->setPositionAlignment(Qt::AlignCenter);
      text->setText(stage.label);
      text->setColor(Qt::white);
      text->setClipToAxisRect(true);
      m_decorationItems.push_back(text);
    }
  }

  // variable rules of the visible channels
  for (Band const &band : std::as_const(m_bands)) {
    if (!shown(band.channel)) {
      continue;
    }
    QCPAxis   *axis = m_plot->graph(band.channel)->valueAxis();
    bool const haveLo{!std::isnan(band.lo)};
    bool const haveHi{!std::isnan(band.hi)};
    if (haveLo && haveHi) {
      auto *rect = new QCPItemRect(m_plot);
      rect->setLayer("regions");
      rect->setSelectable(false);
      for (auto *position : {rect->topLeft, rect->bottomRight}) {
        position->setAxes(m_plot->xAxis, axis);
        position->setTypeX(QCPItemPosition::ptAxisRectRatio);
      }
      rect->topLeft->setCoords(0, std::max(band.lo, band.hi));
      rect->bottomRight->setCoords(1, std::min(band.lo, band.hi));
      QColor fill = band.color;
      fill.setAlpha(45);
      rect->setPen(QPen(band.color, 1, Qt::DashLine));
      rect->setBrush(fill);
      m_decorationItems.push_back(rect);
    } else {
      for (double const value : {band.lo, band.hi}) {
        if (!std::isnan(value)) {
          addLine(axis, value, QPen(band.color, 1.2, Qt::DashLine));
        }
      }
    }
    if (!band.label.isEmpty()) {
      addText(axis, 0.005, haveHi ? band.hi : band.lo, Qt::AlignLeft | Qt::AlignBottom, band.label, band.color.darker(120));
    }
  }

  // min / max / average of the chosen channel (its visible lines)
  if (m_overlayMin || m_overlayMax || m_overlayAvg) {
    QCPAxis *axis{nullptr};
    double minValue{std::numeric_limits<double>::max()};
    double maxValue{std::numeric_limits<double>::lowest()};
    double sum{0.0};
    qint64 count{0};
    for (int i{0}; i < m_plot->graphCount(); ++i) {
      QCPGraph *graph = m_plot->graph(i);
      bool const ofChannel{(i < m_stateListVariant.size()) && (m_stateListVariant[i].toMap()["name"].toString() == m_overlayChannel)};
      if (!graph->visible() || !ofChannel) {
        continue;
      }
      axis = graph->valueAxis();
      for (auto it = graph->data()->constBegin(); it != graph->data()->constEnd(); ++it) {
        if (!std::isnan(it->value)) {
          minValue = std::min(minValue, it->value);
          maxValue = std::max(maxValue, it->value);
          sum += it->value;
          ++count;
        }
      }
    }
    if ((count > 0) && axis) {
      auto overlay = [&](double value, QString const &name, QColor const &color) {
        addLine(axis, value, QPen(color, 1.2, Qt::DashLine));
        addText(axis, 0.995, value, Qt::AlignRight | Qt::AlignBottom, QStringLiteral("%1 %2").arg(name, formatValue(value)), color);
      };
      if (m_overlayMin) {
        overlay(minValue, tr("Min"), QColor("#0d9488"));
      }
      if (m_overlayMax) {
        overlay(maxValue, tr("Max"), QColor("#dc2626"));
      }
      if (m_overlayAvg) {
        overlay(sum / static_cast<double>(count), tr("Avg"), QColor("#ca8a04"));
      }
    }
  }
}

void DataViewer::updateRange() {
  // the time axis spans the data shown, whatever other curves (hidden ones included) still hold
  // (the samples drawn: the data may already hold newer ones, drawn at the next refresh)
  auto fitTime = [this] {
    if ((m_plot->graphCount() == 0) || m_plot->graph(0)->data()->isEmpty()) {
      m_plot->xAxis->rescale(true);
      return;
    }
    double const first{m_plot->graph(0)->data()->constBegin()->key};
    double const last{(m_plot->graph(0)->data()->constEnd() - 1)->key};
    if (!std::isnan(m_fitStart) && (last > m_fitStart)) {
      m_plot->xAxis->setRange(std::max(first, m_fitStart), last); // from the start asked for
    } else if (last > first) {
      m_plot->xAxis->setRange(first, last);
    } else {
      m_plot->xAxis->setRange(first - 30.0, first + 30.0); // a single sample
    }
  };
  switch (m_controlMode) {
    case ControlMode::LEAD_FULL:
      fitTime();
      if (!std::isnan(m_fitStart)) {
        fitValues(m_plot->xAxis->range().lower, m_plot->xAxis->range().upper); // the values of that part only
        m_currentView = getRange();
        break;
      }
      m_plot->yAxis->rescale(true);
      if (logAxisShown()) { // the pressure to its values too, so that both sets of lines fill the chart
        fitAxis(m_plot->yAxis2, m_plot->xAxis->range().lower, m_plot->xAxis->range().upper);
      }

      if (m_plot->yAxis->range().size() > 4000) {
        m_plot->yAxis->setRange(-2000, 2000);
      }

      if (m_plot->yAxis->range().size() < 20) {
        m_plot->yAxis->setRangeLower(m_plot->yAxis->range().lower - 10);
        m_plot->yAxis->setRangeUpper(m_plot->yAxis->range().upper + 10);
      }

      m_currentView = getRange();
      break;
    case ControlMode::LEAD_X: {
      fitTime();
      m_plot->xAxis->setRangeLower(m_plot->xAxis->range().upper - m_currentView.width());
      m_currentView.setLeft(m_plot->xAxis->range().lower);
      m_currentView.setRight(m_plot->xAxis->range().upper);
      break;
    }
    default:
      break;
  }

  // if ((LEAD_FULL == m_controlMode) || (LEAD_X == m_controlMode)) {
  //   setPlotRange(m_currentView);
  // }
}

void DataViewer::initGraphs(uint16_t graphCount) {
  m_tracer->setGraph(nullptr); // its graph is deleted
  hideValueCursor();
  m_plot->clearGraphs();
  m_drawnCount = 0;

  for (int i{0U}; i < graphCount; ++i) {
    auto desAxis = m_logAxisDataIndexes.contains(i) ? m_plot->yAxis2 : m_plot->yAxis;
    m_plot->addGraph(m_plot->xAxis, desAxis);
    // a long recording: the samples of each pixel column drawn as its lowest and highest (QCustomPlot), not one by one
    m_plot->graph()->setAdaptiveSampling(true);
  }
  // the logarithmic axis only while it has lines
  if (m_useLogAxis) {
    m_plot->yAxis2->setVisible(!m_logAxisDataIndexes.isEmpty());
  }
  applyStyles();
}

void DataViewer::mouseEventHeadler(QMouseEvent *event) {
  if (event->type() == QEvent::MouseButtonPress) {
    m_controlModeOnPress = m_controlMode;
    setControlMode(ControlMode::ZOOM);

    m_pointPress = event->pos();

    // right button: rectangle zoom, left button: drag (or a time range to annotate)
    bool const rectZoom{(event->button() == Qt::RightButton) || (m_boxZoomMode && (event->button() == Qt::LeftButton))};
    bool const annotate{m_annotateMode && (event->button() == Qt::LeftButton)};
    // a finger with Tooltip on moves the value label (two fingers pan and zoom)
    bool const scrub{m_fromTouch && m_valuesVisible && !annotate};
    m_plot->setSelectionRectMode(rectZoom ? QCP::srmZoom : (annotate ? QCP::srmCustom : QCP::srmNone));
    m_plot->setInteraction(QCP::iRangeDrag, !rectZoom && !annotate && !scrub);
  }

  if (event->type() == QEvent::MouseMove) { // the crosshair follows the mouse, with values when Tooltip is on
    updateValueCursor(event->position());
  }
}

void DataViewer::wheelEventHandler(QWheelEvent *event) {
  if (nullptr == event) {
    return;
  }

  int delta = event->angleDelta().y();
  if (0 == delta) {
    delta = event->pixelDelta().y();
  }
  if (0 == delta) {
    return;
  }

  double const zoomFactor{delta > 0 ? 0.8 : 1.25};
  zoomViewAt(event->position(), zoomFactor, zoomFactor);
  event->accept();
}

QRectF DataViewer::mouseToCoord(QRectF const &rect) const {
  // top-left
  QPointF const tl(m_plot->xAxis->pixelToCoord(rect.topLeft().x()), m_plot->yAxis->pixelToCoord(rect.topLeft().y()));
  // bottom-right
  QPointF const br(m_plot->xAxis->pixelToCoord(rect.bottomRight().x()),
                   m_plot->yAxis->pixelToCoord(rect.bottomRight().y()));
  return QRectF(tl, br).normalized();
}

QRectF DataViewer::getRange() const {
  // top-left
  QPointF const tl(m_plot->xAxis->range().lower, m_plot->yAxis->range().upper);
  // bottom-right
  QPointF const br(m_plot->xAxis->range().upper, m_plot->yAxis->range().lower);
  return {tl, br};
}

void DataViewer::setPlotRange(QRectF const &rect) {
  m_plot->xAxis->setRange(rect.left(), rect.right());
  m_plot->yAxis->setRange(rect.bottom(), rect.top());
}

void DataViewer::zoomViewAt(QPointF const &pixelPos, double mulX, double mulY) {
  double const xAnchor{m_plot->xAxis->pixelToCoord(pixelPos.x())};
  double const yAnchor{m_plot->yAxis->pixelToCoord(pixelPos.y())};

  QCPRange const xRange{m_plot->xAxis->range()};
  QCPRange const yRange{m_plot->yAxis->range()};

  m_currentView.setLeft(xAnchor - (xAnchor - xRange.lower) * mulX);
  m_currentView.setRight(xAnchor + (xRange.upper - xAnchor) * mulX);
  m_currentView.setTop(yAnchor + (yRange.upper - yAnchor) * mulY);
  m_currentView.setBottom(yAnchor - (yAnchor - yRange.lower) * mulY);
  limitView(m_currentView);
  zoomLogAxis(pixelPos.y(), mulY);

  if (DataViewer::ControlMode::LEAD_FULL == m_controlMode) {
    setControlMode(DataViewer::ControlMode::ZOOM);
  }

  setPlotRange(m_currentView);
  updateRange();
  m_plot->replot();
}

void DataViewer::setIndexesForLogAxis(QVector<int> const &indexes) {
  m_graphsStale        = m_graphsStale || (indexes != m_logAxisDataIndexes);
  m_logAxisDataIndexes = indexes;
}

QString DataViewer::channelLabel(int index) const {
  return ((index >= 0) && (index < m_stateListVariant.size())) ? m_stateListVariant[index].toMap()["label"].toString() : QString{};
}

DataViewerDialog::DataViewerDialog(
  Logger *logger, std::shared_ptr<DataHolder const> interval, QVariantList style, QWidget *parent)
    : QDialog(parent) {
  auto mainLay = new QVBoxLayout(this);
  mainLay->setContentsMargins(0, 0, 0, 0);

  auto ga = new DataViewer(logger, false, this);
  ga->setData(interval);
  ga->setStyles(style);
  ga->resize(600, 400);

  auto *closeBtn = new QPushButton(tr("Close"), this);

  mainLay->addWidget(ga);
  mainLay->addWidget(closeBtn);
  connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);

  setWindowTitle(tr("Preview"));
  resize(theme::dp(1125), theme::dp(750));
  setAttribute(Qt::WA_DeleteOnClose);
}

}
