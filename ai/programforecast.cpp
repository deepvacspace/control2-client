#include "programforecast.h"

#include <QCoreApplication>
#include <QDir>
#include <algorithm>
#include <cmath>

#include "advisorcem.h"

namespace deepvaccontrol::ai {

namespace {
// the chamber's temperature has not really moved: a plan made for it still holds
constexpr double FORECAST_SAME_TEMP_C{2.0};
// the shortest stretch a recommendation is searched over (a short interval still has to settle)
constexpr double MIN_PLAN_HORIZON_S{120.0};
constexpr int    PLAN_STEP_WEIGHT{1000}; // progress units per planned interval

using GainsFn = std::function<std::array<double, 3>(int interval, double setpoint)>;

GainsFn gainsOf(ForecastInput const &input) {
  return [&input](int interval, double setpoint) -> std::array<double, 3> {
    IntervalPlan const planned = input.plan.value(interval);
    if (planned.valid) {
      return {planned.kp, planned.ki, planned.kd};
    }
    return input.table.gainsFor(setpoint);
  };
}

// How long the twin needs to look at an interval (it is validated for an hour at most).
double horizonOf(ForecastInterval const &interval) {
  return interval.await ? MAX_HORIZON_S : std::min(interval.durationS, MAX_HORIZON_S);
}

// One interval from startTemp. False when cancelled.
bool forecastInterval(GruOnnxModel *model, ForecastInterval const &interval, int index, double startTemp,
                      GainsFn const &gains, std::atomic<bool> const *cancel, IntervalForecast &out) {
  out           = IntervalForecast{};
  out.startTemp = startTemp;
  out.endTemp   = startTemp;
  out.timeS     = std::max(interval.durationS, 0.0);
  if (!interval.controlled) {
    return true; // the temperature is not controlled: only the programmed time is known
  }
  out.endTemp = interval.targetTemp;
  double const horizon{horizonOf(interval)};
  if ((horizon <= 0.0) || !model->envelope().checkScenario(startTemp, interval.targetTemp, horizon).isEmpty()) {
    return true; // nothing to simulate, or outside what the twin is validated for
  }

  SimParams params;
  params.startTemp  = startTemp;
  params.targetTemp = interval.targetTemp;
  params.durationS  = horizon;
  params.dtS        = DEFAULT_DT_S;
  params.rampS      = interval.ramp ? interval.durationS : 0.0;
  auto const first  = gains(index, setpointAt(params, 0.0));
  params.kp         = first[0];
  params.ki         = first[1];
  params.kd         = first[2];

  ChamberTwinSim sim(model);
  if (!sim.begin(params)) {
    return true;
  }
  TrajectoryPoint point;
  double          lastTemp{startTemp};
  while (!sim.isFinished()) {
    if (cancel && cancel->load()) {
      return false;
    }
    // the chamber picks the gains of the setpoint's band
    auto const g = gains(index, setpointAt(params, sim.elapsedS()));
    sim.setPid(g[0], g[1], g[2]);
    if (!sim.stepOnce(point)) {
      return true; // the twin failed: the programmed time only
    }
    lastTemp = point.temp;
    if (interval.await && (std::abs(point.temp - interval.targetTemp) <= AWAIT_BAND_C)) {
      out.reachS = point.elapsedS;
      break;
    }
  }
  out.simulated = true;
  if (interval.await) {
    out.reached = std::isfinite(out.reachS);
    // not reached within the hour: at least that long (the chamber keeps waiting)
    out.timeS   = (out.reached ? out.reachS : horizon) + std::max(interval.durationS, 0.0);
    out.endTemp = interval.targetTemp; // then held there
  } else {
    // a ramp longer than the hour simulated goes on to its target
    out.endTemp = (interval.ramp && (interval.durationS > horizon)) ? interval.targetTemp : lastTemp;
  }
  return true;
}
}

bool ForecastInput::hasHold() const {
  return std::any_of(intervals.cbegin(), intervals.cend(), [](ForecastInterval const &i) { return i.controlled && i.await; });
}

bool ForecastInput::sameAs(ForecastInput const &other) const {
  return error.isEmpty() && other.error.isEmpty() && (intervals == other.intervals) && (cycle == other.cycle)
         && (table == other.table) && (plan.size() == other.plan.size())
         && (std::abs(startTemp - other.startTemp) <= FORECAST_SAME_TEMP_C);
}

PidTable PidTable::fromVariant(QVariantMap const &pid) {
  PidTable table;
  table.min = pid.value("min").toDouble();
  table.max = pid.value("max").toDouble();
  for (QVariant const &v : pid.value("p").toList()) {
    table.p << v.toDouble();
  }
  for (QVariant const &v : pid.value("i").toList()) {
    table.i << v.toDouble();
  }
  for (QVariant const &v : pid.value("d").toList()) {
    table.d << v.toDouble();
  }
  return table;
}

bool PidTable::isValid() const {
  return !p.isEmpty() && (p.size() == i.size()) && (p.size() == d.size()) && (max > min);
}

std::array<double, 3> PidTable::gainsFor(double setpoint) const {
  if (!isValid()) {
    return {DEFAULT_KP, DEFAULT_KI, DEFAULT_KD};
  }
  // the chamber's band: floor((setpoint - min) / ((max - min) / points))
  int const    points = static_cast<int>(p.size());
  double const width  = (max - min) / points;
  int const    band   = std::clamp(static_cast<int>(std::floor((setpoint - min) / width)), 0, points - 1);
  return {p[band], i[band], d[band]};
}

ProgramForecast forecastProgram(GruOnnxModel *model, ForecastInput const &input, std::atomic<bool> const *cancel) {
  ProgramForecast result;
  int const       count = static_cast<int>(input.intervals.size());
  if (!input.error.isEmpty()) {
    result.error = input.error;
    return result;
  }
  if (!model || !model->isLoaded()) {
    result.error = model ? model->statusMessage() : QObject::tr("The digital twin is not available.");
    return result;
  }
  if (!std::isfinite(input.startTemp)) {
    result.error = QObject::tr("The current chamber temperature is not available.");
    return result;
  }
  if (count == 0) {
    result.error = QObject::tr("The program has no intervals.");
    return result;
  }

  GainsFn const gains = gainsOf(input);
  result.startTemp    = input.startTemp;
  result.intervals.resize(count);
  result.endless = input.cycle.repeats(count) && input.cycle.endless();
  double temp{input.startTemp};
  // one interval; its first run is the one shown
  auto run = [&](int index, bool first) {
    IntervalForecast f;
    if (!forecastInterval(model, input.intervals[index], index, temp, gains, cancel, f)) {
      return false;
    }
    if (first) {
      result.intervals[index] = f;
    }
    ForecastInterval const &interval = input.intervals[index];
    result.uncertain |= !f.reached || (interval.controlled && interval.await && !f.simulated);
    result.totalS += f.timeS;
    if (std::isfinite(f.endTemp)) {
      temp = f.endTemp;
    }
    return true;
  };
  for (int index{0}; index < count; ++index) {
    if (!run(index, true)) {
      result.cancelled = true;
      return result;
    }
    // the cycle's second pass starts where the first ended; every later one where the second did
    if (input.cycle.repeats(count) && !input.cycle.endless() && (index == input.cycle.end)) {
      double const before{result.totalS};
      for (int again{input.cycle.start}; again <= input.cycle.end; ++again) {
        if (!run(again, false)) {
          result.cancelled = true;
          return result;
        }
      }
      result.totalS += (input.cycle.count - 2) * (result.totalS - before);
    }
  }
  result.ok = true;
  return result;
}

ProgramPlan planProgram(GruOnnxModel *model, ForecastInput const &input, std::function<void(int, int)> const &progress,
                        std::atomic<bool> const *cancel, int threads) {
  ProgramPlan plan;
  plan.input = input;
  int const   count = static_cast<int>(input.intervals.size());
  if (!input.error.isEmpty()) {
    plan.error = input.error;
    return plan;
  }
  if (!model || !model->isLoaded()) {
    plan.error = model ? model->statusMessage() : QObject::tr("The digital twin is not available.");
    return plan;
  }
  if (!std::isfinite(input.startTemp)) {
    plan.error = QObject::tr("The current chamber temperature is not available.");
    return plan;
  }
  int const planned = static_cast<int>(
    std::count_if(input.intervals.cbegin(), input.intervals.cend(), [](ForecastInterval const &i) { return i.controlled; }));
  if (planned == 0) {
    plan.error = QObject::tr("No temperature-controlled interval was found in this experiment.");
    return plan;
  }

  plan.intervals.resize(count);
  GainsFn const userGains = [&input](int, double setpoint) { return input.table.gainsFor(setpoint); };
  int const     total{planned * PLAN_STEP_WEIGHT};
  int           done{0};
  double        temp{input.startTemp};
  for (int index{0}; index < count; ++index) {
    ForecastInterval const &interval = input.intervals[index];
    if (!interval.controlled) {
      continue;
    }
    // the search looks at the interval as the chamber will run it: reaching the target, then the time held
    double horizon{interval.durationS};
    if (interval.await) {
      IntervalForecast reach;
      if (!forecastInterval(model, interval, index, temp, userGains, cancel, reach)) {
        plan.cancelled = true;
        return plan;
      }
      horizon = (reach.reached && std::isfinite(reach.reachS) ? reach.reachS : MAX_HORIZON_S) + interval.durationS;
    }
    horizon = std::clamp(horizon, MIN_PLAN_HORIZON_S, MAX_HORIZON_S);
    if (!model->envelope().checkScenario(temp, interval.targetTemp, horizon).isEmpty()) {
      plan.kept << index; // outside the twin's range: the user's own settings
      temp = interval.targetTemp;
      done += PLAN_STEP_WEIGHT;
      if (progress) {
        progress(done, total);
      }
      continue;
    }

    SimParams params;
    params.startTemp  = temp;
    params.targetTemp = interval.targetTemp;
    params.durationS  = horizon;
    params.dtS        = DEFAULT_DT_S;
    params.rampS      = interval.ramp ? interval.durationS : 0.0;
    CemOptimizer cem(model, cancel);
    cem.setThreads(threads);
    CemResult const best = cem.run(params, [&](int steps, int stepsTotal) {
      if (progress) {
        progress(done + static_cast<int>(static_cast<qint64>(PLAN_STEP_WEIGHT) * steps / std::max(stepsTotal, 1)), total);
      }
    });
    if (cancel && cancel->load()) {
      plan.cancelled = true;
      return plan;
    }
    if (best.valid && model->envelope().containsPid(best.kp, best.ki, best.kd)) {
      plan.intervals[index] = IntervalPlan{true, best.kp, best.ki, best.kd};
      bool const longRamp{interval.ramp && (interval.durationS > horizon)};
      temp = (longRamp || best.trajectory.isEmpty()) ? interval.targetTemp : best.trajectory.last().temp;
    } else {
      plan.kept << index;
      temp = interval.targetTemp;
    }
    done += PLAN_STEP_WEIGHT;
    if (progress) {
      progress(done, total);
    }
  }

  // how long the program takes with these settings
  ForecastInput withPlan = input;
  withPlan.plan          = plan.intervals;
  plan.forecast          = forecastProgram(model, withPlan, cancel);
  if (plan.forecast.cancelled) {
    plan.cancelled = true;
    return plan;
  }
  plan.ok = std::any_of(plan.intervals.cbegin(), plan.intervals.cend(), [](IntervalPlan const &p) { return p.valid; });
  if (!plan.ok) {
    plan.error = QObject::tr("No interval of this experiment is inside the range the digital twin is validated for.");
  }
  return plan;
}

ProgramForecaster::ProgramForecaster(QObject *parent) : QObject{parent} {}

ProgramForecaster::~ProgramForecaster() {
  m_next.reset();
  if (m_thread) {
    m_cancel = true;
    m_thread->wait();
    delete m_thread;
  }
}

void ProgramForecaster::request(ForecastInput const &input, bool withPlan) {
  m_next         = input;
  m_nextWithPlan = withPlan;
  if (m_thread) {
    m_cancel = true; // the running one stops; this one follows
    return;
  }
  startNext();
}

bool ProgramForecaster::isBusy() const {
  return m_thread || m_next;
}

void ProgramForecaster::startNext() {
  if (!m_next) {
    return;
  }
  ForecastInput const input = *m_next;
  bool const          withPlan{m_nextWithPlan};
  m_next.reset();
  m_cancel = false;
  m_thread = QThread::create([this, input, withPlan] {
    // the twin is loaded on first use, and only this thread uses it
    if (!m_modelTried) {
      m_modelTried = true;
      m_model      = std::make_unique<GruOnnxModel>();
      QDir const dir(QDir(QCoreApplication::applicationDirPath()).filePath("data/model"));
      m_model->load(dir.filePath("twin_model.onnx"), dir.filePath("twin_model.json"));
    }
    m_resultPlan.reset();
    if (!withPlan) {
      m_result = forecastProgram(m_model.get(), input, &m_cancel);
      return;
    }
    // half the cores: the search runs while the user works
    int const   threads = std::max(1, QThread::idealThreadCount() / 2);
    ProgramPlan plan    = planProgram(m_model.get(), input, {}, &m_cancel, threads);
    if (plan.cancelled) {
      m_result           = ProgramForecast{};
      m_result.cancelled = true;
      return;
    }
    // no recommendation possible: the user's own settings are what the program would run with
    m_result = plan.ok ? plan.forecast : forecastProgram(m_model.get(), input, &m_cancel);
    if (plan.ok) {
      m_resultPlan = std::move(plan);
    }
  });
  connect(m_thread, &QThread::finished, this, [this] {
    m_thread->deleteLater();
    m_thread = nullptr;
    if (!m_result.cancelled && !m_next) {
      m_plan = m_resultPlan;
      emit finished(m_result);
    }
    startNext();
  });
  m_thread->start(QThread::LowPriority);
}

ProgramPlanWorker::ProgramPlanWorker(GruOnnxModel *model, ForecastInput input, QObject *parent)
    : QThread{parent}, m_model{model}, m_input{std::move(input)} {}

void ProgramPlanWorker::run() {
  m_result = planProgram(m_model, m_input, [this](int done, int total) { emit progress(done, total); }, &m_cancel);
}

}
