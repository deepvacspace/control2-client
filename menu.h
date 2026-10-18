#ifndef MENU_H
#define MENU_H

#include <QHBoxLayout>
#include <QToolButton>
#include <QWidget>

namespace deepvaccontrol {
// Footer navigation: one button (icon over label) per page, then extra widgets (light, power) on the right.
class Menu : public QWidget {
  Q_OBJECT
public:
  explicit Menu(int buttonHeight, QWidget *parent = nullptr);
  // iconKind: see theme::menuIcon
  void addItem(QString const &name, QString const &iconKind = {});
  void selectItem(int const index);
  void setElementsEnable(QVector<bool> enableList);
  void setElementsEnableAll();
  // a page kept in the application but not offered in the menu (whatever the access level)
  void setItemHidden(int index);
  void addWidget(QWidget *widget);

signals:
  void itemClicked(int indexCliked, int indexPrev);

protected:
  void paintEvent(QPaintEvent *event) override;
  void changeEvent(QEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;

private:
  void                   processMenuClicked();
  void                   restyle();
  // hides the menu, leaving a thin strip with the toggle (far left) to bring it back
  void                   setCollapsed(bool collapsed);
  void                   placeToggle();
  QWidget               *m_content{nullptr};
  QToolButton           *m_toggle{nullptr};
  bool                   m_collapsed{false};
  QVector<QToolButton *> m_buttons;
  QVector<int>           m_hidden;
  QStringList            m_iconKinds;
  QHBoxLayout           *m_listLay;
  QHBoxLayout           *m_extraLay;
  int                    m_currentSelected;
  int                    m_prevSelected;
  int                    m_buttonHeight;
};
} // namespace deepvaccontrol

#endif // MENU_H
