#ifndef FILELOADDIALOG_H
#define FILELOADDIALOG_H

#include <QDialog>
#include <QFileSystemModel>
#include <QListView>
#include <QSortFilterProxyModel>

namespace deepvaccontrol {
class FileLoadDialog : public QDialog {
  Q_OBJECT
public:
  explicit FileLoadDialog(QList<QString> pathList, QString const &filter, QWidget *parent = nullptr);
  ~FileLoadDialog() override = default;

  QString getFilePath() const;

private:
  void                   handleFileSelection(QModelIndex const &index);
  void                   handleFileRemoving(QModelIndex const &index);
  void                   selectPath(QString const &path);
  QFileSystemModel      *m_fileSysModel;
  QSortFilterProxyModel *m_sortedModel;
  QListView             *m_fileListView;
  QString                m_fileFilter;
  QString                m_resultFilePath;
};

inline QString FileLoadDialog::getFilePath() const {
  return m_resultFilePath;
}
} // namespace deepvaccontrol

#endif // FILELOADDIALOG_H
