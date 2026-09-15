#ifndef STREAMBUFFER_H
#define STREAMBUFFER_H

#include <QObject>

#include "logger.h"
#include "utils.h"

namespace deepvaccontrol {
class StreamBuffer : public QObject {
  Q_OBJECT

  constexpr static quint8 const PACKAGE_HEADER[4]{0xAA, 0x55, 0xBB, 0x77};

public:
  explicit StreamBuffer(Logger *logger, QObject *parent = nullptr);
  void       addData(QByteArray const &newData);
  QByteArray getPackage();

  ~StreamBuffer() override = default;

  static QByteArray makePackage(QByteArray const &data);

private:
  Logger    *m_logger;
  QByteArray m_buffer;
};

inline QByteArray StreamBuffer::makePackage(QByteArray const &data) {
  QByteArray packet;

  // little-endian
  quint16 const body_size = data.size();
  packet.push_back(body_size & 0xFF);
  packet.push_back(body_size >> 8);

  packet.append(data);

  // calculate CRC16 of the whole packet except HEADER bytes
  quint16 const crc{utils::crc16(packet.data(), packet.size())};
  // add CRC16 to packet
  packet.push_back(crc >> 8);
  packet.push_back(crc & 0xFF);

  for (int i{sizeof(PACKAGE_HEADER) / sizeof(quint8) - 1}; i >= 0; --i) {
    packet.push_front(PACKAGE_HEADER[i]);
  }

  return packet;
}
} // namespace deepvaccontrol

#endif // STREAMBUFFER_H
