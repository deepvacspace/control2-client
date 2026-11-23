#ifndef COLLABHOSTSTORE_H
#define COLLABHOSTSTORE_H

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <optional>

namespace deepvaccontrol::collab {

struct Peer {
  QString    keyHash;
  QByteArray publicKey;
  QString    displayName;
  QString    pairedAt;
};

/**
 * @brief Host-side storage of the sharing server: paired devices, shared items and
 * full experiments (compressed samples_json) in data/deepvac_collab_host.sqlite3.
 */
class CollabHostStore {
public:
  static QString             registerPeer(QByteArray const &publicKey, QString const &displayName);
  static std::optional<Peer> peer(QString const &keyHash);
  static QVector<Peer>       peers();
  static bool                removePeer(QString const &keyHash);

  static QJsonArray  listItems(QString const &kind);       // [{uid, ...data}]
  static QStringList listDeletedUids(QString const &kind);
  // immutable kinds ignore a known uid (even a deleted one); mutable kinds overwrite a live item
  static void putItem(QString const &kind, QString const &uid, QJsonObject const &data, bool mutableKind);
  static bool deleteItem(QString const &kind, QString const &uid);

  // control-app extension: experiments with their samples
  static QStringList runDataUids();
  static bool        putRunData(QString const &uid, QJsonObject const &metadata, QByteArray const &compressedSamples);
  static bool        runData(QString const &uid, QJsonObject &metadata, QByteArray &compressedSamples);
};

}

#endif // COLLABHOSTSTORE_H
