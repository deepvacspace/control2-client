#ifndef SETTINGSWIDGET_H
#define SETTINGSWIDGET_H

#include <QTabWidget>
#include <QWidget>

#include "calibrationwidget.h"
#include "pidwidget.h"
#include "resholder.h"

namespace deepvaccontrol {
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

  // the Users tab for every level; the other tabs for the Service Engineer only
  void handleAccessChange(QString const &, AccessManager::AccessLevel newLevel);

  ResHolder                 *m_resHolder;
  QTabWidget                *m_tabWidget{nullptr};
  QWidget                   *m_usersTab{nullptr};
  QList<PidWidget *>         m_pidWidgets;
  QList<CalibrationWidget *> m_calibWidgets;
};
}

#endif // SETTINGSWIDGET_H
