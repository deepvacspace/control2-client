#include "analysistools.h"
#include "constants.h"
#include <QTextStream>
#include <QFile>
#include <QDir>

#include <QDateTime>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>

#include "analysisnotesstore.h"
#include "comparison.h"
#include "experimentinput.h"
#include "runrecord.h"
#include "runstore.h"
#include "seriesstats.h"

namespace deepvaccontrol::assistant {

namespace {
constexpr int CACHE_SIZE{6};
constexpr int MAX_LIST{50};

// numbers the model sees: 4 significant digits, null when unknown
QJsonValue num(double value, int digits = 4) {
  return std::isfinite(value) ? QJsonValue(QString::number(value, 'g', digits).toDouble()) : QJsonValue(QJsonValue::Null);
}

QJsonValue seconds(double value) {
  return std::isfinite(value) ? QJsonValue(std::round(value * 10.0) / 10.0) : QJsonValue(QJsonValue::Null);
}

// durations in words ("1 h 45 min", "2 min 43 s"): a small model misreads hh:mm:ss
QString hms(double value) {
  if (!std::isfinite(value)) {
    return QStringLiteral("unknown");
  }
  qint64 const total{std::llround(std::fabs(value))};
  qint64 const hours{total / 3600};
  qint64 const minutes{(total % 3600) / 60};
  qint64 const secs{total % 60};
  QStringList parts;
  if (hours > 0) {
    parts << QStringLiteral("%1 h").arg(hours);
  }
  if (minutes > 0) {
    parts << QStringLiteral("%1 min").arg(minutes);
  }
  if ((secs > 0 && hours == 0) || parts.isEmpty()) {
    parts << QStringLiteral("%1 s").arg(secs);
  }
  return (value < 0 ? QStringLiteral("-") : QString()) + parts.join(QLatin1Char(' '));
}

QString dateTime(double epoch) {
  return QDateTime::fromSecsSinceEpoch(static_cast<qint64>(epoch)).toString("yyyy-MM-dd HH:mm");
}

// start and end of an experiment, to the second
QString clockTime(double epoch) {
  return QDateTime::fromSecsSinceEpoch(static_cast<qint64>(epoch)).toString("yyyy-MM-dd HH:mm:ss");
}

// "started 2026-09-24 14:26:16, ended 15:36:15, lasted 1 h 9 min 59 s (4199 s)"
QString spanText(double start, double end) {
  QDateTime const from = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(start));
  QDateTime const to   = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(end));
  QString const   endText = (from.date() == to.date()) ? to.toString("HH:mm:ss") : to.toString("yyyy-MM-dd HH:mm:ss");
  qint64 const    total{std::llround(std::max(0.0, end - start))};
  QString const   exact = QStringLiteral("%1 h %2 min %3 s").arg(total / 3600).arg((total % 3600) / 60).arg(total % 60);
  return QStringLiteral("started %1, ended %2, lasted %3 (%4 s)").arg(clockTime(start), endText, exact).arg(total);
}

QJsonValue percent(double fraction) {
  return std::isfinite(fraction) ? QJsonValue(QStringLiteral("%1 %").arg(std::round(fraction * 1000.0) / 10.0)) : QJsonValue(QJsonValue::Null);
}

// ---- JSON schema helpers
QJsonObject property(char const *type, QString const &description) {
  return QJsonObject{{"type", type}, {"description", description}};
}

QJsonObject stringList(QString const &description) {
  return QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "string"}}}, {"description", description}};
}

QJsonObject schema(QJsonObject const &properties, QStringList const &required = {}) {
  return QJsonObject{{"type", "object"}, {"properties", properties}, {"required", QJsonArray::fromStringList(required)}};
}

QString const RUN_HELP = QStringLiteral("'#12', a title, 'latest' or 'previous'");
QString const STAGE_HELP = QStringLiteral("3, 'last', 'last_hold' (final stabilization), 'first_setpoint', 'hold:2', 'ramp:-1'");

QStringList stringsOf(QJsonValue const &value) {
  QStringList list;
  if (value.isString()) {
    list << value.toString();
  }
  for (QJsonValue const &item : value.toArray()) {
    list << (item.isDouble() ? QStringLiteral("#%1").arg(item.toInt()) : item.toString());
  }
  return list;
}

QString referenceOf(QJsonValue const &value) {
  return value.isDouble() ? QStringLiteral("#%1").arg(value.toInt()) : value.toString();
}

double optionalNumber(QJsonObject const &args, char const *key) {
  QJsonValue const value = args.value(QLatin1String(key));
  return value.isDouble() ? value.toDouble() : core::NaN;
}

QJsonObject kpiJson(core::ControlKpis const &k, bool tracking = true) {
  QJsonObject json{{"overshoot", num(k.overshoot)},
                     {"undershoot", num(k.undershoot)},
                     {"settling_time_s", seconds(k.settlingTimeS)},
                     {"rise_time_s", seconds(k.riseTimeS)},
                     {"fall_time_s", seconds(k.fallTimeS)},
                     {"steady_state_error", num(k.steadyStateError)},
                     {"tail_mae", num(k.tailMae)},
                     {"tail_std", num(k.tailStd)},
                     {"samples", k.samples}};
  if (tracking) {
    json.insert("mae", num(k.mae));
    json.insert("rmse", num(k.rmse));
    json.insert("max_abs_error", num(k.maxAbsError));
  } else {
    json.insert("error_note", "setpoint step without a ramp: it starts away from the setpoint, so the whole MAE is not a "
                              "measure of control; tail_mae (last 5 min) is the error to report");
  }
  return json;
}

QJsonObject detailJson(core::StageDetail const &d) {
  return QJsonObject{{"time_to_steady_s", seconds(d.timeToSteadyS)},
                     {"time_steady", percent(d.steadyFraction)},
                     {"reference_rate_per_min", num(d.referenceRatePerMin)},
                     {"value_rate_per_min", num(d.valueRatePerMin)},
                     {"tracking_lag_s", seconds(d.trackingLagS)},
                     {"output_saturated", percent(d.saturation.fraction)},
                     {"windup_s", seconds(d.saturation.windupS)},
                     {"pid_share", QJsonObject{{"p", percent(d.pid.shareP)}, {"i", percent(d.pid.shareI)}, {"d", percent(d.pid.shareD)}}},
                     {"kp", num(d.kp)},
                     {"ki", num(d.ki)},
                     {"kd", num(d.kd)}};
}

QJsonObject stageJson(core::ExperimentAnalysis const &a, int index, bool withKpis) {
  core::Segment const &s = a.run.stages[index].segment;
  QJsonObject          stage{{"stage", index + 1},
                    {"kind", core::Segment::kindName(s.kind)},
                    {"starts_after", hms(s.start)},
                    {"start_s", seconds(s.start)},
                    {"duration", hms(s.duration())},
                    {"setpoint_start", num(s.referenceStart)},
                    {"setpoint_end", num(s.referenceEnd)}};
  if (s.interval >= 0) {
    stage.insert("program_interval", s.interval);
  }
  if (withKpis) {
    stage.insert("kpis", kpiJson(a.run.stages[index].kpis, core::tracksSetpoint(s.kind)));
    stage.insert("behaviour", detailJson(a.stageDetails.value(index)));
  }
  return stage;
}

QJsonObject anomalyJson(core::Anomaly const &x) {
  return QJsonObject{{"severity", core::Anomaly::severityName(x.severity)},
                     {"kind", core::Anomaly::kindName(x.kind)},
                     {"channel", x.channel},
                     {"starts_after", hms(x.start)},
                     {"duration_s", seconds(x.end - x.start)},
                     {"detail", x.detail}};
}

// ---- cards: what the user sees and what the model reads, with units, written by DeepVac
QString fmt(double value, QString const &unit = {}, int digits = 4) {
  if (!std::isfinite(value)) {
    return QStringLiteral("n/a");
  }
  QString const text = QString::number(value, 'g', digits);
  return unit.isEmpty() ? text : QStringLiteral("%1 %2").arg(text, unit);
}

QString pct(double fraction) {
  return std::isfinite(fraction) ? QStringLiteral("%1 %").arg(QString::number(100.0 * fraction, 'g', 3)) : QStringLiteral("n/a");
}

QString const DEG{QStringLiteral("°C")};

QString setpointText(core::Segment const &s) {
  if (std::fabs(s.referenceEnd - s.referenceStart) < 0.05) {
    return fmt(s.referenceEnd, DEG);
  }
  return QStringLiteral("%1 → %2").arg(fmt(s.referenceStart, DEG), fmt(s.referenceEnd, DEG));
}

QString spanText(core::Segment const &s) {
  return QStringLiteral("from %1 to %2 (%3)")
    .arg(s.start < 0.5 ? QStringLiteral("the start") : hms(s.start), hms(s.end), hms(s.duration()));
}

QString stageLabel(core::ExperimentAnalysis const &a, int index) {
  core::Segment const &s = a.run.stages[index].segment;
  return QStringLiteral("stage %1 (%2, %3)").arg(index + 1).arg(core::Segment::kindName(s.kind), setpointText(s));
}

bool negligible(double delta, double reference) {
  return std::fabs(delta) <= std::max(1e-9, 0.005 * std::fabs(reference));
}

// "lower is better" comparison of run b against run a, as a sentence fragment
QString verdict(double a, double b, bool lowerIsBetter, QString const &runA, QString const &runB, QString const &unit) {
  if (!std::isfinite(a) || !std::isfinite(b)) {
    return QStringLiteral("not comparable");
  }
  double const delta{b - a};
  if (negligible(delta, a)) {
    return QStringLiteral("practically equal");
  }
  bool const    bBetter{lowerIsBetter ? delta < 0.0 : delta > 0.0};
  QString const better = bBetter ? runB : runA;
  return QStringLiteral("%1 better by %2").arg(better, fmt(std::fabs(delta), unit, 3));
}

QJsonObject makeCard(QString const &title, QStringList const &facts, QStringList const &columns = {},
                     QVector<QStringList> const &rows = {}) {
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

QStringList anomalyCounts(core::ExperimentAnalysis const &a) {
  int const critical{a.anomalyCount(core::Anomaly::Severity::CRITICAL)};
  int const warnings{a.anomalyCount(core::Anomaly::Severity::WARNING) - critical};
  int const info{static_cast<int>(a.anomalies.size()) - critical - warnings};
  return {QString::number(critical), QString::number(warnings), QString::number(info)};
}

QString anomalyText(core::Anomaly const &x) {
  return QStringLiteral("%1 %2 on %3, from %4 for %5: %6")
    .arg(core::Anomaly::severityName(x.severity), core::Anomaly::kindName(x.kind).replace(QLatin1Char('_'), QLatin1Char(' ')), x.channel,
         x.start < 0.5 ? QStringLiteral("the start") : hms(x.start), hms(x.end - x.start), x.detail);
}

// The whole-run error to report: MAE / RMSE while the setpoint ramps; a step (no ramp) starts away from its
// setpoint, so then the error over the last 5 minutes. `more`: appended before the full stop.
QString wholeRunError(core::RunAnalysis const &run, QString const &more) {
  if (run.hasRamp()) {
    return QStringLiteral("Whole experiment: MAE %1, RMSE %2%3.").arg(fmt(run.overall.mae, DEG), fmt(run.overall.rmse, DEG), more);
  }
  return QStringLiteral("Setpoint steps without a ramp: error over the last %1 %2, std %3%4.")
    .arg(hms(300.0), fmt(run.overall.tailMae, DEG), fmt(run.overall.tailStd, DEG), more);
}

QStringList stageRow(core::ExperimentAnalysis const &a, int k) {
  core::StageAnalysis const &stage = a.run.stages[k];
  bool const                 step{stage.segment.kind == core::Segment::Kind::SETPOINT};
  return {QString::number(k + 1),
          core::Segment::kindName(stage.segment.kind),
          setpointText(stage.segment),
          stage.segment.start < 0.5 ? QStringLiteral("0 s") : hms(stage.segment.start),
          hms(stage.segment.duration()),
          core::tracksSetpoint(stage.segment.kind) ? fmt(stage.kpis.mae, DEG, 3)
                                                   : QStringLiteral("%1 (end)").arg(fmt(stage.kpis.tailMae, DEG, 3)),
          step ? fmt(stage.kpis.overshoot, DEG, 3) : QStringLiteral("—"),
          step ? hms(stage.kpis.settlingTimeS) : QStringLiteral("—"),
          pct(a.stageDetails.value(k).steadyFraction)};
}
QStringList const STAGE_COLUMNS{"Stage", "Kind", "Setpoint", "Starts after", "Duration", "Error", "Overshoot", "Settling", "Time steady"};

core::AlignMode alignMode(QString const &text, bool &ok) {
  ok = true;
  QString const mode = text.trimmed().toLower();
  if (mode.isEmpty() || mode == QLatin1String("start")) {
    return core::AlignMode::START;
  }
  if (mode.startsWith(QLatin1String("setpoint")) || mode == QLatin1String("step")) {
    return core::AlignMode::SETPOINT_CHANGE;
  }
  if (mode.startsWith(QLatin1String("target")) || mode.contains(QLatin1String("cross"))) {
    return core::AlignMode::TARGET_CROSSING;
  }
  if (mode == QLatin1String("stage")) {
    return core::AlignMode::STAGE;
  }
  ok = false;
  return core::AlignMode::START;
}
}

QVector<double> AnalysisTools::Loaded::series(QString const &name) const {
  qsizetype const index{names.indexOf(name)};
  return (index >= 0) && (index < data->states.size()) ? data->states[index] : QVector<double>{};
}

AnalysisTools::AnalysisTools(ExperimentLibrary *library, AnalysisNotesStore *notes, AnalysisViewer viewer)
    : m_library{library}, m_notes{notes}, m_viewer{std::move(viewer)} {
}

// ------------------------------------------------------------------ runs

QVector<ExperimentRun> AnalysisTools::runs() const {
  return m_library->chronological(); // same numbering as the experiment list
}

QString AnalysisTools::runId(ExperimentRun const &run) const {
  QVector<ExperimentRun> const all = runs();
  for (qsizetype i{0}; i < all.size(); ++i) {
    if (all[i].key == run.key) {
      return QStringLiteral("#%1").arg(i + 1);
    }
  }
  return run.key;
}

std::optional<ExperimentRun> AnalysisTools::resolve(QString const &reference, QString &error) const {
  if (m_library->isScanning()) {
    error = QStringLiteral("the experiment list is still being scanned, try again in a few seconds");
    return std::nullopt;
  }
  QVector<ExperimentRun> const all = runs();
  if (all.isEmpty()) {
    error = QStringLiteral("no experiments are recorded");
    return std::nullopt;
  }
  QString const ref = reference.trimmed();
  QString const lower = ref.toLower();
  if (lower.isEmpty() || lower == QLatin1String("latest") || lower == QLatin1String("last") || lower == QLatin1String("newest")
      || lower == QLatin1String("current")) {
    return all.last();
  }
  if ((lower == QLatin1String("previous")) && (all.size() >= 2)) {
    return all[all.size() - 2];
  }
  // exact key / title first (a database run may be called "Run 105")
  for (ExperimentRun const &run : all) {
    if ((run.key == ref) || (run.storeKey == ref) || (run.title.compare(ref, Qt::CaseInsensitive) == 0)) {
      return run;
    }
  }
  static QRegularExpression const numbered(QStringLiteral("^(?:#|(?:run|experiment)\\s*#?|no\\.?\\s*)?(\\d+)$"), QRegularExpression::CaseInsensitiveOption);
  if (QRegularExpressionMatch const match = numbered.match(ref); match.hasMatch()) {
    int const number{match.captured(1).toInt()};
    if ((number >= 1) && (number <= all.size())) {
      return all[number - 1];
    }
  }
  QVector<ExperimentRun> partial;
  for (ExperimentRun const &run : all) {
    if (run.title.contains(ref, Qt::CaseInsensitive)) {
      partial.push_back(run);
    }
  }
  if (partial.size() == 1) {
    return partial.first();
  }
  error = partial.isEmpty() ? QStringLiteral("no experiment matches '%1' (there are %2 experiments, #1 ... #%2; use list_runs)").arg(ref).arg(all.size())
                            : QStringLiteral("'%1' matches %2 experiments, be more specific (use list_runs)").arg(ref).arg(partial.size());
  return std::nullopt;
}

AnalysisTools::LoadedPtr AnalysisTools::load(QString const &reference, QString &error) {
  std::optional<ExperimentRun> const run = resolve(reference, error);
  return run ? load(*run, error) : nullptr;
}

AnalysisTools::LoadedPtr AnalysisTools::load(ExperimentRun const &run, QString &error) {
  if (LoadedPtr cached = m_cache.value(run.key)) {
    m_cacheOrder.removeAll(run.key);
    m_cacheOrder.push_back(run.key);
    return cached;
  }
  auto data = m_library->load(run, &error);
  if (!data) {
    return nullptr;
  }
  auto loaded      = std::make_shared<Loaded>();
  loaded->run      = data->run;
  loaded->data     = data;
  loaded->channels = m_library->channels(*data);
  for (ChannelInfo const &channel : std::as_const(loaded->channels)) {
    loaded->names << channel.name;
  }
  double const start{data->times.isEmpty() ? 0.0 : data->times.first()};
  for (double const time : data->times) {
    loaded->times.push_back(time - start);
  }
  loaded->analysis = core::analyseExperiment(experimentInput(*data, loaded->names, m_notes ? m_notes->rules(run.key) : QVector<AnalysisNotesStore::Rule>{}));

  m_cache.insert(run.key, loaded);
  m_cacheOrder.push_back(run.key);
  while (m_cacheOrder.size() > CACHE_SIZE) {
    m_cache.remove(m_cacheOrder.takeFirst());
  }
  return loaded;
}

int AnalysisTools::selectStage(core::ExperimentAnalysis const &analysis, QString const &selector, QString &error) {
  QVector<core::StageAnalysis> const &stages = analysis.run.stages;
  if (stages.isEmpty()) {
    error = QStringLiteral("this experiment has no stages (no temp_ref recorded)");
    return -1;
  }
  QString text = selector.trimmed().toLower();
  text.replace(QLatin1Char(' '), QLatin1Char('_'));
  bool      isNumber{false};
  int const number{text.toInt(&isNumber)};
  if (isNumber) {
    if ((number >= 1) && (number <= stages.size())) {
      return number - 1;
    }
    error = QStringLiteral("stage %1 does not exist (1 ... %2)").arg(number).arg(stages.size());
    return -1;
  }
  if (text == QLatin1String("last") || text == QLatin1String("final")) {
    return static_cast<int>(stages.size()) - 1;
  }
  if (text == QLatin1String("first")) {
    return 0;
  }

  // kind with an occurrence: "last_hold", "first_setpoint", "hold:2", "ramp:-1", "final_stabilization"
  int     occurrence{1};
  QString kindText = text;
  if (kindText.startsWith(QLatin1String("last_")) || kindText.startsWith(QLatin1String("final_"))) {
    occurrence = -1;
    kindText   = kindText.mid(kindText.indexOf(QLatin1Char('_')) + 1);
  } else if (kindText.startsWith(QLatin1String("first_"))) {
    kindText = kindText.mid(6);
  }
  if (int const colon = static_cast<int>(kindText.indexOf(QLatin1Char(':'))); colon >= 0) {
    occurrence = kindText.mid(colon + 1).toInt();
    kindText   = kindText.left(colon);
  }
  core::Segment::Kind kind;
  if (kindText.startsWith(QLatin1String("hold")) || kindText.startsWith(QLatin1String("stabili")) || kindText.startsWith(QLatin1String("soak"))
      || kindText.startsWith(QLatin1String("steady"))) {
    kind = core::Segment::Kind::HOLD;
  } else if (kindText.startsWith(QLatin1String("ramp"))) {
    kind = core::Segment::Kind::RAMP;
  } else if (kindText.startsWith(QLatin1String("setpoint")) || kindText.startsWith(QLatin1String("step"))) {
    kind = core::Segment::Kind::SETPOINT;
  } else {
    error = QStringLiteral("unknown stage '%1'. Use a number, 'last', 'last_hold', 'hold:2', 'ramp:-1', 'first_setpoint'").arg(selector);
    return -1;
  }
  QVector<int> matching;
  for (qsizetype k{0}; k < stages.size(); ++k) {
    if (stages[k].segment.kind == kind) {
      matching.push_back(static_cast<int>(k));
    }
  }
  int const position{occurrence < 0 ? static_cast<int>(matching.size()) + occurrence : occurrence - 1};
  if ((position < 0) || (position >= matching.size())) {
    // "the second hold" where stage 2 is the only hold: the stage counted among all stages
    if ((occurrence > 0) && (occurrence <= stages.size()) && (stages[occurrence - 1].segment.kind == kind)) {
      return occurrence - 1;
    }
    error = QStringLiteral("the experiment has %1 %2 stage(s), '%3' does not exist").arg(matching.size()).arg(core::Segment::kindName(kind), selector);
    return -1;
  }
  return matching[position];
}

QJsonObject AnalysisTools::runHeader(Loaded const &loaded) const {
  return QJsonObject{{"run", runId(loaded.run)},
                     {"title", loaded.run.title},
                     {"start", clockTime(loaded.run.start)},
                     {"end", clockTime(loaded.run.end)},
                     {"duration", hms(loaded.run.duration())},
                     {"duration_s", seconds(loaded.run.duration())},
                     {"samples", static_cast<qint64>(loaded.run.samples)}};
}

// ------------------------------------------------------------------ registry

void AnalysisTools::registerTools(ToolRegistry &registry) {
  // descriptions are sent with every request: kept short, they cost prompt time on a CPU
  QJsonObject const run{{"type", QJsonArray{"string", "integer"}}, {"description", RUN_HELP}};
  QJsonObject const stage = property("string", STAGE_HELP);
  QJsonObject const from  = property("number", "range start, s from run start");
  QJsonObject const to    = property("number", "range end, s from run start");

  registry.add({"list_runs", "List recorded runs, newest first.",
                schema({{"query", property("string", "filter by title or date")}, {"limit", property("integer", "default 15")}}),
                [this](QJsonObject const &a) { return listRuns(a); }});
  registry.add({"get_run_summary", "Summary of one run: configuration, KPIs, stages, anomalies.", schema({{"run", run}}, {"run"}),
                [this](QJsonObject const &a) { return runSummary(a); }});
  registry.add({"get_day_summary",
                "What happened on a day, in time order: each experiment (setpoints, error at the end, overshoot), the idle "
                "times between them (the chamber warming or cooling with no job) and when the application was closed.",
                schema({{"date", property("string", "yyyy-MM-dd, or today")},
                        {"from", property("string", "optional start time HH:mm")},
                        {"to", property("string", "optional end time HH:mm")}},
                       {"date"}),
                [this](QJsonObject const &a) { return daySummary(a); }});
  registry.add({"get_run_details", "Run details (DUT with mass and density, operator, purpose, tags), job and program, PID table, calibration, app version and integrity, as recorded.",
                schema({{"run", run}}, {"run"}), [this](QJsonObject const &a) { return runDetails(a); }});
  registry.add({"get_events", "Events of a run: setpoint changes, ramps, PID changes, alarms, status, operator actions.",
                schema({{"run", run}, {"type", property("string", "optional filter, e.g. alarm, setpoint, pid, operator, status")}}, {"run"}),
                [this](QJsonObject const &a) { return events(a); }});
  registry.add({"list_channels", "Channels recorded in a run.", schema({{"run", run}}, {"run"}),
                [this](QJsonObject const &a) { return listChannels(a); }});
  registry.add({"get_statistics", "Statistics of channels over a run, stage or time range.",
                schema({{"run", run}, {"channels", stringList("default temp, temp_ref")}, {"stage", stage}, {"from_s", from}, {"to_s", to}}, {"run"}),
                [this](QJsonObject const &a) { return statistics(a); }});
  registry.add({"get_kpis", "Control KPIs (MAE, overshoot, settling, ...) of a run, stage or time range.",
                schema({{"run", run}, {"stage", stage}, {"from_s", from}, {"to_s", to}}, {"run"}),
                [this](QJsonObject const &a) { return kpis(a); }});
  registry.add({"get_stages", "Stages of a run (setpoint step, ramp, hold) with KPIs.", schema({{"run", run}}, {"run"}),
                [this](QJsonObject const &a) { return stages(a); }});
  registry.add({"get_anomalies", "Anomalies detected in a run.",
                schema({{"run", run}, {"min_severity", property("string", "info, warning (default), critical")}}, {"run"}),
                [this](QJsonObject const &a) { return anomalies(a); }});
  registry.add({"compare_runs", "Compare 2-5 runs, whole runs or one stage of each.",
                schema({{"runs", QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", QJsonArray{"string", "integer"}}}}, {"description", "first = reference"}}},
                        {"stage", stage},
                        {"align", property("string", "start, setpoint_change, target_crossing, stage")},
                        {"align_index", property("integer", "default 1")},
                        {"channel", property("string", "default temp")}},
                       {"runs"}),
                [this](QJsonObject const &a) { return compareRuns(a); }});
  registry.add({"rank_runs", "Rank the saved runs by a KPI to find the best or worst run.",
                schema({{"metric", property("string", "tail_mae (default), mae, overshoot, settling")}, {"limit", property("integer", "default 10")}}),
                [this](QJsonObject const &a) { return rankRuns(a); }});
  registry.add({"plot_run", "Show a run, stage or time range on the Analysis page.",
                schema({{"run", run}, {"channels", stringList("default temp, temp_ref")}, {"stage", stage}, {"from_s", from}, {"to_s", to},
                        {"compare_with", stringList("runs to overlay")}},
                       {"run"}),
                [this](QJsonObject const &a) { return plotRun(a); }});
  registry.add({"export_data",
                "Export the recorded data of a run (or one stage, or a time range) as a CSV file in the export folder, "
                "for Excel or other tools.",
                schema({{"run", run},
                        {"stage", property("string", STAGE_HELP)},
                        {"from_s", property("number", "optional, s after the start")},
                        {"to_s", property("number", "optional, s after the start")},
                        {"channels", stringList("optional state names (default: all channels)")}},
                       {"run"}),
                [this](QJsonObject const &a) { return exportData(a); }});
  registry.add({"generate_report", "Write an HTML or PDF report of a run to the export folder.",
                schema({{"run", run}, {"compare_with", stringList("runs to compare")}, {"format", property("string", "html or pdf")}}, {"run"}),
                [this](QJsonObject const &a) { return generateReport(a); }});
}

// ------------------------------------------------------------------ tools

QJsonObject AnalysisTools::listRuns(QJsonObject const &args) const {
  if (m_library->isScanning()) {
    return QJsonObject{{"error", "the experiment list is still being scanned, try again in a few seconds"}};
  }
  QVector<ExperimentRun> const all   = runs();
  QString const                query = args.value("query").toString().trimmed();
  int const                    limit{std::clamp(args.value("limit").toInt(15), 1, MAX_LIST)};
  QJsonArray                   list;
  int                          matched{0};
  for (qsizetype i{all.size() - 1}; i >= 0; --i) {
    ExperimentRun const &run = all[i];
    if (!query.isEmpty() && !run.title.contains(query, Qt::CaseInsensitive) && !dateTime(run.start).contains(query)
        && !run.details.contains(query, Qt::CaseInsensitive)) {
      continue;
    }
    if (++matched > limit) {
      continue;
    }
    QJsonObject item{{"run", QStringLiteral("#%1").arg(i + 1)},
                     {"title", run.title},
                     {"start", dateTime(run.start)},
                     {"duration", hms(run.duration())},
                     {"saved_in_database", !run.storeKey.isEmpty()}};
    if (!run.storeKey.isEmpty()) {
      if (auto stored = storage::RunStore::get(run.storeKey)) {
        item.insert("tail_mae", num(stored->tailMae));
        item.insert("overshoot", num(stored->overshoot));
        item.insert("settling_time_s", seconds(stored->settleTimeS));
        if (!stored->manifest.metadata.dut.isEmpty()) {
          item.insert("dut", stored->manifest.metadata.dut);
        }
        if (!stored->manifest.metadata.purpose.isEmpty()) {
          item.insert("purpose", stored->manifest.metadata.purpose);
        }
      }
    }
    list.push_back(item);
  }
  QJsonObject result{{"total_runs", static_cast<int>(all.size())}, {"matching", matched}, {"runs", list}};
  QVector<QStringList> rows;
  for (QJsonValue const &value : std::as_const(list)) {
    QJsonObject const item = value.toObject();
    rows.push_back({item.value("run").toString(), item.value("title").toString(), item.value("dut").toString(QStringLiteral("—")),
                    item.value("start").toString(), item.value("duration").toString(),
                    item.contains("tail_mae") ? fmt(item.value("tail_mae").toDouble(core::NaN), DEG, 3) : QStringLiteral("—"),
                    item.contains("overshoot") ? fmt(item.value("overshoot").toDouble(core::NaN), DEG, 3) : QStringLiteral("—")});
  }
  result.insert("card", makeCard(QStringLiteral("Recorded experiments"),
                                 {QStringLiteral("%1 experiments recorded%2; showing the %3 newest.")
                                    .arg(all.size())
                                    .arg(query.isEmpty() ? QString() : QStringLiteral(", %1 matching '%2'").arg(matched).arg(query))
                                    .arg(list.size())},
                                 {"Run", "Title", "DUT", "Start", "Duration", "Tail MAE", "Overshoot"}, rows));
  return result;
}

QJsonObject AnalysisTools::runSummary(QJsonObject const &args) {
  QString         error;
  LoadedPtr const loaded = load(referenceOf(args.value("run")), error);
  if (!loaded) {
    return QJsonObject{{"error", error}};
  }
  core::ExperimentAnalysis const &a = loaded->analysis;
  QJsonObject                     result = runHeader(*loaded);

  QJsonArray setpoints;
  for (double const setpoint : a.config.setpoints) {
    setpoints.push_back(num(setpoint));
  }
  result.insert("configuration", QJsonObject{{"kp", num(a.config.kp)},
                                             {"ki", num(a.config.ki)},
                                             {"kd", num(a.config.kd)},
                                             {"pid_gain_sets", a.config.gainSets},
                                             {"setpoints", setpoints},
                                             {"max_ramp_rate_per_min", num(a.config.maxRampRatePerMin)},
                                             {"sample_period_s", num(a.config.samplePeriodS)},
                                             {"program_intervals", a.config.programIntervals},
                                             {"cycles", a.config.cycles}});
  if (!a.hasControl) {
    result.insert("note", "no temp_ref recorded: no control KPIs or stages");
  } else {
    QJsonObject kpi = kpiJson(a.run.overall, a.run.hasRamp());
    kpi.insert("overshoot", num(a.run.overshoot));
    kpi.insert("undershoot", num(a.run.undershoot));
    kpi.insert("settling_time_s", seconds(a.run.settlingTimeS));
    kpi.insert("settling_note", "overshoot/undershoot: worst step; settling: last setpoint, from its step");
    result.insert("kpis", kpi);
    double const steady{core::steadyFraction(a.steady, a.run.stages.first().segment.start, a.run.stages.last().segment.end)};
    result.insert("control", QJsonObject{{"time_steady", percent(steady)},
                                         {"output_saturated", percent(a.saturation.fraction)},
                                         {"windup_s", seconds(a.saturation.windupS)},
                                         {"pid_share", QJsonObject{{"p", percent(a.pid.shareP)}, {"i", percent(a.pid.shareI)}, {"d", percent(a.pid.shareD)}}}});
    QJsonArray stageList;
    for (int k{0}; k < a.run.stages.size(); ++k) {
      stageList.push_back(stageJson(a, k, false));
    }
    result.insert("stages", stageList);
  }
  QJsonArray important;
  for (core::Anomaly const &x : a.anomalies) {
    if ((x.severity >= core::Anomaly::Severity::WARNING) && (important.size() < 6)) {
      important.push_back(anomalyJson(x));
    }
  }
  result.insert("anomalies", QJsonObject{{"total", static_cast<int>(a.anomalies.size())},
                                         {"critical", a.anomalyCount(core::Anomaly::Severity::CRITICAL)},
                                         {"warning_or_worse", a.anomalyCount(core::Anomaly::Severity::WARNING)},
                                         {"most_important", important}});

  QString const id = runId(loaded->run);
  QStringList   facts{QStringLiteral("Experiment %1 \"%2\" %3.").arg(id, loaded->run.title, spanText(loaded->run.start, loaded->run.end))};
  QStringList   levels;
  for (double const setpoint : a.config.setpoints) {
    levels << fmt(setpoint, DEG);
  }
  facts << QStringLiteral("PID gains: Kp %1, Ki %2, Kd %3.").arg(fmt(a.config.kp), fmt(a.config.ki), fmt(a.config.kd));
  if (!levels.isEmpty()) {
    facts << QStringLiteral("Setpoints: %1.").arg(levels.join(QStringLiteral(" → ")));
  }
  QVector<QStringList> rows;
  if (a.hasControl) {
    double const steady{core::steadyFraction(a.steady, a.run.stages.first().segment.start, a.run.stages.last().segment.end)};
    facts << wholeRunError(a.run, QStringLiteral(", steady-state error %1").arg(fmt(a.run.overall.steadyStateError, DEG)))
          << QStringLiteral("Worst step overshoot %1; the last setpoint step settled after %2.").arg(fmt(a.run.overshoot, DEG), hms(a.run.settlingTimeS))
          << QStringLiteral("Steady %1 of the time; control output saturated %2 of the time; P / I / D share %3 / %4 / %5.")
               .arg(pct(steady), pct(a.saturation.fraction), pct(a.pid.shareP), pct(a.pid.shareI), pct(a.pid.shareD));
    for (int k{0}; k < a.run.stages.size(); ++k) {
      rows.push_back(stageRow(a, k));
    }
    facts << QStringLiteral("%1 stages; the final stabilization is %2, %3.")
               .arg(a.run.stages.size())
               .arg(stageLabel(a, static_cast<int>(a.run.stages.size()) - 1), spanText(a.run.stages.last().segment));
  } else {
    facts << QStringLiteral("No setpoint (temp_ref) recorded, so no control KPIs or stages.");
  }
  QStringList const counts = anomalyCounts(a);
  facts << QStringLiteral("Anomalies: %1 critical, %2 warnings, %3 info.").arg(counts[0], counts[1], counts[2]);
  result.insert("card", makeCard(QStringLiteral("Experiment %1 summary").arg(id), facts, rows.isEmpty() ? QStringList{} : STAGE_COLUMNS, rows));
  return result;
}

QJsonObject AnalysisTools::daySummary(QJsonObject const &args) {
  QString const dateText = args.value("date").toString().trimmed().toLower();
  QDate const   day      = (dateText.isEmpty() || dateText == QLatin1String("today")) ? QDate::currentDate()
                                                                                       : QDate::fromString(dateText.left(10), Qt::ISODate);
  if (!day.isValid()) {
    return QJsonObject{{"error", "date must be yyyy-MM-dd or today"}};
  }
  auto timeOf = [&day](QString const &text, QTime fallback) {
    QTime const t = QTime::fromString(text.trimmed().left(5), "HH:mm");
    return QDateTime(day, t.isValid() ? t : fallback).toSecsSinceEpoch();
  };
  double const from{static_cast<double>(timeOf(args.value("from").toString(), QTime(0, 0)))};
  double const to{static_cast<double>(timeOf(args.value("to").toString(), QTime(23, 59, 59)))};
  auto clock = [](double t) { return QDateTime::fromSecsSinceEpoch(static_cast<qint64>(std::llround(t))).toString("HH:mm"); };
  auto degrees = [](double v) { return fmt(v, DEG, 3); };

  // the samples of the day (the interval file: everything received while the application was connected)
  QVector<double> times;
  QVector<double> temp;
  {
    QString         error;
    LoadedPtr const loaded = load(m_library->makeRangeRun(from, to), error);
    if (loaded && loaded->data) {
      times = loaded->data->times;
      temp  = loaded->series("temp");
    }
  }
  auto valueNear = [&](double t) {
    auto const it = std::lower_bound(times.cbegin(), times.cend(), t);
    for (auto i = it; i != times.cend() && (*i - t) < 120.0; ++i) {
      double const v = temp.value(std::distance(times.cbegin(), i), core::NaN);
      if (std::isfinite(v)) {
        return v;
      }
    }
    return core::NaN;
  };
  auto rangeOf = [&](double a, double b, double &low, double &high) {
    low  = core::NaN;
    high = core::NaN;
    for (qsizetype i{0}; i < times.size(); ++i) {
      if ((times[i] < a) || (times[i] > b) || !std::isfinite(temp.value(i, core::NaN))) {
        continue;
      }
      low  = std::isfinite(low) ? std::min(low, temp[i]) : temp[i];
      high = std::isfinite(high) ? std::max(high, temp[i]) : temp[i];
    }
  };

  // the experiments saved that day, with their number in the experiment list
  QHash<QString, QString> ids;
  for (ExperimentRun const &run : runs()) {
    if (!run.storeKey.isEmpty()) {
      ids.insert(run.storeKey, runId(run));
    }
  }
  struct Item {
    double      start;
    double      end;
    QString     what;
    QString     temperature;
    QString     details;
    QJsonObject json;
  };
  QVector<Item> items;
  QVector<std::pair<double, double>> experimentSpans;
  for (storage::ExperimentSpan const &span : storage::RunStore::spans()) {
    if (!std::isfinite(span.start) || !std::isfinite(span.end) || (span.end < from) || (span.start > to)) {
      continue;
    }
    experimentSpans.push_back({span.start, span.end});
    QString     kind;
    QStringList setpoints;
    QJsonObject json{{"run", ids.value(span.key, span.name)}, {"from", clock(span.start)}, {"to", clock(span.end)}, {"duration", hms(span.end - span.start)}};
    QStringList details;
    if (auto const stored = storage::RunStore::get(span.key)) {
      QJsonObject const job = stored->manifest.configuration.value("job").toObject();
      kind                  = job.value("kind").toString().replace(QLatin1Char('_'), QLatin1Char(' '));
      for (QJsonValue const &interval : job.value("program").toObject().value("intervals").toArray()) {
        double const value{interval.toObject().value("temp").toDouble(core::NaN)};
        QString const text = fmt(value, DEG);
        if (std::isfinite(value) && !setpoints.contains(text)) {
          setpoints << text;
        }
      }
      if (!setpoints.isEmpty()) {
        details << QStringLiteral("setpoint %1").arg(setpoints.join(QStringLiteral(" → ")));
        json.insert("setpoints", QJsonArray::fromStringList(setpoints));
      }
      if (std::isfinite(stored->tailMae)) {
        details << QStringLiteral("error at the end %1").arg(degrees(stored->tailMae));
        json.insert("tail_mae", num(stored->tailMae));
      }
      if (std::isfinite(stored->overshoot)) {
        details << QStringLiteral("overshoot %1").arg(degrees(stored->overshoot));
        json.insert("overshoot", num(stored->overshoot));
      }
    }
    double low{core::NaN};
    double high{core::NaN};
    rangeOf(span.start, span.end, low, high);
    double const first{valueNear(span.start)};
    double const last{valueNear(span.end - 1.0)};
    json.insert("kind", kind.isEmpty() ? QStringLiteral("experiment") : kind);
    json.insert("temp_start", num(first));
    json.insert("temp_end", num(last));
    json.insert("temp_min", num(low));
    json.insert("temp_max", num(high));
    items.push_back({span.start, span.end, QStringLiteral("%1 %2").arg(ids.value(span.key, QStringLiteral("experiment")), kind.isEmpty() ? QStringLiteral("experiment") : kind),
                     QStringLiteral("%1 → %2 (min %3, max %4)").arg(fmt(first, DEG), fmt(last, DEG), fmt(low, DEG), fmt(high, DEG)),
                     details.join(QStringLiteral(" · ")), json});
  }
  std::sort(experimentSpans.begin(), experimentSpans.end());

  // the recorded data between the experiments: idle (no job), and where nothing was recorded (application closed)
  constexpr double GAP_S{30.0};
  constexpr double MIN_IDLE_S{120.0};
  QVector<std::pair<double, double>> recorded; // stretches of samples
  for (qsizetype i{0}; i < times.size(); ++i) {
    if (recorded.isEmpty() || (times[i] - recorded.last().second > GAP_S)) {
      recorded.push_back({times[i], times[i]});
    } else {
      recorded.last().second = times[i];
    }
  }
  QJsonArray closed;
  for (qsizetype k{1}; k < recorded.size(); ++k) {
    double const a{recorded[k - 1].second};
    double const b{recorded[k].first};
    closed.push_back(QJsonObject{{"from", clock(a)}, {"to", clock(b)}, {"duration", hms(b - a)}});
    items.push_back({a, b, QStringLiteral("application closed"), QStringLiteral("—"), QStringLiteral("nothing recorded"), {}});
  }
  QJsonArray idle;
  for (auto const &[a0, b0] : recorded) {
    // the parts of this stretch outside every experiment
    double cursor{a0};
    auto addIdle = [&](double a, double b) {
      if (b - a < MIN_IDLE_S) {
        return;
      }
      double low{core::NaN};
      double high{core::NaN};
      rangeOf(a, b, low, high);
      double const first{valueNear(a)};
      double const last{valueNear(b - 1.0)};
      QString const trend = !(std::isfinite(first) && std::isfinite(last)) ? QString{}
                            : (last > first + 1.0)                      ? QStringLiteral("warming")
                            : (last < first - 1.0)                      ? QStringLiteral("cooling")
                                                                        : QStringLiteral("steady");
      QJsonObject json{{"from", clock(a)}, {"to", clock(b)}, {"duration", hms(b - a)}, {"temp_start", num(first)}, {"temp_end", num(last)}, {"trend", trend}};
      idle.push_back(json);
      items.push_back({a, b, QStringLiteral("no job"), QStringLiteral("%1 → %2").arg(fmt(first, DEG), fmt(last, DEG)),
                       trend.isEmpty() ? QString{} : QStringLiteral("chamber %1 on its own").arg(trend), json});
    };
    for (auto const &[s, e] : experimentSpans) {
      if ((e < cursor) || (s > b0)) {
        continue;
      }
      addIdle(cursor, std::min(s, b0));
      cursor = std::max(cursor, e);
    }
    addIdle(cursor, b0);
  }

  std::sort(items.begin(), items.end(), [](Item const &a, Item const &b) { return a.start < b.start; });
  QJsonArray experimentsJson;
  for (Item const &item : std::as_const(items)) {
    if (item.json.contains("run")) {
      experimentsJson.push_back(item.json);
    }
  }
  QJsonObject result{{"date", day.toString(Qt::ISODate)},
                     {"recorded_from", times.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(clock(times.first()))},
                     {"recorded_to", times.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(clock(times.last()))},
                     {"experiments", experimentsJson},
                     {"idle", idle},
                     {"application_closed", closed}};

  QStringList facts;
  if (times.isEmpty() && experimentsJson.isEmpty()) {
    facts << QStringLiteral("Nothing was recorded on %1.").arg(day.toString(Qt::ISODate));
  } else {
    facts << QStringLiteral("%1: %2 experiments; data recorded %3.")
               .arg(day.toString(Qt::ISODate))
               .arg(experimentsJson.size())
               .arg(times.isEmpty() ? QStringLiteral("only in the experiment database")
                                    : QStringLiteral("from %1 to %2").arg(clock(times.first()), clock(times.last())));
    // the figures a summary of the day needs, ready made (a small model mixes setpoints and temperatures up)
    QStringList daySetpoints;
    for (QJsonValue const &experiment : std::as_const(experimentsJson)) {
      for (QJsonValue const &setpoint : experiment.toObject().value("setpoints").toArray()) {
        if (!daySetpoints.contains(setpoint.toString())) {
          daySetpoints << setpoint.toString();
        }
      }
    }
    if (!daySetpoints.isEmpty()) {
      facts << QStringLiteral("Setpoints of the experiments: %1.").arg(daySetpoints.join(QStringLiteral(", ")));
    }
    qsizetype lowest{-1};
    qsizetype highest{-1};
    for (qsizetype i{0}; i < times.size(); ++i) {
      double const v{temp.value(i, core::NaN)};
      if (!std::isfinite(v)) {
        continue;
      }
      lowest  = ((lowest < 0) || (v < temp[lowest])) ? i : lowest;
      highest = ((highest < 0) || (v > temp[highest])) ? i : highest;
    }
    if ((lowest >= 0) && (highest >= 0)) {
      facts << QStringLiteral("Measured temperature: lowest %1 at %2, highest %3 at %4.")
                 .arg(fmt(temp[lowest], DEG), clock(times[lowest]), fmt(temp[highest], DEG), clock(times[highest]));
    }
    if (!closed.isEmpty()) {
      facts << QStringLiteral("The application was closed %1 times (nothing recorded then).").arg(closed.size());
    }
    if (!idle.isEmpty()) {
      facts << QStringLiteral("%1 periods with no job: the chamber warmed or cooled on its own.").arg(idle.size());
    }
  }
  QVector<QStringList> rows;
  for (Item const &item : std::as_const(items)) {
    if (rows.size() >= 40) {
      break;
    }
    rows.push_back({clock(item.start), clock(item.end), hms(item.end - item.start), item.what, item.temperature, item.details});
  }
  result.insert("card", makeCard(QStringLiteral("Day %1").arg(day.toString(Qt::ISODate)), facts,
                                 {"From", "To", "Duration", "What", "Temperature", "Details"}, rows));
  return result;
}

QJsonObject AnalysisTools::exportData(QJsonObject const &args) {
  QString         error;
  LoadedPtr const loaded = load(referenceOf(args.value("run")), error);
  if (!loaded || !loaded->data) {
    return QJsonObject{{"error", error.isEmpty() ? QStringLiteral("the run could not be read") : error}};
  }
  double from{optionalNumber(args, "from_s")};
  double to{optionalNumber(args, "to_s")};
  QString part = QStringLiteral("all");
  if (args.contains("stage")) {
    int const index{selectStage(loaded->analysis, args.value("stage").toString(), error)};
    if (index < 0) {
      return QJsonObject{{"error", error}};
    }
    from = loaded->analysis.run.stages[index].segment.start;
    to   = loaded->analysis.run.stages[index].segment.end;
    part = stageLabel(loaded->analysis, index);
  } else if (std::isfinite(from) || std::isfinite(to)) {
    part = QStringLiteral("%1 to %2").arg(hms(std::isfinite(from) ? from : 0.0), hms(std::isfinite(to) ? to : loaded->run.duration()));
  }
  // the channels: those asked for (unknown ones reported), else every recorded one
  QStringList       names = stringsOf(args.value("channels"));
  QStringList       unknown;
  QStringList const recorded = loaded->names;
  if (names.isEmpty()) {
    names = recorded;
  }
  QStringList columns;
  QStringList headers;
  for (QString const &name : std::as_const(names)) {
    qsizetype const index = recorded.indexOf(name);
    if (index < 0) {
      unknown << name;
      continue;
    }
    columns << name;
    QString const label = loaded->channels.value(index).label;
    headers << (label.isEmpty() || (label == name) ? name : QStringLiteral("%1 (%2)").arg(label, name));
  }
  if (columns.isEmpty()) {
    return QJsonObject{{"error", QStringLiteral("no such channels: %1 (see list_channels)").arg(unknown.join(QStringLiteral(", ")))}};
  }

  QString safe = loaded->run.title;
  safe.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]+")), QStringLiteral("_"));
  QDir const folder(QDir(deepvaccontrol::EXPORT_DIR).absoluteFilePath(QStringLiteral("data")));
  QDir().mkpath(folder.absolutePath());
  QString const path = folder.absoluteFilePath(QStringLiteral("%1%2.csv")
                                                 .arg(safe.left(80), args.contains("stage") || std::isfinite(from) || std::isfinite(to)
                                                                       ? QStringLiteral("_%1-%2").arg(std::llround(std::isfinite(from) ? from : 0.0)).arg(std::llround(std::isfinite(to) ? to : loaded->run.duration()))
                                                                       : QString{}));
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
    return QJsonObject{{"error", QStringLiteral("cannot write %1").arg(path)}};
  }
  QVector<QVector<double>> series;
  for (QString const &name : std::as_const(columns)) {
    series << loaded->series(name);
  }
  QTextStream out(&file);
  out.setGenerateByteOrderMark(true); // Excel then reads the UTF-8 (°C) right
  out << "time,elapsed_s";
  for (QString const &header : std::as_const(headers)) {
    out << ',' << '"' << QString(header).replace(QLatin1Char('"'), QStringLiteral("\"\"")) << '"';
  }
  out << '\n';
  qint64 written{0};
  for (qsizetype i{0}; i < loaded->times.size(); ++i) {
    double const t{loaded->times[i]};
    if ((std::isfinite(from) && t < from) || (std::isfinite(to) && t > to)) {
      continue;
    }
    out << QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(std::llround(loaded->data->times.value(i) * 1000.0))).toString("yyyy-MM-dd HH:mm:ss.zzz")
        << ',' << QString::number(t, 'f', 3);
    for (QVector<double> const &column : std::as_const(series)) {
      double const v{column.value(i, core::NaN)};
      out << ',' << (std::isfinite(v) ? QString::number(v, 'g', 9) : QString{});
    }
    out << '\n';
    ++written;
  }
  file.close();

  QStringList facts{QStringLiteral("Exported %1 of experiment %2: %3 samples, %4 channels.").arg(part, runId(loaded->run)).arg(written).arg(columns.size()),
                    QStringLiteral("File: %1").arg(QDir::toNativeSeparators(path))};
  if (!unknown.isEmpty()) {
    facts << QStringLiteral("Not recorded, so left out: %1.").arg(unknown.join(QStringLiteral(", ")));
  }
  QJsonObject result{{"path", QDir::toNativeSeparators(path)}, {"samples", written}, {"channels", QJsonArray::fromStringList(columns)}};
  result.insert("card", makeCard(QStringLiteral("Data of experiment %1 exported").arg(runId(loaded->run)), facts));
  return result;
}

QJsonObject AnalysisTools::listChannels(QJsonObject const &args) {
  QString         error;
  LoadedPtr const loaded = load(referenceOf(args.value("run")), error);
  if (!loaded) {
    return QJsonObject{{"error", error}};
  }
  QJsonArray channels;
  for (ChannelInfo const &channel : loaded->channels) {
    channels.push_back(QJsonObject{{"name", channel.name}, {"description", channel.label}});
  }
  QJsonObject result = runHeader(*loaded);
  result.insert("channels", channels);
  QVector<QStringList> rows;
  for (ChannelInfo const &channel : loaded->channels) {
    rows.push_back({channel.name, channel.label});
  }
  result.insert("card", makeCard(QStringLiteral("Channels of experiment %1").arg(runId(loaded->run)),
                                 {QStringLiteral("%1 recorded channels.").arg(rows.size())}, {"Name", "Description"}, rows));
  return result;
}

QJsonObject AnalysisTools::statistics(QJsonObject const &args) {
  QString         error;
  LoadedPtr const loaded = load(referenceOf(args.value("run")), error);
  if (!loaded) {
    return QJsonObject{{"error", error}};
  }
  double from{optionalNumber(args, "from_s")};
  double to{optionalNumber(args, "to_s")};
  QJsonObject result = runHeader(*loaded);
  if (args.contains("stage")) {
    int const index{selectStage(loaded->analysis, args.value("stage").toString(), error)};
    if (index < 0) {
      return QJsonObject{{"error", error}};
    }
    from = loaded->analysis.run.stages[index].segment.start;
    to   = loaded->analysis.run.stages[index].segment.end;
    result.insert("stage", stageJson(loaded->analysis, index, false));
  }
  QStringList channels = stringsOf(args.value("channels"));
  if (channels.isEmpty()) {
    channels = {"temp", "temp_ref"};
  }
  QJsonArray rows;
  for (QString const &name : std::as_const(channels)) {
    QVector<double> const values = loaded->series(name);
    if (values.isEmpty()) {
      rows.push_back(QJsonObject{{"channel", name}, {"error", "no such channel (see list_channels)"}});
      continue;
    }
    core::SeriesStats const s = core::computeStats(loaded->times, values, from, to);
    rows.push_back(QJsonObject{{"channel", name},   {"n", static_cast<qint64>(s.n)}, {"missing", static_cast<qint64>(s.missing)},
                               {"min", num(s.min)}, {"max", num(s.max)},             {"mean", num(s.mean)},
                               {"std", num(s.std)}, {"rms", num(s.rms)},             {"slope_per_min", num(s.slopePerS * 60.0)},
                               {"p95", num(s.p95)}});
  }
  result.insert("range", QJsonObject{{"from_s", seconds(std::isfinite(from) ? from : 0.0)},
                                     {"to_s", seconds(std::isfinite(to) ? to : loaded->run.duration())}});
  result.insert("statistics", rows);
  QVector<QStringList> table;
  for (QJsonValue const &value : std::as_const(rows)) {
    QJsonObject const row = value.toObject();
    if (row.contains("error")) {
      table.push_back({row.value("channel").toString(), row.value("error").toString(), "", "", "", "", ""});
      continue;
    }
    auto cell = [&row](char const *key) { return fmt(row.value(QLatin1String(key)).toDouble(core::NaN)); };
    table.push_back({row.value("channel").toString(), QString::number(row.value("n").toInt()), cell("min"), cell("max"), cell("mean"),
                     cell("std"), cell("slope_per_min")});
  }
  QString const where = result.contains("stage")
                          ? stageLabel(loaded->analysis, result.value("stage").toObject().value("stage").toInt() - 1)
                          : (std::isfinite(from) || std::isfinite(to))
                              ? QStringLiteral("%1 to %2").arg(hms(std::isfinite(from) ? from : 0.0), hms(std::isfinite(to) ? to : loaded->run.duration()))
                              : QStringLiteral("the whole experiment");
  result.insert("card", makeCard(QStringLiteral("Statistics of experiment %1").arg(runId(loaded->run)),
                                 {QStringLiteral("Over %1. Values in each channel's own unit; slope per minute.").arg(where)},
                                 {"Channel", "Samples", "Min", "Max", "Mean", "Std", "Slope /min"}, table));
  return result;
}

QJsonObject AnalysisTools::kpis(QJsonObject const &args) {
  QString         error;
  LoadedPtr const loaded = load(referenceOf(args.value("run")), error);
  if (!loaded) {
    return QJsonObject{{"error", error}};
  }
  core::ExperimentAnalysis const &a      = loaded->analysis;
  QJsonObject                     result = runHeader(*loaded);
  if (!a.hasControl) {
    return QJsonObject{{"error", "this run has no temp / temp_ref, so no control KPIs"}};
  }
  if (args.contains("stage")) {
    int const index{selectStage(a, args.value("stage").toString(), error)};
    if (index < 0) {
      return QJsonObject{{"error", error}};
    }
    result.insert("stage", stageJson(a, index, true));
    core::StageAnalysis const &stage = a.run.stages[index];
    core::StageDetail const   &d     = a.stageDetails.value(index);
    QStringList                facts{QStringLiteral("Experiment %1, %2, %3.").arg(runId(loaded->run), stageLabel(a, index), spanText(stage.segment)),
                      core::tracksSetpoint(stage.segment.kind)
                        ? QStringLiteral("MAE %1, RMSE %2, max error %3.").arg(fmt(stage.kpis.mae, DEG), fmt(stage.kpis.rmse, DEG), fmt(stage.kpis.maxAbsError, DEG))
                        : QStringLiteral("A setpoint step: error at its end (last 5 min) %1, std %2.").arg(fmt(stage.kpis.tailMae, DEG), fmt(stage.kpis.tailStd, DEG))};
    if (stage.segment.kind == core::Segment::Kind::SETPOINT) {
      facts << QStringLiteral("Step response: overshoot %1, settling %2, steady-state error %3.")
                 .arg(fmt(stage.kpis.overshoot, DEG), hms(stage.kpis.settlingTimeS), fmt(stage.kpis.steadyStateError, DEG));
    }
    if (stage.segment.kind == core::Segment::Kind::RAMP) {
      facts << QStringLiteral("Ramp: setpoint %1 /min, temperature %2 /min, tracking lag %3.")
                 .arg(fmt(d.referenceRatePerMin, DEG), fmt(d.valueRatePerMin, DEG), hms(d.trackingLagS));
    }
    facts << QStringLiteral("Steady %1 of the stage; output saturated %2; P / I / D share %3 / %4 / %5; gains Kp %6, Ki %7, Kd %8.")
               .arg(pct(d.steadyFraction), pct(d.saturation.fraction), pct(d.pid.shareP), pct(d.pid.shareI), pct(d.pid.shareD), fmt(d.kp),
                    fmt(d.ki), fmt(d.kd));
    result.insert("card", makeCard(QStringLiteral("Experiment %1, stage %2").arg(runId(loaded->run)).arg(index + 1), facts));
    return result;
  }
  double const from{optionalNumber(args, "from_s")};
  double const to{optionalNumber(args, "to_s")};
  if (std::isfinite(from) || std::isfinite(to)) {
    QVector<double> times, value, reference, interval, cycle;
    QVector<double> const v = loaded->series("temp"), r = loaded->series("temp_ref"), iv = loaded->series("interval"),
                          cy = loaded->series("cycle_cur");
    for (qsizetype i{0}; i < loaded->times.size(); ++i) {
      double const t{loaded->times[i]};
      if ((std::isfinite(from) && t < from) || (std::isfinite(to) && t > to)) {
        continue;
      }
      times << t;
      value << v.value(i, core::NaN);
      reference << r.value(i, core::NaN);
      if (!iv.isEmpty()) {
        interval << iv.value(i, core::NaN);
      }
      if (!cy.isEmpty()) {
        cycle << cy.value(i, core::NaN);
      }
    }
    core::RunAnalysis const range = core::analyseRun(times, value, reference, interval, cycle);
    QJsonObject             kpi   = kpiJson(range.overall, range.hasRamp());
    kpi.insert("overshoot", num(range.overshoot));
    kpi.insert("settling_time_s", seconds(range.settlingTimeS));
    result.insert("range", QJsonObject{{"from_s", seconds(std::isfinite(from) ? from : 0.0)}, {"to_s", seconds(std::isfinite(to) ? to : loaded->run.duration())}});
    result.insert("kpis", kpi);
    result.insert("stages_in_range", static_cast<int>(range.stages.size()));
    result.insert("card", makeCard(QStringLiteral("Experiment %1, %2 to %3").arg(runId(loaded->run), hms(std::isfinite(from) ? from : 0.0),
                                                                           hms(std::isfinite(to) ? to : loaded->run.duration())),
                                   {wholeRunError(range, QString()),
                                    QStringLiteral("Worst step overshoot %1; settling of the last step %2; %3 stages in the range.")
                                      .arg(fmt(range.overshoot, DEG), hms(range.settlingTimeS))
                                      .arg(range.stages.size())}));
    return result;
  }
  QJsonObject kpi = kpiJson(a.run.overall, a.run.hasRamp());
  kpi.insert("overshoot", num(a.run.overshoot));
  kpi.insert("undershoot", num(a.run.undershoot));
  kpi.insert("settling_time_s", seconds(a.run.settlingTimeS));
  kpi.insert("note", "overshoot/undershoot: worst step response; settling: last setpoint, measured from its step");
  result.insert("kpis", kpi);
  QStringList facts{wholeRunError(a.run, QString()),
                    QStringLiteral("Worst step overshoot %1, worst undershoot %2.").arg(fmt(a.run.overshoot, DEG), fmt(a.run.undershoot, DEG)),
                    QStringLiteral("The last setpoint step settled after %1; steady-state error %2.").arg(hms(a.run.settlingTimeS), fmt(a.run.overall.steadyStateError, DEG))};
  if (a.run.hasRamp()) { // (without a ramp the error above is already the one at the end)
    facts << QStringLiteral("Last %1 of the experiment: MAE %2, std %3.").arg(hms(300.0), fmt(a.run.overall.tailMae, DEG), fmt(a.run.overall.tailStd, DEG));
  }
  result.insert("card", makeCard(QStringLiteral("Experiment %1 control KPIs").arg(runId(loaded->run)), facts));
  return result;
}

QJsonObject AnalysisTools::stages(QJsonObject const &args) {
  QString         error;
  LoadedPtr const loaded = load(referenceOf(args.value("run")), error);
  if (!loaded) {
    return QJsonObject{{"error", error}};
  }
  QJsonObject result = runHeader(*loaded);
  QJsonArray  list;
  for (int k{0}; k < loaded->analysis.run.stages.size(); ++k) {
    list.push_back(stageJson(loaded->analysis, k, true));
  }
  result.insert("stages", list);
  if (list.isEmpty()) {
    result.insert("note", "no stages: the run has no temp_ref");
  }
  QVector<QStringList> rows;
  QStringList          facts{QStringLiteral("Experiment %1 has %2 stages (setpoint = step response, ramp, hold = steady soak).").arg(runId(loaded->run)).arg(list.size())};
  for (int k{0}; k < loaded->analysis.run.stages.size(); ++k) {
    rows.push_back(stageRow(loaded->analysis, k));
    facts << QStringLiteral("%1: %2.").arg(stageLabel(loaded->analysis, k), spanText(loaded->analysis.run.stages[k].segment));
  }
  result.insert("card", makeCard(QStringLiteral("Stages of experiment %1").arg(runId(loaded->run)), facts, STAGE_COLUMNS, rows));
  return result;
}

QJsonObject AnalysisTools::anomalies(QJsonObject const &args) {
  QString         error;
  LoadedPtr const loaded = load(referenceOf(args.value("run")), error);
  if (!loaded) {
    return QJsonObject{{"error", error}};
  }
  QString const           minimum = args.value("min_severity").toString(QStringLiteral("warning")).toLower();
  core::Anomaly::Severity threshold{core::Anomaly::Severity::WARNING};
  if (minimum == QLatin1String("info")) {
    threshold = core::Anomaly::Severity::INFO;
  } else if (minimum == QLatin1String("critical")) {
    threshold = core::Anomaly::Severity::CRITICAL;
  }
  QJsonArray list;
  for (core::Anomaly const &x : loaded->analysis.anomalies) {
    if (x.severity >= threshold) {
      list.push_back(anomalyJson(x));
    }
  }
  QJsonObject result = runHeader(*loaded);
  result.insert("total_detected", static_cast<int>(loaded->analysis.anomalies.size()));
  result.insert("shown", list.size());
  result.insert("anomalies", list);
  QStringList const    counts = anomalyCounts(loaded->analysis);
  QStringList          facts{QStringLiteral("Experiment %1: %2 anomalies in total: %3 critical, %4 warnings, %5 info.")
                      .arg(runId(loaded->run))
                      .arg(loaded->analysis.anomalies.size())
                      .arg(counts[0], counts[1], counts[2])};
  QVector<QStringList> rows;
  for (core::Anomaly const &x : loaded->analysis.anomalies) {
    if (x.severity >= threshold) {
      facts << anomalyText(x) + QLatin1Char('.');
      rows.push_back({core::Anomaly::severityName(x.severity), core::Anomaly::kindName(x.kind), x.channel,
                      x.start < 0.5 ? QStringLiteral("0 s") : hms(x.start), hms(x.end - x.start), x.detail});
    }
  }
  if (rows.isEmpty()) {
    facts << QStringLiteral("None at %1 level or above.").arg(minimum);
  }
  result.insert("card", makeCard(QStringLiteral("Anomalies of experiment %1").arg(runId(loaded->run)), facts,
                                 {"Severity", "Type", "Channel", "Starts after", "Duration", "Detail"}, rows));
  return result;
}

QJsonObject AnalysisTools::compareRuns(QJsonObject const &args) {
  QStringList const references = stringsOf(args.value("runs"));
  if ((references.size() < 2) || (references.size() > 5)) {
    return QJsonObject{{"error", "give 2 to 5 runs"}};
  }
  QVector<LoadedPtr> loaded;
  for (QString const &reference : references) {
    QString         error;
    LoadedPtr const one = load(reference, error);
    if (!one) {
      return QJsonObject{{"error", QStringLiteral("%1: %2").arg(reference, error)}};
    }
    loaded.push_back(one);
  }
  QString const channel = args.value("channel").toString(QStringLiteral("temp"));
  QJsonArray    runsJson;
  for (LoadedPtr const &one : std::as_const(loaded)) {
    runsJson.push_back(runHeader(*one));
  }
  QJsonObject result{{"runs", runsJson}, {"reference_run", runId(loaded.first()->run)}};

  // ---- one stage of each run
  if (args.contains("stage")) {
    QString const selector = args.value("stage").toString();
    QJsonArray    stagesJson;
    QVector<int>  indexes;
    for (LoadedPtr const &one : std::as_const(loaded)) {
      QString   error;
      int const index{selectStage(one->analysis, selector, error)};
      if (index < 0) {
        return QJsonObject{{"error", QStringLiteral("%1: %2").arg(runId(one->run), error)}};
      }
      indexes.push_back(index);
      QJsonObject item = stageJson(one->analysis, index, true);
      item.insert("run", runId(one->run));
      stagesJson.push_back(item);
    }
    result.insert("stage_selector", selector);
    result.insert("stages", stagesJson);

    // the same metrics of every run side by side, plus the difference to the first run
    struct Metric {
      char const *name;
      char const *label;
      char const *unit;
      int         better; // -1 lower is better, +1 higher, 0 neither
      double (*get)(core::StageAnalysis const &, core::StageDetail const &);
    };
    static Metric const metrics[]{
      {"mae", "MAE (ramps and holds)", "°C", -1,
       [](core::StageAnalysis const &s, core::StageDetail const &) { return core::tracksSetpoint(s.segment.kind) ? s.kpis.mae : core::NaN; }},
      {"tail_mae", "MAE at the end", "°C", -1, [](core::StageAnalysis const &s, core::StageDetail const &) { return s.kpis.tailMae; }},
      {"rmse", "RMSE", "°C", -1, [](core::StageAnalysis const &s, core::StageDetail const &) { return s.kpis.rmse; }},
      {"max_abs_error", "Max error", "°C", -1, [](core::StageAnalysis const &s, core::StageDetail const &) { return s.kpis.maxAbsError; }},
      {"overshoot", "Overshoot", "°C", -1, [](core::StageAnalysis const &s, core::StageDetail const &) { return s.kpis.overshoot; }},
      {"settling_time", "Settling", "s", -1, [](core::StageAnalysis const &s, core::StageDetail const &) { return s.kpis.settlingTimeS; }},
      {"steady_state_error", "Steady-state error (abs)", "°C", -1,
       [](core::StageAnalysis const &s, core::StageDetail const &) { return std::fabs(s.kpis.steadyStateError); }},
      {"tail_std", "Std at the end", "°C", -1, [](core::StageAnalysis const &s, core::StageDetail const &) { return s.kpis.tailStd; }},
      {"time_steady", "Time steady", "%", +1, [](core::StageAnalysis const &, core::StageDetail const &d) { return 100.0 * d.steadyFraction; }},
      {"output_saturated", "Output saturated", "%", -1,
       [](core::StageAnalysis const &, core::StageDetail const &d) { return 100.0 * d.saturation.fraction; }},
      {"duration", "Duration", "s", 0, [](core::StageAnalysis const &s, core::StageDetail const &) { return s.segment.duration(); }},
    };
    QJsonArray table;
    for (Metric const &metric : metrics) {
      QJsonObject row{{"metric", metric.name}, {"unit", QString::fromUtf8(metric.unit)}};
      double      first{core::NaN};
      for (qsizetype k{0}; k < loaded.size(); ++k) {
        double const value{metric.get(loaded[k]->analysis.run.stages[indexes[k]], loaded[k]->analysis.stageDetails.value(indexes[k]))};
        row.insert(runId(loaded[k]->run), num(value));
        if (k == 0) {
          first = value;
        } else {
          row.insert(QStringLiteral("difference_%1_minus_%2").arg(runId(loaded[k]->run), runId(loaded[0]->run)), num(value - first));
        }
      }
      table.push_back(row);
    }
    result.insert("comparison", table);

    // the stage's signals, both starting at the stage start
    QJsonArray signalRows;
    for (qsizetype k{1}; k < loaded.size(); ++k) {
      auto slice = [&](Loaded const &one, core::Segment const &s, QVector<double> &t, QVector<double> &v) {
        QVector<double> const values = one.series(channel);
        for (int i{s.first}; (i <= s.last) && (i < values.size()); ++i) {
          t << one.times[i];
          v << values[i];
        }
      };
      core::Segment const &sa = loaded[0]->analysis.run.stages[indexes[0]].segment;
      core::Segment const &sb = loaded[k]->analysis.run.stages[indexes[k]].segment;
      QVector<double>      ta, va, tb, vb;
      slice(*loaded[0], sa, ta, va);
      slice(*loaded[k], sb, tb, vb);
      core::SignalComparison const signal = core::compareSignals(ta, va, sa.start, tb, vb, sb.start);
      signalRows.push_back(QJsonObject{{"signal", QStringLiteral("%1 of %2 minus %3").arg(channel, runId(loaded[k]->run), runId(loaded[0]->run))},
                                       {"aligned_on", "stage start"},
                                       {"overlap_s", seconds(signal.overlapS)},
                                       {"mean_difference", num(signal.meanDiff)},
                                       {"rms_difference", num(signal.rmsDiff)},
                                       {"max_abs_difference", num(signal.maxAbsDiff)},
                                       {"correlation", num(signal.correlation)}});
    }
    result.insert("signal_differences", signalRows);

    // card: one row per metric, the better run named by DeepVac
    QStringList columns{"Metric"};
    for (LoadedPtr const &one : std::as_const(loaded)) {
      columns << runId(one->run);
    }
    columns << (loaded.size() == 2 ? QStringLiteral("Verdict") : QStringLiteral("Verdict vs %1").arg(runId(loaded[0]->run)));
    QStringList facts;
    for (qsizetype k{0}; k < loaded.size(); ++k) {
      facts << QStringLiteral("Experiment %1: %2, %3.").arg(runId(loaded[k]->run), stageLabel(loaded[k]->analysis, indexes[k]),
                                                    spanText(loaded[k]->analysis.run.stages[indexes[k]].segment));
    }
    QVector<QStringList> rows;
    QVector<int>         wins(loaded.size(), 0);
    for (Metric const &metric : metrics) {
      QString const unit = QString::fromUtf8(metric.unit);
      QStringList   row{QString::fromUtf8(metric.label)};
      QStringList   verdicts;
      double const  first{metric.get(loaded[0]->analysis.run.stages[indexes[0]], loaded[0]->analysis.stageDetails.value(indexes[0]))};
      for (qsizetype k{0}; k < loaded.size(); ++k) {
        double const value{metric.get(loaded[k]->analysis.run.stages[indexes[k]], loaded[k]->analysis.stageDetails.value(indexes[k]))};
        row << (unit == QLatin1String("s") ? hms(value) : fmt(value, unit));
        if ((k > 0) && (metric.better != 0)) {
          QString const v = verdict(first, value, metric.better < 0, runId(loaded[0]->run), runId(loaded[k]->run), unit == QLatin1String("s") ? QStringLiteral("s") : unit);
          verdicts << v;
          if (v.startsWith(runId(loaded[k]->run) + QLatin1Char(' '))) {
            ++wins[k];
          } else if (v.startsWith(runId(loaded[0]->run) + QLatin1Char(' '))) {
            ++wins[0];
          }
        }
      }
      row << (metric.better == 0 ? QStringLiteral("—") : verdicts.join(QStringLiteral("; ")));
      rows.push_back(row);
    }
    if (loaded.size() == 2) {
      facts << QStringLiteral("%1 is better on %2 metrics, %3 on %4; the rest are practically equal.")
                 .arg(runId(loaded[1]->run))
                 .arg(wins[1])
                 .arg(runId(loaded[0]->run))
                 .arg(wins[0]);
    }
    for (QJsonValue const &value : std::as_const(signalRows)) {
      QJsonObject const signal = value.toObject();
      facts << QStringLiteral("Signal %1, aligned at the stage start: mean difference %2, RMS difference %3, correlation %4.")
                 .arg(signal.value("signal").toString(), fmt(signal.value("mean_difference").toDouble(core::NaN), DEG, 3),
                      fmt(signal.value("rms_difference").toDouble(core::NaN), DEG, 3), fmt(signal.value("correlation").toDouble(core::NaN), {}, 3));
    }
    result.insert("card", makeCard(QStringLiteral("Comparison, %1").arg(selector), facts, columns, rows));
    return result;
  }

  // ---- whole runs
  bool                  alignOk{false};
  core::AlignMode const mode = alignMode(args.value("align").toString(), alignOk);
  if (!alignOk) {
    return QJsonObject{{"error", "align must be start, setpoint_change, target_crossing or stage"}};
  }
  int const n{std::max(1, args.value("align_index").toInt(1))};
  QVector<core::ExperimentAnalysis const *> analyses;
  QVector<core::RunConfig const *>          configs;
  for (LoadedPtr const &one : std::as_const(loaded)) {
    analyses.push_back(&one->analysis);
    configs.push_back(&one->analysis.config);
  }

  QJsonArray kpiRows;
  for (core::KpiComparisonRow const &row : core::compareKpis(analyses)) {
    QJsonObject item{{"kpi", row.key},
                     {"unit", row.unit},
                     {"better", row.better == core::Better::LOWER ? "lower" : row.better == core::Better::HIGHER ? "higher" : "n/a"}};
    for (qsizetype k{0}; k < row.values.size(); ++k) {
      item.insert(runId(loaded[k]->run), row.unit == QLatin1String("s") ? seconds(row.values[k]) : num(row.values[k]));
      if (k > 0) {
        item.insert(QStringLiteral("difference_%1_minus_%2").arg(runId(loaded[k]->run), runId(loaded[0]->run)), num(row.values[k] - row.values[0]));
      }
    }
    kpiRows.push_back(item);
  }
  result.insert("kpis", kpiRows);

  QJsonArray configRows;
  for (core::ConfigComparisonRow const &row : core::compareConfigs(configs)) {
    if (row.differs || row.label.startsWith(QLatin1String("K"))) {
      QJsonObject item{{"setting", row.label}, {"differs", row.differs}};
      for (qsizetype k{0}; k < row.values.size(); ++k) {
        item.insert(runId(loaded[k]->run), row.values[k]);
      }
      configRows.push_back(item);
    }
  }
  result.insert("configuration", configRows);

  QJsonArray steps;
  for (core::StepMatch const &match : core::matchSteps(analyses)) {
    QJsonObject perRun;
    for (qsizetype k{0}; k < match.stages.size(); ++k) {
      int const index{match.stages[k]};
      if (index < 0) {
        perRun.insert(runId(loaded[k]->run), QJsonValue::Null);
        continue;
      }
      core::StageAnalysis const &stage = loaded[k]->analysis.run.stages[index];
      double const from{index > 0 ? loaded[k]->analysis.run.stages[index - 1].segment.referenceEnd : stage.segment.referenceStart};
      perRun.insert(runId(loaded[k]->run), QJsonObject{{"from", num(from)},
                                   {"to", num(stage.segment.referenceEnd)},
                                   {"settling_time_s", seconds(stage.kpis.settlingTimeS)},
                                   {"overshoot", num(stage.kpis.overshoot)},
                                   {"tail_mae", num(stage.kpis.tailMae)}});
    }
    steps.push_back(perRun);
  }
  result.insert("step_responses", steps);

  QJsonArray signalRows;
  double     anchor0{core::NaN};
  for (qsizetype k{0}; k < loaded.size(); ++k) {
    Loaded const &one = *loaded[k];
    double const  anchor{core::alignmentAnchor(one.analysis, one.times, one.series("temp"), one.series("temp_ref"), mode, n)};
    if (k == 0) {
      anchor0 = anchor;
      continue;
    }
    bool const                   aligned{std::isfinite(anchor) && std::isfinite(anchor0)};
    core::SignalComparison const signal = core::compareSignals(loaded[0]->times, loaded[0]->series(channel), aligned ? anchor0 : 0.0,
                                                               one.times, one.series(channel), aligned ? anchor : 0.0);
    signalRows.push_back(QJsonObject{{"run", runId(one.run)},
                                  {"minus", runId(loaded[0]->run)},
                                  {"channel", channel},
                                  {"aligned", aligned},
                                  {"overlap_s", seconds(signal.overlapS)},
                                  {"mean_diff", num(signal.meanDiff)},
                                  {"rms_diff", num(signal.rmsDiff)},
                                  {"max_abs_diff", num(signal.maxAbsDiff)},
                                  {"correlation", num(signal.correlation)}});
  }
  result.insert("alignment", QJsonObject{{"mode", args.value("align").toString(QStringLiteral("start"))}, {"index", n}});
  result.insert("signals", signalRows);

  // card: KPI table with DeepVac's verdict, configuration differences, signals
  QStringList columns{"KPI"};
  for (LoadedPtr const &one : std::as_const(loaded)) {
    columns << runId(one->run);
  }
  columns << QStringLiteral("Verdict");
  QVector<QStringList> rows;
  QVector<int>         wins(loaded.size(), 0);
  for (core::KpiComparisonRow const &row : core::compareKpis(analyses)) {
    QString const unit = row.unit;
    QStringList   cells{row.label};
    QStringList   verdicts;
    for (qsizetype k{0}; k < row.values.size(); ++k) {
      cells << (unit == QLatin1String("s") ? hms(row.values[k]) : row.key == QLatin1String("anomalies") ? fmt(row.values[k]) : fmt(row.values[k], unit));
      if ((k > 0) && (row.better != core::Better::NONE)) {
        QString const v = verdict(row.values[0], row.values[k], row.better == core::Better::LOWER, runId(loaded[0]->run), runId(loaded[k]->run), unit);
        verdicts << v;
        if (v.startsWith(runId(loaded[k]->run) + QLatin1Char(' '))) {
          ++wins[k];
        } else if (v.startsWith(runId(loaded[0]->run) + QLatin1Char(' '))) {
          ++wins[0];
        }
      }
    }
    cells << (row.better == core::Better::NONE ? QStringLiteral("—") : verdicts.join(QStringLiteral("; ")));
    rows.push_back(cells);
  }
  QStringList facts;
  for (LoadedPtr const &one : std::as_const(loaded)) {
    facts << QStringLiteral("Experiment %1: \"%2\", %3, lasted %4.").arg(runId(one->run), one->run.title, dateTime(one->run.start), hms(one->run.duration()));
  }
  for (core::ConfigComparisonRow const &row : core::compareConfigs(configs)) {
    if (row.differs) {
      QStringList values;
      for (qsizetype k{0}; k < row.values.size(); ++k) {
        values << QStringLiteral("%1 %2").arg(runId(loaded[k]->run), row.values[k]);
      }
      facts << QStringLiteral("Different %1: %2.").arg(row.label, values.join(QStringLiteral(", ")));
    }
  }
  // recorded configuration snapshots: differences as they were set, not inferred from the signals
  struct Snapshot {
    char const *label;
    QString (*get)(QJsonObject const &configuration);
  };
  static Snapshot const snapshots[]{
    {"program", [](QJsonObject const &c) { return c.value("job").toObject().contains("program") ? runrecord::programSummary(c) : QString(); }},
    {"PID table (Kp/Ki/Kd per band)", [](QJsonObject const &c) { return c.contains("plc_settings") ? runrecord::pidSummary(c) : QString(); }},
    {"calibration", [](QJsonObject const &c) { return c.contains("plc_settings") ? runrecord::calibrationSummary(c) : QString(); }},
  };
  for (Snapshot const &snapshot : snapshots) {
    QStringList values;
    bool        complete{true};
    for (LoadedPtr const &one : std::as_const(loaded)) {
      QString const value = snapshot.get(one->data->manifest.value("configuration").toObject());
      complete            = complete && !value.isEmpty();
      values << value;
    }
    if (complete && std::any_of(values.cbegin(), values.cend(), [&](QString const &v) { return v != values.first(); })) {
      QStringList labelled;
      for (qsizetype k{0}; k < values.size(); ++k) {
        labelled << QStringLiteral("%1 %2").arg(runId(loaded[k]->run), values[k]);
      }
      facts << QStringLiteral("Different %1 (recorded): %2.").arg(QString::fromLatin1(snapshot.label), labelled.join(QStringLiteral("; ")));
    }
  }
  if (loaded.size() == 2) {
    facts << QStringLiteral("%1 is better on %2 KPIs, %3 on %4; the rest are practically equal or not rated.")
               .arg(runId(loaded[1]->run))
               .arg(wins[1])
               .arg(runId(loaded[0]->run))
               .arg(wins[0]);
  }
  for (QJsonValue const &value : std::as_const(signalRows)) {
    QJsonObject const signal = value.toObject();
    facts << QStringLiteral("Signal %1 of %2 minus %3 (%4): mean difference %5, RMS difference %6, correlation %7.")
               .arg(channel, signal.value("run").toString(), signal.value("minus").toString(),
                    signal.value("aligned").toBool() ? QStringLiteral("aligned on %1 %2").arg(args.value("align").toString(QStringLiteral("start"))).arg(n)
                                                     : QStringLiteral("not aligned"),
                    fmt(signal.value("mean_diff").toDouble(core::NaN), DEG, 3), fmt(signal.value("rms_diff").toDouble(core::NaN), DEG, 3),
                    fmt(signal.value("correlation").toDouble(core::NaN), {}, 3));
  }
  result.insert("card", makeCard(QStringLiteral("Comparison of experiments %1").arg(references.join(QStringLiteral(", "))), facts, columns, rows));
  return result;
}

QJsonObject AnalysisTools::rankRuns(QJsonObject const &args) const {
  struct Metric {
    char const *key;
    char const *label;
    double (*get)(storage::StoredRun const &);
  };
  static Metric const metrics[]{
    {"mae", "MAE over the whole run (meaningful for ramped programs; lower is better)", [](storage::StoredRun const &r) { return r.mae; }},
    {"overshoot", "worst step overshoot (lower is better)", [](storage::StoredRun const &r) { return r.overshoot; }},
    {"settling", "settling of the last setpoint step (lower is better)", [](storage::StoredRun const &r) { return r.settleTimeS; }},
    {"tail_mae", "MAE over the last 5 min (lower is better)", [](storage::StoredRun const &r) { return r.tailMae; }},
  };
  QString const key = args.value("metric").toString(QStringLiteral("tail_mae")).trimmed().toLower();
  auto const    metric = std::find_if(std::begin(metrics), std::end(metrics), [&](Metric const &m) { return key.startsWith(QLatin1String(m.key)); });
  if (metric == std::end(metrics)) {
    return QJsonObject{{"error", "metric must be tail_mae, mae, overshoot or settling"}};
  }
  if (m_library->isScanning()) {
    return QJsonObject{{"error", "the experiment list is still being scanned, try again in a few seconds"}};
  }
  // only runs saved in the experiment database have their KPIs stored
  QHash<QString, QString> ids;
  for (ExperimentRun const &run : runs()) {
    if (!run.storeKey.isEmpty()) {
      ids.insert(run.storeKey, runId(run));
    }
  }
  QVector<storage::StoredRun> stored;
  for (storage::StoredRun const &run : storage::RunStore::list()) {
    if (ids.contains(run.key) && std::isfinite(metric->get(run))) {
      stored.push_back(run);
    }
  }
  std::stable_sort(stored.begin(), stored.end(), [&](storage::StoredRun const &a, storage::StoredRun const &b) { return metric->get(a) < metric->get(b); });
  int const limit{std::clamp(args.value("limit").toInt(10), 1, MAX_LIST)};

  QJsonArray           list;
  QVector<QStringList> rows;
  for (qsizetype i{0}; (i < stored.size()) && (i < limit); ++i) {
    storage::StoredRun const &run = stored[i];
    list.push_back(QJsonObject{{"rank", static_cast<int>(i + 1)}, {"run", ids.value(run.key)}, {"title", run.id}, {"value", num(metric->get(run))}});
    rows.push_back({QString::number(i + 1), ids.value(run.key), run.id, run.startTime.left(16), fmt(run.tailMae, DEG, 3),
                    fmt(run.overshoot, DEG, 3), hms(run.settleTimeS)});
  }
  QStringList facts{QStringLiteral("%1 experiments saved in the experiment database have this KPI; ranked by %2.").arg(stored.size()).arg(QString::fromLatin1(metric->label))};
  if (!stored.isEmpty()) {
    storage::StoredRun const &best  = stored.first();
    storage::StoredRun const &worst = stored.last();
    facts << QStringLiteral("Best: %1 \"%2\" (%3).").arg(ids.value(best.key), best.id, metric->key == QLatin1String("settling") ? hms(metric->get(best)) : fmt(metric->get(best), DEG))
          << QStringLiteral("Worst: %1 \"%2\" (%3).").arg(ids.value(worst.key), worst.id, metric->key == QLatin1String("settling") ? hms(metric->get(worst)) : fmt(metric->get(worst), DEG));
  }
  facts << QStringLiteral("Experiments only recorded in the data files (not saved in the database) are not ranked.");
  QJsonObject result{{"metric", QString::fromLatin1(metric->key)}, {"ranking", list}};
  result.insert("card", makeCard(QStringLiteral("Experiments ranked by %1").arg(QString::fromLatin1(metric->key)), facts,
                                 {"Rank", "Run", "Title", "Start", "Tail MAE", "Overshoot", "Settling"}, rows));
  return result;
}

QJsonObject AnalysisTools::runDetails(QJsonObject const &args) {
  QString         error;
  LoadedPtr const loaded = load(referenceOf(args.value("run")), error);
  if (!loaded) {
    return QJsonObject{{"error", error}};
  }
  core::RunManifest const manifest = core::RunManifest::fromJson(loaded->data->manifest);
  QJsonObject             result   = runHeader(*loaded);
  QString const           id       = runId(loaded->run);
  if (!manifest.isValid()) {
    result.insert("card", makeCard(QStringLiteral("Experiment %1 details").arg(id),
                                   {QStringLiteral("Experiment %1 %2.").arg(id, spanText(loaded->run.start, loaded->run.end)),
                                    QStringLiteral("Run %1 has no recorded details: it is not saved in the experiment database "
                                                   "(or was saved by an older version), so there is no configuration snapshot.").arg(id)}));
    return result;
  }
  auto orDash = [](QString const &value) { return value.isEmpty() ? QStringLiteral("not recorded") : value; };
  QJsonObject const &configuration = manifest.configuration;
  int const integrity{static_cast<int>(storage::RunStore::verify(loaded->run.storeKey))};
  QStringList facts{QStringLiteral("Experiment %1 %2.").arg(id, spanText(loaded->run.start, loaded->run.end)),
                    QStringLiteral("DUT: %1 (mass %2, density %3). Operator: %4. Purpose: %5. Tags: %6.")
                      .arg(orDash(manifest.metadata.dut), orDash(manifest.metadata.dutMassText()), orDash(manifest.metadata.dutDensityText()),
                           orDash(manifest.metadata.operatorName), orDash(manifest.metadata.purpose),
                           orDash(manifest.metadata.tags.join(QStringLiteral(", ")))),
                    QStringLiteral("Job: %1; configuration %2.").arg(runrecord::jobText(configuration), runrecord::capturedText(configuration)),
                    QStringLiteral("Program as sent (planned interval durations, not how long it ran): %1.").arg(runrecord::programSummary(configuration)),
                    QStringLiteral("Calibration: %1.").arg(runrecord::calibrationSummary(configuration)),
                    QStringLiteral("Recorded by %1 %2 at %3 s resolution; %4 events.")
                      .arg(manifest.appName, orDash(manifest.appVersion))
                      .arg(manifest.timeResolutionS)
                      .arg(manifest.events.size()),
                    QStringLiteral("Integrity: %1.").arg(runrecord::integrityText(integrity))};
  if (!manifest.metadata.notes.isEmpty()) {
    facts << QStringLiteral("Notes: %1").arg(manifest.metadata.notes);
  }
  result.insert("details", manifest.metadata.toJson());
  result.insert("card", makeCard(QStringLiteral("Experiment %1 details").arg(id), facts, runrecord::pidColumns(), runrecord::pidRows(configuration)));
  return result;
}

QJsonObject AnalysisTools::events(QJsonObject const &args) {
  QString         error;
  LoadedPtr const loaded = load(referenceOf(args.value("run")), error);
  if (!loaded) {
    return QJsonObject{{"error", error}};
  }
  core::RunManifest manifest = core::RunManifest::fromJson(loaded->data->manifest);
  bool const        recorded{manifest.isValid()};
  if (!recorded) {
    QHash<QString, QVector<double>> channels;
    for (qsizetype c{0}; (c < loaded->names.size()) && (c < loaded->data->states.size()); ++c) {
      channels.insert(loaded->names[c], loaded->data->states[c]);
    }
    manifest.events = core::detectEvents(loaded->data->times, channels);
  }
  QString const        filter = args.value("type").toString().trimmed().toLower();
  QVector<QStringList> rows;
  QJsonArray           list;
  QMap<QString, int>   counts;
  for (core::RunEvent const &event : std::as_const(manifest.events)) {
    if (!filter.isEmpty() && !event.type.contains(filter) && !runrecord::eventTypeName(event.type).toLower().contains(filter)) {
      continue;
    }
    ++counts[runrecord::eventTypeName(event.type)];
    if (rows.size() < 60) {
      double const at{event.time - loaded->data->times.value(0)};
      rows.push_back({at < 0.5 ? QStringLiteral("0 s") : hms(at), runrecord::eventTypeName(event.type),
                      event.source == QLatin1String("app") ? QStringLiteral("live") : QStringLiteral("signal"), event.message});
      list.push_back(QJsonObject{{"after", hms(at)}, {"type", event.type}, {"message", event.message}});
    }
  }
  QStringList summary;
  for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
    summary << QStringLiteral("%1 × %2").arg(it.value()).arg(it.key());
  }
  QStringList facts{QStringLiteral("Experiment %1: %2 events%3: %4.")
                      .arg(runId(loaded->run))
                      .arg(rows.size() < 60 ? QString::number(rows.size()) : QStringLiteral("60+"))
                      .arg(filter.isEmpty() ? QString() : QStringLiteral(" matching '%1'").arg(filter))
                      .arg(summary.isEmpty() ? QStringLiteral("none") : summary.join(QStringLiteral(", ")))};
  facts << (recorded ? QStringLiteral("Recorded with the experiment: live events of the application and events from the recorded signals.")
                     : QStringLiteral("This experiment has no recorded events; these were derived now from its signals."));
  QJsonObject result = runHeader(*loaded);
  result.insert("events", list);
  result.insert("card", makeCard(QStringLiteral("Events of experiment %1").arg(runId(loaded->run)), facts, {"After", "Event", "Source", "Details"}, rows));
  return result;
}

QJsonObject AnalysisTools::plotRun(QJsonObject const &args) {
  if (!m_viewer.show) {
    return QJsonObject{{"error", "plots are not available here"}};
  }
  QString         error;
  LoadedPtr const loaded = load(referenceOf(args.value("run")), error);
  if (!loaded) {
    return QJsonObject{{"error", error}};
  }
  double from{optionalNumber(args, "from_s")};
  double to{optionalNumber(args, "to_s")};
  int    shownStage{-1};
  if (args.contains("stage")) {
    shownStage = selectStage(loaded->analysis, args.value("stage").toString(), error);
    if (shownStage < 0) {
      return QJsonObject{{"error", error}};
    }
    from = loaded->analysis.run.stages[shownStage].segment.start;
    to   = loaded->analysis.run.stages[shownStage].segment.end;
  }
  QVector<ExperimentRun> compare;
  for (QString const &reference : stringsOf(args.value("compare_with"))) {
    std::optional<ExperimentRun> const other = resolve(reference, error);
    if (!other) {
      return QJsonObject{{"error", QStringLiteral("%1: %2").arg(reference, error)}};
    }
    compare.push_back(*other);
  }
  QStringList channels = stringsOf(args.value("channels"));
  if (channels.isEmpty() && !compare.isEmpty()) {
    channels = {"temp"}; // a comparison needs its line; otherwise the chart keeps the lines it shows
  }
  if (QString const failed = m_viewer.show(loaded->run, channels, from, to, compare); !failed.isEmpty()) {
    return QJsonObject{{"error", failed}};
  }
  QJsonObject result = runHeader(*loaded);
  result.insert("shown", QStringLiteral("opened on the Analysis page"));
  result.insert("channels", QJsonArray::fromStringList(channels));
  // what is on screen, so the explanation can stick to facts
  QString what = QStringLiteral("the whole experiment");
  if (shownStage >= 0) {
    result.insert("stage_shown", stageJson(loaded->analysis, shownStage, true));
    what = QStringLiteral("%1, %2").arg(stageLabel(loaded->analysis, shownStage), spanText(loaded->analysis.run.stages[shownStage].segment));
  } else if (std::isfinite(from) || std::isfinite(to)) {
    result.insert("range_shown", QJsonObject{{"from", hms(std::isfinite(from) ? from : 0.0)},
                                             {"to", hms(std::isfinite(to) ? to : loaded->run.duration())}});
    what = QStringLiteral("%1 to %2").arg(hms(std::isfinite(from) ? from : 0.0), hms(std::isfinite(to) ? to : loaded->run.duration()));
  }
  QStringList facts{QStringLiteral("Opened on the Analysis page: experiment %1, %2.").arg(runId(loaded->run), what),
                    QStringLiteral("Channels: %1.").arg(channels.join(QStringLiteral(", ")))};
  if (!compare.isEmpty()) {
    QStringList others;
    for (ExperimentRun const &other : std::as_const(compare)) {
      others << runId(other);
    }
    facts << QStringLiteral("Overlaid experiments: %1.").arg(others.join(QStringLiteral(", ")));
  }
  result.insert("card", makeCard(QStringLiteral("Chart"), facts));
  return result;
}

QJsonObject AnalysisTools::generateReport(QJsonObject const &args) {
  if (!m_viewer.report) {
    return QJsonObject{{"error", "reports are not available here"}};
  }
  QString                            error;
  std::optional<ExperimentRun> const run = resolve(referenceOf(args.value("run")), error);
  if (!run) {
    return QJsonObject{{"error", error}};
  }
  QVector<ExperimentRun> compare;
  for (QString const &reference : stringsOf(args.value("compare_with"))) {
    std::optional<ExperimentRun> const other = resolve(reference, error);
    if (!other) {
      return QJsonObject{{"error", QStringLiteral("%1: %2").arg(reference, error)}};
    }
    compare.push_back(*other);
  }
  bool const pdf{args.value("format").toString().compare(QLatin1String("pdf"), Qt::CaseInsensitive) == 0};
  QString    path;
  if (QString const failed = m_viewer.report(*run, compare, pdf, path); !failed.isEmpty()) {
    return QJsonObject{{"error", failed}};
  }
  QJsonObject result{{"run", runId(*run)}, {"title", run->title}, {"report", path}, {"format", pdf ? "pdf" : "html"}};
  result.insert("card", makeCard(QStringLiteral("Report"), {QStringLiteral("%1 report of experiment %2 written to %3.").arg(pdf ? "PDF" : "HTML", runId(*run), path)}));
  return result;
}

}
