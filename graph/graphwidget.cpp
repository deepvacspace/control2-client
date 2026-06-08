#include "graphwidget.h"

#include <QGridLayout>

#include "calendardialog.h"
#include "constants.h"
#include "dataexporter.h"
#include "exportdialog.h"
#include "utils.h"

namespace control2 {
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

  auto *zoomXBtn    = new QPushButton(this);
  auto *zoomYBtn    = new QPushButton(this);
  auto *outXBtn     = new QPushButton(this);
  auto *outYBtn     = new QPushButton(this);
  auto *incXBtn     = new QPushButton(this);
  auto *decXBtn     = new QPushButton(this);
  auto *incYBtn     = new QPushButton(this);
  auto *decYBtn     = new QPushButton(this);
  m_modeSelectBtn   = new QPushButton(this);
  auto *cursorBtn   = new QPushButton(this);
  auto *calendarBtn = new QPushButton(this);
  m_exportToBtn     = new QPushButton(this);
  m_styleBtn        = new QPushButton(this);

  incYBtn->setIcon(QIcon(":/imgs/graph_up.png"));
  decYBtn->setIcon(QIcon(":/imgs/graph_down.png"));
  incXBtn->setIcon(QIcon(":/imgs/graph_right.png"));
  decXBtn->setIcon(QIcon(":/imgs/graph_left.png"));
  zoomXBtn->setIcon(QIcon(":/imgs/graph_zoomIn.png"));
  zoomYBtn->setIcon(QIcon(":/imgs/graph_zoomIn.png"));
  outXBtn->setIcon(QIcon(":/imgs/graph_zoomOut.png"));
  outYBtn->setIcon(QIcon(":/imgs/graph_zoomOut.png"));
  m_modeSelectBtn->setIcon(QIcon(":/imgs/graph_lead_full.png"));
  cursorBtn->setIcon(QIcon(":/imgs/graph_cursor.png"));
  calendarBtn->setIcon(QIcon(":/imgs/archive.png"));
  m_exportToBtn->setIcon(QIcon(":/imgs/export.png"));
  m_styleBtn->setIcon(QIcon(":/imgs/gsEditor.png"));

  for (QPushButton *btn : {zoomXBtn,
                           zoomYBtn,
                           outXBtn,
                           outYBtn,
                           incXBtn,
                           incYBtn,
                           decXBtn,
                           decYBtn,
                           m_modeSelectBtn,
                           cursorBtn,
                           calendarBtn,
                           m_exportToBtn,
                           m_styleBtn}) {
    btn->setFixedSize(60 * utils::ssf(), 60 * utils::ssf());
    btn->setIconSize(QSize(0.7 * 60 * utils::ssf(), 0.7 * 60 * utils::ssf()));
  }

  mainLay->addWidget(m_dataViewer, 0, 1);

  mainLay->addLayout(yAxisTools, 0, 0);
  mainLay->addLayout(xAxisTools, 1, 1);

  yAxisTools->addWidget(m_exportToBtn);
  yAxisTools->addWidget(m_styleBtn);
  yAxisTools->addStretch();

  yAxisTools->addWidget(zoomYBtn);
  yAxisTools->addWidget(outYBtn);
  yAxisTools->addWidget(incYBtn);
  yAxisTools->addWidget(decYBtn);

  xAxisTools->addWidget(decXBtn);
  xAxisTools->addWidget(incXBtn);
  xAxisTools->addWidget(outXBtn);
  xAxisTools->addWidget(zoomXBtn);
  xAxisTools->addWidget(m_modeSelectBtn);
  xAxisTools->addWidget(cursorBtn);
  xAxisTools->addStretch();
  xAxisTools->addWidget(calendarBtn);

  xAxisTools->setSpacing(3 * utils::ssf());
  yAxisTools->setSpacing(3 * utils::ssf());

  mainLay->setContentsMargins(0, 0, 0, 0);
  mainLay->setSpacing(2 * utils::ssf());
  m_dataViewer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

  calendarBtn->setIconSize(QSize(0.8 * 60 * utils::ssf(), 0.8 * 60 * utils::ssf()));

  m_modeSelectBtn->setFixedSize(1.8 * 60 * utils::ssf(), 60 * utils::ssf());
  m_modeSelectBtn->setIconSize(QSize(1.5 * 60 * utils::ssf(), 0.8 * 60 * utils::ssf()));

  cursorBtn->setFixedSize(1.5 * 60 * utils::ssf(), 60 * utils::ssf());
  cursorBtn->setIconSize(QSize(1.2 * 60 * utils::ssf(), 0.8 * 60 * utils::ssf()));

  connect(m_modeSelectBtn, &QPushButton::clicked, m_dataViewer, &DataViewer::switchControlMode);
  connect(cursorBtn, &QPushButton::clicked, m_dataViewer, &DataViewer::switchCursor);

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
    auto *dialog = new CalendarDialog(control2::INTERVALS_DIR, this);
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

  handleAccessChange("", m_resHolder->users->getCurrentAccessLevel());
  connect(m_resHolder->users, &AccessManager::userSelected, this, &GraphWidget::handleAccessChange);
}

void GraphWidget::updateModeSelectButton(DataViewer::ControlMode mode) {
  switch (mode) {
    case DataViewer::ControlMode::LEAD_FULL:
      m_modeSelectBtn->setIcon(QIcon(":/imgs/graph_lead_full.png"));
      break;
    case DataViewer::ControlMode::LEAD_X:
      m_modeSelectBtn->setIcon(QIcon(":/imgs/graph_lead_x.png"));
      break;
    case DataViewer::ControlMode::ZOOM:
      m_modeSelectBtn->setIcon(QIcon(":/imgs/graph_lead_zoom.png"));
      break;
  }
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
} // namespace control2
