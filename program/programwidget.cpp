#include "programwidget.h"

#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>

#include "constants.h"
#include "delayinfodialog.h"
#include "delayselectdialog.h"
#include "dialogs/messagedialog.h"
#include "experimentrecorder.h"
#include "rundetailsdialog.h"
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
      if (nexIndex <= m_labels.size()) {
        m_labels.insert(nexIndex, QString());
        labelsEdited();
      }
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
      if (index < m_labels.size()) {
        m_labels.removeAt(index);
        labelsEdited();
      }
      m_model->delItem(index);
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
  updateSequenceInfo();
  updateButtonIcons();
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
  connect(m_model, &ProgramModel::numberOfIntervalChanged, this, &ProgramWidget::updateSequenceInfo);
  connect(m_model, &ProgramModel::intervalSelected, this, &ProgramWidget::updateSequenceInfo);
  connect(m_cycleWidget, &CycleWidget::paramsChanged, m_model, &ProgramModel::setCycleParams);
}

void ProgramWidget::openIntervalEditDialog() {
  auto *editDialog = new IntervalEditDialog(m_model, &m_labels, this);
  connect(editDialog, &IntervalEditDialog::labelsChanged, this, &ProgramWidget::labelsEdited);
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

  // ---- title (program name) and file / library actions
  m_programLabel = new QLabel(this);
  {
    QFont font = m_programLabel->font();
    font.setBold(true);
    m_programLabel->setFont(font);
  }
  m_libraryBadge = new QLabel(this);

  // ---- program profile
  auto *graphCard = makeCard({});
  m_programGraph  = new ProgramGraph(graphCard);
  graphCard->layout()->addWidget(m_programGraph);
  mainLay->addWidget(graphCard, 7);

  // ---- interval sequence
  auto *bottomLay = new QHBoxLayout();
  bottomLay->setSpacing(10 * ssf);
  mainLay->addLayout(bottomLay, 3);

  auto *intervalsCard = makeCard({});
  auto *intervalsLay  = static_cast<QVBoxLayout *>(intervalsCard->layout());
  auto  toolButton    = [this, ssf](QString const &text) {
    auto *btn = new QPushButton(text, this);
    btn->setFixedHeight(26 * ssf);
    btn->setMinimumWidth(78 * ssf);
    btn->setIconSize(QSize(14 * ssf, 14 * ssf));
    QFont font = btn->font();
    font.setBold(true);
    font.setPointSizeF(font.pointSizeF() * 0.9);
    btn->setFont(font);
    return btn;
  };

  // header: title, interval count, library program; New / Edit / Delete on the right
  auto *intervalsTop = new QHBoxLayout();
  intervalsTop->setSpacing(8 * ssf);
  m_sequenceIcon = new QLabel(intervalsCard);
  intervalsTop->addWidget(m_sequenceIcon);
  auto *title = new QLabel(tr("Intervals").toUpper(), intervalsCard);
  title->setObjectName("cardTitle");
  {
    QFont font = title->font();
    font.setLetterSpacing(QFont::PercentageSpacing, 106);
    title->setFont(font);
  }
  intervalsTop->addWidget(title);
  m_countBadge = new QLabel(intervalsCard);
  m_countBadge->setObjectName("countBadge");
  intervalsTop->addWidget(m_countBadge);
  intervalsTop->addSpacing(6 * ssf);
  intervalsTop->addWidget(m_programLabel);
  intervalsTop->addWidget(m_libraryBadge);
  intervalsTop->addStretch();
  m_newBtn = toolButton(tr("New"));
  m_newBtn->setObjectName("primaryButton");
  m_editBtn   = toolButton(tr("Edit"));
  m_deleteBtn = toolButton(tr("Delete"));
  m_deleteBtn->setProperty("outline", true);
  intervalsTop->addWidget(m_newBtn);
  intervalsTop->addWidget(m_editBtn);
  intervalsTop->addWidget(m_deleteBtn);
  intervalsLay->addLayout(intervalsTop);

  m_tableWidget = new IntervalListView(intervalsCard);
  intervalsLay->addWidget(m_tableWidget, 1);

  // footer: the program library (database); selected position on the right
  m_saveLibraryBtn = toolButton(tr("Save"));
  m_libraryBtn     = toolButton(tr("Load"));
  m_saveLibraryBtn->setToolTip(tr("Save this program in the program library, by name"));
  m_libraryBtn->setToolTip(tr("Load a program from the program library (also those shared by paired computers)"));
  m_positionLabel = new QLabel(intervalsCard);
  m_positionLabel->setProperty("muted", true);
  auto *intervalsBottom = new QHBoxLayout();
  intervalsBottom->setSpacing(8 * ssf);
  intervalsBottom->addWidget(m_saveLibraryBtn);
  intervalsBottom->addWidget(m_libraryBtn);
  intervalsBottom->addStretch();
  intervalsBottom->addWidget(m_positionLabel);
  intervalsLay->addLayout(intervalsBottom);
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
  auto *detailsTitle = new QLabel(tr("Experiment details"), runCard);
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

void ProgramWidget::changeEvent(QEvent *event) {
  QWidget::changeEvent(event);
  if (event->type() == QEvent::PaletteChange) {
    updateProgramLabel(); // title underline and badge colours follow the theme
    updateButtonIcons();
  }
}

void ProgramWidget::labelsEdited() {
  if (!m_loadingLibrary && !m_libraryName.isEmpty()) {
    m_libraryModified = true;
  }
  updateProgramLabel(); // also hands the labels to the table
}

void ProgramWidget::updateSequenceInfo() {
  if (!m_model || !m_countBadge) {
    return;
  }
  int const count{m_model->getStandardModel()->columnCount()};
  int const selected{m_model->getSelectedInterval()};
  auto      two = [](int n) { return QStringLiteral("%1").arg(n, 2, 10, QLatin1Char('0')); };
  m_countBadge->setText(two(count));
  m_positionLabel->setText((count > 0) ? QStringLiteral("%1 / %2").arg(two(qBound(1, selected + 1, count)), two(count)) : QString());
}

void ProgramWidget::updateButtonIcons() {
  if (!m_newBtn) {
    return;
  }
  theme::Colors const &c = theme::colors();
  int const            size{static_cast<int>(14 * utils::ssf())};
  m_newBtn->setIcon(theme::menuIcon("plus", Qt::white, Qt::white, size));
  m_editBtn->setIcon(theme::menuIcon("edit", c.text, c.text, size));
  m_deleteBtn->setIcon(theme::menuIcon("delete", c.danger, c.danger, size));
  m_saveLibraryBtn->setIcon(theme::menuIcon("save", c.text, c.text, size));
  m_libraryBtn->setIcon(theme::menuIcon("load", c.text, c.text, size));
  int const titleIcon{static_cast<int>(16 * utils::ssf())};
  m_sequenceIcon->setPixmap(theme::menuIcon("sequence", c.muted, c.muted, titleIcon).pixmap(titleIcon));
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
