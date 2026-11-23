#include "theme.h"

#include <QAbstractButton>
#include <QApplication>
#include <QToolTip>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QRegularExpression>
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

// the colours come from the Deepvac HMI colour guide (theme::guide)

// the dark theme, as the light one: Slate pages and inner zones (rows, fields, the plot area) in Slate Raised cards,
// Slate Line borders, Pearl text, Sky for the accent and cooling, Heat for heating
Colors const NAVY{guide::slate, guide::slate, guide::slateRaised, guide::slate,       guide::slateLine, guide::pearl,
                  guide::haze,  guide::sky,   guide::live,        guide::sky,         guide::steel,     guide::alarm,
                  guide::heat,  guide::sky,   guide::connecting,  guide::slateLine};

// the light theme: Plot pages and inner value zones, White cards, Border outlines, Slate text, Steel labels, Cobalt for
// icons, buttons and what is selected (and cooling), Heat for heating, Fog for the tab strips
Colors const LIGHT{guide::plot,  guide::white,  guide::white, guide::plot, guide::border, guide::slate,
                   guide::steel, guide::cobalt, guide::live,  guide::idle, guide::noConnection, guide::alarm,
                   guide::heat,  guide::cobalt, guide::connecting, guide::fog};

// the navigation sidebar, in both themes: Slate, Slate Raised boxes (the status box, hovered buttons), Slate Line
// dividers, Sky icons, Pearl labels
Colors const SIDEBAR{guide::slate, guide::slate, guide::slateRaised, guide::slateRaised, guide::slateLine, guide::pearl,
                     guide::haze,  guide::sky,   guide::live,        guide::sky,         guide::steel,     guide::alarm,
                     guide::heat,  guide::sky,   guide::connecting,  guide::slateLine};

}

int dp(double designPx) {
  return qMax(1, qRound(designPx * utils::ssf() * 1024.0 / 1920.0));
}

int gap() {
  return dp(16);
}

int radius() {
  return dp(8);
}

bool isDark() {
  return g_dark;
}

Colors const &colors() {
  return g_dark ? NAVY : LIGHT;
}

Colors const &sidebarColors() {
  return SIDEBAR;
}

QString monoFamily() {
  static QString const family = [] {
    int const         id{QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/JetBrainsMono-Medium.ttf"))};
    QStringList const families = (id >= 0) ? QFontDatabase::applicationFontFamilies(id) : QStringList{};
    return families.isEmpty() ? QStringLiteral("Consolas") : families.first();
  }();
  return family;
}

QColor selectedBackground() {
  return g_dark ? QColor(NAVY.accent.red(), NAVY.accent.green(), NAVY.accent.blue(), 70) : guide::cobaltTint;
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
  p.setColor(QPalette::BrightText, guide::white);
  p.setColor(QPalette::Light, dark ? guide::steel : guide::white);
  p.setColor(QPalette::Midlight, c.border);
  p.setColor(QPalette::Mid, c.border);
  p.setColor(QPalette::Dark, dark ? guide::slate : guide::fog);
  p.setColor(QPalette::Shadow, guide::slate);
  p.setColor(QPalette::Link, c.accent);
  p.setColor(QPalette::Highlight, c.accent);
  p.setColor(QPalette::HighlightedText, guide::white);
  for (auto role : {QPalette::Text, QPalette::ButtonText, QPalette::WindowText}) {
    p.setColor(QPalette::Disabled, role, dark ? guide::steel : guide::haze);
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

// the × of closable tabs; large: filling most of its square (the Analysis tabs)
QString closeImage(QColor const &color, bool large = false) {
  QString const path = QDir::temp().filePath(QStringLiteral("deepvac_close%2_%1.png").arg(color.name().mid(1), large ? QStringLiteral("L") : QString{}));
  if (!QFile::exists(path)) {
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    double const from{large ? 7.0 : 10.0};
    double const to{32.0 - from};
    p.setPen(QPen(color, large ? 3.0 : 3.4, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(from, from), QPointF(to, to));
    p.drawLine(QPointF(to, from), QPointF(from, to));
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
  QColor const hover = dark ? guide::slateLine : guide::plot;
  QColor const line  = c.border;
  QColor const pick  = dark ? QColor(c.accent.red(), c.accent.green(), c.accent.blue(), 50) : selectedBackground();
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
QFrame#chartToolbar, QFrame#chartPad { background: %1; border: %3px solid %4; border-radius: %16px; }
QToolButton#toolHandle { background: transparent; border: none; border-radius: %10px; color: %15; padding: 0 %10px; font-weight: bold; }
QToolButton#toolHandle:hover { background: %11; }
QToolButton#toolHandle[active="true"] { background: %17; color: %2; }
QToolButton#toolButton { background: transparent; border: none; border-radius: %10px; }
QToolButton#toolButton:hover { background: %11; }
QToolButton#toolButton:checked { background: %17; }
QToolButton#toolButton:disabled { background: transparent; }
QFrame#sidePanel { background: %1; border: %3px solid %4; border-radius: %16px; }
QToolButton#railButton { background: transparent; border: none; border-radius: %16px; color: %15; padding: %10px 0; font-size: %26pt; }
QToolButton#railButton:hover { background: %11; }
QToolButton#railButton:checked { background: %17; color: %2; font-weight: bold; }
QToolButton#railButton:disabled { color: %12; }
QToolButton#panelClose { background: %11; border: none; border-radius: %24px; color: %2; font-size: %25pt; padding: 0 0 %3px 0; }
QToolButton#panelClose:hover, QToolButton#panelClose:pressed { background: %4; }
QFrame#statsBox { background: %1; border: %3px solid %13; border-radius: %16px; }
QLabel#statsValue { font-family: Consolas, monospace; }
QLabel#sectionCaption { color: %15; font-weight: bold; font-size: %19pt; }
QPushButton#drawerSection, QPushButton#drawerSection:hover, QPushButton#drawerSection:pressed, QPushButton#drawerSection:checked {
  background: transparent; border: none; padding: 0; }
QPushButton[softSegment="true"] { background: %1; color: %2; border: %3px solid %4; border-radius: 0; padding: %10px %18px; }
QPushButton[softSegment="true"]:hover { border-color: %4; background: %11; }
QPushButton[softSegment="true"]:checked { background: %17; color: %2; font-weight: bold; border-color: %4; }
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
QTableView#statsTable::item { border: none; padding: 0 %27px; }
QTableView#statsTable::item:selected, QListWidget#statsList::item:selected { background: %17; color: %2; }
QListWidget#statsList::item { padding: %7px %18px; }
QTableView#statsTable QHeaderView { background: transparent; border: none; }
QTableView#statsTable QHeaderView::section { background: transparent; color: %15; border: none; border-bottom: %3px solid %13;
  padding: %7px %27px; font-weight: bold; font-size: %19pt; }
QTableView#statsTable QTableCornerButton::section { background: transparent; border: none; }
)")
    .arg(c.panel.name(), c.text.name())
    .arg(dp(1))
    .arg(c.border.name())
    .arg(dp(18))
    .arg(dp(11))
    .arg(dp(15))
    .arg(dp(67))
    .arg(dp(26))
    .arg(dp(11))
    .arg(hover.name(), c.off.name(), line.name(), checkImage(c.accent), c.muted.name())
    .arg(dp(15))
    .arg(pick.name(QColor::HexArgb))
    .arg(dp(22))
    .arg(QApplication::font().pointSizeF() * 0.82, 0, 'f', 1) // small headings
    .arg(dp(318))
    .arg(c.accent.name())
    .arg(dp(45))
    .arg(chevronImage(c.muted))
    .arg(dp(18))                                                 // rounded square × of the side panels
    .arg(QApplication::font().pointSizeF() * 1.35, 0, 'f', 1)
    .arg(QApplication::font().pointSizeF() * 0.62, 0, 'f', 1) // the rail buttons' names, as the live status' captions
    .arg(dp(12)); // either side of a statistics table's cell: twelve columns of text at the page's size fit
}
}

// Analysis: the panes' tabs in one rounded strip (plain labels between thin lines, the selected one a white box in
// bold), the + a flat icon in it; Expand / Collapse a named button over the chart's top right; the chart and the
// live status (with the options) in cards. The channel rows of the side
// panel as cards with a dark tick, white boxes and roomy Show all / Hide all
QString analysisStyleSheet(Colors const &c, bool dark) {
  QColor const hover  = dark ? QColor(255, 255, 255, 20) : QColor(guide::slate.red(), guide::slate.green(), guide::slate.blue(), 12);
  QColor const strong = dark ? guide::steel : guide::haze;
  QString const tabs = QStringLiteral(R"(
QFrame#paneHeader { background: %3; border: %4px solid %5; border-radius: %6px; }  /* Fog */
QTabBar#paneTabs::tab { background: transparent; color: %2; border: none; border-right: %4px solid %5; border-radius: 0;
  height: %7px; padding: 0 %8px; margin: 0; min-width: %9px; }
QTabBar#paneTabs::tab:hover:!selected { background: %10; }
QTabBar#paneTabs::tab:selected { background: %1; color: %2; font-weight: bold; border: %4px solid %5; border-radius: %11px; }
QTabBar#paneTabs::close-button { image: url(%12); background: transparent; border-radius: %11px; width: %13px; height: %13px;
  margin-left: %14px; subcontrol-position: right; }
QTabBar#paneTabs::close-button:hover { background: %10; }
QToolButton#tabClose { background: transparent; border: none; border-radius: %11px; padding: 0; }
QToolButton#tabClose:hover, QToolButton#tabClose:pressed { background: %10; }
QToolButton#tabAdd, QToolButton#tabMore { background: transparent; border: none; border-radius: %11px; padding: 0; }
QToolButton#liveToggle { background: transparent; border: none; border-radius: %11px; padding: 0; }
QToolButton#tabAdd:hover, QToolButton#tabAdd:pressed, QToolButton#tabMore:hover, QToolButton#tabMore:pressed, QToolButton#liveToggle:hover, QToolButton#liveToggle:pressed { background: %10; }
QFrame#chartCard, QFrame#sideColumn { background: %1; border: %4px solid %5; border-radius: %6px; }
)")
    .arg(c.panel.name(), c.text.name(), c.rule.name())
    .arg(dp(1))
    .arg(c.border.name())
    .arg(dp(8))
    .arg(dp(42))
    .arg(dp(20))
    .arg(dp(48))
    .arg(hover.name(QColor::HexArgb))
    .arg(dp(6))
    .arg(closeImage(c.muted))
    .arg(dp(24))
    .arg(dp(8));
  QString const channels = QStringLiteral(R"(
QFrame#channelRow { background: %1; border: none; border-radius: %2px; }
QFrame#channelRow QCheckBox::indicator, QCheckBox[boxed="true"]::indicator { width: %3px; height: %3px; border: %4px solid %5; border-radius: %6px; background: %7; }
QFrame#channelRow QCheckBox::indicator:checked, QCheckBox[boxed="true"]::indicator:checked { background: %8; border-color: %8; image: url(%9); }
QCheckBox[boxed="true"] { spacing: %15px; font-weight: bold; }
QFrame#channelRow QComboBox { background: %7; border: %10px solid %11; border-radius: %6px; padding: 0 0 0 %6px; }
QFrame#channelRow QComboBox:disabled { background: %1; }
QFrame#channelRow QComboBox::drop-down { border: none; width: %12px; }
QFrame#channelRow QComboBox::down-arrow { image: url(%13); width: %14px; height: %14px; }
QPushButton[panelFooter="true"] { border-radius: %2px; font-weight: bold; }
QPushButton[panelFooter="true"]:!pressed { border-color: %5; }
QPushButton#darkButton[panelFooter="true"] { border: none; }
)")
    .arg(c.panelAlt.name())
    .arg(dp(22))
    .arg(dp(37))
    .arg(dp(3))
    .arg(strong.name())
    .arg(dp(11))
    .arg(c.panel.name(), c.text.name(), checkImage(c.panel))
    .arg(dp(2))
    .arg(c.border.name())
    .arg(dp(37))
    .arg(chevronImage(c.muted))
    .arg(dp(18))
    .arg(dp(14)); // a boxed check box: its tick to its text
  return tabs + channels;
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
QPushButton#primaryButton:hover { background: %25; }
QPushButton#primaryButton:pressed { background: %26; }
QPushButton#primaryButton:disabled { background: %14; color: %10; }
QPushButton#darkButton { background: %2; color: %1; border: none; font-weight: bold; }
QPushButton#darkButton:pressed { background: %10; }
QPushButton#darkButton:disabled { background: %14; color: %10; }
QPushButton#startButton { background: %22; color: %23; border: none; font-weight: bold; }
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
    .arg(dp(1))
    .arg(dp(11))
    .arg(dp(7))
    .arg(dp(15))
    .arg(c.accent.name(), (dark ? guide::cobaltPressed : selectedBackground()).name(), c.muted.name(),
         c.border.name(), c.window.name(), c.danger.name(), c.panelAlt.name())
    .arg(dp(5))
    .arg(c.muted.name(), c.panelAlt.name())
    .arg(dp(3))
    .arg(dp(15))
    .arg(dp(16))
    .arg(dp(8))
    .arg(c.active.name(), guide::slate.name(), // the start button: Slate on Live
         dark ? QColor(c.accent.red(), c.accent.green(), c.accent.blue(), 45).name(QColor::HexArgb) : selectedBackground().name())
    .arg((dark ? guide::cobalt : guide::cobaltPressed).name(), guide::cobaltPressed.name());
  return qss + menuAndTableStyleSheet(c, dark) + analysisStyleSheet(c, dark);
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
          label->setMinimumWidth(dp(487));
        }
        for (QAbstractButton *button : box->buttons()) {
          button->setMinimumSize(dp(112), dp(37));
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
  // every rounded corner the same (radius()); scroll bar handles and tick box / radio indicators keep their own
  QString sheet = styleSheet(dark);
  {
    static QRegularExpression const corner{QStringLiteral("(border(?:-top|-bottom)?(?:-left|-right)?-radius:\\s*)(\\d+)px")};
    QStringList rules = sheet.split(QLatin1Char('}'));
    for (QString &rule : rules) {
      QString const selector = rule.section(QLatin1Char('{'), 0, 0);
      if (selector.contains(QLatin1String("QScrollBar")) || selector.contains(QLatin1String("::indicator"))) {
        continue;
      }
      QString                         out;
      qsizetype                       from{0};
      QRegularExpressionMatchIterator it = corner.globalMatch(rule);
      while (it.hasNext()) {
        QRegularExpressionMatch const m = it.next();
        out += rule.mid(from, m.capturedStart() - from) + m.captured(1)
               + ((m.captured(2).toInt() > 0) ? QString::number(radius()) : m.captured(2)) + QStringLiteral("px");
        from = m.capturedEnd();
      }
      rule = out + rule.mid(from);
    }
    sheet = rules.join(QLatin1Char('}'));
  }
  qApp->setStyleSheet(sheet);
  // Setting a style sheet for the first time sets the application's style again, which brings the platform's own fonts
  // back for tables, lists, menus and message boxes (9 pt on Windows, whatever the screen): their texts were tiny
  // beside everything else. The application's font again clears them, so they follow the screen's scale too
  QApplication::setFont(QApplication::font());
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
                        : (role == QLatin1String("text"))   ? c.text
                        : (role == QLatin1String("heat"))   ? c.heat
                        : (role == QLatin1String("cold"))   ? c.cold
                                                            : c.accent; // "icon", "accent": the icons are Cobalt
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
  // the chart colours of the guide: Axis lines, Tick text labels and titles, a dashed Grid (on the dark theme the
  // dark set: Haze, Slate Line)
  QColor const axisColor = g_dark ? guide::haze : guide::axis;
  QColor const tickColor = g_dark ? guide::haze : guide::tickText;
  QColor const gridColor = g_dark ? guide::slateLine : guide::grid;
  plot->setBackground(c.panel); // the card's; the plot area inside the axes Plot
  for (QCPAxisRect *rect : plot->axisRects()) {
    rect->setBackground(c.panelAlt);
    for (QCPAxis *axis : rect->axes()) {
      axis->setBasePen(QPen(axisColor, 1));
      axis->setTickPen(QPen(axisColor, 1));
      axis->setSubTickPen(QPen(axisColor, 1));
      axis->setTickLabelColor(tickColor);
      axis->setLabelColor(tickColor);
      axis->grid()->setPen(QPen(gridColor, 1, Qt::DashLine));
      axis->grid()->setSubGridPen(QPen(gridColor, 1, Qt::DashLine));
      axis->grid()->setZeroLinePen(QPen(c.border, 1));
    }
  }
  // labels marked "themedLabel" (the date in the corner of the live chart): Tick text on the card's colour
  for (int i{0}; i < plot->itemCount(); ++i) {
    if (auto *text = qobject_cast<QCPItemText *>(plot->item(i)); text && text->property("themedLabel").toBool()) {
      text->setColor(tickColor);
      text->setBrush(QColor(c.panel.red(), c.panel.green(), c.panel.blue(), 200));
    }
  }
  if (plot->legend) {
    plot->legend->setBrush(QColor(c.panel.red(), c.panel.green(), c.panel.blue(), 220));
    plot->legend->setBorderPen(QPen(c.border));
    plot->legend->setTextColor(c.text);
  }
  plot->replot(QCustomPlot::rpQueuedReplot);
}

namespace {
// the decades as powers of ten, "10⁻³" (QCustomPlot's own needs a lower case "e", which Qt 6 locales do not write)
class PowerTicker : public QCPAxisTickerLog {
protected:
  QString getTickLabel(double tick, QLocale const &, QChar, int) override {
    static QString const digits = QStringLiteral("⁰¹²³⁴⁵⁶⁷⁸⁹");
    int const            exponent{static_cast<int>(std::lround(std::log10(tick)))};
    QString              text   = QStringLiteral("10");
    if (exponent < 0) {
      text += QChar(0x207B); // superscript minus
    }
    for (QChar const digit : QString::number(std::abs(exponent))) {
      text += digits.at(digit.digitValue());
    }
    return text;
  }
};
}

void setupPressureAxis(QCPAxis *axis) {
  if (!axis) {
    return;
  }
  QSharedPointer<QCPAxisTickerLog> ticker(new PowerTicker);
  ticker->setSubTickCount(8); // 2…9 between the decades
  ticker->setTickCount(6);
  axis->setTicker(ticker);
  axis->setScaleType(QCPAxis::stLogarithmic);
  axis->setRange(1e-3, 1e3);
  axis->setLabel(QCoreApplication::translate("theme", "Pressure, Torr"));
  // the plot's left axis: same pens and colours (its grid stays the left axis's)
  if (QCPAxis const *left = axis->axisRect()->axis(QCPAxis::atLeft)) {
    axis->setBasePen(left->basePen());
    axis->setTickPen(left->tickPen());
    axis->setSubTickPen(left->subTickPen());
    axis->setTickLabelColor(left->tickLabelColor());
    axis->setLabelColor(left->labelColor());
    axis->setTickLabelFont(left->tickLabelFont());
    axis->setLabelFont(left->labelFont());
  }
  axis->grid()->setVisible(false);
  axis->setVisible(true);
}

QPixmap dialogIcon(QString const &kind, int size) {
  Colors const &c     = colors();
  QColor const  color = (kind == QLatin1String("error"))     ? c.danger
                        : (kind == QLatin1String("warning")) ? guide::connecting
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
  p.setPen(guide::white);
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
    .arg(dp(1))
    .arg(c.border.name(), c.muted.name())
    .arg(dp(3))
    .arg(dp(11))
    .arg(dp(22))
    .arg(dp(7))
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
    {"selection-background-color: #dce6ff", "selection-background-color: " + guide::cobaltPressed.name()},
    {"selection-color: #1e2633", "selection-color: " + guide::pearl.name()},
    {"color: #1e2633", "color: " + c.text.name()},
    {"solid #1e2633", "solid " + c.text.name()},
    {"#d9dee5", c.border.name()},
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
  } else if (kind == "close") { // ×
    p.drawLine(pt(0.24, 0.24), pt(0.76, 0.76));
    p.drawLine(pt(0.76, 0.24), pt(0.24, 0.76));
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
  } else if ((kind == "expand") || (kind == "shrink")) { // arrows to the corners (out), or from them (in)
    bool const out{kind == "expand"};
    for (auto const &[dx, dy] : {std::pair{-1, -1}, std::pair{1, -1}, std::pair{-1, 1}, std::pair{1, 1}}) {
      QPointF const inner = pt(0.5 + dx * 0.1, 0.5 + dy * 0.1);
      QPointF const outer = pt(0.5 + dx * 0.36, 0.5 + dy * 0.36);
      p.drawLine(inner, outer);
      QPointF const tip  = out ? outer : inner;
      double const  back{out ? -1.0 : 1.0}; // the arrowhead's legs go back along the line
      p.drawLine(tip, tip + QPointF(back * dx * s * 0.16, 0));
      p.drawLine(tip, tip + QPointF(0, back * dy * s * 0.16));
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
  } else if (kind == "snowflake") { // six arms, each with a branch either side near its end
    for (int k{0}; k < 6; ++k) {
      double const a{k * 3.14159265 / 3.0 - 3.14159265 / 2.0};
      QPointF const tip{pt(0.5 + 0.4 * std::cos(a), 0.5 + 0.4 * std::sin(a))};
      QPointF const fork{pt(0.5 + 0.25 * std::cos(a), 0.5 + 0.25 * std::sin(a))};
      p.drawLine(pt(0.5, 0.5), tip);
      for (double const side : {-0.7, 0.7}) {
        p.drawLine(fork, fork + QPointF(0.12 * s * std::cos(a + side), 0.12 * s * std::sin(a + side)));
      }
    }
  } else if (kind == "flame") { // heating
    QPainterPath flame;
    flame.moveTo(pt(0.5, 0.9));
    flame.cubicTo(pt(0.24, 0.9), pt(0.16, 0.66), pt(0.28, 0.48));
    flame.cubicTo(pt(0.34, 0.6), pt(0.4, 0.62), pt(0.42, 0.58));
    flame.cubicTo(pt(0.38, 0.38), pt(0.46, 0.2), pt(0.58, 0.1));
    flame.cubicTo(pt(0.6, 0.3), pt(0.84, 0.44), pt(0.8, 0.66));
    flame.cubicTo(pt(0.78, 0.82), pt(0.66, 0.9), pt(0.5, 0.9));
    p.drawPath(flame);
  } else if (kind == "air") { // venting: three lines of wind, their ends curled
    QPainterPath wind;
    wind.moveTo(pt(0.1, 0.34));
    wind.lineTo(pt(0.6, 0.34));
    wind.cubicTo(pt(0.8, 0.34), pt(0.8, 0.1), pt(0.62, 0.14));
    wind.moveTo(pt(0.1, 0.54));
    wind.lineTo(pt(0.76, 0.54));
    wind.cubicTo(pt(0.96, 0.54), pt(0.96, 0.8), pt(0.78, 0.76));
    wind.moveTo(pt(0.1, 0.74));
    wind.lineTo(pt(0.44, 0.74));
    p.drawPath(wind);
  } else if (kind == "clock") {
    p.drawEllipse(QRectF(pt(0.12, 0.12), pt(0.88, 0.88)));
    p.drawLine(pt(0.5, 0.5), pt(0.5, 0.26));
    p.drawLine(pt(0.5, 0.5), pt(0.66, 0.6));
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
