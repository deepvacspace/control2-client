#include "controloutput.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QVBoxLayout>
#include <cmath>

#include "resholder.h"
#include "statusmanager.h"
#include "theme.h"
#include "utils.h"

namespace deepvaccontrol {

namespace {
int px(double value) {
  return qMax(1, static_cast<int>(value * utils::ssf()));
}
}

ControlBar::ControlBar(QWidget *parent) : QWidget{parent} {
  setFixedHeight(px(10));
}

void ControlBar::setValue(double value) {
  if ((value != m_value) && !(std::isnan(value) && std::isnan(m_value))) {
    m_value = value;
    update();
  }
}

void ControlBar::paintEvent(QPaintEvent *) {
  theme::Colors const &c = theme::colors();
  QPainter             p(this);
  p.setRenderHint(QPainter::Antialiasing);
  double const h{static_cast<double>(px(5))};
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

ControlOutputGauge::ControlOutputGauge(ResHolder *resHolder, Kind kind, QWidget *parent)
    : QFrame{parent}, m_resHolder{resHolder}, m_kind{kind} {
  setObjectName("pageCard");
  bool const temperature{kind == Kind::TEMPERATURE};
  auto      *lay = new QVBoxLayout(this);
  lay->setContentsMargins(px(14), px(10), px(14), px(10));
  lay->setSpacing(px(4));
  auto *top   = new QHBoxLayout();
  auto *title = new QLabel(temperature ? tr("Temperature control output") : tr("Pressure control output"), this);
  title->setProperty("muted", true);
  m_value     = new QLabel(this);
  QFont bold  = m_value->font();
  bold.setBold(true);
  bold.setPointSizeF(bold.pointSizeF() * 1.2);
  m_value->setFont(bold);
  top->addWidget(title);
  top->addStretch();
  top->addWidget(m_value);
  lay->addLayout(top);
  m_bar = new ControlBar(this);
  lay->addWidget(m_bar);
  auto  *ends  = new QHBoxLayout();
  QFont  small = title->font();
  small.setPointSizeF(small.pointSizeF() * 0.8);
  for (QString const &text : {temperature ? tr("Cooling") : tr("Pumping"), temperature ? tr("Heating") : tr("Venting")}) {
    auto *label = new QLabel(text, this);
    label->setProperty("muted", true);
    label->setFont(small);
    ends->addWidget(label);
    if (ends->count() == 1) {
      ends->addStretch();
    }
  }
  lay->addLayout(ends);

  connect(m_resHolder->states, &SystemValueList::ValuesUpdated, this, &ControlOutputGauge::refresh);
  if (m_resHolder->status) {
    connect(m_resHolder->status, &StatusManager::appStateChanged, this, &ControlOutputGauge::refresh);
  }
  refresh();
}

void ControlOutputGauge::refresh() {
  theme::Colors const          &c = theme::colors();
  StatusManager::AppState const state{m_resHolder->status ? m_resHolder->status->getAppState() : StatusManager::AppState::OFFLINE};
  bool const online{(StatusManager::AppState::ONLINE == state) || (StatusManager::AppState::DUMMY == state)};
  QString const name = (m_kind == Kind::TEMPERATURE) ? QStringLiteral("temp_u") : QStringLiteral("pres_u");
  double        u{std::numeric_limits<double>::quiet_NaN()};
  if (online && m_resHolder->states->contains(name)) {
    double const v{m_resHolder->states->getSysValue(name)->getValue()};
    u = (std::isnan(v) || (std::fabs(v) > 1e9)) ? u : v;
  }
  // a dash while there is none (not connected, no such output), as every reading
  m_value->setText(std::isnan(u) ? QStringLiteral("— %") : QString::number(u, 'f', 0).replace(QLatin1Char('-'), QChar(0x2212)) + QStringLiteral(" %"));
  QColor const low  = (m_kind == Kind::TEMPERATURE) ? c.cold : c.accent;
  QColor const high = (m_kind == Kind::TEMPERATURE) ? c.heat : c.warning;
  m_value->setStyleSheet(QStringLiteral("color: %1;").arg((std::isnan(u) || (std::fabs(u) < 0.5)) ? c.text.name() : (u < 0 ? low.name() : high.name())));
  m_bar->setValue(u);
}

void ControlOutputGauge::changeEvent(QEvent *event) {
  QFrame::changeEvent(event);
  if (event->type() == QEvent::PaletteChange) {
    refresh();
  }
}

}
