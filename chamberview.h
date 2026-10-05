#ifndef CHAMBERVIEW_H
#define CHAMBERVIEW_H

#include <QHash>
#include <QPointF>
#include <QRectF>
#include <QVector>
#include <QWidget>

#include "resholder.h"

class QPainter;
class QTimer;

namespace deepvaccontrol {

/**
 * @brief "Chamber · live" panel of the Home page: a sketch of the chamber and its refrigeration unit with the
 * components found in the configuration (outputs in "monitor", measured states), their live state from the PLC,
 * and callout cards for the main groups.
 */
class ChamberView : public QWidget {
  Q_OBJECT
public:
  explicit ChamberView(ResHolder *resHolder, QWidget *parent = nullptr);

  enum class State { ACTIVE, READY, OFF };

protected:
  void paintEvent(QPaintEvent *event) override;
  bool event(QEvent *event) override;
  void showEvent(QShowEvent *event) override;
  void hideEvent(QHideEvent *event) override;

private:
  struct Output {
    QString label; // as configured
    int     bit{-1};
  };
  struct Card {
    QString title;
    QString status;
    State   state{State::OFF};
    QPointF anchor;
  };

  void   detect();
  bool   has(QString const &key) const;
  bool   on(QString const &key) const;
  State  outputState(QString const &key) const;
  bool   online() const;
  double value(QString const &name) const;
  void   refreshAnimation();

  // drawing (canvas 1000 x 620)
  void drawVessel(QPainter &p);
  void drawRefrigeration(QPainter &p);
  void drawCards(QPainter &p);
  void drawPipe(QPainter &p, QVector<QPointF> const &points, QColor const &color, bool flowing);
  void drawValve(QPainter &p, QPointF const &at, QString const &key, QString const &tag, bool vertical, QPointF const &labelOffset);
  void drawCompressor(QPainter &p, QPointF const &at, QString const &key, QString const &title, QString const &tag);
  void drawBadge(QPainter &p, QPointF const &topLeft, QString const &text, bool on);
  void drawReadout(QPainter &p, QPointF const &topLeft, QString const &tag, QString const &text, bool live);
  void addTip(QRectF const &rect, QString const &text);
  QVector<Card> cards() const;

  ResHolder              *m_resHolder;
  QHash<QString, Output>  m_outputs; // component key -> output bit
  QTimer                 *m_timer{nullptr};
  double                  m_phase{0.0};
  QVector<QPair<QRectF, QString>> m_tips; // canvas rects -> tool tips
  QTransform              m_transform;     // canvas -> widget
};

} // namespace deepvaccontrol

#endif // CHAMBERVIEW_H
