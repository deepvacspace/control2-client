#ifndef APPLICATIONSETTINGSWIDGET_H
#define APPLICATIONSETTINGSWIDGET_H

#include <QWidget>

#include "resholder.h"

namespace deepvaccontrol {

class LineEditTime;
class SystemValue;

// Settings -> Application: app-only settings from settings.json (not sent to the PLC): chart refresh and PLC
// polling periods, setpoint limits and the program shown at start.
class ApplicationSettingsWidget : public QWidget {
  Q_OBJECT
public:
  explicit ApplicationSettingsWidget(ResHolder *resHolder, QWidget *parent = nullptr);

private:
  void save();

  ResHolder    *m_resHolder;
  SystemValue  *m_graphPeriod{nullptr};
  SystemValue  *m_pollPeriod{nullptr};
  SystemValue  *m_tempMin{nullptr};
  SystemValue  *m_tempMax{nullptr};
  SystemValue  *m_defaultTemp{nullptr};
  SystemValue  *m_backupDays{nullptr}; // automatic database backups: every this many days
  LineEditTime *m_defaultDuration{nullptr};
};

} // namespace deepvaccontrol

#endif // APPLICATIONSETTINGSWIDGET_H
