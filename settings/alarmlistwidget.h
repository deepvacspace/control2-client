#ifndef ALARMLISTWIDGET_H
#define ALARMLISTWIDGET_H

#include <QWidget>

#include "resholder.h"

namespace control2 {
class AlarmListWidget : public QWidget {
  Q_OBJECT
public:
  explicit AlarmListWidget(ResHolder *resHolder, QWidget *parent = nullptr);
  ~AlarmListWidget() override = default;

};
} // namespace control2
#endif // ALARMLISTWIDGET_H
