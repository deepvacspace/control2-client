#include "offlineadvisorstart.h"

#include <QAbstractButton>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <algorithm>
#include <cmath>

#include "advisorcem.h"
#include "gruonnxmodel.h"
#include "offlinetuneworker.h"
#include "programmodel.h"
#include "resholder.h"
#include "systemvalue.h"
#include "systemvaluelist.h"
#include "utils.h"

namespace deepvaccontrol {
namespace {

struct AdvisorScenario {
  bool          valid{false};
  ai::SimParams params;
  QString       error;
};

AdvisorScenario scenarioFromProgram(ResHolder *resHolder, ProgramModel *program) {
  AdvisorScenario scenario;
  if (!resHolder || !resHolder->states || !program) {
    scenario.error = QObject::tr("The experiment data is not available.");
    return scenario;
  }

  SystemValue *temperature = resHolder->states->getSysValue("temp");
  if (!temperature || !std::isfinite(temperature->getValue())) {
    scenario.error = QObject::tr("The current chamber temperature is not available.");
    return scenario;
  }

  QStringList const parameterNames   = program->getParamListOrder();
  int const         temperatureIndex = parameterNames.indexOf("temp");
  if (temperatureIndex < 0) {
    scenario.error = QObject::tr("This experiment has no temperature control parameter.");
    return scenario;
  }

  int const intervalCount = program->getStandardModel()->columnCount();
  for (int interval{0}; interval < intervalCount; ++interval) {
    QVariantMap const values = program->intervalToVariantMap(interval);
    quint32 const     flags  = values.value("flags").toUInt();
    if (!utils::getBitOfDword(flags, static_cast<quint8>(temperatureIndex))) {
      continue;
    }

    double const targetTemp = values.value("temp").toDouble();
    double const durationS  = values.value("duration").toDouble();
    if (!std::isfinite(targetTemp)) {
      continue;
    }

    scenario.params            = ai::SimParams{};
    scenario.params.startTemp  = temperature->getValue();
    scenario.params.targetTemp = targetTemp;
    // The fixed-setpoint twin is validated for a 20-minute horizon. Long
    // stages and indefinite holds use that horizon for their recommendation.
    scenario.params.durationS = std::isfinite(durationS) && (durationS > 0.0)
                                  ? std::clamp(durationS, 1.0, ai::DEFAULT_DURATION_S)
                                  : ai::DEFAULT_DURATION_S;
    scenario.params.dtS       = ai::DEFAULT_DT_S;
    scenario.valid            = true;
    return scenario;
  }

  scenario.error = QObject::tr("No temperature-controlled interval was found in this experiment.");
  return scenario;
}

QString formatPid(ai::OfflineTuneResult const &result) {
  return QObject::tr("Kp = %1\nKi = %2\nKd = %3")
    .arg(QString::number(result.bestKp, 'f', 1), QString::number(result.bestKi, 'f', 1),
         QString::number(result.bestKd, 'f', 1));
}

} // namespace

bool offerOfflineAdvisorBeforeStart(ResHolder *resHolder, ProgramModel *program, QWidget *parent) {
  QMessageBox question{parent};
  question.setIcon(QMessageBox::Question);
  question.setWindowTitle(QObject::tr("AI Advisor"));
  question.setText(QObject::tr(
    "Would you like AI Advisor to search for the optimal Temperature PID parameters for this experiment?"));
  question.setInformativeText(QObject::tr(
    "The experiment will not start after the calculation. You can review the result and enter it manually in Settings -> Temperature PID."));

  QAbstractButton *runAdvisor = question.addButton(QObject::tr("Run AI Advisor"), QMessageBox::AcceptRole);
  QAbstractButton *startNow = question.addButton(QObject::tr("Start without advisor"), QMessageBox::NoRole);
  question.addButton(QMessageBox::Cancel);
  question.setDefaultButton(qobject_cast<QPushButton *>(runAdvisor));
  question.exec();

  if (question.clickedButton() == startNow) {
    return true;
  }
  if (question.clickedButton() != runAdvisor) {
    return false;
  }

  AdvisorScenario const scenario = scenarioFromProgram(resHolder, program);
  if (!scenario.valid) {
    QMessageBox::warning(parent, QObject::tr("AI Advisor"),
                         scenario.error + QObject::tr("\n\nThe experiment was not started."));
    return false;
  }

  QDir const modelDir(QDir(QCoreApplication::applicationDirPath()).filePath("data/model"));
  GruOnnxModel model;
  if (!model.load(modelDir.filePath("gru_rollout.onnx"), modelDir.filePath("gru_rollout.json"))) {
    QMessageBox::warning(parent, QObject::tr("AI Advisor"),
                         model.statusMessage() + QObject::tr("\n\nThe experiment was not started."));
    return false;
  }

  int const totalSteps = std::max(1, static_cast<int>(std::ceil(scenario.params.durationS / scenario.params.dtS)))
                         * ai::CEM_ITERATIONS;
  QProgressDialog progress(QObject::tr("Computing optimal Temperature PID parameters with the digital twin..."),
                           QString(), 0, totalSteps, parent);
  progress.setWindowTitle(QObject::tr("AI Advisor"));
  progress.setWindowModality(Qt::ApplicationModal);
  progress.setMinimumDuration(0);
  progress.setCancelButton(nullptr);
  progress.setValue(0);

  ai::OfflineTuneWorker worker(&model, scenario.params);
  QEventLoop            waitLoop;
  QObject::connect(&worker, &ai::OfflineTuneWorker::progress, &progress, [&progress](int done, int total) {
    progress.setMaximum(total);
    progress.setValue(done);
  });
  QObject::connect(&worker, &QThread::finished, &waitLoop, &QEventLoop::quit);
  worker.start();
  progress.show();
  waitLoop.exec();
  worker.wait();
  progress.close();

  ai::OfflineTuneResult const result = worker.result();
  if (!result.ok) {
    QMessageBox::warning(parent, QObject::tr("AI Advisor"),
                         (result.errorMessage.isEmpty() ? QObject::tr("AI Advisor failed") : result.errorMessage)
                           + QObject::tr("\n\nThe experiment was not started."));
    return false;
  }

  QMessageBox resultDialog{parent};
  resultDialog.setIcon(QMessageBox::Information);
  resultDialog.setWindowTitle(QObject::tr("AI Advisor recommendation"));
  resultDialog.setText(QObject::tr("Recommended Temperature PID parameters:\n\n%1").arg(formatPid(result)));
  resultDialog.setInformativeText(QObject::tr(
    "The experiment has not been started. Change these values manually in Settings -> Temperature PID, send the settings to the PLC, and then press Start again."));
  resultDialog.setStandardButtons(QMessageBox::Ok);
  resultDialog.exec();
  return false;
}

} // namespace deepvaccontrol
