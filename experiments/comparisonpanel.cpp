#include "comparisonpanel.h"

#include <QSignalBlocker>

#include <QComboBox>
#include <QDateTime>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <cmath>

#include "experimentinput.h"
#include "runmanifest.h"
#include "runrecord.h"
#include <functional>
#include "theme.h"
#include "utils.h"

namespace deepvaccontrol {

namespace {
using namespace analysistext;

QTableWidget *makeTable(QWidget *parent) {
  auto *table = new QTableWidget(parent);
  table->setObjectName("statsTable");
  QFont heading = table->horizontalHeader()->font();
  heading.setPointSizeF(heading.pointSizeF() * 0.82);
  heading.setBold(true);
  heading.setLetterSpacing(QFont::PercentageSpacing, 104);
  table->horizontalHeader()->setFont(heading);
  table->horizontalHeader()->setHighlightSections(false);
  table->verticalHeader()->setDefaultSectionSize(static_cast<int>(34 * utils::ssf()));
  table->setShowGrid(false);
  table->setWordWrap(false);
  table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
  table->verticalHeader()->setVisible(false);
  table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table->setSelectionBehavior(QAbstractItemView::SelectRows);
  table->setMinimumHeight(90 * utils::ssf());
  return table;
}

void setHeader(QTableWidget *table, QStringList const &headers, int rows) {
  table->clear();
  table->setColumnCount(static_cast<int>(headers.size()));
  QStringList upper;
  for (QString const &header : headers) {
    upper << header.toUpper();
  }
  table->setHorizontalHeaderLabels(upper);
  table->setRowCount(rows);
}

QTableWidgetItem *cell(QString const &text, bool left = false) {
  auto *item = new QTableWidgetItem(text);
  item->setTextAlignment(left ? (Qt::AlignLeft | Qt::AlignVCenter) : (Qt::AlignRight | Qt::AlignVCenter));
  return item;
}

QString kpiValue(double value, QString const &unit, QString const &key) {
  if (unit == QLatin1String("s")) {
    return seconds(value);
  }
  if ((key == QLatin1String("anomalies")) && std::isfinite(value)) {
    return QString::number(static_cast<qint64>(value));
  }
  return number(value, 3);
}

// differences below half a percent of the reference value are no change
bool negligible(double delta, double reference) {
  return std::fabs(delta) <= std::max(1e-9, 0.005 * std::fabs(reference));
}

// colour of a difference against the reference run: green better, red worse
QColor deltaColor(double delta, double reference, core::Better better) {
  if (!std::isfinite(delta) || negligible(delta, reference) || (better == core::Better::NONE)) {
    return {};
  }
  bool const improved{(better == core::Better::LOWER) ? (delta < 0.0) : (delta > 0.0)};
  return improved ? QColor("#16a34a") : QColor("#dc2626");
}

QString deltaText(double delta, double reference, QString const &unit, QString const &key) {
  if (!std::isfinite(delta)) {
    return {};
  }
  if (negligible(delta, reference)) {
    return QStringLiteral("=");
  }
  QString const sign = delta > 0.0 ? QStringLiteral("+") : QString();
  if (key == QLatin1String("anomalies")) {
    return sign + QString::number(static_cast<qint64>(delta));
  }
  return unit == QLatin1String("s") ? QStringLiteral("%1%2 s").arg(sign, number(delta, 0)) : QStringLiteral("%1%2").arg(sign, number(delta, 3));
}

QString htmlTable(QTableWidget const *table) {
  QString html = QStringLiteral("<table class=\"grid\"><tr>");
  for (int c{0}; c < table->columnCount(); ++c) {
    html += QStringLiteral("<th>%1</th>").arg(table->horizontalHeaderItem(c) ? table->horizontalHeaderItem(c)->text().toHtmlEscaped() : QString());
  }
  html += QStringLiteral("</tr>");
  for (int r{0}; r < table->rowCount(); ++r) {
    html += QStringLiteral("<tr>");
    for (int c{0}; c < table->columnCount(); ++c) {
      QTableWidgetItem const *item  = table->item(r, c);
      QString const           text  = item ? item->text().toHtmlEscaped() : QString();
      QColor const            color = item ? item->foreground().color() : QColor();
      bool const              bold{item && item->font().bold()};
      QString                 style;
      if (item && (item->foreground().style() != Qt::NoBrush)) {
        style += QStringLiteral("color:%1;").arg(color.name());
      }
      if (bold) {
        style += QStringLiteral("font-weight:bold;");
      }
      if (item && (item->background().style() != Qt::NoBrush)) {
        style += QStringLiteral("background:%1;").arg(item->background().color().name());
      }
      html += QStringLiteral("<td style=\"%1\">%2</td>").arg(style, text.isEmpty() ? QStringLiteral("&nbsp;") : text);
    }
    html += QStringLiteral("</tr>");
  }
  return html + QStringLiteral("</table>");
}
}

ComparisonPanel::ComparisonPanel(QWidget *parent) : QWidget{parent} {
  auto *root = new QVBoxLayout(this);
  root->setContentsMargins(0, 4 * utils::ssf(), 0, 0);
  m_stack = new QStackedWidget(this);
  root->addWidget(m_stack);

  auto *hint = new QLabel(tr("Compare this experiment with others (Compare ▾) to see the differences in KPIs, configuration, "
                             "step responses and signal behaviour, aligned by stage, setpoint change or target crossing."),
                          m_stack);
  hint->setWordWrap(true);
  hint->setAlignment(Qt::AlignTop | Qt::AlignLeft);
  hint->setStyleSheet("color: #64748b;");
  m_stack->addWidget(hint);

  auto *page = new QWidget(m_stack);
  auto *lay  = new QVBoxLayout(page);
  lay->setContentsMargins(0, 0, 0, 0);
  auto *alignRow = new QHBoxLayout;
  m_alignCombo   = new QComboBox(page);
  m_alignCombo->addItem(tr("Experiment start"), static_cast<int>(core::AlignMode::START));
  m_alignCombo->addItem(tr("Setpoint change #"), static_cast<int>(core::AlignMode::SETPOINT_CHANGE));
  m_alignCombo->addItem(tr("Target crossing after change #"), static_cast<int>(core::AlignMode::TARGET_CROSSING));
  m_alignCombo->addItem(tr("Stage #"), static_cast<int>(core::AlignMode::STAGE));
  m_alignSpin = new QSpinBox(page);
  m_alignSpin->setRange(1, 999);
  m_alignSpin->setEnabled(false);
  m_anchorLabel = new QLabel(page);
  m_anchorLabel->setStyleSheet("color: #64748b;");
  m_anchorLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  alignRow->addWidget(new QLabel(tr("Align on"), page));
  alignRow->addWidget(m_alignCombo);
  alignRow->addWidget(m_alignSpin);
  alignRow->addWidget(m_anchorLabel, 1);
  lay->addLayout(alignRow);
  connect(m_alignCombo, &QComboBox::currentIndexChanged, this, [this] {
    m_alignSpin->setEnabled(m_alignCombo->currentData().toInt() != static_cast<int>(core::AlignMode::START));
    recompute();
    emit alignmentChanged();
  });
  connect(m_alignSpin, &QSpinBox::valueChanged, this, [this] {
    recompute();
    emit alignmentChanged();
  });

  auto *tabs = new QTabWidget(page);
  theme::setFlatTabs(tabs);
  tabs->setDocumentMode(true);
  tabs->tabBar()->setExpanding(false);
  m_kpiTable    = makeTable(tabs);
  m_configTable = makeTable(tabs);
  m_stepsTable  = makeTable(tabs);
  m_signalTable = makeTable(tabs);
  m_kpiTable->setToolTip(tr("Differences against the first experiment: green better, red worse"));
  m_configTable->setToolTip(tr("Settings that differ between the experiments are highlighted"));
  tabs->addTab(m_kpiTable, tr("KPIs"));
  tabs->addTab(m_configTable, tr("Configuration"));
  tabs->addTab(m_stepsTable, tr("Step responses"));
  tabs->addTab(m_signalTable, tr("Signals"));
  lay->addWidget(tabs);
  m_stack->addWidget(page);
}

void ComparisonPanel::setRuns(QVector<ComparedRun> const &runs, QString const &signalLabel) {
  m_runs        = runs;
  m_signalLabel = signalLabel;
  m_stack->setCurrentIndex(m_runs.size() >= 2 ? 1 : 0);
  recompute();
}

QVector<double> ComparisonPanel::offsets() const {
  return m_offsets;
}

void ComparisonPanel::setAlignment(int mode, int n) {
  QSignalBlocker const comboBlocker(m_alignCombo);
  QSignalBlocker const spinBlocker(m_alignSpin);
  m_alignCombo->setCurrentIndex(std::max(0, m_alignCombo->findData(mode)));
  m_alignSpin->setValue(n);
  m_alignSpin->setEnabled(mode != static_cast<int>(core::AlignMode::START));
  recompute();
}

int ComparisonPanel::alignMode() const {
  return m_alignCombo->currentData().toInt();
}

int ComparisonPanel::alignN() const {
  return m_alignSpin->value();
}

QString ComparisonPanel::alignmentText() const {
  auto const mode = static_cast<core::AlignMode>(m_alignCombo->currentData().toInt());
  return mode == core::AlignMode::START ? m_alignCombo->currentText()
                                        : QStringLiteral("%1%2").arg(m_alignCombo->currentText()).arg(m_alignSpin->value());
}

void ComparisonPanel::recompute() {
  m_anchors.clear();
  m_offsets.clear();
  if (m_runs.size() < 2) {
    return;
  }
  auto const mode = static_cast<core::AlignMode>(m_alignCombo->currentData().toInt());
  int const  n{m_alignSpin->value()};
  for (ComparedRun const &run : std::as_const(m_runs)) {
    m_anchors.push_back(run.analysis ? core::alignmentAnchor(*run.analysis, run.times, run.temp, run.reference, mode, n) : core::NaN);
  }
  QStringList anchors;
  for (int k{0}; k < m_runs.size(); ++k) {
    bool const usable{std::isfinite(m_anchors[k]) && std::isfinite(m_anchors[0])};
    m_offsets.push_back(usable ? m_anchors[0] - m_anchors[k] : 0.0);
    anchors << QStringLiteral("%1: %2").arg(m_runs[k].title, std::isfinite(m_anchors[k]) ? seconds(m_anchors[k]) : tr("no anchor"));
  }
  m_anchorLabel->setText(mode == core::AlignMode::START ? QString() : anchors.join(QStringLiteral("  ·  ")));

  fillKpis();
  fillConfiguration();
  fillSteps();
  fillSignals();
}

void ComparisonPanel::fillKpis() {
  QVector<core::ExperimentAnalysis const *> analyses;
  QStringList                               headers{tr("KPI")};
  for (ComparedRun const &run : std::as_const(m_runs)) {
    analyses.push_back(run.analysis);
    headers << run.title;
  }
  QVector<core::KpiComparisonRow> const rows = core::compareKpis(analyses);
  setHeader(m_kpiTable, headers, static_cast<int>(rows.size()));
  for (int r{0}; r < rows.size(); ++r) {
    core::KpiComparisonRow const &row = rows[r];
    QString const                 unit = (row.unit == QLatin1String("s") || row.unit.isEmpty()) ? QString() : QStringLiteral(", %1").arg(row.unit);
    m_kpiTable->setItem(r, 0, cell(tr(row.label.toUtf8().constData()) + unit, true));
    for (int k{0}; k < row.values.size(); ++k) {
      double const delta{k > 0 ? row.values[k] - row.values[0] : core::NaN};
      QString      text = kpiValue(row.values[k], row.unit, row.key);
      if ((k > 0) && std::isfinite(delta)) {
        text += QStringLiteral("  (%1)").arg(deltaText(delta, row.values[0], row.unit, row.key));
      }
      auto        *item  = cell(text);
      QColor const color = deltaColor(delta, row.values[0], row.better);
      if (color.isValid()) {
        item->setForeground(color);
      }
      m_kpiTable->setItem(r, k + 1, item);
    }
  }
}

void ComparisonPanel::fillConfiguration() {
  QStringList                         headers{tr("Setting")};
  QVector<core::RunConfig const *>    configs;
  QVector<core::ConfigComparisonRow>  rows;
  core::ConfigComparisonRow           chamber{tr("Chamber"), {}, false};
  core::ConfigComparisonRow           started{tr("Started"), {}, false};
  for (ComparedRun const &run : std::as_const(m_runs)) {
    headers << run.title;
    configs.push_back(run.analysis ? &run.analysis->config : nullptr);
    chamber.values << (run.run.chamber.isEmpty() ? QStringLiteral("—") : run.run.chamber);
    started.values << QDateTime::fromSecsSinceEpoch(static_cast<qint64>(run.run.start)).toString("yyyy-MM-dd HH:mm");
  }
  chamber.differs = std::any_of(chamber.values.cbegin(), chamber.values.cend(), [&](QString const &v) { return v != chamber.values.first(); });
  started.differs = false; // always different, not a setting
  rows << chamber << started;

  // recorded configuration snapshots (when the runs have them): what was actually sent and set
  auto snapshotRow = [&](QString const &label, std::function<QString(core::RunManifest const &)> value) {
    core::ConfigComparisonRow row{label, {}, false};
    bool                      any{false};
    for (ComparedRun const &run : std::as_const(m_runs)) {
      core::RunManifest const manifest = core::RunManifest::fromJson(run.manifest);
      QString const           text     = manifest.isValid() ? value(manifest) : QString();
      any                              = any || !text.isEmpty();
      row.values << (text.isEmpty() ? QStringLiteral("—") : text);
    }
    row.differs = std::any_of(row.values.cbegin(), row.values.cend(), [&](QString const &v) { return v != row.values.first(); });
    if (any) {
      rows << row;
    }
  };
  snapshotRow(tr("DUT"), [](core::RunManifest const &m) { return m.metadata.dut; });
  snapshotRow(tr("DUT mass"), [](core::RunManifest const &m) { return m.metadata.dutMassText(); });
  snapshotRow(tr("DUT density"), [](core::RunManifest const &m) { return m.metadata.dutDensityText(); });
  snapshotRow(tr("Purpose"), [](core::RunManifest const &m) { return m.metadata.purpose; });
  snapshotRow(tr("Operator"), [](core::RunManifest const &m) { return m.metadata.operatorName; });
  snapshotRow(tr("Job"), [](core::RunManifest const &m) { return m.configuration.contains("job") ? runrecord::jobText(m.configuration) : QString(); });
  snapshotRow(tr("Program"), [](core::RunManifest const &m) {
    return m.configuration.value("job").toObject().contains("program") ? runrecord::programSummary(m.configuration) : QString();
  });
  snapshotRow(tr("PID table (Kp/Ki/Kd per band)"), [](core::RunManifest const &m) {
    return m.configuration.contains("plc_settings") ? runrecord::pidSummary(m.configuration) : QString();
  });
  snapshotRow(tr("Calibration"), [](core::RunManifest const &m) {
    return m.configuration.contains("plc_settings") ? runrecord::calibrationSummary(m.configuration) : QString();
  });
  snapshotRow(tr("Application version"), [](core::RunManifest const &m) { return m.appVersion; });
  rows << core::compareConfigs(configs);

  setHeader(m_configTable, headers, static_cast<int>(rows.size()));
  for (int r{0}; r < rows.size(); ++r) {
    auto *label = cell(tr(rows[r].label.toUtf8().constData()), true);
    m_configTable->setItem(r, 0, label);
    for (int k{0}; k < rows[r].values.size(); ++k) {
      m_configTable->setItem(r, k + 1, cell(rows[r].values[k], true));
    }
    if (rows[r].differs) {
      for (int c{0}; c < m_configTable->columnCount(); ++c) {
        QTableWidgetItem *item = m_configTable->item(r, c);
        QFont             font = item->font();
        font.setBold(true);
        item->setFont(font);
        item->setBackground(QColor("#fef3c7"));
      }
    }
  }
}

void ComparisonPanel::fillSteps() {
  QVector<core::ExperimentAnalysis const *> analyses;
  QStringList                               headers{tr("Step")};
  for (ComparedRun const &run : std::as_const(m_runs)) {
    analyses.push_back(run.analysis);
    headers << run.title;
  }
  QVector<core::StepMatch> const matches = core::matchSteps(analyses);
  setHeader(m_stepsTable, headers, static_cast<int>(matches.size()));
  for (int r{0}; r < matches.size(); ++r) {
    m_stepsTable->setItem(r, 0, cell(QString::number(r + 1), true));
    for (int k{0}; k < matches[r].stages.size(); ++k) {
      int const stageIndex{matches[r].stages[k]};
      if ((stageIndex < 0) || !analyses[k]) {
        m_stepsTable->setItem(r, k + 1, cell(QStringLiteral("—"), true));
        continue;
      }
      core::StageAnalysis const &stage = analyses[k]->run.stages[stageIndex];
      // the step starts from the level the previous stage ended at
      double const               from{stageIndex > 0 ? analyses[k]->run.stages[stageIndex - 1].segment.referenceEnd
                                                     : stage.segment.referenceStart};
      QString const              text  = tr("%1 → %2 · settling %3 · overshoot %4 · MAE %5")
                                .arg(number(from, 2), number(stage.segment.referenceEnd, 2),
                                     seconds(stage.kpis.settlingTimeS), number(stage.kpis.overshoot, 3), number(stage.kpis.mae, 3));
      m_stepsTable->setItem(r, k + 1, cell(text, true));
    }
  }
}

void ComparisonPanel::fillSignals() {
  setHeader(m_signalTable,
            {tr("Experiment"), tr("Overlap"), tr("Mean diff"), tr("RMS diff"), tr("Max |diff|"), tr("Correlation")},
            std::max(0, static_cast<int>(m_runs.size()) - 1));
  m_signalTable->setToolTip(tr("%1 of each experiment minus the first experiment, on the aligned time axis").arg(m_signalLabel));
  for (int k{1}; k < m_runs.size(); ++k) {
    // offsets are added to each run's time: undo them to compare on aligned axes
    core::SignalComparison const diff = core::compareSignals(m_runs[0].times, m_runs[0].signal, -m_offsets.value(0),
                                                             m_runs[k].times, m_runs[k].signal, -m_offsets.value(k));
    QStringList const cells{m_runs[k].title,           seconds(diff.overlapS),       number(diff.meanDiff, 3),
                            number(diff.rmsDiff, 3),   number(diff.maxAbsDiff, 3),   number(diff.correlation, 4)};
    for (int c{0}; c < cells.size(); ++c) {
      m_signalTable->setItem(k - 1, c, cell(cells[c], c == 0));
    }
  }
}

QString ComparisonPanel::html() const {
  if (m_runs.size() < 2) {
    return {};
  }
  return QStringLiteral("<p>%1: <b>%2</b></p><h3>%3</h3>%4<h3>%5</h3>%6<h3>%7</h3>%8<h3>%9</h3><p>%10</p>%11")
    .arg(tr("Aligned on"), alignmentText().toHtmlEscaped(), tr("KPIs (difference against the first experiment)"), htmlTable(m_kpiTable),
         tr("Configuration"), htmlTable(m_configTable), tr("Step responses"), htmlTable(m_stepsTable), tr("Signals"))
    .arg(tr("%1 of each experiment minus the first experiment, on the aligned time axis.").arg(m_signalLabel.toHtmlEscaped()), htmlTable(m_signalTable));
}

}
