#ifndef GRUONNXMODEL_H
#define GRUONNXMODEL_H

#include <QString>
#include <QStringList>
#include <QVector>
#include <memory>

#include "modelenvelope.h"

namespace deepvaccontrol {


class GruOnnxModel {
public:
  GruOnnxModel();
  ~GruOnnxModel();

  bool load(QString const &onnxPath, QString const &metadataPath);

  bool                isLoaded() const;
  int                 windowSteps() const;
  QStringList const  &featureNames() const;
  QString const      &statusMessage() const;

  // Operating range the twin was validated for: the metadata's optional
  // "valid_range" object (temp_min, temp_max, max_duration_s, kp_min ...
  // kd_max), otherwise the chamber's validated range (-70 ... 100 degC,
  // 20 min horizon, ChamberTwinSim's PID bounds).
  ai::ModelEnvelope const &envelope() const;

  bool predictDeltaT1(QVector<float> const &windowFlat, float &deltaOut);

  // Same as predictDeltaT1, for `batchSize` windows laid out back to back in
  // `windowsFlat` (row-major: [batchSize][windowSteps][nFeatures]). One ONNX
  // Runtime call regardless of batchSize.
  bool predictDeltaT1Batch(QVector<float> const &windowsFlat, int batchSize, QVector<float> &deltaOut);

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
  QStringList           m_featureNames;
  int                   m_windowSteps{60};
  ai::ModelEnvelope     m_envelope;
  bool                  m_loaded{false};
  QString               m_status;
};

}

#endif
