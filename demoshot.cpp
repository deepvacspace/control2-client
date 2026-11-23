#include "demoshot.h"

#include <QDateTime>
#include <QVector>
#include <cmath>
#include <random>
#include <utility>

#include "datamanager.h"
#include "systemvaluelist.h"

namespace deepvaccontrol::demo {
namespace {
using Keys = QVector<std::pair<double, double>>; // minute from the start, value

// the value at minute m, straight between the keys
double at(Keys const &keys, double m) {
  if (m <= keys.front().first) {
    return keys.front().second;
  }
  for (int i{1}; i < keys.size(); ++i) {
    if (m <= keys[i].first) {
      double const f{(m - keys[i - 1].first) / (keys[i].first - keys[i - 1].first)};
      return keys[i - 1].second + f * (keys[i].second - keys[i - 1].second);
    }
  }
  return keys.back().second;
}

// a moving average over ±half samples (the keys' corners rounded)
QVector<double> smooth(QVector<double> const &values, int half) {
  QVector<double> out(values.size());
  for (int i{0}; i < values.size(); ++i) {
    double sum{0.0};
    int    count{0};
    for (int j{qMax(0, i - half)}; j <= qMin(static_cast<int>(values.size()) - 1, i + half); ++j) {
      sum += values[j];
      ++count;
    }
    out[i] = sum / count;
  }
  return out;
}

// temperature, °C: up to 52 °C and settling at 50 °C under the setpoint, then cooling slowly once idle
Keys const temperature{{0, 36.3},     {1.7, 36.3},   {2.6, 38.0},   {5.0, 37.2},   {6.0, 37.0},   {7.9, 42.0},
                       {9.6, 47.0},   {10.8, 50.5},  {12.7, 52.0},  {14.1, 52.0},  {16.0, 50.5},  {18.4, 49.4},
                       {20.3, 49.6},  {22.2, 48.8},  {25.1, 48.0},  {29.4, 47.6},  {31.0, 48.5},  {33.5, 50.5},
                       {35.9, 51.3},  {38.3, 50.5},  {40.6, 50.0},  {44.2, 49.6},  {47.8, 49.4},  {50.2, 50.0},
                       {55.0, 50.0},  {58.6, 50.4},  {63.3, 50.6},  {68.1, 50.6},  {75.3, 50.3},  {89.6, 49.6},
                       {99.2, 48.6},  {113.5, 47.4}, {137.5, 45.9}, {161.4, 44.0}, {185.3, 42.8}, {210.8, 41.4},
                       {240.0, 39.9}};
// pressure, log10 Torr: vented to atmosphere, pumped down to the 10⁻⁵ Torr range, then leaking back up once idle
Keys const pressure{{0, -1.80},     {1.5, -1.70},   {2.2, -1.55},   {2.6, 2.00},    {3.0, 2.86},    {28.9, 2.86},
                    {30.0, 2.04},   {31.5, 1.61},   {33.5, 1.11},   {35.0, 0.69},   {37.1, 0.26},   {40.6, -0.17},
                    {44.2, -0.53},  {49.0, -0.81},  {51.4, -0.99},  {51.7, -1.24},  {52.1, -2.67},  {52.8, -4.10},
                    {54.3, -4.39},  {60.9, -4.47},  {68.1, -4.49},  {75.3, -4.41},  {82.5, -4.24},  {85.3, -4.07},
                    {86.1, -3.53},  {87.2, -2.53},  {88.4, -1.96},  {90.8, -1.60},  {94.4, -1.31},  {101.6, -1.03},
                    {113.5, -0.77}, {137.5, -0.57}, {161.4, -0.46}, {185.3, -0.36}, {210.8, -0.30}, {240.0, -0.215}};
// the setpoint job: 50 °C and 1×10⁻⁶ Torr, twice
QVector<std::pair<double, double>> const setpointRuns{{2.4, 18.6}, {18.75, 22.5}, {28.0, 68.5}};
constexpr double duration{240.0}; // minutes at least, ending now
constexpr double step{5.0};       // seconds between samples
}

void fillExampleRun(SystemValueList *states, DataManager *dataManager) {
  QStringList const names = states->getNames();
  // from 18:30 today when that is 4 h ago or more (the clock's times of the example), otherwise the last 4 h
  double const end{static_cast<double>(QDateTime::currentSecsSinceEpoch())};
  double const at1830{static_cast<double>(QDateTime(QDate::currentDate(), QTime(18, 30)).toSecsSinceEpoch())};
  double const start{(end - at1830 >= duration * 60.0) ? at1830 : end - duration * 60.0};
  double const minutes{(end - start) / 60.0};
  int const    count{static_cast<int>(minutes * 60.0 / step) + 1};
  // the last values (39.9 °C, 6.1×10⁻¹ Torr) now
  Keys temperature = demo::temperature;
  Keys pressure    = demo::pressure;
  temperature.back().first = minutes;
  pressure.back().first    = minutes;

  std::mt19937                     random{7};
  std::normal_distribution<double> noise{0.0, 1.0};
  QVector<double>                  temp(count);
  QVector<double>                  logPres(count);
  for (int i{0}; i < count; ++i) {
    double const m{i * step / 60.0};
    temp[i]    = at(temperature, m);
    logPres[i] = at(pressure, m);
  }
  temp    = smooth(temp, 10);
  logPres = smooth(logPres, 2);

  auto index = [&names](char const *name) { return static_cast<int>(names.indexOf(QLatin1String(name))); };
  int const iTemp{index("temp")}, iTempRaw{index("temp_raw")}, iTempRef{index("temp_ref")}, iTempU{index("temp_u")};
  int const iPres{index("pres")}, iPresRaw{index("pres_raw")}, iPresRef{index("pres_ref")}, iPresU{index("pres_u")};
  int const iStatus{index("status_condition")}, iWorkType{index("work_type")}, iTotal{index("t_total")};

  QVector<double> point;
  for (int i{0}; i < count; ++i) {
    double const m{i * step / 60.0};
    bool         running{false};
    double       runStart{0.0};
    for (auto const &[from, to] : setpointRuns) {
      if ((from <= m) && (m < to)) {
        running  = true;
        runStart = from;
      }
    }
    bool const   last{i == count - 1}; // the chamber's values now, as given (39.9 °C, 6.1×10⁻¹ Torr)
    double const t{last ? 39.9 : temp[i] + 0.04 * noise(random)};
    double const p{last ? 0.61 : std::pow(10.0, logPres[i] + 0.004 * noise(random))};
    point = QVector<double>(names.size(), qQNaN());
    auto set = [&point](int at, double value) {
      if (at >= 0) {
        point[at] = value;
      }
    };
    set(iTemp, t);
    set(iTempRaw, t + 0.3);
    set(iTempRef, running ? 50.0 : qQNaN());
    set(iTempU, running ? qBound(0.0, 30.0 + 8.0 * (50.0 - t), 100.0) : 0.0);
    set(iPres, p);
    set(iPresRaw, p);
    set(iPresRef, running ? 1e-6 : qQNaN());
    set(iPresU, running ? 100.0 : 0.0);
    set(iStatus, running ? 2.0 : 1.0); // WORK, IDLE
    set(iWorkType, running ? 1.0 : 0.0); // a setpoint
    set(iTotal, running ? (m - runStart) * 60.0 : 0.0);
    dataManager->addPoint(start + i * step, point);
  }
  // the chamber's values now: the last sample
  states->setFloatValues(QVector<float>(point.cbegin(), point.cend()));
}
}
