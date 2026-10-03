#ifndef LLMSIDECAR_H
#define LLMSIDECAR_H

#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

namespace deepvaccontrol::assistant {

class LlmClient;

// Settings of the local model server (settings.json "assistant" section).
struct SidecarConfig {
  bool        managed{true};   // start llama-server ourselves; false: use a server that is already running
  bool        preload{false};  // load at application start (else when the Assistant tab is first opened)
  QString     serverPath;      // llama-server executable
  QString     modelPath;       // .gguf file
  QString     host{QStringLiteral("127.0.0.1")};
  quint16     port{8089};
  int         contextSize{8192};
  int         threads{0};      // 0: llama.cpp default
  int         gpuLayers{99};   // all layers on the GPU (Vulkan build); ignored by a CPU-only build; 0 = CPU only
  QStringList extraArgs;

  static SidecarConfig fromVariant(QVariantMap const &map, QString const &appDir);
  [[nodiscard]] QVariantMap toVariant() const;
  [[nodiscard]] QUrl        baseUrl() const;
  [[nodiscard]] QStringList arguments() const;
};

/**
 * @brief Runs llama.cpp's llama-server as a local sidecar process (bound to loopback only),
 * or watches an external one, and reports when the model is ready to answer.
 */
class LlmSidecar : public QObject {
  Q_OBJECT
public:
  enum class State : uint8_t {
    STOPPED,
    STARTING, // process started / waiting for the model to load
    READY,
    FAILED
  };

  explicit LlmSidecar(LlmClient *client, QObject *parent = nullptr);
  ~LlmSidecar() override;

  void                 setConfig(SidecarConfig const &config);
  [[nodiscard]] SidecarConfig const &config() const;
  void                 start();
  void                 stop();
  [[nodiscard]] State  state() const;
  [[nodiscard]] QString message() const;

signals:
  void stateChanged(State state, QString const &message);

private:
  void setState(State state, QString const &message);
  void poll();

  LlmClient    *m_client;
  SidecarConfig m_config;
  QProcess      m_process;
  QTimer        m_pollTimer;
  qint64        m_startedMs{0};
  State         m_state{State::STOPPED};
  QString       m_message;
  QStringList   m_log; // last lines of the server output, for error messages
};

} // namespace deepvaccontrol::assistant

#endif // LLMSIDECAR_H
