#include "lightbutton.h"

#include <QPainter>

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
  setFixedSize(92 * utils::ssf(), 34 * utils::ssf());

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
  theme::Colors const &c = theme::colors();
  double const         s{utils::ssf()};
  QPainter             p(this);
  p.setRenderHint(QPainter::Antialiasing);
  QRectF const box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
  p.setPen(QPen(isDown() ? c.accent : c.border, 1 * s));
  p.setBrush(c.panel);
  p.drawRoundedRect(box, 5 * s, 5 * s);
  // lamp
  QPointF const lamp(box.left() + 13 * s, box.center().y());
  if (m_enabled) {
    QRadialGradient glow(lamp, 9 * s);
    glow.setColorAt(0, QColor(250, 204, 21, 120));
    glow.setColorAt(1, QColor(250, 204, 21, 0));
    p.setPen(Qt::NoPen);
    p.setBrush(glow);
    p.drawEllipse(lamp, 9 * s, 9 * s);
  }
  p.setPen(Qt::NoPen);
  p.setBrush(m_enabled ? QColor("#fde68a") : c.off);
  p.drawEllipse(lamp, 4.5 * s, 4.5 * s);
  // label
  p.setPen(isEnabled() ? c.text : c.muted);
  p.setFont(font());
  p.drawText(QRectF(lamp.x() + 9 * s, box.top(), 40 * s, box.height()), Qt::AlignVCenter | Qt::AlignLeft, text());
  // switch
  QRectF const track(box.right() - 30 * s, box.center().y() - 6 * s, 22 * s, 12 * s);
  p.setBrush(m_enabled ? c.active : c.border);
  if (!isEnabled()) {
    p.setOpacity(0.45);
  }
  p.drawRoundedRect(track, 6 * s, 6 * s);
  p.setBrush(Qt::white);
  p.drawEllipse(QPointF(m_enabled ? track.right() - 6 * s : track.left() + 6 * s, track.center().y()), 4.5 * s, 4.5 * s);
}
}
