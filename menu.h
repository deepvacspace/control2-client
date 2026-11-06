#ifndef MENU_H
#define MENU_H

#include <QVBoxLayout>
#include <QToolButton>
#include <QWidget>

namespace deepvaccontrol {
// Sidebar navigation: one button (icon over label) per page at the top, extra widgets (light, power) at the bottom.
// The space between them stretches: a hidden extra widget leaves its place empty rather than moving the others.
class Menu : public QWidget {
  Q_OBJECT
public:
  explicit Menu(int buttonHeight, QWidget *parent = nullptr);
  // width of the sidebar (its buttons and extra widgets fit in it)
  [[nodiscard]] static int sidebarWidth();
  // iconKind: see theme::menuIcon
  void addItem(QString const &name, QString const &iconKind = {});
  void selectItem(int const index);
  void setElementsEnable(QVector<bool> enableList);
  void setElementsEnableAll();
  // a page kept in the application but not offered in the menu (whatever the access level)
  void setItemHidden(int index);
  void addWidget(QWidget *widget);     // at the bottom, under the separator
  void addListWidget(QWidget *widget); // in the list, after the items added so far

signals:
  void itemClicked(int indexCliked, int indexPrev);

protected:
  void paintEvent(QPaintEvent *event) override;
  void changeEvent(QEvent *event) override;

private:
  void                   processMenuClicked();
  void                   restyle();
  QWidget               *m_content{nullptr};
  QVector<QToolButton *> m_buttons;
  QVector<int>           m_hidden;
  QStringList            m_iconKinds;
  QVBoxLayout           *m_listLay;
  QVBoxLayout           *m_extraLay;
  int                    m_currentSelected;
  int                    m_prevSelected;
  int                    m_buttonHeight;
};
}

#endif // MENU_H
