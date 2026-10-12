#include "graphwidget.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QRadioButton>
#include <QSaveFile>
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
#include "analysisservices.h"
#include "annotationdialog.h"
#include "comparison.h"
#include "comparisondialog.h"
#include "experimentinput.h"
#include "calendardialog.h"
#include "chartstatisticsdialog.h"
#include "constants.h"
#include "dataexporter.h"
#include "experimentbrowser.h"
#include "exportdialog.h"
#include "overlaydrawer.h"
#include "rundetailsdialog.h"
#include "dialogs/messagedialog.h"
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
        for (DataViewer *chart : pane->viewers()) {
          chart->setRefreshPeriodMs(1000 * period.toString().toInt());
        }
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

  // compare, statistics, annotate, export, channels | labels, tooltip, day, options
  m_compareBtn = textButton(tr("Compare"));
  m_compareBtn->setMinimumWidth(84 * ssf);
  m_compareBtn->setToolTip(tr("Compare channels of open tabs or other experiments in a new tab"));
  m_compareBtn->setEnabled(false); // until the experiment list is set
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
  for (QPushButton *button :
       {m_compareBtn, m_statisticsBtn, m_annotateBtn, m_exportToBtn, m_styleBtn, m_labelsBtn, m_valuesBtn, calendarBtn, m_optionsBtn}) {
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
    Tab const *tab = m_focused->currentTab();
    if (tab && tab->comparison) {
      openCompareDialog(tab->key); // a comparison's channels
    } else {
      m_styleEditor->show();
    }
  });
  connect(m_compareBtn, &QPushButton::clicked, this, [this] { openCompareDialog(); });

  // styles apply to every chart
  connect(m_styleEditor, &GraphStyleEditor::styleChanged, this, [this](int index, QVariantMap style) {
    for (ChartPane *pane : std::as_const(m_panes)) {
      pane->mainViewer()->setStyle(index, style);
    }
  });
  connect(m_styleEditor, &GraphStyleEditor::styleAllChanged, this, [this](QVariantList const &styles) {
    for (ChartPane *pane : std::as_const(m_panes)) {
      pane->mainViewer()->setStyles(styles);
    }
  });

  connect(m_exportToBtn, &QPushButton::clicked, this, &GraphWidget::exportData);

  connect(calendarBtn, &QPushButton::clicked, this, &GraphWidget::openCalendar);

  // options: a drawer over the right of the page
  m_optionsDrawer = new OverlayDrawer(this, 320, OverlayDrawer::Side::RIGHT);
  m_optionsDrawer->setContent(buildOptions());
  connect(m_optionsBtn, &QPushButton::clicked, this, [this] {
    updateOptions();
    m_optionsDrawer->toggle();
  });

  connect(m_annotateBtn, &QPushButton::toggled, this, [this](bool on) {
    for (ChartPane *pane : std::as_const(m_panes)) {
      for (DataViewer *chart : pane->viewers()) {
        chart->setAnnotateMode(on);
      }
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
  initAnalysis();
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
    if (m_dragDepth == 0) { // otherwise once the drag ended
      removePane(pane);
    }
  });
  connect(pane, &ChartPane::tabDropped, this, [this, pane](ChartPane *source, QString const &key, int at, bool rightHalf) {
    onTabDropped(pane, source, key, at, rightHalf);
  });
  connect(pane, &ChartPane::tabDragStarted, this, [this] { ++m_dragDepth; });
  connect(pane, &ChartPane::tabDragFinished, this, [this] {
    --m_dragDepth;
    // after the drag's own event loop: an emptied right pane goes now
    QTimer::singleShot(0, this, [this] {
      for (ChartPane *each : QVector<ChartPane *>(m_panes)) {
        if ((each != m_panes.first()) && (each->count() == 0)) {
          removePane(each);
        }
      }
    });
  });
  connect(pane, &ChartPane::annotationDrawn, this, [this, pane](double x0, double x1) {
    onAnnotationDrawn(pane, x0, x1);
  });
  // tooltip on: the other pane mirrors the crosshair
  connect(pane, &ChartPane::cursorMoved, this, [this, pane](double x, int graph) {
    syncCursor(pane, x, graph);
  });
  connect(pane, &ChartPane::cursorHidden, this, [this, pane] {
    for (ChartPane *other : std::as_const(m_panes)) {
      if (other != pane) {
        other->viewer()->hideValueCursor();
      }
    }
  });

  if (!m_focused) {
    m_focused = pane;
  }
  pane->setPrimary(m_panes.size() == 1);
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

void GraphWidget::moveTab(ChartPane *from, int index, ChartPane *to, int at) {
  if ((from == to) || !from->tab(index) || (from->tab(index)->key == ChartPane::LIVE_KEY)) {
    return;
  }
  Tab const moved = from->takeTab(index); // an emptied right pane goes away
  to->addTab(moved, at);
  setFocusedPane(to);
}

void GraphWidget::onTabDropped(ChartPane *target, ChartPane *source, QString const &key, int at, bool rightHalf) {
  if (!m_panes.contains(source) || !m_panes.contains(target)) {
    return;
  }
  int const index{source->indexOf(key)};
  if (index < 0) {
    return;
  }
  if (rightHalf) {
    moveTab(source, index, rightPane()); // dropped on the right of its own chart: split
  } else {
    moveTab(source, index, target, at);
  }
}

ChartPane *GraphWidget::paneWith(QString const &key) const {
  for (ChartPane *pane : m_panes) {
    if (pane->indexOf(key) >= 0) {
      return pane;
    }
  }
  return nullptr;
}

std::shared_ptr<DataHolder const> GraphWidget::holderFrom(QVector<double> const &times, QStringList const &columns,
                                                          QVector<QVector<double>> const &values) const {
  // the channels of the chart, in the order of the states: recorded days keep that order,
  // database runs name their channels; channels the run lacks stay empty
  QStringList const        names = m_resHolder->states->getNames();
  QVector<QVector<double>> states(names.size());
  for (int i{0}; i < names.size(); ++i) {
    qsizetype const column = columns.isEmpty() ? i : columns.indexOf(names[i]);
    states[i] = ((column >= 0) && (column < values.size())) ? values[column] : QVector<double>(times.size(), qQNaN());
  }
  auto holder = std::make_shared<DataHolder>(static_cast<quint16>(names.size()), m_resHolder->logger,
                                             utils::calculateKey(times.isEmpty() ? 0.0 : times.first()));
  holder->setSeries(times, states);
  return holder;
}

void GraphWidget::openLoading(Tab tab, bool splitRight, std::function<bool(Samples &out, QString &error)> read) {
  if (paneWith(tab.key)) {
    openTab(tab, splitRight); // open already (or still loading)
    return;
  }
  tab.loading = true;
  openTab(tab, splitRight);
  auto     samples = std::make_shared<Samples>();
  auto     ok      = std::make_shared<bool>(false);
  auto     error   = std::make_shared<QString>();
  QThread *thread  = QThread::create([read, samples, ok, error] { *ok = read(*samples, *error); });
  connect(thread, &QThread::finished, this, [this, thread, samples, ok, error, tab] {
    thread->deleteLater();
    ChartPane *pane = paneWith(tab.key);
    if (!pane) {
      return; // closed meanwhile
    }
    if (!*ok || samples->times.isEmpty()) {
      pane->closeTab(pane->indexOf(tab.key));
      QMessageBox::warning(this, tr("Chart"), error->isEmpty() ? tr("%1 could not be read.").arg(tab.name) : *error);
      return;
    }
    pane->setTabData(tab.key, holderFrom(samples->times, samples->columns, samples->values)); // a QObject: made here
  });
  thread->start(QThread::LowPriority);
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
  if (tab->comparison) {
    QString const key = tab->key;
    menu.addAction(tr("Change comparison…"), this, [this, key] { openCompareDialog(key); });
  }
  menu.addSeparator();
  menu.addAction(tr("Close"), this, [pane, index] {
        pane->closeTab(index);
      })
    ->setEnabled(!live);
  menu.exec(globalPos);
}

void GraphWidget::syncCursor(ChartPane *source, double x, int graph) {
  if (!m_valuesBtn->isChecked() || (m_panes.size() < 2)) {
    return;
  }
  double const sourceStart{source->dataStart()};
  Tab const   *sourceTab = source->currentTab();
  for (ChartPane *other : std::as_const(m_panes)) {
    double const otherStart{other->dataStart()};
    if ((other == source) || std::isnan(sourceStart) || std::isnan(otherStart)) {
      continue;
    }
    // the same line where both charts have the same lines (channels, or the same comparison); else the time only
    Tab const *otherTab = other->currentTab();
    bool const sameLines{sourceTab && otherTab && (sourceTab->comparison == otherTab->comparison)};
    // the same time from the start of the data shown
    other->viewer()->showSyncCursor(otherStart + (x - sourceStart), sameLines ? graph : -1);
  }
}

void GraphWidget::updateDayButton() {
  Tab const *shown = m_focused ? m_focused->currentTab() : nullptr;
  // annotations and rules are of recorded time: not on a comparison's aligned axis
  bool const comparison{shown && shown->comparison};
  if (comparison && m_annotateBtn->isChecked()) {
    m_annotateBtn->setChecked(false);
  }
  m_annotateBtn->setEnabled(m_canAnnotate && !comparison);
  m_styleBtn->setText(comparison ? tr("Channels…") : tr("Channels ▾"));
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
    for (DataViewer *chart : pane->viewers()) {
      chart->setLabelsVisible(m_labelsBtn->isChecked());
      chart->setValuesVisible(m_valuesBtn->isChecked());
    }
  }
}

void GraphWidget::openCalendar() {
  auto *dialog = new CalendarDialog(storage::RunStore::spans(), this);
  connect(dialog, &CalendarDialog::experimentSelected, this, &GraphWidget::showExperiment);
  connect(dialog, &CalendarDialog::liveSelected, this, [this] {
    m_panes.first()->selectTab(ChartPane::LIVE_KEY);
    setFocusedPane(m_panes.first());
  });
  connect(dialog, &CalendarDialog::daySelected, this, &GraphWidget::showDay);
  dialog->open();
}

void GraphWidget::initAnalysis() {
  m_services = m_resHolder->analysis;
  if (!m_services) {
    return; // no experiment list: the live data only
  }
  m_library = m_services->library();
  m_notes   = m_services->notes();

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
  m_compareBtn->setEnabled(true);

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

  // experiments the assistant shows open here
  m_services->setShowHandler([this](ExperimentRun const &run, QStringList const &channels, double from, double to,
                                    QVector<ExperimentRun> const &compare) { return showFromServices(run, channels, from, to, compare); });
  handleAccessChange(QString{}, m_resHolder->users->getCurrentAccessLevel());
}

QString GraphWidget::showFromServices(ExperimentRun const &run, QStringList const &channels, double from, double to,
                                      QVector<ExperimentRun> const &compare) {
  if (!compare.isEmpty()) {
    ChartComparison comparison;
    comparison.members.push_back({run.key, startText(run.start), run});
    for (ExperimentRun const &other : compare) {
      comparison.members.push_back({other.key, startText(other.start), other});
    }
    comparison.channels = channels.isEmpty() ? QStringList{QStringLiteral("temp")} : channels;
    createComparison(comparison);
    return {};
  }
  if (ChartPane *pane = paneWith(run.key)) { // open already: there, at that range
    pane->selectTab(run.key);
    setFocusedPane(pane);
    double const start{pane->dataStart()};
    if (!std::isnan(from) && !std::isnan(to) && !std::isnan(start)) {
      pane->viewer()->setTimeRange(start + from, start + to);
    }
    return {};
  }
  showRun(run, false, from, to);
  return {};
}

ExperimentRun GraphWidget::runOfTab(Tab const &tab) const {
  if (m_library) {
    if (auto run = m_library->findRun(tab.key)) {
      return *run;
    }
    if (!tab.storeKey.isEmpty()) {
      for (ExperimentRun const &stored : m_library->storedRuns()) {
        if (stored.storeKey == tab.storeKey) {
          return stored;
        }
      }
    }
  }
  // the live data and days: their time range
  auto const      data  = (tab.key == ChartPane::LIVE_KEY) ? std::shared_ptr<DataHolder const>(m_resHolder->dataManager->getIntervalToDisplay()) : tab.data;
  QVector<double> times = data ? data->getTimes() : QVector<double>{};
  ExperimentRun   run   = (m_library && !times.isEmpty()) ? m_library->makeRangeRun(times.first(), times.last()) : ExperimentRun{};
  run.title             = tab.name;
  return run;
}

// ---------------------------------------------------------------- comparisons
QPair<QString, QString> GraphWidget::comparisonTitles(ChartComparison const &comparison) const {
  QStringList const  names  = m_resHolder->states->getNames();
  QVariantList const styles = m_resHolder->settings->settingsMap()["states"].toList();
  QStringList        channelLabels;
  for (QString const &channel : comparison.channels) {
    qsizetype const index{names.indexOf(channel)};
    QString const   label = (index >= 0) && (index < styles.size()) ? styles[index].toMap()["label"].toString() : channel;
    channelLabels << label;
  }
  QStringList members;
  for (ChartComparison::Member const &member : comparison.members) {
    members << member.label;
  }
  QString const shortChannel = (channelLabels.size() == 1) ? channelLabels.first().section(QLatin1Char(','), 0, 0).trimmed()
                                                           : tr("%1 channels").arg(channelLabels.size());
  return {QStringLiteral("≈ %1 · %2").arg(shortChannel).arg(comparison.members.size()),
          tr("Comparison of %1: %2").arg(channelLabels.join(QStringLiteral(", ")), members.join(QStringLiteral(" · ")))};
}

void GraphWidget::styleComparison(DataViewer *chart, ChartComparison const &comparison) const {
  static QVector<QColor> const palette{QColor("#2563eb"), QColor("#dc2626"), QColor("#16a34a"), QColor("#9333ea"),
                                       QColor("#d97706"), QColor("#0d9488"), QColor("#db2777"), QColor("#475569")};
  QStringList const  names  = m_resHolder->states->getNames();
  QVariantList const states = m_resHolder->settings->settingsMap()["states"].toList();
  QVariantList       styles;
  QVector<int>       logIndexes;
  // one line per experiment and channel: the colour tells the experiment, the dash pattern the channel
  for (int k{0}; k < comparison.members.size(); ++k) {
    for (int c{0}; c < comparison.channels.size(); ++c) {
      qsizetype const   index{names.indexOf(comparison.channels[c])};
      QVariantMap const channel = ((index >= 0) && (index < states.size())) ? states[index].toMap() : QVariantMap{};
      QString const     label   = channel.value("label", comparison.channels[c]).toString();
      if (channel.value("notation").toString() == QLatin1String("scientific")) {
        logIndexes.push_back(static_cast<int>(styles.size()));
      }
      styles.push_back(QVariantMap{{"label", (comparison.channels.size() == 1) ? comparison.members[k].label
                                                                              : QStringLiteral("%1 · %2").arg(comparison.members[k].label, label)},
                                   {"name", comparison.channels[c]},
                                   {"color", palette[k % palette.size()].name()},
                                   {"pen", (c % 5) + 1},
                                   {"width", 2},
                                   {"visible", true}});
    }
  }
  chart->setIndexesForLogAxis(logIndexes);
  chart->setStyles(styles);
}

DataViewer *GraphWidget::makeComparisonViewer() {
  auto *chart = new DataViewer(m_resHolder->logger, true); // the log axis shows only when a line uses it
  QVariant const period = m_resHolder->settings->settingsMap()["app_settings"].toMap()["graphUpdatePeriod"];
  if (period.isValid() && (period.toString().toInt() > 0)) {
    chart->setRefreshPeriodMs(1000 * period.toString().toInt());
  }
  chart->setLabelsVisible(m_labelsBtn->isChecked());
  chart->setValuesVisible(m_valuesBtn->isChecked());
  applyViewOptions(chart);
  return chart;
}

void GraphWidget::openCompareDialog(QString const &editKey) {
  if (!m_library) {
    return;
  }
  // the tabs that can take part: everything open except comparisons and tabs still being read
  QVector<ComparisonDialog::OpenTab> openTabs;
  for (ChartPane *pane : std::as_const(m_panes)) {
    for (int i{0}; i < pane->count(); ++i) {
      Tab const *tab = pane->tab(i);
      if (!tab->comparison && !tab->loading) {
        openTabs.push_back({tab->key, tab->label, tab->name, runOfTab(*tab)});
      }
    }
  }
  ChartPane      *editPane = editKey.isEmpty() ? nullptr : paneWith(editKey);
  Tab const      *edited   = editPane ? editPane->tab(editPane->indexOf(editKey)) : nullptr;
  ChartComparison initial  = (edited && edited->comparison) ? *edited->comparison : ChartComparison{};
  Tab const      *current  = m_focused->currentTab();
  ComparisonDialog dialog(m_resHolder, m_library, openTabs, initial, (current && !current->comparison) ? current->key : QString{}, this);
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }
  ChartComparison const comparison = dialog.comparison();
  if (!edited) {
    createComparison(comparison);
    return;
  }
  // the same tab, new settings
  auto const [label, name] = comparisonTitles(comparison);
  styleComparison(edited->ownViewer, comparison);
  editPane->updateTab(editKey, label, name, std::make_shared<ChartComparison const>(comparison));
  computeComparison(editKey, comparison);
}

void GraphWidget::createComparison(ChartComparison const &comparison, bool splitRight) {
  auto const [label, name] = comparisonTitles(comparison);
  Tab tab;
  tab.key        = QStringLiteral("cmp:%1").arg(++m_comparisons);
  tab.label      = label;
  tab.name       = name;
  tab.loading    = true;
  tab.comparison = std::make_shared<ChartComparison const>(comparison);
  tab.ownViewer  = makeComparisonViewer();
  styleComparison(tab.ownViewer, comparison);
  openTab(tab, splitRight);
  computeComparison(tab.key, comparison);
}

namespace {
// one experiment of a comparison: its samples (in the order of the states), or the run to read them from
struct ComparedInput {
  ExperimentRun            run;
  bool                     haveData{false};
  QVector<double>          times; // epoch s
  QVector<QVector<double>> states;
};

double typicalPeriod(QVector<double> const &times) {
  QVector<double> steps;
  for (int i{1}; (i < times.size()) && (steps.size() < 2000); ++i) {
    if (times[i] > times[i - 1]) {
      steps.push_back(times[i] - times[i - 1]);
    }
  }
  if (steps.isEmpty()) {
    return 1.0;
  }
  std::nth_element(steps.begin(), steps.begin() + steps.size() / 2, steps.end());
  return steps[steps.size() / 2];
}
} // namespace

void GraphWidget::computeComparison(QString const &key, ChartComparison const &comparison) {
  QStringList const names = m_resHolder->states->getNames();
  // open tabs give their data right away (the live one as it is now); the others are read
  QVector<ComparedInput> inputs;
  for (ChartComparison::Member const &member : comparison.members) {
    ComparedInput input;
    input.run = member.run;
    std::shared_ptr<DataHolder const> data;
    if (member.key == ChartPane::LIVE_KEY) {
      data = m_resHolder->dataManager->getIntervalToDisplay();
    } else if (ChartPane *pane = paneWith(member.key)) {
      data = pane->tab(pane->indexOf(member.key))->data;
    }
    if (data && !data->getTimes().isEmpty()) {
      input.haveData = true;
      input.times    = data->getTimes();
      input.states   = data->getStates();
    }
    inputs.push_back(input);
  }

  struct Result {
    QVector<double>          times;
    QVector<QVector<double>> values;
    QString                  error;
  };
  auto     result = std::make_shared<Result>();
  QThread *thread = QThread::create([inputs, names, comparison, result]() mutable {
    auto const mode = static_cast<core::AlignMode>(comparison.alignMode);
    QVector<QVector<double>> elapsed;
    QVector<double>          offsets;
    QVector<double>          periods;
    double                   referenceAnchor{qQNaN()};
    for (int k{0}; k < inputs.size(); ++k) {
      ComparedInput &input = inputs[k];
      if (!input.haveData) {
        QString    error;
        auto const loaded = ExperimentLibrary::loadDetached(input.run, &error);
        if (!loaded || loaded->times.isEmpty()) {
          result->error = error.isEmpty() ? QObject::tr("%1 could not be read.").arg(input.run.title) : error;
          return;
        }
        input.times = loaded->times;
        input.states.resize(names.size());
        for (int i{0}; i < names.size(); ++i) { // in the order of the states
          qsizetype const column = loaded->channelNames.isEmpty() ? i : loaded->channelNames.indexOf(names[i]);
          input.states[i] = ((column >= 0) && (column < loaded->states.size())) ? loaded->states[column]
                                                                               : QVector<double>(loaded->times.size(), qQNaN());
        }
      }
      QVector<double> times;
      times.reserve(input.times.size());
      for (double const time : std::as_const(input.times)) {
        times.push_back(time - input.times.first());
      }
      // the anchor the experiments are aligned on
      double offset{0.0};
      if (mode != core::AlignMode::START) {
        ExperimentData data;
        data.run          = input.run;
        data.times        = input.times;
        data.states       = input.states;
        data.channelNames = names;
        core::ExperimentAnalysis const analysis = core::analyseExperiment(experimentInput(data, names));
        qsizetype const temp{names.indexOf("temp")};
        qsizetype const reference{names.indexOf("temp_ref")};
        double const    anchor = core::alignmentAnchor(analysis, times, (temp >= 0) ? input.states[temp] : QVector<double>{},
                                                       (reference >= 0) ? input.states[reference] : QVector<double>{}, mode, comparison.alignN);
        if (k == 0) {
          referenceAnchor = anchor;
        } else if (std::isfinite(anchor) && std::isfinite(referenceAnchor)) {
          offset = referenceAnchor - anchor;
        }
      }
      for (double &time : times) {
        time += offset;
      }
      elapsed.push_back(times);
      offsets.push_back(offset);
      periods.push_back(typicalPeriod(times));
    }

    // a common time grid over all of them (at most 200 000 points)
    double tMin{std::numeric_limits<double>::max()};
    double tMax{std::numeric_limits<double>::lowest()};
    for (QVector<double> const &times : std::as_const(elapsed)) {
      tMin = std::min(tMin, times.first());
      tMax = std::max(tMax, times.last());
    }
    double const finest{*std::min_element(periods.cbegin(), periods.cend())};
    double const step{std::max(finest, (tMax - tMin) / 200000.0)};
    if (!(tMax > tMin) || !(step > 0.0)) {
      result->error = QObject::tr("The experiments have no time span to compare.");
      return;
    }
    qsizetype const count{static_cast<qsizetype>(std::floor((tMax - tMin) / step)) + 1};
    double const    base{inputs.first().times.first()}; // the reference experiment's clock
    result->times.resize(count);
    for (qsizetype i{0}; i < count; ++i) {
      result->times[i] = base + tMin + static_cast<double>(i) * step;
    }
    // each channel of each experiment, interpolated on the grid; no line across gaps
    for (int k{0}; k < inputs.size(); ++k) {
      QVector<double> const &times = elapsed[k];
      for (QString const &channel : comparison.channels) {
        qsizetype const       index{names.indexOf(channel)};
        QVector<double> const values = (index >= 0) ? inputs[k].states[index] : QVector<double>{};
        QVector<double>       column(count, qQNaN());
        qsizetype             j{0};
        for (qsizetype i{0}; (i < count) && !values.isEmpty(); ++i) {
          double const t{tMin + static_cast<double>(i) * step};
          if ((t < times.first()) || (t > times.last())) {
            continue;
          }
          while ((j + 1 < times.size()) && (times[j + 1] < t)) {
            ++j;
          }
          if (j + 1 >= times.size()) {
            column[i] = values.value(j, qQNaN());
            continue;
          }
          double const gap{times[j + 1] - times[j]};
          double const y0{values.value(j, qQNaN())};
          double const y1{values.value(j + 1, qQNaN())};
          if ((gap > 5.0 * periods[k]) || std::isnan(y0) || std::isnan(y1) || (gap <= 0.0)) {
            column[i] = (std::fabs(t - times[j]) <= periods[k]) ? y0 : qQNaN();
          } else {
            column[i] = y0 + (y1 - y0) * (t - times[j]) / gap;
          }
        }
        result->values.push_back(column);
      }
    }
  });
  connect(thread, &QThread::finished, this, [this, thread, key, result] {
    thread->deleteLater();
    ChartPane *pane = paneWith(key);
    if (!pane) {
      return; // closed meanwhile
    }
    if (!result->error.isEmpty()) {
      pane->closeTab(pane->indexOf(key));
      QMessageBox::warning(this, tr("Comparison"), result->error);
      return;
    }
    // a QObject: made here
    auto holder = std::make_shared<DataHolder>(static_cast<quint16>(result->values.size()), m_resHolder->logger,
                                               utils::calculateKey(result->times.first()));
    holder->setSeries(result->times, result->values);
    pane->setTabData(key, holder);
  });
  thread->start(QThread::LowPriority);
}

void GraphWidget::updateStatisticsSource() {
  if (m_optionsDrawer && m_optionsDrawer->isOpen()) {
    updateOptions();
  }
  if (!m_statistics || !m_focused) {
    return;
  }
  Tab const *tab = m_focused->currentTab();
  bool const comparison{tab && tab->comparison};
  m_statistics->setSource(viewer(), (tab && !comparison) ? tab->runKey : QString{}, tab ? tab->name : QString{},
                          comparison ? QStringList{} : m_resHolder->states->getNames());
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
  lay->addSpacing(14 * ssf);

  // overlays
  lay->addWidget(heading(tr("Overlays")));
  // of one channel, chamber temperature unless chosen otherwise
  m_overlayChannel = new QComboBox(page);
  m_overlayChannel->setMinimumHeight(30 * ssf);
  {
    QStringList const  names  = m_resHolder->states->getNames();
    QVariantList const styles = m_resHolder->settings->settingsMap()["states"].toList();
    for (int i{0}; i < names.size(); ++i) {
      QString const label = (i < styles.size()) ? styles[i].toMap()["label"].toString() : QString{};
      m_overlayChannel->addItem(label.isEmpty() ? names[i] : label, names[i]);
    }
    m_overlayChannel->setCurrentIndex(std::max(0, m_overlayChannel->findData(QStringLiteral("temp"))));
  }
  lay->addWidget(m_overlayChannel);
  m_minCheck = new QCheckBox(tr("Minimum"), page);
  m_maxCheck = new QCheckBox(tr("Maximum"), page);
  m_avgCheck = new QCheckBox(tr("Average"), page);
  for (QCheckBox *check : {m_minCheck, m_maxCheck, m_avgCheck}) {
    lay->addWidget(check);
  }
  lay->addSpacing(14 * ssf);

  // time axis
  lay->addWidget(heading(tr("Time axis")));
  auto *clockRadio = new QRadioButton(tr("Wall clock time"), page);
  m_relativeRadio  = new QRadioButton(tr("Time from the start"), page);
  clockRadio->setChecked(true);
  lay->addWidget(clockRadio);
  lay->addWidget(m_relativeRadio);
  lay->addSpacing(14 * ssf);

  // the experiment shown
  lay->addWidget(heading(tr("Experiment")));
  m_detailsBtn = new QPushButton(tr("Edit experiment details…"), page);
  m_detailsBtn->setMinimumHeight(30 * ssf);
  m_detailsBtn->setFocusPolicy(Qt::NoFocus);
  lay->addWidget(m_detailsBtn);
  connect(m_detailsBtn, &QPushButton::clicked, this, &GraphWidget::editExperimentDetails);
  lay->addStretch();

  // smoothing and overlays apply to both charts when the page is split
  auto apply = [this] {
    for (ChartPane *pane : std::as_const(m_panes)) {
      for (DataViewer *chart : pane->viewers()) {
        applyViewOptions(chart);
      }
    }
  };
  connect(m_smoothSpin, &QSpinBox::valueChanged, this, apply);
  connect(m_relativeRadio, &QRadioButton::toggled, this, apply);
  for (QCheckBox *check : {m_minCheck, m_maxCheck, m_avgCheck}) {
    connect(check, &QCheckBox::toggled, this, apply);
  }
  connect(m_overlayChannel, &QComboBox::currentIndexChanged, this, apply);
  return page;
}

void GraphWidget::applyViewOptions(DataViewer *chart) const {
  if (!m_smoothSpin) {
    return;
  }
  chart->setSmoothingWindow(m_smoothSpin->value());
  chart->setRelativeTime(m_relativeRadio->isChecked());
  chart->setOverlays(m_minCheck->isChecked(), m_maxCheck->isChecked(), m_avgCheck->isChecked(), m_overlayChannel->currentData().toString());
}

void GraphWidget::updateOptions() {
  updateRangeFields();
  Tab const *tab = m_focused ? m_focused->currentTab() : nullptr;
  bool const stored{tab && !tab->storeKey.isEmpty() && !tab->loading};
  m_detailsBtn->setEnabled(stored && (m_resHolder->users->getCurrentAccessLevel() >= AccessManager::AccessLevel::USER_START_1));
  m_detailsBtn->setToolTip(stored ? tr("DUT, operator and purpose of %1").arg(tab->name)
                                  : tr("Only for experiments saved in the experiment database"));
}

void GraphWidget::editExperimentDetails() {
  Tab const *tab = m_focused ? m_focused->currentTab() : nullptr;
  if (!tab || tab->storeKey.isEmpty()) {
    return;
  }
  auto const manifest = storage::RunStore::manifest(tab->storeKey);
  // tags and notes are no longer edited here (kept as they are)
  RunDetailsDialog dialog(manifest ? manifest->metadata : core::RunMetadata{}, tr("Experiment details: %1").arg(tab->name), this, false);
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }
  if (!storage::RunStore::updateMetadata(tab->storeKey, dialog.metadata())) {
    (new MessageDialog(tr("Experiment details"), tr("This experiment has no manifest yet; it gets one at the next start of the application.")))->open();
    return;
  }
  if (m_library) {
    m_library->refresh(); // searchable details in the experiment list
  }
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

void GraphWidget::showRun(ExperimentRun const &run, bool splitRight, double from, double to) {
  if (!m_library) {
    return;
  }
  Tab tab{run.key, startText(run.start), run.title, run.key, nullptr, run.storeKey};
  tab.zoomFrom = from;
  tab.zoomTo   = to;
  openLoading(tab, splitRight, [run](Samples &out, QString &error) {
    auto const loaded = ExperimentLibrary::loadDetached(run, &error);
    if (!loaded) {
      return false;
    }
    out = {loaded->times, loaded->channelNames, loaded->states};
    return true;
  });
}

void GraphWidget::showDay(QDate const &day, QVector<storage::ExperimentSpan> const &experiments) {
  // the experiments of the day one after the other, with a gap between them
  QString const name = (experiments.size() == 1) ? tr("%1 · 1 experiment").arg(day.toString("yyyy-MM-dd"))
                                                 : tr("%1 · %2 experiments").arg(day.toString("yyyy-MM-dd")).arg(experiments.size());
  // one experiment: its date and time; several: the day and how many
  QString const label = (experiments.size() == 1) ? startText(experiments.first().start) : name;
  QString const key   = QStringLiteral("day:") + day.toString(Qt::ISODate);
  Tab           tab{key, label, name, QStringLiteral("chart:") + key, nullptr, experiments.size() == 1 ? experiments.first().key : QString{}};
  QStringList const names = m_resHolder->states->getNames();
  openLoading(tab, false, [names, experiments](Samples &out, QString &error) {
    QVector<double>          times;
    QVector<QVector<double>> states(names.size());
    for (storage::ExperimentSpan const &experiment : experiments) {
      storage::RunSamples samples;
      if (!storage::RunStore::loadSamples(experiment.key, samples) || samples.times.isEmpty()) {
        continue;
      }
      if (!times.isEmpty()) { // gap: no line drawn between two experiments
        times.push_back((times.last() + samples.times.first()) / 2.0);
        for (auto &state : states) {
          state.push_back(qQNaN());
        }
      }
      times += samples.times;
      for (int i{0}; i < names.size(); ++i) {
        qsizetype const column = samples.columns.indexOf(names[i]);
        states[i] += (column >= 0) ? samples.values[column] : QVector<double>(samples.times.size(), qQNaN());
      }
    }
    if (times.isEmpty()) {
      error = tr("The experiments of this day could not be read.");
      return false;
    }
    out = {times, QStringList{}, states};
    return true;
  });
}

void GraphWidget::showExperiment(storage::ExperimentSpan const &experiment) {
  // samples from the experiment database
  QString const key = QStringLiteral("db:") + experiment.key;
  Tab           tab{key, startText(experiment.start), experiment.name, QStringLiteral("chart:") + key, nullptr, experiment.key};
  QString const storeKey = experiment.key;
  openLoading(tab, false, [storeKey](Samples &out, QString &error) {
    storage::RunSamples samples;
    if (!storage::RunStore::loadSamples(storeKey, samples, &error)) {
      return false;
    }
    out = {samples.times, samples.columns, samples.values};
    return true;
  });
}

void GraphWidget::handleAccessChange(QString const &name, AccessManager::AccessLevel newLevel) {
  bool const enablePress{newLevel >= AccessManager::AccessLevel::USER_START_1};
  m_exportToBtn->setEnabled(enablePress);
  m_styleBtn->setEnabled(enablePress);
  m_canAnnotate = enablePress && m_notes;
  updateDayButton(); // annotate: also depends on the tab shown
}

void GraphWidget::exportData() {
  Tab const *tab   = m_focused->currentTab();
  auto const shown = viewer()->getData();
  if (!tab || !shown || shown->getTimes().isEmpty()) {
    (new MessageDialog(tr("Export"), tr("There is no data to export in this chart.")))->open();
    return;
  }
  QVector<double> const shownTimes = shown->getTimes();
  QDate const           day = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(shownTimes.first())).date();
  bool const comparison{static_cast<bool>(tab->comparison)};
  // a comparison is exported as shown (its own lines): no day or custom range from the recorded files
  auto *dialog = new ExportDialog(m_resHolder, day, m_services != nullptr, !tab->storeKey.isEmpty(), !comparison, this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  Tab const tabCopy = *tab;
  connect(dialog, &QDialog::accepted, this, [this, dialog, tabCopy, shown, shownTimes]() {
    QString const format   = dialog->format();
    QString const filePath = dialog->filePath();
    auto const    range    = dialog->range();
    QRectF const  view     = viewer()->getCurrentView();
    auto done = [this](bool ok, QString const &path, QString const &error = {}) {
      (new MessageDialog(tr("Export"), ok ? tr("Saved to %1").arg(path) : (error.isEmpty() ? tr("Failed to write %1").arg(path) : error)))->open();
    };

    // the time range chosen
    double from{shownTimes.first()};
    double to{shownTimes.last()};
    if (range == ExportDialog::Range::VIEW) {
      from = view.left();
      to   = view.right();
    } else if ((range == ExportDialog::Range::DAY) || (range == ExportDialog::Range::CUSTOM)) {
      from = static_cast<double>(dialog->from().toSecsSinceEpoch());
      to   = static_cast<double>(dialog->to().toSecsSinceEpoch());
    }

    if (format == QLatin1String("json")) { // the saved experiment as a bundle
      QString           error;
      QJsonObject const bundle = storage::RunStore::bundle(tabCopy.storeKey, &error);
      QSaveFile         file(filePath);
      bool const ok{!bundle.isEmpty() && file.open(QIODevice::WriteOnly) && (file.write(QJsonDocument(bundle).toJson(QJsonDocument::Indented)) > 0)
                    && file.commit()};
      done(ok, filePath, error);
      return;
    }
    if ((format == QLatin1String("html")) || (format == QLatin1String("pdf"))) { // experiment report of the range
      ExperimentRun          run;
      QVector<ExperimentRun> compared;
      QStringList            channels;
      if (tabCopy.comparison) { // the first experiment, compared with the others
        run = tabCopy.comparison->members.first().run;
        for (int k{1}; k < tabCopy.comparison->members.size(); ++k) {
          compared.push_back(tabCopy.comparison->members[k].run);
        }
        channels = tabCopy.comparison->channels;
      } else {
        run = (range == ExportDialog::Range::EXPERIMENT) ? runOfTab(tabCopy) : m_library->makeRangeRun(from, to);
      }
      QApplication::setOverrideCursor(Qt::WaitCursor);
      QString       written;
      QString const error = m_services->writeReport(run, compared, format == QLatin1String("pdf"), written, channels);
      QApplication::restoreOverrideCursor();
      bool ok{error.isEmpty()};
      if (ok && (QDir::cleanPath(written) != QDir::cleanPath(filePath))) {
        QFile::remove(filePath);
        ok = QFile::rename(written, filePath) || QFile::copy(written, filePath);
      }
      done(ok, filePath, error);
      return;
    }

    // chart image or data of the range
    auto interval = std::make_shared<DataHolder>(m_resHolder->states->size(), m_resHolder->logger, 0);
    if ((range == ExportDialog::Range::DAY) || (range == ExportDialog::Range::CUSTOM)) {
      interval = dialog->readInterval();
    } else {
      interval->append(shown.get());
      if (range == ExportDialog::Range::VIEW) {
        interval->slice(view.left(), view.right());
      }
    }
    // the lines as the chart has them (a comparison: its own)
    QVariantList const statesVariantList = viewer()->styles();

    if (ExportDialog::isData(format)) {
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
      auto              *exporter = new DataExporter(interval.get(), format, labelsList, enablesList, this);
      StringPathProvider path(filePath, m_resHolder->logger);
      exporter->saveToFile(&path, this);
      return;
    }

    // chart image: drawn like the focused chart
    auto *exportViewer = new DataViewer(m_resHolder->logger, viewer()->hasLogAxis(), this);
    exportViewer->setIndexesForLogAxis(viewer()->logAxisIndexes());
    exportViewer->setData(interval);
    exportViewer->setStyles(statesVariantList);
    exportViewer->setLabelsVisible(viewer()->labelsVisible());
    applyViewOptions(exportViewer);
    if (range == ExportDialog::Range::VIEW) {
      exportViewer->setView(view);
    }
    bool ok{false};
    if (format == QLatin1String("bmp")) {
      ok = exportViewer->getPlot()->saveBmp(filePath, 2000, 1000, 1);
    } else if (format == QLatin1String("png")) {
      ok = exportViewer->getPlot()->savePng(filePath, 2000, 1000, 1);
    } else {
      ok = exportViewer->getPlot()->saveJpg(filePath, 2000, 1000, 1, 100);
    }
    exportViewer->deleteLater();
    done(ok, filePath);
  });
  dialog->open();
}
} // namespace deepvaccontrol
