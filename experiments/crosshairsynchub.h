#ifndef CROSSHAIRSYNCHUB_H
#define CROSSHAIRSYNCHUB_H

#include <QObject>
#include <QPointer>
#include <QVector>

namespace deepvaccontrol {

class AnalysisChart;

/**
 * @brief Mirrors the crosshair of the chart under the mouse to every other visible
 * registered chart (same elapsed time), when enabled.
 */
class CrosshairSyncHub : public QObject {
  Q_OBJECT
public:
  explicit CrosshairSyncHub(QObject *parent = nullptr);

  void registerChart(AnalysisChart *chart);
  void unregisterChart(AnalysisChart *chart);

  void               setEnabled(bool enabled);
  [[nodiscard]] bool isEnabled() const;

  // series: label of the line hovered in the source chart, empty for none
  void broadcast(AnalysisChart *source, double x, double y, QString const &series);
  void broadcastHide(AnalysisChart *source);

signals:
  void enabledChanged(bool enabled);

private:
  template <typename Op>
  void forEachOther(AnalysisChart *source, Op op);

  QVector<QPointer<AnalysisChart>> m_charts;
  bool                             m_enabled{false};
  bool                             m_busy{false};
};

inline bool CrosshairSyncHub::isEnabled() const {
  return m_enabled;
}

}

#endif // CROSSHAIRSYNCHUB_H
