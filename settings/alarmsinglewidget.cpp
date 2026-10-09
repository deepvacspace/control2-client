#include "alarmsinglewidget.h"

#include "theme.h"

#include <QButtonGroup>
#include <QEvent>
#include <QHBoxLayout>

#include "utils.h"

namespace deepvaccontrol {
AlarmSingleWidget::AlarmSingleWidget(QString const &label,
                                     SystemValue   *statusValue,
                                     SystemValue   *enableValue,
                                     SystemValue   *conditionValue,
                                     quint32        index,
                                     QWidget       *parent)
    : QFrame{parent}
    , m_label{label}
    , m_statusValue{statusValue}
    , m_enableValue{enableValue}
    , m_conditionValue{conditionValue}
    , m_index{index} {
  initGui();

  connect(statusValue, &SystemValue::valueChanged, this, &AlarmSingleWidget::updateStatusColors);
  connect(enableValue, &SystemValue::valueChanged, this, &AlarmSingleWidget::updateSelection);
  connect(conditionValue, &SystemValue::valueChanged, this, &AlarmSingleWidget::updateSelection);

  connect(onLock, &QPushButton::toggled, this, &AlarmSingleWidget::updateConfigValue);
  connect(onUnlock, &QPushButton::toggled, this, &AlarmSingleWidget::updateConfigValue);
  connect(off, &QPushButton::toggled, this, &AlarmSingleWidget::updateConfigValue);
}

void AlarmSingleWidget::updateStatusColors() {
  bool const           conditionStatus{utils::getBitOfFloat(m_statusValue->getValue(), m_index)};
  theme::Colors const &c = theme::colors();
  auto                 tint = [](QColor const &color, int alpha) {
    return QColor(color.red(), color.green(), color.blue(), alpha).name(QColor::HexArgb);
  };

  // input state
  QColor const stateColor = conditionStatus ? c.warning : c.off;
  curState->setText(conditionStatus ? tr("CLOSED") : tr("OPEN"));
  curState->setStyleSheet(QStringLiteral("color: %1; border: %2px solid %1; border-radius: %3px; background: %4; font-weight: bold;")
                            .arg(stateColor.name())
                            .arg(qMax(1, static_cast<int>(1 * utils::ssf())))
                            .arg(static_cast<int>(3 * utils::ssf()))
                            .arg(tint(stateColor, conditionStatus ? 40 : 0)));

  // the row is red while the alarm condition is met
  bool const triggered{getActive() && (conditionStatus == getPosition())};
  if (m_restyling || (m_triggered != static_cast<int>(triggered))) { // restyle only on a change (or a theme switch)
    m_triggered = static_cast<int>(triggered);
    setStyleSheet(triggered ? QStringLiteral("QFrame#outputRow { background: %1; }").arg(tint(c.danger, 45)) : QString());
  }
}

void AlarmSingleWidget::changeEvent(QEvent *event) {
  QFrame::changeEvent(event);
  if ((event->type() == QEvent::PaletteChange) && !m_restyling) {
    m_restyling = true;
    updateStatusColors();
    m_restyling = false;
  }
}

void AlarmSingleWidget::setActiveAndPosition(bool active, bool position) {
  off->setChecked(!active);
  onLock->setChecked(position && active);
  onUnlock->setChecked(!position && active);
}

void AlarmSingleWidget::updateSelection() {
  bool const active{utils::getBitOfFloat(m_enableValue->getValue(), m_index)};
  bool const position{utils::getBitOfFloat(m_conditionValue->getValue(), m_index)};
  setActiveAndPosition(active, position);
  updateStatusColors();
}

void AlarmSingleWidget::initGui() {
  double const ssf{utils::ssf()};
  setObjectName("outputRow");
  auto *mainLay = new QHBoxLayout(this);
  mainLay->setContentsMargins(12 * ssf, 6 * ssf, 10 * ssf, 6 * ssf);
  mainLay->setSpacing(10 * ssf);

  auto *description = new QLabel(m_label, this);
  description->setObjectName("rowTitle");
  mainLay->addWidget(description, 1);

  curState = new QLabel(this);
  curState->setAlignment(Qt::AlignCenter);
  curState->setFixedSize(70 * ssf, 26 * ssf);
  curState->setToolTip(tr("State of the alarm input"));
  mainLay->addWidget(curState);
  mainLay->addSpacing(10 * ssf);

  // trigger condition: one segmented control
  auto *group    = new QButtonGroup(this);
  auto *segments = new QHBoxLayout();
  segments->setSpacing(0);
  QList<QPushButton **> const buttons{&onLock, &onUnlock, &off};
  QStringList const           texts{tr("Closed"), tr("Open"), tr("Off")};
  for (int i{0}; i < buttons.size(); ++i) {
    auto *button = new QPushButton(texts[i], this);
    button->setCheckable(true);
    button->setProperty("segment", true);
    button->setProperty("segmentFirst", i == 0);
    button->setProperty("segmentLast", i == buttons.size() - 1);
    button->setFixedSize(80 * ssf, 28 * ssf);
    group->addButton(button, i);
    segments->addWidget(button);
    *buttons[i] = button;
  }
  group->setExclusive(true);
  onLock->setToolTip(tr("The alarm triggers when the contact is closed"));
  onUnlock->setToolTip(tr("The alarm triggers when the contact is open"));
  off->setToolTip(tr("This alarm input is not processed"));
  mainLay->addLayout(segments);

  updateSelection();
}

void AlarmSingleWidget::updateConfigValue() {
  qDebug() << m_index << onLock->isChecked() << onUnlock->isChecked() << off->isChecked();
  m_enableValue->setBitOfFloat(m_index, getActive(), true);
  m_conditionValue->setBitOfFloat(m_index, getPosition(), true);

  m_enableValue->notify();
  m_conditionValue->notify();
}

} // namespace deepvaccontrol
