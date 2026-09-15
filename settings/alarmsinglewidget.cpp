#include "alarmsinglewidget.h"

#include <QHBoxLayout>

#include "utils.h"

namespace deepvaccontrol {
AlarmSingleWidget::AlarmSingleWidget(QString const &label,
                                     SystemValue   *statusValue,
                                     SystemValue   *enableValue,
                                     SystemValue   *conditionValue,
                                     quint32        index,
                                     QWidget       *parent)
    : QWidget{parent}
    , m_label{label}
    , m_statusValue{statusValue}
    , m_enableValue{enableValue}
    , m_conditionValue{conditionValue}
    , m_index{index} {
  initGui();

  connect(statusValue, &SystemValue::valueChanged, this, &AlarmSingleWidget::updateStatusColors);
  connect(enableValue, &SystemValue::valueChanged, this, &AlarmSingleWidget::updateSelection);
  connect(conditionValue, &SystemValue::valueChanged, this, &AlarmSingleWidget::updateSelection);

  connect(onLock, &QRadioButton::toggled, this, &AlarmSingleWidget::updateConfigValue);
  connect(onUnlock, &QRadioButton::toggled, this, &AlarmSingleWidget::updateConfigValue);
  connect(off, &QRadioButton::toggled, this, &AlarmSingleWidget::updateConfigValue);
}

void AlarmSingleWidget::updateStatusColors() {
  bool const conditionStatus{utils::getBitOfFloat(m_statusValue->getValue(), m_index)};

  QString const stateColor{conditionStatus ? QColor(230, 230, 0).name() : QColor(240, 240, 240).name()};
  curState->setStyleSheet("background-color:" + stateColor + ";");

  QString boxColor{conditionStatus == getPosition() ? QColor(255, 221, 221).name() : QColor(221, 255, 221).name()};
  if (!getActive()) {
    boxColor = QColor::fromRgb(230, 230, 230).name();
  }
  box->setStyleSheet("background-color:" + boxColor + ";");
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
  auto *mainLay     = new QHBoxLayout(this);
  auto *description = new QLabel(m_label, this);
  curState          = new QFrame(this);
  box               = new QFrame(this);

  auto *btnsLay = new QHBoxLayout(box);
  onLock        = new QRadioButton(this);
  onUnlock      = new QRadioButton(this);
  off           = new QRadioButton(this);
  btnsLay->addWidget(onLock);
  btnsLay->addWidget(onUnlock);
  btnsLay->addStretch();
  btnsLay->addWidget(off);
  btnsLay->setStretch(0, 1);
  btnsLay->setStretch(1, 1);
  btnsLay->setStretch(2, 1);
  btnsLay->setStretch(3, 1);
  btnsLay->setContentsMargins(0, 0, 0, 0);

  mainLay->addWidget(description);
  mainLay->addWidget(curState);
  mainLay->addWidget(box);
  mainLay->setContentsMargins(0, 0, 0, 0);

  box->setContentsMargins(0, 0, 0, 0);
  box->setFrameStyle(QFrame::StyledPanel | QFrame::Plain);
  setStyleSheet(QString("QRadioButton::indicator{subcontrol-position: center center; width: %1px; height: %1px;}")
                  .arg(20 * utils::ssf()));

  box->setMinimumSize(200 * utils::ssf(), 30 * utils::ssf());

  curState->setFrameStyle(QFrame::StyledPanel | QFrame::Plain);
  curState->setFixedSize(10 * utils::ssf(), 30 * utils::ssf());

  mainLay->setStretch(0, 1);

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
