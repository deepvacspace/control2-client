#include "logger.h"

#include <QDateTime>
#include <QTimer>
#include <filesystem>

namespace control2 {
Logger::Logger(QString const &logFilePath, quint32 flushInterval, QObject *parent)
    : QObject{parent}
    , m_currentLevel{Level::INFO}
    , m_flushThreshold{flushInterval}
    , m_logToStdOut{true}
    , m_logToFile{false} {
  if (m_logToFile) {
    m_logFile.setFileName(logFilePath);

    if (!m_logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
      throw std::runtime_error("Failed to open log file: " + logFilePath.toStdString());
    }
    m_logStream.setDevice(&m_logFile);

    // Периодическая запись через QTimer
    auto *flushTimer = new QTimer(this);
    connect(flushTimer, &QTimer::timeout, this, &Logger::flush);
    flushTimer->start(5000); // Каждые 5 секунд
  }
}

Logger::~Logger() {
  if (m_logToFile) {
    // Финальная запись оставшихся сообщений
    flush();

    m_logFile.close();
  }
}

void Logger::log(Level level, QString const &message, std::source_location location) {
  QString formattedMessage = formatMessage(level, message, location);

  if (static_cast<int>(level) < static_cast<int>(m_currentLevel)) {
    return;
  }

  if (m_logToStdOut) {
    qDebug().noquote() << formattedMessage;
  }
  if (m_logToFile) {
    QMutexLocker locker(&m_mutex);
    m_logQueue.enqueue(formattedMessage);

    if (m_logQueue.size() >= static_cast<int>(m_flushThreshold)) {
      flush();
    }
  }
}

void Logger::flush() {
  QMutexLocker locker(&m_mutex);

  while (!m_logQueue.isEmpty()) {
    m_logStream << m_logQueue.dequeue() << Qt::endl;
  }

  m_logStream.flush();
}

QString Logger::formatMessage(Level level, QString const &message, std::source_location const &location) {
  QString levelStr;
  switch (level) {
    case Level::DEB:
      levelStr = "DEBUG";
      break;
    case Level::INFO:
      levelStr = "INFO";
      break;
    case Level::WAR:
      levelStr = "WARNING";
      break;
    case Level::ERR:
      levelStr = "ERROR";
      break;
  }

  QString const timeStr{QDateTime::currentDateTime().toString("HH:mm:ss")}; // yyyy-MM-dd
  QString const fileName{QString::fromStdString(std::filesystem::path(location.file_name()).filename().string())};
  QString       functionName{location.function_name()};
  functionName.replace("control2::", "");
  QString const locationInfo{QString("[%1:%2] [%3]").arg(fileName).arg(location.line()).arg(functionName)};

  return QString("[%1] [%2] %3 %4").arg(timeStr, levelStr, locationInfo, message);
}
} // namespace control2
