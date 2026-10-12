#include "overlaydrawer.h"

#include <QEvent>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPropertyAnimation>
#include <QVBoxLayout>

#include "utils.h"

namespace deepvaccontrol {

OverlayDrawer::OverlayDrawer(QWidget *host, int width, Side side)
    : QWidget{host}
    , m_host{host}
    , m_panel{new QFrame(this)}
    , m_slide{new QPropertyAnimation(m_panel, "pos", this)}
    , m_width{static_cast<int>(width * utils::ssf())}
    , m_side{side} {
  setFocusPolicy(Qt::StrongFocus);
  m_panel->setObjectName("pageCard");
  m_panel->setAutoFillBackground(true);
  auto *lay = new QVBoxLayout(m_panel);
  double const ssf{utils::ssf()};
  lay->setContentsMargins(10 * ssf, 10 * ssf, 10 * ssf, 10 * ssf);

  auto *shadow = new QGraphicsDropShadowEffect(m_panel);
  shadow->setBlurRadius(24 * ssf);
  shadow->setOffset((side == Side::LEFT ? 4 : -4) * ssf, 0);
  shadow->setColor(QColor(15, 23, 42, 90));
  m_panel->setGraphicsEffect(shadow);

  m_slide->setDuration(180);
  m_slide->setEasingCurve(QEasingCurve::OutCubic);
  connect(m_slide, &QPropertyAnimation::finished, this, [this] {
    if (!m_open) {
      hide();
    }
  });

  m_host->installEventFilter(this);
  hide();
}

void OverlayDrawer::setContent(QWidget *content) {
  content->setParent(m_panel);
  m_panel->layout()->addWidget(content);
}

void OverlayDrawer::open() {
  if (m_open) {
    return;
  }
  m_open = true;
  setGeometry(m_host->rect());
  placePanel();
  m_panel->move(hiddenPos());
  show();
  raise();
  setFocus();
  m_slide->stop();
  m_slide->setStartValue(m_panel->pos());
  m_slide->setEndValue(shownPos());
  m_slide->start();
  emit openChanged(true);
}

void OverlayDrawer::close() {
  if (!m_open) {
    return;
  }
  m_open = false;
  m_slide->stop();
  m_slide->setStartValue(m_panel->pos());
  m_slide->setEndValue(hiddenPos());
  m_slide->start();
  emit openChanged(false);
}

void OverlayDrawer::toggle() {
  m_open ? close() : open();
}

void OverlayDrawer::placePanel() {
  m_panel->resize(std::min(m_width, width()), height());
  if (m_open && (m_slide->state() != QAbstractAnimation::Running)) {
    m_panel->move(shownPos());
  }
}

QPoint OverlayDrawer::shownPos() const {
  return {(m_side == Side::LEFT) ? 0 : width() - m_panel->width(), 0};
}

QPoint OverlayDrawer::hiddenPos() const {
  int const shadow{static_cast<int>(30 * utils::ssf())}; // the shadow goes out of sight too
  return {(m_side == Side::LEFT) ? -m_panel->width() - shadow : width() + shadow, 0};
}

bool OverlayDrawer::eventFilter(QObject *watched, QEvent *event) {
  if ((watched == m_host) && (event->type() == QEvent::Resize) && isVisible()) {
    setGeometry(m_host->rect());
    placePanel();
  }
  return QWidget::eventFilter(watched, event);
}

void OverlayDrawer::paintEvent(QPaintEvent *) {
  QPainter p(this);
  p.fillRect(rect(), QColor(15, 23, 42, 50)); // dims the rest of the host
}

void OverlayDrawer::mousePressEvent(QMouseEvent *event) {
  if (!m_panel->geometry().contains(event->position().toPoint())) {
    close();
  }
}

void OverlayDrawer::keyPressEvent(QKeyEvent *event) {
  if (event->key() == Qt::Key_Escape) {
    close();
    return;
  }
  QWidget::keyPressEvent(event);
}

} // namespace deepvaccontrol
