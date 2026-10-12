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
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <cmath>

#include "analysisnotesstore.h"
#include "annotationdialog.h"
#include "dataviewer.h"
#include "dialogs/messagedialog.h"
#include "experimentlibrary.h"
#include "resholder.h"
#include "runanalysis.h"
#include "seriesstats.h"
#include "theme.h"
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

QTableWidget *makeTable(QStringList const &headers, int stretchColumn, QWidget *parent) {
  auto *table = new QTableWidget(0, static_cast<int>(headers.size()), parent);
  table->setObjectName("flatTable");
  QStringList upper;
  for (QString const &header : headers) {
    upper << header.toUpper();
  }
  table->setHorizontalHeaderLabels(upper);
  table->verticalHeader()->setVisible(false);
  table->verticalHeader()->setDefaultSectionSize(px(28));
  table->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  table->horizontalHeader()->setHighlightSections(false);
  table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
  table->horizontalHeader()->setSectionResizeMode(stretchColumn, QHeaderView::Stretch);
  table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table->setSelectionBehavior(QAbstractItemView::SelectRows);
  table->setSelectionMode(QAbstractItemView::SingleSelection);
  table->setShowGrid(false);
  table->setWordWrap(false);
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

  m_tabs = new QTabWidget(this);
  theme::setFlatTabs(m_tabs);
  m_tabs->setDocumentMode(true);
  m_tabs->tabBar()->setExpanding(false);
  m_tabs->addTab(buildRangePage(), tr("Selected range"));
  m_tabs->addTab(buildAnnotationsPage(), tr("Annotations"));
  m_tabs->addTab(buildRulesPage(), tr("Variable rules"));
  lay->addWidget(m_tabs, 1);

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
                           0, page);
  m_kpiLabel = new QLabel(page);
  m_kpiLabel->setWordWrap(true);
  m_kpiLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  lay->addWidget(m_rangeLabel);
  lay->addWidget(m_rangeTable, 1);
  lay->addWidget(m_kpiLabel);
  return page;
}

QWidget *ChartStatisticsDialog::buildAnnotationsPage() {
  auto *page = new QWidget(this);
  auto *lay  = new QVBoxLayout(page);
  lay->setContentsMargins(0, px(8), 0, 0);
  lay->setSpacing(px(8));
  auto *hint = new QLabel(tr("Annotations of the time span shown. To add one, press Annotate and drag across the chart."), page);
  hint->setProperty("muted", true);
  hint->setWordWrap(true);
  m_annotationsTable = makeTable({QString(), tr("Label"), tr("From"), tr("To"), tr("Duration"), tr("By")}, 1, page);
  lay->addWidget(hint);
  lay->addWidget(m_annotationsTable, 1);
  return page;
}

QWidget *ChartStatisticsDialog::buildRulesPage() {
  auto *page = new QWidget(this);
  auto *lay  = new QVBoxLayout(page);
  lay->setContentsMargins(0, px(8), 0, 0);
  lay->setSpacing(px(8));
  auto *hint = new QLabel(tr("Acceptable range of a channel, drawn as a band on the chart (a single limit when min or max is empty)."), page);
  hint->setProperty("muted", true);
  hint->setWordWrap(true);
  lay->addWidget(hint);

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
  m_ruleLoEdit    = new QLineEdit(page);
  m_ruleHiEdit    = new QLineEdit(page);
  m_ruleLabelEdit = new QLineEdit(page);
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
  lay->addWidget(m_rulesList, 1);
  return page;
}

void ChartStatisticsDialog::setSource(DataViewer *viewer, QString const &runKey, QString const &title, QStringList const &channelNames) {
  m_channelNames = channelNames;
  if (m_viewer) {
    disconnect(m_viewer->getPlot()->xAxis, nullptr, m_rangeTimer, nullptr);
  }
  m_viewer = viewer;
  m_runKey = runKey;
  setWindowTitle(tr("Statistics: %1").arg(title));
  if (m_viewer) {
    connect(m_viewer->getPlot()->xAxis, qOverload<QCPRange const &>(&QCPAxis::rangeChanged), m_rangeTimer,
            qOverload<>(&QTimer::start));
  }
  refreshRange();
  refreshAnnotations();
  refreshRules();
}

void ChartStatisticsDialog::updateButtons() {
  int const tab{m_tabs->currentIndex()};
  for (QPushButton *button : std::as_const(m_annotationButtons)) {
    button->setVisible(tab == 1);
  }
  for (QPushButton *button : std::as_const(m_ruleButtons)) {
    button->setVisible(tab == 2);
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
    m_kpiLabel->clear();
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

  // control KPIs of the visible slice (temp against temp_ref)
  int const temp{static_cast<int>(names.indexOf("temp"))};
  int const reference{static_cast<int>(names.indexOf("temp_ref"))};
  if (names.isEmpty()) {
    m_kpiLabel->setText(tr("Control KPIs are given for one experiment at a time."));
    return;
  }
  if ((temp < 0) || (reference < 0) || (temp >= states.size()) || (reference >= states.size())) {
    m_kpiLabel->setText(tr("Control KPIs need the channels temp and temp_ref."));
    return;
  }
  int const       interval{static_cast<int>(names.indexOf("interval"))};
  int const       cycle{static_cast<int>(names.indexOf("cycle_cur"))};
  QVector<double> sliceTimes;
  QVector<double> slices[4];
  for (int i{0}; i < times.size(); ++i) {
    if ((times[i] < from) || (times[i] > to)) {
      continue;
    }
    sliceTimes.push_back(times[i] - from); // elapsed s
    int const columns[4]{temp, reference, interval, cycle};
    for (int c{0}; c < 4; ++c) {
      bool const have{(columns[c] >= 0) && (columns[c] < states.size()) && (i < states[columns[c]].size())};
      slices[c].push_back(have ? states[columns[c]][i] : core::NaN);
    }
  }
  if (interval < 0) {
    slices[2].clear();
  }
  if (cycle < 0) {
    slices[3].clear();
  }
  core::RunAnalysis const analysis = core::analyseRun(sliceTimes, slices[0], slices[1], slices[2], slices[3]);
  if (analysis.overall.samples == 0) {
    m_kpiLabel->setText(tr("<b>Control in range</b>: no setpoint (temp_ref) in this range."));
    return;
  }
  QMap<core::Segment::Kind, int> stageCount;
  for (core::StageAnalysis const &stage : analysis.stages) {
    ++stageCount[stage.segment.kind];
  }
  QStringList stages;
  for (auto it = stageCount.cbegin(); it != stageCount.cend(); ++it) {
    stages << QStringLiteral("%1 ×%2").arg(stageName(it.key())).arg(it.value());
  }
  core::ControlKpis const &k = analysis.overall;
  m_kpiLabel->setText(tr("<b>Control in range</b> (temp vs temp_ref, %1 samples): MAE %2 · RMSE %3 · max |error| %4 · overshoot %5 · "
                         "undershoot %6 · settling %7 · steady-state error %8 · tail MAE %9 · tail std %10 · stages: %11")
                        .arg(k.samples)
                        .arg(statText(k.mae), statText(k.rmse), statText(k.maxAbsError), statText(analysis.overshoot),
                             statText(analysis.undershoot), secondsText(analysis.settlingTimeS), statText(k.steadyStateError),
                             statText(k.tailMae), statText(k.tailStd))
                        .arg(stages.join(QStringLiteral(", "))));
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
