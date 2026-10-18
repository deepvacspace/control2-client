#ifndef ASSISTANTWIDGET_H
#define ASSISTANTWIDGET_H

#include <QTimer>
#include <QJsonObject>
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
class AnalysisServices;
struct ResHolder;
} // namespace deepvaccontrol

namespace deepvaccontrol::assistant {

class AnalysisTools;
class ChamberTools;
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
  // experiments, notes and reports; showPage brings the page that shows experiments forward
  void setServices(AnalysisServices *services, std::function<void()> showPage);
  // from the chart page: questions about this experiment ("#N"; empty: the latest), and this one asked now
  void askAbout(QString const &run, QString const &question);
  // asked in the chart page's panel, about what its chart shows (see AssistantEngine::ask); the exchange is
  // mirrored there with chartExchange()
  void askFromChart(QString const &question, QJsonObject const &view);

signals:
  // a question asked from the chart: what is going on (status) or the answer so far; done: the answer is final
  void chartExchange(QString const &status, QString const &answer, bool done);

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
  AnalysisServices              *m_analysis{nullptr};
  LlmClient                     *m_client;
  LlmSidecar                    *m_sidecar;
  ToolRegistry                   m_registry;
  std::unique_ptr<AnalysisTools> m_tools;
  std::unique_ptr<ChamberTools>  m_chamberTools; // the chamber now: live state, settings, digital twin
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
  QJsonObject   m_queuedView;    // the chart view of the queued question
  bool          m_fromChart{false}; // the question being answered was asked from the chart
  int           m_liveBlock{-1}; // transcript block holding the answer being streamed
  int           m_statusBlock{-1}; // "Looking up …" while the data is fetched (replaced by the answer)
  int           m_checkBlock{-1};  // "Checking the values …" under the answer while it is checked
  void          setStatus(QString const &text);
  void          clearNotes();      // removes the status and check lines
  QTimer        m_renderTimer;
  bool          m_shownOnce{false};
};

} // namespace deepvaccontrol::assistant

#endif // ASSISTANTWIDGET_H
