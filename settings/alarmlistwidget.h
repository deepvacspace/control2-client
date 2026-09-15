#ifndef ALARMLISTWIDGET_H
#define ALARMLISTWIDGET_H

#include <QWidget>

#include "resholder.h"

namespace deepvaccontrol {
class AlarmListWidget : public QWidget {
  Q_OBJECT
public:
  explicit AlarmListWidget(ResHolder *resHolder, QWidget *parent = nullptr);
  ~AlarmListWidget() override = default;

};
} // namespace deepvaccontrol
#endif // ALARMLISTWIDGET_H
