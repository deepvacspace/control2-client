#ifndef SCREENSWIDGET_H
#define SCREENSWIDGET_H

#include <QWidget>
#include <QStackedWidget>

namespace deepvaccontrol{
/**
 * @brief Switches different widgets/'screens' in same space.
 * Based on QStackedWidget widget.
 */
class ScreensWidget : public QWidget {
  Q_OBJECT
public:
  explicit ScreensWidget(QWidget *parent = nullptr);
  void addWidget(QWidget* widget, const QString &name);
  void selectItem(int index);

QStackedWidget *m_widgets;
};
}

#endif // SCREENSWIDGET_H
