#ifndef DATAEXPORTER_H
#define DATAEXPORTER_H

#include <QObject>

#include "dataholder.h"
#include "filehandler.h"

namespace control2 {
class DataExporter
    : public QObject
    , public FileHandler {
  Q_OBJECT
public:
  explicit DataExporter(DataHolder        *dataHolder,
                        QString const     &fileExtension,
                        QStringList const &stateLabels,
                        QVector<int>       enabelsList,
                        QObject           *parent = nullptr);

private:
  DataHolder  *m_dataHolder;
  QString      m_fileExtension;
  QStringList  m_stateLabels;
  QVector<int> m_enabelsList;
  // FileHandler interface
protected:
  bool writeData(QFile &file) override;
  bool readData(QFile &file) override;
};

inline bool DataExporter::readData(QFile &file) {
  return true;
}
} // namespace control2

#endif // DATAEXPORTER_H
