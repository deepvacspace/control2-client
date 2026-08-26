#ifndef GRUONNXMODEL_H
#define GRUONNXMODEL_H

#include <QString>
#include <QStringList>
#include <QVector>
#include <memory>

namespace control2 {


class GruOnnxModel {
public:
  GruOnnxModel();
  ~GruOnnxModel();

  bool load(QString const &onnxPath, QString const &metadataPath);

  bool                isLoaded() const;
  int                 windowSteps() const;
  QStringList const  &featureNames() const;
  QString const      &statusMessage() const;

  bool predictDeltaT1(QVector<float> const &windowFlat, float &deltaOut);

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
  QStringList           m_featureNames;
  int                   m_windowSteps{60};
  bool                  m_loaded{false};
  QString               m_status;
};

}

#endif
