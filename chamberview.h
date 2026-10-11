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
 * @brief Home page panel: a schematic of the chamber (vacuum line with turbo and backing pump, LN2 supply,
 * refrigeration, heater and the PLC) with the live state of the components found in the configuration (outputs
 * in "monitor", measured states) in a small tag next to each. Components the chamber does not have are drawn
 * greyed out.
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
  // the small label next to a component: its name and state
  struct Tag {
    QString status;
    State   state{State::OFF};
    bool    fitted{true}; // the chamber has it
  };

  void   detect();
  bool   fitted(QString const &key) const;
  bool   on(QString const &key) const;
  bool   online() const;
  double value(QString const &name) const;
  bool   cooling() const;
  void   refreshAnimation();
  Tag    tag(QString const &key) const;

  // drawing (canvas 740 x 620)
  void drawChamber(QPainter &p);
  void drawVacuum(QPainter &p);
  void drawSupply(QPainter &p);
  void drawPlc(QPainter &p);
  void drawLine(QPainter &p, QVector<QPointF> const &points, bool fitted, bool flowing, QColor const &flowColor);
  void drawPipe(QPainter &p, QPointF const &from, QPointF const &to, bool fitted, bool flowing, QColor const &flowColor);
  void drawValve(QPainter &p, QPointF const &at, QString const &key, QString const &title, QString const &tip, bool tagAbove);
  // anchor: the point of the tag given by align (e.g. AlignRight | AlignVCenter: the middle of its right edge)
  void drawTag(QPainter &p, QPointF const &anchor, Qt::Alignment align, QString const &key, QString const &title);
  void drawSnowflake(QPainter &p, QPointF const &center, double radius, QColor const &color);
  void addTip(QRectF const &rect, QString const &text);

  ResHolder                      *m_resHolder;
  QHash<QString, Output>          m_outputs; // component key -> output bit
  QTimer                         *m_timer{nullptr};
  double                          m_phase{0.0};
  QVector<QPair<QRectF, QString>> m_tips;      // canvas rects -> tool tips
  QTransform                      m_transform; // canvas -> widget
};

} // namespace deepvaccontrol

#endif // CHAMBERVIEW_H
