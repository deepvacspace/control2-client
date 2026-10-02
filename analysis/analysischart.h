#ifndef ANALYSISCHART_H
#define ANALYSISCHART_H

#include <QColor>
#include <QPointer>
#include <QVector>
#include <QWidget>

#include "qcustomplot.h"

namespace deepvaccontrol {

class CrosshairSyncHub;

struct ChartSeries {
  QString         label;
  QColor          color;
  QVector<double> x; // elapsed sec from run start
  QVector<double> y;
  int             width{2};
  bool            logAxis{false};
};

// user annotation: highlighted time range
struct ChartRegion {
  double  x0{0.0};
  double  x1{0.0};
  QString label;
  QColor  color;
};

// variable rule: acceptable band (or single limit if lo/hi is NaN)
struct ChartBand {
  double  lo{qQNaN()};
  double  hi{qQNaN()};
  QString label;
  QColor  color;
  bool    logAxis{false};
};

struct ChartEvent {
  double  x{0.0};
  QString text;
  QColor  color;
};

/**
 * @brief QCustomPlot based chart of one or several runs with crosshair, hover values,
 * smoothing, statistic overlays, rule bands, annotations and event markers.
 * X axis is the time elapsed from the run start.
 */
class AnalysisChart : public QWidget {
  Q_OBJECT
public:
  enum class Mode : uint8_t {
    LINE,
    SCATTER
  };

  explicit AnalysisChart(QWidget *parent = nullptr);
  ~AnalysisChart() override;

  // startEpoch > 0 adds the absolute time to the hover label
  void setSeries(QVector<ChartSeries> const &series, double startEpoch);
  void setMode(Mode mode);
  void setSmoothingWindow(int window);
  void setOverlays(bool min, bool max, bool avg);
  void setRegions(QVector<ChartRegion> const &regions);
  void setBands(QVector<ChartBand> const &bands);
  void setEvents(QVector<ChartEvent> const &events);
  void setAnnotateMode(bool enabled);

  // batch several setters into a single redraw
  void beginUpdate();
  void endUpdate();

  void                       setTimeRange(double start, double end);
  void                       resetView();
  [[nodiscard]] QPair<double, double> dataXRange() const;
  [[nodiscard]] QPair<double, double> viewXRange() const;
  bool                       exportPng(QString const &path);

  void setSyncHub(CrosshairSyncHub *hub);
  void showSyncCrosshair(double x, double y);
  void hideSyncCrosshair();

signals:
  void annotationDrawn(double x0, double x1);

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;

private:
  void rebuild();
  void rebuildDecorations();
  void clearDecorations();
  void addOverlays();
  void addBands();
  void addRegions();
  void addEvents();
  void autoRange();
  void onMousePress(QMouseEvent *event);
  void onMouseMove(QMouseEvent *event);
  void onSelectionAccepted(QRect const &rect, QMouseEvent *event);
  void setCrosshair(double x, double y, bool visible);
  void hideHover();
  void replotCrosshair();

  [[nodiscard]] QVector<double> smoothed(QVector<double> const &y) const;
  [[nodiscard]] QString         timeText(double x) const;
  [[nodiscard]] QCPAxis        *valueAxisFor(bool logAxis) const;

  QCustomPlot              *m_plot;
  QPointer<CrosshairSyncHub> m_syncHub;

  QVector<ChartSeries> m_series;
  QVector<ChartRegion> m_regions;
  QVector<ChartBand>   m_bands;
  QVector<ChartEvent>  m_events;
  double               m_startEpoch{0.0};
  Mode                 m_mode{Mode::LINE};
  int                  m_smoothingWindow{1};
  bool                 m_overlayMin{false};
  bool                 m_overlayMax{false};
  bool                 m_overlayAvg{false};
  bool                 m_annotateMode{false};
  bool                 m_autoRangePending{true};
  int                  m_updateDepth{0};
  bool                 m_graphsDirty{false};
  bool                 m_decorationsDirty{false};

  QVector<QCPAbstractItem *> m_decorations; // rebuilt on every change
  QCPItemStraightLine       *m_crossV{nullptr};
  QCPItemStraightLine       *m_crossH{nullptr};
  QCPItemText               *m_hoverText{nullptr};
};

} // namespace deepvaccontrol

#endif // ANALYSISCHART_H
