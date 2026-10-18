#include "exportdialog.h"

#include <QButtonGroup>
#include "datetimefield.h"
#include <QDir>
#include <QFileDialog>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

#include "constants.h"
#include "dataviewer.h"
#include "stringpathprovider.h"
#include "theme.h"
#include "utils.h"

namespace deepvaccontrol {

namespace {
int px(double value) {
  return qMax(1, static_cast<int>(value * utils::ssf()));
}

QLabel *caption(QString const &text, QWidget *parent) {
  auto *label = new QLabel(text.toUpper(), parent);
  label->setProperty("muted", true);
  QFont font = label->font();
  font.setPointSizeF(font.pointSizeF() * 0.82);
  font.setBold(true);
  font.setLetterSpacing(QFont::PercentageSpacing, 108);
  label->setFont(font);
  return label;
}

QFrame *card(QWidget *parent) {
  auto *frame = new QFrame(parent);
  frame->setObjectName("exportCard");
  theme::Colors const &c = theme::colors();
  frame->setStyleSheet(QStringLiteral("QFrame#exportCard { background: %1; border: %2px solid %3; border-radius: %4px; }")
                         .arg(c.panelAlt.name())
                         .arg(px(1))
                         .arg(c.border.name())
                         .arg(px(8)));
  return frame;
}

struct Format {
  char const *id;
  char const *label;
};
} // namespace

ExportDialog::ExportDialog(ResHolder *resHolder, QDate const &day, bool canReport, bool canBundle, bool canUseFiles, bool canComparisonCsv,
                           QWidget *parent, bool embedded)
    : QDialog{parent}, m_resHolder{resHolder}, m_day{day} {
  setWindowTitle(tr("Export"));
  if (embedded) {
    setWindowFlags(Qt::Widget);
  }
  auto *lay = new QVBoxLayout(this);
  lay->setContentsMargins(embedded ? QMargins(0, 0, px(8), 0) : QMargins(px(20), px(18), px(20), px(16)));
  lay->setSpacing(px(12));

  auto *title = new QLabel(tr("Export"), this);
  QFont font  = title->font();
  font.setBold(true);
  font.setPointSizeF(font.pointSizeF() * 1.25);
  title->setFont(font);
  title->setVisible(!embedded); // the panel has its own
  lay->addWidget(title);

  QBoxLayout *columns = embedded ? static_cast<QBoxLayout *>(new QVBoxLayout()) : static_cast<QBoxLayout *>(new QHBoxLayout());
  columns->setSpacing(px(12));
  lay->addLayout(columns, embedded ? 0 : 1);

  // ---- what: file types as chips, in three groups
  auto *formatCard = card(this);
  auto *formatLay  = new QVBoxLayout(formatCard);
  formatLay->setContentsMargins(px(14), px(12), px(14), px(14));
  formatLay->setSpacing(px(6));
  m_formats = new QButtonGroup(this);
  m_formats->setExclusive(true);
  auto addGroup = [&](QString const &name, QString const &note, std::initializer_list<Format> formats, bool enabled) {
    formatLay->addWidget(caption(name, formatCard));
    auto *row = new QHBoxLayout();
    row->setSpacing(px(6));
    for (Format const &format : formats) {
      auto *chip = new QPushButton(tr(format.label), formatCard);
      chip->setCheckable(true);
      chip->setMinimumSize(px(64), px(30));
      chip->setFocusPolicy(Qt::NoFocus);
      chip->setProperty("format", QString(format.id));
      chip->setEnabled(enabled);
      if (!enabled) {
        chip->setToolTip(note);
      }
      m_formats->addButton(chip);
      row->addWidget(chip);
    }
    row->addStretch();
    formatLay->addLayout(row);
    formatLay->addSpacing(px(6));
  };
  addGroup(tr("Chart image"), QString{}, {{"png", "PNG"}, {"bmp", "BMP"}, {"jpeg", "JPEG"}}, true);
  addGroup(tr("Data"), QString{}, {{"csv", "CSV"}, {"txt", "TXT"}}, true);
  formatLay->addWidget(caption(tr("Experiment"), formatCard));
  {
    auto *row = new QHBoxLayout();
    row->setSpacing(px(6));
    for (auto const &[id, label, enabled, note] :
         {std::tuple{"html", QT_TR_NOOP("Report HTML"), canReport, tr("Reports need the experiment list")},
          std::tuple{"pdf", QT_TR_NOOP("Report PDF"), canReport, tr("Reports need the experiment list")},
          std::tuple{"json", QT_TR_NOOP("Bundle JSON"), canBundle, tr("Only for experiments saved in the experiment database")}}) {
      auto *chip = new QPushButton(tr(label), formatCard);
      chip->setCheckable(true);
      chip->setMinimumSize(px(64), px(30));
      chip->setFocusPolicy(Qt::NoFocus);
      chip->setProperty("format", QString(id));
      chip->setEnabled(enabled);
      if (!enabled) {
        chip->setToolTip(note);
      }
      m_formats->addButton(chip);
      row->addWidget(chip);
    }
    row->addStretch();
    formatLay->addLayout(row);
  }
  if (canComparisonCsv) {
    formatLay->addSpacing(px(6));
    addGroup(tr("Comparison"), QString{}, {{"comparison-csv", QT_TR_NOOP("Comparison CSV")}}, true);
  }
  formatLay->addStretch();
  m_formats->buttons().first()->setChecked(true);
  columns->addWidget(formatCard, embedded ? 0 : 3);

  // ---- which range
  auto *rangeCard = card(this);
  auto *rangeLay  = new QVBoxLayout(rangeCard);
  rangeLay->setContentsMargins(px(14), px(12), px(14), px(14));
  rangeLay->setSpacing(px(6));
  rangeLay->addWidget(caption(tr("Range"), rangeCard));
  m_ranges = new QButtonGroup(this);
  for (auto const &[id, text] : {std::pair{Range::VIEW, tr("Visible view")},
                                 std::pair{Range::EXPERIMENT, tr("Whole experiment shown")},
                                 std::pair{Range::DAY, tr("Day %1").arg(m_day.toString("yyyy-MM-dd"))},
                                 std::pair{Range::CUSTOM, tr("Custom")}}) {
    auto *radio = new QRadioButton(text, rangeCard);
    m_ranges->addButton(radio, static_cast<int>(id));
    rangeLay->addWidget(radio);
  }
  m_ranges->button(static_cast<int>(Range::VIEW))->setChecked(true);
  for (Range const fromFiles : {Range::DAY, Range::CUSTOM}) {
    m_ranges->button(static_cast<int>(fromFiles))->setVisible(canUseFiles);
  }
  auto *customGrid = new QGridLayout();
  customGrid->setContentsMargins(px(24), 0, 0, 0);
  customGrid->setHorizontalSpacing(px(8));
  QDateTime const dayStart(m_day, QTime(0, 0));
  m_fromEdit = new DateTimeField(dayStart, rangeCard); // date from a calendar, time from the number pad
  m_toEdit   = new DateTimeField(QDateTime(m_day, QTime(23, 59, 59)), rangeCard);
  int row{0};
  for (auto const &[label, edit] : {std::pair{tr("From"), m_fromEdit}, std::pair{tr("To"), m_toEdit}}) {
    auto *name = new QLabel(label, rangeCard);
    name->setVisible(canUseFiles);
    customGrid->addWidget(name, row, 0);
    customGrid->addWidget(edit, row, 1);
    ++row;
  }
  rangeLay->addLayout(customGrid);
  for (QWidget *edit : std::initializer_list<QWidget *>{m_fromEdit, m_toEdit}) {
    edit->setVisible(canUseFiles);
  }
  m_rangeNote = new QLabel(rangeCard);
  m_rangeNote->setProperty("muted", true);
  m_rangeNote->setWordWrap(true);
  rangeLay->addWidget(m_rangeNote);
  rangeLay->addStretch();
  columns->addWidget(rangeCard, embedded ? 0 : 2);

  // ---- buttons
  auto *buttons = new QHBoxLayout();
  m_previewBtn  = new QPushButton(tr("Preview"), this);
  auto *cancelBtn = new QPushButton(tr("Cancel"), this);
  auto *exportBtn = new QPushButton(tr("Export…"), this);
  m_previewBtn->setProperty("outline", true);
  cancelBtn->setProperty("outline", true);
  exportBtn->setObjectName("primaryButton");
  exportBtn->setDefault(true);
  for (QPushButton *button : {m_previewBtn, cancelBtn, exportBtn}) {
    button->setMinimumSize(px(100), px(32));
    button->setFocusPolicy(Qt::NoFocus);
  }
  buttons->addWidget(m_previewBtn);
  if (!embedded) {
    buttons->addStretch();
  }
  buttons->addWidget(cancelBtn);
  buttons->addWidget(exportBtn, embedded ? 1 : 0);
  cancelBtn->setVisible(!embedded); // the panel closes with its ×
  if (embedded) {
    lay->addStretch();
  }
  lay->addLayout(buttons);

  connect(m_formats, &QButtonGroup::buttonToggled, this, &ExportDialog::updateState);
  connect(m_ranges, &QButtonGroup::idToggled, this, &ExportDialog::updateState);
  connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
  connect(exportBtn, &QPushButton::clicked, this, &ExportDialog::choosePath);
  connect(m_previewBtn, &QPushButton::clicked, this, [this] {
    auto *preview = new DataViewerDialog(m_resHolder->logger, readInterval(), m_resHolder->settings->settingsMap()["states"].toList(), this);
    preview->open();
  });

  updateState();
  if (!embedded) {
    resize(px(720), sizeHint().height());
  }
}

QString ExportDialog::format() const {
  return m_formats->checkedButton() ? m_formats->checkedButton()->property("format").toString() : QString{};
}

ExportDialog::Range ExportDialog::range() const {
  return static_cast<Range>(m_ranges->checkedId());
}

QDateTime ExportDialog::from() const {
  return (range() == Range::DAY) ? QDateTime(m_day, QTime(0, 0)) : std::min(m_fromEdit->dateTime(), m_toEdit->dateTime());
}

QDateTime ExportDialog::to() const {
  return (range() == Range::DAY) ? QDateTime(m_day, QTime(23, 59, 59)) : std::max(m_fromEdit->dateTime(), m_toEdit->dateTime());
}

QString ExportDialog::filePath() const {
  return m_filePath;
}

bool ExportDialog::isImage(QString const &format) {
  return QStringList{"png", "bmp", "jpeg"}.contains(format);
}

bool ExportDialog::isData(QString const &format) {
  return QStringList{"csv", "txt"}.contains(format);
}

void ExportDialog::updateState() {
  QString const fmt = format();
  bool const    comparisonCsv{fmt == QLatin1String("comparison-csv")};
  bool const    bundle{(fmt == QLatin1String("json")) || comparisonCsv};
  // a bundle is the whole saved experiment
  for (QAbstractButton *radio : m_ranges->buttons()) {
    radio->setEnabled(!bundle);
  }
  bool const custom{!bundle && (range() == Range::CUSTOM)};
  m_fromEdit->setEnabled(custom);
  m_toEdit->setEnabled(custom);
  bool const fromFiles{!bundle && ((range() == Range::DAY) || (range() == Range::CUSTOM))};
  m_previewBtn->setVisible(fromFiles && (isImage(fmt) || isData(fmt)));
  m_rangeNote->setText(comparisonCsv ? tr("One row per experiment and channel (min, max, mean, final), on the comparison's time axis.")
                       : bundle  ? tr("The bundle holds the whole experiment as saved in the database.")
                       : fromFiles ? tr("Read from the recorded data of this computer.")
                                   : QString{});
}

void ExportDialog::choosePath() {
  QString const extension = (format() == QLatin1String("comparison-csv")) ? QStringLiteral("csv") : format();
  if (extension.isEmpty()) {
    return;
  }
  QDir().mkpath(deepvaccontrol::EXPORT_DIR);
  QString const suggested = QDir(deepvaccontrol::EXPORT_DIR)
                              .absoluteFilePath(QStringLiteral("chart_%1.%2").arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"), extension));
  QString path = QFileDialog::getSaveFileName(this, tr("Export"), suggested, QStringLiteral("%1 (*.%2)").arg(extension.toUpper(), extension));
  if (path.isEmpty()) {
    return;
  }
  if (!path.endsWith("." + extension, Qt::CaseInsensitive)) {
    path += "." + extension;
  }
  m_filePath = path;
  accept();
}

std::shared_ptr<DataHolder> ExportDialog::readInterval() const {
  auto         interval = std::make_shared<DataHolder>(m_resHolder->states->size(), m_resHolder->logger, 0);
  QDateTime const start = from();
  QDateTime const end   = to();

  // the interval files (one per day) covering the range
  QDir const        dir(INTERVALS_DIR);
  QStringList const files = dir.entryList(QDir::Files, QDir::Name);
  for (QDate date = start.date(); date <= end.date(); date = date.addDays(1)) {
    double const  key{utils::calculateKey(QDateTime(date, QTime(12, 0)).toSecsSinceEpoch())};
    QString const name = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(key)).toString("yyyy-MM-dd_HH-mm-ss") + ".dat";
    if (!files.contains(name)) {
      continue;
    }
    StringPathProvider path(INTERVALS_DIR + name, m_resHolder->logger);
    auto dayData = std::make_shared<DataHolder>(m_resHolder->states->size(), m_resHolder->logger, 0);
    dayData->loadFromFile(&path);
    dayData->slice(static_cast<double>(start.toSecsSinceEpoch()), static_cast<double>(end.toSecsSinceEpoch()));
    interval->append(dayData.get());
  }
  return interval;
}

} // namespace deepvaccontrol
