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
int px(double value) {
  return qMax(1, static_cast<int>(value * utils::ssf()));
}

int shadowMargin() {
  return px(12);
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
    QRectF const panel = QRectF(rect()).adjusted(margin, margin, -margin, -margin).translated(0, px(3));
    // layers from the widest, faint, to the panel's edge: they add up to a gradient
    for (int k{margin}; k > 0; --k) {
      QPainterPath path;
      path.addRoundedRect(panel.adjusted(-k, -k, k, k), px(17) + k, px(17) + k);
      painter.fillPath(path, QColor(15, 23, 42, 5));
    }
  }
};
}

ChartSideBar::ChartSideBar(QWidget *parent) : QFrame{parent} {
  setObjectName("sideBar");
  auto *lay = new QHBoxLayout(this);
  lay->setContentsMargins(0, 0, 0, 0);
  lay->setSpacing(0);

  // the panel: its title, ×, a muted line under the title, then the page; over the page, left of the rail
  QWidget *page    = parent ? parent : this;
  m_overlay        = new PanelShadow(page);
  auto *overlayLay = new QVBoxLayout(m_overlay);
  overlayLay->setContentsMargins(shadowMargin(), shadowMargin(), shadowMargin(), shadowMargin());
  m_panel = new QFrame(m_overlay);
  m_panel->setObjectName("sidePanel");
  m_panel->setFixedWidth(px(330));
  auto *panelLay = new QVBoxLayout(m_panel);
  panelLay->setContentsMargins(px(18), px(14), px(10), px(14));
  panelLay->setSpacing(px(4));
  auto *header = new QHBoxLayout();
  header->setContentsMargins(0, 0, px(6), 0);
  header->setSpacing(px(8));
  m_header = header;
  m_title    = new QLabel(m_panel);
  QFont font = m_title->font();
  font.setBold(true);
  font.setPointSizeF(font.pointSizeF() * 1.45);
  m_title->setFont(font);
  auto *closeBtn = new QToolButton(m_panel);
  closeBtn->setObjectName("panelClose");
  closeBtn->setText(QStringLiteral("×"));
  closeBtn->setToolTip(tr("Close"));
  closeBtn->setFocusPolicy(Qt::NoFocus);
  closeBtn->setFixedSize(px(36), px(36)); // a rounded square; the finger's target with the space around it
  header->addWidget(m_title, 1);
  header->addWidget(closeBtn);
  panelLay->addLayout(header);
  m_subtitle = new QLabel(m_panel);
  m_subtitle->setProperty("muted", true);
  m_subtitle->hide();
  panelLay->addWidget(m_subtitle);
  panelLay->addSpacing(px(8));
  m_stack = new QStackedWidget(m_panel);
  panelLay->addWidget(m_stack, 1);
  overlayLay->addWidget(m_panel);
  m_overlay->hide();

  // the rail: the panels' buttons, a group at the top and one at the bottom
  auto *rail = new QFrame(this);
  rail->setObjectName("sideRail");
  rail->setFixedWidth(px(52)); // icons only
  auto *railLay = new QVBoxLayout(rail);
  railLay->setContentsMargins(px(6), px(10), px(6), px(10));
  railLay->setSpacing(px(6));
  m_top    = new QVBoxLayout();
  m_bottom = new QVBoxLayout();
  m_top->setSpacing(px(6));
  m_bottom->setSpacing(px(6));
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

void ChartSideBar::setOverlayBottom(QWidget *widget) {
  if (m_overlayBottom) {
    m_overlayBottom->removeEventFilter(this);
  }
  m_overlayBottom = widget;
  if (m_overlayBottom) {
    m_overlayBottom->installEventFilter(this); // follows it
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
  if (watched == m_overlayBottom) {
    if ((event->type() == QEvent::Move) || (event->type() == QEvent::Resize)) {
      placePanel();
    }
    return QFrame::eventFilter(watched, event);
  }
  // (the application's events while the panel is open) a tap on the page beside the panel closes it; the tap goes
  // on to what it was on
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
  // by where it is, not by the widget: a tap nothing in the panel takes (an interval card's head) goes on to the
  // panel's parents, the page among them
  QPoint const at{page->mapFromGlobal(global.toPoint())};
  if (QRect(m_panel->mapTo(page, QPoint(0, 0)), m_panel->size()).contains(at) || QRect(mapTo(page, QPoint(0, 0)), size()).contains(at)) {
    return false;
  }
  // beside the panel, not below it (Program control: the Start / Stop bar)
  int const y{at.y()};
  if ((y >= m_overlay->y()) && (y <= m_overlay->geometry().bottom() - shadowMargin())) {
    close();
  }
  return false;
}

void ChartSideBar::placePanel() {
  QWidget *page = m_overlay->parentWidget();
  if (page == this) {
    return;
  }
  // left of the rail, a gap apart; as tall as the rail, or down to the widget it ends above
  QRect const rail{mapTo(page, QPoint(0, 0)), size()};
  int         bottom{rail.bottom()};
  if (m_overlayBottom) {
    bottom = std::min(bottom, m_overlayBottom->mapTo(page, QPoint(0, 0)).y() - px(10) - 1);
  }
  QRect const panel(rail.left() - px(10) - m_panel->width(), rail.top(), m_panel->width(), bottom - rail.top() + 1);
  int const   margin{shadowMargin()};
  m_overlay->setGeometry(panel.adjusted(-margin, -margin, margin, margin));
}

QToolButton *ChartSideBar::railButton(QString const &title, QString const &icon, bool bottom) {
  auto *button = new QToolButton(this);
  button->setObjectName("railButton");
  button->setToolTip(title);
  button->setFocusPolicy(Qt::NoFocus);
  button->setToolButtonStyle(Qt::ToolButtonIconOnly);
  button->setFixedSize(px(40), px(40));
  theme::setThemedIcon(button, icon, px(20), QStringLiteral("text"));
  (bottom ? m_bottom : m_top)->addWidget(button);
  return button;
}

QToolButton *ChartSideBar::addAction(QString const &title, QString const &icon, bool bottom) {
  return railButton(title, icon, bottom);
}

void ChartSideBar::addPage(QString const &key, QString const &title, QString const &icon, QWidget *page, bool bottom, bool scroll) {
  QToolButton *button = railButton(title, icon, bottom);
  button->setCheckable(true);

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
  placePanel();
  m_overlay->raise();
  m_overlay->show();
  qApp->installEventFilter(this); // a tap beside the panel closes it
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
  m_overlay->hide();
  qApp->removeEventFilter(this);
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
