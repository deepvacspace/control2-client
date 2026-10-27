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
 * @brief Chart page, right: a rail of buttons (icon over a word) and the panel of the one chosen, beside the chart.
 *
 * A tap opens a button's panel; tapping it again, or the panel's ×, closes it. Pages are added once and kept.
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

signals:
  void opened(QString const &key);
  void closed();

private:
  QToolButton *railButton(QString const &title, QString const &icon, bool bottom);

  struct Page {
    QToolButton *button{nullptr};
    QWidget     *holder{nullptr};
    QString      title;
    QWidget     *header{nullptr};
  };

  QFrame              *m_panel{nullptr};
  QLabel              *m_title{nullptr};
  QHBoxLayout         *m_header{nullptr};
  QStackedWidget      *m_stack{nullptr};
  QVBoxLayout         *m_top{nullptr};
  QVBoxLayout         *m_bottom{nullptr};
  QMap<QString, Page>  m_pages;
  QString              m_current;
};

}

#endif // CHARTSIDEBAR_H
