#ifndef CORE_RUN_RUNMANIFEST_H
#define CORE_RUN_RUNMANIFEST_H

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QStringList>
#include <QVariantList>
#include <QVector>
#include <limits>

namespace deepvaccontrol::core {

/**
 * Self-describing run ("deepvac.run/1"): everything needed to understand, reproduce and compare an
 * experiment without the settings that happened to be active when it is opened later.
 *
 *   Run
 *   ├── identity & metadata        RunManifest (uid, chamber, times, app version; DUT, operator, purpose, tags)
 *   ├── configuration snapshot     RunManifest::configuration (captured when the job was sent)
 *   ├── channel definitions        RunManifest::channels (name, unit, quantity, role)
 *   ├── measurements               samples (RunStore samples_json, ms timestamps)
 *   ├── stages, KPIs, anomalies    ExperimentAnalysis (RunStore summary_json)
 *   ├── typed events               RunManifest::events
 *   ├── annotations                AnnotationsDb
 *   └── integrity / version        RunManifest::samplesSha256, schema / analysis versions
 */
inline constexpr char RUN_MANIFEST_SCHEMA[]{"deepvac.run/1"};

// ---------------------------------------------------------------- channels

// Roles of a recorded channel.
namespace channelrole {
inline constexpr char MEASURED[]{"measured"};
inline constexpr char SETPOINT[]{"setpoint"};
inline constexpr char OUTPUT[]{"output"};
inline constexpr char OUTPUT_COMPONENT[]{"output_component"}; // P / I / D terms
inline constexpr char GAIN[]{"gain"};
inline constexpr char INDEX[]{"index"};                       // e.g. active PID band
inline constexpr char STATUS[]{"status"};
inline constexpr char PROGRAM[]{"program"};                   // interval / cycle position
inline constexpr char TIMER[]{"timer"};
inline constexpr char BITMASK[]{"bitmask"};                   // digital inputs / outputs
inline constexpr char ALARM[]{"alarm"};                       // alarm words
inline constexpr char OTHER[]{"other"};
} // namespace channelrole

struct ChannelMeta {
  QString name;     // state name as recorded ("temp")
  QString label;    // description without the unit ("Chamber temperature")
  QString unit;     // engineering unit ("°C", "%", "bar", "s"), empty when dimensionless
  QString quantity; // "temperature", "pressure", "control output", ...
  QString role;     // channelrole::*

  [[nodiscard]] QJsonObject toJson() const;
  static ChannelMeta        fromJson(QJsonObject const &json);
};

// Metadata of one channel: explicit "unit" / "quantity" / "role" keys of its states config entry win,
// otherwise the unit is read from the label ("Chamber temperature, °C") and role / quantity from the name.
ChannelMeta channelMeta(QVariantMap const &stateConfig);
// metadata of the recorded channels (names), from the "states" settings section
QVector<ChannelMeta> channelMetadata(QVariantList const &statesConfig, QStringList const &recordedNames);

// ---------------------------------------------------------------- events

namespace eventtype {
inline constexpr char JOB_START[]{"job_start"};
inline constexpr char JOB_END[]{"job_end"};
inline constexpr char SETPOINT_CHANGE[]{"setpoint_change"};
inline constexpr char RAMP_START[]{"ramp_start"};
inline constexpr char RAMP_END[]{"ramp_end"};
inline constexpr char PID_GAINS_CHANGE[]{"pid_gains_change"};
inline constexpr char PID_BAND_CHANGE[]{"pid_band_change"};
inline constexpr char PID_TABLE_WRITTEN[]{"pid_table_written"};
inline constexpr char STATUS_CHANGE[]{"status_change"};
inline constexpr char PROGRAM_INTERVAL[]{"program_interval"};
inline constexpr char PROGRAM_CYCLE[]{"program_cycle"};
inline constexpr char ALARM_RAISED[]{"alarm_raised"};
inline constexpr char ALARM_CLEARED[]{"alarm_cleared"};
inline constexpr char ALARM_RECEIVED[]{"alarm_received"};
inline constexpr char OPERATOR_ACTION[]{"operator_action"};
inline constexpr char PLC_COMMAND[]{"plc_command"};
inline constexpr char APP_ERROR[]{"error"};
inline constexpr char USER_CHANGE[]{"user_change"};
} // namespace eventtype

struct RunEvent {
  double      time{0.0}; // epoch s, ms resolution
  QString     type;      // eventtype::*
  QString     source;    // "app": captured live by the application; "signal": from the recorded channels
  QString     message;   // human readable (English)
  QJsonObject data;      // type specific values (from / to, gains, bits, ...)

  [[nodiscard]] QJsonObject toJson() const;
  static RunEvent           fromJson(QJsonObject const &json);
};

// Events visible in the recorded channels, at the exact sample where they happen: setpoint steps,
// ramps, PID gain and band changes, status transitions, program interval / cycle changes, alarm-bit
// edges. times: epoch s; channels by state name (missing ones are skipped); alarmLabels: bit -> label.
QVector<RunEvent> detectEvents(QVector<double> const &times, QHash<QString, QVector<double>> const &channels,
                               QMap<int, QString> const &alarmLabels = {}, double setpointTolerance = 0.05);
// alarm labels of the "alarms" settings section ([{index, label}])
QMap<int, QString> alarmLabels(QVariantList const &alarmsConfig);
QString            statusName(int statusCondition); // status_condition value -> "idle", "work", ...

// ---------------------------------------------------------------- metadata and manifest

struct RunMetadata {
  QString     dut;      // device under test
  QString     operatorName;
  QString     purpose;
  QStringList tags;
  QString     notes;

  [[nodiscard]] bool        isEmpty() const;
  [[nodiscard]] QJsonObject toJson() const;
  [[nodiscard]] QString     searchText() const; // all fields, for searching
  static RunMetadata        fromJson(QJsonObject const &json);
};

struct RunManifest {
  QString schema{QString::fromLatin1(RUN_MANIFEST_SCHEMA)};
  // identity
  QString runUid;
  QString name;
  QString chamber;
  QString chamberRef;
  double  start{std::numeric_limits<double>::quiet_NaN()}; // epoch s
  double  end{std::numeric_limits<double>::quiet_NaN()};
  QString appName;
  QString appVersion;
  double  timeResolutionS{0.001};
  RunMetadata metadata;
  // captured when the job was sent: job (kind, label, program / profile), plc_settings (general, pids,
  // calibrations), control_params, alarms, app, user, chamber
  QJsonObject          configuration;
  QVector<ChannelMeta> channels;
  QVector<RunEvent>    events; // sorted by time
  // integrity
  QString samplesSha256; // of the stored samples (RunStore samples_json)
  qint64  sampleCount{0};
  int     analysisVersion{0};

  [[nodiscard]] bool        isValid() const; // has the schema and a uid
  [[nodiscard]] QJsonObject toJson() const;
  static RunManifest        fromJson(QJsonObject const &json);
};

QString sha256Hex(QByteArray const &data);
void    sortEvents(QVector<RunEvent> &events);

} // namespace deepvaccontrol::core

#endif // CORE_RUN_RUNMANIFEST_H
