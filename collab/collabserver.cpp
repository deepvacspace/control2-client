#include "collabserver.h"

#include <QHostInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSslConfiguration>
#include <QSslKey>
#include <QSslServer>
#include <QSslSocket>

#include "collabcrypto.h"
#include "collabhoststore.h"

namespace deepvaccontrol::collab {

namespace {
constexpr int       JOIN_CODE_TTL_MS{600'000};
constexpr int       JOIN_CODE_MAX_FAILURES{5};
constexpr qsizetype MAX_BODY_BYTES{1'000'000};      // items
constexpr qsizetype MAX_RUN_BODY_BYTES{96'000'000}; // full experiments
constexpr qsizetype MAX_HEADER_BYTES{32'000};
constexpr int       HANDSHAKE_TIMEOUT_MS{10'000};
constexpr int       ED25519_KEY_BYTES{32};
char const          JOIN_CODE_ALPHABET[]{"ABCDEFGHJKLMNPQRSTUVWXYZ23456789"};

QRegularExpression const UID_PATTERN{QStringLiteral("^[A-Za-z0-9_-]{1,64}$")};

class HttpError {
public:
  HttpError(int status, QString detail) : status{status}, detail{std::move(detail)} {
  }
  int     status;
  QString detail;
};

// ---- item validation: accepted kinds and their fields
enum class Type : uint8_t {
  STR,
  OPT_STR,
  FLOAT,
  OPT_FLOAT,
  OPT_INT,
  ARRAY
};

struct Field {
  char const *name;
  Type        type;
  bool        required{true}; // control-app extension fields are optional
};

struct KindSpec {
  bool           mutableKind;
  QVector<Field> fields;
};

QHash<QString, KindSpec> const &kindSpecs() {
  static QHash<QString, KindSpec> const specs{
    {"annotation",
     {false,
      {{"run_uid", Type::STR},
       {"user_name", Type::STR},
       {"x0", Type::FLOAT},
       {"x1", Type::FLOAT},
       {"label", Type::STR},
       {"color", Type::STR},
       {"created_at", Type::STR}}}},
    {"variable_rule",
     {false,
      {{"run_uid", Type::STR},
       {"user_name", Type::STR},
       {"name", Type::STR},
       {"channel", Type::STR},
       {"lo", Type::OPT_FLOAT},
       {"hi", Type::OPT_FLOAT},
       {"color", Type::STR},
       {"created_at", Type::STR}}}},
    {"run_metadata",
     {true,
      {{"shared_by", Type::STR},
       {"id", Type::STR},
       {"group", Type::OPT_STR},
       {"samples", Type::OPT_INT},
       {"duration_s", Type::OPT_FLOAT},
       {"mae", Type::OPT_FLOAT},
       {"cost", Type::OPT_FLOAT},
       {"tail_mae", Type::OPT_FLOAT},
       {"overshoot", Type::OPT_FLOAT},
       {"settle_time_s", Type::OPT_FLOAT},
       {"start_time", Type::OPT_STR},
       {"end_time", Type::OPT_STR},
       {"chamber", Type::OPT_STR},
       {"test_profile", Type::OPT_STR},
       {"start_epoch", Type::OPT_FLOAT, false},
       {"end_epoch", Type::OPT_FLOAT, false}}}},
    {"test_profile",
     {true,
      {{"name", Type::STR},
       {"description", Type::STR},
       {"created_by", Type::STR},
       {"created_at", Type::STR},
       {"updated_at", Type::STR},
       {"steps", Type::ARRAY},
       {"program_json", Type::OPT_STR, false}}}},
  };
  return specs;
}

QJsonValue convert(QJsonValue const &value, Type type) {
  auto toNumber = [](QJsonValue const &v) {
    if (v.isDouble()) {
      return v;
    }
    bool         ok{false};
    double const number{v.toString().toDouble(&ok)};
    if (!v.isString() || !ok) {
      throw HttpError(400, QStringLiteral("Invalid item."));
    }
    return QJsonValue(number);
  };
  switch (type) {
    case Type::STR:
      if (value.isString()) {
        return value;
      }
      if (value.isDouble()) {
        return QJsonValue(QString::number(value.toDouble()));
      }
      throw HttpError(400, QStringLiteral("Invalid item."));
    case Type::OPT_STR:
      return value.isNull() ? value : convert(value, Type::STR);
    case Type::FLOAT:
      return toNumber(value);
    case Type::OPT_FLOAT:
      return value.isNull() ? value : toNumber(value);
    case Type::OPT_INT:
      return value.isNull() ? value : QJsonValue(static_cast<qint64>(toNumber(value).toDouble()));
    case Type::ARRAY:
      if (!value.isArray()) {
        throw HttpError(400, QStringLiteral("Invalid item."));
      }
      return value;
  }
  return value;
}

QJsonObject parseJson(QByteArray const &body) {
  QJsonParseError     error;
  QJsonDocument const doc = QJsonDocument::fromJson(body, &error);
  if (error.error != QJsonParseError::NoError) {
    throw HttpError(400, QStringLiteral("Body must be JSON."));
  }
  if (!doc.isObject()) {
    throw HttpError(400, QStringLiteral("Body must be a JSON object."));
  }
  return doc.object();
}

QString normalizeCode(QString const &code) {
  QString result;
  for (QChar const ch : code.toUpper()) {
    if (ch.isLetterOrNumber()) {
      result += ch;
    }
  }
  return result;
}

bool constantTimeEquals(QByteArray const &a, QByteArray const &b) {
  if (a.size() != b.size()) {
    return false;
  }
  unsigned char diff{0};
  for (qsizetype i{0}; i < a.size(); ++i) {
    diff |= static_cast<unsigned char>(a[i] ^ b[i]);
  }
  return diff == 0;
}

QByteArray reason(int status) {
  switch (status) {
    case 200:
      return "OK";
    case 201:
      return "Created";
    case 400:
      return "Bad Request";
    case 401:
      return "Unauthorized";
    case 403:
      return "Forbidden";
    case 404:
      return "Not Found";
    case 413:
      return "Payload Too Large";
    default:
      return "Internal Server Error";
  }
}
} // namespace

CollabServer::CollabServer(QObject *parent) : QObject{parent}, m_server{new QSslServer(this)} {
  connect(m_server, &QSslServer::pendingConnectionAvailable, this, &CollabServer::onNewConnection);
}

CollabServer::~CollabServer() {
  stop();
}

bool CollabServer::start(quint16 port, QString *error) {
  if (m_server->isListening()) {
    return true;
  }
  QByteArray keyPem;
  if (!CollabCrypto::loadOrCreateHostIdentity(m_certPem, keyPem, error)) {
    return false;
  }

  QSslConfiguration config = QSslConfiguration::defaultConfiguration();
  config.setLocalCertificate(QSslCertificate(m_certPem, QSsl::Pem));
  config.setPrivateKey(QSslKey(keyPem, QSsl::Ec, QSsl::Pem));
  config.setProtocol(QSsl::TlsV1_2OrLater);
  config.setPeerVerifyMode(QSslSocket::VerifyNone); // devices authenticate with signatures, not certificates
  m_server->setSslConfiguration(config);
  m_server->setHandshakeTimeout(HANDSHAKE_TIMEOUT_MS);

  if (!m_server->listen(QHostAddress::AnyIPv4, port)) {
    if (error) {
      *error = m_server->errorString();
    }
    return false;
  }
  return true;
}

void CollabServer::stop() {
  m_server->close();
  for (QSslSocket *socket : m_requests.keys()) {
    socket->abort();
  }
  m_requests.clear();
  m_joinCode.clear();
}

bool CollabServer::isRunning() const {
  return m_server->isListening();
}

quint16 CollabServer::port() const {
  return m_server->serverPort();
}

QByteArray CollabServer::certPem() const {
  return m_certPem;
}

QString CollabServer::newJoinCode() {
  QString raw;
  for (int i{0}; i < 8; ++i) {
    raw += QChar(JOIN_CODE_ALPHABET[QRandomGenerator::system()->bounded(static_cast<int>(sizeof(JOIN_CODE_ALPHABET) - 1))]);
  }
  m_joinCode         = raw;
  m_joinCodeDeadline = QDeadlineTimer(JOIN_CODE_TTL_MS);
  m_joinCodeFailures = 0;
  return raw.left(4) + "-" + raw.mid(4);
}

bool CollabServer::joinCodeActive() const {
  return !m_joinCode.isEmpty() && !m_joinCodeDeadline.hasExpired();
}

bool CollabServer::consumeJoinCode(QString const &code) {
  if (m_joinCode.isEmpty() || m_joinCodeDeadline.hasExpired()) {
    m_joinCode.clear();
    return false;
  }
  if (constantTimeEquals(normalizeCode(code).toLatin1(), m_joinCode.toLatin1())) {
    m_joinCode.clear();
    return true;
  }
  if (++m_joinCodeFailures >= JOIN_CODE_MAX_FAILURES) {
    m_joinCode.clear();
  }
  return false;
}

void CollabServer::onNewConnection() {
  while (m_server->hasPendingConnections()) {
    auto *socket = qobject_cast<QSslSocket *>(m_server->nextPendingConnection());
    if (!socket) {
      continue;
    }
    m_requests.insert(socket, Request{});
    connect(socket, &QSslSocket::readyRead, this, [this, socket] {
      onReadyRead(socket);
    });
    connect(socket, &QSslSocket::disconnected, this, [this, socket] {
      m_requests.remove(socket);
      socket->deleteLater();
    });
    if (socket->bytesAvailable() > 0) {
      onReadyRead(socket);
    }
  }
}

void CollabServer::onReadyRead(QSslSocket *socket) {
  auto it = m_requests.find(socket);
  if (it == m_requests.end()) {
    return;
  }
  Request &request = it.value();
  request.buffer += socket->readAll();

  try {
    if (!request.headersDone) {
      qsizetype const end{request.buffer.indexOf("\r\n\r\n")};
      if (end < 0) {
        if (request.buffer.size() > MAX_HEADER_BYTES) {
          throw HttpError(413, QStringLiteral("Request headers too large."));
        }
        return;
      }
      QList<QByteArray> const lines = request.buffer.left(end).split('\n');
      QList<QByteArray> const first = lines.value(0).trimmed().split(' ');
      if (first.size() < 2) {
        throw HttpError(400, QStringLiteral("Bad request line."));
      }
      request.method = first[0];
      request.path   = first[1].split('?').first();
      for (qsizetype i{1}; i < lines.size(); ++i) {
        qsizetype const colon{lines[i].indexOf(':')};
        if (colon > 0) {
          request.headers.insert(lines[i].left(colon).trimmed().toLower(), lines[i].mid(colon + 1).trimmed());
        }
      }
      request.bodyLength   = request.headers.value("content-length", "0").toLongLong();
      request.headerLength = end + 4;
      request.headersDone  = true;
      qsizetype const limit{request.path.startsWith("/runs/") ? MAX_RUN_BODY_BYTES : MAX_BODY_BYTES};
      if ((request.bodyLength < 0) || (request.bodyLength > limit)) {
        throw HttpError(413, QStringLiteral("Request body too large."));
      }
    }
    if (request.buffer.size() < request.headerLength + request.bodyLength) {
      return; // wait for the rest of the body
    }
    QByteArray const body = request.buffer.mid(request.headerLength, request.bodyLength);
    Request const    done = request;
    request               = Request{};
    handle(socket, done, body);
  } catch (HttpError const &error) {
    request = Request{};
    send(socket, error.status, QJsonDocument(QJsonObject{{"detail", error.detail}}).toJson(QJsonDocument::Compact));
  }
}

void CollabServer::handle(QSslSocket *socket, Request const &request, QByteArray const &body) {
  QJsonObject response;
  int         status{200};
  try {
    // pairing is the only unauthenticated call
    if ((request.method == "POST") && (request.path == "/pair")) {
      QJsonObject const payload   = parseJson(body);
      QByteArray const  publicKey = QByteArray::fromBase64(payload.value("device_public_key").toString().toLatin1(),
                                                          QByteArray::Base64UrlEncoding);
      if (publicKey.size() != ED25519_KEY_BYTES) {
        throw HttpError(400, QStringLiteral("A valid device_public_key is required."));
      }
      if (!consumeJoinCode(payload.value("join_code").toString())) {
        throw HttpError(403, QStringLiteral("Invalid or expired join code."));
      }
      CollabHostStore::registerPeer(publicKey, payload.value("display_name").toString());
      emit peersChanged();
      send(socket, 200, QJsonDocument(QJsonObject{{"host_name", QHostInfo::localHostName()}}).toJson(QJsonDocument::Compact));
      return;
    }

    // every other request is signed by a paired device
    QString const    keyHash   = QString::fromLatin1(request.headers.value("x-device-key-hash"));
    QByteArray const signature = QByteArray::fromBase64(request.headers.value("x-device-signature"), QByteArray::Base64UrlEncoding);
    if (keyHash.isEmpty() || signature.isEmpty()) {
      throw HttpError(401, QStringLiteral("Missing device signature headers."));
    }
    auto const peer = CollabHostStore::peer(keyHash);
    if (!peer) {
      throw HttpError(403, QStringLiteral("This device is not paired with this host."));
    }
    if (!CollabCrypto::verify(peer->publicKey, body, signature)) {
      throw HttpError(401, QStringLiteral("Device signature verification failed."));
    }

    QStringList const parts = QString::fromUtf8(request.path).split('/', Qt::SkipEmptyParts);
    if ((parts.size() >= 2) && (parts[0] == "items")) {
      auto const spec = kindSpecs().constFind(parts[1]);
      if (spec == kindSpecs().constEnd()) {
        throw HttpError(404, QStringLiteral("Unknown item kind."));
      }
      if ((request.method == "GET") && (parts.size() == 2)) {
        response = QJsonObject{{"items", CollabHostStore::listItems(parts[1])},
                               {"deleted", QJsonArray::fromStringList(CollabHostStore::listDeletedUids(parts[1]))}};
      } else if ((request.method == "POST") && (parts.size() == 2)) {
        QJsonObject const payload = parseJson(body);
        QString const     uid     = payload.value("uid").toString();
        if (!UID_PATTERN.match(uid).hasMatch()) {
          throw HttpError(400, QStringLiteral("Invalid uid."));
        }
        QJsonObject data;
        for (Field const &field : spec->fields) {
          if (!payload.contains(QLatin1String(field.name))) {
            if (field.required) {
              throw HttpError(400, QStringLiteral("Invalid item."));
            }
            continue;
          }
          data.insert(QLatin1String(field.name), convert(payload.value(QLatin1String(field.name)), field.type));
        }
        CollabHostStore::putItem(parts[1], uid, data, spec->mutableKind);
        emit itemsChanged();
        status   = 201;
        response = QJsonObject{{"uid", uid}};
      } else if ((request.method == "POST") && (parts.size() == 4) && (parts[3] == "delete")) {
        response = QJsonObject{{"deleted", CollabHostStore::deleteItem(parts[1], parts[2])}};
        emit itemsChanged();
      } else {
        throw HttpError(404, QStringLiteral("Not found."));
      }
    } else if ((parts.size() >= 1) && (parts[0] == "runs")) {
      // full experiments (compressed samples_json)
      if ((request.method == "GET") && (parts.size() == 1)) {
        response = QJsonObject{{"uids", QJsonArray::fromStringList(CollabHostStore::runDataUids())}};
      } else if ((parts.size() == 2) && UID_PATTERN.match(parts[1]).hasMatch()) {
        if (request.method == "GET") {
          QJsonObject metadata;
          QByteArray  samples;
          if (!CollabHostStore::runData(parts[1], metadata, samples)) {
            throw HttpError(404, QStringLiteral("Not found."));
          }
          response = QJsonObject{{"uid", parts[1]},
                                 {"metadata", metadata},
                                 {"samples_z", QString::fromLatin1(samples.toBase64())}};
        } else if (request.method == "POST") {
          QJsonObject const payload = parseJson(body);
          QByteArray const  samples = QByteArray::fromBase64(payload.value("samples_z").toString().toLatin1());
          if (samples.isEmpty() || !payload.value("metadata").isObject()) {
            throw HttpError(400, QStringLiteral("Invalid run data."));
          }
          CollabHostStore::putRunData(parts[1], payload.value("metadata").toObject(), samples);
          emit itemsChanged();
          status   = 201;
          response = QJsonObject{{"uid", parts[1]}};
        } else {
          throw HttpError(404, QStringLiteral("Not found."));
        }
      } else {
        throw HttpError(404, QStringLiteral("Not found."));
      }
    } else {
      throw HttpError(404, QStringLiteral("Not found."));
    }
  } catch (HttpError const &error) {
    status   = error.status;
    response = QJsonObject{{"detail", error.detail}};
  }
  send(socket, status, QJsonDocument(response).toJson(QJsonDocument::Compact));
}

void CollabServer::send(QSslSocket *socket, int status, QByteArray const &json) {
  QByteArray header = "HTTP/1.1 " + QByteArray::number(status) + ' ' + reason(status) + "\r\n";
  header += "Server: DeepvacCollab\r\nContent-Type: application/json\r\n";
  header += "Content-Length: " + QByteArray::number(json.size()) + "\r\nConnection: close\r\n\r\n";
  socket->write(header + json);
  socket->disconnectFromHost(); // after the pending data is written
}

} // namespace deepvaccontrol::collab
