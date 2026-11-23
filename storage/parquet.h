#ifndef PARQUET_H
#define PARQUET_H

#include <QByteArray>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>

namespace deepvaccontrol::parquet {

/**
 * @brief A flat table of a time column and numeric columns, as an Apache Parquet file.
 *
 * The writer makes the subset every Parquet reader understands (pyarrow, pandas, DuckDB,
 * Spark, ...): one row group, a "timestamp" column (INT64, TIMESTAMP(MICROS, UTC)) and one
 * DOUBLE column per channel, all REQUIRED (gaps are NaN), PLAIN encoding, data pages v1,
 * GZIP compression, key-value metadata in the footer.
 *
 * The reader takes those files and the ones other tools write with the same choices
 * (pyarrow with use_dictionary=False): PLAIN encoding, data pages v1 or v2, UNCOMPRESSED or
 * GZIP, required or optional columns (nulls become NaN), several row groups and pages.
 * Columns it can't read (strings, dictionary or delta encodings, other codecs) fail the read
 * with a message rather than being guessed.
 */
struct Table {
  QString                         timeColumn{QStringLiteral("timestamp")};
  QVector<qint64>                 timesUs; // microseconds since the Unix epoch, UTC
  QStringList                     names;   // the numeric columns, in order
  QVector<QVector<double>>        columns; // parallel to names, each timesUs.size() long
  QVector<QPair<QString, QString>> metadata;

  [[nodiscard]] QString metadataValue(QString const &key) const;
};

// The whole file; empty on error. level: GZIP compression level, 1 (fast) ... 9 (small).
QByteArray write(Table const &table, QString *error = nullptr, int level = 6);

bool read(QByteArray const &file, Table &table, QString *error = nullptr);

// "PAR1" at both ends
bool looksLikeParquet(QByteArray const &file);

}

#endif // PARQUET_H
