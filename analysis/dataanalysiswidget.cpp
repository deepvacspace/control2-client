#include "dataanalysiswidget.h"

#include <QDateTime>
#include <QDir>
#include <QRegularExpression>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLocale>
#include <QFrame>
#include <QStyledItemDelegate>
#include <QPainter>
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
#include "theme.h"
#include "utils.h"

namespace deepvaccontrol {

namespace {
constexpr int RUN_KEY_ROLE{Qt::UserRole};
constexpr int DAY_ROLE{Qt::UserRole + 1};
constexpr int SUB_ROLE{Qt::UserRole + 2};    // second line of an experiment: duration · samples
constexpr int HEADER_ROLE{Qt::UserRole + 3}; // section title (saved in database / recorded days)
constexpr int COUNT_ROLE{Qt::UserRole + 4};  // count shown on the right of sections and days

// Experiments as two-line entries, sections as small capitals, days as plain rows.
class ExperimentItemDelegate : public QStyledItemDelegate {
public:
  using QStyledItemDelegate::QStyledItemDelegate;

  QSize sizeHint(QStyleOptionViewItem const &option, QModelIndex const &index) const override {
    QFontMetrics const fm(option.font);
    double const       s{utils::ssf()};
    int const          lines{index.data(SUB_ROLE).isValid() ? 2 : 1};
    return {option.rect.width(), static_cast<int>(fm.height() * lines + (lines == 2 ? 14 : 10) * s)};
  }

  void paint(QPainter *p, QStyleOptionViewItem const &option, QModelIndex const &index) const override {
    theme::Colors const &c = theme::colors();
    double const         s{utils::ssf()};
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    QRectF const r      = QRectF(option.rect).adjusted(2 * s, 1 * s, -2 * s, -1 * s);
    bool const   header = index.data(HEADER_ROLE).toBool();
    bool const   active = (option.state & QStyle::State_Selected) && !header;
    if (active) {
      p->setPen(QPen(c.accent, 1 * s));
      p->setBrush(QColor(c.accent.red(), c.accent.green(), c.accent.blue(), theme::isDark() ? 50 : 30));
      p->drawRoundedRect(r, 5 * s, 5 * s);
    } else if (option.state & QStyle::State_MouseOver && !header) {
      p->setPen(Qt::NoPen);
      p->setBrush(QColor(c.accent.red(), c.accent.green(), c.accent.blue(), 14));
      p->drawRoundedRect(r, 5 * s, 5 * s);
    }
    QRectF const text = r.adjusted(8 * s, 0, -8 * s, 0);
    QString const count = index.data(COUNT_ROLE).toString();
    QFont         font  = option.font;
    if (header) {
      font.setPointSizeF(option.font.pointSizeF() * 0.8);
      font.setBold(true);
      font.setLetterSpacing(QFont::PercentageSpacing, 108);
      p->setFont(font);
      p->setPen(c.muted);
      p->drawText(text, Qt::AlignVCenter | Qt::AlignLeft, index.data().toString().toUpper());
      p->drawText(text, Qt::AlignVCenter | Qt::AlignRight, count);
    } else if (index.data(SUB_ROLE).isValid()) {
      QRectF const top(text.left(), text.top() + 4 * s, text.width(), text.height() / 2 - 4 * s);
      QRectF const bottom(text.left(), text.center().y(), text.width(), text.height() / 2 - 4 * s);
      font.setBold(true);
      p->setFont(font);
      p->setPen(c.text);
      p->drawText(top, Qt::AlignVCenter | Qt::AlignLeft,
                  QFontMetrics(font).elidedText(index.data().toString(), Qt::ElideRight, static_cast<int>(top.width())));
      QFont small = option.font;
      small.setPointSizeF(option.font.pointSizeF() * 0.82);
      p->setFont(small);
      p->setPen(c.muted);
      p->drawText(bottom, Qt::AlignVCenter | Qt::AlignLeft, index.data(SUB_ROLE).toString());
    } else {
      p->setFont(font);
      p->setPen(index.flags() == Qt::NoItemFlags ? c.muted : c.text);
      p->drawText(text, Qt::AlignVCenter | Qt::AlignLeft, index.data().toString());
      p->setPen(c.muted);
      p->drawText(text, Qt::AlignVCenter | Qt::AlignRight, count);
    }
    p->restore();
  }
};

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
  m_browser = new QFrame(this);
  m_browser->setObjectName("pageCard");
  auto *browserLay = new QVBoxLayout(m_browser);
  browserLay->setContentsMargins(10 * utils::ssf(), 10 * utils::ssf(), 10 * utils::ssf(), 10 * utils::ssf());
  browserLay->setSpacing(8 * utils::ssf());

  auto *hideBtn = makeButton(QStringLiteral("‹"), m_browser);
  hideBtn->setFixedWidth(40 * utils::ssf());
  hideBtn->setToolTip(tr("Hide the experiment list"));

  m_filterEdit = new QLineEdit(m_browser);
  m_filterEdit->setPlaceholderText(tr("Filter: date, name, DUT, tag…"));
  m_filterEdit->setClearButtonEnabled(true);
  m_filterEdit->setMinimumHeight(34 * utils::ssf());
  connect(m_filterEdit, &QLineEdit::textChanged, this, &DataAnalysisWidget::applyFilter);
  browserLay->addWidget(m_filterEdit);

  m_tree = new QTreeWidget(m_browser);
  m_tree->setColumnCount(3);
  m_tree->setHeaderLabels({tr("Date"), tr("Duration"), tr("Pts")});
  m_tree->headerItem()->setToolTip(2, tr("Number of samples"));
  m_tree->headerItem()->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter);
  m_tree->setRootIsDecorated(false);
  m_tree->setUniformRowHeights(false);
  m_tree->setAlternatingRowColors(false);
  m_tree->setHeaderHidden(true);
  m_tree->setColumnHidden(1, true); // duration and samples are on the second line of each experiment
  m_tree->setColumnHidden(2, true);
  m_tree->setItemDelegate(new ExperimentItemDelegate(m_tree));
  m_tree->setMouseTracking(true);
  m_tree->setFrameShape(QFrame::NoFrame);
  m_tree->setIndentation(12 * utils::ssf());
  m_tree->header()->setStretchLastSection(false);
  m_tree->header()->setMinimumSectionSize(10);
  m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  m_tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
  m_tree->setStyleSheet(QStringLiteral("QTreeWidget { background: transparent; border: none; }"));
  // touch friendly: one tap opens an experiment or expands / collapses a day
  connect(m_tree, &QTreeWidget::itemClicked, this, &DataAnalysisWidget::openItem);

  m_refreshBtn       = makeButton(tr("Refresh"), m_browser);
  auto *openRangeBtn = makeButton(tr("Range…"), m_browser);
  m_refreshBtn->setToolTip(tr("Search the recorded data for new experiments"));
  openRangeBtn->setToolTip(tr("Open an arbitrary date / time range"));
  connect(m_refreshBtn, &QPushButton::clicked, m_library, &ExperimentLibrary::refresh);
  connect(openRangeBtn, &QPushButton::clicked, this, &DataAnalysisWidget::openCustomRange);
  auto *buttonsLay = new QHBoxLayout();
  buttonsLay->setSpacing(6 * utils::ssf());
  buttonsLay->addWidget(m_refreshBtn, 1);
  buttonsLay->addWidget(openRangeBtn, 1);
  buttonsLay->addWidget(hideBtn);

  browserLay->addWidget(m_tree, 1);
  browserLay->addLayout(buttonsLay);

  auto *splitter = new QSplitter(Qt::Horizontal, this);
  splitter->setHandleWidth(10 * utils::ssf());
  splitter->setStyleSheet(QStringLiteral("QSplitter::handle { background: transparent; }"));
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

  // #N as used everywhere (assistant, page header): oldest experiment is #1
  QHash<QString, int>          numbers;
  QVector<ExperimentRun> const all = m_library->chronological();
  for (int i{0}; i < all.size(); ++i) {
    numbers.insert(all[i].key, i + 1);
  }
  auto subtitle = [](ExperimentRun const &run) {
    return tr("%1 · %2 samples").arg(ExperimentLibrary::formatDuration(run.duration())).arg(QLocale().toString(run.samples));
  };

  // experiments saved in the database (recorded here, imported or received from paired computers), newest first
  if (!m_library->storedRuns().isEmpty()) {
    auto *dbItem = new QTreeWidgetItem(m_tree);
    dbItem->setText(0, tr("Saved in database"));
    dbItem->setData(0, HEADER_ROLE, true);
    dbItem->setData(0, COUNT_ROLE, QString::number(m_library->storedRuns().size()));
    dbItem->setFlags(Qt::ItemIsEnabled);
    for (ExperimentRun const &run : m_library->storedRuns()) {
      auto *item = new QTreeWidgetItem(dbItem);
      item->setText(0, QStringLiteral("#%1 · %2").arg(numbers.value(run.key)).arg(run.title));
      item->setText(1, ExperimentLibrary::formatDuration(run.duration()));
      item->setText(2, QString::number(run.samples));
      item->setData(0, SUB_ROLE, subtitle(run));
      item->setData(0, RUN_KEY_ROLE, run.key);
      item->setToolTip(0, run.details.isEmpty() ? QStringLiteral("%1\n%2").arg(run.title, run.chamber)
                                                : QStringLiteral("%1\n%2\n%3").arg(run.title, run.chamber, run.details));
      if (run.key == selectedKey) {
        selected = item;
      }
    }
    dbItem->setExpanded(true);
  }
  if (!m_library->runs().isEmpty()) {
    auto *daysHeader = new QTreeWidgetItem(m_tree);
    daysHeader->setText(0, tr("Recorded days"));
    daysHeader->setData(0, HEADER_ROLE, true);
    daysHeader->setFlags(Qt::NoItemFlags);
  }
  for (ExperimentRun const &run : m_library->runs()) {
    QDateTime const start = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(run.start));
    QDate const     day   = start.date();

    QTreeWidgetItem *dayItem = dayItems.value(day);
    if (!dayItem) {
      dayItem = new QTreeWidgetItem(m_tree);
      dayItem->setText(0, day == QDate::currentDate() ? tr("%1 (today)").arg(day.toString("yyyy-MM-dd")) : day.toString("yyyy-MM-dd, ddd"));
      dayItem->setData(0, DAY_ROLE, day);
      dayItems.insert(day, dayItem);
    }

    auto *item = new QTreeWidgetItem(dayItem);
    QDateTime const end = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(run.end));
    item->setText(0, QStringLiteral("#%1 · %2 – %3")
                       .arg(numbers.value(run.key))
                       .arg(start.toString("HH:mm:ss"), end.date() == day ? end.toString("HH:mm") : end.toString("dd.MM HH:mm")));
    item->setText(1, ExperimentLibrary::formatDuration(run.duration()));
    item->setText(2, QString::number(run.samples));
    item->setData(0, SUB_ROLE, subtitle(run));
    item->setData(0, RUN_KEY_ROLE, run.key);
    item->setData(0, DAY_ROLE, day);
    item->setToolTip(0, run.title);
    if (run.key == selectedKey) {
      selected = item;
    }
  }

  for (QTreeWidgetItem *dayItem : std::as_const(dayItems)) {
    dayItem->setText(2, QString::number(dayItem->childCount()));
    dayItem->setData(0, COUNT_ROLE, QString::number(dayItem->childCount()));
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
