#include "sidebarstatus.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QEvent>
#include <QLabel>
#include <QPixmap>
#include <QResizeEvent>
#include <QTextDocument>
#include <QTimer>
#include <QVBoxLayout>
#include <cmath>

#include "constants.h"
#include "pressureunits.h"
#include "theme.h"
#include "utils.h"

namespace deepvaccontrol {

namespace {
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
  theme::Colors const &c = theme::sidebarColors(); // on the sidebar
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

// from the box's left edge
QLabel *leftAligned(QWidget *parent) {
  auto *label = new QLabel(parent);
  label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
  return label;
}

QWidget *separator(QWidget *parent) {
  auto *line = new QWidget(parent);
  line->setObjectName("menuSeparator"); // styled by the menu
  line->setFixedHeight(theme::dp(1));
  return line;
}
}

SidebarStatus::SidebarStatus(ResHolder *resHolder, QWidget *parent) : QWidget{parent}, m_resHolder{resHolder} {
  // in a rounded box outlined in Sky: the status, then each value under its unit and setpoint ("°C → 24.0"), lines between
  // them
  auto *outer = new QVBoxLayout(this);
  outer->setContentsMargins(0, 0, 0, 0);
  auto *box = new QFrame(this);
  box->setObjectName("sidebarStatusBox");
  outer->addWidget(box);
  auto *lay = new QVBoxLayout(box);
  lay->setContentsMargins(theme::dp(12), theme::dp(12), theme::dp(12), theme::dp(16));
  lay->setSpacing(theme::dp(8));
  m_status = leftAligned(box);
  m_status->setTextFormat(Qt::RichText);
  m_job = leftAligned(box);
  lay->addWidget(m_status);
  lay->addWidget(m_job);
  for (auto const &[name, label] : statusBarStatesList()) {
    Value value;
    value.name = name;
    value.box  = new QWidget(box);
    value.box->setToolTip(label);
    auto *boxLay = new QVBoxLayout(value.box);
    boxLay->setContentsMargins(0, 0, 0, 0);
    boxLay->setSpacing(theme::dp(8));
    boxLay->addWidget(separator(value.box));
    value.setpoint = leftAligned(value.box);
    value.value    = leftAligned(value.box);
    boxLay->addWidget(value.setpoint);
    boxLay->addWidget(value.value);
    lay->addWidget(value.box);
    m_values.push_back(value);
  }

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
  // a shade lighter than the sidebar, outlined in Sky; the lines between the values a faint Sky
  setStyleSheet(QStringLiteral(R"(
QFrame#sidebarStatusBox { background: %1; border: %2px solid %3; border-radius: %4px; }
QWidget#menuSeparator { background: %5; }
)")
                  .arg(sc.panel.name())
                  .arg(qMax(2, theme::dp(2)))
                  .arg(sc.accent.name())
                  .arg(theme::radius())
                  .arg(QColor(sc.accent.red(), sc.accent.green(), sc.accent.blue(), 90).name(QColor::HexArgb)));
  fitStatus(); // its font
  m_status->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(sc.text.name()));
  QFont small = font();
  small.setPointSizeF(font().pointSizeF() * 0.6);
  m_job->setFont(small);
  m_job->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(sc.muted.name()));
  // the values large, their unit and setpoint in Sky
  QFont valueFont = font();
  valueFont.setBold(true);
  valueFont.setPointSizeF(font().pointSizeF() * 0.75);
  QFont unitFont = font();
  unitFont.setPointSizeF(font().pointSizeF() * 0.6);
  for (Value const &value : std::as_const(m_values)) {
    value.value->setFont(valueFont);
    value.value->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(sc.text.name()));
    value.setpoint->setFont(unitFont);
    value.setpoint->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(sc.accent.name()));
  }
  updateStatus();
  updateValues();
  busy = false;
}

void SidebarStatus::updateStatus() {
  // the connection first: the chamber's status means something only while the application talks to it
  theme::Colors const              &c = theme::sidebarColors();
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
  m_status->setText(QStringLiteral("<span style='color:%1; font-size:large'>&#9679;</span>&nbsp;%2").arg(color.name(), text.toHtmlEscaped()));

  QString job;
  if (online && m_resHolder->states->contains("work_type")) {
    int const jobType{static_cast<int>(m_resHolder->states->getSysValue("work_type")->getValue())};
    job = (jobType == 1)   ? QCoreApplication::translate("deepvaccontrol::StatusLabel", "(setpoint)")
          : (jobType == 2) ? QCoreApplication::translate("deepvaccontrol::StatusLabel", "(program)")
                           : QString{};
  }
  m_job->setText(job);
  m_job->setVisible(!job.isEmpty());
  fitStatus();
}

void SidebarStatus::fitStatus() {
  // a long status ("Connecting", or another language's) in smaller letters instead of cut off at the box's edge
  QFont        font = this->font();
  double const base{font.pointSizeF() * 0.75};
  font.setBold(true);
  font.setPointSizeF(base);
  if (m_status->width() > 0) {
    QTextDocument text; // measured apart from the label: its font is set once, to the size that fits
    text.setDocumentMargin(0);
    text.setHtml(m_status->text());
    for (double scale{1.0}; scale >= 0.7; scale -= 0.05) {
      font.setPointSizeF(base * scale);
      text.setDefaultFont(font);
      if (text.idealWidth() <= m_status->contentsRect().width()) {
        break;
      }
    }
  }
  if (m_status->font() != font) {
    m_status->setFont(font);
  }
}

void SidebarStatus::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  fitStatus();
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
    // "°C → 24.0": the unit, and the setpoint when there is one
    SystemValue const *setpoint = state(block.name + "_ref");
    block.setpoint->setText((setpoint || isPressure) ? QStringLiteral("%1 → %2").arg(unitOf(block.name), shown(setpoint)) : unitOf(block.name));
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
  lay->setContentsMargins(0, 0, 0, 0);
  lay->setSpacing(theme::dp(8));
  m_clock = centred(this);
  m_clock->setWordWrap(false);
  m_brand = centred(this);
  m_brand->setWordWrap(false);
  m_brand->setAccessibleName(QStringLiteral("Deepvac"));
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
  // the wordmark (brand book: 80 px wide at least), sharp on high-density screens
  qreal const dpr{devicePixelRatioF()};
  QPixmap     logo = QPixmap(QStringLiteral(":/imgs/deepvac-logo-white.png"))
                   .scaledToWidth(qRound(theme::dp(120) * dpr), Qt::SmoothTransformation);
  logo.setDevicePixelRatio(dpr);
  m_brand->setPixmap(logo);
  m_brand->setStyleSheet(QStringLiteral("background: transparent;"));
  busy = false;
}

void SidebarFooter::changeEvent(QEvent *event) {
  QWidget::changeEvent(event);
  if (event->type() == QEvent::PaletteChange) {
    restyle();
  }
}

}
