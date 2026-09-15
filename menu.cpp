#include "menu.h"

#include "utils.h"

namespace deepvaccontrol {
Menu::Menu(int buttonHeight, QWidget *parent)
    : QWidget{parent}
    , m_listLay{new QVBoxLayout()}
    , m_currentSelected{0}
    , m_prevSelected{0}
    , m_buttonHeight{buttonHeight} {
  auto *mainlay = new QVBoxLayout(this);
  m_listLay->setSpacing(2 * utils::ssf());

  mainlay->addLayout(m_listLay);
  mainlay->addStretch(0);
  mainlay->setContentsMargins(0, 0, 0, 0);
  mainlay->setSpacing(0);
}

void Menu::addItem(QString const &name) {
  auto *itemBtn = new QPushButton(name, this);

  {
    auto font = itemBtn->font();
    font.setPointSize(font.pointSize() * 1.5);
    font.setBold(true);
    itemBtn->setFont(font);
  }

  itemBtn->setFixedSize(150 * utils::ssf(), m_buttonHeight * utils::ssf());
  itemBtn->installEventFilter(this);
  itemBtn->setFocusPolicy(Qt::NoFocus);
  connect(itemBtn, &QPushButton::clicked, this, &Menu::processMenuClicked);

  m_buttons.push_back(itemBtn);
  m_listLay->addWidget(itemBtn);
}

void Menu::processMenuClicked() {
  auto *pressedButton = qobject_cast<QPushButton *>(sender());
  if (pressedButton) {
    int index = m_buttons.indexOf(pressedButton);
    selectItem(index);
  } else {
    qWarning() << "[Menu::processMenuClicked] no button in layout";
  }
}

void Menu::selectItem(int const index) {
  if (index >= m_buttons.size()) {
    qWarning() << "[Menu::selectItem] no button with index" << index;
    return;
  }

  m_buttons[index]->setDown(true);

  if (index == m_currentSelected) {
    return;
  }

  // save previously pressed
  m_prevSelected    = m_currentSelected;
  // update
  m_currentSelected = index;
  // release previously pressed
  m_buttons[m_prevSelected]->setDown(false);

  emit itemClicked(m_currentSelected, m_prevSelected);
}

void Menu::setElementsEnable(QVector<bool> enableList) {
  if (m_buttons.size() > enableList.size()) {
    qWarning() << "[Menu::setElementsEnable] Menu has more elements than enable vector";
    return;
  }

  for (int i{0}; i < m_buttons.size(); ++i) {
    m_buttons[i]->setVisible(enableList[i]);
  }
}

void Menu::setElementsEnableAll() {
  for (auto &item : m_buttons) {
    item->setVisible(true);
  }
}

void Menu::addWidget(QWidget *widget) {
  m_listLay->addWidget(widget);
}

} // namespace deepvaccontrol
