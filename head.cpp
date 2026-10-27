#include "head.h"
#include "hometrend.h"

#include <QCoreApplication>
#include <QEvent>
#include <QFontMetrics>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <cmath>

#include "chamberview.h"
#include "controloutput.h"
#include "constants.h"
#include "experimentrecorder.h"
#include "pressureunits.h"
#include "statusmanager.h"
#include "theme.h"
#include "utils.h"

namespace deepvaccontrol {

namespace {
int px(double value) {
  return qMax(1, static_cast<int>(value * utils::ssf()));
}

QString valueText(float value, int prec, QString const &unit = {}) {
  if (std::isnan(value) || (value < -10e9) || (value > 10e9)) {
    return QStringLiteral("--");
  }
  return QString::number(value, 'f', prec).replace('-', QChar(0x2212)) + unit;
}
}

Head::Head(ResHolder *resHolder, QWidget *parent)
    : QWidget{parent}, m_resHolder{resHolder}, m_accessLevel{resHolder->users->getCurrentAccessLevel()} {
  initBaseGui();
  setupGuiAndLogic();
}

void Head::setupGuiAndLogic() {
  if (!m_resHolder->states) {
    m_resHolder->logger->stream(Logger::Level::ERR) << "m_resHolder.states is empty";
    return;
  }

  if (!m_resHolder->tcpGate) {
    m_resHolder->logger->stream(Logger::Level::ERR) << "m_resHolder.tcpGate is empty";
    return;
  }

  connect(m_resHolder->states, &SystemValueList::ValuesUpdated, this, &Head::updateGui);
  connect(m_resHolder->hard_status, &HardwareStatus::statusChanged, this, &Head::updateGui);
  connect(m_resHolder->status, &StatusManager::appStateChanged, this, &Head::updateGui);

  connect(m_stopBtn, &StopButton::confirmPressed, this, [this]() {
    m_resHolder->tcpGate->sendTextCmd("stop");
    m_resHolder->actions->addAction(ActionManager::ActionType::PLC, tr("Chamber operation stopped by user"));
  });

  connect(m_nextIntervalBtn, &QPushButton::clicked, this, [this]() {
    m_resHolder->tcpGate->sendTextCmd("next");
  });

  connect(m_resHolder->users, &AccessManager::userSelected, this, &Head::handleAccessChange);
  updateGui();
}

void Head::handleAccessChange(QString const &name, AccessManager::AccessLevel newLevel) {
  if (newLevel == m_accessLevel) {
    return;
  }

  m_accessLevel = newLevel;
  updateGui();
}

void Head::initBaseGui() {
  // the chart or the chamber's diagram on the left (a switch in its corner), the readings beside it
  auto *mainLay = new QHBoxLayout(this);
  mainLay->setContentsMargins(0, 0, 0, 0);
  mainLay->setSpacing(px(10));

  m_leftStack = new QStackedWidget(this);
  m_trend     = new HomeTrend(m_resHolder, m_leftStack);
  m_diagram   = new ChamberView(m_resHolder, m_leftStack);
  m_leftStack->addWidget(m_trend);
  m_leftStack->addWidget(m_diagram);
  mainLay->addWidget(m_leftStack, 64);
  // one button: it names the other view
  m_viewSwitch = new QPushButton(tr("Diagram"), m_leftStack);
  m_viewSwitch->setFocusPolicy(Qt::NoFocus);
  m_viewSwitch->setFixedHeight(px(30));
  m_viewSwitch->setMinimumWidth(px(84));
  connect(m_viewSwitch, &QPushButton::clicked, this, [this] {
    bool const toDiagram{m_leftStack->currentWidget() == m_trend};
    m_leftStack->setCurrentWidget(toDiagram ? static_cast<QWidget *>(m_diagram) : m_trend);
    m_viewSwitch->setText(toDiagram ? tr("Chart") : tr("Diagram"));
    m_viewSwitch->adjustSize();
    m_viewSwitch->move(m_leftStack->width() - m_viewSwitch->width() - px(14), px(12));
    m_viewSwitch->raise();
  });
  m_leftStack->installEventFilter(this);

  auto *column = new QVBoxLayout();
  column->setSpacing(px(10));
  mainLay->addLayout(column, 36);

  auto makeCard = [this] {
    auto *card = new QFrame(this);
    card->setObjectName("homeCard");
    auto *lay = new QVBoxLayout(card);
    lay->setContentsMargins(px(14), px(12), px(14), px(14));
    lay->setSpacing(px(6));
    return card;
  };
  auto muted = [this](QString const &text) {
    auto *label = new QLabel(text, this);
    label->setProperty("muted", true);
    return label;
  };

  // status and temperature
  auto *statusCard = makeCard();
  auto *statusLay  = static_cast<QVBoxLayout *>(statusCard->layout());
  auto *topRow     = new QHBoxLayout();
  m_statusDot      = new QLabel(statusCard);
  m_statusDot->setFixedSize(px(8), px(8));
  m_statusText = new QLabel(statusCard);
  m_stopBtn    = new StopButton(statusCard);
  m_stopBtn->setFixedSize(px(76), px(26));
  topRow->addWidget(m_statusDot);
  topRow->addSpacing(px(4));
  topRow->addWidget(m_statusText);
  topRow->addStretch();
  topRow->addWidget(m_stopBtn);
  statusLay->addLayout(topRow);
  statusLay->addSpacing(px(14));
  // temperature and pressure side by side: the reading, its setpoint and control output, and the control output's
  // gauge (pressure always shown: a dash without a gauge or a connection)
  auto *columns = new QHBoxLayout();
  columns->setSpacing(px(18));
  auto block = [&](QString const &title, QLabel *&value, QLabel *&unit, QString const &unitText, QLabel *&setpoint, QLabel *&output,
                   ControlBar *&bar, QString const &low, QString const &high) {
    auto *lay = new QVBoxLayout();
    lay->setSpacing(px(2));
    lay->addWidget(muted(title));
    // the number with its unit, sharing the bottom line
    auto *box = new QWidget(statusCard);
    auto *row = new QHBoxLayout(box);
    row->setContentsMargins(0, 0, 0, 0);
    value = new QLabel(box);
    unit  = new QLabel(unitText, box);
    unit->setProperty("muted", true);
    unit->setAlignment(Qt::AlignLeft | Qt::AlignBottom);
    row->addWidget(value, 0, Qt::AlignBottom);
    row->addWidget(unit, 0, Qt::AlignBottom);
    row->addStretch();
    lay->addWidget(box);
    lay->addSpacing(px(6));
    auto *line = new QFrame(statusCard);
    line->setObjectName("homeDivider");
    line->setFixedHeight(px(1));
    lay->addWidget(line);
    lay->addSpacing(px(6));
    auto *grid = new QGridLayout();
    grid->setHorizontalSpacing(px(16));
    grid->setVerticalSpacing(px(2));
    grid->addWidget(muted(tr("Setpoint")), 0, 0);
    grid->addWidget(muted(tr("Control output")), 0, 1);
    setpoint = new QLabel(statusCard);
    output   = new QLabel(statusCard);
    grid->addWidget(setpoint, 1, 0);
    grid->addWidget(output, 1, 1);
    lay->addLayout(grid);
    lay->addSpacing(px(12));
    bar = new ControlBar(statusCard);
    lay->addWidget(bar);
    auto   *labels    = new QHBoxLayout();
    QLabel *lowLabel  = muted(low);
    QLabel *highLabel = muted(high);
    lowLabel->setProperty("small", true);
    highLabel->setProperty("small", true);
    labels->addWidget(lowLabel);
    labels->addStretch();
    labels->addWidget(highLabel);
    lay->addLayout(labels);
    columns->addLayout(lay, 1);
  };
  block(tr("Chamber temperature"), m_tempValue, m_tempUnit, QStringLiteral("°C"), m_setpointValue, m_outputValue, m_outputBar,
        tr("Cooling"), tr("Heating"));
  auto *separator = new QFrame(statusCard);
  separator->setObjectName("homeDivider");
  separator->setFixedWidth(px(1));
  columns->addWidget(separator);
  block(tr("Pressure"), m_presValue, m_presUnit, QStringLiteral("Torr"), m_presSetpointValue, m_presOutputValue, m_presOutputBar,
        tr("Pumping"), tr("Venting"));
  statusLay->addLayout(columns);
  statusLay->addStretch();
  column->addWidget(statusCard, 60);

  // test time and program position
  auto *timeCard = makeCard();
  auto *timeLay  = static_cast<QVBoxLayout *>(timeCard->layout());
  // all centred; the program position always shown (a dash without a program), so the card reads the same way
  timeLay->addStretch();
  auto *timeTitle = muted(tr("Total test time"));
  timeTitle->setAlignment(Qt::AlignHCenter);
  timeLay->addWidget(timeTitle);
  m_curWorkTimeLabel = new QLabel(timeCard);
  m_curWorkTimeLabel->setAlignment(Qt::AlignHCenter);
  timeLay->addWidget(m_curWorkTimeLabel);
  timeLay->addSpacing(px(8));
  auto *positionRow = new QHBoxLayout();
  auto  makeBox     = [&](QString const &title, QLabel *&value) {
    auto *box = new QWidget(timeCard);
    auto *lay = new QVBoxLayout(box);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(px(2));
    auto *name = muted(title);
    name->setAlignment(Qt::AlignHCenter);
    lay->addWidget(name);
    value = new QLabel(box);
    value->setAlignment(Qt::AlignHCenter);
    lay->addWidget(value);
    positionRow->addWidget(box, 1);
    return box;
  };
  m_intervalBox = makeBox(tr("Current interval"), m_intervalLabel);
  m_cycleBox    = makeBox(tr("Cycle"), m_cycleLabel);
  timeLay->addLayout(positionRow);
  timeLay->addSpacing(px(10));
  m_nextIntervalBtn = new QPushButton(tr("Next step"), timeCard);
  m_nextIntervalBtn->setFixedSize(px(100), px(26));
  timeLay->addWidget(m_nextIntervalBtn, 0, Qt::AlignHCenter);
  timeLay->addStretch();
  column->addWidget(timeCard, 40);

  restyle();
}

void Head::restyle() {
  static bool busy{false}; // style sheet changes may raise a palette change again
  if (busy) {
    return;
  }
  busy                   = true;
  theme::Colors const &c = theme::colors();
  setStyleSheet(QStringLiteral(R"(
QFrame#homeCard { background: %1; border: %2px solid %3; border-radius: %4px; }
QFrame#homeDivider { background: %3; border: none; }
QLabel { color: %5; }
QLabel[muted="true"] { color: %6; }
)")
                  .arg(c.panel.name())
                  .arg(px(1))
                  .arg(c.border.name())
                  .arg(px(8))
                  .arg(c.text.name(), c.muted.name()));
  QFont const base = QWidget::font();
  auto        sized = [&](double factor, bool bold) {
    QFont f = base;
    f.setPointSizeF(base.pointSizeF() * factor);
    f.setBold(bold);
    return f;
  };
  m_statusText->setFont(sized(1.15, true));
  m_tempValue->setFont(sized(4.2, true));
  m_tempUnit->setFont(sized(1.8, false));
  m_presValue->setFont(sized(4.2, true));
  m_presUnit->setFont(sized(1.8, false));
  // the unit's baseline on the number's: lifted by the difference of their descents
  for (auto const &[value, unit] : {std::pair{m_tempValue, m_tempUnit}, std::pair{m_presValue, m_presUnit}}) {
    int const lift{QFontMetrics(value->font()).descent() - QFontMetrics(unit->font()).descent()};
    unit->setContentsMargins(0, 0, 0, std::max(0, lift));
  }
  for (QLabel *label : {m_setpointValue, m_outputValue, m_presSetpointValue, m_presOutputValue}) {
    label->setFont(sized(1.3, true));
  }
  m_curWorkTimeLabel->setFont(sized(2.6, true));
  m_intervalLabel->setFont(sized(1.3, true));
  m_cycleLabel->setFont(sized(1.3, true));
  for (auto *label : findChildren<QLabel *>()) {
    if (label->property("small").toBool()) {
      label->setFont(sized(0.8, false));
    }
  }
  updateGui();
  busy = false;
}

bool Head::eventFilter(QObject *watched, QEvent *event) {
  if ((watched == m_leftStack) && ((event->type() == QEvent::Resize) || (event->type() == QEvent::Show))) {
    m_viewSwitch->adjustSize();
    m_viewSwitch->move(m_leftStack->width() - m_viewSwitch->width() - px(14), px(12));
    m_viewSwitch->raise();
  }
  return QWidget::eventFilter(watched, event);
}

void Head::changeEvent(QEvent *event) {
  QWidget::changeEvent(event);
  if (event->type() == QEvent::PaletteChange) {
    restyle();
  }
}

void Head::updateGui() {
  theme::Colors const &c      = theme::colors();
  auto const           states = m_resHolder->states;
  auto                 sys    = [states](char const *name) -> SystemValue * {
    return states->contains(name) ? states->getSysValue(name) : nullptr;
  };

  HardwareStatus::HardwareStatusEnum const hardWorkStatus{m_resHolder->hard_status->getHardwareStatus()};
  int const                                jobType{sys("work_type") ? static_cast<int>(sys("work_type")->getValue()) : 0};
  bool const                               working{HardwareStatus::HardwareStatusEnum::WORK == hardWorkStatus};

  // status line: as in the header (status and job type)
  QString status = hardwareStatusList().value(static_cast<int>(hardWorkStatus));
  if (jobType == HardwareStatus::JobType::SETPOINT) {
    status += QStringLiteral(" ") + QCoreApplication::translate("deepvaccontrol::StatusLabel", "(setpoint)");
  } else if (jobType == HardwareStatus::JobType::PROGRAM) {
    status += QStringLiteral(" ") + QCoreApplication::translate("deepvaccontrol::StatusLabel", "(program)");
  }
  m_statusText->setText(status);
  QColor const dot = working ? c.active
                             : (hardWorkStatus == HardwareStatus::ALARM ? c.danger
                                                                        : (hardWorkStatus == HardwareStatus::IDLE ? c.ready : c.off));
  m_statusDot->setStyleSheet(QStringLiteral("background: %1; border-radius: %2px;").arg(dot.name()).arg(px(4)));
  m_stopBtn->setEnabled(working && (AccessManager::AccessLevel::SPECTATOR_0 != m_accessLevel));

  // temperature and pressure, each with its setpoint and control output
  // not connected: the values are not the chamber's (zeros or the last ones received), a dash instead (with its unit)
  StatusManager::AppState const appState{m_resHolder->status ? m_resHolder->status->getAppState() : StatusManager::AppState::OFFLINE};
  bool const online{(StatusManager::AppState::ONLINE == appState) || (StatusManager::AppState::DUMMY == appState)};
  QString const      none{QStringLiteral("—")};
  SystemValue const *temp = online ? sys("temp") : nullptr;
  m_tempValue->setText(temp ? valueText(temp->getValue(), static_cast<int>(temp->getPrec())) : none);
  SystemValue const *tempRef = online ? sys("temp_ref") : nullptr;
  m_setpointValue->setText(tempRef ? valueText(tempRef->getValue(), static_cast<int>(tempRef->getPrec()), QStringLiteral(" °C"))
                                   : none + QStringLiteral(" °C"));
  // pressure in Torr, whatever unit the chamber reports
  auto torr = [&](char const *name) {
    SystemValue const *value = online ? sys(name) : nullptr;
    return value ? pressure::toTorr(value->getValue(), sys("pres") ? sys("pres")->getLabel() : value->getLabel())
                 : std::numeric_limits<double>::quiet_NaN();
  };
  m_presValue->setText(pressure::text(torr("pres")));
  m_presSetpointValue->setText(pressure::text(torr("pres_ref")) + QStringLiteral(" Torr"));
  // the control outputs: cooling .. heating, pumping .. venting
  auto showOutput = [&](char const *name, QLabel *label, ControlBar *bar, QColor const &low, QColor const &high) {
    SystemValue const *out = online ? sys(name) : nullptr;
    double const       u{(out && (std::fabs(out->getValue()) < 1e9)) ? out->getValue() : std::numeric_limits<double>::quiet_NaN()};
    label->setText(std::isnan(u) ? none + QStringLiteral(" %") : valueText(static_cast<float>(u), 0, QStringLiteral(" %")));
    label->setStyleSheet(QStringLiteral("color: %1;").arg((std::isnan(u) || (std::fabs(u) < 0.5)) ? c.text.name()
                                                                                                 : (u < 0 ? low.name() : high.name())));
    bar->setValue(u);
  };
  showOutput("temp_u", m_outputValue, m_outputBar, c.cold, c.heat);
  showOutput("pres_u", m_presOutputValue, m_presOutputBar, c.accent, c.warning);

  // test time and program position
  SystemValue const *total = sys("t_total");
  m_curWorkTimeLabel->setText(working && total ? utils::timeFromSec(static_cast<long>(total->getValue()), true) : QStringLiteral("--:--:--"));

  // the program position: always shown, a dash when no program runs; Next step only usable when there is one
  bool const isProgramJob{working && (HardwareStatus::JobType::PROGRAM == jobType)};
  bool       canStep{false};
  if (isProgramJob && sys("interval") && sys("intervals_count") && sys("cycle_cur") && sys("cycle_total")) {
    qint32 const intervalCur{static_cast<qint32>(sys("interval")->getValue())};
    qint32 const intervalCount{static_cast<qint32>(sys("intervals_count")->getValue())};
    qint32 const cycleCur{static_cast<qint32>(sys("cycle_cur")->getValue())};
    qint32 const cycleCount{static_cast<qint32>(sys("cycle_total")->getValue())};

    m_intervalLabel->setText(tr("%1 of %2").arg(intervalCur).arg(intervalCount));
    m_cycleLabel->setText(tr("%1 of %2").arg(cycleCur).arg(cycleCount));
    canStep = (intervalCur < intervalCount) || (cycleCur < cycleCount);
  } else {
    m_intervalLabel->setText(QStringLiteral("—"));
    m_cycleLabel->setText(QStringLiteral("—"));
  }
  m_intervalBox->setVisible(true);
  m_cycleBox->setVisible(true);
  m_nextIntervalBtn->setVisible(true);
  m_nextIntervalBtn->setEnabled(canStep && (AccessManager::AccessLevel::SPECTATOR_0 != m_accessLevel));
}
}
