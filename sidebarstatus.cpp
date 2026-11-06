#include "sidebarstatus.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QEvent>
#include <QLabel>
#include <QTimer>
#include <QVBoxLayout>
#include <cmath>

#include "constants.h"
#include "pressureunits.h"
#include "theme.h"
#include "utils.h"

namespace deepvaccontrol {

namespace {
int px(double value) {
  return qMax(1, static_cast<int>(value * utils::ssf()));
}

QString number(SystemValue const *value) {
  float const v{value->getValue()};
  if (std::isnan(v) || (v < -10e9) || (v > 10e9)) {
    return QStringLiteral("--");
  }
  if (value->isSciNotation()) {
    return utils::formatScientific(v, static_cast<int>(value->getPrec()));
  }
  return QString::number(v, 'f', static_cast<int>(value->getPrec()));
}

QString unitOf(QString const &name) {
  if (name == QLatin1String("temp")) {
    return QStringLiteral("°C");
  }
  if (name == QLatin1String("hum")) {
    return QStringLiteral("%");
  }
  if (name == QLatin1String("pres")) {
    return QStringLiteral("Torr"); // converted from the chamber's unit (pressure::toTorr)
  }
  return {};
}

QColor statusColor(HardwareStatus::HardwareStatusEnum status) {
  theme::Colors const &c = theme::colors();
  switch (status) {
    case HardwareStatus::WORK:
      return c.active;
    case HardwareStatus::FINISH:
      return c.accent;
    case HardwareStatus::ALARM:
      return c.danger;
    case HardwareStatus::IDLE:
      return c.ready;
    default:
      return c.off;
  }
}

// centred in the sidebar's width, wrapped when longer
QLabel *centred(QWidget *parent) {
  auto *label = new QLabel(parent);
  label->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
  label->setWordWrap(true);
  return label;
}

QWidget *separator(QWidget *parent) {
  auto *line = new QWidget(parent);
  line->setObjectName("menuSeparator"); // styled by the menu
  line->setFixedHeight(px(1));
  return line;
}
}

SidebarStatus::SidebarStatus(ResHolder *resHolder, QWidget *parent) : QWidget{parent}, m_resHolder{resHolder} {
  auto *lay = new QVBoxLayout(this);
  lay->setContentsMargins(0, px(4), 0, px(4));
  lay->setSpacing(px(2));
  lay->addWidget(separator(this));
  lay->addSpacing(px(4));
  m_status = centred(this);
  m_status->setTextFormat(Qt::RichText);
  m_job = centred(this);
  lay->addWidget(m_status);
  lay->addWidget(m_job);
  for (auto const &[name, label] : statusBarStatesList()) {
    Value value;
    value.name   = name;
    value.box    = new QWidget(this);
    value.box->setToolTip(label);
    auto *boxLay = new QVBoxLayout(value.box);
    boxLay->setContentsMargins(0, px(6), 0, 0);
    boxLay->setSpacing(0);
    value.value    = centred(value.box);
    value.unit     = centred(value.box);
    value.setpoint = centred(value.box);
    boxLay->addWidget(value.value);
    boxLay->addWidget(value.unit);
    boxLay->addWidget(value.setpoint);
    lay->addWidget(value.box);
    m_values.push_back(value);
  }
  lay->addSpacing(px(4));
  lay->addWidget(separator(this));

  connect(m_resHolder->hard_status, &HardwareStatus::statusChanged, this, &SidebarStatus::updateStatus);
  if (m_resHolder->states->contains("work_type")) {
    connect(m_resHolder->states->getSysValue("work_type"), &SystemValue::valueChanged, this, &SidebarStatus::updateStatus);
  }
  connect(m_resHolder->states, &SystemValueList::ValuesUpdated, this, &SidebarStatus::updateValues);
  connect(m_resHolder->status, &StatusManager::appStateChanged, this, &SidebarStatus::updateStatus);
  connect(m_resHolder->status, &StatusManager::appStateChanged, this, &SidebarStatus::updateValues);
  restyle();
}

void SidebarStatus::restyle() {
  static bool busy{false}; // style sheet changes may raise a palette change again
  if (busy) {
    return;
  }
  busy                    = true;
  theme::Colors const &sc = theme::sidebarColors();
  QFont                bold = font();
  bold.setBold(true);
  bold.setPointSizeF(font().pointSizeF() * 0.85);
  m_status->setFont(bold);
  m_status->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(sc.text.name()));
  QFont small = font();
  small.setPointSizeF(font().pointSizeF() * 0.75);
  m_job->setFont(small);
  m_job->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(sc.muted.name()));
  QFont valueFont = font();
  valueFont.setBold(true);
  valueFont.setPointSizeF(font().pointSizeF() * 0.95);
  for (Value const &value : std::as_const(m_values)) {
    value.value->setFont(valueFont);
    value.value->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(sc.text.name()));
    for (QLabel *label : {value.unit, value.setpoint}) {
      label->setFont(small);
      label->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(sc.muted.name()));
    }
  }
  updateStatus();
  updateValues();
  busy = false;
}

void SidebarStatus::updateStatus() {
  // the connection first: the chamber's status means something only while the application talks to it
  theme::Colors const              &c = theme::colors();
  StatusManager::AppState const     appState{m_resHolder->status->getAppState()};
  bool const                        online{(StatusManager::AppState::ONLINE == appState) || (StatusManager::AppState::DUMMY == appState)};
  HardwareStatus::HardwareStatusEnum const status{m_resHolder->hard_status->getHardwareStatus()};
  QString text;
  QColor  color;
  if (online) {
    text  = hardwareStatusList().value(static_cast<int>(status));
    color = statusColor(status);
  } else if ((StatusManager::AppState::CONNECTING == appState) || (StatusManager::AppState::CONNECTED == appState)) {
    text  = tr("Connecting");
    color = c.warning;
  } else {
    text  = hardwareStatusList().value(static_cast<int>(HardwareStatus::NONE));
    color = (StatusManager::AppState::OFFLINE == appState) ? c.danger : c.off;
  }
  m_status->setText(QStringLiteral("<span style='color:%1'>&#9679;</span> %2").arg(color.name(), text.toHtmlEscaped()));

  QString job;
  if (online && m_resHolder->states->contains("work_type")) {
    int const jobType{static_cast<int>(m_resHolder->states->getSysValue("work_type")->getValue())};
    job = (jobType == 1)   ? QCoreApplication::translate("deepvaccontrol::StatusLabel", "(setpoint)")
          : (jobType == 2) ? QCoreApplication::translate("deepvaccontrol::StatusLabel", "(program)")
                           : QString{};
  }
  m_job->setText(job);
  m_job->setVisible(!job.isEmpty());
}

void SidebarStatus::updateValues() {
  // not connected: the values are not the chamber's (zeros or the last ones received), a dash instead
  StatusManager::AppState const appState{m_resHolder->status->getAppState()};
  bool const online{(StatusManager::AppState::ONLINE == appState) || (StatusManager::AppState::DUMMY == appState)};
  auto state = [this](QString const &name) -> SystemValue const * {
    return m_resHolder->states->contains(name) ? m_resHolder->states->getSysValue(name) : nullptr;
  };
  for (Value const &block : std::as_const(m_values)) {
    bool const         isPressure{block.name == QLatin1String("pres")};
    SystemValue const *value = state(block.name);
    // pressure is always shown (a dash without a gauge), as on Home
    bool const shownBlock{value || isPressure};
    block.box->setVisible(shownBlock);
    if (!shownBlock) {
      continue;
    }
    // pressure in Torr, whatever unit the chamber reports
    auto shown = [&](SystemValue const *shownValue) {
      if (!online || !shownValue) {
        return QStringLiteral("—");
      }
      return isPressure ? pressure::text(pressure::toTorr(shownValue->getValue(), value ? value->getLabel() : QString{})) : number(shownValue);
    };
    block.value->setText(shown(value));
    block.unit->setText(unitOf(block.name));
    SystemValue const *setpoint = state(block.name + "_ref");
    block.setpoint->setVisible(setpoint || isPressure);
    block.setpoint->setText(QStringLiteral("→ %1").arg(shown(setpoint)));
  }
}

void SidebarStatus::changeEvent(QEvent *event) {
  QWidget::changeEvent(event);
  if (event->type() == QEvent::PaletteChange) {
    restyle();
  }
}

SidebarFooter::SidebarFooter(QWidget *parent) : QWidget{parent} {
  auto *lay = new QVBoxLayout(this);
  lay->setContentsMargins(0, px(2), 0, 0);
  lay->setSpacing(px(6));
  m_clock = centred(this);
  m_clock->setWordWrap(false);
  // the product name only: a logo is to come in its place
  m_brand = centred(this);
  m_brand->setWordWrap(false);
  m_brand->setText(QStringLiteral("Deepvac"));
  lay->addWidget(m_clock);
  lay->addWidget(m_brand);

  auto *timer = new QTimer(this);
  connect(timer, &QTimer::timeout, this, [this] {
    QString const now = QDateTime::currentDateTime().toString("HH:mm:ss");
    if (now != m_clock->text()) {
      m_clock->setText(now);
    }
  });
  timer->start(250);
  m_clock->setText(QDateTime::currentDateTime().toString("HH:mm:ss"));
  restyle();
}

void SidebarFooter::restyle() {
  static bool busy{false}; // style sheet changes may raise a palette change again
  if (busy) {
    return;
  }
  busy                    = true;
  theme::Colors const &sc = theme::sidebarColors();
  QFont                clockFont = font();
  clockFont.setPointSizeF(font().pointSizeF() * 0.95);
  m_clock->setFont(clockFont);
  m_clock->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(sc.text.name()));
  QFont brandFont = font();
  brandFont.setBold(true);
  brandFont.setPointSizeF(font().pointSizeF() * 0.95);
  m_brand->setFont(brandFont);
  m_brand->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(sc.text.name()));
  busy = false;
}

void SidebarFooter::changeEvent(QEvent *event) {
  QWidget::changeEvent(event);
  if (event->type() == QEvent::PaletteChange) {
    restyle();
  }
}

}
