#include "actionwidget.h"

#include <QPushButton>
#include <QVBoxLayout>

#include "constants.h"
#include "utils.h"

namespace deepvaccontrol {
ActionWidget::ActionWidget(ActionManager *actionManager, QWidget *parent)
    : QWidget{parent}, m_actionManager{actionManager} {
  auto *mainLay = new QVBoxLayout(this);
  mainLay->setContentsMargins(3 * utils::ssf(), 3 * utils::ssf(), 3 * utils::ssf(), 3 * utils::ssf());

  m_listWidget = new QListWidget(this);
  m_listWidget->setStyleSheet(utils::readStyleFromFile(":/styles/verticalScroll.qss"));
  mainLay->addWidget(m_listWidget);

  auto *btnLay = new QHBoxLayout();
  mainLay->addLayout(btnLay);

  auto *moreBtn = new QPushButton(tr("Показать больше"), this);
  moreBtn->setFixedSize(120 * utils::ssf(), deepvaccontrol::UI_BTN_HEIGHT * utils::ssf());
  btnLay->addStretch();
  btnLay->addWidget(moreBtn);

  connect(moreBtn, &QPushButton::clicked, this, [this]() {
    addElements(m_actionManager->getNext(), true);
  });

  connect(actionManager, &ActionManager::added, this, &ActionWidget::addElements);

  connect(actionManager, &ActionManager::allDisplayed, this, [moreBtn]() {
    moreBtn->setEnabled(false);
  });

  addElements(m_actionManager->getNext(), true);
}

void ActionWidget::addElements(QVariantList const &newElements, bool addToBottom) {
  for (int i = 0; i < newElements.size(); ++i) {
    QVariantMap const actionMap = newElements[i].toMap();

    quint8 const  type{static_cast<quint8>(actionMap["type"].toUInt())};
    QString const msg{actionMap["msg"].toString()};
    quint64 const date{actionMap["date"].toUInt()};

    QString const time{QDateTime::fromSecsSinceEpoch(date).toString("yyyy-MM-dd   HH:mm:ss")};
    QString const itemString{time + " | " + msg};

    auto *listElement = new QListWidgetItem(itemString);

    switch (static_cast<ActionManager::ActionType>(type)) {
      case ActionManager::ActionType::PLC:
        listElement->setBackground(QBrush(QColor(0, 200, 0, 50)));
        break;
      case ActionManager::ActionType::ERR:
        listElement->setBackground(QBrush(QColor(200, 0, 0, 50)));
        break;
      case ActionManager::ActionType::APP:
        listElement->setBackground(QBrush(QColor(0, 0, 200, 30)));
        break;
      default:
        listElement->setBackground(QBrush(QColor(200, 200, 0, 50)));
        break;
    }

    if (addToBottom) {
      m_listWidget->addItem(listElement);
    } else {
      m_listWidget->insertItem(0, listElement);
    }
  }
}
} // namespace deepvaccontrol
