#include "programwidget.h"

#include <QHBoxLayout>
#include <QLabel>

#include "constants.h"
#include "delayinfodialog.h"
#include "delayselectdialog.h"
#include "fileloaddialogpathprovider.h"
#include "filesavedialogpathprovider.h"
#include "intervaleditdialog.h"
#include "utils.h"

namespace deepvaccontrol {
ProgramWidget::ProgramWidget(ResHolder *resHolder, QWidget *parent)
    : QWidget{parent}
    , m_resHolder{resHolder}
    , m_model{nullptr}
    , m_accessLevel{resHolder->users->getCurrentAccessLevel()} {
  initGui();
  setProgramModel(resHolder->defaultProgram);
  updateGui();

  connect(m_editBtn, &QPushButton::clicked, this, &ProgramWidget::openIntervalEditDialog);
  connect(m_newBtn, &QPushButton::clicked, this, [this]() {
    if (m_model) {
      int const nexIndex{m_model->getSelectedInterval() + 1};
      m_model->newItem(nexIndex);
      m_model->selectInterval(nexIndex);
    }
  });

  connect(m_deleteBtn, &QPushButton::clicked, this, [this]() {
    if (m_model) {
      if (1 == m_model->getStandardModel()->columnCount()) {
        return;
      }
      int index{m_model->getSelectedInterval()};
      m_model->delItem(index);
    }
  });

  connect(m_saveBtn, &QPushButton::clicked, this, [this]() {
    if (m_model) {
      auto sourceList = QStringList{deepvaccontrol::PROGS_DIR};
      sourceList.append(m_resHolder->usb->getAvailableDrives());
      auto *savePathProvider = new FileSaveDialogPathProvider(sourceList, m_resHolder->logger, this);
      if (m_model) {
        m_model->saveToFileAsync(savePathProvider, this);
      }
    }
  });

  connect(m_loadBtn, &QPushButton::clicked, this, [this]() {
    auto sourceList = QStringList{deepvaccontrol::PROGS_DIR};
    sourceList.append(m_resHolder->usb->getAvailableDrives());
    auto *loadPath = new FileLoadDialogPathProvider(sourceList, m_resHolder->logger, this);
    if (m_model) {
      m_model->loadFromFileAsync(loadPath, this, "*");
    }
  });

  connect(m_startBtn, &QPushButton::clicked, this, [this]() {
    if (m_model) {
      sendProgram();
    }
  });

  connect(m_startDelayedBtn, &QPushButton::clicked, this, [this] {
    if (m_model) {
      auto *delaySelectDialog = new DelaySelectDialog(this);
      connect(delaySelectDialog, &DelaySelectDialog::startTimeSelected, this, [this](QDateTime startTime) {
        auto *delayInfoDialog = new DelayInfoDialog(tr("Работа по программе"), startTime, this);
        connect(delayInfoDialog, &QDialog::accepted, this, &ProgramWidget::sendProgram);
        delayInfoDialog->open();
      });
      delaySelectDialog->open();
    }
  });

  connect(m_tableWidget, &ProgramTableWidget::doubleClicked, this, &ProgramWidget::openIntervalEditDialog);
  connect(m_programGraph, &ProgramGraph::intervalDoubleClicked, this, &ProgramWidget::openIntervalEditDialog);

  connect(m_resHolder->hard_status, &HardwareStatus::statusChanged, this, &ProgramWidget::updateGui);
  connect(m_resHolder->users, &AccessManager::userSelected, this, &ProgramWidget::handleAccessChange);
}

void ProgramWidget::setProgramModel(ProgramModel *newProgramModel) {
  if (newProgramModel == m_model) {
    return;
  }

  if (m_model) {
    disconnect(m_model, nullptr, nullptr, nullptr);
  }

  m_model = newProgramModel;
  m_tableWidget->setModel(newProgramModel);
  m_programGraph->setModel(newProgramModel);
  m_cycleWidget->setParams(m_model->getCycleParams());
  m_cycleWidget->setIntervalsCount(m_model->getStandardModel()->columnCount());

  connect(m_model, &ProgramModel::numberOfIntervalChanged, this, &ProgramWidget::updateGui);
  connect(m_model, &ProgramModel::cycleParamsUpdated, m_cycleWidget, &CycleWidget::setParams);
  connect(m_cycleWidget, &CycleWidget::paramsChanged, m_model, &ProgramModel::setCycleParams);
}

void ProgramWidget::openIntervalEditDialog() {
  auto *editDialog = new IntervalEditDialog(m_model, this);
  editDialog->open();
}

void ProgramWidget::initGui() {
  auto *mainLay = new QHBoxLayout(this);
  mainLay->setContentsMargins(0, 0, 0, 0);
  mainLay->setSpacing(3 * utils::ssf());

  m_programGraph = new ProgramGraph(this);
  m_tableWidget  = new ProgramTableWidget(this);
  m_cycleWidget  = new CycleWidget(this);

  auto *rightLay = new QVBoxLayout();
  rightLay->addWidget(m_programGraph);
  rightLay->addWidget(m_tableWidget);

  auto *intervalButtonsLay = new QHBoxLayout();
  rightLay->addLayout(intervalButtonsLay);

  rightLay->setStretch(0, 10);

  m_startBtn = new QPushButton(tr("Пуск"), this);
  m_startBtn->setFont(QFont("MS Shell Dlg 2", 12 * utils::ssf(), 75));
  m_startDelayedBtn = new QPushButton(tr("Отлож.\nпуск"), this);
  m_startDelayedBtn->setFont(QFont("MS Shell Dlg 2", 12 * utils::ssf(), 75));

  m_saveBtn = new QPushButton(this);
  m_loadBtn = new QPushButton(this);

  m_saveBtn->setIcon(QIcon(":/imgs/save.png"));
  m_loadBtn->setIcon(QIcon(":/imgs/open.png"));

  m_saveBtn->setIconSize(QSize(30 * utils::ssf(), 30 * utils::ssf()));
  m_loadBtn->setIconSize(QSize(30 * utils::ssf(), 30 * utils::ssf()));

  m_newBtn    = new QPushButton(tr("Новый"), this);
  m_editBtn   = new QPushButton(tr("Редактировать"), this);
  m_deleteBtn = new QPushButton(tr("Удалить"), this);

  m_buttons << m_startBtn << m_newBtn << m_editBtn << m_deleteBtn << m_startDelayedBtn;
  for (auto &btn : m_buttons) {
    btn->setFixedSize(100 * utils::ssf(), 50 * utils::ssf());
  }
  for (auto &btn : {m_saveBtn, m_loadBtn}) {
    btn->setFixedSize(45 * utils::ssf(), 45 * utils::ssf());
  }

  auto *btnsLay = new QVBoxLayout();
  mainLay->addLayout(btnsLay);

  auto *saveLoadLay = new QHBoxLayout();
  saveLoadLay->addWidget(m_saveBtn);
  saveLoadLay->addWidget(m_loadBtn);

  auto label1 = new QLabel(tr("Программа"), this);
  auto label2 = new QLabel(tr("Цикл"), this);
  {
    QFont font1 = label1->font();
    font1.setBold(true);
    for (QLabel *item : {label1, label2}) {
      item->setFont(font1);
    }
  }

  mainLay->addLayout(rightLay);

  btnsLay->addWidget(m_startBtn);
  btnsLay->addWidget(m_startDelayedBtn);
  btnsLay->addSpacing(30 * utils::ssf());
  btnsLay->addWidget(label2);
  btnsLay->addWidget(m_cycleWidget);
  btnsLay->addStretch();
  btnsLay->addWidget(label1);
  btnsLay->addLayout(saveLoadLay);

  intervalButtonsLay->addWidget(m_newBtn);
  intervalButtonsLay->addWidget(m_editBtn);
  intervalButtonsLay->addWidget(m_deleteBtn);
  intervalButtonsLay->addStretch();

  for (QLabel *item : {label1, label2}) {
    btnsLay->setAlignment(item, Qt::AlignCenter);
  }
}

void ProgramWidget::handleAccessChange(QString const &name, AccessManager::AccessLevel newLevel) {
  if (newLevel == m_accessLevel) {
    return;
  }

  m_accessLevel = newLevel;
  updateGui();
}

void ProgramWidget::updateGui() {
  int const intervalCount{m_model->getStandardModel()->columnCount()};

  HardwareStatus::HardwareStatusEnum const hardWorkStatus{m_resHolder->hard_status->getHardwareStatus()};

  bool const enableStart{(HardwareStatus::HardwareStatus::WORK != hardWorkStatus)
                         && (HardwareStatus::HardwareStatus::NONE != hardWorkStatus) && (intervalCount > 0)};
  m_startBtn->setEnabled(enableStart);
  m_startDelayedBtn->setEnabled(enableStart);

  m_cycleWidget->setIntervalsCount(intervalCount);

  m_saveBtn->setEnabled(m_accessLevel >= AccessManager::AccessLevel::USER_EDIT_2);
  m_newBtn->setEnabled(m_accessLevel >= AccessManager::AccessLevel::USER_EDIT_2);
  m_editBtn->setEnabled(m_accessLevel >= AccessManager::AccessLevel::USER_EDIT_2);
  m_deleteBtn->setEnabled((m_accessLevel >= AccessManager::AccessLevel::USER_EDIT_2) && (intervalCount > 1));
  m_cycleWidget->setEnabled((m_accessLevel >= AccessManager::AccessLevel::USER_EDIT_2) && (intervalCount > 1));
}

void ProgramWidget::sendProgram() {
  if (m_resHolder->aiAdvisorEnabled) {
    utils::showAiAdvisorStartPromptAndRun(this);
  }

  m_model->printToDebug();
  m_resHolder->tcpGate->sendGeneral(GateTcp::OutcomeMessageCode::JOB, m_model->makeSendData());
  QString const actionMsg{
    QString(tr("Запуск работы по программе. %1 интервал(а,ов).")).arg(m_model->getStandardModel()->columnCount())};
  m_resHolder->actions->addAction(ActionManager::ActionType::PLC, actionMsg);
}
} // namespace deepvaccontrol
