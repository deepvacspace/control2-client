#ifndef RECORDINGFILE_H
#define RECORDINGFILE_H

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

namespace deepvaccontrol::recordingfile {

/**
 * @brief The full-day recording: every sample received in a day, one Apache Parquet file per day
 * (data/recordings/yyyy-MM-dd.parquet, see parquet.h), format "deepvac.recording/1":
 *
 *   timestamp      TIMESTAMP(MICROS, UTC)
 *   <state name>   DOUBLE, one column per value of the chamber (settings "states"); gaps are NaN
 *   footer         deepvac.format, deepvac.day, deepvac.channels (JSON: name, label), deepvac.source
 *
 * Columns are found by name, so a file stays readable after the value list changes. DeepVac
 * Insight writes and reads the same files (app/services/day_recorder.py), and pandas, pyarrow or
 * DuckDB read them directly. The day files of older versions (data/yyyy-MM-dd_00-00-00.dat,
 * QDataStream) are still read; new samples of such a day are saved as Parquet.
 */

inline QString const FORMAT{QStringLiteral("deepvac.recording/1")};

struct Channel {
  QString name;
  QString label;
};

// the chamber's value list (settings "states", in order): interval data follows it
void             setChannels(QVector<Channel> const &channels);
QVector<Channel> channels();
QStringList      channelNames();

// the day containing epochSecTime (local time)
QString dayText(double epochSecTime);             // yyyy-MM-dd
QString fileName(double epochSecTime);            // yyyy-MM-dd.parquet
QString legacyFileName(double epochSecTime);      // yyyy-MM-dd_00-00-00.dat
QString savePath(double epochSecTime);            // RECORDINGS_DIR + fileName
// the day's file to read: the Parquet file, or the older .dat when only that exists (empty: neither)
QString readPath(double epochSecTime);
// every day's file, oldest first: Parquet files, and .dat files of days without one
QStringList dayFiles(QString const &recordingsDir, QString const &legacyDir);

// a day's file: `states` in the order of `names` (channelNames() when empty), NaN for missing ones
bool read(QString const &path, QVector<double> &times, QVector<QVector<double>> &states, QStringList names = {},
          QString *error = nullptr);
bool readBytes(QByteArray const &bytes, QVector<double> &times, QVector<QVector<double>> &states, QStringList names = {},
               QString *error = nullptr);

// the Parquet file of a day: states in the order of channelNames() (or ch0, ch1, ... when the count differs)
QByteArray write(QVector<double> const &times, QVector<QVector<double>> const &states, QString *error = nullptr);

// Saves (replaces) a day's file atomically; thread-safe. Saves are numbered when their data is taken
// (nextGeneration()): one that finishes after a newer save of the same file is dropped, so a slow
// background save never overwrites a newer one. Returns false only on a write error.
quint64 nextGeneration();
bool    saveDay(QString const &path, quint64 generation, QVector<double> const &times, QVector<QVector<double>> const &states,
                QString *error = nullptr);

}

#endif // RECORDINGFILE_H
