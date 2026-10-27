#include "datamanager.h"

#include <QDateTime>
#include <QDebug>

#include "constants.h"
#include "dataholder.h"
#include "recordingfile.h"
#include "resholder.h"
#include "stringpathprovider.h"

namespace deepvaccontrol {
DataManager::DataManager(ResHolder *resHolder, quint16 numberOfStates, quint32 intervalDuration, QObject *parent)
    : QObject{parent}, m_numberOfStates{numberOfStates}, m_intervalDuration{intervalDuration}, m_resHolder{resHolder} {
}

DataManager::~DataManager() {
  if (m_intervalToAdd && m_intervalToAdd->isDirty()) {
    m_intervalToAdd->addNaN();
    m_intervalToAdd->save();
  }
}

bool DataManager::addPoint(double epochSecTime, QVector<double> const &pointVector) {
  double const key{utils::calculateKey(epochSecTime)};

  bool needAnotherInterval{false};

  if (m_intervalToAdd) {
    if (m_intervalToAdd->getIntervalKey() != key) { // need another interval
      m_resHolder->logger->stream(Logger::Level::INFO) << "Data from another interval, lets create/open it";

      // commit holder data
      m_intervalToAdd->save();

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

    // the day so far, when the application ran earlier today (a Parquet day, or an older .dat one)
    if (QString const existing = recordingfile::readPath(key); !existing.isEmpty()) {
      m_resHolder->logger->stream(Logger::Level::DEB) << "Loading the day's recording" << existing;
      StringPathProvider path(existing, m_resHolder->logger);
      m_intervalToAdd->loadFromFile(&path);
    }

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

  // the day's recording (Parquet, or an older .dat day)
  QString const existing = recordingfile::readPath(key);
  if (existing.isEmpty()) {
    return false;
  }
  StringPathProvider path(existing, m_resHolder->logger);
  return m_intervalToDisplay->loadFromFile(&path);
}

}
