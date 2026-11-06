#include "menu.h"

#include <QEvent>
#include <QPainter>
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
    , m_listLay{new QVBoxLayout()}
    , m_extraLay{new QVBoxLayout()}
    , m_currentSelected{0}
    , m_prevSelected{0}
    , m_buttonHeight{buttonHeight} {
  auto *outer = new QVBoxLayout(this);
  outer->setContentsMargins(0, 0, 0, 0);
  m_content = new QWidget(this);
  outer->addWidget(m_content);
  auto *mainlay = new QVBoxLayout(m_content);
  mainlay->setContentsMargins(px(7), px(8), px(7), px(10));
  mainlay->setSpacing(px(8));
  m_listLay->setSpacing(px(4));
  m_extraLay->setSpacing(px(10));
  m_extraLay->setAlignment(Qt::AlignHCenter);

  auto *separator = new QWidget(m_content);
  separator->setObjectName("menuSeparator");
  separator->setFixedHeight(px(1));

  mainlay->addLayout(m_listLay);
  // the light button, the clock and the product name stay at the bottom whatever is shown above them
  mainlay->addStretch(1);
  mainlay->addWidget(separator);
  mainlay->addSpacing(px(4));
  mainlay->addLayout(m_extraLay);
  setFixedWidth(sidebarWidth());
  restyle();
}

int Menu::sidebarWidth() {
  return px(76);
}

void Menu::addItem(QString const &name, QString const &iconKind) {
  auto *itemBtn = new QToolButton(m_content);
  itemBtn->setObjectName("menuButton");
  itemBtn->setText(name);
  itemBtn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
  itemBtn->setCheckable(true);
  itemBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  // two-line labels ("Manual\ncontrol") get a taller button
  itemBtn->setFixedHeight(px(name.contains('\n') ? m_buttonHeight + 12 : m_buttonHeight + 2));
  itemBtn->setIconSize(QSize(px(17), px(17)));
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
  busy                    = true;
  theme::Colors const &sc = theme::sidebarColors();
  QColor const         selectedBg("#dbe7fb");
  QColor const         selectedText("#1e3a8a");
  setStyleSheet(QStringLiteral(R"(
QToolButton#menuButton { background: transparent; color: %1; border: none; border-radius: %4px; padding-top: %5px; }
QToolButton#menuButton:hover { background: %6; }
QToolButton#menuButton:checked { color: %2; background: %3; }
QWidget#menuSeparator { background: %7; }
)")
                  .arg(sc.text.name(), selectedText.name(), selectedBg.name())
                  .arg(px(9))
                  .arg(px(4))
                  .arg(QColor(255, 255, 255, 18).name(QColor::HexArgb), QColor(255, 255, 255, 30).name(QColor::HexArgb)));
  QFont small = font();
  small.setPointSizeF(QWidget::font().pointSizeF() * 0.9);
  for (int i{0}; i < m_buttons.size(); ++i) {
    m_buttons[i]->setFont(small);
    if (!m_iconKinds[i].isEmpty()) {
      m_buttons[i]->setIcon(theme::menuIcon(m_iconKinds[i], sc.text, selectedText, px(17)));
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

void Menu::addListWidget(QWidget *widget) {
  m_listLay->addWidget(widget);
}

void Menu::paintEvent(QPaintEvent *) {
  theme::Colors const &sc = theme::sidebarColors();
  QPainter             p(this);
  p.setRenderHint(QPainter::Antialiasing);
  // a rounded navy card; outlined on the dark theme, where the page background is navy too
  double const radius{14 * utils::ssf()};
  p.setPen(theme::isDark() ? QPen(sc.border, px(1)) : Qt::NoPen);
  p.setBrush(sc.header);
  p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
}

void Menu::changeEvent(QEvent *event) {
  QWidget::changeEvent(event);
  if ((event->type() == QEvent::PaletteChange) && !m_buttons.isEmpty()) {
    restyle();
  }
}

}
