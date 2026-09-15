#include "calibrationmodel.h"

namespace deepvaccontrol {
CalibrationModel::CalibrationModel(QVariantMap calibVariantMap, QObject *parent)
    : QStandardItemModel{parent}, m_selectedIndex{0U} {
  this->setColumnCount(3);
  setHorizontalHeaderLabels({tr("Датчик камеры"), tr("Датчик внешний"), tr("Корректировка")});

  Q_ASSERT(!calibVariantMap.empty());
  setDataVariant(calibVariantMap);
}

void CalibrationModel::setDataVariant(QVariantMap const &calibVariantMap) {
  m_points    = calibVariantMap["points"].toUInt();
  m_deltaMax  = calibVariantMap["delta_max"].toFloat();
  m_min       = calibVariantMap["min"].toFloat();
  m_max       = calibVariantMap["max"].toFloat();
  m_name      = calibVariantMap["name"].toString();
  m_stateName = calibVariantMap["state_name"].toString();
  m_label     = calibVariantMap["label"].toString();

  // clear everything except header items
  removeRows(0U, rowCount());

  QVariantList const xPoints = calibVariantMap["x"].toList();
  QVariantList const deltas  = calibVariantMap["delta"].toList();

  if (xPoints.size() != deltas.size()) {
    qWarning() << "Wrong calibration data";
    return;
  }

  for (int row{0U}; row < xPoints.size(); ++row) {
    float const value{xPoints[row].toFloat()};
    float const delta{deltas[row].toFloat()};
    float const corrected{value + delta};

    addCalibPoint(value, corrected, delta);
  }

  if (rowCount() > 0) {
    selectIndex(0, true);
  }
}

QVariantMap CalibrationModel::getDataVariant() const {
  QVariantMap ret;

  ret["points"]     = m_points;
  ret["delta_max"]  = m_deltaMax;
  ret["min"]        = m_min;
  ret["max"]        = m_max;
  ret["name"]       = m_name;
  ret["state_name"] = m_stateName;
  ret["label"]      = m_label;

  QVariantList xPoints;
  QVariantList deltas;

  for (int row{0U}; row < rowCount(); ++row) {
    xPoints.push_back(item(row, 0)->data(Qt::DisplayRole).toFloat());
    deltas.push_back(item(row, 2)->data(Qt::DisplayRole).toFloat());
  }

  ret["x"]     = xPoints;
  ret["delta"] = deltas;

  return ret;
}

int CalibrationModel::addCalibPoint(float value, float corrected, float delta) {
  QList<QStandardItem *> rowToAdd;

  auto *valueItem = new QStandardItem();
  valueItem->setData(value, Qt::DisplayRole);

  auto *correctedItem = new QStandardItem();
  correctedItem->setData(corrected, Qt::DisplayRole);

  auto *deltaItem = new QStandardItem();
  deltaItem->setData(delta, Qt::DisplayRole);

  rowToAdd.push_back(valueItem);
  rowToAdd.push_back(correctedItem);
  rowToAdd.push_back(deltaItem);

  appendRow(rowToAdd);
  sort(0, Qt::SortOrder::DescendingOrder);

  emit sizeChanged(rowCount());
  return indexFromItem(valueItem).row();
}

void CalibrationModel::removePoint(int index) {
  removeRow(index);
  emit sizeChanged(rowCount());

  if (rowCount() <= index) {
    index = rowCount() - 1;
  }
  qDebug() << index;
  selectIndex(index, /*force*/ true);
}

} // namespace deepvaccontrol
