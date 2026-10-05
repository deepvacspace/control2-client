#include "head.h"

#include <QCoreApplication>
#include <QEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QVBoxLayout>
#include <cmath>

#include "chamberview.h"
#include "constants.h"
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
} // namespace

// Control output from -100 % (cooling) to +100 % (heating), filled from the centre.
class ControlBar : public QWidget {
public:
  explicit ControlBar(QWidget *parent = nullptr) : QWidget{parent} {
    setFixedHeight(px(10));
  }
  void setValue(double value) {
    if (value != m_value) {
      m_value = value;
      update();
    }
  }

protected:
  void paintEvent(QPaintEvent *) override {
    theme::Colors const &c = theme::colors();
    QPainter             p(this);
    p.setRenderHint(QPainter::Antialiasing);
    double const h{px(5)};
    QRectF const track(0, (height() - h) / 2.0, width(), h);
    p.setPen(Qt::NoPen);
    p.setBrush(c.panelAlt.lighter(theme::isDark() ? 130 : 97));
    p.drawRoundedRect(track, h / 2, h / 2);
    if (std::isnan(m_value)) {
      return;
    }
    double const center = track.center().x();
    double const v      = std::clamp(m_value, -100.0, 100.0) / 100.0;
    double const end    = center + v * track.width() / 2.0;
    p.setBrush(v < 0 ? c.accent : c.heat);
    p.drawRoundedRect(QRectF(QPointF(std::min(center, end), track.top()), QPointF(std::max(center, end), track.bottom())), h / 2, h / 2);
    p.setBrush(c.text);
    p.drawRoundedRect(QRectF(end - px(1), 0, px(2), height()), px(1), px(1));
  }

private:
  double m_value{std::numeric_limits<double>::quiet_NaN()};
};

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
    m_resHolder->actions->addAction(ActionManager::ActionType::PLC, tr("Работа камеры остановлена пользователем"));
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
  auto *mainLay = new QHBoxLayout(this);
  mainLay->setContentsMargins(0, 0, 0, 0);
  mainLay->setSpacing(px(10));

  m_chamber = new ChamberView(m_resHolder, this);
  mainLay->addWidget(m_chamber, 64);

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
  statusLay->addWidget(muted(tr("Chamber temperature")));
  auto *tempRow = new QHBoxLayout();
  m_tempValue   = new QLabel(statusCard);
  m_tempUnit    = new QLabel(QStringLiteral("°C"), statusCard);
  m_tempUnit->setProperty("muted", true);
  m_tempUnit->setAlignment(Qt::AlignTop);
  tempRow->addWidget(m_tempValue);
  tempRow->addWidget(m_tempUnit);
  tempRow->addStretch();
  statusLay->addLayout(tempRow);
  auto *line = new QFrame(statusCard);
  line->setObjectName("homeDivider");
  line->setFixedHeight(px(1));
  statusLay->addWidget(line);
  statusLay->addSpacing(px(6));
  auto *grid = new QGridLayout();
  grid->setHorizontalSpacing(px(16));
  grid->setVerticalSpacing(px(2));
  grid->addWidget(muted(tr("Setpoint")), 0, 0);
  grid->addWidget(muted(tr("Control output")), 0, 1);
  m_setpointValue = new QLabel(statusCard);
  m_outputValue   = new QLabel(statusCard);
  grid->addWidget(m_setpointValue, 1, 0);
  grid->addWidget(m_outputValue, 1, 1);
  statusLay->addLayout(grid);
  statusLay->addSpacing(px(14));
  m_outputBar = new ControlBar(statusCard);
  statusLay->addWidget(m_outputBar);
  auto *barLabels = new QHBoxLayout();
  auto *cooling   = muted(tr("Cooling"));
  auto *heating   = muted(tr("Heating"));
  cooling->setProperty("small", true);
  heating->setProperty("small", true);
  barLabels->addWidget(cooling);
  barLabels->addStretch();
  barLabels->addWidget(heating);
  statusLay->addLayout(barLabels);
  statusLay->addStretch();
  column->addWidget(statusCard, 60);

  // test time and program position
  auto *timeCard = makeCard();
  auto *timeLay  = static_cast<QVBoxLayout *>(timeCard->layout());
  timeLay->addWidget(muted(tr("Total test time")));
  m_curWorkTimeLabel = new QLabel(timeCard);
  timeLay->addWidget(m_curWorkTimeLabel);
  timeLay->addSpacing(px(8));
  auto *positionRow = new QHBoxLayout();
  auto  makeBox     = [&](QString const &title, QLabel *&value) {
    auto *box = new QWidget(timeCard);
    auto *lay = new QVBoxLayout(box);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(px(2));
    lay->addWidget(muted(title));
    value = new QLabel(box);
    lay->addWidget(value);
    positionRow->addWidget(box, 1);
    return box;
  };
  m_intervalBox = makeBox(tr("Current interval"), m_intervalLabel);
  m_cycleBox    = makeBox(tr("Cycle"), m_cycleLabel);
  timeLay->addLayout(positionRow);
  timeLay->addSpacing(px(10));
  m_nextIntervalBtn = new QPushButton(tr("Следующий шаг"), timeCard);
  m_nextIntervalBtn->setFixedSize(px(100), px(26));
  timeLay->addWidget(m_nextIntervalBtn);
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
  m_tempValue->setFont(sized(5.4, true));
  m_tempUnit->setFont(sized(1.8, false));
  m_setpointValue->setFont(sized(1.3, true));
  m_outputValue->setFont(sized(1.3, true));
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
    status += QStringLiteral(" ") + QCoreApplication::translate("deepvaccontrol::StatusLabel", "по уставке");
  } else if (jobType == HardwareStatus::JobType::PROGRAM) {
    status += QStringLiteral(" ") + QCoreApplication::translate("deepvaccontrol::StatusLabel", "по программе");
  }
  m_statusText->setText(status);
  QColor const dot = working ? c.active
                             : (hardWorkStatus == HardwareStatus::ALARM ? c.danger
                                                                        : (hardWorkStatus == HardwareStatus::IDLE ? c.ready : c.off));
  m_statusDot->setStyleSheet(QStringLiteral("background: %1; border-radius: %2px;").arg(dot.name()).arg(px(4)));
  m_stopBtn->setEnabled(working && (AccessManager::AccessLevel::SPECTATOR_0 != m_accessLevel));

  // temperature, setpoint, control output
  SystemValue const *temp = sys("temp");
  SystemValue const *ref  = sys("temp_ref");
  SystemValue const *out  = sys("temp_u");
  m_tempValue->setText(temp ? valueText(temp->getValue(), static_cast<int>(temp->getPrec())) : QStringLiteral("--"));
  m_setpointValue->setText(ref ? valueText(ref->getValue(), static_cast<int>(ref->getPrec()), QStringLiteral(" °C")) : QStringLiteral("--"));
  double const u = out ? out->getValue() : std::numeric_limits<double>::quiet_NaN();
  m_outputValue->setText(out ? valueText(static_cast<float>(u), 0, QStringLiteral(" %")) : QStringLiteral("--"));
  m_outputValue->setStyleSheet(QStringLiteral("color: %1;").arg((std::isnan(u) || std::fabs(u) < 0.5) ? c.text.name() : (u < 0 ? c.cold.name() : c.heat.name())));
  m_outputBar->setValue(std::fabs(u) > 1e9 ? std::numeric_limits<double>::quiet_NaN() : u);

  // test time and program position
  SystemValue const *total = sys("t_total");
  m_curWorkTimeLabel->setText(working && total ? utils::timeFromSec(static_cast<long>(total->getValue()), true) : QStringLiteral("--:--:--"));

  bool const isProgramJob{working && (HardwareStatus::JobType::PROGRAM == jobType)};
  m_intervalBox->setVisible(isProgramJob);
  if (isProgramJob && sys("interval") && sys("intervals_count") && sys("cycle_cur") && sys("cycle_total")) {
    qint32 const intervalCur{static_cast<qint32>(sys("interval")->getValue())};
    qint32 const intervalCount{static_cast<qint32>(sys("intervals_count")->getValue())};
    qint32 const cycleCur{static_cast<qint32>(sys("cycle_cur")->getValue())};
    qint32 const cycleCount{static_cast<qint32>(sys("cycle_total")->getValue())};

    m_intervalLabel->setText(tr("%1 из %2").arg(intervalCur).arg(intervalCount));
    m_nextIntervalBtn->setVisible((intervalCur < intervalCount) || (cycleCur < cycleCount));
    m_cycleBox->setVisible(cycleCount > 1);
    m_cycleLabel->setText(tr("%1 из %2").arg(cycleCur).arg(cycleCount));
  } else {
    m_cycleBox->setVisible(false);
    m_nextIntervalBtn->setVisible(false);
  }
  m_nextIntervalBtn->setEnabled(AccessManager::AccessLevel::SPECTATOR_0 != m_accessLevel);
}
} // namespace deepvaccontrol
