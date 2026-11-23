// recordingtest: checks the day recordings (storage/parquet.cpp, storage/recordingfile.cpp) without the application.
//
//   recordingtest selftest              round trips in memory; exit code 0 when everything matches
//   recordingtest write <file>          writes a known day (3 channels, a NaN gap) for other readers to check
//   recordingtest read <file> [names]   prints what a recording holds, as JSON (columns in the order of names)
//   recordingtest bench <rows> <cols>   times writing and reading a day of that size
//   recordingtest holder                DataHolder's saves (background and direct) and loads, in ./data
//
// (ninja recordingtest; not in "all")

#include <QCoreApplication>
#include <QDataStream>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <cmath>
#include <limits>

#include <QDir>
#include <QThread>

#include "constants.h"
#include "dataholder.h"
#include "logger.h"
#include "parquet.h"
#include "recordingfile.h"
#include "stringpathprovider.h"
#include "utils.h"

using namespace deepvaccontrol;

namespace {

int failures{0};

void check(bool ok, QString const &what) {
  QTextStream(stdout) << (ok ? "ok   " : "FAIL ") << what << Qt::endl;
  if (!ok) {
    ++failures;
  }
}

double const NaN{std::numeric_limits<double>::quiet_NaN()};

void knownDay(QVector<double> &times, QVector<QVector<double>> &states) {
  double const start{1780000000.25}; // a quarter second: microseconds survive
  times.clear();
  states = QVector<QVector<double>>(3);
  for (int i{0}; i < 1000; ++i) {
    times.push_back(start + i);
    states[0].push_back(20.0 + 0.01 * i);
    states[1].push_back(i < 500 ? 25.0 : 40.0);
    states[2].push_back((i >= 300) && (i < 310) ? NaN : std::sin(i / 50.0) * 100.0);
  }
}

bool same(double a, double b) {
  return (std::isnan(a) && std::isnan(b)) || (a == b);
}

int selftest() {
  recordingfile::setChannels({{"temp", "Temperature, °C"}, {"temp_ref", "Setpoint, °C"}, {"temp_u", "Output, %"}});
  QVector<double>          times;
  QVector<QVector<double>> states;
  knownDay(times, states);

  QString          error;
  QByteArray const bytes = recordingfile::write(times, states, &error);
  check(!bytes.isEmpty() && parquet::looksLikeParquet(bytes), "writes a Parquet file " + error);

  QVector<double>          t2;
  QVector<QVector<double>> s2;
  check(recordingfile::readBytes(bytes, t2, s2, {}, &error), "reads it back " + error);
  bool timesMatch{t2.size() == times.size()};
  for (qsizetype i{0}; timesMatch && (i < times.size()); ++i) {
    timesMatch = std::abs(t2[i] - times[i]) < 1e-6;
  }
  check(timesMatch, "times to the microsecond");
  bool valuesMatch{s2.size() == states.size()};
  for (qsizetype c{0}; valuesMatch && (c < states.size()); ++c) {
    for (qsizetype i{0}; valuesMatch && (i < states[c].size()); ++i) {
      valuesMatch = same(s2[c][i], states[c][i]);
    }
  }
  check(valuesMatch, "values exactly, NaN included");

  // columns by name: another value list
  check(recordingfile::readBytes(bytes, t2, s2, {"temp_u", "pressure", "temp"}, &error), "reads with another value list");
  check((s2.size() == 3) && same(s2[0][5], states[2][5]) && std::isnan(s2[1][5]) && same(s2[2][5], states[0][5]),
        "columns found by name, a missing one is NaN");

  parquet::Table table;
  check(parquet::read(bytes, table) && (table.metadataValue("deepvac.format") == recordingfile::FORMAT)
          && table.metadataValue("deepvac.channels").contains("Setpoint"),
        "footer metadata");

  // an older .dat day
  QByteArray  legacy;
  QDataStream out(&legacy, QIODevice::WriteOnly);
  out.setFloatingPointPrecision(QDataStream::DoublePrecision);
  out << QVariantMap{} << times << states;
  check(recordingfile::readBytes(legacy, t2, s2, {}, &error) && (t2 == times) && (s2.size() == 3), "reads an older .dat day");

  // an empty day
  QByteArray const empty = recordingfile::write({}, QVector<QVector<double>>(3), &error);
  check(!empty.isEmpty() && recordingfile::readBytes(empty, t2, s2, {}, &error) && t2.isEmpty() && (s2.size() == 3), "an empty day");

  // not a recording
  check(!recordingfile::readBytes("PAR1 nonsense PAR1", t2, s2, {}, &error), "rejects a damaged file: " + error);

  QTextStream(stdout) << (failures ? "FAILED" : "all passed") << Qt::endl;
  return failures ? 1 : 0;
}

int writeKnown(QString const &path) {
  recordingfile::setChannels({{"temp", "Temperature, °C"}, {"temp_ref", "Setpoint, °C"}, {"temp_u", "Output, %"}});
  QVector<double>          times;
  QVector<QVector<double>> states;
  knownDay(times, states);
  QString          error;
  QByteArray const bytes = recordingfile::write(times, states, &error);
  QFile            file(path);
  if (bytes.isEmpty() || !file.open(QIODevice::WriteOnly) || (file.write(bytes) != bytes.size())) {
    QTextStream(stderr) << "not written: " << error << Qt::endl;
    return 1;
  }
  return 0;
}

int readFile(QString const &path, QStringList const &names) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    QTextStream(stderr) << file.errorString() << Qt::endl;
    return 1;
  }
  QByteArray const bytes = file.readAll();
  parquet::Table   table;
  QString          error;
  if (!parquet::read(bytes, table, &error)) {
    QTextStream(stderr) << "not read: " << error << Qt::endl;
    return 1;
  }
  QVector<double>          times;
  QVector<QVector<double>> states;
  recordingfile::readBytes(bytes, times, states, names.isEmpty() ? table.names : names, &error);
  QJsonObject result{{"rows", times.size()}, {"time_column", table.timeColumn}};
  if (!times.isEmpty()) {
    result["first_time_us"] = QString::number(table.timesUs.first());
    result["last_time_us"]  = QString::number(table.timesUs.last());
  }
  QJsonArray columns;
  QStringList const order = names.isEmpty() ? table.names : names;
  for (qsizetype c{0}; c < states.size(); ++c) {
    double sum{0.0};
    int    nans{0};
    for (double const v : states[c]) {
      if (std::isnan(v)) {
        ++nans;
      } else {
        sum += v;
      }
    }
    columns.push_back(QJsonObject{{"name", order.value(c)}, {"sum", sum}, {"nan", nans},
                                  {"first", states[c].isEmpty() || std::isnan(states[c].first()) ? QJsonValue() : states[c].first()}});
  }
  result["columns"] = columns;
  QJsonObject meta;
  for (auto const &[key, value] : table.metadata) {
    meta[key] = value;
  }
  result["metadata"] = meta;
  QTextStream(stdout) << QJsonDocument(result).toJson(QJsonDocument::Compact) << Qt::endl;
  return 0;
}

// the application's save path: samples, a background save, more samples and a direct save; then load
int holder() {
  recordingfile::setChannels({{"temp", "Temperature"}, {"temp_ref", "Setpoint"}, {"temp_u", "Output"}});
  Logger       logger{"", 5};
  double const now{static_cast<double>(QDateTime::currentSecsSinceEpoch())};
  double const key{utils::calculateKey(static_cast<quint64>(now))};
  QFile::remove(recordingfile::savePath(key));
  {
    DataHolder day(3, &logger, key);
    for (int i{0}; i < 50000; ++i) { // a big day: the background save takes a while
      day.addPoint(key + 1 + i * 0.5, {20.0 + i * 1e-4, 25.0, static_cast<double>(i % 100)});
    }
    day.save(true); // the autosave
    for (int i{50000}; i < 50010; ++i) {
      day.addPoint(key + 1 + i * 0.5, {20.0 + i * 1e-4, 25.0, static_cast<double>(i % 100)});
    }
    day.save(); // the day change / closing, while the background save may still run
  } // waits for the background save
  QString const path = recordingfile::readPath(key);
  check(path == recordingfile::savePath(key) && QFile::exists(path), "saved as " + path);
  DataHolder         loaded(3, &logger, key);
  StringPathProvider provider(path, &logger);
  check(loaded.loadFromFile(&provider), "DataHolder loads it");
  check(loaded.getTimes().size() == 50010, QStringLiteral("the newest save won: %1 samples").arg(loaded.getTimes().size()));
  check(loaded.getStates().size() == 3 && loaded.getStates()[2].last() == 9.0, "values by column");

  // an older .dat day is still read, through readPath
  double const       oldKey{key - 3 * 86400};
  QString const      legacy = INTERVALS_DIR + recordingfile::legacyFileName(oldKey);
  QDir().mkpath(INTERVALS_DIR);
  QFile              file(legacy);
  file.open(QIODevice::WriteOnly);
  QDataStream out(&file);
  out.setFloatingPointPrecision(QDataStream::DoublePrecision);
  out << QVariantMap{} << QVector<double>{oldKey + 10, oldKey + 11} << QVector<QVector<double>>{{1, 2}, {3, 4}, {5, 6}};
  file.close();
  check(recordingfile::readPath(oldKey) == legacy, "an older day's .dat is found");
  DataHolder         old(3, &logger, oldKey);
  StringPathProvider oldProvider(legacy, &logger);
  check(old.loadFromFile(&oldProvider) && (old.getTimes().size() == 2) && (old.getStates()[2][1] == 6.0), "and read");
  QStringList const days = recordingfile::dayFiles(RECORDINGS_DIR, INTERVALS_DIR);
  check(days.size() == 2 && days.first() == legacy && days.last() == path, "day list: " + days.join(", "));
  QTextStream(stdout) << (failures ? "FAILED" : "all passed") << Qt::endl;
  return failures ? 1 : 0;
}

int bench(int rows, int cols) {
  QVector<double>          times;
  QVector<QVector<double>> states(cols);
  for (int i{0}; i < rows; ++i) {
    times.push_back(1780000000.0 + i);
    for (int c{0}; c < cols; ++c) {
      states[c].push_back(c % 3 == 0 ? std::round(20.0 + 5.0 * std::sin(i / 600.0 + c) * 1000.0) / 1000.0 : (c % 3 == 1 ? 2.0 : i / 3600));
    }
  }
  QElapsedTimer timer;
  timer.start();
  QString          error;
  QByteArray const bytes = recordingfile::write(times, states, &error);
  qint64 const     writeMs{timer.elapsed()};
  timer.restart();
  QVector<double>          t2;
  QVector<QVector<double>> s2;
  recordingfile::readBytes(bytes, t2, s2, {}, &error);
  QTextStream(stdout) << rows << " rows x " << cols << " columns: " << bytes.size() / 1e6 << " MB ("
                      << static_cast<double>(bytes.size()) / rows << " B/row), write " << writeMs << " ms, read " << timer.elapsed()
                      << " ms" << Qt::endl;
  return 0;
}

}

int main(int argc, char *argv[]) {
  QCoreApplication  app(argc, argv);
  QStringList const args = app.arguments().mid(1);
  if (args.value(0) == "selftest") {
    return selftest();
  }
  if ((args.value(0) == "write") && (args.size() == 2)) {
    return writeKnown(args[1]);
  }
  if ((args.value(0) == "read") && (args.size() >= 2)) {
    return readFile(args[1], args.mid(2));
  }
  if (args.value(0) == "holder") {
    return holder();
  }
  if ((args.value(0) == "bench") && (args.size() == 3)) {
    return bench(args[1].toInt(), args[2].toInt());
  }
  QTextStream(stderr) << "usage: recordingtest selftest | write <file> | read <file> [names...] | bench <rows> <cols> | holder" << Qt::endl;
  return 2;
}
