#include "head.h"
#include "hometrend.h"

#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QFontMetrics>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>

#include "chamberview.h"
#include "controloutput.h"
#include "constants.h"
#include "experimentrecorder.h"
#include "graphstyleeditor.h"
#include "pressureunits.h"
#include "statusmanager.h"
#include "theme.h"
#include "utils.h"

namespace deepvaccontrol {

namespace {
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
  mainLay->setSpacing(theme::dp(20));

  m_leftStack = new QStackedWidget(this);
  m_trend     = new HomeTrend(m_resHolder, m_leftStack);
  m_diagram   = new ChamberView(m_resHolder, m_leftStack);
  m_leftStack->addWidget(m_trend);
  m_leftStack->addWidget(m_diagram);
  mainLay->addWidget(m_leftStack, 55);
  // small buttons in the corner, clear of the chart: the chart's channels, and the other view (named)
  m_corner        = new QWidget(m_leftStack);
  auto *cornerLay = new QHBoxLayout(m_corner);
  cornerLay->setContentsMargins(0, 0, 0, 0);
  cornerLay->setSpacing(theme::dp(12));
  m_channelsBtn = new QPushButton(tr("Channels"), m_corner);
  m_viewSwitch  = new QPushButton(tr("Diagram"), m_corner);
  for (QPushButton *button : {m_channelsBtn, m_viewSwitch}) {
    button->setFocusPolicy(Qt::NoFocus);
    button->setFixedHeight(theme::dp(48));
    QFont font = button->font();
    font.setPointSizeF(font.pointSizeF() * 0.9);
    button->setFont(font);
    cornerLay->addWidget(button);
  }
  connect(m_viewSwitch, &QPushButton::clicked, this, [this] {
    bool const toDiagram{m_leftStack->currentWidget() == m_trend};
    setChannelsOpen(false);
    m_leftStack->setCurrentWidget(toDiagram ? static_cast<QWidget *>(m_diagram) : m_trend);
    m_viewSwitch->setText(toDiagram ? tr("Chart") : tr("Diagram"));
    m_channelsBtn->setVisible(!toDiagram);
    placeOverlays();
  });
  connect(m_channelsBtn, &QPushButton::clicked, this, [this] { setChannelsOpen(m_channelsPanel->isHidden()); });
  m_leftStack->installEventFilter(this);

  // the Channels panel: as the Analysis page's side panel (title, count, ×, a card per channel), the chart's own
  // choice; temperature, its setpoint and the pressure until one is made
  m_channelsPanel = new QFrame(this);
  m_channelsPanel->setObjectName("sidePanel");
  auto *shadow = new QGraphicsDropShadowEffect(m_channelsPanel);
  shadow->setBlurRadius(theme::dp(45));
  shadow->setOffset(0, theme::dp(5));
  shadow->setColor(QColor(30, 38, 51, 60));
  m_channelsPanel->setGraphicsEffect(shadow);
  auto *panelLay = new QVBoxLayout(m_channelsPanel);
  panelLay->setContentsMargins(theme::dp(32), theme::dp(28), theme::dp(20), theme::dp(28));
  panelLay->setSpacing(theme::dp(8));
  auto *panelHeader = new QHBoxLayout();
  panelHeader->setContentsMargins(0, 0, theme::dp(12), 0);
  auto *panelTitle = new QLabel(tr("Channels"), m_channelsPanel);
  QFont titleFont  = panelTitle->font();
  titleFont.setBold(true);
  titleFont.setPointSizeF(titleFont.pointSizeF() * 1.45);
  panelTitle->setFont(titleFont);
  auto *closeBtn = new QToolButton(m_channelsPanel);
  closeBtn->setObjectName("panelClose");
  closeBtn->setText(QStringLiteral("×"));
  closeBtn->setToolTip(tr("Close"));
  closeBtn->setFocusPolicy(Qt::NoFocus);
  closeBtn->setFixedSize(theme::dp(67), theme::dp(67));
  panelHeader->addWidget(panelTitle, 1);
  panelHeader->addWidget(closeBtn);
  panelLay->addLayout(panelHeader);
  m_channelsCount = new QLabel(m_channelsPanel);
  m_channelsCount->setProperty("muted", true);
  panelLay->addWidget(m_channelsCount);
  panelLay->addSpacing(theme::dp(16));
  m_channels = new GraphStyleEditor(m_resHolder, QStringLiteral("homeStates"), {QStringLiteral("temp"), QStringLiteral("temp_ref"), QStringLiteral("pres")},
                                    m_channelsPanel);
  m_channels->setCompact();
  panelLay->addWidget(m_channels, 1);
  m_channelsPanel->setFixedWidth(theme::dp(618));
  m_channelsPanel->hide();
  m_channelsCount->setText(m_channels->countText());
  connect(closeBtn, &QToolButton::clicked, this, [this] { setChannelsOpen(false); });
  connect(m_channels, &GraphStyleEditor::countChanged, m_channelsCount, &QLabel::setText);
  connect(m_channels, &GraphStyleEditor::styleChanged, m_trend, &HomeTrend::setStyle);
  connect(m_channels, &GraphStyleEditor::styleAllChanged, m_trend, &HomeTrend::setStyles);
  m_trend->setStyles(m_channels->getStateStylesList());

  auto *column = new QVBoxLayout();
  column->setSpacing(theme::dp(20));
  mainLay->addLayout(column, 45);

  auto makeCard = [this] {
    auto *card = new QFrame(this);
    card->setObjectName("homeCard");
    auto *lay = new QVBoxLayout(card);
    lay->setContentsMargins(theme::dp(28), theme::dp(24), theme::dp(28), theme::dp(28));
    lay->setSpacing(theme::dp(12));
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
  m_statusDot->setFixedSize(theme::dp(15), theme::dp(15));
  m_statusText = new QLabel(statusCard);
  m_stopBtn    = new StopButton(statusCard);
  m_stopBtn->setFixedSize(theme::dp(142), theme::dp(48));
  topRow->addWidget(m_statusDot);
  topRow->addSpacing(theme::dp(8));
  topRow->addWidget(m_statusText);
  topRow->addStretch();
  topRow->addWidget(m_stopBtn);
  statusLay->addLayout(topRow);
  statusLay->addSpacing(theme::dp(28));
  // temperature and pressure side by side: the reading, its setpoint and control output, and the control output's
  // gauge (pressure always shown: a dash without a gauge or a connection)
  auto *columns = new QHBoxLayout();
  columns->setSpacing(theme::dp(32));
  auto block = [&](QString const &title, QLabel *&value, QLabel *&unit, QString const &unitText, QLabel *&setpoint, QLabel *&output,
                   ControlBar *&bar, QString const &low, QString const &high) {
    auto *lay = new QVBoxLayout();
    lay->setSpacing(theme::dp(4));
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
    lay->addSpacing(theme::dp(12));
    auto *line = new QFrame(statusCard);
    line->setObjectName("homeDivider");
    line->setFixedHeight(theme::dp(1));
    lay->addWidget(line);
    lay->addSpacing(theme::dp(12));
    auto *grid = new QGridLayout();
    grid->setHorizontalSpacing(theme::dp(32));
    grid->setVerticalSpacing(theme::dp(4));
    grid->addWidget(muted(tr("Setpoint")), 0, 0);
    grid->addWidget(muted(tr("Control output")), 0, 1);
    setpoint = new QLabel(statusCard);
    output   = new QLabel(statusCard);
    grid->addWidget(setpoint, 1, 0);
    grid->addWidget(output, 1, 1);
    lay->addLayout(grid);
    lay->addSpacing(theme::dp(24));
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
  separator->setFixedWidth(theme::dp(1));
  columns->addWidget(separator);
  block(tr("Pressure"), m_presValue, m_presUnit, QStringLiteral("Torr"), m_presSetpointValue, m_presOutputValue, m_presOutputBar,
        tr("Pumping"), tr("Venting"));
  statusLay->addLayout(columns);
  statusLay->addStretch();
  column->addWidget(statusCard, 54);

  // test time and program position
  auto *timeCard = makeCard();
  auto *timeLay  = static_cast<QVBoxLayout *>(timeCard->layout());
  // the program position always shown (a dash without a program), so the card reads the same way
  m_timeTitle = new QLabel(tr("Total test time"), timeCard);
  timeLay->addWidget(m_timeTitle);
  timeLay->addStretch();
  m_curWorkTimeLabel = new QLabel(timeCard);
  m_curWorkTimeLabel->setAlignment(Qt::AlignHCenter);
  timeLay->addWidget(m_curWorkTimeLabel);
  timeLay->addStretch();
  auto *timeLine = new QFrame(timeCard);
  timeLine->setObjectName("homeDivider");
  timeLine->setFixedHeight(theme::dp(1));
  timeLay->addWidget(timeLine);
  timeLay->addSpacing(theme::dp(8));
  auto *positionRow = new QHBoxLayout();
  auto  makeBox     = [&](QString const &title, QLabel *&value) {
    auto *box = new QWidget(timeCard);
    auto *lay = new QVBoxLayout(box);
    lay->setContentsMargins(0, theme::dp(8), 0, theme::dp(8));
    lay->setSpacing(theme::dp(8));
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
  auto *positionLine = new QFrame(timeCard);
  positionLine->setObjectName("homeDivider");
  positionLine->setFixedWidth(theme::dp(1));
  positionRow->addWidget(positionLine);
  m_cycleBox = makeBox(tr("Cycle"), m_cycleLabel);
  timeLay->addLayout(positionRow);
  timeLay->addStretch();
  m_nextIntervalBtn = new QPushButton(tr("Next step"), timeCard);
  m_nextIntervalBtn->setFixedSize(theme::dp(318), theme::dp(63));
  timeLay->addWidget(m_nextIntervalBtn, 0, Qt::AlignHCenter);
  column->addWidget(timeCard, 46);

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
                  .arg(theme::dp(1))
                  .arg(c.border.name())
                  .arg(theme::dp(15))
                  .arg(c.text.name(), c.muted.name()));
  QFont const base = QWidget::font();
  auto        sized = [&](double factor, bool bold) {
    QFont f = base;
    f.setPointSizeF(base.pointSizeF() * factor);
    f.setBold(bold);
    return f;
  };
  m_statusText->setFont(sized(1.15, true));
  m_timeTitle->setFont(sized(1.15, true));
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
  if (watched == m_leftStack) {
    if ((event->type() == QEvent::Resize) || (event->type() == QEvent::Show) || (event->type() == QEvent::Move)) {
      placeOverlays();
    }
    return QWidget::eventFilter(watched, event);
  }
  // (the application's events while the Channels panel is open) a tap beside it closes it, and goes on to what it
  // was on; the Channels button toggles it itself
  QPointF global;
  if (event->type() == QEvent::MouseButtonPress) {
    global = static_cast<QMouseEvent *>(event)->globalPosition();
  } else if ((event->type() == QEvent::TouchBegin) && !static_cast<QTouchEvent *>(event)->points().isEmpty()) {
    global = static_cast<QTouchEvent *>(event)->points().first().globalPosition();
  } else {
    return false;
  }
  auto const inside = [&global](QWidget const *widget) {
    return widget->isVisible() && QRect(widget->mapToGlobal(QPoint(0, 0)), widget->size()).contains(global.toPoint());
  };
  if (!inside(m_channelsPanel) && !inside(m_channelsBtn)) {
    setChannelsOpen(false);
  }
  return false;
}

// the corner buttons in the top right corner, the panel under them down to the chart's bottom
void Head::placeOverlays() {
  m_corner->adjustSize();
  m_corner->move(m_leftStack->width() - m_corner->width() - theme::dp(26), theme::dp(18));
  m_corner->raise();
  QPoint const corner = m_corner->mapTo(this, QPoint(0, 0));
  QRect const  stack{m_leftStack->mapTo(this, QPoint(0, 0)), m_leftStack->size()};
  int const    top{corner.y() + m_corner->height() + theme::dp(15)};
  m_channelsPanel->setGeometry(stack.right() - theme::dp(18) - m_channelsPanel->width() + 1, top, m_channelsPanel->width(),
                               std::max(theme::dp(225), stack.bottom() - theme::dp(18) - top + 1));
  m_channelsPanel->raise();
}

void Head::setChannelsOpen(bool open) {
  if (open == !m_channelsPanel->isHidden()) {
    return;
  }
  if (open) {
    placeOverlays();
    m_channelsPanel->show();
    m_channelsPanel->raise();
    qApp->installEventFilter(this);
  } else {
    m_channelsPanel->hide(); // keeps the choice (the editor's hide)
    qApp->removeEventFilter(this);
  }
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
  m_statusDot->setStyleSheet(QStringLiteral("background: %1; border-radius: %2px;").arg(dot.name()).arg(theme::dp(7)));
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
