#ifndef THEME_H
#define THEME_H

#include <QColor>
#include <QIcon>
#include <QPalette>
#include <QString>

class QCustomPlot;

namespace deepvaccontrol::theme {

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
};

[[nodiscard]] bool          isDark();
[[nodiscard]] Colors const &colors();
[[nodiscard]] QPalette      palette(bool dark);
[[nodiscard]] QString       styleSheet(bool dark);
// palette and application style sheet
void apply(bool dark);

// colours of a QCustomPlot (background, axes, grid, legend)
void stylePlot(QCustomPlot *plot);
// light colours of the resource style sheets (res/styles) replaced by the current theme
[[nodiscard]] QString adaptStyleSheet(QString qss);

// line icons of the footer menu: "home", "manual", "program", "chart", "analysis", "ai", "settings", "users",
// "system", "power"
[[nodiscard]] QIcon menuIcon(QString const &kind, QColor const &normal, QColor const &selected, int size);

} // namespace deepvaccontrol::theme

#endif // THEME_H
