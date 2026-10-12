#include "dataviewer.h"

#include "theme.h"

#include <QHBoxLayout>
#include <cmath>

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

// centred moving average ignoring NaN, NaN gaps are kept
QVector<double> smoothed(QVector<double> const &y, int samples) {
  int const window{(samples % 2 == 0) ? samples + 1 : samples};
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

QString formatValue(double value) {
  double const absValue{std::fabs(value)};
  if ((absValue != 0.0) && ((absValue < 1e-2) || (absValue >= 1e6))) {
    return QString::number(value, 'e', 2);
  }
  return QString::number(value, 'f', absValue >= 100 ? 1 : 3);
}
} // namespace

DataViewer::DataViewer(Logger *logger, bool addSecondLogAxis, QWidget *parent)
    : QWidget(parent)
    , m_logger{logger}
    , m_plot(new QCustomPlot())
    , m_legendController(m_plot)
    , m_useLogAxis{addSecondLogAxis} {
  m_plot->setAutoAddPlottableToLegend(false);
  auto mainLay = new QHBoxLayout(this);
  mainLay->setContentsMargins(0, 0, 0, 0);
  mainLay->addWidget(m_plot);

  QSharedPointer<QCPAxisTickerDateTime> timeTicker(new QCPAxisTickerDateTime);
  m_plot->xAxis->setTicker(timeTicker);
  timeTicker->setDateTimeFormat("hh:mm:ss");

  if (m_useLogAxis) {
    QSharedPointer<QCPAxisTickerLog> logTicker(new QCPAxisTickerLog);
    logTicker->setSubTickCount(4);
    logTicker->setTickCount(10);
    m_plot->yAxis2->setTicker(logTicker);
    m_plot->yAxis2->setScaleType(QCPAxis::stLogarithmic);
    m_plot->yAxis2->setNumberFormat("eb"); // e = exponential, b = beautiful decimal powers
    m_plot->yAxis2->setNumberPrecision(0); // makes sure "1*10^4" is displayed only as "10^4"
    m_plot->yAxis2->setRange(1e-7, 1e3);
    m_plot->yAxis2->setVisible(true);
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

  // legend position: a box over the top left of the chart, small labels
  m_plot->axisRect()->insetLayout()->setInsetAlignment(0, Qt::AlignLeft | Qt::AlignTop);
  m_plot->legend->setBorderPen(QPen(theme::colors().border));
  {
    QFont font = m_plot->legend->font();
    font.setPointSizeF(font.pointSizeF() * 0.8);
    m_plot->legend->setFont(font);
  }
  m_plot->setMouseTracking(true); // the value label follows the mouse without a button pressed
  m_plot->installEventFilter(this);

  // value label: over the legend box
  m_plot->addLayer("cursor", m_plot->layer("legend"), QCustomPlot::limAbove);
  m_plot->layer("cursor")->setMode(QCPLayer::lmBuffered);

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
  m_tracer->setSize(9 * utils::ssf());
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
  m_textItem->setColor(QColor{128, 128, 128, 160});
  theme::stylePlot(m_plot);
  m_textItem->setBrush(QBrush(QColor(theme::colors().panel.red(), theme::colors().panel.green(), theme::colors().panel.blue(), 200)));
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

  if (data->getNumberOfStates() != m_plot->graphCount()) {
    // [re]init graphs
    initGraphs(data->getNumberOfStates());

    if (m_data) { // previous dataInterval not empty
      m_logger->stream(Logger::Level::WAR) << "New DataHolder has another number of states";
    }
  }

  m_data = data;
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
  if (!m_data) {
    return;
  }

  if (m_dirtyForUi.exchange(false, std::memory_order_relaxed)) {
    updateData();
  }
}

void DataViewer::setStyles(QVariantList const &stateListVariant) {
  m_legendController.setStyles(stateListVariant);
  if (m_compactLegend) {
    m_legendController.compactLayout(3);
  }
  m_stateListVariant = stateListVariant;
  applyStyles();
  rebuildDecorations(); // bands and overlays follow the visible channels
}

void DataViewer::setStyle(int index, QVariantMap stateMapVariant) {
  m_legendController.setStyle(index, stateMapVariant);
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

void DataViewer::moveView(double addX, double addY) {
  QPointF const addition(fabs(m_currentView.width()) * addX, fabs(m_currentView.height()) * addY);
  m_currentView.moveCenter(m_currentView.center() + addition);

  if ((DataViewer::ControlMode::LEAD_FULL == m_controlMode) || (addX != 0.0)) {
    setControlMode(DataViewer::ControlMode::ZOOM);
  }

  setPlotRange(m_currentView);
  updateRange();
  m_plot->replot();
}

void DataViewer::zoomView(double mulX, double mulY) {
  QPointF const center{m_currentView.center()};
  double const  halfWidth{qAbs(m_currentView.width()) * 0.5};
  double const  halfHeight{qAbs(m_currentView.height()) * 0.5};

  m_currentView.setLeft(center.x() - halfWidth * mulX);
  m_currentView.setRight(center.x() + halfWidth * mulX);

  m_currentView.setTop(center.y() + halfHeight * mulY);
  m_currentView.setBottom(center.y() - halfHeight * mulY);

  if (DataViewer::ControlMode::LEAD_FULL == m_controlMode) {
    setControlMode(DataViewer::ControlMode::ZOOM);
  }

  setPlotRange(m_currentView);
  updateRange();
  m_plot->replot();
}

void DataViewer::setControlMode(ControlMode const mode) {
  m_controlMode = mode;
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
  m_plot->setCursor(enabled ? Qt::CrossCursor : Qt::ArrowCursor);
}

void DataViewer::setLabelsVisible(bool visible) {
  m_labelsVisible = visible;
  m_legendController.setVisible(visible || m_compactLegend);
}

void DataViewer::setValuesVisible(bool visible) {
  m_valuesVisible = visible;
  if (!visible) {
    hideValueCursor();
  }
}

void DataViewer::updateValueCursor(QPointF const &pos) {
  if (!m_data || (0 == m_plot->graphCount()) || !m_plot->axisRect()->rect().contains(pos.toPoint())) {
    hideValueCursor();
    emit valueCursorHidden();
    return;
  }

  // the visible line closest to the mouse at the time pointed at
  double const x{m_plot->xAxis->pixelToCoord(pos.x())};
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
  if (index < 0) {
    m_tracer->setVisible(false);
    m_valueText->setVisible(false);
    m_plot->layer("cursor")->replot();
    return;
  }

  QCPGraph     *graph = m_plot->graph(graphIndex);
  auto const    point = graph->data()->constBegin() + index;
  QColor const  color = graph->pen().color();
  QString const label = (graphIndex < m_stateListVariant.size()) ? m_stateListVariant[graphIndex].toMap()["label"].toString() : QString{};
  QString const value = m_logAxisDataIndexes.contains(graphIndex) ? utils::formatScientific(point->value, 2) : formatValue(point->value);
  QString const time  = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(std::llround(point->key))).toString("hh:mm:ss");

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
}

bool DataViewer::eventFilter(QObject *watched, QEvent *event) {
  if ((watched == m_plot) && (event->type() == QEvent::Leave)) {
    hideValueCursor();
    emit valueCursorHidden();
  }
  return QWidget::eventFilter(watched, event);
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

  if (!shadow) {
    m_plot->replot();
  }
}

void DataViewer::updateData() {
  int const graphCount{m_plot->graphCount()};

  auto const times = m_data->getTimes();
  if (times.empty()) {
    qDebug() << "There are no data in DataHolder";
    return;
  }

  QVector<QVector<double>> const states = m_data->getStates();
  for (int i{0U}; i < graphCount; ++i) {
    m_plot->graph(i)->setData(times, (m_smoothing > 1) ? smoothed(states[i], m_smoothing) : states[i], /*alreadySorted*/ true);
  }

  updateRange();
  rebuildDecorations();
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
  m_smoothing = samples;
  if (m_data) {
    updateData();
  }
}

void DataViewer::setOverlays(bool min, bool max, bool avg) {
  m_overlayMin = min;
  m_overlayMax = max;
  m_overlayAvg = avg;
  rebuildDecorations();
  m_plot->replot(QCustomPlot::rpQueuedReplot);
}

void DataViewer::setTimeRange(double from, double to) {
  if (to <= from) {
    return;
  }
  setControlMode(ControlMode::ZOOM);
  m_plot->xAxis->setRange(from, to);
  // values fitted to the visible time range
  for (QCPAxis *axis : {m_plot->yAxis, m_plot->yAxis2}) {
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
    if (haveRange) {
      if (axis == m_plot->yAxis2) {
        axis->setRange(range.lower / 1.5, range.upper * 1.5);
      } else {
        double const pad{range.size() > 0 ? range.size() * 0.05 : 1.0};
        axis->setRange(range.lower - pad, range.upper + pad);
      }
    }
  }
  m_currentView = getRange();
  m_plot->replot();
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

  // min / max / average of the visible channels on the main value axis
  if (m_overlayMin || m_overlayMax || m_overlayAvg) {
    double minValue{std::numeric_limits<double>::max()};
    double maxValue{std::numeric_limits<double>::lowest()};
    double sum{0.0};
    qint64 count{0};
    for (int i{0}; i < m_plot->graphCount(); ++i) {
      QCPGraph *graph = m_plot->graph(i);
      if (!graph->visible() || (graph->valueAxis() != m_plot->yAxis)) {
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
    if (count > 0) {
      auto overlay = [&](double value, QString const &name, QColor const &color) {
        addLine(m_plot->yAxis, value, QPen(color, 1.2, Qt::DashLine));
        addText(m_plot->yAxis, 0.995, value, Qt::AlignRight | Qt::AlignBottom, QStringLiteral("%1 %2").arg(name, formatValue(value)), color);
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
  switch (m_controlMode) {
    case ControlMode::LEAD_FULL:
      m_plot->xAxis->rescale();
      m_plot->yAxis->rescale(true);

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
      m_plot->xAxis->rescale();
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

  for (int i{0U}; i < graphCount; ++i) {
    auto desAxis = m_logAxisDataIndexes.contains(i) ? m_plot->yAxis2 : m_plot->yAxis;
    m_plot->addGraph(m_plot->xAxis, desAxis);
    m_plot->graph()->setAdaptiveSampling(false);
    // m_plot->graph()->setAntialiased(false);
  }
  applyStyles();
}

void DataViewer::mouseEventHeadler(QMouseEvent *event) {
  if (event->type() == QEvent::MouseButtonPress) {
    m_controlModeOnPress = m_controlMode;
    setControlMode(ControlMode::ZOOM);

    m_pointPress = event->pos();

    // right button: rectangle zoom, left button: drag (or a time range to annotate)
    bool const rectZoom{event->button() == Qt::RightButton};
    bool const annotate{m_annotateMode && (event->button() == Qt::LeftButton)};
    m_plot->setSelectionRectMode(rectZoom ? QCP::srmZoom : (annotate ? QCP::srmCustom : QCP::srmNone));
    m_plot->setInteraction(QCP::iRangeDrag, !rectZoom && !annotate);
  }

  if ((event->type() == QEvent::MouseMove) && m_valuesVisible) {
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

  if (DataViewer::ControlMode::LEAD_FULL == m_controlMode) {
    setControlMode(DataViewer::ControlMode::ZOOM);
  }

  setPlotRange(m_currentView);
  updateRange();
  m_plot->replot();
}

void DataViewer::setIndexesForLogAxis(QVector<int> const &indexes) {
  m_logAxisDataIndexes = indexes;
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

  auto *closeBtn = new QPushButton(tr("Закрыть"), this);

  mainLay->addWidget(ga);
  mainLay->addWidget(closeBtn);
  connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);

  setWindowTitle(tr("Предпросмотр"));
  resize(600 * utils::ssf(), 400 * utils::ssf());
  setAttribute(Qt::WA_DeleteOnClose);
}

} // namespace deepvaccontrol
