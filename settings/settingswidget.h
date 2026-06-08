#ifndef SETTINGSWIDGET_H
#define SETTINGSWIDGET_H

#include <QWidget>

#include "calibrationwidget.h"
#include "pidwidget.h"
#include "resholder.h"

namespace control2 {
class SettingsWidget : public QWidget {
  Q_OBJECT
public:
  explicit SettingsWidget(ResHolder *resHolder, QWidget *parent = nullptr);
  ~SettingsWidget() override = default;

private slots:
  // Save to disk
  // Send to server
  void commitSettings();

private:
  void initGui();

  void         setPids(QVariantList const &pidVariantList);
  QVariantList getPidsVariantList() const;

  void         setCalibrations(QVariantList const &calibVariantList);
  QVariantList getCalibrationsVariantList() const;

  void updatePidsAndCalibrations();

  ResHolder                 *m_resHolder;
  QList<PidWidget *>         m_pidWidgets;
  QList<CalibrationWidget *> m_calibWidgets;
};
} // namespace control2

#endif // SETTINGSWIDGET_H
