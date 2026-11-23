#ifndef CHARTSIDEBAR_H
#define CHARTSIDEBAR_H

#include <QFrame>
#include <QMap>

class QHBoxLayout;
class QLabel;
class QStackedWidget;
class QToolButton;
class QVBoxLayout;

namespace deepvaccontrol {

/**
 * @brief Analysis page: a row of icon buttons (the options bar) and the panel of the one chosen. The bar takes its
 * place in the page's layout; the panel is either docked in a widget of the page (the Live tab: where the chamber's
 * status is) or floats over a widget of the page (the chart's card: at its right, above the bar, with a shadow), so
 * opening it does not narrow the charts.
 *
 * A tap opens a button's panel; tapping it again or the panel's × closes it (floating: a tap on the page beside it
 * too). Pages are added once and kept.
 */
class ChartSideBar : public QFrame {
  Q_OBJECT
public:
  explicit ChartSideBar(QWidget *parent = nullptr);

  // a button of the bar and its page; scroll: the page scrolls when it is taller than the panel
  void addPage(QString const &key, QString const &title, QString const &icon, QWidget *page, bool scroll = true);
  // a bar button's size (the chart's tools have the same)
  [[nodiscard]] static QSize barButtonSize();
  void open(QString const &key);
  void close();
  void toggle(QString const &key);
  [[nodiscard]] QString current() const; // empty: closed
  void setPageEnabled(QString const &key, bool enabled);
  // shown in the panel's header, beside the title, while that page is open
  void setHeaderWidget(QString const &key, QWidget *widget);
  // a muted line under the panel's title while that page is open ("6 of 8 shown"); empty: none
  void setSubtitle(QString const &key, QString const &text);
  // the panel in this widget's layout (it shows it in place of what it holds while a page is open); none: floating
  void setDock(QWidget *dock);
  [[nodiscard]] bool docked() const;
  // floating: the panel over this widget of the page, at its right, down to the bar when the bar is in it
  void setFloatArea(QWidget *area);

signals:
  void opened(QString const &key);
  void closed();

protected:
  void resizeEvent(QResizeEvent *event) override;
  void moveEvent(QMoveEvent *event) override;
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  void placePanel();
  void showPanel(); // docked or floating, as it is now

  struct Page {
    QToolButton *button{nullptr};
    QWidget     *holder{nullptr};
    QString      title;
    QWidget     *header{nullptr};
    QString      subtitle;
  };
  void showSubtitle();

  QWidget             *m_overlay{nullptr}; // the floating panel and its shadow, over the page
  QWidget             *m_dock{nullptr};
  QWidget             *m_floatArea{nullptr};
  QFrame              *m_panel{nullptr};
  QVBoxLayout         *m_panelLay{nullptr};
  QLabel              *m_title{nullptr};
  QLabel              *m_subtitle{nullptr};
  QHBoxLayout         *m_header{nullptr};
  QStackedWidget      *m_stack{nullptr};
  QHBoxLayout         *m_row{nullptr}; // the bar's buttons
  QMap<QString, Page>  m_pages;
  QString              m_current;
};

}

#endif // CHARTSIDEBAR_H
