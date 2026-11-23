#include "filehandler.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <io.h>
#include <windows.h>
#else
#include <cstdio>
#include <unistd.h>
#endif

namespace deepvaccontrol {

namespace {
// the written bytes reach the disk, not only the system cache
void syncToDisk(QFile &file) {
#ifdef Q_OS_WIN
  FlushFileBuffers(reinterpret_cast<HANDLE>(_get_osfhandle(file.handle())));
#else
  ::fsync(file.handle());
#endif
}

// atomic replacement of `to` by `from` (same folder)
bool replaceFile(QString const &from, QString const &to) {
#ifdef Q_OS_WIN
  return 0 != MoveFileExW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(from).utf16()),
                          reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(to).utf16()),
                          MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
#else
  return 0 == std::rename(QFile::encodeName(from).constData(), QFile::encodeName(to).constData());
#endif
}
}


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

  // written to a temporary file next to the target and swapped in only when complete: a crash or a power cut
  // leaves the previous file or the new one, never a half-written one
  QString const tmpPath{filePath + QStringLiteral(".tmp")};
  QFile         file{tmpPath};
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    logger->stream(Logger::Level::WAR) << "Error opening file for writing:" << tmpPath;
    return false;
  }
  if (!writeData(file) || !file.flush()) {
    logger->stream(Logger::Level::WAR) << "Error while writing data to file:" << filePath;
    file.close();
    QFile::remove(tmpPath);
    return false;
  }
  syncToDisk(file);
  file.close();
  if (!replaceFile(tmpPath, filePath)) {
    logger->stream(Logger::Level::WAR) << "Error while replacing" << filePath << "with the new version";
    QFile::remove(tmpPath);
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

}
