#include "programwidget.h"

#include <QHBoxLayout>
#include <QLabel>

#include "constants.h"
#include "delayinfodialog.h"
#include "delayselectdialog.h"
#include "dialogs/messagedialog.h"
#include "experimentrecorder.h"
#include "rundetailsdialog.h"
#include "fileloaddialogpathprovider.h"
#include "filesavedialogpathprovider.h"
#include "intervaleditdialog.h"
#include "offlineadvisorstart.h"
#include "programlibrarydialog.h"
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
      setLibraryProgram(-1, QString{}, {});
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

  connect(m_libraryBtn, &QPushButton::clicked, this, &ProgramWidget::openLibrary);
  connect(m_saveLibraryBtn, &QPushButton::clicked, this, &ProgramWidget::saveToLibrary);

  connect(m_resHolder->hard_status, &HardwareStatus::statusChanged, this, &ProgramWidget::updateGui);
  connect(m_resHolder->hard_status, &HardwareStatus::statusChanged, this, &ProgramWidget::updateRunStatus);
  connect(m_resHolder->states, &SystemValueList::ValuesUpdated, this, &ProgramWidget::updateRunStatus);
  connect(m_resHolder->users, &AccessManager::userSelected, this, &ProgramWidget::handleAccessChange);
  updateProgramLabel();
  updateRunStatus();
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
  // edits after opening a library program mark it as modified
  auto markModified = [this] {
    if (!m_loadingLibrary && !m_libraryName.isEmpty() && !m_libraryModified) {
      m_libraryModified = true;
      updateProgramLabel();
    }
  };
  connect(m_model, &ProgramModel::numberOfIntervalChanged, this, markModified);
  connect(m_model, &ProgramModel::cycleParamsUpdated, this, markModified);
  connect(m_model, &ProgramModel::modelChanged, this, markModified);
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

  m_libraryBtn     = new QPushButton(tr("Library…"), this);
  m_saveLibraryBtn = new QPushButton(tr("Save to\nlibrary…"), this);
  m_libraryBtn->setToolTip(tr("Open a saved program (also those shared by paired computers)"));
  m_saveLibraryBtn->setToolTip(tr("Save this program by name, with a label per interval"));

  m_programLabel = new QLabel(this);
  m_programLabel->setWordWrap(true);
  m_programLabel->setAlignment(Qt::AlignCenter);
  m_programLabel->setFixedWidth(100 * utils::ssf());
  m_runStatus = new QLabel(this);
  m_runStatus->setWordWrap(true);
  m_runStatus->setAlignment(Qt::AlignCenter);
  m_runStatus->setFixedWidth(100 * utils::ssf());

  m_buttons << m_startBtn << m_newBtn << m_editBtn << m_deleteBtn << m_startDelayedBtn << m_libraryBtn << m_saveLibraryBtn;
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
  btnsLay->addWidget(new RunDetailsButton(m_resHolder, this));
  btnsLay->addWidget(m_runStatus);
  btnsLay->addSpacing(30 * utils::ssf());
  btnsLay->addWidget(label2);
  btnsLay->addWidget(m_cycleWidget);
  btnsLay->addStretch();
  btnsLay->addWidget(label1);
  btnsLay->addWidget(m_programLabel);
  btnsLay->addWidget(m_libraryBtn);
  btnsLay->addWidget(m_saveLibraryBtn);
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
  m_saveLibraryBtn->setEnabled((m_accessLevel >= AccessManager::AccessLevel::USER_EDIT_2) && (intervalCount > 0));
  m_newBtn->setEnabled(m_accessLevel >= AccessManager::AccessLevel::USER_EDIT_2);
  m_editBtn->setEnabled(m_accessLevel >= AccessManager::AccessLevel::USER_EDIT_2);
  m_deleteBtn->setEnabled((m_accessLevel >= AccessManager::AccessLevel::USER_EDIT_2) && (intervalCount > 1));
  m_cycleWidget->setEnabled((m_accessLevel >= AccessManager::AccessLevel::USER_EDIT_2) && (intervalCount > 1));
}

void ProgramWidget::sendProgram() {
  if (m_resHolder->aiAdvisorEnabled && !offerOfflineAdvisorBeforeStart(m_resHolder, m_model, this)) {
    return;
  }

  m_model->printToDebug();
  m_runningName   = m_libraryName.isEmpty() ? QString{} : (m_libraryModified ? tr("%1 (modified)").arg(m_libraryName) : m_libraryName);
  m_runningLabels = m_labels;
  if (m_resHolder->recorder) {
    m_resHolder->recorder->setNextJob(ExperimentRecorder::JobInfo{
      QStringLiteral("program"), m_runningName.isEmpty() ? tr("Program") : tr("Program %1").arg(m_runningName), m_runningName,
      m_model->programVariant(), QJsonObject{{"labels", QJsonArray::fromStringList(m_runningLabels)}}});
  }
  m_resHolder->tcpGate->sendGeneral(GateTcp::OutcomeMessageCode::JOB, m_model->makeSendData());
  QString const actionMsg{
    QString(tr("Запуск работы по программе. %1 интервал(а,ов).")).arg(m_model->getStandardModel()->columnCount())};
  m_resHolder->actions->addAction(ActionManager::ActionType::PLC,
                                  m_runningName.isEmpty() ? actionMsg : QStringLiteral("%1 (%2)").arg(actionMsg, m_runningName));
  updateRunStatus();
}

void ProgramWidget::openLibrary() {
  auto *dialog = new ProgramLibraryDialog(this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  connect(dialog, &QDialog::accepted, this, [this, dialog] {
    auto const profile = dialog->selected();
    if (!profile || !m_model) {
      return;
    }
    QString error;
    m_loadingLibrary = true;
    bool const ok    = programlibrary::loadIntoModel(*profile, *m_model, &error);
    m_loadingLibrary = false;
    if (!ok) {
      (new MessageDialog(tr("Program library"), error))->open();
      return;
    }
    QStringList labels;
    for (auto const &step : profile->steps) {
      labels << step.label;
    }
    setLibraryProgram(profile->id, profile->name, labels);
    m_model->selectInterval(0);
  });
  dialog->open();
}

void ProgramWidget::saveToLibrary() {
  if (!m_model || (m_model->getStandardModel()->columnCount() < 1)) {
    return;
  }
  auto *dialog = new ProgramSaveDialog(*m_model, m_libraryName, m_labels, m_resHolder->users->getCurrentUserName(), this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  connect(dialog, &QDialog::accepted, this, [this, dialog] {
    if (auto const profile = dialog->saved()) {
      QStringList labels;
      for (auto const &step : profile->steps) {
        labels << step.label;
      }
      setLibraryProgram(profile->id, profile->name, labels);
    }
  });
  dialog->open();
}

void ProgramWidget::setLibraryProgram(qint64 id, QString const &name, QStringList const &labels) {
  m_libraryId       = id;
  m_libraryName     = name;
  m_labels          = labels;
  m_libraryModified = false;
  updateProgramLabel();
}

void ProgramWidget::updateProgramLabel() {
  m_programLabel->setText(m_libraryName.isEmpty() ? QString{}
                                                  : (m_libraryModified ? tr("%1\n(modified)").arg(m_libraryName) : m_libraryName));
  m_programLabel->setVisible(!m_libraryName.isEmpty());
}

void ProgramWidget::updateRunStatus() {
  SystemValueList const *states = m_resHolder->states;
  bool const working{HardwareStatus::HardwareStatusEnum::WORK == m_resHolder->hard_status->getHardwareStatus()};
  if (!working || !states->contains("interval") || !states->contains("intervals_count")) {
    m_runStatus->clear();
    return;
  }
  int const   current{static_cast<int>(states->getSysValue("interval")->getValue())};
  int const   count{static_cast<int>(states->getSysValue("intervals_count")->getValue())};
  QStringList lines;
  if (!m_runningName.isEmpty()) {
    lines << QStringLiteral("<b>%1</b>").arg(m_runningName.toHtmlEscaped());
  }
  lines << tr("interval %1 of %2").arg(current).arg(count);
  QString const label = m_runningLabels.value(current - 1);
  if (!label.isEmpty()) {
    lines << QStringLiteral("<i>%1</i>").arg(label.toHtmlEscaped());
  }
  if (states->contains("cycle_total") && (states->getSysValue("cycle_total")->getValue() > 1)) {
    lines << tr("cycle %1 of %2")
               .arg(static_cast<int>(states->getSysValue("cycle_cur")->getValue()))
               .arg(static_cast<int>(states->getSysValue("cycle_total")->getValue()));
  }
  m_runStatus->setText(lines.join("<br>"));
}
} // namespace deepvaccontrol
