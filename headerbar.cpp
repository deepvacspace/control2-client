#include "headerbar.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QResizeEvent>
#include <QToolButton>
#include <QVBoxLayout>
#include <QTimer>
#include <cmath>

#include "constants.h"
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
  return QString::number(v, 'f', static_cast<int>(value->getPrec()));
}

QString unitOf(QString const &name) {
  if (name == QLatin1String("temp")) {
    return QStringLiteral(" °C");
  }
  if (name == QLatin1String("hum")) {
    return QStringLiteral(" %");
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
} // namespace

HeaderBar::HeaderBar(ResHolder *resHolder, QWidget *parent) : QWidget{parent}, m_resHolder{resHolder} {
  setFixedHeight(px(38));
  auto *outer = new QVBoxLayout(this);
  outer->setContentsMargins(0, 0, 0, 0);
  m_content = new QWidget(this);
  outer->addWidget(m_content);
  auto *lay = new QHBoxLayout(m_content);
  lay->setContentsMargins(px(14), 0, px(14), 0);
  lay->setSpacing(px(10));

  // hide / unhide toggle in the middle of the bar (positioned in placeToggle)
  m_toggle = new QToolButton(this);
  m_toggle->setObjectName("barToggle");
  m_toggle->setFocusPolicy(Qt::NoFocus);
  m_toggle->setFixedSize(px(30), px(14));
  connect(m_toggle, &QToolButton::clicked, this, [this] { setCollapsed(!m_collapsed); });

  m_brand = new QLabel(QStringLiteral("Deepvac Control"), m_content);
  m_brand->setObjectName("headerBrand");
  m_statusDot = new QLabel(m_content);
  m_statusDot->setFixedSize(px(8), px(8));
  m_status = new QLabel(m_content);
  m_values = new QLabel(m_content);
  m_values->setTextFormat(Qt::RichText);
  m_user     = new QLabel(m_resHolder->users->getCurrentUserName(), m_content);
  m_appState = new QLabel(m_content);
  m_usb      = new QLabel(QStringLiteral("USB"), m_content);
  m_opc      = new QLabel(m_content);
  m_clock    = new QLabel(m_content);
  for (QLabel *badge : {m_appState, m_usb, m_opc}) {
    badge->setAlignment(Qt::AlignCenter);
  }

  auto *separator = new QWidget(m_content);
  separator->setObjectName("headerSeparator");
  separator->setFixedSize(px(1), px(18));

  lay->addWidget(m_brand);
  lay->addWidget(separator);
  lay->addSpacing(px(30));
  lay->addWidget(m_statusDot);
  lay->addWidget(m_status);
  lay->addSpacing(px(30));
  lay->addWidget(m_values);
  lay->addStretch();
  lay->addWidget(m_user);
  lay->addSpacing(px(8));
  lay->addWidget(m_appState);
  lay->addWidget(m_usb);
  lay->addWidget(m_opc);
  lay->addSpacing(px(30));
  lay->addWidget(m_clock);

  // clock
  auto *timer = new QTimer(this);
  connect(timer, &QTimer::timeout, this, [this] {
    QString const now = QDateTime::currentDateTime().toString("HH:mm:ss");
    if (now != m_clock->text()) {
      m_clock->setText(now);
    }
  });
  timer->start(250);
  m_clock->setText(QDateTime::currentDateTime().toString("HH:mm:ss"));

  connect(m_resHolder->hard_status, &HardwareStatus::statusChanged, this, &HeaderBar::updateStatus);
  if (m_resHolder->states->contains("work_type")) {
    connect(m_resHolder->states->getSysValue("work_type"), &SystemValue::valueChanged, this, &HeaderBar::updateStatus);
  }
  connect(m_resHolder->states, &SystemValueList::ValuesUpdated, this, &HeaderBar::updateValues);
  connect(m_resHolder->status, &StatusManager::appStateChanged, this, &HeaderBar::updateBadges);
  connect(m_resHolder->status, &StatusManager::opcStateChanged, this, [this](int state) {
    m_opcState = state;
    updateBadges();
  });
  connect(m_resHolder->users, &AccessManager::userSelected, this, [this](QString const &name, AccessManager::AccessLevel) {
    m_user->setText(name);
  });
  connect(m_resHolder->usb, &UsbManager::drivesUpdated, this, [this](QStringList drives) {
    m_usbPresent = !drives.isEmpty();
    updateBadges();
  });
  m_usbPresent = !m_resHolder->usb->getAvailableDrives().isEmpty();

  for (QLabel *label : findChildren<QLabel *>()) {
    label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed); // badges keep their height
  }
  restyle();
  setCollapsed(false);
}

void HeaderBar::restyle() {
  static bool busy{false}; // style sheet changes may raise a palette change again
  if (busy) {
    return;
  }
  busy                   = true;
  theme::Colors const &c = theme::colors();
  QFont                brandFont = font();
  brandFont.setBold(true);
  brandFont.setPointSizeF(font().pointSizeF() * 1.35);
  m_brand->setFont(brandFont);
  m_brand->setStyleSheet(QStringLiteral("color: %1; border-bottom: %2px solid %3; padding-bottom: %4px;")
                           .arg(c.text.name())
                           .arg(px(2))
                           .arg(c.accent.name())
                           .arg(px(1)));
  findChild<QWidget *>("headerSeparator")->setStyleSheet(QStringLiteral("background: %1;").arg(c.border.name()));
  QFont bold = font();
  bold.setBold(true);
  bold.setPointSizeF(font().pointSizeF() * 1.1);
  m_status->setFont(bold);
  m_status->setStyleSheet(QStringLiteral("color: %1;").arg(c.text.name()));
  m_user->setStyleSheet(QStringLiteral("color: %1;").arg(c.muted.name()));
  m_clock->setStyleSheet(QStringLiteral("color: %1;").arg(c.text.name()));
  QFont clockFont = font();
  clockFont.setPointSizeF(font().pointSizeF() * 1.15);
  m_clock->setFont(clockFont);
  m_toggle->setStyleSheet(QStringLiteral("QToolButton#barToggle { color: %1; background: %2; border: %3px solid %4; border-radius: %5px; "
                                  "font-weight: bold; padding: 0; } QToolButton#barToggle:hover { color: %6; border-color: %7; }")
                     .arg(c.muted.name(), c.panel.name())
                     .arg(px(1))
                     .arg(c.border.name())
                     .arg(px(4))
                     .arg(c.text.name(), c.accent.name()));
  updateStatus();
  updateValues();
  updateBadges();
  update();
  busy = false;
}

void HeaderBar::updateStatus() {
  auto const status = m_resHolder->hard_status->getHardwareStatus();
  QString    text   = hardwareStatusList().value(static_cast<int>(status));
  if (m_resHolder->states->contains("work_type")) {
    int const jobType{static_cast<int>(m_resHolder->states->getSysValue("work_type")->getValue())};
    if (jobType == 1) {
      text += QStringLiteral(" ") + QCoreApplication::translate("deepvaccontrol::StatusLabel", "по уставке");
    } else if (jobType == 2) {
      text += QStringLiteral(" ") + QCoreApplication::translate("deepvaccontrol::StatusLabel", "по программе");
    }
  }
  m_status->setText(text);
  m_statusDot->setStyleSheet(
    QStringLiteral("background: %1; border-radius: %2px;").arg(statusColor(status).name()).arg(px(4)));
}

void HeaderBar::updateValues() {
  theme::Colors const &c = theme::colors();
  QStringList          parts;
  for (auto const &[name, label] : statusBarStatesList()) {
    if (!m_resHolder->states->contains(name)) {
      continue;
    }
    SystemValue const *value = m_resHolder->states->getSysValue(name);
    QString            part  = QStringLiteral("<span style='font-weight:bold; font-size:%1pt; color:%2'>%3%4</span>")
                     .arg(font().pointSizeF() * 1.3)
                     .arg(c.text.name(), number(value), unitOf(name));
    if (m_resHolder->states->contains(name + "_ref")) {
      part += QStringLiteral("&nbsp;&nbsp;<span style='color:%1'>[%2]</span>")
                .arg(c.muted.name(), number(m_resHolder->states->getSysValue(name + "_ref")));
    }
    parts << part;
  }
  m_values->setText(parts.join(QStringLiteral("&nbsp;&nbsp;&nbsp;&nbsp;")));
}

void HeaderBar::updateBadges() {
  theme::Colors const &c     = theme::colors();
  auto                 badge = [&](QLabel *label, QString const &text, QColor const &color) {
    label->setText(text);
    label->setStyleSheet(QStringLiteral("color: %1; border: %2px solid %1; border-radius: %3px; padding: %4px %5px; "
                                                        "background: %6;")
                           .arg(color.name())
                           .arg(px(1))
                           .arg(px(3))
                           .arg(px(1))
                           .arg(px(6))
                           .arg(QColor(color.red(), color.green(), color.blue(), 28).name(QColor::HexArgb)));
  };
  auto const appState = m_resHolder->status->getAppState();
  QString    stateText = APP_STATE_LIST.value(static_cast<int>(appState));
  badge(m_appState, stateText, appState == StatusManager::AppState::ONLINE ? c.active : (appState == StatusManager::AppState::OFFLINE ? c.danger : c.warning));
  m_usb->setVisible(m_usbPresent);
  badge(m_usb, QStringLiteral("USB"), c.ready);
  QString const opcText = (m_opcState == 0) ? QStringLiteral("OPC OK") : (m_opcState == -1 ? QStringLiteral("OPC ERR") : QStringLiteral("OPC OFF"));
  badge(m_opc, opcText, (m_opcState == 0) ? c.ready : (m_opcState == -1 ? c.danger : c.off));
}

void HeaderBar::setCollapsed(bool collapsed) {
  m_collapsed = collapsed;
  m_content->setVisible(!collapsed);
  setFixedHeight(collapsed ? px(16) : px(38));
  m_toggle->setText(collapsed ? QStringLiteral("▾") : QStringLiteral("▴"));
  m_toggle->setToolTip(collapsed ? tr("Show the header") : tr("Hide the header"));
  placeToggle();
}

void HeaderBar::placeToggle() {
  m_toggle->move((width() - m_toggle->width()) / 2, (height() - m_toggle->height()) / 2);
  m_toggle->raise();
}

void HeaderBar::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  placeToggle();
}

void HeaderBar::paintEvent(QPaintEvent *) {
  theme::Colors const &c = theme::colors();
  QPainter             p(this);
  p.fillRect(rect(), c.header);
  p.setPen(c.border);
  p.drawLine(rect().bottomLeft(), rect().bottomRight());
}

void HeaderBar::changeEvent(QEvent *event) {
  QWidget::changeEvent(event);
  if (event->type() == QEvent::PaletteChange) {
    restyle();
  }
}

} // namespace deepvaccontrol
