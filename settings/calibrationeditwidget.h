#ifndef CALIBRATIONEDITWIDGET_H
#define CALIBRATIONEDITWIDGET_H

#include <QPushButton>
#include <QWidget>

#include "calibrationmodel.h"
#include "lineeditnum.h"
#include "systemvalue.h"

namespace control2 {
class CalibrationEditWidget : public QWidget {
  Q_OBJECT
public:
  explicit CalibrationEditWidget(
    CalibrationModel *model, SystemValue *current, SystemValue *raw, QWidget *parent = nullptr);
  ~CalibrationEditWidget() override = default;

private:
  void updateGui(int selectedIndex);
  void handleSizeChanged(int newSize);

  CalibrationModel *m_model;
  SystemValue      *m_currentValue;
  SystemValue      *m_rawValue;

  SystemValue *m_valueSysValue;
  SystemValue *m_correctedSysValue;
  SystemValue *m_deltaSysValue;

  LineEditNum *m_valueEdit;
  LineEditNum *m_correctedEdit;
  LineEditNum *m_deltaEdit;

  QPushButton *m_addBtn;
  QPushButton *m_delBtn;
};
} // namespace control2

#endif // CALIBRATIONEDITWIDGET_H
