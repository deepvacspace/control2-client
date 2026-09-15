#include "dataexporter.h"

#include <QDateTime>
#include <QTextStream>

namespace deepvaccontrol {
DataExporter::DataExporter(DataHolder        *dataHolder,
                           QString const     &fileExtension,
                           QStringList const &stateLabels,
                           QVector<int>       enabelsList,
                           QObject           *parent)
    : QObject{parent}
    , m_dataHolder{dataHolder}
    , FileHandler{}
    , m_fileExtension{fileExtension}
    , m_stateLabels{stateLabels}
    , m_enabelsList{enabelsList} {
}

bool DataExporter::writeData(QFile &file) {
  QTextStream out(&file);
  out.setRealNumberNotation(QTextStream::FixedNotation);
  out.setRealNumberPrecision(2);

  QMap<QString, QString> const sep{
    {"txt", "\t"},
    {"csv",  ";"}
  };
  QString const sepSymbol{sep.value(m_fileExtension)};

  // time lamel & param labels
  out << QString(tr("Дата/время")) << sepSymbol;
  for (auto const &label : m_stateLabels) {
    out << label;
    if (label != m_stateLabels.last()) {
      out << sepSymbol;
    }
  }
  out << "\r\n";
  QLocale const locale{QLocale::German};
  quint64 const totalTimes = m_dataHolder->m_times.size();
  for (quint64 i{0U}; i < totalTimes; ++i) {
    out << QDateTime::fromSecsSinceEpoch(m_dataHolder->m_times[i]).toString("dd.MM.yy HH:mm:ss") << sepSymbol;
    for (auto const &stateIndex : m_enabelsList) {
      out << locale.toString(m_dataHolder->m_states[stateIndex][i], 'f', 2);
      if (stateIndex != m_enabelsList.last()) {
        out << sepSymbol;
      }
    }
    out << "\r\n";
  }

  return true;
}
} // namespace deepvaccontrol
