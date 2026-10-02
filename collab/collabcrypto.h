#ifndef COLLABCRYPTO_H
#define COLLABCRYPTO_H

#include <QByteArray>
#include <QString>

namespace deepvaccontrol::collab {

// Cryptography of the local-network sharing between Deepvac Control apps. Implemented with OpenSSL 3.
class CollabCrypto {
public:
  [[nodiscard]] static bool available(); // built with OpenSSL

  // Host TLS identity: self-signed ECDSA P-256 certificate (10 years) that peers pin.
  // Created on first use in data/collab/host_cert.pem + host_key.pem.
  static bool loadOrCreateHostIdentity(QByteArray &certPem, QByteArray &keyPem, QString *error = nullptr);

  // SHA-256 fingerprint of a PEM certificate as colon separated hex (what users compare when pairing).
  static QString fingerprint(QByteArray const &certPem);

  // This installation's Ed25519 device key (raw 32 bytes, data/collab/device_ed25519.key).
  static bool       loadOrCreateDeviceKey(QByteArray &privateRaw, QByteArray &publicRaw, QString *error = nullptr);
  static QByteArray sign(QByteArray const &privateRaw, QByteArray const &data);
  static bool       verify(QByteArray const &publicRaw, QByteArray const &data, QByteArray const &signature);
  // urlsafe base64 (with padding) of SHA-256(public key): how hosts identify paired devices
  static QString keyHash(QByteArray const &publicRaw);

  static QString collabDir();
};

} // namespace deepvaccontrol::collab

#endif // COLLABCRYPTO_H
