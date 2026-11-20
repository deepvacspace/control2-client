#include "llmsidecar.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>

#include "llmclient.h"

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace deepvaccontrol::assistant {

namespace {
#ifdef Q_OS_WIN
// The server ends with the application however the application ends (closed, crashed, stopped in a debugger): its
// process goes into a job that Windows closes with ours. Otherwise it stays, holding the model and its port, and the
// next session cannot start its own
void endWithApplication(qint64 processId) {
  static HANDLE const job = [] {
    HANDLE const handle = CreateJobObjectW(nullptr, nullptr);
    if (handle) {
      JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
      limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
      SetInformationJobObject(handle, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
    }
    return handle;
  }();
  if (!job || (processId <= 0)) {
    return;
  }
  if (HANDLE const process = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, static_cast<DWORD>(processId))) {
    AssignProcessToJobObject(job, process);
    CloseHandle(process);
  }
}
#endif

constexpr int    POLL_MS{1000};
constexpr qint64 LOAD_TIMEOUT_MS{300'000}; // a cold disk and a big model can take a while
constexpr int    LOG_LINES{12};
}

SidecarConfig SidecarConfig::fromVariant(QVariantMap const &map, QString const &appDir) {
  SidecarConfig config;
  QDir const    llmDir(QDir(appDir).filePath(QStringLiteral("llm")));
  config.managed     = map.value("managed", true).toBool();
  config.preload     = map.value("preload", false).toBool();
  config.serverPath  = map.value("server_path", llmDir.filePath(QStringLiteral("llama-server.exe"))).toString();
  config.modelPath   = map.value("model_path", llmDir.filePath(QStringLiteral("models/Qwen3-4B-Instruct-2507-Q4_K_M.gguf"))).toString();
  config.host        = map.value("host", config.host).toString();
  config.port        = static_cast<quint16>(map.value("port", config.port).toUInt());
  config.contextSize = map.value("context_size", config.contextSize).toInt();
  config.threads     = map.value("threads", config.threads).toInt();
  config.gpuLayers   = map.value("gpu_layers", config.gpuLayers).toInt();
  config.extraArgs   = map.value("extra_args").toStringList();
  return config;
}

QVariantMap SidecarConfig::toVariant() const {
  return QVariantMap{{"managed", managed},         {"preload", preload},         {"server_path", serverPath}, {"model_path", modelPath},
                     {"host", host},               {"port", port},              {"context_size", contextSize},
                     {"threads", threads},         {"gpu_layers", gpuLayers},   {"extra_args", extraArgs}};
}

QUrl SidecarConfig::baseUrl() const {
  QUrl url;
  url.setScheme(QStringLiteral("http"));
  url.setHost(host);
  url.setPort(port);
  return url;
}

QStringList SidecarConfig::arguments() const {
  QStringList args{QStringLiteral("--model"), modelPath,
                   QStringLiteral("--host"), host,
                   QStringLiteral("--port"), QString::number(port),
                   QStringLiteral("--ctx-size"), QString::number(contextSize),
                   QStringLiteral("--n-gpu-layers"), QString::number(gpuLayers),
                   QStringLiteral("--jinja"), // chat template with tool calling
                   QStringLiteral("--no-webui")};
  if (threads > 0) {
    args << QStringLiteral("--threads") << QString::number(threads);
  }
  return args + extraArgs;
}

LlmSidecar::LlmSidecar(LlmClient *client, QObject *parent) : QObject{parent}, m_client{client} {
  m_process.setProcessChannelMode(QProcess::MergedChannels);
  connect(&m_process, &QProcess::readyRead, this, [this] {
    for (QByteArray const &line : m_process.readAll().split('\n')) {
      if (!line.trimmed().isEmpty()) {
        m_log.push_back(QString::fromUtf8(line.trimmed()));
      }
    }
    while (m_log.size() > LOG_LINES) {
      m_log.removeFirst();
    }
  });
  connect(&m_process, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus) {
    m_pollTimer.stop();
    if (m_state != State::STOPPED) {
      setState(State::FAILED, tr("The model server stopped (exit code %1). %2").arg(exitCode).arg(m_log.mid(m_log.size() - 3).join(' ')));
    }
  });
  connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
    if (error == QProcess::FailedToStart) {
      m_pollTimer.stop();
      setState(State::FAILED, tr("Could not start %1").arg(m_config.serverPath));
    }
  });
#ifdef Q_OS_WIN
  connect(&m_process, &QProcess::started, this, [this] { endWithApplication(m_process.processId()); });
#endif
  m_pollTimer.setInterval(POLL_MS);
  connect(&m_pollTimer, &QTimer::timeout, this, &LlmSidecar::poll);
}

LlmSidecar::~LlmSidecar() {
  m_state = State::STOPPED;
  if (m_process.state() != QProcess::NotRunning) {
    m_process.kill();
    m_process.waitForFinished(3000);
  }
}

void LlmSidecar::setConfig(SidecarConfig const &config) {
  m_config = config;
}

SidecarConfig const &LlmSidecar::config() const {
  return m_config;
}

LlmSidecar::State LlmSidecar::state() const {
  return m_state;
}

QString LlmSidecar::message() const {
  return m_message;
}

void LlmSidecar::setState(State state, QString const &message) {
  m_state   = state;
  m_message = message;
  emit stateChanged(state, message);
}

void LlmSidecar::start() {
  if ((m_state == State::STARTING) || (m_state == State::READY)) {
    return;
  }
  if (!m_client->setBaseUrl(m_config.baseUrl())) {
    setState(State::FAILED, tr("The model server must run on this computer (127.0.0.1 / localhost)"));
    return;
  }
  m_log.clear();
  m_startedMs = QDateTime::currentMSecsSinceEpoch();
  if (!m_config.managed) {
    setState(State::STARTING, tr("Connecting to %1…").arg(m_config.baseUrl().toString()));
    m_pollTimer.start();
    poll();
    return;
  }
  // A model server already answering there (one a session left behind when it ended without closing it, or one started
  // by hand): used as it is. A second one could not take the same port and would end at once
  setState(State::STARTING, tr("Loading %1…").arg(QFileInfo(m_config.modelPath).fileName()));
  m_client->checkHealth([this](bool ok, QString const &) {
    if ((m_state != State::STARTING) || (m_process.state() != QProcess::NotRunning) || m_pollTimer.isActive()) {
      return; // stopped or started again meanwhile
    }
    if (ok) {
      setState(State::READY, tr("Model ready (a model server was already running at %1)").arg(m_config.baseUrl().toString()));
      return;
    }
    launch();
  });
}

void LlmSidecar::launch() {
  if (!QFileInfo::exists(m_config.serverPath)) {
    setState(State::FAILED, tr("llama-server not found: %1").arg(QDir::toNativeSeparators(m_config.serverPath)));
    return;
  }
  if (!QFileInfo::exists(m_config.modelPath)) {
    setState(State::FAILED, tr("Model file not found: %1").arg(QDir::toNativeSeparators(m_config.modelPath)));
    return;
  }
  m_startedMs = QDateTime::currentMSecsSinceEpoch();
  m_process.setWorkingDirectory(QFileInfo(m_config.serverPath).absolutePath());
  m_process.start(m_config.serverPath, m_config.arguments());
  m_pollTimer.start();
  poll();
}

void LlmSidecar::stop() {
  m_pollTimer.stop();
  State const previous{m_state};
  m_state = State::STOPPED;
  if (m_process.state() != QProcess::NotRunning) {
    m_process.terminate();
    if (!m_process.waitForFinished(2000)) {
      m_process.kill();
      m_process.waitForFinished(2000);
    }
  }
  if (previous != State::STOPPED) {
    setState(State::STOPPED, tr("Model stopped"));
  }
}

void LlmSidecar::poll() {
  m_client->checkHealth([this](bool ok, QString const &detail) {
    if (m_state != State::STARTING) {
      return;
    }
    if (ok) {
      m_pollTimer.stop();
      setState(State::READY, tr("Model ready (%1)").arg(m_config.managed ? QFileInfo(m_config.modelPath).fileName()
                                                                          : m_config.baseUrl().toString()));
      return;
    }
    qint64 const waited{QDateTime::currentMSecsSinceEpoch() - m_startedMs};
    if (!m_config.managed && (waited > 5000)) {
      m_pollTimer.stop();
      setState(State::FAILED, tr("No model server at %1 (%2)").arg(m_config.baseUrl().toString(), detail));
    } else if (waited > LOAD_TIMEOUT_MS) {
      // the server ended without a "stopped" in between: whoever waits for the model is told why it failed
      m_pollTimer.stop();
      m_state = State::STOPPED; // its end is not reported as a failure of its own
      if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
        m_process.waitForFinished(2000);
      }
      setState(State::FAILED, tr("The model did not finish loading in %1 s").arg(LOAD_TIMEOUT_MS / 1000));
    }
  });
}

}
