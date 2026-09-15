#ifndef SETTINGSLISTWIDGET_H
#define SETTINGSLISTWIDGET_H

#include "resholder.h"
#include "scrollwidget.h"

namespace deepvaccontrol {
class SettingsListWidget : public ScrollWidget {
  Q_OBJECT
public:
  explicit SettingsListWidget(ResHolder *resHolder, QWidget *parent = nullptr);
  ~SettingsListWidget() override = default;

private:
  ResHolder *m_resHolder;
};
} // namespace deepvaccontrol

#endif // SETTINGSLISTWIDGET_H
