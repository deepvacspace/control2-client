#include "filehandler.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>

namespace control2 {

bool FileHandler::saveToFile(PathProvider *pathProvider, QWidget *parent, QString const &filter) {
  QString path{pathProvider->getPath(parent, filter)};
  auto   *logger = pathProvider->getLogger();
  if (path.isEmpty()) {
    logger->stream(Logger::Level::WAR) << "Failed to save file:" << path << "is empty";
    return false;
  }
  return saveToFileImpl(path, logger);
}

void FileHandler::saveToFileAsync(PathProvider *pathProvider, QWidget *parent, QString const &filter) {
  auto *logger = pathProvider->getLogger();
  pathProvider->getPathAsync(parent, filter, [this, logger](QString const &path) {
    if (path.isEmpty()) {
      logger->stream(Logger::Level::WAR) << "Failed to save file:" << path << "is empty";
      return;
    }
    saveToFileImpl(path, logger);
  });
}

bool FileHandler::loadFromFile(PathProvider *pathProvider, QWidget *parent, QString const &filter) {
  QString path{pathProvider->getPath(parent, filter)};
  auto   *logger = pathProvider->getLogger();
  if (path.isEmpty()) {
    logger->stream(Logger::Level::WAR) << "Load operation canceled:" << path << "is empty";
    return false;
  }
  return loadFromFileImpl(path, logger);
}

void FileHandler::loadFromFileAsync(PathProvider *pathProvider, QWidget *parent, QString const &filter) {
  auto *logger = pathProvider->getLogger();
  pathProvider->getPathAsync(parent, filter, [this, logger](QString const &path) {
    if (path.isEmpty()) {
      logger->stream(Logger::Level::WAR) << "Load operation canceled: " << path << "is empty";
      return;
    }
    loadFromFileImpl(path, logger);
  });
}

bool FileHandler::saveToFileImpl(QString const &filePath, Logger *logger) {
  QFile file{filePath};
  if (!file.exists()) {
    logger->stream(Logger::Level::INFO) << "File for writing doesn't exist:" << filePath;
  }

  QFileInfo const fileInfo{filePath};
  QDir const      dir{fileInfo.dir()};
  if (!dir.exists()) {
    // Создаем директорию и все родительские директории, если их нет
    logger->stream(Logger::Level::INFO) << "Target dir to save(create) file doesn't exist:" << dir.path();
    if (!dir.mkpath(".")) {
      logger->stream(Logger::Level::ERR) << "Error while creating target for to save(create) file:" << dir.path();
      return false;
    }
  }

  if (!file.open(QIODevice::WriteOnly)) {
    logger->stream(Logger::Level::WAR) << "Error opening file for writing:" << filePath;
    return false;
  }
  if (!writeData(file)) {
    logger->stream(Logger::Level::WAR) << "Error while writing data to file:" << filePath;
    return false;
  }

  return true;
}

bool FileHandler::loadFromFileImpl(QString const &filePath, Logger *logger) {
  QFile file(filePath);
  if (!file.exists()) {
    logger->stream(Logger::Level::INFO) << "File for reading doesn't exist:" << filePath;
    return false;
  }
  if (!file.open(QIODevice::ReadOnly)) {
    logger->stream(Logger::Level::WAR) << "Error opening file for reading:" << filePath;
    return false;
  }
  if (!readData(file)) {
    logger->stream(Logger::Level::WAR) << "Error while reading data from file:" << filePath;
    return false;
  }

  return true;
}

} // namespace control2
