#include "programgraph.h"

#include "theme.h"
#include <QTimer>

#include "constants.h"
#include "pressureunits.h"
#include "utils.h"

namespace deepvaccontrol {
ProgramGraph::ProgramGraph(QWidget *parent) : QFrame{parent} {
  setFrameStyle(QFrame::Box | QFrame::Plain);
  setStyleSheet("QFrame {border: 1px solid #d9dee5;}");
}

void ProgramGraph::setPressureLabel(QString const &label) {
  m_pressureLabel = label;
}

void ProgramGraph::setModel(ProgramModel *newModel) {
  if (newModel == m_model) {
    return;
  }

  // unconnect

  m_model = newModel;

  m_paramOrder = m_model->getParamListOrder();

  // Get param types
  auto const paramListVariant = m_model->getParamListVariant();
  m_paramTypes.resize(paramListVariant.size());
  for (auto const &param : paramListVariant) {
    QString const param_name{param.toMap()["name"].toString()};
    m_paramTypes[m_paramOrder.indexOf(param_name)] = param.toMap()["type"].toInt();
  }

  // Get frequently used value from settings
  m_flagsParamIndexInModel = m_paramOrder.indexOf("flags");
  m_flagsParamType         = m_model->getParamListVariant()[m_flagsParamIndexInModel].toMap()["type"].toInt();

  for (int i{0U}; i < m_paramOrder.size(); ++i) {
    m_chartData << QVector<double>();
  }

  connect(m_model, &ProgramModel::numberOfIntervalChanged, this, &ProgramGraph::reDraw);
  connect(m_model, &ProgramModel::modelChanged, this, &ProgramGraph::reDraw);

  connect(m_model, &ProgramModel::intervalSelected, this, &ProgramGraph::selectInterval);

  initGui();
  reDraw();
}

void ProgramGraph::setGraphUpdatesEnabled(bool enable) {
  if (enable == updatesEnabled()) {
    return;
  }

  QWidget::setUpdatesEnabled(enable);
  reDraw();
}

void ProgramGraph::setStartTemperature(std::optional<double> temperature) {
  // small moves are not worth a redraw
  if ((temperature.has_value() == m_startTemp.has_value()) && (!temperature || (std::abs(*temperature - *m_startTemp) < 0.1))) {
    return;
  }
  m_startTemp = temperature;
  // hidden (another page or tab): drawn when shown
  if (!isVisible()) {
    m_stale = true;
    return;
  }
  if (m_model) {
    reDraw();
  }
}

void ProgramGraph::showEvent(QShowEvent *event) {
  QFrame::showEvent(event);
  if (m_stale && m_model) {
    m_stale = false;
    reDraw();
  }
}

void ProgramGraph::selectInterval(int index) {
  // m_lastSelectedIndex = index;

  double const max{m_plot->yAxis->range().upper + 10};
  double const min{m_plot->yAxis->range().lower - 10};
  selectionRect->topLeft->setCoords(index, max);
  selectionRect->bottomRight->setCoords(index + 1, min);
  // a single interval is the whole program: nothing to tell apart
  selectionRect->setVisible(m_model && (m_model->getStandardModel()->columnCount() > 1));
  m_plot->layer("selectionLay")->replot();
}

void ProgramGraph::initGui() {
  m_plot = new QCustomPlot(this);
  QTimer::singleShot(0, m_plot, [plot = m_plot] { theme::stylePlot(plot); });

  // a programmed pressure: in Torr on a logarithmic axis at the right
  bool const hasPressure{m_paramOrder.contains(QStringLiteral("pres"))};
  if (hasPressure) {
    theme::setupPressureAxis(m_plot->yAxis2);
  }
  for (int i{0}; i < m_paramOrder.size(); ++i) {
    bool const pressure{m_paramOrder.at(i) == QLatin1String("pres")};
    m_plot->addGraph(m_plot->xAxis, pressure ? m_plot->yAxis2 : m_plot->yAxis);

    QVariantMap paramMap = m_model->getParamListVariant().at(i).toMap();
    if (pressure) {
      m_plot->graph(i)->setPen(QPen(theme::colors().accent, 2));
    }
    if (paramMap.contains("color")) {
      QColor color(paramMap.value("color").toString());
      if (color.isValid()) {
        m_plot->graph(i)->setPen(QPen(color));
      }
    }
    if (QStringList{"duration", "flags"}.contains(m_paramOrder.at(i))) {
      m_plot->graph(i)->setVisible(false);
    }
    // a bold profile
    QPen pen = m_plot->graph(i)->pen();
    pen.setWidthF(3.0 * utils::ssf());
    m_plot->graph(i)->setPen(pen);
  }
  ticker = QSharedPointer<QCPAxisTickerText>(new QCPAxisTickerText());
  ticker->setSubTickCount(1);

  m_plot->xAxis->setTicker(ticker);
  m_plot->xAxis->setTickLengthIn(0);

  m_plot->xAxis->setSubTicks(true);
  m_plot->xAxis->setSubTickLengthIn(0);

  m_plot->xAxis->grid()->setSubGridPen(m_plot->xAxis->grid()->pen());
  m_plot->xAxis->grid()->setSubGridVisible(true);

  m_plot->xAxis->grid()->setPen(QPen(Qt::transparent));

  // axis titles: the intervals, and what the program sets (its temperature, in °C)
  m_plot->xAxis->setLabel(tr("Interval"));
  QStringList quantities;
  for (int i{0}; i < m_paramOrder.size(); ++i) {
    if (m_plot->graph(i)->visible() && (m_plot->graph(i)->valueAxis() == m_plot->yAxis)) {
      quantities << ((m_paramOrder.at(i) == QLatin1String("temp")) ? tr("Temperature, °C")
                                                                     : m_model->getParamListVariant().at(i).toMap().value("label").toString());
    }
  }
  m_plot->yAxis->setLabel(quantities.join(QStringLiteral(" | ")));

  m_plot->addLayer("selectionLay");
  m_plot->layer("selectionLay")->setMode(QCPLayer::lmBuffered);

  selectionRect = new QCPItemRect(m_plot);
  selectionRect->setLayer("selectionLay");
  selectionRect->setBrush(QBrush(QColor(39, 96, 230, 40)));
  selectionRect->setPen(Qt::NoPen);

  auto *mainLay = new QVBoxLayout(this);
  mainLay->setContentsMargins(0, 0, 0, 0);
  mainLay->addWidget(m_plot);
  // m_plot->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding);
  // this->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);

  connect(m_plot, &QCustomPlot::mousePress, this, &ProgramGraph::handleMouseClicked);
  connect(m_plot, &QCustomPlot::mouseDoubleClick, this, &ProgramGraph::handleMouseDoubleClicked);
}

void ProgramGraph::calculateChartData() {
  for (auto &paramChart : m_chartData) {
    paramChart.clear();
  }

  // update ticks
  {
    QMap<double, QString> labels;
    for (int i{0U}; i < m_model->getStandardModel()->columnCount(); ++i) {
      labels[i + 0.5f] = QString::number(i + 1);
    }
    ticker->setTicks(labels);
  }

  for (int colonIndex{0U}; colonIndex < m_model->getStandardModel()->columnCount(); ++colonIndex) {
    quint32 const intervalFlags{
      m_model->getStandardModel()->item(m_flagsParamIndexInModel, colonIndex)->data(m_flagsParamType).toUInt()};
    bool const intervalTempRamp{utils::getBitOfDword(intervalFlags, deepvaccontrol::RAMP_BIT)};

    for (int i{0U}; i < m_paramOrder.size(); ++i) {
      int const    itemDataType{m_paramTypes[i]};
      double const itemData{m_model->getStandardModel()->item(i, colonIndex)->data(itemDataType).toDouble()};

      switch (itemDataType) {
        case static_cast<int>(Qt::UserRole + 0): // duration
        {
          m_chartData[i] << colonIndex;
          m_chartData[i] << colonIndex + 1;
          break;
        }
        case static_cast<int>(Qt::UserRole + 2): // lum
        {
          m_chartData[i] << (utils::getBitOfDword(intervalFlags, i) ? utils::nLampToLux(itemData) * 0.01 : qQNaN());
          m_chartData[i] << (utils::getBitOfDword(intervalFlags, i) ? utils::nLampToLux(itemData) * 0.01 : qQNaN());
          break;
        }
        case static_cast<int>(Qt::UserRole + 3): // solar_en
        {
          m_chartData[i] << (utils::getBitOfDword(intervalFlags, i) ? 100 : qQNaN());
          m_chartData[i] << (utils::getBitOfDword(intervalFlags, i) ? 100 : qQNaN());
          break;
        }
        case 0: // any float point number
        {
          if ("temp" == m_paramOrder[i]) {
            // none where the interval controls only the pressure; a ramp from the previous interval's target (as the
            // PLC: also when that one controlled only the pressure), the first one from the chamber's temperature
            // (24 °C when not known, the reference of the control charts)
            bool const   controlled{utils::getBitOfDword(intervalFlags, i)};
            double const from{(colonIndex == 0) ? m_startTemp.value_or(24.0)
                                                : m_model->getStandardModel()->item(i, colonIndex - 1)->data(itemDataType).toDouble()};
            m_chartData[i] << (!controlled ? qQNaN() : intervalTempRamp ? from : itemData);
            m_chartData[i] << (controlled ? itemData : qQNaN());
          } else if ("pres" == m_paramOrder[i]) {
            // in Torr; none at or below zero on the logarithmic axis
            double const torr{utils::getBitOfDword(intervalFlags, i) ? pressure::toTorr(itemData, m_pressureLabel) : qQNaN()};
            m_chartData[i] << ((torr > 0.0) ? torr : qQNaN());
            m_chartData[i] << ((torr > 0.0) ? torr : qQNaN());
          } else {
            m_chartData[i] << (utils::getBitOfDword(intervalFlags, i) ? itemData : qQNaN());
            m_chartData[i] << (utils::getBitOfDword(intervalFlags, i) ? itemData : qQNaN());
          }
          break;
        }
        default: // flags and another invisivle
        {
          m_chartData[i] << 0;
          m_chartData[i] << 0;
        }
      }
    }
  } 
}

int ProgramGraph::getIntervalIndexOnPlot(QPoint onScreenPosition) {
  auto plotCord = m_plot->xAxis->pixelToCoord(onScreenPosition.x());
  return static_cast<int>(ceil(plotCord) - 1);
}

void ProgramGraph::handleMouseClicked(QMouseEvent *event) {
  int const index{getIntervalIndexOnPlot(event->pos())};
  if ((0 <= index) && (index < m_model->getStandardModel()->columnCount())) {
    m_model->selectInterval(index);
  }
}

void ProgramGraph::handleMouseDoubleClicked(QMouseEvent *event) {
  int const index{getIntervalIndexOnPlot(event->pos())};
  if ((0 <= index) && (index < m_model->getStandardModel()->columnCount())) {
    emit intervalDoubleClicked(index);
  }
}

void ProgramGraph::reDraw() {
  if (!updatesEnabled()) {
    return;
  }

  calculateChartData();

  for (int i{0U}; i < m_paramOrder.size(); ++i) {
    m_plot->graph(i)->setData(m_chartData.at(0), m_chartData.at(i), true);
  }
  m_plot->rescaleAxes();
  m_plot->yAxis->setRange(m_plot->yAxis->range().lower - 5, m_plot->yAxis->range().upper + 5);
  if (m_plot->yAxis2->visible()) { // the programmed pressure, in whole decades
    int const pi{static_cast<int>(m_paramOrder.indexOf(QStringLiteral("pres")))};
    bool      found{false};
    QCPRange const range = m_plot->graph(pi)->getValueRange(found, QCP::sdPositive);
    auto const [lower, upper] = found ? pressure::decadeRange(range.lower, range.upper) : std::pair{0.999e-3, 1.001e3};
    m_plot->yAxis2->setRange(lower, upper);
  }
  m_plot->replot();

  selectInterval(m_model->getSelectedInterval());
}

}
