#include "usbmanager.h"

#include <QStorageInfo>

namespace deepvaccontrol {

UsbManager::UsbManager(QObject *parent) : QObject(parent) {
  connect(&m_timer, &QTimer::timeout, this, &UsbManager::updateDriveList);
  m_timer.start(5000); // Проверять каждые 5 секунд
  updateDriveList();   // Первоначальное сканирование
}

QStringList UsbManager::getAvailableDrives() const {
  return m_currentDrives;
}

void UsbManager::updateDriveList() {
  QStringList newDrives;
  for (QStorageInfo const &storage : QStorageInfo::mountedVolumes()) {
    if (storage.isValid() && storage.isReady() && !storage.isRoot()) {
      newDrives.append(storage.rootPath());
    }
  }

  if (newDrives != m_currentDrives) {
    m_currentDrives = newDrives;
    emit drivesUpdated(m_currentDrives);
  }
}

} // namespace deepvaccontrol
