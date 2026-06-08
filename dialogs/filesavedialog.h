#ifndef FILESAVEDIALOG_H
#define FILESAVEDIALOG_H

#include <QDialog>

#include "drivepathselect.h"
#include "lineeditscreen.h"

namespace control2 {
class FileSaveDialog : public QDialog {
  Q_OBJECT
public:
  explicit FileSaveDialog(QList<QString> pathList, QWidget *parent = nullptr);
  ~FileSaveDialog() = default;

  QString getFilePath() const;

private:
  LineEditScreen  *fileNameEdit;
  DrivePathSelect *m_drivePathSelect;
  QPushButton     *m_acceptBtn;
  QPushButton     *m_rejectBtn;
  QString          m_resultFilePath;
};

inline QString FileSaveDialog::getFilePath() const {
  return m_resultFilePath;
}
} // namespace control2

#endif // FILESAVEDIALOG_H
