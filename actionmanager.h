#ifndef ACTIONMANAGER_H
#define ACTIONMANAGER_H

#include <QObject>

#include "filehandler.h"
#include "logger.h"
#include "stringpathprovider.h"
#include <QVariantList>

namespace control2 {
class ActionManager
    : public QObject
    , public FileHandler {
  Q_OBJECT
public:
  enum class ActionType : quint8 {
    APP = 0U,
    PLC,
    ERR
  };
  explicit ActionManager(Logger *logger, QString const &fileStoragePath, QObject *parent = nullptr);
  void         addAction(ActionType type, QString const &msg);
  void         loadFromDisk();
  void         saveToDisk();
  QVariantList getNext();

signals:
  void added(QVariantList addedAction, bool);
  void allDisplayed();

private:
  Logger      *m_logger;
  QString      m_fileStoragePath;
  QVariantList m_actionList;
  quint32      m_displayCount;

  // FileHandler interface
protected:
  bool writeData(QFile &file) override;
  bool readData(QFile &file) override;
};

inline void ActionManager::loadFromDisk() {
  StringPathProvider path(m_fileStoragePath, m_logger);
  FileHandler::loadFromFile(&path);
}

inline void ActionManager::saveToDisk() {
  StringPathProvider path(m_fileStoragePath, m_logger);
  FileHandler::saveToFile(&path);
}
} // namespace control2

#endif // ACTIONMANAGER_H
