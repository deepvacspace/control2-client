#ifndef SCROLLWIDGET_H
#define SCROLLWIDGET_H

#include <QWidget>

namespace deepvaccontrol {
class ScrollWidget : public QWidget {
  Q_OBJECT
public:
  explicit ScrollWidget(bool useStyle = true, QWidget *parent = nullptr);
  ~ScrollWidget() override = default;
  void setLayout2(QLayout *layout);

private:
  QWidget *m_marginWidget;
};
} // namespace deepvaccontrol

#endif // SCROLLWIDGET_H
