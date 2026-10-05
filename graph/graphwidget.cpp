#include "graphwidget.h"

#include <QFrame>
#include <QGridLayout>
#include <QMenu>
#include <cmath>

#include "calendardialog.h"
#include "constants.h"
#include "dataexporter.h"
#include "exportdialog.h"
#include "theme.h"
#include "utils.h"

namespace deepvaccontrol {
GraphWidget::GraphWidget(ResHolder *resHolder, QWidget *parent)
    : QWidget{parent}, m_resHolder{resHolder}, m_styleEditor{nullptr} {
  auto *mainLay    = new QGridLayout(this);
  auto *yAxisTools = new QVBoxLayout();
  auto *xAxisTools = new QHBoxLayout();

  // check if 'pres' param in settings has notation parameter
  bool                  addLogAxis{false};
  QList<QVariant> const states_list = resHolder->settings->settingsMap()["states"].toList();
  QVector<int>          notationIndexes{};
  {
    QVariantMap params_map{};
    for (int i{0U}; i < states_list.size(); ++i) {
      QVariant const &param{states_list[i]};
      auto            paramMap = param.toMap();
      if (paramMap.contains("notation") && paramMap["notation"].toString() == "scientific") {
        addLogAxis = true;
        notationIndexes.push_back(i);
      }
    }
  }

  m_dataViewer = new DataViewer(resHolder->logger, addLogAxis, this);
  if (addLogAxis) {
    m_dataViewer->setIndexesForLogAxis(notationIndexes);
  }
  m_dataViewer->setStyles(resHolder->settings->settingsMap()["states"].toList());
  if (resHolder->settings->settingsMap()["app_settings"].isValid()
      && resHolder->settings->settingsMap()["app_settings"].toMap()["graphUpdatePeriod"].isValid()) {
    m_dataViewer->setRefreshPeriodMs(
      1000 * resHolder->settings->settingsMap()["app_settings"].toMap()["graphUpdatePeriod"].toString().toInt());
  }
  m_styleEditor = new GraphStyleEditor(resHolder, this);
  double const         ssf{utils::ssf()};
  theme::Colors const &c = theme::colors();

  // whole page in one card
  mainLay->setContentsMargins(0, 0, 0, 0);
  auto *card = new QFrame(this);
  card->setObjectName("pageCard");
  mainLay->addWidget(card, 0, 0);
  auto *cardLay = new QGridLayout(card);
  cardLay->setContentsMargins(14 * ssf, 12 * ssf, 14 * ssf, 12 * ssf);
  cardLay->setHorizontalSpacing(8 * ssf);
  cardLay->setVerticalSpacing(8 * ssf);

  auto iconButton = [&](QString const &kind, QString const &tip) {
    auto *button = new QPushButton(card);
    button->setIcon(theme::menuIcon(kind, c.text, c.text, static_cast<int>(14 * ssf)));
    button->setIconSize(QSize(14 * ssf, 14 * ssf));
    button->setFixedSize(26 * ssf, 26 * ssf);
    button->setToolTip(tip);
    return button;
  };
  auto textButton = [&](QString const &text) {
    auto *button = new QPushButton(text, card);
    button->setFixedHeight(26 * ssf);
    return button;
  };

  // value axis (left) and time axis (below) controls
  auto *zoomYBtn = iconButton("plus", tr("Zoom in (values)"));
  auto *outYBtn  = iconButton("minus", tr("Zoom out (values)"));
  auto *incYBtn  = iconButton("up", tr("Move up"));
  auto *decYBtn  = iconButton("down", tr("Move down"));
  auto *zoomXBtn = iconButton("plus", tr("Zoom in (time)"));
  auto *outXBtn  = iconButton("minus", tr("Zoom out (time)"));
  auto *decXBtn  = iconButton("left", tr("Earlier"));
  auto *incXBtn  = iconButton("right", tr("Later"));
  auto *fitBtn   = textButton(tr("Fit"));
  fitBtn->setMinimumWidth(52 * ssf);
  fitBtn->setToolTip(tr("Show the whole day and follow new values"));

  // top bar: export, channels, view mode | read-out, day
  m_exportToBtn = textButton(tr("Export"));
  m_exportToBtn->setMinimumWidth(70 * ssf);
  m_styleBtn = textButton(tr("Channels ▾"));
  m_styleBtn->setMinimumWidth(90 * ssf);
  m_styleBtn->setToolTip(tr("Visible channels, colours and line styles"));
  m_modeSelectBtn = textButton(QString());
  m_modeSelectBtn->setMinimumWidth(120 * ssf);
  auto *modeMenu = new QMenu(m_modeSelectBtn);
  modeMenu->addAction(tr("Fit whole day"), this, [this] { m_dataViewer->setControlMode(DataViewer::ControlMode::LEAD_FULL); });
  modeMenu->addAction(tr("Follow latest"), this, [this] { m_dataViewer->setControlMode(DataViewer::ControlMode::LEAD_X); });
  modeMenu->addAction(tr("Free zoom"), this, [this] { m_dataViewer->setControlMode(DataViewer::ControlMode::ZOOM); });
  m_modeSelectBtn->setMenu(modeMenu);
  m_modeSelectBtn->setToolTip(tr("How the view follows the data"));

  m_readoutBtn = textButton(QString());
  m_readoutBtn->setObjectName("readoutBox");
  m_readoutBtn->setCheckable(true);
  m_readoutBtn->setMinimumWidth(320 * ssf);
  m_readoutBtn->setToolTip(tr("Latest values. Press to show a cursor and read the values under it."));
  auto *calendarBtn = textButton(QString());
  m_dayBtn          = calendarBtn;
  calendarBtn->setIcon(theme::menuIcon("calendar", c.text, c.text, static_cast<int>(13 * ssf)));
  calendarBtn->setIconSize(QSize(13 * ssf, 13 * ssf));
  calendarBtn->setMinimumWidth(110 * ssf);
  calendarBtn->setToolTip(tr("Show another recorded day"));

  auto *topBar = new QHBoxLayout();
  topBar->setSpacing(8 * ssf);
  topBar->addWidget(m_exportToBtn);
  topBar->addWidget(m_styleBtn);
  topBar->addWidget(m_modeSelectBtn);
  topBar->addStretch();
  topBar->addWidget(m_readoutBtn);
  topBar->addWidget(calendarBtn);
  cardLay->addLayout(topBar, 0, 0, 1, 2);

  for (QPushButton *button : {zoomYBtn, outYBtn, incYBtn, decYBtn}) {
    yAxisTools->addWidget(button);
  }
  yAxisTools->addStretch();
  yAxisTools->setSpacing(8 * ssf);
  cardLay->addLayout(yAxisTools, 1, 0);
  m_dataViewer->setParent(card);
  cardLay->addWidget(m_dataViewer, 1, 1);
  for (QPushButton *button : {zoomXBtn, outXBtn, decXBtn, incXBtn}) {
    xAxisTools->addWidget(button);
  }
  xAxisTools->addWidget(fitBtn);
  xAxisTools->addStretch();
  xAxisTools->setSpacing(8 * ssf);
  cardLay->addLayout(xAxisTools, 2, 1);
  cardLay->setColumnStretch(1, 1);
  cardLay->setRowStretch(1, 1);
  m_dataViewer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  m_dataViewer->setCompactLegend(true);

  setStyleSheet(QStringLiteral(R"(
QFrame#pageCard { background: %1; border: %2px solid %3; border-radius: %4px; }
QPushButton#readoutBox { background: %5; text-align: left; padding-left: %6px; }
QPushButton#readoutBox:checked { border-color: %7; }
)")
                  .arg(c.panel.name())
                  .arg(qMax(1, static_cast<int>(1 * ssf)))
                  .arg(c.border.name())
                  .arg(static_cast<int>(8 * ssf))
                  .arg(c.panelAlt.name())
                  .arg(static_cast<int>(10 * ssf))
                  .arg(c.accent.name()));

  connect(fitBtn, &QPushButton::clicked, this, [this] { m_dataViewer->setControlMode(DataViewer::ControlMode::LEAD_FULL); });
  connect(m_readoutBtn, &QPushButton::toggled, this, [this](bool on) {
    m_dataViewer->setCursorMode(on ? DataViewer::CursorMode::LEGEND_CURSOR : DataViewer::CursorMode::NONE);
    updateReadout();
  });
  connect(m_dataViewer, &DataViewer::cursorPositionChanged, this, [this](double key) { updateReadout(key); });
  connect(m_resHolder->states, &SystemValueList::ValuesUpdated, this, [this] {
    if (!m_readoutBtn->isChecked()) {
      updateReadout();
    }
  });

  connect(zoomXBtn, &QPushButton::clicked, this, [this] {
    m_dataViewer->zoomView(0.8, 1);
  });
  connect(outXBtn, &QPushButton::clicked, this, [this] {
    m_dataViewer->zoomView(1.2, 1);
  });
  connect(zoomYBtn, &QPushButton::clicked, this, [this] {
    m_dataViewer->zoomView(1, 0.8);
  });
  connect(outYBtn, &QPushButton::clicked, this, [this] {
    m_dataViewer->zoomView(1, 1.2);
  });
  connect(incXBtn, &QPushButton::clicked, this, [this] {
    m_dataViewer->moveView(0.2, 0);
  });
  connect(decXBtn, &QPushButton::clicked, this, [this] {
    m_dataViewer->moveView(-0.2, 0);
  });
  connect(incYBtn, &QPushButton::clicked, this, [this] {
    m_dataViewer->moveView(0, 0.2);
  });
  connect(decYBtn, &QPushButton::clicked, this, [this] {
    m_dataViewer->moveView(0, -0.2);
  });
  connect(m_styleBtn, &QPushButton::clicked, this, [this] {
    m_styleEditor->show();
  });

  connect(m_styleEditor, &GraphStyleEditor::styleChanged, m_dataViewer, &DataViewer::setStyle);
  connect(m_styleEditor, &GraphStyleEditor::styleAllChanged, m_dataViewer, &DataViewer::setStyles);

  connect(m_exportToBtn, &QPushButton::clicked, this, &GraphWidget::exportData);

  connect(m_dataViewer, &DataViewer::controlModeChanged, this, &GraphWidget::updateModeSelectButton);

  connect(calendarBtn, &QPushButton::clicked, this, [this]() {
    auto *dialog = new CalendarDialog(deepvaccontrol::INTERVALS_DIR, this);
    connect(dialog, &CalendarDialog::intervalKeySelected, this, [this, dialog](double selectedIntervalKey) {
      if (m_resHolder->dataManager->prepareIntervalToDisplay(selectedIntervalKey)) {
        if (m_dataViewer->getData()
            && (m_resHolder->dataManager->getIntervalToDisplay()->getNumberOfStates()
                != m_dataViewer->getData()->getNumberOfStates())) {
          m_resHolder->logger->stream(Logger::Level::WAR) << "New DataHolder has another number of states";
          QMessageBox::critical(this,
                                tr("Внимание!"),
                                QString(tr("Выбранный период содержит отличное количество перменных для "
                                           "отображения!\nПриложение может работать некорректно.")));
        }
        m_dataViewer->setData(m_resHolder->dataManager->getIntervalToDisplay());
        updateDayButton();
      }
      dialog->deleteLater();
    });
    dialog->open();
  });

  connect(m_resHolder->dataManager, &DataManager::intervalToAddRecreated, this, [this](double key) {
    if (m_resHolder->dataManager->prepareIntervalToDisplay(key)) {
      m_dataViewer->setData(m_resHolder->dataManager->getIntervalToDisplay());
    }
  });

  // try load current day interval
  double currentDayKey = utils::calculateKey(QDateTime::currentSecsSinceEpoch());
  if (m_resHolder->dataManager->prepareIntervalToDisplay(currentDayKey)) {
    m_dataViewer->setData(m_resHolder->dataManager->getIntervalToDisplay());
  }

  updateDayButton();
  updateReadout();
  handleAccessChange("", m_resHolder->users->getCurrentAccessLevel());
  connect(m_resHolder->users, &AccessManager::userSelected, this, &GraphWidget::handleAccessChange);
}

void GraphWidget::updateModeSelectButton(DataViewer::ControlMode mode) {
  switch (mode) {
    case DataViewer::ControlMode::LEAD_FULL:
      m_modeSelectBtn->setText(tr("Fit whole day"));
      break;
    case DataViewer::ControlMode::LEAD_X:
      m_modeSelectBtn->setText(tr("Follow latest"));
      break;
    case DataViewer::ControlMode::ZOOM:
      m_modeSelectBtn->setText(tr("Free zoom"));
      break;
  }
}

void GraphWidget::updateDayButton() {
  auto const interval = m_resHolder->dataManager->getIntervalToDisplay();
  if (!interval) {
    m_dayBtn->setText(tr("Today"));
    return;
  }
  QDate const shown = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(interval->getIntervalKey())).date();
  m_dayBtn->setText(shown == QDate::currentDate() ? tr("Today") : shown.toString("yyyy-MM-dd"));
}

void GraphWidget::updateReadout(double cursorKey) {
  // the main temperature channels, with short names and the unit from their label
  struct Item {
    char const *name;
    QString     title;
  };
  QList<Item> const items{{"temp", tr("Temp")}, {"temp_ref", tr("Setpoint")}, {"temp_u", tr("Output")}};
  QStringList const names = m_resHolder->states->getNames();
  bool const        atCursor{!std::isnan(cursorKey) && m_dataViewer->getData()};
  QVector<double>   values;
  if (atCursor) {
    values = m_dataViewer->getData()->getValueAtTime(cursorKey);
  }
  QStringList parts{atCursor ? QDateTime::fromSecsSinceEpoch(static_cast<qint64>(cursorKey)).toString("HH:mm:ss")
                             : QDateTime::currentDateTime().toString("HH:mm:ss")};
  for (Item const &item : items) {
    int const index = static_cast<int>(names.indexOf(QLatin1String(item.name)));
    if (index < 0) {
      continue;
    }
    SystemValue const *state = m_resHolder->states->getSysValue(item.name);
    double const       value = atCursor ? values.value(index, std::numeric_limits<double>::quiet_NaN()) : state->getValue();
    QString const      label = state->getLabel();
    QString const      unit  = label.contains(',') ? label.section(',', -1).trimmed() : QString();
    QString const      text  = (std::isnan(value) || std::fabs(value) > 1e9) ? QStringLiteral("--")
                                                                                : QString::number(value, 'f', item.name == QLatin1String("temp_u") ? 0 : 1);
    parts << QStringLiteral("%1 %2%3").arg(item.title, text, unit.isEmpty() ? QString() : QStringLiteral(" ") + unit);
  }
  m_readoutBtn->setText(parts.join(QStringLiteral("    ")));
}

void GraphWidget::handleAccessChange(QString const &name, AccessManager::AccessLevel newLevel) {
  bool const enablePress{newLevel >= AccessManager::AccessLevel::USER_START_1};
  m_exportToBtn->setEnabled(enablePress);
  m_styleBtn->setEnabled(enablePress);
}

void GraphWidget::exportData() {
  auto  interval     = std::make_shared<DataHolder>(m_resHolder->states->size(), m_resHolder->logger, 0);
  auto *exportDialog = new ExportDialog(m_resHolder, interval, this);
  connect(exportDialog, &QDialog::accepted, this, [this, interval, exportDialog]() {
    QString const fileExtension{exportDialog->selectedFileExtension()};
    QString const fileFullPath{exportDialog->selectedFilePath() + "." + fileExtension};

    auto const viewableRange = m_dataViewer->getCurrentView();

    // update interval if need
    int const selectedRangeType{exportDialog->selectedType()};
    if (selectedRangeType < 2) { // current view interval or part of it
      interval->clean();
      interval->append(m_resHolder->dataManager->getIntervalToDisplay().get());
      if (0 == selectedRangeType) { // part of current view interval
        interval->slice(viewableRange.left(), viewableRange.right());
      }
    }

    QVariantList const statesVariantList = m_resHolder->settings->settingsMap()["states"].toList();

    if (("txt" == fileExtension) || ("csv" == fileExtension)) {
      QStringList  labelsList;
      QVector<int> enablesList;

      quint32 const actualStatesCount{
        std::min(static_cast<quint32>(statesVariantList.size()), static_cast<quint32>(interval->getNumberOfStates()))};

      for (quint32 i{0U}; i < actualStatesCount; ++i) {
        QVariantMap const stateVariantMap{statesVariantList[i].toMap()};
        if (stateVariantMap["visible"].toBool()) {
          labelsList.push_back(stateVariantMap["label"].toString());
          enablesList.push_back(i);
        }
      }

      auto              *exporter = new DataExporter(interval.get(), fileExtension, labelsList, enablesList, this);
      StringPathProvider path(fileFullPath, m_resHolder->logger);
      exporter->saveToFile(&path, this);
    } else {
      auto *exportViewer = new DataViewer(m_resHolder->logger, false, this);
      exportViewer->setData(interval);

      // set same style as current m_dataViewer
      exportViewer->setStyles(statesVariantList);
      exportViewer->setCursorMode(m_dataViewer->getCursorMode());
      if (0 == selectedRangeType) {
        exportViewer->setView(viewableRange);
      }

      int intType = QStringList({"bmp", "png", "jpeg"}).indexOf(fileExtension);
      switch (intType) {
        case 0:
          exportViewer->getPlot()->saveBmp(fileFullPath, 2000, 1000, 1);
          break;
        case 1:
          exportViewer->getPlot()->savePng(fileFullPath, 2000, 1000, 1);
          break;
        case 2:
          exportViewer->getPlot()->saveJpg(fileFullPath, 2000, 1000, 1, 100);
          break;
        default:
          break;
      }
    }
  });
  exportDialog->open();
}
} // namespace deepvaccontrol
