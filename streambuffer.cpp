#include "streambuffer.h"

#include "utils.h"

namespace deepvaccontrol {
StreamBuffer::StreamBuffer(Logger *logger, QObject *parent) : QObject{parent}, m_logger{logger} {
}

void StreamBuffer::addData(QByteArray const &newData) {
  m_buffer += newData;
}

QByteArray StreamBuffer::getPackage() {
  if (m_buffer.size() > 8) {
    quint16 bodySize;
    {
      QByteArray  sizeBytes = m_buffer.mid(4, 2);
      QDataStream sizeStream(&sizeBytes, QIODeviceBase::ReadOnly);
      sizeStream.setByteOrder(QDataStream::LittleEndian);
      sizeStream >> bodySize;
    }
    if ((8 + bodySize) <= m_buffer.size()) {
      quint16 const crc16 = utils::crc16(m_buffer.data() + 4, 2 + bodySize + 2);

      if (0x0 == crc16) {
        QByteArray const ret{m_buffer.mid(6, bodySize)};
        m_buffer.remove(0, 8 + bodySize);
        return ret;
      } else {
        m_logger->stream(Logger::Level::WAR) << "Wrong CRC" << QString::number(crc16, 16);
      }
    }
  }
  return {};
}
} // namespace deepvaccontrol
