#ifndef SETTINGSLISTWIDGET_H
#define SETTINGSLISTWIDGET_H

#include "resholder.h"
#include <QWidget>

namespace deepvaccontrol {
// Settings -> Operating parameters: the PLC parameters as rows (label, PLC name, value with its unit).
class SettingsListWidget : public QWidget {
  Q_OBJECT
public:
  explicit SettingsListWidget(ResHolder *resHolder, QWidget *parent = nullptr);
  ~SettingsListWidget() override = default;

private:
  ResHolder *m_resHolder;
};
}

#endif // SETTINGSLISTWIDGET_H
