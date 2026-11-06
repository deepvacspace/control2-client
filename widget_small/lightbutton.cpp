#include "lightbutton.h"

#include <QPainter>
#include <QtMath>
#include <cmath>

#include "constants.h"
#include "theme.h"
#include "utils.h"

namespace deepvaccontrol {
LightButton::LightButton(ResHolder *resHolder, QWidget *parent) : QPushButton{parent}, m_resHolder{resHolder} {
  // the configured light output, if any
  for (QVariant const &entry : m_resHolder->settings->settingsMap().value("monitor").toList()) {
    QVariantMap const item  = entry.toMap();
    QString const     label = item.value("label").toString().toLower();
    QString const     name  = item.value("name").toString().isEmpty() ? QStringLiteral("outputs") : item.value("name").toString();
    if (((label == QLatin1String("light")) || label.contains(QLatin1String("chamber light"))) && m_resHolder->states->contains(name)) {
      m_bit    = item.value("bit").toInt();
      m_sysVal = m_resHolder->states->getSysValue(name);
      break;
    }
  }
  setText(tr("Light"));
  setFocusPolicy(Qt::NoFocus);
  setFixedSize(62 * utils::ssf(), 46 * utils::ssf());

  connect(this, &QPushButton::clicked, this, [this] {
    m_resHolder->tcpGate->sendPair("light", static_cast<float>(!m_enabled));
  });

  if (m_sysVal) {
    connect(m_sysVal, &SystemValue::valueChanged, this, [this](float value) {
      bool const newValue{isEnabled() && utils::getBitOfFloat(value, static_cast<quint8>(m_bit))};
      if (newValue == m_enabled) {
        return;
      }
      m_enabled = newValue;
      update();
    });
  }
  connect(m_resHolder->status, &StatusManager::appStateChanged, this, &LightButton::updateAvailability);
  updateAvailability();
}

void LightButton::updateAvailability() {
  bool const available{m_sysVal && (m_resHolder->status->getAppState() == StatusManager::AppState::ONLINE)};
  setEnabled(available);
  setToolTip(m_sysVal ? (available ? QString() : tr("The chamber is not online")) : tr("No chamber light is configured"));
  if (!available && m_enabled) {
    m_enabled = false;
  }
  update();
}

void LightButton::paintEvent(QPaintEvent *) {
  theme::Colors const &sc = theme::sidebarColors(); // drawn on the navigation sidebar
  double const         s{utils::ssf()};
  QPainter             p(this);
  p.setRenderHint(QPainter::Antialiasing);
  QRectF const box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
  if (isDown()) {
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 18));
    p.drawRoundedRect(box, 9 * s, 9 * s);
  }
  QColor const sun = m_enabled ? QColor("#facc15") : (isEnabled() ? sc.text : sc.muted);
  // sun
  QPointF const centre(box.left() + 12 * s, box.top() + 14 * s);
  if (m_enabled) {
    QRadialGradient glow(centre, 11 * s);
    glow.setColorAt(0, QColor(250, 204, 21, 110));
    glow.setColorAt(1, QColor(250, 204, 21, 0));
    p.setPen(Qt::NoPen);
    p.setBrush(glow);
    p.drawEllipse(centre, 11 * s, 11 * s);
  }
  p.setPen(QPen(sun, 1.4 * s, Qt::SolidLine, Qt::RoundCap));
  p.setBrush(m_enabled ? sun : Qt::NoBrush);
  p.drawEllipse(centre, 3.6 * s, 3.6 * s);
  for (int i{0}; i < 8; ++i) {
    double const angle{i * M_PI / 4};
    QPointF const dir(std::cos(angle), std::sin(angle));
    p.drawLine(centre + dir * 6 * s, centre + dir * 8.5 * s);
  }
  // switch
  if (!isEnabled()) {
    p.setOpacity(0.6);
  }
  QRectF const track(box.right() - 32 * s, centre.y() - 7.5 * s, 30 * s, 15 * s);
  p.setPen(Qt::NoPen);
  p.setBrush(m_enabled ? sc.active : QColor("#c8d2e0"));
  p.drawRoundedRect(track, 7.5 * s, 7.5 * s);
  p.setBrush(Qt::white);
  p.drawEllipse(QPointF(m_enabled ? track.right() - 7.5 * s : track.left() + 7.5 * s, track.center().y()), 5.5 * s, 5.5 * s);
  p.setOpacity(1.0);
  // label
  QFont small = font();
  small.setPointSizeF(font().pointSizeF() * 0.9);
  p.setFont(small);
  p.setPen(isEnabled() ? sc.text : sc.muted);
  p.drawText(QRectF(box.left(), centre.y() + 10 * s, box.width(), box.bottom() - centre.y() - 10 * s), Qt::AlignHCenter | Qt::AlignVCenter, text());
}
}
