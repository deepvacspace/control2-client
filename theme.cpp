#include "theme.h"

#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QtMath>

#include "qcustomplot.h"
#include "utils.h"

namespace deepvaccontrol::theme {

namespace {
bool g_dark{true};

Colors const NAVY{
  QColor("#0b1524"), QColor("#0d1a2c"), QColor("#122138"), QColor("#0f1c30"), QColor("#22385a"), QColor("#e6edf7"),
  QColor("#8193ad"), QColor("#3b82f6"), QColor("#22c55e"), QColor("#5aa9ff"), QColor("#5b6b82"), QColor("#ef4444"),
  QColor("#f59e0b"), QColor("#38bdf8"), QColor("#facc15")};

Colors const LIGHT{
  QColor("#eef2f7"), QColor("#ffffff"), QColor("#ffffff"), QColor("#f5f7fa"), QColor("#cfd8e3"), QColor("#14213a"),
  QColor("#5c6b80"), QColor("#2563eb"), QColor("#16a34a"), QColor("#2563eb"), QColor("#94a3b8"), QColor("#dc2626"),
  QColor("#d97706"), QColor("#0284c7"), QColor("#ca8a04")};

int px(double value) {
  return qMax(1, static_cast<int>(value * utils::ssf()));
}
} // namespace

bool isDark() {
  return g_dark;
}

Colors const &colors() {
  return g_dark ? NAVY : LIGHT;
}

QPalette palette(bool dark) {
  Colors const &c = dark ? NAVY : LIGHT;
  QPalette      p;
  p.setColor(QPalette::Window, c.window);
  p.setColor(QPalette::WindowText, c.text);
  p.setColor(QPalette::Base, c.panelAlt);
  p.setColor(QPalette::AlternateBase, c.panel);
  p.setColor(QPalette::ToolTipBase, c.panel);
  p.setColor(QPalette::ToolTipText, c.text);
  p.setColor(QPalette::PlaceholderText, c.muted);
  p.setColor(QPalette::Text, c.text);
  p.setColor(QPalette::Button, c.panel);
  p.setColor(QPalette::ButtonText, c.text);
  p.setColor(QPalette::BrightText, Qt::white);
  p.setColor(QPalette::Light, c.border.lighter(130));
  p.setColor(QPalette::Midlight, c.border);
  p.setColor(QPalette::Mid, c.border);
  p.setColor(QPalette::Dark, c.window.darker(120));
  p.setColor(QPalette::Shadow, Qt::black);
  p.setColor(QPalette::Link, c.accent);
  p.setColor(QPalette::Highlight, c.accent);
  p.setColor(QPalette::HighlightedText, Qt::white);
  for (auto role : {QPalette::Text, QPalette::ButtonText, QPalette::WindowText}) {
    p.setColor(QPalette::Disabled, role, c.muted.darker(dark ? 130 : 90));
  }
  p.setColor(QPalette::Disabled, QPalette::Button, c.panel);
  return p;
}

QString styleSheet(bool dark) {
  Colors const &c = dark ? NAVY : LIGHT;
  QString       qss = QStringLiteral(R"(
QPushButton, QToolButton#plain {
  background: %1; color: %2; border: %4px solid %3; border-radius: %5px; padding: %6px %7px;
}
QPushButton:hover { border-color: %8; }
QPushButton:pressed, QPushButton:checked { background: %9; border-color: %8; }
QPushButton:disabled { color: %10; border-color: %11; background: %12; }
QPushButton#stopButton { background: %13; color: white; border: none; font-weight: bold; }
QPushButton#stopButton:disabled { background: %11; color: %10; }
QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox, QDateTimeEdit, QTimeEdit, QDateEdit, QTextEdit, QPlainTextEdit {
  background: %14; color: %2; border: %4px solid %3; border-radius: %15px; selection-background-color: %8;
}
QLineEdit:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled, QComboBox:disabled, QTextEdit:disabled,
QPlainTextEdit:disabled { color: %10; background: %12; border-color: %11; }
QComboBox QAbstractItemView { background: %1; color: %2; border: %4px solid %3; selection-background-color: %8; }
QListView, QListWidget, QTreeView, QTreeWidget, QTableView, QTableWidget, QTextBrowser {
  background: %14; color: %2; border: %4px solid %3; border-radius: %15px; gridline-color: %3;
  alternate-background-color: %1; selection-background-color: %8; selection-color: white;
}
QHeaderView::section { background: %1; color: %16; border: none; border-right: %4px solid %3;
  border-bottom: %4px solid %3; padding: %15px; }
QTableCornerButton::section { background: %1; border: none; }
QTabWidget::pane { border: %4px solid %3; border-radius: %15px; top: -%4px; }
QTabBar::tab { background: %17; color: %16; border: %4px solid %3; padding: %6px %7px;
  border-top-left-radius: %15px; border-top-right-radius: %15px; margin-right: %4px; }
QTabBar::tab:selected { background: %1; color: %2; border-bottom: %18px solid %8; }
QGroupBox { border: %4px solid %3; border-radius: %5px; margin-top: %19px; padding-top: %15px; }
QGroupBox::title { subcontrol-origin: margin; left: %7px; padding: 0 %15px; color: %16; }
QMenu { background: %1; color: %2; border: %4px solid %3; }
QMenu::item:selected { background: %8; color: white; }
QMenu::item:disabled { color: %10; }
QToolTip { background: %1; color: %2; border: %4px solid %3; }
QScrollBar:vertical { background: transparent; width: %20px; margin: 0; }
QScrollBar:horizontal { background: transparent; height: %20px; margin: 0; }
QScrollBar::handle { background: %3; border-radius: %21px; min-height: %20px; min-width: %20px; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
QSplitter::handle { background: %3; }
)");
  return qss.arg(c.panel.name(), c.text.name(), c.border.name())
    .arg(px(1))
    .arg(px(6))
    .arg(px(4))
    .arg(px(8))
    .arg(c.accent.name(), (dark ? c.accent.darker(160) : c.accent.lighter(170)).name(), c.muted.name(),
         c.border.darker(dark ? 120 : 105).name(), c.window.name(), c.danger.name(), c.panelAlt.name())
    .arg(px(3))
    .arg(c.muted.name(), c.panelAlt.name())
    .arg(px(2))
    .arg(px(8))
    .arg(px(9))
    .arg(px(4.5));
}

void apply(bool dark) {
  g_dark = dark;
  QApplication::setPalette(palette(dark));
  qApp->setStyleSheet(styleSheet(dark));
}

void stylePlot(QCustomPlot *plot) {
  if (!plot) {
    return;
  }
  Colors const &c = colors();
  plot->setBackground(c.panelAlt);
  for (QCPAxisRect *rect : plot->axisRects()) {
    rect->setBackground(c.panelAlt);
    for (QCPAxis *axis : rect->axes()) {
      axis->setBasePen(QPen(c.muted, 1));
      axis->setTickPen(QPen(c.muted, 1));
      axis->setSubTickPen(QPen(c.muted, 1));
      axis->setTickLabelColor(c.muted);
      axis->setLabelColor(c.muted);
      axis->grid()->setPen(QPen(c.border, 1, Qt::DotLine));
      axis->grid()->setSubGridPen(QPen(c.border.darker(120), 1, Qt::DotLine));
      axis->grid()->setZeroLinePen(QPen(c.border.lighter(140), 1));
    }
  }
  if (plot->legend) {
    plot->legend->setBrush(QColor(c.panel.red(), c.panel.green(), c.panel.blue(), 220));
    plot->legend->setBorderPen(QPen(c.border));
    plot->legend->setTextColor(c.text);
  }
  plot->replot(QCustomPlot::rpQueuedReplot);
}

QString adaptStyleSheet(QString qss) {
  if (!g_dark) {
    return qss;
  }
  Colors const &c = colors();
  QList<std::pair<QString, QString>> const replacements{
    {"background-color: white", "background-color: " + c.panelAlt.name()},
    {"selection-background-color: white", "selection-background-color: " + c.accent.name()},
    {"selection-background-color: #d7dfef", "selection-background-color: " + c.accent.darker(150).name()},
    {"selection-color: black", "selection-color: white"},
    {"color: black", "color: " + c.text.name()},
    {"rgb(220, 220, 220)", c.border.name()},
  };
  for (auto const &[from, to] : replacements) {
    qss.replace(from, to, Qt::CaseInsensitive);
  }
  return qss;
}

// ---------------------------------------------------------------- icons
namespace {
void drawIcon(QPainter &p, QString const &kind, QColor const &color, double s) {
  QPen pen(color, s * 0.07, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
  p.setPen(pen);
  p.setBrush(Qt::NoBrush);
  auto pt = [s](double x, double y) { return QPointF(x * s, y * s); };
  if (kind == "home") {
    QPainterPath path;
    path.moveTo(pt(0.12, 0.48));
    path.lineTo(pt(0.5, 0.14));
    path.lineTo(pt(0.88, 0.48));
    p.drawPath(path);
    p.drawRoundedRect(QRectF(pt(0.24, 0.42), pt(0.76, 0.86)), s * 0.04, s * 0.04);
    p.drawLine(pt(0.44, 0.86), pt(0.44, 0.64));
    p.drawLine(pt(0.44, 0.64), pt(0.56, 0.64));
    p.drawLine(pt(0.56, 0.64), pt(0.56, 0.86));
  } else if (kind == "manual") { // sliders
    for (int i = 0; i < 3; ++i) {
      double const x = 0.25 + 0.25 * i;
      p.drawLine(pt(x, 0.14), pt(x, 0.86));
      double const k = (i == 1) ? 0.35 : (i == 0 ? 0.62 : 0.55);
      p.setBrush(color);
      p.drawEllipse(pt(x, k), s * 0.075, s * 0.075);
      p.setBrush(Qt::NoBrush);
    }
  } else if (kind == "program") { // steps profile
    QPainterPath path;
    path.moveTo(pt(0.1, 0.78));
    path.lineTo(pt(0.3, 0.78));
    path.lineTo(pt(0.45, 0.3));
    path.lineTo(pt(0.65, 0.3));
    path.lineTo(pt(0.78, 0.58));
    path.lineTo(pt(0.92, 0.58));
    p.drawPath(path);
    p.drawLine(pt(0.1, 0.9), pt(0.92, 0.9));
  } else if (kind == "chart") {
    p.drawLine(pt(0.12, 0.12), pt(0.12, 0.88));
    p.drawLine(pt(0.12, 0.88), pt(0.9, 0.88));
    QPainterPath path;
    path.moveTo(pt(0.2, 0.7));
    path.lineTo(pt(0.4, 0.45));
    path.lineTo(pt(0.58, 0.58));
    path.lineTo(pt(0.86, 0.22));
    p.drawPath(path);
  } else if (kind == "analysis") { // bars and magnifier
    p.drawLine(pt(0.18, 0.86), pt(0.18, 0.56));
    p.drawLine(pt(0.36, 0.86), pt(0.36, 0.36));
    p.drawLine(pt(0.54, 0.86), pt(0.54, 0.66));
    p.drawEllipse(pt(0.66, 0.34), s * 0.16, s * 0.16);
    p.drawLine(pt(0.78, 0.46), pt(0.92, 0.62));
  } else if (kind == "ai") { // sparkles
    auto star = [&](double cx, double cy, double r) {
      QPainterPath path;
      path.moveTo(pt(cx, cy - r));
      path.quadTo(pt(cx, cy), pt(cx + r, cy));
      path.quadTo(pt(cx, cy), pt(cx, cy + r));
      path.quadTo(pt(cx, cy), pt(cx - r, cy));
      path.quadTo(pt(cx, cy), pt(cx, cy - r));
      p.drawPath(path);
    };
    star(0.42, 0.56, 0.32);
    star(0.78, 0.22, 0.13);
  } else if (kind == "settings") { // gear
    QPointF const c = pt(0.5, 0.5);
    QPainterPath  path;
    int const     teeth{8};
    for (int i = 0; i < teeth * 2; ++i) {
      double const a  = i * M_PI / teeth;
      double const r  = (i % 2 == 0) ? 0.4 : 0.31;
      QPointF const q = c + QPointF(std::cos(a) * r * s, std::sin(a) * r * s);
      i == 0 ? path.moveTo(q) : path.lineTo(q);
    }
    path.closeSubpath();
    p.drawPath(path);
    p.drawEllipse(c, s * 0.12, s * 0.12);
  } else if (kind == "users") {
    p.drawEllipse(pt(0.38, 0.34), s * 0.13, s * 0.13);
    p.drawArc(QRectF(pt(0.14, 0.56), pt(0.62, 1.04)), 0, 180 * 16);
    p.drawEllipse(pt(0.7, 0.3), s * 0.1, s * 0.1);
    p.drawArc(QRectF(pt(0.56, 0.5), pt(0.92, 0.86)), 0, 180 * 16);
  } else if (kind == "system") { // monitor
    p.drawRoundedRect(QRectF(pt(0.1, 0.16), pt(0.9, 0.7)), s * 0.05, s * 0.05);
    p.drawLine(pt(0.5, 0.7), pt(0.5, 0.86));
    p.drawLine(pt(0.32, 0.86), pt(0.68, 0.86));
  } else if (kind == "power") {
    p.drawArc(QRectF(pt(0.16, 0.2), pt(0.84, 0.88)), 120 * 16, 300 * 16);
    p.drawLine(pt(0.5, 0.1), pt(0.5, 0.5));
  }
}
} // namespace

QIcon menuIcon(QString const &kind, QColor const &normal, QColor const &selected, int size) {
  QIcon icon;
  for (auto const &[mode, state, color] : {std::tuple{QIcon::Normal, QIcon::Off, normal}, std::tuple{QIcon::Normal, QIcon::On, selected},
                                          std::tuple{QIcon::Active, QIcon::On, selected}, std::tuple{QIcon::Disabled, QIcon::Off, normal}}) {
    QPixmap pixmap(size * 2, size * 2);
    pixmap.fill(Qt::transparent);
    pixmap.setDevicePixelRatio(2.0);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    drawIcon(p, kind, color, size);
    p.end();
    icon.addPixmap(pixmap, mode, state);
  }
  return icon;
}

} // namespace deepvaccontrol::theme
