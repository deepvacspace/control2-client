#include "chamberview.h"

#include <QEvent>
#include <QHelpEvent>
#include <QPainter>
#include <QPainterPath>
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
constexpr double CANVAS_W{1000.0};
constexpr double CANVAS_H{620.0};

// vessel and its test space
QRectF const VESSEL{255, 70, 300, 315};
QRectF const SPACE{283, 98, 244, 255};
QRectF const EVAPORATOR{296, 140, 26, 175};
QPointF const FAN{309, 121};
QPointF const LAMP{405, 112};
QPointF const PROBE{470, 200};
double const  HEATER_Y{335};
// refrigeration unit
QPointF const COMP23{115, 250};
QPointF const COMP404{115, 455};
QRectF const  EXCHANGER{80, 340, 70, 34};
QRectF const  CONDENSER{185, 536, 60, 26};
QRectF const  PLC{525, 448, 64, 32};
double const  COMP_R{26};

QFont canvasFont(double pixels, bool bold = false) {
  QFont font;
  font.setPixelSize(qMax(1, static_cast<int>(std::lround(pixels))));
  font.setBold(bold);
  return font;
}

QString valueText(double value, int decimals, QString const &unit) {
  if (std::isnan(value)) {
    return QStringLiteral("--");
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
    char const *key;
    std::function<bool(QString const &)> match;
  };
  auto has  = [](QString const &l, char const *w) { return l.contains(QLatin1String(w)); };
  QVector<Rule> const rules{
    {"light", [&](QString const &l) { return l == QLatin1String("light") || has(l, "chamber light"); }},
    {"fan", [&](QString const &l) { return has(l, "fan"); }},
    {"heater", [&](QString const &l) { return l.startsWith(QLatin1String("heater")); }},
    {"perimeter", [&](QString const &l) { return has(l, "perimeter") || has(l, "glass"); }},
    {"comp404", [&](QString const &l) { return has(l, "compressor") && has(l, "r404"); }},
    {"comp23", [&](QString const &l) { return has(l, "compressor") && has(l, "r23"); }},
    {"evap404", [&](QString const &l) { return has(l, "evaporator") && has(l, "r404"); }},
    {"evap23", [&](QString const &l) { return has(l, "evaporator") && has(l, "r23"); }},
    {"bypass404", [&](QString const &l) { return has(l, "bypass") && has(l, "r404"); }},
    {"bypass23", [&](QString const &l) { return has(l, "bypass") && has(l, "r23"); }},
    {"hx404", [&](QString const &l) { return has(l, "heat exchanger") && has(l, "r404"); }},
    {"sub404", [&](QString const &l) { return has(l, "subcooling") && has(l, "r404"); }},
    {"sub23", [&](QString const &l) { return has(l, "subcooling") && has(l, "r23"); }},
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

bool ChamberView::has(QString const &key) const {
  if (key == QLatin1String("sensor")) {
    return m_resHolder->states->contains("temp");
  }
  if (key == QLatin1String("pressure")) {
    return m_resHolder->states->contains("pres_23_high");
  }
  return m_outputs.contains(key);
}

bool ChamberView::online() const {
  return m_resHolder->status->getAppState() == StatusManager::AppState::ONLINE;
}

bool ChamberView::on(QString const &key) const {
  if (!online() || !m_outputs.contains(key) || !m_resHolder->states->contains("outputs")) {
    return false;
  }
  return utils::getBitOfFloat(m_resHolder->states->getSysValue("outputs")->getValue(), m_outputs.value(key).bit);
}

ChamberView::State ChamberView::outputState(QString const &key) const {
  if (!online()) {
    return State::OFF;
  }
  return on(key) ? State::ACTIVE : State::READY;
}

double ChamberView::value(QString const &name) const {
  if (!online() || !m_resHolder->states->contains(name)) {
    return std::numeric_limits<double>::quiet_NaN();
  }
  double const v{m_resHolder->states->getSysValue(name)->getValue()};
  return (std::isnan(v) || std::fabs(v) > 1e9) ? std::numeric_limits<double>::quiet_NaN() : v;
}

void ChamberView::refreshAnimation() {
  bool const moving = isVisible() && (on("fan") || on("comp23") || on("comp404"));
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

QVector<ChamberView::Card> ChamberView::cards() const {
  QVector<Card> list;
  auto          stateText = [](State s, QString const &active, QString const &ready) {
    return s == State::ACTIVE ? active : (s == State::READY ? ready : tr("Off"));
  };
  if (has("light")) {
    State const s = outputState("light");
    list.push_back({tr("Chamber light"), stateText(s, tr("On"), tr("Off")), s, LAMP});
  }
  if (has("sensor")) {
    double const t = value("temp");
    list.push_back({tr("Chamber sensor"), valueText(t, 1, QStringLiteral(" °C")), std::isnan(t) ? State::OFF : State::ACTIVE, PROBE});
  }
  if (has("fan")) {
    State const s = outputState("fan");
    list.push_back({tr("Chamber fan"), stateText(s, tr("Running"), tr("Stopped")), s, FAN});
  }
  double const u{value("temp_u")};
  if (has("evap23") || has("evap404") || has("comp23")) {
    bool const  cooling = on("evap23") || on("comp23");
    State const s       = !online() ? State::OFF : (cooling ? State::ACTIVE : State::READY);
    list.push_back({tr("Evaporator"), stateText(s, tr("Cooling"), tr("Idle")), s, QPointF(EVAPORATOR.center().x(), 180)});
  }
  if (has("heater")) {
    State const   s       = outputState("heater");
    QString const percent = valueText(std::isnan(u) ? u : std::max(0.0, u), 0, QStringLiteral(" %"));
    list.push_back({tr("Heater"), stateText(s, tr("Active · %1").arg(percent), tr("Ready · %1").arg(percent)), s, QPointF(425, HEATER_Y)});
  }
  if (has("comp23") || has("comp404")) {
    bool const    running = on("comp23") || on("comp404");
    State const   s       = !online() ? State::OFF : (running ? State::ACTIVE : State::READY);
    QString const percent = valueText(std::isnan(u) ? u : std::min(0.0, u), 0, QStringLiteral(" %"));
    list.push_back({tr("Refrigeration unit"), stateText(s, tr("Active · %1").arg(percent), tr("Ready")), s,
                    QPointF(EXCHANGER.right(), EXCHANGER.center().y())});
  }
  {
    QString const text = APP_STATE_LIST.value(static_cast<int>(m_resHolder->status->getAppState()));
    list.push_back({tr("PLC"), text, online() ? State::ACTIVE : State::OFF, QPointF(PLC.left(), PLC.center().y())});
  }
  return list;
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

  // title, legend and note in widget coordinates, at the panel edges
  QFont label = font();
  p.setFont(label);
  p.setPen(c.muted);
  p.drawText(QPointF(14 * s, 24 * s), tr("Chamber · live"));
  {
    QFont small = font();
    small.setPointSizeF(font().pointSizeF() * 0.8);
    QFontMetricsF const fm(small);
    QList<std::pair<State, QString>> const items{{State::ACTIVE, tr("Active")}, {State::READY, tr("Ready")}, {State::OFF, tr("Off")}};
    double width{0};
    for (auto const &item : items) {
      width += 16 * s + fm.horizontalAdvance(item.second);
    }
    double x = panel.right() - 14 * s - width;
    p.setFont(small);
    for (auto const &[state, text] : items) {
      QColor const color = state == State::ACTIVE ? c.active : (state == State::READY ? c.ready : c.off);
      p.setPen(state == State::READY ? QPen(color, 1.4 * s) : Qt::NoPen);
      p.setBrush(state == State::READY ? Qt::NoBrush : QBrush(color));
      p.drawEllipse(QPointF(x + 3 * s, 20 * s), 3 * s, 3 * s);
      p.setPen(c.muted);
      p.drawText(QPointF(x + 9 * s, 24 * s), text);
      x += 16 * s + fm.horizontalAdvance(text);
    }
    p.drawText(QPointF(14 * s, panel.bottom() - 10 * s), online() ? tr("Live view · components update while ONLINE")
                                                                    : tr("Live view · waiting for the chamber (not ONLINE)"));
  }

  // sketch and cards: canvas content (0, 50)-(1000, 595) fitted into the panel
  QRectF const content(0, 50, CANVAS_W, 545);
  QRectF const area = QRectF(rect()).adjusted(10 * s, 34 * s, -10 * s, -24 * s);
  double const scale = std::min(area.width() / content.width(), area.height() / content.height());
  m_transform        = QTransform();
  m_transform.translate(area.center().x() - content.center().x() * scale, area.center().y() - content.center().y() * scale);
  m_transform.scale(scale, scale);
  p.setTransform(m_transform);
  m_tips.clear();

  drawCards(p); // leader lines under the sketch, cards on top of everything right of it
  drawRefrigeration(p);
  drawVessel(p);

  // anchor dots of the cards above the sketch
  for (Card const &card : cards()) {
    QColor const color = card.state == State::ACTIVE ? c.active : (card.state == State::READY ? c.ready : c.off);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawEllipse(card.anchor, 3.2, 3.2);
  }
}

void ChamberView::drawPipe(QPainter &p, QVector<QPointF> const &points, QColor const &color, bool flowing) {
  QPainterPath path;
  path.moveTo(points.first());
  for (int i{1}; i < points.size(); ++i) {
    path.lineTo(points[i]);
  }
  // casing and fluid like the insulated lines of the reference drawing
  p.setBrush(Qt::NoBrush);
  p.setPen(QPen(QColor(120, 132, 146), 7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  p.drawPath(path);
  p.setPen(QPen(QColor(205, 212, 220), 5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  p.drawPath(path);
  p.setPen(QPen(flowing ? color : color.darker(170), 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  p.drawPath(path);
  if (flowing) {
    QPen dash(color.lighter(150), 1.6, Qt::CustomDashLine, Qt::FlatCap, Qt::RoundJoin);
    dash.setDashPattern({3, 5});
    dash.setDashOffset(-m_phase * 1.2);
    p.setPen(dash);
    p.drawPath(path);
  }
}

void ChamberView::drawBadge(QPainter &p, QPointF const &topLeft, QString const &text, bool on) {
  QFont const          font = canvasFont(10, true);
  double const         w    = QFontMetricsF(font).horizontalAdvance(text) + 12;
  QRectF const         r(topLeft, QSizeF(w, 15));
  p.setPen(Qt::NoPen);
  p.setBrush(on ? QColor(34, 197, 94) : QColor(100, 116, 139));
  p.drawRoundedRect(r, 3, 3);
  p.setPen(on ? QColor(5, 46, 22) : QColor(226, 232, 240));
  p.setFont(font);
  p.drawText(r, Qt::AlignCenter, text);
}

void ChamberView::drawReadout(QPainter &p, QPointF const &topLeft, QString const &tag, QString const &text, bool live) {
  theme::Colors const &c = theme::colors();
  QFont const          tagFont  = canvasFont(10, true);
  QFont const          textFont = canvasFont(12, true);
  double const         w        = 22 + QFontMetricsF(tagFont).horizontalAdvance(tag) + QFontMetricsF(textFont).horizontalAdvance(text);
  QRectF const         r(topLeft, QSizeF(w, 22));
  p.setPen(QPen(QColor(71, 85, 105), 1));
  p.setBrush(QColor(15, 23, 42, 230));
  p.drawRoundedRect(r, 4, 4);
  p.setPen(Qt::NoPen);
  p.setBrush(live ? c.active : c.off);
  p.drawEllipse(QPointF(r.left() + 8, r.center().y()), 3, 3);
  p.setFont(tagFont);
  p.setPen(QColor(203, 213, 225));
  double const tagW = QFontMetricsF(tagFont).horizontalAdvance(tag);
  p.drawText(QRectF(r.left() + 15, r.top(), tagW, r.height()), Qt::AlignVCenter, tag);
  p.setFont(textFont);
  p.setPen(Qt::white);
  p.drawText(QRectF(r.left() + 19 + tagW, r.top(), w, r.height()), Qt::AlignVCenter, text);
}

void ChamberView::drawValve(QPainter &p, QPointF const &at, QString const &key, QString const &tag, bool vertical,
                            QPointF const &labelOffset) {
  if (!has(key)) {
    return;
  }
  bool const open = on(key);
  p.save();
  p.translate(at);
  if (vertical) {
    p.rotate(90);
  }
  // bow tie body and actuator, as in the reference drawing
  p.setPen(QPen(QColor(71, 85, 105), 1));
  p.setBrush(open ? QColor(34, 197, 94) : QColor(148, 163, 184));
  p.drawPolygon(QPolygonF({QPointF(-9, -7), QPointF(0, 0), QPointF(-9, 7)}));
  p.drawPolygon(QPolygonF({QPointF(9, -7), QPointF(0, 0), QPointF(9, 7)}));
  p.setBrush(QColor(203, 213, 225));
  p.drawLine(QPointF(0, 0), QPointF(0, -10));
  p.drawRect(QRectF(-5, -15, 10, 5));
  p.restore();
  // tag and state
  QPointF const label = at + labelOffset;
  p.setFont(canvasFont(10, true));
  p.setPen(theme::colors().text);
  p.drawText(label, tag);
  double const tagW = QFontMetricsF(canvasFont(10, true)).horizontalAdvance(tag);
  drawBadge(p, QPointF(label.x() + tagW + 4, label.y() - 11.5), open ? tr("OPEN") : tr("CLOSED"), open);
  addTip(QRectF(at - QPointF(12, 12), QSizeF(24, 24)), m_outputs.value(key).label);
}

void ChamberView::drawCompressor(QPainter &p, QPointF const &at, QString const &key, QString const &title, QString const &tag) {
  if (!has(key)) {
    return;
  }
  bool const running = on(key);
  // base plate and motor body
  p.setPen(QPen(QColor(71, 85, 105), 1));
  QLinearGradient base(at.x(), at.y() + COMP_R, at.x(), at.y() + COMP_R + 8);
  base.setColorAt(0, QColor(148, 163, 184));
  base.setColorAt(1, QColor(71, 85, 105));
  p.setBrush(base);
  p.drawRoundedRect(QRectF(at.x() - COMP_R - 6, at.y() + COMP_R - 2, 2 * COMP_R + 12, 8), 2, 2);
  QRadialGradient body(at - QPointF(8, 8), COMP_R * 1.4);
  body.setColorAt(0, QColor(241, 245, 249));
  body.setColorAt(0.6, QColor(160, 172, 186));
  body.setColorAt(1, QColor(90, 102, 118));
  p.setBrush(body);
  p.setPen(QPen(QColor(51, 65, 85), 1.4));
  p.drawEllipse(at, COMP_R, COMP_R);
  p.setBrush(QColor(71, 85, 105));
  p.drawEllipse(at, COMP_R * 0.55, COMP_R * 0.55);
  // impeller
  p.save();
  p.translate(at);
  p.rotate(running ? m_phase * 24 : 0);
  p.setPen(QPen(QColor(226, 232, 240), 2));
  for (int i{0}; i < 4; ++i) {
    p.drawLine(QPointF(0, 0), QPointF(COMP_R * 0.45, 0));
    p.rotate(90);
  }
  p.restore();
  p.setBrush(QColor(226, 232, 240));
  p.setPen(Qt::NoPen);
  p.drawEllipse(at, 3, 3);
  // title, tag, state
  QPointF const label(at.x() + COMP_R + 8, at.y() - 4);
  p.setFont(canvasFont(11));
  p.setPen(theme::colors().text);
  p.drawText(label, title);
  p.setFont(canvasFont(10, true));
  p.drawText(label + QPointF(0, 17), tag);
  double const tagW = QFontMetricsF(canvasFont(10, true)).horizontalAdvance(tag);
  drawBadge(p, label + QPointF(tagW + 5, 5.5), running ? tr("RUNNING") : tr("OFF"), running);
  addTip(QRectF(at - QPointF(COMP_R, COMP_R), QSizeF(2 * COMP_R, 2 * COMP_R)), m_outputs.value(key).label);
}

void ChamberView::drawRefrigeration(QPainter &p) {
  theme::Colors const &c = theme::colors();
  bool const           run23{on("comp23")}, run404{on("comp404")};
  QColor const         cold = c.cold, hot = c.heat, liquid = QColor("#7dd3fc");
  if (has("comp23")) {
    // R23 stage: evaporator in the chamber -> compressor -> cascade exchanger -> back to the evaporator
    drawPipe(p, {QPointF(EVAPORATOR.left(), 200), QPointF(COMP23.x(), 200), QPointF(COMP23.x(), COMP23.y() - COMP_R)}, cold, run23);
    drawPipe(p, {QPointF(COMP23.x(), COMP23.y() + COMP_R), QPointF(COMP23.x(), EXCHANGER.top())}, hot, run23);
    drawPipe(p, {QPointF(EXCHANGER.right(), 348), QPointF(240, 348), QPointF(240, 262), QPointF(EVAPORATOR.left(), 262)}, liquid, run23);
    if (has("bypass23")) {
      drawPipe(p, {QPointF(COMP23.x(), 318), QPointF(55, 318), QPointF(55, 200), QPointF(COMP23.x(), 200)}, hot, on("bypass23"));
    }
  }
  if (has("comp404")) {
    // R404A stage: cascade exchanger (its evaporator) -> compressor -> condenser -> back to the exchanger
    drawPipe(p, {QPointF(EXCHANGER.left(), 357), QPointF(45, 357), QPointF(45, COMP404.y()), QPointF(COMP404.x() - COMP_R, COMP404.y())}, cold, run404);
    drawPipe(p, {QPointF(COMP404.x(), COMP404.y() + COMP_R + 6), QPointF(COMP404.x(), CONDENSER.center().y()), QPointF(CONDENSER.left(), CONDENSER.center().y())}, hot, run404);
    drawPipe(p, {QPointF(232, CONDENSER.top()), QPointF(232, 366), QPointF(EXCHANGER.right(), 366)}, liquid, run404);
    if (has("bypass404")) {
      drawPipe(p, {QPointF(COMP404.x(), 512), QPointF(70, 512), QPointF(70, COMP404.y())}, hot, on("bypass404"));
    }
  }

  // cascade heat exchanger
  if (has("comp23") || has("comp404")) {
    QLinearGradient g(EXCHANGER.topLeft(), EXCHANGER.bottomLeft());
    g.setColorAt(0, QColor(203, 213, 225));
    g.setColorAt(1, QColor(100, 116, 139));
    p.setPen(QPen(QColor(51, 65, 85), 1.2));
    p.setBrush(g);
    p.drawRoundedRect(EXCHANGER, 4, 4);
    p.setPen(QPen(QColor(71, 85, 105), 1));
    for (double x = EXCHANGER.left() + 8; x < EXCHANGER.right() - 4; x += 6) {
      p.drawLine(QPointF(x, EXCHANGER.top() + 5), QPointF(x, EXCHANGER.bottom() - 5));
    }
    p.setFont(canvasFont(10, true));
    p.setPen(QColor(15, 23, 42));
    p.drawText(EXCHANGER, Qt::AlignCenter, QStringLiteral("HX"));
    addTip(EXCHANGER, tr("Cascade heat exchanger (R23 condenser / R404A evaporator)"));
  }
  if (has("comp404")) {
    QLinearGradient g(CONDENSER.topLeft(), CONDENSER.bottomLeft());
    g.setColorAt(0, QColor(203, 213, 225));
    g.setColorAt(1, QColor(100, 116, 139));
    p.setPen(QPen(QColor(51, 65, 85), 1.2));
    p.setBrush(g);
    p.drawRoundedRect(CONDENSER, 3, 3);
    p.setPen(QPen(QColor(71, 85, 105), 1));
    for (double x = CONDENSER.left() + 6; x < CONDENSER.right() - 3; x += 5) {
      p.drawLine(QPointF(x, CONDENSER.top() + 4), QPointF(x, CONDENSER.bottom() - 4));
    }
    p.setFont(canvasFont(10));
    p.setPen(c.muted);
    p.drawText(QPointF(CONDENSER.right() + 6, CONDENSER.center().y() + 4), tr("Condenser"));
    addTip(CONDENSER, tr("R404A condenser"));
  }

  drawCompressor(p, COMP23, "comp23", tr("R23 compressor"), QStringLiteral("C-23"));
  drawCompressor(p, COMP404, "comp404", tr("R404A compressor"), QStringLiteral("C-404"));

  // valves (tags as on the reference drawing; full names in the tool tips)
  drawValve(p, QPointF(240, 305), "evap23", QStringLiteral("EV-23"), true, QPointF(-64, -14));
  drawValve(p, QPointF(195, 348), "sub23", QStringLiteral("SC-23"), false, QPointF(-40, 32));
  drawValve(p, QPointF(55, 262), "bypass23", QStringLiteral("BP-23"), true, QPointF(-46, 28));
  drawValve(p, QPointF(45, 410), "hx404", QStringLiteral("HX-404"), true, QPointF(-36, -12));
  drawValve(p, QPointF(232, 412), "evap404", QStringLiteral("EV-404"), true, QPointF(-104, 4));
  drawValve(p, QPointF(232, 504), "sub404", QStringLiteral("SC-404"), true, QPointF(-104, 4));
  drawValve(p, QPointF(70, 512), "bypass404", QStringLiteral("BP-404"), false, QPointF(-56, 22));

  // R23 discharge pressure
  if (has("pressure") && has("comp23")) {
    QPointF const gauge(146, 318);
    p.setPen(QPen(QColor(120, 132, 146), 2));
    p.drawLine(QPointF(COMP23.x(), 318), gauge - QPointF(9, 0));
    p.setPen(QPen(QColor(51, 65, 85), 1.2));
    p.setBrush(QColor(241, 245, 249));
    p.drawEllipse(gauge, 9, 9);
    double const pressure = value("pres_23_high");
    double const angle    = std::isnan(pressure) ? 220.0 : 220.0 - std::clamp(pressure / 25.0, 0.0, 1.0) * 260.0;
    p.setPen(QPen(QColor(220, 38, 38), 1.4));
    p.drawLine(gauge, gauge + QPointF(std::cos(qDegreesToRadians(angle)) * 7, -std::sin(qDegreesToRadians(angle)) * 7));
    drawReadout(p, QPointF(160, 306), QStringLiteral("P"), valueText(pressure, 1, QStringLiteral(" bar")), !std::isnan(pressure));
    addTip(QRectF(gauge - QPointF(10, 10), QSizeF(20, 20)), m_resHolder->states->getSysValue("pres_23_high")->getLabel());
  }

  // PLC
  p.setPen(QPen(QColor(100, 116, 139), 1.4, Qt::DashLine));
  p.drawLine(QPointF(PLC.center().x(), PLC.top()), QPointF(PLC.center().x(), 360));
  p.drawLine(QPointF(PLC.center().x(), 360), QPointF(VESSEL.right(), 360));
  QLinearGradient plc(PLC.topLeft(), PLC.bottomLeft());
  plc.setColorAt(0, QColor(71, 85, 105));
  plc.setColorAt(1, QColor(30, 41, 59));
  p.setPen(QPen(QColor(100, 116, 139), 1));
  p.setBrush(plc);
  p.drawRoundedRect(PLC, 4, 4);
  p.setPen(Qt::NoPen);
  p.setBrush(online() ? c.active : c.danger);
  p.drawEllipse(QPointF(PLC.left() + 10, PLC.center().y()), 3.5, 3.5);
  p.setFont(canvasFont(12, true));
  p.setPen(QColor(226, 232, 240));
  p.drawText(PLC.adjusted(16, 0, 0, 0), Qt::AlignCenter, QStringLiteral("PLC"));
}

void ChamberView::drawVessel(QPainter &p) {
  theme::Colors const &c       = theme::colors();
  bool const           cooling = on("comp23") || on("comp404");
  bool const           heating = on("heater");

  // legs and top nozzle
  QLinearGradient steel(VESSEL.left(), 0, VESSEL.right(), 0);
  steel.setColorAt(0, QColor(120, 131, 145));
  steel.setColorAt(0.3, QColor(226, 232, 240));
  steel.setColorAt(0.65, QColor(170, 180, 192));
  steel.setColorAt(1, QColor(96, 107, 121));
  p.setPen(QPen(QColor(51, 65, 85), 1.2));
  p.setBrush(steel);
  for (double x : {290.0, 510.0}) {
    p.drawPolygon(QPolygonF({QPointF(x - 10, VESSEL.bottom() - 4), QPointF(x + 10, VESSEL.bottom() - 4), QPointF(x + 14, 428),
                             QPointF(x - 14, 428)}));
    p.drawRoundedRect(QRectF(x - 24, 428, 48, 6), 2, 2);
  }
  p.drawRect(QRectF(VESSEL.center().x() - 9, VESSEL.top() - 14, 18, 16));
  p.drawRoundedRect(QRectF(VESSEL.center().x() - 15, VESSEL.top() - 18, 30, 6), 2, 2);

  // shell
  p.setBrush(steel);
  p.setPen(QPen(QColor(51, 65, 85), 1.6));
  p.drawRoundedRect(VESSEL, 30, 30);
  // door frame with bolts
  QRectF const frame = SPACE.adjusted(-10, -10, 10, 10);
  p.setPen(QPen(QColor(71, 85, 105), 6));
  p.setBrush(Qt::NoBrush);
  p.drawRoundedRect(frame, 20, 20);
  p.setPen(Qt::NoPen);
  p.setBrush(QColor(71, 85, 105));
  for (double x = frame.left() + 26; x < frame.right() - 20; x += 24) {
    p.drawEllipse(QPointF(x, frame.top() - 7), 2.2, 2.2);
    p.drawEllipse(QPointF(x, frame.bottom() + 7), 2.2, 2.2);
  }
  for (double y = frame.top() + 28; y < frame.bottom() - 20; y += 24) {
    p.drawEllipse(QPointF(frame.left() - 7, y), 2.2, 2.2);
    p.drawEllipse(QPointF(frame.right() + 7, y), 2.2, 2.2);
  }
  // side flanges (hinge and latch)
  p.setBrush(QColor(100, 116, 139));
  p.setPen(QPen(QColor(51, 65, 85), 1));
  p.drawRoundedRect(QRectF(VESSEL.left() - 8, VESSEL.center().y() - 26, 10, 52), 2, 2);
  p.drawRoundedRect(QRectF(VESSEL.right() - 2, VESSEL.center().y() - 18, 10, 36), 2, 2);

  // test space and its thermal glow
  QLinearGradient inside(SPACE.topLeft(), SPACE.bottomLeft());
  inside.setColorAt(0, QColor(15, 30, 52));
  inside.setColorAt(1, QColor(10, 20, 36));
  p.setPen(QPen(QColor(30, 41, 59), 2));
  p.setBrush(inside);
  p.drawRoundedRect(SPACE, 14, 14);
  if (cooling || heating) {
    QColor const glow = heating ? QColor(249, 115, 22) : QColor(56, 189, 248);
    for (int i{0}; i < 5; ++i) {
      QColor g = glow;
      g.setAlpha(150 - i * 28);
      p.setPen(QPen(g, 2.2));
      p.setBrush(Qt::NoBrush);
      p.drawRoundedRect(SPACE.adjusted(2 + i * 2.2, 2 + i * 2.2, -2 - i * 2.2, -2 - i * 2.2), 13 - i, 13 - i);
    }
  }
  if (has("perimeter")) {
    QPen pen(on("perimeter") ? QColor(249, 115, 22) : QColor(100, 116, 139), 1.4, Qt::DashLine);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(SPACE.adjusted(-4, -4, 4, 4), 17, 17);
    addTip(QRectF(SPACE.left() - 6, SPACE.top() - 6, SPACE.width() + 12, 6), m_outputs.value("perimeter").label);
  }
  p.setFont(canvasFont(11));
  p.setPen(QColor(148, 163, 184));
  p.drawText(QPointF(SPACE.left() + 52, SPACE.top() + 26), tr("Test space"));

  // evaporator coil
  if (has("comp23") || has("evap23") || has("evap404")) {
    QColor const coil = cooling ? QColor(56, 189, 248) : QColor(100, 116, 139);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(30, 41, 59));
    p.drawRoundedRect(EVAPORATOR, 3, 3);
    p.setPen(QPen(coil.darker(130), 1));
    for (double y = EVAPORATOR.top() + 4; y < EVAPORATOR.bottom(); y += 6) {
      p.drawLine(QPointF(EVAPORATOR.left() + 2, y), QPointF(EVAPORATOR.right() - 2, y));
    }
    QPainterPath zig;
    zig.moveTo(EVAPORATOR.left() + 4, EVAPORATOR.top() + 4);
    bool right{true};
    for (double y = EVAPORATOR.top() + 14; y <= EVAPORATOR.bottom() - 4; y += 12) {
      zig.lineTo(right ? EVAPORATOR.right() - 4 : EVAPORATOR.left() + 4, y);
      right = !right;
    }
    p.setPen(QPen(coil, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawPath(zig);
    addTip(EVAPORATOR, tr("Evaporator"));
  }

  // fan
  if (has("fan")) {
    bool const running = on("fan");
    p.setPen(QPen(QColor(100, 116, 139), 1.2));
    p.setBrush(QColor(30, 41, 59));
    p.drawEllipse(FAN, 11, 11);
    p.save();
    p.translate(FAN);
    p.rotate(running ? m_phase * 30 : 15);
    p.setPen(Qt::NoPen);
    p.setBrush(running ? QColor(148, 210, 255) : QColor(100, 116, 139));
    for (int i{0}; i < 3; ++i) {
      QPainterPath blade;
      blade.moveTo(0, 0);
      blade.cubicTo(3, -4, 9, -5, 9, 0);
      blade.cubicTo(6, 1, 3, 1, 0, 0);
      p.drawPath(blade);
      p.rotate(120);
    }
    p.restore();
    addTip(QRectF(FAN - QPointF(11, 11), QSizeF(22, 22)), m_outputs.value("fan").label);
  }

  // heater coil
  if (has("heater")) {
    bool const   lit = on("heater");
    QPainterPath coil;
    coil.moveTo(340, HEATER_Y);
    bool up{true};
    for (double x = 348; x <= 512; x += 8) {
      coil.lineTo(x, HEATER_Y + (up ? -6 : 6));
      up = !up;
    }
    if (lit) {
      p.setPen(QPen(QColor(249, 115, 22, 90), 7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
      p.drawPath(coil);
    }
    p.setPen(QPen(lit ? QColor(251, 146, 60) : QColor(146, 64, 14), 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawPath(coil);
    addTip(QRectF(336, HEATER_Y - 10, 180, 20), m_outputs.value("heater").label);
  }

  // lamp
  if (has("light")) {
    bool const lit = on("light");
    if (lit) {
      QRadialGradient glow(LAMP, 46);
      glow.setColorAt(0, QColor(253, 224, 71, 110));
      glow.setColorAt(1, QColor(253, 224, 71, 0));
      p.setPen(Qt::NoPen);
      p.setBrush(glow);
      p.drawEllipse(LAMP, 46, 46);
    }
    p.setPen(QPen(QColor(100, 116, 139), 1));
    p.setBrush(QColor(71, 85, 105));
    p.drawRoundedRect(QRectF(LAMP.x() - 16, SPACE.top() + 2, 32, 5), 2, 2);
    p.setBrush(lit ? QColor(253, 230, 138) : QColor(71, 85, 105));
    p.drawEllipse(LAMP, 6, 6);
    addTip(QRectF(LAMP - QPointF(10, 10), QSizeF(20, 20)), m_outputs.value("light").label);
  }

  // temperature probe and reading
  if (has("sensor")) {
    double const t = value("temp");
    p.setPen(QPen(QColor(148, 163, 184), 1.6));
    p.drawLine(QPointF(PROBE.x(), SPACE.top()), PROBE - QPointF(0, 4));
    p.setPen(QPen(QColor(15, 23, 42), 1));
    p.setBrush(std::isnan(t) ? c.off : QColor(34, 211, 238));
    p.drawEllipse(PROBE, 4, 4);
    drawReadout(p, QPointF(PROBE.x() - 54, PROBE.y() + 12), QStringLiteral("T1"), valueText(t, 1, QStringLiteral(" °C")), !std::isnan(t));
    addTip(QRectF(PROBE - QPointF(8, 8), QSizeF(16, 16)), m_resHolder->states->getSysValue("temp")->getLabel());
  }
}

void ChamberView::drawCards(QPainter &p) {
  theme::Colors const &c    = theme::colors();
  QVector<Card> const  list = cards();
  double const         top{64}, bottom{580}, left{622}, right{978};
  double const         h{48};
  double const         gap = list.size() > 1 ? std::min(18.0, (bottom - top - h * list.size()) / (list.size() - 1)) : 0.0;
  double const         blockH = h * list.size() + gap * (list.size() - 1);
  double               y      = top + (bottom - top - blockH) / 2.0;
  for (Card const &card : list) {
    QRectF const r(left, y, right - left, h);
    QColor const color = card.state == State::ACTIVE ? c.active : (card.state == State::READY ? c.ready : c.off);
    // leader line
    p.setPen(QPen(QColor(c.muted.red(), c.muted.green(), c.muted.blue(), 150), 1));
    p.setBrush(Qt::NoBrush);
    QPainterPath leader;
    leader.moveTo(r.left(), r.center().y());
    leader.lineTo(r.left() - 18, r.center().y());
    leader.lineTo(card.anchor);
    p.drawPath(leader);
    // card
    p.setPen(QPen(c.border, 1));
    p.setBrush(theme::isDark() ? c.window : c.panelAlt);
    p.drawRoundedRect(r, 6, 6);
    p.setPen(card.state == State::READY ? QPen(color, 1.8) : Qt::NoPen);
    p.setBrush(card.state == State::READY ? Qt::NoBrush : QBrush(color));
    p.drawEllipse(QPointF(r.left() + 20, r.center().y()), 5, 5);
    p.setFont(canvasFont(15, true));
    p.setPen(c.text);
    p.drawText(r.adjusted(36, 0, -10, 0), Qt::AlignVCenter | Qt::AlignLeft, card.title);
    p.setFont(canvasFont(13));
    p.setPen(card.state == State::OFF ? c.muted : color);
    p.drawText(r.adjusted(36, 0, -14, 0), Qt::AlignVCenter | Qt::AlignRight, card.status);
    y += h + gap;
  }
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
