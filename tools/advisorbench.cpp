// Times the AI Advisor's PID search (CEM over the digital twin) outside the application.
//
//   advisorbench <model dir> [population] [iterations] [threads] [duration s] [start degC] [target degC]
//
// threads 0: every core. Prints the time and the gains found.
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <cstdio>

#include "advisorcem.h"
#include "gruonnxmodel.h"

using namespace deepvaccontrol;
using namespace deepvaccontrol::ai;

int main(int argc, char **argv) {
  QCoreApplication  app(argc, argv);
  QStringList const args = app.arguments();
  if (args.size() < 2) {
    std::fprintf(stderr, "usage: advisorbench <model dir> [population] [iterations] [threads] [duration s] [start] [target]\n");
    return 2;
  }
  auto arg = [&](int i, double fallback) { return (args.size() > i) ? args[i].toDouble() : fallback; };
  QDir const dir(args[1]);
  int const  population{static_cast<int>(arg(2, CEM_POPULATION))};
  int const  iterations{static_cast<int>(arg(3, CEM_ITERATIONS))};
  int const  threads{static_cast<int>(arg(4, 0))};

  GruOnnxModel model;
  if (!model.load(dir.filePath("twin_model.onnx"), dir.filePath("twin_model.json"))) {
    std::fprintf(stderr, "model: %s\n", qPrintable(model.statusMessage()));
    return 1;
  }
  SimParams params;
  params.durationS  = arg(5, 1200.0);
  params.startTemp  = arg(6, 24.0);
  params.targetTemp = arg(7, 0.0);
  params.dtS        = DEFAULT_DT_S;

  CemOptimizer cem(&model);
  cem.setSearchSize(population, iterations);
  cem.setThreads(threads);
  QElapsedTimer timer;
  timer.start();
  CemResult const result = cem.run(params);
  double const    seconds{timer.nsecsElapsed() * 1e-9};
  std::printf("%dx%d, %d thread(s), %.0f s from %.1f to %.1f degC: %.1f s -> Kp %.0f Ki %.0f Kd %.0f (cost %.4g, %s)\n",
              population, iterations, threads, params.durationS, params.startTemp, params.targetTemp, seconds, result.kp,
              result.ki, result.kd, result.cost, result.valid ? "valid" : "INVALID");
  return result.valid ? 0 : 1;
}
