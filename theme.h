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
// a menuIcon on a button (icon) or a label (pixmap) in a theme colour ("text", "muted", "danger"), redrawn by apply()
// when the theme changes
void setThemedIcon(QWidget *target, QString const &kind, int size, QString const &role = QStringLiteral("text"));

}

#endif // THEME_H
