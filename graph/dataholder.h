#ifndef DATAHOLDER_H
#define DATAHOLDER_H

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVector>

#include "filehandler.h"
#include "logger.h"

namespace control2 {

class DataExporter;

/**
 * @brief Manages concrete interval data storage in memory and on disk.
 */
class DataHolder
    : public QObject
    , public FileHandler {
  Q_OBJECT
public:
  DataHolder(quint16  numberOfStates,
             Logger  *logger,
             double   intervalKey       = 0.0,
             quint64  maxLength         = static_cast<quint64>(-1),
             quint32  autosaveSecPeriod = 5 * 60,
             QObject *parent            = nullptr);
  ~DataHolder() override;

  // Clean all datas and set zero size.
  void clean();

  // Add data point to interval.
  // No need to checks range as soon as DataManager controls it.
  bool addPoint(double epochSecTime, QVector<double> const &pointVector);

  // Add vector of nan values as point to make terminator (empty space on graph)
  void addNaN();

  // Get index in arrays at given timestamp of first after it.
  qint64 findFirstIndexAfterEqualTime(double epochSecTime) const;

  qint64 findLastIndexBeforeEqualTime(double epochSecTime) const;

  // Get values of the states at time.
  // Precond: point with given timestamp exists!
  QVector<double> getValueAtTime(double epochSecTime) const;

  // Get point values vector at given timestamp of first after it or last.
  QPair<double, QVector<double>> getValueNearTime(double epochSecTime) const;

  [[nodiscard]] QVector<double>          getTimes() const;
  [[nodiscard]] QVector<QVector<double>> getStates() const;

  [[nodiscard]] quint16 getNumberOfStates() const;
  [[nodiscard]] double  getIntervalKey() const;

  bool isDirty() const noexcept;

  DataHolder &slice(double epochSecStartTime, double epochSecEndTime);
  void        append(DataHolder const *other);

signals:
  void dataChanged();

private:
  bool            inRange(double epochSecTime) const;
  QVector<double> getValueAtIndex(quint64 index) const;
  void            autoSave();

  quint16 m_numberOfStates{0U};
  Logger *m_logger;
  double  m_intervalKey;
  quint64 m_maxLength;
  quint32 m_autosaveSecPeriod;
  QTimer *m_autosaveTimer;

  QVector<double>          m_times;
  QVector<QVector<double>> m_states;

  bool m_dirty;

  // FileHandler interface
protected:
  bool writeData(QFile &file) override;
  bool readData(QFile &file) override;

  friend control2::DataExporter;
};

inline QVector<double> DataHolder::getTimes() const {
  return m_times;
}

inline QVector<QVector<double>> DataHolder::getStates() const {
  return m_states;
}

inline quint16 DataHolder::getNumberOfStates() const {
  return m_numberOfStates;
}

inline double DataHolder::getIntervalKey() const {
  return m_intervalKey;
}

inline bool DataHolder::isDirty() const noexcept {
  return m_dirty;
}

inline bool DataHolder::inRange(double epochSecTime) const {
  return (m_intervalKey <= epochSecTime) && ((epochSecTime - m_intervalKey) < m_maxLength);
}
} // namespace control2

#endif
