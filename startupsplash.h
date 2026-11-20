#ifndef STARTUPSPLASH_H
#define STARTUPSPLASH_H

#include <QPixmap>
#include <QPointer>
#include <QWidget>

namespace deepvaccontrol {

// While the application starts: a card in the middle of the screen with the logo, what is being done, how far the
// start is and the version. The start blocks the event loop, so it is drawn at once (show(), setMessage(),
// setProgress()) instead of waiting for it; once the window is there the bar runs to its end and the card fades out
class StartupSplash : public QWidget {
  Q_OBJECT
public:
  StartupSplash();
  void setMessage(QString const &text);
  // how far the start is, 0 … 1 (never back)
  void setProgress(double progress);
  // setProgress() of the splash shown, if there is one: for the steps of the start that do not know of it
  static void advance(double progress);
  // closes once `window` has been drawn at its full size (and at the latest after a while)
  void finishWhenShown(QWidget *window);

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;
  void paintEvent(QPaintEvent *event) override;

private:
  void               repaintNow(bool events);
  void               finish(); // the bar to its end, then faded out and closed
  [[nodiscard]] int  shadowMargin() const;

  QPixmap           m_logo;
  QString           m_message;
  double            m_progress{0.0};
  QPointer<QWidget> m_window;
  bool              m_closing{false};
};

}

#endif // STARTUPSPLASH_H
