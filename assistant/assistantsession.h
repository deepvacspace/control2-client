#ifndef ASSISTANTSESSION_H
#define ASSISTANTSESSION_H

#include <QJsonObject>
#include <QObject>
#include <functional>
#include <memory>

#include "llmsidecar.h"
#include "toolregistry.h"

namespace deepvaccontrol {
class AnalysisServices;
struct ResHolder;
}

namespace deepvaccontrol::assistant {

class AnalysisTools;
class ChamberTools;
class AssistantEngine;
class LlmClient;

/**
 * @brief The assistant behind the Analysis page's Assistant panel: a fully offline conversation with a local model
 * (llama.cpp llama-server sidecar) that answers questions about recorded experiments, days, the chamber and the
 * digital twin through the read-only tools. The panel shows the exchange (exchange()) and the model's state.
 */
class AssistantSession : public QObject {
  Q_OBJECT
public:
  explicit AssistantSession(ResHolder *resHolder, QObject *parent = nullptr);
  ~AssistantSession() override;

  // connects the tools to the experiment library; showPage brings the page that shows experiments forward
  void setServices(AnalysisServices *services, std::function<void()> showPage);
  [[nodiscard]] LlmSidecar::State modelState() const;
  [[nodiscard]] QString           modelMessage() const;

public slots:
  // a question about what the chart shows (see AssistantEngine::ask)
  void ask(QString const &question, QJsonObject const &view);
  // the answer being written (or the question waiting for the model) is dropped
  void stop();
  // forgets the conversation
  void newChat();
  // loads the model now (it is most likely wanted soon), not at the first question
  void startModel();

signals:
  // what is going on (status) or the answer so far; done: the answer is final
  void exchange(QString const &status, QString const &answer, bool done);
  // the answer shown is checked against the data (a better one may replace it)
  void checkingAnswer();
  void modelStateChanged(LlmSidecar::State state, QString const &message);

private:
  void dispatch();
  void onStateChanged(LlmSidecar::State state, QString const &message);
  void setWorking();

  ResHolder                     *m_resHolder;
  AnalysisServices              *m_analysis{nullptr};
  LlmClient                     *m_client;
  LlmSidecar                    *m_sidecar;
  ToolRegistry                   m_registry;
  std::unique_ptr<AnalysisTools> m_tools;
  std::unique_ptr<ChamberTools>  m_chamberTools; // the chamber now: live state, settings, digital twin
  AssistantEngine               *m_engine;
  QString                        m_queued; // question waiting for the model to load
  QJsonObject                    m_queuedView;
  QString                        m_message; // the model's state, in words
  bool                           m_stopped{false}; // stopped by the user: no error shown
  bool                           m_working{false}; // answering, or waiting for the model to load
};

}

#endif // ASSISTANTSESSION_H
