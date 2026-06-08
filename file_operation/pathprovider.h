#ifndef PATHPROVIDER_H
#define PATHPROVIDER_H
#include <QObject>

#include "logger.h"

namespace control2 {
class PathProvider : public QObject {
  Q_OBJECT
public:
  explicit PathProvider(Logger *logger, QObject *parent = nullptr) : QObject{parent}, m_logger{logger} {
  }
  virtual ~PathProvider() = default;

  // Получение пути, который может быть задан вручную или через блокирующий диалог
  virtual QString getPath(QWidget *parent = nullptr, QString const &filter = QString()) {
    return {};
  };
  virtual void getPathAsync(QWidget *parent, QString const &filter, std::function<void(QString const &)> callback) {
  }

  Logger *getLogger() {
    return m_logger;
  }

private:
  Logger *m_logger;
};

} // namespace control2

#endif // PATHPROVIDER_H
