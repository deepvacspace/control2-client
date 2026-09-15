#include "programgraph.h"

#include "constants.h"
#include "utils.h"

namespace deepvaccontrol {
ProgramGraph::ProgramGraph(QWidget *parent) : QFrame{parent} {
  setFrameStyle(QFrame::Box | QFrame::Plain);
  setStyleSheet("QFrame {border: 1px solid #b9b9b9;}");
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

void ProgramGraph::selectInterval(int index) {
  // m_lastSelectedIndex = index;

  double const max{m_plot->yAxis->range().upper + 10};
  double const min{m_plot->yAxis->range().lower - 10};
  selectionRect->topLeft->setCoords(index, max);
  selectionRect->bottomRight->setCoords(index + 1, min);
  m_plot->layer("selectionLay")->replot();
}

void ProgramGraph::initGui() {
  m_plot = new QCustomPlot(this);

  for (int i{0}; i < m_paramOrder.size(); ++i) {
    m_plot->addGraph();

    QVariantMap paramMap = m_model->getParamListVariant().at(i).toMap();
    if (paramMap.contains("color")) {
      QColor color(paramMap.value("color").toString());
      if (color.isValid()) {
        m_plot->graph(i)->setPen(QPen(color));
      }
    }
    if (QStringList{"duration", "flags"}.contains(m_paramOrder.at(i))) {
      m_plot->graph(i)->setVisible(false);
    }
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

  m_plot->addLayer("selectionLay");
  m_plot->layer("selectionLay")->setMode(QCPLayer::lmBuffered);

  selectionRect = new QCPItemRect(m_plot);
  selectionRect->setLayer("selectionLay");
  selectionRect->setBrush(QBrush(QColor(0, 50, 150, 40)));
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
            m_chartData[i] << (intervalTempRamp ? (m_chartData[i].isEmpty() ? 25 : m_chartData[i].last()) : itemData);
            m_chartData[i] << itemData;
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
  } // namespace deepvaccontrol
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
  m_plot->replot();

  selectInterval(m_model->getSelectedInterval());
}

} // namespace deepvaccontrol
