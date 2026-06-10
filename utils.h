#ifndef UTILS_H
#define UTILS_H
#include <QByteArray>
#include <QJsonDocument>
#include <QPalette>
#include <QStringList>
#include <limits>

class QWidget;

namespace control2::utils {

constexpr inline float const floatNan{std::numeric_limits<float>::quiet_NaN()};

void   setSsf(double value);
double ssf(); // screen scaling factor

quint32     float2dword(float const float_value);
float       dword2float(quint32 const dword_value);
void        setBitOfDword(quint32 &dword_value, quint8 position, bool bit_value);
bool        getBitOfDword(quint32 dword_value, quint8 position);
void        setBitOfFloat(float &float_value, quint8 position, bool bit_value);
bool        getBitOfFloat(float float_value, quint8 position);
QString     timeFromSec(long sec, bool withDay = false);
QVariantMap parseJsonToVariant(QByteArray const &jsonRawData, QString const &hint);
double      limitValue(double const min, double const val, double const max);
QString     readStyleFromFile(QString const &filepath);
QPalette    lightPalette();
QPalette    darkPalette();
bool        isDarkMode();
void        applyDarkMode(bool enabled);
int         nLampToLux(int i);
QByteArray  makePascalString(QString const &str);
QString     fromPascalString(QByteArray &pstr);
QByteArray  makePascalStringPair(QString const &str, float value);
QString     binaryString(char const *adr, int size, bool isLittleEndian);
double      calculateKey(quint64 epochSecTime);
QString     calculateFileName(quint64 epochSecTime);
void        showAiAdvisorStartPromptAndRun(QWidget *parent);

QString toSuperscript(int exp); // Convert integer exponent to superscript string like "⁻¹²"
QString formatScientific(double v, int frac, bool fixedWhenReadable);
QString formatScientific(float v, int frac, bool fixedWhenReadable);
QString formatScientific(double v, int frac = 3);
QString formatScientific(float v, int frac = 3);
QString formatScientificRef(float v, int frac = 3);

template<typename t_type> concept CharSized = (sizeof(t_type) == sizeof(char));

template<typename CharSized> quint16 crc16(CharSized const *data, size_t length) {
  // Polynom value
  constexpr static quint16 const poly = 0x1021;
  // Initial value
  quint16                        crc  = 0xFFFF;

  // Loop through each byte of input data
  for (size_t i{0U}; i < length; ++i) {
    quint8 byte = static_cast<quint8>(data[i]);

    crc ^= static_cast<quint16>(byte) << 8;
    for (quint8 j{8}; j > 0U; j--) {
      if (crc & (1U << 15)) {
        crc <<= 1;
        crc ^= poly;
      } else {
        crc <<= 1;
      }
    }
  }

  return crc;
}

} // namespace control2::utils
#endif // UTILS_H
