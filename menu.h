#ifndef MENU_H
#define MENU_H

#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

namespace deepvaccontrol {
class Menu : public QWidget {
  Q_OBJECT
public:
  explicit Menu(int buttonHeight, QWidget *parent = nullptr);
  void addItem(QString const &name);
  void selectItem(int const index);
  void setElementsEnable(QVector<bool> enableList);
  void setElementsEnableAll();
  void addWidget(QWidget *widget);

signals:
  void itemClicked(int indexCliked, int indexPrev);

private:
  void                   processMenuClicked();
  QVector<QPushButton *> m_buttons;
  QVBoxLayout           *m_listLay;
  int                    m_currentSelected;
  int                    m_prevSelected;
  int                    m_buttonHeight;
};
} // namespace deepvaccontrol

#endif // MENU_H
