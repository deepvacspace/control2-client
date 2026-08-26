#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

#include "aiwidget.h"
#include "flasher.h"
#include "graphwidget.h"
#include "head.h"
#include "manual.h"
#include "menu.h"
#include "programwidget.h"
#include "resholder.h"
#include "settingswidget.h"

namespace control2 {
class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  MainWindow(ResHolder *resHolder, bool windowMode, QWidget *parent = nullptr);
  ~MainWindow() = default;
  void setConnectionErrorFlash(bool enable);

signals:
  void aboutClosed();

private:
  void            initGui();
  void            handleAccessChange(QString const &, AccessManager::AccessLevel newLevel);
  void            selectWidgetWindow(int index);
  void            displayMsgFromGate(QString const &msg);
  ResHolder      *m_resHolder;
  QStackedWidget *m_stackedWidget;
  Menu           *m_menu;
  Head           *m_head;
  Manual         *m_manual;
  ProgramWidget  *m_programWidget;
  SettingsWidget *m_settingsWidget;
  GraphWidget    *m_graphWidget;
  AiWidget       *m_aiWidget;
  Flasher        *m_flasher;
  bool            m_windowMode;

  // QWidget interface
protected:
  // void resizeEvent(QResizeEvent *event) override;
  void closeEvent(QCloseEvent *event) override;
};
} // namespace control2

#endif // MAINWINDOW_H
