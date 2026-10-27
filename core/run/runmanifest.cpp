#include "runmanifest.h"

#include <QLocale>

#include <QCryptographicHash>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <tuple>

#include "segmentation.h"

namespace deepvaccontrol::core {

namespace {
QJsonValue number(double value) {
  return std::isfinite(value) ? QJsonValue(value) : QJsonValue(QJsonValue::Null);
}

double numberOf(QJsonValue const &value) {
  return value.isDouble() ? value.toDouble() : std::numeric_limits<double>::quiet_NaN();
}

QString text(double value) {
  return QString::number(value, 'g', 6);
}

// temperatures and rates in messages: at most two decimals
QString degrees(double value) {
  return QString::number(std::round(value * 100.0) / 100.0, 'g', 8);
}

}

// ---------------------------------------------------------------- channels

QJsonObject ChannelMeta::toJson() const {
  return QJsonObject{{"name", name}, {"label", label}, {"unit", unit}, {"quantity", quantity}, {"role", role}};
}

ChannelMeta ChannelMeta::fromJson(QJsonObject const &json) {
  return ChannelMeta{json.value("name").toString(), json.value("label").toString(), json.value("unit").toString(),
                     json.value("quantity").toString(), json.value("role").toString()};
}

ChannelMeta channelMeta(QVariantMap const &stateConfig) {
  ChannelMeta meta;
  meta.name           = stateConfig.value("name").toString();
  QString const label = stateConfig.value("label").toString();
  // "Chamber temperature, °C" -> label + unit
  static QRegularExpression const unitSuffix(QStringLiteral("^(.*),\\s*([^,]{1,12})$"));
  QRegularExpressionMatch const   match = unitSuffix.match(label);
  meta.label                            = match.hasMatch() ? match.captured(1).trimmed() : label;
  meta.unit                             = match.hasMatch() ? match.captured(2).trimmed() : QString();

  QString const name = meta.name;
  auto          is   = [&name](char const *prefix) { return name.startsWith(QLatin1String(prefix)); };
  if (name == QLatin1String("temp_ref")) {
    meta.role     = channelrole::SETPOINT;
    meta.quantity = QStringLiteral("temperature");
  } else if (name == QLatin1String("temp_u")) {
    meta.role     = channelrole::OUTPUT;
    meta.quantity = QStringLiteral("control output");
  } else if (is("temp_u_")) {
    meta.role     = channelrole::OUTPUT_COMPONENT;
    meta.quantity = QStringLiteral("control output");
  } else if ((name == QLatin1String("temp_kp")) || (name == QLatin1String("temp_ki")) || (name == QLatin1String("temp_kd"))) {
    meta.role     = channelrole::GAIN;
    meta.quantity = QStringLiteral("controller gain");
  } else if (name == QLatin1String("temp_pid_idx")) {
    meta.role     = channelrole::INDEX;
    meta.quantity = QStringLiteral("PID band");
  } else if ((name == QLatin1String("status_condition")) || (name == QLatin1String("work_type"))) {
    meta.role     = channelrole::STATUS;
    meta.quantity = QStringLiteral("state");
  } else if ((name == QLatin1String("interval")) || is("intervals_") || is("cycle_")) {
    meta.role     = channelrole::PROGRAM;
    meta.quantity = QStringLiteral("program position");
  } else if (is("t_")) {
    meta.role     = channelrole::TIMER;
    meta.quantity = QStringLiteral("time");
    if (meta.unit.isEmpty()) {
      meta.unit = QStringLiteral("s");
    }
  } else if ((name == QLatin1String("inputs")) || (name == QLatin1String("outputs"))) {
    meta.role     = channelrole::BITMASK;
    meta.quantity = QStringLiteral("digital I/O");
  } else if (is("alarm_")) {
    meta.role     = channelrole::ALARM;
    meta.quantity = QStringLiteral("alarm word");
  } else if (is("temp")) {
    meta.role     = channelrole::MEASURED;
    meta.quantity = QStringLiteral("temperature");
  } else if (is("pres")) {
    meta.role     = channelrole::MEASURED;
    meta.quantity = QStringLiteral("pressure");
  } else {
    meta.role = channelrole::OTHER;
  }
  // explicit configuration wins
  for (auto const &[key, field] : {std::pair{"unit", &meta.unit}, std::pair{"quantity", &meta.quantity}, std::pair{"role", &meta.role}}) {
    if (stateConfig.contains(QLatin1String(key)) && !stateConfig.value(QLatin1String(key)).toString().isEmpty()) {
      *field = stateConfig.value(QLatin1String(key)).toString();
    }
  }
  return meta;
}

QVector<ChannelMeta> channelMetadata(QVariantList const &statesConfig, QStringList const &recordedNames) {
  QHash<QString, QVariantMap> byName;
  for (QVariant const &entry : statesConfig) {
    QVariantMap const map = entry.toMap();
    byName.insert(map.value("name").toString(), map);
  }
  QVector<ChannelMeta> result;
  for (QString const &name : recordedNames) {
    QVariantMap config = byName.value(name);
    config.insert("name", name);
    result.push_back(channelMeta(config));
  }
  return result;
}

// ---------------------------------------------------------------- events

QJsonObject RunEvent::toJson() const {
  QJsonObject json{{"t", time}, {"type", type}, {"source", source}, {"message", message}};
  if (!data.isEmpty()) {
    json.insert("data", data);
  }
  return json;
}

RunEvent RunEvent::fromJson(QJsonObject const &json) {
  return RunEvent{json.value("t").toDouble(), json.value("type").toString(), json.value("source").toString(),
                  json.value("message").toString(), json.value("data").toObject()};
}

QString statusName(int statusCondition) {
  switch (statusCondition) {
    case 1:
      return QStringLiteral("idle");
    case 2:
      return QStringLiteral("work");
    case 3:
      return QStringLiteral("finish");
    case 4:
      return QStringLiteral("alarm");
    default:
      return QStringLiteral("none");
  }
}

QMap<int, QString> alarmLabels(QVariantList const &alarmsConfig) {
  QMap<int, QString> labels;
  for (QVariant const &entry : alarmsConfig) {
    QVariantMap const alarm = entry.toMap();
    labels.insert(alarm.value("index").toInt(), alarm.value("label").toString());
  }
  return labels;
}

void sortEvents(QVector<RunEvent> &events) {
  std::stable_sort(events.begin(), events.end(), [](RunEvent const &a, RunEvent const &b) { return a.time < b.time; });
}

QVector<RunEvent> detectEvents(QVector<double> const &times, QHash<QString, QVector<double>> const &channels,
                               QMap<int, QString> const &alarmLabels, double setpointTolerance) {
  QVector<RunEvent> events;
  qsizetype const   n{times.size()};
  auto              series = [&](char const *name) {
    QVector<double> const values = channels.value(QString::fromLatin1(name));
    return values.size() >= n ? values : QVector<double>{};
  };
  auto add = [&events](double time, char const *type, QString const &message, QJsonObject const &data) {
    events.push_back(RunEvent{time, QString::fromLatin1(type), QStringLiteral("signal"), message, data});
  };

  // setpoint ramps (from the stage segmentation) and steps (jumps between samples outside ramps)
  QVector<double> const reference = series("temp_ref");
  if (!reference.isEmpty()) {
    QVector<Segment> const stages = segmentExperiment(times, series("temp"), reference, series("interval"), series("cycle_cur"));
    QVector<bool>          inRamp(n, false);
    for (Segment const &stage : stages) {
      if (stage.kind != Segment::Kind::RAMP) {
        continue;
      }
      for (int i{stage.first + 1}; (i <= stage.last) && (i < n); ++i) {
        inRamp[i] = true;
      }
      double const rate{stage.duration() > 0.0 ? (stage.referenceEnd - stage.referenceStart) / stage.duration() * 60.0 : 0.0};
      add(stage.start, eventtype::RAMP_START,
          QStringLiteral("Ramp %1 → %2 °C at %3 °C/min").arg(degrees(stage.referenceStart), degrees(stage.referenceEnd), degrees(rate)),
          QJsonObject{{"from", number(stage.referenceStart)}, {"to", number(stage.referenceEnd)}, {"rate_per_min", number(rate)}});
      add(stage.end, eventtype::RAMP_END, QStringLiteral("Ramp reached %1 °C").arg(degrees(stage.referenceEnd)),
          QJsonObject{{"to", number(stage.referenceEnd)}});
    }
    double last{std::numeric_limits<double>::quiet_NaN()};
    double lastChange{0.0}; // reference change into the previous sample
    for (qsizetype i{0}; i < n; ++i) {
      if (!std::isfinite(reference[i])) {
        continue;
      }
      double const change{std::isfinite(last) ? reference[i] - last : 0.0};
      // the last increment of a ramp can fall into the next stage (program interval change)
      bool const rampEnd{(i > 0) && inRamp[i - 1] && (change * lastChange > 0.0) && (std::fabs(change) <= 3.0 * std::fabs(lastChange))};
      if (std::isfinite(last) && !inRamp[i] && !rampEnd && (std::fabs(change) >= setpointTolerance)) {
        add(times[i], eventtype::SETPOINT_CHANGE, QStringLiteral("Setpoint %1 → %2 °C").arg(degrees(last), degrees(reference[i])),
            QJsonObject{{"from", last}, {"to", reference[i]}});
      }
      lastChange = change;
      last       = reference[i];
    }
  }

  // PID gains (any of Kp / Ki / Kd)
  QVector<double> const kp = series("temp_kp"), ki = series("temp_ki"), kd = series("temp_kd");
  if (!kp.isEmpty() && !ki.isEmpty() && !kd.isEmpty()) {
    double lastP{std::numeric_limits<double>::quiet_NaN()}, lastI{lastP}, lastD{lastP};
    for (qsizetype i{0}; i < n; ++i) {
      if (!(std::isfinite(kp[i]) && std::isfinite(ki[i]) && std::isfinite(kd[i]))) {
        continue;
      }
      if (std::isfinite(lastP) && ((kp[i] != lastP) || (ki[i] != lastI) || (kd[i] != lastD))) {
        QStringList changed; // only the gains that changed
        for (auto const &[name, from, to] : {std::tuple{"Kp", lastP, kp[i]}, std::tuple{"Ki", lastI, ki[i]}, std::tuple{"Kd", lastD, kd[i]}}) {
          if (from != to) {
            changed << QStringLiteral("%1 %2 → %3").arg(QLatin1String(name), text(from), text(to));
          }
        }
        add(times[i], eventtype::PID_GAINS_CHANGE, QStringLiteral("PID gains %1").arg(changed.join(QStringLiteral(", "))),
            QJsonObject{{"from", QJsonObject{{"kp", lastP}, {"ki", lastI}, {"kd", lastD}}},
                        {"to", QJsonObject{{"kp", kp[i]}, {"ki", ki[i]}, {"kd", kd[i]}}}});
      }
      lastP = kp[i];
      lastI = ki[i];
      lastD = kd[i];
    }
  }

  // integer-valued channels: PID band, status, program position
  struct Discrete {
    char const *channel;
    char const *type;
    QString (*describe)(int from, int to);
  };
  static Discrete const discrete[]{
    {"temp_pid_idx", eventtype::PID_BAND_CHANGE, [](int from, int to) { return QStringLiteral("PID band %1 → %2").arg(from).arg(to); }},
    {"status_condition", eventtype::STATUS_CHANGE,
     [](int from, int to) { return QStringLiteral("Status %1 → %2").arg(statusName(from), statusName(to)); }},
    {"interval", eventtype::PROGRAM_INTERVAL, [](int from, int to) { return QStringLiteral("Program interval %1 → %2").arg(from).arg(to); }},
    {"cycle_cur", eventtype::PROGRAM_CYCLE, [](int from, int to) { return QStringLiteral("Program cycle %1 → %2").arg(from).arg(to); }},
  };
  for (Discrete const &item : discrete) {
    QVector<double> const values = series(item.channel);
    int                   last{std::numeric_limits<int>::min()};
    for (qsizetype i{0}; i < values.size() && i < n; ++i) {
      if (!std::isfinite(values[i])) {
        continue;
      }
      int const value{static_cast<int>(std::lround(values[i]))};
      if ((last != std::numeric_limits<int>::min()) && (value != last)) {
        QJsonObject data{{"from", last}, {"to", value}};
        if (QLatin1String(item.channel) == QLatin1String("status_condition")) {
          data.insert("from_name", statusName(last));
          data.insert("to_name", statusName(value));
        }
        add(times[i], item.type, item.describe(last, value), data);
      }
      last = value;
    }
  }

  // alarm word: one event per bit edge
  QVector<double> const alarms = series("alarm_out_status");
  quint32               lastWord{0};
  bool                  haveWord{false};
  for (qsizetype i{0}; i < alarms.size() && i < n; ++i) {
    if (!std::isfinite(alarms[i])) {
      continue;
    }
    quint32 const word{static_cast<quint32>(std::llround(alarms[i]))};
    if (haveWord && (word != lastWord)) {
      for (int bit{0}; bit < 32; ++bit) {
        bool const before{((lastWord >> bit) & 1U) != 0U};
        bool const now{((word >> bit) & 1U) != 0U};
        if (before == now) {
          continue;
        }
        QString const label = alarmLabels.value(bit, QStringLiteral("alarm bit %1").arg(bit));
        add(times[i], now ? eventtype::ALARM_RAISED : eventtype::ALARM_CLEARED,
            QStringLiteral("%1: %2").arg(now ? QStringLiteral("Alarm") : QStringLiteral("Alarm cleared"), label),
            QJsonObject{{"bit", bit}, {"label", label}});
      }
    }
    lastWord = word;
    haveWord = true;
  }

  sortEvents(events);
  return events;
}

// ---------------------------------------------------------------- metadata and manifest

bool RunMetadata::isEmpty() const {
  return dut.isEmpty() && std::isnan(dutMassKg) && std::isnan(dutDensityKgM3) && operatorName.isEmpty() && purpose.isEmpty()
         && tags.isEmpty() && notes.isEmpty();
}

QJsonObject RunMetadata::toJson() const {
  QJsonObject json{{"dut", dut}, {"operator", operatorName}, {"purpose", purpose}, {"tags", QJsonArray::fromStringList(tags)}, {"notes", notes}};
  // only when given
  if (std::isfinite(dutMassKg)) {
    json.insert("dut_mass_kg", dutMassKg);
  }
  if (std::isfinite(dutDensityKgM3)) {
    json.insert("dut_density_kg_m3", dutDensityKgM3);
  }
  return json;
}

QString RunMetadata::dutMassText() const {
  return std::isfinite(dutMassKg) ? QStringLiteral("%1 kg").arg(QLocale::c().toString(dutMassKg, 'g', 6)) : QString{};
}

QString RunMetadata::dutDensityText() const {
  return std::isfinite(dutDensityKgM3) ? QStringLiteral("%1 kg/m³").arg(QLocale::c().toString(dutDensityKgM3, 'g', 6)) : QString{};
}

double RunMetadata::parseNumber(QString const &text) {
  bool         ok{false};
  double const value = QString(text).trimmed().replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&ok);
  return (ok && std::isfinite(value)) ? value : std::numeric_limits<double>::quiet_NaN();
}

QString RunMetadata::searchText() const {
  return QStringList{dut, operatorName, purpose, tags.join(QLatin1Char(' ')), notes}.join(QLatin1Char(' '));
}

RunMetadata RunMetadata::fromJson(QJsonObject const &json) {
  RunMetadata metadata;
  metadata.dut          = json.value("dut").toString();
  metadata.dutMassKg      = json.value("dut_mass_kg").toDouble(std::numeric_limits<double>::quiet_NaN());
  metadata.dutDensityKgM3 = json.value("dut_density_kg_m3").toDouble(std::numeric_limits<double>::quiet_NaN());
  metadata.operatorName = json.value("operator").toString();
  metadata.purpose      = json.value("purpose").toString();
  metadata.notes        = json.value("notes").toString();
  for (QJsonValue const &tag : json.value("tags").toArray()) {
    if (!tag.toString().trimmed().isEmpty()) {
      metadata.tags << tag.toString().trimmed();
    }
  }
  return metadata;
}

bool RunManifest::isValid() const {
  return schema.startsWith(QLatin1String("deepvac.run/")) && !runUid.isEmpty();
}

QJsonObject RunManifest::toJson() const {
  QJsonArray channelArray;
  for (ChannelMeta const &channel : channels) {
    channelArray.push_back(channel.toJson());
  }
  QJsonArray eventArray;
  for (RunEvent const &event : events) {
    eventArray.push_back(event.toJson());
  }
  return QJsonObject{
    {"schema", schema},
    {"identity", QJsonObject{{"run_uid", runUid},
                             {"name", name},
                             {"chamber", chamber},
                             {"chamber_ref", chamberRef},
                             {"start", number(start)},
                             {"end", number(end)},
                             {"app", QJsonObject{{"name", appName}, {"version", appVersion}}},
                             {"time_resolution_s", timeResolutionS}}},
    {"metadata", metadata.toJson()},
    {"configuration", configuration},
    {"channels", channelArray},
    {"events", eventArray},
    {"integrity", QJsonObject{{"samples_sha256", samplesSha256}, {"sample_count", sampleCount}, {"analysis_version", analysisVersion}}}};
}

RunManifest RunManifest::fromJson(QJsonObject const &json) {
  RunManifest       manifest;
  QJsonObject const identity  = json.value("identity").toObject();
  QJsonObject const integrity = json.value("integrity").toObject();
  manifest.schema             = json.value("schema").toString();
  manifest.runUid             = identity.value("run_uid").toString();
  manifest.name               = identity.value("name").toString();
  manifest.chamber            = identity.value("chamber").toString();
  manifest.chamberRef         = identity.value("chamber_ref").toString();
  manifest.start              = numberOf(identity.value("start"));
  manifest.end                = numberOf(identity.value("end"));
  manifest.appName            = identity.value("app").toObject().value("name").toString();
  manifest.appVersion         = identity.value("app").toObject().value("version").toString();
  manifest.timeResolutionS    = identity.value("time_resolution_s").toDouble(1.0);
  manifest.metadata           = RunMetadata::fromJson(json.value("metadata").toObject());
  manifest.configuration      = json.value("configuration").toObject();
  for (QJsonValue const &channel : json.value("channels").toArray()) {
    manifest.channels.push_back(ChannelMeta::fromJson(channel.toObject()));
  }
  for (QJsonValue const &event : json.value("events").toArray()) {
    manifest.events.push_back(RunEvent::fromJson(event.toObject()));
  }
  manifest.samplesSha256   = integrity.value("samples_sha256").toString();
  manifest.sampleCount     = static_cast<qint64>(integrity.value("sample_count").toDouble());
  manifest.analysisVersion = integrity.value("analysis_version").toInt();
  return manifest;
}

QString sha256Hex(QByteArray const &data) {
  return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

}
