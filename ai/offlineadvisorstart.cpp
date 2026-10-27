#include "offlineadvisorstart.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QPushButton>
#include <algorithm>
#include <cmath>

#include "actionmanager.h"
#include "advisorcem.h"
#include "advisorprogressdialog.h"
#include "advisorsafety.h"
#include "constants.h"
#include "gatetcp.h"
#include "choicedialog.h"
#include "messagedialog.h"
#include "settingsmanager.h"
#include "settingstransformer.h"
#include "statusmanager.h"
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
    // The twin is validated for up to an hour: longer stages use that horizon for their recommendation, indefinite
    // holds the default one.
    bool const timed{std::isfinite(durationS) && (durationS > 0.0)};
    scenario.params.durationS = timed ? std::clamp(durationS, 1.0, ai::MAX_HORIZON_S) : ai::DEFAULT_DURATION_S;
    scenario.params.rampS     = (timed && utils::getBitOfDword(flags, RAMP_BIT)) ? durationS : 0.0;
    scenario.params.dtS       = ai::DEFAULT_DT_S;
    scenario.valid            = true;
    return scenario;
  }

  scenario.error = QObject::tr("No temperature-controlled interval was found in this experiment.");
  return scenario;
}

QString formatPid(ai::OfflineTuneResult const &result) {
  return QObject::tr("P = %1    I = %2    D = %3")
    .arg(QString::number(result.bestKp, 'f', 1), QString::number(result.bestKi, 'f', 1),
         QString::number(result.bestKd, 'f', 1));
}

void showNotice(QWidget *parent, QString const &message, QString const &icon = QStringLiteral("warning")) {
  auto *dialog = new ChoiceDialog(QObject::tr("AI Advisor"), message, icon, parent);
  dialog->setWindowTitle(QObject::tr("AI Advisor"));
  dialog->addChoice(QObject::tr("Close"), 1, true);
  dialog->exec();
}

// The coefficients in every band of the Temperature PID, saved and sent to the chamber.
bool applyToAllBands(ResHolder *resHolder, ai::OfflineTuneResult const &result) {
  QVariantMap  plcSettings = resHolder->settings->settingsMap().value("plc_settings").toMap();
  QVariantList pids        = plcSettings.value("pids").toList();
  if (pids.isEmpty() || !resHolder->tcpGate) {
    return false;
  }
  QVariantMap pid    = pids.first().toMap();
  int const   points = pid.value("points").toInt();
  if (points <= 0) {
    return false;
  }
  pid["p"] = QVariantList(points, result.bestKp);
  pid["i"] = QVariantList(points, result.bestKi);
  pid["d"] = QVariantList(points, result.bestKd);
  QMap<QString, float> const flat = pidVariantToStringMap(pid);
  if (flat.isEmpty()) {
    return false;
  }
  pids[0]             = pid;
  plcSettings["pids"] = pids;
  resHolder->settings->setPlcSettings(plcSettings);
  if (resHolder->status) {
    resHolder->status->setSettingsListIdentical(false); // no "settings match" pop-up for the chamber's echo
  }
  resHolder->tcpGate->sendSettingMap(flat);
  if (resHolder->actions) {
    resHolder->actions->addAction(ActionManager::ActionType::PLC,
                                  QObject::tr("AI Advisor: recommended settings applied (Temperature PID %1 in all bands)")
                                    .arg(formatPid(result))); // the values stay in the log, for service
  }
  return true;
}

QString durationText(double seconds) {
  qint64 const total = std::llround(std::max(seconds, 0.0));
  return (total >= 3600) ? QObject::tr("%1 h %2 min").arg(total / 3600).arg((total / 60) % 60, 2, 10, QLatin1Char('0'))
                         : QObject::tr("%1 min").arg((total + 59) / 60);
}

}

ai::ForecastInput forecastInputFromProgram(ResHolder *resHolder, ProgramModel *program) {
  ai::ForecastInput input;
  if (!resHolder || !resHolder->states || !program) {
    input.error = QObject::tr("The experiment data is not available.");
    return input;
  }
  QStringList const parameterNames   = program->getParamListOrder();
  int const         temperatureIndex = parameterNames.indexOf("temp");
  int const         count            = program->getStandardModel()->columnCount();
  for (int interval{0}; interval < count; ++interval) {
    QVariantMap const values = program->intervalToVariantMap(interval);
    quint32 const     flags  = values.value("flags").toUInt();
    ai::ForecastInterval item;
    item.controlled = (temperatureIndex >= 0) && utils::getBitOfDword(flags, static_cast<quint8>(temperatureIndex))
                      && std::isfinite(values.value("temp").toDouble());
    item.targetTemp = values.value("temp").toDouble();
    item.durationS  = std::max(values.value("duration").toDouble(), 0.0);
    item.ramp       = utils::getBitOfDword(flags, RAMP_BIT);
    item.await      = !item.ramp && utils::getBitOfDword(flags, AWAIT_BIT);
    input.intervals << item;
  }
  QVariantMap const cycle = program->getCycleParams();
  input.cycle.start       = cycle.value("start").toInt();
  input.cycle.end         = cycle.value("end").toInt();
  input.cycle.count       = cycle.value("count").toInt();
  input.table             = ai::PidTable::fromVariant(
    resHolder->settings->settingsMap().value("plc_settings").toMap().value("pids").toList().value(0).toMap());

  bool const online{resHolder->status && (StatusManager::AppState::ONLINE == resHolder->status->getAppState())};
  SystemValue *temperature = resHolder->states->getSysValue("temp");
  if (!online || !temperature || !std::isfinite(temperature->getValue())) {
    input.error = QObject::tr("The current chamber temperature is not available.");
    return input;
  }
  input.startTemp = temperature->getValue();
  if (!input.table.isValid()) {
    input.error = QObject::tr("The chamber's temperature control configuration is not available.");
  }
  return input;
}

bool offerOfflineAdvisorBeforeStart(ResHolder *resHolder, ProgramModel *program, QWidget *parent) {
  enum { CANCEL = 1, START = 2, ADVISOR = 3 };
  auto *question = new ChoiceDialog(
    QObject::tr("Optimize the settings first?"),
    QObject::tr("AI Advisor can calculate the best settings for this experiment with the "
                "chamber's digital twin."),
    QStringLiteral("question"), parent);
  question->setWindowTitle(QObject::tr("AI Advisor"));
  question->addChoice(QObject::tr("Cancel"), CANCEL);
  question->addChoice(QObject::tr("Start without advisor"), START);
  question->addChoice(QObject::tr("Run AI Advisor"), ADVISOR, true);
  int const answer = question->exec();
  if (START == answer) {
    return true;
  }
  if (ADVISOR != answer) {
    return false;
  }

  AdvisorScenario const scenario = scenarioFromProgram(resHolder, program);
  if (!scenario.valid) {
    showNotice(parent, scenario.error + QObject::tr("\n\nThe experiment was not started."));
    return false;
  }

  QDir const modelDir(QDir(QCoreApplication::applicationDirPath()).filePath("data/model"));
  GruOnnxModel model;
  if (!model.load(modelDir.filePath("twin_model.onnx"), modelDir.filePath("twin_model.json"))) {
    showNotice(parent, model.statusMessage() + QObject::tr("\n\nThe experiment was not started."));
    return false;
  }

  // Outside the twin's validated range a "recommendation" is extrapolation: don't give one.
  QString const outside =
    model.envelope().checkScenario(scenario.params.startTemp, scenario.params.targetTemp, scenario.params.durationS);
  if (!outside.isEmpty()) {
    auto *choice = new ChoiceDialog(
      QObject::tr("AI Advisor"),
      outside + QObject::tr(".\n\nThe AI Advisor cannot give a reliable recommendation for this experiment. "
                            "Start it with the current settings?"),
      QStringLiteral("warning"), parent);
    choice->setWindowTitle(QObject::tr("AI Advisor"));
  choice->addChoice(QObject::tr("No"), CANCEL);
    choice->addChoice(QObject::tr("Start"), START, true);
    return START == choice->exec();
  }

  // the calculation; closing its window cancels it
  ai::AdvisorProgressDialog progress(parent);
  ai::OfflineTuneWorker     worker(&model, scenario.params);
  QEventLoop                waitLoop;
  QObject::connect(&worker, &ai::OfflineTuneWorker::progress, &progress, &ai::AdvisorProgressDialog::setProgress);
  QObject::connect(&progress, &ai::AdvisorProgressDialog::cancelRequested, &progress, [&worker] { worker.cancel(); });
  QObject::connect(&worker, &QThread::finished, &waitLoop, &QEventLoop::quit);
  worker.start();
  progress.show();
  waitLoop.exec();
  worker.wait();
  progress.finish();

  ai::OfflineTuneResult const result = worker.result();
  if (result.cancelled) {
    return false;
  }
  if (!result.ok) {
    showNotice(parent, (result.errorMessage.isEmpty() ? QObject::tr("AI Advisor failed") : result.errorMessage)
                         + QObject::tr("\n\nThe experiment was not started."));
    return false;
  }

  // the recommendation (what it changes is not shown): applied, or applied and the experiment started
  bool const online{resHolder->status && (StatusManager::AppState::ONLINE == resHolder->status->getAppState())};
  bool const valid{model.envelope().containsPid(result.bestKp, result.bestKi, result.bestKd)};
  QString    note = QObject::tr("Apply settings sets them and sends them to the chamber; press Start when ready. Apply "
                                "settings and start also starts the experiment.");
  if (!valid) {
    note = QObject::tr("These settings are outside the range the digital twin was validated for, so they "
                       "cannot be applied automatically.");
  } else if (!online) {
    note = QObject::tr("The chamber is not connected, so the settings cannot be applied now.");
  }
  enum { CLOSE = 1, APPLY = 2, APPLY_AND_START = 3 };
  auto *recommendation = new ChoiceDialog(QObject::tr("Recommended Settings"), note, QStringLiteral("info"), parent);
  recommendation->setWindowTitle(QObject::tr("AI Advisor"));
  recommendation->addChoice(QObject::tr("Close"), CLOSE);
  recommendation->addChoice(QObject::tr("Apply settings"), APPLY)->setEnabled(valid && online);
  recommendation->addChoice(QObject::tr("Apply settings and start"), APPLY_AND_START, true)->setEnabled(valid && online);
  int const choice = recommendation->exec();
  if ((APPLY != choice) && (APPLY_AND_START != choice)) {
    return false;
  }
  if (!applyToAllBands(resHolder, result)) {
    showNotice(parent, QObject::tr("The settings could not be applied: the chamber's temperature control configuration is not "
                                   "available.\n\nThe experiment was not started."));
    return false;
  }
  return APPLY_AND_START == choice; // the settings go to the chamber first, then the experiment
}


bool offerProgramPlanBeforeStart(ResHolder *resHolder, ProgramModel *program, QWidget *parent, QVector<ai::IntervalPlan> *plan,
                                 ai::ProgramPlan const *ready) {
  plan->clear();
  enum { CANCEL = 1, START = 2, ADVISOR = 3 };
  auto *question = new ChoiceDialog(
    QObject::tr("Optimize the settings first?"),
    QObject::tr("AI Advisor can calculate the best settings for each interval of this program with the chamber's "
                "digital twin. They are sent to the chamber as each interval begins."),
    QStringLiteral("question"), parent);
  question->setWindowTitle(QObject::tr("AI Advisor"));
  question->addChoice(QObject::tr("Cancel"), CANCEL);
  question->addChoice(QObject::tr("Start without advisor"), START);
  question->addChoice(QObject::tr("Run AI Advisor"), ADVISOR, true);
  int const answer = question->exec();
  if (START == answer) {
    return true;
  }
  if (ADVISOR != answer) {
    return false;
  }
  if (QString const denied = ai::advisorControlDenied(resHolder); !denied.isEmpty()) {
    showNotice(parent, denied + QObject::tr(".\n\nThe experiment was not started."));
    return false;
  }

  ai::ForecastInput const input = forecastInputFromProgram(resHolder, program);
  if (!input.error.isEmpty()) {
    showNotice(parent, input.error + QObject::tr("\n\nThe experiment was not started."));
    return false;
  }
  QDir const   modelDir(QDir(QCoreApplication::applicationDirPath()).filePath("data/model"));
  GruOnnxModel model;
  if (!model.load(modelDir.filePath("twin_model.onnx"), modelDir.filePath("twin_model.json"))) {
    showNotice(parent, model.statusMessage() + QObject::tr("\n\nThe experiment was not started."));
    return false;
  }

  ai::ProgramPlan result;
  if (ready && ready->ok && ready->input.sameAs(input)) {
    result = *ready; // the plan behind the estimate on the page: the same settings, the same time
  } else {
    // every temperature interval in turn; closing the window cancels
    ai::AdvisorProgressDialog progress(parent);
    ai::ProgramPlanWorker     worker(&model, input);
    QEventLoop                waitLoop;
    QObject::connect(&worker, &ai::ProgramPlanWorker::progress, &progress, &ai::AdvisorProgressDialog::setProgress);
    QObject::connect(&progress, &ai::AdvisorProgressDialog::cancelRequested, &progress, [&worker] { worker.cancel(); });
    QObject::connect(&worker, &QThread::finished, &waitLoop, &QEventLoop::quit);
    worker.start();
    progress.show();
    waitLoop.exec();
    worker.wait();
    progress.finish();
    result = worker.result();
  }
  if (result.cancelled) {
    return false;
  }
  if (!result.ok) {
    auto *choice = new ChoiceDialog(
      QObject::tr("AI Advisor"),
      (result.error.isEmpty() ? QObject::tr("AI Advisor failed") : result.error)
        + QObject::tr("\n\nThe AI Advisor cannot give a reliable recommendation for this experiment. Start it with the "
                      "current settings?"),
      QStringLiteral("warning"), parent);
    choice->setWindowTitle(QObject::tr("AI Advisor"));
    choice->addChoice(QObject::tr("No"), CANCEL);
    choice->addChoice(QObject::tr("Start"), START, true);
    return START == choice->exec();
  }

  // the recommendation (what it changes is not shown)
  int const planned = static_cast<int>(
    std::count_if(result.intervals.cbegin(), result.intervals.cend(), [](ai::IntervalPlan const &p) { return p.valid; }));
  QStringList notes;
  notes << QObject::tr("Settings were calculated for %n interval(s). Each interval's settings are sent to the chamber as "
                       "it begins; your own settings come back when the experiment ends.",
                       nullptr, planned);
  if (!result.kept.isEmpty()) {
    QStringList numbers;
    for (int interval : result.kept) {
      numbers << QString::number(interval + 1);
    }
    notes << QObject::tr("Interval(s) %1 keep your own settings: they are outside the range the digital twin is "
                         "validated for (%2, up to 1 hour).")
               .arg(numbers.join(QStringLiteral(", ")), model.envelope().describe());
  }
  if (result.forecast.ok) {
    QString estimate = result.forecast.endless ? QObject::tr("Estimated time with these settings: ≈ %1 per pass.")
                                                   .arg(durationText(result.forecast.totalS))
                                               : QObject::tr("Estimated time with these settings: ≈ %1.")
                                                   .arg(durationText(result.forecast.totalS));
    if (result.forecast.uncertain) {
      estimate += QObject::tr(" It may take longer: not every target is reached within the hour the twin can predict.");
    }
    notes << estimate;
  }
  bool const online{resHolder->status && (StatusManager::AppState::ONLINE == resHolder->status->getAppState())};
  if (!online) {
    notes << QObject::tr("The chamber is not connected, so the settings cannot be applied now.");
  }
  enum { CLOSE = 1, APPLY_AND_START = 2 };
  auto *recommendation = new ChoiceDialog(QObject::tr("Recommended Settings"), notes.join(QStringLiteral("\n\n")),
                                          QStringLiteral("info"), parent);
  recommendation->setWindowTitle(QObject::tr("AI Advisor"));
  recommendation->addChoice(QObject::tr("Close"), CLOSE);
  recommendation->addChoice(QObject::tr("Apply settings and start"), APPLY_AND_START, true)->setEnabled(online);
  if (APPLY_AND_START != recommendation->exec()) {
    return false;
  }
  *plan = result.intervals;
  return true;
}

}
