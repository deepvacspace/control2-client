#include "dataanalysiswidget.h"

#include <QDateTime>
#include <QDir>
#include <QRegularExpression>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "analysiseditorarea.h"
#include "analysisnotesstore.h"
#include "analysisrunpage.h"
#include "collabmanager.h"
#include "constants.h"
#include "experimentreport.h"
#include "dialogs/messagedialog.h"
#include "experimentrecorder.h"
#include "graph/daterangedialog.h"
#include "resholder.h"
#include "utils.h"

namespace deepvaccontrol {

namespace {
constexpr int RUN_KEY_ROLE{Qt::UserRole};
constexpr int DAY_ROLE{Qt::UserRole + 1};

QPushButton *makeButton(QString const &text, QWidget *parent) {
  auto *button = new QPushButton(text, parent);
  button->setMinimumHeight(40 * utils::ssf());
  button->setFocusPolicy(Qt::NoFocus);
  return button;
}
} // namespace

DataAnalysisWidget::DataAnalysisWidget(ResHolder *resHolder, QWidget *parent)
    : QWidget{parent}
    , m_resHolder{resHolder}
    , m_library{new ExperimentLibrary(deepvaccontrol::INTERVALS_DIR,
                                      deepvaccontrol::ACTIONS_FILE,
                                      [resHolder]() {
                                        return resHolder->settings->settingsMap()["states"].toList();
                                      },
                                      this)}
    , m_notes{new AnalysisNotesStore(deepvaccontrol::ANALYSIS_NOTES_FILE, this)}
    , m_editor{new AnalysisEditorArea(this)} {
  auto *mainLay = new QVBoxLayout(this);
  mainLay->setContentsMargins(0, 0, 0, 0);
  mainLay->setSpacing(4 * utils::ssf());

  // browser: hide button + filter | list | buttons
  m_browser        = new QWidget(this);
  auto *browserLay = new QVBoxLayout(m_browser);
  browserLay->setContentsMargins(0, 0, 0, 0);
  browserLay->setSpacing(4 * utils::ssf());

  auto *hideBtn = makeButton(QStringLiteral("◀"), m_browser);
  hideBtn->setFixedWidth(40 * utils::ssf());
  hideBtn->setToolTip(tr("Hide the experiment list"));

  m_filterEdit = new QLineEdit(m_browser);
  m_filterEdit->setPlaceholderText(tr("Filter, e.g. 2026-04"));
  m_filterEdit->setClearButtonEnabled(true);
  m_filterEdit->setMinimumHeight(40 * utils::ssf());
  connect(m_filterEdit, &QLineEdit::textChanged, this, &DataAnalysisWidget::applyFilter);

  auto *filterLay = new QHBoxLayout();
  filterLay->setSpacing(4 * utils::ssf());
  filterLay->addWidget(hideBtn);
  filterLay->addWidget(m_filterEdit, 1);
  browserLay->addLayout(filterLay);

  m_tree = new QTreeWidget(m_browser);
  m_tree->setColumnCount(3);
  m_tree->setHeaderLabels({tr("Date"), tr("Duration"), tr("Pts")});
  m_tree->headerItem()->setToolTip(2, tr("Number of samples"));
  m_tree->headerItem()->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter);
  m_tree->setRootIsDecorated(true);
  m_tree->setUniformRowHeights(true);
  m_tree->setAlternatingRowColors(true);
  m_tree->setIndentation(12 * utils::ssf());
  m_tree->header()->setStretchLastSection(false);
  m_tree->header()->setMinimumSectionSize(10);
  m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  m_tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
  m_tree->setStyleSheet(QStringLiteral("QTreeWidget::item { height: %1px; }").arg(static_cast<int>(30 * utils::ssf())));
  // touch friendly: one tap opens an experiment or expands / collapses a day
  connect(m_tree, &QTreeWidget::itemClicked, this, &DataAnalysisWidget::openItem);

  m_refreshBtn       = makeButton(tr("Refresh"), m_browser);
  auto *openRangeBtn = makeButton(tr("Range…"), m_browser);
  m_refreshBtn->setToolTip(tr("Search the recorded data for new experiments"));
  openRangeBtn->setToolTip(tr("Open an arbitrary date / time range"));
  connect(m_refreshBtn, &QPushButton::clicked, m_library, &ExperimentLibrary::refresh);
  connect(openRangeBtn, &QPushButton::clicked, this, &DataAnalysisWidget::openCustomRange);
  auto *buttonsLay = new QHBoxLayout();
  buttonsLay->addWidget(m_refreshBtn);
  buttonsLay->addWidget(openRangeBtn);

  browserLay->addWidget(m_tree, 1);
  browserLay->addLayout(buttonsLay);

  auto *splitter = new QSplitter(Qt::Horizontal, this);
  splitter->setHandleWidth(6 * utils::ssf());
  splitter->addWidget(m_browser);
  splitter->addWidget(m_editor);
  splitter->setStretchFactor(0, 0);
  splitter->setStretchFactor(1, 1);
  splitter->setCollapsible(0, false);
  splitter->setCollapsible(1, false);
  splitter->setSizes({static_cast<int>(270 * utils::ssf()), static_cast<int>(1000 * utils::ssf())});

  // thin full-height strip that brings the hidden list back
  auto *showStrip = new QPushButton(QStringLiteral("▶"), this);
  showStrip->setToolTip(tr("Show the experiment list"));
  showStrip->setFocusPolicy(Qt::NoFocus);
  showStrip->setFixedWidth(24 * utils::ssf());
  showStrip->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
  showStrip->setVisible(false);

  auto *bodyLay = new QHBoxLayout();
  bodyLay->setContentsMargins(0, 0, 0, 0);
  bodyLay->setSpacing(0);
  bodyLay->addWidget(showStrip);
  bodyLay->addWidget(splitter, 1);
  mainLay->addLayout(bodyLay, 1);

  connect(hideBtn, &QPushButton::clicked, this, [this, showStrip] {
    m_browser->setVisible(false);
    showStrip->setVisible(true);
  });
  connect(showStrip, &QPushButton::clicked, this, [this, showStrip] {
    showStrip->setVisible(false);
    m_browser->setVisible(true);
  });

  connect(m_library, &ExperimentLibrary::scanStarted, this, [this] {
    m_refreshBtn->setText(tr("Searching…"));
    m_refreshBtn->setEnabled(false);
  });
  connect(m_library, &ExperimentLibrary::runsChanged, this, &DataAnalysisWidget::populateTree);
  if (m_resHolder->recorder) {
    connect(m_resHolder->recorder, &ExperimentRecorder::runSaved, m_library, &ExperimentLibrary::refresh);
  }
  if (m_resHolder->collab) {
    connect(m_resHolder->collab, &CollabManager::localDataChanged, this, [this] {
      m_notes->notifyExternalChange();
      m_library->refresh();
    });
  }
}

ExperimentLibrary *DataAnalysisWidget::library() const {
  return m_library;
}

AnalysisNotesStore *DataAnalysisWidget::notes() const {
  return m_notes;
}

void DataAnalysisWidget::ensureScanned() {
  if (!m_scanned) {
    m_scanned = true;
    m_library->refresh();
  }
}

QString DataAnalysisWidget::showRun(ExperimentRun const &run, QStringList const &channels, double from, double to,
                                    QVector<ExperimentRun> const &compare) {
  openRun(run);
  AnalysisRunPage *page = m_editor->page(run.key);
  return page ? page->applyView(channels, from, to, compare) : tr("Could not open %1").arg(run.title);
}

QString DataAnalysisWidget::writeReport(ExperimentRun const &run, QVector<ExperimentRun> const &compare, bool pdf, QString &path) {
  openRun(run);
  AnalysisRunPage *page = m_editor->page(run.key);
  if (!page) {
    return tr("Could not open %1").arg(run.title);
  }
  if (QString const error = page->applyView({}, qQNaN(), qQNaN(), compare); !error.isEmpty()) {
    return error;
  }
  QString name = run.title;
  name.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]+")), QStringLiteral("_"));
  QDir const folder(QDir(deepvaccontrol::EXPORT_DIR).absoluteFilePath(QStringLiteral("reports")));
  QDir().mkpath(folder.absolutePath());
  path = folder.filePath(QStringLiteral("%1_%2.%3").arg(name, QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"), pdf ? "pdf" : "html"));
  ExperimentReportData const report = page->buildReport();
  bool const ok{pdf ? experimentreport::writePdf(path, report) : experimentreport::writeHtml(path, report)};
  return ok ? QString{} : tr("Could not write %1").arg(path);
}

void DataAnalysisWidget::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);
  // scan lazily: the page may never be opened
  if (!m_scanned) {
    m_scanned = true;
    m_library->refresh();
  }
}

void DataAnalysisWidget::populateTree() {
  QString const selectedKey = m_tree->currentItem() ? m_tree->currentItem()->data(0, RUN_KEY_ROLE).toString() : QString{};

  m_tree->clear();
  QHash<QDate, QTreeWidgetItem *> dayItems;
  QTreeWidgetItem                *selected{nullptr};

  // experiments saved in the database (recorded here or received from paired computers)
  if (!m_library->storedRuns().isEmpty()) {
    auto *dbItem = new QTreeWidgetItem(m_tree);
    dbItem->setText(0, tr("Saved in database"));
    dbItem->setText(2, QString::number(m_library->storedRuns().size()));
    dbItem->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter);
    QFont font = dbItem->font(0);
    font.setBold(true);
    dbItem->setFont(0, font);
    for (ExperimentRun const &run : m_library->storedRuns()) {
      auto *item = new QTreeWidgetItem(dbItem);
      item->setText(0, run.title);
      item->setText(1, ExperimentLibrary::formatDuration(run.duration()));
      item->setText(2, QString::number(run.samples));
      item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
      item->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter);
      item->setData(0, RUN_KEY_ROLE, run.key);
      item->setToolTip(0, run.details.isEmpty() ? QStringLiteral("%1\n%2").arg(run.title, run.chamber)
                                                : QStringLiteral("%1\n%2\n%3").arg(run.title, run.chamber, run.details));
      if (run.key == selectedKey) {
        selected = item;
      }
    }
  }
  for (ExperimentRun const &run : m_library->runs()) {
    QDateTime const start = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(run.start));
    QDate const     day   = start.date();

    QTreeWidgetItem *dayItem = dayItems.value(day);
    if (!dayItem) {
      dayItem = new QTreeWidgetItem(m_tree);
      dayItem->setText(0, day.toString("yyyy-MM-dd, ddd"));
      dayItem->setData(0, DAY_ROLE, day);
      QFont font = dayItem->font(0);
      font.setBold(true);
      dayItem->setFont(0, font);
      dayItems.insert(day, dayItem);
    }

    auto *item = new QTreeWidgetItem(dayItem);
    QDateTime const end = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(run.end));
    item->setText(0,
                  start.toString("HH:mm:ss") + " – "
                    + (end.date() == day ? end.toString("HH:mm") : end.toString("dd.MM HH:mm")));
    item->setText(1, ExperimentLibrary::formatDuration(run.duration()));
    item->setText(2, QString::number(run.samples));
    item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
    item->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter);
    item->setData(0, RUN_KEY_ROLE, run.key);
    item->setData(0, DAY_ROLE, day);
    item->setToolTip(0, run.title);
    if (run.key == selectedKey) {
      selected = item;
    }
  }

  for (QTreeWidgetItem *dayItem : std::as_const(dayItems)) {
    dayItem->setText(2, QString::number(dayItem->childCount()));
    dayItem->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter);
  }
  for (int i{0}; i < m_tree->topLevelItemCount(); ++i) {
    if (m_tree->topLevelItem(i)->data(0, DAY_ROLE).isValid()) {
      m_tree->topLevelItem(i)->setExpanded(true); // newest day
      break;
    }
  }
  if (selected) {
    selected->parent()->setExpanded(true);
    m_tree->setCurrentItem(selected);
  }

  m_refreshBtn->setText(tr("Refresh"));
  m_refreshBtn->setEnabled(true);
  if (m_library->runs().isEmpty()) {
    auto *empty = new QTreeWidgetItem(m_tree);
    empty->setText(0, tr("No recorded experiments"));
    empty->setFlags(Qt::NoItemFlags);
  }
  applyFilter();
}

void DataAnalysisWidget::applyFilter() {
  QString const filter = m_filterEdit->text().trimmed();
  for (int i{0}; i < m_tree->topLevelItemCount(); ++i) {
    QTreeWidgetItem *dayItem = m_tree->topLevelItem(i);
    bool const       dayMatches{filter.isEmpty() || dayItem->text(0).contains(filter, Qt::CaseInsensitive)};
    bool             anyChild{false};
    for (int c{0}; c < dayItem->childCount(); ++c) {
      QTreeWidgetItem *item = dayItem->child(c);
      bool const       matches{dayMatches || item->toolTip(0).contains(filter, Qt::CaseInsensitive)};
      item->setHidden(!matches);
      anyChild = anyChild || matches;
    }
    dayItem->setHidden(!anyChild);
  }
}

void DataAnalysisWidget::openRun(ExperimentRun const &run) {
  if (m_editor->focusPage(run.key)) {
    return;
  }
  m_editor->addPage(new AnalysisRunPage(m_resHolder, m_library, m_notes, run));
}

void DataAnalysisWidget::openItem(QTreeWidgetItem *item) {
  if (!item) {
    return;
  }
  QString const key = item->data(0, RUN_KEY_ROLE).toString();
  if (key.isEmpty()) {
    item->setExpanded(!item->isExpanded());
    return;
  }
  if (auto run = m_library->findRun(key)) {
    openRun(*run);
  }
}

QDate DataAnalysisWidget::selectedDay() const {
  QTreeWidgetItem *item = m_tree->currentItem();
  return item ? item->data(0, DAY_ROLE).toDate() : QDate{};
}

void DataAnalysisWidget::openCustomRange() {
  QDateTime end = QDateTime::currentDateTime();
  if (QDate const day = selectedDay(); day.isValid()) {
    end = QDateTime(day, QTime(23, 59, 59));
  }
  auto *dialog = new DateRangeDialog(end.addSecs(-6 * 3600), end, this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  connect(dialog, &DateRangeDialog::dateSelected, this, [this](QDateTime const &from, QDateTime const &to) {
    if (from >= to) {
      (new MessageDialog(tr("Open range"), tr("The end of the range must be after its start")))->open();
      return;
    }
    openRun(m_library->makeRangeRun(from.toSecsSinceEpoch(), to.toSecsSinceEpoch()));
  });
  dialog->open();
}

} // namespace deepvaccontrol
