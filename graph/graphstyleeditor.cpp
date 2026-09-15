#include "graphstyleeditor.h"

#include <QLayoutItem>
#include <QVBoxLayout>

#include "constants.h"
#include "scrollwidget.h"
#include "utils.h"

namespace deepvaccontrol {
GraphStyleEditor::GraphStyleEditor(ResHolder *resHolder, QWidget *parent) : QDialog{parent}, m_resHolder{resHolder} {
  auto *mainLay = new QVBoxLayout(this);
  mainLay->setContentsMargins(5 * utils::ssf(), 5 * utils::ssf(), 5 * utils::ssf(), 5 * utils::ssf());
  mainLay->setSpacing(3 * utils::ssf());

  auto *elementLay = new QVBoxLayout();

  QVariantList const statesList = resHolder->settings->settingsMap()["states"].toList();

  for (int i{0U}; i < statesList.size(); ++i) {
    QVariantMap const stateMap{statesList[i].toMap()};
    if (stateMap.contains("visible")) {
      auto *stateStyleWidget = new GraphStyleSingleWidget(i, stateMap, this);
      connect(stateStyleWidget, &GraphStyleSingleWidget::styleUpdated, this, [this, i, stateStyleWidget]() {
        emit styleChanged(i, stateStyleWidget->getStateMapVariant());
      });
      elementLay->addWidget(stateStyleWidget);
      widgetsMap[i] = stateStyleWidget;
    }
  }

  elementLay->addStretch();

  auto *elementScrollWidget = new ScrollWidget(true, this);
  elementScrollWidget->setLayout2(elementLay);
  elementScrollWidget->setContentsMargins(0, 0, 0, 0);

  mainLay->addWidget(elementScrollWidget);

  auto *closeBtn      = new QPushButton(tr("Закрыть"), this);
  auto *selectAllBtn  = new QPushButton(tr("Все"), this);
  auto *selectNoneBtn = new QPushButton(tr("Ни одного"), this);
  for (QPushButton *item : {closeBtn, selectAllBtn, selectNoneBtn}) {
    item->setFixedSize(90 * utils::ssf(), deepvaccontrol::UI_BTN_HEIGHT * utils::ssf());
  }

  auto *btnLay = new QHBoxLayout();
  btnLay->setContentsMargins(3 * utils::ssf(), 3 * utils::ssf(), 3 * utils::ssf(), 3 * utils::ssf());
  btnLay->setSpacing(6 * utils::ssf());
  btnLay->addWidget(selectAllBtn);
  btnLay->addWidget(selectNoneBtn);
  btnLay->addStretch();
  btnLay->addWidget(closeBtn);

  mainLay->addLayout(btnLay);
  connect(closeBtn, &QPushButton::clicked, this, &GraphStyleEditor::hide);

  connect(selectAllBtn, &QPushButton::clicked, this, [this] {
    for (auto &item : widgetsMap) {
      item->setEnabled(true);
    }
    emit styleAllChanged(getStateStylesList());
  });

  connect(selectNoneBtn, &QPushButton::clicked, this, [this] {
    for (auto &item : widgetsMap) {
      item->setEnabled(false);
    }
    emit styleAllChanged(getStateStylesList());
  });

  setMinimumWidth(600 * utils::ssf());
  setModal(true);
  setWindowTitle(tr("Выбор отображения переменных"));
}

QVariantList GraphStyleEditor::getStateStylesList() {
  QVariantList statesListVariantNew;
  QVariantList statesListVariantOld = m_resHolder->settings->settingsMap()["states"].toList();

  for (int i{0U}; i < statesListVariantOld.size(); ++i) {
    QVariantMap stateMapResult = statesListVariantOld[i].toMap();

    // update state map from widget if state has fields to display on graph
    if (widgetsMap.contains(i)) {
      stateMapResult = widgetsMap[i]->getStateMapVariant();
    }
    statesListVariantNew.push_back(stateMapResult);
  }

  return statesListVariantNew;
}

void GraphStyleEditor::hideEvent(QHideEvent *event) {
  m_resHolder->settings->setStatesSettings(getStateStylesList());

  // Call the base class implementation
  QDialog::hideEvent(event);
}
} // namespace deepvaccontrol
