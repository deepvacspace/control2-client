#ifndef USBMANAGER_H
#define USBMANAGER_H
#include <QObject>
#include <QTimer>

namespace control2 {

class UsbManager : public QObject {
  Q_OBJECT
public:
  explicit UsbManager(QObject *parent = nullptr);
  QStringList getAvailableDrives() const;

signals:
  void drivesUpdated(QStringList const &newDrives);

private slots:
  void updateDriveList();

private:
  QTimer      m_timer;
  QStringList m_currentDrives;
};

}

#endif // USBMANAGER_H
