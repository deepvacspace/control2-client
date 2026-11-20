#include "headerbar.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>
#include <QTimer>
#include <cmath>
#include <iterator>

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

QString unitOf(QString const &name, SystemValue const *value) {
  if (name == QLatin1String("temp")) {
    return QStringLiteral(" °C");
  }
  if (name == QLatin1String("hum")) {
    return QStringLiteral(" %");
  }
  if (name == QLatin1String("pres")) {
    Q_UNUSED(value)
    return QStringLiteral(" Torr"); // converted from the chamber's unit (pressure::toTorr)
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

// the user's avatar: a person on a disc
QPixmap userAvatar(int size, QColor const &disc, QColor const &person) {
  QPixmap pixmap(size * 2, size * 2);
  pixmap.fill(Qt::transparent);
  pixmap.setDevicePixelRatio(2.0);
  QPainter p(&pixmap);
  p.setRenderHint(QPainter::Antialiasing);
  double const s{static_cast<double>(size)};
  p.setPen(Qt::NoPen);
  p.setBrush(disc);
  p.drawEllipse(QRectF(0, 0, s, s));
  QPainterPath clip;
  clip.addEllipse(QRectF(0, 0, s, s));
  p.setClipPath(clip);
  p.setBrush(person);
  p.drawEllipse(QPointF(s * 0.5, s * 0.38), s * 0.17, s * 0.17);
  p.drawEllipse(QRectF(s * 0.2, s * 0.64, s * 0.6, s * 0.56));
  return pixmap;
}

// a label that shortens its text with "…" rather than being cut when the header is short of room (a long user name)
class ElidedLabel : public QLabel {
public:
  using QLabel::QLabel;
  [[nodiscard]] QSize minimumSizeHint() const override {
    return {fontMetrics().horizontalAdvance(QStringLiteral("Wo…")), QLabel::minimumSizeHint().height()};
  }

protected:
  void paintEvent(QPaintEvent *) override {
    QPainter p(this);
    p.setPen(palette().color(foregroundRole()));
    p.drawText(contentsRect(), Qt::AlignVCenter | Qt::AlignLeft, fontMetrics().elidedText(text(), Qt::ElideRight, contentsRect().width()));
  }
};
}

HeaderBar::HeaderBar(ResHolder *resHolder, QWidget *parent) : QWidget{parent}, m_resHolder{resHolder} {
  setFixedHeight(theme::dp(101));
  auto *outer = new QVBoxLayout(this);
  outer->setContentsMargins(0, 0, 0, 0);
  m_content = new QWidget(this);
  outer->addWidget(m_content);
  auto *lay = new QHBoxLayout(m_content);
  lay->setContentsMargins(theme::dp(32), 0, theme::dp(28), 0);
  lay->setSpacing(theme::dp(16));

  // the product name only: a logo is to come beside it
  m_brand = new QLabel(QStringLiteral("Deepvac"), m_content);
  m_brand->setObjectName("headerBrand");
  m_statusDot = new QLabel(m_content);
  m_statusDot->setFixedSize(theme::dp(16), theme::dp(16));
  m_status   = new QLabel(m_content);
  m_userPill = new QFrame(m_content);
  m_userPill->setObjectName("userPill");
  m_userIcon = new QLabel(m_userPill);
  m_user     = new ElidedLabel(m_resHolder->users->getCurrentUserName(), m_userPill);
  m_user->setToolTip(m_user->text());
  m_appState = new QLabel(m_content);
  m_appState->setTextFormat(Qt::RichText);
  m_usb   = new QLabel(QStringLiteral("USB"), m_content);
  m_opc   = new QLabel(m_content);
  m_clock = new QLabel(m_content);
  for (QLabel *badge : {m_appState, m_usb, m_opc}) {
    badge->setAlignment(Qt::AlignCenter);
    badge->setFixedHeight(theme::dp(52));
  }
  // the clock keeps its width as the seconds change
  m_clock->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  auto *pillLay = new QHBoxLayout(m_userPill);
  pillLay->setContentsMargins(theme::dp(8), theme::dp(8), theme::dp(20), theme::dp(8));
  pillLay->setSpacing(theme::dp(12));
  pillLay->addWidget(m_userIcon);
  pillLay->addWidget(m_user);
  m_userPill->setFixedHeight(theme::dp(60));

  lay->addWidget(m_brand);
  lay->addWidget(separator());
  auto *statusLay = new QHBoxLayout();
  statusLay->setSpacing(theme::dp(12));
  statusLay->addWidget(m_statusDot);
  statusLay->addWidget(m_status);
  lay->addLayout(statusLay);
  // the values with their setpoints, each over its caption; shown or hidden by updateValues()
  for (auto const &[name, label] : statusBarStatesList()) {
    ValueBlock block;
    block.name      = name;
    block.separator = separator();
    block.box       = new QWidget(m_content);
    auto *boxLay    = new QVBoxLayout(block.box);
    boxLay->setContentsMargins(0, 0, 0, 0);
    boxLay->setSpacing(0);
    block.value = new QLabel(block.box);
    block.value->setTextFormat(Qt::RichText);
    block.caption = new QLabel(label, block.box);
    boxLay->addStretch();
    boxLay->addWidget(block.value);
    boxLay->addWidget(block.caption);
    boxLay->addStretch();
    lay->addWidget(block.separator);
    lay->addWidget(block.box);
    m_valueBlocks.push_back(block);
  }
  lay->addStretch();
  lay->addWidget(m_userPill);
  lay->addWidget(m_appState);
  lay->addWidget(m_opc);
  lay->addSpacing(theme::dp(8));
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
  connect(m_resHolder->status, &StatusManager::appStateChanged, this, &HeaderBar::updateValues);
  connect(m_resHolder->status, &StatusManager::opcStateChanged, this, [this](int state) {
    m_opcState = state;
    updateBadges();
  });
  connect(m_resHolder->users, &AccessManager::userSelected, this, [this](QString const &name, AccessManager::AccessLevel) {
    m_user->setText(name);
    m_user->setToolTip(name);
  });
  connect(m_resHolder->usb, &UsbManager::drivesUpdated, this, [this](QStringList drives) {
    m_usbPresent = !drives.isEmpty();
    updateBadges();
  });
  m_usbPresent = !m_resHolder->usb->getAvailableDrives().isEmpty();
  m_opcState   = m_resHolder->status->getOpcStatus();

  for (QLabel *label : findChildren<QLabel *>()) {
    label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed); // badges keep their height
  }
  // the user name gives way first when the header is short of room
  m_user->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  m_userPill->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  restyle();
  setFixedHeight(theme::dp(101));
}

QWidget *HeaderBar::separator() {
  auto *line = new QWidget(m_content);
  line->setObjectName("headerSeparator");
  line->setFixedSize(theme::dp(1), theme::dp(48));
  return line;
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
  brandFont.setPointSizeF(font().pointSizeF() * 1.5);
  m_brand->setFont(brandFont);
  m_brand->setStyleSheet(QStringLiteral("color: %1;").arg(c.text.name()));
  for (QWidget *line : findChildren<QWidget *>("headerSeparator")) {
    line->setStyleSheet(QStringLiteral("background: %1;").arg(c.border.name()));
  }
  QFont bold = font();
  bold.setBold(true);
  bold.setPointSizeF(font().pointSizeF() * 1.05);
  m_status->setFont(bold);
  m_status->setStyleSheet(QStringLiteral("color: %1;").arg(c.text.name()));
  QFont small = font();
  small.setPointSizeF(font().pointSizeF() * 0.85);
  for (ValueBlock const &block : m_valueBlocks) {
    block.caption->setFont(small);
    block.caption->setStyleSheet(QStringLiteral("color: %1;").arg(c.muted.name()));
  }
  m_userPill->setStyleSheet(QStringLiteral("QFrame#userPill { background: %1; border: %2px solid %3; border-radius: %4px; }")
                              .arg(c.panelAlt.name())
                              .arg(theme::dp(1))
                              .arg(c.border.name())
                              .arg(m_userPill->height() / 2));
  int const avatar{theme::dp(37)};
  m_userIcon->setFixedSize(avatar, avatar);
  m_userIcon->setPixmap(userAvatar(avatar, c.text, c.panel));
  QFont userFont = font();
  userFont.setWeight(QFont::DemiBold);
  m_user->setFont(userFont);
  m_user->setStyleSheet(QStringLiteral("color: %1; background: transparent; border: none;").arg(c.text.name()));
  m_clock->setStyleSheet(QStringLiteral("color: %1;").arg(c.text.name()));
  QFont clockFont = font();
  clockFont.setPointSizeF(font().pointSizeF() * 1.25);
  m_clock->setFont(clockFont);
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
      text += QStringLiteral(" ") + QCoreApplication::translate("deepvaccontrol::StatusLabel", "(setpoint)");
    } else if (jobType == 2) {
      text += QStringLiteral(" ") + QCoreApplication::translate("deepvaccontrol::StatusLabel", "(program)");
    }
  }
  m_status->setText(text);
  m_statusDot->setStyleSheet(
    QStringLiteral("background: %1; border-radius: %2px;").arg(statusColor(status).name()).arg(m_statusDot->width() / 2));
}

void HeaderBar::updateValues() {
  theme::Colors const &c = theme::colors();
  // not connected: the values are not the chamber's (zeros or the last ones received), a dash instead (with its unit)
  StatusManager::AppState const appState{m_resHolder->status->getAppState()};
  bool const online{(StatusManager::AppState::ONLINE == appState) || (StatusManager::AppState::DUMMY == appState)};
  auto state = [this](QString const &name) -> SystemValue const * {
    return m_resHolder->states->contains(name) ? m_resHolder->states->getSysValue(name) : nullptr;
  };
  for (ValueBlock const &block : m_valueBlocks) {
    bool const         isPressure{block.name == QLatin1String("pres")};
    SystemValue const *value = state(block.name);
    // pressure is always shown (a dash without a gauge), as on Home
    bool const shownBlock{value || isPressure};
    block.box->setVisible(shownBlock);
    block.separator->setVisible(shownBlock);
    if (!shownBlock) {
      continue;
    }
    QString const unit = unitOf(block.name, value);
    // pressure in Torr, whatever unit the chamber reports; a dash when there is none
    auto shown = [&](SystemValue const *shownValue) {
      if (!online || !shownValue) {
        return QStringLiteral("—");
      }
      return isPressure ? pressure::text(pressure::toTorr(shownValue->getValue(), value ? value->getLabel() : QString{})) : number(shownValue);
    };
    QString text = QStringLiteral("<span style='font-weight:bold; font-size:%1pt; color:%2'>%3%4</span>")
                     .arg(font().pointSizeF() * 1.2)
                     .arg(c.text.name(), shown(value), unit);
    SystemValue const *setpoint = state(block.name + "_ref");
    if (setpoint || isPressure) {
      // the pressure setpoint with its unit too
      text += QStringLiteral("&nbsp;&nbsp;<span style='color:%1'>[%2%3]</span>").arg(c.muted.name(), shown(setpoint), isPressure ? unit : QString{});
    }
    block.value->setText(text);
  }
}

void HeaderBar::updateBadges() {
  theme::Colors const &c = theme::colors();
  // rounded pills; a state is tinted with its colour, the connection one has a dot before its text
  auto badge = [&](QLabel *label, QString const &text, QColor const &color, bool tinted, bool dot) {
    label->setText(dot ? QStringLiteral("<span style='font-size:%1pt'>&#9679;</span>&nbsp;&nbsp;%2").arg(font().pointSizeF() * 0.9).arg(text)
                       : text);
    label->setStyleSheet(QStringLiteral("color: %1; border: %2px solid %3; border-radius: %4px; padding: 0 %5px; background: %6; "
                                        "font-weight: %7;")
                           .arg(color.name())
                           .arg(theme::dp(1))
                           .arg(tinted ? QColor(color.red(), color.green(), color.blue(), 110).name(QColor::HexArgb) : c.border.name())
                           .arg(label->height() / 2)
                           .arg(theme::dp(18))
                           .arg(tinted ? QColor(color.red(), color.green(), color.blue(), 34).name(QColor::HexArgb) : c.panel.name())
                           .arg(tinted ? 600 : 400));
  };
  // the state's name in the interface language (APP_STATE_LIST holds the identifiers), in its order
  static char const *const STATE_TEXTS[]{QT_TR_NOOP("IDLE"),    QT_TR_NOOP("CONNECTING"), QT_TR_NOOP("CONNECTED"),
                                         QT_TR_NOOP("ONLINE"),  QT_TR_NOOP("OFFLINE"),    QT_TR_NOOP("DUMMY"),
                                         QT_TR_NOOP("WAIT_IN_DEBUG"), QT_TR_NOOP("ERROR")};
  auto const appState = m_resHolder->status->getAppState();
  int const  shown{static_cast<int>(appState == StatusManager::AppState::CONNECTED ? StatusManager::AppState::CONNECTING : appState)}; // until the chamber answers
  QString    stateText = ((shown >= 0) && (shown < static_cast<int>(std::size(STATE_TEXTS)))) ? tr(STATE_TEXTS[shown]) : APP_STATE_LIST.value(shown);
  badge(m_appState,
        stateText,
        appState == StatusManager::AppState::ONLINE ? c.active : (appState == StatusManager::AppState::OFFLINE ? c.danger : c.warning),
        true,
        true);
  m_usb->setVisible(false); // removed from the design; drives are still offered as export locations
  // OPC OK only while the OPC UA server broadcasts (0); stopped (-2) or failed to start (an error code): OPC OFF,
  // a plain outlined pill
  bool const broadcasting{m_opcState == 0};
  badge(m_opc, broadcasting ? tr("OPC OK") : tr("OPC OFF"), broadcasting ? c.ready : c.muted, broadcasting, false);
  m_opc->setToolTip((broadcasting || (m_opcState == -2)) ? QString{} : tr("The OPC UA server could not start (code %1)").arg(m_opcState));
}

void HeaderBar::paintEvent(QPaintEvent *) {
  theme::Colors const &c = theme::colors();
  QPainter             p(this);
  p.setRenderHint(QPainter::Antialiasing);
  // a rounded card, as the pages' cards
  double const radius{12 * utils::ssf()};
  p.setPen(QPen(c.border, theme::dp(1)));
  p.setBrush(c.panel);
  p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
}

void HeaderBar::changeEvent(QEvent *event) {
  QWidget::changeEvent(event);
  if (event->type() == QEvent::PaletteChange) {
    restyle();
  }
}

}
