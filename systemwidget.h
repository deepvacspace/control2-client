#ifndef SYSTEMWIDGET_H
#define SYSTEMWIDGET_H

#include <QTabWidget>
#include <QWidget>

#include "resholder.h"

namespace control2 {

class SysCmdWidget : public QWidget {
  Q_OBJECT
public:
  explicit SysCmdWidget(ResHolder *resHolder, QWidget *parent = nullptr);
};

class SysOsWidget : public QWidget {
  Q_OBJECT
public:
  explicit SysOsWidget(ResHolder *resHolder, QWidget *parent = nullptr);

signals:
  void prepareToCloseApp();
  void switchAppMaximizedClicked();
  void appearanceChanged();
};

class SystemWidget : public QWidget {
  Q_OBJECT
public:
  explicit SystemWidget(ResHolder *resHolder, QWidget *parent = nullptr);

signals:
  void prepareToCloseApp();
  void switchAppMaximizedClicked();

private:
  void        handleAccessChange(QString const &, AccessManager::AccessLevel newLevel);
  QTabWidget *m_tabWidget;
};
} // namespace control2

#endif // SYSTEMWIDGET_H
