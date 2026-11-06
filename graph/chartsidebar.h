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
 * @brief Analysis and Program control pages, right: a rail of icon buttons and the panel of the one chosen; both are
 * rounded cards. The rail takes its place in the page's layout; the panel floats over the page beside it (with a
 * shadow), so opening it does not narrow the charts.
 *
 * A tap opens a button's panel; tapping it again, the panel's ×, or the page beside the panel closes it. Pages are
 * added once and kept.
 */
class ChartSideBar : public QFrame {
  Q_OBJECT
public:
  explicit ChartSideBar(QWidget *parent = nullptr);

  // a rail button and its page; bottom: in the lower group; scroll: the page scrolls when it is taller than the panel
  void addPage(QString const &key, QString const &title, QString const &icon, QWidget *page, bool bottom = false, bool scroll = true);
  // a rail button that does something (no panel of its own)
  QToolButton *addAction(QString const &title, QString const &icon, bool bottom = false);
  void open(QString const &key);
  void close();
  void toggle(QString const &key);
  [[nodiscard]] QString current() const; // empty: closed
  void setPageEnabled(QString const &key, bool enabled);
  // shown in the panel's header, beside the title, while that page is open
  void setHeaderWidget(QString const &key, QWidget *widget);
  // a muted line under the panel's title while that page is open ("6 of 8 shown"); empty: none
  void setSubtitle(QString const &key, QString const &text);
  // the panel ends above this widget of the page (Program control: the Start / Stop bar stays in view)
  void setOverlayBottom(QWidget *widget);

signals:
  void opened(QString const &key);
  void closed();

protected:
  void resizeEvent(QResizeEvent *event) override;
  void moveEvent(QMoveEvent *event) override;
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  void         placePanel();
  QToolButton *railButton(QString const &title, QString const &icon, bool bottom);

  struct Page {
    QToolButton *button{nullptr};
    QWidget     *holder{nullptr};
    QString      title;
    QWidget     *header{nullptr};
    QString      subtitle;
  };
  void showSubtitle();

  QWidget             *m_overlay{nullptr}; // the panel and its shadow, over the page
  QWidget             *m_overlayBottom{nullptr};
  QFrame              *m_panel{nullptr};
  QLabel              *m_title{nullptr};
  QLabel              *m_subtitle{nullptr};
  QHBoxLayout         *m_header{nullptr};
  QStackedWidget      *m_stack{nullptr};
  QVBoxLayout         *m_top{nullptr};
  QVBoxLayout         *m_bottom{nullptr};
  QMap<QString, Page>  m_pages;
  QString              m_current;
};

}

#endif // CHARTSIDEBAR_H
