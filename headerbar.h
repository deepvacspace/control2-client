#ifndef HEADERBAR_H
#define HEADERBAR_H

#include <QWidget>

#include "resholder.h"

class QLabel;
class QToolButton;

namespace deepvaccontrol {

/**
 * @brief Top bar of the main window: the information of the former status bar (chamber and job status,
 * temperature with its setpoint, user, connection, USB and OPC state, clock) in the application design.
 */
class HeaderBar : public QWidget {
  Q_OBJECT
public:
  explicit HeaderBar(ResHolder *resHolder, QWidget *parent = nullptr);

protected:
  void paintEvent(QPaintEvent *event) override;
  void changeEvent(QEvent *event) override;

private:
  void updateStatus();
  void updateValues();
  void updateBadges();
  void restyle();

  ResHolder *m_resHolder;
  QWidget     *m_content{nullptr};
  QLabel    *m_brand{nullptr};
  QLabel    *m_statusDot{nullptr};
  QLabel    *m_status{nullptr};
  QLabel    *m_values{nullptr};
  QLabel    *m_user{nullptr};
  QLabel    *m_appState{nullptr};
  QLabel    *m_usb{nullptr};
  QLabel    *m_opc{nullptr};
  QLabel    *m_clock{nullptr};
  int        m_opcState{-2};
  bool       m_usbPresent{false};
};

}

#endif // HEADERBAR_H
