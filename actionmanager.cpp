#include "actionmanager.h"

#include <QDataStream>
#include <QDateTime>

#include "math.h"

namespace deepvaccontrol {
ActionManager::ActionManager(Logger *logger, QString const &fileStoragePath, QObject *parent)
    : QObject{parent}, m_logger{logger}, m_fileStoragePath{fileStoragePath}, m_displayCount{0U} {
  loadFromDisk();
}

void ActionManager::addAction(ActionType type, QString const &msg) {
  QVariantMap newAction;
  newAction["type"] = static_cast<quint8>(type);
  newAction["date"] = static_cast<quint64>(QDateTime::currentSecsSinceEpoch());
  newAction["msg"]  = msg;

  m_actionList.push_front(newAction);
  saveToDisk();

  QVariantList addedList;
  addedList.push_back(newAction);
  emit added(addedList, false);
}

QVariantList ActionManager::getNext() {
  quint32 const oldCount{m_displayCount};
  quint32 const newCount{std::min(2 * std::max(5U, m_displayCount), static_cast<quint32>(m_actionList.size()))};

  m_displayCount = newCount;
  if (m_actionList.size() == m_displayCount) {
    emit allDisplayed();
  }
  QVariantList ret{m_actionList.cbegin() + oldCount, m_actionList.cbegin() + newCount};
  return ret;
}

bool ActionManager::writeData(QFile &file) {
  QDataStream out{&file};
  out << m_actionList;
  return QDataStream::Status::Ok == out.status();
}

bool ActionManager::readData(QFile &file) {
  QVariantList loadList;

  QDataStream in{&file};
  in >> loadList;

  if (QDataStream::Status::Ok == in.status()) {
    std::swap(loadList, m_actionList);
    return true;
  }
  return false;
}
} // namespace deepvaccontrol
