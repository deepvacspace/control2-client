#ifndef THEME_H
#define THEME_H

#include <QColor>
#include <QIcon>
#include <QPalette>
#include <QString>

class QCPAxis;
class QCustomPlot;

namespace deepvaccontrol::theme {

// The Deepvac HMI colour guide (theme::guide): every colour of the application is one of these (the lines of the charts excepted,
// their colours are the user's)
namespace guide {
// surfaces
inline QColor const plot{"#f2f6f8"};   // chart plot area, app background, inner value zones
inline QColor const fog{"#d3dae5"};    // tab bar, page chrome, active nav tile
inline QColor const border{"#d9dee5"}; // card and panel outlines
inline QColor const white{"#ffffff"};  // cards, tool rail, chart card
// dark and text
inline QColor const slate{"#1e2633"};     // sidebar, status card, titles, big values
inline QColor const steel{"#5c6472"};     // labels, secondary text
inline QColor const haze{"#a9afba"};      // ghost text, placeholders
inline QColor const pearl{"#f0f3f7"};     // text on the dark sidebar
inline QColor const slateLine{"#3a475c"}; // dividers on dark
inline QColor const slateRaised{"#273142"}; // cards and panels on dark: the sidebar's status box, dark-theme cards
// accent
inline QColor const cobaltPressed{"#1a4bc2"}; // pressed state
inline QColor const cobalt{"#2760e6"};        // icons, + button, selected
inline QColor const cobaltTint{"#dce6ff"};    // selected row / tab fill
inline QColor const sky{"#5c93f5"};           // icons and labels on the dark sidebar
// status
inline QColor const alarm{"#d4122e"};        // STOP, alarms, the temperature line
inline QColor const idle{"#2760e6"};         // idle dot
inline QColor const connecting{"#f2a60c"};   // connecting dot
inline QColor const noConnection{"#a9afba"}; // offline dot, disabled STOP / buttons
inline QColor const live{"#19b26b"};         // live / connected
inline QColor const heat{"#ea580c"};         // heating: chamber heater, control output, flame icon
// chart
inline QColor const temperature{"#d4122e"};
inline QColor const pressure{"#2760e6"};
inline QColor const grid{"#e3e8ee"};     // dashed grid (also "Mist")
inline QColor const axis{"#686b70"};     // axis lines
inline QColor const tickText{"#65686f"}; // tick labels, axis titles
}

// Colours of the application design; the navy set is the default, the light set is used after
// System -> OS -> "Switch to Light mode".
struct Colors {
  QColor window;    // page background
  QColor header;    // header and footer bars
  QColor panel;     // cards
  QColor panelAlt;  // inputs, inner boxes
  QColor border;    // card and control borders
  QColor text;      // main text
  QColor muted;     // secondary text
  QColor accent;    // selection, links, cooling
  QColor active;    // running / on / online
  QColor ready;     // available but off
  QColor off;       // not available
  QColor danger;    // stop, alarms
  QColor heat;      // heating
  QColor cold;      // cooling
  QColor warning;
  QColor rule;      // light lines: tab strips, the chart's grid
};

// a size of the design (Figma frames of a 1920 px wide screen) on this screen
[[nodiscard]] int dp(double designPx);
// the spacing between the elements of a page, and between icons: 16 of the design
[[nodiscard]] int gap();
// the corners of cards, panels, fields, buttons and badges: 8 of the design
[[nodiscard]] int radius();

[[nodiscard]] bool          isDark();
[[nodiscard]] Colors const &colors();
// the navigation sidebar is Slate in both themes
[[nodiscard]] Colors const &sidebarColors();
// the monospace face of readings and values: JetBrains Mono (bundled), else Consolas
[[nodiscard]] QString       monoFamily();
// the background of what is selected, checked or pressed: Silver on the light theme
[[nodiscard]] QColor        selectedBackground();
[[nodiscard]] QPalette      palette(bool dark);
[[nodiscard]] QString       styleSheet(bool dark);
// palette and application style sheet
void apply(bool dark);

// colours of a QCustomPlot (background, axes, grid, legend)
void stylePlot(QCustomPlot *plot);
// a chart's right axis for pressure: logarithmic, powers of ten, "Pressure, Torr"; styled like the plot's other axes
void setupPressureAxis(QCPAxis *axis);
// flat tab bar: plain labels, the selected one underlined (System and Settings pages)
[[nodiscard]] QString flatTabStyleSheet();
// flatTabStyleSheet() on a tab widget, re-applied by apply() when the theme changes
void setFlatTabs(QWidget *tabWidget);
// round icon of the message windows: "warning", "error", "question", "info"
[[nodiscard]] QPixmap dialogIcon(QString const &kind, int size);
// light colours of the resource style sheets (res/styles) replaced by the current theme
[[nodiscard]] QString adaptStyleSheet(QString qss);

// line icons of the footer menu: "home", "manual", "program", "chart", "analysis", "ai", "settings", "users",
// "system", "power"
[[nodiscard]] QIcon menuIcon(QString const &kind, QColor const &normal, QColor const &selected, int size);
// a menuIcon on a button (icon) or a label (pixmap) in a theme colour ("icon": the accent, "text", "muted", "danger",
// "accent", "heat", "cold"), redrawn by apply() when the theme changes
void setThemedIcon(QWidget *target, QString const &kind, int size, QString const &role = QStringLiteral("icon"));

}

#endif // THEME_H
