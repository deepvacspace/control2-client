#include "dataholder.h"

#include <QDateTime>
#include <QDebug>
#include <QElapsedTimer>
#include <QFile>
#include <QThread>
#include <QVariant>

#include "constants.h"
#include "recordingfile.h"
#include "stringpathprovider.h"
#include "utils.h"

namespace deepvaccontrol {
DataHolder::DataHolder(uint16_t numberOfStates,
                       Logger  *logger,
                       double   intervalKey,
                       quint64  maxLength,
                       quint32  autosaveSecPeriod,
                       QObject *parent)
    : QObject(parent)
    , FileHandler{}
    , m_numberOfStates{numberOfStates}
    , m_logger{logger}
    , m_intervalKey{intervalKey}
    , m_maxLength{maxLength}
    , m_autosaveSecPeriod{autosaveSecPeriod}
    , m_dirty{false} {
  clean();
  m_autosaveTimer = new QTimer(this);
  m_autosaveTimer->setSingleShot(false);
  connect(m_autosaveTimer, &QTimer::timeout, this, &DataHolder::autoSave);
}

DataHolder::~DataHolder() {
  m_autosaveTimer->stop();
  for (auto *thread : findChildren<QThread *>()) { // background saves finish first
    thread->wait();
  }
}

void DataHolder::save(bool inBackground) {
  QString const                  path = recordingfile::savePath(getIntervalKey());
  quint64 const                  generation{recordingfile::nextGeneration()};
  QVector<double> const          times  = m_times; // shared until the next sample: cheap
  QVector<QVector<double>> const states = m_states;
  m_dirty                               = false;
  if (!inBackground) {
    QString error;
    if (!recordingfile::saveDay(path, generation, times, states, &error)) {
      m_logger->stream(Logger::Level::ERR) << "Recording" << path << "not saved:" << error;
      m_dirty = true;
    }
    return;
  }
  QThread *thread = QThread::create([this, path, generation, times, states]() {
    QString error;
    if (!recordingfile::saveDay(path, generation, times, states, &error)) {
      QMetaObject::invokeMethod(
        this,
        [this, path, error]() {
          m_logger->stream(Logger::Level::ERR) << "Recording" << path << "not saved:" << error;
          m_dirty = true;
        },
        Qt::QueuedConnection);
    }
  });
  thread->setParent(this);
  connect(thread, &QThread::finished, thread, &QObject::deleteLater);
  thread->start(QThread::LowPriority);
}

void DataHolder::clean() {
  m_states.clear();
  m_times.clear();
  m_states.resize(m_numberOfStates);
}

bool DataHolder::addPoint(double epochSecTime, QVector<double> const &pointVector) {
  if (m_numberOfStates != pointVector.size()) {
    m_logger->stream(Logger::Level::DEB)
      << QString("DataHolder::addPoint: pointVector to add has %1 states, but dataHolder has %2 states")
           .arg(pointVector.size())
           .arg(m_numberOfStates);
    return false;
  }

  if (!m_autosaveTimer->isActive()) {
    m_autosaveTimer->start(m_autosaveSecPeriod * 1000);
  }

  // samples carry millisecond timestamps: only an exact duplicate time replaces the previous sample
  if (!m_times.empty() && (epochSecTime == m_times.back())) {
    // replace last
    for (int i{0U}; i < m_numberOfStates; ++i) {
      m_states[i].back() = pointVector[i];
    }
  } else {
    // add new
    m_times.push_back(epochSecTime);
    for (int i{0U}; i < m_numberOfStates; ++i) {
      m_states[i].push_back(pointVector[i]);
    }
  }

  emit dataChanged();
  m_dirty = true;
  return true;
}

void DataHolder::setSeries(QVector<double> times, QVector<QVector<double>> states) {
  m_autosaveTimer->stop();
  m_times = std::move(times);
  m_states = std::move(states);
  m_states.resize(m_numberOfStates);
  for (auto &state : m_states) {
    state.resize(m_times.size(), qQNaN());
  }
  m_dirty = false;
  emit dataChanged();
}

void DataHolder::addNaN() {
  QVector<double> vectorOfNaN(m_numberOfStates, qQNaN());
  addPoint(QDateTime::currentDateTime().toSecsSinceEpoch() + 1, vectorOfNaN);
}

QVector<double> DataHolder::getValueAtIndex(quint64 index) const {
  if (index >= m_states[0].size()) {
    m_logger->stream(Logger::Level::DEB) << "out of range!";
    return {};
  }

  QVector<double> out;
  for (auto const &item : m_states) {
    out.push_back(item.at(index));
  };
  return out;
}

void DataHolder::autoSave() {
  if (isDirty()) {
    save(true); // compressing a whole day takes a moment: not on the GUI thread
  }
}

QVector<double> DataHolder::getValueAtTime(double epochSecTime) const {
  auto pos             = std::find(m_times.begin(), m_times.end(), epochSecTime);
  auto indexInInterval = static_cast<quint64>(pos - m_times.begin());
  return getValueAtIndex(indexInInterval);
}

QPair<double, QVector<double>> DataHolder::getValueNearTime(double epochSecTime) const {
  if (!inRange(epochSecTime)) {
    return {0.0, {}};
  }

  // Some logic of getting apropriate point index
  //
  qint64 const indexInInterval{findFirstIndexAfterEqualTime(epochSecTime)};

  // if there are no points after given time, select prev (last in the interval)
  quint64 const indexNear = (-1 == indexInInterval) ? (m_times.size() - 1) : indexInInterval;

  return {m_times[indexNear], getValueAtIndex(indexNear)};
}

DataHolder &DataHolder::slice(double epochSecStartTime, double epochSecEndTime) {
  qint64 const startIndex{findFirstIndexAfterEqualTime(epochSecStartTime)};
  if (-1 == startIndex) {
    clean();
    m_logger->stream(Logger::Level::DEB) << "Sliced completely";
    return *this;
  }

  qint64 const endIndex{findLastIndexBeforeEqualTime(epochSecEndTime)};
  if (-1 == endIndex) {
    clean();
    m_logger->stream(Logger::Level::DEB) << "Sliced completely";
    return *this;
  }

  // whole interval in [epochSecStartTime, epochSecEndTime]
  if ((0 == startIndex) && ((m_times.size() - 1) == endIndex)) {
    m_logger->stream(Logger::Level::DEB) << "Sliced none, return whole interval";
    return *this;
  }

  // endIndex is inclusive
  QVector<double>          slicedTime(m_times.cbegin() + startIndex, m_times.cbegin() + endIndex + 1);
  QVector<QVector<double>> slicedStates;
  {
    for (QVector<double> const &state : m_states) {
      QVector<double> slicedState(state.cbegin() + startIndex, state.cbegin() + endIndex + 1);
      slicedStates << slicedState;
    }
  }

  m_logger->stream(Logger::Level::DEB) << "interval"
                                       << QDateTime::fromSecsSinceEpoch(m_intervalKey).toString("yyyy-MM-dd_HH-mm-ss");
  m_logger->stream(Logger::Level::DEB) << "indexes" << m_times.size() << "-->" << slicedTime.size();
  m_times.swap(slicedTime);
  m_states.swap(slicedStates);

  return *this;
}

void DataHolder::append(DataHolder const *other) {
  if (!other || other->m_times.empty()) {
    m_logger->stream(Logger::Level::WAR) << "Nothing to append";
    return;
  }
  quint64 const oldSize{static_cast<quint64>(m_times.size())};
  m_times.append(other->getTimes());

  auto      otherStates       = other->getStates();
  int const actualStatesCount = std::min(m_states.size(), otherStates.size());
  for (int i{0U}; i < actualStatesCount; ++i) {
    m_states[i].append(otherStates[i]);
  }

  if (actualStatesCount != m_states.size()) {
    m_logger->stream(Logger::Level::WAR) << "states count changed:" << m_states.size() << "-->" << actualStatesCount;
    m_states.resize(actualStatesCount);
    m_numberOfStates = actualStatesCount;
  }

  // update inteval's key if interval was empty
  if ((0 == oldSize) && (0 != m_times.size())) {
    m_intervalKey = utils::calculateKey(m_times[0]);
  }
  m_logger->stream(Logger::Level::INFO) << "size changed:" << oldSize << "-->" << m_times.size();
}

qint64 DataHolder::findFirstIndexAfterEqualTime(double epochSecTime) const {
  auto const iter = std::lower_bound(m_times.cbegin(), m_times.cend(), epochSecTime);
  if (m_times.cend() == iter) {
    return -1;
  }
  return std::distance(m_times.cbegin(), iter);
}

qint64 DataHolder::findLastIndexBeforeEqualTime(double epochSecTime) const {
  auto const iter = std::upper_bound(m_times.cbegin(), m_times.cend(), epochSecTime);
  if (m_times.cbegin() == iter) {
    return -1;
  }
  return std::distance(m_times.cbegin(), iter - 1);
}

bool deepvaccontrol::DataHolder::writeData(QFile &file) {
  QElapsedTimer timer;
  timer.start();

  // the day's Parquet file (recordingfile.h): one column per state, by name
  QString          error;
  QByteArray const bytes = recordingfile::write(m_times, m_states, &error);
  if (bytes.isEmpty()) {
    m_logger->stream(Logger::Level::ERR) << "Recording not written:" << error;
    return false;
  }
  if (file.write(bytes) != bytes.size()) {
    return false;
  }

  m_logger->stream(Logger::Level::DEB)
    << QString("Data of interval %1 saved in %2 msec")
         .arg(QDateTime::fromSecsSinceEpoch(m_intervalKey).toString("yyyy-MM-dd_HH-mm-ss"))
         .arg(timer.nsecsElapsed() * 1e-6, 0, 'f', 2);

  return true;
}

bool deepvaccontrol::DataHolder::readData(QFile &file) {
  QElapsedTimer timer;
  timer.start();

  clean();
  // a Parquet day (columns by name, in the order of the value list) or an older .dat day
  QString error;
  if (!recordingfile::readBytes(file.readAll(), m_times, m_states, {}, &error)) {
    m_logger->stream(Logger::Level::WAR) << "Recording" << file.fileName() << "not read:" << error;
    m_times.clear();
    m_states = QVector<QVector<double>>(m_numberOfStates);
    return false;
  }

  if (m_states.count() != m_numberOfStates) {
    m_logger->stream(Logger::Level::WAR)
      << QString("Loaded %1 states, but DataHolder assume %2").arg(m_states.count()).arg((m_numberOfStates));
    m_states.resize(m_states.count());
    m_numberOfStates = m_states.count();
  }

  if (!m_times.empty()) {
    m_intervalKey = utils::calculateKey(m_times[0]);
  }

  m_logger->stream(Logger::Level::DEB)
    << QString("Data of interval %1 loaded in %2 msec")
         .arg(QDateTime::fromSecsSinceEpoch(m_intervalKey).toString("yyyy-MM-dd_HH-mm-ss"))
         .arg(timer.nsecsElapsed() * 1e-6, 0, 'f', 2);

  emit dataChanged();
  return true;
}
}
