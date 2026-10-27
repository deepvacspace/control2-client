#include "menu.h"

#include <QEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QVBoxLayout>

#include "theme.h"
#include "utils.h"

namespace deepvaccontrol {
namespace {
int px(double value) {
  return qMax(1, static_cast<int>(value * utils::ssf()));
}
}

Menu::Menu(int buttonHeight, QWidget *parent)
    : QWidget{parent}
    , m_listLay{new QHBoxLayout()}
    , m_extraLay{new QHBoxLayout()}
    , m_currentSelected{0}
    , m_prevSelected{0}
    , m_buttonHeight{buttonHeight} {
  auto *outer = new QVBoxLayout(this);
  outer->setContentsMargins(0, 0, 0, 0);
  m_content = new QWidget(this);
  outer->addWidget(m_content);
  auto *mainlay = new QHBoxLayout(m_content);
  mainlay->setContentsMargins(px(8), px(4), px(8), px(4));
  mainlay->setSpacing(px(10));
  m_listLay->setSpacing(px(4));
  m_extraLay->setSpacing(px(10));

  auto *separator = new QWidget(m_content);
  separator->setObjectName("menuSeparator");
  separator->setFixedWidth(px(1));

  mainlay->addLayout(m_listLay, 1);
  mainlay->addWidget(separator);
  mainlay->addLayout(m_extraLay);
  setFixedHeight(px(46));
  restyle();
}

void Menu::addItem(QString const &name, QString const &iconKind) {
  auto *itemBtn = new QToolButton(m_content);
  itemBtn->setObjectName("menuButton");
  itemBtn->setText(QString(name).replace('\n', ' '));
  itemBtn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
  itemBtn->setCheckable(true);
  itemBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  itemBtn->setIconSize(QSize(px(14), px(14)));
  itemBtn->setFocusPolicy(Qt::NoFocus);
  connect(itemBtn, &QToolButton::clicked, this, &Menu::processMenuClicked);

  m_buttons.push_back(itemBtn);
  m_iconKinds.push_back(iconKind);
  m_listLay->addWidget(itemBtn);
  restyle();
}

void Menu::restyle() {
  static bool busy{false}; // setStyleSheet may raise a palette change again
  if (busy) {
    return;
  }
  busy                   = true;
  theme::Colors const &c = theme::colors();
  setStyleSheet(QStringLiteral(R"(
QToolButton#menuButton { background: transparent; color: %1; border: %4px solid transparent; border-radius: %5px;
  padding-top: %6px; }
QToolButton#menuButton:hover { color: %2; }
QToolButton#menuButton:checked { color: %2; background: %3; border-color: %7; }
QWidget#menuSeparator { background: %8; }
QToolButton#barToggle { color: %1; background: %9; border: %4px solid %8; border-radius: %10px; font-weight: bold; padding: 0; }
QToolButton#barToggle:hover { color: %2; border-color: %7; }
)")
                  .arg(c.muted.name(), c.text.name(), QColor(c.accent.red(), c.accent.green(), c.accent.blue(), 60).name(QColor::HexArgb))
                  .arg(px(1))
                  .arg(px(5))
                  .arg(px(3))
                  .arg(c.accent.name(), c.border.name(), c.panel.name())
                  .arg(px(4)));
  QFont small = font();
  small.setPointSizeF(QWidget::font().pointSizeF() * 0.9);
  for (int i{0}; i < m_buttons.size(); ++i) {
    m_buttons[i]->setFont(small);
    if (!m_iconKinds[i].isEmpty()) {
      m_buttons[i]->setIcon(theme::menuIcon(m_iconKinds[i], c.muted, c.text, px(14)));
    }
  }
  update();
  busy = false;
}

void Menu::processMenuClicked() {
  auto *pressedButton = qobject_cast<QToolButton *>(sender());
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

  for (int i{0}; i < m_buttons.size(); ++i) {
    m_buttons[i]->setChecked(i == index);
  }

  if (index == m_currentSelected) {
    return;
  }

  m_prevSelected    = m_currentSelected;
  m_currentSelected = index;

  emit itemClicked(m_currentSelected, m_prevSelected);
}

void Menu::setElementsEnable(QVector<bool> enableList) {
  if (m_buttons.size() > enableList.size()) {
    qWarning() << "[Menu::setElementsEnable] Menu has more elements than enable vector";
    return;
  }

  for (int i{0}; i < m_buttons.size(); ++i) {
    m_buttons[i]->setVisible(enableList[i] && !m_hidden.contains(i));
  }
}

void Menu::setItemHidden(int index) {
  if ((index >= 0) && (index < m_buttons.size()) && !m_hidden.contains(index)) {
    m_hidden.push_back(index);
    m_buttons[index]->hide();
  }
}

void Menu::setElementsEnableAll() {
  for (int i{0}; i < m_buttons.size(); ++i) {
    m_buttons[i]->setVisible(!m_hidden.contains(i));
  }
}

void Menu::addWidget(QWidget *widget) {
  m_extraLay->addWidget(widget);
}

void Menu::paintEvent(QPaintEvent *) {
  theme::Colors const &c = theme::colors();
  QPainter             p(this);
  p.fillRect(rect(), c.header);
  p.setPen(c.border);
  p.drawLine(rect().topLeft(), rect().topRight());
}

void Menu::changeEvent(QEvent *event) {
  QWidget::changeEvent(event);
  if ((event->type() == QEvent::PaletteChange) && !m_buttons.isEmpty()) {
    restyle();
  }
}

}
