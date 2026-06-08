#include "syslistwidget.h"

#include <QGridLayout>

#include "indicatorbool.h"
#include "utils.h"

namespace control2 {
SysListWidget::SysListWidget(ResHolder *resHolder, QWidget *parent)
    : QWidget{parent}, m_resHolder{resHolder}, m_states{resHolder->states} {
  // states list / indicators
  auto *mainLay = new QHBoxLayout(this);
  mainLay->setContentsMargins(0, 0, 0, 0);
  mainLay->setSpacing(0);

  auto *statesListLay = new QGridLayout();
  statesListLay->setContentsMargins(0, 0, 0, 0);
  statesListLay->setSpacing(0);
  mainLay->addLayout(statesListLay);

  QStringList const stateNames{m_states->getNames()};
  for (int i{0U}; i < m_states->size(); ++i) {
    QString const stateName{stateNames[i]};
    auto         *nameLabel  = new QLabel(m_states->getSysValue(stateName)->getLabel());
    auto         *valueLabel = new QLabel("", this);
    nameLabel->setContentsMargins(5, 0, 5, 0);
    valueLabel->setContentsMargins(5, 0, 0, 0);
    m_values.push_back(valueLabel);

    statesListLay->addWidget(nameLabel, i, 0);
    statesListLay->addWidget(valueLabel, i, 1);

    QColor const backgroudColor{i % 2 ? QColor(220, 220, 220) : QColor(240, 240, 240)};
    nameLabel->setStyleSheet("QWidget{background-color:" + backgroudColor.name() + "}");
    valueLabel->setStyleSheet("QWidget{background-color:" + backgroudColor.name() + "}");
  }

  statesListLay->setRowStretch(m_states->size(), 10);
  statesListLay->setColumnStretch(1, 10);

  auto *indicatorsLay = new QGridLayout();
  indicatorsLay->setContentsMargins(3 * utils::ssf(), 3 * utils::ssf(), 3 * utils::ssf(), 3 * utils::ssf());
  indicatorsLay->setSpacing(3 * utils::ssf());
  mainLay->addLayout(indicatorsLay);

  // fill indicatorsLay with indicators
  {
    QVariantList const displayList = m_resHolder->settings->settingsMap()["monitor"].toList();
    int                rowCount{static_cast<int>((displayList.size() + 1) / 2)};
    for (int i{0U}; i < displayList.size(); ++i) {
      QVariantMap const displayVarMap{displayList[i].toMap()};
      quint32 const     bitIndex{displayVarMap["bit"].toUInt()};
      QString const     displayText{displayVarMap["label"].toString()};
      QString           sysValName{displayVarMap["name"].toString()};
      if (sysValName.isEmpty()) {
        sysValName = "outputs";
      }
      SystemValue *const sysValForIndicator{m_resHolder->states->getSysValue(sysValName)};

      auto *indicator = new IndicatorBool(sysValForIndicator, bitIndex, this, displayText, displayText);
      indicatorsLay->addWidget(indicator, i % rowCount, i / rowCount);
    }
    indicatorsLay->setRowStretch(rowCount, 10);
  }

  connect(m_states, &SystemValueList::ValuesUpdated, this, &SysListWidget::updateList);
  updateList();
}

void SysListWidget::updateList() {
  QStringList const stateNames{m_states->getNames()};
  for (int i{0U}; i < m_states->size(); ++i) {
    QString const stateName{stateNames[i]};
    auto         *state = m_states->getSysValue(stateName);
    float const   floatValue{state->getValue()};

    auto *valueLabel = m_values[i];
    if (stateName.startsWith("t_")) {
      valueLabel->setText(utils::timeFromSec(static_cast<quint32>(floatValue), true));
    } else {
      if (std::isnan(floatValue)) {
        valueLabel->setText("--");
      } else {
        valueLabel->setText(QString::number(floatValue, 'f', state->getPrec()));
      }
    }
  }
}
} // namespace control2
