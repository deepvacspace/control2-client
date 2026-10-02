#include "analysisrunpage.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <cmath>

#include "analysisnotesstore.h"
#include "constants.h"
#include "flowlayout.h"
#include "dialogs/messagedialog.h"
#include "filesavedialogpathprovider.h"
#include "resholder.h"
#include "utils.h"

namespace deepvaccontrol {

namespace {
struct NamedColor {
  char const *name;
  char const *color;
};

NamedColor const NOTE_COLORS[]{{QT_TRANSLATE_NOOP("AnalysisRunPage", "Green"), "#16a34a"},
                               {QT_TRANSLATE_NOOP("AnalysisRunPage", "Amber"), "#d97706"},
                               {QT_TRANSLATE_NOOP("AnalysisRunPage", "Red"), "#dc2626"},
                               {QT_TRANSLATE_NOOP("AnalysisRunPage", "Blue"), "#2563eb"},
                               {QT_TRANSLATE_NOOP("AnalysisRunPage", "Purple"), "#9333ea"},
                               {QT_TRANSLATE_NOOP("AnalysisRunPage", "Teal"), "#0d9488"}};

QVector<QColor> const COMPARE_COLORS{QColor("#2563eb"),
                                     QColor("#dc2626"),
                                     QColor("#16a34a"),
                                     QColor("#9333ea"),
                                     QColor("#d97706"),
                                     QColor("#0d9488"),
                                     QColor("#db2777"),
                                     QColor("#475569")};

QColor eventColor(int type) {
  switch (type) {
    case 1: // PLC
      return QColor("#d97706");
    case 2: // ERR
      return QColor("#dc2626");
    default: // APP
      return QColor("#64748b");
  }
}

constexpr int MAX_COMPARE_MENU_RUNS{40};

int buttonHeight() {
  return static_cast<int>(40 * utils::ssf());
}

QPushButton *makeButton(QString const &text, QWidget *parent) {
  auto *button = new QPushButton(text, parent);
  button->setMinimumHeight(buttonHeight());
  button->setFocusPolicy(Qt::NoFocus);
  return button;
}

QComboBox *makeColorCombo(QWidget *parent) {
  auto *combo = new QComboBox(parent);
  for (auto const &option : NOTE_COLORS) {
    QPixmap swatch(14, 14);
    swatch.fill(QColor(option.color));
    combo->addItem(QIcon(swatch), QCoreApplication::translate("AnalysisRunPage", option.name), QString(option.color));
  }
  combo->setMinimumHeight(buttonHeight());
  return combo;
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

QString csvQuote(QString const &text) {
  if (text.contains(',') || text.contains('"') || text.contains('\n')) {
    return QStringLiteral("\"%1\"").arg(QString(text).replace('"', "\"\""));
  }
  return text;
}

QString csvNumber(double value) {
  return std::isnan(value) ? QString{} : QString::number(value, 'g', 10);
}

QString limitText(double value) {
  return std::isnan(value) ? QString{} : QString::number(value, 'g', 6);
}
} // namespace

AnalysisRunPage::AnalysisRunPage(ResHolder          *resHolder,
                                 ExperimentLibrary  *library,
                                 AnalysisNotesStore *notes,
                                 ExperimentRun       run,
                                 QWidget            *parent)
    : QWidget{parent}, m_resHolder{resHolder}, m_library{library}, m_notes{notes}, m_run{std::move(run)} {
  buildUi();

  connect(m_notes, &AnalysisNotesStore::annotationsChanged, this, [this] {
    refreshRegions();
    refreshAnnotationsList();
  });
  connect(m_notes, &AnalysisNotesStore::rulesChanged, this, [this](QString const &runKey) {
    if (runKey == m_run.key) {
      refreshBands();
      refreshRulesList();
    }
  });

  handleAccessChange({}, m_resHolder->users->getCurrentAccessLevel());
  connect(m_resHolder->users, &AccessManager::userSelected, this, &AnalysisRunPage::handleAccessChange);

  // let the tab appear before the (possibly large) files are read
  QTimer::singleShot(0, this, &AnalysisRunPage::load);
}

void AnalysisRunPage::buildUi() {
  auto *root = new QVBoxLayout(this);
  root->setContentsMargins(6 * utils::ssf(), 6 * utils::ssf(), 6 * utils::ssf(), 6 * utils::ssf());
  root->setSpacing(4 * utils::ssf());

  // title / stats
  m_titleLabel = new QLabel(tr("Loading…"), this);
  {
    QFont font = m_titleLabel->font();
    font.setBold(true);
    font.setPointSizeF(font.pointSizeF() * 1.2);
    m_titleLabel->setFont(font);
  }
  m_statsLabel = new QLabel(this);
  m_statsLabel->setStyleSheet("color: #64748b;");
  for (QLabel *label : {m_titleLabel, m_statsLabel}) {
    // long texts must not force the pane wider when tabs are split
    label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  }
  root->addWidget(m_titleLabel);
  root->addWidget(m_statsLabel);

  // tools (wrap to more rows in narrow panes)
  m_channelsBtn = makeButton(tr("Channels ▾"), this);
  connect(m_channelsBtn, &QPushButton::clicked, this, &AnalysisRunPage::openChannelsMenu);

  m_compareBtn = makeButton(tr("Compare ▾"), this);
  m_compareBtn->setToolTip(tr("Overlay the first selected channel of other experiments"));
  connect(m_compareBtn, &QPushButton::clicked, this, &AnalysisRunPage::openCompareMenu);

  m_modeCombo = new QComboBox(this);
  m_modeCombo->addItem(tr("Line"));
  m_modeCombo->addItem(tr("Scatter"));
  m_modeCombo->setMinimumHeight(buttonHeight());
  connect(m_modeCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
    m_chart->setMode(index == 1 ? AnalysisChart::Mode::SCATTER : AnalysisChart::Mode::LINE);
  });

  auto *resetBtn = makeButton(tr("Reset view"), this);
  resetBtn->setToolTip(tr("Fit the whole experiment.\nDrag to pan, wheel to zoom,\nright-drag or Ctrl+drag to zoom into a box."));
  connect(resetBtn, &QPushButton::clicked, this, [this] {
    m_chart->resetView();
    updateTimeControls();
  });

  m_annotateBtn = makeButton(tr("Annotate"), this);
  m_annotateBtn->setCheckable(true);
  m_annotateBtn->setToolTip(tr("Drag across the chart to mark and label a time range"));
  connect(m_annotateBtn, &QPushButton::toggled, this, [this](bool checked) {
    m_chart->setAnnotateMode(checked);
    m_annotateBtn->setText(checked ? tr("Done") : tr("Annotate"));
  });

  auto *notesBtn = makeButton(tr("Rules && notes"), this);
  notesBtn->setCheckable(true);

  m_exportBtn      = makeButton(tr("Export ▾"), this);
  auto *exportMenu = new QMenu(m_exportBtn);
  exportMenu->addAction(tr("Chart PNG"), this, &AnalysisRunPage::exportChartPng);
  exportMenu->addAction(tr("Experiment CSV"), this, &AnalysisRunPage::exportRunCsv);
  exportMenu->addAction(tr("Comparison CSV"), this, &AnalysisRunPage::exportComparisonCsv);
  m_exportBtn->setMenu(exportMenu);

  auto *tools = new FlowLayout(nullptr, 4 * utils::ssf());
  for (QWidget *widget : std::initializer_list<QWidget *>{
         m_channelsBtn, m_compareBtn, m_modeCombo, resetBtn, m_annotateBtn, notesBtn, m_exportBtn}) {
    tools->addWidget(widget);
  }
  root->addLayout(tools);

  // chart
  m_chart = new AnalysisChart(this);
  m_chart->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  connect(m_chart, &AnalysisChart::annotationDrawn, this, &AnalysisRunPage::onAnnotationDrawn);
  root->addWidget(m_chart, 1);

  // plot controls, grouped so a group never breaks across rows
  auto makeGroup = [this](std::initializer_list<QWidget *> widgets) {
    auto *group = new QWidget(this);
    auto *lay   = new QHBoxLayout(group);
    lay->setContentsMargins(0, 0, 8 * utils::ssf(), 0);
    lay->setSpacing(4 * utils::ssf());
    for (QWidget *widget : widgets) {
      lay->addWidget(widget);
    }
    return group;
  };

  auto makeTimeSpin = [this]() {
    auto *spin = new QDoubleSpinBox(this);
    spin->setDecimals(1);
    spin->setSuffix(tr(" min"));
    spin->setRange(0, 0);
    spin->setMinimumHeight(buttonHeight());
    spin->setMinimumWidth(100 * utils::ssf());
    return spin;
  };
  m_fromSpin = makeTimeSpin();
  m_toSpin   = makeTimeSpin();

  auto *applyRangeBtn = makeButton(tr("Apply"), this);
  applyRangeBtn->setToolTip(tr("Show only the selected time range"));
  connect(applyRangeBtn, &QPushButton::clicked, this, [this] {
    m_chart->setTimeRange(m_fromSpin->value() * 60.0, m_toSpin->value() * 60.0);
  });
  auto *fullRangeBtn = makeButton(tr("Full range"), this);
  connect(fullRangeBtn, &QPushButton::clicked, this, [this] {
    m_chart->resetView();
    updateTimeControls();
  });

  m_smoothSpin = new QSpinBox(this);
  m_smoothSpin->setRange(1, 601);
  m_smoothSpin->setSingleStep(2);
  m_smoothSpin->setSuffix(tr(" pt"));
  m_smoothSpin->setMinimumHeight(buttonHeight());
  m_smoothSpin->setToolTip(tr("Moving average window"));
  connect(m_smoothSpin, &QSpinBox::valueChanged, this, [this](int value) {
    m_chart->setSmoothingWindow(value);
  });

  m_minCheck    = new QCheckBox(tr("Min"), this);
  m_maxCheck    = new QCheckBox(tr("Max"), this);
  m_avgCheck    = new QCheckBox(tr("Average"), this);
  m_eventsCheck = new QCheckBox(tr("Events"), this);
  m_eventsCheck->setToolTip(tr("Show the application / PLC event log as vertical markers"));
  m_eventsCheck->setChecked(true);
  for (QCheckBox *check : {m_minCheck, m_maxCheck, m_avgCheck}) {
    connect(check, &QCheckBox::toggled, this, [this] {
      m_chart->setOverlays(m_minCheck->isChecked(), m_maxCheck->isChecked(), m_avgCheck->isChecked());
    });
  }
  connect(m_eventsCheck, &QCheckBox::toggled, this, &AnalysisRunPage::refreshEvents);

  auto *controls = new FlowLayout(nullptr, 4 * utils::ssf());
  controls->addWidget(
    makeGroup({new QLabel(tr("From"), this), m_fromSpin, new QLabel(tr("to"), this), m_toSpin, applyRangeBtn, fullRangeBtn}));
  controls->addWidget(makeGroup({new QLabel(tr("Smooth"), this), m_smoothSpin}));
  controls->addWidget(makeGroup({new QLabel(tr("Overlays:"), this), m_minCheck, m_maxCheck, m_avgCheck}));
  controls->addWidget(makeGroup({m_eventsCheck}));
  root->addLayout(controls);

  m_notesPanel = buildNotesPanel();
  m_notesPanel->setVisible(false);
  root->addWidget(m_notesPanel);
  connect(notesBtn, &QPushButton::toggled, m_notesPanel, &QWidget::setVisible);
}

QWidget *AnalysisRunPage::buildNotesPanel() {
  auto *panel = new QTabWidget(this);
  panel->setDocumentMode(true);

  // variable rules
  auto *rulesPage = new QWidget(panel);
  auto *rulesLay  = new QVBoxLayout(rulesPage);
  rulesLay->setContentsMargins(0, 4 * utils::ssf(), 0, 0);

  m_ruleChannelCombo = new QComboBox(rulesPage);
  m_ruleChannelCombo->setMinimumHeight(buttonHeight());
  m_ruleChannelCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  m_ruleChannelCombo->setMinimumContentsLength(16);
  m_ruleLoEdit    = new QLineEdit(rulesPage);
  m_ruleHiEdit    = new QLineEdit(rulesPage);
  m_ruleLabelEdit = new QLineEdit(rulesPage);
  m_ruleLoEdit->setPlaceholderText(tr("min"));
  m_ruleHiEdit->setPlaceholderText(tr("max"));
  m_ruleLabelEdit->setPlaceholderText(tr("label"));
  for (QLineEdit *edit : {m_ruleLoEdit, m_ruleHiEdit, m_ruleLabelEdit}) {
    edit->setFixedWidth(90 * utils::ssf());
    edit->setMinimumHeight(buttonHeight());
  }
  m_ruleColorCombo = makeColorCombo(rulesPage);
  auto *addRuleBtn = makeButton(tr("Add rule"), rulesPage);
  connect(addRuleBtn, &QPushButton::clicked, this, &AnalysisRunPage::addRule);

  auto *form = new FlowLayout(nullptr, 4 * utils::ssf());
  for (QWidget *widget : std::initializer_list<QWidget *>{
         m_ruleChannelCombo, m_ruleLoEdit, m_ruleHiEdit, m_ruleLabelEdit, m_ruleColorCombo, addRuleBtn}) {
    form->addWidget(widget);
  }
  rulesLay->addLayout(form);

  auto *rulesListLay = new QHBoxLayout();
  m_rulesList        = new QListWidget(rulesPage);
  m_rulesList->setFixedHeight(100 * utils::ssf());
  auto *removeRuleBtn = makeButton(tr("Remove"), rulesPage);
  connect(removeRuleBtn, &QPushButton::clicked, this, [this] {
    if (auto *item = m_rulesList->currentItem()) {
      m_notes->removeRule(m_run.key, item->data(Qt::UserRole).toString());
    }
  });
  rulesListLay->addWidget(m_rulesList, 1);
  rulesListLay->addWidget(removeRuleBtn, 0, Qt::AlignTop);
  rulesLay->addLayout(rulesListLay);

  // annotations
  auto *annotationsPage = new QWidget(panel);
  auto *annotationsLay  = new QVBoxLayout(annotationsPage);
  annotationsLay->setContentsMargins(0, 4 * utils::ssf(), 0, 0);
  auto *hint = new QLabel(tr("Press \"Annotate\", then drag across the chart to mark and label a time range."),
                          annotationsPage);
  hint->setWordWrap(true);
  hint->setStyleSheet("color: #64748b;");
  auto *annotationsListLay = new QHBoxLayout();
  m_annotationsList        = new QListWidget(annotationsPage);
  m_annotationsList->setFixedHeight(100 * utils::ssf());
  auto *removeAnnotationBtn = makeButton(tr("Remove"), annotationsPage);
  connect(removeAnnotationBtn, &QPushButton::clicked, this, [this] {
    if (auto *item = m_annotationsList->currentItem()) {
      m_notes->removeAnnotation(item->data(Qt::UserRole).toString());
    }
  });
  annotationsListLay->addWidget(m_annotationsList, 1);
  annotationsListLay->addWidget(removeAnnotationBtn, 0, Qt::AlignTop);
  annotationsLay->addWidget(hint);
  annotationsLay->addLayout(annotationsListLay);

  panel->addTab(rulesPage, tr("Variable rules"));
  panel->addTab(annotationsPage, tr("Annotations"));
  return panel;
}

void AnalysisRunPage::load() {
  QString error;
  m_data = m_library->load(m_run, &error);
  if (!m_data) {
    m_titleLabel->setText(m_run.title);
    m_statsLabel->setText(error);
    m_statsLabel->setStyleSheet("color: #b03030;");
    return;
  }
  m_run     = m_data->run;
  m_elapsed = elapsed(*m_data);
  m_channels = m_library->channels(m_data->states.size());

  // channel selection: saved one, else the ones visible on the main graph
  QStringList const saved = m_notes->channels(m_run.key);
  for (int i{0}; i < m_channels.size(); ++i) {
    if (saved.isEmpty() ? m_channels[i].defaultVisible : saved.contains(m_channels[i].name)) {
      m_selected.push_back(i);
    }
  }
  if (m_selected.isEmpty() && !m_channels.isEmpty()) {
    m_selected.push_back(0);
  }

  m_ruleChannelCombo->clear();
  for (ChannelInfo const &channel : std::as_const(m_channels)) {
    m_ruleChannelCombo->addItem(channel.label, channel.name);
  }

  refreshStats();
  updateChannelsButton();
  refreshChart();
  refreshRulesList();
  refreshAnnotationsList();
  updateTimeControls();
}

QVector<double> AnalysisRunPage::elapsed(ExperimentData const &data) const {
  QVector<double> result;
  result.reserve(data.times.size());
  double const start{data.times.isEmpty() ? 0.0 : data.times.first()};
  for (double const time : data.times) {
    result.push_back(time - start);
  }
  return result;
}

bool AnalysisRunPage::compareMode() const {
  return !m_compareKeys.isEmpty();
}

int AnalysisRunPage::compareChannel() const {
  return m_selected.isEmpty() ? 0 : m_selected.first();
}

QColor AnalysisRunPage::channelColor(int index) const {
  return m_colorOverrides.value(index, m_channels.value(index).color);
}

void AnalysisRunPage::refreshStats() {
  m_titleLabel->setText((m_run.custom ? tr("Range %1") : tr("Experiment %1")).arg(m_run.title));
  m_statsLabel->setText(tr("%1 → %2   ·   duration %3   ·   %4 samples")
                          .arg(QDateTime::fromSecsSinceEpoch(static_cast<qint64>(m_run.start)).toString("yyyy-MM-dd HH:mm:ss"),
                               QDateTime::fromSecsSinceEpoch(static_cast<qint64>(m_run.end)).toString("yyyy-MM-dd HH:mm:ss"),
                               ExperimentLibrary::formatDuration(m_run.duration()))
                          .arg(m_run.samples));
}

void AnalysisRunPage::refreshChart() {
  if (!m_data) {
    return;
  }
  m_chart->beginUpdate();

  QVector<ChartSeries> series;
  if (compareMode()) {
    int const          channel{compareChannel()};
    ChannelInfo const &info = m_channels.value(channel);

    auto addRun = [&](ExperimentData const &data, QString const &label) {
      if (channel >= data.states.size()) {
        return;
      }
      ChartSeries item;
      item.label   = label;
      item.color   = COMPARE_COLORS[series.size() % COMPARE_COLORS.size()];
      item.x       = elapsed(data);
      item.y       = data.states[channel];
      item.logAxis = info.scientific;
      series.push_back(item);
    };
    addRun(*m_data, m_run.title);
    for (QString const &key : std::as_const(m_compareKeys)) {
      if (auto data = m_compareData.value(key)) {
        addRun(*data, data->run.title);
      }
    }
    m_titleLabel->setText(tr("Comparing: %1").arg(info.label));
    m_chart->setSeries(series, 0.0);
  } else {
    for (int const index : std::as_const(m_selected)) {
      if (index >= m_data->states.size()) {
        continue;
      }
      ChartSeries item;
      item.label   = m_channels[index].label;
      item.color   = channelColor(index);
      item.x       = m_elapsed;
      item.y       = m_data->states[index];
      item.logAxis = m_channels[index].scientific;
      series.push_back(item);
    }
    refreshStats();
    m_chart->setSeries(series, m_run.start);
  }

  m_annotateBtn->setEnabled(!compareMode());
  if (compareMode()) {
    m_annotateBtn->setChecked(false);
  }
  refreshRegions();
  refreshBands();
  refreshEvents();
  m_chart->endUpdate();
}

void AnalysisRunPage::refreshRegions() {
  if (!m_data || compareMode()) {
    m_chart->setRegions({});
    return;
  }
  QVector<ChartRegion> regions;
  for (auto const &annotation : m_notes->annotations(m_run.start, m_run.end)) {
    regions.push_back({annotation.start - m_run.start, annotation.end - m_run.start, annotation.label, annotation.color});
  }
  m_chart->setRegions(regions);
}

void AnalysisRunPage::refreshBands() {
  if (!m_data) {
    return;
  }
  QStringList shownChannels;
  if (compareMode()) {
    shownChannels.push_back(m_channels.value(compareChannel()).name);
  } else {
    for (int const index : std::as_const(m_selected)) {
      shownChannels.push_back(m_channels.value(index).name);
    }
  }

  QVector<ChartBand> bands;
  for (auto const &rule : m_notes->rules(m_run.key)) {
    if (!shownChannels.contains(rule.channel)) {
      continue;
    }
    bool scientific{false};
    for (ChannelInfo const &channel : std::as_const(m_channels)) {
      if (channel.name == rule.channel) {
        scientific = channel.scientific;
      }
    }
    bands.push_back({rule.lo, rule.hi, rule.label, rule.color, scientific});
  }
  m_chart->setBands(bands);
}

void AnalysisRunPage::refreshEvents() {
  if (!m_data || compareMode() || !m_eventsCheck->isChecked()) {
    m_chart->setEvents({});
    return;
  }
  QVector<ChartEvent> events;
  for (ExperimentEvent const &event : m_library->events(m_run.start, m_run.end)) {
    events.push_back({event.time - m_run.start, event.msg, eventColor(event.type)});
  }
  m_chart->setEvents(events);
}

void AnalysisRunPage::refreshRulesList() {
  m_rulesList->clear();
  for (auto const &rule : m_notes->rules(m_run.key)) {
    QString channelLabel = rule.channel;
    for (ChannelInfo const &channel : std::as_const(m_channels)) {
      if (channel.name == rule.channel) {
        channelLabel = channel.label;
      }
    }
    QString const lo = std::isnan(rule.lo) ? QStringLiteral("−∞") : limitText(rule.lo);
    QString const hi = std::isnan(rule.hi) ? QStringLiteral("+∞") : limitText(rule.hi);
    QPixmap       swatch(14, 14);
    swatch.fill(rule.color);
    auto *item = new QListWidgetItem(QIcon(swatch),
                                     QStringLiteral("%1 · %2  [%3 … %4]  · %5").arg(rule.label, channelLabel, lo, hi, rule.user),
                                     m_rulesList);
    item->setData(Qt::UserRole, rule.id);
  }
}

void AnalysisRunPage::refreshAnnotationsList() {
  m_annotationsList->clear();
  for (auto const &annotation : m_notes->annotations(m_run.start, m_run.end)) {
    QPixmap swatch(14, 14);
    swatch.fill(annotation.color);
    auto *item = new QListWidgetItem(
      QIcon(swatch),
      QStringLiteral("%1 · %2 – %3 · %4")
        .arg(annotation.label,
             QDateTime::fromSecsSinceEpoch(static_cast<qint64>(annotation.start)).toString("dd.MM HH:mm:ss"),
             QDateTime::fromSecsSinceEpoch(static_cast<qint64>(annotation.end)).toString("HH:mm:ss"),
             annotation.user),
      m_annotationsList);
    item->setData(Qt::UserRole, annotation.id);
  }
}

void AnalysisRunPage::updateTimeControls() {
  auto const [start, end] = m_chart->dataXRange();
  auto const [viewStart, viewEnd] = m_chart->viewXRange();
  for (QDoubleSpinBox *spin : {m_fromSpin, m_toSpin}) {
    spin->blockSignals(true);
    spin->setRange(start / 60.0, std::max(start, end) / 60.0);
  }
  m_fromSpin->setValue(std::max(start, viewStart) / 60.0);
  m_toSpin->setValue(std::min(end, viewEnd) / 60.0);
  for (QDoubleSpinBox *spin : {m_fromSpin, m_toSpin}) {
    spin->blockSignals(false);
  }
}

void AnalysisRunPage::updateChannelsButton() {
  m_channelsBtn->setText(tr("Channels (%1/%2) ▾").arg(m_selected.size()).arg(m_channels.size()));
}

void AnalysisRunPage::openChannelsMenu() {
  if (!m_data) {
    return;
  }
  auto *menu = new QMenu(this);
  menu->setAttribute(Qt::WA_DeleteOnClose);

  auto  checks   = std::make_shared<QVector<QCheckBox *>>();
  auto *quickRow = new QWidgetAction(menu);
  {
    auto *row      = new QWidget(menu);
    auto *rowLay   = new QHBoxLayout(row);
    rowLay->setContentsMargins(8, 4, 8, 4);
    auto *defaults = new QPushButton(tr("Default"), row);
    auto *none     = new QPushButton(tr("None"), row);
    rowLay->addWidget(defaults);
    rowLay->addWidget(none);
    quickRow->setDefaultWidget(row);

    // apply the whole selection at once instead of one redraw per channel
    auto applySelection = [this, checks](bool defaultsOnly) {
      QVector<int> selection;
      for (int i{0}; i < checks->size(); ++i) {
        bool const selected{defaultsOnly && m_channels[i].defaultVisible};
        QSignalBlocker blocker((*checks)[i]);
        (*checks)[i]->setChecked(selected);
        if (selected) {
          selection.push_back(i);
        }
      }
      setSelectedChannels(selection);
    };
    connect(defaults, &QPushButton::clicked, this, [applySelection] {
      applySelection(true);
    });
    connect(none, &QPushButton::clicked, this, [applySelection] {
      applySelection(false);
    });
  }
  menu->addAction(quickRow);
  menu->addSeparator();

  for (int i{0}; i < m_channels.size(); ++i) {
    auto *action = new QWidgetAction(menu);
    auto *row    = new QWidget(menu);
    auto *rowLay = new QHBoxLayout(row);
    rowLay->setContentsMargins(8, 2, 8, 2);
    rowLay->setSpacing(8);

    auto *check = new QCheckBox(m_channels[i].label, row);
    check->setChecked(m_selected.contains(i));
    connect(check, &QCheckBox::toggled, this, [this, i](bool checked) {
      setChannelSelected(i, checked);
    });
    checks->push_back(check);

    auto *swatch = new QPushButton(row);
    swatch->setFixedSize(18, 18);
    swatch->setFocusPolicy(Qt::NoFocus);
    swatch->setStyleSheet(QStringLiteral("background: %1; border: 1px solid #94a3b8; border-radius: 3px;")
                            .arg(channelColor(i).name()));
    connect(swatch, &QPushButton::clicked, this, [this, i, menu]() {
      menu->close();
      QColor const picked = QColorDialog::getColor(channelColor(i), this, m_channels[i].label);
      if (picked.isValid()) {
        m_colorOverrides[i] = picked;
        refreshChart();
      }
    });

    rowLay->addWidget(check, 1);
    rowLay->addWidget(swatch);
    action->setDefaultWidget(row);
    menu->addAction(action);
  }

  menu->exec(m_channelsBtn->mapToGlobal(m_channelsBtn->rect().bottomLeft()));
}

void AnalysisRunPage::setChannelSelected(int index, bool selected) {
  if (selected == m_selected.contains(index)) {
    return;
  }
  QVector<int> selection = m_selected;
  if (selected) {
    selection.push_back(index);
  } else {
    selection.removeAll(index);
  }
  setSelectedChannels(selection);
}

void AnalysisRunPage::setSelectedChannels(QVector<int> selection) {
  std::sort(selection.begin(), selection.end());
  if (selection == m_selected) {
    return;
  }
  m_selected = selection;

  QStringList names;
  for (int const i : std::as_const(m_selected)) {
    names.push_back(m_channels[i].name);
  }
  m_notes->setChannels(m_run.key, names);

  updateChannelsButton();
  refreshChart();
}

void AnalysisRunPage::openCompareMenu() {
  if (!m_data) {
    return;
  }
  auto *menu = new QMenu(this);
  menu->setAttribute(Qt::WA_DeleteOnClose);

  auto *header = menu->addAction(tr("Compare \"%1\" with:").arg(m_channels.value(compareChannel()).label));
  header->setEnabled(false);

  auto *clear = menu->addAction(tr("Clear comparison"));
  clear->setEnabled(compareMode());
  connect(clear, &QAction::triggered, this, [this] {
    m_compareKeys.clear();
    m_compareBtn->setText(tr("Compare ▾"));
    refreshChart();
    m_chart->resetView();
    updateTimeControls();
  });
  menu->addSeparator();

  int shown{0};
  for (ExperimentRun const &run : m_library->runs()) {
    if (run.key == m_run.key) {
      continue;
    }
    if (++shown > MAX_COMPARE_MENU_RUNS) {
      break;
    }
    auto *action = new QWidgetAction(menu);
    auto *check  = new QCheckBox(QStringLiteral("%1   (%2)").arg(run.title, ExperimentLibrary::formatDuration(run.duration())), menu);
    check->setContentsMargins(8, 2, 8, 2);
    check->setChecked(m_compareKeys.contains(run.key));
    connect(check, &QCheckBox::toggled, this, [this, run](bool checked) {
      setCompareRun(run, checked);
    });
    action->setDefaultWidget(check);
    menu->addAction(action);
  }
  if (0 == shown) {
    menu->addAction(tr("No other experiments found"))->setEnabled(false);
  }

  menu->exec(m_compareBtn->mapToGlobal(m_compareBtn->rect().bottomLeft()));
}

void AnalysisRunPage::setCompareRun(ExperimentRun const &run, bool compared) {
  bool const wasCompare{compareMode()};
  if (compared) {
    if (!m_compareData.contains(run.key)) {
      QString error;
      auto    data = m_library->load(run, &error);
      if (!data) {
        (new MessageDialog(tr("Compare"), error))->open();
        return;
      }
      m_compareData.insert(run.key, data);
    }
    if (!m_compareKeys.contains(run.key)) {
      m_compareKeys.push_back(run.key);
    }
  } else {
    m_compareKeys.removeAll(run.key);
  }

  m_compareBtn->setText(compareMode() ? tr("Compare (%1) ▾").arg(m_compareKeys.size()) : tr("Compare ▾"));
  refreshChart();
  if (wasCompare != compareMode()) {
    m_chart->resetView();
  }
  updateTimeControls();
}

void AnalysisRunPage::onAnnotationDrawn(double x0, double x1) {
  QDialog dialog(this);
  dialog.setWindowTitle(tr("Add annotation"));
  auto *lay  = new QVBoxLayout(&dialog);
  auto *info = new QLabel(QStringLiteral("%1 – %2").arg(ExperimentLibrary::formatDuration(x0), ExperimentLibrary::formatDuration(x1)),
                          &dialog);
  auto *form       = new QHBoxLayout();
  auto *labelEdit  = new QLineEdit(&dialog);
  labelEdit->setPlaceholderText(tr("e.g. Steady state"));
  labelEdit->setMinimumHeight(buttonHeight());
  auto *colorCombo = makeColorCombo(&dialog);
  form->addWidget(new QLabel(tr("Label:"), &dialog));
  form->addWidget(labelEdit, 1);
  form->addWidget(colorCombo);

  auto *buttons   = new QHBoxLayout();
  auto *cancelBtn = makeButton(tr("Cancel"), &dialog);
  auto *addBtn    = makeButton(tr("Add"), &dialog);
  addBtn->setDefault(true);
  buttons->addWidget(cancelBtn);
  buttons->addStretch();
  buttons->addWidget(addBtn);
  connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);
  connect(addBtn, &QPushButton::clicked, &dialog, &QDialog::accept);

  lay->addWidget(info);
  lay->addLayout(form);
  lay->addLayout(buttons);
  dialog.resize(420 * utils::ssf(), dialog.sizeHint().height());

  if (dialog.exec() != QDialog::Accepted) {
    return;
  }

  AnalysisNotesStore::Annotation annotation;
  annotation.start = m_run.start + x0;
  annotation.end   = m_run.start + x1;
  annotation.label = labelEdit->text().trimmed();
  if (annotation.label.isEmpty()) {
    annotation.label = tr("t = %1 – %2").arg(ExperimentLibrary::formatDuration(x0), ExperimentLibrary::formatDuration(x1));
  }
  annotation.color = QColor(colorCombo->currentData().toString());
  annotation.user  = m_resHolder->users->getCurrentUserName();
  m_notes->addAnnotation(annotation);
  m_annotateBtn->setChecked(false);
}

void AnalysisRunPage::addRule() {
  QString const channel = m_ruleChannelCombo->currentData().toString();
  double        lo{qQNaN()};
  double        hi{qQNaN()};
  if (channel.isEmpty() || !parseNumber(m_ruleLoEdit->text(), lo) || !parseNumber(m_ruleHiEdit->text(), hi)
      || (std::isnan(lo) && std::isnan(hi))) {
    (new MessageDialog(tr("Variable rule"), tr("Enter a numeric min and/or max value")))->open();
    return;
  }
  if (!std::isnan(lo) && !std::isnan(hi) && (lo > hi)) {
    std::swap(lo, hi);
  }

  AnalysisNotesStore::Rule rule;
  rule.channel = channel;
  rule.lo      = lo;
  rule.hi      = hi;
  rule.label   = m_ruleLabelEdit->text().trimmed();
  if (rule.label.isEmpty()) {
    rule.label = m_ruleChannelCombo->currentText();
  }
  rule.color = QColor(m_ruleColorCombo->currentData().toString());
  rule.user  = m_resHolder->users->getCurrentUserName();

  // make the rule visible right away
  for (int i{0}; i < m_channels.size(); ++i) {
    if (m_channels[i].name == channel && !compareMode()) {
      setChannelSelected(i, true);
    }
  }
  m_notes->addRule(m_run.key, rule);

  m_ruleLoEdit->clear();
  m_ruleHiEdit->clear();
  m_ruleLabelEdit->clear();
}

void AnalysisRunPage::askExportPath(QString const &extension, std::function<bool(QString const &)> writer) {
  QStringList sources{deepvaccontrol::EXPORT_DIR};
  sources.append(m_resHolder->usb->getAvailableDrives());
  auto *pathProvider = new FileSaveDialogPathProvider(sources, m_resHolder->logger, this);
  pathProvider->getPathAsync(this, QString{}, [this, extension, writer, pathProvider](QString const &filePath) {
    pathProvider->deleteLater();
    if (filePath.isEmpty()) {
      return;
    }
    QString const fullPath = filePath.endsWith("." + extension) ? filePath : filePath + "." + extension;
    bool const    ok{writer(fullPath)};
    (new MessageDialog(tr("Export"), ok ? tr("Saved to %1").arg(fullPath) : tr("Failed to write %1").arg(fullPath)))->open();
  });
}

void AnalysisRunPage::exportChartPng() {
  if (!m_data) {
    return;
  }
  askExportPath("png", [this](QString const &path) {
    return m_chart->exportPng(path);
  });
}

void AnalysisRunPage::exportRunCsv() {
  if (!m_data) {
    return;
  }
  askExportPath("csv", [this](QString const &path) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
      return false;
    }
    QTextStream out(&file);
    out.setGenerateByteOrderMark(true);

    QStringList header{tr("Time"), tr("Elapsed, s")};
    for (ChannelInfo const &channel : std::as_const(m_channels)) {
      header.push_back(csvQuote(channel.label));
    }
    out << header.join(',') << '\n';

    for (qsizetype i{0}; i < m_data->times.size(); ++i) {
      QStringList row{QDateTime::fromSecsSinceEpoch(static_cast<qint64>(m_data->times[i])).toString("yyyy-MM-dd HH:mm:ss"),
                      csvNumber(m_elapsed[i])};
      for (auto const &channel : m_data->states) {
        row.push_back(csvNumber(i < channel.size() ? channel[i] : qQNaN()));
      }
      out << row.join(',') << '\n';
    }
    out.flush();
    return file.commit();
  });
}

void AnalysisRunPage::exportComparisonCsv() {
  if (!m_data) {
    return;
  }
  askExportPath("csv", [this](QString const &path) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
      return false;
    }
    QTextStream out(&file);
    out.setGenerateByteOrderMark(true);

    int const          channel{compareChannel()};
    ChannelInfo const &info = m_channels.value(channel);
    out << QStringList{tr("Experiment"),
                       tr("Start"),
                       tr("End"),
                       tr("Duration, s"),
                       tr("Samples"),
                       tr("Channel"),
                       tr("Min"),
                       tr("Max"),
                       tr("Mean"),
                       tr("Final")}
             .join(',')
        << '\n';

    QVector<std::shared_ptr<ExperimentData const>> runs{m_data};
    for (QString const &key : std::as_const(m_compareKeys)) {
      if (auto data = m_compareData.value(key)) {
        runs.push_back(data);
      }
    }
    for (auto const &data : runs) {
      double minValue{qQNaN()};
      double maxValue{qQNaN()};
      double finalValue{qQNaN()};
      double sum{0.0};
      qint64 count{0};
      if (channel < data->states.size()) {
        for (double const value : data->states[channel]) {
          if (std::isnan(value)) {
            continue;
          }
          minValue   = std::isnan(minValue) ? value : std::min(minValue, value);
          maxValue   = std::isnan(maxValue) ? value : std::max(maxValue, value);
          finalValue = value;
          sum += value;
          ++count;
        }
      }
      out << QStringList{csvQuote(data->run.title),
                         QDateTime::fromSecsSinceEpoch(static_cast<qint64>(data->run.start)).toString("yyyy-MM-dd HH:mm:ss"),
                         QDateTime::fromSecsSinceEpoch(static_cast<qint64>(data->run.end)).toString("yyyy-MM-dd HH:mm:ss"),
                         csvNumber(data->run.duration()),
                         QString::number(data->run.samples),
                         csvQuote(info.label),
                         csvNumber(minValue),
                         csvNumber(maxValue),
                         csvNumber(count > 0 ? sum / static_cast<double>(count) : qQNaN()),
                         csvNumber(finalValue)}
               .join(',')
          << '\n';
    }
    out.flush();
    return file.commit();
  });
}

void AnalysisRunPage::handleAccessChange(QString const &name, AccessManager::AccessLevel level) {
  Q_UNUSED(name)
  m_exportBtn->setEnabled(level >= AccessManager::AccessLevel::USER_START_1);
}

} // namespace deepvaccontrol
