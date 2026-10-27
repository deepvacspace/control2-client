#include "mainwindow.h"
#include "chamberview.h"

#include "assistantsession.h"

#include "analysisservices.h"

#include <QPushButton>
#include <QStackedWidget>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

#include "constants.h"
#include "backupservice.h"
#include "collabmanager.h"
#include "dialogs/messagedialog.h"
#include "experimentrecorder.h"
#include "headerbar.h"
#include "lightbutton.h"
#include "powerbutton.h"
#include "statusbar.h"
#include "systemwidget.h"
#include "touchsupport.h"
#include "userwidget.h"
#include "utils.h"

namespace deepvaccontrol {
MainWindow::MainWindow(ResHolder *resHolder, bool windowMode, QWidget *parent)
    : QMainWindow(parent)
    , m_resHolder{resHolder}
    , m_windowMode{windowMode}
    , m_menu{new Menu(deepvaccontrol::UI_MENU_BTN_HEIGHT)} {
  initGui();

  connect(m_resHolder->tcpGate, &GateTcp::userMsgReceived, this, &MainWindow::displayMsgFromGate);
}

void MainWindow::initGui() {
  auto *mainWidget = new QWidget(this);
  setCentralWidget(mainWidget);
  // an explicit minimum: hiding / showing the header or menu re-lays the window out, and the pages' combined
  // minimum must not resize a full-screen window beyond the screen
  mainWidget->setMinimumSize(1, 1);

  // stackedWidgetMenuLay / status bar
  auto *mainLay = new QVBoxLayout(mainWidget);
  mainLay->setContentsMargins(2 * utils::ssf(), 2 * utils::ssf(), 2 * utils::ssf(), 2 * utils::ssf());
  mainLay->setSpacing(0);

  // header / pages / footer menu
  mainLay->setContentsMargins(0, 0, 0, 0);
  auto *pagesLay = new QHBoxLayout();
  pagesLay->setContentsMargins(10 * utils::ssf(), 8 * utils::ssf(), 10 * utils::ssf(), 8 * utils::ssf());
  m_stackedWidget = new QStackedWidget(this);
  pagesLay->addWidget(m_stackedWidget);

  // saves every finished chamber job to the experiment database (Deepvac Insight format)
  m_resHolder->recorder = new ExperimentRecorder(m_resHolder, this);
  // local-network sharing with other Deepvac apps (settings: Connections tab)
  m_resHolder->collab = new CollabManager(m_resHolder, this);
  // experiment library, notes and reports, shared by the chart and data analysis pages and the assistant
  m_resHolder->analysis = new AnalysisServices(m_resHolder, this);

  m_head             = new Head(m_resHolder, this);
  m_manual           = new Manual(m_resHolder, this);
  m_programWidget    = new ProgramWidget(m_resHolder, this);
  m_settingsWidget   = new SettingsWidget(m_resHolder, this);
  m_graphWidget      = new GraphWidget(m_resHolder, this);
  m_dataAnalysisWidget = new DataAnalysisWidget(m_resHolder, this);
  // the assistant of the chart page's Assistant panel, about what the chart shows; it can show experiments there
  auto *session = new assistant::AssistantSession(m_resHolder, this);
  if (m_resHolder->analysis) {
    session->setServices(m_resHolder->analysis, [this] { selectWidgetWindow(m_stackedWidget->indexOf(m_graphWidget)); });
  }
  connect(m_graphWidget, &GraphWidget::chartQuestionAsked, session, &assistant::AssistantSession::ask);
  connect(m_graphWidget, &GraphWidget::assistantStopRequested, session, &assistant::AssistantSession::stop);
  connect(m_graphWidget, &GraphWidget::assistantNewChatRequested, session, &assistant::AssistantSession::newChat);
  connect(m_graphWidget, &GraphWidget::assistantWanted, session, &assistant::AssistantSession::startModel);
  connect(session, &assistant::AssistantSession::exchange, m_graphWidget, &GraphWidget::showAssistantExchange);
  connect(session, &assistant::AssistantSession::checkingAnswer, m_graphWidget, &GraphWidget::showAssistantChecking);
  auto showModel = [this](assistant::LlmSidecar::State state, QString const &message) {
    using State = assistant::LlmSidecar::State;
    m_graphWidget->showAssistantModel(state == State::READY      ? QStringLiteral("ready")
                                      : state == State::STARTING ? QStringLiteral("loading")
                                      : state == State::FAILED   ? QStringLiteral("failed")
                                                                 : QStringLiteral("off"),
                                      message);
  };
  connect(session, &assistant::AssistantSession::modelStateChanged, m_graphWidget, showModel);
  showModel(session->modelState(), session->modelMessage());
  auto *userWidget   = new UserWidget(m_resHolder->logger, m_resHolder->users, this);
  auto *systemWidget = new SystemWidget(m_resHolder, this);
  // a finger dragging their lists, tables and pages scrolls them
  for (QWidget *page : {static_cast<QWidget *>(m_settingsWidget), static_cast<QWidget *>(systemWidget), static_cast<QWidget *>(userWidget)}) {
    touch::enableScrollingWithin(page);
  }

  // accessSelectWidget            = new AccessSelectWidget2(&accessManager, this);

  auto addWidgetToStacked = [this](QWidget *widget, QString const &label, QString const &icon) {
    m_stackedWidget->addWidget(widget);
    m_menu->addItem(label, icon);
  };

  addWidgetToStacked(m_head, tr("Home"), "home");
  addWidgetToStacked(m_manual, tr("Manual\ncontrol"), "manual");
  addWidgetToStacked(m_programWidget, tr("Program\ncontrol"), "program");
  addWidgetToStacked(m_graphWidget, tr("Chart"), "chart");
  addWidgetToStacked(m_dataAnalysisWidget, tr("Data\nanalysis"), "analysis");
  addWidgetToStacked(userWidget, tr("Users"), "users");
  addWidgetToStacked(m_settingsWidget, tr("Settings"), "settings");
  addWidgetToStacked(systemWidget, tr("System"), "system");
  // the data analysis page is merged into the chart page: no longer offered (its code is kept)
  m_menu->setItemHidden(m_stackedWidget->indexOf(m_dataAnalysisWidget));

  selectWidgetWindow(0);
  connect(m_menu, &Menu::itemClicked, this, [this](int indexSelected, int) {
    m_stackedWidget->setCurrentIndex(indexSelected);
  });

  // the header carries what the former status bar showed; it and the menu are always shown
  m_headerBar = new HeaderBar(m_resHolder, this);
  mainLay->addWidget(m_headerBar);
  connect(m_stackedWidget, &QStackedWidget::currentChanged, this, [this, pagesLay](int) {
    QWidget *current = m_stackedWidget->currentWidget();
    // the side rails of the chart and program pages sit on the right edge of the window
    int const  gap{static_cast<int>(10 * utils::ssf())};
    bool const rail{(current == m_graphWidget) || (current == m_programWidget)};
    pagesLay->setContentsMargins(gap, static_cast<int>(8 * utils::ssf()), rail ? 0 : gap, static_cast<int>(8 * utils::ssf()));
  });
  mainLay->addLayout(pagesLay, 1);
  mainLay->addWidget(m_menu);

  connect(m_resHolder->hard_status,
          &HardwareStatus::statusChanged,
          this,
          [this](HardwareStatus::HardwareStatusEnum newStatus, HardwareStatus::HardwareStatusEnum oldStatus) {
            if (1 != m_stackedWidget->currentIndex()) {
              selectWidgetWindow(0);
            }
          });

  connect(m_resHolder->users, &AccessManager::userSelected, this, &MainWindow::handleAccessChange);
  handleAccessChange(QString(), m_resHolder->users->getCurrentAccessLevel());

  // connect(statusBar, &StatusBar::mouseReleased, this, &MainWindow::displayLogViewer);

  auto *lightBtn = new LightButton(m_resHolder, this);
  m_menu->addWidget(lightBtn);
  // only for a chamber with a light: fitted (the PLC's report, a Service Engineer's choice or the settings) and an
  // output to switch it
  ChamberView *diagram     = systemWidget->chamberView();
  auto         updateLight = [lightBtn, diagram] {
    lightBtn->setVisible(lightBtn->isConfigured() && diagram && diagram->isFitted(QStringLiteral("light")));
  };
  updateLight();
  connect(diagram, &ChamberView::refreshed, lightBtn, updateLight);

  auto *powerBtn = new PowerButton(m_resHolder, this);
  m_menu->addWidget(powerBtn);

  if (m_windowMode) {
    powerBtn->setVisible(false);
  }

  connect(systemWidget, &SystemWidget::switchAppMaximizedClicked, this, [this, powerBtn] {
    if (isFullScreen()) {
      showMaximized();
      powerBtn->setVisible(!m_windowMode);
    } else {
      showFullScreen();
      powerBtn->setVisible(true);
    }
  });

  connect(systemWidget, &SystemWidget::prepareToCloseApp, this, [this]() {
    emit aboutClosed();
    close();
  });

  connect(powerBtn, &PowerButton::prepareToCloseApp, this, [this]() {
    emit aboutClosed();
    close();
  });

  // connect(&usbDevManager, &UsbDeviceManager::changed, statusBar, &StatusBar::setUsb);

  m_flasher = new Flasher(this);

  // database backups (data/backups) every N days (Settings > Application), checked shortly after start and then hourly
  auto runBackups = [this] {
    int const days{std::max(1, m_resHolder->settings->settingsMap().value("app_settings").toMap().value("backup_days", 1).toInt())};
    QThread  *thread = QThread::create([days] {
      storage::BackupService::backupAll(false, nullptr, days);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start(QThread::LowestPriority);
  };
  QTimer::singleShot(10'000, this, runBackups);
  auto *backupTimer = new QTimer(this);
  connect(backupTimer, &QTimer::timeout, this, runBackups);
  backupTimer->start(3'600'000);

  // titling
  auto const tcp_server_address = m_resHolder->settings->settingsMap()["tcp_server"].toMap()["server_ip"].toString();
  QString const title = tcp_server_address.isEmpty() ? QStringLiteral("Deepvac Control")
                                                     : QStringLiteral("Deepvac Control (%1)").arg(tcp_server_address);
  setWindowTitle(title);
}

void MainWindow::handleAccessChange(QString const &, AccessManager::AccessLevel newLevel) {
  // one flag per menu page, in menu order: Home, Manual control, Program control, Chart, Data analysis (hidden),
  // Users, Settings, System
  switch (newLevel) {
    case AccessManager::AccessLevel::USER_START_1:
    case AccessManager::AccessLevel::USER_EDIT_2:
      m_menu->setElementsEnable({1, 1, 1, 1, 1, 1, 0, 1});
      break;
    case AccessManager::AccessLevel::USER_ADMIN_3:
      m_menu->setElementsEnableAll();
      break;
    case AccessManager::AccessLevel::SPECTATOR_0:
    default:
      m_menu->setElementsEnable({1, 0, 0, 1, 1, 1, 0, 1});
      break;
  }
}

void MainWindow::setConnectionErrorFlash(bool enable) {
  m_flasher->setFlashing(enable);
}

void MainWindow::selectWidgetWindow(int index) {
  m_stackedWidget->setCurrentIndex(index);
  m_menu->selectItem(index);
}

void MainWindow::displayMsgFromGate(QString const &msg) {
  auto *msgDlg = new MessageDialog(tr("Warning"), msg);
  msgDlg->open();
}

// void MainWindow::resizeEvent(QResizeEvent *event) {
//   QMainWindow::resizeEvent(event);

//   setWindowTitle(QString("%1x%2").arg(event->size().width()).arg(event->size().height()));
// }

void MainWindow::closeEvent(QCloseEvent *event) {
  emit aboutClosed();

  // Proceed with the regular close event
  event->accept();
}

}
