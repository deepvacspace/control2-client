#include "utils.h"

#include <QAbstractButton>
#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QMessageBox>
#include <QProcess>

namespace control2::utils {

inline double g_ssf{1.0};

quint32 float2dword(float const float_value) {
  quint32 ret_value;
  std::memcpy(&ret_value, &float_value, sizeof(quint32));
  return ret_value;
}

float dword2float(quint32 const dword_value) {
  float ret_value;
  std::memcpy(&ret_value, &dword_value, sizeof(quint32));
  return ret_value;
}

void setBitOfDword(quint32 &dword_value, quint8 position, bool bit_value) {
  dword_value = (dword_value & ~(0x01 << position)) | (bit_value << position);
}

bool getBitOfDword(quint32 dword_value, quint8 position) {
  return static_cast<bool>((dword_value >> position) & 0x01);
}

bool getBitOfFloat(float float_value, quint8 position) {
  return utils::getBitOfDword(utils::float2dword(float_value), position);
}

void setBitOfFloat(float &float_value, quint8 position, bool bit_value) {
  quint32 buf = utils::float2dword(float_value);
  utils::setBitOfDword(buf, position, bit_value);
  float_value = utils::dword2float(buf);
}

double ssf() {
  return g_ssf;
}

void setSsf(double value) {
  g_ssf = value;
}

QString timeFromSec(long sec, bool withDay) {
  if (sec < 0) {
    sec = 0;
  }

  int d  = static_cast<int>(floor(sec / 86400));
  int h  = static_cast<int>(floor(sec % 86400 / 3600));
  int h2 = static_cast<int>(floor(sec / 3600));
  int m  = static_cast<int>(floor(sec % 3600 / 60));
  int s  = static_cast<int>(sec % 60);
  if (d && withDay) {
    return QString("%1д. %2:%3:%4")
      .arg(d)
      .arg(h, 1, 10, QChar('0'))
      .arg(m, 2, 10, QChar('0'))
      .arg(s, 2, 10, QChar('0'));
  }
  return QString("%1:%2:%3").arg(h2, 2, 10, QChar('0')).arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
}

QVariantMap parseJsonToVariant(QByteArray const &jsonRawData, QString const &hint) {
  QJsonParseError parseError;
  QJsonDocument   jsonDocument{QJsonDocument::fromJson(jsonRawData, &parseError)};

  if (QJsonParseError::ParseError::NoError != parseError.error) {
    qDebug() << QString("Failed to parse the JSON structure from '%1'").arg(hint);
    qDebug() << QString("Parsing error '%1' with offset '%2'").arg(parseError.errorString()).arg(parseError.offset);

    // todo: add emit error_parsing(parseError.errorString());
    return QVariantMap{};
  }
  QJsonObject jsonObject{jsonDocument.object()};

  return jsonObject.toVariantMap();
}

double limitValue(double const min, double const val, double const max) {
  double ret{(val < min) ? min : val};
  ret = (ret > max) ? max : ret;
  return ret;
}

QString readStyleFromFile(QString const &filepath) {
  QString text;
  QFile   file{filepath};
  if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    text = file.readAll();
    file.close();
  }
  return (text);
}

QPalette lightPalette() {
  QPalette palette;
  palette.setColor(QPalette::Window, QColor(240, 240, 240));
  palette.setColor(QPalette::WindowText, Qt::black);
  palette.setColor(QPalette::Base, Qt::white);
  palette.setColor(QPalette::AlternateBase, QColor(233, 233, 233));
  palette.setColor(QPalette::ToolTipBase, Qt::white);
  palette.setColor(QPalette::ToolTipText, Qt::black);
  palette.setColor(QPalette::Text, Qt::black);
  palette.setColor(QPalette::Button, QColor(240, 240, 240));
  palette.setColor(QPalette::ButtonText, Qt::black);
  palette.setColor(QPalette::BrightText, Qt::white);
  palette.setColor(QPalette::Link, QColor(0, 0, 255));
  palette.setColor(QPalette::Highlight, QColor(0, 120, 215));
  palette.setColor(QPalette::HighlightedText, Qt::white);
  palette.setColor(QPalette::Disabled, QPalette::Text, QColor(120, 120, 120));
  palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(120, 120, 120));
  return palette;
}

QPalette darkPalette() {
  QPalette palette;
  palette.setColor(QPalette::Window, QColor(45, 45, 45));
  palette.setColor(QPalette::WindowText, Qt::white);
  palette.setColor(QPalette::Base, QColor(30, 30, 30));
  palette.setColor(QPalette::AlternateBase, QColor(45, 45, 45));
  palette.setColor(QPalette::ToolTipBase, Qt::white);
  palette.setColor(QPalette::ToolTipText, Qt::white);
  palette.setColor(QPalette::Text, Qt::white);
  palette.setColor(QPalette::Button, QColor(53, 53, 53));
  palette.setColor(QPalette::ButtonText, Qt::white);
  palette.setColor(QPalette::BrightText, Qt::red);
  palette.setColor(QPalette::Link, QColor(42, 130, 218));
  palette.setColor(QPalette::Highlight, QColor(42, 130, 218));
  palette.setColor(QPalette::HighlightedText, Qt::black);
  palette.setColor(QPalette::Disabled, QPalette::Text, QColor(127, 127, 127));
  palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(127, 127, 127));
  return palette;
}

bool isDarkMode() {
  return QApplication::palette().color(QPalette::Window).lightness() < 128;
}

void applyDarkMode(bool enabled) {
  QApplication::setPalette(enabled ? darkPalette() : lightPalette());
}

int nLampToLux(int i) {
  static QVector<int> lum_values{0, 1333, 2666, 4000, 8000, 12000, 16000, 20000};
  if ((i < 1) || (i > 7)) {
    i = 0;
  }
  return lum_values[i];
}

QByteArray makePascalString(QString const &str) {
  QByteArray asciiData = str.toLatin1(); // Преобразуем QString в ASCII (один байт на символ)
  quint8 length = asciiData.size();      // Длина строки

  if (length > 255) {
    qWarning("String length exceeds Pascal string limit (255 characters). It will be truncated.");
    length    = 255; // Ограничиваем длину строки
    asciiData = asciiData.left(length);
  }

  QByteArray pascalString;
  pascalString.append(static_cast<quint8>(length)); // Добавляем длину как первый байт
  pascalString.append(asciiData);                   // Добавляем данные строки

  return pascalString;
}

QString fromPascalString(QByteArray &pstr) {
  if (pstr.isEmpty()) {
    return QString(); // Пустая Pascal-строка
  }

  quint8 const length{static_cast<quint8>(pstr[0])}; // Извлекаем длину строки (первый байт)

  if (length + 1 > pstr.size()) {
    qWarning("Invalid Pascal string: declared length exceeds available data.");
    return QString(); // Неверный формат Pascal-строки
  }

  QByteArray const asciiData = pstr.mid(1, length); // Извлекаем данные строки
  pstr.remove(0, 1 + length);
  // return QString::fromLatin1(asciiData); // Преобразуем в QString
  return QString::fromLocal8Bit(asciiData); // Преобразуем в QString
}

QByteArray makePascalStringPair(QString const &str, float value) {
  QByteArray  pair{makePascalString(str)};
  QDataStream stream(&pair, QIODeviceBase::Append);
  stream.setByteOrder(QDataStream::ByteOrder::LittleEndian);
  stream.setFloatingPointPrecision(QDataStream::FloatingPointPrecision::SinglePrecision);
  stream << value;
  return pair;
}

QString binaryString(char const *adr, int size, bool isLittleEndian) {
  char buf[size];
  std::copy(adr, adr + size, buf);
  if (!isLittleEndian) {
    std::reverse(buf, buf + size);
  }
  QString ret;
  for (int i = 0; i < size; ++i) {
    for (int j = 7; j >= 0; --j) {
      ret.append((buf[i] >> j & 0x01) ? '1' : '0');
    }
    if (i != (size - 1)) {
      ret.append(' ');
    }
  }
  return ret;
}

double calculateKey(quint64 epochSecTime) {
  QDateTime dateTime{QDateTime::fromSecsSinceEpoch(epochSecTime)};
  dateTime.setTime(QTime()); // reset time of timestamp to 00:00:00 (midnight)
  return static_cast<double>(dateTime.toSecsSinceEpoch());
}

QString calculateFileName(quint64 epochSecTime) {
  QDateTime dateTime{QDateTime::fromSecsSinceEpoch(utils::calculateKey(epochSecTime))};
  return dateTime.toString("yyyy-MM-dd_HH-mm-ss") + ".dat";
}

void showAiAdvisorStartPromptAndRun(QWidget *parent) {
  QMessageBox msgBox{parent};
  msgBox.setIcon(QMessageBox::Information);
  msgBox.setWindowTitle(QStringLiteral("AI Advisor"));
  msgBox.setText(QStringLiteral("The AI module will choose the best coefficients for the experiment."));
  msgBox.setStandardButtons(QMessageBox::Ok);
  msgBox.button(QMessageBox::Ok)->setText(QStringLiteral("Accept"));
  msgBox.exec();

  QString const advisorPath = QDir(QCoreApplication::applicationDirPath()).filePath("DeepvacAIAdvisor.exe");
  if (!QFileInfo::exists(advisorPath)) {
    QMessageBox::warning(parent,
                         QStringLiteral("AI Advisor"),
                         QStringLiteral("DeepvacAIAdvisor.exe was not found next to the application executable."));
    return;
  }

  QProcess::startDetached(advisorPath, {}, QCoreApplication::applicationDirPath());
}

QString toSuperscript(int exp) {
  constexpr static QChar s[10] = {u'⁰', u'¹', u'²', u'³', u'⁴', u'⁵', u'⁶', u'⁷', u'⁸', u'⁹'};
  if (exp == 0) {
    return QStringLiteral("⁰");
  }
  QString    out;
  bool const neg{exp < 0};
  if (neg) {
    exp = -exp;
  }
  do {
    int d = exp % 10;
    out.prepend(s[d]);
    exp /= 10;
  } while (exp);
  if (neg) {
    out.prepend(u'⁻');
  }
  return out;
}

// Scientific formatter: mantissa in [1,10); arbitrary integer exponent.
// `fixedWhenReadable` shows fixed for moderate values (0.1..1000).
QString formatScientific(double v, int frac, bool fixedWhenReadable) {
  // Non-finite -> placeholder
  if (!std::isfinite(v)) {
    return QStringLiteral("--");
  }

  // Zero: avoid log10(0) and "-0"
  if (qFuzzyIsNull(v)) {
    return QStringLiteral("0");
  }

  QLocale const loc{QLocale::c()};
  double const  av{std::fabs(v)};

  // Human-friendly fixed range
  if (fixedWhenReadable && (av >= 0.1) && (av < 1000.0)) {
    return loc.toString(v, 'f', frac);
  }

  // Exponent in base-10 and mantissa
  int          exp10{static_cast<int>(std::floor(std::log10(av)))};
  double const mant{v / std::pow(10.0, exp10)};

  // Round mantissa to `frac` and guard spillover to 10.0
  double const p10{std::pow(10.0, frac)};
  double       mant_r{std::round(mant * p10) / p10};
  if (std::fabs(mant_r) >= 10.0) {
    mant_r /= 10.0;
    ++exp10;
  }

  // Format mantissa, trim trailing zeros and dangling decimal point
  QString     m{loc.toString(mant_r, 'f', frac)};
  QChar const dec{'.'};
  while (m.endsWith(u'0')) {
    m.chop(1);
  }
  if (m.endsWith(dec)) {
    m.chop(1);
  }
  if (m == QStringLiteral("-0")) {
    m = QStringLiteral("0");
  }

  // Middle dot (∙) by default; можно заменить на × (u'×') если хотите
  return m + u'∙' + QStringLiteral("10") + toSuperscript(exp10);
}

// Convenience overload for float
QString formatScientific(float v, int frac, bool fixedWhenReadable) {
  return formatScientific(static_cast<double>(v), frac, fixedWhenReadable);
}

QString formatScientific(double v, int frac) {
  return formatScientific(v, frac, true);
}

QString formatScientific(float v, int frac) {
  return formatScientific(v, frac, true);
}

QString formatScientificRef(float v, int frac) {
  return QString("(%1)").arg(formatScientific(v, frac, true));
}

} // namespace control2::utils
