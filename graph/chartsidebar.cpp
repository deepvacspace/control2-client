#include "chartsidebar.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include "theme.h"
#include "touchsupport.h"
#include "utils.h"

namespace deepvaccontrol {

namespace {
int shadowMargin() {
  return theme::dp(22);
}

int floatingWidth() {
  return theme::dp(618);
}

// holds the panel, a shadow margin around it: a soft shadow, a little lower than the panel
class PanelShadow : public QWidget {
public:
  using QWidget::QWidget;

protected:
  void paintEvent(QPaintEvent *) override {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    int const    margin{shadowMargin()};
    QRectF const panel = QRectF(rect()).adjusted(margin, margin, -margin, -margin).translated(0, theme::dp(5));
    // layers from the widest, faint, to the panel's edge: they add up to a gradient
    for (int k{margin}; k > 0; --k) {
      QPainterPath path;
      path.addRoundedRect(panel.adjusted(-k, -k, k, k), theme::radius() + k, theme::radius() + k);
      painter.fillPath(path, QColor(30, 38, 51, 5));
    }
  }
};
}

ChartSideBar::ChartSideBar(QWidget *parent) : QFrame{parent} {
  setObjectName("sideBar");
  m_row = new QHBoxLayout(this);
  m_row->setContentsMargins(0, 0, 0, 0);
  m_row->setSpacing(theme::dp(6));

  // the panel: its title, ×, a muted line under the title, then the page; floating over the page, or docked
  QWidget *page    = parent ? parent : this;
  m_overlay        = new PanelShadow(page);
  auto *overlayLay = new QVBoxLayout(m_overlay);
  overlayLay->setContentsMargins(shadowMargin(), shadowMargin(), shadowMargin(), shadowMargin());
  m_panel = new QFrame(m_overlay);
  m_panel->setObjectName("sidePanel");
  m_panel->setFixedWidth(floatingWidth());
  m_panelLay = new QVBoxLayout(m_panel);
  m_panelLay->setContentsMargins(theme::gap(), theme::gap(), theme::gap(), theme::gap()); // as in the side column
  m_panelLay->setSpacing(theme::dp(8));
  auto *header = new QHBoxLayout();
  header->setContentsMargins(0, 0, theme::dp(8), 0);
  header->setSpacing(theme::dp(16));
  m_header = header;
  m_title    = new QLabel(m_panel);
  QFont font = m_title->font();
  font.setBold(true);
  font.setPointSizeF(font.pointSizeF() * 1.45);
  m_title->setFont(font);
  auto *closeBtn = new QToolButton(m_panel);
  closeBtn->setObjectName("panelClose");
  theme::setThemedIcon(closeBtn, QStringLiteral("close"), theme::dp(33));
  closeBtn->setToolTip(tr("Close"));
  closeBtn->setFocusPolicy(Qt::NoFocus);
  closeBtn->setFixedSize(theme::dp(67), theme::dp(67)); // a rounded square; the finger's target with the space around it
  header->addWidget(m_title, 1);
  header->addWidget(closeBtn);
  m_panelLay->addLayout(header);
  m_subtitle = new QLabel(m_panel);
  m_subtitle->setProperty("muted", true);
  m_subtitle->hide();
  m_panelLay->addWidget(m_subtitle);
  m_panelLay->addSpacing(theme::dp(16));
  m_stack = new QStackedWidget(m_panel);
  m_panelLay->addWidget(m_stack, 1);
  overlayLay->addWidget(m_panel);
  m_overlay->hide();

  connect(closeBtn, &QToolButton::clicked, this, &ChartSideBar::close);
}

void ChartSideBar::setDock(QWidget *dock) {
  if (dock == m_dock) {
    return;
  }
  m_dock         = dock;
  bool const in{m_dock != nullptr};
  QWidget   *host = in ? m_dock : m_overlay;
  m_panel->setParent(host);
  host->layout()->addWidget(m_panel);
  // docked: on the card holding it, its whole width; floating: a card of its own
  m_panel->setStyleSheet(in ? QStringLiteral("QFrame#sidePanel { background: transparent; border: none; }") : QString{});
  if (in) {
    m_panelLay->setContentsMargins(0, 0, 0, 0);
    m_panel->setMinimumWidth(0);
    m_panel->setMaximumWidth(QWIDGETSIZE_MAX);
  } else {
    // the contents where the side column's would be: a gap in from the card's edges
    m_panelLay->setContentsMargins(theme::gap(), theme::gap(), theme::gap(), theme::gap());
    m_panel->setFixedWidth(floatingWidth());
  }
  showPanel();
}

bool ChartSideBar::docked() const {
  return m_dock != nullptr;
}

void ChartSideBar::setFloatArea(QWidget *area) {
  if (m_floatArea) {
    m_floatArea->removeEventFilter(this);
  }
  m_floatArea = area;
  if (m_floatArea) {
    m_floatArea->installEventFilter(this); // follows it
  }
  placePanel();
}

void ChartSideBar::resizeEvent(QResizeEvent *event) {
  QFrame::resizeEvent(event);
  placePanel();
}

void ChartSideBar::moveEvent(QMoveEvent *event) {
  QFrame::moveEvent(event);
  placePanel();
}

bool ChartSideBar::eventFilter(QObject *watched, QEvent *event) {
  if (watched == m_floatArea) {
    if ((event->type() == QEvent::Move) || (event->type() == QEvent::Resize)) {
      placePanel();
    }
    return QFrame::eventFilter(watched, event);
  }
  // (the application's events while the floating panel is open) a tap on the page beside the panel closes it; the
  // tap goes on to what it was on
  bool const mouse{event->type() == QEvent::MouseButtonPress};
  if (!mouse && (event->type() != QEvent::TouchBegin)) {
    return false;
  }
  auto          *widget = qobject_cast<QWidget *>(watched);
  QWidget const *page   = m_overlay->parentWidget();
  if (!widget || !((widget == page) || page->isAncestorOf(widget)) || (widget == m_overlay) || m_overlay->isAncestorOf(widget)
      || (widget == this) || isAncestorOf(widget)) {
    return false;
  }
  QPointF global;
  if (mouse) {
    global = static_cast<QMouseEvent *>(event)->globalPosition();
  } else if (!static_cast<QTouchEvent *>(event)->points().isEmpty()) {
    global = static_cast<QTouchEvent *>(event)->points().first().globalPosition();
  }
  // by where it is, not by the widget: a tap nothing in the panel takes goes on to the panel's parents, the page
  // among them
  QPoint const at{page->mapFromGlobal(global.toPoint())};
  if (!QRect(m_panel->mapTo(page, QPoint(0, 0)), m_panel->size()).contains(at) && !QRect(mapTo(page, QPoint(0, 0)), size()).contains(at)) {
    close();
  }
  return false;
}

void ChartSideBar::placePanel() {
  QWidget *page = m_overlay->parentWidget();
  if ((page == this) || !m_floatArea || m_dock) {
    return;
  }
  // where the side column stands while it shows (ChartPane::setSideWidget: 2 parts of the row's 7, a gap from the
  // chart): the same width at the area's right edge, from its top; down to the bar when the bar is in the area
  QRect const area{m_floatArea->mapTo(page, QPoint(0, 0)), m_floatArea->size()};
  QRect const bar{mapTo(page, QPoint(0, 0)), size()};
  int const   gap{theme::gap()};
  int         bottom{area.bottom()};
  if (isVisibleTo(page) && (bar.top() > area.top()) && (bar.top() <= area.bottom())) {
    bottom = bar.top() - gap - 1;
  }
  int const width{std::max(theme::dp(200), (area.width() - gap) * 2 / 7)};
  m_panel->setFixedWidth(width);
  QRect const panel(area.right() - width + 1, area.top(), width, std::max(theme::dp(200), bottom - area.top() + 1));
  int const   margin{shadowMargin()};
  m_overlay->setGeometry(panel.adjusted(-margin, -margin, margin, margin));
}

void ChartSideBar::showPanel() {
  bool const open{!m_current.isEmpty()};
  if (m_dock || !open) {
    m_overlay->hide();
    qApp->removeEventFilter(this);
    if (m_dock) {
      m_panel->setVisible(open);
    }
    return;
  }
  placePanel();
  m_panel->show();
  m_overlay->raise();
  m_overlay->show();
  qApp->installEventFilter(this); // a tap beside the panel closes it
}

QSize ChartSideBar::barButtonSize() {
  return {theme::dp(52), theme::dp(52)}; // a 28 icon, a finger's target
}

void ChartSideBar::addPage(QString const &key, QString const &title, QString const &icon, QWidget *page, bool scroll) {
  auto *button = new QToolButton(this);
  button->setObjectName("toolButton");
  button->setToolTip(title);
  button->setFocusPolicy(Qt::NoFocus);
  button->setCheckable(true);
  button->setFixedSize(barButtonSize());
  theme::setThemedIcon(button, icon, theme::dp(28));
  m_row->addWidget(button);

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

void ChartSideBar::setHeaderWidget(QString const &key, QWidget *widget) {
  auto page = m_pages.find(key);
  if (page == m_pages.end()) {
    return;
  }
  widget->setParent(m_panel);
  m_header->insertWidget(1, widget); // after the title, before the ×
  widget->setVisible(m_current == key);
  page->header = widget;
}

void ChartSideBar::setSubtitle(QString const &key, QString const &text) {
  auto page = m_pages.find(key);
  if (page == m_pages.end()) {
    return;
  }
  page->subtitle = text;
  if (m_current == key) {
    showSubtitle();
  }
}

void ChartSideBar::showSubtitle() {
  QString const text = m_pages.value(m_current).subtitle;
  m_subtitle->setText(text);
  m_subtitle->setVisible(!text.isEmpty());
}

void ChartSideBar::open(QString const &key) {
  auto const page = m_pages.constFind(key);
  if ((page == m_pages.cend()) || !page->button->isEnabled()) {
    return;
  }
  for (auto it = m_pages.cbegin(); it != m_pages.cend(); ++it) {
    it->button->setChecked(it.key() == key);
    if (it->header) {
      it->header->setVisible(it.key() == key);
    }
  }
  m_current = key;
  m_title->setText(page->title);
  showSubtitle();
  m_stack->setCurrentWidget(page->holder);
  showPanel();
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
  showPanel();
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

}
