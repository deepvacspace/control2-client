#ifndef TESTPROFILESSTORE_H
#define TESTPROFILESSTORE_H

#include <QJsonObject>
#include <QString>
#include <QVector>
#include <optional>

namespace deepvaccontrol::storage {

struct TestProfileStep {
  double  setpointTemp{qQNaN()};     // NaN = not set
  double  setpointPressure{qQNaN()}; // NaN = not set
  double  durationS{0.0};
  QString label;
};

struct TestProfile {
  qint64                   id{-1};
  QString                  name;
  QString                  description;
  QString                  createdBy;
  QString                  createdAt;
  QVector<TestProfileStep> steps;
  // control-app extensions
  QString collabUid;
  QString updatedAt;
  QString programJson; // complete PLC program (ProgramModel::programVariant()), steps hold its summary + labels

  [[nodiscard]] double totalDurationS() const;
};

/**
 * @brief Test profiles in Deepvac Insight's format (deepvac_test_profiles.sqlite3):
 * named multi-step schedules of temperature / pressure setpoints, each held for a duration.
 */
class TestProfilesStore {
public:
  static QVector<TestProfile>       list(QString *error = nullptr);
  static std::optional<TestProfile> get(qint64 id);
  static std::optional<TestProfile> findByCollabUid(QString const &uid);

  // validation and error texts follow Insight's test_profiles_service
  // a stored program may have steps without temperature / pressure (e.g. humidity only)
  static bool validate(QString const &name, QVector<TestProfileStep> const &steps, QString *error, bool hasProgram = false);
  static std::optional<TestProfile> add(QString const &name, QString const &description, QVector<TestProfileStep> const &steps,
                                        QString const &createdBy, QString *error = nullptr, QString const &programJson = {});
  static std::optional<TestProfile> update(qint64 id, QString const &name, QString const &description,
                                           QVector<TestProfileStep> const &steps, QString *error = nullptr,
                                           QString const &programJson = {});
  static std::optional<TestProfile> findByName(QString const &name);
  static bool remove(qint64 id);

  // collaboration helpers
  static bool        setCollabUid(qint64 id, QString const &uid);
  static QStringList tombstones();
  static void        clearTombstone(QString const &uid);
  static bool        removeWithoutTombstone(qint64 id);
  // inserts or updates a profile received from a peer (keeps its timestamps and uid)
  static bool        upsertFromRemote(QJsonObject const &item, QString *error = nullptr);
  static QJsonObject toJson(TestProfile const &profile);
};

}

#endif // TESTPROFILESSTORE_H
