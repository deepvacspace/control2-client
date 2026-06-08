#include "statuslabel.h"

#include "constants.h"
#include "hardwarestatus.h"
#include "utils.h"

namespace control2 {
StatusLabel::StatusLabel(ResHolder *resHolder, QWidget *parent) : QLabel{parent} {
  setFixedWidth(200 * utils::ssf());

  {
    auto fnt = font();
    fnt.setBold(true);
    fnt.setPointSize(fnt.pointSize() * 1.5);
    setFont(fnt);
  }

  connect(resHolder->hard_status,
          &HardwareStatus::statusChanged,
          this,
          [this](HardwareStatus::HardwareStatusEnum const &newStatus, HardwareStatus::HardwareStatusEnum const &) {
            m_statusStr = control2::hardwareStatusList()[static_cast<quint32>(newStatus)];
            updateString();
          });

  if (resHolder->states->contains("work_type")) {
    connect(resHolder->states->getSysValue("work_type"), &SystemValue::valueChanged, this, [this](float val) {
      qint32 jobTypeIndex{static_cast<qint32>(val)};
      if ((jobTypeIndex < 0) || (2 < jobTypeIndex)) {
        jobTypeIndex = 0;
      }
      m_typeStr = QStringList({"", "tr(по уставке)", "tr(по программе)"}).at(jobTypeIndex);
      updateString();
    });
  }
}

void StatusLabel::updateString() {
  setText(m_statusStr + " " + m_typeStr);
}
} // namespace control2
