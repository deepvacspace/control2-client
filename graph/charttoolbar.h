#ifndef CHARTTOOLBAR_H
#define CHARTTOOLBAR_H

#include <QFrame>
#include <QMap>

class QAction;
class QButtonGroup;
class QGraphicsOpacityEffect;
class QLabel;
class QTimer;
class QToolButton;

namespace deepvaccontrol {

/**
 * @brief The chart's tools, floating in the top left corner of the chart (over the value axis): a small handle that opens downwards.
 *
 * Collapsed, the handle shows the tool in use (with its name, tinted, when it is not Inspect). Opened, the tools are
 * listed below it: picking one, or the pointer leaving for 2 s, closes it again. Pinned, it is a strip beside the
 * chart, always open (the chart gets narrower instead of being covered).
 */
class ChartToolbar : public QFrame {
  Q_OBJECT
public:
  enum class Tool : uint8_t {
    INSPECT,  // drag pans, the tooltip follows the mouse
    BOX_ZOOM, // drag a box to zoom into it
    ZOOM,     // the zoom pad (time and values, in and out)
    PAN,      // the pan pad (four directions)
    ANNOTATE  // drag across the chart to label a time range
  };

  explicit ChartToolbar(QWidget *parent = nullptr);

  void               setTool(Tool tool);
  [[nodiscard]] Tool tool() const;
  void               setPinned(bool pinned);
  [[nodiscard]] bool pinned() const;
  void               setAnnotateEnabled(bool enabled);
  // the Labels and Tooltip toggles, kept in step with these actions
  void               bindToggles(QAction *labels, QAction *tooltip);

signals:
  void toolChanged(ChartToolbar::Tool tool);
  void fitRequested();
  void pinnedChanged(bool pinned);
  void sizeChanged(); // opened, closed or pinned: placed again
  void dragged(QPoint const &delta); // moved by its handle or border (not while pinned)

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void enterEvent(QEnterEvent *event) override;
  void leaveEvent(QEvent *event) override;

private:
  void         setExpanded(bool expanded);
  void         updateView();
  QToolButton *addButton(QString const &icon, QString const &tip, bool checkable);
  void         addSeparator();

  QToolButton            *m_handle{nullptr};
  QWidget                *m_tools{nullptr};
  QButtonGroup           *m_toolGroup{nullptr};
  QMap<Tool, QToolButton *> m_toolButtons;
  QToolButton            *m_labelsBtn{nullptr};
  QToolButton            *m_tooltipBtn{nullptr};
  QToolButton            *m_pinBtn{nullptr};
  QToolButton            *m_collapseBtn{nullptr};
  QGraphicsOpacityEffect *m_opacity{nullptr};
  QTimer                 *m_collapseTimer{nullptr};
  Tool                    m_tool{Tool::INSPECT};
  bool                    m_expanded{false};
  bool                    m_pinned{false};
  bool                    m_hovered{false};
  // dragging: from where (global), and whether it moved far enough to be a drag rather than a tap
  QPoint                  m_dragLast;
  QPoint                  m_dragStart;
  bool                    m_pressed{false};
  bool                    m_dragging{false};
  bool                    dragPress(QPoint const &global);
  bool                    dragMove(QPoint const &global);
  bool                    dragRelease();
};

/**
 * @brief Four buttons around the tool's icon, in the bottom left corner of the plot: zoom (time and values, in and
 * out) or pan (earlier, later, up, down). The icon in the middle goes back to Inspect.
 */
class ChartPad : public QFrame {
  Q_OBJECT
public:
  explicit ChartPad(QWidget *parent = nullptr);
  void setMode(ChartToolbar::Tool tool); // ZOOM or PAN

signals:
  void zoomRequested(double mulX, double mulY);
  void moveRequested(double addX, double addY);
  void closeRequested();

private:
  ChartToolbar::Tool m_mode{ChartToolbar::Tool::ZOOM};
  QToolButton       *m_up{nullptr};
  QToolButton       *m_down{nullptr};
  QToolButton       *m_left{nullptr};
  QToolButton       *m_right{nullptr};
  QToolButton       *m_center{nullptr};
};

}

#endif // CHARTTOOLBAR_H
