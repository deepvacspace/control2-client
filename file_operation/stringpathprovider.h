#ifndef STRINGPATHPROVIDER_H
#define STRINGPATHPROVIDER_H
#include "pathprovider.h"

namespace control2 {
class StringPathProvider : public PathProvider {
public:
  StringPathProvider(QString const &path, Logger *logger, QObject *parent = nullptr)
      : PathProvider{logger, parent}, m_predefinedPath{path} {
  }
  ~StringPathProvider() = default;

  // PathProvider interface
  QString getPath(QWidget *parent, QString const &filter) override {
    return m_predefinedPath;
  }

private:
  QString m_predefinedPath;
};
} // namespace control2

#endif // STRINGPATHPROVIDER_H
