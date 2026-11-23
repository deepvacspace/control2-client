#include "forceoutsinglewidget.h"

#include <QDataStream>
#include <QEvent>
#include <QHBoxLayout>

#include "theme.h"
#include "utils.h"

namespace deepvaccontrol {
ForceOutSingleWidget::ForceOutSingleWidget(
  int index, QString const &stateLabel, SystemValue *sysValue, ForceType type, QWidget *parent)
    : QFrame{parent}, m_index{index}, m_sysValue{sysValue} {
  setObjectName("outputRow");
  auto *mainLay = new QHBoxLayout(this);
  mainLay->setContentsMargins(theme::dp(24), theme::dp(12), theme::dp(20), theme::dp(12));
  mainLay->setSpacing(theme::dp(20));

  // name and current state
  auto *label = new QLabel(stateLabel, this);
  label->setObjectName("rowTitle");
  label->setMinimumWidth(theme::dp(375));
  mainLay->addWidget(label, 1);
  m_stateBadge = new QLabel(this);
  m_stateBadge->setAlignment(Qt::AlignCenter);
  m_stateBadge->setFixedWidth(theme::dp(82));
  m_stateBadge->setToolTip(tr("Current state of the output"));
  mainLay->addWidget(m_stateBadge);
  mainLay->addSpacing(theme::dp(20));

  // how it is forced: one segmented control
  m_modeGroup = new QButtonGroup(this);
  m_modeGroup->setExclusive(true);
  QStringList modes{tr("Auto"), tr("Off"), tr("On")};
  if (type == ForceType::DUTY) {
    modes << tr("Set");
  }
  auto *segments = new QHBoxLayout();
  segments->setSpacing(0);
  for (int mode{0}; mode < modes.size(); ++mode) {
    auto *button = new QPushButton(modes[mode], this);
    button->setCheckable(true);
    button->setProperty("segment", true);
    button->setProperty("segmentFirst", mode == 0);
    button->setProperty("segmentLast", mode == modes.size() - 1);
    button->setFixedSize(theme::dp(120), theme::dp(52));
    m_modeGroup->addButton(button, mode);
    segments->addWidget(button);
  }
  m_modeGroup->button(0)->setToolTip(tr("Released: the PLC controls the output"));
  m_modeGroup->button(0)->setChecked(true);
  if (type != ForceType::DUTY) {
    segments->addSpacing(theme::dp(120)); // rows line up with the rows that have a duty cycle
  }
  mainLay->addLayout(segments);

  // duty cycle: share and period
  m_dutyBox    = new QWidget(this);
  auto *dutyLay = new QHBoxLayout(m_dutyBox);
  dutyLay->setContentsMargins(0, 0, 0, 0);
  dutyLay->setSpacing(theme::dp(12));
  if (type == ForceType::DUTY) {
    duty   = new LineEditNum(0, 1, 3, true, m_dutyBox);
    period = new LineEditNum(0, 600, 1, true, m_dutyBox);
    duty->setText("0.500");
    period->setText("4.0");
    QFont fieldFont = font();
    for (LineEditNum *edit : {duty, period}) {
      edit->setFont(fieldFont);
      edit->setFixedSize(theme::dp(131), theme::dp(52));
    }
    auto muted = [this](QString const &text) {
      auto *l = new QLabel(text, m_dutyBox);
      l->setProperty("muted", true);
      return l;
    };
    dutyLay->addWidget(muted(tr("Set")));
    dutyLay->addWidget(duty);
    dutyLay->addWidget(muted(tr("Period, s")));
    dutyLay->addWidget(period);
    connect(duty, &LineEditNum::userEditingFinished, this, [this] { m_setBtn->setVisible(true); });
    connect(period, &LineEditNum::userEditingFinished, this, [this] { m_setBtn->setVisible(true); });
  }
  m_dutyBox->setFixedWidth(theme::dp(468)); // same width in every row, shown only for a duty cycle
  m_dutyBox->setVisible(false);
  QSizePolicy keep = m_dutyBox->sizePolicy();
  keep.setRetainSizeWhenHidden(true);
  m_dutyBox->setSizePolicy(keep);
  mainLay->addWidget(m_dutyBox);

  // send the choice
  m_setBtn = new QPushButton(tr("Set"), this);
  m_setBtn->setObjectName("primaryButton");
  m_setBtn->setFixedSize(theme::dp(131), theme::dp(52));
  QSizePolicy keepSet = m_setBtn->sizePolicy();
  keepSet.setRetainSizeWhenHidden(true);
  m_setBtn->setSizePolicy(keepSet);
  resetSetBtn();
  mainLay->addWidget(m_setBtn);

  connect(m_modeGroup, &QButtonGroup::idClicked, this, &ForceOutSingleWidget::selectMode);
  connect(m_setBtn, &QPushButton::clicked, this, [this] {
    m_setBtn->setVisible(false);
    emit generateRaw(1, getRawData());
  });
  connect(sysValue, &SystemValue::valueChanged, this, &ForceOutSingleWidget::updateStateBadge);
  updateStateBadge();
}

void ForceOutSingleWidget::selectMode(int mode) {
  plcType = static_cast<uint>(mode);
  m_dutyBox->setVisible(mode == 3);
  m_setBtn->setVisible(true);
}

void ForceOutSingleWidget::resetSelection() {
  plcType = 0;
  m_modeGroup->button(0)->setChecked(true);
  m_dutyBox->setVisible(false);
  resetSetBtn();
}

void ForceOutSingleWidget::resetSetBtn() {
  m_setBtn->setVisible(false);
}

void ForceOutSingleWidget::changeEvent(QEvent *event) {
  QFrame::changeEvent(event);
  if ((event->type() == QEvent::PaletteChange) && !m_restyling) {
    m_restyling = true;
    updateStateBadge();
    m_restyling = false;
  }
}

void ForceOutSingleWidget::updateStateBadge() {
  bool const           on{m_sysValue && utils::getBitOfFloat(m_sysValue->getValue(), m_index)};
  theme::Colors const &c     = theme::colors();
  QColor const         color = on ? c.active : c.off;
  m_stateBadge->setText(on ? tr("ON") : tr("OFF"));
  m_stateBadge->setStyleSheet(QStringLiteral("color: %1; border: %2px solid %1; border-radius: %3px; background: %4; font-weight: bold;")
                                .arg(color.name())
                                .arg(qMax(1, static_cast<int>(1 * utils::ssf())))
                                .arg(static_cast<int>(3 * utils::ssf()))
                                .arg(QColor(color.red(), color.green(), color.blue(), on ? 40 : 0).name(QColor::HexArgb)));
}

QByteArray ForceOutSingleWidget::getRawData() const {
  QByteArray  result;
  QDataStream out(&result, QIODevice::WriteOnly);
  out.setByteOrder(QDataStream::LittleEndian);
  out.setFloatingPointPrecision(QDataStream::SinglePrecision);

  out << static_cast<uint8_t>(m_index);
  out << static_cast<uint8_t>(plcType);

  if (plcType == 3) {
    out << static_cast<float>(duty->text().toDouble());
    out << static_cast<float>(period->text().toDouble());
  }
  return result;
}
}
