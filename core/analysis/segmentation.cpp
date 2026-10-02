#include "segmentation.h"

#include <cmath>

namespace deepvaccontrol::core {

namespace {
constexpr double RAMP_WINDOW_S{120.0}; // reference drift is measured over this centred window

int toInt(QVector<double> const &values, int i) {
  return ((i < values.size()) && std::isfinite(values[i])) ? static_cast<int>(std::lround(values[i])) : -1;
}

// NaN gaps of the reference take the last known value (the first known one before it)
QVector<double> filledReference(QVector<double> const &reference, qsizetype n) {
  QVector<double> filled(n, NaN);
  double          last{NaN};
  for (qsizetype i{0}; i < n; ++i) {
    if ((i < reference.size()) && std::isfinite(reference[i])) {
      last = reference[i];
    }
    filled[i] = last;
  }
  double first{NaN};
  for (qsizetype i{n - 1}; i >= 0; --i) {
    if (std::isfinite(filled[i])) {
      first = filled[i];
    } else {
      filled[i] = first;
    }
  }
  return filled;
}
} // namespace

QString Segment::kindName(Kind kind) {
  switch (kind) {
    case Kind::RAMP:
      return QStringLiteral("ramp");
    case Kind::SETPOINT:
      return QStringLiteral("setpoint");
    case Kind::HOLD:
      return QStringLiteral("hold");
    default:
      return QStringLiteral("unknown");
  }
}

QVector<Segment> segmentExperiment(QVector<double> const &times, QVector<double> const &value, QVector<double> const &reference,
                                   QVector<double> const &interval, QVector<double> const &cycle,
                                   SegmentationOptions const &options) {
  qsizetype const n{times.size()};
  if (n == 0) {
    return {};
  }
  QVector<double> const ref = filledReference(reference, n);
  bool const            haveReference{std::isfinite(ref[0])};

  // reference drift without its steps, to tell ramps from steps
  QVector<bool>   jump(n, false);
  QVector<double> drift(n, 0.0);
  for (qsizetype i{1}; i < n; ++i) {
    double const delta{haveReference ? ref[i] - ref[i - 1] : 0.0};
    jump[i]  = std::fabs(delta) >= options.referenceTolerance;
    drift[i] = drift[i - 1] + (jump[i] ? 0.0 : delta);
  }
  QVector<bool> ramping(n, false);
  if (haveReference) {
    qsizetype lo{0};
    qsizetype hi{0};
    for (qsizetype i{0}; i < n; ++i) {
      while (times[lo] < times[i] - RAMP_WINDOW_S / 2.0) {
        ++lo;
      }
      while ((hi + 1 < n) && (times[hi + 1] <= times[i] + RAMP_WINDOW_S / 2.0)) {
        ++hi;
      }
      ramping[i] = std::fabs(drift[hi] - drift[lo]) > options.referenceTolerance;
    }
    // the centred window flags half a window before / after a ramp: keep only where the reference moves
    for (qsizetype a{0}; a < n;) {
      if (!ramping[a]) {
        ++a;
        continue;
      }
      qsizetype b{a};
      while ((b + 1 < n) && ramping[b + 1]) {
        ++b;
      }
      qsizetype first{-1};
      qsizetype last{-1};
      for (qsizetype i{std::max<qsizetype>(a, 1)}; i <= b; ++i) {
        if (drift[i] != drift[i - 1]) {
          first = (first < 0) ? i - 1 : first;
          last  = i;
        }
      }
      for (qsizetype i{a}; i <= b; ++i) {
        ramping[i] = (first >= 0) && (i >= first) && (i <= last);
      }
      a = b + 1;
    }
  }

  // raw stages: a new one at every program position change, step, or ramp start / end
  QVector<Segment> stages;
  auto             open = [&](qsizetype i, Segment::Kind kind) {
    Segment segment;
    segment.kind     = kind;
    segment.first    = static_cast<int>(i);
    segment.last     = static_cast<int>(i);
    segment.interval = toInt(interval, static_cast<int>(i));
    segment.cycle    = toInt(cycle, static_cast<int>(i));
    stages.push_back(segment);
  };
  auto kindAt = [&](qsizetype i) {
    return !haveReference ? Segment::Kind::UNKNOWN : (ramping[i] ? Segment::Kind::RAMP : Segment::Kind::HOLD);
  };

  for (qsizetype i{0}; i < n; ++i) {
    Segment::Kind const kind = kindAt(i);
    bool                startNew{stages.isEmpty()};
    if (!startNew) {
      Segment const &current = stages.last();
      int const      programInterval{toInt(interval, static_cast<int>(i))};
      int const      programCycle{toInt(cycle, static_cast<int>(i))};
      startNew = (kind != current.kind) || ((programInterval >= 0) && (programInterval != current.interval))
                 || ((programCycle >= 0) && (programCycle != current.cycle))
                 || (jump[i] && (kind == Segment::Kind::HOLD));
    }
    if (startNew) {
      open(i, kind);
    } else {
      stages.last().last = static_cast<int>(i);
    }
  }

  // constant reference: SETPOINT while the value approaches, HOLD once it got there
  QVector<Segment> split;
  for (Segment const &stage : std::as_const(stages)) {
    if (stage.kind != Segment::Kind::HOLD) {
      split.push_back(stage);
      continue;
    }
    int reachedAt{-1};
    for (int i{stage.first}; i <= stage.last; ++i) {
      if ((i < value.size()) && std::isfinite(value[i]) && (std::fabs(ref[i] - value[i]) <= options.settleBand)) {
        reachedAt = i;
        break;
      }
    }
    if (reachedAt == stage.first) {
      split.push_back(stage);
    } else if (reachedAt < 0) {
      Segment approach = stage;
      approach.kind    = (value.isEmpty() ? Segment::Kind::HOLD : Segment::Kind::SETPOINT);
      split.push_back(approach);
    } else {
      Segment approach = stage;
      approach.kind    = Segment::Kind::SETPOINT;
      approach.last    = reachedAt - 1;
      Segment hold     = stage;
      hold.first       = reachedAt;
      split.push_back(approach);
      split.push_back(hold);
    }
  }

  // merge stages too short to mean anything into their neighbour (same program position)
  QVector<Segment> merged;
  for (Segment const &stage : std::as_const(split)) {
    double const duration{times[stage.last] - times[stage.first]};
    bool const   samePosition{!merged.isEmpty() && (merged.last().interval == stage.interval) && (merged.last().cycle == stage.cycle)};
    // a reference step always starts a new stage
    bool const mergeable{samePosition && !jump[stage.first]};
    if (mergeable && ((duration < options.minDurationS) || (merged.last().kind == stage.kind))) {
      merged.last().last = stage.last;
    } else {
      merged.push_back(stage);
    }
  }
  // a short stage at the start of a program position (or of the run) joins the next stage there
  for (qsizetype i{0}; i + 1 < merged.size();) {
    Segment const &stage = merged[i];
    Segment const &next  = merged[i + 1];
    bool const     shortStage{times[stage.last] - times[stage.first] < options.minDurationS};
    if (shortStage && (stage.interval == next.interval) && (stage.cycle == next.cycle) && !jump[next.first]) {
      merged[i + 1].first = stage.first;
      merged.removeAt(i);
    } else {
      ++i;
    }
  }

  for (Segment &segment : merged) {
    segment.start          = times[segment.first];
    segment.end            = times[segment.last];
    segment.referenceStart = ref[segment.first];
    segment.referenceEnd   = ref[segment.last];
  }
  return merged;
}

} // namespace deepvaccontrol::core
