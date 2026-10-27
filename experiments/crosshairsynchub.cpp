#include "crosshairsynchub.h"

#include "analysischart.h"

namespace deepvaccontrol {

CrosshairSyncHub::CrosshairSyncHub(QObject *parent) : QObject{parent} {
}

void CrosshairSyncHub::registerChart(AnalysisChart *chart) {
  if (chart && !m_charts.contains(chart)) {
    m_charts.push_back(chart);
  }
}

void CrosshairSyncHub::unregisterChart(AnalysisChart *chart) {
  m_charts.removeAll(chart);
}

void CrosshairSyncHub::setEnabled(bool enabled) {
  if (enabled == m_enabled) {
    return;
  }
  m_enabled = enabled;
  if (!m_enabled) {
    forEachOther(nullptr, [](AnalysisChart *chart) {
      chart->hideSyncCrosshair();
    });
  }
  emit enabledChanged(m_enabled);
}

void CrosshairSyncHub::broadcast(AnalysisChart *source, double x, double y, QString const &series) {
  if (!m_enabled) {
    return;
  }
  forEachOther(source, [x, y, &series](AnalysisChart *chart) {
    chart->showSyncCrosshair(x, y, series);
  });
}

void CrosshairSyncHub::broadcastHide(AnalysisChart *source) {
  if (!m_enabled) {
    return;
  }
  forEachOther(source, [](AnalysisChart *chart) {
    chart->hideSyncCrosshair();
  });
}

template <typename Op>
void CrosshairSyncHub::forEachOther(AnalysisChart *source, Op op) {
  if (m_busy) {
    return;
  }
  m_busy = true;
  m_charts.removeAll(nullptr);
  for (auto const &chart : std::as_const(m_charts)) {
    if (chart && (chart != source) && chart->isVisible()) {
      op(chart.data());
    }
  }
  m_busy = false;
}

}
