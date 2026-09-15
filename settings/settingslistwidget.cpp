#include "settingslistwidget.h"

#include <QGridLayout>
#include <QLabel>

#include "lineeditnum.h"
#include "lineedittime.h"

namespace deepvaccontrol {
SettingsListWidget::SettingsListWidget(ResHolder *resHolder, QWidget *parent)
    : ScrollWidget{true, parent}, m_resHolder{resHolder} {
  auto *mainLay = new QGridLayout();
  mainLay->setContentsMargins(3, 3, 3, 3);
  this->setLayout2(mainLay);

  SystemValueList &settingsValueList{resHolder->settings->plcSettingsSystemValueList()};
  QStringList      settingsOrder{settingsValueList.getNames()};

  QStringList ignoreList{"alarm_cfg_1", "alarm_cfg_2"};
  // in same order as in settings.json
  int         i{0U};
  for (; i < settingsValueList.size(); ++i) {
    auto const settingName{settingsOrder[i]};
    auto       settingSysValue = settingsValueList.getSysValue(settingName);

    if (ignoreList.contains(settingName)) {
      continue;
    }

    mainLay->addWidget(new QLabel(settingSysValue->getLabel() + "\n(" + settingName + ")"), i, 0);

    // Different delegates
    if (settingName.startsWith("t_")) {
      auto *editField = new LineEditTime(settingSysValue, true, this);
      mainLay->addWidget(editField, i, 1);
    } else {
      auto *editField = new LineEditNum(settingSysValue, this);
      mainLay->addWidget(editField, i, 1);
    }
  }
  mainLay->setRowStretch(i, 1);
}
} // namespace deepvaccontrol
