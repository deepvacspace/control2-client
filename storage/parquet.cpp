#include "parquet.h"

#include <QHash>
#include <QtEndian>
#include <cmath>
#include <cstring>
#include <limits>

// zlib for the GZIP codec: Qt Core ships and exports it (z_-prefixed), so no extra library
#include <QtZlib/zlib.h>

namespace deepvaccontrol::parquet {

namespace {

// ---------------------------------------------------------------- Parquet constants (parquet.thrift)

enum Type : qint32 { BOOLEAN = 0, INT32 = 1, INT64 = 2, INT96 = 3, FLOAT = 4, DOUBLE = 5, BYTE_ARRAY = 6, FIXED_LEN_BYTE_ARRAY = 7 };
enum Repetition : qint32 { REQUIRED = 0, OPTIONAL = 1, REPEATED = 2 };
enum Encoding : qint32 { PLAIN = 0, PLAIN_DICTIONARY = 2, RLE = 3, BIT_PACKED = 4, RLE_DICTIONARY = 8 };
enum Codec : qint32 { UNCOMPRESSED = 0, SNAPPY = 1, GZIP = 2 };
enum PageType : qint32 { DATA_PAGE = 0, INDEX_PAGE = 1, DICTIONARY_PAGE = 2, DATA_PAGE_V2 = 3 };
enum ConvertedType : qint32 { TIMESTAMP_MILLIS = 9, TIMESTAMP_MICROS = 10 };

QByteArray const MAGIC{"PAR1"};

// ---------------------------------------------------------------- Thrift compact protocol

enum CType : quint8 {
  C_STOP = 0, C_TRUE = 1, C_FALSE = 2, C_BYTE = 3, C_I16 = 4, C_I32 = 5, C_I64 = 6, C_DOUBLE = 7,
  C_BINARY = 8, C_LIST = 9, C_SET = 10, C_MAP = 11, C_STRUCT = 12
};

class CompactWriter {
public:
  QByteArray out;

  void beginStruct() { m_lastIds.push_back(0); }
  void endStruct() {
    out.append('\0');
    m_lastIds.pop_back();
  }
  void i32(int id, qint32 v) {
    field(id, C_I32);
    varint(zigzag(v));
  }
  void i64(int id, qint64 v) {
    field(id, C_I64);
    varint(zigzag(v));
  }
  void boolean(int id, bool v) { field(id, v ? C_TRUE : C_FALSE); }
  void string(int id, QByteArray const &v) {
    field(id, C_BINARY);
    bytes(v);
  }
  // a struct field: write its fields, then endStruct()
  void structField(int id) {
    field(id, C_STRUCT);
    beginStruct();
  }
  // a list field: write `size` elements with the element writers below
  void list(int id, quint8 elementType, int size) {
    field(id, C_LIST);
    if (size < 15) {
      out.append(static_cast<char>((size << 4) | elementType));
    } else {
      out.append(static_cast<char>(0xF0 | elementType));
      varint(static_cast<quint64>(size));
    }
  }
  void elementI32(qint32 v) { varint(zigzag(v)); }
  void elementString(QByteArray const &v) { bytes(v); }
  void elementStruct() { beginStruct(); }

private:
  static quint64 zigzag(qint64 v) { return (static_cast<quint64>(v) << 1) ^ static_cast<quint64>(v >> 63); }
  void           varint(quint64 v) {
    while (v >= 0x80) {
      out.append(static_cast<char>((v & 0x7F) | 0x80));
      v >>= 7;
    }
    out.append(static_cast<char>(v));
  }
  void bytes(QByteArray const &v) {
    varint(static_cast<quint64>(v.size()));
    out.append(v);
  }
  void field(int id, quint8 type) {
    int const delta{id - m_lastIds.back()};
    if ((delta > 0) && (delta <= 15)) {
      out.append(static_cast<char>((delta << 4) | type));
    } else {
      out.append(static_cast<char>(type));
      varint(zigzag(static_cast<qint64>(id)));
    }
    m_lastIds.back() = id;
  }

  QVector<int> m_lastIds;
};

// a decoded Thrift value: structs keep their fields by id
struct TValue {
  quint8               type{C_STOP};
  qint64               i{0};
  double               d{0.0};
  QByteArray           bin;
  QVector<TValue>      items;
  QHash<int, TValue>   fields;

  [[nodiscard]] bool          has(int id) const { return fields.contains(id); }
  [[nodiscard]] qint64        integer(int id, qint64 fallback = 0) const { return fields.contains(id) ? fields[id].i : fallback; }
  [[nodiscard]] QByteArray    bytes(int id) const { return fields.value(id).bin; }
  [[nodiscard]] TValue        child(int id) const { return fields.value(id); }
  [[nodiscard]] QVector<TValue> list(int id) const { return fields.value(id).items; }
};

class CompactReader {
public:
  CompactReader(char const *data, qint64 size) : m_p{reinterpret_cast<uchar const *>(data)}, m_begin{m_p}, m_end{m_p + size} {}

  TValue readStruct(int depth = 0) {
    TValue result;
    result.type = C_STRUCT;
    if (depth > 32) {
      m_ok = false;
      return result;
    }
    int lastId{0};
    while (m_ok) {
      quint8 const header{byte()};
      if (!m_ok || (header == C_STOP)) {
        break;
      }
      quint8 const type{static_cast<quint8>(header & 0x0F)};
      int const    delta{header >> 4};
      int const    id{delta != 0 ? lastId + delta : static_cast<int>(unzigzag(varint()))};
      lastId = id;
      result.fields.insert(id, readValue(type, depth + 1));
    }
    return result;
  }

  [[nodiscard]] bool   ok() const { return m_ok; }
  [[nodiscard]] qint64 consumed() const { return m_p - m_begin; }

private:
  TValue readValue(quint8 type, int depth) {
    TValue v;
    v.type = type;
    switch (type) {
      case C_TRUE:
      case C_FALSE:
        v.i = (type == C_TRUE) ? 1 : 0;
        break;
      case C_BYTE:
        v.i = static_cast<qint8>(byte());
        break;
      case C_I16:
      case C_I32:
      case C_I64:
        v.i = unzigzag(varint());
        break;
      case C_DOUBLE: {
        if (m_end - m_p < 8) {
          m_ok = false;
          break;
        }
        std::memcpy(&v.d, m_p, 8); // little endian
        m_p += 8;
        break;
      }
      case C_BINARY: {
        quint64 const size{varint()};
        if (!m_ok || (size > static_cast<quint64>(m_end - m_p))) {
          m_ok = false;
          break;
        }
        v.bin = QByteArray(reinterpret_cast<char const *>(m_p), static_cast<qsizetype>(size));
        m_p += size;
        break;
      }
      case C_LIST:
      case C_SET: {
        quint8 const header{byte()};
        quint64      size{static_cast<quint64>(header >> 4)};
        if (size == 15) {
          size = varint();
        }
        quint8 const elementType{static_cast<quint8>(header & 0x0F)};
        if (!m_ok || (size > static_cast<quint64>(m_end - m_p))) { // every element takes a byte at least
          m_ok = false;
          break;
        }
        for (quint64 n{0}; (n < size) && m_ok; ++n) {
          if ((elementType == C_TRUE) || (elementType == C_FALSE)) { // booleans in a list: one byte each
            TValue item;
            item.type = C_TRUE;
            item.i    = (byte() == C_TRUE) ? 1 : 0;
            v.items.push_back(item);
          } else {
            v.items.push_back(readValue(elementType, depth + 1));
          }
        }
        break;
      }
      case C_MAP: {
        quint64 const size{varint()};
        if (size > 0) {
          quint8 const types{byte()};
          for (quint64 n{0}; (n < size) && m_ok; ++n) {
            v.items.push_back(readValue(types >> 4, depth + 1));
            v.items.push_back(readValue(types & 0x0F, depth + 1));
          }
        }
        break;
      }
      case C_STRUCT:
        v = readStruct(depth);
        break;
      default:
        m_ok = false;
    }
    return v;
  }

  quint8 byte() {
    if (m_p >= m_end) {
      m_ok = false;
      return 0;
    }
    return *m_p++;
  }
  quint64 varint() {
    quint64 result{0};
    for (int shift{0}; shift < 64; shift += 7) {
      quint8 const b{byte()};
      if (!m_ok) {
        return 0;
      }
      result |= static_cast<quint64>(b & 0x7F) << shift;
      if (!(b & 0x80)) {
        return result;
      }
    }
    m_ok = false;
    return 0;
  }
  static qint64 unzigzag(quint64 v) { return static_cast<qint64>(v >> 1) ^ -static_cast<qint64>(v & 1); }

  uchar const *m_p;
  uchar const *m_begin;
  uchar const *m_end;
  bool         m_ok{true};
};

// ---------------------------------------------------------------- GZIP

QByteArray gzip(QByteArray const &data, int level) {
  z_stream stream{};
  if (deflateInit2(&stream, level, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK) { // +16: gzip wrapper
    return {};
  }
  QByteArray out;
  out.resize(static_cast<qsizetype>(deflateBound(&stream, static_cast<uLong>(data.size()))) + 32);
  stream.next_in   = reinterpret_cast<Bytef *>(const_cast<char *>(data.constData()));
  stream.avail_in  = static_cast<uInt>(data.size());
  stream.next_out  = reinterpret_cast<Bytef *>(out.data());
  stream.avail_out = static_cast<uInt>(out.size());
  int const result{deflate(&stream, Z_FINISH)};
  qsizetype const written{static_cast<qsizetype>(stream.total_out)};
  deflateEnd(&stream);
  if (result != Z_STREAM_END) {
    return {};
  }
  out.resize(written);
  return out;
}

// gzip or zlib data (several gzip members one after the other too)
bool gunzip(char const *data, qsizetype size, qsizetype expected, QByteArray &out) {
  out.resize(expected);
  z_stream stream{};
  if (inflateInit2(&stream, 15 + 32) != Z_OK) { // +32: detect gzip / zlib
    return false;
  }
  stream.next_in   = reinterpret_cast<Bytef *>(const_cast<char *>(data));
  stream.avail_in  = static_cast<uInt>(size);
  stream.next_out  = reinterpret_cast<Bytef *>(out.data());
  stream.avail_out = static_cast<uInt>(out.size());
  int result{Z_OK};
  while (true) {
    result = inflate(&stream, Z_NO_FLUSH);
    if ((result == Z_STREAM_END) && (stream.avail_in > 0) && (stream.avail_out > 0)) {
      inflateReset(&stream); // the next member
      continue;
    }
    break;
  }
  bool const ok{(result == Z_STREAM_END) && (static_cast<qsizetype>(stream.total_out) == expected)};
  inflateEnd(&stream);
  return ok;
}

// ---------------------------------------------------------------- writing

template <typename T>
void appendLittleEndian(QByteArray &out, T value) {
  T le{qToLittleEndian(value)};
  out.append(reinterpret_cast<char const *>(&le), sizeof(T));
}

struct ChunkInfo {
  qint32  type{DOUBLE};
  QString name;
  qint64  offset{0};
  qint64  uncompressed{0}; // page header + values
  qint64  compressed{0};
};

// one data page holding all values of a column
bool writeColumn(QByteArray &file, ChunkInfo &chunk, QByteArray const &plain, qint64 numValues, int level) {
  QByteArray const compressed = gzip(plain, level);
  if (compressed.isEmpty() && !plain.isEmpty()) {
    return false;
  }
  CompactWriter header;
  header.beginStruct();
  header.i32(1, DATA_PAGE);
  header.i32(2, static_cast<qint32>(plain.size()));
  header.i32(3, static_cast<qint32>(compressed.size()));
  header.structField(5); // DataPageHeader
  header.i32(1, static_cast<qint32>(numValues));
  header.i32(2, PLAIN);
  header.i32(3, RLE); // definition levels (none: REQUIRED columns)
  header.i32(4, RLE); // repetition levels (none)
  header.endStruct();
  header.endStruct();

  chunk.offset       = file.size();
  chunk.uncompressed = header.out.size() + plain.size();
  chunk.compressed   = header.out.size() + compressed.size();
  file.append(header.out);
  file.append(compressed);
  return true;
}

}

QString Table::metadataValue(QString const &key) const {
  for (auto const &[k, v] : metadata) {
    if (k == key) {
      return v;
    }
  }
  return {};
}

bool looksLikeParquet(QByteArray const &file) {
  return (file.size() >= 12) && file.startsWith(MAGIC) && file.endsWith(MAGIC);
}

QByteArray write(Table const &table, QString *error, int level) {
  auto fail = [error](QString const &message) {
    if (error) {
      *error = message;
    }
    return QByteArray{};
  };
  qint64 const rows{table.timesUs.size()};
  if (table.names.size() != table.columns.size()) {
    return fail(QStringLiteral("%1 column names for %2 columns").arg(table.names.size()).arg(table.columns.size()));
  }
  for (qsizetype c{0}; c < table.columns.size(); ++c) {
    if (table.columns[c].size() != rows) {
      return fail(QStringLiteral("column %1 has %2 values for %3 times").arg(table.names[c]).arg(table.columns[c].size()).arg(rows));
    }
  }

  QByteArray file{MAGIC};
  QVector<ChunkInfo> chunks;

  // the time column
  {
    QByteArray plain;
    plain.reserve(rows * 8);
    for (qint64 const t : table.timesUs) {
      appendLittleEndian<qint64>(plain, t);
    }
    ChunkInfo chunk{INT64, table.timeColumn};
    if (!writeColumn(file, chunk, plain, rows, level)) {
      return fail(QStringLiteral("compression failed"));
    }
    chunks.push_back(chunk);
  }
  for (qsizetype c{0}; c < table.columns.size(); ++c) {
    QByteArray plain;
    plain.reserve(rows * 8);
    for (double const value : table.columns[c]) {
      quint64 bits;
      std::memcpy(&bits, &value, 8);
      appendLittleEndian<quint64>(plain, bits);
    }
    ChunkInfo chunk{DOUBLE, table.names[c]};
    if (!writeColumn(file, chunk, plain, rows, level)) {
      return fail(QStringLiteral("compression failed"));
    }
    chunks.push_back(chunk);
  }

  // footer: FileMetaData
  CompactWriter meta;
  meta.beginStruct();
  meta.i32(1, 1); // version
  meta.list(2, C_STRUCT, static_cast<int>(chunks.size()) + 1);
  meta.elementStruct(); // root
  meta.string(4, QByteArrayLiteral("schema"));
  meta.i32(5, static_cast<qint32>(chunks.size()));
  meta.endStruct();
  for (ChunkInfo const &chunk : chunks) {
    meta.elementStruct();
    meta.i32(1, chunk.type);
    meta.i32(3, REQUIRED);
    meta.string(4, chunk.name.toUtf8());
    if (chunk.type == INT64) {
      meta.i32(6, TIMESTAMP_MICROS);
      meta.structField(10); // LogicalType
      meta.structField(8);  // TIMESTAMP
      meta.boolean(1, true); // isAdjustedToUTC
      meta.structField(2);  // unit
      meta.structField(2);  // MICROS
      meta.endStruct();
      meta.endStruct();
      meta.endStruct();
      meta.endStruct();
    }
    meta.endStruct();
  }
  meta.i64(3, rows);
  qint64 totalUncompressed{0};
  qint64 totalCompressed{0};
  for (ChunkInfo const &chunk : chunks) {
    totalUncompressed += chunk.uncompressed;
    totalCompressed += chunk.compressed;
  }
  meta.list(4, C_STRUCT, 1); // one row group
  meta.elementStruct();
  meta.list(1, C_STRUCT, static_cast<int>(chunks.size()));
  for (ChunkInfo const &chunk : chunks) {
    meta.elementStruct(); // ColumnChunk
    meta.i64(2, chunk.offset);
    meta.structField(3); // ColumnMetaData
    meta.i32(1, chunk.type);
    meta.list(2, C_I32, 2);
    meta.elementI32(PLAIN);
    meta.elementI32(RLE);
    meta.list(3, C_BINARY, 1);
    meta.elementString(chunk.name.toUtf8());
    meta.i32(4, GZIP);
    meta.i64(5, rows);
    meta.i64(6, chunk.uncompressed);
    meta.i64(7, chunk.compressed);
    meta.i64(9, chunk.offset);
    meta.endStruct();
    meta.endStruct();
  }
  meta.i64(2, totalUncompressed);
  meta.i64(3, rows);
  meta.i64(5, chunks.isEmpty() ? 4 : chunks.first().offset);
  meta.i64(6, totalCompressed);
  meta.endStruct();
  if (!table.metadata.isEmpty()) {
    meta.list(5, C_STRUCT, static_cast<int>(table.metadata.size()));
    for (auto const &[key, value] : table.metadata) {
      meta.elementStruct();
      meta.string(1, key.toUtf8());
      meta.string(2, value.toUtf8());
      meta.endStruct();
    }
  }
  meta.string(6, QByteArrayLiteral("deepvac-control version 1.0 (Parquet subset: PLAIN, GZIP, data page v1)"));
  meta.endStruct();

  file.append(meta.out);
  appendLittleEndian<qint32>(file, static_cast<qint32>(meta.out.size()));
  file.append(MAGIC);
  return file;
}

// ---------------------------------------------------------------- reading

namespace {

// RLE / bit-packed hybrid levels (bit width 1: definition levels of a flat optional column)
bool decodeLevels(uchar const *p, qsizetype size, int bitWidth, qint64 count, QVector<int> &levels) {
  levels.clear();
  levels.reserve(count);
  uchar const *end{p + size};
  int const    byteWidth{(bitWidth + 7) / 8};
  while ((levels.size() < count) && (p < end)) {
    quint64 header{0};
    for (int shift{0};; shift += 7) {
      if ((p >= end) || (shift > 56)) {
        return false;
      }
      uchar const b{*p++};
      header |= static_cast<quint64>(b & 0x7F) << shift;
      if (!(b & 0x80)) {
        break;
      }
    }
    if (header & 1) { // bit-packed: (header >> 1) groups of 8 values
      qint64 const values{static_cast<qint64>(header >> 1) * 8};
      qint64 const bytes{(values * bitWidth + 7) / 8};
      if (end - p < bytes) {
        return false;
      }
      for (qint64 n{0}; (n < values) && (levels.size() < count); ++n) {
        int value{0};
        for (int b{0}; b < bitWidth; ++b) {
          qint64 const bit{n * bitWidth + b};
          value |= ((p[bit / 8] >> (bit % 8)) & 1) << b;
        }
        levels.push_back(value);
      }
      p += bytes;
    } else { // run: (header >> 1) repeats of one value
      if (end - p < byteWidth) {
        return false;
      }
      int value{0};
      for (int b{0}; b < byteWidth; ++b) {
        value |= p[b] << (8 * b);
      }
      p += byteWidth;
      for (quint64 n{0}; (n < (header >> 1)) && (levels.size() < count); ++n) {
        levels.push_back(value);
      }
    }
  }
  return levels.size() == count;
}

struct Leaf {
  QString name;
  qint32  type{-1};
  qint32  repetition{REQUIRED};
  bool    isTimestamp{false};
  qint64  toMicros{1};  // timestamps in ms: multiply ...
  qint64  fromNanos{1}; // ... in ns: divide
};

// one column chunk's values as doubles (time column: int64 microseconds in `ints`)
bool readChunk(QByteArray const &file, TValue const &columnMeta, Leaf const &leaf, QVector<double> &doubles, QVector<qint64> &ints,
               QString &error) {
  qint32 const codec{static_cast<qint32>(columnMeta.integer(4))};
  if ((codec != UNCOMPRESSED) && (codec != GZIP)) {
    error = QStringLiteral("column %1: compression codec %2 is not supported (use GZIP or none)").arg(leaf.name).arg(codec);
    return false;
  }
  if (columnMeta.has(11)) {
    error = QStringLiteral("column %1: dictionary encoding is not supported (write with use_dictionary=False)").arg(leaf.name);
    return false;
  }
  qint64 const numValues{columnMeta.integer(5)};
  qint64       pos{columnMeta.integer(9)};
  qint64 const valueSize{(leaf.type == INT32) || (leaf.type == FLOAT) ? 4 : 8};
  bool const   optional{leaf.repetition == OPTIONAL};

  qint64 read{0};
  while (read < numValues) {
    if ((pos < 4) || (pos >= file.size())) {
      error = QStringLiteral("column %1: page outside the file").arg(leaf.name);
      return false;
    }
    CompactReader reader(file.constData() + pos, file.size() - pos);
    TValue const  header = reader.readStruct();
    if (!reader.ok()) {
      error = QStringLiteral("column %1: unreadable page header").arg(leaf.name);
      return false;
    }
    pos += reader.consumed();
    qint32 const pageType{static_cast<qint32>(header.integer(1))};
    qint64 const uncompressedSize{header.integer(2)};
    qint64 const compressedSize{header.integer(3)};
    if ((compressedSize < 0) || (pos + compressedSize > file.size())) {
      error = QStringLiteral("column %1: page past the end of the file").arg(leaf.name);
      return false;
    }
    char const *pageData{file.constData() + pos};
    pos += compressedSize;
    if ((pageType != DATA_PAGE) && (pageType != DATA_PAGE_V2)) {
      continue; // index pages and the like
    }

    qint64     pageValues{0};
    qint32     encoding{PLAIN};
    QByteArray body; // levels (v1) and values, uncompressed
    qint64     levelsBytes{0};
    bool       v1{pageType == DATA_PAGE};
    if (v1) {
      TValue const dataHeader = header.child(5);
      pageValues              = dataHeader.integer(1);
      encoding                = static_cast<qint32>(dataHeader.integer(2));
      if (codec == GZIP) {
        if (!gunzip(pageData, compressedSize, uncompressedSize, body)) {
          error = QStringLiteral("column %1: GZIP data could not be read").arg(leaf.name);
          return false;
        }
      } else {
        body = QByteArray(pageData, compressedSize);
      }
    } else {
      TValue const dataHeader = header.child(8);
      pageValues              = dataHeader.integer(1);
      encoding                = static_cast<qint32>(dataHeader.integer(4));
      qint64 const defBytes{dataHeader.integer(5)};
      qint64 const repBytes{dataHeader.integer(6)};
      bool const   compressed{dataHeader.integer(7, 1) != 0};
      levelsBytes = defBytes + repBytes;
      if ((levelsBytes < 0) || (levelsBytes > compressedSize)) {
        error = QStringLiteral("column %1: bad page levels").arg(leaf.name);
        return false;
      }
      body = QByteArray(pageData, levelsBytes); // v2 levels are never compressed
      if (compressed && (codec == GZIP)) {
        QByteArray values;
        if (!gunzip(pageData + levelsBytes, compressedSize - levelsBytes, uncompressedSize - levelsBytes, values)) {
          error = QStringLiteral("column %1: GZIP data could not be read").arg(leaf.name);
          return false;
        }
        body.append(values);
      } else {
        body.append(pageData + levelsBytes, compressedSize - levelsBytes);
      }
      if (repBytes != 0) {
        error = QStringLiteral("column %1: repeated (nested) columns are not supported").arg(leaf.name);
        return false;
      }
    }
    if (encoding != PLAIN) {
      error = QStringLiteral("column %1: encoding %2 is not supported (PLAIN only)").arg(leaf.name).arg(encoding);
      return false;
    }

    // definition levels: which rows hold a value
    QVector<int> defined;
    qint64       offset{0};
    if (optional) {
      uchar const *levelsData{reinterpret_cast<uchar const *>(body.constData())};
      qint64       levelsSize{levelsBytes};
      if (v1) {
        if (body.size() < 4) {
          error = QStringLiteral("column %1: missing definition levels").arg(leaf.name);
          return false;
        }
        levelsSize = qFromLittleEndian<qint32>(body.constData());
        levelsData += 4;
        offset = 4 + levelsSize;
      } else {
        offset = levelsBytes;
      }
      if ((offset > body.size()) || !decodeLevels(levelsData, levelsSize, 1, pageValues, defined)) {
        error = QStringLiteral("column %1: unreadable definition levels").arg(leaf.name);
        return false;
      }
    } else if (!v1) {
      offset = levelsBytes;
    }

    char const *values{body.constData() + offset};
    qint64 const available{(body.size() - offset) / valueSize};
    qint64       next{0};
    for (qint64 row{0}; row < pageValues; ++row) {
      bool const present{!optional || (defined[row] == 1)};
      if (present && (next >= available)) {
        error = QStringLiteral("column %1: page shorter than its values").arg(leaf.name);
        return false;
      }
      char const *v{values + next * valueSize};
      if (leaf.isTimestamp) {
        qint64 const raw{present ? qFromLittleEndian<qint64>(v) : 0};
        ints.push_back(leaf.fromNanos > 1 ? raw / leaf.fromNanos : raw * leaf.toMicros);
      } else if (!present) {
        doubles.push_back(std::numeric_limits<double>::quiet_NaN());
      } else {
        switch (leaf.type) {
          case DOUBLE: {
            quint64 const bits{qFromLittleEndian<quint64>(v)};
            double        d;
            std::memcpy(&d, &bits, 8);
            doubles.push_back(d);
            break;
          }
          case FLOAT: {
            quint32 const bits{qFromLittleEndian<quint32>(v)};
            float         f;
            std::memcpy(&f, &bits, 4);
            doubles.push_back(f);
            break;
          }
          case INT32:
            doubles.push_back(qFromLittleEndian<qint32>(v));
            break;
          default:
            doubles.push_back(static_cast<double>(qFromLittleEndian<qint64>(v)));
        }
      }
      if (present) {
        ++next;
      }
    }
    read += pageValues;
  }
  return true;
}

}

bool read(QByteArray const &file, Table &table, QString *error) {
  auto fail = [error](QString const &message) {
    if (error) {
      *error = message;
    }
    return false;
  };
  table = Table{};
  if (!looksLikeParquet(file)) {
    return fail(QStringLiteral("not a Parquet file"));
  }
  qint32 const metaSize{qFromLittleEndian<qint32>(file.constData() + file.size() - 8)};
  qint64 const metaStart{file.size() - 8 - metaSize};
  if ((metaSize <= 0) || (metaStart < 4)) {
    return fail(QStringLiteral("bad footer"));
  }
  CompactReader reader(file.constData() + metaStart, metaSize);
  TValue const  meta = reader.readStruct();
  if (!reader.ok()) {
    return fail(QStringLiteral("unreadable footer"));
  }

  // the flat schema: the root, then one leaf per column
  QVector<TValue> const schema = meta.list(2);
  if (schema.isEmpty()) {
    return fail(QStringLiteral("no schema"));
  }
  QVector<Leaf> leaves;
  for (qsizetype i{1}; i < schema.size(); ++i) {
    TValue const &element = schema[i];
    if (element.integer(5, 0) > 0) {
      return fail(QStringLiteral("nested columns are not supported"));
    }
    Leaf leaf;
    leaf.name       = QString::fromUtf8(element.bytes(4));
    leaf.type       = static_cast<qint32>(element.integer(1, -1));
    leaf.repetition = static_cast<qint32>(element.integer(3, REQUIRED));
    if (leaf.repetition == REPEATED) {
      return fail(QStringLiteral("repeated columns are not supported"));
    }
    TValue const logical   = element.child(10);
    TValue const timestamp = logical.child(8);
    qint32 const converted{static_cast<qint32>(element.integer(6, -1))};
    if ((leaf.type == INT64) && (logical.has(8) || (converted == TIMESTAMP_MILLIS) || (converted == TIMESTAMP_MICROS))) {
      leaf.isTimestamp = true;
      TValue const unit = timestamp.child(2);
      if (unit.has(1) || (!logical.has(8) && (converted == TIMESTAMP_MILLIS))) {
        leaf.toMicros = 1000;
      } else if (unit.has(3)) {
        leaf.fromNanos = 1000;
      }
    }
    leaves.push_back(leaf);
  }

  // the time column: the first timestamp, else one named like it
  qsizetype timeIndex{-1};
  for (qsizetype i{0}; i < leaves.size(); ++i) {
    if (leaves[i].isTimestamp) {
      timeIndex = i;
      break;
    }
  }
  if (timeIndex < 0) {
    for (qsizetype i{0}; i < leaves.size(); ++i) {
      if ((leaves[i].type == INT64) && (leaves[i].name.compare(QStringLiteral("timestamp"), Qt::CaseInsensitive) == 0)) {
        leaves[i].isTimestamp = true; // plain INT64: taken as microseconds
        timeIndex             = i;
      }
    }
  }
  if (timeIndex < 0) {
    return fail(QStringLiteral("no timestamp column"));
  }
  table.timeColumn = leaves[timeIndex].name;
  QVector<qsizetype> numeric; // leaf indexes of the columns read as numbers
  for (qsizetype i{0}; i < leaves.size(); ++i) {
    qint32 const type{leaves[i].type};
    if ((i != timeIndex) && ((type == DOUBLE) || (type == FLOAT) || (type == INT32) || (type == INT64))) {
      numeric.push_back(i);
      table.names.push_back(leaves[i].name);
    }
  }
  table.columns.resize(numeric.size());

  for (TValue const &rowGroup : meta.list(4)) {
    QVector<TValue> const chunks = rowGroup.list(1);
    if (chunks.size() != leaves.size()) {
      return fail(QStringLiteral("a row group does not match the schema"));
    }
    QString      chunkError;
    QVector<double> unused;
    if (!readChunk(file, chunks[timeIndex].child(3), leaves[timeIndex], unused, table.timesUs, chunkError)) {
      return fail(chunkError);
    }
    for (qsizetype c{0}; c < numeric.size(); ++c) {
      QVector<qint64> ignored;
      if (!readChunk(file, chunks[numeric[c]].child(3), leaves[numeric[c]], table.columns[c], ignored, chunkError)) {
        return fail(chunkError);
      }
    }
  }
  for (QVector<double> const &column : std::as_const(table.columns)) {
    if (column.size() != table.timesUs.size()) {
      return fail(QStringLiteral("columns of different lengths"));
    }
  }
  for (TValue const &pair : meta.list(5)) {
    table.metadata.push_back({QString::fromUtf8(pair.bytes(1)), QString::fromUtf8(pair.bytes(2))});
  }
  return true;
}

}
