#include "lightbutton.h"

#include "constants.h"
#include "utils.h"

namespace control2 {
LightButton::LightButton(GateTcp *gate, SystemValue *sysVal, QWidget *parent)
    : QPushButton{parent}, m_gate{gate}, m_sysVal{sysVal} {
  setText(tr("Свет"));
  setFocusPolicy(Qt::NoFocus);
  setFixedHeight(control2::UI_MENU_BTN_HEIGHT * utils::ssf());
  setIconSize(QSize(30 * utils::ssf(), 30 * utils::ssf()));
  setIcon(QIcon(":/imgs/light_off.png"));

  {
    auto fn = font();
    fn.setPointSize(fn.pointSize() * 1.5);
    fn.setBold(true);
    setFont(fn);
  }

  connect(this, &QPushButton::clicked, this, [this] {
    m_gate->sendPair("light", static_cast<float>(!m_enabled));
  });

  connect(m_sysVal, &SystemValue::valueChanged, this, [this](float value) {
    bool const newValue{utils::getBitOfFloat(value, 6)}; // "outputs"
    if (newValue == m_enabled) {
      return;
    }

    m_enabled = newValue;
    setIcon(QIcon(m_enabled ? ":/imgs/light_on.png" : ":/imgs/light_off.png"));
  });
}
} // namespace control2
