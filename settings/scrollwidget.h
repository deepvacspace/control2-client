#ifndef SCROLLWIDGET_H
#define SCROLLWIDGET_H

#include <QWidget>

namespace control2 {
class ScrollWidget : public QWidget {
  Q_OBJECT
public:
  explicit ScrollWidget(bool useStyle = true, QWidget *parent = nullptr);
  ~ScrollWidget() override = default;
  void setLayout2(QLayout *layout);

private:
  QWidget *m_marginWidget;
};
} // namespace control2

#endif // SCROLLWIDGET_H
