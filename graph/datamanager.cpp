#include "datamanager.h"

#include <QDateTime>
#include <QDebug>

#include "constants.h"
#include "dataholder.h"
#include "resholder.h"
#include "stringpathprovider.h"

namespace control2 {
DataManager::DataManager(ResHolder *resHolder, quint16 numberOfStates, quint32 intervalDuration, QObject *parent)
    : QObject{parent}, m_numberOfStates{numberOfStates}, m_intervalDuration{intervalDuration}, m_resHolder{resHolder} {
}

DataManager::~DataManager() {
  if (m_intervalToAdd && m_intervalToAdd->isDirty()) {
    m_intervalToAdd->addNaN();
    StringPathProvider path(control2::INTERVALS_DIR + utils::calculateFileName(m_intervalToAdd->getIntervalKey()),
                            m_resHolder->logger);
    m_intervalToAdd->saveToFile(&path);
  }
}

bool DataManager::addPoint(double epochSecTime, QVector<double> const &pointVector) {
  double const key{utils::calculateKey(epochSecTime)};

  bool needAnotherInterval{false};

  if (m_intervalToAdd) {
    if (m_intervalToAdd->getIntervalKey() != key) { // need another interval
      m_resHolder->logger->stream(Logger::Level::INFO) << "Data from another interval, lets create/open it";

      // commit holder data
      StringPathProvider path(control2::INTERVALS_DIR + utils::calculateFileName(m_intervalToAdd->getIntervalKey()),
                              m_resHolder->logger);
      m_intervalToAdd->saveToFile(&path);

      needAnotherInterval = true;
    }
  } else {
    needAnotherInterval = true;
  }

  if (needAnotherInterval) {
    // make in memory new holder anyway
    m_resHolder->logger->stream(Logger::Level::DEB)
      << "Creating new data holder with key" << QDateTime::fromSecsSinceEpoch(key).toString("yyyy-MM-dd_HH-mm-ss")
      << "...";
    m_intervalToAdd = std::make_shared<DataHolder>(m_numberOfStates, m_resHolder->logger, key);

    // try to load interval from file
    m_resHolder->logger->stream(Logger::Level::DEB) << "Trying to load data holder from file...";
    StringPathProvider path(control2::INTERVALS_DIR + utils::calculateFileName(key), m_resHolder->logger);
    m_intervalToAdd->loadFromFile(&path);

    emit intervalToAddRecreated(key);
  }

  return m_intervalToAdd->addPoint(epochSecTime, pointVector);
}

bool DataManager::prepareIntervalToDisplay(double epochSecTime) {
  double const key{utils::calculateKey(epochSecTime)};
  if (m_intervalToAdd && (m_intervalToAdd->getIntervalKey() == key)) {
    m_intervalToDisplay = m_intervalToAdd;
    return true;
  }

  // make in memory new holder anyway (release prev)
  m_intervalToDisplay = std::make_shared<DataHolder>(m_numberOfStates, m_resHolder->logger, key);

  // try to load interval from file
  StringPathProvider path(control2::INTERVALS_DIR + utils::calculateFileName(key), m_resHolder->logger);
  return m_intervalToDisplay->loadFromFile(&path);
}

} // namespace control2
