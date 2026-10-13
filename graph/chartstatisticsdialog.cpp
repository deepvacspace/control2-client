#include "chartstatisticsdialog.h"

#include <QComboBox>
#include <QDateTime>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMap>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <cmath>

#include "analysisnotesstore.h"
#include "annotationdialog.h"
#include "comparisonpanel.h"
#include "experimentinput.h"
#include "runrecord.h"
#include "dataviewer.h"
#include "dialogs/messagedialog.h"
#include "experimentlibrary.h"
#include "resholder.h"
#include "runanalysis.h"
#include "seriesstats.h"
#include "lineeditscreen.h"
#include "theme.h"
#include "touchsupport.h"
#include "utils.h"

namespace deepvaccontrol {

namespace {
int px(double value) {
  return qMax(1, static_cast<int>(value * utils::ssf()));
}

QString statText(double value) {
  if (!std::isfinite(value)) {
    return QStringLiteral("—");
  }
  double const absValue{std::fabs(value)};
  if ((absValue != 0.0) && ((absValue < 1e-3) || (absValue >= 1e6))) {
    return QString::number(value, 'e', 2);
  }
  return QString::number(value, 'f', absValue >= 100 ? 1 : 3);
}

QString secondsText(double seconds) {
  return std::isfinite(seconds) ? ExperimentLibrary::formatDuration(seconds) : QStringLiteral("—");
}

QString clockText(double epochSec) {
  return QDateTime::fromSecsSinceEpoch(static_cast<qint64>(std::llround(epochSec))).toString("HH:mm:ss");
}

QString stageName(core::Segment::Kind kind) {
  switch (kind) {
    case core::Segment::Kind::RAMP:
      return QObject::tr("Ramp");
    case core::Segment::Kind::SETPOINT:
      return QObject::tr("Setpoint");
    case core::Segment::Kind::HOLD:
      return QObject::tr("Hold");
    default:
      return QObject::tr("Unknown");
  }
}

bool parseNumber(QString text, double &value) {
  text = text.trimmed().replace(',', '.');
  if (text.isEmpty()) {
    value = qQNaN();
    return true;
  }
  bool ok{false};
  value = text.toDouble(&ok);
  return ok;
}

// numbers right-aligned in their columns, whatever fills them
class AlignDelegate : public QStyledItemDelegate {
public:
  AlignDelegate(QSet<int> right, QObject *parent) : QStyledItemDelegate{parent}, m_right{std::move(right)} {}

protected:
  void initStyleOption(QStyleOptionViewItem *option, QModelIndex const &index) const override {
    QStyledItemDelegate::initStyleOption(option, index);
    if (m_right.contains(index.column())) {
      option->displayAlignment = Qt::AlignRight | Qt::AlignVCenter;
    }
  }

private:
  QSet<int> m_right;
};

QTableWidget *makeTable(QStringList const &headers, int stretchColumn, QWidget *parent, QSet<int> const &numeric = {}) {
  auto *table = new QTableWidget(0, static_cast<int>(headers.size()), parent);
  table->setObjectName("statsTable");
  QStringList upper;
  for (QString const &header : headers) {
    upper << header.toUpper();
  }
  table->setHorizontalHeaderLabels(upper);
  for (int const column : numeric) {
    if (QTableWidgetItem *header = table->horizontalHeaderItem(column)) {
      header->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    }
  }
  table->setItemDelegate(new AlignDelegate(numeric, table));
  // small headings, a little spaced
  QFont heading = table->horizontalHeader()->font();
  heading.setPointSizeF(heading.pointSizeF() * 0.82);
  heading.setBold(true);
  heading.setLetterSpacing(QFont::PercentageSpacing, 104);
  table->horizontalHeader()->setFont(heading);
  table->verticalHeader()->setVisible(false);
  table->verticalHeader()->setDefaultSectionSize(px(34));
  table->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  table->horizontalHeader()->setHighlightSections(false);
  table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
  table->horizontalHeader()->setSectionResizeMode(stretchColumn, QHeaderView::Stretch);
  table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table->setSelectionBehavior(QAbstractItemView::SelectRows);
  table->setSelectionMode(QAbstractItemView::SingleSelection);
  table->setShowGrid(false);
  table->setWordWrap(false);
  touch::enableScrolling(table);
  return table;
}

QPushButton *makeButton(QString const &text, QWidget *parent) {
  auto *button = new QPushButton(text, parent);
  button->setMinimumSize(px(110), px(32));
  button->setFocusPolicy(Qt::NoFocus);
  return button;
}
} // namespace

ChartStatisticsDialog::ChartStatisticsDialog(ResHolder *resHolder, AnalysisNotesStore *notes, QWidget *parent)
    : QDialog{parent}, m_resHolder{resHolder}, m_notes{notes}, m_rangeTimer{new QTimer(this)} {
  setWindowTitle(tr("Statistics"));
  setModal(false); // the chart can still be zoomed and panned: the statistics follow it
  auto *lay = new QVBoxLayout(this);
  lay->setContentsMargins(px(16), px(12), px(16), px(14));
  lay->setSpacing(px(10));
  auto *card = new QFrame(this);
  card->setObjectName("statsCard");
  auto *cardLay = new QVBoxLayout(card);
  cardLay->setContentsMargins(px(16), px(10), px(16), px(14));

  m_tabs = new QTabWidget(card);
  theme::setFlatTabs(m_tabs);
  m_tabs->setDocumentMode(false); // the line under the tabs, not above
  m_tabs->tabBar()->setExpanding(false);
  m_rangePage       = buildRangePage();
  m_kpiPage         = buildKpiPage();
  m_anomaliesPage   = buildAnomaliesPage();
  m_eventsPage      = buildEventsPage();
  m_comparison      = new ComparisonPanel(this);
  m_annotationsPage = buildAnnotationsPage();
  m_rulesPage       = buildRulesPage();
  m_tabs->addTab(m_rangePage, tr("Channels")); // the channels in view; the experiment's control: KPIs & stages
  m_tabs->addTab(m_kpiPage, tr("KPIs && stages"));
  m_tabs->addTab(m_anomaliesPage, tr("Anomalies"));
  m_tabs->addTab(m_eventsPage, tr("Events"));
  m_tabs->addTab(m_comparison, tr("Comparison"));
  m_tabs->addTab(m_annotationsPage, tr("Annotations"));
  m_tabs->addTab(m_rulesPage, tr("Variable rules"));
  setComparisonMode(false);
  cardLay->addWidget(m_tabs, 1);
  lay->addWidget(card, 1);

  // the buttons of the selected tab, then Close
  auto *buttons = new QHBoxLayout();
  buttons->setSpacing(px(8));
  m_showBtn             = makeButton(tr("Show on chart"), this);
  m_editBtn             = makeButton(tr("Edit…"), this);
  m_removeAnnotationBtn = makeButton(tr("Remove"), this);
  m_removeAnnotationBtn->setProperty("outline", true);
  auto *addRuleBtn = makeButton(tr("Add rule"), this);
  m_removeRuleBtn  = makeButton(tr("Remove"), this);
  m_removeRuleBtn->setProperty("outline", true);
  m_annotationButtons = {m_showBtn, m_editBtn, m_removeAnnotationBtn};
  m_ruleButtons       = {addRuleBtn, m_removeRuleBtn};
  for (QPushButton *button : m_annotationButtons + m_ruleButtons) {
    buttons->addWidget(button);
  }
  buttons->addStretch();
  auto *closeBtn = makeButton(tr("Close"), this);
  closeBtn->setObjectName("primaryButton");
  buttons->addWidget(closeBtn);
  lay->addLayout(buttons);

  connect(closeBtn, &QPushButton::clicked, this, &QDialog::close);
  connect(m_tabs, &QTabWidget::currentChanged, this, &ChartStatisticsDialog::updateButtons);
  connect(m_annotationsTable, &QTableWidget::itemSelectionChanged, this, &ChartStatisticsDialog::updateButtons);
  connect(m_rulesList, &QListWidget::itemSelectionChanged, this, &ChartStatisticsDialog::updateButtons);

  connect(m_showBtn, &QPushButton::clicked, this, [this] {
    int const row{selectedRow(m_annotationsTable)};
    if (row < 0) {
      return;
    }
    double const from{m_annotationsTable->item(row, 2)->data(Qt::UserRole).toDouble()};
    double const to{m_annotationsTable->item(row, 3)->data(Qt::UserRole).toDouble()};
    double const margin{std::max(30.0, 0.15 * (to - from))};
    emit zoomRequested(from - margin, to + margin);
  });
  connect(m_editBtn, &QPushButton::clicked, this, [this] { editAnnotation(selectedRow(m_annotationsTable)); });
  connect(m_annotationsTable, &QTableWidget::cellDoubleClicked, this, [this](int row, int) { editAnnotation(row); });
  connect(m_removeAnnotationBtn, &QPushButton::clicked, this, [this] {
    int const row{selectedRow(m_annotationsTable)};
    if (row < 0) {
      return;
    }
    QString const label = m_annotationsTable->item(row, 1)->text();
    if (QMessageBox::question(this, tr("Remove annotation"), tr("Remove the annotation \"%1\"?").arg(label)) == QMessageBox::Yes) {
      m_notes->removeAnnotation(m_annotationsTable->item(row, 0)->data(Qt::UserRole).toString());
    }
  });
  connect(addRuleBtn, &QPushButton::clicked, this, &ChartStatisticsDialog::addRule);
  connect(m_removeRuleBtn, &QPushButton::clicked, this, [this] {
    if (auto *item = m_rulesList->currentItem()) {
      m_notes->removeRule(m_runKey, item->data(Qt::UserRole).toString());
    }
  });

  connect(m_notes, &AnalysisNotesStore::annotationsChanged, this, &ChartStatisticsDialog::refreshAnnotations);
  connect(m_notes, &AnalysisNotesStore::rulesChanged, this, &ChartStatisticsDialog::refreshRules);
  m_rangeTimer->setSingleShot(true);
  m_rangeTimer->setInterval(250);
  connect(m_rangeTimer, &QTimer::timeout, this, &ChartStatisticsDialog::refreshRange);

  updateButtons();
  resize(px(860), px(520));
}

QWidget *ChartStatisticsDialog::buildRangePage() {
  auto *page = new QWidget(this);
  auto *lay  = new QVBoxLayout(page);
  lay->setContentsMargins(0, px(8), 0, 0);
  lay->setSpacing(px(8));
  m_rangeLabel = new QLabel(page);
  m_rangeLabel->setProperty("muted", true);
  m_rangeTable = makeTable({tr("Channel"), tr("n"), tr("Missing"), tr("Min"), tr("Max"), tr("Mean"), tr("Std"), tr("RMS"),
                            tr("Slope /min"), tr("P95")},
                           0, page, {1, 2, 3, 4, 5, 6, 7, 8, 9});
  lay->addWidget(m_rangeLabel);
  lay->addWidget(m_rangeTable, 1);
  return page;
}

QWidget *ChartStatisticsDialog::buildAnnotationsPage() {
  auto *page = new QWidget(this);
  auto *lay  = new QVBoxLayout(page);
  lay->setContentsMargins(0, px(8), 0, 0);
  lay->setSpacing(px(8));
  m_annotationsTable = makeTable({QString(), tr("Label"), tr("From"), tr("To"), tr("Duration"), tr("By")}, 1, page);
  lay->addWidget(m_annotationsTable, 1);
  return page;
}

QWidget *ChartStatisticsDialog::buildRulesPage() {
  auto *page = new QWidget(this);
  auto *lay  = new QVBoxLayout(page);
  lay->setContentsMargins(0, px(8), 0, 0);
  lay->setSpacing(px(8));

  m_ruleChannelCombo = new QComboBox(page);
  m_ruleChannelCombo->setMinimumHeight(px(30));
  m_ruleChannelCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  m_ruleChannelCombo->setMinimumContentsLength(18);
  QStringList const  names  = m_resHolder->states->getNames();
  QVariantList const styles = m_resHolder->settings->settingsMap()["states"].toList();
  for (int i{0}; i < names.size(); ++i) {
    QString const label = (i < styles.size()) ? styles[i].toMap()["label"].toString() : QString{};
    m_ruleChannelCombo->addItem(label.isEmpty() ? names[i] : label, names[i]);
  }
  m_ruleLoEdit    = new LineEditScreen(page, VirtualKeyboard::EN);
  m_ruleHiEdit    = new LineEditScreen(page, VirtualKeyboard::EN);
  m_ruleLabelEdit = new LineEditScreen(page, VirtualKeyboard::EN);
  m_ruleLoEdit->setPlaceholderText(tr("min"));
  m_ruleHiEdit->setPlaceholderText(tr("max"));
  m_ruleLabelEdit->setPlaceholderText(tr("label"));
  for (QLineEdit *edit : {m_ruleLoEdit, m_ruleHiEdit}) {
    edit->setFixedWidth(px(90));
  }
  for (QLineEdit *edit : {m_ruleLoEdit, m_ruleHiEdit, m_ruleLabelEdit}) {
    edit->setMinimumHeight(px(30));
  }
  m_ruleColorCombo = annotationdialog::makeColorCombo(page, px(30));
  auto *form       = new QHBoxLayout();
  form->setSpacing(px(6));
  form->addWidget(m_ruleChannelCombo, 2);
  form->addWidget(m_ruleLoEdit);
  form->addWidget(m_ruleHiEdit);
  form->addWidget(m_ruleLabelEdit, 1);
  form->addWidget(m_ruleColorCombo);
  lay->addLayout(form);

  m_rulesList = new QListWidget(page);
  m_rulesList->setObjectName("statsList");
  touch::enableScrolling(m_rulesList);
  lay->addWidget(m_rulesList, 1);
  return page;
}

ComparisonPanel *ChartStatisticsDialog::comparisonPanel() const {
  return m_comparison;
}

void ChartStatisticsDialog::setComparisonMode(bool comparison) {
  // a comparison: its own tab; one experiment: its analysis, annotations and rules
  m_tabs->setTabVisible(m_tabs->indexOf(m_comparison), comparison);
  for (QWidget *page : {m_kpiPage, m_anomaliesPage, m_eventsPage, m_annotationsPage, m_rulesPage}) {
    m_tabs->setTabVisible(m_tabs->indexOf(page), !comparison);
  }
  updateButtons();
}

QWidget *ChartStatisticsDialog::buildKpiPage() {
  auto *page = new QWidget(this);
  auto *lay  = new QVBoxLayout(page);
  lay->setContentsMargins(0, px(8), 0, 0);
  lay->setSpacing(px(8));
  auto *tiles = new QHBoxLayout();
  tiles->setSpacing(px(16));
  for (QString const &title : {tr("MAE"), tr("RMSE"), tr("Max error"), tr("Overshoot"), tr("Undershoot"), tr("Settling"), tr("Tail MAE")}) {
    auto *tile    = new QWidget(page);
    auto *tileLay = new QVBoxLayout(tile);
    tileLay->setContentsMargins(0, 0, 0, 0);
    tileLay->setSpacing(px(1));
    auto *name = new QLabel(title, tile);
    name->setProperty("muted", true);
    auto *value = new QLabel(QStringLiteral("—"), tile);
    QFont font  = value->font();
    font.setBold(true);
    font.setPointSizeF(font.pointSizeF() * 1.3);
    value->setFont(font);
    tileLay->addWidget(name);
    tileLay->addWidget(value);
    tiles->addWidget(tile, 1);
    m_kpiTiles << value;
  }
  lay->addLayout(tiles);
  m_kpiLabel2 = new QLabel(page);
  m_kpiLabel2->setProperty("muted", true);
  m_kpiLabel2->setWordWrap(true);
  m_stagesTable = makeTable({tr("#"), tr("Stage"), tr("Interval"), tr("Cycle"), tr("Start"), tr("Duration"), tr("Setpoint"), tr("MAE"),
                             tr("Overshoot"), tr("Settling"), tr("Steady-state error"), tr("Steady")},
                            1, page, {2, 3, 6, 7, 8, 9, 10, 11});
  m_stagesTable->setToolTip(tr("Tap a stage to zoom to it"));
  connect(m_stagesTable, &QTableWidget::cellClicked, this, [this](int row) {
    if (m_insight.analysis && (row >= 0) && (row < m_insight.analysis->run.stages.size())) {
      core::Segment const &segment = m_insight.analysis->run.stages[row].segment;
      emit zoomRequested(m_insight.start + segment.start, m_insight.start + std::max(segment.end, segment.start + 1.0));
    }
  });
  lay->addWidget(m_stagesTable, 1);
  lay->addWidget(m_kpiLabel2); // the note under the table
  return page;
}

QWidget *ChartStatisticsDialog::buildAnomaliesPage() {
  auto *page = new QWidget(this);
  auto *lay  = new QVBoxLayout(page);
  lay->setContentsMargins(0, px(8), 0, 0);
  lay->setSpacing(px(8));
  m_anomalyLabel = new QLabel(page);
  m_anomalyLabel->setWordWrap(true);
  m_anomalyLabel->setProperty("muted", true);
  m_anomalyTable = makeTable({tr("Severity"), tr("Type"), tr("Channel"), tr("Start"), tr("Duration"), tr("Detail")}, 5, page);
  m_anomalyTable->setToolTip(tr("Tap an anomaly to zoom to it"));
  connect(m_anomalyTable, &QTableWidget::cellClicked, this, [this](int row) {
    if (m_insight.analysis && (row >= 0) && (row < m_insight.analysis->anomalies.size())) {
      core::Anomaly const &anomaly = m_insight.analysis->anomalies[row];
      double const         margin{std::max(60.0, 0.5 * (anomaly.end - anomaly.start))};
      emit zoomRequested(m_insight.start + anomaly.start - margin, m_insight.start + anomaly.end + margin);
    }
  });
  lay->addWidget(m_anomalyLabel);
  lay->addWidget(m_anomalyTable, 1);
  return page;
}

QWidget *ChartStatisticsDialog::buildEventsPage() {
  auto *page = new QWidget(this);
  auto *lay  = new QVBoxLayout(page);
  lay->setContentsMargins(0, px(8), 0, 0);
  lay->setSpacing(px(8));
  m_eventsLabel = new QLabel(page);
  m_eventsLabel->setWordWrap(true);
  m_eventsLabel->setProperty("muted", true);
  m_eventsTable = makeTable({tr("Time"), tr("Event"), tr("Source"), tr("Details")}, 3, page);
  m_eventsTable->setToolTip(tr("Tap an event to zoom to it"));
  connect(m_eventsTable, &QTableWidget::cellClicked, this, [this](int row) {
    if ((row >= 0) && (row < m_insight.events.size())) {
      double const at{m_insight.events[row].time};
      emit zoomRequested(at - 120.0, at + 120.0);
    }
  });
  lay->addWidget(m_eventsLabel);
  lay->addWidget(m_eventsTable, 1);
  return page;
}

void ChartStatisticsDialog::setInsight(RunInsight const &insight, bool computing) {
  m_insight = insight;
  core::ExperimentAnalysis const *a = insight.analysis.get();
  QString const waiting = computing ? tr("Analysing the experiment…") : tr("No data shown.");

  // KPIs and stages
  auto degrees = [](double v) { return std::isfinite(v) ? statText(v) + QStringLiteral(" °C") : QStringLiteral("—"); };
  if (!a || !a->hasControl) {
    for (QLabel *tile : std::as_const(m_kpiTiles)) {
      tile->setText(QStringLiteral("—"));
    }
    m_kpiLabel2->setText(!a ? waiting : tr("No setpoint (temp_ref) was recorded in this experiment, so there are no KPIs or stages."));
    m_stagesTable->setRowCount(0);
  } else {
    core::ControlKpis const &k = a->run.overall;
    QStringList const        values{degrees(k.mae),           degrees(k.rmse),           degrees(k.maxAbsError),
                             degrees(a->run.overshoot), degrees(a->run.undershoot), secondsText(a->run.settlingTimeS),
                             degrees(k.tailMae)};
    for (int i{0}; i < m_kpiTiles.size(); ++i) {
      m_kpiTiles[i]->setText(values.value(i));
    }
    QStringList setpoints;
    for (double const setpoint : a->config.setpoints) {
      setpoints << statText(setpoint);
    }
    m_kpiLabel2->setText(tr("%1 samples · output saturated %2 · P / I / D share %3 · setpoints %4 · %5 anomalies.")
                           .arg(k.samples)
                           .arg(analysistext::percent(a->saturation.fraction), analysistext::pidShares(a->pid),
                                setpoints.isEmpty() ? QStringLiteral("—") : setpoints.join(QStringLiteral(" → ")))
                           .arg(a->anomalies.size()));
    m_stagesTable->setRowCount(static_cast<int>(a->run.stages.size()));
    for (int r{0}; r < a->run.stages.size(); ++r) {
      core::StageAnalysis const &stage = a->run.stages[r];
      core::StageDetail const   &d     = a->stageDetails.value(r);
      core::Segment const       &s     = stage.segment;
      bool const                 step{s.kind == core::Segment::Kind::SETPOINT};
      QString const              setpoint = (std::fabs(s.referenceEnd - s.referenceStart) < 0.05)
                                              ? statText(s.referenceEnd)
                                              : QStringLiteral("%1 → %2").arg(statText(s.referenceStart), statText(s.referenceEnd));
      QStringList const cells{QString::number(r + 1),
                              analysistext::stageName(s.kind),
                              s.interval >= 0 ? QString::number(s.interval) : QStringLiteral("—"),
                              s.cycle >= 0 ? QString::number(s.cycle) : QStringLiteral("—"),
                              clockText(insight.start + s.start),
                              secondsText(s.duration()),
                              setpoint,
                              statText(stage.kpis.mae),
                              step ? statText(stage.kpis.overshoot) : QStringLiteral("—"),
                              step ? secondsText(stage.kpis.settlingTimeS) : QStringLiteral("—"),
                              step ? statText(stage.kpis.steadyStateError) : QStringLiteral("—"),
                              analysistext::percent(d.steadyFraction)};
      for (int c{0}; c < cells.size(); ++c) {
        auto *item = new QTableWidgetItem(cells[c]);
        if (c == 1) {
          item->setForeground(analysistext::stageColor(s.kind));
        }
        m_stagesTable->setItem(r, c, item);
      }
    }
  }

  // anomalies
  QVector<core::Anomaly> const anomalies = a ? a->anomalies : QVector<core::Anomaly>{};
  m_anomalyLabel->setText(!a                    ? waiting
                          : anomalies.isEmpty() ? tr("No anomalies detected (variable rules, spikes, flat signals, missing data, oscillation, "
                                                     "setpoints not reached, long output saturation).")
                                                : tr("%1 anomalies: %2 critical, %3 warnings.")
                                                    .arg(anomalies.size())
                                                    .arg(a->anomalyCount(core::Anomaly::Severity::CRITICAL))
                                                    .arg(a->anomalyCount(core::Anomaly::Severity::WARNING)
                                                         - a->anomalyCount(core::Anomaly::Severity::CRITICAL)));
  m_anomalyTable->setRowCount(static_cast<int>(anomalies.size()));
  for (int r{0}; r < anomalies.size(); ++r) {
    core::Anomaly const &anomaly = anomalies[r];
    QStringList const    cells{analysistext::severityName(anomaly.severity), analysistext::anomalyName(anomaly.kind), anomaly.channel,
                            clockText(insight.start + anomaly.start), secondsText(anomaly.end - anomaly.start), anomaly.detail};
    for (int c{0}; c < cells.size(); ++c) {
      auto *item = new QTableWidgetItem(cells[c]);
      if (c == 0) {
        item->setForeground(analysistext::severityColor(anomaly.severity));
      }
      m_anomalyTable->setItem(r, c, item);
    }
  }

  // events
  m_eventsLabel->setText(!a ? waiting
                         : insight.eventsRecorded
                           ? tr("%1 events recorded with the experiment: captured live (job, operator, PLC alarms, PID table writes) and "
                                "from the recorded signals.")
                               .arg(insight.events.size())
                           : tr("%1 events found in the recorded signals (setpoint, gains, PID band, status, program, alarm bits).")
                               .arg(insight.events.size()));
  m_eventsTable->setRowCount(static_cast<int>(insight.events.size()));
  for (int r{0}; r < insight.events.size(); ++r) {
    core::RunEvent const &event = insight.events[r];
    QStringList const     cells{clockText(event.time), runrecord::eventTypeName(event.type),
                            event.source == QLatin1String("app") ? tr("live") : tr("signal"), event.message};
    for (int c{0}; c < cells.size(); ++c) {
      auto *item = new QTableWidgetItem(cells[c]);
      if (c == 1) {
        item->setForeground(runrecord::eventColor(event.type));
      }
      m_eventsTable->setItem(r, c, item);
    }
  }
}

void ChartStatisticsDialog::setSource(DataViewer *viewer, QString const &runKey, QString const &title, QStringList const &channelNames) {
  m_channelNames = channelNames;
  if (m_viewer) {
    disconnect(m_viewer->getPlot()->xAxis, nullptr, m_rangeTimer, nullptr);
  }
  m_viewer = viewer;
  m_runKey = runKey;
  Q_UNUSED(title)
  setWindowTitle(tr("Statistics"));
  if (m_viewer) {
    connect(m_viewer->getPlot()->xAxis, qOverload<QCPRange const &>(&QCPAxis::rangeChanged), m_rangeTimer,
            qOverload<>(&QTimer::start));
  }
  refreshRange();
  refreshAnnotations();
  refreshRules();
}

void ChartStatisticsDialog::updateButtons() {
  if (!m_showBtn) {
    return; // still being built
  }
  QWidget const *tab = m_tabs->currentWidget();
  for (QPushButton *button : std::as_const(m_annotationButtons)) {
    button->setVisible(tab == m_annotationsPage);
  }
  for (QPushButton *button : std::as_const(m_ruleButtons)) {
    button->setVisible(tab == m_rulesPage);
  }
  bool const annotation{selectedRow(m_annotationsTable) >= 0};
  m_showBtn->setEnabled(annotation);
  m_editBtn->setEnabled(annotation);
  m_removeAnnotationBtn->setEnabled(annotation);
  m_removeRuleBtn->setEnabled(m_rulesList->currentItem() != nullptr);
  m_ruleButtons.first()->setEnabled(!m_runKey.isEmpty()); // add: not for a comparison
}

int ChartStatisticsDialog::selectedRow(QTableWidget *table) const {
  QList<QTableWidgetItem *> const items = table->selectedItems();
  return items.isEmpty() ? -1 : items.first()->row();
}

void ChartStatisticsDialog::refreshRange() {
  auto const data = m_viewer ? m_viewer->getData() : nullptr;
  QVector<double> const times = data ? data->getTimes() : QVector<double>{};
  if (times.isEmpty()) {
    m_rangeLabel->setText(tr("No data shown."));
    m_rangeTable->setRowCount(0);
    return;
  }
  QCPRange const view = m_viewer->getPlot()->xAxis->range();
  double const   from{view.lower};
  double const   to{view.upper};
  m_rangeLabel->setText(tr("Visible range %1 – %2 (raw samples, before smoothing)").arg(clockText(from), clockText(to)));

  // the visible channels
  QVector<QVector<double>> const states = data->getStates();
  QStringList const              names  = m_channelNames;
  QCustomPlot *const             plot   = m_viewer->getPlot();
  m_rangeTable->setRowCount(0);
  for (int i{0}; (i < states.size()) && (i < plot->graphCount()); ++i) {
    if (!plot->graph(i)->visible()) {
      continue;
    }
    QString const label = m_viewer->channelLabel(i).isEmpty() ? names.value(i) : m_viewer->channelLabel(i);
    core::SeriesStats const stats = core::computeStats(times, states[i], from, to);
    QStringList const cells{label, QString::number(stats.n), QString::number(stats.missing), statText(stats.min), statText(stats.max),
                            statText(stats.mean), statText(stats.std), statText(stats.rms), statText(stats.slopePerS * 60.0),
                            statText(stats.p95)};
    int const row{m_rangeTable->rowCount()};
    m_rangeTable->insertRow(row);
    for (int c{0}; c < cells.size(); ++c) {
      auto *item = new QTableWidgetItem(cells[c]);
      item->setTextAlignment(c == 0 ? (Qt::AlignLeft | Qt::AlignVCenter) : (Qt::AlignRight | Qt::AlignVCenter));
      m_rangeTable->setItem(row, c, item);
    }
  }
}

void ChartStatisticsDialog::refreshAnnotations() {
  auto const data = m_viewer ? m_viewer->getData() : nullptr;
  QVector<double> const times = data ? data->getTimes() : QVector<double>{};
  m_annotationsTable->setRowCount(0);
  if (times.isEmpty() || m_runKey.isEmpty()) { // a comparison has no annotations
    updateButtons();
    return;
  }
  double const end{std::max(times.last(), static_cast<double>(QDateTime::currentSecsSinceEpoch()))};
  for (auto const &annotation : m_notes->annotations(times.first(), end, m_runKey)) {
    int const row{m_annotationsTable->rowCount()};
    m_annotationsTable->insertRow(row);
    QPixmap swatch(px(12), px(12));
    swatch.fill(annotation.color);
    auto *colorItem = new QTableWidgetItem(QIcon(swatch), QString{});
    colorItem->setData(Qt::UserRole, annotation.id);
    m_annotationsTable->setItem(row, 0, colorItem);
    m_annotationsTable->setItem(row, 1, new QTableWidgetItem(annotation.label));
    auto *fromItem = new QTableWidgetItem(clockText(annotation.start));
    fromItem->setData(Qt::UserRole, annotation.start);
    auto *toItem = new QTableWidgetItem(clockText(annotation.end));
    toItem->setData(Qt::UserRole, annotation.end);
    m_annotationsTable->setItem(row, 2, fromItem);
    m_annotationsTable->setItem(row, 3, toItem);
    m_annotationsTable->setItem(row, 4, new QTableWidgetItem(secondsText(annotation.end - annotation.start)));
    m_annotationsTable->setItem(row, 5, new QTableWidgetItem(annotation.user));
  }
  updateButtons();
}

void ChartStatisticsDialog::editAnnotation(int row) {
  if ((row < 0) || (row >= m_annotationsTable->rowCount())) {
    return;
  }
  QString const id    = m_annotationsTable->item(row, 0)->data(Qt::UserRole).toString();
  double const  start{m_annotationsTable->item(row, 2)->data(Qt::UserRole).toDouble()};
  double const  end{m_annotationsTable->item(row, 3)->data(Qt::UserRole).toDouble()};
  for (auto const &annotation : m_notes->annotations(start, end, m_runKey)) {
    if (annotation.id != id) {
      continue;
    }
    QString   label = annotation.label;
    QColor    color = annotation.color;
    QDateTime from  = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(std::llround(annotation.start)));
    QDateTime to    = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(std::llround(annotation.end)));
    if (!annotationdialog::edit(this, label, color, from, to)) {
      return;
    }
    AnalysisNotesStore::Annotation changed = annotation;
    changed.label = label.isEmpty() ? annotation.label : label;
    changed.color = color;
    changed.start = static_cast<double>(from.toSecsSinceEpoch());
    changed.end   = static_cast<double>(to.toSecsSinceEpoch());
    changed.user  = m_resHolder->users->getCurrentUserName();
    m_notes->updateAnnotation(id, changed);
    return;
  }
}

void ChartStatisticsDialog::refreshRules() {
  m_rulesList->clear();
  if (m_runKey.isEmpty()) {
    updateButtons();
    return;
  }
  for (auto const &rule : m_notes->rules(m_runKey)) {
    int const     index   = m_ruleChannelCombo->findData(rule.channel);
    QString const channel = (index >= 0) ? m_ruleChannelCombo->itemText(index) : rule.channel;
    QString const limits  = QStringLiteral("%1 … %2").arg(std::isnan(rule.lo) ? QStringLiteral("−∞") : statText(rule.lo),
                                                         std::isnan(rule.hi) ? QStringLiteral("+∞") : statText(rule.hi));
    QPixmap swatch(px(12), px(12));
    swatch.fill(rule.color);
    auto *item = new QListWidgetItem(QIcon(swatch), rule.label.isEmpty() ? QStringLiteral("%1: %2").arg(channel, limits)
                                                                         : QStringLiteral("%1 — %2: %3").arg(rule.label, channel, limits));
    item->setData(Qt::UserRole, rule.id);
    m_rulesList->addItem(item);
  }
  updateButtons();
}

void ChartStatisticsDialog::addRule() {
  QString const channel = m_ruleChannelCombo->currentData().toString();
  double        lo{qQNaN()};
  double        hi{qQNaN()};
  if (channel.isEmpty() || m_runKey.isEmpty() || !parseNumber(m_ruleLoEdit->text(), lo) || !parseNumber(m_ruleHiEdit->text(), hi)
      || (std::isnan(lo) && std::isnan(hi))) {
    (new MessageDialog(tr("Variable rule"), tr("Enter a numeric min and/or max value")))->open();
    return;
  }
  AnalysisNotesStore::Rule rule;
  rule.channel = channel;
  rule.lo      = lo;
  rule.hi      = hi;
  rule.label   = m_ruleLabelEdit->text().trimmed();
  rule.color   = QColor(m_ruleColorCombo->currentData().toString());
  rule.user    = m_resHolder->users->getCurrentUserName();
  m_notes->addRule(m_runKey, rule);
  m_ruleLoEdit->clear();
  m_ruleHiEdit->clear();
  m_ruleLabelEdit->clear();
}

} // namespace deepvaccontrol
