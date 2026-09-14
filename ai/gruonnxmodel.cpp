#include "gruonnxmodel.h"

#include <algorithm>

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QObject>

#ifdef CONTROL2_HAVE_ONNXRUNTIME
#include <onnxruntime_cxx_api.h>
#endif

namespace control2 {

#ifdef CONTROL2_HAVE_ONNXRUNTIME
struct GruOnnxModel::Impl {
  Ort::Env                      env{ORT_LOGGING_LEVEL_WARNING, "control2-ai"};
  std::unique_ptr<Ort::Session> session;
  Ort::MemoryInfo               memoryInfo{Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault)};
};
#else
struct GruOnnxModel::Impl {};
#endif

GruOnnxModel::GruOnnxModel() : m_impl{std::make_unique<Impl>()}, m_status{QObject::tr("Model not loaded")} {}

GruOnnxModel::~GruOnnxModel() = default;

bool GruOnnxModel::load(QString const &onnxPath, QString const &metadataPath) {
  m_loaded = false;

  QFile metaFile(metadataPath);
  if (!metaFile.open(QIODevice::ReadOnly)) {
    m_status = QObject::tr("Model metadata file not found: %1").arg(metadataPath);
    return false;
  }
  QJsonParseError      parseError{};
  QJsonDocument const  doc = QJsonDocument::fromJson(metaFile.readAll(), &parseError);
  metaFile.close();
  if (QJsonParseError::NoError != parseError.error || !doc.isObject()) {
    m_status = QObject::tr("Failed to parse model metadata file: %1").arg(metadataPath);
    return false;
  }

  QJsonObject const obj = doc.object();
  m_featureNames.clear();
  for (QJsonValue const &v : obj.value("feature_names").toArray()) {
    m_featureNames << v.toString();
  }
  m_windowSteps = obj.value("window_steps").toInt(60);

  if (m_featureNames.isEmpty()) {
    m_status = QObject::tr("Metadata file has no feature_names: %1").arg(metadataPath);
    return false;
  }

  if (!QFileInfo::exists(onnxPath)) {
    m_status = QObject::tr("Model file not found: %1").arg(onnxPath);
    return false;
  }

#ifdef CONTROL2_HAVE_ONNXRUNTIME
  try {
    Ort::SessionOptions sessionOptions;
    sessionOptions.SetIntraOpNumThreads(1);
#ifdef _WIN32
    std::wstring const widePath{onnxPath.toStdWString()};
    m_impl->session = std::make_unique<Ort::Session>(m_impl->env, widePath.c_str(), sessionOptions);
#else
    m_impl->session = std::make_unique<Ort::Session>(m_impl->env, onnxPath.toUtf8().constData(), sessionOptions);
#endif
  } catch (Ort::Exception const &ex) {
    m_status = QObject::tr("Failed to load ONNX model: %1").arg(ex.what());
    return false;
  }

  m_loaded = true;
  m_status = QObject::tr("Model loaded (%1 steps x %2 features)").arg(m_windowSteps).arg(m_featureNames.size());
  return true;
#else
  m_status = QObject::tr(
    "ONNX Runtime was not linked at build time. Place the SDK in libs/onnxruntime and rebuild the project.");
  return false;
#endif
}

bool GruOnnxModel::isLoaded() const {
  return m_loaded;
}

int GruOnnxModel::windowSteps() const {
  return m_windowSteps;
}

QStringList const &GruOnnxModel::featureNames() const {
  return m_featureNames;
}

QString const &GruOnnxModel::statusMessage() const {
  return m_status;
}

bool GruOnnxModel::predictDeltaT1(QVector<float> const &windowFlat, float &deltaOut) {
#ifdef CONTROL2_HAVE_ONNXRUNTIME
  if (!m_loaded || !m_impl->session) {
    m_status = QObject::tr("Model not loaded");
    return false;
  }

  int64_t const nFeatures = m_featureNames.size();
  int64_t const shape[3]{1, m_windowSteps, nFeatures};

  if (windowFlat.size() != m_windowSteps * nFeatures) {
    m_status = QObject::tr("Invalid feature window size for prediction");
    return false;
  }

  try {
    Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
      m_impl->memoryInfo, const_cast<float *>(windowFlat.constData()), windowFlat.size(), shape, 3);

    char const *inputNames[]{"feature_window"};
    char const *outputNames[]{"temp_delta_t1"};

    auto outputTensors =
      m_impl->session->Run(Ort::RunOptions{nullptr}, inputNames, &inputTensor, 1, outputNames, 1);

    deltaOut = *outputTensors.front().GetTensorMutableData<float>();
    return true;
  } catch (Ort::Exception const &ex) {
    m_status = QObject::tr("ONNX prediction error: %1").arg(ex.what());
    return false;
  }
#else
  Q_UNUSED(windowFlat);
  deltaOut = 0.0f;
  m_status = QObject::tr("ONNX Runtime was not linked at build time");
  return false;
#endif
}

bool GruOnnxModel::predictDeltaT1Batch(QVector<float> const &windowsFlat, int batchSize, QVector<float> &deltaOut) {
#ifdef CONTROL2_HAVE_ONNXRUNTIME
  if (!m_loaded || !m_impl->session) {
    m_status = QObject::tr("Model not loaded");
    return false;
  }

  int64_t const nFeatures = m_featureNames.size();
  int64_t const shape[3]{batchSize, m_windowSteps, nFeatures};

  if (windowsFlat.size() != static_cast<qsizetype>(batchSize) * m_windowSteps * nFeatures) {
    m_status = QObject::tr("Invalid feature window size for batched prediction");
    return false;
  }

  try {
    Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
      m_impl->memoryInfo, const_cast<float *>(windowsFlat.constData()), windowsFlat.size(), shape, 3);

    char const *inputNames[]{"feature_window"};
    char const *outputNames[]{"temp_delta_t1"};

    auto outputTensors =
      m_impl->session->Run(Ort::RunOptions{nullptr}, inputNames, &inputTensor, 1, outputNames, 1);

    float const *outData = outputTensors.front().GetTensorMutableData<float>();
    deltaOut.resize(batchSize);
    std::copy(outData, outData + batchSize, deltaOut.begin());
    return true;
  } catch (Ort::Exception const &ex) {
    m_status = QObject::tr("ONNX batched prediction error: %1").arg(ex.what());
    return false;
  }
#else
  Q_UNUSED(windowsFlat);
  Q_UNUSED(batchSize);
  deltaOut.clear();
  m_status = QObject::tr("ONNX Runtime was not linked at build time");
  return false;
#endif
}

}
