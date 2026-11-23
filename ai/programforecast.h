#ifndef PROGRAMFORECAST_H
#define PROGRAMFORECAST_H

#include <QObject>
#include <QString>
#include <QThread>
#include <QVariantMap>
#include <QVector>
#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>

#include "chambertwinsim.h"
#include "gruonnxmodel.h"

namespace deepvaccontrol::ai {

// "Reached" for an interval that waits for its target (the chamber's await band).
constexpr double AWAIT_BAND_C{1.0};

// One program interval as the twin sees it.
struct ForecastInterval {
  bool   controlled{false}; // the interval sets the temperature (its flag bit)
  double targetTemp{0.0};
  double durationS{0.0};
  bool   ramp{false};  // the setpoint moves linearly over the interval
  bool   await{false}; // full speed, the interval's time counts once the target is reached
  bool operator==(ForecastInterval const &) const = default;
};

// The program's cycle: intervals start..end run `count` times (65535: until stopped).
struct ForecastCycle {
  int start{0};
  int end{-1};
  int count{0};
  [[nodiscard]] bool endless() const { return count == 65535; }
  bool operator==(ForecastCycle const &) const = default;
  [[nodiscard]] bool repeats(int intervals) const { return (count > 1) && (start >= 0) && (start <= end) && (end < intervals); }
};

// The chamber's Temperature PID table (plc_settings.pids[0]): the gains of the setpoint's band.
struct PidTable {
  double          min{0.0};
  double          max{0.0};
  QVector<double> p, i, d;

  bool operator==(PidTable const &) const = default;
  static PidTable           fromVariant(QVariantMap const &pid);
  [[nodiscard]] bool        isValid() const;
  [[nodiscard]] std::array<double, 3> gainsFor(double setpoint) const;
};

// Settings the AI Advisor recommends for one interval (invalid: the interval keeps the user's own).
struct IntervalPlan {
  bool   valid{false};
  double kp{0.0};
  double ki{0.0};
  double kd{0.0};
};

struct ForecastInput {
  QVector<ForecastInterval> intervals;
  ForecastCycle             cycle;
  double                    startTemp{qQNaN()}; // the chamber now
  PidTable                  table;
  QVector<IntervalPlan>     plan; // empty: the table's gains
  QString                   error; // why there is nothing to forecast

  // an interval goes to its target at full speed and holds it: its time depends on the settings
  [[nodiscard]] bool hasHold() const;
  // the same program, settings and (within a couple of degrees) chamber temperature
  [[nodiscard]] bool sameAs(ForecastInput const &other) const;
};

struct IntervalForecast {
  double startTemp{qQNaN()};
  double endTemp{qQNaN()};
  double timeS{0.0};        // spent in this interval (await: reaching the target, then the duration)
  double reachS{qQNaN()};   // await: until within AWAIT_BAND_C of the target
  bool   simulated{false};  // by the twin (otherwise only the programmed duration is known)
  bool   reached{true};     // await: within the hour the twin is validated for
};

struct ProgramForecast {
  bool                      ok{false};
  bool                      cancelled{false};
  QString                   error;
  double                    startTemp{qQNaN()};
  QVector<IntervalForecast> intervals; // each interval's first run
  double                    totalS{0.0}; // the whole program (until stopped: one pass)
  bool                      endless{false};
  bool                      uncertain{false}; // a target not reached within the hour, or intervals the twin can't simulate
};

// The program run through the digital twin from the chamber's temperature, each interval from where the one before
// ended. A cycle's first two passes are simulated; the later ones last as long as the second.
ProgramForecast forecastProgram(GruOnnxModel *model, ForecastInput const &input, std::atomic<bool> const *cancel = nullptr);

struct ProgramPlan {
  ForecastInput         input; // what it was calculated for
  bool                  ok{false};
  bool                  cancelled{false};
  QString               error;
  QVector<IntervalPlan> intervals; // per program interval
  QVector<int>          kept;      // temperature intervals that keep the user's settings (outside the twin's range)
  ProgramForecast       forecast;  // the program with these settings
};

// One CEM search per temperature interval, in program order, each from the temperature the one before is predicted
// to end at. progress(done, total) over all of them; threads for the search (0: every core).
ProgramPlan planProgram(GruOnnxModel *model, ForecastInput const &input, std::function<void(int, int)> const &progress,
                        std::atomic<bool> const *cancel, int threads = 0);

// The program's forecast in the background, with its own copy of the twin. A new request replaces the one running.
// withPlan: with the AI Advisor's settings for each interval (its plan is kept for the start), otherwise the user's.
class ProgramForecaster : public QObject {
  Q_OBJECT
public:
  explicit ProgramForecaster(QObject *parent = nullptr);
  ~ProgramForecaster() override;

  void request(ForecastInput const &input, bool withPlan);
  [[nodiscard]] bool isBusy() const;
  // the plan behind the last forecast (null: it was made with the user's settings)
  [[nodiscard]] ProgramPlan const *plan() const { return m_plan ? &*m_plan : nullptr; }

signals:
  void finished(deepvaccontrol::ai::ProgramForecast const &forecast);

private:
  void startNext();

  std::unique_ptr<GruOnnxModel> m_model;
  bool                          m_modelTried{false};
  QThread                      *m_thread{nullptr};
  std::atomic<bool>             m_cancel{false};
  std::optional<ForecastInput>  m_next;
  bool                          m_nextWithPlan{false};
  ProgramForecast               m_result;
  std::optional<ProgramPlan>    m_resultPlan;  // made on the thread
  std::optional<ProgramPlan>    m_plan;        // of the last forecast shown
};

// planProgram() on a thread, while a progress dialog is shown.
class ProgramPlanWorker : public QThread {
  Q_OBJECT
public:
  ProgramPlanWorker(GruOnnxModel *model, ForecastInput input, QObject *parent = nullptr);
  [[nodiscard]] ProgramPlan const &result() const { return m_result; }
  void cancel() { m_cancel = true; }

signals:
  void progress(int done, int total);

protected:
  void run() override;

private:
  GruOnnxModel     *m_model;
  ForecastInput     m_input;
  ProgramPlan       m_result;
  std::atomic<bool> m_cancel{false};
};

}

#endif // PROGRAMFORECAST_H
