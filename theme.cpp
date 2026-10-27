#include "theme.h"

#include <QAbstractButton>
#include <QApplication>
#include <QToolTip>
#include <QDir>
#include <QFile>
#include <QMenu>
#include <QLabel>
#include <QHash>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <cmath>
#include <QPixmap>
#include <QtMath>

#include "qcustomplot.h"
#include "utils.h"

namespace deepvaccontrol::theme {

namespace {
bool g_dark{false};

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
}

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

namespace {
// a tick for checked menu entries, drawn once per colour into a file the style sheet can point to
QString checkImage(QColor const &color) {
  QString const path = QDir::temp().filePath(QStringLiteral("deepvac_menu_check_%1.png").arg(color.name().mid(1)));
  if (!QFile::exists(path)) {
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(color, 3.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPolyline(QPolygonF{{QPointF(7, 17), QPointF(13, 23), QPointF(25, 9)}});
    p.end();
    pixmap.save(path);
  }
  return QDir::fromNativeSeparators(path);
}

// the arrow of drop-down fields
QString chevronImage(QColor const &color) {
  QString const path = QDir::temp().filePath(QStringLiteral("deepvac_chevron_%1.png").arg(color.name().mid(1)));
  if (!QFile::exists(path)) {
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(color, 3.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPolyline(QPolygonF{{QPointF(8, 12), QPointF(16, 20), QPointF(24, 12)}});
    p.end();
    pixmap.save(path);
  }
  return QDir::fromNativeSeparators(path);
}

// menus: a rounded card, roomy rows, a soft hover, thin separators, a tick for checked entries;
// statistics: tables in a rounded box with small upper-case headings
QString menuAndTableStyleSheet(Colors const &c, bool dark) {
  QColor const hover = dark ? c.panel.lighter(135) : QColor::fromRgbF(0.945f, 0.957f, 0.973f);
  QColor const line  = dark ? c.border : c.border.lighter(108);
  QColor const pick  = QColor(c.accent.red(), c.accent.green(), c.accent.blue(), dark ? 50 : 26);
  return QStringLiteral(R"(
QMenu { background: %1; color: %2; border: %3px solid %4; border-radius: %5px; padding: %6px; min-width: %20px; }
QMenu::item { padding: %7px %8px %7px %9px; border-radius: %10px; background: transparent; margin: %3px 0; }
QMenu::item:selected { background: %11; color: %2; }
QMenu::item:disabled { color: %12; }
QMenu::separator { height: %3px; background: %13; margin: %6px %6px; }
QMenu::indicator { width: %9px; height: %9px; }
QMenu::indicator:checked { image: url(%14); }
QFrame#statsCard { background: %1; border: %3px solid %4; border-radius: %5px; }
QFrame#sideBar { background: transparent; border: none; }
QFrame#sideRail { background: %1; border: none; border-left: %3px solid %4; }
QFrame#chartToolbar, QFrame#chartPad { background: %1; border: %3px solid %4; border-radius: %16px; }
QToolButton#toolHandle { background: transparent; border: none; border-radius: %10px; color: %15; padding: 0 %10px; font-weight: bold; }
QToolButton#toolHandle:hover { background: %11; }
QToolButton#toolHandle[active="true"] { background: %17; color: %21; }
QToolButton#toolButton { background: transparent; border: none; border-radius: %10px; }
QToolButton#toolButton:hover { background: %11; }
QToolButton#toolButton:checked { background: %17; }
QToolButton#toolButton:disabled { background: transparent; }
QToolButton#tabAdd { background: %1; border: %3px solid %4; border-radius: %16px; color: %15; font-size: 13pt; padding: 0; }
QToolButton#tabAdd:hover { background: %11; color: %2; }
QFrame#sidePanel { background: %1; border: none; border-left: %3px solid %4; }
QToolButton#railButton { background: transparent; border: none; border-radius: %16px; color: %15; padding: %10px 0; font-size: %19pt; }
QToolButton#railButton:hover { background: %11; }
QToolButton#railButton:checked { background: %17; color: %21; font-weight: bold; }
QToolButton#railButton:disabled { color: %12; }
QToolButton#panelClose { background: transparent; border: none; color: %15; font-size: 24pt; padding: 0 %10px; }
QToolButton#panelClose:hover { color: %2; }
QFrame#statsBox { background: %1; border: %3px solid %13; border-radius: %16px; }
QLabel#statsValue { font-family: Consolas, monospace; }
QLabel#sectionCaption { color: %15; font-weight: bold; font-size: %19pt; }
QPushButton#drawerSection, QPushButton#drawerSection:hover, QPushButton#drawerSection:pressed, QPushButton#drawerSection:checked {
  background: transparent; border: none; padding: 0; }
QPushButton[softSegment="true"] { background: %1; color: %2; border: %3px solid %4; border-radius: 0; padding: %10px %18px; }
QPushButton[softSegment="true"]:hover { border-color: %4; background: %11; }
QPushButton[softSegment="true"]:checked { background: %17; color: %21; font-weight: bold; border-color: %4; }
QPushButton[softSegment="true"][segmentFirst="true"] { border-top-left-radius: %10px; border-bottom-left-radius: %10px; }
QPushButton[softSegment="true"][segmentLast="true"] { border-top-right-radius: %10px; border-bottom-right-radius: %10px; }
QPushButton[link="true"], QPushButton[link="true"]:hover, QPushButton[link="true"]:pressed {
  background: transparent; border: none; color: %21; font-weight: bold; padding: 0; text-align: left; }
QPushButton[link="true"]:disabled { color: %12; }
QComboBox[soft="true"], QDateEdit[soft="true"], QLineEdit[soft="true"] { padding: 0 %6px; border-radius: %10px; }
QComboBox[soft="true"]::drop-down, QDateEdit[soft="true"]::drop-down { border: none; width: %22px; }
QComboBox[soft="true"]::down-arrow, QDateEdit[soft="true"]::down-arrow { image: url(%23); width: %7px; height: %7px; }
QLabel#statsTitle { color: %15; font-weight: bold; }
QTableView#statsTable, QListWidget#statsList { background: %1; border: %3px solid %13; border-radius: %16px;
  selection-background-color: %17; selection-color: %2; outline: 0; }
QTableView#statsTable::item { border: none; padding: 0 %18px; }
QTableView#statsTable::item:selected, QListWidget#statsList::item:selected { background: %17; color: %2; }
QListWidget#statsList::item { padding: %7px %18px; }
QTableView#statsTable QHeaderView { background: transparent; border: none; }
QTableView#statsTable QHeaderView::section { background: transparent; color: %15; border: none; border-bottom: %3px solid %13;
  padding: %7px %18px; font-weight: bold; font-size: %19pt; }
QTableView#statsTable QTableCornerButton::section { background: transparent; border: none; }
)")
    .arg(c.panel.name(), c.text.name())
    .arg(px(1))
    .arg(c.border.name())
    .arg(px(10))
    .arg(px(6))
    .arg(px(8))
    .arg(px(36))
    .arg(px(14))
    .arg(px(6))
    .arg(hover.name(), c.off.name(), line.name(), checkImage(c.accent), c.muted.name())
    .arg(px(8))
    .arg(pick.name(QColor::HexArgb))
    .arg(px(12))
    .arg(QApplication::font().pointSizeF() * 0.82, 0, 'f', 1) // small headings
    .arg(px(170))
    .arg(c.accent.name())
    .arg(px(24))
    .arg(chevronImage(c.muted));
}
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
QToolTip { background: %1; color: %2; border: %4px solid %3; border-radius: %15px; padding: %15px %6px; }
QScrollBar:vertical { background: transparent; width: %20px; margin: 0; }
QScrollBar:horizontal { background: transparent; height: %20px; margin: 0; }
QScrollBar::handle { background: %3; border-radius: %21px; min-height: %20px; min-width: %20px; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
QSplitter::handle { background: %3; }
QFrame#pageCard { background: %1; border: %4px solid %3; border-radius: %19px; }
QFrame#cardDivider { background: %3; border: none; }
QProgressBar#slimProgress { background: %14; border: none; border-radius: %6px; }
QProgressBar#slimProgress::chunk { background: %8; border-radius: %6px; }
QLabel#cardTitle { font-weight: bold; }
QLabel[muted="true"] { color: %16; }
QPushButton#primaryButton { background: %8; color: white; border: none; font-weight: bold; }
QPushButton#primaryButton:disabled { background: %14; color: %10; }
QPushButton#startButton { background: %22; color: #052e16; border: none; font-weight: bold; }
QPushButton#startButton:disabled { background: %14; color: %10; }
QPushButton[outline="true"] { background: transparent; color: %13; border: %4px solid %13; }
QPushButton[outline="true"]:disabled { color: %10; border-color: %3; }
QGroupBox#setpointBox { border: none; margin-top: 0; padding-top: 0; }
QLabel#dutField { background: %14; border: %4px solid %3; border-radius: %15px; padding: %15px %7px; }
QPushButton#readoutBox { background: %14; text-align: left; padding-left: %7px; }
QPushButton#readoutBox:checked { border-color: %8; }
QFrame#outputRow { background: %14; border-radius: %15px; }
QFrame#bandHeader { background: %14; border-radius: %15px; }
QFrame#bandRow { border: none; border-bottom: %4px solid %3; }
QFrame#bandRow[active="true"] { background: %24; border: %4px solid %8; border-radius: %15px; }
QLabel#countBadge { background: %14; color: %16; border: %4px solid %3; border-radius: %15px; padding: 0 %15px; }
QTableWidget#intervalTable { background: transparent; border: none; selection-background-color: %24; selection-color: %2; }
QTableWidget#intervalTable::item { border: none; border-bottom: %4px solid %3; padding-left: %15px; }
QTableWidget#intervalTable::item:selected { background: %24; color: %2; }
QTableWidget#intervalTable QHeaderView::section { background: transparent; color: %16; border: none;
  border-bottom: %4px solid %3; padding: %15px; }
QLabel[fieldLabel="true"] { color: %16; }
QTableView#flatTable { background: transparent; border: none; selection-background-color: %24; selection-color: %2; }
QTableView#flatTable::item { border: none; border-bottom: %4px solid %3; padding-left: %15px; }
QTableView#flatTable::item:selected { background: %24; color: %2; }
QTableView#flatTable QHeaderView::section { background: transparent; color: %16; border: none;
  border-bottom: %4px solid %3; padding: %15px; }
QMessageBox { background: %1; }
QMessageBox QLabel#qt_msgbox_informativelabel { color: %16; }
QMessageBox QPushButton:default { background: %8; color: white; border: none; font-weight: bold; }
QPushButton[segment="true"] { background: %14; color: %16; border-radius: 0; }
QPushButton[segment="true"]:checked { background: %8; color: white; border-color: %8; font-weight: bold; }
QPushButton[segment="true"]:disabled { color: %10; }
QPushButton[segmentFirst="true"] { border-top-left-radius: %15px; border-bottom-left-radius: %15px; }
QPushButton[segmentLast="true"] { border-top-right-radius: %15px; border-bottom-right-radius: %15px; }
QLabel#rowTitle { font-weight: bold; }
)");
  qss = qss.arg(c.panel.name(), c.text.name(), c.border.name())
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
    .arg(px(4.5))
    .arg(c.active.name(), dark ? QStringLiteral("#c084fc") : QStringLiteral("#7e22ce"),
         QColor(c.accent.red(), c.accent.green(), c.accent.blue(), dark ? 45 : 30).name(QColor::HexArgb));
  return qss + menuAndTableStyleSheet(c, dark);
}

namespace {
// standard message boxes (QMessageBox::warning() etc.) get the round icon of the app's own message windows
class MessageBoxStyler : public QObject {
public:
  using QObject::QObject;
  bool eventFilter(QObject *watched, QEvent *event) override {
    // menus: no square window behind their rounded corners (before the window is created)
    if (event->type() == QEvent::Polish) {
      if (auto *menu = qobject_cast<QMenu *>(watched)) {
        menu->setAttribute(Qt::WA_TranslucentBackground);
        menu->setWindowFlag(Qt::FramelessWindowHint);
        menu->setWindowFlag(Qt::NoDropShadowWindowHint);
      }
    }
    // before QMessageBox::showEvent() fixes the window size
    if (event->type() == QEvent::Show) {
      if (auto *box = qobject_cast<QMessageBox *>(watched)) {
        if (auto *label = box->findChild<QLabel *>(QStringLiteral("qt_msgbox_label"))) {
          label->setMinimumWidth(px(260));
        }
        for (QAbstractButton *button : box->buttons()) {
          button->setMinimumSize(px(60), px(20));
        }
        static QHash<int, QString> const kinds{{QMessageBox::Warning, "warning"},
                                               {QMessageBox::Critical, "error"},
                                               {QMessageBox::Question, "question"},
                                               {QMessageBox::Information, "info"}};
        if (QString const kind = kinds.value(box->icon()); !kind.isEmpty()) {
          box->setIconPixmap(dialogIcon(kind, static_cast<int>(34 * utils::ssf())));
        }
      }
    }
    return false;
  }
};
}

void apply(bool dark) {
  static MessageBoxStyler *styler = nullptr;
  if (!styler) {
    styler = new MessageBoxStyler(qApp);
    qApp->installEventFilter(styler);
  }
  g_dark = dark;
  QApplication::setPalette(palette(dark));
  qApp->setStyleSheet(styleSheet(dark));
  // tooltips of widgets with style sheets of their own fall back to the native look: the palette and font of the
  // application for them too
  {
    Colors const &c       = colors();
    QPalette      tipPalette = QToolTip::palette();
    tipPalette.setColor(QPalette::ToolTipBase, c.panel);
    tipPalette.setColor(QPalette::ToolTipText, c.text);
    tipPalette.setColor(QPalette::Window, c.panel);
    tipPalette.setColor(QPalette::WindowText, c.text);
    QToolTip::setPalette(tipPalette);
    QToolTip::setFont(QApplication::font());
  }
  for (QWidget *widget : QApplication::allWidgets()) {
    if (auto *plot = qobject_cast<QCustomPlot *>(widget)) {
      stylePlot(plot);
    }
    if (widget->property("flatTabs").toBool()) {
      widget->setStyleSheet(flatTabStyleSheet());
    }
    if (QString const kind = widget->property("themeIcon").toString(); !kind.isEmpty()) {
      setThemedIcon(widget, kind, widget->property("themeIconSize").toInt(), widget->property("themeIconRole").toString());
    }
  }
}

void setThemedIcon(QWidget *target, QString const &kind, int size, QString const &role) {
  if (!target) {
    return;
  }
  Colors const &c     = colors();
  QColor const  color = (role == QLatin1String("muted"))    ? c.muted
                        : (role == QLatin1String("danger")) ? c.danger
                        : (role == QLatin1String("accent")) ? c.accent
                                                            : c.text;
  QIcon const   icon  = menuIcon(kind, color, color, size);
  target->setProperty("themeIcon", kind);
  target->setProperty("themeIconSize", size);
  target->setProperty("themeIconRole", role);
  if (auto *button = qobject_cast<QAbstractButton *>(target)) {
    button->setIcon(icon);
    button->setIconSize(QSize(size, size));
  } else if (auto *label = qobject_cast<QLabel *>(target)) {
    label->setPixmap(icon.pixmap(size));
  }
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

QPixmap dialogIcon(QString const &kind, int size) {
  Colors const &c     = colors();
  QColor const  color = (kind == QLatin1String("error"))     ? c.danger
                        : (kind == QLatin1String("warning")) ? QColor(245, 158, 11)
                                                             : c.accent;
  qreal const   dpr   = qApp->devicePixelRatio();
  QPixmap       pixmap(QSize(size, size) * dpr);
  pixmap.setDevicePixelRatio(dpr);
  pixmap.fill(Qt::transparent);
  QPainter p(&pixmap);
  p.setRenderHint(QPainter::Antialiasing);
  p.setPen(Qt::NoPen);
  p.setBrush(QColor(color.red(), color.green(), color.blue(), 45));
  p.drawEllipse(QRectF(0, 0, size, size));
  p.setBrush(color);
  double const inner = size * 0.68;
  p.drawEllipse(QRectF((size - inner) / 2, (size - inner) / 2, inner, inner));
  QFont font;
  font.setBold(true);
  font.setPixelSize(static_cast<int>(size * 0.46));
  p.setFont(font);
  p.setPen(Qt::white);
  QString const glyph = (kind == QLatin1String("question")) ? QStringLiteral("?")
                        : (kind == QLatin1String("info"))   ? QStringLiteral("i")
                                                            : QStringLiteral("!");
  p.drawText(QRectF(0, 0, size, size), Qt::AlignCenter, glyph);
  return pixmap;
}

void setFlatTabs(QWidget *tabWidget) {
  if (tabWidget) {
    tabWidget->setProperty("flatTabs", true);
    tabWidget->setStyleSheet(flatTabStyleSheet());
  }
}

QString flatTabStyleSheet() {
  Colors const &c = colors();
  return QStringLiteral(R"(
QTabWidget::pane { border: none; border-top: %1px solid %2; top: -%1px; }
QTabBar::tab { background: transparent; color: %3; border: none; border-bottom: %4px solid transparent;
  padding: %5px %6px; margin-right: %7px; }
QTabBar::tab:hover { color: %8; }
QTabBar::tab:selected { color: %8; font-weight: bold; border-bottom: %4px solid %9; }
)")
    .arg(px(1))
    .arg(c.border.name(), c.muted.name())
    .arg(px(2))
    .arg(px(6))
    .arg(px(12))
    .arg(px(4))
    .arg(c.text.name(), c.accent.name());
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
  } else if (kind == "plus") {
    p.drawLine(pt(0.5, 0.2), pt(0.5, 0.8));
    p.drawLine(pt(0.2, 0.5), pt(0.8, 0.5));
  } else if (kind == "minus") {
    p.drawLine(pt(0.2, 0.5), pt(0.8, 0.5));
  } else if (kind == "up" || kind == "down" || kind == "left" || kind == "right") {
    QPainterPath path;
    if (kind == "up") {
      path.moveTo(pt(0.25, 0.62)); path.lineTo(pt(0.5, 0.36)); path.lineTo(pt(0.75, 0.62));
    } else if (kind == "down") {
      path.moveTo(pt(0.25, 0.38)); path.lineTo(pt(0.5, 0.64)); path.lineTo(pt(0.75, 0.38));
    } else if (kind == "left") {
      path.moveTo(pt(0.62, 0.25)); path.lineTo(pt(0.36, 0.5)); path.lineTo(pt(0.62, 0.75));
    } else {
      path.moveTo(pt(0.38, 0.25)); path.lineTo(pt(0.64, 0.5)); path.lineTo(pt(0.38, 0.75));
    }
    p.drawPath(path);
  } else if (kind == "globe") {
    p.drawEllipse(pt(0.5, 0.5), s * 0.36, s * 0.36);
    p.drawEllipse(pt(0.5, 0.5), s * 0.15, s * 0.36);
    p.drawLine(pt(0.14, 0.5), pt(0.86, 0.5));
    p.drawLine(pt(0.22, 0.3), pt(0.78, 0.3));
    p.drawLine(pt(0.22, 0.7), pt(0.78, 0.7));
  } else if (kind == "calendar") {
    p.drawRoundedRect(QRectF(pt(0.14, 0.2), pt(0.86, 0.86)), s * 0.06, s * 0.06);
    p.drawLine(pt(0.14, 0.4), pt(0.86, 0.4));
    p.drawLine(pt(0.34, 0.1), pt(0.34, 0.28));
    p.drawLine(pt(0.66, 0.1), pt(0.66, 0.28));
  } else if (kind == "folder") { // experiments list
    QPainterPath path;
    path.moveTo(pt(0.12, 0.8));
    path.lineTo(pt(0.12, 0.22));
    path.lineTo(pt(0.4, 0.22));
    path.lineTo(pt(0.48, 0.32));
    path.lineTo(pt(0.88, 0.32));
    path.lineTo(pt(0.88, 0.8));
    path.closeSubpath();
    p.drawPath(path);
    p.drawLine(pt(0.12, 0.44), pt(0.88, 0.44));
  } else if (kind == "edit") { // pencil
    QPainterPath path;
    path.moveTo(pt(0.2, 0.8));
    path.lineTo(pt(0.24, 0.62));
    path.lineTo(pt(0.66, 0.2));
    path.lineTo(pt(0.8, 0.34));
    path.lineTo(pt(0.38, 0.76));
    path.closeSubpath();
    p.drawPath(path);
    p.drawLine(pt(0.56, 0.3), pt(0.7, 0.44));
  } else if (kind == "delete") { // bin
    p.drawLine(pt(0.18, 0.28), pt(0.82, 0.28));
    p.drawLine(pt(0.4, 0.28), pt(0.42, 0.16));
    p.drawLine(pt(0.42, 0.16), pt(0.58, 0.16));
    p.drawLine(pt(0.58, 0.16), pt(0.6, 0.28));
    QPainterPath path;
    path.moveTo(pt(0.26, 0.28));
    path.lineTo(pt(0.31, 0.86));
    path.lineTo(pt(0.69, 0.86));
    path.lineTo(pt(0.74, 0.28));
    p.drawPath(path);
    p.drawLine(pt(0.44, 0.42), pt(0.44, 0.72));
    p.drawLine(pt(0.56, 0.42), pt(0.56, 0.72));
  } else if (kind == "save" || kind == "load") { // tray with an arrow into it / out of it
    QPainterPath tray;
    tray.moveTo(pt(0.16, 0.62));
    tray.lineTo(pt(0.16, 0.84));
    tray.lineTo(pt(0.84, 0.84));
    tray.lineTo(pt(0.84, 0.62));
    p.drawPath(tray);
    p.drawLine(pt(0.5, 0.14), pt(0.5, 0.64));
    QPainterPath head;
    if (kind == "save") {
      head.moveTo(pt(0.32, 0.46)); head.lineTo(pt(0.5, 0.64)); head.lineTo(pt(0.68, 0.46));
    } else {
      head.moveTo(pt(0.32, 0.32)); head.lineTo(pt(0.5, 0.14)); head.lineTo(pt(0.68, 0.32));
    }
    p.drawPath(head);
  } else if (kind == "compare") { // two overlaid curves
    QPainterPath a;
    a.moveTo(pt(0.1, 0.72));
    a.cubicTo(pt(0.35, 0.72), pt(0.4, 0.22), pt(0.9, 0.22));
    p.drawPath(a);
    QPen dashed = pen;
    dashed.setStyle(Qt::DashLine);
    p.setPen(dashed);
    QPainterPath b;
    b.moveTo(pt(0.1, 0.86));
    b.cubicTo(pt(0.4, 0.86), pt(0.5, 0.4), pt(0.9, 0.4));
    p.drawPath(b);
    p.setPen(pen);
  } else if (kind == "channels") { // three lines with a dot each
    for (int i = 0; i < 3; ++i) {
      double const y = 0.26 + 0.24 * i;
      p.drawLine(pt(0.36, y), pt(0.88, y));
      p.setBrush(color);
      p.drawEllipse(pt(0.18, y), s * 0.06, s * 0.06);
      p.setBrush(Qt::NoBrush);
    }
  } else if (kind == "annotate") { // speech bubble
    QPainterPath path;
    path.addRoundedRect(QRectF(pt(0.12, 0.16), pt(0.88, 0.68)), s * 0.12, s * 0.12);
    p.drawPath(path);
    QPainterPath tail;
    tail.moveTo(pt(0.32, 0.68));
    tail.lineTo(pt(0.26, 0.88));
    tail.lineTo(pt(0.48, 0.68));
    p.drawPath(tail);
  } else if (kind == "fit") { // four corners
    for (auto const &[x, y, dx, dy] : {std::tuple{0.14, 0.14, 1, 1}, std::tuple{0.86, 0.14, -1, 1}, std::tuple{0.14, 0.86, 1, -1},
                                       std::tuple{0.86, 0.86, -1, -1}}) {
      p.drawLine(pt(x, y), pt(x + dx * 0.24, y));
      p.drawLine(pt(x, y), pt(x, y + dy * 0.24));
    }
  } else if (kind == "stats") { // bars
    p.drawLine(pt(0.12, 0.88), pt(0.88, 0.88));
    p.drawRect(QRectF(pt(0.2, 0.5), pt(0.36, 0.88)));
    p.drawRect(QRectF(pt(0.44, 0.22), pt(0.6, 0.88)));
    p.drawRect(QRectF(pt(0.68, 0.38), pt(0.84, 0.88)));
  } else if (kind == "info") { // i in a circle
    p.drawEllipse(pt(0.5, 0.5), s * 0.38, s * 0.38);
    p.drawLine(pt(0.5, 0.46), pt(0.5, 0.72));
    p.setBrush(color);
    p.drawEllipse(pt(0.5, 0.31), s * 0.045, s * 0.045);
    p.setBrush(Qt::NoBrush);
  } else if (kind == "layers") { // what the chart shows: stacked sheets
    QPolygonF const sheet{{pt(0.5, 0.14), pt(0.88, 0.34), pt(0.5, 0.54), pt(0.12, 0.34)}};
    p.drawPolygon(sheet);
    QPainterPath lower;
    lower.moveTo(pt(0.12, 0.52));
    lower.lineTo(pt(0.5, 0.72));
    lower.lineTo(pt(0.88, 0.52));
    p.drawPath(lower);
    QPainterPath lowest;
    lowest.moveTo(pt(0.12, 0.68));
    lowest.lineTo(pt(0.5, 0.88));
    lowest.lineTo(pt(0.88, 0.68));
    p.drawPath(lowest);
  } else if (kind == "export") { // arrow down into a tray
    p.drawLine(pt(0.5, 0.12), pt(0.5, 0.62));
    p.drawLine(pt(0.3, 0.44), pt(0.5, 0.64));
    p.drawLine(pt(0.7, 0.44), pt(0.5, 0.64));
    QPainterPath tray;
    tray.moveTo(pt(0.14, 0.62));
    tray.lineTo(pt(0.14, 0.86));
    tray.lineTo(pt(0.86, 0.86));
    tray.lineTo(pt(0.86, 0.62));
    p.drawPath(tray);
  } else if (kind == "pointer") { // mouse arrow: inspect
    QPolygonF const arrow{{pt(0.28, 0.14), pt(0.28, 0.78), pt(0.44, 0.63), pt(0.56, 0.88), pt(0.66, 0.84), pt(0.54, 0.59), pt(0.76, 0.58)}};
    p.drawPolygon(arrow);
  } else if (kind == "boxzoom") { // a dashed box
    QPen dashed = p.pen();
    dashed.setStyle(Qt::DashLine);
    dashed.setDashPattern({2.2, 1.6});
    p.setPen(dashed);
    p.drawRect(QRectF(pt(0.16, 0.2), pt(0.72, 0.66)));
    p.setPen(pen);
    p.drawLine(pt(0.58, 0.54), pt(0.86, 0.84));
  } else if (kind == "zoom") { // magnifier
    p.drawEllipse(pt(0.42, 0.42), s * 0.26, s * 0.26);
    p.drawLine(pt(0.61, 0.61), pt(0.86, 0.86));
    p.drawLine(pt(0.31, 0.42), pt(0.53, 0.42));
    p.drawLine(pt(0.42, 0.31), pt(0.42, 0.53));
  } else if (kind == "pan") { // four arrows
    p.drawLine(pt(0.5, 0.1), pt(0.5, 0.9));
    p.drawLine(pt(0.1, 0.5), pt(0.9, 0.5));
    for (auto const &[tip, a, b] : {std::tuple{pt(0.5, 0.1), pt(0.38, 0.22), pt(0.62, 0.22)}, std::tuple{pt(0.5, 0.9), pt(0.38, 0.78), pt(0.62, 0.78)},
                                    std::tuple{pt(0.1, 0.5), pt(0.22, 0.38), pt(0.22, 0.62)}, std::tuple{pt(0.9, 0.5), pt(0.78, 0.38), pt(0.78, 0.62)}}) {
      p.drawLine(tip, a);
      p.drawLine(tip, b);
    }
  } else if (kind == "labels") { // the labels box: lines with their names
    p.drawRoundedRect(QRectF(pt(0.12, 0.2), pt(0.88, 0.8)), s * 0.06, s * 0.06);
    p.drawLine(pt(0.24, 0.4), pt(0.4, 0.4));
    p.drawLine(pt(0.5, 0.4), pt(0.76, 0.4));
    p.drawLine(pt(0.24, 0.6), pt(0.4, 0.6));
    p.drawLine(pt(0.5, 0.6), pt(0.7, 0.6));
  } else if (kind == "tooltip") { // a crosshair with a value box
    p.drawLine(pt(0.3, 0.12), pt(0.3, 0.88));
    p.drawLine(pt(0.12, 0.62), pt(0.88, 0.62));
    p.drawRoundedRect(QRectF(pt(0.44, 0.16), pt(0.88, 0.44)), s * 0.05, s * 0.05);
  } else if (kind == "pin") {
    p.drawLine(pt(0.5, 0.62), pt(0.5, 0.92));
    p.drawLine(pt(0.24, 0.62), pt(0.76, 0.62));
    QPainterPath body;
    body.moveTo(pt(0.34, 0.62));
    body.lineTo(pt(0.38, 0.32));
    body.lineTo(pt(0.32, 0.12));
    body.lineTo(pt(0.68, 0.12));
    body.lineTo(pt(0.62, 0.32));
    body.lineTo(pt(0.66, 0.62));
    p.drawPath(body);
  } else if (kind == "collapse") { // chevron up
    p.drawLine(pt(0.26, 0.62), pt(0.5, 0.38));
    p.drawLine(pt(0.5, 0.38), pt(0.74, 0.62));
  } else if (kind == "dots") { // vertical ellipsis
    p.setBrush(color);
    p.setPen(Qt::NoPen);
    for (double const y : {0.22, 0.5, 0.78}) {
      p.drawEllipse(pt(0.5, y), s * 0.08, s * 0.08);
    }
  } else if (kind == "sequence") { // pulse
    QPainterPath path;
    path.moveTo(pt(0.08, 0.52));
    path.lineTo(pt(0.3, 0.52));
    path.lineTo(pt(0.42, 0.2));
    path.lineTo(pt(0.58, 0.82));
    path.lineTo(pt(0.7, 0.52));
    path.lineTo(pt(0.92, 0.52));
    p.drawPath(path);
  } else if (kind == "repeat") { // two arrows round a loop
    QPainterPath upper;
    upper.moveTo(pt(0.16, 0.58));
    upper.lineTo(pt(0.16, 0.32));
    upper.lineTo(pt(0.8, 0.32));
    p.drawPath(upper);
    p.drawLine(pt(0.68, 0.2), pt(0.8, 0.32));
    p.drawLine(pt(0.68, 0.44), pt(0.8, 0.32));
    QPainterPath lower;
    lower.moveTo(pt(0.84, 0.42));
    lower.lineTo(pt(0.84, 0.68));
    lower.lineTo(pt(0.2, 0.68));
    p.drawPath(lower);
    p.drawLine(pt(0.32, 0.56), pt(0.2, 0.68));
    p.drawLine(pt(0.32, 0.8), pt(0.2, 0.68));
  } else if (kind == "clipboard") { // experiment details: a sheet on a clipboard
    p.drawRoundedRect(QRectF(pt(0.22, 0.18), pt(0.78, 0.9)), s * 0.06, s * 0.06);
    p.drawRect(QRectF(pt(0.38, 0.1), pt(0.62, 0.26)));
    for (double const y : {0.44, 0.6, 0.76}) {
      p.drawLine(pt(0.34, y), pt(0.66, y));
    }
  } else if (kind == "power") {
    p.drawArc(QRectF(pt(0.16, 0.2), pt(0.84, 0.88)), 120 * 16, 300 * 16);
    p.drawLine(pt(0.5, 0.1), pt(0.5, 0.5));
    // ---- chamber components (the diagram's indicators)
  } else if (kind == "bulb") {
    QPainterPath glass;
    glass.moveTo(pt(0.38, 0.66));
    glass.arcTo(QRectF(pt(0.22, 0.1), pt(0.78, 0.66)), 230, -280);
    glass.lineTo(pt(0.62, 0.66));
    glass.closeSubpath();
    p.drawPath(glass);
    p.drawLine(pt(0.4, 0.76), pt(0.6, 0.76));
    p.drawLine(pt(0.43, 0.86), pt(0.57, 0.86));
  } else if (kind == "thermometer") {
    p.drawRoundedRect(QRectF(pt(0.42, 0.1), pt(0.58, 0.62)), 0.08 * s, 0.08 * s);
    p.setBrush(color);
    p.drawEllipse(QRectF(pt(0.34, 0.6), pt(0.66, 0.92)));
    p.drawLine(pt(0.5, 0.35), pt(0.5, 0.65));
  } else if (kind == "snowflake") {
    for (int k{0}; k < 3; ++k) {
      double const a{k * 3.14159265 / 3.0};
      p.drawLine(pt(0.5 + 0.38 * std::cos(a), 0.5 + 0.38 * std::sin(a)), pt(0.5 - 0.38 * std::cos(a), 0.5 - 0.38 * std::sin(a)));
    }
  } else if (kind == "heater") { // three heat waves
    for (double const x : {0.3, 0.5, 0.7}) {
      QPainterPath wave;
      wave.moveTo(pt(x, 0.12));
      wave.cubicTo(pt(x - 0.1, 0.3), pt(x + 0.1, 0.42), pt(x, 0.6));
      wave.cubicTo(pt(x - 0.1, 0.74), pt(x + 0.06, 0.82), pt(x, 0.9));
      p.drawPath(wave);
    }
  } else if (kind == "fridge") { // a unit with a grille
    p.drawRoundedRect(QRectF(pt(0.14, 0.2), pt(0.86, 0.8)), 0.08 * s, 0.08 * s);
    p.drawLine(pt(0.14, 0.44), pt(0.86, 0.44));
    p.drawLine(pt(0.3, 0.6), pt(0.7, 0.6));
  } else if (kind == "fan") { // turbo pump: a rotor
    p.drawEllipse(QRectF(pt(0.12, 0.12), pt(0.88, 0.88)));
    for (int k{0}; k < 4; ++k) {
      double const a{k * 3.14159265 / 2.0 + 0.4};
      p.drawLine(pt(0.5, 0.5), pt(0.5 + 0.3 * std::cos(a), 0.5 + 0.3 * std::sin(a)));
    }
  } else if (kind == "spiral") { // scroll pump
    QPainterPath spiral;
    spiral.moveTo(pt(0.5, 0.5));
    for (int k{1}; k <= 40; ++k) {
      double const a{k * 0.45};
      double const r{0.012 * k};
      spiral.lineTo(pt(0.5 + r * std::cos(a), 0.5 + r * std::sin(a)));
    }
    p.drawPath(spiral);
  } else if (kind == "valve") { // two triangles meeting
    QPolygonF const bowtie{pt(0.14, 0.3), pt(0.86, 0.7), pt(0.86, 0.3), pt(0.14, 0.7)};
    p.drawPolygon(bowtie);
  } else if (kind == "gauge") { // a dial with its needle
    p.drawArc(QRectF(pt(0.12, 0.18), pt(0.88, 0.94)), 0, 180 * 16);
    p.drawLine(pt(0.12, 0.56), pt(0.88, 0.56));
    p.drawLine(pt(0.5, 0.56), pt(0.68, 0.32));
  } else if (kind == "chip") { // the PLC
    p.drawRoundedRect(QRectF(pt(0.26, 0.26), pt(0.74, 0.74)), 0.06 * s, 0.06 * s);
    for (double const t : {0.38, 0.5, 0.62}) {
      p.drawLine(pt(t, 0.1), pt(t, 0.26));
      p.drawLine(pt(t, 0.74), pt(t, 0.9));
      p.drawLine(pt(0.1, t), pt(0.26, t));
      p.drawLine(pt(0.74, t), pt(0.9, t));
    }
  }
}
}

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

}
