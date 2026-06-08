#ifndef EXPORTDIALOG_H
#define EXPORTDIALOG_H

#include <QDateTime>
#include <QDialog>
#include <QLabel>

#include "qcustomplot.h"
#include "resholder.h"

namespace control2 {
class ExportDialog : public QDialog {
  Q_OBJECT
public:
  explicit ExportDialog(ResHolder *resHolder, std::shared_ptr<DataHolder> m_interval, QWidget *parent = nullptr);
  ~ExportDialog() override {
    qDebug() << "~ExportDialog()";
  }
  QString selectedFileExtension() const;
  QString selectedFilePath() const;
  int     selectedType() const;

private:
  void updateDates(QDateTime rangeStart, QDateTime rangeEnd);
  void compileCustomInterval();

  ResHolder                  *m_resHolder;
  std::shared_ptr<DataHolder> m_interval;

  QLabel   *m_datesLabel{nullptr};
  QDateTime m_dateStart;
  QDateTime m_dateEnd;

  QString m_selectedFileExtension;
  QString m_selectedFilePath;
  int     m_selectedType;
};

inline QString ExportDialog::selectedFileExtension() const {
  return m_selectedFileExtension;
}

inline QString ExportDialog::selectedFilePath() const {
  return m_selectedFilePath;
}

inline int ExportDialog::selectedType() const {
  return m_selectedType;
}
} // namespace control2

#endif // EXPORTDIALOG_H
