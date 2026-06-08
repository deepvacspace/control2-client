#include "programmodel.h"

#include <winsock.h>

#include "constants.h"
#include "utils.h"

namespace control2 {
ProgramModel::ProgramModel(QVariantList const &paramListVariant, ProgType progType, QObject *parent)
    : QObject{parent}
    , FileHandler{}
    , m_paramListVariant(paramListVariant)
    , m_selectedInterval{0U}
    , m_cycle_start{0U}
    , m_cycle_end{0U}
    , m_cycle_count{0U}
    , m_progType{progType} {
  m_paramListOrder.clear();
  // get param order list and header labels
  QStringList headerLabels;
  for (auto const &paramMap : paramListVariant) {
    m_paramListOrder.push_back(paramMap.toMap()["name"].toString());
    m_paramListTypes.push_back(paramMap.toMap()["type"].toInt());
    headerLabels.push_back(paramMap.toMap()["label"].toString());
  }

  m_standardModel.setVerticalHeaderLabels(headerLabels);
  m_standardModel.setRowCount(headerLabels.size());
}

QVariantMap ProgramModel::intervalToVariantMap(int index) const {
  QVariantMap out;

  for (int i{0U}; i < m_paramListOrder.size(); ++i) {
    auto const &paramName = m_paramListOrder[i];
    int const   paramType{m_paramListTypes[i]};
    float const paramValue{m_standardModel.item(i, index)->data(paramType).toFloat()};
    out[paramName] = paramValue;
  }

  return out;
}

bool ProgramModel::setIntervalFromVariantMap(int index, QVariantMap const &paramVariant) {
  QList<QStandardItem *> columnToAdd;

  for (int i{0U}; i < m_paramListOrder.size(); ++i) {
    QString const &paramName = m_paramListOrder[i];
    float          paramValue{paramVariant[paramName].toFloat()};
    auto           paramSettingsMap = m_paramListVariant[i].toMap();
    if (paramSettingsMap.contains("min")) {
      float const paramMin{paramSettingsMap["min"].toFloat()};
      float const paramMax{paramSettingsMap["max"].toFloat()};
      paramValue = utils::limitValue(paramMin, paramValue, paramMax);
    }

    int const paramType{paramSettingsMap["type"].toInt()};

    auto *standardItem = new QStandardItem();
    standardItem->setData(paramValue, paramType);
    columnToAdd.append(standardItem);
  }

  m_standardModel.insertColumn(index, columnToAdd);
  emit numberOfIntervalChanged(m_standardModel.columnCount());
  return true;
}

void ProgramModel::selectInterval(int index) {
  if ((index < 0) || (index >= m_standardModel.columnCount())) {
    return;
  }
  if (index == m_selectedInterval) {
    return;
  }

  m_selectedInterval = index;
  emit intervalSelected(m_selectedInterval);
}

void ProgramModel::newItem(int index) {
  if (index > 0) { // clone interval-column
    QList<QStandardItem *> clonedIntervalColumn;
    for (int rowIndex{0U}; rowIndex < m_standardModel.rowCount(); ++rowIndex) {
      clonedIntervalColumn.push_back(m_standardModel.item(rowIndex, index - 1)->clone());
    }
    m_standardModel.insertColumn(index, clonedIntervalColumn);
  }

  resetCycle();
  emit numberOfIntervalChanged(m_standardModel.columnCount());
}

void ProgramModel::delItem(int index) {
  int const selectedBeforeRemove{m_selectedInterval};
  m_standardModel.removeColumn(index);

  resetCycle();
  emit numberOfIntervalChanged(m_standardModel.columnCount());

  // update selected index
  if (index == m_selectedInterval) {
    int indexToSelect = (0 == selectedBeforeRemove) ? 0 : selectedBeforeRemove - 1;

    // select without self checks
    m_selectedInterval = indexToSelect;
    emit intervalSelected(m_selectedInterval);
  }
}

void ProgramModel::setParamValue(quint32 intervalIndex, QString const &paramName, double value) {
  if (static_cast<int>(intervalIndex) >= m_standardModel.columnCount()) {
    return;
  }

  int const paramIndex{static_cast<int>(m_paramListOrder.indexOf(paramName))};
  int const paramType{m_paramListTypes[paramIndex]};

  m_standardModel.item(paramIndex, intervalIndex)->setData(value, paramType);
}

void ProgramModel::printToDebug() const {
  constexpr static int const colWidth{11};
  constexpr static int const colHeaderWidth{15};

  QDebug debug = qDebug();
  debug.noquote();

  debug << "vvv PROG vvv \n";
  debug << "Program type:" << (ProgType::SETPOINT == m_progType ? "MANUAL" : "PROG") << "\n";
  debug << "Cycle count:" << m_cycle_count << "\n";
  debug << "Cycle start:" << m_cycle_start << "\n";
  debug << "Cycle end:" << m_cycle_end << "\n";
  debug << "\n";
  debug << "Intervals count:" << m_standardModel.columnCount() << "\n";

  QString indexLine = QString("%1").arg("interval index:", colHeaderWidth);
  for (int col{0U}; col < m_standardModel.columnCount(); ++col) {
    indexLine += QString("%1 |").arg(col + 1, colWidth - 1);
  }
  debug << indexLine << "\n";

  for (int i{0U}; i < m_paramListOrder.size(); ++i) {
    QString paramLine = QString("%1").arg(m_paramListOrder[i], colHeaderWidth);
    for (int col{0U}; col < m_standardModel.columnCount(); ++col) {
      QString displayValue;
      if ((Qt::UserRole) == m_paramListTypes[i]) {
        displayValue = utils::timeFromSec(m_standardModel.item(i, col)->data(Qt::UserRole).toUInt());
      } else if ((Qt::UserRole + 1) == m_paramListTypes[i]) {
        quint32 const flagsValue = m_standardModel.item(i, col)->data(Qt::UserRole + 1).toUInt();
        displayValue += (utils::getBitOfDword(flagsValue, control2::AWAIT_BIT) ? "A+" : "");
        displayValue += (utils::getBitOfDword(flagsValue, control2::RAMP_BIT) ? "R+" : "");
        displayValue
          += utils::binaryString(static_cast<char const *>(static_cast<void const *>(&flagsValue)), 1, false);
        // displayValue += QString::number(flagsValue);
      } else {
        float const floatValue{m_standardModel.item(i, col)->data(0).toFloat()};
        displayValue += QString::number(floatValue, 'f', 1);
      }
      paramLine += QString("%1|").arg(displayValue, colWidth);
    }
    debug << paramLine << "\n";
  }
  debug << "^^^ PROG ^^^";
}

void ProgramModel::resetCycle() {
  m_cycle_start = 0U;
  m_cycle_end   = 0U;
  m_cycle_count = 0U;
  emit cycleParamsUpdated(getCycleParams());
}

QByteArray ProgramModel::makeSendData() const {
  // тип_8 начальный_16 цикл_16 старт_16 конец_16 число_интервалов_16 [длительность_32 темп_32 влаг_32 ... флаги_32]
  QByteArray data;

  QDataStream dataStream(&data, QIODeviceBase::WriteOnly);
  dataStream.setByteOrder(QDataStream::ByteOrder::LittleEndian);
  dataStream.setFloatingPointPrecision(QDataStream::FloatingPointPrecision::SinglePrecision);

  dataStream << static_cast<quint8>(m_progType);

  dataStream << static_cast<quint16>(0);

  dataStream << static_cast<quint16>(m_cycle_count);
  dataStream << static_cast<quint16>(m_cycle_start);
  dataStream << static_cast<quint16>(m_cycle_end);

  quint16 const intervalsCount{static_cast<quint16>(m_standardModel.columnCount())};

  dataStream << static_cast<quint16>(intervalsCount);

  // iterate every interval
  for (quint16 interval{0U}; interval < intervalsCount; ++interval) {
    // every param
    for (int par{0U}; par < m_paramListOrder.size(); ++par) {
      int const paramType{m_paramListTypes[par]};
      switch (paramType) {
        case Qt::UserRole: // duration
          dataStream << static_cast<quint32>(m_standardModel.item(par, interval)->data(paramType).toUInt());
          break;
        case Qt::UserRole + 1: // flags
          dataStream << static_cast<quint32>(m_standardModel.item(par, interval)->data(paramType).toUInt());
          break;
        default:
          dataStream << static_cast<float>(m_standardModel.item(par, interval)->data(paramType).toFloat());
      }
    }
  }

  return data;
}

bool ProgramModel::writeData(QFile &file) {
  QVariantList intervals;
  if (m_standardModel.columnCount() < 1) {
    return false;
  }
  for (int i{0U}; i < m_standardModel.columnCount(); ++i) {
    intervals << intervalToVariantMap(i);
  }

  QVariantMap cycle;
  cycle["start"] = m_cycle_start;
  cycle["end"]   = m_cycle_end;
  cycle["count"] = m_cycle_count;

  QVariantMap outMap;
  outMap["intervals"] = intervals;
  outMap["cycle"]     = cycle;

  file.write(QJsonDocument::fromVariant(outMap).toJson());
  return true;
}

bool ProgramModel::readData(QFile &file) {
  QVariantMap loadedProgramVariant = utils::parseJsonToVariant(file.readAll(), file.fileName());

  // Integrity checks
  if (loadedProgramVariant.empty()) {
    return false;
  }

  bool const cycleOk{loadedProgramVariant.contains("cycle") && loadedProgramVariant["cycle"].toMap().contains("start")
                     && loadedProgramVariant["cycle"].toMap().contains("end")
                     && loadedProgramVariant["cycle"].toMap().contains("count")};

  QVariantMap fileParamMap = loadedProgramVariant["intervals"].toList()[0].toMap();
  if (fileParamMap.empty()) {
    return false;
  }

  {
    QStringList fileParamList{fileParamMap.keys()};
    QStringList settingsParamList{m_paramListOrder};
    fileParamList.sort();
    settingsParamList.sort();
    if (fileParamList != settingsParamList) {
      qWarning() << "file interval params: " << fileParamMap.keys();
      qWarning() << "settings interval params: " << settingsParamList;
      return false;
    }
  }

  // Applying values from file

  if (cycleOk) {
    m_cycle_start = loadedProgramVariant["cycle"].toMap()["start"].toUInt();
    m_cycle_end   = loadedProgramVariant["cycle"].toMap()["end"].toUInt();
    m_cycle_count = loadedProgramVariant["cycle"].toMap()["count"].toUInt();
  } else {
    m_cycle_start = 0U;
    m_cycle_end   = 0U;
    m_cycle_count = 0U;
  }

  // clear everything except header items
  m_standardModel.removeColumns(0U, m_standardModel.columnCount());

  QVariantList const intervalListVariant = loadedProgramVariant["intervals"].toList();
  bool               result{false};
  for (int i{0U}; i < intervalListVariant.size(); ++i) {
    auto const fileIntervalMap = intervalListVariant[i].toMap();
    result                     = setIntervalFromVariantMap(i, fileIntervalMap);
    if (!result) {
      qWarning() << "Error on adding" << i << "interval from" << file.fileName();
      break;
    }
  }

  emit intervalSelected(0);
  // Because the settings are read asynchronously, it is necessary to transmit the cycle parameters asynchronously via
  // the signal as well.
  emit cycleParamsUpdated(getCycleParams());

  return result;
}
} // namespace control2
