#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

#include "dataanalysiswidget.h"
#include "flasher.h"
#include "graphwidget.h"
#include "menu.h"
#include "programwidget.h"
#include "resholder.h"
#include "settingswidget.h"

namespace deepvaccontrol {
namespace assistant {
class AssistantSession;
}

class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  MainWindow(ResHolder *resHolder, bool windowMode, QWidget *parent = nullptr);
  ~MainWindow() = default;
  void setConnectionErrorFlash(bool enable);
  // every screen (Analysis with its panels and tools, Control's steps, each tab of Settings and System)
  // into dir as PNG and SVG (charts as vectors: for a design tool such as Figma); screensExported() when done
  void exportScreens(QString const &dir);
  // --demo-shot: Analysis' Live tab with the status beside the chart, saved as PNG at scale times the window's size
  // (the charts' buffers at that resolution meanwhile), then screensExported()
  void saveLiveShot(QString const &path, double scale);
  // --assistant-check: the assistant asked a fixed list of questions, its answers with their times and lengths written
  // into path (assistant::AssistantCheck), then screensExported()
  void runAssistantCheck(QString const &path);

signals:
  void aboutClosed();
  void screensExported();

private:
  void            initGui();
  void            handleAccessChange(QString const &, AccessManager::AccessLevel newLevel);
  void            selectWidgetWindow(int index);
  void            displayMsgFromGate(QString const &msg);
  ResHolder      *m_resHolder;
  QStackedWidget *m_stackedWidget;
  Menu           *m_menu;
  ProgramWidget  *m_programWidget;
  SettingsWidget *m_settingsWidget;
  GraphWidget        *m_graphWidget;
  DataAnalysisWidget *m_dataAnalysisWidget;
  Flasher        *m_flasher;
  assistant::AssistantSession *m_assistant{nullptr}; // of the Analysis page's Assistant panel
  bool            m_windowMode;

  // QWidget interface
protected:
  // void resizeEvent(QResizeEvent *event) override;
  void closeEvent(QCloseEvent *event) override;
};
}

#endif // MAINWINDOW_H
