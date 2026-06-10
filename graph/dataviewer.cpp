#include "dataviewer.h"

#include <QHBoxLayout>

#include "utils.h"
namespace control2 {

static QString const textLayerName{"text_layer"};

DataViewer::DataViewer(Logger *logger, bool addSecondLogAxis, QWidget *parent)
    : QWidget(parent)
    , m_logger{logger}
    , m_plot(new QCustomPlot())
    , m_legendController(m_plot)
    , m_cursorMode{CursorMode::NONE}
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
    // update internal variable
    (void)event;
    m_currentView = mouseToCoord(rect);
  });

  // legend position
  m_plot->axisRect()->insetLayout()->setInsetAlignment(0, Qt::AlignLeft | Qt::AlignTop);

  m_plot->addLayer("cursor", m_plot->layer("legend"), QCustomPlot::limBelow);
  m_plot->layer("cursor")->setMode(QCPLayer::lmBuffered);

  m_tracer = new QCPItemTracer(m_plot);
  m_tracer->setLayer("cursor");
  m_tracer->setStyle(QCPItemTracer::tsNone);

  m_cursor = new QCPItemStraightLine(m_plot);
  m_cursor->setLayer("cursor");
  m_cursor->setAntialiased(false);

  m_cursor->setPen(QPen(Qt::black, 1, Qt::DashLine));

  m_plot->addLayer(textLayerName, m_plot->layer("legend"), QCustomPlot::limBelow);
  m_plot->layer(textLayerName)->setMode(QCPLayer::lmBuffered);

  m_textItem = new QCPItemText(m_plot);
  m_textItem->setLayer(m_plot->layer(textLayerName));
  m_textItem->setPositionAlignment(Qt::AlignRight | Qt::AlignTop);
  m_textItem->position->setType(QCPItemPosition::ptAxisRectRatio);
  m_textItem->position->setCoords(0.98, 0.0);
  m_textItem->setColor(QColor{0, 0, 0, 127});
  m_textItem->setBrush(QBrush(QColor{255, 255, 255, 200}));
  {
    QFont font;
    font.setBold(true);
    font.setPointSize(10 * utils::ssf());
    m_textItem->setFont(font);
  }
  updateTextItem(QString{});

  connect(this, &DataViewer::cursorPositionChanged, this, &DataViewer::updateLegend);

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
  m_stateListVariant = stateListVariant;
  applyStyles();
}

void DataViewer::setStyle(int index, QVariantMap stateMapVariant) {
  m_legendController.setStyle(index, stateMapVariant);
  applyStyle(index, stateMapVariant, false); // this replot graphs and legend
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
  // setCursorMode(CursorMode::NONE);
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

void DataViewer::switchCursor() {
  switch (m_cursorMode) {
    case DataViewer::CursorMode::NONE:
      setCursorMode(DataViewer::CursorMode::LEGEND_ONLY);
      break;
    case DataViewer::CursorMode::LEGEND_ONLY:
      setCursorMode(DataViewer::CursorMode::LEGEND_CURSOR);
      break;
    case DataViewer::CursorMode::LEGEND_CURSOR:
      setCursorMode(DataViewer::CursorMode::NONE);
      break;
    default:
      break;
  }
  updateLegend();
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
}

void DataViewer::setCursorMode(CursorMode mode) {
  m_cursorMode = mode;
  bool const cursorVis{CursorMode::LEGEND_CURSOR == m_cursorMode};
  bool const legendVis{CursorMode::NONE != m_cursorMode};

  if (cursorVis) {
    m_tracer->setGraph(m_plot->graph(0));
    if (CursorMode::LEGEND_CURSOR == m_cursorMode) {
      setControlMode(ControlMode::ZOOM);
    }
    m_plot->setSelectionRectMode(QCP::srmNone);

    m_cursor->point1->setParentAnchorX(m_tracer->anchor("position"));
    m_cursor->point2->setParentAnchorX(m_tracer->anchor("position"));
  }

  m_plot->layer("cursor")->setVisible(cursorVis);
  m_legendController.setVisible(legendVis);
  m_legendController.setValueVisible(cursorVis);

  m_plot->layer("cursor")->replot();
  m_plot->layer("legend")->replot();
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

  for (int i{0U}; i < graphCount; ++i) {
    m_plot->graph(i)->setData(times, m_data->getStates()[i], /*alreadySorted*/ true);
  }

  updateRange();
  m_plot->replot(QCustomPlot::rpQueuedReplot);
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

void DataViewer::updateLegend() {
  if ((CursorMode::NONE == m_cursorMode) || (0 == m_plot->graphCount())) {
    return;
  }

  double const    time{m_tracer->position->key()};
  QDateTime const timeStamp{QDateTime::fromSecsSinceEpoch(time)};
  QVector<double> data = m_data->getValueAtTime(time);

  m_legendController.setValues(timeStamp, data);

  m_plot->layer("legend")->replot();
}

void DataViewer::initGraphs(uint16_t graphCount) {
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
  bool const isCtrlPressed  = event->modifiers().testFlag(Qt::ControlModifier);
  bool const isMousePressed = event->type() == QEvent::MouseButtonPress;
  bool const isMouseMove    = event->type() == QEvent::MouseMove;
  if (isMousePressed) {
    m_controlModeOnPress = m_controlMode;
    setControlMode(ControlMode::ZOOM);

    m_pointPress = event->pos();

    if (CursorMode::LEGEND_CURSOR != m_cursorMode) {
      if (isCtrlPressed) {
        m_plot->setSelectionRectMode(QCP::srmNone);
        m_plot->setInteraction(QCP::iRangeDrag, true);
      } else {
        m_plot->setSelectionRectMode(QCP::srmZoom);
        m_plot->setInteraction(QCP::iRangeDrag, false);
      }
    }
  }

  if (isMouseMove && (CursorMode::LEGEND_CURSOR == m_cursorMode)) {
    if ((!m_cursor) || (!m_tracer)) {
      return;
    }

    auto const mouseCord = event->pos();
    auto const xCord     = m_plot->xAxis->pixelToCoord(mouseCord.x());

    m_tracer->setGraphKey(xCord);
    m_tracer->updatePosition();

    double const tracerKey{m_tracer->position->key()};
    if (tracerKey == m_lastTracerKey) {
      return;
    }
    m_lastTracerKey = tracerKey;

    m_plot->layer("cursor")->replot();
    emit cursorPositionChanged(tracerKey);
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

} // namespace control2
