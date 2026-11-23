#include "livestatuspanel.h"

#include <QCoreApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QResizeEvent>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <limits>

#include "constants.h"
#include "experimentrecorder.h"
#include "pressureunits.h"
#include "statusmanager.h"
#include "stopbutton.h"
#include "theme.h"
#include "utils.h"

namespace deepvaccontrol {

namespace {
QString valueText(float value, int prec, QString const &unit = {}) {
  if (std::isnan(value) || (value < -10e9) || (value > 10e9)) {
    return QStringLiteral("--");
  }
  return QString::number(value, 'f', prec).replace('-', QChar(0x2212)) + unit;
}
}

/**
 * @brief A reading's state, top right of its shaded box: the icon of what it does, moving with the control output (the
 * snowflake turns, the flame flickers, the pump's rotor spins, the air flows in), faster at a higher output; under it a
 * small vertical bar filled from the bottom as far as the output's share. Still at 0 % (standby); the bar empty without
 * an output.
 */
class StateIcon : public QWidget {
public:
  StateIcon(bool withBar, QWidget *parent) : QWidget{parent}, m_withBar{withBar}, m_timer{new QTimer(this)} {
    setFixedSize(theme::dp(32), withBar ? theme::dp(32 + 8 + 48) : theme::dp(32));
    m_timer->setInterval(33); // 30 frames a second
    QObject::connect(m_timer, &QTimer::timeout, this, [this] {
      m_phase += 0.033 * cyclesPerSecond();
      update();
    });
  }

  // the icon (a theme icon kind), its colour (also the bar's) and the control output, -100 … 100 % (NaN: none)
  void setState(QString const &kind, QColor const &color, double output) {
    double const u{std::isnan(output) ? output : std::clamp(output, -100.0, 100.0)};
    bool const   sameOutput{(u == m_output) || (std::isnan(u) && std::isnan(m_output))};
    if ((kind == m_kind) && (color == m_color) && sameOutput) {
      return;
    }
    if ((kind != m_kind) || (color != m_color)) {
      m_pixmap = QPixmap();
    }
    m_kind   = kind;
    m_color  = color;
    m_output = u;
    run();
    update();
  }

  // drawn again (the theme changed)
  void refresh() {
    m_pixmap = QPixmap();
    update();
  }

protected:
  void paintEvent(QPaintEvent *) override {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    double const size{static_cast<double>(theme::dp(32))};
    if (m_pixmap.isNull() && !m_kind.isEmpty()) {
      m_pixmap = theme::menuIcon(m_kind, m_color, m_color, theme::dp(32)).pixmap(QSize(theme::dp(32), theme::dp(32)), devicePixelRatioF());
    }
    double const wave{std::sin(2.0 * M_PI * m_phase)};
    p.save();
    p.translate(size / 2, size / 2);
    if (moving()) {
      if (m_kind == QLatin1String("snowflake")) {
        p.rotate(std::fmod(m_phase, 1.0) * 60.0); // a sixth of a turn a cycle: it looks the same after each
      } else if (m_kind == QLatin1String("flame")) {
        // grows and shrinks from its base, a second, quicker wave on top
        double const flicker{0.6 * wave + 0.4 * std::sin(2.0 * M_PI * m_phase * 2.7)};
        p.translate(0, size * 0.4);
        p.scale(1.0 - 0.04 * flicker, 1.0 + 0.10 * flicker);
        p.translate(0, -size * 0.4);
      } else if (m_kind == QLatin1String("fan")) {
        p.rotate(std::fmod(m_phase, 1.0) * 90.0); // the rotor: a quarter turn a cycle (four blades)
      } else if (m_kind == QLatin1String("air")) {
        p.translate(theme::dp(3) * wave, 0); // the air drifting in and back
      }
    }
    p.drawPixmap(QRectF(-size / 2, -size / 2, size, size), m_pixmap, QRectF(m_pixmap.rect()));
    p.restore();
    if (!m_withBar) {
      return;
    }
    // the bar: its track, the output's share from the bottom
    double const thick{static_cast<double>(theme::dp(6))};
    QRectF const track((width() - thick) / 2, size + theme::dp(8), thick, theme::dp(48));
    QColor       faint{m_color};
    faint.setAlphaF(0.18);
    p.setPen(Qt::NoPen);
    p.setBrush(faint);
    p.drawRoundedRect(track, thick / 2, thick / 2);
    double const filled{share() * track.height()};
    if (filled >= 1.0) {
      p.setBrush(m_color);
      p.drawRoundedRect(QRectF(track.left(), track.bottom() - filled, thick, filled), thick / 2, thick / 2);
    }
  }

  void showEvent(QShowEvent *event) override {
    QWidget::showEvent(event);
    run();
  }

  void hideEvent(QHideEvent *event) override {
    QWidget::hideEvent(event);
    m_timer->stop();
  }

private:
  [[nodiscard]] double share() const { return std::isnan(m_output) ? 0.0 : std::abs(m_output) / 100.0; }
  [[nodiscard]] bool   moving() const { return share() >= 0.005; }
  // slow at a low output, about one and a half a second at 100 %
  [[nodiscard]] double cyclesPerSecond() const { return 0.25 + 1.25 * share(); }

  void run() {
    if (isVisible() && moving()) {
      m_timer->start();
    } else {
      m_timer->stop();
    }
  }

  bool    m_withBar;
  QTimer *m_timer;
  QString m_kind;
  QColor  m_color;
  QPixmap m_pixmap;
  double  m_output{std::numeric_limits<double>::quiet_NaN()};
  double  m_phase{0.0};
};

LiveStatusPanel::LiveStatusPanel(ResHolder *resHolder, QWidget *parent) : QFrame{parent}, m_resHolder{resHolder} {
  setObjectName("liveStatusCard");
  initGui();

  connect(m_resHolder->states, &SystemValueList::ValuesUpdated, this, &LiveStatusPanel::updateGui);
  connect(m_resHolder->hard_status, &HardwareStatus::statusChanged, this, &LiveStatusPanel::updateGui);
  connect(m_resHolder->status, &StatusManager::appStateChanged, this, &LiveStatusPanel::updateGui);
  connect(m_resHolder->users, &AccessManager::userSelected, this, &LiveStatusPanel::updateGui);
  // the job, whatever started it
  connect(m_stopBtn, &StopButton::confirmPressed, this, [this] {
    m_resHolder->tcpGate->sendTextCmd("stop");
    m_resHolder->actions->addAction(ActionManager::ActionType::PLC, tr("Chamber operation stopped by user"));
  });
  connect(m_nextBtn, &QPushButton::clicked, this, [this] { m_resHolder->tcpGate->sendTextCmd("next"); });
  // the test time every second, whenever the chamber's values come
  m_secondTick = new QTimer(this);
  m_secondTick->setInterval(250);
  connect(m_secondTick, &QTimer::timeout, this, &LiveStatusPanel::showTestTime);
  m_secondTick->start();
  restyle();
}

void LiveStatusPanel::showTestTime() {
  if (std::isnan(m_totalBase)) {
    m_totalShown = -1;
    m_time.value->setText(QStringLiteral("--:--:--"));
    return;
  }
  long seconds{static_cast<long>(m_totalBase + static_cast<double>(m_totalSince.elapsed()) / 1000.0)};
  // not back by a second when the chamber's value comes in (a new job starting from 0 does go back)
  if ((seconds < m_totalShown) && (seconds > m_totalShown - 3)) {
    seconds = m_totalShown;
  }
  if (seconds != m_totalShown) {
    m_totalShown = seconds;
    m_time.value->setText(utils::timeFromSec(seconds, true));
  }
}

LiveStatusPanel::Tile LiveStatusPanel::addTile(QString const &caption, QString const &firstCaption, QString const &secondCaption,
                                               bool withBar) {
  Tile  tile;
  auto *frame = new QFrame(this);
  frame->setObjectName("liveTile");
  auto *column = new QVBoxLayout(frame);
  column->setContentsMargins(theme::dp(12), theme::dp(12), theme::dp(12), theme::dp(16));
  column->setSpacing(theme::dp(16));
  auto muted = [](QString const &text, QWidget *parent) {
    auto *label = new QLabel(text, parent);
    label->setProperty("muted", true);
    label->setProperty("caption", true);
    return label;
  };

  // the shaded box, taking the tile's spare height: the caption and the reading; right, the state icon (with the
  // control output's bar under it)
  auto *main = new QFrame(frame);
  main->setObjectName("liveTileMain");
  auto *mainLay = new QHBoxLayout(main);
  mainLay->setContentsMargins(theme::gap(), theme::dp(12), theme::gap(), theme::dp(16));
  mainLay->setSpacing(theme::dp(8));
  auto *texts = new QVBoxLayout();
  texts->setSpacing(theme::dp(4));
  tile.caption = muted(caption, main);
  tile.value   = new QLabel(main);
  texts->addWidget(tile.caption);
  texts->addWidget(tile.value);
  texts->addStretch(1);
  mainLay->addLayout(texts, 1);
  tile.icon = new StateIcon(withBar, main);
  mainLay->addWidget(tile.icon, 0, Qt::AlignTop);
  column->addWidget(main);

  // its details under it, side by side with a line between
  auto *details = new QHBoxLayout();
  details->setSpacing(0);
  auto detail = [&](QString const &text, QLabel *&captionLabel, QLabel *&valueLabel) {
    auto *lay = new QVBoxLayout();
    lay->setContentsMargins(theme::gap() - theme::dp(4), 0, theme::dp(8), 0);
    lay->setSpacing(theme::dp(4));
    captionLabel = muted(text, frame);
    valueLabel   = new QLabel(frame);
    lay->addWidget(captionLabel);
    lay->addWidget(valueLabel);
    return lay;
  };
  details->addLayout(detail(firstCaption, tile.firstCaption, tile.first), 1);
  auto *line = new QFrame(frame);
  line->setObjectName("cardDivider");
  line->setFixedWidth(theme::dp(1));
  details->addWidget(line);
  details->addLayout(detail(secondCaption, tile.secondCaption, tile.second), 1);
  column->addLayout(details);

  layout()->addWidget(frame);
  return tile;
}

void LiveStatusPanel::initGui() {
  auto *lay = new QVBoxLayout(this);
  lay->setContentsMargins(0, 0, 0, 0); // the side column's card holds it
  lay->setSpacing(theme::gap());

  // the status, the running experiment's name beside it (right, cut to the room left)
  auto *head   = new QHBoxLayout();
  head->setSpacing(theme::dp(8));
  m_statusDot  = new QLabel(this);
  m_statusDot->setFixedSize(theme::dp(10), theme::dp(10));
  m_statusText = new QLabel(this);
  m_experiment = new QLabel(this);
  m_experiment->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  m_experiment->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  head->addWidget(m_statusDot, 0, Qt::AlignVCenter);
  head->addWidget(m_statusText);
  head->addSpacing(theme::dp(8));
  head->addWidget(m_experiment, 1);
  lay->addLayout(head);

  m_temp = addTile(tr("Temperature — °C"), tr("Control output"), tr("Setpoint °C"), true);
  m_pres = addTile(tr("Pressure — Torr"), tr("Control output"), tr("Setpoint Torr"), true);
  m_time = addTile(tr("Total test time"), tr("Current interval"), tr("Cycle"), false);
  lay->addStretch(1); // the room left: above the buttons, which stay at the bottom

  // Stop (the job, whatever started it) and Next step (a program's next interval)
  auto *buttons = new QHBoxLayout();
  buttons->setSpacing(theme::gap());
  m_stopBtn = new StopButton(this);
  m_stopBtn->setText(tr("STOP"));
  m_nextBtn = new QPushButton(tr("Next step"), this);
  for (QPushButton *button : {static_cast<QPushButton *>(m_stopBtn), m_nextBtn}) {
    button->setFixedHeight(theme::dp(56));
    button->setMinimumWidth(theme::dp(110));
    button->setMaximumWidth(QWIDGETSIZE_MAX); // StopButton fixes its own size
    button->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed); // as wide as each other, whatever their text
    buttons->addWidget(button, 1);
  }
  lay->addLayout(buttons);
}


void LiveStatusPanel::showExperiment() {
  m_experiment->setText(m_experiment->fontMetrics().elidedText(m_experimentText, Qt::ElideRight, m_experiment->width()));
  m_experiment->setToolTip((m_experiment->text() != m_experimentText) ? m_experimentText : QString{});
}

void LiveStatusPanel::resizeEvent(QResizeEvent *event) {
  QFrame::resizeEvent(event);
  showExperiment();
}

void LiveStatusPanel::restyle() {
  static bool busy{false}; // style sheet changes may raise a palette change again
  if (busy) {
    return;
  }
  busy                   = true;
  theme::Colors const &c = theme::colors();
  int const            inner{theme::radius()}; // the shaded box's corners, as the tile's
  setStyleSheet(QStringLiteral(R"(
QFrame#liveStatusCard { background: transparent; border: none; border-radius: %4px; }
QFrame#liveTile { background: %1; border: %2px solid %3; border-radius: %5px; }
QFrame#liveTileMain { background: %6; border: none; border-radius: %7px; }
QLabel { color: %8; background: transparent; }
QLabel[muted="true"] { color: %9; }
)")
                  .arg(c.panel.name())
                  .arg(theme::dp(1))
                  .arg(c.border.name())
                  .arg(theme::radius())
                  .arg(theme::radius())
                  .arg((theme::isDark() ? c.panelAlt : c.window).name())
                  .arg(inner)
                  .arg(c.text.name(), c.muted.name()));
  QFont const base = QWidget::font();
  auto        sized = [&](double factor, bool bold) {
    QFont font = base;
    font.setPointSizeF(base.pointSizeF() * factor);
    font.setBold(bold);
    return font;
  };
  m_statusText->setFont(sized(1.0, true));
  m_experiment->setFont(sized(0.8, false));
  for (QLabel *label : findChildren<QLabel *>()) {
    if (label->property("caption").toBool()) {
      label->setFont(sized(0.62, false));
    }
  }
  // the readings and their details in the monospace face (digits of one width, a technical look); the readings
  // large, the test time a little smaller (eight characters)
  auto mono = [&](double factor) {
    QFont font(theme::monoFamily());
    font.setStyleHint(QFont::Monospace);
    font.setPointSizeF(base.pointSizeF() * factor);
    return font;
  };
  m_temp.value->setFont(mono(2.6));
  m_pres.value->setFont(mono(2.6));
  m_time.value->setFont(mono(2.0));
  for (Tile const *tile : {&m_temp, &m_pres, &m_time}) {
    tile->first->setFont(mono(0.85));
    tile->second->setFont(mono(0.85));
  }
  for (QPushButton *button : {static_cast<QPushButton *>(m_stopBtn), m_nextBtn}) {
    button->setFont(sized(0.85, true));
  }
  for (Tile const *tile : {&m_temp, &m_pres, &m_time}) {
    tile->icon->refresh(); // drawn again in the theme's colours
  }
  updateGui();
  busy = false;
}

void LiveStatusPanel::changeEvent(QEvent *event) {
  QFrame::changeEvent(event);
  if (event->type() == QEvent::PaletteChange) {
    restyle();
  }
}

void LiveStatusPanel::updateGui() {
  theme::Colors const &c      = theme::colors();
  auto const           states = m_resHolder->states;
  auto                 sys    = [states](char const *name) -> SystemValue * {
    return states->contains(name) ? states->getSysValue(name) : nullptr;
  };
  bool const allowed{AccessManager::AccessLevel::SPECTATOR_0 != m_resHolder->users->getCurrentAccessLevel()};

  HardwareStatus::HardwareStatusEnum const status{m_resHolder->hard_status->getHardwareStatus()};
  int const                                jobType{sys("work_type") ? static_cast<int>(sys("work_type")->getValue()) : 0};
  bool const                               working{HardwareStatus::HardwareStatusEnum::WORK == status};

  // status line: status and job type; under it the running experiment
  QString text = hardwareStatusList().value(static_cast<int>(status));
  if (jobType == HardwareStatus::JobType::SETPOINT) {
    text += QStringLiteral(" ") + QCoreApplication::translate("deepvaccontrol::StatusLabel", "(setpoint)");
  } else if (jobType == HardwareStatus::JobType::PROGRAM) {
    text += QStringLiteral(" ") + QCoreApplication::translate("deepvaccontrol::StatusLabel", "(program)");
  }
  m_statusText->setText(text);
  QColor const dot = working ? c.active : (status == HardwareStatus::ALARM ? c.danger : (status == HardwareStatus::IDLE ? c.ready : c.off));
  m_statusDot->setStyleSheet(QStringLiteral("background: %1; border-radius: %2px;").arg(dot.name()).arg(m_statusDot->width() / 2));
  QString const experiment = m_resHolder->recorder ? m_resHolder->recorder->runningJobName() : QString{};
  m_experimentText = experiment.isEmpty() ? tr("No experiment running") : experiment;
  showExperiment();
  if (m_experiment->property("muted").toBool() != experiment.isEmpty()) {
    m_experiment->setProperty("muted", experiment.isEmpty());
    m_experiment->style()->unpolish(m_experiment);
    m_experiment->style()->polish(m_experiment);
  }
  m_stopBtn->setEnabled(working && allowed);

  // temperature and pressure; not connected: the values are not the chamber's, a dash instead
  StatusManager::AppState const appState{m_resHolder->status ? m_resHolder->status->getAppState() : StatusManager::AppState::OFFLINE};
  bool const    online{(StatusManager::AppState::ONLINE == appState) || (StatusManager::AppState::DUMMY == appState)};
  QString const none{QStringLiteral("—")};
  SystemValue const *temp = online ? sys("temp") : nullptr;
  m_temp.value->setText(temp ? valueText(temp->getValue(), static_cast<int>(temp->getPrec())) : none);
  SystemValue const *tempRef = online ? sys("temp_ref") : nullptr;
  m_temp.second->setText(tempRef ? valueText(tempRef->getValue(), static_cast<int>(tempRef->getPrec())) : none);
  // pressure in Torr, whatever unit the chamber reports
  auto torr = [&](char const *name) {
    SystemValue const *value = online ? sys(name) : nullptr;
    return value ? pressure::toTorr(value->getValue(), sys("pres") ? sys("pres")->getLabel() : value->getLabel())
                 : std::numeric_limits<double>::quiet_NaN();
  };
  m_pres.value->setText(pressure::text(torr("pres")));
  m_pres.second->setText(pressure::text(torr("pres_ref")));
  // the control outputs, with what they do: "−50% Cooling", "50% Venting"
  auto output = [&](char const *name, QString const &low, QString const &high) {
    SystemValue const *out = online ? sys(name) : nullptr;
    double const       u{(out && (std::fabs(out->getValue()) < 1e9)) ? out->getValue() : std::numeric_limits<double>::quiet_NaN()};
    if (std::isnan(u)) {
      return std::pair{none, u};
    }
    QString const doing = (std::fabs(u) < 0.5) ? QString{} : QStringLiteral(" ") + (u < 0 ? low : high);
    return std::pair{valueText(static_cast<float>(u), 0, QStringLiteral("%")) + doing, u};
  };
  auto const [tempOutput, tempU] = output("temp_u", tr("Cooling"), tr("Heating"));
  m_temp.first->setText(tempOutput);
  auto const [presOutput, presU] = output("pres_u", tr("Pumping"), tr("Venting"));
  m_pres.first->setText(presOutput);
  // what each does, in the tile's corner, moving with its output: cooling / heating, pumping / venting, otherwise
  // standby (muted, still)
  if (tempU < -0.5) {
    m_temp.icon->setState(QStringLiteral("snowflake"), c.cold, tempU);
  } else if (tempU > 0.5) {
    m_temp.icon->setState(QStringLiteral("flame"), c.heat, tempU);
  } else {
    m_temp.icon->setState(QStringLiteral("thermometer"), c.muted, tempU);
  }
  if (presU < -0.5) {
    m_pres.icon->setState(QStringLiteral("fan"), c.accent, presU); // the pump's rotor
  } else if (presU > 0.5) {
    m_pres.icon->setState(QStringLiteral("air"), c.accent, presU);
  } else {
    m_pres.icon->setState(QStringLiteral("gauge"), c.muted, presU);
  }
  m_time.icon->setState(QStringLiteral("clock"), working ? c.accent : c.muted, std::numeric_limits<double>::quiet_NaN());

  // test time, and the job's position (a dash when none runs; a program of one interval runs as a setpoint job); Next
  // step while an interval or a cycle follows
  // the chamber's test time comes with its values (every few seconds): counted on from it, shown every second
  SystemValue const *total = sys("t_total");
  double const       totalNow{(working && total) ? static_cast<double>(total->getValue()) : std::numeric_limits<double>::quiet_NaN()};
  if (!((totalNow == m_totalBase) || (std::isnan(totalNow) && std::isnan(m_totalBase)))) {
    m_totalBase = totalNow;
    m_totalSince.restart();
  }
  showTestTime();
  bool canStep{false};
  if (working && sys("interval") && sys("intervals_count") && sys("cycle_cur") && sys("cycle_total")) {
    qint32 const intervalCur{static_cast<qint32>(sys("interval")->getValue())};
    qint32 const intervalCount{static_cast<qint32>(sys("intervals_count")->getValue())};
    qint32 const cycleCur{static_cast<qint32>(sys("cycle_cur")->getValue())};
    qint32 const cycleCount{static_cast<qint32>(sys("cycle_total")->getValue())};
    m_time.first->setText(tr("%1 of %2").arg(intervalCur).arg(intervalCount));
    m_time.second->setText(tr("%1 of %2").arg(cycleCur).arg(cycleCount));
    canStep = (intervalCur < intervalCount) || (cycleCur < cycleCount);
  } else {
    m_time.first->setText(none);
    m_time.second->setText(none);
  }
  m_nextBtn->setEnabled(canStep && allowed);
}

}
