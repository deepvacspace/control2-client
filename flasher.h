#ifndef FLASHER_H
#define FLASHER_H
#include <QLabel>
#include <QPainter>
#include <QTimer>

namespace deepvaccontrol {
class Flasher : public QLabel {
  Q_OBJECT
public:
  explicit Flasher(QWidget *parent = nullptr);
  void setFlashing(bool enable);

  // QObject interface
  bool eventFilter(QObject *watched, QEvent *event) override;
  void paintEvent(QPaintEvent *event) override;

  // QWidget interface
protected:
  void resizeEvent(QResizeEvent *event) override;

private:
  bool   m_flashingEnable;
  QTimer m_timer;
  int    m_elapsedTime{0}; // Время с момента старта анимации
};
} // namespace deepvaccontrol

#endif // FLASHER_H
