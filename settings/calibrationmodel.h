#ifndef CALIBRATIONMODEL_H
#define CALIBRATIONMODEL_H

#include <QStandardItemModel>

namespace deepvaccontrol {
class CalibrationModel : public QStandardItemModel {
  Q_OBJECT
public:
  explicit CalibrationModel(QVariantMap calibVariantMap, QObject *parent = nullptr);
  ~CalibrationModel() override = default;

  void        setDataVariant(QVariantMap const &calibVariantMap);
  QVariantMap getDataVariant() const;

  // Returns index in model after insertion and sorting.
  int  addCalibPoint(float value, float corrected, float delta);
  void removePoint(int index);
  void selectIndex(int index, bool force = false);
  int  getSelectedIndex() const;

  float   getDeltaMax() const;
  float   getMin() const;
  float   getMax() const;
  quint32 getPoints() const;

signals:
  void indexSelected(int index);
  void sizeChanged(int newSize);

private:
  QString m_name;
  QString m_stateName;
  QString m_label;
  quint32 m_points;
  float   m_deltaMax; // delta
  float   m_min;      // main param value (temp/hum/pres)
  float   m_max;      // main param value (temp/hum/pres)

  int m_selectedIndex;
};

inline int CalibrationModel::getSelectedIndex() const {
  return m_selectedIndex;
}

inline void CalibrationModel::selectIndex(int index, bool force) {
  if ((index == m_selectedIndex) && !force) {
    return;
  }

  m_selectedIndex = index;
  if (m_selectedIndex >= rowCount()) {
    m_selectedIndex = -1;
  }

  emit indexSelected(index);
}

inline float CalibrationModel::getDeltaMax() const {
  return m_deltaMax;
}

inline float CalibrationModel::getMin() const {
  return m_min;
}

inline float CalibrationModel::getMax() const {
  return m_max;
}

inline quint32 CalibrationModel::getPoints() const {
  return m_points;
}
} // namespace deepvaccontrol

#endif // CALIBRATIONMODEL_H
