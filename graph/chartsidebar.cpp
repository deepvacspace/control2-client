#include "chartsidebar.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include "theme.h"
#include "touchsupport.h"
#include "utils.h"

namespace deepvaccontrol {

namespace {
int px(double value) {
  return qMax(1, static_cast<int>(value * utils::ssf()));
}
} // namespace

ChartSideBar::ChartSideBar(QWidget *parent) : QFrame{parent} {
  setObjectName("sideBar");
  auto *lay = new QHBoxLayout(this);
  lay->setContentsMargins(0, 0, 0, 0);
  lay->setSpacing(0);

  // the panel: its title and ×, then the page
  m_panel = new QFrame(this);
  m_panel->setObjectName("sidePanel");
  m_panel->setFixedWidth(px(360));
  auto *panelLay = new QVBoxLayout(m_panel);
  panelLay->setContentsMargins(px(16), px(12), px(8), px(12));
  panelLay->setSpacing(px(8));
  auto *header = new QHBoxLayout();
  header->setContentsMargins(0, 0, px(8), 0);
  m_title    = new QLabel(m_panel);
  QFont font = m_title->font();
  font.setBold(true);
  font.setPointSizeF(font.pointSizeF() * 1.2);
  m_title->setFont(font);
  auto *closeBtn = new QToolButton(m_panel);
  closeBtn->setObjectName("panelClose");
  closeBtn->setText(QStringLiteral("×"));
  closeBtn->setToolTip(tr("Close"));
  closeBtn->setFocusPolicy(Qt::NoFocus);
  header->addWidget(m_title, 1);
  header->addWidget(closeBtn);
  panelLay->addLayout(header);
  m_stack = new QStackedWidget(m_panel);
  panelLay->addWidget(m_stack, 1);
  m_panel->hide();
  lay->addWidget(m_panel);

  // the rail: the panels' buttons, a group at the top and one at the bottom
  auto *rail = new QFrame(this);
  rail->setObjectName("sideRail");
  rail->setFixedWidth(px(44)); // icons only: as narrow as it gets
  auto *railLay = new QVBoxLayout(rail);
  railLay->setContentsMargins(px(4), px(8), px(4), px(8));
  railLay->setSpacing(px(4));
  m_top    = new QVBoxLayout();
  m_bottom = new QVBoxLayout();
  m_top->setSpacing(px(4));
  m_bottom->setSpacing(px(4));
  railLay->addLayout(m_top);
  railLay->addStretch();
  auto *line = new QFrame(rail);
  line->setObjectName("cardDivider");
  line->setFixedHeight(1);
  railLay->addWidget(line);
  railLay->addSpacing(px(4));
  railLay->addLayout(m_bottom);
  lay->addWidget(rail);

  connect(closeBtn, &QToolButton::clicked, this, &ChartSideBar::close);
}

void ChartSideBar::addPage(QString const &key, QString const &title, QString const &icon, QWidget *page, bool bottom, bool scroll) {
  auto *button = new QToolButton(this);
  button->setObjectName("railButton");
  button->setToolTip(title);
  button->setCheckable(true);
  button->setFocusPolicy(Qt::NoFocus);
  button->setToolButtonStyle(Qt::ToolButtonIconOnly);
  button->setFixedSize(px(36), px(36));
  theme::setThemedIcon(button, icon, px(18), QStringLiteral("muted"));
  (bottom ? m_bottom : m_top)->addWidget(button);

  QWidget *holder = page;
  if (scroll) {
    auto *area = new QScrollArea(m_stack);
    area->setWidgetResizable(true);
    area->setFrameShape(QFrame::NoFrame);
    area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    area->setWidget(page);
    area->viewport()->setAutoFillBackground(false);
    page->setAutoFillBackground(false);
    area->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; }"));
    touch::enableScrolling(area);
    holder = area;
  }
  m_stack->addWidget(holder);
  m_pages.insert(key, Page{button, holder, title});
  connect(button, &QToolButton::clicked, this, [this, key] { toggle(key); });
}

void ChartSideBar::open(QString const &key) {
  auto const page = m_pages.constFind(key);
  if ((page == m_pages.cend()) || !page->button->isEnabled()) {
    return;
  }
  for (auto it = m_pages.cbegin(); it != m_pages.cend(); ++it) {
    it->button->setChecked(it.key() == key);
  }
  m_current = key;
  m_title->setText(page->title);
  m_stack->setCurrentWidget(page->holder);
  m_panel->show();
  emit opened(key);
}

void ChartSideBar::close() {
  if (m_current.isEmpty()) {
    return;
  }
  for (Page const &page : std::as_const(m_pages)) {
    page.button->setChecked(false);
  }
  m_current.clear();
  m_panel->hide();
  emit closed();
}

void ChartSideBar::toggle(QString const &key) {
  if (m_current == key) {
    close();
  } else {
    open(key);
  }
}

QString ChartSideBar::current() const {
  return m_current;
}

void ChartSideBar::setPageEnabled(QString const &key, bool enabled) {
  auto const page = m_pages.constFind(key);
  if (page == m_pages.cend()) {
    return;
  }
  page->button->setEnabled(enabled);
  if (!enabled && (m_current == key)) {
    close();
  }
}

} // namespace deepvaccontrol
