#ifndef STATUSBAR_H
#define STATUSBAR_H

#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QWidget>

#include "resholder.h"

namespace control2 {
class StatusBar : public QWidget {
  Q_OBJECT
public:
  explicit StatusBar(ResHolder *resHolder, QWidget *parent = nullptr);

signals:
  void mouseReleased();

private:
  QFrame *makeSeparator();
  void    handleAppStateCahnged(StatusManager::AppState newState);
  void    handleHardStatusCahnged(HardwareStatus::HardwareStatusEnum const &newStatus,
                                  HardwareStatus::HardwareStatusEnum const &);
  void    handleOpcStateCahnged(int newState);
  void    handleUsb(QStringList driveList);

  ResHolder   *m_resHolder;
  QHBoxLayout *stateLay{nullptr};
  QDateTime    last;

  QLabel *m_hardStatus{nullptr};
  QLabel *m_appState{nullptr};
  QLabel *m_opcState{nullptr};
  QLabel *m_usbLabel{nullptr};

  // QObject interface
public:
  bool eventFilter(QObject *watched, QEvent *event) override;
};
} // namespace control2

#endif // STATUSBAR_H
