#ifndef PPOADVISORMODEL_H
#define PPOADVISORMODEL_H

#include <QString>
#include <array>
#include <vector>

namespace control2 {

// The trained PPO "advisor" policy: a small MLP (foresight's
// advisor/train_policy_ppo.py::ActorCritic actor network) that picks a PID
// triplet directly from a live observation of the chamber state. Weights
// are extracted (read-only, offline) from foresight's trained checkpoint
// (advisor/policy_ppo_v5/policy.pt) into data/model/ppo_advisor.json --
// this class just does the forward pass, no PyTorch/ONNX dependency
// needed for a network this small.
//
// Deterministic inference only (matches the checkpoint's deployed
// behavior): action = tanh(actor(obs)), then de-normalized to real PID
// units and clipped/rounded to the checkpoint's bounds.
class PpoAdvisorModel {
public:
  PpoAdvisorModel();

  bool load(QString const &jsonPath);
  bool           isLoaded() const;
  QString const &statusMessage() const;

  double tempMin() const;
  double tempMax() const;
  double targetTemp() const;
  double kpMin() const;
  double kpMax() const;
  double kiMin() const;
  double kiMax() const;
  double kdMin() const;
  double kdMax() const;
  double mpcHoldS() const;
  double dtS() const;

  // obs, in this exact order (see foresight advisor/train_policy_ppo.py::VecTwinEnv::_obs):
  //   0: (temp - targetTemp) / scale                  scale = max(|tempMin|,|tempMax|,1e-6)
  //   1: (temp - previousTemp) / dtS / scale
  //   2: 2*(kp - kpMin)/(kpMax - kpMin) - 1
  //   3: 2*(ki - kiMin)/(kiMax - kiMin) - 1
  //   4: 2*(kd - kdMin)/(kdMax - kdMin) - 1
  //   5: 1.0 - decisionIdx / totalDecisions
  //   6: pidIntegral / iScale                          iScale = max(|uMin|,|uMax|,1e-6) = 1.0
  //   7: diffFilterOut / 5.0                            (CodesysDiff clip constant)
  // Returns {kp, ki, kd}, already de-normalized, clipped, and rounded to
  // the nearest integer (matches deepvac.pid.clip_pid).
  std::array<double, 3> decide(std::array<double, 8> const &obs) const;

private:
  struct Layer {
    std::vector<std::vector<double>> weight; // [out][in]
    std::vector<double>              bias;   // [out]
  };

  std::vector<double> forward(std::vector<double> const &input) const;

  std::vector<Layer> m_layers;
  double             m_tempMin{-5.0};
  double             m_tempMax{26.0};
  double             m_targetTemp{0.0};
  double             m_kpMin{1.0};
  double             m_kpMax{50.0};
  double             m_kiMin{1.0};
  double             m_kiMax{1000.0};
  double             m_kdMin{1.0};
  double             m_kdMax{20.0};
  double             m_mpcHoldS{20.0};
  double             m_dtS{2.0};
  bool               m_loaded{false};
  QString            m_status;
};

} // namespace control2

#endif // PPOADVISORMODEL_H
