#include "mainwindow.h"

#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "constants.h"
#include "dialogs/messagedialog.h"
#include "lightbutton.h"
#include "powerbutton.h"
#include "statusbar.h"
#include "systemwidget.h"
#include "userwidget.h"
#include "utils.h"

namespace control2 {
MainWindow::MainWindow(ResHolder *resHolder, bool windowMode, QWidget *parent)
    : QMainWindow(parent)
    , m_resHolder{resHolder}
    , m_windowMode{windowMode}
    , m_menu{new Menu(control2::UI_MENU_BTN_HEIGHT)} {
  initGui();

  connect(m_resHolder->tcpGate, &GateTcp::userMsgReceived, this, &MainWindow::displayMsgFromGate);
}

void MainWindow::initGui() {
  auto *mainWidget = new QWidget(this);
  setCentralWidget(mainWidget);

  // stackedWidgetMenuLay / status bar
  auto *mainLay = new QVBoxLayout(mainWidget);
  mainLay->setContentsMargins(2 * utils::ssf(), 2 * utils::ssf(), 2 * utils::ssf(), 2 * utils::ssf());
  mainLay->setSpacing(0);

  // stackedWidget | menu
  auto *stackedWidgetMenuLay = new QHBoxLayout();
  stackedWidgetMenuLay->setContentsMargins(2 * utils::ssf(), 2 * utils::ssf(), 2 * utils::ssf(), 2 * utils::ssf());
  stackedWidgetMenuLay->setSpacing(2 * utils::ssf());
  mainLay->addLayout(stackedWidgetMenuLay);

  m_stackedWidget = new QStackedWidget(this);

  stackedWidgetMenuLay->addWidget(m_stackedWidget);
  stackedWidgetMenuLay->addWidget(m_menu);

  m_head             = new Head(m_resHolder, this);
  m_manual           = new Manual(m_resHolder, this);
  m_programWidget    = new ProgramWidget(m_resHolder, this);
  m_settingsWidget   = new SettingsWidget(m_resHolder, this);
  m_graphWidget      = new GraphWidget(m_resHolder, this);
  m_aiWidget         = new AiWidget(m_resHolder, this);
  auto *userWidget   = new UserWidget(m_resHolder->logger, m_resHolder->users, this);
  auto *systemWidget = new SystemWidget(m_resHolder, this);

  // accessSelectWidget            = new AccessSelectWidget2(&accessManager, this);

  auto addWidgetToStacked = [this](QWidget *widget, QString const &label) {
    m_stackedWidget->addWidget(widget);
    m_menu->addItem(label);
  };

  addWidgetToStacked(m_head, tr("Главная"));
  addWidgetToStacked(m_manual, tr("Ручное\nуправление"));
  addWidgetToStacked(m_programWidget, tr("Программное\nуправление"));
  addWidgetToStacked(m_graphWidget, tr("График"));
  addWidgetToStacked(m_aiWidget, tr("AI"));
  addWidgetToStacked(m_settingsWidget, tr("Настройки"));
  addWidgetToStacked(userWidget, tr("Пользователи"));
  addWidgetToStacked(systemWidget, tr("Система"));

  selectWidgetWindow(0);

  connect(m_menu, &Menu::itemClicked, this, [this](int indexSelected, int) {
    m_stackedWidget->setCurrentIndex(indexSelected);
  });

  auto *statusBar = new StatusBar(m_resHolder, this);
  mainLay->addWidget(statusBar);

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

  auto *lightBtn = new LightButton(m_resHolder->tcpGate, m_resHolder->states->getSysValue("outputs"), this);
  m_menu->addWidget(lightBtn);

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

  // titling
  QString    eq_name{};
  auto       plc_settings_map   = m_resHolder->settings->settingsMap()["app_settings"].toMap();
  auto const tcp_server_address = m_resHolder->settings->settingsMap()["tcp_server"].toMap()["server_ip"].toString();
  if (plc_settings_map.contains("name")) {
    eq_name = plc_settings_map["name"].toString();
  }
  QString title = eq_name.isEmpty() ? tcp_server_address : QString("%1 (%2)").arg(eq_name).arg(tcp_server_address);
  setWindowTitle(title);
}

void MainWindow::handleAccessChange(QString const &, AccessManager::AccessLevel newLevel) {
  // "Главная"
  // "Ручное\nуправление"
  // "Программное\nуправление"
  // "График"
  // "AI"
  // "Настройки"
  // "Пользователи"
  // "Система"
  // "Свет"
  // "Выкл"
  switch (newLevel) {
    case AccessManager::AccessLevel::USER_START_1:
    case AccessManager::AccessLevel::USER_EDIT_2:
      m_menu->setElementsEnable({1, 1, 1, 1, 1, 0, 1, 1});
      break;
    case AccessManager::AccessLevel::USER_ADMIN_3:
      m_menu->setElementsEnableAll();
      break;
    case AccessManager::AccessLevel::SPECTATOR_0:
    default:
      m_menu->setElementsEnable({1, 0, 0, 1, 1, 0, 1, 1});
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
  auto *msgDlg = new MessageDialog(tr("Внимание"), msg);
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

} // namespace control2
