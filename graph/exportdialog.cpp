#include "exportdialog.h"

#include <QButtonGroup>
#include <QDir>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QPushButton>
#include <QRadioButton>

#include "constants.h"
#include "datamanager.h"
#include "dataviewer.h"
#include "daterangedialog.h"
#include "filesavedialogpathprovider.h"
#include "utils.h"

namespace control2 {
ExportDialog::ExportDialog(ResHolder *resHolder, std::shared_ptr<DataHolder> interval, QWidget *parent)
    : QDialog{parent}, m_resHolder{resHolder}, m_interval{interval} {
  // boxes / btns
  auto *mainLay = new QVBoxLayout(this);

  // range select | type select
  auto *boxesLay = new QHBoxLayout();

  auto *rangeBox = new QGroupBox(tr("Диапазон"), this);
  auto *rangeLay = new QVBoxLayout(rangeBox);
  rangeLay->setSpacing(2 * utils::ssf());

  auto *formatBox = new QGroupBox(tr("Тип файла"), this);
  auto *formatLay = new QVBoxLayout(formatBox);
  formatLay->setSpacing(2 * utils::ssf());

  boxesLay->addWidget(rangeBox);
  boxesLay->addWidget(formatBox);

  auto *dateRangeBox    = new QGroupBox(this);
  auto *dateRangeBoxLay = new QGridLayout(dateRangeBox);
  dateRangeBoxLay->setContentsMargins(8 * utils::ssf(), 0, 8 * utils::ssf(), 12 * utils::ssf());

  m_datesLabel = new QLabel("date1\ndate2", this);
  m_datesLabel->setWordWrap(true);
  m_datesLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

  auto *changeDatesBtn = new QPushButton(tr("Изменить"), this);
  auto *previewBtn     = new QPushButton(tr("Предпросмотр"), this);
  changeDatesBtn->setFixedSize(100 * utils::ssf(), 40 * utils::ssf());
  previewBtn->setFixedSize(100 * utils::ssf(), 40 * utils::ssf());

  dateRangeBoxLay->addWidget(m_datesLabel, 0, 0, 1, 2);
  dateRangeBoxLay->addWidget(previewBtn, 1, 0);
  dateRangeBoxLay->addWidget(changeDatesBtn, 1, 1);
  dateRangeBoxLay->setAlignment(m_datesLabel, Qt::AlignLeft);

  QList<QRadioButton *> rangeBtnsList;
  auto                 *rangeTypeGroup = new QButtonGroup(this); // not a widget

  auto *currentViewRangeRadioBtn = new QRadioButton(tr("Текущая область видимости"), this);
  rangeTypeGroup->addButton(currentViewRangeRadioBtn, 0);
  rangeBtnsList.push_back(currentViewRangeRadioBtn);
  auto *currentDayRangeRadioBtn = new QRadioButton(tr("Текущий день"), this);
  rangeTypeGroup->addButton(currentDayRangeRadioBtn, 1);
  rangeBtnsList.push_back(currentDayRangeRadioBtn);
  auto *selectableRangeRadioBtn = new QRadioButton(tr("Настраиваемый"), this);
  rangeTypeGroup->addButton(selectableRangeRadioBtn, 2);
  rangeBtnsList.push_back(selectableRangeRadioBtn);

  for (auto &btn : rangeBtnsList) {
    if (utils::ssf() > 1) {
      btn->setStyleSheet(utils::readStyleFromFile(":/styles/radioBtnBig.qss")
                         + QString("QRadioButton::indicator{width: %1px; height: %1px;}").arg(20 * utils::ssf()));
    }
    rangeLay->addWidget(btn);
  }
  rangeLay->addWidget(dateRangeBox);
  rangeLay->addStretch();

  auto *formatTypeGroup = new QButtonGroup(this); // not a widget

  QStringList const formatsList{"txt", "csv", "bmp", "png", "jpeg"};
  for (int i{0U}; i < formatsList.size(); ++i) {
    auto *btn = new QRadioButton(formatsList[i], this);
    if (utils::ssf() > 1) {
      btn->setStyleSheet(utils::readStyleFromFile(":/styles/radioBtnBig.qss")
                         + QString("QRadioButton::indicator{width: %1px; height: %1px;}").arg(20 * utils::ssf()));
    }
    formatTypeGroup->addButton(btn, i);
    formatLay->addWidget(btn);
  }
  formatLay->addStretch();

  auto btnsLay   = new QHBoxLayout();
  auto cancelBtn = new QPushButton(tr("Отмена"), this);
  auto saveBtn   = new QPushButton(tr("Далее"), this);

  cancelBtn->setFixedSize(120 * utils::ssf(), 40 * utils::ssf());
  saveBtn->setFixedSize(120 * utils::ssf(), 40 * utils::ssf());

  btnsLay->addWidget(cancelBtn);
  btnsLay->addStretch();
  btnsLay->addWidget(saveBtn);

  mainLay->addLayout(boxesLay);
  mainLay->addLayout(btnsLay);

  rangeTypeGroup->button(0)->setChecked(true);
  formatTypeGroup->button(0)->setChecked(true);

  dateRangeBox->setEnabled(false);

  connect(selectableRangeRadioBtn, &QRadioButton::toggled, dateRangeBox, &QGroupBox::setEnabled);

  connect(changeDatesBtn, &QPushButton::clicked, this, [this] {
    auto *dateDialog = new DateRangeDialog(m_dateStart, m_dateEnd, this);
    connect(dateDialog, &DateRangeDialog::dateSelected, this, &ExportDialog::updateDates);
    dateDialog->open();
  });

  connect(previewBtn, &QPushButton::clicked, this, [this] {
    compileCustomInterval();
    auto *previewDialog = new DataViewerDialog(
      m_resHolder->logger, m_interval, m_resHolder->settings->settingsMap()["states"].toList(), this);
    previewDialog->open();
  });

  connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
  connect(saveBtn, &QPushButton::clicked, this, [this, formatTypeGroup, rangeTypeGroup, selectableRangeRadioBtn]() {
    m_selectedFileExtension = formatTypeGroup->checkedButton()->text();
    m_selectedType          = rangeTypeGroup->checkedId();
    if (selectableRangeRadioBtn->isChecked()) {
      compileCustomInterval();
    }
    auto sourceList = QStringList{control2::EXPORT_DIR};
    sourceList.append(m_resHolder->usb->getAvailableDrives());
    auto *pathProvider = new FileSaveDialogPathProvider(sourceList, m_resHolder->logger, this);
    pathProvider->getPathAsync(this, QString{}, [this](QString const &filePath) {
      m_selectedFilePath = filePath;
      accept();
    });
  });

  auto dt = QDateTime::currentDateTime();
  dt.setTime(QTime(12, 0, 0));
  updateDates(dt.addDays(-2), dt);

  this->setWindowTitle(tr("Экспорт данных"));
  // this->setAttribute(Qt::WA_DeleteOnClose);
  this->adjustSize();
  setFixedSize(size());
}

void ExportDialog::updateDates(QDateTime rangeStart, QDateTime rangeEnd) {
  m_dateStart = rangeStart;
  m_dateEnd   = rangeEnd;

  m_datesLabel->setText(tr("начало") + ": " + m_dateStart.toString("yyyy-MM-dd HH:mm:ss") + "\r\n" + tr("конец") + ": "
                        + m_dateEnd.toString("yyyy-MM-dd HH:mm:ss"));
}

void ExportDialog::compileCustomInterval() {
  QList<double> possibleKeys;

  qint64 daysDiff{m_dateStart.daysTo(m_dateEnd)};
  auto   dateFromInterval = m_dateStart;
  while (daysDiff >= 0) {
    possibleKeys.push_back(utils::calculateKey(dateFromInterval.toSecsSinceEpoch()));
    dateFromInterval = dateFromInterval.addDays(1);
    --daysDiff;
  }

  QDir       dir(INTERVALS_DIR);
  auto const intervalFiles = dir.entryList(QDir::Files, QDir::Name);

  QStringList filesList;
  for (int i{0U}; i < possibleKeys.size(); ++i) {
    QString const intervalFileName{
      QDateTime::fromSecsSinceEpoch(utils::calculateKey(possibleKeys[i])).toString("yyyy-MM-dd_HH-mm-ss") + ".dat"};

    if (intervalFiles.contains(intervalFileName)) {
      filesList.push_back(intervalFileName);
    }
  }

  double const secsStart{static_cast<double>(m_dateStart.toSecsSinceEpoch())};
  double const secsEnd{static_cast<double>(m_dateEnd.toSecsSinceEpoch())};

  m_interval->clean();
  while (!filesList.empty()) {
    StringPathProvider path(INTERVALS_DIR + filesList.first(), m_resHolder->logger);
    auto               dataInterval = std::make_shared<DataHolder>(m_resHolder->states->size(), m_resHolder->logger, 0);
    dataInterval->loadFromFile(&path);
    dataInterval->slice(secsStart, secsEnd);
    m_interval->append(dataInterval.get());
    filesList.removeFirst();
  }
}
} // namespace control2
