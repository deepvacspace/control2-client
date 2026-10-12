#ifndef DATAVIEWER_H
#define DATAVIEWER_H

#include <QMetaObject>
#include <QWidget>

#include "dataholder.h"
#include "dataviewerlegend.h"
#include "logger.h"
#include "qcustomplot.h"

namespace deepvaccontrol {
class DataViewerSettings {
public:
};

/**
 * @brief Widget to display some number of data graphs.
 * It has interface to manage displayed region and graphs' visibility.
 */
class DataViewer : public QWidget {
  Q_OBJECT
public:
  enum class ControlMode : uint8_t {
    LEAD_FULL,
    LEAD_X,
    ZOOM
  };

  // labelled time range drawn over the chart (annotations)
  struct Region {
    double  x0{0.0}; // epoch s
    double  x1{0.0};
    QString label;
    QColor  color;
  };

  // acceptable range of a channel (variable rule): a band, or a single limit when lo or hi is NaN
  struct Band {
    double  lo{qQNaN()};
    double  hi{qQNaN()};
    QString label;
    QColor  color;
    int     channel{-1}; // index of the state (graph)
  };

public:
  explicit DataViewer(Logger *logger, bool addSecondLogAxis = false, QWidget *parent = nullptr);
  ~DataViewer() = default;

  void                              setData(std::shared_ptr<DataHolder const> data);
  std::shared_ptr<DataHolder const> getData() const;
  void                              setRefreshPeriodMs(size_t ms);
  void                              setStyles(QVariantList const &stateListVariant);
  void                              setStyle(int index, QVariantMap stateMapVariant);
  void                              updateTextItem(QString const &text);
  void                              setView(QRectF const &view); // used in graph review
  QRectF                            getCurrentView() const;
  void                              setIndexesForLogAxis(QVector<int> const &indexes);
  [[nodiscard]] QVector<int>        logAxisIndexes() const;
  [[nodiscard]] bool                hasLogAxis() const;
  // the styles of the lines (label, colour, pen, width, visible), as set
  [[nodiscard]] QVariantList        styles() const;
  [[nodiscard]] QString             channelLabel(int index) const;

  QCustomPlot *getPlot();

  void switchControlMode();
  void setControlMode(ControlMode mode);

  // the box with the line labels over the top left of the chart
  void setLabelsVisible(bool visible);
  bool labelsVisible() const;
  // a label with the value of the line under the mouse, a circle on its point and a time line
  void setValuesVisible(bool visible);
  bool valuesVisible() const;
  // the value label of another chart mirrored here: crosshair at time x, label and circle on the line `graph`
  void showSyncCursor(double x, int graph);
  void hideValueCursor();

  void moveView(double addX, double addY);

  void setRegions(QVector<Region> const &regions);
  // shown for the visible channels
  void setBands(QVector<Band> const &bands);
  // centred moving average over `samples` samples (1: raw data)
  void setSmoothingWindow(int samples);
  int  smoothingWindow() const;
  // dashed lines at the min / max / average of one channel (state name; its lines, e.g. all experiments of a comparison)
  void setOverlays(bool min, bool max, bool avg, QString const &channel);
  // shows [from, to] (epoch s) with the value axes fitted to it
  void setTimeRange(double from, double to);
  // time axis: wall clock (false) or time from the first sample shown (true)
  void setRelativeTime(bool relative);
  bool relativeTime() const;
  // no data (e.g. while it is read); text on the chart
  void clear(QString const &text);
  // left-drag marks a time range (annotationDrawn) instead of moving the view
  void setAnnotateMode(bool enabled);

  // legend as one row under the plot with the channel names only, always shown
  void setCompactLegend(bool compact);
  void zoomView(double mulX, double mulY);

signals:
  void controlModeChanged(ControlMode);
  void annotationDrawn(double x0, double x1); // epoch s
  // the value label follows the mouse (time x, epoch s, on the line `graph`) / was hidden
  void valueCursorMoved(double x, int graph);
  void valueCursorHidden();

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  void applyStyles();
  void applyStyle(int index, QVariantMap const &stateVariantMap, bool shadow = false);
  void updateData();
  void updateRange();
  void updateValueCursor(QPointF const &pos);
  // crosshair at (x, y) of the time / value axes; label and circle at sample `index` of `graph` (-1: none)
  void showCursor(double x, double y, int graph, int index);
  void initGraphs(uint16_t graphCount);
  void mouseEventHeadler(QMouseEvent *event);
  void wheelEventHandler(QWheelEvent *event);

  void onDataChanged(); // cheap
  void onRenderTick();  // throttled update

  Logger *m_logger;

  // Convert range of the screen(pixels) into secs and states units (temps, %, etc)
  // rect - input 2d range in pixels
  // return 2d range in secs and states units
  [[nodiscard]] QRectF mouseToCoord(QRectF const &rect) const;

  // Get current visible 2d range in secs and states units
  [[nodiscard]] QRectF getRange() const;

  void setPlotRange(QRectF const &rect);
  void zoomViewAt(QPointF const &pixelPos, double mulX, double mulY);

  QCustomPlot                      *m_plot{nullptr};
  std::shared_ptr<DataHolder const> m_data;
  QMetaObject::Connection           m_updateDataConnection;

  // Legend
  DataViewerLegend m_legendController;
  QVariantList     m_stateListVariant;

  QCPItemStraightLine *m_cursor{nullptr};  // crosshair: vertical
  QCPItemStraightLine *m_cursorH{nullptr}; // and horizontal line
  QCPItemTracer       *m_tracer{nullptr}; // circle on the point the value label refers to
  QCPItemText         *m_valueText{nullptr};

  QVector<QCPAbstractItem *> m_regionItems;
  bool                       m_annotateMode{false};

  // variable rule bands and min / max / average lines, rebuilt when the data or the styles change
  void                       rebuildDecorations();
  QVector<QCPAbstractItem *> m_decorationItems;
  QVector<Band>              m_bands;
  int                        m_smoothing{1};
  bool                       m_relativeTime{false};
  QSharedPointer<QCPAxisTicker> m_clockTicker;
  QSharedPointer<QCPAxisTicker> m_elapsedTicker;
  void                       updateTimeOrigin();
  bool                       m_overlayMin{false};
  QString                    m_overlayChannel{QStringLiteral("temp")};
  bool                       m_overlayMax{false};
  bool                       m_overlayAvg{false};

  QCPItemText *m_textItem;

  QRectF      m_currentView;
  ControlMode m_controlMode;
  ControlMode m_controlModeOnPress;
  bool        m_labelsVisible{false};
  bool        m_valuesVisible{false};
  QPointF     m_pointPress;
  QPointF     m_pointRelease;

  bool         m_compactLegend{false};
  bool         m_useLogAxis;
  bool         m_graphsStale{false}; // the log axis channels changed: graphs made again with the next data
  QVector<int> m_logAxisDataIndexes;

  QTimer           m_renderTimer;
  std::atomic_bool m_dirtyForUi{false}; // or just bool if same thread

  // bool m_isCursorActive{false};
};

inline QCustomPlot *DataViewer::getPlot() {
  return m_plot;
}

inline bool DataViewer::labelsVisible() const {
  return m_labelsVisible;
}

inline bool DataViewer::relativeTime() const {
  return m_relativeTime;
}

inline QVector<int> DataViewer::logAxisIndexes() const {
  return m_logAxisDataIndexes;
}

inline bool DataViewer::hasLogAxis() const {
  return m_useLogAxis;
}

inline QVariantList DataViewer::styles() const {
  return m_stateListVariant;
}

inline int DataViewer::smoothingWindow() const {
  return m_smoothing;
}

inline bool DataViewer::valuesVisible() const {
  return m_valuesVisible;
}

inline QRectF DataViewer::getCurrentView() const {
  return m_currentView;
}

class DataViewerDialog : public QDialog {
  Q_OBJECT
public:
  explicit DataViewerDialog(
    Logger *logger, std::shared_ptr<DataHolder const> interval, QVariantList style, QWidget *parent = nullptr);
};

} // namespace deepvaccontrol

#endif /* DATAVIEWER_H */
