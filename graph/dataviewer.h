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

  enum class CursorMode : uint8_t {
    NONE,
    LEGEND_ONLY,
    LEGEND_CURSOR
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

  QCustomPlot *getPlot();

  void switchControlMode();
  void setControlMode(ControlMode mode);

  void       switchCursor();
  void       setCursorMode(CursorMode mode);
  CursorMode getCursorMode() const;

  void moveView(double addX, double addY);
  void zoomView(double mulX, double mulY);

signals:
  void controlModeChanged(ControlMode);
  void cursorPositionChanged(double);

private:
  void applyStyles();
  void applyStyle(int index, QVariantMap const &stateVariantMap, bool shadow = false);
  void updateData();
  void updateRange();
  void updateLegend();
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

  QCPItemStraightLine *m_cursor{nullptr};
  QCPItemTracer       *m_tracer{nullptr};
  double               m_lastTracerKey{0.0};

  QCPItemText *m_textItem;

  QRectF      m_currentView;
  ControlMode m_controlMode;
  ControlMode m_controlModeOnPress;
  CursorMode  m_cursorMode;
  QPointF     m_pointPress;
  QPointF     m_pointRelease;

  bool         m_useLogAxis;
  QVector<int> m_logAxisDataIndexes;

  QTimer           m_renderTimer;
  std::atomic_bool m_dirtyForUi{false}; // or just bool if same thread

  // bool m_isCursorActive{false};
};

inline QCustomPlot *DataViewer::getPlot() {
  return m_plot;
}

inline DataViewer::CursorMode DataViewer::getCursorMode() const {
  return m_cursorMode;
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
