#ifndef CALIBRATIONWIDGET_H
#define CALIBRATIONWIDGET_H

#include <QWidget>

#include "calibrationmodel.h"
#include "systemvalue.h"

namespace control2 {
class CalibrationWidget : public QWidget {
  Q_OBJECT
public:
  explicit CalibrationWidget(QVariantMap const &calibrationVariantMap,
                             SystemValue       *curSysValue,
                             SystemValue       *curRawSysValue,
                             QWidget           *parent = nullptr);
  ~CalibrationWidget() override = default;

  void        setDataVariant(QVariantMap const &data);
  QVariantMap getDataVariant() const;

private:
  CalibrationModel m_calibModel;
};

inline void CalibrationWidget::setDataVariant(QVariantMap const &data) {
  m_calibModel.setDataVariant(data);
}

inline QVariantMap CalibrationWidget::getDataVariant() const {
  return m_calibModel.getDataVariant();
}
} // namespace control2

#endif // CALIBRATIONWIDGET_H
