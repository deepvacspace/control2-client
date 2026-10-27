#include "touchsupport.h"

#include <QAbstractScrollArea>
#include <QCoreApplication>
#include <QEvent>
#include <QInputDevice>
#include <QMouseEvent>
#include <QPointer>
#include <QScroller>
#include <QScrollerProperties>
#include <QTimer>
#include <QWidget>

#include "utils.h"

namespace deepvaccontrol::touch {

namespace {
// scroll areas are given touch scrolling when first shown (Polish) if they are inside one of the roots
class ScrollWatcher : public QObject {
public:
  static ScrollWatcher &instance() {
    static auto *watcher = new ScrollWatcher(qApp);
    return *watcher;
  }
  void addRoot(QWidget *root) {
    m_roots.push_back(root);
    for (QAbstractScrollArea *area : root->findChildren<QAbstractScrollArea *>()) {
      enable(area);
    }
  }

protected:
  bool eventFilter(QObject *watched, QEvent *event) override {
    if (event->type() == QEvent::Polish) {
      if (auto *area = qobject_cast<QAbstractScrollArea *>(watched)) {
        for (QPointer<QWidget> const &root : std::as_const(m_roots)) {
          if (root && root->isAncestorOf(area)) {
            enable(area);
            break;
          }
        }
      }
    }
    return QObject::eventFilter(watched, event);
  }

private:
  explicit ScrollWatcher(QObject *parent) : QObject{parent} {
    qApp->installEventFilter(this);
  }
  static void enable(QAbstractScrollArea *area) {
    if (!area->property("touchScrolling").toBool()) {
      area->setProperty("touchScrolling", true);
      enableScrolling(area);
    }
  }

  QVector<QPointer<QWidget>> m_roots;
};

bool fromTouch(QMouseEvent const *event) {
  return event->device() && (event->device()->type() == QInputDevice::DeviceType::TouchScreen);
}

// press and hold: the context menu of the target
class LongPress : public QObject {
public:
  LongPress(QWidget *target, QWidget *events, bool releaseFirst)
      : QObject{events}, m_target{target}, m_events{events}, m_releaseFirst{releaseFirst}, m_timer{new QTimer(this)} {
    m_timer->setSingleShot(true);
    m_timer->setInterval(550);
    connect(m_timer, &QTimer::timeout, this, &LongPress::fire);
    events->installEventFilter(this);
  }

protected:
  bool eventFilter(QObject *watched, QEvent *event) override {
    if (watched != m_events) {
      return false;
    }
    switch (event->type()) {
      case QEvent::MouseButtonPress: {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (fromTouch(mouse) && (mouse->button() == Qt::LeftButton)) {
          m_pressPos = mouse->position().toPoint();
          m_globalPos = mouse->globalPosition();
          m_timer->start();
        }
        break;
      }
      case QEvent::MouseMove: {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (m_timer->isActive() && ((mouse->position().toPoint() - m_pressPos).manhattanLength() > 12 * utils::ssf())) {
          m_timer->stop(); // a drag, not a hold
        }
        break;
      }
      case QEvent::MouseButtonRelease:
      case QEvent::Hide:
        m_timer->stop();
        break;
      default:
        break;
    }
    return false;
  }

private:
  void fire() {
    if (!m_target || !m_events) {
      return;
    }
    if (m_releaseFirst) {
      QMouseEvent release(QEvent::MouseButtonRelease, QPointF(m_pressPos), m_globalPos, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
      QCoreApplication::sendEvent(m_events, &release);
    }
    // an item view reports its menu position in viewport coordinates, other widgets in their own
    auto *area = qobject_cast<QAbstractScrollArea *>(m_target.data());
    bool const own{(m_events == m_target) || (area && (m_events == area->viewport()))};
    QPoint const pos = own ? m_pressPos : m_target->mapFrom(m_events, m_pressPos);
    QMetaObject::invokeMethod(m_target, "customContextMenuRequested", Q_ARG(QPoint, pos));
  }

  QPointer<QWidget> m_target;
  QPointer<QWidget> m_events;
  bool              m_releaseFirst;
  QTimer           *m_timer;
  QPoint            m_pressPos;
  QPointF           m_globalPos;
};
}

void enableScrolling(QAbstractScrollArea *area) {
  QScroller::grabGesture(area->viewport(), QScroller::TouchGesture);
  QScrollerProperties properties = QScroller::scroller(area->viewport())->scrollerProperties();
  properties.setScrollMetric(QScrollerProperties::OvershootDragResistanceFactor, 0.0);
  properties.setScrollMetric(QScrollerProperties::OvershootScrollDistanceFactor, 0.0);
  properties.setScrollMetric(QScrollerProperties::VerticalOvershootPolicy, QScrollerProperties::OvershootAlwaysOff);
  properties.setScrollMetric(QScrollerProperties::HorizontalOvershootPolicy, QScrollerProperties::OvershootAlwaysOff);
  QScroller::scroller(area->viewport())->setScrollerProperties(properties);
}

void enableScrollingWithin(QWidget *root) {
  ScrollWatcher::instance().addRoot(root);
}

int tapSlop() {
  return static_cast<int>(12 * utils::ssf());
}

void enableLongPress(QWidget *target, QWidget *events, bool releaseFirst) {
  new LongPress(target, events ? events : target, releaseFirst);
}

}
