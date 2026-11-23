#include "collabclient.h"

#include <QEventLoop>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSslCertificate>
#include <QSslSocket>
#include <QTimer>
#include <QUrl>

#include "collabcrypto.h"
#include "collabserver.h"

namespace deepvaccontrol::collab {

namespace {
constexpr int FETCH_TIMEOUT_MS{5000};

void setError(QString *error, QString const &text) {
  if (error) {
    *error = text;
  }
}
}

CollabClient::CollabClient(Connection connection) : m_connection{std::move(connection)} {
  CollabCrypto::loadOrCreateDeviceKey(m_privateKey, m_publicKey);
}

QString CollabClient::normalizeHostUrl(QString const &text, QString *error) {
  QString const raw = text.trimmed();
  QUrl const    url = QUrl::fromUserInput(raw.contains("://") ? raw : QStringLiteral("https://%1").arg(raw));
  if (raw.isEmpty() || url.host().isEmpty()) {
    setError(error, QObject::tr("Enter the host's address, for example 192.168.1.20:8765."));
    return {};
  }
  return QStringLiteral("https://%1:%2").arg(url.host()).arg(url.port(CollabServer::DEFAULT_PORT));
}

QByteArray CollabClient::fetchCertificate(QString const &url, QString *error) {
  QUrl const parsed(normalizeHostUrl(url, error));
  if (parsed.host().isEmpty()) {
    return {};
  }
  QSslSocket socket;
  socket.setPeerVerifyMode(QSslSocket::VerifyNone); // only reading the certificate, nothing is trusted yet
  socket.connectToHostEncrypted(parsed.host(), static_cast<quint16>(parsed.port()));
  if (!socket.waitForEncrypted(FETCH_TIMEOUT_MS)) {
    setError(error, QObject::tr("Could not reach %1:%2: %3").arg(parsed.host()).arg(parsed.port()).arg(socket.errorString()));
    return {};
  }
  QByteArray const pem = socket.peerCertificate().toPem();
  socket.disconnectFromHost();
  return pem;
}

QString CollabClient::pair(QString const &url, QString const &joinCode, QString const &displayName, QByteArray const &certPem,
                           QString *error) {
  QByteArray privateKey;
  QByteArray publicKey;
  if (!CollabCrypto::loadOrCreateDeviceKey(privateKey, publicKey, error)) {
    return {};
  }
  QJsonObject const payload{{"join_code", joinCode},
                            {"device_public_key", QString::fromLatin1(publicKey.toBase64(QByteArray::Base64UrlEncoding))},
                            {"display_name", displayName}};
  QJsonObject response;
  if (!send("POST", normalizeHostUrl(url) + "/pair", QJsonDocument(payload).toJson(QJsonDocument::Compact), certPem, {},
            response, error, nullptr, FETCH_TIMEOUT_MS * 3)) {
    return {};
  }
  return response.value("host_name").toString(QUrl(url).host());
}

bool CollabClient::request(QByteArray const &method, QString const &path, QJsonObject const *payload, QJsonObject &response,
                           QString *error, int *status, int timeoutMs) {
  if (!m_connection.isValid()) {
    setError(error, QObject::tr("Not connected to a collaboration host."));
    return false;
  }
  // the host verifies the signature over the exact body bytes
  QByteArray const body = payload ? QJsonDocument(*payload).toJson(QJsonDocument::Compact) : QByteArray{};
  QByteArray const signature = CollabCrypto::sign(m_privateKey, body);
  if (signature.isEmpty()) {
    setError(error, QObject::tr("Could not sign the request (device key missing)."));
    return false;
  }
  QList<QPair<QByteArray, QByteArray>> const headers{
    {"X-Device-Key-Hash", CollabCrypto::keyHash(m_publicKey).toLatin1()},
    {"X-Device-Signature", signature.toBase64(QByteArray::Base64UrlEncoding)}};
  return send(method, m_connection.url + path, body, m_connection.certPem, headers, response, error, status, timeoutMs);
}

bool CollabClient::send(QByteArray const &method, QString const &url, QByteArray const &body, QByteArray const &certPem,
                        QList<QPair<QByteArray, QByteArray>> const &headers, QJsonObject &response, QString *error, int *status,
                        int timeoutMs) {
  QSslCertificate const pinned(certPem, QSsl::Pem);
  QByteArray const      pinnedDigest = pinned.digest(QCryptographicHash::Sha256);

  QNetworkAccessManager manager;
  QNetworkRequest       request{QUrl(url)};
  request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
  request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
  for (auto const &[name, value] : headers) {
    request.setRawHeader(name, value);
  }

  QNetworkReply *reply = manager.sendCustomRequest(request, method, body);
  bool           identityChanged{false};
  // trust exactly the pinned certificate: self-signed / host name errors are expected for it, and only for it
  QObject::connect(reply, &QNetworkReply::sslErrors, reply, [&](QList<QSslError> const &errors) {
    QSslCertificate const presented = reply->sslConfiguration().peerCertificate();
    if (!pinnedDigest.isEmpty() && (presented.digest(QCryptographicHash::Sha256) == pinnedDigest)) {
      reply->ignoreSslErrors(errors);
    } else {
      identityChanged = true;
    }
  });

  QEventLoop loop;
  QTimer     timer;
  timer.setSingleShot(true);
  QObject::connect(&timer, &QTimer::timeout, reply, &QNetworkReply::abort);
  QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
  timer.start(timeoutMs);
  loop.exec();

  int const        httpStatus{reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()};
  QByteArray const data = reply->readAll();
  if (status) {
    *status = httpStatus;
  }
  response = QJsonDocument::fromJson(data).object();

  bool ok{true};
  if (identityChanged) {
    setError(error, QObject::tr("The host's identity changed. Disconnect and pair again if you trust this host."));
    ok = false;
  } else if (!timer.isActive()) {
    setError(error, QObject::tr("The host did not answer in time."));
    ok = false;
  } else if (httpStatus == 0) {
    setError(error, QObject::tr("Could not reach the host: %1").arg(reply->errorString()));
    ok = false;
  } else if (httpStatus >= 400) {
    setError(error, response.value("detail").toString(QObject::tr("Host error %1").arg(httpStatus)));
    ok = false;
  }
  reply->deleteLater();
  return ok;
}

}
