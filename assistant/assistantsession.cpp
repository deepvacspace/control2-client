#include "assistantsession.h"

#include <QCoreApplication>
#include <QHash>
#include <QTimer>

#include "analysisservices.h"
#include "analysistools.h"
#include "assistantengine.h"
#include "chambertools.h"
#include "experimentlibrary.h"
#include "llmclient.h"
#include "resholder.h"
#include "settingsmanager.h"

namespace deepvaccontrol::assistant {

namespace {
QString const SETTINGS_SECTION{QStringLiteral("assistant")};

// what the user is told while a tool runs (the tool itself is not shown)
QString lookupText(QString const &tool) {
  static QHash<QString, char const *> const texts{
    {"get_run_summary", QT_TRANSLATE_NOOP("deepvaccontrol::assistant::AssistantSession", "Looking up the experiment…")},
    {"compare_runs", QT_TRANSLATE_NOOP("deepvaccontrol::assistant::AssistantSession", "Comparing the experiments…")},
    {"rank_runs", QT_TRANSLATE_NOOP("deepvaccontrol::assistant::AssistantSession", "Ranking the experiments…")},
    {"get_stages", QT_TRANSLATE_NOOP("deepvaccontrol::assistant::AssistantSession", "Looking up the stages…")},
    {"get_kpis", QT_TRANSLATE_NOOP("deepvaccontrol::assistant::AssistantSession", "Looking up the KPIs…")},
    {"get_anomalies", QT_TRANSLATE_NOOP("deepvaccontrol::assistant::AssistantSession", "Looking up the anomalies…")},
    {"list_runs", QT_TRANSLATE_NOOP("deepvaccontrol::assistant::AssistantSession", "Looking through the experiments…")},
    {"get_run_details", QT_TRANSLATE_NOOP("deepvaccontrol::assistant::AssistantSession", "Looking up the experiment details…")},
    {"get_events", QT_TRANSLATE_NOOP("deepvaccontrol::assistant::AssistantSession", "Looking up the events…")},
    {"plot_run", QT_TRANSLATE_NOOP("deepvaccontrol::assistant::AssistantSession", "Opening the chart…")},
    {"generate_report", QT_TRANSLATE_NOOP("deepvaccontrol::assistant::AssistantSession", "Writing the report…")},
    {"get_day_summary", QT_TRANSLATE_NOOP("deepvaccontrol::assistant::AssistantSession", "Looking up the day…")},
    {"get_live_status", QT_TRANSLATE_NOOP("deepvaccontrol::assistant::AssistantSession", "Reading the chamber…")},
    {"get_settings", QT_TRANSLATE_NOOP("deepvaccontrol::assistant::AssistantSession", "Reading the settings…")},
    {"predict_setpoint", QT_TRANSLATE_NOOP("deepvaccontrol::assistant::AssistantSession", "Simulating with the digital twin…")},
    {"suggest_pid", QT_TRANSLATE_NOOP("deepvaccontrol::assistant::AssistantSession", "Calculating PID coefficients…")},
    {"export_data", QT_TRANSLATE_NOOP("deepvaccontrol::assistant::AssistantSession", "Exporting the data…")},
    {"get_statistics", QT_TRANSLATE_NOOP("deepvaccontrol::assistant::AssistantSession", "Calculating the statistics…")},
    {"list_channels", QT_TRANSLATE_NOOP("deepvaccontrol::assistant::AssistantSession", "Looking up the channels…")},
    {"find_similar_runs", QT_TRANSLATE_NOOP("deepvaccontrol::assistant::AssistantSession", "Looking for similar experiments…")},
    {"improve_run", QT_TRANSLATE_NOOP("deepvaccontrol::assistant::AssistantSession", "Simulating with the digital twin…")}};
  char const *text = texts.value(tool, QT_TRANSLATE_NOOP("deepvaccontrol::assistant::AssistantSession", "Looking up the data…"));
  return QCoreApplication::translate("deepvaccontrol::assistant::AssistantSession", text);
}
}

AssistantSession::AssistantSession(ResHolder *resHolder, QObject *parent)
    : QObject{parent}
    , m_resHolder{resHolder}
    , m_client{new LlmClient(this)}
    , m_sidecar{new LlmSidecar(m_client, this)}
    , m_engine{new AssistantEngine(m_client, &m_registry, this)} {
  m_sidecar->setConfig(SidecarConfig::fromVariant(m_resHolder->settings->settingsMap().value(SETTINGS_SECTION).toMap(),
                                                  QCoreApplication::applicationDirPath()));

  connect(m_sidecar, &LlmSidecar::stateChanged, this, &AssistantSession::onStateChanged);
  connect(m_engine, &AssistantEngine::busyChanged, this, &AssistantSession::setWorking);
  connect(m_engine, &AssistantEngine::toolCalled, this, [this](QString const &name, QString const &) {
    emit exchange(lookupText(name), QString{}, false);
  });
  connect(m_engine, &AssistantEngine::partialAnswer, this, [this](QString const &text) {
    if (!text.trimmed().isEmpty()) {
      emit exchange(QString{}, text, false);
    }
  });
  connect(m_engine, &AssistantEngine::checkingAnswer, this, &AssistantSession::checkingAnswer);
  connect(m_engine, &AssistantEngine::answered, this, [this](QString const &text) { emit exchange(QString{}, text, true); });
  connect(m_engine, &AssistantEngine::failed, this, [this](QString const &error) {
    if (std::exchange(m_stopped, false)) {
      emit exchange(tr("Stopped"), QString{}, true);
    } else {
      emit exchange(QString{}, tr("Error: %1").arg(error), true);
    }
  });

  onStateChanged(LlmSidecar::State::STOPPED, tr("Model not started"));
  if (m_sidecar->config().preload) {
    QTimer::singleShot(0, m_sidecar, &LlmSidecar::start); // load while the operator works elsewhere
  }
}

AssistantSession::~AssistantSession() = default;

void AssistantSession::setServices(AnalysisServices *analysis, std::function<void()> showPage) {
  m_analysis = analysis;
  AnalysisViewer viewer;
  viewer.show = [analysis, showPage](ExperimentRun const &run, QStringList const &channels, double from, double to,
                                     QVector<ExperimentRun> const &compare) {
    QString const error = analysis->show(run, channels, from, to, compare);
    if (error.isEmpty() && showPage) {
      showPage();
    }
    return error;
  };
  viewer.report = [analysis](ExperimentRun const &run, QVector<ExperimentRun> const &compare, bool pdf, QString &path) {
    return analysis->writeReport(run, compare, pdf, path);
  };
  m_tools = std::make_unique<AnalysisTools>(analysis->library(), analysis->notes(), viewer);
  m_tools->registerTools(m_registry);
  m_chamberTools = std::make_unique<ChamberTools>(m_resHolder);
  m_chamberTools->registerTools(m_registry);
  m_tools->setPidAdvisor([this](QJsonObject const &args) { return m_chamberTools->suggestPid(args); });
}

LlmSidecar::State AssistantSession::modelState() const {
  return m_sidecar->state();
}

QString AssistantSession::modelMessage() const {
  return m_message;
}

void AssistantSession::startModel() {
  if (m_sidecar->state() == LlmSidecar::State::STOPPED) {
    m_sidecar->start();
  }
  if (m_analysis) {
    m_analysis->ensureScanned();
  }
}

void AssistantSession::ask(QString const &question, QJsonObject const &view) {
  QString const text = question.trimmed();
  if (text.isEmpty()) {
    return;
  }
  if (m_working) {
    emit exchange(QString{}, tr("Still answering the previous question."), true);
    return;
  }
  if (!m_tools) {
    emit exchange(QString{}, tr("The experiment library is not available."), true);
    return;
  }
  m_queued     = text;
  m_queuedView = view;
  m_stopped    = false;
  if (m_analysis) {
    m_analysis->ensureScanned();
  }
  // working from the moment it is asked (the model may still load, or the experiment list be read first)
  if (m_sidecar->state() != LlmSidecar::State::READY) {
    if (m_sidecar->state() != LlmSidecar::State::STARTING) {
      m_sidecar->start();
    }
    emit exchange(tr("Starting the local model"), QString{}, false);
  } else {
    emit exchange(tr("Assistant is thinking"), QString{}, false);
  }
  setWorking();
  dispatch();
}

void AssistantSession::stop() {
  if (!m_queued.isEmpty()) { // not asked yet: nothing to cancel in the engine
    m_queued.clear();
    m_queuedView = {};
    setWorking();
    emit exchange(tr("Stopped"), QString{}, true);
    return;
  }
  if (m_engine->busy()) {
    m_stopped = true;
    m_engine->cancel();
  }
}

void AssistantSession::newChat() {
  stop();
  m_engine->reset();
}

void AssistantSession::dispatch() {
  if (m_queued.isEmpty() || (m_sidecar->state() != LlmSidecar::State::READY)) {
    return;
  }
  if (m_client->busy()) {
    QTimer::singleShot(300, this, &AssistantSession::dispatch); // the warm-up is still running
    return;
  }
  if (m_analysis && m_analysis->library()->isScanning()) {
    QTimer::singleShot(500, this, &AssistantSession::dispatch); // wait for the experiment list
    return;
  }
  QString const     question = std::exchange(m_queued, QString{});
  QJsonObject const view     = std::exchange(m_queuedView, QJsonObject{});
  // the experiment the question is about unless it names another: the one shown, else the latest (a day or a
  // comparison shown: none)
  QString focus = view.value("run").toString();
  if (focus.isEmpty() && !view.contains(QLatin1String("day")) && !view.contains(QLatin1String("compare"))) {
    QVector<ExperimentRun> const runs = m_tools->runs();
    focus = runs.isEmpty() ? QString{} : QStringLiteral("#%1").arg(runs.size());
  }
  m_engine->ask(question, focus, view);
  setWorking();
}

void AssistantSession::onStateChanged(LlmSidecar::State state, QString const &message) {
  m_message = message;
  switch (state) {
    case LlmSidecar::State::READY:
      if (m_queued.isEmpty()) {
        m_engine->warmUp();
      }
      break;
    case LlmSidecar::State::STARTING:
      break;
    case LlmSidecar::State::FAILED:
      if (!m_queued.isEmpty()) {
        m_queued.clear();
        setWorking();
        emit exchange(QString{}, tr("Error: %1").arg(message), true);
      }
      break;
    default:
      if (!m_queued.isEmpty()) {
        m_queued.clear();
        setWorking();
        emit exchange(tr("Stopped"), QString{}, true);
      }
      break;
  }
  emit modelStateChanged(state, message);
  dispatch();
}

void AssistantSession::setWorking() {
  m_working = m_engine->busy() || !m_queued.isEmpty();
}

}
