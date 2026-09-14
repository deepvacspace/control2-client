#ifndef CHAMBERTWINSIM_H
#define CHAMBERTWINSIM_H

#include <QString>
#include <QStringList>
#include <QVector>
#include <array>

#include "gruonnxmodel.h"

namespace control2::ai {

// Maps the GRU plant model's named input features to their live/simulated
// values for one timestep row.
QVector<float> buildTwinFeatureRow(QStringList const &featureNames, double temp, double tempRef,
                                    double previousTemp, double dtS, double u, double uP, double uI, double uD,
                                    double kp, double ki, double kd);

constexpr double U_MIN{-1.0};
constexpr double U_MAX{1.0};
constexpr double CONTROL_FEATURE_SCALE{100.0};
constexpr double PID_I_REVERSE_MUL{0.333};
constexpr double PID_PERIOD_S{0.1};
constexpr double TAIL_WINDOW_S{300.0};
constexpr double NEAR_BAND{2.0};
constexpr double SETTLE_BAND{0.5};
constexpr double W_TAIL_MAE{1.0};
constexpr double W_OVERSHOOT_MAX{10.0};
constexpr double W_TAIL_STD{0.5};
constexpr double W_FINAL_ERROR{0.5};
constexpr double W_INVALID{1'000'000.0};
constexpr double MAX_ABS_TEMP{100.0};

constexpr double KP_MIN{1.0};
constexpr double KP_MAX{50.0};
constexpr double KI_MIN{1.0};
constexpr double KI_MAX{1000.0};
constexpr double KD_MIN{1.0};
constexpr double KD_MAX{20.0};

constexpr double DEFAULT_KP{7.0};
constexpr double DEFAULT_KI{700.0};
constexpr double DEFAULT_KD{10.0};
constexpr double DEFAULT_START_TEMP{27.0};
constexpr double DEFAULT_TARGET_TEMP{0.0};
constexpr double DEFAULT_DURATION_S{1200.0};
constexpr double DEFAULT_DT_S{2.0};

struct SimParams {
  double kp{DEFAULT_KP};
  double ki{DEFAULT_KI};
  double kd{DEFAULT_KD};
  double startTemp{DEFAULT_START_TEMP};
  double targetTemp{DEFAULT_TARGET_TEMP};
  double durationS{DEFAULT_DURATION_S};
  double dtS{DEFAULT_DT_S};
  double initialP{0.0};
  double initialI{0.0};
  double initialD{0.0};
};

struct TrajectoryPoint {
  double elapsedS{0.0};
  double temp{0.0};
  double tempRef{0.0};
  double error{0.0};
  double u{0.0};
  double uP{0.0};
  double uI{0.0};
  double uD{0.0};
  double predDelta{0.0};
};

struct SimMetrics {
  bool    valid{true};
  QString invalidReason;
  double  cost{0.0};
  double  tailMae{0.0};
  double  tailBias{0.0};
  double  tailStd{0.0};
  double  overshootMax{0.0};
  double  overshootRmse{0.0};
  double  finalError{0.0};
  double  finalAbsError{0.0};
  double  timeToNearS{0.0};
  double  timeToSettleS{0.0};
  double  maeFull{0.0};
  double  minTemp{0.0};
  double  maxTemp{0.0};
  double  endTemp{0.0};
};

class CodesysDiff {
public:
  double update(double value);

  double prevValue{0.0};
  double filterOut{0.0};
  double out{0.0};

private:
  double m_dc{0.995};
};

class ChamberPid {
public:
  explicit ChamberPid(double uMin = U_MIN, double uMax = U_MAX, double iReverseMul = PID_I_REVERSE_MUL);

  // Returns {u, u_p, u_i, u_d}.
  std::array<double, 4> step(
    bool enable, double xTarget, double xMeasured, double pCoef, double iCoef, double dCoef, double diffOut);

  double pPart{0.0};
  double iPart{0.0};
  double dPart{0.0};

private:
  double m_uMin;
  double m_uMax;
  double m_iReverseMul;
};


class ChamberTwinSim {
public:
  explicit ChamberTwinSim(GruOnnxModel *model);

  bool begin(SimParams const &params, QString *errorOut = nullptr);
  bool stepOnce(TrajectoryPoint &pointOut, QString *errorOut = nullptr);
  bool isFinished() const;
  int  stepsDone() const;
  int  totalSteps() const;

  void setPid(double kp, double ki, double kd);

  double currentTemp() const;
  double previousTemp() const;
  double kp() const;
  double ki() const;
  double kd() const;
  double targetTemp() const;
  double pidIntegral() const;
  double diffFilterOut() const;
  double elapsedS() const;

  SimMetrics                     metrics() const;
  QVector<TrajectoryPoint> const &trajectory() const;

private:
  GruOnnxModel  *m_model;
  SimParams      m_params;
  ChamberPid     m_pid;
  CodesysDiff    m_diff;
  QStringList    m_featureNames;
  int            m_windowSteps{60};
  int            m_nFeatures{10};
  QVector<float> m_featureWindow;
  double         m_currentTemp{0.0};
  double         m_previousTemp{0.0};
  int            m_step{0};
  int            m_totalSteps{0};
  bool           m_valid{true};
  QString        m_invalidReason;

  QVector<TrajectoryPoint> m_trajectory;
};

}

#endif
