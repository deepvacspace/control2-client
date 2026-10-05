#include "programwidget.h"

#include <QFrame>
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
#include "theme.h"
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

  connect(m_tableWidget, &QTableWidget::cellDoubleClicked, this, &ProgramWidget::openIntervalEditDialog);
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
  double const ssf{utils::ssf()};
  auto        *mainLay = new QVBoxLayout(this);
  mainLay->setContentsMargins(0, 0, 0, 0);
  mainLay->setSpacing(10 * ssf);

  auto makeCard = [this, ssf](QString const &title) {
    auto *card = new QFrame(this);
    card->setObjectName("pageCard");
    auto *lay = new QVBoxLayout(card);
    lay->setContentsMargins(14 * ssf, 8 * ssf, 14 * ssf, 10 * ssf);
    lay->setSpacing(6 * ssf);
    if (!title.isEmpty()) {
      auto *label = new QLabel(title, card);
      label->setObjectName("cardTitle");
      lay->addWidget(label);
    }
    return card;
  };
  auto button = [this, ssf](QString const &text, int width) {
    auto *btn = new QPushButton(text, this);
    btn->setFixedSize(width * ssf, 26 * ssf);
    return btn;
  };

  // ---- title (program name) and file / library actions
  m_programLabel = new QLabel(this);
  {
    QFont font = m_programLabel->font();
    font.setBold(true);
    m_programLabel->setFont(font);
  }
  m_libraryBadge = new QLabel(this);
  m_saveBtn        = button(tr("Save file"), 76);
  m_loadBtn        = button(tr("Open file"), 76);
  m_libraryBtn     = button(tr("Library…"), 76);
  m_saveLibraryBtn = button(tr("Save to library…"), 110);
  m_saveBtn->setToolTip(tr("Save the program to a file (also on a USB drive)"));
  m_loadBtn->setToolTip(tr("Open a program file"));
  m_libraryBtn->setToolTip(tr("Open a saved program (also those shared by paired computers)"));
  m_saveLibraryBtn->setToolTip(tr("Save this program by name, with a label per interval"));

  // ---- program profile
  auto *graphCard = makeCard({});
  m_programGraph  = new ProgramGraph(graphCard);
  graphCard->layout()->addWidget(m_programGraph);
  mainLay->addWidget(graphCard, 7);

  // ---- intervals
  auto *bottomLay = new QHBoxLayout();
  bottomLay->setSpacing(10 * ssf);
  mainLay->addLayout(bottomLay, 3);

  // compact cards so the program graph keeps most of the height
  auto *intervalsCard = makeCard(tr("Intervals"));
  auto *intervalsLay  = static_cast<QVBoxLayout *>(intervalsCard->layout());
  auto *titleRow      = static_cast<QLabel *>(intervalsLay->itemAt(0)->widget());
  intervalsLay->removeWidget(titleRow);
  auto *intervalsTop = new QHBoxLayout();
  intervalsTop->setSpacing(10 * ssf);
  intervalsTop->addWidget(titleRow);
  intervalsTop->addSpacing(6 * ssf);
  intervalsTop->addWidget(m_programLabel);
  intervalsTop->addWidget(m_libraryBadge);
  intervalsTop->addStretch();
  intervalsLay->addLayout(intervalsTop);
  m_tableWidget       = new IntervalListView(intervalsCard);
  intervalsLay->addWidget(m_tableWidget, 1);
  m_newBtn    = button(tr("Новый"), 70);
  m_editBtn   = button(tr("Редактировать"), 70);
  m_deleteBtn = button(tr("Удалить"), 70);
  m_deleteBtn->setProperty("outline", true);
  auto *intervalButtonsLay = new QHBoxLayout();
  intervalButtonsLay->setSpacing(8 * ssf);
  intervalButtonsLay->addWidget(m_newBtn);
  intervalButtonsLay->addWidget(m_editBtn);
  intervalButtonsLay->addWidget(m_deleteBtn);
  intervalButtonsLay->addStretch();
  for (QPushButton *btn : {m_saveBtn, m_loadBtn, m_libraryBtn, m_saveLibraryBtn}) {
    intervalButtonsLay->addWidget(btn);
  }
  intervalsLay->addLayout(intervalButtonsLay);
  bottomLay->addWidget(intervalsCard, 62);

  // ---- cycle, run details, start
  auto *runCard = makeCard(tr("Цикл"));
  auto *runLay  = static_cast<QVBoxLayout *>(runCard->layout());
  m_cycleWidget = new CycleWidget(runCard);
  runLay->addWidget(m_cycleWidget);
  auto *divider = new QFrame(runCard);
  divider->setObjectName("cardDivider");
  divider->setFixedHeight(qMax(1, static_cast<int>(1 * ssf)));
  runLay->addWidget(divider);
  auto *detailsTitle = new QLabel(tr("Run details"), runCard);
  detailsTitle->setProperty("muted", true);
  runLay->addWidget(detailsTitle);
  m_dutLabel = new QLabel(runCard);
  m_dutLabel->setObjectName("dutField");
  auto *detailsBtn = new RunDetailsButton(m_resHolder, runCard);
  detailsBtn->setPlainCaption(true);
  detailsBtn->setFixedSize(110 * ssf, 26 * ssf);
  auto *detailsRow = new QHBoxLayout();
  detailsRow->setSpacing(8 * ssf);
  detailsRow->addWidget(m_dutLabel, 1);
  detailsRow->addWidget(detailsBtn);
  runLay->addLayout(detailsRow);
  m_runStatus = new QLabel(runCard);
  m_runStatus->setWordWrap(true);
  runLay->addWidget(m_runStatus);
  runLay->addStretch();
  m_startBtn = new QPushButton(tr("Пуск"), runCard);
  m_startBtn->setObjectName("startButton");
  m_startDelayedBtn = new QPushButton(tr("Отлож.\nпуск").replace('\n', ' '), runCard);
  for (QPushButton *btn : {m_startBtn, m_startDelayedBtn}) {
    btn->setFixedHeight(32 * ssf);
    QFont font = btn->font();
    font.setBold(true);
    font.setPointSizeF(QWidget::font().pointSizeF() * 1.1);
    btn->setFont(font);
  }
  auto *startLay = new QHBoxLayout();
  startLay->setSpacing(10 * ssf);
  startLay->addWidget(m_startBtn, 1);
  startLay->addWidget(m_startDelayedBtn, 1);
  runLay->addLayout(startLay);
  bottomLay->addWidget(runCard, 38);

  m_buttons << m_startBtn << m_newBtn << m_editBtn << m_deleteBtn << m_startDelayedBtn << m_libraryBtn << m_saveLibraryBtn;

  theme::Colors const &c = theme::colors();
  m_dutLabel->setStyleSheet(QStringLiteral("QLabel#dutField { background: %1; border: %2px solid %3; border-radius: %4px; padding: %5px %6px; }")
                              .arg(c.panelAlt.name())
                              .arg(qMax(1, static_cast<int>(1 * ssf)))
                              .arg(c.border.name())
                              .arg(static_cast<int>(3 * ssf))
                              .arg(static_cast<int>(3 * ssf))
                              .arg(static_cast<int>(8 * ssf)));
  auto updateDut = [this] {
    core::RunMetadata const metadata = m_resHolder->recorder ? m_resHolder->recorder->nextRunMetadata() : core::RunMetadata{};
    m_dutLabel->setText(metadata.dut.isEmpty() ? tr("DUT: not set") : tr("DUT: %1").arg(metadata.dut));
  };
  if (m_resHolder->recorder) {
    connect(m_resHolder->recorder, &ExperimentRecorder::nextRunMetadataChanged, this, updateDut);
  }
  updateDut();
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
  theme::Colors const &c = theme::colors();
  m_programLabel->setText(m_libraryName);
  m_programLabel->setVisible(!m_libraryName.isEmpty());
  m_programLabel->setStyleSheet(QStringLiteral("border-bottom: %1px solid %2; padding-bottom: %3px;")
                                  .arg(qMax(1, static_cast<int>(2 * utils::ssf())))
                                  .arg(m_libraryName.isEmpty() ? QStringLiteral("transparent") : c.accent.name())
                                  .arg(static_cast<int>(1 * utils::ssf())));
  QColor const badgeColor = m_libraryModified ? c.warning : c.ready;
  m_libraryBadge->setText(m_libraryModified ? tr("LIBRARY · MODIFIED") : tr("LIBRARY"));
  m_libraryBadge->setStyleSheet(QStringLiteral("color: %1; border: %2px solid %1; border-radius: %3px; padding: %4px %5px; font-weight: bold;")
                                  .arg(badgeColor.name())
                                  .arg(qMax(1, static_cast<int>(1 * utils::ssf())))
                                  .arg(static_cast<int>(3 * utils::ssf()))
                                  .arg(static_cast<int>(1 * utils::ssf()))
                                  .arg(static_cast<int>(6 * utils::ssf())));
  QFont small = font();
  small.setPointSizeF(font().pointSizeF() * 0.8);
  m_libraryBadge->setFont(small);
  m_libraryBadge->setVisible(!m_libraryName.isEmpty());
  if (m_tableWidget) {
    m_tableWidget->setLabels(m_labels);
  }
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
