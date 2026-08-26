#include "ppoadvisormodel.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QObject>
#include <algorithm>
#include <cmath>

namespace control2 {

namespace {
double limit(double low, double x, double high) {
  return std::max(low, std::min(x, high));
}
} // namespace

PpoAdvisorModel::PpoAdvisorModel() : m_status{QObject::tr("Advisor model not loaded")} {}

bool PpoAdvisorModel::load(QString const &jsonPath) {
  m_loaded = false;
  m_layers.clear();

  if (!QFileInfo::exists(jsonPath)) {
    m_status = QObject::tr("Advisor model file not found: %1").arg(jsonPath);
    return false;
  }

  QFile file(jsonPath);
  if (!file.open(QIODevice::ReadOnly)) {
    m_status = QObject::tr("Could not open advisor model file: %1").arg(jsonPath);
    return false;
  }

  QJsonParseError     parseError{};
  QJsonDocument const doc = QJsonDocument::fromJson(file.readAll(), &parseError);
  file.close();
  if (QJsonParseError::NoError != parseError.error || !doc.isObject()) {
    m_status = QObject::tr("Failed to parse advisor model file: %1").arg(jsonPath);
    return false;
  }

  QJsonObject const obj = doc.object();

  m_tempMin    = obj.value("temp_min").toDouble(m_tempMin);
  m_tempMax    = obj.value("temp_max").toDouble(m_tempMax);
  m_targetTemp = obj.value("target_temp").toDouble(m_targetTemp);
  m_kpMin      = obj.value("kp_min").toDouble(m_kpMin);
  m_kpMax      = obj.value("kp_max").toDouble(m_kpMax);
  m_kiMin      = obj.value("ki_min").toDouble(m_kiMin);
  m_kiMax      = obj.value("ki_max").toDouble(m_kiMax);
  m_kdMin      = obj.value("kd_min").toDouble(m_kdMin);
  m_kdMax      = obj.value("kd_max").toDouble(m_kdMax);
  m_mpcHoldS   = obj.value("mpc_hold_s").toDouble(m_mpcHoldS);
  m_dtS        = obj.value("dt_s").toDouble(m_dtS);

  QJsonArray const layersArr = obj.value("actor_layers").toArray();
  if (layersArr.isEmpty()) {
    m_status = QObject::tr("Advisor model file has no actor_layers: %1").arg(jsonPath);
    return false;
  }

  for (QJsonValue const &layerVal : layersArr) {
    QJsonObject const layerObj = layerVal.toObject();
    Layer             layer;

    for (QJsonValue const &rowVal : layerObj.value("weight").toArray()) {
      std::vector<double> row;
      for (QJsonValue const &w : rowVal.toArray()) {
        row.push_back(w.toDouble());
      }
      layer.weight.push_back(std::move(row));
    }
    for (QJsonValue const &b : layerObj.value("bias").toArray()) {
      layer.bias.push_back(b.toDouble());
    }

    if (layer.weight.empty() || layer.bias.empty() || (layer.weight.size() != layer.bias.size())) {
      m_status = QObject::tr("Advisor model file has malformed layer data: %1").arg(jsonPath);
      return false;
    }

    m_layers.push_back(std::move(layer));
  }

  m_loaded = true;
  m_status = QObject::tr("Advisor model loaded (%1 layers)").arg(m_layers.size());
  return true;
}

bool PpoAdvisorModel::isLoaded() const {
  return m_loaded;
}

QString const &PpoAdvisorModel::statusMessage() const {
  return m_status;
}

double PpoAdvisorModel::tempMin() const {
  return m_tempMin;
}
double PpoAdvisorModel::tempMax() const {
  return m_tempMax;
}
double PpoAdvisorModel::targetTemp() const {
  return m_targetTemp;
}
double PpoAdvisorModel::kpMin() const {
  return m_kpMin;
}
double PpoAdvisorModel::kpMax() const {
  return m_kpMax;
}
double PpoAdvisorModel::kiMin() const {
  return m_kiMin;
}
double PpoAdvisorModel::kiMax() const {
  return m_kiMax;
}
double PpoAdvisorModel::kdMin() const {
  return m_kdMin;
}
double PpoAdvisorModel::kdMax() const {
  return m_kdMax;
}
double PpoAdvisorModel::mpcHoldS() const {
  return m_mpcHoldS;
}
double PpoAdvisorModel::dtS() const {
  return m_dtS;
}

std::vector<double> PpoAdvisorModel::forward(std::vector<double> const &input) const {
  std::vector<double> activation = input;

  for (std::size_t layerIdx{0U}; layerIdx < m_layers.size(); ++layerIdx) {
    Layer const         &layer = m_layers[layerIdx];
    std::vector<double>  next(layer.bias.size(), 0.0);

    for (std::size_t o{0U}; o < layer.weight.size(); ++o) {
      double sum{layer.bias[o]};
      std::vector<double> const &row = layer.weight[o];
      for (std::size_t i{0U}; i < row.size() && i < activation.size(); ++i) {
        sum += row[i] * activation[i];
      }
      // Tanh after every layer except the last (which produces the raw mean).
      next[o] = (layerIdx + 1 < m_layers.size()) ? std::tanh(sum) : sum;
    }

    activation = std::move(next);
  }

  return activation;
}

std::array<double, 3> PpoAdvisorModel::decide(std::array<double, 8> const &obs) const {
  if (!m_loaded) {
    return {0.0, 0.0, 0.0};
  }

  std::vector<double> const mean = forward(std::vector<double>(obs.begin(), obs.end()));

  std::array<double, 3> lo{m_kpMin, m_kiMin, m_kdMin};
  std::array<double, 3> hi{m_kpMax, m_kiMax, m_kdMax};
  std::array<double, 3> pid{};

  for (std::size_t i{0U}; i < 3U && i < mean.size(); ++i) {
    double const action = std::tanh(mean[i]);
    double       value   = lo[i] + (action + 1.0) / 2.0 * (hi[i] - lo[i]);
    value                = limit(lo[i], value, hi[i]);
    pid[i]                = std::floor(value + 0.5); // deepvac.pid.clip_pid rounding
  }

  return pid;
}

} // namespace control2
