#include "collabcrypto.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QHostInfo>
#include <QSslCertificate>

#include "sqlitedb.h"

#ifdef DEEPVACCONTROL_HAVE_OPENSSL
#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509v3.h>
#endif

namespace deepvaccontrol::collab {

namespace {
constexpr int  VALIDITY_DAYS{3650};
constexpr int  ED25519_KEY_BYTES{32};
QString const  CERT_NAME{"host_cert.pem"};
QString const  KEY_NAME{"host_key.pem"};
QString const  DEVICE_KEY_NAME{"device_ed25519.key"};

bool writePrivateFile(QString const &path, QByteArray const &data) {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return false;
  }
  file.write(data);
  file.close();
  file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
  return true;
}

void setError(QString *error, QString const &text) {
  if (error) {
    *error = text;
  }
}

#ifdef DEEPVACCONTROL_HAVE_OPENSSL
QByteArray bioToBytes(BIO *bio) {
  char      *data{nullptr};
  long const size{BIO_get_mem_data(bio, &data)};
  return QByteArray(data, static_cast<qsizetype>(size));
}

bool addExtension(X509 *cert, int nid, char const *value) {
  X509V3_CTX ctx;
  X509V3_set_ctx_nodb(&ctx);
  X509V3_set_ctx(&ctx, cert, cert, nullptr, nullptr, 0);
  X509_EXTENSION *extension = X509V3_EXT_conf_nid(nullptr, &ctx, nid, value);
  if (!extension) {
    return false;
  }
  bool const ok{X509_add_ext(cert, extension, -1) == 1};
  X509_EXTENSION_free(extension);
  return ok;
}

// self-signed ECDSA P-256 CA certificate, CN = host name, valid 10 years
bool createHostIdentity(QByteArray &certPem, QByteArray &keyPem) {
  EVP_PKEY *key = EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", "P-256");
  X509     *cert = X509_new();
  bool      ok{key && cert};

  if (ok) {
    QByteArray const host = QHostInfo::localHostName().toUtf8().isEmpty() ? QByteArray("deepvac-host")
                                                                           : QHostInfo::localHostName().toUtf8();
    BIGNUM *serial = BN_new();
    ok = ok && serial && (BN_rand(serial, 159, BN_RAND_TOP_ANY, BN_RAND_BOTTOM_ANY) == 1)
         && BN_to_ASN1_INTEGER(serial, X509_get_serialNumber(cert));
    BN_free(serial);

    X509_NAME *name = X509_get_subject_name(cert);
    ok = ok && (X509_set_version(cert, 2) == 1)
         && X509_gmtime_adj(X509_getm_notBefore(cert), -24L * 3600L)
         && X509_gmtime_adj(X509_getm_notAfter(cert), static_cast<long>(VALIDITY_DAYS) * 24L * 3600L)
         && (X509_set_pubkey(cert, key) == 1)
         && (X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_UTF8,
                                        reinterpret_cast<unsigned char const *>(host.constData()), -1, -1, 0) == 1)
         && (X509_set_issuer_name(cert, name) == 1)
         && addExtension(cert, NID_basic_constraints, "critical,CA:TRUE,pathlen:0")
         && addExtension(cert, NID_subject_key_identifier, "hash")
         && addExtension(cert, NID_key_usage, "critical,digitalSignature,keyCertSign")
         && (X509_sign(cert, key, EVP_sha256()) > 0);
  }

  if (ok) {
    BIO *certBio = BIO_new(BIO_s_mem());
    BIO *keyBio  = BIO_new(BIO_s_mem());
    ok           = (PEM_write_bio_X509(certBio, cert) == 1)
         && (PEM_write_bio_PrivateKey(keyBio, key, nullptr, nullptr, 0, nullptr, nullptr) == 1); // PKCS#8
    if (ok) {
      certPem = bioToBytes(certBio);
      keyPem  = bioToBytes(keyBio);
    }
    BIO_free(certBio);
    BIO_free(keyBio);
  }

  X509_free(cert);
  EVP_PKEY_free(key);
  return ok;
}
#endif
} // namespace

bool CollabCrypto::available() {
#ifdef DEEPVACCONTROL_HAVE_OPENSSL
  return true;
#else
  return false;
#endif
}

QString CollabCrypto::collabDir() {
  return QDir(storage::dataDir()).filePath("collab");
}

bool CollabCrypto::loadOrCreateHostIdentity(QByteArray &certPem, QByteArray &keyPem, QString *error) {
  QDir const dir(collabDir());
  QFile      certFile(dir.filePath(CERT_NAME));
  QFile      keyFile(dir.filePath(KEY_NAME));
  if (certFile.exists() && keyFile.exists() && certFile.open(QIODevice::ReadOnly) && keyFile.open(QIODevice::ReadOnly)) {
    certPem = certFile.readAll();
    keyPem  = keyFile.readAll();
    return true;
  }

#ifdef DEEPVACCONTROL_HAVE_OPENSSL
  if (!createHostIdentity(certPem, keyPem)) {
    setError(error, QObject::tr("Could not create the host certificate"));
    return false;
  }
  QDir().mkpath(dir.absolutePath());
  if (!writePrivateFile(dir.filePath(KEY_NAME), keyPem) || !writePrivateFile(dir.filePath(CERT_NAME), certPem)) {
    setError(error, QObject::tr("Could not save the host certificate in %1").arg(dir.absolutePath()));
    return false;
  }
  return true;
#else
  setError(error, QObject::tr("This build has no OpenSSL support"));
  return false;
#endif
}

QString CollabCrypto::fingerprint(QByteArray const &certPem) {
  QSslCertificate const certificate(certPem, QSsl::Pem);
  if (certificate.isNull()) {
    return {};
  }
  QByteArray const digest = certificate.digest(QCryptographicHash::Sha256).toHex().toUpper();
  QStringList      pairs;
  for (qsizetype i{0}; i < digest.size(); i += 2) {
    pairs.push_back(QString::fromLatin1(digest.mid(i, 2)));
  }
  return pairs.join(':');
}

bool CollabCrypto::loadOrCreateDeviceKey(QByteArray &privateRaw, QByteArray &publicRaw, QString *error) {
#ifdef DEEPVACCONTROL_HAVE_OPENSSL
  QString const path = QDir(collabDir()).filePath(DEVICE_KEY_NAME);
  QFile         file(path);
  if (file.exists() && file.open(QIODevice::ReadOnly)) {
    privateRaw = file.readAll();
    file.close();
  }

  EVP_PKEY *key{nullptr};
  if (privateRaw.size() == ED25519_KEY_BYTES) {
    key = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr,
                                       reinterpret_cast<unsigned char const *>(privateRaw.constData()), privateRaw.size());
  } else {
    key = EVP_PKEY_Q_keygen(nullptr, nullptr, "ED25519");
    size_t length{ED25519_KEY_BYTES};
    privateRaw.resize(ED25519_KEY_BYTES);
    if (key && (EVP_PKEY_get_raw_private_key(key, reinterpret_cast<unsigned char *>(privateRaw.data()), &length) == 1)) {
      QDir().mkpath(collabDir());
      if (!writePrivateFile(path, privateRaw)) {
        setError(error, QObject::tr("Could not save the device key in %1").arg(collabDir()));
        EVP_PKEY_free(key);
        return false;
      }
    }
  }
  if (!key) {
    setError(error, QObject::tr("Could not create the device key"));
    return false;
  }

  size_t length{ED25519_KEY_BYTES};
  publicRaw.resize(ED25519_KEY_BYTES);
  bool const ok{EVP_PKEY_get_raw_public_key(key, reinterpret_cast<unsigned char *>(publicRaw.data()), &length) == 1};
  EVP_PKEY_free(key);
  return ok;
#else
  Q_UNUSED(privateRaw)
  Q_UNUSED(publicRaw)
  setError(error, QObject::tr("This build has no OpenSSL support"));
  return false;
#endif
}

QByteArray CollabCrypto::sign(QByteArray const &privateRaw, QByteArray const &data) {
#ifdef DEEPVACCONTROL_HAVE_OPENSSL
  EVP_PKEY *key = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr,
                                               reinterpret_cast<unsigned char const *>(privateRaw.constData()), privateRaw.size());
  EVP_MD_CTX *ctx = EVP_MD_CTX_new();
  QByteArray  signature(64, '\0');
  size_t      length{64};
  bool const  ok{key && ctx && (EVP_DigestSignInit(ctx, nullptr, nullptr, nullptr, key) == 1)
                && (EVP_DigestSign(ctx, reinterpret_cast<unsigned char *>(signature.data()), &length,
                                   reinterpret_cast<unsigned char const *>(data.constData()), data.size())
                    == 1)};
  EVP_MD_CTX_free(ctx);
  EVP_PKEY_free(key);
  return ok ? signature.left(static_cast<qsizetype>(length)) : QByteArray{};
#else
  Q_UNUSED(privateRaw)
  Q_UNUSED(data)
  return {};
#endif
}

bool CollabCrypto::verify(QByteArray const &publicRaw, QByteArray const &data, QByteArray const &signature) {
#ifdef DEEPVACCONTROL_HAVE_OPENSSL
  if (publicRaw.size() != ED25519_KEY_BYTES) {
    return false;
  }
  EVP_PKEY *key = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr,
                                              reinterpret_cast<unsigned char const *>(publicRaw.constData()), publicRaw.size());
  EVP_MD_CTX *ctx = EVP_MD_CTX_new();
  bool const  ok{key && ctx && (EVP_DigestVerifyInit(ctx, nullptr, nullptr, nullptr, key) == 1)
                && (EVP_DigestVerify(ctx, reinterpret_cast<unsigned char const *>(signature.constData()), signature.size(),
                                     reinterpret_cast<unsigned char const *>(data.constData()), data.size())
                    == 1)};
  EVP_MD_CTX_free(ctx);
  EVP_PKEY_free(key);
  return ok;
#else
  Q_UNUSED(publicRaw)
  Q_UNUSED(data)
  Q_UNUSED(signature)
  return false;
#endif
}

QString CollabCrypto::keyHash(QByteArray const &publicRaw) {
  return QString::fromLatin1(QCryptographicHash::hash(publicRaw, QCryptographicHash::Sha256).toBase64(QByteArray::Base64UrlEncoding));
}

} // namespace deepvaccontrol::collab
