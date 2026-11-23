#include "mainwindow.h"
#include "chamberview.h"

#include "assistantcheck.h"
#include "assistantsession.h"

#include "analysisservices.h"

#include <QDir>
#include <QGraphicsOpacityEffect>
#include <QFileInfo>
#include <QPushButton>
#include <QRegularExpression>
#include <QStackedWidget>
#include <QSvgGenerator>
#include <QTabWidget>
#include <QThread>
#include <QToolButton>
#include <QTimer>
#include <QVBoxLayout>
#include <functional>
#include <memory>

#include "chartpane.h"
#include "chartsidebar.h"
#include "chartstatisticsdialog.h"
#include "chartstatspanel.h"
#include "livestatuspanel.h"
#include "programstepper.h"
#include "qcustomplot.h"

#include "constants.h"
#include "backupservice.h"
#include "collabmanager.h"
#include "dialogs/messagedialog.h"
#include "experimentrecorder.h"
#include "layoutreport.h"
#include "sidebarstatus.h"
#include "startupsplash.h"
#include "lightbutton.h"
#include "statusbar.h"
#include "systemwidget.h"
#include "theme.h"
#include "touchsupport.h"
#include "utils.h"

namespace deepvaccontrol {
MainWindow::MainWindow(ResHolder *resHolder, bool windowMode, QWidget *parent)
    : QMainWindow(parent)
    , m_resHolder{resHolder}
    , m_windowMode{windowMode}
    , m_menu{new Menu(deepvaccontrol::UI_MENU_BTN_HEIGHT)} {
  initGui();

  connect(m_resHolder->tcpGate, &GateTcp::userMsgReceived, this, &MainWindow::displayMsgFromGate);
}

void MainWindow::initGui() {
  auto *mainWidget = new QWidget(this);
  setCentralWidget(mainWidget);
  // an explicit minimum: hiding / showing the menu re-lays the window out, and the pages' combined minimum must not
  // resize a full-screen window beyond the screen
  mainWidget->setMinimumSize(1, 1);

  // sidebar menu on the left, against the window's top, left and bottom edges (with the status, the values and the
  // clock); the pages on the right, a gap from the edges
  int const gap{theme::gap()}; // 16 of the design, as between all the elements
  auto     *mainLay = new QHBoxLayout(mainWidget);
  mainLay->setContentsMargins(0, 0, 0, 0);
  mainLay->setSpacing(gap);
  auto *pagesLay = new QHBoxLayout();
  pagesLay->setContentsMargins(0, gap, gap, gap);
  m_stackedWidget = new QStackedWidget(this);
  pagesLay->addWidget(m_stackedWidget);

  // saves every finished chamber job to the experiment database (Deepvac Insight format)
  m_resHolder->recorder = new ExperimentRecorder(m_resHolder, this);
  // local-network sharing with other Deepvac apps (settings: Connections tab)
  m_resHolder->collab = new CollabManager(m_resHolder, this);
  // experiment library, notes and reports, shared by the chart and data analysis pages and the assistant
  m_resHolder->analysis = new AnalysisServices(m_resHolder, this);

  // each page built: the start's bar a step on
  StartupSplash::advance(0.30);
  m_programWidget    = new ProgramWidget(m_resHolder, this);
  StartupSplash::advance(0.42);
  m_settingsWidget   = new SettingsWidget(m_resHolder, this);
  StartupSplash::advance(0.54);
  m_graphWidget      = new GraphWidget(m_resHolder, this);
  StartupSplash::advance(0.70);
  m_dataAnalysisWidget = new DataAnalysisWidget(m_resHolder, this);
  StartupSplash::advance(0.76);
  // the assistant of the Analysis page's Assistant panel, about what the chart shows; it can show experiments there
  auto *session = new assistant::AssistantSession(m_resHolder, this);
  m_assistant   = session;
  if (m_resHolder->analysis) {
    session->setServices(m_resHolder->analysis, [this] { selectWidgetWindow(m_stackedWidget->indexOf(m_graphWidget)); });
  }
  connect(m_graphWidget, &GraphWidget::chartQuestionAsked, session, &assistant::AssistantSession::ask);
  connect(m_graphWidget, &GraphWidget::assistantStopRequested, session, &assistant::AssistantSession::stop);
  connect(m_graphWidget, &GraphWidget::assistantNewChatRequested, session, &assistant::AssistantSession::newChat);
  connect(m_graphWidget, &GraphWidget::assistantWanted, session, &assistant::AssistantSession::startModel);
  connect(session, &assistant::AssistantSession::exchange, m_graphWidget, &GraphWidget::showAssistantExchange);
  connect(session, &assistant::AssistantSession::checkingAnswer, m_graphWidget, &GraphWidget::showAssistantChecking);
  auto showModel = [this](assistant::LlmSidecar::State state, QString const &message) {
    using State = assistant::LlmSidecar::State;
    m_graphWidget->showAssistantModel(state == State::READY      ? QStringLiteral("ready")
                                      : state == State::STARTING ? QStringLiteral("loading")
                                      : state == State::FAILED   ? QStringLiteral("failed")
                                                                 : QStringLiteral("off"),
                                      message);
  };
  connect(session, &assistant::AssistantSession::modelStateChanged, m_graphWidget, showModel);
  showModel(session->modelState(), session->modelMessage());
  auto *systemWidget = new SystemWidget(m_resHolder, this);
  StartupSplash::advance(0.84);
  // a finger dragging their lists, tables and pages scrolls them
  for (QWidget *page : {static_cast<QWidget *>(m_settingsWidget), static_cast<QWidget *>(systemWidget)}) {
    touch::enableScrollingWithin(page);
  }

  // accessSelectWidget            = new AccessSelectWidget2(&accessManager, this);

  auto addWidgetToStacked = [this](QWidget *widget, QString const &label, QString const &icon) {
    m_stackedWidget->addWidget(widget);
    m_menu->addItem(label, icon);
  };

  // Analysis first (its Live tab holds what Home showed), the page the application starts on
  addWidgetToStacked(m_graphWidget, tr("Analysis"), "chart");
  addWidgetToStacked(m_programWidget, tr("Control"), "program");
  addWidgetToStacked(m_dataAnalysisWidget, tr("Data\nanalysis"), "analysis");
  // users are a tab of Settings
  addWidgetToStacked(m_settingsWidget, tr("Settings"), "settings");
  addWidgetToStacked(systemWidget, tr("System"), "system");
  // the data analysis page is merged into the Analysis page: no longer offered (its code is kept)
  m_menu->setItemHidden(m_stackedWidget->indexOf(m_dataAnalysisWidget));

  selectWidgetWindow(0);
  connect(m_menu, &Menu::itemClicked, this, [this](int indexSelected, int) {
    m_stackedWidget->setCurrentIndex(indexSelected);
  });

  mainLay->addWidget(m_menu);
  mainLay->addLayout(pagesLay, 1);

  connect(m_resHolder->hard_status,
          &HardwareStatus::statusChanged,
          this,
          [this](HardwareStatus::HardwareStatusEnum newStatus, HardwareStatus::HardwareStatusEnum oldStatus) {
            // the Program page stays (its job was just started or stopped there); another goes to Analysis
            if (m_stackedWidget->currentWidget() != m_programWidget) {
              selectWidgetWindow(m_stackedWidget->indexOf(m_graphWidget));
            }
          });

  connect(m_resHolder->users, &AccessManager::userSelected, this, &MainWindow::handleAccessChange);
  handleAccessChange(QString(), m_resHolder->users->getCurrentAccessLevel());

  // connect(statusBar, &StatusBar::mouseReleased, this, &MainWindow::displayLogViewer);

  auto *lightBtn = new LightButton(m_resHolder, this);
  m_menu->addWidget(lightBtn);
  // only for a chamber with a light: fitted (the PLC's report, a Service Engineer's choice or the settings) and an
  // output to switch it
  ChamberView *diagram     = systemWidget->chamberView();
  auto         updateLight = [lightBtn, diagram] {
    lightBtn->setVisible(lightBtn->isConfigured() && diagram && diagram->isFitted(QStringLiteral("light")));
  };
  updateLight();
  connect(diagram, &ChamberView::refreshed, lightBtn, updateLight);
  // at the bottom, under the navigation: the connection and chamber status, the temperature and the pressure (the
  // former top bar's), then the clock and the product name (a logo is to come)
  m_menu->addWidget(new SidebarStatus(m_resHolder, this));
  m_menu->addWidget(new SidebarFooter(this));

  connect(systemWidget, &SystemWidget::switchAppMaximizedClicked, this, [this] {
    if (isFullScreen()) {
      showMaximized();
    } else {
      showFullScreen();
    }
  });

  // System > OS > Exit: closes the application, turning the chamber off first when asked to
  connect(systemWidget, &SystemWidget::prepareToCloseApp, this, [this]() {
    emit aboutClosed();
    close();
  });

  // connect(&usbDevManager, &UsbDeviceManager::changed, statusBar, &StatusBar::setUsb);

  m_flasher = new Flasher(this);

  // database backups (data/backups) every N days (Settings > Application), checked shortly after start and then hourly
  auto runBackups = [this] {
    int const days{std::max(1, m_resHolder->settings->settingsMap().value("app_settings").toMap().value("backup_days", 1).toInt())};
    QThread  *thread = QThread::create([days] {
      storage::BackupService::backupAll(false, nullptr, days);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start(QThread::LowestPriority);
  };
  QTimer::singleShot(10'000, this, runBackups);
  auto *backupTimer = new QTimer(this);
  connect(backupTimer, &QTimer::timeout, this, runBackups);
  backupTimer->start(3'600'000);

  // titling
  auto const tcp_server_address = m_resHolder->settings->settingsMap()["tcp_server"].toMap()["server_ip"].toString();
  QString const title = tcp_server_address.isEmpty() ? QStringLiteral("Deepvac Control")
                                                     : QStringLiteral("Deepvac Control (%1)").arg(tcp_server_address);
  setWindowTitle(title);
}

void MainWindow::handleAccessChange(QString const &, AccessManager::AccessLevel newLevel) {
  // one flag per menu page, in menu order: Analysis, Program control, Data analysis (hidden), Settings, System.
  // Settings is for every level: below the Service Engineer it shows its Users tab only.
  switch (newLevel) {
    case AccessManager::AccessLevel::USER_START_1:
    case AccessManager::AccessLevel::USER_EDIT_2:
    case AccessManager::AccessLevel::USER_ADMIN_3:
      m_menu->setElementsEnableAll();
      break;
    case AccessManager::AccessLevel::SPECTATOR_0:
    default:
      m_menu->setElementsEnable({1, 0, 1, 1, 1});
      break;
  }
}

void MainWindow::setConnectionErrorFlash(bool enable) {
  m_flasher->setFlashing(enable);
}

void MainWindow::selectWidgetWindow(int index) {
  m_stackedWidget->setCurrentIndex(index);
  m_menu->selectItem(index);
}

void MainWindow::displayMsgFromGate(QString const &msg) {
  auto *msgDlg = new MessageDialog(tr("Warning"), msg);
  msgDlg->open();
}

// void MainWindow::resizeEvent(QResizeEvent *event) {
//   QMainWindow::resizeEvent(event);

//   setWindowTitle(QString("%1x%2").arg(event->size().width()).arg(event->size().height()));
// }

void MainWindow::closeEvent(QCloseEvent *event) {
  emit aboutClosed();

  // Proceed with the regular close event
  event->accept();
}

namespace {
// the window as it is drawn now, as PNG and SVG. In the SVG the charts are drawn again as vectors over their pixel
// buffers (vectorCharts: not while a panel floats over a chart, which it would cover)
void saveScreen(QWidget *window, QString const &path, bool vectorCharts) {
  window->grab().save(path + QStringLiteral(".png"));
  QSvgGenerator svg;
  svg.setFileName(path + QStringLiteral(".svg"));
  svg.setSize(window->size());
  svg.setViewBox(QRect(QPoint(0, 0), window->size()));
  svg.setResolution(window->logicalDpiX());
  svg.setTitle(QFileInfo(path).fileName());
  QCPPainter painter(&svg);
  window->render(&painter);
  if (vectorCharts) {
    for (QCustomPlot *plot : window->findChildren<QCustomPlot *>()) {
      if (!plot->isVisibleTo(window) || (plot->width() <= 0) || (plot->height() <= 0)) {
        continue;
      }
      painter.save();
      painter.translate(plot->mapTo(window, QPoint(0, 0)));
      painter.setClipRect(QRect(QPoint(0, 0), plot->size()));
      plot->toPainter(&painter, plot->width(), plot->height());
      painter.restore();
    }
  }
}
}

void MainWindow::saveLiveShot(QString const &path, double scale) {
  selectWidgetWindow(m_stackedWidget->indexOf(m_graphWidget));
  if (auto *pane = m_graphWidget->findChild<ChartPane *>()) {
    pane->selectTab(ChartPane::LIVE_KEY);
  }
  if (auto *side = m_graphWidget->findChild<ChartSideBar *>()) {
    side->close();
  }
  auto *liveToggle = m_graphWidget->findChild<QToolButton *>(QStringLiteral("liveToggle"));
  auto *liveStatus = m_graphWidget->findChild<LiveStatusPanel *>();
  if (liveToggle && liveStatus && !liveStatus->isVisibleTo(m_graphWidget)) {
    liveToggle->click(); // the status beside the chart
  }
  QTimer::singleShot(3000, this, [this, path, scale] {
    QWidget *window = this->window();
    // the charts' pixel buffers at this resolution for the time of the capture (drawn in their place: the widgets over
    // a chart, the corner button, stay over it)
    QList<QPair<QCustomPlot *, double>> plots;
    for (QCustomPlot *plot : window->findChildren<QCustomPlot *>()) {
      if (plot->isVisibleTo(window)) {
        plots.append({plot, plot->bufferDevicePixelRatio()});
        plot->setBufferDevicePixelRatio(scale);
        plot->replot(QCustomPlot::rpImmediateRefresh);
      }
    }
    // a widget under an opacity effect is left out of render() at this scale: the effect off meanwhile where it is opaque
    QList<QGraphicsEffect *> effects;
    for (QWidget *widget : window->findChildren<QWidget *>()) {
      auto *opacity = qobject_cast<QGraphicsOpacityEffect *>(widget->graphicsEffect());
      if (opacity && opacity->isEnabled() && (opacity->opacity() >= 0.999)) {
        opacity->setEnabled(false);
        effects.append(opacity);
      }
    }
    QImage image(window->size() * scale, QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(scale);
    image.fill(window->palette().color(QPalette::Window));
    {
      QPainter painter(&image);
      painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
      window->render(&painter);
    }
    for (QGraphicsEffect *effect : std::as_const(effects)) {
      effect->setEnabled(true);
    }
    for (auto const &[plot, ratio] : std::as_const(plots)) {
      plot->setBufferDevicePixelRatio(ratio);
      plot->replot(QCustomPlot::rpQueuedReplot);
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    if (!image.save(path)) {
      qWarning() << "demo-shot: not saved" << path;
    }
    emit screensExported();
  });
}

void MainWindow::runAssistantCheck(QString const &path) {
  auto *check = new assistant::AssistantCheck(m_assistant, m_resHolder->analysis, path, this);
  connect(check, &assistant::AssistantCheck::finished, this, &MainWindow::screensExported);
  check->start();
}

void MainWindow::exportScreens(QString const &dir) {
  QDir().mkpath(dir);
  struct Step {
    QString               name;
    std::function<void()> prepare;
    bool                  vectorCharts{true};
    int                   waitMs{900}; // laid out, drawn (and read, for data computed in the background)
    std::function<QWidget *()> target{}; // what is saved: this window unless a dialog is given
  };
  QVector<Step> steps;
  auto page = [this](QWidget *widget) { selectWidgetWindow(m_stackedWidget->indexOf(widget)); };

  // ---- Analysis: the Live tab collapsed (status beside the chart) and expanded, each panel
  auto *pane       = m_graphWidget->findChild<ChartPane *>();
  auto *side       = m_graphWidget->findChild<ChartSideBar *>();
  auto *liveToggle = m_graphWidget->findChild<QToolButton *>(QStringLiteral("liveToggle"));
  auto *liveStatus = m_graphWidget->findChild<LiveStatusPanel *>();
  auto  live       = [this, page, pane, side, liveToggle, liveStatus](bool expanded) {
    page(m_graphWidget);
    if (pane) {
      pane->selectTab(ChartPane::LIVE_KEY);
    }
    if (side) {
      side->close();
    }
    if (liveToggle && liveStatus && (liveStatus->isVisibleTo(m_graphWidget) == expanded)) {
      liveToggle->click(); // shown while collapsed
    }
  };
  steps.push_back({QStringLiteral("analysis-live"), [live] { live(false); }});
  steps.push_back({QStringLiteral("analysis-live-expanded"), [live] { live(true); }});
  for (auto const &[key, name] : {std::pair{"channels", "channels"}, std::pair{"options", "options"}, std::pair{"statistics", "statistics"},
                                  std::pair{"info", "info"}, std::pair{"ai", "assistant"}, std::pair{"calendar", "calendar"},
                                  std::pair{"export", "export"}}) {
    steps.push_back({QStringLiteral("analysis-panel-%1").arg(QLatin1String(name)),
                     [live, side, key] {
                       live(false);
                       if (side) {
                         side->open(QString::fromLatin1(key));
                       }
                     },
                     false});
  }

  // the two latest experiments side by side, Split right (read in the background: a longer wait)
  steps.push_back({QStringLiteral("analysis-split"),
                   [this, live] {
                     live(false);
                     if (!m_graphWidget->splitLatest()) {
                       qWarning() << "export: fewer than two experiments of 10 min or longer to show side by side";
                     }
                   },
                   true, 5000});

  // the full report (Statistics) of the experiment shown then: each of its tabs
  auto report = [this]() -> QWidget * { return m_graphWidget->findChild<ChartStatisticsDialog *>(); };
  for (auto const &[index, name] : {std::pair{0, "channels"}, std::pair{1, "kpis"}, std::pair{2, "anomalies"}, std::pair{3, "events"}}) {
    steps.push_back({QStringLiteral("analysis-statistics-%1").arg(QLatin1String(name)),
                     [this, report, index] {
                       if (auto *panel = m_graphWidget->findChild<ChartStatsPanel *>()) {
                         QMetaObject::invokeMethod(panel, "fullReportRequested"); // its Full report button
                       }
                       if (QWidget *dialog = report()) {
                         if (auto *tabs = dialog->findChild<QTabWidget *>()) {
                           tabs->setCurrentIndex(index);
                         }
                       }
                     },
                     false, (index == 0) ? 4000 : 900, report});
  }

  // ---- Control: the program's three steps
  auto *stepper = m_programWidget->findChild<ProgramStepper *>();
  auto  control = [this, page, side, stepper, report](int step) {
    if (QWidget *dialog = report()) {
      dialog->close();
    }
    m_graphWidget->closeSplitLatest(); // not left in the user's session
    if (side) {
      side->close();
    }
    page(m_programWidget);
    m_programWidget->showProgram();
    if (stepper) {
      stepper->setCurrent(step);
    }
  };
  steps.push_back({QStringLiteral("control-0-manual"), [this, page, side, report] {
                     if (QWidget *dialog = report()) {
                       dialog->close();
                     }
                     m_graphWidget->closeSplitLatest();
                     if (side) {
                       side->close();
                     }
                     page(m_programWidget);
                     m_programWidget->showManual();
                   }});
  steps.push_back({QStringLiteral("control-1-experiment-details"), [control] { control(0); }});
  steps.push_back({QStringLiteral("control-2-intervals"), [control] { control(1); }});
  // a program of four intervals (a ramp, a hold, a ramp at a rate down, a hold), the third one open. Only in this
  // process: the export quits when done
  steps.push_back({QStringLiteral("control-2-intervals-four"),
                   [this, control] {
                     ProgramModel *model = m_resHolder->defaultProgram;
                     if (model) {
                       QStringList const  order  = model->getParamListOrder();
                       QVariantList const params = model->getParamListVariant();
                       int const          tempBit{static_cast<int>(order.indexOf(QStringLiteral("temp")))};
                       auto flags = [tempBit](bool ramp, bool await, bool custom) {
                         quint32 value{0U};
                         utils::setBitOfDword(value, tempBit, true);
                         utils::setBitOfDword(value, RAMP_BIT, ramp);
                         utils::setBitOfDword(value, AWAIT_BIT, await);
                         utils::setBitOfDword(value, CUSTOM_RATE_BIT, custom);
                         return value;
                       };
                       struct Interval {
                         double  temp;
                         int     duration;
                         quint32 flags;
                       };
                       QVariantList intervals;
                       for (Interval const &interval : {Interval{60.0, 3600, flags(true, false, false)}, Interval{60.0, 1800, flags(false, true, false)},
                                                        Interval{-20.0, 4800, flags(true, false, true)}, Interval{-20.0, 1800, flags(false, true, false)}}) {
                         QVariantMap values;
                         for (int i{0}; i < order.size(); ++i) {
                           values[order[i]] = params.value(i).toMap().value(QStringLiteral("value"));
                         }
                         values[QStringLiteral("temp")]     = interval.temp;
                         values[QStringLiteral("duration")] = interval.duration;
                         values[QStringLiteral("flags")]    = interval.flags;
                         intervals << values;
                       }
                       model->setProgramVariant({{QStringLiteral("intervals"), intervals},
                                                 {QStringLiteral("cycle"), QVariantMap{{"start", 0}, {"end", 0}, {"count", 0}}}});
                       model->selectInterval(2);
                     }
                     control(1);
                   },
                   true, 1500});
  steps.push_back({QStringLiteral("control-3-confirm-and-start"), [control] { control(2); }});
  // a setpoint: one interval, full speed to 40 °C and held until stopped
  steps.push_back({QStringLiteral("control-2-interval-until-stopped"),
                   [this, control] {
                     ProgramModel *model = m_resHolder->defaultProgram;
                     if (model) {
                       QStringList const  order  = model->getParamListOrder();
                       QVariantList const params = model->getParamListVariant();
                       QVariantMap        values;
                       for (int i{0}; i < order.size(); ++i) {
                         values[order[i]] = params.value(i).toMap().value(QStringLiteral("value"));
                       }
                       quint32 flags{0U};
                       utils::setBitOfDword(flags, static_cast<int>(order.indexOf(QStringLiteral("temp"))), true);
                       utils::setBitOfDword(flags, AWAIT_BIT, true);
                       values[QStringLiteral("temp")]     = 40.0;
                       values[QStringLiteral("duration")] = HOLD_UNTIL_STOPPED;
                       values[QStringLiteral("flags")]    = flags;
                       model->setProgramVariant({{QStringLiteral("intervals"), QVariantList{values}},
                                                 {QStringLiteral("cycle"), QVariantMap{{"start", 0}, {"end", 0}, {"count", 0}}}});
                       model->selectInterval(0);
                     }
                     control(1);
                   },
                   true, 1500});

  // ---- Settings and System: each tab shown to this user
  auto tabs = [&steps, page](QWidget *widget, QString const &prefix) {
    auto *tabWidget = widget ? widget->findChild<QTabWidget *>(QString(), Qt::FindDirectChildrenOnly) : nullptr;
    for (int i{0}; tabWidget && (i < tabWidget->count()); ++i) {
      if (!tabWidget->isTabVisible(i)) {
        continue;
      }
      QString const name = tabWidget->tabText(i).toLower().replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
      steps.push_back({QStringLiteral("%1-%2-%3").arg(prefix).arg(i + 1, 2, 10, QLatin1Char('0')).arg(name), [page, widget, tabWidget, i] {
                         page(widget);
                         tabWidget->setCurrentIndex(i);
                       }});
    }
  };
  tabs(m_settingsWidget, QStringLiteral("settings"));
  tabs(findChild<SystemWidget *>(), QStringLiteral("system"));

  // one after the other: prepared, laid out and drawn, then saved
  auto next = std::make_shared<std::function<void(int)>>();
  *next     = [this, steps, dir, next](int index) {
    if (index >= steps.size()) {
      selectWidgetWindow(m_stackedWidget->indexOf(m_graphWidget));
      emit screensExported();
      return;
    }
    steps[index].prepare();
    QTimer::singleShot(steps[index].waitMs, this, [this, steps, dir, next, index] {
      QWidget *target = steps[index].target ? steps[index].target() : this;
      if (target && target->isVisible()) {
        QString const path = QDir(dir).filePath(QStringLiteral("%1-%2").arg(index + 1, 2, 10, QLatin1Char('0')).arg(steps[index].name));
        saveScreen(target, path, steps[index].vectorCharts);
        if (qEnvironmentVariableIntValue("DEEPVAC_EXPORT_LAYOUT") != 0) { // what does not fit, and the font sizes
          layoutreport::write(target, path + QStringLiteral(".layout.txt"));
        }
      }
      (*next)(index + 1);
    });
  };
  (*next)(0);
}

}
