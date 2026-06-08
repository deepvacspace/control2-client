#ifndef LOGGER_H
#define LOGGER_H

#include <QDebug>
#include <QFile>
#include <QMutex>
#include <QObject>
#include <QQueue>
#include <QTextStream>
#include <source_location>
// #include <sstream>

namespace control2 {
class Logger : public QObject {
  Q_OBJECT
public:
  enum class Level {
    DEB,
    INFO,
    WAR,
    ERR
  };

  explicit Logger(QString const &logFilePath, quint32 flushInterval = 5, QObject *parent = nullptr);
  ~Logger() override;

  void setLogLevel(Level newLevel);

  void log(Level level, QString const &message, std::source_location location = std::source_location::current());

  class LogStream {
  public:
    LogStream(Logger &logger, Level level, std::source_location location = std::source_location::current())
        : m_logger(logger), m_level(level), m_location(location), m_debugStream(&m_buffer) {
    }

    ~LogStream() {
      m_logger.log(m_level, m_buffer, m_location);
    }

    template<typename t_type> LogStream &operator<<(t_type const &value) {
      m_debugStream.noquote() << value;
      return *this;
    }

  private:
    Logger              &m_logger;
    Level             m_level;
    std::source_location m_location;
    QString              m_buffer;
    QDebug               m_debugStream;
  };

  LogStream stream(Level level, std::source_location location = std::source_location::current()) {
    return LogStream(*this, level, location);
  }

private:
  void    flush(); // Записывает накопленные логи в файл
  QString formatMessage(Level level, QString const &message, std::source_location const &location);

  QFile           m_logFile;
  QTextStream     m_logStream;
  QQueue<QString> m_logQueue;
  QMutex          m_mutex;

  Level m_currentLevel;
  quint32  m_flushThreshold; // Порог количества записей для автофлаша
  bool     m_logToStdOut;
  bool     m_logToFile;
};

inline void Logger::setLogLevel(Level newLevel)
{
  m_currentLevel = newLevel;
}
} // namespace control2

#endif // LOGGER_H
