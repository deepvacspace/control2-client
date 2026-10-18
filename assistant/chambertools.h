#ifndef CHAMBERTOOLS_H
#define CHAMBERTOOLS_H

#include <QJsonObject>
#include <QObject>
#include <memory>

#include "toolregistry.h"

namespace deepvaccontrol {
struct ResHolder;
class GruOnnxModel;
} // namespace deepvaccontrol

namespace deepvaccontrol::assistant {

/**
 * @brief Tools about the chamber now, for the assistant: its live state, its settings and the digital twin.
 *
 * Read only, like AnalysisTools: they read the states, the settings and the running job and simulate with the
 * twin, but never send anything to the PLC, change a setting or start a job. A suggested PID is only a suggestion.
 */
class ChamberTools : public QObject {
  Q_OBJECT
public:
  explicit ChamberTools(ResHolder *resHolder, QObject *parent = nullptr);
  ~ChamberTools() override;

  void registerTools(ToolRegistry &registry);

  // tool implementations (public for tests)
  QJsonObject liveStatus(QJsonObject const &args);
  QJsonObject settings(QJsonObject const &args) const;
  QJsonObject predictSetpoint(QJsonObject const &args);
  QJsonObject suggestPid(QJsonObject const &args);

private:
  GruOnnxModel *model(QString &error); // loaded on first use
  // the Temperature PID of the band a setpoint falls in (as the PLC selects it); false without a PID table
  bool bandGains(double setpoint, double &kp, double &ki, double &kd, int &band) const;
  // the start of a simulation: the argument, else the chamber's temperature now; NaN when neither
  double startTemperature(QJsonObject const &args) const;

  ResHolder                    *m_resHolder;
  std::unique_ptr<GruOnnxModel> m_model;
  quint32                       m_alarmCode{0}; // the last alarm the PLC reported
  double                        m_alarmTime{0.0};
};

} // namespace deepvaccontrol::assistant

#endif // CHAMBERTOOLS_H
