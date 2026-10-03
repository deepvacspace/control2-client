#ifndef ASSISTANTWIDGET_H
#define ASSISTANTWIDGET_H

#include <QTimer>
#include <QWidget>
#include <functional>
#include <memory>

#include "llmsidecar.h"
#include "toolregistry.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTextBrowser;

namespace deepvaccontrol {
class DataAnalysisWidget;
struct ResHolder;
} // namespace deepvaccontrol

namespace deepvaccontrol::assistant {

class AnalysisTools;
class AssistantEngine;
class LlmClient;

/**
 * @brief "Assistant" tab of the AI page: a fully offline chat with a local model (llama.cpp
 * llama-server sidecar) that answers questions about recorded experiments through the read-only
 * analysis tools ("Ask your run", summaries, comparisons, plots, reports).
 */
class AssistantWidget : public QWidget {
  Q_OBJECT
public:
  explicit AssistantWidget(ResHolder *resHolder, QWidget *parent = nullptr);
  ~AssistantWidget() override;

  // connects the tools to the experiment library; showPage brings the Data analysis page forward
  void setAnalysis(DataAnalysisWidget *analysis, std::function<void()> showPage);

protected:
  void showEvent(QShowEvent *event) override;

private:
  void send(QString const &question);
  void dispatch();
  void onStateChanged(LlmSidecar::State state, QString const &message);
  void refreshRunCombo();
  void append(QString const &markdown);
  void render();
  void updateButtons();
  [[nodiscard]] QString focusRun() const;

  ResHolder                     *m_resHolder;
  DataAnalysisWidget            *m_analysis{nullptr};
  LlmClient                     *m_client;
  LlmSidecar                    *m_sidecar;
  ToolRegistry                   m_registry;
  std::unique_ptr<AnalysisTools> m_tools;
  AssistantEngine               *m_engine;

  QLabel       *m_statusDot{nullptr};
  QComboBox    *m_runCombo{nullptr};
  QTextBrowser *m_transcript{nullptr};
  QLineEdit    *m_input{nullptr};
  QPushButton  *m_sendBtn{nullptr};
  QPushButton  *m_stopBtn{nullptr};
  QStringList   m_blocks;        // transcript, markdown
  QString       m_queued;        // question waiting for the model to load
  QString       m_queuedFocus;
  int           m_liveBlock{-1}; // transcript block holding the answer being streamed
  QTimer        m_renderTimer;
  bool          m_shownOnce{false};
};

} // namespace deepvaccontrol::assistant

#endif // ASSISTANTWIDGET_H
