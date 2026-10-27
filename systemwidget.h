#ifndef SYSTEMWIDGET_H
#define SYSTEMWIDGET_H

#include <QTabWidget>
#include <QWidget>

#include "resholder.h"

namespace deepvaccontrol {

class ChamberView;

class SysCmdWidget : public QWidget {
  Q_OBJECT
public:
  explicit SysCmdWidget(ResHolder *resHolder, QWidget *parent = nullptr);
};

class SysOsWidget : public QWidget {
  Q_OBJECT
public:
  explicit SysOsWidget(ResHolder *resHolder, QWidget *parent = nullptr);

protected:
  void changeEvent(QEvent *event) override;

signals:
  void prepareToCloseApp();
  void switchAppMaximizedClicked();
  void appearanceChanged();
};

class SystemWidget : public QWidget {
  Q_OBJECT
public:
  explicit SystemWidget(ResHolder *resHolder, QWidget *parent = nullptr);
  // the Diagram tab's drawing: what the chamber has fitted (the Light button follows it)
  [[nodiscard]] ChamberView *chamberView() const {
    return m_chamberView;
  }

signals:
  void prepareToCloseApp();
  void switchAppMaximizedClicked();

private:
  void        handleAccessChange(QString const &, AccessManager::AccessLevel newLevel);
  QTabWidget *m_tabWidget;
  QWidget    *m_diagramTab{nullptr};
  ChamberView *m_chamberView{nullptr};
  QWidget    *m_osTab{nullptr};
  QWidget    *m_outputsTab{nullptr};
  QWidget    *m_commandsTab{nullptr};
};
}

#endif // SYSTEMWIDGET_H
