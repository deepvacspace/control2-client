#include "indicatorbool.h"

#include "theme.h"

#include <QHBoxLayout>

#include "utils.h"

namespace deepvaccontrol {
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
  mainLay->setContentsMargins(theme::dp(4), theme::dp(4), theme::dp(4), theme::dp(4));

  updateGui();
  connect(m_sysVal, &SystemValue::valueChanged, this, &IndicatorBool::updateGui);
}

void IndicatorBool::updateGui() {
  if (!m_sysVal) {
    return;
  }

  bool const boolState{m_sysVal->getBitOfFloat(m_bitIndex)};
  theme::Colors const &c = theme::colors();
  setStyleSheet(boolState ? QStringLiteral("background-color: %1; color: %2;").arg(c.active.name(), c.text.name())
                          : QStringLiteral("background-color: %1; color: %2;").arg(c.panelAlt.name(), c.muted.name()));
  m_label->setText(boolState ? m_textOn : m_textOff);
}
}
