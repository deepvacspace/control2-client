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
#include "gatetcp.h"
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
  return QObject::tr("Kp = %1    Ki = %2    Kd = %3")
    .arg(QString::number(result.bestKp, 'f', 1), QString::number(result.bestKi, 'f', 1),
         QString::number(result.bestKd, 'f', 1));
}

// The app's message window with any number of answers; exec() returns the code of the one pressed (0: closed).
class ChoiceDialog : public MessageDialog {
public:
  ChoiceDialog(QString const &title, QString const &message, QString const &icon, QWidget *parent)
      : MessageDialog{title, message, parent} {
    m_closeBtn->setVisible(false);
    setIconKind(icon);
    setWindowTitle(QObject::tr("AI Advisor"));
  }
  // left to right; the primary one (last) is highlighted and the default
  QPushButton *addChoice(QString const &text, int code, bool primary = false) {
    auto *button = new QPushButton(text, this);
    button->setMinimumSize(static_cast<int>(110 * utils::ssf()), static_cast<int>(36 * utils::ssf()));
    if (primary) {
      button->setObjectName("primaryButton");
      button->setDefault(true);
    } else {
      button->setAutoDefault(false);
    }
    m_btnLayout->addWidget(button);
    connect(button, &QPushButton::clicked, this, [this, code] { done(code); });
    adjustSize();
    return button;
  }
};

void showNotice(QWidget *parent, QString const &message, QString const &icon = QStringLiteral("warning")) {
  auto *dialog = new ChoiceDialog(QObject::tr("AI Advisor"), message, icon, parent);
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
                                  QObject::tr("AI Advisor: Temperature PID set to %1 in all bands").arg(formatPid(result)));
  }
  return true;
}

} // namespace

bool offerOfflineAdvisorBeforeStart(ResHolder *resHolder, ProgramModel *program, QWidget *parent) {
  enum { CANCEL = 1, START = 2, ADVISOR = 3 };
  auto *question = new ChoiceDialog(
    QObject::tr("Optimize the Temperature PID first?"),
    QObject::tr("AI Advisor can calculate the best settings for this experiment with the "
                "chamber's digital twin."),
    QStringLiteral("question"), parent);
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
                            "Start it with the current PID settings?"),
      QStringLiteral("warning"), parent);
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

  // the recommendation, with the option to set it in every band
  bool const online{resHolder->status && (StatusManager::AppState::ONLINE == resHolder->status->getAppState())};
  bool const valid{model.envelope().containsPid(result.bestKp, result.bestKi, result.bestKd)};
  QString    note = QObject::tr("Apply sets these coefficients in the Temperature PID configuration and sends them to "
                                   "the chamber. The experiment has not been started: press Start again when ready.");
  if (!valid) {
    note = QObject::tr("These coefficients are outside the range the digital twin was validated for, so they "
                       "cannot be applied automatically.");
  } else if (!online) {
    note = QObject::tr("The chamber is not connected, so the coefficients cannot be applied now.");
  }
  enum { CLOSE = 1, APPLY = 2 };
  auto *recommendation = new ChoiceDialog(
    QObject::tr("Recommended Temperature PID"),
    QStringLiteral("<b>%1</b><br><br>%2").arg(formatPid(result).toHtmlEscaped().replace(QStringLiteral(" "), QStringLiteral("&nbsp;")), note.toHtmlEscaped()),
    QStringLiteral("info"), parent);
  recommendation->addChoice(QObject::tr("Close"), CLOSE);
  recommendation->addChoice(QObject::tr("Apply coefficients"), APPLY, true)->setEnabled(valid && online);
  if (APPLY == recommendation->exec()) {
    if (!applyToAllBands(resHolder, result)) {
      showNotice(parent, QObject::tr("The coefficients could not be applied: the Temperature PID settings are not available."));
    }
  }
  return false;
}

} // namespace deepvaccontrol
