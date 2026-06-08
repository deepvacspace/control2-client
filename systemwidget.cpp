#include "systemwidget.h"

#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPalette>
#include <QPushButton>
#include <QVBoxLayout>

#include "actionwidget.h"
#include "confdialog.h"
#include "constants.h"
#include "forceoutlistwidget.h"
#include "syslistwidget.h"
namespace control2 {
namespace {
char const SYSTEM_TAB_PAGE_OBJECT_NAME[]{"systemTabPage"};

QString systemTabStyleSheet() {
  QString styleSheet = utils::readStyleFromFile(":/styles/tabBar.qss")
                         .arg(40 * utils::ssf())
                         .arg(100 * utils::ssf())
                         .arg(100 * utils::ssf());

  QPalette const palette  = QApplication::palette();
  QColor const   tabColor = palette.color(QPalette::Button);
  if (tabColor.lightness() < 128) {
    styleSheet += QString("\n"
                          "QTabWidget::pane {"
                          "background-color: %1;"
                          "}\n"
                          "QWidget#%2 {"
                          "background-color: %1;"
                          "color: white;"
                          "}\n"
                          "QWidget#%2 QLabel {"
                          "color: white;"
                          "}\n"
                          "QWidget#%2 QCheckBox {"
                          "color: white;"
                          "}")
                    .arg(tabColor.name(), SYSTEM_TAB_PAGE_OBJECT_NAME);
  }

  return styleSheet;
}

void prepareSystemTabPage(QWidget *widget) {
  widget->setObjectName(SYSTEM_TAB_PAGE_OBJECT_NAME);
}
} // namespace

SysCmdWidget::SysCmdWidget(ResHolder *resHolder, QWidget *parent) : QWidget{parent} {
  auto *mainLay = new QVBoxLayout(this);

  auto *lineBtnLay = new QHBoxLayout();
  mainLay->addLayout(lineBtnLay);

  auto *cmdLine = new QLineEdit("finish", this);
  cmdLine->setFixedWidth(200 * utils::ssf());
  auto *sendCmd = new QPushButton(tr("Отправить"), this);
  sendCmd->setFixedSize(100 * utils::ssf(), control2::UI_BTN_HEIGHT * utils::ssf());

  lineBtnLay->addWidget(cmdLine);
  lineBtnLay->addWidget(sendCmd);
  lineBtnLay->addStretch();

  auto *cmdBtnsLay = new QHBoxLayout();
  mainLay->addLayout(cmdBtnsLay);

  auto addBtn = [cmdLine, cmdBtnsLay, this](QString const &cmd) {
    auto *cmdBtn = new QPushButton(cmd, this);
    cmdBtn->setFixedSize(80 * utils::ssf(), control2::UI_BTN_HEIGHT * utils::ssf());
    cmdBtnsLay->addWidget(cmdBtn);

    connect(cmdBtn, &QPushButton::clicked, this, [cmd, cmdLine]() {
      cmdLine->setText(cmd);
    });
  };

  addBtn("next");
  addBtn("get_status");
  addBtn("get_states");
  addBtn("get_settings");
  addBtn("delete_settings");
  addBtn("stop");
  addBtn("confrim_alarm");

  cmdBtnsLay->addStretch();
  mainLay->addStretch();

  connect(sendCmd, &QPushButton::clicked, this, [cmdLine, resHolder]() {
    resHolder->tcpGate->sendTextCmd(cmdLine->text());
  });
};

SysOsWidget::SysOsWidget(ResHolder *resHolder, QWidget *parent) : QWidget{parent} {
  auto *mainLay = new QVBoxLayout(this);

  auto *exitBtn = new QPushButton(tr("Выйти в ОС"), this);
  exitBtn->setIcon(QIcon(":/imgs/os.png"));
  exitBtn->setIconSize(
    QSize((control2::UI_BTN_HEIGHT - 5) * utils::ssf(), (control2::UI_BTN_HEIGHT - 5) * utils::ssf()));
  exitBtn->setStyleSheet(QString("QPushButton { text-align: left; padding-left: %1px}").arg(5 * utils::ssf()));
  exitBtn->setFixedSize(340 * utils::ssf(), control2::UI_BTN_HEIGHT * utils::ssf());

  auto *windowBtn = new QPushButton(tr("На весь экран / окно"), this);
  windowBtn->setIcon(QIcon(":/imgs/fullscreen.png"));
  windowBtn->setIconSize(
    QSize((control2::UI_BTN_HEIGHT - 5) * utils::ssf(), (control2::UI_BTN_HEIGHT - 5) * utils::ssf()));
  windowBtn->setStyleSheet(QString("QPushButton { text-align: left; padding-left: %1px}").arg(5 * utils::ssf()));
  windowBtn->setFixedSize(340 * utils::ssf(), control2::UI_BTN_HEIGHT * utils::ssf());

  {
    auto font = exitBtn->font();
    font.setPointSize(font.pointSize() * 1.5);
    font.setBold(true);
    exitBtn->setFont(font);
    windowBtn->setFont(font);
  }

  mainLay->addWidget(exitBtn);
  mainLay->addWidget(windowBtn);
  mainLay->addStretch();

  connect(exitBtn, &QPushButton::clicked, this, [this, resHolder] {
    auto *confDialog = new ConfDialog(tr("Подтверждение"),
                                      tr("Вы действительно хотите выйти в ОС?\nРаботы камеры НЕ будет остановлена."),
                                      tr("Да"),
                                      tr("Нет"),
                                      this);
    connect(confDialog, &ConfDialog::finished, this, [&](int result) {
      if (1 == result) { // == accepted()
        emit prepareToCloseApp();
      }
    });
    confDialog->open();
  });

  connect(windowBtn, &QPushButton::clicked, this, &SysOsWidget::switchAppMaximizedClicked);
};

SystemWidget::SystemWidget(ResHolder *resHolder, QWidget *parent) : QWidget{parent} {
  auto *mainLay = new QVBoxLayout(this);
  mainLay->setContentsMargins(0, 0, 0, 0);

  m_tabWidget = new QTabWidget(this);
  m_tabWidget->setStyleSheet(systemTabStyleSheet());
  mainLay->addWidget(m_tabWidget);

  // 0 - список
  auto *sysListWidget = new SysListWidget(resHolder, this);
  prepareSystemTabPage(sysListWidget);
  m_tabWidget->addTab(sysListWidget, tr("Состояния"));

  // 1 - журнал
  auto *actionWidget = new ActionWidget(resHolder->actions, this);
  prepareSystemTabPage(actionWidget);
  m_tabWidget->addTab(actionWidget, tr("Журнал"));

  // 2 - ос
  auto *osWidget = new SysOsWidget(resHolder, this);
  prepareSystemTabPage(osWidget);
  m_tabWidget->addTab(osWidget, "ОС");

  // 3 - ручный выходы
  auto *forceOutWidget = new ForceOutListWidget(
    resHolder, resHolder->states->getSysValue("outputs"), resHolder->settings->settingsMap()["monitor"].toList(), this);
  prepareSystemTabPage(forceOutWidget);
  m_tabWidget->addTab(forceOutWidget, tr("Ручное управление выходами"));

  // 4 - ПЛК
  auto *cmdWidget = new SysCmdWidget(resHolder, this);
  prepareSystemTabPage(cmdWidget);
  m_tabWidget->addTab(cmdWidget, tr("Команды ПЛК"));

  auto *aiWidget = new QWidget(this);
  prepareSystemTabPage(aiWidget);
  auto *aiLayout = new QHBoxLayout(aiWidget);
  auto *aiAdvisorToggle = new QCheckBox("AI Advisor", aiWidget);
  aiAdvisorToggle->setChecked(true);
  resHolder->aiAdvisorEnabled = aiAdvisorToggle->isChecked();
  connect(aiAdvisorToggle, &QCheckBox::toggled, this, [resHolder](bool checked) {
    resHolder->aiAdvisorEnabled = checked;
  });
  aiLayout->addWidget(aiAdvisorToggle);
  aiLayout->addStretch();
  m_tabWidget->addTab(aiWidget, "AI");

  connect(resHolder->users, &AccessManager::userSelected, this, &SystemWidget::handleAccessChange);
  handleAccessChange(QString(), resHolder->users->getCurrentAccessLevel());

  connect(osWidget, &SysOsWidget::switchAppMaximizedClicked, this, &SystemWidget::switchAppMaximizedClicked);
  connect(osWidget, &SysOsWidget::prepareToCloseApp, this, &SystemWidget::prepareToCloseApp);
}

void SystemWidget::handleAccessChange(QString const &, AccessManager::AccessLevel newLevel) {
  bool const enableTabUser{newLevel >= AccessManager::AccessLevel::SPECTATOR_0};
  bool const enableTabAll{newLevel == AccessManager::AccessLevel::USER_ADMIN_3};

  m_tabWidget->setTabVisible(2, enableTabUser);
  m_tabWidget->setTabVisible(3, enableTabAll);
  m_tabWidget->setTabVisible(4, enableTabAll);
}
} // namespace control2
