#ifndef OVERLAYDRAWER_H
#define OVERLAYDRAWER_H

#include <QWidget>

class QFrame;
class QPropertyAnimation;

namespace deepvaccontrol {

/**
 * @brief Panel sliding in from the left or right edge over its host widget (not part of the host's layout), with
 * the rest of the host dimmed. A click on the dimmed part or Esc closes it.
 */
class OverlayDrawer : public QWidget {
  Q_OBJECT
public:
  enum class Side : uint8_t {
    LEFT,
    RIGHT
  };

  // width in unscaled pixels
  OverlayDrawer(QWidget *host, int width, Side side = Side::LEFT);

  void               setContent(QWidget *content);
  void               open();
  void               close();
  void               toggle();
  [[nodiscard]] bool isOpen() const;

signals:
  void openChanged(bool open);

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;
  void paintEvent(QPaintEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;

private:
  void placePanel();
  // panel position when shown / hidden
  [[nodiscard]] QPoint shownPos() const;
  [[nodiscard]] QPoint hiddenPos() const;

  QWidget            *m_host;
  QFrame             *m_panel;
  QPropertyAnimation *m_slide;
  int                 m_width;
  Side                m_side;
  bool                m_open{false};
};

inline bool OverlayDrawer::isOpen() const {
  return m_open;
}

} // namespace deepvaccontrol

#endif // OVERLAYDRAWER_H
