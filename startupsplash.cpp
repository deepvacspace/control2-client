#include "startupsplash.h"

#include <QApplication>
#include <QEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QScreen>
#include <QTimer>
#include <QVariantAnimation>
#include <algorithm>

#include "theme.h"

namespace deepvaccontrol {

namespace {
QPointer<StartupSplash> g_splash; // the one shown: StartupSplash::advance()
}

StartupSplash::StartupSplash() : QWidget{nullptr, Qt::SplashScreen | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint} {
  setAttribute(Qt::WA_TranslucentBackground); // the rounded corners and the shadow
  setAttribute(Qt::WA_DeleteOnClose);
  // the white wordmark, sharp on high-density screens
  qreal const dpr{qApp->devicePixelRatio()};
  m_logo = QPixmap(QStringLiteral(":/imgs/deepvac-logo-white.png")).scaledToWidth(qRound(theme::dp(300) * dpr), Qt::SmoothTransformation);
  m_logo.setDevicePixelRatio(dpr);
  m_message = QStringLiteral("Starting…");
  int const margin{shadowMargin()};
  setFixedSize(theme::dp(660) + 2 * margin, theme::dp(360) + 2 * margin);
  if (QScreen const *screen = QApplication::primaryScreen()) {
    move(screen->availableGeometry().center() - rect().center());
  }
  g_splash = this;
}

int StartupSplash::shadowMargin() const {
  return theme::dp(36);
}

void StartupSplash::setMessage(QString const &text) {
  m_message = text;
  repaintNow(true);
}

void StartupSplash::setProgress(double progress) {
  double const value{std::clamp(progress, m_progress, 1.0)};
  if (value == m_progress) {
    return;
  }
  m_progress = value;
  repaintNow(false);
}

void StartupSplash::advance(double progress) {
  if (g_splash && !g_splash->m_closing) {
    g_splash->setProgress(progress);
  }
}

void StartupSplash::repaintNow(bool events) {
  if (!isVisible()) {
    return;
  }
  repaint(); // drawn while the start goes on
  if (events) {
    // between the steps of the start only: a step half done must not have its queued events run
    raise();
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  }
}

void StartupSplash::paintEvent(QPaintEvent *) {
  namespace guide = theme::guide;
  QPainter painter(this);
  painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
  double const margin{static_cast<double>(shadowMargin())};
  QRectF const card = QRectF(rect()).adjusted(margin, margin, -margin, -margin);
  double const corner{theme::radius() * 2.0};

  // a soft shadow under the card, a little lower than it
  constexpr int RINGS{14};
  painter.setPen(Qt::NoPen);
  painter.setBrush(QColor(0, 0, 0, 7));
  for (int ring{RINGS}; ring >= 1; --ring) {
    double const spread{margin * 0.7 * ring / RINGS};
    painter.drawRoundedRect(card.adjusted(-spread, -spread + margin * 0.25, spread, spread + margin * 0.25), corner + spread, corner + spread);
  }

  // the card: the sidebar's Slate, lighter at the top, with the accent glowing in two of its corners
  QPainterPath path;
  path.addRoundedRect(card, corner, corner);
  QLinearGradient back(card.topLeft(), card.bottomLeft());
  back.setColorAt(0.0, guide::slateRaised);
  back.setColorAt(1.0, guide::slate);
  painter.fillPath(path, back);
  painter.save();
  painter.setClipPath(path);
  auto glow = [&](QPointF const &centre, double radius, QColor color, int alpha) {
    QRadialGradient gradient(centre, radius);
    color.setAlpha(alpha);
    gradient.setColorAt(0.0, color);
    color.setAlpha(0);
    gradient.setColorAt(1.0, color);
    painter.fillRect(card, gradient);
  };
  glow(card.topRight() + QPointF(-card.width() * 0.08, card.height() * 0.05), card.width() * 0.62, guide::sky, 70);
  glow(card.bottomLeft() + QPointF(card.width() * 0.05, 0.0), card.width() * 0.5, guide::cobalt, 60);
  painter.restore();
  painter.setPen(QPen(guide::slateLine, std::max(1, theme::dp(1))));
  painter.setBrush(Qt::NoBrush);
  painter.drawPath(path);

  // the logo, in the upper half
  QSizeF const  logoSize = m_logo.deviceIndependentSize();
  QPointF const logoAt(card.center().x() - logoSize.width() / 2.0, card.top() + card.height() * 0.40 - logoSize.height());
  painter.drawPixmap(logoAt, m_logo);

  // what is being done
  QFont font = QApplication::font();
  font.setPointSizeF(font.pointSizeF() * 1.05);
  painter.setFont(font);
  painter.setPen(guide::pearl);
  QRectF const textRect(card.left(), card.top() + card.height() * 0.56, card.width(), painter.fontMetrics().height() * 1.4);
  painter.drawText(textRect, Qt::AlignCenter, m_message);

  // how far: a thin rounded bar, Cobalt to Sky
  double const barHeight{std::max(3.0, static_cast<double>(theme::dp(7)))};
  QRectF const track(card.center().x() - card.width() * 0.27, textRect.bottom() + theme::dp(14), card.width() * 0.54, barHeight);
  painter.setPen(Qt::NoPen);
  painter.setBrush(guide::slateLine);
  painter.drawRoundedRect(track, barHeight / 2.0, barHeight / 2.0);
  if (m_progress > 0.0) {
    QRectF fill = track;
    fill.setWidth(std::max(barHeight, track.width() * m_progress));
    QLinearGradient bar(track.topLeft(), track.topRight());
    bar.setColorAt(0.0, guide::cobalt);
    bar.setColorAt(1.0, guide::sky);
    painter.setBrush(bar);
    painter.drawRoundedRect(fill, barHeight / 2.0, barHeight / 2.0);
  }

  // the product and its version, small at the bottom
  QFont small = QApplication::font();
  small.setPointSizeF(small.pointSizeF() * 0.78);
  small.setLetterSpacing(QFont::PercentageSpacing, 106);
  painter.setFont(small);
  painter.setPen(guide::haze);
  QString const version = QApplication::applicationVersion();
  QString const product = version.isEmpty() ? QApplication::applicationName()
                                            : QStringLiteral("%1  ·  v%2").arg(QApplication::applicationName(), version);
  painter.drawText(QRectF(card.left(), card.bottom() - theme::dp(26) - painter.fontMetrics().height(), card.width(), painter.fontMetrics().height()),
                   Qt::AlignCenter, product);
}

void StartupSplash::finishWhenShown(QWidget *window) {
  m_window = window;
  setProgress(0.92);
  repaintNow(true);
  window->installEventFilter(this);
  QTimer::singleShot(20'000, this, &StartupSplash::finish); // whatever happens, not for ever
}

bool StartupSplash::eventFilter(QObject *watched, QEvent *event) {
  // the window drawn at its full size, and a moment for what its first frames start: then the window alone
  if ((watched == m_window) && (event->type() == QEvent::Paint) && !m_closing) {
    QScreen const *screen = m_window->screen();
    if (!screen || (m_window->width() >= screen->availableGeometry().width() * 9 / 10)) {
      m_window->removeEventFilter(this);
      QTimer::singleShot(250, this, &StartupSplash::finish);
    }
  }
  return QWidget::eventFilter(watched, event);
}

void StartupSplash::finish() {
  if (m_closing) {
    return;
  }
  m_closing = true;
  if (m_window) {
    m_window->removeEventFilter(this);
  }
  // the event loop runs by now: the bar to its end, then the card fades out
  auto *fill = new QVariantAnimation(this);
  fill->setDuration(220);
  fill->setStartValue(m_progress);
  fill->setEndValue(1.0);
  connect(fill, &QVariantAnimation::valueChanged, this, [this](QVariant const &value) {
    m_progress = value.toDouble();
    update();
  });
  auto *fade = new QPropertyAnimation(this, "windowOpacity", this);
  fade->setDuration(260);
  fade->setStartValue(1.0);
  fade->setEndValue(0.0);
  connect(fill, &QVariantAnimation::finished, fade, [fade] { fade->start(); });
  connect(fade, &QPropertyAnimation::finished, this, &QWidget::close);
  fill->start();
}

}
