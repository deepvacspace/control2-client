#include "assistantwidget.h"

#include <QComboBox>
#include <QFrame>
#include <QHash>
#include <array>
#include <QCoreApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTextBrowser>
#include <QTimer>
#include <QVBoxLayout>

#include "analysistools.h"
#include "chambertools.h"
#include "assistantengine.h"
#include "analysisservices.h"
#include "experimentlibrary.h"
#include "llmclient.h"
#include "resholder.h"
#include "settingsmanager.h"
#include "utils.h"

namespace deepvaccontrol::assistant {

namespace {
QString const SETTINGS_SECTION{QStringLiteral("assistant")};

// what the user is told while a tool runs (the tool itself is not shown)
QString lookupText(QString const &tool) {
  static QHash<QString, char const *> const texts{
    {"get_run_summary", QT_TRANSLATE_NOOP("deepvaccontrol::assistant::AssistantWidget", "Looking up the experiment…")},
    {"compare_runs", QT_TRANSLATE_NOOP("deepvaccontrol::assistant::AssistantWidget", "Comparing the experiments…")},
    {"rank_runs", QT_TRANSLATE_NOOP("deepvaccontrol::assistant::AssistantWidget", "Ranking the experiments…")},
    {"get_stages", QT_TRANSLATE_NOOP("deepvaccontrol::assistant::AssistantWidget", "Looking up the stages…")},
    {"get_kpis", QT_TRANSLATE_NOOP("deepvaccontrol::assistant::AssistantWidget", "Looking up the KPIs…")},
    {"get_anomalies", QT_TRANSLATE_NOOP("deepvaccontrol::assistant::AssistantWidget", "Looking up the anomalies…")},
    {"list_runs", QT_TRANSLATE_NOOP("deepvaccontrol::assistant::AssistantWidget", "Looking through the experiments…")},
    {"get_run_details", QT_TRANSLATE_NOOP("deepvaccontrol::assistant::AssistantWidget", "Looking up the experiment details…")},
    {"get_events", QT_TRANSLATE_NOOP("deepvaccontrol::assistant::AssistantWidget", "Looking up the events…")},
    {"plot_run", QT_TRANSLATE_NOOP("deepvaccontrol::assistant::AssistantWidget", "Opening the chart…")},
    {"generate_report", QT_TRANSLATE_NOOP("deepvaccontrol::assistant::AssistantWidget", "Writing the report…")},
    {"get_day_summary", QT_TRANSLATE_NOOP("deepvaccontrol::assistant::AssistantWidget", "Looking up the day…")},
    {"get_live_status", QT_TRANSLATE_NOOP("deepvaccontrol::assistant::AssistantWidget", "Reading the chamber…")},
    {"get_settings", QT_TRANSLATE_NOOP("deepvaccontrol::assistant::AssistantWidget", "Reading the settings…")},
    {"predict_setpoint", QT_TRANSLATE_NOOP("deepvaccontrol::assistant::AssistantWidget", "Simulating with the digital twin…")},
    {"suggest_pid", QT_TRANSLATE_NOOP("deepvaccontrol::assistant::AssistantWidget", "Calculating PID coefficients…")},
    {"export_data", QT_TRANSLATE_NOOP("deepvaccontrol::assistant::AssistantWidget", "Exporting the data…")}};
  char const *text = texts.value(tool, QT_TRANSLATE_NOOP("deepvaccontrol::assistant::AssistantWidget", "Looking up the data…"));
  return QCoreApplication::translate("deepvaccontrol::assistant::AssistantWidget", text);
}

QString muted(QString const &text) {
  return QStringLiteral("<span style=\"color:#64748b\">%1</span>").arg(text.toHtmlEscaped());
}

QString userMarkdown(QString text) {
  return QStringLiteral("> **User -** ") + text.replace(QLatin1Char('\n'), QStringLiteral("\n> "));
}
} // namespace

AssistantWidget::AssistantWidget(ResHolder *resHolder, QWidget *parent)
    : QWidget{parent}
    , m_resHolder{resHolder}
    , m_client{new LlmClient(this)}
    , m_sidecar{new LlmSidecar(m_client, this)}
    , m_engine{new AssistantEngine(m_client, &m_registry, this)} {
  m_sidecar->setConfig(SidecarConfig::fromVariant(m_resHolder->settings->settingsMap().value(SETTINGS_SECTION).toMap(),
                                                  QCoreApplication::applicationDirPath()));

  double const ssf{utils::ssf()};
  auto        *root = new QVBoxLayout(this);
  root->setContentsMargins(0, 8 * ssf, 0, 0);
  auto *card = new QFrame(this);
  card->setObjectName("pageCard");
  root->addWidget(card);
  auto *cardLay = new QVBoxLayout(card);
  cardLay->setContentsMargins(14 * ssf, 10 * ssf, 14 * ssf, 12 * ssf);
  cardLay->setSpacing(10 * ssf);

  // experiment in focus, model state, quick questions
  auto *contextRow = new QHBoxLayout;
  contextRow->setSpacing(8 * ssf);
  auto *title = new QLabel(tr("Assistant"), card);
  title->setObjectName("cardTitle");
  contextRow->addWidget(title);
  m_statusDot = new QLabel(QStringLiteral("●"), card); // model state; details in the tooltip
  contextRow->addWidget(m_statusDot);
  contextRow->addSpacing(12 * ssf);
  auto *experimentLabel = new QLabel(tr("Experiment"), card);
  experimentLabel->setProperty("muted", true);
  contextRow->addWidget(experimentLabel);
  m_runCombo = new QComboBox(card);
  m_runCombo->setFixedHeight(static_cast<int>(30 * ssf));
  m_runCombo->setMinimumContentsLength(24);
  m_runCombo->setToolTip(tr("Questions are about this experiment unless you name another one"));
  contextRow->addWidget(m_runCombo, 1);
  struct Quick {
    QString label;
    QString question;
  };
  for (Quick const &quick : {Quick{tr("Summarize"), tr("Summarize this experiment: configuration, main KPIs, stages and anything abnormal.")},
                             Quick{tr("Anomalies"), tr("Which anomalies were detected in this experiment and how serious are they?")},
                             Quick{tr("Compare with previous"), tr("Compare this experiment with the previous experiment. What changed and which one controlled better?")},
                             Quick{tr("Report"), tr("Generate an HTML report of this experiment.")}}) {
    auto *button = new QPushButton(quick.label, card);
    button->setFixedHeight(static_cast<int>(30 * ssf));
    button->setToolTip(quick.question);
    connect(button, &QPushButton::clicked, this, [this, question = quick.question] { send(question); });
    contextRow->addWidget(button);
  }
  cardLay->addLayout(contextRow);

  // the conversation
  m_transcript = new QTextBrowser(card);
  m_transcript->setOpenExternalLinks(false);
  m_transcript->setOpenLinks(false);
  m_transcript->document()->setDocumentMargin(12 * ssf);
  cardLay->addWidget(m_transcript, 1);

  // question
  auto *inputRow = new QHBoxLayout;
  inputRow->setSpacing(8 * ssf);
  m_input = new QLineEdit(card);
  m_input->setFixedHeight(static_cast<int>(36 * ssf));
  m_input->setPlaceholderText(tr("Ask about your experiments, e.g. \"Which −70 °C test had the lowest overshoot?\""));
  m_input->setTextMargins(8 * ssf, 0, 8 * ssf, 0);
  connect(m_input, &QLineEdit::returnPressed, this, [this] { send(m_input->text()); });
  m_sendBtn = new QPushButton(tr("Send"), card);
  m_sendBtn->setObjectName("primaryButton");
  connect(m_sendBtn, &QPushButton::clicked, this, [this] { send(m_input->text()); });
  m_stopBtn = new QPushButton(tr("Stop"), card);
  m_stopBtn->setProperty("outline", true);
  connect(m_stopBtn, &QPushButton::clicked, this, [this] {
    m_queued.clear();
    m_engine->cancel();
  });
  auto *newChatBtn = new QPushButton(tr("New chat"), card);
  connect(newChatBtn, &QPushButton::clicked, this, [this] {
    m_engine->reset();
    m_blocks.clear();
    m_liveBlock   = -1;
    m_statusBlock = -1;
    m_checkBlock  = -1;
    render();
  });
  for (QPushButton *button : {m_sendBtn, m_stopBtn, newChatBtn}) {
    button->setFixedHeight(static_cast<int>(36 * ssf));
    button->setMinimumWidth(static_cast<int>(90 * ssf));
  }
  inputRow->addWidget(m_input, 1);
  inputRow->addWidget(m_sendBtn);
  inputRow->addWidget(m_stopBtn);
  inputRow->addWidget(newChatBtn);
  cardLay->addLayout(inputRow);

  connect(m_sidecar, &LlmSidecar::stateChanged, this, &AssistantWidget::onStateChanged);
  connect(m_engine, &AssistantEngine::busyChanged, this, &AssistantWidget::updateButtons);
  connect(m_engine, &AssistantEngine::toolCalled, this, [this](QString const &name, QString const &) {
    if (m_checkBlock >= 0) {
      return; // the background correction: the answer shown stays, with its check note
    }
    if (m_liveBlock >= 0) {
      m_blocks.removeAt(m_liveBlock); // text written before a tool call is not the answer
      m_liveBlock = -1;
    }
    setStatus(lookupText(name));
    if (m_fromChart) {
      emit chartExchange(lookupText(name), QString{}, false);
    }
  });
  connect(m_engine, &AssistantEngine::partialAnswer, this, [this](QString const &text) {
    if (text.trimmed().isEmpty()) {
      return;
    }
    if (m_liveBlock < 0) {
      if (m_statusBlock >= 0) { // the answer takes the place of the status line
        m_liveBlock   = m_statusBlock;
        m_statusBlock = -1;
        m_blocks[m_liveBlock] = text;
      } else {
        m_blocks.push_back(text);
        m_liveBlock = static_cast<int>(m_blocks.size()) - 1;
      }
    } else {
      m_blocks[m_liveBlock] = text;
    }
    if (!m_renderTimer.isActive()) {
      m_renderTimer.start(); // at most ~8 redraws per second while tokens arrive
    }
    if (m_fromChart) {
      emit chartExchange(QString{}, text, false);
    }
  });
  connect(m_engine, &AssistantEngine::answered, this, [this](QString const &text) {
    int const live{m_liveBlock};
    m_liveBlock = -1;
    clearNotes();
    if ((live >= 0) && (live < m_blocks.size())) {
      m_blocks[live] = text; // the streamed text (or the better corrected one), plus the grounding note if any
      render();
    } else {
      append(text);
    }
    if (std::exchange(m_fromChart, false)) {
      emit chartExchange(QString{}, text, true);
    }
  });
  // the shown answer stays while a better one is looked for
  connect(m_engine, &AssistantEngine::checkingAnswer, this, [this] {
    if (m_checkBlock < 0) {
      m_blocks.push_back(muted(tr("Checking the values against the experiment data…")));
      m_checkBlock = static_cast<int>(m_blocks.size()) - 1;
      render();
    }
  });
  m_renderTimer.setSingleShot(true);
  m_renderTimer.setInterval(120);
  connect(&m_renderTimer, &QTimer::timeout, this, &AssistantWidget::render);
  connect(m_engine, &AssistantEngine::failed, this, [this](QString const &error) {
    m_liveBlock = -1;
    clearNotes();
    append(QStringLiteral("<span style=\"color:#dc2626\">%1</span>").arg(tr("Error: %1").arg(error).toHtmlEscaped()));
    if (std::exchange(m_fromChart, false)) {
      emit chartExchange(QString{}, tr("Error: %1").arg(error), true);
    }
  });

  onStateChanged(LlmSidecar::State::STOPPED, tr("Model not started"));
  if (m_sidecar->config().preload) {
    QTimer::singleShot(0, m_sidecar, &LlmSidecar::start); // load while the operator works elsewhere
  }
  append(tr("**Assistant** — ask about recorded experiments and days (summaries, KPIs, stages, anomalies, comparisons, "
            "charts, reports, data export), the chamber now and its settings, or the digital twin (predictions, PID "
            "suggestions)."));
}

AssistantWidget::~AssistantWidget() = default;

void AssistantWidget::setServices(AnalysisServices *analysis, std::function<void()> showPage) {
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

  connect(analysis->library(), &ExperimentLibrary::runsChanged, this, &AssistantWidget::refreshRunCombo);
  refreshRunCombo();
}

void AssistantWidget::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);
  if (!m_shownOnce) {
    m_shownOnce = true;
    if (m_sidecar->state() == LlmSidecar::State::STOPPED) {
      m_sidecar->start(); // the model is most likely wanted soon: load it now, not at the first question
    }
  }
  if (m_analysis) {
    m_analysis->ensureScanned();
  }
}

void AssistantWidget::refreshRunCombo() {
  QString const current = m_runCombo->currentData().toString();
  m_runCombo->clear();
  m_runCombo->addItem(tr("Latest experiment"), QString());
  if (!m_tools) {
    return;
  }
  QVector<ExperimentRun> const runs = m_tools->runs();
  for (qsizetype i{runs.size() - 1}; i >= 0; --i) {
    m_runCombo->addItem(QStringLiteral("#%1  %2").arg(i + 1).arg(runs[i].title), QStringLiteral("#%1").arg(i + 1));
  }
  if (int const index = m_runCombo->findData(current); index >= 0) {
    m_runCombo->setCurrentIndex(index);
  }
}

void AssistantWidget::askFromChart(QString const &question, QJsonObject const &view) {
  if (question.trimmed().isEmpty()) {
    return;
  }
  if (m_engine->busy() || !m_queued.isEmpty()) {
    emit chartExchange(QString{}, tr("Still answering the previous question."), true);
    return;
  }
  refreshRunCombo();
  m_runCombo->setCurrentIndex(std::max(0, m_runCombo->findData(view.value("run").toString())));
  m_queuedView = view;
  m_fromChart  = true;
  emit chartExchange(m_sidecar->state() == LlmSidecar::State::READY ? tr("Thinking…") : tr("Starting the local model…"), QString{}, false);
  send(question);
}

void AssistantWidget::askAbout(QString const &run, QString const &question) {
  refreshRunCombo();
  m_runCombo->setCurrentIndex(std::max(0, m_runCombo->findData(run)));
  if (!question.trimmed().isEmpty()) {
    send(question);
  }
}

QString AssistantWidget::focusRun() const {
  QString const selected = m_runCombo->currentData().toString();
  if (!selected.isEmpty() || !m_tools) {
    return selected;
  }
  QVector<ExperimentRun> const runs = m_tools->runs();
  return runs.isEmpty() ? QString() : QStringLiteral("#%1").arg(runs.size());
}

void AssistantWidget::send(QString const &question) {
  QString const text = question.trimmed();
  if (text.isEmpty() || m_engine->busy() || !m_queued.isEmpty()) {
    return;
  }
  if (!m_tools) {
    append(tr("The experiment library is not available."));
    return;
  }
  m_input->clear();
  append(userMarkdown(text));
  m_queued      = text;
  m_queuedFocus = focusRun();
  if (m_analysis) {
    m_analysis->ensureScanned();
  }
  if (m_sidecar->state() != LlmSidecar::State::READY) {
    if (m_sidecar->state() != LlmSidecar::State::STARTING) {
      m_sidecar->start();
    }
    append(QStringLiteral("<span style=\"color:#64748b\">%1</span>").arg(tr("Starting the local model, the question will be sent when it is ready…")));
  }
  updateButtons();
  dispatch();
}

void AssistantWidget::dispatch() {
  if (m_queued.isEmpty() || (m_sidecar->state() != LlmSidecar::State::READY)) {
    return;
  }
  if (m_client->busy()) {
    QTimer::singleShot(300, this, &AssistantWidget::dispatch); // the warm-up is still running
    return;
  }
  if (m_analysis && m_analysis->library()->isScanning()) {
    QTimer::singleShot(500, this, &AssistantWidget::dispatch); // wait for the experiment list
    return;
  }
  QString const     question = m_queued;
  QString const     focus    = m_queuedFocus.isEmpty() ? focusRun() : m_queuedFocus;
  QJsonObject const view     = std::exchange(m_queuedView, QJsonObject{});
  m_queued.clear();
  m_engine->ask(question, focus, view);
}

void AssistantWidget::onStateChanged(LlmSidecar::State state, QString const &message) {
  QString color;
  switch (state) {
    case LlmSidecar::State::READY:
      if (m_queued.isEmpty()) {
        m_engine->warmUp();
      }
      color = QStringLiteral("#16a34a");
      break;
    case LlmSidecar::State::STARTING:
      color = QStringLiteral("#d97706");
      break;
    case LlmSidecar::State::FAILED:
      color = QStringLiteral("#dc2626");
      if (!m_queued.isEmpty()) {
        m_queued.clear();
        append(QStringLiteral("<span style=\"color:#dc2626\">%1</span>").arg(message.toHtmlEscaped()));
      }
      break;
    default:
      color = QStringLiteral("#94a3b8");
      m_queued.clear();
      break;
  }
  m_statusDot->setStyleSheet(QStringLiteral("color: %1; font-size: 14pt;").arg(color));
  m_statusDot->setToolTip(tr("Local model: %1").arg(message));
  updateButtons();
  dispatch();
}

void AssistantWidget::updateButtons() {
  bool const working{m_engine->busy() || !m_queued.isEmpty()};
  m_sendBtn->setEnabled(!working);
  m_stopBtn->setEnabled(working);
}

void AssistantWidget::setStatus(QString const &text) {
  if (m_statusBlock >= 0) {
    m_blocks[m_statusBlock] = muted(text);
    render();
    return;
  }
  m_blocks.push_back(muted(text));
  m_statusBlock = static_cast<int>(m_blocks.size()) - 1;
  render();
}

void AssistantWidget::clearNotes() {
  // the later block first, so the other index stays valid
  for (int *note : (m_checkBlock > m_statusBlock) ? std::array<int *, 2>{&m_checkBlock, &m_statusBlock}
                                                  : std::array<int *, 2>{&m_statusBlock, &m_checkBlock}) {
    if ((*note >= 0) && (*note < m_blocks.size())) {
      m_blocks.removeAt(*note);
      if ((m_liveBlock > *note)) {
        --m_liveBlock;
      }
    }
    *note = -1;
  }
  render();
}

void AssistantWidget::append(QString const &markdown) {
  m_blocks.push_back(markdown);
  render();
}

void AssistantWidget::render() {
  m_transcript->setMarkdown(m_blocks.join(QStringLiteral("\n\n")));
  m_transcript->verticalScrollBar()->setValue(m_transcript->verticalScrollBar()->maximum());
}

} // namespace deepvaccontrol::assistant
