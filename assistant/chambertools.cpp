#include "chambertools.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QJsonArray>
#include <algorithm>
#include <cmath>

#include "advisorcem.h"
#include "chambertwinsim.h"
#include "experimentrecorder.h"
#include "gatetcp.h"
#include "gruonnxmodel.h"
#include "offlinetuneworker.h"
#include "resholder.h"
#include "runmanifest.h"
#include "settingsmanager.h"
#include "statusmanager.h"
#include "systemvalue.h"
#include "systemvaluelist.h"

namespace deepvaccontrol::assistant {
namespace {

double const NaN{std::numeric_limits<double>::quiet_NaN()};
QString const DEG{QStringLiteral("°C")};
constexpr double WORK{2.0};
constexpr double ALARM{4.0};
constexpr int    RAMP_FLAG{9};
constexpr int    AWAIT_FLAG{8};
constexpr double HOLD_FOREVER_S{100.0 * 3600.0 - 1.0}; // a manual setpoint held "until stopped"

QString fmt(double value, QString const &unit = {}, int digits = 4) {
  if (!std::isfinite(value)) {
    return QStringLiteral("unknown");
  }
  QString text = QString::number(value, 'g', digits);
  return unit.isEmpty() ? text : QStringLiteral("%1 %2").arg(text, unit);
}

QJsonValue num(double value) {
  return std::isfinite(value) ? QJsonValue(std::round(value * 1000.0) / 1000.0) : QJsonValue(QJsonValue::Null);
}

// durations in words: a small model misreads hh:mm:ss
QString hms(double value) {
  if (!std::isfinite(value)) {
    return QStringLiteral("unknown");
  }
  qint64 const total{std::llround(std::fabs(value))};
  QStringList  parts;
  if (total >= 3600) {
    parts << QStringLiteral("%1 h").arg(total / 3600);
  }
  if ((total % 3600) / 60 > 0) {
    parts << QStringLiteral("%1 min").arg((total % 3600) / 60);
  }
  if (((total % 60) > 0 && total < 3600) || parts.isEmpty()) {
    parts << QStringLiteral("%1 s").arg(total % 60);
  }
  return parts.join(QLatin1Char(' '));
}

QJsonObject prop(char const *type, QString const &description) {
  return QJsonObject{{"type", type}, {"description", description}};
}

QJsonObject schema(QJsonObject const &properties, QStringList const &required = {}) {
  return QJsonObject{{"type", "object"}, {"properties", properties}, {"required", QJsonArray::fromStringList(required)}};
}

QJsonObject makeCard(QString const &title, QStringList const &facts, QStringList const &columns = {}, QVector<QStringList> const &rows = {}) {
  QJsonObject card{{"title", title}, {"facts", QJsonArray::fromStringList(facts)}};
  if (!columns.isEmpty()) {
    QJsonArray rowArray;
    for (QStringList const &row : rows) {
      rowArray.push_back(QJsonArray::fromStringList(row));
    }
    card.insert("table", QJsonObject{{"columns", QJsonArray::fromStringList(columns)}, {"rows", rowArray}});
  }
  return card;
}

// a number argument: the model may send 20, "20" or "-40 °C"
double numberArg(QJsonObject const &args, char const *key) {
  QJsonValue const value = args.value(QLatin1String(key));
  if (value.isDouble()) {
    return value.toDouble();
  }
  QString text = value.toString().trimmed();
  text.remove(QStringLiteral("°C")).remove(QLatin1Char('C')).replace(QChar(0x2212), QLatin1Char('-'));
  bool         ok{false};
  double const parsed{text.trimmed().toDouble(&ok)};
  return ok ? parsed : NaN;
}

// time to come within `band` of the target and stay there; NaN if it never does
double timeToStayWithin(QVector<ai::TrajectoryPoint> const &trajectory, double target, double band) {
  double since{NaN};
  for (ai::TrajectoryPoint const &p : trajectory) {
    if (std::fabs(p.temp - target) <= band) {
      since = std::isnan(since) ? p.elapsedS : since;
    } else {
      since = NaN;
    }
  }
  return since;
}

} // namespace

ChamberTools::ChamberTools(ResHolder *resHolder, QObject *parent) : QObject{parent}, m_resHolder{resHolder} {
  if (m_resHolder && m_resHolder->tcpGate) {
    connect(m_resHolder->tcpGate, &GateTcp::alarmReceived, this, [this](quint32 code) {
      m_alarmCode = code;
      m_alarmTime = static_cast<double>(QDateTime::currentSecsSinceEpoch());
    });
  }
}

ChamberTools::~ChamberTools() = default;

void ChamberTools::registerTools(ToolRegistry &registry) {
  registry.add({"get_live_status",
                "What the chamber is doing now: connection, status, temperature and setpoint, control output, program "
                "step and cycle, elapsed and remaining time, active alarms.",
                schema({}), [this](QJsonObject const &a) { return liveStatus(a); }});
  registry.add({"get_settings",
                "The chamber's current settings: Temperature PID per band, calibration, setpoint range and the other PLC "
                "settings.",
                schema({{"section", prop("string", "pid (default), calibration, limits, general or all")}}),
                [this](QJsonObject const &a) { return settings(a); }});
  registry.add({"predict_setpoint",
                "Predict with the digital twin how the chamber would go to a setpoint: time to reach it, overshoot, "
                "temperature at the end. Uses the current PID of that band unless kp/ki/kd are given.",
                schema({{"target", prop("number", "setpoint, °C")},
                        {"start", prop("number", "start temperature, °C (default: the chamber's temperature now)")},
                        {"minutes", prop("number", "how long to simulate, minutes (default 20)")},
                        {"kp", prop("number", "optional")},
                        {"ki", prop("number", "optional")},
                        {"kd", prop("number", "optional")}},
                       {"target"}),
                [this](QJsonObject const &a) { return predictSetpoint(a); }});
  registry.add({"suggest_pid",
                "AI Advisor: search with the digital twin for the best Temperature PID (Kp, Ki, Kd) to go to a setpoint, "
                "compared with the current PID. Only a suggestion: nothing is changed. Takes some seconds.",
                schema({{"target", prop("number", "setpoint, °C")},
                        {"start", prop("number", "start temperature, °C (default: the chamber's temperature now)")},
                        {"minutes", prop("number", "how long the run is, minutes (default 20)")}},
                       {"target"}),
                [this](QJsonObject const &a) { return suggestPid(a); }});
}

// ---- the chamber now

QJsonObject ChamberTools::liveStatus(QJsonObject const &) {
  StatusManager::AppState const state = m_resHolder->status ? m_resHolder->status->getAppState() : StatusManager::AppState::IDLE;
  bool const                    online{StatusManager::AppState::ONLINE == state};
  QString const connection = online ? QStringLiteral("online")
                             : (state == StatusManager::AppState::OFFLINE) ? QStringLiteral("connected, but its configuration differs from the application's")
                             : ((state == StatusManager::AppState::CONNECTING) || (state == StatusManager::AppState::CONNECTED))
                               ? QStringLiteral("connecting")
                               : QStringLiteral("not connected");
  if (!online) {
    return QJsonObject{{"connection", connection},
                       {"card", makeCard(QStringLiteral("Chamber now"), {QStringLiteral("The chamber is %1: no live values.").arg(connection)})}};
  }
  auto value = [this](char const *name) {
    SystemValueList *states = m_resHolder->states;
    SystemValue     *v      = (states && states->contains(QLatin1String(name))) ? states->getSysValue(QLatin1String(name)) : nullptr;
    double const     x{v ? static_cast<double>(v->getValue()) : NaN};
    return (std::isfinite(x) && std::fabs(x) < 1e9) ? x : NaN;
  };
  double const status{value("status_condition")};
  double const temp{value("temp")};
  double const ref{value("temp_ref")};
  double const output{value("temp_u")};
  bool const   working{status == WORK};
  QString const statusText = core::statusName(static_cast<int>(status));

  QJsonObject result{{"connection", connection}, {"status", statusText}, {"temperature", num(temp)}, {"setpoint", num(ref)},
                     {"control_output_percent", num(output)}};
  QStringList facts{QStringLiteral("Chamber %1, status %2.").arg(connection, statusText),
                    QStringLiteral("Temperature %1, setpoint %2, control output %3 % (%4).")
                      .arg(fmt(temp, DEG), fmt(ref, DEG), fmt(output, {}, 3),
                           !std::isfinite(output) ? QStringLiteral("unknown")
                           : (output > 1.0)       ? QStringLiteral("heating")
                           : (output < -1.0)      ? QStringLiteral("cooling")
                                                  : QStringLiteral("idle"))};
  if (std::isfinite(value("pres"))) {
    result.insert("pressure", num(value("pres")));
    facts << QStringLiteral("Pressure %1.").arg(fmt(value("pres"), QStringLiteral("mmHg")));
  }

  if (working) {
    double const elapsed{value("t_total")};
    double const intervalTime{value("t_interval")};
    int const    interval{static_cast<int>(value("interval"))};
    int const    intervals{static_cast<int>(value("intervals_count"))};
    int const    cycle{static_cast<int>(value("cycle_cur"))};
    int const    cycles{static_cast<int>(value("cycle_total"))};
    result.insert("elapsed", hms(elapsed));
    facts << QStringLiteral("The job has run %1.").arg(hms(elapsed));
    // the rest of the program sent with the job: the current step, then the steps (and cycles) still to come
    QVariantMap const  program  = m_resHolder->recorder ? m_resHolder->recorder->runningProgram() : QVariantMap{};
    QVariantList const steps    = program.value("intervals").toList();
    QString const      kind     = m_resHolder->recorder ? m_resHolder->recorder->runningJobKind() : QString{};
    if (!steps.isEmpty() && (interval >= 1) && (interval <= steps.size())) {
      QVariantMap const cycleMap = program.value("cycle").toMap();
      int const         cycleStart{cycleMap.value("start").toInt()};
      int const         cycleEnd{cycleMap.value("end").toInt()};
      int const         cycleCount{std::max(1, cycleMap.value("count").toInt())};
      auto durationOf = [&steps](int step) { return steps.value(step - 1).toMap().value("duration").toDouble(); };
      double const current{durationOf(interval)};
      bool const   holdsForever{current >= HOLD_FOREVER_S};
      quint32 const flags{static_cast<quint32>(steps.value(interval - 1).toMap().value("flags").toDouble())};
      bool const    waiting{((flags >> AWAIT_FLAG) & 1U) && std::isfinite(intervalTime) && (intervalTime <= 0.0)};
      // the steps in the order the PLC runs them: 1 .. end of the cycle block, the block again (count - 1) times, the rest
      bool const   cycled{(cycleCount > 1) && (cycleStart >= 1) && (cycleEnd >= cycleStart) && (cycleEnd <= steps.size())};
      QVector<int> order;
      for (int s{1}; s <= static_cast<int>(steps.size()); ++s) {
        order << s;
        if (cycled && (s == cycleEnd)) {
          for (int pass{1}; pass < cycleCount; ++pass) {
            for (int b{cycleStart}; b <= cycleEnd; ++b) {
              order << b;
            }
          }
        }
      }
      // where the job is: the step's occurrence of this cycle (a step in the block runs once per cycle)
      bool const inCycle{cycled && (interval >= cycleStart) && (interval <= cycleEnd)};
      int        seen{0};
      qsizetype  at{-1};
      for (qsizetype k{0}; k < order.size(); ++k) {
        if ((order[k] == interval) && (++seen == (inCycle ? std::max(cycle, 1) : 1))) {
          at = k;
          break;
        }
      }
      double remaining{std::max(0.0, current - (std::isfinite(intervalTime) ? intervalTime : 0.0))};
      for (qsizetype k{at + 1}; (at >= 0) && (k < order.size()); ++k) {
        remaining += durationOf(order[k]);
      }
      double const stepTarget{steps.value(interval - 1).toMap().value("temp").toDouble()};
      bool const   ramp{((flags >> RAMP_FLAG) & 1U) != 0};
      QJsonObject  position{{"step", interval}, {"steps", intervals}, {"cycle", cycle}, {"cycles", cycles},
                           {"step_setpoint", num(stepTarget)}, {"step_ramps", ramp}};
      if (holdsForever) {
        position.insert("remaining", "held until stopped");
        facts << QStringLiteral("%1 %2: held until stopped.")
                   .arg(kind == QLatin1String("manual_setpoint") ? QStringLiteral("Manual setpoint") : QStringLiteral("Setpoint"), fmt(stepTarget, DEG));
      } else {
        position.insert("step_remaining", hms(std::max(0.0, current - intervalTime)));
        position.insert("remaining", hms(remaining));
        position.insert("expected_end", QDateTime::currentDateTime().addSecs(static_cast<qint64>(remaining)).toString("HH:mm"));
        facts << QStringLiteral("Step %1 of %2%3 (%4 %5): %6 left in the step.")
                   .arg(interval)
                   .arg(intervals)
                   .arg(cycles > 1 ? QStringLiteral(", cycle %1 of %2").arg(cycle).arg(cycles) : QString{})
                   .arg(ramp ? QStringLiteral("ramp to") : QStringLiteral("setpoint"), fmt(stepTarget, DEG), hms(std::max(0.0, current - intervalTime)))
              << QStringLiteral("About %1 left in the program: it should end around %2%3.")
                   .arg(hms(remaining), position.value("expected_end").toString(),
                        waiting ? QStringLiteral(" (the step waits for the setpoint first, so later)") : QString{});
      }
      result.insert("program", position);
    } else if (intervals > 0) {
      facts << QStringLiteral("Step %1 of %2, cycle %3 of %4.").arg(interval).arg(intervals).arg(cycle).arg(cycles);
    }
  }

  // alarms: the last one the PLC reported, while the chamber is in alarm
  if (status == ALARM) {
    QStringList active;
    if (m_resHolder->settings) {
      for (quint32 bit{0}; bit < 32; ++bit) {
        if ((m_alarmCode >> bit) & 1U) {
          active << m_resHolder->settings->getAlarmsMap().value(bit, QStringLiteral("alarm %1").arg(bit + 1));
        }
      }
    }
    result.insert("alarms", QJsonArray::fromStringList(active));
    facts << (active.isEmpty() ? QStringLiteral("The chamber is in alarm.")
                               : QStringLiteral("Active alarms: %1 (since %2).")
                                   .arg(active.join(QStringLiteral("; ")),
                                        QDateTime::fromSecsSinceEpoch(static_cast<qint64>(m_alarmTime)).toString("HH:mm")));
  } else {
    result.insert("alarms", QJsonArray{});
    facts << QStringLiteral("No active alarm.");
  }
  result.insert("card", makeCard(QStringLiteral("Chamber now"), facts));
  return result;
}

// ---- the settings

QJsonObject ChamberTools::settings(QJsonObject const &args) const {
  QString const section = args.value("section").toString(QStringLiteral("pid")).trimmed().toLower();
  bool const    all{section == QLatin1String("all")};
  QVariantMap const map = m_resHolder->settings->settingsMap();
  QVariantMap const plc = map.value("plc_settings").toMap();
  QJsonObject       result;
  QStringList       facts;
  QStringList       columns;
  QVector<QStringList> rows;

  if (all || section.startsWith(QLatin1String("pid"))) {
    QJsonArray bands;
    for (QVariant const &entry : plc.value("pids").toList()) {
      QVariantMap const  pid = entry.toMap();
      int const          points{pid.value("points").toInt()};
      double const       low{pid.value("min").toDouble()};
      double const       high{pid.value("max").toDouble()};
      QVariantList const p = pid.value("p").toList(), i = pid.value("i").toList(), d = pid.value("d").toList();
      facts << QStringLiteral("%1: %2 bands from %3 to %4.").arg(pid.value("label").toString()).arg(points).arg(fmt(low, DEG), fmt(high, DEG));
      columns = {"PID", "Band", "From", "To", "Kp", "Ki", "Kd"};
      for (int k{0}; k < points; ++k) {
        double const from{low + (high - low) * k / std::max(points, 1)};
        double const to{low + (high - low) * (k + 1) / std::max(points, 1)};
        bands.push_back(QJsonObject{{"pid", pid.value("label").toString()}, {"band", k + 1}, {"from", num(from)}, {"to", num(to)},
                                    {"kp", num(p.value(k).toDouble())}, {"ki", num(i.value(k).toDouble())}, {"kd", num(d.value(k).toDouble())}});
        rows.push_back({pid.value("label").toString(), QString::number(k + 1), fmt(from, DEG), fmt(to, DEG), fmt(p.value(k).toDouble()),
                        fmt(i.value(k).toDouble()), fmt(d.value(k).toDouble())});
      }
    }
    result.insert("pid_bands", bands);
    if (m_resHolder->states && m_resHolder->states->contains("temp_pid_idx")
        && (StatusManager::AppState::ONLINE == m_resHolder->status->getAppState())) {
      double const band{m_resHolder->states->getSysValue("temp_pid_idx")->getValue()};
      if (std::isfinite(band)) {
        facts << QStringLiteral("The band in use now is %1.").arg(std::lround(band) + 1);
      }
    }
  }
  if (all || section.startsWith(QLatin1String("cal"))) {
    QJsonArray calibrations;
    for (QVariant const &entry : plc.value("calibrations").toList()) {
      QVariantMap const  cal = entry.toMap();
      QVariantList const x = cal.value("x").toList(), delta = cal.value("delta").toList();
      QStringList        points;
      QJsonArray         list;
      for (qsizetype k{0}; k < std::min(x.size(), delta.size()); ++k) {
        points << QStringLiteral("%1 → %2").arg(fmt(x[k].toDouble(), DEG), fmt(delta[k].toDouble(), DEG));
        list.push_back(QJsonObject{{"measured", num(x[k].toDouble())}, {"correction", num(delta[k].toDouble())}});
      }
      calibrations.push_back(QJsonObject{{"name", cal.value("label").toString()}, {"points", list}});
      facts << QStringLiteral("%1 (measured value → correction): %2.").arg(cal.value("label").toString(), points.join(QStringLiteral(", ")));
    }
    result.insert("calibrations", calibrations);
  }
  if (all || section.startsWith(QLatin1String("lim"))) {
    for (QVariant const &entry : map.value("control_params").toList()) {
      QVariantMap const param = entry.toMap();
      if (param.contains("min") && param.contains("max") && (param.value("name").toString() != QLatin1String("duration"))) {
        facts << QStringLiteral("%1 setpoint range: %2 to %3.")
                   .arg(param.value("label").toString(), fmt(param.value("min").toDouble()), fmt(param.value("max").toDouble()));
        result.insert(param.value("name").toString() + QStringLiteral("_range"),
                      QJsonObject{{"min", num(param.value("min").toDouble())}, {"max", num(param.value("max").toDouble())}});
      }
    }
  }
  if (all || section.startsWith(QLatin1String("gen"))) {
    QJsonArray general;
    QVector<QStringList> generalRows;
    for (QVariant const &entry : plc.value("general").toList()) {
      QVariantMap const item = entry.toMap();
      if (item.value("name").toString().startsWith(QLatin1String("alarm_cfg"))) {
        continue; // bit masks, not readable values
      }
      general.push_back(QJsonObject{{"name", item.value("name").toString()}, {"label", item.value("label").toString()},
                                    {"value", num(item.value("value").toDouble())}});
      generalRows.push_back({item.value("label").toString(), fmt(item.value("value").toDouble())});
    }
    result.insert("general", general);
    if (columns.isEmpty()) {
      columns = {"Setting", "Value"};
      rows    = generalRows;
    } else {
      facts << QStringLiteral("%1 general settings (ask for section general to see them).").arg(general.size());
    }
  }
  if (facts.isEmpty() && rows.isEmpty()) {
    return QJsonObject{{"error", "section must be pid, calibration, limits, general or all"}};
  }
  result.insert("card", makeCard(QStringLiteral("Chamber settings"), facts, columns, rows));
  return result;
}

// ---- the digital twin

GruOnnxModel *ChamberTools::model(QString &error) {
  if (!m_model) {
    QDir const dir(QDir(QCoreApplication::applicationDirPath()).filePath("data/model"));
    auto       model = std::make_unique<GruOnnxModel>();
    if (!model->load(dir.filePath("twin_model.onnx"), dir.filePath("twin_model.json"))) {
      error = QStringLiteral("the digital twin is not available: %1").arg(model->statusMessage());
      return nullptr;
    }
    m_model = std::move(model);
  }
  return m_model.get();
}

bool ChamberTools::bandGains(double setpoint, double &kp, double &ki, double &kd, int &band) const {
  QVariantList const pids = m_resHolder->settings->settingsMap().value("plc_settings").toMap().value("pids").toList();
  if (pids.isEmpty()) {
    return false;
  }
  QVariantMap const pid = pids.first().toMap();
  int const         points{pid.value("points").toInt()};
  double const      low{pid.value("min").toDouble()};
  double const      high{pid.value("max").toDouble()};
  if ((points <= 0) || (high <= low)) {
    return false;
  }
  band = std::clamp(static_cast<int>(std::floor((setpoint - low) / ((high - low) / points))), 0, points - 1);
  kp   = pid.value("p").toList().value(band).toDouble();
  ki   = pid.value("i").toList().value(band).toDouble();
  kd   = pid.value("d").toList().value(band).toDouble();
  return true;
}

double ChamberTools::startTemperature(QJsonObject const &args) const {
  double const given{numberArg(args, "start")};
  if (std::isfinite(given)) {
    return given;
  }
  if (m_resHolder->states && m_resHolder->states->contains("temp") && m_resHolder->status
      && (StatusManager::AppState::ONLINE == m_resHolder->status->getAppState())) {
    double const now{m_resHolder->states->getSysValue("temp")->getValue()};
    return std::isfinite(now) ? now : NaN;
  }
  return NaN;
}

QJsonObject ChamberTools::predictSetpoint(QJsonObject const &args) {
  double const target{numberArg(args, "target")};
  double const start{startTemperature(args)};
  double const minutes{std::isfinite(numberArg(args, "minutes")) ? numberArg(args, "minutes") : ai::DEFAULT_DURATION_S / 60.0};
  if (!std::isfinite(target)) {
    return QJsonObject{{"error", "target (°C) is needed"}};
  }
  if (!std::isfinite(start)) {
    return QJsonObject{{"error", "the chamber is not connected: give the start temperature (start, °C)"}};
  }
  QString       error;
  GruOnnxModel *twin = model(error);
  if (!twin) {
    return QJsonObject{{"error", error}};
  }
  ai::SimParams params;
  params.startTemp  = start;
  params.targetTemp = target;
  params.durationS  = std::clamp(minutes, 1.0, 120.0) * 60.0;
  params.dtS        = ai::DEFAULT_DT_S;
  int  band{-1};
  bool custom{std::isfinite(numberArg(args, "kp")) && std::isfinite(numberArg(args, "ki")) && std::isfinite(numberArg(args, "kd"))};
  if (custom) {
    params.kp = numberArg(args, "kp");
    params.ki = numberArg(args, "ki");
    params.kd = numberArg(args, "kd");
  } else if (!bandGains(target, params.kp, params.ki, params.kd, band)) {
    return QJsonObject{{"error", "no Temperature PID in the settings: give kp, ki and kd"}};
  }
  ai::ChamberTwinSim sim(twin);
  if (!sim.begin(params, &error)) {
    return QJsonObject{{"error", error}};
  }
  ai::TrajectoryPoint point;
  while (!sim.isFinished()) {
    if (!sim.stepOnce(point, &error)) {
      return QJsonObject{{"error", error}};
    }
  }
  ai::SimMetrics const metrics = sim.metrics();
  double const         near{timeToStayWithin(sim.trajectory(), target, ai::NEAR_BAND)};
  double const         settled{timeToStayWithin(sim.trajectory(), target, ai::SETTLE_BAND)};
  QString const        outside = twin->envelope().checkScenario(start, target, params.durationS);

  QString const pidText = QStringLiteral("Kp %1, Ki %2, Kd %3").arg(fmt(params.kp), fmt(params.ki), fmt(params.kd));
  QStringList   facts{QStringLiteral("Digital twin, from %1 to a setpoint of %2 over %3, with %4 (%5).")
                      .arg(fmt(start, DEG), fmt(target, DEG), hms(params.durationS), pidText,
                           custom ? QStringLiteral("as given") : QStringLiteral("the current PID of band %1").arg(band + 1)),
                    !std::isfinite(near)
                      ? QStringLiteral("It does not come within %1 of the setpoint in that time.").arg(fmt(ai::NEAR_BAND, DEG))
                      : QStringLiteral("Within %1 of the setpoint after %2; within %3 %4.")
                          .arg(fmt(ai::NEAR_BAND, DEG), hms(near), fmt(ai::SETTLE_BAND, DEG),
                               std::isfinite(settled) ? QStringLiteral("after %1").arg(hms(settled)) : QStringLiteral("not in that time")),
                    QStringLiteral("Overshoot %1; temperature at the end %2; error over the last minutes %3.")
                      .arg(fmt(metrics.overshootMax, DEG), fmt(metrics.endTemp, DEG), fmt(metrics.tailMae, DEG))};
  if (!outside.isEmpty()) {
    facts << QStringLiteral("Caution: %1, so the prediction is less reliable.").arg(outside);
  }
  facts << QStringLiteral("A prediction of the twin, not a measurement.");
  QJsonObject result{{"start", num(start)},
                     {"target", num(target)},
                     {"pid", QJsonObject{{"kp", num(params.kp)}, {"ki", num(params.ki)}, {"kd", num(params.kd)}}},
                     {"time_within_2C", std::isfinite(near) ? QJsonValue(hms(near)) : QJsonValue("not reached")},
                     {"time_within_0_5C", std::isfinite(settled) ? QJsonValue(hms(settled)) : QJsonValue("not reached")},
                     {"overshoot", num(metrics.overshootMax)},
                     {"end_temperature", num(metrics.endTemp)},
                     {"tail_mae", num(metrics.tailMae)}};
  if (!outside.isEmpty()) {
    result.insert("warning", outside);
  }
  result.insert("card", makeCard(QStringLiteral("Prediction: %1 to %2").arg(fmt(start, DEG), fmt(target, DEG)), facts));
  return result;
}

QJsonObject ChamberTools::suggestPid(QJsonObject const &args) {
  double const target{numberArg(args, "target")};
  double const start{startTemperature(args)};
  double const minutes{std::isfinite(numberArg(args, "minutes")) ? numberArg(args, "minutes") : ai::DEFAULT_DURATION_S / 60.0};
  if (!std::isfinite(target)) {
    return QJsonObject{{"error", "target (°C) is needed"}};
  }
  if (!std::isfinite(start)) {
    return QJsonObject{{"error", "the chamber is not connected: give the start temperature (start, °C)"}};
  }
  QString       error;
  GruOnnxModel *twin = model(error);
  if (!twin) {
    return QJsonObject{{"error", error}};
  }
  ai::SimParams params;
  params.startTemp  = start;
  params.targetTemp = target;
  params.durationS  = std::clamp(minutes, 1.0, ai::DEFAULT_DURATION_S / 60.0) * 60.0; // the twin's validated horizon
  params.dtS        = ai::DEFAULT_DT_S;
  QString const outside = twin->envelope().checkScenario(start, target, params.durationS);
  if (!outside.isEmpty()) {
    return QJsonObject{{"error", QStringLiteral("%1: the twin cannot give a reliable recommendation here").arg(outside)}};
  }

  // the search on every core, in a worker thread; the window stays responsive meanwhile
  ai::OfflineTuneWorker worker(twin, params);
  QEventLoop            wait;
  QObject::connect(&worker, &QThread::finished, &wait, &QEventLoop::quit);
  worker.start();
  wait.exec(QEventLoop::ExcludeUserInputEvents);
  worker.wait();
  ai::OfflineTuneResult const best = worker.result();
  if (!best.ok) {
    return QJsonObject{{"error", best.errorMessage.isEmpty() ? QStringLiteral("the search failed") : best.errorMessage}};
  }

  // the same run with the current PID, for comparison
  QJsonObject currentArgs{{"target", target}, {"start", start}, {"minutes", params.durationS / 60.0}};
  QJsonObject const current = predictSetpoint(currentArgs);
  double const      near{timeToStayWithin(best.bestTrajectory, target, ai::NEAR_BAND)};
  bool const        valid{twin->envelope().containsPid(best.bestKp, best.bestKi, best.bestKd)};

  // the current PID's result, to judge the suggestion by (the search only guards against its own starting point)
  double const currentTail{current.contains("error") ? NaN : current.value("tail_mae").toDouble(NaN)};
  // better: clearly less error at the end, or clearly less overshoot for the same error (not a rounding difference)
  QJsonObject const currentPid = current.value("pid").toObject();
  bool const        same{!current.contains("error") && (std::fabs(best.bestKp - currentPid.value("kp").toDouble()) < 0.5)
                  && (std::fabs(best.bestKi - currentPid.value("ki").toDouble()) < 0.5) && (std::fabs(best.bestKd - currentPid.value("kd").toDouble()) < 0.5)};
  double const      currentOvershoot{current.value("overshoot").toDouble(NaN)};
  bool const        better{!same
                    && (!std::isfinite(currentTail) || (currentTail - best.bestMetrics.tailMae > std::max(0.05 * currentTail, 0.02))
                        || ((currentOvershoot - best.bestMetrics.overshootMax > 0.5) && (best.bestMetrics.tailMae <= currentTail + 0.02)))};
  bool const   reached{std::isfinite(near) || (!current.contains("error") && (current.value("time_within_2C").toString() != QLatin1String("not reached")))};
  QStringList  facts;
  if (better) {
    facts << QStringLiteral("AI Advisor for %1 → %2 over %3: Kp %4, Ki %5, Kd %6.")
               .arg(fmt(start, DEG), fmt(target, DEG), hms(params.durationS), fmt(best.bestKp), fmt(best.bestKi), fmt(best.bestKd))
          << QStringLiteral("Predicted with it: %1, overshoot %2, error over the last minutes %3.")
               .arg(std::isfinite(near) ? QStringLiteral("within %1 after %2").arg(fmt(ai::NEAR_BAND, DEG), hms(near))
                                        : QStringLiteral("not within %1 in that time").arg(fmt(ai::NEAR_BAND, DEG)),
                    fmt(best.bestMetrics.overshootMax, DEG), fmt(best.bestMetrics.tailMae, DEG));
  } else {
    facts << (same ? QStringLiteral("AI Advisor for %1 → %2 over %3: the current PID is already the best it found, so keep it.")
                   : QStringLiteral("AI Advisor for %1 → %2 over %3: no PID found that does better than the current one, so keep it."))
                 .arg(fmt(start, DEG), fmt(target, DEG), hms(params.durationS));
  }
  if (!reached) {
    facts << QStringLiteral("The twin predicts the chamber cannot get within %1 of %2 in %3 from %4 with any of these gains (it ends near %5): "
                            "the run is too short for this change of temperature.")
               .arg(fmt(ai::NEAR_BAND, DEG), fmt(target, DEG), hms(params.durationS), fmt(start, DEG),
                    fmt(current.contains("error") ? best.bestMetrics.endTemp : current.value("end_temperature").toDouble(NaN), DEG));
  }
  if (!current.contains("error")) {
    QJsonObject const pid = current.value("pid").toObject();
    QString const reach = (current.value("time_within_2C").toString() == QLatin1String("not reached"))
                            ? QStringLiteral("not within %1 in that time").arg(fmt(ai::NEAR_BAND, DEG))
                            : QStringLiteral("within %1 after %2").arg(fmt(ai::NEAR_BAND, DEG), current.value("time_within_2C").toString());
    facts << QStringLiteral("With the current PID (Kp %1, Ki %2, Kd %3): %4, overshoot %5, error over the last minutes %6.")
               .arg(fmt(pid.value("kp").toDouble()), fmt(pid.value("ki").toDouble()), fmt(pid.value("kd").toDouble()), reach,
                    fmt(current.value("overshoot").toDouble(NaN), DEG), fmt(current.value("tail_mae").toDouble(NaN), DEG));
  }
  if (better) {
    facts << (valid ? QStringLiteral("Only a suggestion: nothing was changed. To use it, apply it from the AI Advisor or enter it in Settings → Temperature PID.")
                    : QStringLiteral("These gains are outside the range the twin was validated for: do not use them without checking."));
  }
  QJsonObject result{{"start", num(start)},
                     {"target", num(target)},
                     {"suggested_pid", better ? QJsonValue(QJsonObject{{"kp", num(best.bestKp)}, {"ki", num(best.bestKi)}, {"kd", num(best.bestKd)}})
                                              : QJsonValue("keep the current PID")},
                     {"setpoint_reachable_in_time", reached},
                     {"predicted_overshoot", num(best.bestMetrics.overshootMax)},
                     {"predicted_tail_mae", num(best.bestMetrics.tailMae)},
                     {"applied", false}};
  if (!current.contains("error")) {
    result.insert("current_pid", current.value("pid"));
  }
  result.insert("card", makeCard(QStringLiteral("Suggested PID for %1").arg(fmt(target, DEG)), facts));
  return result;
}

} // namespace deepvaccontrol::assistant
