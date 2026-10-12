#include "chamberview.h"

#include <QEvent>
#include <QHelpEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QTimer>
#include <QToolTip>
#include <QtMath>
#include <cmath>

#include <functional>

#include "constants.h"
#include "theme.h"
#include "utils.h"

namespace deepvaccontrol {

namespace {
constexpr double CANVAS_W{740.0};
constexpr double CANVAS_H{620.0};

// schematic geometry (canvas units)
QPointF const TMP{262, 112};                      // turbo pump
double const  TMP_R{34};
double const  VAC_Y{40};                          // top run of the vacuum line
double const  MAIN_X{600};                        // main line (bellows)
QPointF const JUNCTION{600, 352};
QPointF const BP{600, 452};                       // backing pump
double const  BP_R{30};
QRectF const  DEWAR{548, 528, 96, 40};            // LN2 dewar
QRectF const  FRIDGE{18, 470, 90, 66};            // refrigeration unit
QRectF const  VESSEL{150, 204, 320, 388};          // the chamber body (cabinet)
QRectF const  OPENING{174, 228, 272, 340};         // front edge of the inner space, inside door frame and gasket
QRectF const  BACK{217, 271, 186, 220};            // back wall of the inner space (perspective)
// thermal plate on the floor, in perspective: top face (heater coil) between the back and front edges, front face
// (coolant channel) below the front edge
QPointF const PLATE_BACK_LEFT{220, 484};
QPointF const PLATE_BACK_RIGHT{400, 484};
QRectF const  PLATE{190, 538, 240, 16};             // front face
double const  CHANNEL_Y{546};                       // coolant channel in the front face
QRectF const  PLC{22, 26, 112, 58};               // PLC miniature

QFont canvasFont(double pixels, bool bold = false) {
  QFont font;
  font.setPixelSize(qMax(1, static_cast<int>(std::lround(pixels))));
  font.setBold(bold);
  return font;
}

// a colour between two others (t = 0: a, 1: b)
QColor mix(QColor const &a, QColor const &b, double t) {
  return QColor::fromRgbF(static_cast<float>(a.redF() + (b.redF() - a.redF()) * t),
                          static_cast<float>(a.greenF() + (b.greenF() - a.greenF()) * t),
                          static_cast<float>(a.blueF() + (b.blueF() - a.blueF()) * t));
}

QString valueText(double value, int decimals, QString const &unit) {
  if (std::isnan(value)) {
    return QStringLiteral("—");
  }
  return QString::number(value, 'f', decimals).replace('-', QChar(0x2212)) + unit;
}

} // namespace

ChamberView::ChamberView(ResHolder *resHolder, QWidget *parent) : QWidget{parent}, m_resHolder{resHolder} {
  setMouseTracking(true);
  setMinimumSize(static_cast<int>(400 * utils::ssf()), static_cast<int>(250 * utils::ssf()));
  detect();

  m_timer = new QTimer(this);
  connect(m_timer, &QTimer::timeout, this, [this] {
    m_phase += 1.0;
    update();
  });
  connect(m_resHolder->states, &SystemValueList::ValuesUpdated, this, [this] {
    refreshAnimation();
    update();
  });
  connect(m_resHolder->status, &StatusManager::appStateChanged, this, [this] {
    refreshAnimation();
    update();
  });
}

// ---------------------------------------------------------------- detection and state
void ChamberView::detect() {
  // components are recognised by the configured output labels, so another chamber with another output list
  // shows only what it has
  struct Rule {
    char const                          *key;
    std::function<bool(QString const &)> match;
  };
  auto has  = [](QString const &l, char const *w) { return l.contains(QLatin1String(w)); };
  auto word = [](QString const &l, char const *w) {
    return l.contains(QRegularExpression(QStringLiteral("\\b%1\\b").arg(QLatin1String(w))));
  };
  QVector<Rule> const rules{
    {"light", [&](QString const &l) { return l == QLatin1String("light") || has(l, "chamber light"); }},
    {"heater", [&](QString const &l) { return l.startsWith(QLatin1String("heater")); }},
    {"comp404", [&](QString const &l) { return has(l, "compressor") && has(l, "r404"); }},
    {"comp23", [&](QString const &l) { return has(l, "compressor") && has(l, "r23"); }},
    {"evap404", [&](QString const &l) { return has(l, "evaporator") && has(l, "r404"); }},
    {"evap23", [&](QString const &l) { return has(l, "evaporator") && has(l, "r23"); }},
    {"vtmh", [&](QString const &l) { return has(l, "v-tmh") || (has(l, "valve") && (has(l, "tmp") || has(l, "turbo"))); }},
    {"vk", [&](QString const &l) { return has(l, "v-k") || (has(l, "valve") && has(l, "chamber")); }},
    {"ln2", [&](QString const &l) { return has(l, "ln2") || has(l, "ln₂") || has(l, "nitrogen"); }},
    {"tmp", [&](QString const &l) { return word(l, "tmp") || has(l, "turbo"); }},
    {"bp", [&](QString const &l) { return has(l, "backing") || has(l, "scroll") || has(l, "roughing") || word(l, "bp"); }},
  };
  for (QVariant const &entry : m_resHolder->settings->settingsMap().value("monitor").toList()) {
    QVariantMap const item = entry.toMap();
    if (item.value("name").toString() != QLatin1String("outputs")) {
      continue;
    }
    QString const label = item.value("label").toString();
    QString const lower = label.toLower();
    for (Rule const &rule : rules) {
      if (!m_outputs.contains(rule.key) && rule.match(lower)) {
        m_outputs.insert(rule.key, Output{label, item.value("bit").toInt()});
        break;
      }
    }
  }
}

bool ChamberView::fitted(QString const &key) const {
  if (key == QLatin1String("sensor")) {
    return m_resHolder->states->contains("temp");
  }
  if (key == QLatin1String("bp")) {
    return true; // the backing pump is part of every chamber (with or without its own PLC output)
  }
  if (key == QLatin1String("fridge")) {
    return m_outputs.contains("comp23") || m_outputs.contains("comp404") || m_outputs.contains("evap23") || m_outputs.contains("evap404");
  }
  return m_outputs.contains(key);
}

bool ChamberView::online() const {
  return m_resHolder->status->getAppState() == StatusManager::AppState::ONLINE;
}

bool ChamberView::on(QString const &key) const {
  if ((key == QLatin1String("bp")) && !m_outputs.contains(key)) {
    return online(); // no output of its own: it runs while the chamber is on
  }
  if (!online() || !m_outputs.contains(key) || !m_resHolder->states->contains("outputs")) {
    return false;
  }
  return utils::getBitOfFloat(m_resHolder->states->getSysValue("outputs")->getValue(), m_outputs.value(key).bit);
}

bool ChamberView::cooling() const {
  return on("comp23") || on("comp404") || on("evap23") || on("evap404");
}

double ChamberView::value(QString const &name) const {
  if (!online() || !m_resHolder->states->contains(name)) {
    return std::numeric_limits<double>::quiet_NaN();
  }
  double const v{m_resHolder->states->getSysValue(name)->getValue()};
  return (std::isnan(v) || std::fabs(v) > 1e9) ? std::numeric_limits<double>::quiet_NaN() : v;
}

void ChamberView::refreshAnimation() {
  // online: the PLC link blinks, the backing pump turns and whatever is on animates
  bool const moving = isVisible() && online();
  if (moving && !m_timer->isActive()) {
    m_timer->start(80);
  } else if (!moving && m_timer->isActive()) {
    m_timer->stop();
  }
}

void ChamberView::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);
  refreshAnimation();
}

void ChamberView::hideEvent(QHideEvent *event) {
  QWidget::hideEvent(event);
  refreshAnimation();
}

ChamberView::Tag ChamberView::tag(QString const &key) const {
  Tag t;
  auto onOff = [&](bool isOn, QString const &onText, QString const &offText) {
    if (!t.fitted) {
      t.status = QStringLiteral("—");
    } else if (!online()) {
      t.status = offText;
    } else {
      t.status = isOn ? onText : offText;
      t.state  = isOn ? State::ACTIVE : State::READY;
    }
  };
  if (key == QLatin1String("plc")) {
    QString const state = APP_STATE_LIST.value(static_cast<int>(m_resHolder->status->getAppState())).section('=', -1).toLower();
    t.status            = state.isEmpty() ? state : state.at(0).toUpper() + state.mid(1);
    t.state  = online() ? State::ACTIVE : State::OFF;
  } else if (key == QLatin1String("sensor")) {
    t.fitted        = fitted("sensor");
    double const v  = value("temp");
    t.status        = valueText(v, 1, QStringLiteral(" °C"));
    t.state         = std::isnan(v) ? State::OFF : State::ACTIVE;
  } else if (key == QLatin1String("fridge")) {
    t.fitted = fitted("fridge");
    onOff(on("comp23") || on("comp404"), tr("On"), tr("Off"));
  } else if (key == QLatin1String("heater")) {
    t.fitted = fitted("heater");
    onOff(on("heater"), tr("On"), tr("Off"));
    double const u{value("temp_u")};
    if ((t.state == State::ACTIVE) && !std::isnan(u) && (u > 0.0)) {
      t.status = tr("On · %1").arg(valueText(u, 0, QStringLiteral(" %")));
    }
  } else if ((key == QLatin1String("ln2")) || (key == QLatin1String("vk")) || (key == QLatin1String("vtmh"))) {
    t.fitted = fitted(key);
    onOff(on(key), tr("Open"), tr("Closed"));
  } else {
    t.fitted = fitted(key);
    onOff(on(key), tr("On"), tr("Off"));
  }
  return t;
}

// ---------------------------------------------------------------- painting
void ChamberView::paintEvent(QPaintEvent *) {
  theme::Colors const &c = theme::colors();
  QPainter             p(this);
  p.setRenderHint(QPainter::Antialiasing);
  p.setRenderHint(QPainter::TextAntialiasing);

  // panel
  double const s{utils::ssf()};
  QRectF const panel = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
  p.setPen(QPen(c.border, 1 * s));
  p.setBrush(c.panel);
  p.drawRoundedRect(panel, 8 * s, 8 * s);

  // canvas fitted into the panel
  QRectF const content(0, 0, CANVAS_W, CANVAS_H);
  QRectF const area  = QRectF(rect()).adjusted(12 * s, 12 * s, -12 * s, -12 * s);
  double const scale = std::min(area.width() / content.width(), area.height() / content.height());
  m_transform        = QTransform();
  m_transform.translate(area.center().x() - content.center().x() * scale, area.center().y() - content.center().y() * scale);
  m_transform.scale(scale, scale);
  p.setTransform(m_transform);
  m_tips.clear();

  drawVacuum(p);
  drawChamber(p);
  drawSupply(p); // after the chamber: the coolant lines run inside it to the plate
  drawPlc(p);
}

void ChamberView::drawLine(QPainter &p, QVector<QPointF> const &points, bool isFitted, bool flowing, QColor const &flowColor) {
  theme::Colors const &c = theme::colors();
  QPainterPath         path;
  path.moveTo(points.first());
  for (int i{1}; i < points.size(); ++i) {
    path.lineTo(points[i]);
  }
  p.setBrush(Qt::NoBrush);
  QPen base(isFitted ? c.muted : c.off, 3, isFitted ? Qt::SolidLine : Qt::DotLine, Qt::RoundCap, Qt::RoundJoin);
  p.setPen(base);
  p.drawPath(path);
  if (flowing) {
    QPen dash(flowColor, 3, Qt::CustomDashLine, Qt::FlatCap, Qt::RoundJoin);
    dash.setDashPattern({2.5, 2.5});
    dash.setDashOffset(-m_phase * 0.6);
    p.setPen(dash);
    p.drawPath(path);
  }
}

void ChamberView::drawPipe(QPainter &p, QPointF const &from, QPointF const &to, bool isFitted, bool flowing, QColor const &flowColor) {
  theme::Colors const &c = theme::colors();
  QLineF const         line(from, to);
  QPointF const        across = QPointF(-line.dy(), line.dx()) / std::max(1.0, line.length()) * 10.0;
  QColor const         wall   = isFitted ? c.muted : c.off;
  p.setBrush(Qt::NoBrush);
  // walls, bore, flanges at both ends
  p.setPen(QPen(wall, 12, Qt::SolidLine, Qt::FlatCap));
  p.drawLine(line);
  p.setPen(QPen(theme::isDark() ? c.window : c.panelAlt, 7, Qt::SolidLine, Qt::FlatCap));
  p.drawLine(line);
  p.setPen(QPen(wall, 3, Qt::SolidLine, Qt::RoundCap));
  p.drawLine(from - across, from + across);
  p.drawLine(to - across, to + across);
  if (flowing) {
    QPen dash(flowColor, 4, Qt::CustomDashLine, Qt::FlatCap);
    dash.setDashPattern({1.5, 1.5});
    dash.setDashOffset(-m_phase * 0.6);
    p.setPen(dash);
    p.drawLine(line);
  }
}

void ChamberView::drawValve(QPainter &p, QPointF const &at, QString const &key, QString const &title, QString const &tip, bool tagAbove) {
  theme::Colors const &c = theme::colors();
  bool const           isFitted{fitted(key)};
  bool const           open{on(key)};
  QColor const         color = !isFitted ? c.off : (open ? c.active : c.muted);
  double const         w{15}, h{11};
  // actuator
  p.setPen(QPen(color, 2));
  p.setBrush(Qt::NoBrush);
  p.drawLine(at, at + QPointF(0, -20));
  p.setBrush(theme::colors().panel);
  p.drawEllipse(at + QPointF(0, -26), 6, 6);
  // body: two triangles tip to tip
  QPolygonF left;
  left << at + QPointF(-w, -h) << at << at + QPointF(-w, h);
  QPolygonF right;
  right << at + QPointF(w, -h) << at << at + QPointF(w, h);
  p.setPen(QPen(color, 2, isFitted ? Qt::SolidLine : Qt::DotLine, Qt::RoundCap, Qt::RoundJoin));
  p.setBrush(open ? QBrush(QColor(color.red(), color.green(), color.blue(), 170)) : QBrush(c.panel));
  p.drawPolygon(left);
  p.drawPolygon(right);
  drawTag(p, at + QPointF(0, tagAbove ? -36 : 14), Qt::AlignHCenter | (tagAbove ? Qt::AlignBottom : Qt::AlignTop), key, title);
  addTip(QRectF(at.x() - 22, at.y() - 34, 44, 50), isFitted ? tip : tr("%1: not fitted on this chamber").arg(tip));
}

void ChamberView::drawChamber(QPainter &p) {
  theme::Colors const &c = theme::colors();

  QColor const base = theme::isDark() ? c.window : c.panelAlt;
  QPen const   edge(c.muted, 1.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);

  // lifting eyes and a fitting on top, hinges on the left, latch on the right: drawn first, the body covers their feet
  p.setPen(QPen(c.muted, 2.5));
  p.setBrush(c.panel);
  for (double const x : {204.0, 376.0}) {
    p.drawEllipse(QPointF(x, VESSEL.top() - 7), 10, 10);
    p.drawEllipse(QPointF(x, VESSEL.top() - 7), 4.5, 4.5);
  }
  p.drawRoundedRect(QRectF(418, VESSEL.top() - 9, 36, 12), 2, 2);
  for (double const y : {244.0, 538.0}) {
    p.drawRoundedRect(QRectF(VESSEL.left() - 8, y, 12, 42), 2.5, 2.5);
  }

  // cabinet: body, door frame, gasket
  p.setPen(QPen(c.muted, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  p.setBrush(c.panel);
  p.drawRoundedRect(VESSEL, 12, 12);
  p.setPen(QPen(c.muted, 2));
  p.setBrush(Qt::NoBrush);
  p.drawRoundedRect(VESSEL.adjusted(10, 10, -10, -10), 9, 9);
  p.setPen(QPen(theme::isDark() ? c.muted : c.muted.darker(125), 4.5));
  p.drawRoundedRect(VESSEL.adjusted(17, 17, -17, -17), 7, 7);

  // inner space in perspective: ceiling, side walls and floor towards the back wall
  QPolygonF const ceiling{{OPENING.topLeft(), OPENING.topRight(), BACK.topRight(), BACK.topLeft()}};
  QPolygonF const left{{OPENING.topLeft(), BACK.topLeft(), BACK.bottomLeft(), OPENING.bottomLeft()}};
  QPolygonF const right{{OPENING.topRight(), OPENING.bottomRight(), BACK.bottomRight(), BACK.topRight()}};
  QPolygonF const floorWall{{BACK.bottomLeft(), BACK.bottomRight(), OPENING.bottomRight(), OPENING.bottomLeft()}};
  p.setPen(edge);
  p.setBrush(mix(base, c.muted, 0.16));
  p.drawPolygon(ceiling);
  p.setBrush(mix(base, c.muted, 0.09));
  p.drawPolygon(left);
  p.drawPolygon(right);
  p.setBrush(mix(base, c.muted, 0.04));
  p.drawPolygon(floorWall);
  p.setBrush(mix(base, c.panel, 0.5));
  p.drawRect(BACK);

  // latch on the right of the door
  p.setPen(QPen(c.muted, 2.5));
  p.setBrush(c.panel);
  p.drawRoundedRect(QRectF(VESSEL.right() - 4, 380, 10, 60), 3, 3);
  p.drawRoundedRect(QRectF(VESSEL.right() - 10, 396, 22, 28), 4, 4);
  p.drawEllipse(QPointF(VESSEL.right() + 1, 410), 3.5, 6);

  // light: a bulb at the top, glowing when on
  QPointF const lamp{400, 252}; // on the ceiling
  QPointF const glass{lamp.x(), lamp.y() - 3};
  bool const    lightOn{on("light")};
  if (lightOn) {
    double const    radius = 56 + 6 * std::sin(m_phase * 0.2);
    QRadialGradient glow(glass, radius);
    glow.setColorAt(0.0, QColor(250, 204, 21, 120));
    glow.setColorAt(1.0, QColor(250, 204, 21, 0));
    p.setPen(Qt::NoPen);
    p.setBrush(glow);
    p.drawEllipse(glass, radius, radius);
  }
  {
    // glass: a circle narrowing to the neck, then the screw base
    double const r{7};
    QPainterPath bulb;
    bulb.moveTo(lamp.x() - 3.5, lamp.y() + 6);
    bulb.arcTo(QRectF(glass.x() - r, glass.y() - r, 2 * r, 2 * r), 225, -270);
    bulb.lineTo(lamp.x() + 3.5, lamp.y() + 6);
    bulb.closeSubpath();
    QColor const line = fitted("light") ? c.muted : c.off;
    p.setPen(QPen(line, 1.6, fitted("light") ? Qt::SolidLine : Qt::DotLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(lightOn ? QColor(250, 204, 21) : c.panel);
    p.drawPath(bulb);
    p.setBrush(line);
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(QRectF(lamp.x() - 3.5, lamp.y() + 6.5, 7, 2.2), 0.8, 0.8);
    p.drawRoundedRect(QRectF(lamp.x() - 3.5, lamp.y() + 9.3, 7, 2.2), 0.8, 0.8);
    p.drawEllipse(QPointF(lamp.x(), lamp.y() + 12.6), 1.6, 1.2);
  }
  addTip(QRectF(lamp.x() - 12, lamp.y() - 14, 24, 28), fitted("light") ? tr("Light") : tr("Light: disconnected"));
  drawTag(p, QPointF(lamp.x() - 14, lamp.y() + 2), Qt::AlignRight | Qt::AlignVCenter, "light", tr("Light"), true);

  // the plate: a slab on the floor, heater coil on its top face, coolant channel in its front (cooling unit and LN₂)
  bool const heating{on("heater")};
  bool const chilled{cooling() || on("ln2")};
  // a point of the top face: u across (0 left .. 1 right), v in depth (0 back .. 1 front)
  auto top = [](double u, double v) {
    double const back{PLATE_BACK_LEFT.x() + (PLATE_BACK_RIGHT.x() - PLATE_BACK_LEFT.x()) * u};
    double const front{PLATE.left() + PLATE.width() * u};
    return QPointF(back + (front - back) * v, PLATE_BACK_LEFT.y() + (PLATE.top() - PLATE_BACK_LEFT.y()) * v);
  };
  QPolygonF const topFace{{top(0, 0), top(1, 0), top(1, 1), top(0, 1)}};
  if (heating) {
    int const alpha = 25 + static_cast<int>(30 * (0.5 + 0.5 * std::sin(m_phase * 0.25))); // pulsing glow
    p.setPen(QPen(QColor(c.heat.red(), c.heat.green(), c.heat.blue(), alpha), 16, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawPolygon(QPolygonF{{top(0, 0), top(1, 0), PLATE.bottomRight(), PLATE.bottomLeft()}});
  }
  // feet, then the front face, then the top face with its rim
  p.setPen(QPen(c.muted, 1.4));
  p.setBrush(mix(base, c.muted, 0.25));
  for (double const x : {PLATE.left() + 14, PLATE.right() - 14}) {
    p.drawEllipse(QPointF(x, PLATE.bottom() + 2), 5, 2.5);
  }
  p.setPen(QPen(c.muted, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  p.setBrush(mix(base, c.muted, 0.14));
  p.drawRect(PLATE);
  QColor const tint = heating ? c.heat : (chilled ? c.cold : QColor());
  p.setBrush(tint.isValid() ? QBrush(mix(c.panel, tint, 0.18)) : QBrush(c.panel));
  p.drawPolygon(topFace);
  p.setPen(QPen(mix(c.panel, c.muted, 0.45), 1));
  p.setBrush(Qt::NoBrush);
  p.drawPolygon(QPolygonF{{top(0.03, 0.08), top(0.97, 0.08), top(0.97, 0.92), top(0.03, 0.92)}});

  // heater coil: three long rounded grooves across the top face
  QColor const heatColor = !fitted("heater") ? c.off : (heating ? c.heat : c.muted);
  QPainterPath coil;
  double const rv{0.075};                  // half groove width, in depth
  double const ru{rv * (PLATE.top() - PLATE_BACK_LEFT.y()) / 215.0}; // the same on screen, across
  for (double const centre : {0.27, 0.5, 0.73}) {
    double const u1{0.1};
    double const u2{0.9};
    QVector<QPointF> points;
    for (int i{0}; i <= 8; ++i) { // far edge, right end, near edge, left end
      points << top(u1 + (u2 - u1) * i / 8.0, centre - rv);
    }
    for (int i{1}; i < 12; ++i) {
      double const a{-M_PI / 2 + M_PI * i / 12.0};
      points << top(u2 + ru * std::cos(a), centre + rv * std::sin(a));
    }
    for (int i{0}; i <= 8; ++i) {
      points << top(u2 - (u2 - u1) * i / 8.0, centre + rv);
    }
    for (int i{1}; i < 12; ++i) {
      double const a{M_PI / 2 + M_PI * i / 12.0};
      points << top(u1 + ru * std::cos(a), centre + rv * std::sin(a));
    }
    coil.addPolygon(QPolygonF(points));
    coil.closeSubpath();
  }
  p.setPen(QPen(heatColor, 1.8, fitted("heater") ? Qt::SolidLine : Qt::DotLine, Qt::RoundCap, Qt::RoundJoin));
  p.drawPath(coil);

  // coolant channel along the front face
  bool const   channelFitted{fitted("fridge") || fitted("ln2")};
  QColor const channelColor = !channelFitted ? c.off : (chilled ? c.cold : c.muted);
  QLineF const channel(PLATE.left() + 6, CHANNEL_Y, PLATE.right() - 6, CHANNEL_Y);
  p.setPen(QPen(channelColor, 2.5, channelFitted ? Qt::SolidLine : Qt::DotLine, Qt::FlatCap));
  p.drawLine(channel);
  if (chilled) {
    QPen flow(c.cold.lighter(160), 2.5, Qt::CustomDashLine, Qt::FlatCap);
    flow.setDashPattern({2, 3});
    flow.setDashOffset(-m_phase * 0.8);
    p.setPen(flow);
    p.drawLine(channel);
  }
  QRectF const plateArea(PLATE.left(), PLATE_BACK_LEFT.y(), PLATE.width(), PLATE.bottom() - PLATE_BACK_LEFT.y());
  addTip(plateArea, fitted("heater") ? tr("Plate: heater and coolant channel") : tr("Plate: coolant channel (no heater fitted)"));
  drawTag(p, QPointF(PLATE.center().x(), PLATE.bottom() + 6), Qt::AlignHCenter | Qt::AlignTop, "heater", tr("Thermal Plate"), true);

  // sensor T1, on the plate
  double const  probeR{22};
  // in the middle of the plate's top face
  QPointF const probe{PLATE.center().x(), (PLATE_BACK_LEFT.y() + PLATE.top()) / 2.0};
  bool const    sensorFitted{fitted("sensor")};
  p.setPen(QPen(sensorFitted ? c.muted : c.off, 2));
  p.setBrush(c.panel);
  p.drawEllipse(probe, probeR, probeR);
  p.setFont(canvasFont(12, true));
  p.setPen(c.text);
  p.drawText(QRectF(probe.x() - probeR, probe.y() - probeR, 2 * probeR, 2 * probeR), Qt::AlignCenter, QStringLiteral("T1"));
  addTip(QRectF(probe.x() - probeR, probe.y() - probeR, 2 * probeR, 2 * probeR), tr("Temperature sensor T1"));
  // the reading is in its tag
  // centred above it, just over the back edge of the plate
  drawTag(p, QPointF(probe.x(), probe.y() - probeR - 4), Qt::AlignHCenter | Qt::AlignBottom, "sensor", tr("Temperature sensor"), true);
}

void ChamberView::drawVacuum(QPainter &p) {
  theme::Colors const &c = theme::colors();
  bool const           tmpOn{on("tmp")};
  bool const           bpOn{on("bp")};
  bool const           vtmhOpen{on("vtmh")};
  bool const           vkOpen{on("vk")};
  bool const           anyFitted{fitted("tmp") || fitted("bp")};

  // lines: TMP -> V-TMH -> main line -> junction; chamber -> V-K -> junction; junction -> BP
  QPointF const vtmh{440, VAC_Y};
  QPointF const vk{548, JUNCTION.y()};
  drawLine(p, {TMP + QPointF(0, -TMP_R), QPointF(TMP.x(), VAC_Y), vtmh}, fitted("tmp"), tmpOn && vtmhOpen && bpOn, c.accent);
  drawLine(p, {vtmh, QPointF(MAIN_X, VAC_Y), QPointF(MAIN_X, 78)}, anyFitted, vtmhOpen && bpOn, c.accent);
  drawLine(p, {QPointF(TMP.x(), VESSEL.top()), TMP + QPointF(0, TMP_R)}, fitted("tmp"), tmpOn && bpOn, c.accent);
  // main line: a pipe, gas flowing down to the backing pump
  drawPipe(p, QPointF(MAIN_X, 78), QPointF(MAIN_X, JUNCTION.y() - 6), anyFitted, (vtmhOpen || vkOpen) && bpOn, c.accent);
  addTip(QRectF(MAIN_X - 10, 78, 20, JUNCTION.y() - 84), tr("Main line"));
  drawLine(p, {QPointF(VESSEL.right(), JUNCTION.y()), vk, JUNCTION}, fitted("vk") || fitted("bp"), vkOpen && bpOn, c.accent);
  drawLine(p, {JUNCTION, BP + QPointF(0, -BP_R)}, fitted("bp"), bpOn && (vkOpen || vtmhOpen), c.accent);
  // exhaust
  drawLine(p, {BP + QPointF(BP_R, 0), QPointF(BP.x() + 72, BP.y())}, fitted("bp"), bpOn, c.accent);
  {
    QPointF const tip{BP.x() + 80, BP.y()};
    p.setPen(QPen(fitted("bp") ? c.muted : c.off, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawLine(tip, tip + QPointF(-9, -7));
    p.drawLine(tip, tip + QPointF(-9, 7));
  }
  p.setPen(Qt::NoPen);
  p.setBrush(anyFitted ? c.muted : c.off);
  p.drawEllipse(JUNCTION, 5, 5);

  drawValve(p, vtmh, "vtmh", QStringLiteral("V-TMH"), tr("Valve between the turbo pump and the main line"), false);
  drawValve(p, vk, "vk", QStringLiteral("V-K"), tr("Valve between the chamber and the backing pump"), true);

  // turbo pump above the chamber, with its rotor
  QColor const tmpColor = !fitted("tmp") ? c.off : (tmpOn ? c.active : c.muted);
  p.setPen(QPen(tmpColor, 2.5, fitted("tmp") ? Qt::SolidLine : Qt::DotLine));
  p.setBrush(tmpOn ? QColor(c.active.red(), c.active.green(), c.active.blue(), 40) : c.panel);
  p.drawEllipse(TMP, TMP_R, TMP_R);
  if (tmpOn) {
    QPen ring(c.accent, 1.5, Qt::DashLine);
    p.setPen(ring);
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(TMP, TMP_R + 7, TMP_R + 7);
  }
  p.save();
  p.translate(TMP);
  p.rotate(tmpOn ? m_phase * 18.0 : 0.0);
  p.setPen(QPen(tmpColor, 2.5, Qt::SolidLine, Qt::RoundCap));
  for (int i{0}; i < 6; ++i) {
    p.rotate(60);
    p.drawLine(QPointF(5, 0), QPointF(TMP_R - 9, -8));
  }
  p.drawEllipse(QPointF(0, 0), 5, 5);
  p.restore();
  drawTag(p, QPointF(TMP.x() - TMP_R - 12, TMP.y()), Qt::AlignRight | Qt::AlignVCenter, "tmp", QStringLiteral("TMP"));
  addTip(QRectF(TMP.x() - TMP_R, TMP.y() - TMP_R, 2 * TMP_R, 2 * TMP_R),
         fitted("tmp") ? tr("Turbomolecular pump (TMP)") : tr("Turbomolecular pump: not fitted on this chamber"));

  // backing pump: a scroll
  QColor const bpColor = !fitted("bp") ? c.off : (bpOn ? c.active : c.muted);
  p.setPen(QPen(bpColor, 2.5, fitted("bp") ? Qt::SolidLine : Qt::DotLine));
  p.setBrush(bpOn ? QColor(c.active.red(), c.active.green(), c.active.blue(), 40) : c.panel);
  p.drawEllipse(BP, BP_R, BP_R);
  {
    QPainterPath spiral;
    double const turn = bpOn ? m_phase * 0.25 : 0.0;
    for (int i{0}; i <= 80; ++i) {
      double const a = i * 0.22 + turn;
      double const r = 2 + i * 0.28;
      QPointF const pt(BP.x() + r * std::cos(a), BP.y() + r * std::sin(a));
      i == 0 ? spiral.moveTo(pt) : spiral.lineTo(pt);
    }
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(bpColor, 2, Qt::SolidLine, Qt::RoundCap));
    p.drawPath(spiral);
  }
  drawTag(p, QPointF(BP.x() + BP_R + 8, BP.y() + 12), Qt::AlignLeft | Qt::AlignTop, "bp", tr("Backing pump"));
  addTip(QRectF(BP.x() - BP_R, BP.y() - BP_R, 2 * BP_R, 2 * BP_R),
         fitted("bp") ? tr("Backing pump (BP)") : tr("Backing pump: not fitted on this chamber"));
}

void ChamberView::drawSupply(QPainter &p) {
  theme::Colors const &c = theme::colors();

  // LN2: chamber -> valve -> dewar
  bool const    ln2Open{on("ln2")};
  QPointF const valve{518, 548};
  drawLine(p, {QPointF(DEWAR.left(), 548), valve, QPointF(452, 548), QPointF(452, CHANNEL_Y), QPointF(PLATE.right(), CHANNEL_Y)},
           fitted("ln2"), ln2Open, c.cold);
  drawValve(p, valve, "ln2", QStringLiteral("LN₂"), tr("LN₂ valve"), false);
  p.setPen(QPen(fitted("ln2") ? c.accent : c.off, 2, fitted("ln2") ? Qt::SolidLine : Qt::DotLine));
  p.setBrush(c.panel);
  p.drawRoundedRect(DEWAR, 6, 6);
  p.setFont(canvasFont(14, true));
  p.setPen(fitted("ln2") ? c.text : c.off);
  p.drawText(DEWAR, Qt::AlignCenter, QStringLiteral("LN₂"));
  addTip(DEWAR, fitted("ln2") ? tr("LN₂ dewar") : tr("LN₂ supply: not fitted on this chamber"));

  // refrigeration unit -> plate (supply and return)
  bool const cold{cooling()};
  drawLine(p, {QPointF(FRIDGE.right(), FRIDGE.top() + 18), QPointF(172, FRIDGE.top() + 18), QPointF(172, CHANNEL_Y - 6),
               QPointF(PLATE.left(), CHANNEL_Y - 6)},
           fitted("fridge"), cold, c.cold);
  drawLine(p, {QPointF(PLATE.left(), CHANNEL_Y + 4), QPointF(160, CHANNEL_Y + 4), QPointF(160, FRIDGE.bottom() - 18),
               QPointF(FRIDGE.right(), FRIDGE.bottom() - 18)},
           fitted("fridge"), cold, c.cold);
  p.setPen(QPen(!fitted("fridge") ? c.off : (cold ? c.cold : c.muted), 2, fitted("fridge") ? Qt::SolidLine : Qt::DotLine));
  p.setBrush(c.panel);
  p.drawRoundedRect(FRIDGE, 8, 8);
  // the snowflake turns while cooling
  p.save();
  p.translate(FRIDGE.center());
  p.rotate(cold ? m_phase * 3.0 : 0.0);
  drawSnowflake(p, QPointF(0, 0), 13, !fitted("fridge") ? c.off : (cold ? c.cold : c.muted));
  p.restore();
  addTip(FRIDGE, fitted("fridge") ? tr("Cooling unit") : tr("Cooling unit: not fitted on this chamber"));
  drawTag(p, QPointF(FRIDGE.center().x(), FRIDGE.bottom() + 8), Qt::AlignHCenter | Qt::AlignTop, "fridge", tr("Cooling unit"));
}

void ChamberView::drawSnowflake(QPainter &p, QPointF const &center, double radius, QColor const &color) {
  p.setPen(QPen(color, 2, Qt::SolidLine, Qt::RoundCap));
  p.setBrush(Qt::NoBrush);
  for (int i{0}; i < 3; ++i) {
    double const  a = i * M_PI / 3.0;
    QPointF const d = QPointF(std::cos(a), std::sin(a)) * radius;
    p.drawLine(center + d, center - d);
  }
}

void ChamberView::drawTag(QPainter &p, QPointF const &anchor, Qt::Alignment align, QString const &key, QString const &title,
                          bool backdrop) {
  theme::Colors const &c = theme::colors();
  Tag const            t = tag(key);
  bool const           active{t.state == State::ACTIVE};
  QFont const          titleFont  = canvasFont(12, true);
  QFont const          statusFont = canvasFont(11, active);
  double const         dot{3.5}, gap{5}, pad{7}, height{36};
  double const         statusW = 2 * dot + gap + QFontMetricsF(statusFont).horizontalAdvance(t.status);
  double const         width   = std::max(QFontMetricsF(titleFont).horizontalAdvance(title), statusW) + 2 * pad;
  QPointF              corner  = anchor;
  if (align & Qt::AlignRight) {
    corner.rx() -= width;
  } else if (align & Qt::AlignHCenter) {
    corner.rx() -= width / 2;
  }
  if (align & Qt::AlignBottom) {
    corner.ry() -= height;
  } else if (align & Qt::AlignVCenter) {
    corner.ry() -= height / 2;
  }
  QRectF const box(corner, QSizeF(width, height));

  if (backdrop) {
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(c.panel.red(), c.panel.green(), c.panel.blue(), 225));
    p.drawRoundedRect(box, 7, 7);
  }
  // lit while the component is on
  if (active) {
    p.setPen(QPen(QColor(c.active.red(), c.active.green(), c.active.blue(), 120), 1));
    p.setBrush(QColor(c.active.red(), c.active.green(), c.active.blue(), theme::isDark() ? 50 : 30));
    p.drawRoundedRect(box, 7, 7);
  }
  Qt::Alignment const horizontal = (align & Qt::AlignRight)     ? Qt::AlignRight
                                   : (align & Qt::AlignHCenter) ? Qt::AlignHCenter
                                                                : Qt::AlignLeft;
  p.setFont(titleFont);
  p.setPen(t.fitted ? c.text : c.off);
  p.drawText(box.adjusted(pad, 3, -pad, -height / 2), horizontal | Qt::AlignVCenter, title);

  // state: a dot (pulsing while on) and the text
  QRectF const  line = box.adjusted(pad, height / 2 - 1, -pad, -3);
  double const  x    = (horizontal == Qt::AlignRight)     ? line.right() - statusW
                       : (horizontal == Qt::AlignHCenter) ? line.center().x() - statusW / 2
                                                          : line.left();
  QPointF const at(x + dot, line.center().y());
  if (active) {
    double const pulse = 0.5 + 0.5 * std::sin(m_phase * 0.3);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(c.active.red(), c.active.green(), c.active.blue(), static_cast<int>(40 + 60 * pulse)));
    p.drawEllipse(at, dot + 1.5 + 2 * pulse, dot + 1.5 + 2 * pulse);
    p.setBrush(c.active);
  } else {
    p.setPen(QPen(t.fitted && (t.state == State::READY) ? c.muted : c.off, 1.2));
    p.setBrush(t.state == State::READY ? QBrush(c.ready) : QBrush(Qt::NoBrush));
  }
  p.drawEllipse(at, dot, dot);
  p.setFont(statusFont);
  p.setPen(active ? c.active : (t.fitted ? c.muted : c.off));
  p.drawText(QRectF(x + 2 * dot + gap, line.top(), statusW, line.height()), Qt::AlignLeft | Qt::AlignVCenter, t.status);
}

void ChamberView::drawPlc(QPainter &p) {
  theme::Colors const &c = theme::colors();
  auto const           state = m_resHolder->status->getAppState();
  bool const           live{online()};
  bool const           blink{(static_cast<int>(m_phase / 4) % 2) == 0};

  // a small rack: the CPU module and three I/O modules
  p.setPen(QPen(live ? c.active : c.muted, 2));
  p.setBrush(c.panel);
  p.drawRoundedRect(PLC, 5, 5);
  double const module{PLC.width() / 4};
  p.setPen(QPen(c.border, 1.5));
  for (int i{1}; i < 4; ++i) {
    p.drawLine(QPointF(PLC.left() + i * module, PLC.top() + 4), QPointF(PLC.left() + i * module, PLC.bottom() - 4));
  }
  auto led = [&](QPointF const &at, QColor const &color, bool lit) {
    p.setPen(QPen(lit ? color : c.off, 1));
    p.setBrush(lit ? QBrush(color) : QBrush(Qt::NoBrush));
    p.drawEllipse(at, 3, 3);
  };
  // CPU: link (blinking while connecting), run, communication (blinks while data flows)
  double const cpuX{PLC.left() + module / 2};
  bool const   linking{state == StatusManager::AppState::CONNECTING};
  bool const   linked{live || (state == StatusManager::AppState::CONNECTED)};
  led(QPointF(cpuX, PLC.top() + 14), linking ? c.warning : c.active, linked || (linking && blink));
  led(QPointF(cpuX, PLC.top() + 29), c.active, live);
  led(QPointF(cpuX, PLC.top() + 44), c.accent, live && blink);
  // I/O modules: one LED per output bit
  double const outputs = (live && m_resHolder->states->contains("outputs")) ? m_resHolder->states->getSysValue("outputs")->getValue() : 0.0;
  for (int m{1}; m < 4; ++m) {
    double const left{PLC.left() + m * module};
    for (int row{0}; row < 5; ++row) {
      double const y{PLC.top() + 10 + row * 9.5};
      p.setPen(QPen(c.border, 1.5, Qt::SolidLine, Qt::RoundCap));
      p.drawLine(QPointF(left + 12, y), QPointF(left + module - 6, y));
      led(QPointF(left + 7, y), c.active, live && utils::getBitOfFloat(outputs, (m - 1) * 5 + row));
    }
  }
  addTip(PLC, tr("PLC: %1").arg(tag("plc").status));
  drawTag(p, QPointF(PLC.center().x(), PLC.bottom() + 8), Qt::AlignHCenter | Qt::AlignTop, "plc", QStringLiteral("PLC"));
}

void ChamberView::addTip(QRectF const &rect, QString const &text) {
  if (!text.isEmpty()) {
    m_tips.push_back({rect, text});
  }
}

bool ChamberView::event(QEvent *event) {
  if (event->type() == QEvent::ToolTip) {
    auto         *help = static_cast<QHelpEvent *>(event);
    QPointF const at   = m_transform.inverted().map(QPointF(help->pos()));
    for (auto const &[rect, text] : std::as_const(m_tips)) {
      if (rect.contains(at)) {
        QToolTip::showText(help->globalPos(), text, this);
        return true;
      }
    }
    QToolTip::hideText();
    event->ignore();
    return true;
  }
  return QWidget::event(event);
}

} // namespace deepvaccontrol
