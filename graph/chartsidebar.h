#ifndef CHARTSIDEBAR_H
#define CHARTSIDEBAR_H

#include <QFrame>
#include <QMap>

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
  void open(QString const &key);
  void close();
  void toggle(QString const &key);
  [[nodiscard]] QString current() const; // empty: closed
  void setPageEnabled(QString const &key, bool enabled);

signals:
  void opened(QString const &key);
  void closed();

private:
  struct Page {
    QToolButton *button{nullptr};
    QWidget     *holder{nullptr};
    QString      title;
  };

  QFrame              *m_panel{nullptr};
  QLabel              *m_title{nullptr};
  QStackedWidget      *m_stack{nullptr};
  QVBoxLayout         *m_top{nullptr};
  QVBoxLayout         *m_bottom{nullptr};
  QMap<QString, Page>  m_pages;
  QString              m_current;
};

} // namespace deepvaccontrol

#endif // CHARTSIDEBAR_H
