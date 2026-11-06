#include "graphwidget.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QFontMetrics>
#include <QScrollBar>
#include <QTextBrowser>
#include <QComboBox>
#include "calendarpanel.h"
#include "chartsidebar.h"
#include "chartstatspanel.h"
#include "datetimefield.h"
#include "lineeditscreen.h"
#include "hardwarestatus.h"
#include "derivedexpression.h"
#include "derivedvariablesdialog.h"
#include "recordingfile.h"
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <numeric>
#include <QDataStream>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRadioButton>
#include <QSaveFile>
#include <QFrame>
#include <QLabel>
#include <QLocale>
#include <QSpinBox>
#include <QGridLayout>
#include <QMenu>
#include <QPainter>
#include <QSplitter>
#include <QSet>
#include <QSettings>
#include <QTabBar>
#include <QTextStream>
#include <QThread>
#include <cmath>

#include "analysisnotesstore.h"
#include "analysisservices.h"
#include "annotationdialog.h"
#include "comparison.h"
#include "runmanifest.h"
#include "runrecord.h"
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
#include "pressureunits.h"
#include "theme.h"
#include "touchsupport.h"
#include "utils.h"

namespace deepvaccontrol {
namespace {
QString const SESSION_FILE{QStringLiteral("data/chart/session.json")}; // open tabs, split, zoom, options

QJsonObject runToJson(ExperimentRun const &run) {
  return QJsonObject{{"key", run.key},         {"title", run.title},     {"start", run.start},
                     {"end", run.end},         {"samples", run.samples}, {"files", QJsonArray::fromStringList(run.files)},
                     {"custom", run.custom},   {"storeKey", run.storeKey}, {"chamber", run.chamber},
                     {"details", run.details}};
}

ExperimentRun runFromJson(QJsonObject const &json) {
  ExperimentRun run;
  run.key     = json.value("key").toString();
  run.title   = json.value("title").toString();
  run.start   = json.value("start").toDouble();
  run.end     = json.value("end").toDouble();
  run.samples = json.value("samples").toInteger();
  for (QJsonValue const &file : json.value("files").toArray()) {
    run.files << file.toString();
  }
  run.custom   = json.value("custom").toBool();
  run.storeKey = json.value("storeKey").toString();
  run.chamber  = json.value("chamber").toString();
  run.details  = json.value("details").toString();
  return run;
}

QJsonObject comparisonToJson(ChartComparison const &comparison) {
  QJsonArray members;
  for (ChartComparison::Member const &member : comparison.members) {
    members.push_back(QJsonObject{{"key", member.key}, {"label", member.label}, {"run", runToJson(member.run)}});
  }
  return QJsonObject{{"members", members},
                     {"channels", QJsonArray::fromStringList(comparison.channels)},
                     {"alignMode", comparison.alignMode},
                     {"alignN", comparison.alignN}};
}

ChartComparison comparisonFromJson(QJsonObject const &json) {
  ChartComparison comparison;
  for (QJsonValue const &value : json.value("members").toArray()) {
    QJsonObject const member = value.toObject();
    comparison.members.push_back({member.value("key").toString(), member.value("label").toString(), runFromJson(member.value("run").toObject())});
  }
  for (QJsonValue const &channel : json.value("channels").toArray()) {
    comparison.channels << channel.toString();
  }
  comparison.alignMode = json.value("alignMode").toInt();
  comparison.alignN    = json.value("alignN").toInt(1);
  return comparison;
}

// "2026-09-24 09:55": the day button text of a single experiment
QString startText(double epochSec) {
  return QDateTime::fromSecsSinceEpoch(static_cast<qint64>(epochSec)).toString("yyyy-MM-dd HH:mm");
}
}

GraphWidget::GraphWidget(ResHolder *resHolder, QWidget *parent)
    : QWidget{parent}, m_resHolder{resHolder}, m_styleEditor{nullptr} {
  auto *mainLay = new QGridLayout(this);

  // channels shown on the logarithmic axis: "notation": "scientific" in the settings, and the pressure (in Torr)
  QList<QVariant> const states_list = resHolder->settings->settingsMap()["states"].toList();
  for (int i{0U}; i < states_list.size(); ++i) {
    QVariantMap const paramMap = states_list[i].toMap();
    if ((paramMap.value("notation").toString() == QLatin1String("scientific")) || pressure::isPressureChannel(paramMap.value("name").toString())) {
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
  m_sessionTimer = new QTimer(this);
  m_sessionTimer->setSingleShot(true);
  m_sessionTimer->setInterval(2000);
  connect(m_sessionTimer, &QTimer::timeout, this, &GraphWidget::saveSession);

  // whole page in one card
  mainLay->setContentsMargins(0, 0, 0, 0);
  auto *card = new QFrame(this);
  card->setObjectName("pageCard");
  mainLay->addWidget(card, 0, 0);
  auto *cardLay = new QGridLayout(card);
  cardLay->setContentsMargins(14 * ssf, 6 * ssf, 14 * ssf, 12 * ssf); // little above the tabs
  cardLay->setHorizontalSpacing(8 * ssf);
  cardLay->setVerticalSpacing(8 * ssf);

  // what the chart shows (side bar ▸ Actions)
  auto toggle = [this](QString const &text, QString const &tip, bool on) {
    auto *action = new QAction(text, this);
    action->setCheckable(true);
    action->setChecked(on);
    action->setToolTip(tip);
    return action;
  };
  m_labelsBtn = toggle(tr("Labels"), tr("The box with the line labels and colours"), true);
  m_valuesBtn = toggle(tr("Tooltip"), tr("The value of the line where the mouse points (split: the other chart follows)"), false);
  m_eventsAct    = toggle(tr("Events"), tr("Program, PLC and operator events as dotted lines"), false);
  m_anomaliesAct = toggle(tr("Anomalies"), tr("The detected anomalies as coloured lines"), false);
  m_stagesAct    = toggle(tr("Stages"), tr("Ramp / setpoint / hold stages as a strip along the top"), false);
  m_setpointAct  = toggle(tr("Setpoint"), tr("The setpoint as a dashed line (a comparison: its first experiment's)"), false);

  // the charts with their tabs: one pane, or two side by side ("Split right")
  m_split = new QSplitter(Qt::Horizontal, card);
  m_split->setHandleWidth(12 * ssf); // wide enough for a finger
  m_split->setChildrenCollapsible(false);
  m_split->setStyleSheet(QStringLiteral("QSplitter::handle { background: transparent; }"));
  ChartPane *first = addPane();
  cardLay->addWidget(m_split, 0, 0);
  cardLay->setRowStretch(0, 1);
  cardLay->setColumnStretch(0, 1);


  // the side bar right of the chart: a rail of buttons and the panel of the one chosen
  mainLay->setHorizontalSpacing(10 * ssf);
  m_sideBar    = new ChartSideBar(this);
  m_statsPanel = new ChartStatsPanel(m_resHolder->states->getNames(), m_resHolder->settings->settingsMap()["states"].toList());
  m_calendarPanel = new CalendarPanel();
  m_exportHost    = new QWidget();
  auto *exportLay = new QVBoxLayout(m_exportHost);
  exportLay->setContentsMargins(0, 0, 0, 0);
  m_exportEmpty = new QLabel(tr("There is no data to export in this chart."), m_exportHost);
  m_exportEmpty->setProperty("muted", true);
  m_exportEmpty->setWordWrap(true);
  exportLay->addWidget(m_exportEmpty);
  m_sideBar->addPage(QStringLiteral("channels"), tr("Channels"), QStringLiteral("channels"), buildChannelsPage(), false, false);
  m_sideBar->addPage(QStringLiteral("options"), tr("Options"), QStringLiteral("manual"), buildOptions());
  m_sideBar->addPage(QStringLiteral("statistics"), tr("Statistics"), QStringLiteral("stats"), m_statsPanel);
  m_sideBar->addPage(QStringLiteral("info"), tr("Info"), QStringLiteral("info"), buildInfoPage());
  m_sideBar->addPage(QStringLiteral("ai"), tr("Assistant"), QStringLiteral("ai"), buildAiPage());
  m_sideBar->setHeaderWidget(QStringLiteral("ai"), m_aiHeader);
  // "6 of 8 shown" under the Channels title
  connect(m_styleEditor, &GraphStyleEditor::countChanged, m_sideBar, [this](QString const &text) {
    m_sideBar->setSubtitle(QStringLiteral("channels"), text);
  });
  m_sideBar->setSubtitle(QStringLiteral("channels"), m_styleEditor->countText());
  m_sideBar->addPage(QStringLiteral("calendar"), tr("Calendar"), QStringLiteral("calendar"), m_calendarPanel, true);
  m_sideBar->addPage(QStringLiteral("export"), tr("Export"), QStringLiteral("export"), m_exportHost, true);
  mainLay->addWidget(m_sideBar, 0, 1);
  mainLay->setColumnStretch(0, 1);
  connect(m_sideBar, &ChartSideBar::opened, this, &GraphWidget::refreshSide);
  connect(m_calendarPanel, &CalendarPanel::experimentSelected, this, &GraphWidget::showExperiment);
  connect(m_calendarPanel, &CalendarPanel::daySelected, this, &GraphWidget::showDay);
  connect(m_calendarPanel, &CalendarPanel::liveSelected, this, [this] {
    m_panes.first()->selectTab(ChartPane::LIVE_KEY);
    setFocusedPane(m_panes.first());
  });
  connect(m_statsPanel, &ChartStatsPanel::fullReportRequested, this, [this] {
    if (m_statistics) {
      m_statistics->show();
      m_statistics->raise();
      updateStatisticsSource(); // shown: its analysis is wanted now
    }
  });

  connect(m_labelsBtn, &QAction::toggled, this, &GraphWidget::updateLegendMode);
  connect(m_valuesBtn, &QAction::toggled, this, &GraphWidget::updateLegendMode);
  for (QAction *marker : {m_eventsAct, m_anomaliesAct, m_stagesAct}) {
    connect(marker, &QAction::toggled, this, [this] {
      requestInsights();
      scheduleSessionSave();
    });
  }
  connect(m_setpointAct, &QAction::toggled, this, [this] {
    applySetpointLine();
    scheduleSessionSave();
  });
  // the live tab's analysis is brought up to date now and then
  m_insightTimer = new QTimer(this);
  m_insightTimer->setInterval(60'000);
  connect(m_insightTimer, &QTimer::timeout, this, &GraphWidget::requestInsights);
  m_insightTimer->start();


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



  connect(m_resHolder->dataManager, &DataManager::intervalToAddRecreated, this, [this](double key) {
    for (ChartPane *pane : std::as_const(m_panes)) {
      pane->reloadLive(key);
    }
  });

  connect(m_resHolder->status, &StatusManager::appStateChanged, this, &GraphWidget::updateLiveActivity);

  // the live data of today, always the first tab of the first pane
  updateLiveActivity();
  first->addTab({ChartPane::LIVE_KEY, tr("Live"), tr("Live data of today"), QStringLiteral("chart:live"), nullptr});

  updateDayButton();
  updateLegendMode();
  handleAccessChange("", m_resHolder->users->getCurrentAccessLevel());
  connect(m_resHolder->users, &AccessManager::userSelected, this, &GraphWidget::handleAccessChange);
  initAnalysis();
  connect(m_split, &QSplitter::splitterMoved, this, &GraphWidget::scheduleSessionSave);
  for (QAction *action : {m_labelsBtn, m_valuesBtn}) {
    connect(action, &QAction::toggled, this, &GraphWidget::scheduleSessionSave);
  }
  // the tabs of the last time, once the page is built
  QTimer::singleShot(0, this, &GraphWidget::restoreSession);
}

GraphWidget::~GraphWidget() {
  saveSession();
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
  chart->setSetpointChannel(static_cast<int>(m_resHolder->states->getNames().indexOf("temp_ref")));
  if (m_labelsBtn) {
    chart->setSetpointLine(m_setpointAct->isChecked());
    chart->setLabelsVisible(m_labelsBtn->isChecked());
    chart->setValuesVisible(m_valuesBtn->isChecked());
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
  pane->setLiveActive(m_liveActive, m_liveIdleText);
  m_panes.push_back(pane);
  if (m_notes) {
    pane->setNotes(m_notes);
  }
  // the tools: shown in the left pane only, acting on the selected pane
  pane->setToolbarShown(m_panes.size() == 1);
  pane->setToolbarArea(m_split->parentWidget()); // the card: over both panes
  pane->toolbar()->bindToggles(m_labelsBtn, m_valuesBtn);
  pane->setToolbarPinned(m_toolbarPinned);
  pane->setTool(m_tool, false);
  connect(pane->toolbar(), &ChartToolbar::pinnedChanged, this, [this](bool pinned) { setToolbarsPinned(pinned); });
  connect(pane->toolbar(), &ChartToolbar::toolChanged, this, [this](ChartToolbar::Tool tool) {
    m_tool = tool;
    applyTool();
  });
  connect(pane->toolbar(), &ChartToolbar::fitRequested, this, &GraphWidget::fitView);
  connect(pane, &ChartPane::toolResetRequested, this, [this] { m_panes.first()->toolbar()->setTool(ChartToolbar::Tool::INSPECT); });
  connect(pane, &ChartPane::addRequested, this, [this, pane](QPoint const &globalPos) { showAddMenu(pane, globalPos); });

  connect(pane, &ChartPane::activated, this, [this, pane] {
    setFocusedPane(pane);
  });
  connect(pane, &ChartPane::currentTabChanged, this, [this, pane] {
    if (pane == m_focused) {
      updateDayButton();
      updateStatisticsSource();
    }
  });
  connect(pane, &ChartPane::tabsChanged, this, &GraphWidget::scheduleSessionSave);
  connect(pane, &ChartPane::tabsChanged, this, [this, pane] { requestInsight(pane); });
  connect(pane, &ChartPane::currentTabChanged, this, [this, pane] { requestInsight(pane); });
  connect(pane, &ChartPane::currentTabChanged, this, &GraphWidget::scheduleSessionSave);
  connect(pane->mainViewer(), &DataViewer::dataUpdated, this, [this, pane] { refreshDerived(pane); });
  connect(pane, &ChartPane::currentTabChanged, this, [this, pane] { refreshDerived(pane); });
  connect(pane->mainViewer()->getPlot()->xAxis, qOverload<QCPRange const &>(&QCPAxis::rangeChanged), this,
          &GraphWidget::scheduleSessionSave);
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

void GraphWidget::applyTool() {
  // every pane drags the same way; the zoom / pan pad is on the selected one
  for (ChartPane *pane : std::as_const(m_panes)) {
    pane->setTool(m_tool, pane == m_focused);
  }
}

void GraphWidget::setFocusedPane(ChartPane *pane) {
  m_focused = pane;
  applyTool();
  bool const split{m_panes.size() > 1};
  for (ChartPane *each : std::as_const(m_panes)) {
    each->setHighlighted(split && (each == m_focused));
  }
  updateDayButton();
  updateStatisticsSource();
  scheduleSessionSave();
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
  ChartPane *target = m_restoreTarget ? m_restoreTarget : (splitRight ? rightPane() : m_focused);
  Tab        opened = tab;
  if (m_restoreView) {
    opened.view = *m_restoreView;
  }
  target->addTab(opened);
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
      QMessageBox::warning(this, tr("Analysis"), error->isEmpty() ? tr("%1 could not be read.").arg(tab.name) : *error);
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
  // all the others of this pane (the live tab stays)
  bool others{false};
  for (int i{0}; i < pane->count(); ++i) {
    others = others || ((i != index) && (pane->tab(i)->key != ChartPane::LIVE_KEY));
  }
  QString const keep = tab->key;
  menu.addAction(tr("Close others"), this, [pane, keep] {
        for (int i{pane->count() - 1}; i >= 0; --i) {
          if ((pane->tab(i)->key != keep) && (pane->tab(i)->key != ChartPane::LIVE_KEY)) {
            pane->closeTab(i);
          }
        }
      })
    ->setEnabled(others);
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
  // annotations and rules are of recorded time: not on a comparison's aligned axis
  Tab const *shown = m_focused ? m_focused->currentTab() : nullptr;
  m_panes.first()->toolbar()->setAnnotateEnabled(m_canAnnotate && !(shown && shown->comparison));
  refreshSide();
}

void GraphWidget::updateLegendMode() {
  for (ChartPane *pane : std::as_const(m_panes)) {
    for (DataViewer *chart : pane->viewers()) {
      chart->setLabelsVisible(m_labelsBtn->isChecked());
      chart->setValuesVisible(m_valuesBtn->isChecked());
    }
  }
}

void GraphWidget::showAddMenu(ChartPane *pane, QPoint const &globalPos) {
  setFocusedPane(pane);
  QMenu    menu(this);
  QAction *open    = menu.addAction(tr("Open experiment…"));
  QAction *compare = menu.addAction(tr("New comparison…"));
  open->setEnabled(m_drawer != nullptr);
  compare->setEnabled(m_services != nullptr);
  QAction const *chosen = menu.exec(globalPos);
  if ((chosen == open) && m_drawer) {
    m_drawer->open(); // the experiment list
  } else if (chosen == compare) {
    openCompareDialog();
  }
}

void GraphWidget::setToolbarsPinned(bool pinned, bool remember) {
  m_toolbarPinned = pinned;
  for (ChartPane *pane : std::as_const(m_panes)) {
    pane->setToolbarPinned(pinned);
  }
  if (remember) {
    QSettings settings(QStringLiteral("data/chart/toolbar.ini"), QSettings::IniFormat);
    settings.setValue(QStringLiteral("pinned/%1").arg(m_resHolder->users->getCurrentUserName()), pinned);
  }
}

void GraphWidget::refreshCalendar() {
  // "#N" as in the experiment list (#1 is the oldest)
  QHash<QString, int> numbers;
  if (m_services) {
    m_services->ensureScanned(); // the numbers come once the list is read (runsChanged)
  }
  if (m_library) {
    QVector<ExperimentRun> const all = m_library->chronological();
    for (int i{0}; i < all.size(); ++i) {
      if (!all[i].storeKey.isEmpty()) {
        numbers.insert(all[i].storeKey, i + 1);
      }
    }
  }
  m_calendarPanel->setExperiments(storage::RunStore::spans(), numbers);
}

void GraphWidget::refreshSide() {
  if (!m_sideBar || !m_focused) {
    return;
  }
  QString const key = m_sideBar->current();
  Tab const    *tab = m_focused->currentTab();
  bool const    comparison{tab && tab->comparison};
  if (key == QLatin1String("options")) {
    updateOptions();
  } else if (key == QLatin1String("info")) {
    refreshInfo();
  } else if (key == QLatin1String("ai")) {
    if (m_services) {
      m_services->ensureScanned(); // the experiment's number comes with the list (runsChanged)
    }
    emit assistantWanted();
    updateAiButtons();
  } else if (key == QLatin1String("statistics")) {
    m_statsPanel->setSource(viewer(), comparison);
  } else if (key == QLatin1String("export")) {
    refreshExportPanel();
  } else if (key == QLatin1String("calendar")) {
    refreshCalendar();
  } else if (key == QLatin1String("channels")) {
    m_styleEditor->setVisible(!comparison); // a comparison draws the channels chosen for it
    m_channelsNote->setVisible(comparison);
  }
}

QWidget *GraphWidget::buildChannelsPage() {
  double const ssf{utils::ssf()};
  auto        *page = new QWidget();
  auto        *lay  = new QVBoxLayout(page);
  lay->setContentsMargins(0, 0, 0, 0);
  m_styleEditor->setParent(page);
  m_styleEditor->setCompact();
  lay->addWidget(m_styleEditor, 1);
  m_styleEditor->show();
  m_channelsNote = new QWidget(page);
  auto *noteLay  = new QVBoxLayout(m_channelsNote);
  noteLay->setContentsMargins(0, 0, 8 * ssf, 0);
  auto *note = new QLabel(tr("A comparison draws the channels chosen for it."), m_channelsNote);
  note->setProperty("muted", true);
  note->setWordWrap(true);
  auto *changeBtn = new QPushButton(tr("Change comparison…"), m_channelsNote);
  changeBtn->setMinimumHeight(30 * ssf);
  changeBtn->setFocusPolicy(Qt::NoFocus);
  connect(changeBtn, &QPushButton::clicked, this, [this] {
    if (Tab const *tab = m_focused ? m_focused->currentTab() : nullptr; tab && tab->comparison) {
      openCompareDialog(tab->key);
    }
  });
  noteLay->addWidget(note);
  noteLay->addWidget(changeBtn);
  noteLay->addStretch();
  m_channelsNote->hide();
  lay->addWidget(m_channelsNote, 1);
  return page;
}

QString GraphWidget::runReference() const {
  Tab const *tab = m_focused ? m_focused->currentTab() : nullptr;
  if (!tab || tab->comparison || !m_library || (tab->key == ChartPane::LIVE_KEY)) {
    return {};
  }
  ExperimentRun const          run = runOfTab(*tab);
  QVector<ExperimentRun> const all = m_library->chronological();
  for (int i{0}; i < all.size(); ++i) {
    if ((all[i].key == run.key) || (!run.storeKey.isEmpty() && (all[i].storeKey == run.storeKey))) {
      return QStringLiteral("#%1").arg(i + 1);
    }
  }
  // not in the list yet (it is still being read): the experiment's database key, which the assistant resolves too
  return tab->storeKey;
}

QJsonObject GraphWidget::chartView() const {
  QString const run = runReference();
  if (!m_focused) {
    return {};
  }
  auto clock = [](double t) { return QDateTime::fromSecsSinceEpoch(static_cast<qint64>(std::llround(t))).toString("yyyy-MM-dd HH:mm:ss"); };
  if (Tab const *tab = m_focused->currentTab(); tab && tab->comparison) {
    // a comparison: its experiments (the assistant's numbers of the saved ones), their alignment and the part shown
    QJsonArray members;
    for (ChartComparison::Member const &member : tab->comparison->members) {
      QString ref;
      if (m_library && (member.key != ChartPane::LIVE_KEY) && !member.key.startsWith(QLatin1String("day:"))) {
        QVector<ExperimentRun> const all = m_library->chronological();
        for (int i{0}; i < all.size(); ++i) {
          if ((all[i].key == member.run.key) || (!member.run.storeKey.isEmpty() && (all[i].storeKey == member.run.storeKey))) {
            ref = QStringLiteral("#%1").arg(i + 1);
            break;
          }
        }
        if (ref.isEmpty()) {
          ref = member.run.storeKey;
        }
      }
      members.push_back(QJsonObject{{"run", ref}, {"label", member.label.isEmpty() ? member.run.title : member.label}});
    }
    static char const *const ALIGNS[]{"start", "setpoint_change", "target_crossing", "stage"};
    int const          mode{std::clamp(tab->comparison->alignMode, 0, 3)};
    QCPRange const     view   = viewer()->getPlot()->xAxis->range();
    double             origin = m_comparisonData.value(tab->key).origin;
    if (!std::isfinite(origin)) {
      origin = m_focused->dataStart();
    }
    QJsonObject result{{"compare", members},
                       {"align", ALIGNS[mode]},
                       {"align_index", tab->comparison->alignN},
                       {"channels", QJsonArray::fromStringList(tab->comparison->channels)}};
    if (std::isfinite(origin)) {
      result.insert("from_s", view.lower - origin);
      result.insert("to_s", view.upper - origin);
    }
    return result;
  }
  if (run.isEmpty()) {
    // the live data or a recorded day: the day and the part of it shown
    Tab const *tab = m_focused->currentTab();
    if (!tab || tab->comparison || !viewer()->getData()) {
      return {};
    }
    QString day;
    if (tab->key == ChartPane::LIVE_KEY) {
      day = QDate::currentDate().toString(Qt::ISODate);
    } else if (tab->key.startsWith(QLatin1String("day:"))) {
      day = tab->key.mid(4, 10);
    } else {
      return {};
    }
    QCPRange const view = viewer()->getPlot()->xAxis->range();
    return QJsonObject{{"day", day}, {"from", clock(view.lower)}, {"to", clock(view.upper)}};
  }
  double const start{m_focused->dataStart()};
  if (std::isnan(start)) {
    return {};
  }
  QCPRange const view = viewer()->getPlot()->xAxis->range();
  // the lines shown
  QStringList const names = m_resHolder->states->getNames();
  QCustomPlot      *plot  = viewer()->getPlot();
  QStringList       channels;
  QStringList       offScale; // drawn, but all above or below the values shown
  QCPRange const    values = plot->yAxis->range();
  for (int i{0}; (i < plot->graphCount()) && (i < names.size()); ++i) {
    if (!plot->graph(i)->visible()) {
      continue;
    }
    channels << names[i];
    bool           found{false};
    QCPRange const range = plot->graph(i)->getValueRange(found, QCP::sdBoth, QCPRange(view.lower, view.upper));
    if (found && (plot->graph(i)->valueAxis() == plot->yAxis) && ((range.lower > values.upper) || (range.upper < values.lower))) {
      offScale << names[i];
    }
  }
  auto const       data  = viewer()->getData();
  double const     last{(data && !data->getTimes().isEmpty()) ? data->getTimes().last() : view.upper};
  return QJsonObject{{"run", run},
                     {"duration_s", std::max(0.0, last - start)},
                     {"from_s", std::max(0.0, view.lower - start)},
                     {"to_s", std::max(0.0, view.upper - start)},
                     {"from", clock(view.lower)},
                     {"to", clock(view.upper)},
                     {"channels", QJsonArray::fromStringList(channels)},
                     {"values_from", values.lower},
                     {"values_to", values.upper},
                     {"off_scale", QJsonArray::fromStringList(offScale)}};
}

void GraphWidget::askAssistant(QString const &question) {
  if (question.trimmed().isEmpty() || (m_aiLive >= 0)) {
    return;
  }
  m_aiChecking    = false;
  m_aiAnswerShown = false;
  m_aiBlocks << QStringLiteral("**%1**").arg(question.trimmed().toHtmlEscaped());
  m_aiBlocks << QString{};
  m_aiLive   = static_cast<int>(m_aiBlocks.size()) - 1;
  m_aiStatus = tr("Assistant is thinking");
  m_aiDots   = 1;
  showAiStatus();
  m_aiDotsTimer->start();
  updateAiButtons();
  emit chartQuestionAsked(question, chartView());
}

void GraphWidget::showAiStatus() {
  if ((m_aiLive < 0) || (m_aiLive >= m_aiBlocks.size())) {
    m_aiDotsTimer->stop();
    return;
  }
  // the dots appear one by one; their room is kept so the text does not move
  QString const dots = QString(m_aiDots, QLatin1Char('.')) + QString(3 - m_aiDots, QChar(0x2007));
  m_aiBlocks[m_aiLive] = QStringLiteral("<span style=\"color:#64748b\">%1</span>").arg((m_aiStatus + dots).toHtmlEscaped());
  renderAi();
}

void GraphWidget::renderAi() {
  QString text = m_aiBlocks.join(QStringLiteral("\n\n"));
  if (m_aiChecking) { // under the answer while a better one is looked for
    text += QStringLiteral("\n\n<span style=\"color:#64748b\">%1</span>").arg(tr("Checking the values against the experiment data…").toHtmlEscaped());
  }
  m_aiTranscript->setMarkdown(text);
  m_aiTranscript->verticalScrollBar()->setValue(m_aiTranscript->verticalScrollBar()->maximum());
}

void GraphWidget::aiWelcome() {
  m_aiBlocks = {tr("**Assistant** — %1")
                  .arg(tr("Ask about recorded experiments and days, the chamber and its settings, or the digital twin. I can help "
                          "with summaries, KPIs, stages, anomalies, comparisons, charts, reports, data export, predictions and "
                          "PID suggestions."))};
  m_aiChecking = false;
  renderAi();
}

void GraphWidget::showAssistantChecking() {
  if (m_aiLive >= 0) {
    m_aiChecking = true;
    renderAi();
  }
}

void GraphWidget::showAssistantModel(QString const &state, QString const &message) {
  if (!m_aiModelDot) {
    return;
  }
  static QHash<QString, std::pair<char const *, char const *>> const looks{
    {"ready", {"#16a34a", QT_TR_NOOP("Model ready")}},
    {"loading", {"#d97706", QT_TR_NOOP("Loading model")}},
    {"failed", {"#dc2626", QT_TR_NOOP("Model failed")}},
    {"off", {"#94a3b8", QT_TR_NOOP("Model not started")}}};
  auto const look = looks.value(state, looks.value(QStringLiteral("off")));
  m_aiModelDot->setStyleSheet(QStringLiteral("color: %1; font-size: 14pt;").arg(QLatin1String(look.first)));
  m_aiModelText->setText(tr(look.second));
  for (QLabel *label : {m_aiModelDot, m_aiModelText}) {
    label->setToolTip(tr("Local model: %1").arg(message));
  }
}

void GraphWidget::updateAiButtons() {
  if (!m_aiExplainBtn) {
    return;
  }
  bool const        idle{m_aiLive < 0};
  bool const        experiment{!runReference().isEmpty()}; // one experiment shown (not a day or the live data)
  QJsonObject const view = chartView();
  bool const        experiments{experiment || view.contains(QLatin1String("compare"))}; // or a comparison
  m_aiExplainBtn->setEnabled(idle && (experiment || !view.isEmpty()));
  for (QPushButton *button : {m_aiSummaryBtn, m_aiAnomaliesBtn, m_aiReportBtn}) {
    button->setEnabled(idle && experiments);
  }
  // Send turns into Stop while the answer is written
  m_aiAskBtn->setText(idle ? tr("Send") : tr("Stop"));
  m_aiAskBtn->setObjectName(idle ? QStringLiteral("primaryButton") : QString{});
  m_aiAskBtn->style()->unpolish(m_aiAskBtn);
  m_aiAskBtn->style()->polish(m_aiAskBtn);
}

void GraphWidget::showAssistantExchange(QString const &status, QString const &answer, bool done) {
  if ((m_aiLive < 0) || (m_aiLive >= m_aiBlocks.size())) {
    return;
  }
  if (answer.isEmpty() && done) {
    // stopped: the status says so
    m_aiDotsTimer->stop();
    m_aiChecking = false;
    if (!std::exchange(m_aiAnswerShown, false)) { // an answer shown stays
      m_aiBlocks[m_aiLive] = QStringLiteral("<span style=\"color:#64748b\">%1</span>").arg(status.toHtmlEscaped());
    }
    m_aiLive = -1;
    updateAiButtons();
    renderAi();
    return;
  }
  if (answer.isEmpty()) {
    // what it is doing, its dots animated
    QString text = status;
    while (text.endsWith(QChar(0x2026)) || text.endsWith(QLatin1Char('.'))) {
      text.chop(1);
    }
    m_aiStatus = text;
    showAiStatus();
    if (!m_aiDotsTimer->isActive()) {
      m_aiDotsTimer->start();
    }
    return;
  }
  m_aiDotsTimer->stop();
  m_aiAnswerShown = !done;
  // a list or heading starts on its own line
  static QRegularExpression const block(QStringLiteral("^\\s*(?:[-*+]\\s|#|\\d+[.)]\\s|\\|)"));
  m_aiBlocks[m_aiLive] = tr("**Assistant** — %1").arg(block.match(answer).hasMatch() ? QStringLiteral("\n\n") + answer : answer);
  if (done) {
    m_aiLive     = -1;
    m_aiChecking = false;
    updateAiButtons();
  }
  renderAi();
}

QWidget *GraphWidget::buildAiPage() {
  double const ssf{utils::ssf()};
  auto        *page = new QWidget();
  auto        *lay  = new QVBoxLayout(page);
  lay->setContentsMargins(0, 0, 8 * ssf, 0);
  lay->setSpacing(8 * ssf);
  // in the panel's header: the local model's state, and a new conversation
  m_aiHeader     = new QWidget();
  auto *modelRow = new QHBoxLayout(m_aiHeader);
  modelRow->setContentsMargins(0, 0, 0, 0);
  modelRow->setSpacing(4 * ssf);
  m_aiModelDot = new QLabel(QStringLiteral("●"), m_aiHeader);
  m_aiModelText = new QLabel(m_aiHeader);
  m_aiModelText->setProperty("muted", true);
  auto *newChatBtn = new QPushButton(tr("New chat"), m_aiHeader);
  newChatBtn->setToolTip(tr("Forget this conversation and start a new one"));
  newChatBtn->setFixedHeight(26 * ssf);
  newChatBtn->setFocusPolicy(Qt::NoFocus);
  modelRow->addWidget(m_aiModelDot);
  modelRow->addWidget(m_aiModelText);
  modelRow->addSpacing(4 * ssf);
  modelRow->addWidget(newChatBtn);
  showAssistantModel(QStringLiteral("off"), tr("Model not started"));
  // what is shown explained; the whole experiment (or comparison) summarized, its anomalies and its report
  m_aiExplainBtn = new QPushButton(tr("Explain view"), page);
  m_aiExplainBtn->setObjectName("primaryButton");
  m_aiSummaryBtn = new QPushButton(tr("Summarize"), page);
  m_aiSummaryBtn->setToolTip(tr("The whole experiment: configuration, main KPIs, stages and anything abnormal"));
  m_aiAnomaliesBtn = new QPushButton(tr("Anomalies"), page);
  m_aiAnomaliesBtn->setToolTip(tr("The anomalies detected in this experiment"));
  m_aiReportBtn = new QPushButton(tr("Report"), page);
  m_aiReportBtn->setToolTip(tr("Write an HTML report of this experiment"));
  // the four in one row: as narrow as their words
  auto *quickRow = new QHBoxLayout();
  quickRow->setSpacing(6 * ssf);
  for (QPushButton *button : {m_aiExplainBtn, m_aiSummaryBtn, m_aiAnomaliesBtn, m_aiReportBtn}) {
    button->setMinimumHeight(32 * ssf);
    button->setMinimumWidth(0);
    button->setFocusPolicy(Qt::NoFocus);
    button->setStyleSheet(QStringLiteral("padding-left: %1px; padding-right: %1px;").arg(static_cast<int>(4 * ssf)));
  }
  quickRow->addWidget(m_aiExplainBtn, 1);
  quickRow->addWidget(m_aiSummaryBtn, 1);
  quickRow->addWidget(m_aiAnomaliesBtn, 1);
  quickRow->addWidget(m_aiReportBtn, 1);
  lay->addLayout(quickRow);
  // the questions and answers of this panel
  m_aiTranscript = new QTextBrowser(page);
  m_aiTranscript->setObjectName("statsList");
  m_aiTranscript->setOpenExternalLinks(false);
  m_aiTranscript->setMinimumHeight(220 * ssf);
  touch::enableScrolling(m_aiTranscript);
  lay->addWidget(m_aiTranscript, 1);
  // the question and its Send button side by side
  auto *row      = new QHBoxLayout();
  row->setSpacing(8 * ssf);
  auto *question = new LineEditScreen(page, VirtualKeyboard::EN);
  question->setPlaceholderText(tr("Ask, or tell the chart: \"zoom to the second hold\"…"));
  question->setMinimumHeight(32 * ssf);
  question->setProperty("soft", true);
  m_aiAskBtn = new QPushButton(tr("Send"), page);
  m_aiAskBtn->setMinimumHeight(32 * ssf);
  m_aiAskBtn->setMinimumWidth(80 * ssf);
  m_aiAskBtn->setFocusPolicy(Qt::NoFocus);
  row->addWidget(question, 1);
  row->addWidget(m_aiAskBtn);
  lay->addLayout(row);
  m_aiDotsTimer = new QTimer(page);
  m_aiDotsTimer->setInterval(400);
  connect(m_aiDotsTimer, &QTimer::timeout, this, [this] {
    m_aiDots = (m_aiDots % 3) + 1;
    showAiStatus();
  });
  auto ask = [this, question] {
    if (m_aiLive >= 0) {
      emit assistantStopRequested(); // the button is Stop while the answer is written
      return;
    }
    QString const text = question->text();
    question->clear();
    askAssistant(text);
  };
  connect(m_aiAskBtn, &QPushButton::clicked, this, ask);
  connect(question, &QLineEdit::returnPressed, this, [this, ask] {
    if (m_aiLive < 0) {
      ask();
    }
  });
  connect(newChatBtn, &QPushButton::clicked, this, [this] {
    emit assistantNewChatRequested();
    m_aiDotsTimer->stop();
    m_aiLive = -1;
    aiWelcome();
    updateAiButtons();
  });
  // the questions are sent to the model in English; a comparison shown gets its own
  auto comparisonShown = [this] { return chartView().contains(QLatin1String("compare")); };
  connect(m_aiExplainBtn, &QPushButton::clicked, this, [this, comparisonShown] {
    if (comparisonShown()) {
      askAssistant(tr("Explain this comparison in this view: how the experiments differ, which one followed the setpoint better, and anything unusual."));
    } else if (chartView().contains(QLatin1String("day"))) { // a day: its experiments and what happened between them
      askAssistant(tr("Explain what happened on this day: the experiments, and what the chamber did between them."));
    } else {
      askAssistant(tr("Explain what happens in this view: the setpoint and how well the temperature followed it, and anything unusual."));
    }
  });
  connect(m_aiSummaryBtn, &QPushButton::clicked, this, [this, comparisonShown] {
    askAssistant(comparisonShown() ? tr("Summarize this comparison: how the experiments differ and which one controlled better.")
                                   : tr("Summarize this experiment: configuration, main KPIs, stages and anything abnormal."));
  });
  connect(m_aiAnomaliesBtn, &QPushButton::clicked, this, [this, comparisonShown] {
    askAssistant(comparisonShown() ? tr("Which anomalies were detected in these experiments and how serious are they?")
                                   : tr("Which anomalies were detected in this experiment and how serious are they?"));
  });
  connect(m_aiReportBtn, &QPushButton::clicked, this, [this, comparisonShown] {
    askAssistant(comparisonShown() ? tr("Generate an HTML report of this comparison.") : tr("Generate an HTML report of this experiment."));
  });
  aiWelcome();
  return page;
}

QWidget *GraphWidget::buildInfoPage() {
  double const ssf{utils::ssf()};
  auto        *page = new QWidget();
  auto        *lay  = new QVBoxLayout(page);
  lay->setContentsMargins(0, 0, 8 * ssf, 0);
  lay->setSpacing(10 * ssf);
  m_infoLabel = new QLabel(page);
  m_infoLabel->setWordWrap(true);
  m_infoLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  lay->addWidget(m_infoLabel);
  m_detailsBtn = new QPushButton(tr("Edit experiment details…"), page);
  m_detailsBtn->setMinimumHeight(30 * ssf);
  m_detailsBtn->setFocusPolicy(Qt::NoFocus);
  connect(m_detailsBtn, &QPushButton::clicked, this, &GraphWidget::editExperimentDetails);
  lay->addWidget(m_detailsBtn);
  lay->addStretch();
  return page;
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
  connect(m_library, &ExperimentLibrary::runsChanged, this, [this] {
    // the panels that number experiments, once the list is read
    QString const open = m_sideBar->current();
    if ((open == QLatin1String("calendar")) || (open == QLatin1String("ai")) || (open == QLatin1String("info"))) {
      refreshSide();
    }
  });

  for (ChartPane *pane : std::as_const(m_panes)) {
    pane->setNotes(m_notes);
  }

  // statistics, annotations and variable rules of the focused pane
  m_statistics = new ChartStatisticsDialog(m_resHolder, m_notes, this);
  connect(m_statistics, &ChartStatisticsDialog::zoomRequested, this, [this](double from, double to) {
    viewer()->setTimeRange(from, to);
  });
  // the alignment changed in the comparison tab of the statistics: the comparison tab follows
  connect(m_statistics->comparisonPanel(), &ComparisonPanel::alignmentChanged, this, [this] {
    Tab const *tab = m_focused ? m_focused->currentTab() : nullptr;
    if (!tab || !tab->comparison) {
      return;
    }
    ChartComparison changed = *tab->comparison;
    changed.alignMode       = m_statistics->comparisonPanel()->alignMode();
    changed.alignN          = m_statistics->comparisonPanel()->alignN();
    if ((changed.alignMode == tab->comparison->alignMode) && (changed.alignN == tab->comparison->alignN)) {
      return;
    }
    QString const key        = tab->key;
    auto const [label, name] = comparisonTitles(changed);
    m_focused->updateTab(key, tab->label, name, std::make_shared<ChartComparison const>(changed));
    computeComparison(key, changed);
  });

  // experiments the assistant shows open here
  m_services->setShowHandler([this](ExperimentRun const &run, QStringList const &channels, double from, double to,
                                    QVector<ExperimentRun> const &compare) { return showFromServices(run, channels, from, to, compare); });
  handleAccessChange(QString{}, m_resHolder->users->getCurrentAccessLevel());
}

bool GraphWidget::needInsight() const {
  return (m_statistics && m_statistics->isVisible()) || m_eventsAct->isChecked() || m_anomaliesAct->isChecked() || m_stagesAct->isChecked();
}

void GraphWidget::requestInsights() {
  for (ChartPane *pane : std::as_const(m_panes)) {
    requestInsight(pane);
  }
}

void GraphWidget::requestInsight(ChartPane *pane) {
  if (!m_panes.contains(pane)) {
    return;
  }
  Tab const *tab = pane->currentTab();
  auto const data = pane->viewer()->getData();
  if (!tab || tab->comparison || tab->loading || !data || data->getTimes().isEmpty()) {
    applyInsight(pane);
    return;
  }
  QString const key = tab->key;
  qint64 const  samples{data->getTimes().size()};
  qint64 const  now{QDateTime::currentMSecsSinceEpoch()};
  bool const    live{key == ChartPane::LIVE_KEY};
  auto const    cached = m_insights.constFind(key);
  bool const    fresh{(cached != m_insights.cend())
                   && ((cached->samples == samples) || (live && (now - m_insightTimes.value(key) < 60'000)))};
  applyInsight(pane);
  if (fresh || !needInsight() || m_insightPending.contains(key)) {
    return;
  }
  // in the background: the analysis core and the events
  m_insightPending.insert(key);
  applyInsight(pane); // "Analysing…"
  auto                             result = std::make_shared<RunInsight>();
  ExperimentRun const              run    = runOfTab(*tab);
  QVector<double> const            times  = data->getTimes();
  QVector<QVector<double>> const   states = data->getStates();
  QStringList const                names  = m_resHolder->states->getNames();
  QVector<AnalysisNotesStore::Rule> const rules = m_notes ? m_notes->rules(tab->runKey) : QVector<AnalysisNotesStore::Rule>{};
  QVariantList const               alarms = m_resHolder->settings->settingsMap().value("alarms").toList();
  QThread *thread = QThread::create([result, run, times, states, names, rules, alarms] {
    *result = runinsight::compute(run, times, states, names, rules, alarms);
  });
  connect(thread, &QThread::finished, this, [this, thread, key, result] {
    thread->deleteLater();
    m_insightPending.remove(key);
    m_insights.insert(key, *result);
    m_insightTimes.insert(key, QDateTime::currentMSecsSinceEpoch());
    for (ChartPane *each : std::as_const(m_panes)) {
      if (each->currentTab() && (each->currentTab()->key == key)) {
        applyInsight(each);
      }
    }
  });
  thread->start(QThread::LowPriority);
}

void GraphWidget::applyInsight(ChartPane *pane) {
  Tab const       *tab     = pane->currentTab();
  RunInsight const insight = (tab && !tab->comparison) ? m_insights.value(tab->key) : RunInsight{};
  DataViewer      *chart   = pane->viewer();

  // events and anomalies as dotted lines, stages as a strip
  QVector<DataViewer::Marker> markers;
  if (m_eventsAct->isChecked()) {
    for (core::RunEvent const &event : insight.events) {
      markers.push_back({event.time, event.message, runrecord::eventColor(event.type)});
    }
  }
  if (m_anomaliesAct->isChecked() && insight.analysis) {
    for (core::Anomaly const &anomaly : insight.analysis->anomalies) {
      markers.push_back({insight.start + anomaly.start, QStringLiteral("⚠ %1: %2").arg(analysistext::anomalyName(anomaly.kind), anomaly.detail),
                         analysistext::severityColor(anomaly.severity)});
    }
  }
  chart->setMarkers(markers);
  QVector<DataViewer::Region> stages;
  if (m_stagesAct->isChecked() && insight.analysis) {
    for (core::StageAnalysis const &stage : insight.analysis->run.stages) {
      core::Segment const &s     = stage.segment;
      QString              label = analysistext::stageName(s.kind);
      if (s.interval >= 0) {
        label += QStringLiteral(" %1").arg(s.interval);
      }
      stages.push_back({insight.start + s.start, insight.start + std::max(s.end, s.start + 1.0), label, analysistext::stageColor(s.kind)});
    }
  }
  chart->setStages(stages);

  if ((pane == m_focused) && m_statistics && !(tab && tab->comparison)) {
    m_statistics->setInsight(insight, tab && m_insightPending.contains(tab->key));
  }
}

void GraphWidget::applySetpointLine() {
  for (ChartPane *pane : std::as_const(m_panes)) {
    for (DataViewer *chart : pane->viewers()) {
      chart->setSetpointLine(m_setpointAct->isChecked());
    }
  }
}

void GraphWidget::refreshComparisonStatistics() {
  Tab const *tab = m_focused ? m_focused->currentTab() : nullptr;
  if (!m_statistics || !tab || !tab->comparison) {
    return;
  }
  ComparisonData const data = m_comparisonData.value(tab->key);
  m_statistics->comparisonPanel()->setRuns(data.runs, data.signalLabel);
  m_statistics->comparisonPanel()->setAlignment(tab->comparison->alignMode, tab->comparison->alignN);
}

void GraphWidget::refreshInfo() {
  Tab const *tab = m_focused ? m_focused->currentTab() : nullptr;
  updateOptions(); // Edit experiment details: only for saved experiments
  if (!tab) {
    m_infoLabel->clear();
    return;
  }
  auto const data  = viewer()->getData();
  QVector<double> const times = data ? data->getTimes() : QVector<double>{};
  auto clock = [](double t) { return QDateTime::fromSecsSinceEpoch(static_cast<qint64>(std::llround(t))).toString("yyyy-MM-dd HH:mm:ss"); };
  auto row   = [](QString const &name, QString const &value) {
    return value.trimmed().isEmpty() ? QString{}
                                     : QStringLiteral("<tr><td style='color:#64748b; padding-right:12px'>%1</td><td>%2</td></tr>")
                                         .arg(name.toHtmlEscaped(), value.toHtmlEscaped());
  };
  QString rows;
  if (tab->comparison) {
    for (int k{0}; k < tab->comparison->members.size(); ++k) {
      ChartComparison::Member const &member = tab->comparison->members[k];
      rows += row(k == 0 ? tr("Reference") : tr("Compared"), QStringLiteral("%1 · %2").arg(member.label, member.run.title));
    }
  } else {
    ExperimentRun const run = runOfTab(*tab);
    // the number the experiment list and the assistant use (#1 is the oldest)
    if (m_library && (tab->key != ChartPane::LIVE_KEY)) {
      QVector<ExperimentRun> const all = m_library->chronological();
      for (int i{0}; i < all.size(); ++i) {
        if ((all[i].key == run.key) || (!run.storeKey.isEmpty() && (all[i].storeKey == run.storeKey))) {
          rows += row(tr("Experiment"), QStringLiteral("#%1").arg(i + 1));
          break;
        }
      }
    }
    // the details of a saved experiment, always listed ("—" when not filled in); the tab knows its database key
    // even before the experiment list is read
    QString const storeKey = !run.storeKey.isEmpty() ? run.storeKey : tab->storeKey;
    if (!storeKey.isEmpty()) {
      auto const manifest = storage::RunStore::manifest(storeKey);
      auto       detail   = [](QString const &value) { return value.trimmed().isEmpty() ? QStringLiteral("—") : value; };
      rows += row(tr("DUT"), detail(manifest ? manifest->metadata.dut : QString{}));
      rows += row(tr("DUT mass"), detail(manifest ? manifest->metadata.dutMassText() : QString{}));
      rows += row(tr("DUT density"), detail(manifest ? manifest->metadata.dutDensityText() : QString{}));
      rows += row(tr("Purpose"), detail(manifest ? manifest->metadata.purpose : QString{}));
      rows += row(tr("Operator"), detail(manifest ? manifest->metadata.operatorName : QString{}));
    }
    rows += row(tr("Chamber"), run.chamber);
  }
  if (!times.isEmpty()) {
    rows += row(tr("Start"), clock(times.first()));
    rows += row(tr("End"), clock(times.last()));
    rows += row(tr("Duration"), ExperimentLibrary::formatDuration(times.last() - times.first()));
    rows += row(tr("Samples"), QLocale().toString(times.size()));
  }

  m_infoLabel->setText(QStringLiteral("<b>%1</b><table style='margin-top:8px' cellpadding='3'>%2</table>").arg(tab->name.toHtmlEscaped(), rows));
}

void GraphWidget::fitView() {
  // first Fit: all the data of the tab (live or a day: the day); again on a day: its last experiment, and back
  DataViewer *chart = viewer();
  Tab const  *tab   = m_focused ? m_focused->currentTab() : nullptr;
  bool const wholeDay{tab && ((tab->key == ChartPane::LIVE_KEY) || tab->key.startsWith(QLatin1String("day:")))};
  if (!chart->fittedToAll() || !wholeDay || !chart->getData()) {
    chart->setFitStart(qQNaN());
    return;
  }
  // the experiment: the last stretch of samples with the chamber working (between gaps, which are NaN)
  int const status{static_cast<int>(m_resHolder->states->getNames().indexOf("status_condition"))};
  QVector<double> const          times  = chart->getData()->getTimes();
  QVector<QVector<double>> const states = chart->getData()->getStates();
  if ((status < 0) || (status >= states.size()) || times.isEmpty()) {
    return;
  }
  constexpr double WORK{2.0};
  QVector<double> const &condition = states[status];
  for (int end{static_cast<int>(times.size()) - 1}; end >= 0; --end) {
    if (condition[end] != WORK) {
      continue;
    }
    int start{end};
    while ((start > 0) && (condition[start - 1] == WORK)) {
      --start;
    }
    if ((times[end] - times[start] < 60.0) && (end < times.size() - 1)) {
      end = start; // a moment of work, not an experiment: look further back
      continue;
    }
    if (end == times.size() - 1) {
      chart->setFitStart(times[start]); // still running: from its start, following it
    } else {
      chart->setTimeRange(times[start], times[end]);
    }
    return;
  }
}

void GraphWidget::updateLiveActivity() {
  // every sample of a connected chamber is drawn (and recorded), with or without a job
  StatusManager::AppState const state = m_resHolder->status->getAppState();
  bool const                    active{StatusManager::AppState::ONLINE == state};
  QString const idle = (StatusManager::AppState::OFFLINE == state)
                         ? tr("Connected, but the chamber's configuration differs from this application's.")
                         : ((StatusManager::AppState::CONNECTING == state) || (StatusManager::AppState::CONNECTED == state))
                             ? tr("Connecting to the chamber…")
                             : tr("Not connected to the chamber.");
  if (active == m_liveActive && idle == m_liveIdleText) {
    return;
  }
  m_liveActive   = active;
  m_liveIdleText = idle;
  m_insights.remove(ChartPane::LIVE_KEY); // the stages and events of the data shown before
  m_insightTimes.remove(ChartPane::LIVE_KEY);
  for (ChartPane *pane : std::as_const(m_panes)) {
    pane->setLiveActive(m_liveActive, m_liveIdleText);
  }
  if (!m_panes.isEmpty()) {
    requestInsights();
  }
}

void GraphWidget::refreshDerivedList() {
  if (!m_derivedBox) {
    return;
  }
  m_derived = storage::DerivedVariablesStore::list();
  QLayout *lay = m_derivedBox->layout();
  while (QLayoutItem *item = lay->takeAt(0)) {
    delete item->widget();
    delete item;
  }
  if (m_derived.isEmpty()) {
    auto *none = new QLabel(tr("None yet."), m_derivedBox);
    none->setProperty("muted", true);
    lay->addWidget(none);
  }
  for (storage::DerivedVariable const &definition : std::as_const(m_derived)) {
    auto *check = new QCheckBox(definition.name, m_derivedBox);
    check->setChecked(m_derivedShown.contains(definition.name));
    check->setToolTip(definition.type == storage::DerivedVariable::Type::Custom ? definition.expression
                                                                                 : storage::DerivedVariable::typeKey(definition.type));
    connect(check, &QCheckBox::toggled, this, [this, name = definition.name](bool on) {
      m_derivedShown.removeAll(name);
      if (on) {
        m_derivedShown << name;
      }
      for (ChartPane *pane : std::as_const(m_panes)) {
        refreshDerived(pane);
      }
      scheduleSessionSave();
    });
    lay->addWidget(check);
  }
  for (ChartPane *pane : std::as_const(m_panes)) {
    refreshDerived(pane);
  }
}

void GraphWidget::refreshDerived(ChartPane *pane) {
  if (!m_panes.contains(pane)) {
    return;
  }
  Tab const  *tab   = pane->currentTab();
  DataViewer *chart = pane->mainViewer();
  auto const  data  = chart->getData();
  if (!tab || tab->comparison || m_derivedShown.isEmpty() || !data || data->getTimes().isEmpty()) {
    chart->setExtraSeries({});
    return;
  }
  // the formulas over the channels shown, by state name
  QVector<double> const          times  = data->getTimes();
  QVector<QVector<double>> const states = data->getStates();
  QStringList const              names  = m_resHolder->states->getNames();
  QHash<QString, QVector<double> const *> columns;
  for (int i{0}; (i < names.size()) && (i < states.size()); ++i) {
    columns.insert(names[i], &states[i]);
  }
  QVector<double> elapsed;
  elapsed.reserve(times.size());
  for (double const time : times) {
    elapsed.push_back(time - times.first());
  }
  QVector<DataViewer::Series> series;
  for (storage::DerivedVariable const &definition : std::as_const(m_derived)) {
    QVector<double> values;
    if (!m_derivedShown.contains(definition.name) || !derived::compute(definition, columns, elapsed, values)) {
      continue; // not shown, or needs channels this data does not have
    }
    series.push_back({definition.name, definition.color.isValid() ? definition.color : QColor("#9333ea"), times, values});
  }
  chart->setExtraSeries(series);
}

void GraphWidget::manageDerived() {
  // the channels the formulas can use
  QVector<ChannelInfo> channels;
  QStringList const    names  = m_resHolder->states->getNames();
  QVariantList const   styles = m_resHolder->settings->settingsMap()["states"].toList();
  for (int i{0}; i < names.size(); ++i) {
    ChannelInfo info;
    info.name  = names[i];
    info.label = (i < styles.size()) ? styles[i].toMap()["label"].toString() : names[i];
    info.color = (i < styles.size()) ? QColor(styles[i].toMap()["color"].toString()) : QColor();
    channels.push_back(info);
  }
  DerivedVariablesDialog dialog(channels, m_resHolder->users->getCurrentUserName(), this);
  dialog.exec();
  refreshDerivedList();
}

bool GraphWidget::writeComparisonCsv(Tab const &tab, QString const &path) const {
  auto const data = tab.ownViewer ? tab.ownViewer->getData() : nullptr;
  if (!tab.comparison || !data) {
    return false;
  }
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    return false;
  }
  auto quote = [](QString text) {
    return (text.contains(',') || text.contains('"')) ? QStringLiteral("\"%1\"").arg(text.replace('"', QStringLiteral("\"\""))) : text;
  };
  auto number = [](double value) { return std::isnan(value) ? QString{} : QString::number(value, 'g', 10); };
  auto clock  = [](double t) { return QDateTime::fromSecsSinceEpoch(static_cast<qint64>(t)).toString("yyyy-MM-dd HH:mm:ss"); };
  QTextStream out(&file);
  out.setGenerateByteOrderMark(true);
  out << QStringList{tr("Experiment"), tr("Start"), tr("End"), tr("Duration, s"), tr("Channel"), tr("Min"), tr("Max"), tr("Mean"), tr("Final")}
           .join(',')
      << '\n';
  // the columns of the comparison: experiment by experiment, channel by channel (as aligned on its time axis)
  QVector<QVector<double>> const columns = data->getStates();
  QStringList const              names   = m_resHolder->states->getNames();
  QVariantList const             styles  = m_resHolder->settings->settingsMap()["states"].toList();
  ChartComparison const         &spec    = *tab.comparison;
  for (int k{0}; k < spec.members.size(); ++k) {
    for (int c{0}; c < spec.channels.size(); ++c) {
      QVector<double> const values = columns.value(k * spec.channels.size() + c);
      double minValue{qQNaN()};
      double maxValue{qQNaN()};
      double finalValue{qQNaN()};
      double sum{0.0};
      qint64 count{0};
      for (double const value : values) {
        if (std::isnan(value)) {
          continue;
        }
        minValue   = std::isnan(minValue) ? value : std::min(minValue, value);
        maxValue   = std::isnan(maxValue) ? value : std::max(maxValue, value);
        finalValue = value;
        sum += value;
        ++count;
      }
      qsizetype const    index{names.indexOf(spec.channels[c])};
      QString const      channel = ((index >= 0) && (index < styles.size())) ? styles[index].toMap()["label"].toString() : spec.channels[c];
      ExperimentRun const &run   = spec.members[k].run;
      out << QStringList{quote(run.title.isEmpty() ? spec.members[k].label : run.title), clock(run.start), clock(run.end), number(run.duration()),
                         quote(channel), number(minValue), number(maxValue), number(count > 0 ? sum / static_cast<double>(count) : qQNaN()),
                         number(finalValue)}
               .join(',')
          << '\n';
    }
  }
  out.flush();
  return file.commit();
}

void GraphWidget::scheduleSessionSave() {
  if (!m_restoring && m_sessionTimer) {
    m_sessionTimer->start();
  }
}

QJsonObject GraphWidget::tabJson(ChartPane *pane, int index) const {
  Tab const  *tab = pane->tab(index);
  QJsonObject json{{"key", tab->key},       {"label", tab->label},     {"name", tab->name},
                   {"storeKey", tab->storeKey}, {"view", pane->viewOf(index)}};
  if (tab->comparison) {
    json.insert("comparison", comparisonToJson(*tab->comparison));
  } else if (!tab->run.key.isEmpty()) {
    json.insert("run", runToJson(tab->run));
  }
  return json;
}

void GraphWidget::saveSession() const {
  if (m_restoring || m_panes.isEmpty()) {
    return;
  }
  QJsonArray panes;
  for (ChartPane *pane : m_panes) {
    QJsonArray tabs;
    for (int i{0}; i < pane->count(); ++i) {
      tabs.push_back(tabJson(pane, i));
    }
    Tab const *current = pane->currentTab();
    panes.push_back(QJsonObject{{"tabs", tabs}, {"current", current ? current->key : QString{}}});
  }
  QJsonArray sizes;
  for (int const size : m_split->sizes()) {
    sizes.push_back(size);
  }
  QJsonObject const options{{"smoothing", smoothing()},
                            {"min", m_minCheck && m_minCheck->isChecked()},
                            {"max", m_maxCheck && m_maxCheck->isChecked()},
                            {"avg", m_avgCheck && m_avgCheck->isChecked()},
                            {"overlayChannel", m_overlayChannel ? m_overlayChannel->currentData().toString() : QString{}},
                            {"relativeTime", m_relativeRadio && m_relativeRadio->isChecked()},
                            {"scatter", m_scatterRadio && m_scatterRadio->isChecked()},
                            {"labels", m_labelsBtn->isChecked()},
                            {"tooltip", m_valuesBtn->isChecked()},
                            {"events", m_eventsAct->isChecked()},
                            {"anomalies", m_anomaliesAct->isChecked()},
                            {"stages", m_stagesAct->isChecked()},
                            {"setpoint", m_setpointAct->isChecked()},
                            {"derived", QJsonArray::fromStringList(m_derivedShown)}};
  QJsonObject const session{{"version", 1},
                            {"options", options},
                            {"panes", panes},
                            {"split", sizes},
                            {"focused", static_cast<int>(m_panes.indexOf(m_focused))}};
  QDir().mkpath(QFileInfo(SESSION_FILE).absolutePath());
  QSaveFile file(SESSION_FILE);
  if (file.open(QIODevice::WriteOnly)) {
    file.write(QJsonDocument(session).toJson(QJsonDocument::Indented));
    file.commit();
  }
}

void GraphWidget::restoreSession() {
  QFile file(SESSION_FILE);
  if (!file.open(QIODevice::ReadOnly)) {
    return; // first start
  }
  QJsonObject const session = QJsonDocument::fromJson(file.readAll()).object();
  if (session.value("version").toInt() != 1) {
    return;
  }
  m_restoring = true;

  // options first: the charts of the restored tabs get them
  QJsonObject const options = session.value("options").toObject();
  if (m_smoothCombo) {
    setSmoothing(options.value("smoothing").toInt(1));
    m_minCheck->setChecked(options.value("min").toBool());
    m_maxCheck->setChecked(options.value("max").toBool());
    m_avgCheck->setChecked(options.value("avg").toBool());
    if (int const channel{m_overlayChannel->findData(options.value("overlayChannel").toString())}; channel >= 0) {
      m_overlayChannel->setCurrentIndex(channel);
    }
    m_relativeRadio->setChecked(options.value("relativeTime").toBool());
    m_scatterRadio->setChecked(options.value("scatter").toBool());
  }
  m_labelsBtn->setChecked(options.value("labels").toBool(true));
  m_valuesBtn->setChecked(options.value("tooltip").toBool(false));
  m_eventsAct->setChecked(options.value("events").toBool(false));
  m_anomaliesAct->setChecked(options.value("anomalies").toBool(false));
  m_stagesAct->setChecked(options.value("stages").toBool(false));
  m_setpointAct->setChecked(options.value("setpoint").toBool(false));
  m_derivedShown.clear();
  for (QJsonValue const &name : options.value("derived").toArray()) {
    m_derivedShown << name.toString();
  }
  refreshDerivedList();

  // the tabs, in their panes, with their zoom (they are read again in the background)
  QJsonArray const panes = session.value("panes").toArray();
  QHash<QString, QString> renamed; // comparisons get new keys
  QVector<storage::ExperimentSpan> spans;
  bool                             spansRead{false};
  for (int p{0}; p < std::min<int>(static_cast<int>(panes.size()), 2); ++p) {
    QJsonObject const paneJson = panes[p].toObject();
    QJsonArray const  tabs     = paneJson.value("tabs").toArray();
    bool              any{false};
    for (QJsonValue const &value : tabs) {
      any = any || (value.toObject().value("key").toString() != ChartPane::LIVE_KEY);
    }
    if ((p > 0) && !any) {
      continue;
    }
    ChartPane *pane = (p == 0) ? m_panes.first() : rightPane();
    for (QJsonValue const &value : tabs) {
      QJsonObject const tab  = value.toObject();
      QString const     key  = tab.value("key").toString();
      QJsonObject       view = tab.value("view").toObject();
      if (key == ChartPane::LIVE_KEY) {
        continue; // live data opens following the newest samples, not a zoom of an earlier session
      }
      m_restoreTarget = pane;
      m_restoreView   = &view;
      if (tab.contains("comparison")) {
        renamed.insert(key, createComparison(comparisonFromJson(tab.value("comparison").toObject())));
      } else if (key.startsWith(QLatin1String("db:"))) {
        openStored(tab.value("storeKey").toString(), tab.value("label").toString(), tab.value("name").toString());
      } else if (key.startsWith(QLatin1String("day:"))) {
        if (!spansRead) {
          spans     = storage::RunStore::spans();
          spansRead = true;
        }
        QDate const                      day = QDate::fromString(key.mid(4), Qt::ISODate);
        QVector<storage::ExperimentSpan> ofDay;
        for (storage::ExperimentSpan const &span : std::as_const(spans)) {
          if (QDateTime::fromSecsSinceEpoch(static_cast<qint64>(span.start)).date() == day) {
            ofDay.push_back(span);
          }
        }
        if (!ofDay.isEmpty()) {
          showDay(day, ofDay);
        }
      } else if (tab.contains("run")) {
        showRun(runFromJson(tab.value("run").toObject()), false);
      }
      m_restoreTarget = nullptr;
      m_restoreView   = nullptr;
    }
    QString const current = paneJson.value("current").toString();
    pane->selectTab(renamed.value(current, current));
  }

  QVector<int> sizes;
  for (QJsonValue const &size : session.value("split").toArray()) {
    sizes.push_back(size.toInt());
  }
  if ((sizes.size() == m_split->count()) && (sizes.size() > 1)) {
    m_split->setSizes(QList<int>(sizes.cbegin(), sizes.cend()));
  }
  m_restoring = false;
  setFocusedPane(m_panes.value(session.value("focused").toInt(), m_panes.first()));
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
    } else {
      pane->viewer()->setControlMode(DataViewer::ControlMode::LEAD_FULL); // no range: the whole experiment
    }
    if (!channels.isEmpty()) {
      m_styleEditor->showOnly(channels);
    }
    return {};
  }
  if (!channels.isEmpty()) {
    m_styleEditor->showOnly(channels);
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

QString GraphWidget::nextComparisonLabel() const {
  QSet<int> used;
  for (ChartPane *pane : std::as_const(m_panes)) {
    for (int i{0}; i < pane->count(); ++i) {
      Tab const *tab = pane->tab(i);
      if (tab && tab->comparison) {
        QString const number = tab->label.section(QLatin1Char(' '), -1);
        used.insert(number == tab->label ? 1 : number.toInt());
      }
    }
  }
  int n{1};
  while (used.contains(n)) {
    ++n;
  }
  return (n == 1) ? tr("Comparison") : tr("Comparison %1").arg(n);
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
      if ((channel.value("notation").toString() == QLatin1String("scientific")) || pressure::isPressureChannel(comparison.channels[c])) {
        logIndexes.push_back(static_cast<int>(styles.size()));
      }
      styles.push_back(QVariantMap{{"label", (comparison.channels.size() == 1) ? comparison.members[k].label
                                                                              : QStringLiteral("%1 · %2").arg(comparison.members[k].label, label)},
                                   {"name", comparison.channels[c]},
                                   {"axis_label", label}, // the axis title: the channel, not the experiment
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
  chart->setSetpointLine(m_setpointAct->isChecked()); // its series comes with the comparison
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
      std::shared_ptr<DataHolder const> const data =
        (tab->key == ChartPane::LIVE_KEY) ? std::shared_ptr<DataHolder const>(m_resHolder->dataManager->getIntervalToDisplay()) : tab->data;
      bool const hasData{data && !data->getTimes().isEmpty()};
      if (!tab->comparison && !tab->loading && hasData) { // nothing to compare in an empty tab (e.g. no live data yet)
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
  editPane->updateTab(editKey, edited->label, name, std::make_shared<ChartComparison const>(comparison));
  computeComparison(editKey, comparison);
}

QString GraphWidget::createComparison(ChartComparison const &comparison, bool splitRight) {
  auto const [label, name] = comparisonTitles(comparison);
  Tab tab;
  tab.key        = QStringLiteral("cmp:%1").arg(++m_comparisons);
  tab.label      = nextComparisonLabel();
  tab.name       = name;
  tab.loading    = true;
  tab.comparison = std::make_shared<ChartComparison const>(comparison);
  tab.ownViewer  = makeComparisonViewer();
  styleComparison(tab.ownViewer, comparison);
  connect(tab.ownViewer->getPlot()->xAxis, qOverload<QCPRange const &>(&QCPAxis::rangeChanged), this,
          &GraphWidget::scheduleSessionSave);
  openTab(tab, splitRight);
  computeComparison(tab.key, comparison);
  return tab.key;
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
}

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
    QStringList              skipped; // experiments that could not be read
    QVector<double>          setpoint; // the first experiment's setpoint on the grid
    QVector<std::shared_ptr<core::ExperimentAnalysis const>> analyses; // of the experiments read
    QVector<ComparedRun>     runs;
    double                   origin{qQNaN()}; // chart time of the alignment point
  };
  auto     result = std::make_shared<Result>();
  QThread *thread = QThread::create([inputs, names, comparison, result]() mutable {
    auto const mode = static_cast<core::AlignMode>(comparison.alignMode);
    QVector<QVector<double>> elapsed;
    QVector<double>          offsets;
    QVector<double>          periods;
    double                   referenceAnchor{qQNaN()};
    QVector<bool>            readable(inputs.size(), true);
    int                      reference{-1}; // the first experiment that could be read
    for (int k{0}; k < inputs.size(); ++k) {
      ComparedInput &input = inputs[k];
      if (!input.haveData) {
        QString    error;
        auto const loaded = ExperimentLibrary::loadDetached(input.run, &error);
        if (!loaded || loaded->times.isEmpty()) {
          readable[k] = false;
          result->skipped << QStringLiteral("%1 (%2)").arg(input.run.title.isEmpty() ? QObject::tr("experiment %1").arg(k + 1) : input.run.title,
                                                           error.isEmpty() ? QObject::tr("no data") : error);
          elapsed.push_back({});
          offsets.push_back(0.0);
          periods.push_back(1.0);
          continue;
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
      // its analysis: the comparison tables, and the anchor it is aligned on
      ExperimentData data;
      data.run          = input.run;
      data.times        = input.times;
      data.states       = input.states;
      data.channelNames = names;
      auto const analysis = std::make_shared<core::ExperimentAnalysis const>(core::analyseExperiment(experimentInput(data, names)));
      qsizetype const       tempColumn{names.indexOf("temp")};
      qsizetype const       setpointColumn{names.indexOf("temp_ref")};
      QVector<double> const temp      = (tempColumn >= 0) ? input.states[tempColumn] : QVector<double>{};
      QVector<double> const setpoints = (setpointColumn >= 0) ? input.states[setpointColumn] : QVector<double>{};
      double                offset{0.0};
      if (mode != core::AlignMode::START) {
        double const anchor = core::alignmentAnchor(*analysis, times, temp, setpoints, mode, comparison.alignN);
        if (reference < 0) {
          referenceAnchor = anchor;
        } else if (std::isfinite(anchor) && std::isfinite(referenceAnchor)) {
          offset = referenceAnchor - anchor;
        }
      }
      ComparedRun compared;
      compared.title     = input.run.title;
      compared.run       = input.run;
      compared.analysis  = analysis.get();
      compared.times     = times; // elapsed, not aligned: the comparison tables align on their own
      compared.temp      = temp;
      compared.reference = setpoints;
      qsizetype const signalColumn{names.indexOf(comparison.channels.value(0))};
      compared.signal = (signalColumn >= 0) ? input.states[signalColumn] : QVector<double>{};
      if (!input.run.storeKey.isEmpty()) {
        if (auto const manifest = storage::RunStore::manifest(input.run.storeKey)) {
          compared.manifest = manifest->toJson();
        }
      }
      result->analyses.push_back(analysis);
      result->runs.push_back(compared);
      for (double &time : times) {
        time += offset;
      }
      if (reference < 0) {
        reference = k;
      }
      elapsed.push_back(times);
      offsets.push_back(offset);
      periods.push_back(typicalPeriod(times));
    }
    if (reference < 0) {
      result->error = QObject::tr("None of the experiments could be read:\n%1").arg(result->skipped.join(QLatin1Char('\n')));
      return;
    }

    // a common time grid over all of them (at most 200 000 points)
    double tMin{std::numeric_limits<double>::max()};
    double tMax{std::numeric_limits<double>::lowest()};
    double       finest{std::numeric_limits<double>::max()};
    for (int k{0}; k < elapsed.size(); ++k) {
      if (readable[k]) {
        tMin   = std::min(tMin, elapsed[k].first());
        tMax   = std::max(tMax, elapsed[k].last());
        finest = std::min(finest, periods[k]);
      }
    }
    double const step{std::max(finest, (tMax - tMin) / 200000.0)};
    if (!(tMax > tMin) || !(step > 0.0)) {
      result->error = QObject::tr("The experiments have no time span to compare.");
      return;
    }
    qsizetype const count{static_cast<qsizetype>(std::floor((tMax - tMin) / step)) + 1};
    double const    base{inputs[reference].times.first()}; // the reference experiment's clock
    result->origin = base + (((mode != core::AlignMode::START) && std::isfinite(referenceAnchor)) ? referenceAnchor : 0.0);
    result->times.resize(count);
    for (qsizetype i{0}; i < count; ++i) {
      result->times[i] = base + tMin + static_cast<double>(i) * step;
    }
    // each channel of each experiment, interpolated on the grid; no line across gaps
    auto interpolate = [&](int k, QVector<double> const &values) {
      QVector<double> column(count, qQNaN());
      if (!readable[k] || values.isEmpty()) {
        return column;
      }
      QVector<double> const &times = elapsed[k];
      qsizetype              j{0};
      for (qsizetype i{0}; i < count; ++i) {
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
      return column;
    };
    for (int k{0}; k < inputs.size(); ++k) {
      for (QString const &channel : comparison.channels) {
        qsizetype const index{names.indexOf(channel)};
        result->values.push_back(interpolate(k, (readable[k] && (index >= 0)) ? inputs[k].states[index] : QVector<double>{}));
      }
    }
    // the setpoint line: the first experiment's, aligned like it
    if (qsizetype const setpointColumn{names.indexOf("temp_ref")}; setpointColumn >= 0) {
      result->setpoint = interpolate(reference, inputs[reference].states[setpointColumn]);
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
    // the comparison tables and the setpoint line
    QStringList const  names  = m_resHolder->states->getNames();
    QVariantList const styles = m_resHolder->settings->settingsMap()["states"].toList();
    Tab const         *tab    = pane->tab(pane->indexOf(key));
    QString const      signal = (tab && tab->comparison) ? tab->comparison->channels.value(0) : QString{};
    qsizetype const    signalIndex{names.indexOf(signal)};
    m_comparisonData.insert(key, {result->analyses, result->runs,
                                  ((signalIndex >= 0) && (signalIndex < styles.size())) ? styles[signalIndex].toMap()["label"].toString() : signal,
                                  result->origin});
    if (tab && tab->ownViewer) {
      tab->ownViewer->setSetpointSeries(result->times, result->setpoint);
      tab->ownViewer->setSetpointLine(m_setpointAct->isChecked());
    }
    pane->setTabData(key, holder);
    if (pane == m_focused) {
      updateStatisticsSource();
    }
    if (!result->skipped.isEmpty()) {
      (new MessageDialog(tr("Comparison"), tr("Left out, they could not be read:\n%1").arg(result->skipped.join(QLatin1Char('\n')))))->open();
    }
  });
  thread->start(QThread::LowPriority);
}

void GraphWidget::updateStatisticsSource() {
  refreshSide();
  if (!m_statistics || !m_focused) {
    return;
  }
  Tab const *tab = m_focused->currentTab();
  bool const comparison{tab && tab->comparison};
  m_statistics->setSource(viewer(), (tab && !comparison) ? tab->runKey : QString{}, tab ? tab->name : QString{},
                          comparison ? QStringList{} : m_resHolder->states->getNames());
  m_statistics->setComparisonMode(comparison);
  if (comparison) {
    refreshComparisonStatistics();
  } else {
    requestInsight(m_focused);
  }
}

QWidget *GraphWidget::buildOptions() {
  double const ssf{utils::ssf()};
  auto        *page = new QWidget();
  auto        *lay  = new QVBoxLayout(page);
  lay->setContentsMargins(14 * ssf, 8 * ssf, 14 * ssf, 8 * ssf);
  lay->setSpacing(4 * ssf);

  // a chevron: down when the section is open, right when folded
  auto chevron = [ssf](bool down) {
    int const size{static_cast<int>(14 * ssf)};
    qreal const dpr{qApp->devicePixelRatio()};
    QPixmap     pixmap(QSize(size, size) * dpr);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(theme::colors().muted, 1.6 * ssf, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    double const s{static_cast<double>(size)};
    if (down) {
      p.drawPolyline(QPolygonF{{QPointF(s * 0.25, s * 0.38), QPointF(s * 0.5, s * 0.63), QPointF(s * 0.75, s * 0.38)}});
    } else {
      p.drawPolyline(QPolygonF{{QPointF(s * 0.38, s * 0.25), QPointF(s * 0.63, s * 0.5), QPointF(s * 0.38, s * 0.75)}});
    }
    return pixmap;
  };
  // a section: its bold title with a chevron (tap to fold), its content, a line under it
  bool first{true};
  auto section = [&](QString const &title, bool open) {
    if (!first) {
      auto *line = new QFrame(page);
      line->setObjectName("cardDivider");
      line->setFixedHeight(1);
      lay->addSpacing(8 * ssf);
      lay->addWidget(line);
      lay->addSpacing(4 * ssf);
    }
    first        = false;
    auto *header = new QPushButton(page);
    header->setObjectName("drawerSection");
    header->setCheckable(true);
    header->setChecked(open);
    header->setFocusPolicy(Qt::NoFocus);
    header->setMinimumHeight(30 * ssf);
    auto *headerLay = new QHBoxLayout(header);
    headerLay->setContentsMargins(0, 0, 0, 0);
    auto *name = new QLabel(title, header);
    QFont font = name->font();
    font.setBold(true);
    name->setFont(font);
    auto *arrow = new QLabel(header);
    for (QLabel *label : {name, arrow}) {
      label->setAttribute(Qt::WA_TransparentForMouseEvents);
    }
    headerLay->addWidget(name);
    headerLay->addStretch();
    headerLay->addWidget(arrow);
    auto *body    = new QWidget(page);
    auto *bodyLay = new QVBoxLayout(body);
    bodyLay->setContentsMargins(0, 0, 0, 0);
    bodyLay->setSpacing(4 * ssf);
    auto fold = [arrow, body, chevron](bool on) {
      arrow->setPixmap(chevron(on));
      body->setVisible(on);
    };
    fold(open);
    connect(header, &QPushButton::toggled, header, fold);
    lay->addWidget(header);
    lay->addWidget(body);
    return bodyLay;
  };
  // a small grey caption over a field
  auto caption = [page, ssf](QVBoxLayout *into, QString const &text, bool spaced = true) {
    if (spaced) {
      into->addSpacing(4 * ssf);
    }
    auto *label = new QLabel(text, page);
    label->setProperty("fieldLabel", true);
    QFont font = label->font();
    font.setPointSizeF(font.pointSizeF() * 0.9);
    label->setFont(font);
    into->addWidget(label);
  };
  // joined toggle buttons (one of them, or any of them)
  auto segments = [page, ssf](QVBoxLayout *into, QStringList const &labels, bool exclusive) {
    auto                 *row   = new QHBoxLayout();
    auto                 *group = exclusive ? new QButtonGroup(page) : nullptr;
    QVector<QPushButton *> buttons;
    row->setSpacing(0);
    for (int i{0}; i < labels.size(); ++i) {
      auto *button = new QPushButton(labels[i], page);
      button->setCheckable(true);
      button->setFocusPolicy(Qt::NoFocus);
      button->setMinimumHeight(30 * ssf);
      button->setProperty("softSegment", true);
      button->setProperty("segmentFirst", i == 0);
      button->setProperty("segmentLast", i == labels.size() - 1);
      // wide enough for its text in bold (checked), with the theme's padding
      button->ensurePolished();
      QFont bold = button->font();
      bold.setBold(true);
      button->setMinimumWidth(QFontMetrics(bold).horizontalAdvance(labels[i]) + static_cast<int>(30 * ssf));
      if (group) {
        group->addButton(button);
      }
      row->addWidget(button);
      buttons << button;
    }
    row->addStretch();
    into->addLayout(row);
    return buttons;
  };
  auto link = [page, ssf](QString const &text) {
    auto *button = new QPushButton(text, page);
    button->setProperty("link", true);
    button->setFocusPolicy(Qt::NoFocus);
    button->setCursor(Qt::PointingHandCursor);
    button->setMinimumHeight(30 * ssf);
    return button;
  };

  // time range of the focused chart
  QVBoxLayout *range = section(tr("Time range"), true);
  m_fromEdit         = new DateTimeField({}, page); // date from a calendar, time from the number pad
  m_toEdit           = new DateTimeField({}, page);
  caption(range, tr("From"), false);
  range->addWidget(m_fromEdit);
  caption(range, tr("To"));
  range->addWidget(m_toEdit);
  range->addSpacing(6 * ssf);
  auto *rangeButtons = new QHBoxLayout();
  rangeButtons->setSpacing(12 * ssf);
  auto *applyBtn = new QPushButton(tr("Apply"), page);
  applyBtn->setObjectName("primaryButton");
  applyBtn->setMinimumSize(70 * ssf, 30 * ssf);
  applyBtn->setFocusPolicy(Qt::NoFocus);
  auto *wholeBtn = link(tr("Reset to whole range"));
  rangeButtons->addWidget(applyBtn);
  rangeButtons->addWidget(wholeBtn);
  rangeButtons->addStretch();
  range->addLayout(rangeButtons);
  connect(applyBtn, &QPushButton::clicked, this, [this] {
    viewer()->setTimeRange(static_cast<double>(m_fromEdit->dateTime().toSecsSinceEpoch()),
                           static_cast<double>(m_toEdit->dateTime().toSecsSinceEpoch()));
  });
  connect(wholeBtn, &QPushButton::clicked, this, [this] {
    viewer()->setControlMode(DataViewer::ControlMode::LEAD_FULL);
    updateRangeFields();
  });

  // display: smoothing, lines or points, time axis
  QVBoxLayout *display = section(tr("Display"), true);
  caption(display, tr("Smoothing"), false);
  m_smoothCombo = new QComboBox(page);
  m_smoothCombo->setMinimumHeight(30 * ssf);
  m_smoothCombo->setProperty("soft", true);
  m_smoothCombo->addItem(tr("Off"), 1);
  for (int const samples : {3, 5, 10, 20, 50, 100, 200}) {
    m_smoothCombo->addItem(tr("%1 samples").arg(samples), samples);
  }
  display->addWidget(m_smoothCombo);
  caption(display, tr("Draw as"));
  QVector<QPushButton *> const drawing = segments(display, {tr("Lines"), tr("Points")}, true);
  drawing[0]->setChecked(true);
  m_scatterRadio = drawing[1];
  caption(display, tr("Time axis"));
  QVector<QPushButton *> const axis = segments(display, {tr("Wall clock"), tr("From start")}, true);
  axis[0]->setChecked(true);
  m_relativeRadio = axis[1];

  // markers on the chart: each toggle kept in step with its action both ways
  QVBoxLayout *markers = section(tr("Markers"), true);
  {
    QVector<QAction *> const actions{m_eventsAct, m_anomaliesAct, m_stagesAct, m_setpointAct};
    QStringList              labels;
    for (QAction *action : actions) {
      labels << action->text();
    }
    QVector<QPushButton *> const toggles = segments(markers, labels, false);
    for (int i{0}; i < actions.size(); ++i) {
      toggles[i]->setToolTip(actions[i]->toolTip());
      toggles[i]->setChecked(actions[i]->isChecked());
      connect(toggles[i], &QPushButton::toggled, actions[i], &QAction::setChecked);
      connect(actions[i], &QAction::toggled, toggles[i], &QPushButton::setChecked);
    }
  }

  // reference lines of one channel, chamber temperature unless chosen otherwise
  QVBoxLayout *overlays = section(tr("Reference lines"), true);
  m_overlayChannel      = new QComboBox(page);
  m_overlayChannel->setMinimumHeight(30 * ssf);
  m_overlayChannel->setProperty("soft", true);
  {
    QStringList const  names  = m_resHolder->states->getNames();
    QVariantList const styles = m_resHolder->settings->settingsMap()["states"].toList();
    for (int i{0}; i < names.size(); ++i) {
      QString const label = (i < styles.size()) ? styles[i].toMap()["label"].toString() : QString{};
      m_overlayChannel->addItem(label.isEmpty() ? names[i] : label, names[i]);
    }
    m_overlayChannel->setCurrentIndex(std::max(0, m_overlayChannel->findData(QStringLiteral("temp"))));
  }
  overlays->addWidget(m_overlayChannel);
  caption(overlays, tr("Lines at"));
  QVector<QPushButton *> const lines = segments(overlays, {tr("Min"), tr("Max"), tr("Average")}, false);
  m_minCheck                         = lines[0];
  m_maxCheck                         = lines[1];
  m_avgCheck                         = lines[2];

  // calculated channels: tick to draw them; their formulas in the dialog
  QVBoxLayout *derived = section(tr("Calculated channels"), true);
  m_derivedBox         = new QWidget(page);
  m_derivedBox->setLayout(new QVBoxLayout());
  m_derivedBox->layout()->setContentsMargins(0, 0, 0, 0);
  derived->addWidget(m_derivedBox);
  auto *manageBtn = link(tr("+ New calculated channel"));
  manageBtn->setToolTip(tr("Define, change or remove calculated channels"));
  connect(manageBtn, &QPushButton::clicked, this, &GraphWidget::manageDerived);
  derived->addWidget(manageBtn);
  refreshDerivedList();

  lay->addStretch();

  // smoothing and overlays apply to both charts when the page is split
  auto apply = [this] {
    for (ChartPane *pane : std::as_const(m_panes)) {
      for (DataViewer *chart : pane->viewers()) {
        applyViewOptions(chart);
      }
    }
    scheduleSessionSave();
  };
  connect(m_smoothCombo, &QComboBox::currentIndexChanged, this, apply);
  for (QAbstractButton *button : {m_relativeRadio, m_scatterRadio, m_minCheck, m_maxCheck, m_avgCheck}) {
    connect(button, &QAbstractButton::toggled, this, apply);
  }
  connect(m_overlayChannel, &QComboBox::currentIndexChanged, this, apply);
  return page;
}

int GraphWidget::smoothing() const {
  return m_smoothCombo ? std::max(1, m_smoothCombo->currentData().toInt()) : 1;
}

void GraphWidget::setSmoothing(int samples) {
  if (!m_smoothCombo) {
    return;
  }
  int index{m_smoothCombo->findData(std::max(1, samples))};
  if (index < 0) { // a value from an older session
    m_smoothCombo->addItem(tr("%1 samples").arg(samples), samples);
    index = m_smoothCombo->count() - 1;
  }
  m_smoothCombo->setCurrentIndex(index);
}

void GraphWidget::applyViewOptions(DataViewer *chart) const {
  if (!m_smoothCombo) {
    return;
  }
  chart->setSmoothingWindow(smoothing());
  chart->setRelativeTime(m_relativeRadio->isChecked());
  chart->setScatter(m_scatterRadio->isChecked());
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
  m_panes.first()->toolbar()->setTool(ChartToolbar::Tool::INSPECT); // one annotation, then back to Inspect
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
  tab.run      = run;
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
  // the day's recording: every sample received while the application was connected (idle times included)
  QString const dayFile = recordingfile::readPath(static_cast<double>(QDateTime(day, QTime(12, 0)).toSecsSinceEpoch()));
  openLoading(tab, false, [names, experiments, dayFile](Samples &out, QString &error) {
    QVector<double>          times;
    QVector<QVector<double>> states(names.size());
    if (!dayFile.isEmpty()) {
      QVector<double>          fileTimes;
      QVector<QVector<double>> fileStates;
      if (recordingfile::read(dayFile, fileTimes, fileStates, names) && (fileStates.size() == names.size())) {
        times  = fileTimes;
        states = fileStates;
      }
    }
    // gaps (the application closed): no line across them
    auto breakGaps = [&names](QVector<double> &t, QVector<QVector<double>> &s) {
      constexpr double GAP_S{30.0};
      QVector<double>          bt;
      QVector<QVector<double>> bs(names.size());
      for (qsizetype i{0}; i < t.size(); ++i) {
        if ((i > 0) && (t[i] - t[i - 1] > GAP_S)) {
          bt.push_back((t[i] + t[i - 1]) / 2.0);
          for (auto &column : bs) {
            column.push_back(qQNaN());
          }
        }
        bt.push_back(t[i]);
        for (int c{0}; c < names.size(); ++c) {
          bs[c].push_back((i < s[c].size()) ? s[c][i] : qQNaN());
        }
      }
      t = std::move(bt);
      s = std::move(bs);
    };
    breakGaps(times, states);
    // experiments the file does not hold (recorded on another computer, or the file is missing): added
    QVector<double> const fromFile = times; // sorted
    auto covered = [&fromFile](double from, double to) {
      auto const it = std::lower_bound(fromFile.cbegin(), fromFile.cend(), from);
      return (it != fromFile.cend()) && (*it <= to);
    };
    for (storage::ExperimentSpan const &experiment : experiments) {
      if (covered(experiment.start, experiment.end)) {
        continue;
      }
      storage::RunSamples samples;
      if (!storage::RunStore::loadSamples(experiment.key, samples) || samples.times.isEmpty()) {
        continue;
      }
      // no line drawn into or out of it
      times.push_back(samples.times.first() - 1e-3);
      times += samples.times;
      times.push_back(samples.times.last() + 1e-3);
      for (int i{0}; i < names.size(); ++i) {
        qsizetype const column = samples.columns.indexOf(names[i]);
        states[i].push_back(qQNaN());
        states[i] += (column >= 0) ? samples.values[column] : QVector<double>(samples.times.size(), qQNaN());
        states[i].push_back(qQNaN());
      }
    }
    if (times.isEmpty()) {
      error = tr("The experiments of this day could not be read.");
      return false;
    }
    // in time order (experiments added after the file's samples)
    QVector<qsizetype> order(times.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&times](qsizetype a, qsizetype b) { return times[a] < times[b]; });
    QVector<double>          sortedTimes(times.size());
    QVector<QVector<double>> sortedStates(states.size(), QVector<double>(times.size(), qQNaN()));
    for (qsizetype i{0}; i < order.size(); ++i) {
      sortedTimes[i] = times[order[i]];
      for (qsizetype c{0}; c < states.size(); ++c) {
        if (order[i] < states[c].size()) {
          sortedStates[c][i] = states[c][order[i]];
        }
      }
    }
    out = {sortedTimes, QStringList{}, sortedStates};
    return true;
  });
}

void GraphWidget::showExperiment(storage::ExperimentSpan const &experiment) {
  openStored(experiment.key, startText(experiment.start), experiment.name);
}

void GraphWidget::openStored(QString const &storeKey, QString const &label, QString const &name) {
  // samples from the experiment database
  QString const key = QStringLiteral("db:") + storeKey;
  Tab           tab{key, label, name, QStringLiteral("chart:") + key, nullptr, storeKey};
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
  m_sideBar->setPageEnabled(QStringLiteral("export"), enablePress);
  m_sideBar->setPageEnabled(QStringLiteral("channels"), enablePress);
  m_canAnnotate = enablePress && m_notes;
  // the tools pinned or not, as this user left them
  QSettings const settings(QStringLiteral("data/chart/toolbar.ini"), QSettings::IniFormat);
  QString const   user = name.isEmpty() ? m_resHolder->users->getCurrentUserName() : name;
  setToolbarsPinned(settings.value(QStringLiteral("pinned/%1").arg(user), false).toBool(), false);
  updateDayButton(); // annotate: also depends on the tab shown
}

void GraphWidget::refreshExportPanel() {
  if (m_exportForm) {
    m_exportForm->hide();
    m_exportForm->deleteLater();
    m_exportForm = nullptr;
  }
  Tab const *tab   = m_focused ? m_focused->currentTab() : nullptr;
  auto const shown = m_focused ? viewer()->getData() : nullptr;
  m_exportEmpty->setVisible(!tab || !shown || shown->getTimes().isEmpty());
  if (m_exportEmpty->isVisible()) {
    return;
  }
  QVector<double> const shownTimes = shown->getTimes();
  QDate const           day = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(shownTimes.first())).date();
  bool const comparison{static_cast<bool>(tab->comparison)};
  // a comparison is exported as shown (its own lines): no day or custom range from the recorded files
  auto *dialog = new ExportDialog(m_resHolder, day, m_services != nullptr, !tab->storeKey.isEmpty(), !comparison, comparison, m_exportHost, true);
  m_exportHost->layout()->addWidget(dialog);
  m_exportForm = dialog;
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

    if (format == QLatin1String("comparison-csv")) {
      done(writeComparisonCsv(tabCopy, filePath), filePath);
      return;
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
  // a new form for the next export, once this one is written
  connect(dialog, &QDialog::accepted, this, [this] { QTimer::singleShot(0, this, &GraphWidget::refreshExportPanel); });
  dialog->show();
}
}
