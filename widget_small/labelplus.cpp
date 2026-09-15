#include "labelplus.h"

#include <bitset>
#include "utils.h"

namespace deepvaccontrol {
QString defaultFormat(float value, int prec) {
  if ((std::isnan(value)) || (value < -10e9) || (value > 10e9)) {
    return QString("--");
  } else {
    return QString::number(value, 'f', prec);
  }
}

LabelPlus::LabelPlus(SystemValue *sysVal, QString (*df)(float, int), QWidget *parent)
    : QLabel(parent), m_sysValue{nullptr}, m_prec{0}, m_dispFnc(df) {
  if(sysVal->isSciNotation()) {
    m_dispFnc = &utils::formatScientific;
  }
  setSysValue(sysVal);
}

LabelPlus::LabelPlus(SystemValue *sysVal, QWidget *parent) : LabelPlus(sysVal, &defaultFormat, parent) {
}

LabelPlus::LabelPlus(QWidget *parent) : LabelPlus(nullptr, &defaultFormat, parent) {
}

LabelPlus::~LabelPlus() {
}

void LabelPlus::setSysValue(SystemValue *sysVal) {
  if (!sysVal) {
    return;
  }
  if (m_sysValue) {
    qWarning() << "LabelPlus::setSysValue: label alredy connected to SysValue" << m_sysValue->getName();
  }

  m_sysValue = sysVal;
  setPrec(m_sysValue->getPrec());
  setToolTip(m_sysValue->getLabel());
  connect(m_sysValue, &SystemValue::valueChanged, this, &LabelPlus::updateValue);
  updateValue(); // NOTE check;
}

void LabelPlus::updateValue() {
  if (!m_sysValue) {
    return;
  }

  float const floatValue{m_sysValue->getValue()};

  if (m_sysValue->getName().right(2) == "_b") {
    union {
      quint32 int32_val;
      char    chars_val[4];
    };

    static_cast<void>(std::memcpy(chars_val, &floatValue, sizeof(floatValue)));
    QByteArray ba(chars_val);
    setText(ba.toHex());

    std::bitset<32> bs(int32_val);
    QString         tmp;
    uint            i = 0;
    for (auto const &item : m_sysValue->getLabel().split(';')) {
      tmp += (item + ": " + (bs[i++] ? "●" : "○") + "\n");
    }
    setToolTip(tmp);
  } else {
    setText(m_dispFnc(floatValue, m_prec));
  }
}
} // namespace deepvaccontrol
