#ifndef DATAMANAGER_H
#define DATAMANAGER_H

#include <QObject>
#include <QTimer>
#include <QVector>

#include "dataholder.h"

namespace deepvaccontrol {

class ResHolder;

/**
 * @brief Somehow manages an input data points, store it in a memory and on a disk.
 * May creats objects like intervals to split data.
 * Sould be used as end-point to operate with user data.
 *
 * Idea is to use dataHolders.
 * Every dataHolder contains vectors of data points, manages and storage them in memoey and on disk in files.
 */
class DataManager : public QObject {
  Q_OBJECT
public:
  explicit DataManager(
    ResHolder *resHolder, quint16 numberOfStates, quint32 intervalDuration = 3600, QObject *parent = nullptr);
  ~DataManager();

  bool addPoint(double epochSecTime, QVector<double> const &pointVector);
  bool prepareIntervalToDisplay(double epochSecTime);

  std::shared_ptr<DataHolder> getIntervalToDisplay() const;
signals:

  // interval created
  // if the first point of the day added to interval it created and loaded (but no file)
  // otherwise, interval created and old data loaded from file.
  void intervalToAddRecreated(double);

private:
  // DataHolder под указателем соотствеует настоящему моменту времени
  std::shared_ptr<DataHolder> m_intervalToAdd;

  // Может указывать на ту же память, что и m_intervalToAdd, а может и нет
  std::shared_ptr<DataHolder> m_intervalToDisplay;

  // Максимальная длина интервала (секунды)
  quint32 m_intervalDuration;

  uint16_t m_numberOfStates{0U};

  ResHolder *m_resHolder;
};

inline std::shared_ptr<DataHolder> DataManager::getIntervalToDisplay() const {
  return m_intervalToDisplay;
}
} // namespace deepvaccontrol

#endif
