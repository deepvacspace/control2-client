#include "mainwindow.h"

#include "assistantwidget.h"

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
  m_aiWidget         = new AiWidget(m_resHolder, this);
  // the assistant shows experiments on the chart page
  m_aiWidget->setServices(m_resHolder->analysis, [this] { selectWidgetWindow(m_stackedWidget->indexOf(m_graphWidget)); });
  // the chart page's AI panel: the assistant, about the experiment shown
  connect(m_graphWidget, &GraphWidget::assistantRequested, this, [this](QString const &run, QString const &question) {
    selectWidgetWindow(m_stackedWidget->indexOf(m_aiWidget));
    m_aiWidget->openAssistant(run, question);
  });
  // questions asked in the chart's AI panel are answered there, about what the chart shows
  connect(m_graphWidget, &GraphWidget::chartQuestionAsked, m_aiWidget->assistantWidget(), &assistant::AssistantWidget::askFromChart);
  connect(m_aiWidget->assistantWidget(), &assistant::AssistantWidget::chartExchange, m_graphWidget, &GraphWidget::showAssistantExchange);
  auto *userWidget   = new UserWidget(m_resHolder->logger, m_resHolder->users, this);
  auto *systemWidget = new SystemWidget(m_resHolder, this);

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
  addWidgetToStacked(m_aiWidget, tr("AI"), "ai");
  addWidgetToStacked(m_settingsWidget, tr("Settings"), "settings");
  addWidgetToStacked(userWidget, tr("Users"), "users");
  addWidgetToStacked(systemWidget, tr("System"), "system");
  // the data analysis page is merged into the chart page: no longer offered (its code is kept)
  m_menu->setItemHidden(m_stackedWidget->indexOf(m_dataAnalysisWidget));

  selectWidgetWindow(0);
  // the compact diagram on Home opens the full one
  connect(m_head, &Head::diagramRequested, this, [this, systemWidget] {
    selectWidgetWindow(m_stackedWidget->indexOf(systemWidget));
    systemWidget->showDiagram();
  });

  connect(m_menu, &Menu::itemClicked, this, [this](int indexSelected, int) {
    m_stackedWidget->setCurrentIndex(indexSelected);
  });

  // the header carries what the former status bar showed; the chart page hides it for a bigger chart
  m_headerBar = new HeaderBar(m_resHolder, this);
  mainLay->addWidget(m_headerBar);
  connect(m_stackedWidget, &QStackedWidget::currentChanged, this, [this, pagesLay, previous = m_stackedWidget->currentWidget()](int) mutable {
    QWidget *current = m_stackedWidget->currentWidget();
    // the chart page's side rail sits on the right edge of the window
    int const gap{static_cast<int>(10 * utils::ssf())};
    pagesLay->setContentsMargins(gap, static_cast<int>(8 * utils::ssf()), (current == m_graphWidget) ? 0 : gap, static_cast<int>(8 * utils::ssf()));
    if ((current == m_graphWidget) && (previous != m_graphWidget)) {
      m_headerWasCollapsed = m_headerBar->isCollapsed();
      m_headerBar->setCollapsed(true);
    } else if ((previous == m_graphWidget) && (current != m_graphWidget)) {
      m_headerBar->setCollapsed(m_headerWasCollapsed);
    }
    previous = current;
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
  lightBtn->setVisible(lightBtn->isConfigured()); // only for a chamber with a light

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
  // "Главная"
  // "Ручное\nуправление"
  // "Программное\nуправление"
  // "График"
  // "Data\nanalysis"
  // "AI"
  // "Настройки"
  // "Пользователи"
  // "Система"
  // "Свет"
  // "Выкл"
  switch (newLevel) {
    case AccessManager::AccessLevel::USER_START_1:
    case AccessManager::AccessLevel::USER_EDIT_2:
      m_menu->setElementsEnable({1, 1, 1, 1, 1, 1, 0, 1, 1});
      break;
    case AccessManager::AccessLevel::USER_ADMIN_3:
      m_menu->setElementsEnableAll();
      break;
    case AccessManager::AccessLevel::SPECTATOR_0:
    default:
      m_menu->setElementsEnable({1, 0, 0, 1, 1, 1, 0, 1, 1});
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

} // namespace deepvaccontrol
