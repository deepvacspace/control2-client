#ifndef CHAMBERVIEW_H
#define CHAMBERVIEW_H

#include <QHash>
#include <QSet>
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
  ~ChamberView() override;

  enum class State { ACTIVE, READY, OFF };

  // a component as the indicator list shows it
  struct Indicator {
    QString key;
    QString title;
    QString icon;   // theme icon kind
    QString status; // "On", "Off", "Open", a value, or "—" when not fitted
    State   state{State::OFF};
    bool    fitted{false};
    bool    overridden{false}; // fitted or not as a Service Engineer set it for this chamber
    QString note;              // where the PLC's report disagrees (with that choice, or with the settings)
  };
  [[nodiscard]] QVector<Indicator> indicators() const;
  [[nodiscard]] bool               isFitted(QString const &key) const {
    return fitted(key);
  }
  // fitted / not fitted as a Service Engineer says, for the chamber connected to (saved in the settings, not sent
  // to the PLC); every diagram follows
  static void setFittedOverride(QString const &key, bool fitted);
  static void clearFittedOverrides();

  // compact: the components the chamber has as a strip of status chips (the Home page); a tap asks for the diagram
  void                setCompact(bool compact);
  // compact: only these components (shown even when not fitted, greyed out), as a bare row of chips without
  // the card and the title
  void                setChips(QStringList const &keys);
  [[nodiscard]] QSize sizeHint() const override;

signals:
  void diagramRequested();
  void refreshed(); // what is shown changed (values, connection, components, overrides)

protected:
  void paintEvent(QPaintEvent *event) override;
  bool event(QEvent *event) override;
  void showEvent(QShowEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
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
  bool   heating() const; // the thermal plate heats
  void   refreshAnimation();
  Tag    tag(QString const &key) const;

  // drawing (canvas 740 x 620)
  void drawChamber(QPainter &p);
  // the chamber pressure in Torr (NaN: no reading), big in the middle of the inner space
  [[nodiscard]] double pressureTorr() const;
  void                 drawPressure(QPainter &p);
  // the pressure gauge standing on the top right of the cabinet
  void                 drawGauge(QPainter &p);
  void drawVacuum(QPainter &p);
  // air drawn moving along a route of pipes (points in flow order), as particles in the bore
  void drawAirflow(QPainter &p, QVector<QPointF> const &points, QColor const &color);
  void drawSupply(QPainter &p);
  void drawPlc(QPainter &p);
  void drawLine(QPainter &p, QVector<QPointF> const &points, bool fitted, bool flowing, QColor const &flowColor);
  void drawPipe(QPainter &p, QPointF const &from, QPointF const &to, bool fitted, bool flowing, QColor const &flowColor);
  void drawValve(QPainter &p, QPointF const &at, QString const &key, QString const &title, QString const &tip, bool tagAbove,
                 bool vertical = false);
  // a pipe along these points (bends included); flanges at the ends asked for
  void drawPipePath(QPainter &p, QVector<QPointF> const &points, bool isFitted, bool flowing, QColor const &flowColor,
                    bool startFlange = true, bool endFlange = true);
  // anchor: the point of the tag given by align (e.g. AlignRight | AlignVCenter: the middle of its right edge)
  // backdrop: on a panel-coloured card (over the drawing of the chamber)
  void drawTag(QPainter &p, QPointF const &anchor, Qt::Alignment align, QString const &key, QString const &title,
               bool backdrop = false);
  void drawSnowflake(QPainter &p, QPointF const &center, double radius, QColor const &color);
  void paintCompact(QPainter &p);
  // the chips of the compact strip: key and title of each component the chamber has
  [[nodiscard]] QVector<QPair<QString, QString>> chips() const;
  [[nodiscard]] double                          tagWidth(QString const &key, QString const &title) const;
  void addTip(QRectF const &rect, QString const &text);

  ResHolder                      *m_resHolder;
  QHash<QString, Output>          m_outputs; // component key -> output bit
  QSet<QString>                   m_configured; // the "components" of the settings (used while the PLC reports none)
  QSet<QString>                   m_reported;  // the components the chamber itself reports as fitted
  bool                            m_hasReport{false};
  QTimer                         *m_timer{nullptr};
  double                          m_phase{0.0};
  QVector<QPair<QRectF, QString>> m_tips;      // canvas rects -> tool tips
  QTransform                      m_transform; // canvas -> widget
  bool                            m_compact{false};
  void                            changed(); // update() and refreshed()
  static QHash<QString, bool>     s_overrides; // of the chamber connected to
  static ResHolder               *s_resHolder;
  static void                     loadOverrides();
  static void                     saveOverrides();
  [[nodiscard]] bool              detectedFitted(QString const &key) const; // the PLC's report, else the settings
  static QList<ChamberView *>     s_views;
  QStringList                     m_chipKeys; // compact: the components shown (empty: every fitted one)
};

}

#endif // CHAMBERVIEW_H
