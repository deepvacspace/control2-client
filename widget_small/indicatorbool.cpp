#include "indicatorbool.h"

#include <QHBoxLayout>

#include "utils.h"

namespace control2 {
IndicatorBool::IndicatorBool(SystemValue *sysVal, quint8 bitIndex, QWidget *parent, QString textOn, QString textOff)
    : QFrame{parent}
    , m_sysVal{sysVal}
    , m_bitIndex{bitIndex}
    , m_textOn{textOn}
    , m_textOff{textOff}
    , m_label{new QLabel(textOff, this)} {
  setFrameStyle(QFrame::Panel | QFrame::Plain);

  if (m_textOn.isEmpty() and m_textOff.isEmpty()) {
    setFixedSize(15, 15);
  } else {
    setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
  }

  auto *mainLay = new QHBoxLayout(this);
  mainLay->addStretch();
  mainLay->addWidget(m_label);
  mainLay->addStretch();
  mainLay->setContentsMargins(2 * utils::ssf(), 2 * utils::ssf(), 2 * utils::ssf(), 2 * utils::ssf());

  updateGui();
  connect(m_sysVal, &SystemValue::valueChanged, this, &IndicatorBool::updateGui);
}

void IndicatorBool::updateGui() {
  if (!m_sysVal) {
    return;
  }

  bool const boolState{m_sysVal->getBitOfFloat(m_bitIndex)};
  setStyleSheet(boolState ? "background-color: #ffde21;" : "background-color: #cecece;");
  m_label->setText(boolState ? m_textOn : m_textOff);
}
} // namespace control2
