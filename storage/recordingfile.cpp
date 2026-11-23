#include "recordingfile.h"

#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMap>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QSaveFile>
#include <atomic>
#include <QVariantMap>
#include <cmath>
#include <limits>

#include "constants.h"
#include "parquet.h"

#ifndef DEEPVACCONTROL_VERSION
#define DEEPVACCONTROL_VERSION "dev"
#endif

namespace deepvaccontrol::recordingfile {

namespace {
QMutex           s_mutex;
QVector<Channel> s_channels;

std::atomic<quint64>   s_generation{0};
QMutex                 s_saveMutex;
QHash<QString, quint64> s_savedGeneration; // path -> newest generation written

// the whole day so far, rewritten every few minutes: level 6 is a fifth smaller than 3 (9 gains nothing)
constexpr int GZIP_LEVEL{6};

QDate dayOf(double epochSecTime) {
  return QDateTime::fromSecsSinceEpoch(static_cast<qint64>(std::floor(epochSecTime))).date();
}

// the older day files (DataHolder before Parquet): QDataStream of a header, times and states
bool readLegacy(QByteArray const &bytes, QVector<double> &times, QVector<QVector<double>> &states) {
  QDataStream in(bytes);
  in.setFloatingPointPrecision(QDataStream::DoublePrecision);
  QVariantMap header;
  in >> header >> times >> states;
  return QDataStream::Ok == in.status();
}
}

void setChannels(QVector<Channel> const &channels) {
  QMutexLocker const lock(&s_mutex);
  s_channels = channels;
}

QVector<Channel> channels() {
  QMutexLocker const lock(&s_mutex);
  return s_channels;
}

QStringList channelNames() {
  QStringList names;
  for (Channel const &channel : channels()) {
    names.push_back(channel.name);
  }
  return names;
}

QString dayText(double epochSecTime) {
  return dayOf(epochSecTime).toString(QStringLiteral("yyyy-MM-dd"));
}

QString fileName(double epochSecTime) {
  return dayText(epochSecTime) + QStringLiteral(".parquet");
}

QString legacyFileName(double epochSecTime) {
  return dayText(epochSecTime) + QStringLiteral("_00-00-00.dat");
}

QString savePath(double epochSecTime) {
  return RECORDINGS_DIR + fileName(epochSecTime);
}

QString readPath(double epochSecTime) {
  if (QString const path = savePath(epochSecTime); QFile::exists(path)) {
    return path;
  }
  if (QString const legacy = INTERVALS_DIR + legacyFileName(epochSecTime); QFile::exists(legacy)) {
    return legacy;
  }
  return {};
}

QStringList dayFiles(QString const &recordingsDir, QString const &legacyDir) {
  QMap<QString, QString> byDay; // yyyy-MM-dd -> path (sorted)
  auto isDay = [](QString const &name) { return QDate::fromString(name.left(10), QStringLiteral("yyyy-MM-dd")).isValid(); };
  QDir const legacy(legacyDir);
  for (QString const &name : legacy.entryList({QStringLiteral("*.dat")}, QDir::Files, QDir::Name)) {
    if (isDay(name)) {
      byDay.insert(name.left(10), legacy.filePath(name));
    }
  }
  QDir const recordings(recordingsDir);
  for (QString const &name : recordings.entryList({QStringLiteral("*.parquet")}, QDir::Files, QDir::Name)) {
    if (isDay(name)) {
      byDay.insert(name.left(10), recordings.filePath(name)); // the Parquet file wins
    }
  }
  return byDay.values();
}

bool readBytes(QByteArray const &bytes, QVector<double> &times, QVector<QVector<double>> &states, QStringList names,
               QString *error) {
  times.clear();
  states.clear();
  if (names.isEmpty()) {
    names = channelNames();
  }
  if (!parquet::looksLikeParquet(bytes)) {
    // an older .dat day: its states follow the value list of that time, by position
    if (!readLegacy(bytes, times, states)) {
      if (error) {
        *error = QStringLiteral("neither a Parquet nor a .dat recording");
      }
      return false;
    }
    return true;
  }
  parquet::Table table;
  if (!parquet::read(bytes, table, error)) {
    return false;
  }
  times.reserve(table.timesUs.size());
  for (qint64 const us : std::as_const(table.timesUs)) {
    times.push_back(static_cast<double>(us) / 1e6);
  }
  QHash<QString, qsizetype> byName;
  for (qsizetype c{0}; c < table.names.size(); ++c) {
    byName.insert(table.names[c], c);
  }
  if (names.isEmpty()) {
    names = table.names; // no value list known: the file's own columns
  }
  QVector<double> const missing(times.size(), std::numeric_limits<double>::quiet_NaN());
  states.reserve(names.size());
  for (QString const &name : std::as_const(names)) {
    auto const it = byName.constFind(name);
    states.push_back(it != byName.constEnd() ? table.columns[*it] : missing);
  }
  return true;
}

bool read(QString const &path, QVector<double> &times, QVector<QVector<double>> &states, QStringList names, QString *error) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    if (error) {
      *error = file.errorString();
    }
    return false;
  }
  return readBytes(file.readAll(), times, states, std::move(names), error);
}

QByteArray write(QVector<double> const &times, QVector<QVector<double>> const &states, QString *error) {
  QVector<Channel> known = channels();
  if (known.size() != states.size()) { // value list unknown or different: numbered columns
    known.clear();
    for (qsizetype i{0}; i < states.size(); ++i) {
      known.push_back({QStringLiteral("ch%1").arg(i), QString{}});
    }
  }
  parquet::Table table;
  table.timesUs.reserve(times.size());
  for (double const t : times) {
    table.timesUs.push_back(std::llround(t * 1e6));
  }
  QJsonArray described;
  for (qsizetype c{0}; c < states.size(); ++c) {
    table.names.push_back(known[c].name);
    QVector<double> column = states[c];
    column.resize(times.size(), std::numeric_limits<double>::quiet_NaN()); // a short column: NaN to the end
    table.columns.push_back(std::move(column));
    described.push_back(QJsonObject{{"name", known[c].name}, {"label", known[c].label}});
  }
  table.metadata = {
    {QStringLiteral("deepvac.format"), FORMAT},
    {QStringLiteral("deepvac.day"), times.isEmpty() ? QString{} : dayText(times.first())},
    {QStringLiteral("deepvac.channels"), QString::fromUtf8(QJsonDocument(described).toJson(QJsonDocument::Compact))},
    {QStringLiteral("deepvac.source"), QStringLiteral("DeepVac Control %1").arg(QStringLiteral(DEEPVACCONTROL_VERSION))},
  };
  return parquet::write(table, error, GZIP_LEVEL);
}

quint64 nextGeneration() {
  return ++s_generation;
}

bool saveDay(QString const &path, quint64 generation, QVector<double> const &times, QVector<QVector<double>> const &states,
             QString *error) {
  QByteArray const bytes = write(times, states, error); // compressed before taking the lock
  if (bytes.isEmpty()) {
    return false;
  }
  QMutexLocker const lock(&s_saveMutex);
  if (s_savedGeneration.value(path, 0) > generation) {
    return true; // a newer save got there first
  }
  QDir().mkpath(QFileInfo(path).absolutePath());
  QSaveFile file(path); // a temporary file swapped in when complete: never a half-written day
  if (!file.open(QIODevice::WriteOnly) || (file.write(bytes) != bytes.size()) || !file.commit()) {
    if (error) {
      *error = file.errorString();
    }
    return false;
  }
  s_savedGeneration.insert(path, generation);
  return true;
}

}
