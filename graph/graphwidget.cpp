#include "graphwidget.h"

#include <QApplication>
#include <QCheckBox>
#include <QDateTimeEdit>
#include <QFrame>
#include <QLabel>
#include <QSpinBox>
#include <QGridLayout>
#include <QMenu>
#include <QSplitter>
#include <QTabBar>
#include <QThread>
#include <cmath>

#include "analysisnotesstore.h"
#include "annotationdialog.h"
#include "calendardialog.h"
#include "chartstatisticsdialog.h"
#include "constants.h"
#include "dataexporter.h"
#include "experimentbrowser.h"
#include "exportdialog.h"
#include "overlaydrawer.h"
#include "theme.h"
#include "utils.h"

namespace deepvaccontrol {
namespace {
// "2026-09-24 09:55": the day button text of a single experiment
QString startText(double epochSec) {
  return QDateTime::fromSecsSinceEpoch(static_cast<qint64>(epochSec)).toString("yyyy-MM-dd HH:mm");
}
} // namespace

GraphWidget::GraphWidget(ResHolder *resHolder, QWidget *parent)
    : QWidget{parent}, m_resHolder{resHolder}, m_styleEditor{nullptr} {
  auto *mainLay = new QGridLayout(this);

  // channels shown on the logarithmic axis ("notation": "scientific" in the settings)
  QList<QVariant> const states_list = resHolder->settings->settingsMap()["states"].toList();
  for (int i{0U}; i < states_list.size(); ++i) {
    QVariantMap const paramMap = states_list[i].toMap();
    if (paramMap.contains("notation") && paramMap["notation"].toString() == "scientific") {
      m_addLogAxis = true;
      m_logAxisIndexes.push_back(i);
    }
  }

  connect(resHolder->settings, &SettingsManager::appSettingsChanged, this, [this] { // Settings -> Application
    QVariant const period = m_resHolder->settings->settingsMap()["app_settings"].toMap()["graphUpdatePeriod"];
    if (period.isValid() && (period.toString().toInt() > 0)) {
      for (ChartPane *pane : std::as_const(m_panes)) {
        pane->viewer()->setRefreshPeriodMs(1000 * period.toString().toInt());
      }
    }
  });
  m_styleEditor = new GraphStyleEditor(resHolder, this);
  double const ssf{utils::ssf()};

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
    theme::setThemedIcon(button, kind, static_cast<int>(14 * ssf));
    button->setFixedSize(26 * ssf, 26 * ssf);
    button->setToolTip(tip);
    return button;
  };
  auto textButton = [&](QString const &text) {
    auto *button = new QPushButton(text, card);
    button->setFixedHeight(26 * ssf);
    return button;
  };

  // value axis and time axis controls
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

  // experiment list (drawer over the page)
  m_listBtn = iconButton("folder", tr("Experiments: open a saved experiment or a recorded day in a new tab"));
  m_listBtn->setEnabled(false); // until the experiment list is set

  // statistics, annotate, export, channels | labels, tooltip, day, options
  m_statisticsBtn = textButton(tr("Statistics"));
  m_statisticsBtn->setMinimumWidth(90 * ssf);
  m_statisticsBtn->setToolTip(tr("Statistics of the visible range, annotations and variable rules"));
  m_statisticsBtn->setEnabled(false); // until the annotations store is set
  m_optionsBtn = textButton(tr("Options"));
  m_optionsBtn->setMinimumWidth(80 * ssf);
  m_optionsBtn->setToolTip(tr("Time range, smoothing and overlays"));
  m_annotateBtn = textButton(tr("Annotate"));
  m_annotateBtn->setCheckable(true);
  m_annotateBtn->setMinimumWidth(80 * ssf);
  m_annotateBtn->setToolTip(tr("Drag across the chart to mark and label a time range"));
  m_annotateBtn->setEnabled(false); // until the annotations store is set
  m_exportToBtn = textButton(tr("Export"));
  m_exportToBtn->setMinimumWidth(70 * ssf);
  m_styleBtn = textButton(tr("Channels ▾"));
  m_styleBtn->setMinimumWidth(90 * ssf);
  m_styleBtn->setToolTip(tr("Visible channels, colours and line styles"));

  m_labelsBtn = textButton(tr("Labels"));
  m_labelsBtn->setCheckable(true);
  m_labelsBtn->setChecked(true);
  m_labelsBtn->setMinimumWidth(80 * ssf);
  m_labelsBtn->setToolTip(tr("Show or hide the box with the line labels and colours"));
  m_valuesBtn = textButton(tr("Tooltip"));
  m_valuesBtn->setCheckable(true);
  m_valuesBtn->setMinimumWidth(80 * ssf);
  m_valuesBtn->setToolTip(tr("Show the value of the line where the mouse points in the chart\n"
                             "(split: the other chart shows the same line at the same time from its start)"));
  auto *calendarBtn = textButton(QString());
  m_dayBtn          = calendarBtn;
  theme::setThemedIcon(calendarBtn, "calendar", static_cast<int>(13 * ssf));
  calendarBtn->setMinimumWidth(110 * ssf);
  calendarBtn->setToolTip(tr("Open a saved experiment, or go back to the live data of today"));

  // the charts with their tabs: one pane, or two side by side ("Split right")
  m_split = new QSplitter(Qt::Horizontal, card);
  m_split->setHandleWidth(8 * ssf);
  m_split->setChildrenCollapsible(false);
  m_split->setStyleSheet(QStringLiteral("QSplitter::handle { background: transparent; }"));
  ChartPane *first = addPane();
  cardLay->addWidget(m_split, 0, 1, 2, 1);

  // experiment list and value axis controls left of the chart (below the tabs); the other controls in one row below
  auto *yAxisTools = new QVBoxLayout();
  yAxisTools->setSpacing(8 * ssf);
  yAxisTools->addSpacing(first->tabBar()->sizeHint().height() + 4 * ssf);
  yAxisTools->addWidget(m_listBtn);
  yAxisTools->addSpacing(8 * ssf);
  for (QPushButton *button : {zoomYBtn, outYBtn, incYBtn, decYBtn}) {
    yAxisTools->addWidget(button);
  }
  yAxisTools->addStretch();
  cardLay->addLayout(yAxisTools, 0, 0, 2, 1);
  cardLay->setRowStretch(1, 1);
  cardLay->setColumnStretch(1, 1);

  auto *toolBar = new QHBoxLayout();
  toolBar->setSpacing(8 * ssf);
  for (QPushButton *button : {zoomXBtn, outXBtn, decXBtn, incXBtn}) {
    toolBar->addWidget(button);
  }
  toolBar->addWidget(fitBtn);
  toolBar->addStretch();
  for (QPushButton *button : {m_statisticsBtn, m_annotateBtn, m_exportToBtn, m_styleBtn, m_labelsBtn, m_valuesBtn, calendarBtn, m_optionsBtn}) {
    toolBar->addWidget(button);
  }
  cardLay->addLayout(toolBar, 2, 1);

  connect(fitBtn, &QPushButton::clicked, this, [this] { viewer()->setControlMode(DataViewer::ControlMode::LEAD_FULL); });
  connect(m_labelsBtn, &QPushButton::toggled, this, &GraphWidget::updateLegendMode);
  connect(m_valuesBtn, &QPushButton::toggled, this, &GraphWidget::updateLegendMode);

  connect(zoomXBtn, &QPushButton::clicked, this, [this] {
    viewer()->zoomView(0.8, 1);
  });
  connect(outXBtn, &QPushButton::clicked, this, [this] {
    viewer()->zoomView(1.2, 1);
  });
  connect(zoomYBtn, &QPushButton::clicked, this, [this] {
    viewer()->zoomView(1, 0.8);
  });
  connect(outYBtn, &QPushButton::clicked, this, [this] {
    viewer()->zoomView(1, 1.2);
  });
  connect(incXBtn, &QPushButton::clicked, this, [this] {
    viewer()->moveView(0.2, 0);
  });
  connect(decXBtn, &QPushButton::clicked, this, [this] {
    viewer()->moveView(-0.2, 0);
  });
  connect(incYBtn, &QPushButton::clicked, this, [this] {
    viewer()->moveView(0, 0.2);
  });
  connect(decYBtn, &QPushButton::clicked, this, [this] {
    viewer()->moveView(0, -0.2);
  });
  connect(m_styleBtn, &QPushButton::clicked, this, [this] {
    m_styleEditor->show();
  });

  // styles apply to every chart
  connect(m_styleEditor, &GraphStyleEditor::styleChanged, this, [this](int index, QVariantMap style) {
    for (ChartPane *pane : std::as_const(m_panes)) {
      pane->viewer()->setStyle(index, style);
    }
  });
  connect(m_styleEditor, &GraphStyleEditor::styleAllChanged, this, [this](QVariantList const &styles) {
    for (ChartPane *pane : std::as_const(m_panes)) {
      pane->viewer()->setStyles(styles);
    }
  });

  connect(m_exportToBtn, &QPushButton::clicked, this, &GraphWidget::exportData);

  connect(calendarBtn, &QPushButton::clicked, this, &GraphWidget::openCalendar);

  // options: a drawer over the right of the page
  m_optionsDrawer = new OverlayDrawer(this, 320, OverlayDrawer::Side::RIGHT);
  m_optionsDrawer->setContent(buildOptions());
  connect(m_optionsBtn, &QPushButton::clicked, this, [this] {
    updateRangeFields();
    m_optionsDrawer->toggle();
  });

  connect(m_annotateBtn, &QPushButton::toggled, this, [this](bool on) {
    for (ChartPane *pane : std::as_const(m_panes)) {
      pane->viewer()->setAnnotateMode(on);
    }
  });

  connect(m_resHolder->dataManager, &DataManager::intervalToAddRecreated, this, [this](double key) {
    for (ChartPane *pane : std::as_const(m_panes)) {
      pane->reloadLive(key);
    }
  });

  // the live data of today, always the first tab of the first pane
  first->addTab({ChartPane::LIVE_KEY, tr("Live"), tr("Live data of today"), QStringLiteral("chart:live"), nullptr});

  updateDayButton();
  updateLegendMode();
  handleAccessChange("", m_resHolder->users->getCurrentAccessLevel());
  connect(m_resHolder->users, &AccessManager::userSelected, this, &GraphWidget::handleAccessChange);
}

DataViewer *GraphWidget::makeViewer() {
  auto *chart = new DataViewer(m_resHolder->logger, m_addLogAxis);
  if (m_addLogAxis) {
    chart->setIndexesForLogAxis(m_logAxisIndexes);
  }
  chart->setStyles(m_resHolder->settings->settingsMap()["states"].toList());
  QVariant const period = m_resHolder->settings->settingsMap()["app_settings"].toMap()["graphUpdatePeriod"];
  if (period.isValid() && (period.toString().toInt() > 0)) {
    chart->setRefreshPeriodMs(1000 * period.toString().toInt());
  }
  if (m_labelsBtn) {
    chart->setLabelsVisible(m_labelsBtn->isChecked());
    chart->setValuesVisible(m_valuesBtn->isChecked());
    chart->setAnnotateMode(m_annotateBtn->isChecked());
  }
  applyViewOptions(chart);
  return chart;
}

DataViewer *GraphWidget::viewer() const {
  return m_focused->viewer();
}

ChartPane *GraphWidget::addPane() {
  auto *pane = new ChartPane(makeViewer(), m_resHolder->dataManager, m_resHolder->states->getNames(), m_split);
  m_split->addWidget(pane);
  m_panes.push_back(pane);
  if (m_notes) {
    pane->setNotes(m_notes);
  }

  connect(pane, &ChartPane::activated, this, [this, pane] {
    setFocusedPane(pane);
  });
  connect(pane, &ChartPane::currentTabChanged, this, [this, pane] {
    if (pane == m_focused) {
      updateDayButton();
      updateStatisticsSource();
    }
  });
  connect(pane, &ChartPane::tabMenuRequested, this, [this, pane](int index, QPoint const &globalPos) {
    showTabMenu(pane, index, globalPos);
  });
  connect(pane, &ChartPane::emptied, this, [this, pane] {
    removePane(pane);
  });
  connect(pane->viewer(), &DataViewer::annotationDrawn, this, [this, pane](double x0, double x1) {
    onAnnotationDrawn(pane, x0, x1);
  });
  // tooltip on: the other pane mirrors the crosshair
  connect(pane->viewer(), &DataViewer::valueCursorMoved, this, [this, pane](double x, int graph) {
    syncCursor(pane, x, graph);
  });
  connect(pane->viewer(), &DataViewer::valueCursorHidden, this, [this, pane] {
    for (ChartPane *other : std::as_const(m_panes)) {
      if (other != pane) {
        other->viewer()->hideValueCursor();
      }
    }
  });

  if (!m_focused) {
    m_focused = pane;
  }
  if (m_panes.size() > 1) {
    int const half{std::max(1, m_split->width() / 2)};
    m_split->setSizes({half, half});
    setFocusedPane(pane);
  }
  return pane;
}

ChartPane *GraphWidget::rightPane() {
  return (m_panes.size() > 1) ? m_panes[1] : addPane();
}

void GraphWidget::removePane(ChartPane *pane) {
  if ((m_panes.size() <= 1) || !m_panes.contains(pane)) {
    return; // the first pane (with the live tab) stays
  }
  m_panes.removeAll(pane);
  pane->hide();
  pane->deleteLater(); // may be inside one of its own signals
  setFocusedPane(m_panes.first());
}

void GraphWidget::setFocusedPane(ChartPane *pane) {
  m_focused = pane;
  bool const split{m_panes.size() > 1};
  for (ChartPane *each : std::as_const(m_panes)) {
    each->setHighlighted(split && (each == m_focused));
  }
  updateDayButton();
  updateStatisticsSource();
}

void GraphWidget::openTab(Tab const &tab, bool splitRight) {
  // open already: there (moved to the right pane when asked)
  for (ChartPane *pane : std::as_const(m_panes)) {
    for (int i{0}; i < pane->count(); ++i) {
      if (pane->tab(i)->key != tab.key) {
        continue;
      }
      if (splitRight && (pane == m_panes.first()) && (tab.key != ChartPane::LIVE_KEY)) {
        moveTab(pane, i, rightPane());
      } else {
        pane->selectTab(tab.key);
        setFocusedPane(pane);
      }
      return;
    }
  }
  ChartPane *target = splitRight ? rightPane() : m_focused;
  target->addTab(tab);
  setFocusedPane(target);
}

void GraphWidget::moveTab(ChartPane *from, int index, ChartPane *to) {
  if ((from == to) || !from->tab(index) || (from->tab(index)->key == ChartPane::LIVE_KEY)) {
    return;
  }
  Tab const moved = from->takeTab(index); // an emptied right pane goes away
  to->addTab(moved);
  setFocusedPane(to);
}

void GraphWidget::showTabMenu(ChartPane *pane, int index, QPoint const &globalPos) {
  Tab const *tab = pane->tab(index);
  if (!tab) {
    return;
  }
  bool const live{tab->key == ChartPane::LIVE_KEY};
  QMenu      menu(this);
  if (pane == m_panes.first()) {
    menu.addAction(tr("Split right"), this, [this, pane, index] {
          moveTab(pane, index, rightPane());
        })
      ->setEnabled(!live);
  } else {
    menu.addAction(tr("Move to the left"), this, [this, pane, index] {
      moveTab(pane, index, m_panes.first());
    });
  }
  menu.addSeparator();
  menu.addAction(tr("Close"), this, [pane, index] {
        pane->takeTab(index);
      })
    ->setEnabled(!live);
  menu.exec(globalPos);
}

void GraphWidget::syncCursor(ChartPane *source, double x, int graph) {
  if (!m_valuesBtn->isChecked() || (m_panes.size() < 2)) {
    return;
  }
  double const sourceStart{source->dataStart()};
  for (ChartPane *other : std::as_const(m_panes)) {
    double const otherStart{other->dataStart()};
    if ((other == source) || std::isnan(sourceStart) || std::isnan(otherStart)) {
      continue;
    }
    // the same time from the start of the data shown, on the same line
    other->viewer()->showSyncCursor(otherStart + (x - sourceStart), graph);
  }
}

void GraphWidget::updateDayButton() {
  Tab const *shown = m_focused ? m_focused->currentTab() : nullptr;
  if (shown && (shown->key != ChartPane::LIVE_KEY)) {
    m_dayBtn->setText(shown->label);
    m_dayBtn->setToolTip(tr("%1\nOpen another experiment, or go back to the live data of today").arg(shown->name));
    return;
  }
  m_dayBtn->setToolTip(tr("Open a saved experiment, or go back to the live data of today"));
  auto const interval = m_resHolder->dataManager->getIntervalToDisplay();
  if (!interval) {
    m_dayBtn->setText(tr("Today"));
    return;
  }
  QDate const shownDay = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(interval->getIntervalKey())).date();
  m_dayBtn->setText(shownDay == QDate::currentDate() ? tr("Today") : shownDay.toString("yyyy-MM-dd"));
}

void GraphWidget::updateLegendMode() {
  for (ChartPane *pane : std::as_const(m_panes)) {
    pane->viewer()->setLabelsVisible(m_labelsBtn->isChecked());
    pane->viewer()->setValuesVisible(m_valuesBtn->isChecked());
  }
}

void GraphWidget::openCalendar() {
  if (m_loading) {
    return;
  }
  auto *dialog = new CalendarDialog(storage::RunStore::spans(), this);
  connect(dialog, &CalendarDialog::experimentSelected, this, &GraphWidget::showExperiment);
  connect(dialog, &CalendarDialog::liveSelected, this, [this] {
    m_panes.first()->selectTab(ChartPane::LIVE_KEY);
    setFocusedPane(m_panes.first());
  });
  connect(dialog, &CalendarDialog::daySelected, this, &GraphWidget::showDay);
  dialog->open();
}

void GraphWidget::setAnalysis(ExperimentLibrary *library, AnalysisNotesStore *notes) {
  m_library = library;
  m_notes   = notes;

  // experiment list: a drawer over the page, from the button on top of the left column
  m_drawer      = new OverlayDrawer(this, 340);
  auto *browser = new ExperimentBrowser(m_library, true);
  m_drawer->setContent(browser);
  connect(browser, &ExperimentBrowser::openRequested, this, [this](ExperimentRun const &run, bool splitRight) {
    m_drawer->close();
    showRun(run, splitRight);
  });
  connect(m_listBtn, &QPushButton::clicked, m_drawer, &OverlayDrawer::toggle);
  m_listBtn->setEnabled(true);

  for (ChartPane *pane : std::as_const(m_panes)) {
    pane->setNotes(m_notes);
  }

  // statistics, annotations and variable rules of the focused pane
  m_statistics = new ChartStatisticsDialog(m_resHolder, m_notes, this);
  connect(m_statistics, &ChartStatisticsDialog::zoomRequested, this, [this](double from, double to) {
    viewer()->setTimeRange(from, to);
  });
  connect(m_statisticsBtn, &QPushButton::clicked, this, [this] {
    updateStatisticsSource();
    m_statistics->show();
    m_statistics->raise();
  });
  m_statisticsBtn->setEnabled(true);
  handleAccessChange(QString{}, m_resHolder->users->getCurrentAccessLevel());
}

void GraphWidget::updateStatisticsSource() {
  if (!m_statistics || !m_focused) {
    return;
  }
  Tab const *tab = m_focused->currentTab();
  m_statistics->setSource(viewer(), tab ? tab->runKey : QString{}, tab ? tab->name : QString{});
}

QWidget *GraphWidget::buildOptions() {
  double const ssf{utils::ssf()};
  auto        *page = new QWidget();
  auto        *lay  = new QVBoxLayout(page);
  lay->setContentsMargins(4 * ssf, 4 * ssf, 4 * ssf, 4 * ssf);
  lay->setSpacing(6 * ssf);
  auto heading = [page](QString const &text) {
    auto *label = new QLabel(text, page);
    QFont font  = label->font();
    font.setBold(true);
    label->setFont(font);
    return label;
  };
  auto hint = [page](QString const &text) {
    auto *label = new QLabel(text, page);
    label->setProperty("muted", true);
    label->setWordWrap(true);
    return label;
  };
  auto *title = new QLabel(tr("Options"), page);
  {
    QFont font = title->font();
    font.setBold(true);
    font.setPointSizeF(font.pointSizeF() * 1.25);
    title->setFont(font);
  }
  lay->addWidget(title);
  lay->addSpacing(8 * ssf);

  // time range of the focused chart
  lay->addWidget(heading(tr("Time range")));
  m_fromEdit = new QDateTimeEdit(page);
  m_toEdit   = new QDateTimeEdit(page);
  auto *rangeGrid = new QGridLayout();
  rangeGrid->setHorizontalSpacing(8 * ssf);
  int row{0};
  for (auto const &[label, edit] : {std::pair{tr("From"), m_fromEdit}, std::pair{tr("To"), m_toEdit}}) {
    edit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    edit->setMinimumHeight(30 * ssf);
    rangeGrid->addWidget(new QLabel(label, page), row, 0);
    rangeGrid->addWidget(edit, row, 1);
    ++row;
  }
  lay->addLayout(rangeGrid);
  auto *rangeButtons = new QHBoxLayout();
  auto *applyBtn     = new QPushButton(tr("Apply"), page);
  auto *wholeBtn     = new QPushButton(tr("Whole range"), page);
  applyBtn->setObjectName("primaryButton");
  for (QPushButton *button : {applyBtn, wholeBtn}) {
    button->setMinimumHeight(30 * ssf);
    button->setFocusPolicy(Qt::NoFocus);
    rangeButtons->addWidget(button, 1);
  }
  lay->addLayout(rangeButtons);
  connect(applyBtn, &QPushButton::clicked, this, [this] {
    viewer()->setTimeRange(static_cast<double>(m_fromEdit->dateTime().toSecsSinceEpoch()),
                           static_cast<double>(m_toEdit->dateTime().toSecsSinceEpoch()));
  });
  connect(wholeBtn, &QPushButton::clicked, this, [this] {
    viewer()->setControlMode(DataViewer::ControlMode::LEAD_FULL);
    updateRangeFields();
  });
  lay->addSpacing(14 * ssf);

  // smoothing
  lay->addWidget(heading(tr("Smoothing")));
  m_smoothSpin = new QSpinBox(page);
  m_smoothSpin->setRange(1, 999);
  m_smoothSpin->setSuffix(tr(" samples"));
  m_smoothSpin->setMinimumHeight(30 * ssf);
  lay->addWidget(m_smoothSpin);
  lay->addWidget(hint(tr("Centred moving average; 1 shows the raw data.")));
  lay->addSpacing(14 * ssf);

  // overlays
  lay->addWidget(heading(tr("Overlays")));
  m_minCheck = new QCheckBox(tr("Minimum"), page);
  m_maxCheck = new QCheckBox(tr("Maximum"), page);
  m_avgCheck = new QCheckBox(tr("Average"), page);
  for (QCheckBox *check : {m_minCheck, m_maxCheck, m_avgCheck}) {
    lay->addWidget(check);
  }
  lay->addWidget(hint(tr("Dashed lines over the visible channels of the main value axis.")));
  lay->addStretch();

  // smoothing and overlays apply to both charts when the page is split
  auto apply = [this] {
    for (ChartPane *pane : std::as_const(m_panes)) {
      applyViewOptions(pane->viewer());
    }
  };
  connect(m_smoothSpin, &QSpinBox::valueChanged, this, apply);
  for (QCheckBox *check : {m_minCheck, m_maxCheck, m_avgCheck}) {
    connect(check, &QCheckBox::toggled, this, apply);
  }
  return page;
}

void GraphWidget::applyViewOptions(DataViewer *chart) const {
  if (!m_smoothSpin) {
    return;
  }
  chart->setSmoothingWindow(m_smoothSpin->value());
  chart->setOverlays(m_minCheck->isChecked(), m_maxCheck->isChecked(), m_avgCheck->isChecked());
}

void GraphWidget::updateRangeFields() {
  if (!m_fromEdit) {
    return;
  }
  QCPRange const view = viewer()->getPlot()->xAxis->range();
  m_fromEdit->setDateTime(QDateTime::fromSecsSinceEpoch(static_cast<qint64>(std::floor(view.lower))));
  m_toEdit->setDateTime(QDateTime::fromSecsSinceEpoch(static_cast<qint64>(std::ceil(view.upper))));
}

void GraphWidget::onAnnotationDrawn(ChartPane *pane, double x0, double x1) {
  Tab const *tab   = pane->currentTab();
  double const start{pane->dataStart()};
  if (!m_notes || !tab || std::isnan(start)) {
    return;
  }
  auto timeOf = [](double t) {
    return QDateTime::fromSecsSinceEpoch(static_cast<qint64>(std::llround(t))).toString("HH:mm:ss");
  };
  QString    label;
  QColor     color;
  bool const ok{annotationdialog::ask(this, QStringLiteral("%1 – %2").arg(timeOf(x0), timeOf(x1)), label, color)};
  m_annotateBtn->setChecked(false);
  if (!ok) {
    return;
  }
  AnalysisNotesStore::Annotation annotation;
  annotation.runKey   = tab->runKey;
  annotation.runStart = start;
  annotation.start    = x0;
  annotation.end      = x1;
  annotation.label    = label.isEmpty() ? QStringLiteral("%1 – %2").arg(timeOf(x0), timeOf(x1)) : label;
  annotation.color    = color;
  annotation.user     = m_resHolder->users->getCurrentUserName();
  m_notes->addAnnotation(annotation); // the panes refresh through annotationsChanged
}

void GraphWidget::showRun(ExperimentRun const &run, bool splitRight) {
  if (!m_library) {
    return;
  }
  for (ChartPane *pane : std::as_const(m_panes)) {
    for (int i{0}; i < pane->count(); ++i) {
      if (pane->tab(i)->key == run.key) {
        openTab(*pane->tab(i), splitRight); // open already
        return;
      }
    }
  }
  QApplication::setOverrideCursor(Qt::WaitCursor);
  QString    error;
  auto const loaded = m_library->load(run, &error);
  QApplication::restoreOverrideCursor();
  if (!loaded || loaded->times.isEmpty()) {
    QMessageBox::warning(this, tr("Chart"), error.isEmpty() ? tr("%1 could not be read.").arg(run.title) : error);
    return;
  }
  // the channels of the chart, in the order of the states: recorded days keep that order,
  // database runs name their channels; channels the run lacks stay empty
  QStringList const        names = m_resHolder->states->getNames();
  QVector<QVector<double>> states(names.size());
  for (int i{0}; i < names.size(); ++i) {
    qsizetype const column = loaded->channelNames.isEmpty() ? i : loaded->channelNames.indexOf(names[i]);
    states[i] = ((column >= 0) && (column < loaded->states.size())) ? loaded->states[column]
                                                                     : QVector<double>(loaded->times.size(), qQNaN());
  }
  auto holder = std::make_shared<DataHolder>(static_cast<quint16>(names.size()), m_resHolder->logger,
                                             utils::calculateKey(loaded->times.first()));
  holder->setSeries(loaded->times, states);
  openTab({run.key, startText(run.start), run.title, run.key, holder}, splitRight);
}

void GraphWidget::showDay(QDate const &day, QVector<storage::ExperimentSpan> const &experiments) {
  // the experiments of the day one after the other, with a gap between them, read in the background
  m_loading = true;
  m_dayBtn->setText(tr("Loading…"));
  QStringList const names = m_resHolder->states->getNames();
  struct Merged {
    QVector<double>          times;
    QVector<QVector<double>> states;
    int                      loaded{0};
    double                   firstStart{qQNaN()}; // epoch s of the first experiment read
  };
  auto     merged  = std::make_shared<Merged>();
  QThread *thread  = QThread::create([experiments, names, merged] {
    merged->states.resize(names.size());
    for (storage::ExperimentSpan const &experiment : experiments) {
      storage::RunSamples samples;
      if (!storage::RunStore::loadSamples(experiment.key, samples) || samples.times.isEmpty()) {
        continue;
      }
      if (!merged->times.isEmpty()) { // gap: no line drawn between two experiments
        merged->times.push_back((merged->times.last() + samples.times.first()) / 2.0);
        for (auto &state : merged->states) {
          state.push_back(qQNaN());
        }
      }
      if (0 == merged->loaded) {
        merged->firstStart = experiment.start;
      }
      merged->times += samples.times;
      for (int i{0}; i < names.size(); ++i) {
        qsizetype const column = samples.columns.indexOf(names[i]);
        merged->states[i] += (column >= 0) ? samples.values[column] : QVector<double>(samples.times.size(), qQNaN());
      }
      ++merged->loaded;
    }
  });
  connect(thread, &QThread::finished, this, [this, thread, merged, day, names] {
    thread->deleteLater();
    m_loading = false;
    if (merged->times.isEmpty()) {
      QMessageBox::warning(this, tr("Chart"), tr("The experiments of %1 could not be read.").arg(day.toString("yyyy-MM-dd")));
      updateDayButton();
      return;
    }
    auto holder = std::make_shared<DataHolder>(static_cast<quint16>(names.size()), m_resHolder->logger,
                                               utils::calculateKey(merged->times.first()));
    holder->setSeries(merged->times, merged->states);
    QString const name = (merged->loaded == 1) ? tr("%1 · 1 experiment").arg(day.toString("yyyy-MM-dd"))
                                               : tr("%1 · %2 experiments").arg(day.toString("yyyy-MM-dd")).arg(merged->loaded);
    // one experiment: its date and time; several: the day and how many
    QString const label = (merged->loaded == 1) ? startText(merged->firstStart) : name;
    QString const key   = QStringLiteral("day:") + day.toString(Qt::ISODate);
    openTab({key, label, name, QStringLiteral("chart:") + key, holder});
  });
  thread->start(QThread::LowPriority);
}

void GraphWidget::showExperiment(storage::ExperimentSpan const &experiment) {
  // samples from the experiment database, read in the background (long experiments take a moment)
  m_loading = true;
  m_dayBtn->setText(tr("Loading…"));
  auto     samples = std::make_shared<storage::RunSamples>();
  auto     ok      = std::make_shared<bool>(false);
  QString  key     = experiment.key;
  QThread *thread  = QThread::create([key, samples, ok] { *ok = storage::RunStore::loadSamples(key, *samples); });
  connect(thread, &QThread::finished, this, [this, thread, samples, ok, experiment] {
    thread->deleteLater();
    m_loading = false;
    if (!*ok) {
      QMessageBox::warning(this, tr("Chart"), tr("The samples of %1 could not be read.").arg(experiment.name));
      updateDayButton();
      return;
    }
    // the channels of the chart, in the order of the states; channels the experiment lacks stay empty
    QStringList const        names = m_resHolder->states->getNames();
    QVector<QVector<double>> states(names.size());
    for (int i{0}; i < names.size(); ++i) {
      qsizetype const column = samples->columns.indexOf(names[i]);
      states[i] = (column >= 0) ? samples->values[column] : QVector<double>(samples->times.size(), qQNaN());
    }
    auto holder = std::make_shared<DataHolder>(static_cast<quint16>(names.size()), m_resHolder->logger,
                                               utils::calculateKey(experiment.start));
    holder->setSeries(samples->times, states);
    QString const key = QStringLiteral("db:") + experiment.key;
    openTab({key, startText(experiment.start), experiment.name, QStringLiteral("chart:") + key, holder});
  });
  thread->start(QThread::LowPriority);
}

void GraphWidget::handleAccessChange(QString const &name, AccessManager::AccessLevel newLevel) {
  bool const enablePress{newLevel >= AccessManager::AccessLevel::USER_START_1};
  m_exportToBtn->setEnabled(enablePress);
  m_styleBtn->setEnabled(enablePress);
  m_annotateBtn->setEnabled(enablePress && m_notes);
}

void GraphWidget::exportData() {
  auto  interval     = std::make_shared<DataHolder>(m_resHolder->states->size(), m_resHolder->logger, 0);
  auto *exportDialog = new ExportDialog(m_resHolder, interval, this);
  connect(exportDialog, &QDialog::accepted, this, [this, interval, exportDialog]() {
    QString const fileExtension{exportDialog->selectedFileExtension()};
    QString const fileFullPath{exportDialog->selectedFilePath() + "." + fileExtension};

    auto const viewableRange = viewer()->getCurrentView();

    // update interval if need
    int const selectedRangeType{exportDialog->selectedType()};
    if (selectedRangeType < 2) { // current view interval or part of it
      interval->clean();
      interval->append(viewer()->getData().get());
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

      // same style as the focused chart
      exportViewer->setStyles(statesVariantList);
      exportViewer->setLabelsVisible(viewer()->labelsVisible());
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
