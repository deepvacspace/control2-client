#ifndef PROGRAMMODEL_H
#define PROGRAMMODEL_H

#include <QStandardItemModel>

#include "filehandler.h"
namespace deepvaccontrol {
/*
Program model should be represented as json to save/load
{
  "cycle_param": {
    "cycle_end": 0,
    "cycle_start": 0,
    "cycles_number": 0
  },
  "intervals": [
    {
      "duration": 3600,
      "flags": 3,
      "temp": 60
      "hum": 60
    },
    {
      control_params...
    }
  ]
}
*/

/**
 * @brief Contains table (rows and colons) of QStandardItems
 *
 *
 */
class ProgramModel
    : public QObject
    , public FileHandler {
  Q_OBJECT
public:
  enum class ProgType : quint8 {
    SETPOINT = 1U,
    PROG     = 2U
  };
  explicit ProgramModel(
    QVariantList const &paramListVariant, ProgType progType = ProgType::SETPOINT, QObject *parent = nullptr);
  ~ProgramModel() {
  }

  QVariantMap         intervalToVariantMap(int index) const;
  bool                setIntervalFromVariantMap(int index, QVariantMap const &paramVariant);
  QStringList         getParamListOrder() const;   // used by TableDelegate
  QVariantList        getParamListVariant() const; // used by TableDelegate
  QStandardItemModel *getStandardModel();
  void                setCycleParams(QVariantMap const &cycleParamMap);
  QVariantMap         getCycleParams() const;
  int                 getFlagsIndex() const;
  void                selectInterval(int index);
  int                 getSelectedInterval() const;
  void                newItem(int index); // new empty interval on given index
  void                delItem(int index); // remove interval on given index
  void                emitModelUpdated(); // inform model client, that m_standardModel changed
  void                setParamValue(quint32 intervalIndex, QString const &paramName, double value); // for manual
  void                printToDebug() const;
  ProgType            getProgType() const;
  void                resetCycle();
  QByteArray          makeSendData() const;

signals:
  void numberOfIntervalChanged(qint16);
  void cycleParamsUpdated(QVariantMap);
  void intervalSelected(int);
  void modelChanged();

private:
  QStandardItemModel m_standardModel;
  QVariantList       m_paramListVariant;
  QStringList        m_paramListOrder;
  QList<int>         m_paramListTypes;
  int                m_selectedInterval;

  quint32 m_cycle_start;
  quint32 m_cycle_end;
  quint32 m_cycle_count;

  ProgType m_progType;

  // FileHandler interface
protected:
  bool writeData(QFile &file) override;
  bool readData(QFile &file) override;
};

inline QStringList ProgramModel::getParamListOrder() const {
  return m_paramListOrder;
}

inline QVariantList ProgramModel::getParamListVariant() const {
  return m_paramListVariant;
}

inline QStandardItemModel *ProgramModel::getStandardModel() {
  return &m_standardModel;
}

inline void ProgramModel::setCycleParams(QVariantMap const &cycleParamMap) {
  m_cycle_start = cycleParamMap["start"].toUInt();
  m_cycle_end   = cycleParamMap["end"].toUInt();
  m_cycle_count = cycleParamMap["count"].toUInt();
}

inline QVariantMap ProgramModel::getCycleParams() const {
  QVariantMap ret;
  ret["start"] = m_cycle_start;
  ret["end"]   = m_cycle_end;
  ret["count"] = m_cycle_count;
  return ret;
}

inline int ProgramModel::getFlagsIndex() const {
  return m_paramListOrder.indexOf("flags");
}

inline int ProgramModel::getSelectedInterval() const {
  return m_selectedInterval;
}

inline void ProgramModel::emitModelUpdated() {
  emit modelChanged();
}

inline ProgramModel::ProgType ProgramModel::getProgType() const {
  return m_progType;
}
} // namespace deepvaccontrol

#endif // PROGRAMMODEL_H
