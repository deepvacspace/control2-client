#ifndef DRIVEPATHSELECT_H
#define DRIVEPATHSELECT_H

#include <QWidget>

namespace control2 {
class DrivePathSelect : public QWidget {
  Q_OBJECT
public:
  explicit DrivePathSelect(QList<QString> pathList, QWidget *parent = nullptr);
  QString getSelectedPath() const;
signals:
  void selectedPathChanged(QString);

private:
  QList<QString> m_pathList;
  QString        m_selectedPath;
};

inline QString DrivePathSelect::getSelectedPath() const {
  return m_selectedPath;
}
} // namespace control2

#endif // DRIVEPATHSELECT_H
