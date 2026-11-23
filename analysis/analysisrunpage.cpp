#include "analysisrunpage.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QPainter>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QResizeEvent>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QHeaderView>
#include <QSpinBox>
#include <QScrollArea>
#include <QSplitter>
#include <QGridLayout>
#include <QMessageBox>
#include <QRegularExpression>
#include <QActionGroup>
#include <functional>
#include <QScrollBar>
#include <QTableWidget>
#include <QTabBar>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <cmath>

#include "analysisnotesstore.h"
#include "annotationdialog.h"
#include "comparisonpanel.h"
#include "experimentinput.h"
#include "experimentreport.h"
#include "theme.h"
#include "rundetailsdialog.h"
#include "runrecord.h"
#include "seriesstats.h"
#include "derivedexpression.h"
#include "derivedvariablesdialog.h"
#include "experimentrecorder.h"
#include "constants.h"
#include "flowlayout.h"
#include "dialogs/messagedialog.h"
#include "lineeditnum.h"
#include "lineeditscreen.h"
#include "runstore.h"
#include "resholder.h"
#include "utils.h"

namespace deepvaccontrol {

namespace {
QVector<QColor> const COMPARE_COLORS{QColor("#2563eb"),
                                     QColor("#dc2626"),
                                     QColor("#16a34a"),
                                     QColor("#9333ea"),
                                     QColor("#d97706"),
                                     QColor("#0d9488"),
                                     QColor("#db2777"),
                                     QColor("#475569")};

QColor eventColor(int type) {
  switch (type) {
    case 1: // PLC
      return QColor("#f2a60c");
    case 2: // ERR
      return QColor("#d4122e");
    default: // APP
      return QColor("#5c6472");
  }
}

constexpr int MAX_COMPARE_MENU_RUNS{40};

int buttonHeight() {
  return static_cast<int>(28 * utils::ssf());
}

// Holds a wrapping (flow) layout and keeps its minimum height equal to the wrapped rows, so splitters and box
// layouts that ignore height-for-width still leave room for every row.
class FlowBox : public QWidget {
public:
  using QWidget::QWidget;

protected:
  void resizeEvent(QResizeEvent *event) override {
    QWidget::resizeEvent(event);
    updateMinimum();
  }
  bool event(QEvent *event) override {
    bool const handled{QWidget::event(event)};
    if (event->type() == QEvent::LayoutRequest) { // a group was shown or hidden
      updateMinimum();
    }
    return handled;
  }

private:
  void updateMinimum() {
    if (layout() && layout()->hasHeightForWidth()) {
      int const needed{layout()->heightForWidth(width())};
      if (needed != minimumHeight()) {
        setMinimumHeight(needed);
        // a splitter only applies a new minimum on its next resize
        for (QWidget *w = parentWidget(); w; w = w->parentWidget()) {
          if (auto *splitter = qobject_cast<QSplitter *>(w)) {
            splitter->refresh();
            break;
          }
        }
      }
    }
  }
};

// Calls back with the new size whenever the watched widget is resized.
class ViewportSizer : public QObject {
public:
  ViewportSizer(QWidget *watched, std::function<void(QSize const &)> onResize)
      : QObject{watched}, m_onResize{std::move(onResize)} {
    watched->installEventFilter(this);
  }

protected:
  bool eventFilter(QObject *watched, QEvent *event) override {
    if (event->type() == QEvent::Resize) {
      m_onResize(static_cast<QWidget *>(watched)->size());
    }
    return QObject::eventFilter(watched, event);
  }

private:
  std::function<void(QSize const &)> m_onResize;
};

QPushButton *makeButton(QString const &text, QWidget *parent) {
  auto *button = new QPushButton(text, parent);
  button->setMinimumHeight(buttonHeight());
  button->setFocusPolicy(Qt::NoFocus);
  return button;
}

QComboBox *makeColorCombo(QWidget *parent) {
  return annotationdialog::makeColorCombo(parent, buttonHeight());
}

bool parseNumber(QString text, double &value) {
  text = text.trimmed().replace(',', '.');
  if (text.isEmpty()) {
    value = qQNaN();
    return true;
  }
  bool ok{false};
  value = text.toDouble(&ok);
  return ok;
}

QString csvQuote(QString const &text) {
  if (text.contains(',') || text.contains('"') || text.contains('\n')) {
    return QStringLiteral("\"%1\"").arg(QString(text).replace('"', "\"\""));
  }
  return text;
}

QString csvNumber(double value) {
  return std::isnan(value) ? QString{} : QString::number(value, 'g', 10);
}

QString limitText(double value) {
  return std::isnan(value) ? QString{} : QString::number(value, 'g', 6);
}

// a short line in the given style, for the style combo
QIcon lineIcon(QColor const &color, Qt::PenStyle style, int width) {
  double const ssf{utils::ssf()};
  QPixmap      pix(static_cast<int>(80 * ssf), static_cast<int>(14 * ssf));
  pix.fill(Qt::transparent);
  QPainter painter(&pix);
  painter.setRenderHint(QPainter::Antialiasing);
  QPen pen(color);
  pen.setWidthF(width * ssf);
  pen.setStyle(style);
  painter.setPen(pen);
  painter.drawLine(QPointF(2 * ssf, 7 * ssf), QPointF(78 * ssf, 7 * ssf));
  return QIcon(pix);
}
}

AnalysisRunPage::AnalysisRunPage(ResHolder          *resHolder,
                                 ExperimentLibrary  *library,
                                 AnalysisNotesStore *notes,
                                 ExperimentRun       run,
                                 QWidget            *parent)
    : QWidget{parent}, m_resHolder{resHolder}, m_library{library}, m_notes{notes}, m_run{std::move(run)} {
  buildUi();

  connect(m_notes, &AnalysisNotesStore::annotationsChanged, this, [this] {
    refreshRegions();
    refreshAnnotationsList();
  });
  connect(m_notes, &AnalysisNotesStore::rulesChanged, this, [this](QString const &runKey) {
    if (runKey.isEmpty() || (runKey == m_run.key)) { // empty: changed by a sync
      refreshBands();
      refreshRulesList();
      if (m_data) {
        computeRunAnalysis(); // limit violations
        refreshEvents();
      }
    }
  });

  handleAccessChange({}, m_resHolder->users->getCurrentAccessLevel());
  connect(m_resHolder->users, &AccessManager::userSelected, this, &AnalysisRunPage::handleAccessChange);

  // let the tab appear before the (possibly large) files are read
  QTimer::singleShot(0, this, &AnalysisRunPage::load);
}

void AnalysisRunPage::updateToolIcons() {
  theme::Colors const &c = theme::colors();
  int const            size{static_cast<int>(16 * utils::ssf())};
  for (auto const &[button, kind] : std::as_const(m_iconButtons)) {
    button->setIcon(theme::menuIcon(kind, c.text, c.text, size));
  }
}

void AnalysisRunPage::changeEvent(QEvent *event) {
  QWidget::changeEvent(event);
  if (event->type() == QEvent::PaletteChange) {
    updateToolIcons();
  }
}

QString AnalysisRunPage::tabTitle() const {
  QString text = m_run.title;
  // "... (test)", "... (alarm)": the part in brackets at the end
  static QRegularExpression const trailing(QStringLiteral("\\s*\\([^()]*\\)\\s*$"));
  while (trailing.match(text).hasMatch()) {
    text.remove(trailing);
  }
  // imported logger runs: "yyyy-MM-dd HH:mm <folder> run_..." -> without the folder
  static QRegularExpression const imported(QStringLiteral("^(\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}) \\S+ (run_\\S+)$"));
  if (QRegularExpressionMatch const match = imported.match(text); match.hasMatch()) {
    text = match.captured(1) + QLatin1Char(' ') + match.captured(2);
  }
  return text.trimmed().isEmpty() ? m_run.title : text.trimmed();
}

void AnalysisRunPage::buildUi() {
  setObjectName("pageCard");
  setAttribute(Qt::WA_StyledBackground);
  auto *root = new QVBoxLayout(this);
  root->setContentsMargins(theme::dp(24), theme::dp(16), theme::dp(24), theme::dp(16));
  root->setSpacing(theme::dp(12));

  // title on the left, the main tools on the right, everything else in the ⋮ menu
  m_titleLabel = new QLabel(tr("Loading…"), this);
  {
    QFont font = m_titleLabel->font();
    font.setBold(true);
    font.setPointSizeF(font.pointSizeF() * 1.1);
    m_titleLabel->setFont(font);
  }
  // long texts must not force the pane wider when tabs are split
  m_titleLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  m_statsLabel = new QLabel(this); // only shows a load error
  m_statsLabel->setVisible(false);
  m_statsLabel->setWordWrap(true);
  m_integrityBadge = new QLabel(this); // integrity: Statistics -> Experiment record
  m_integrityBadge->setVisible(false);

  auto toolButton = [this](QString const &text, QString const &icon) {
    auto *button = new QPushButton(text, this);
    button->setFixedHeight(theme::dp(56));
    button->setIconSize(QSize(theme::dp(30), theme::dp(30)));
    button->setFocusPolicy(Qt::NoFocus);
    m_iconButtons.push_back({button, icon});
    return button;
  };

  m_compareBtn = toolButton(tr("Compare"), "compare");
  m_compareBtn->setToolTip(tr("Overlay the first selected channel of other experiments"));
  connect(m_compareBtn, &QPushButton::clicked, this, &AnalysisRunPage::openCompareMenu);

  m_channelsBtn = toolButton(tr("Channels"), "channels");
  m_channelsBtn->setToolTip(tr("Channels shown and their colours"));
  connect(m_channelsBtn, &QPushButton::clicked, this, &AnalysisRunPage::openChannelsMenu);

  m_annotateBtn = toolButton(tr("Annotate"), "annotate");
  m_annotateBtn->setCheckable(true);
  m_annotateBtn->setToolTip(tr("Drag across the chart to mark and label a time range"));
  connect(m_annotateBtn, &QPushButton::toggled, this, [this](bool checked) {
    m_chart->setAnnotateMode(checked);
    m_annotateBtn->setText(checked ? tr("Done") : tr("Annotate"));
  });

  auto *resetBtn = toolButton(tr("Reset view"), "fit");
  resetBtn->setToolTip(tr("Fit the whole experiment.\nDrag to pan, wheel to zoom,\nright-drag or Ctrl+drag to zoom into a box."));
  connect(resetBtn, &QPushButton::clicked, this, [this] {
    m_chart->resetView();
    updateTimeControls();
  });

  auto *statisticsBtn = toolButton(tr("Statistics"), "stats");
  statisticsBtn->setToolTip(tr("Statistics of the visible range, experiment KPIs, stages, anomalies, comparison, events and record"));

  m_moreBtn = toolButton(QString(), "dots");
  m_moreBtn->setFixedWidth(theme::dp(63));
  m_moreBtn->setToolTip(tr("More: line style, labels, view options, rules & notes, export"));

  // line / scatter (kept as a hidden combo: the ⋮ menu drives it)
  m_modeCombo = new QComboBox(this);
  m_modeCombo->addItem(tr("Line"));
  m_modeCombo->addItem(tr("Scatter"));
  m_modeCombo->setVisible(false);
  connect(m_modeCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
    m_chart->setMode(index == 1 ? AnalysisChart::Mode::SCATTER : AnalysisChart::Mode::LINE);
  });

  // export (in the ⋮ menu; the hidden button keeps the menu)
  m_exportBtn      = new QPushButton(tr("Export"), this);
  m_exportBtn->setVisible(false);
  auto *exportMenu = new QMenu(tr("Export"), this);
  QMenu *imageMenu = exportMenu->addMenu(tr("Chart image"));
  imageMenu->addAction(tr("PNG…"), this, [this] { exportChartImage("png"); });
  imageMenu->addAction(tr("BMP…"), this, [this] { exportChartImage("bmp"); });
  imageMenu->addAction(tr("JPEG…"), this, [this] { exportChartImage("jpg"); });
  QMenu *dataMenu = exportMenu->addMenu(tr("Data"));
  dataMenu->addAction(tr("Whole experiment (CSV)…"), this, [this] { exportRunData(false, false); });
  dataMenu->addAction(tr("Whole experiment (TXT)…"), this, [this] { exportRunData(false, true); });
  dataMenu->addSeparator();
  dataMenu->addAction(tr("Visible range (CSV)…"), this, [this] { exportRunData(true, false); });
  dataMenu->addAction(tr("Visible range (TXT)…"), this, [this] { exportRunData(true, true); });
  exportMenu->addAction(tr("Comparison CSV…"), this, &AnalysisRunPage::exportComparisonCsv);
  exportMenu->addSeparator();
  exportMenu->addAction(tr("Experiment report (HTML)…"), this, [this] { exportReport(false); });
  exportMenu->addAction(tr("Experiment report (PDF)…"), this, [this] { exportReport(true); });
  exportMenu->addSeparator();
  exportMenu->addAction(tr("Experiment bundle (JSON)…"), this, &AnalysisRunPage::exportBundle)->setEnabled(!m_run.storeKey.isEmpty());
  exportMenu->addSeparator();
  exportMenu->addAction(tr("Edit experiment details…"), this, &AnalysisRunPage::editRunDetails)->setEnabled(!m_run.storeKey.isEmpty());
  exportMenu->addAction(tr("Save to experiment database…"), this, &AnalysisRunPage::saveToDatabase)
    ->setEnabled(m_run.storeKey.isEmpty());
  m_exportBtn->setMenu(exportMenu);

  // ⋮ menu
  m_moreMenu       = new QMenu(this);
  auto *styleGroup = new QActionGroup(m_moreMenu);
  QAction *lineAct = m_moreMenu->addAction(tr("Line"));
  QAction *dotsAct = m_moreMenu->addAction(tr("Scatter"));
  for (QAction *action : {lineAct, dotsAct}) {
    action->setCheckable(true);
    styleGroup->addAction(action);
  }
  lineAct->setChecked(true);
  connect(lineAct, &QAction::triggered, this, [this] { m_modeCombo->setCurrentIndex(0); });
  connect(dotsAct, &QAction::triggered, this, [this] { m_modeCombo->setCurrentIndex(1); });
  m_moreMenu->addSeparator();
  QAction *labelsAct = m_moreMenu->addAction(tr("Show labels"));
  labelsAct->setCheckable(true);
  labelsAct->setChecked(true);
  QAction *optionsAct = m_moreMenu->addAction(tr("View options (range, smoothing, overlays)"));
  optionsAct->setCheckable(true);
  m_moreMenu->addSeparator();
  QAction *annotationsAct = m_moreMenu->addAction(tr("Annotations…"));
  QAction *notesAct       = m_moreMenu->addAction(tr("Variable rules…"));
  m_moreMenu->addSeparator();
  m_moreMenu->addMenu(exportMenu);
  connect(m_moreBtn, &QPushButton::clicked, this, [this] {
    m_moreMenu->exec(m_moreBtn->mapToGlobal(QPoint(m_moreBtn->width() - m_moreMenu->sizeHint().width(), m_moreBtn->height())));
  });

  auto *headerLay = new QHBoxLayout();
  headerLay->setSpacing(theme::dp(12));
  headerLay->addWidget(m_titleLabel, 1);
  for (QPushButton *button : {m_compareBtn, m_channelsBtn, m_annotateBtn, resetBtn, statisticsBtn, m_moreBtn}) {
    headerLay->addWidget(button);
  }
  root->addLayout(headerLay);
  root->addWidget(m_statsLabel);
  updateToolIcons();

  // chart
  m_chart = new AnalysisChart(this);
  m_chart->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  // the panels below share the height with the chart (splitter), so it may get smaller than elsewhere
  m_chart->setMinimumHeight(theme::dp(225));
  connect(m_chart, &AnalysisChart::annotationDrawn, this, &AnalysisRunPage::onAnnotationDrawn);

  // plot controls, grouped so a group never breaks across rows
  auto makeGroup = [this](std::initializer_list<QWidget *> widgets) {
    auto *group = new QWidget(this);
    auto *lay   = new QHBoxLayout(group);
    lay->setContentsMargins(0, 0, theme::dp(16), 0);
    lay->setSpacing(theme::dp(8));
    for (QWidget *widget : widgets) {
      lay->addWidget(widget);
    }
    return group;
  };

  auto makeTimeSpin = [this]() {
    auto *spin = new QDoubleSpinBox(this);
    spin->setDecimals(1);
    spin->setSuffix(tr(" min"));
    spin->setRange(0, 0);
    spin->setMinimumHeight(buttonHeight());
    spin->setMinimumWidth(theme::dp(187));
    return spin;
  };
  m_fromSpin = makeTimeSpin();
  m_toSpin   = makeTimeSpin();

  auto *applyRangeBtn = makeButton(tr("Apply"), this);
  applyRangeBtn->setToolTip(tr("Show only the selected time range"));
  connect(applyRangeBtn, &QPushButton::clicked, this, [this] {
    m_chart->setTimeRange(m_fromSpin->value() * 60.0, m_toSpin->value() * 60.0);
  });
  auto *fullRangeBtn = makeButton(tr("Full range"), this);
  connect(fullRangeBtn, &QPushButton::clicked, this, [this] {
    m_chart->resetView();
    updateTimeControls();
  });

  m_smoothSpin = new QSpinBox(this);
  m_smoothSpin->setRange(1, 601);
  m_smoothSpin->setSingleStep(2);
  m_smoothSpin->setSuffix(tr(" pt"));
  m_smoothSpin->setMinimumHeight(buttonHeight());
  m_smoothSpin->setToolTip(tr("Moving average window"));
  connect(m_smoothSpin, &QSpinBox::valueChanged, this, [this](int value) {
    m_chart->setSmoothingWindow(value);
  });

  m_minCheck    = new QCheckBox(tr("Min"), this);
  m_maxCheck    = new QCheckBox(tr("Max"), this);
  m_avgCheck    = new QCheckBox(tr("Average"), this);
  m_eventsCheck = new QCheckBox(tr("Events"), this);
  m_eventsCheck->setToolTip(tr("Show the application / PLC event log as vertical markers"));
  m_eventsCheck->setChecked(true);
  for (QCheckBox *check : {m_minCheck, m_maxCheck, m_avgCheck}) {
    connect(check, &QCheckBox::toggled, this, [this] {
      m_chart->setOverlays(m_minCheck->isChecked(), m_maxCheck->isChecked(), m_avgCheck->isChecked());
    });
  }
  connect(m_eventsCheck, &QCheckBox::toggled, this, &AnalysisRunPage::refreshEvents);

  auto *controls = new FlowLayout(nullptr, 4 * utils::ssf());
  controls->addWidget(
    makeGroup({new QLabel(tr("From"), this), m_fromSpin, new QLabel(tr("to"), this), m_toSpin, applyRangeBtn, fullRangeBtn}));
  controls->addWidget(makeGroup({new QLabel(tr("Smooth"), this), m_smoothSpin}));
  controls->addWidget(makeGroup({new QLabel(tr("Overlays:"), this), m_minCheck, m_maxCheck, m_avgCheck}));
  m_stagesCheck = new QCheckBox(tr("Stages"), this);
  m_stagesCheck->setToolTip(tr("Show the automatic ramp / setpoint / hold stages"));
  connect(m_stagesCheck, &QCheckBox::toggled, this, &AnalysisRunPage::refreshStages);
  m_anomaliesCheck = new QCheckBox(tr("Anomalies"), this);
  m_anomaliesCheck->setToolTip(tr("Mark the detected anomalies on the chart"));
  connect(m_anomaliesCheck, &QCheckBox::toggled, this, &AnalysisRunPage::refreshEvents);
  controls->addWidget(makeGroup({m_eventsCheck, m_stagesCheck, m_anomaliesCheck}));

  // compare mode: reference line, by default the run's own setpoint channel
  m_setpointCheck = new QCheckBox(tr("Setpoint"), this);
  m_setpointCheck->setChecked(true);
  m_setpointEdit = new LineEditNum(-1'000'000, 1'000'000, 2, false, this);
  m_setpointEdit->setFont(font()); // same size as the range fields
  m_setpointEdit->setFixedWidth(theme::dp(206));
  m_setpointEdit->setMinimumHeight(buttonHeight());
  m_setpointGroup = makeGroup({m_setpointCheck, m_setpointEdit});
  m_setpointGroup->setVisible(false);
  controls->addWidget(m_setpointGroup);
  connect(m_setpointCheck, &QCheckBox::toggled, this, &AnalysisRunPage::refreshSetpoint);
  connect(m_setpointEdit, &LineEditNum::userEditingFinished, this, &AnalysisRunPage::refreshSetpoint);

  // the chart fills the visible height; view options, rules / notes and statistics open below it
  // and the page scrolls to them, so the chart never gets squeezed
  auto *page = new QScrollArea(this);
  page->setWidgetResizable(true);
  page->setFrameShape(QFrame::NoFrame);
  page->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; }"));
  auto *pageContent = new QWidget(page);
  pageContent->setAutoFillBackground(false);
  page->viewport()->setAutoFillBackground(false);
  page->setWidget(pageContent);
  auto *pageLay = new QVBoxLayout(pageContent);
  pageLay->setContentsMargins(0, 0, 0, 0);
  pageLay->setSpacing(theme::dp(12));
  m_chart->setParent(pageContent);
  pageLay->addWidget(m_chart);
  auto *controlsBox = new FlowBox(pageContent);
  controlsBox->setLayout(controls);
  controlsBox->setVisible(false);
  pageLay->addWidget(controlsBox);
  pageLay->addStretch();
  root->addWidget(page, 1);

  // the chart fills the visible height
  auto *sizer = new ViewportSizer(page->viewport(), [this](QSize const &size) {
    m_chart->setFixedHeight(qMax(theme::dp(375), size.height()));
  });
  (void)sizer;

  // statistics and rules & notes open in their own windows over the page; they stay open while
  // the chart is zoomed or panned (the statistics follow the visible range)
  auto makeDialog = [this](QWidget *panel, QString const &title) {
    auto *dialog = new QDialog(this);
    dialog->setWindowTitle(title);
    auto *lay = new QVBoxLayout(dialog);
    lay->setContentsMargins(theme::dp(28), theme::dp(24), theme::dp(28), theme::dp(24));
    panel->setParent(dialog);
    lay->addWidget(panel);
    return dialog;
  };
  m_notesPanel       = buildNotesPanel();
  m_statisticsPanel  = buildStatisticsPanel();
  m_notesDialog       = makeDialog(m_notesPanel, tr("Variable rules · %1").arg(tabTitle()));
  m_annotationsDialog = makeDialog(buildAnnotationsPanel(), tr("Annotations · %1").arg(tabTitle()));
  refreshAnnotationsList();
  m_statisticsDialog = makeDialog(m_statisticsPanel, m_run.title.isEmpty() ? tr("Statistics") : tr("Statistics · %1").arg(tabTitle()));

  // scroll down to the view options when they open; back to the chart when they close
  connect(labelsAct, &QAction::toggled, this, [this](bool visible) { m_chart->setLegendVisible(visible); });
  connect(optionsAct, &QAction::toggled, this, [controlsBox, page](bool visible) {
    controlsBox->setVisible(visible);
    QTimer::singleShot(0, page, [page, controlsBox, visible] {
      page->verticalScrollBar()->setValue(visible ? controlsBox->mapTo(page->widget(), QPoint(0, 0)).y() : 0);
    });
  });
  // open over the main window, most of its size
  auto placeDialog = [this](QDialog *dialog) {
    QRect const area = window()->geometry();
    dialog->resize(static_cast<int>(area.width() * 0.85), static_cast<int>(area.height() * 0.8));
    dialog->move(area.center() - QPoint(dialog->width() / 2, dialog->height() / 2));
  };
  connect(annotationsAct, &QAction::triggered, this, [this, placeDialog] {
    if (!m_annotationsDialog->isVisible()) {
      placeDialog(m_annotationsDialog);
    }
    m_annotationsDialog->show();
    m_annotationsDialog->raise();
    m_annotationsDialog->activateWindow();
  });
  connect(notesAct, &QAction::triggered, this, [this, placeDialog] {
    if (!m_notesDialog->isVisible()) {
      placeDialog(m_notesDialog);
    }
    m_notesDialog->show();
    m_notesDialog->raise();
    m_notesDialog->activateWindow();
  });
  connect(statisticsBtn, &QPushButton::clicked, this, [this, placeDialog] {
    if (!m_statisticsDialog->isVisible()) {
      placeDialog(m_statisticsDialog);
    }
    m_statisticsDialog->show();
    m_statisticsDialog->raise();
    m_statisticsDialog->activateWindow();
    refreshRangeStatistics();
  });
  // follow zoom / pan, without recomputing on every intermediate step
  m_statisticsTimer = new QTimer(this);
  m_statisticsTimer->setSingleShot(true);
  m_statisticsTimer->setInterval(250);
  connect(m_statisticsTimer, &QTimer::timeout, this, &AnalysisRunPage::refreshRangeStatistics);
  connect(m_chart, &AnalysisChart::viewRangeChanged, m_statisticsTimer, qOverload<>(&QTimer::start));
  connect(m_comparisonPanel, &ComparisonPanel::alignmentChanged, this, [this] {
    refreshChart();
    m_chart->resetView();
    updateTimeControls();
  });
}

QWidget *AnalysisRunPage::buildNotesPanel() {
  auto *panel = new QTabWidget(this);
  theme::setFlatTabs(panel);
  panel->setDocumentMode(true);
  panel->tabBar()->setExpanding(false);

  // variable rules
  auto *rulesPage = new QWidget(panel);
  auto *rulesLay  = new QVBoxLayout(rulesPage);
  rulesLay->setContentsMargins(0, theme::dp(8), 0, 0);

  m_ruleChannelCombo = new QComboBox(rulesPage);
  m_ruleChannelCombo->setMinimumHeight(buttonHeight());
  m_ruleChannelCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  m_ruleChannelCombo->setMinimumContentsLength(16);
  m_ruleLoEdit    = new QLineEdit(rulesPage);
  m_ruleHiEdit    = new QLineEdit(rulesPage);
  m_ruleLabelEdit = new QLineEdit(rulesPage);
  m_ruleLoEdit->setPlaceholderText(tr("min"));
  m_ruleHiEdit->setPlaceholderText(tr("max"));
  m_ruleLabelEdit->setPlaceholderText(tr("label"));
  for (QLineEdit *edit : {m_ruleLoEdit, m_ruleHiEdit, m_ruleLabelEdit}) {
    edit->setFixedWidth(theme::dp(168));
    edit->setMinimumHeight(buttonHeight());
  }
  m_ruleColorCombo = makeColorCombo(rulesPage);
  auto *addRuleBtn = makeButton(tr("Add rule"), rulesPage);
  connect(addRuleBtn, &QPushButton::clicked, this, &AnalysisRunPage::addRule);

  auto *form = new FlowLayout(nullptr, 4 * utils::ssf());
  for (QWidget *widget : std::initializer_list<QWidget *>{
         m_ruleChannelCombo, m_ruleLoEdit, m_ruleHiEdit, m_ruleLabelEdit, m_ruleColorCombo, addRuleBtn}) {
    form->addWidget(widget);
  }
  rulesLay->addLayout(form);

  auto *rulesListLay = new QHBoxLayout();
  m_rulesList        = new QListWidget(rulesPage);
  m_rulesList->setMinimumHeight(theme::dp(187));
  auto *removeRuleBtn = makeButton(tr("Remove"), rulesPage);
  connect(removeRuleBtn, &QPushButton::clicked, this, [this] {
    if (auto *item = m_rulesList->currentItem()) {
      m_notes->removeRule(m_run.key, item->data(Qt::UserRole).toString());
    }
  });
  rulesListLay->addWidget(m_rulesList, 1);
  rulesListLay->addWidget(removeRuleBtn, 0, Qt::AlignTop);
  rulesLay->addLayout(rulesListLay, 1); // the list takes the free height

  panel->addTab(rulesPage, tr("Variable rules"));
  return panel;
}

QWidget *AnalysisRunPage::buildAnnotationsPanel() {
  double const ssf{utils::ssf()};
  auto        *panel = new QWidget(this);
  auto        *lay   = new QVBoxLayout(panel);
  lay->setContentsMargins(0, 0, 0, 0);
  lay->setSpacing(theme::dp(16));

  auto *hint = new QLabel(tr("Annotations of this experiment's time span. To add one, press Annotate and drag across the chart."), panel);
  hint->setProperty("muted", true);
  hint->setWordWrap(true);
  lay->addWidget(hint);

  m_annotationsTable = new QTableWidget(0, 6, panel);
  m_annotationsTable->setObjectName("flatTable");
  m_annotationsTable->setHorizontalHeaderLabels(
    {QString(), tr("Label").toUpper(), tr("From").toUpper(), tr("To").toUpper(), tr("Duration").toUpper(), tr("By").toUpper()});
  m_annotationsTable->verticalHeader()->setVisible(false);
  m_annotationsTable->verticalHeader()->setDefaultSectionSize(static_cast<int>(30 * ssf));
  m_annotationsTable->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  m_annotationsTable->horizontalHeader()->setHighlightSections(false);
  m_annotationsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
  m_annotationsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  m_annotationsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_annotationsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_annotationsTable->setSelectionMode(QAbstractItemView::SingleSelection);
  m_annotationsTable->setShowGrid(false);
  m_annotationsTable->setWordWrap(false);
  lay->addWidget(m_annotationsTable, 1);

  auto *buttons = new QHBoxLayout();
  buttons->setSpacing(theme::dp(16));
  auto *showBtn   = makeButton(tr("Show on chart"), panel);
  auto *editBtn   = makeButton(tr("Edit…"), panel);
  auto *removeBtn = makeButton(tr("Remove"), panel);
  removeBtn->setProperty("outline", true);
  auto *closeBtn = makeButton(tr("Close"), panel);
  closeBtn->setObjectName("primaryButton");
  for (QPushButton *button : {showBtn, editBtn, removeBtn, closeBtn}) {
    button->setMinimumWidth(theme::dp(206));
  }
  buttons->addWidget(showBtn);
  buttons->addWidget(editBtn);
  buttons->addWidget(removeBtn);
  buttons->addStretch();
  buttons->addWidget(closeBtn);
  lay->addLayout(buttons);

  auto selectedRow = [this] {
    QList<QTableWidgetItem *> const items = m_annotationsTable->selectedItems();
    return items.isEmpty() ? -1 : items.first()->row();
  };
  auto updateButtons = [this, showBtn, editBtn, removeBtn, selectedRow] {
    bool const any{selectedRow() >= 0};
    showBtn->setEnabled(any);
    editBtn->setEnabled(any);
    removeBtn->setEnabled(any);
  };
  connect(m_annotationsTable, &QTableWidget::itemSelectionChanged, this, updateButtons);
  connect(m_notes, &AnalysisNotesStore::annotationsChanged, this, updateButtons);
  updateButtons();

  connect(showBtn, &QPushButton::clicked, this, [this, selectedRow] {
    int const row{selectedRow()};
    if (row < 0) {
      return;
    }
    double const x0{m_annotationsTable->item(row, 2)->data(Qt::UserRole).toDouble() - m_run.start};
    double const x1{m_annotationsTable->item(row, 3)->data(Qt::UserRole).toDouble() - m_run.start};
    double const margin{std::max(30.0, 0.15 * (x1 - x0))};
    m_chart->setTimeRange(std::max(0.0, x0 - margin), x1 + margin);
    updateTimeControls();
  });
  connect(editBtn, &QPushButton::clicked, this, [this, selectedRow] { editAnnotation(selectedRow()); });
  connect(m_annotationsTable, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
    editAnnotation(row);
  });
  connect(removeBtn, &QPushButton::clicked, this, [this, selectedRow] {
    int const row{selectedRow()};
    if (row < 0) {
      return;
    }
    QString const label = m_annotationsTable->item(row, 1)->text();
    if (QMessageBox::question(this, tr("Remove annotation"), tr("Remove the annotation \"%1\"?").arg(label)) == QMessageBox::Yes) {
      m_notes->removeAnnotation(m_annotationsTable->item(row, 0)->data(Qt::UserRole).toString());
    }
  });
  connect(closeBtn, &QPushButton::clicked, panel, [panel] { panel->window()->close(); });
  return panel;
}

void AnalysisRunPage::editAnnotation(int row) {
  if ((row < 0) || (row >= m_annotationsTable->rowCount())) {
    return;
  }
  QString const id = m_annotationsTable->item(row, 0)->data(Qt::UserRole).toString();
  AnalysisNotesStore::Annotation current;
  bool                           found{false};
  for (auto const &annotation : m_notes->annotations(m_run.start, m_run.end, m_run.key)) {
    if (annotation.id == id) {
      current = annotation;
      found   = true;
      break;
    }
  }
  if (!found) {
    return;
  }

  QDialog dialog(this);
  dialog.setWindowTitle(tr("Edit annotation"));
  auto *lay  = new QVBoxLayout(&dialog);
  auto *grid = new QGridLayout();
  grid->setHorizontalSpacing(theme::dp(20));
  grid->setVerticalSpacing(theme::dp(16));
  auto *labelEdit = new QLineEdit(current.label, &dialog);
  labelEdit->setMinimumHeight(buttonHeight());
  auto *colorCombo = makeColorCombo(&dialog);
  for (int i{0}; i < colorCombo->count(); ++i) {
    if (QColor(colorCombo->itemData(i).toString()) == current.color) {
      colorCombo->setCurrentIndex(i);
    }
  }
  auto timeSpin = [&dialog](double minutes) {
    auto *spin = new QDoubleSpinBox(&dialog);
    spin->setDecimals(2);
    spin->setSuffix(QObject::tr(" min"));
    spin->setRange(-1e6, 1e6);
    spin->setValue(minutes);
    spin->setMinimumHeight(buttonHeight());
    return spin;
  };
  auto *fromSpin = timeSpin((current.start - m_run.start) / 60.0);
  auto *toSpin   = timeSpin((current.end - m_run.start) / 60.0);
  grid->addWidget(new QLabel(tr("Label"), &dialog), 0, 0);
  grid->addWidget(labelEdit, 0, 1, 1, 3);
  grid->addWidget(new QLabel(tr("Colour"), &dialog), 1, 0);
  grid->addWidget(colorCombo, 1, 1, 1, 3);
  grid->addWidget(new QLabel(tr("From"), &dialog), 2, 0);
  grid->addWidget(fromSpin, 2, 1);
  grid->addWidget(new QLabel(tr("to"), &dialog), 2, 2);
  grid->addWidget(toSpin, 2, 3);
  lay->addLayout(grid);
  auto *hint = new QLabel(tr("Times from the start of this experiment."), &dialog);
  hint->setProperty("muted", true);
  lay->addWidget(hint);
  auto *buttons   = new QHBoxLayout();
  auto *cancelBtn = makeButton(tr("Cancel"), &dialog);
  auto *saveBtn   = makeButton(tr("Save"), &dialog);
  saveBtn->setObjectName("primaryButton");
  saveBtn->setDefault(true);
  buttons->addStretch();
  buttons->addWidget(cancelBtn);
  buttons->addWidget(saveBtn);
  lay->addLayout(buttons);
  connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);
  connect(saveBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
  dialog.resize(theme::dp(862), dialog.sizeHint().height());
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }

  AnalysisNotesStore::Annotation changed = current;
  changed.label = labelEdit->text().trimmed().isEmpty() ? current.label : labelEdit->text().trimmed();
  changed.color = QColor(colorCombo->currentData().toString());
  double const from{m_run.start + std::min(fromSpin->value(), toSpin->value()) * 60.0};
  double const to{m_run.start + std::max(fromSpin->value(), toSpin->value()) * 60.0};
  changed.start = from;
  changed.end   = to;
  if (m_resHolder->users) {
    changed.user = m_resHolder->users->getCurrentUserName();
  }
  m_notes->updateAnnotation(id, changed);
}

bool AnalysisRunPage::ensureLoaded() {
  if (!m_data) {
    load();
  }
  return static_cast<bool>(m_data);
}

QString AnalysisRunPage::applyView(QStringList const &channels, double from, double to, QVector<ExperimentRun> const &compare) {
  if (!ensureLoaded()) {
    return tr("Could not read %1").arg(m_run.title);
  }
  // overlays first: compare mode shows the first selected channel of every run
  for (QString const &key : QStringList(m_compareKeys)) {
    if (std::none_of(compare.cbegin(), compare.cend(), [&](ExperimentRun const &run) { return run.key == key; })) {
      if (auto run = m_library->findRun(key)) {
        setCompareRun(*run, false);
      }
    }
  }
  QVector<int> selection;
  for (QString const &name : channels) {
    if (int const index{channelIndex(name)}; index >= 0) {
      selection.push_back(index);
    }
  }
  if (!selection.isEmpty()) {
    setSelectedChannels(selection);
  }
  for (ExperimentRun const &run : compare) {
    if (run.key != m_run.key) {
      setCompareRun(run, true);
    }
  }
  if (std::isfinite(from) || std::isfinite(to)) {
    double const lower{std::isfinite(from) ? from : 0.0};
    double const upper{std::isfinite(to) ? to : m_run.duration()};
    double const margin{std::max(30.0, 0.05 * (upper - lower))};
    m_chart->setTimeRange(std::max(0.0, lower - margin), upper + margin);
  } else {
    m_chart->resetView();
  }
  updateTimeControls();
  return {};
}

void AnalysisRunPage::load() {
  if (m_data) {
    return; // already loaded (ensureLoaded)
  }
  QString error;
  m_data = m_library->load(m_run, &error);
  if (!m_data) {
    m_titleLabel->setText(m_run.title);
    m_statsLabel->setText(error);
    m_statsLabel->setStyleSheet("color: #d4122e;");
    m_statsLabel->setVisible(true);
    return;
  }
  m_run      = m_data->run;
  m_baseData = m_data;
  m_elapsed  = elapsed(*m_data);
  applyDerived();
  loadRecord();

  // channel selection: saved one, else the ones visible on the main graph
  QStringList const saved = m_notes->channels(m_run.key);
  for (int i{0}; i < m_channels.size(); ++i) {
    if (saved.isEmpty() ? m_channels[i].defaultVisible : saved.contains(m_channels[i].name)) {
      m_selected.push_back(i);
    }
  }
  if (m_selected.isEmpty() && !m_channels.isEmpty()) {
    m_selected.push_back(0);
  }

  m_ruleChannelCombo->clear();
  for (ChannelInfo const &channel : std::as_const(m_channels)) {
    m_ruleChannelCombo->addItem(channel.label, channel.name);
  }

  refreshStats();
  updateChannelsButton();
  computeRunAnalysis();
  refreshChart();
  refreshRulesList();
  refreshAnnotationsList();
  updateTimeControls();
}

QVector<double> AnalysisRunPage::elapsed(ExperimentData const &data) const {
  QVector<double> result;
  result.reserve(data.times.size());
  double const start{data.times.isEmpty() ? 0.0 : data.times.first()};
  for (double const time : data.times) {
    result.push_back(time - start);
  }
  return result;
}

bool AnalysisRunPage::compareMode() const {
  return !m_compareKeys.isEmpty();
}

int AnalysisRunPage::compareChannel() const {
  return m_selected.isEmpty() ? 0 : m_selected.first();
}

int AnalysisRunPage::channelIndexIn(ExperimentData const &data, int channel) const {
  // same channel in another run: by name for database runs, by position for interval files
  if (data.channelNames.isEmpty() && m_data->channelNames.isEmpty()) {
    return channel < data.states.size() ? channel : -1;
  }
  QString const name = m_channels.value(channel).name;
  if (!data.channelNames.isEmpty()) {
    return static_cast<int>(data.channelNames.indexOf(name));
  }
  QVector<ChannelInfo> const infos = m_library->channels(data);
  for (int i{0}; i < infos.size(); ++i) {
    if (infos[i].name == name) {
      return i;
    }
  }
  return -1;
}

QVector<double> AnalysisRunPage::channelSeries(ExperimentData const &data, int channel) const {
  int const index{channelIndexIn(data, channel)};
  if ((index >= 0) && (index < data.states.size())) {
    return data.states[index];
  }
  int const derivedIndex{channel - m_baseChannelCount};
  if ((derivedIndex < 0) || (derivedIndex >= m_derived.size())) {
    return {};
  }
  // derived channel: compute it from that experiment's own channels
  QVector<ChannelInfo> const              infos = m_library->channels(data);
  QHash<QString, QVector<double> const *> columns;
  for (int i{0}; i < infos.size(); ++i) {
    columns.insert(infos[i].name, &data.states[i]);
  }
  QVector<double> values;
  return derived::compute(m_derived[derivedIndex], columns, elapsed(data), values) ? values : QVector<double>{};
}

void AnalysisRunPage::applyDerived() {
  QStringList selectedNames;
  for (int const index : std::as_const(m_selected)) {
    selectedNames.push_back(m_channels.value(index).name);
  }

  QVector<ChannelInfo> channels = m_library->channels(*m_baseData);
  m_baseChannelCount            = static_cast<int>(channels.size());

  QHash<QString, QVector<double> const *> columns;
  for (int i{0}; i < channels.size(); ++i) {
    columns.insert(channels[i].name, &m_baseData->states[i]);
  }

  auto data = std::make_shared<ExperimentData>(*m_baseData);
  m_derived.clear();
  for (storage::DerivedVariable const &definition : storage::DerivedVariablesStore::list()) {
    QVector<double> values;
    if (columns.contains(definition.name) || !derived::compute(definition, columns, m_elapsed, values)) {
      continue; // needs channels this experiment does not have
    }
    data->states.push_back(values);
    if (!data->channelNames.isEmpty()) {
      data->channelNames.push_back(definition.name);
    }
    ChannelInfo info;
    info.name  = definition.name;
    info.label = tr("%1 (derived)").arg(definition.name);
    info.color = definition.color.isValid() ? definition.color : QColor("#9333ea");
    channels.push_back(info);
    m_derived.push_back(definition);
  }
  m_data     = data;
  m_channels = channels;

  if (!selectedNames.isEmpty()) {
    m_selected.clear();
    for (int i{0}; i < m_channels.size(); ++i) {
      if (selectedNames.contains(m_channels[i].name)) {
        m_selected.push_back(i);
      }
    }
  }
}

void AnalysisRunPage::openDerivedDialog() {
  if (!m_baseData) {
    return;
  }
  DerivedVariablesDialog dialog(m_channels.mid(0, m_baseChannelCount), m_resHolder->users->getCurrentUserName(), this);
  dialog.exec();
  if (!dialog.changed()) {
    return;
  }
  applyDerived();
  computeRunAnalysis();
  m_ruleChannelCombo->clear();
  for (ChannelInfo const &channel : std::as_const(m_channels)) {
    m_ruleChannelCombo->addItem(channel.label, channel.name);
  }
  updateChannelsButton();
  refreshChart();
}

void AnalysisRunPage::updateSetpointDefault() {
  int const channel{compareChannel()};
  if (channel == m_setpointDefaultFor) {
    return;
  }
  m_setpointDefaultFor = channel;
  // last recorded value of "<channel>_ref" (e.g. temp -> temp_ref)
  QString const reference = m_channels.value(channel).name + "_ref";
  for (int i{0}; i < m_channels.size(); ++i) {
    if (m_channels[i].name != reference) {
      continue;
    }
    QVector<double> const &values = m_data->states[i];
    for (qsizetype j = values.size() - 1; j >= 0; --j) {
      if (!std::isnan(values[j])) {
        m_setpointEdit->setValue(static_cast<float>(values[j]));
        return;
      }
    }
  }
}

void AnalysisRunPage::refreshSetpoint() {
  bool const show{compareMode() && m_setpointCheck->isChecked()};
  m_chart->setSetpoint(show ? m_setpointEdit->text().toDouble() : qQNaN(), m_channels.value(compareChannel()).scientific);
}

QColor AnalysisRunPage::channelColor(int index) const {
  return m_colorOverrides.value(index, m_channels.value(index).color);
}

Qt::PenStyle AnalysisRunPage::channelPenStyle(int index) const {
  return m_penOverrides.value(index, m_channels.value(index).penStyle);
}

int AnalysisRunPage::channelWidth(int index) const {
  return m_widthOverrides.value(index, ChartSeries{}.width);
}

void AnalysisRunPage::editChannelStyle(int index) {
  QDialog dialog(this);
  dialog.setWindowTitle(m_channels.value(index).label);
  auto *lay  = new QVBoxLayout(&dialog);
  auto *grid = new QGridLayout();
  lay->addLayout(grid);

  auto color = std::make_shared<QColor>(channelColor(index));
  auto *colorBtn = new QPushButton(&dialog);
  colorBtn->setFixedSize(theme::dp(75), theme::dp(48));
  auto *styleCombo = new QComboBox(&dialog);
  styleCombo->setIconSize(QSize(theme::dp(150), theme::dp(26)));
  for (int style{Qt::SolidLine}; style < Qt::CustomDashLine; ++style) {
    styleCombo->addItem(QString{}, style);
  }
  auto *widthCombo = new QComboBox(&dialog);
  for (int width{1}; width <= 4; ++width) {
    widthCombo->addItem(tr("%1 px").arg(width), width);
  }
  styleCombo->setCurrentIndex(styleCombo->findData(static_cast<int>(channelPenStyle(index))));
  widthCombo->setCurrentIndex(std::max(0, widthCombo->findData(channelWidth(index))));

  // the colour swatch and the line previews follow the choices
  auto refresh = [colorBtn, styleCombo, widthCombo, color] {
    colorBtn->setStyleSheet(QStringLiteral("background: %1; border: 1px solid #a9afba; border-radius: 3px;").arg(color->name()));
    for (int i{0}; i < styleCombo->count(); ++i) {
      styleCombo->setItemIcon(i, lineIcon(*color, static_cast<Qt::PenStyle>(styleCombo->itemData(i).toInt()),
                                          widthCombo->currentData().toInt()));
    }
  };
  refresh();
  connect(colorBtn, &QPushButton::clicked, &dialog, [&dialog, color, refresh] {
    QColor const picked = QColorDialog::getColor(*color, &dialog);
    if (picked.isValid()) {
      *color = picked;
      refresh();
    }
  });
  connect(widthCombo, &QComboBox::currentIndexChanged, &dialog, refresh);

  grid->addWidget(new QLabel(tr("Colour"), &dialog), 0, 0);
  grid->addWidget(colorBtn, 0, 1, Qt::AlignLeft);
  grid->addWidget(new QLabel(tr("Line style"), &dialog), 1, 0);
  grid->addWidget(styleCombo, 1, 1);
  grid->addWidget(new QLabel(tr("Line width"), &dialog), 2, 0);
  grid->addWidget(widthCombo, 2, 1);

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  lay->addWidget(buttons);

  if (dialog.exec() != QDialog::Accepted) {
    return;
  }
  m_colorOverrides[index] = *color;
  m_penOverrides[index]   = static_cast<Qt::PenStyle>(styleCombo->currentData().toInt());
  m_widthOverrides[index] = widthCombo->currentData().toInt();
  refreshChart();
}

void AnalysisRunPage::refreshStats() {
  QDateTime const start = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(m_run.start));
  QDateTime const end   = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(m_run.end));
  int             number{0};
  if (m_library && !m_run.custom) {
    QVector<ExperimentRun> const all = m_library->chronological();
    for (int i{0}; i < all.size(); ++i) {
      if (all[i].key == m_run.key) {
        number = i + 1;
        break;
      }
    }
  }
  QString const when = QStringLiteral("%1 – %2").arg(start.toString("yyyy-MM-dd HH:mm"),
                                                    end.date() == start.date() ? end.toString("HH:mm") : end.toString("yyyy-MM-dd HH:mm"));
  QString const head = m_run.custom ? tr("Range · %1").arg(when) : (number > 0 ? tr("Experiment #%1 · %2").arg(number).arg(when) : when);
  m_titleLabel->setText(head);

  core::RunMetadata const &details = m_manifest.metadata;
  QStringList              about{m_run.title};
  if (!details.dut.isEmpty()) {
    about << tr("DUT %1").arg(details.dut);
  }
  for (QString const &fact : {details.dutMassText(), details.dutDensityText()}) {
    if (!fact.isEmpty()) {
      about << fact;
    }
  }
  if (!details.purpose.isEmpty()) {
    about << tr("Purpose %1").arg(details.purpose);
  }
  if (!details.operatorName.isEmpty()) {
    about << tr("Operator %1").arg(details.operatorName);
  }
  if (!details.tags.isEmpty()) {
    about << QStringLiteral("#") + details.tags.join(QStringLiteral(" #"));
  }
  about << tr("Duration %1 · %2 samples").arg(ExperimentLibrary::formatDuration(m_run.duration())).arg(QLocale().toString(m_run.samples));
  m_titleLabel->setToolTip(about.join(QStringLiteral("\n")));

  // integrity: verified / modified (nothing for experiments without a fingerprint)
  theme::Colors const &c = theme::colors();
  bool const           known{m_manifestStored && (m_integrity != 2)};
  m_integrityBadge->setVisible(false); // shown in Statistics -> Experiment record
  if (known) {
    QColor const color = (m_integrity == 0) ? c.active : c.danger;
    m_integrityBadge->setText(m_integrity == 0 ? tr("VERIFIED") : tr("MODIFIED"));
    m_integrityBadge->setToolTip(runrecord::integrityText(m_integrity));
    QFont small = font();
    small.setPointSizeF(font().pointSizeF() * 0.8);
    small.setBold(true);
    m_integrityBadge->setFont(small);
    m_integrityBadge->setStyleSheet(QStringLiteral("color: %1; border: %2px solid %1; border-radius: %3px; padding: %4px %5px; background: %6;")
                                      .arg(color.name())
                                      .arg(qMax(1, static_cast<int>(1 * utils::ssf())))
                                      .arg(static_cast<int>(3 * utils::ssf()))
                                      .arg(static_cast<int>(1 * utils::ssf()))
                                      .arg(static_cast<int>(6 * utils::ssf()))
                                      .arg(QColor(color.red(), color.green(), color.blue(), 30).name(QColor::HexArgb)));
  }
  m_statsLabel->setToolTip(m_manifestStored ? runrecord::integrityText(m_integrity) : QString());
}

void AnalysisRunPage::refreshChart() {
  if (!m_data) {
    return;
  }
  m_chart->beginUpdate();

  QVector<ChartSeries> series;
  refreshComparison();
  if (compareMode()) {
    int const          channel{compareChannel()};
    ChannelInfo const &info = m_channels.value(channel);

    auto addRun = [&](ExperimentData const &data, QString const &label) {
      QVector<double> values = channelSeries(data, channel);
      if (values.isEmpty()) {
        return;
      }
      ChartSeries item;
      item.label   = label;
      item.color   = COMPARE_COLORS[series.size() % COMPARE_COLORS.size()];
      item.x       = comparedElapsed(data);
      item.y       = std::move(values);
      item.logAxis = info.scientific;
      series.push_back(item);
    };
    addRun(*m_data, m_run.title);
    for (QString const &key : std::as_const(m_compareKeys)) {
      if (auto data = m_compareData.value(key)) {
        addRun(*data, data->run.title);
      }
    }
    m_titleLabel->setText(tr("Comparing: %1").arg(info.label));
    m_chart->setSeries(series, 0.0);
  } else {
    for (int const index : std::as_const(m_selected)) {
      if (index >= m_data->states.size()) {
        continue;
      }
      ChartSeries item;
      item.label    = m_channels[index].label;
      item.color    = channelColor(index);
      item.penStyle = channelPenStyle(index);
      item.width    = channelWidth(index);
      item.x        = m_elapsed;
      item.y       = m_data->states[index];
      item.logAxis = m_channels[index].scientific;
      series.push_back(item);
    }
    refreshStats();
    m_chart->setSeries(series, m_run.start);
  }

  m_setpointGroup->setVisible(compareMode());
  if (compareMode()) {
    updateSetpointDefault();
  }
  refreshSetpoint();
  refreshStages();
  m_statisticsTimer->start();
  m_annotateBtn->setEnabled(!compareMode());
  if (compareMode()) {
    m_annotateBtn->setChecked(false);
  }
  refreshRegions();
  refreshBands();
  refreshEvents();
  m_chart->endUpdate();
}

void AnalysisRunPage::refreshRegions() {
  if (!m_data || compareMode()) {
    m_chart->setRegions({});
    return;
  }
  QVector<ChartRegion> regions;
  for (auto const &annotation : m_notes->annotations(m_run.start, m_run.end, m_run.key)) {
    regions.push_back({annotation.start - m_run.start, annotation.end - m_run.start, annotation.label, annotation.color});
  }
  m_chart->setRegions(regions);
}

void AnalysisRunPage::refreshBands() {
  if (!m_data) {
    return;
  }
  QStringList shownChannels;
  if (compareMode()) {
    shownChannels.push_back(m_channels.value(compareChannel()).name);
  } else {
    for (int const index : std::as_const(m_selected)) {
      shownChannels.push_back(m_channels.value(index).name);
    }
  }

  QVector<ChartBand> bands;
  for (auto const &rule : m_notes->rules(m_run.key)) {
    if (!shownChannels.contains(rule.channel)) {
      continue;
    }
    bool scientific{false};
    for (ChannelInfo const &channel : std::as_const(m_channels)) {
      if (channel.name == rule.channel) {
        scientific = channel.scientific;
      }
    }
    bands.push_back({rule.lo, rule.hi, rule.label, rule.color, scientific});
  }
  m_chart->setBands(bands);
}

namespace {
QString statText(double value, int precision = 4) {
  if (!std::isfinite(value)) {
    return QStringLiteral("—");
  }
  double const absValue{std::fabs(value)};
  if ((absValue != 0.0) && ((absValue < 1e-3) || (absValue >= 1e6))) {
    return utils::formatScientific(value, 2, false);
  }
  return QString::number(value, 'f', absValue >= 100 ? 1 : precision > 2 ? 3 : precision);
}

QString secondsText(double seconds) {
  return std::isfinite(seconds) ? ExperimentLibrary::formatDuration(seconds) : QStringLiteral("—");
}

QColor stageColor(core::Segment::Kind kind) {
  switch (kind) {
    case core::Segment::Kind::RAMP:
      return QColor("#f2a60c");
    case core::Segment::Kind::SETPOINT:
      return QColor("#2760e6");
    case core::Segment::Kind::HOLD:
      return QColor("#19b26b");
    default:
      return QColor("#5c6472");
  }
}

QString stageName(core::Segment::Kind kind) {
  switch (kind) {
    case core::Segment::Kind::RAMP:
      return QObject::tr("Ramp");
    case core::Segment::Kind::SETPOINT:
      return QObject::tr("Setpoint");
    case core::Segment::Kind::HOLD:
      return QObject::tr("Hold");
    default:
      return QObject::tr("Unknown");
  }
}

QTableWidget *makeTable(QStringList const &headers, int stretchColumn, QWidget *parent) {
  auto *table = new QTableWidget(0, static_cast<int>(headers.size()), parent);
  table->setHorizontalHeaderLabels(headers);
  table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
  table->horizontalHeader()->setSectionResizeMode(stretchColumn, QHeaderView::Stretch);
  table->verticalHeader()->setVisible(false);
  table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table->setSelectionBehavior(QAbstractItemView::SelectRows);
  table->setMinimumHeight(theme::dp(168));
  return table;
}
}

int AnalysisRunPage::channelIndex(QString const &name) const {
  for (int i{0}; i < m_channels.size(); ++i) {
    if (m_channels[i].name == name) {
      return i;
    }
  }
  return -1;
}

QWidget *AnalysisRunPage::buildStatisticsPanel() {
  auto *panel = new QTabWidget(this);
  theme::setFlatTabs(panel);
  panel->setDocumentMode(true);
  panel->tabBar()->setExpanding(false);

  auto *rangePage = new QWidget(panel);
  auto *rangeLay  = new QVBoxLayout(rangePage);
  rangeLay->setContentsMargins(0, theme::dp(8), 0, 0);
  m_rangeLabel = new QLabel(rangePage);
  m_rangeLabel->setStyleSheet("color: #5c6472;");
  m_rangeTable = makeTable({tr("Channel"), tr("n"), tr("Missing"), tr("Min"), tr("Max"), tr("Mean"), tr("Std"), tr("RMS"),
                            tr("Slope /min"), tr("P95")},
                           0,
                           rangePage);
  m_rangeKpiLabel = new QLabel(rangePage);
  m_rangeKpiLabel->setWordWrap(true);
  m_rangeKpiLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  rangeLay->addWidget(m_rangeLabel);
  rangeLay->addWidget(m_rangeTable);
  rangeLay->addWidget(m_rangeKpiLabel);

  auto *kpiPage = new QWidget(panel);
  auto *kpiLay  = new QVBoxLayout(kpiPage);
  kpiLay->setContentsMargins(0, theme::dp(8), 0, 0);
  m_kpiLabel = new QLabel(kpiPage);
  m_kpiLabel->setWordWrap(true);
  m_kpiLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  m_stagesTable = makeTable({tr("#"), tr("Stage"), tr("Interval"), tr("Cycle"), tr("Start"), tr("Duration"), tr("Setpoint"),
                             tr("MAE"), tr("Overshoot"), tr("Settling"), tr("Rise / fall"), tr("Steady-state error"),
                             tr("Steady after"), tr("Steady"), tr("Rate ref / value, /min"), tr("Lag"), tr("Saturated"),
                             tr("P / I / D"), tr("Kp / Ki / Kd")},
                            18,
                            kpiPage);
  m_stagesTable->setToolTip(tr("Tap a stage to zoom to it"));
  connect(m_stagesTable, &QTableWidget::cellClicked, this, [this](int row) {
    if (m_analysis && (row >= 0) && (row < m_analysis->run.stages.size())) {
      core::Segment const &segment = m_analysis->run.stages[row].segment;
      m_chart->setTimeRange(segment.start, std::max(segment.end, segment.start + 1.0));
      updateTimeControls();
    }
  });
  auto *tilesLay = new QHBoxLayout();
  tilesLay->setSpacing(theme::dp(32));
  for (QString const &title : {tr("MAE"), tr("RMSE"), tr("Max error"), tr("Overshoot"), tr("Undershoot"), tr("Settling"), tr("Tail MAE")}) {
    auto *tile    = new QWidget(kpiPage);
    auto *tileLay = new QVBoxLayout(tile);
    tileLay->setContentsMargins(0, 0, 0, 0);
    tileLay->setSpacing(theme::dp(4));
    auto *name = new QLabel(title, tile);
    name->setProperty("muted", true);
    auto *value = new QLabel(QStringLiteral("—"), tile);
    QFont font  = value->font();
    font.setBold(true);
    font.setPointSizeF(font.pointSizeF() * 1.35);
    value->setFont(font);
    tileLay->addWidget(name);
    tileLay->addWidget(value);
    tilesLay->addWidget(tile, 1);
    m_kpiTiles << value;
  }
  kpiLay->addLayout(tilesLay);
  m_kpiLabel->setProperty("muted", true);
  kpiLay->addWidget(m_kpiLabel);
  kpiLay->addWidget(m_stagesTable);

  auto *anomalyPage = new QWidget(panel);
  auto *anomalyLay  = new QVBoxLayout(anomalyPage);
  anomalyLay->setContentsMargins(0, theme::dp(8), 0, 0);
  m_anomalyLabel = new QLabel(anomalyPage);
  m_anomalyLabel->setWordWrap(true);
  m_anomalyTable = makeTable({tr("Severity"), tr("Type"), tr("Channel"), tr("Start"), tr("Duration"), tr("Detail")}, 5, anomalyPage);
  m_anomalyTable->setToolTip(tr("Tap an anomaly to zoom to it"));
  connect(m_anomalyTable, &QTableWidget::cellClicked, this, [this](int row) {
    if (m_analysis && (row >= 0) && (row < m_analysis->anomalies.size())) {
      core::Anomaly const &anomaly = m_analysis->anomalies[row];
      double const         margin{std::max(60.0, 0.5 * (anomaly.end - anomaly.start))};
      m_chart->setTimeRange(std::max(0.0, anomaly.start - margin), anomaly.end + margin);
      updateTimeControls();
    }
  });
  anomalyLay->addWidget(m_anomalyLabel);
  anomalyLay->addWidget(m_anomalyTable);

  m_comparisonPanel = new ComparisonPanel(panel);

  auto *eventsPage = new QWidget(panel);
  auto *eventsLay  = new QVBoxLayout(eventsPage);
  eventsLay->setContentsMargins(0, theme::dp(8), 0, 0);
  m_eventsLabel = new QLabel(eventsPage);
  m_eventsLabel->setWordWrap(true);
  m_eventsTable = makeTable({tr("Time"), tr("Event"), tr("Source"), tr("Details")}, 3, eventsPage);
  m_eventsTable->setToolTip(tr("Tap an event to zoom to it"));
  connect(m_eventsTable, &QTableWidget::cellClicked, this, [this](int row) {
    if ((row >= 0) && (row < m_manifest.events.size())) {
      double const at{m_manifest.events[row].time - m_run.start};
      m_chart->setTimeRange(std::max(0.0, at - 120.0), at + 120.0);
      updateTimeControls();
    }
  });
  eventsLay->addWidget(m_eventsLabel);
  eventsLay->addWidget(m_eventsTable);

  m_recordView = new QTextBrowser(panel);
  m_recordView->setMinimumHeight(theme::dp(168));
  m_recordView->setOpenLinks(false);

  panel->addTab(rangePage, tr("Selected range"));
  panel->addTab(kpiPage, tr("KPIs && stages"));
  panel->addTab(anomalyPage, tr("Anomalies"));
  panel->addTab(m_comparisonPanel, tr("Comparison"));
  panel->addTab(eventsPage, tr("Events"));
  panel->addTab(m_recordView, tr("Experiment record"));
  return panel;
}

void AnalysisRunPage::refreshRangeStatistics() {
  if (!m_data || !m_statisticsPanel->isVisible()) {
    return;
  }
  auto const [from, to] = m_chart->viewXRange();
  m_rangeLabel->setText(tr("Visible range %1 – %2 (raw samples, before smoothing)")
                          .arg(ExperimentLibrary::formatDuration(std::max(0.0, from)), ExperimentLibrary::formatDuration(std::max(0.0, to))));

  struct Row {
    QString         label;
    QVector<double> times;
    QVector<double> values;
  };
  QVector<Row> rows;
  if (compareMode()) {
    int const channel{compareChannel()};
    rows.push_back({m_run.title, m_elapsed, channelSeries(*m_data, channel)});
    for (QString const &key : std::as_const(m_compareKeys)) {
      if (auto data = m_compareData.value(key)) {
        rows.push_back({data->run.title, comparedElapsed(*data), channelSeries(*data, channel)});
      }
    }
  } else {
    for (int const index : std::as_const(m_selected)) {
      if (index < m_data->states.size()) {
        rows.push_back({m_channels[index].label, m_elapsed, m_data->states[index]});
      }
    }
  }

  m_rangeTable->setRowCount(static_cast<int>(rows.size()));
  for (int r{0}; r < rows.size(); ++r) {
    core::SeriesStats const stats = core::computeStats(rows[r].times, rows[r].values, from, to);
    QStringList const       cells{rows[r].label,
                            QString::number(stats.n),
                            QString::number(stats.missing),
                            statText(stats.min),
                            statText(stats.max),
                            statText(stats.mean),
                            statText(stats.std),
                            statText(stats.rms),
                            statText(stats.slopePerS * 60.0),
                            statText(stats.p95)};
    for (int c{0}; c < cells.size(); ++c) {
      auto *item = new QTableWidgetItem(cells[c]);
      item->setTextAlignment(c == 0 ? (Qt::AlignLeft | Qt::AlignVCenter) : (Qt::AlignRight | Qt::AlignVCenter));
      m_rangeTable->setItem(r, c, item);
    }
  }
  refreshRangeKpis(from, to);
}

void AnalysisRunPage::refreshRangeKpis(double from, double to) {
  int const temp{channelIndex("temp")};
  int const reference{channelIndex("temp_ref")};
  if ((temp < 0) || (reference < 0)) {
    m_rangeKpiLabel->setText(tr("Control KPIs need the channels temp and temp_ref."));
    return;
  }

  QVector<std::pair<QString, std::shared_ptr<ExperimentData const>>> runs{{m_run.title, m_data}};
  if (compareMode()) {
    for (QString const &key : std::as_const(m_compareKeys)) {
      if (auto data = m_compareData.value(key)) {
        runs.push_back({data->run.title, data});
      }
    }
  }

  QStringList lines;
  for (auto const &[title, data] : runs) {
    // the visible slice of this run
    QVector<double> const times = (data == m_data) ? m_elapsed : comparedElapsed(*data);
    QVector<double> const columns[4]{channelSeries(*data, temp), channelSeries(*data, reference),
                                     channelSeries(*data, channelIndex("interval")), channelSeries(*data, channelIndex("cycle_cur"))};
    QVector<double>       sliceTimes;
    QVector<double>       slices[4];
    for (int i{0}; i < times.size(); ++i) {
      if ((times[i] < from) || (times[i] > to)) {
        continue;
      }
      sliceTimes.push_back(times[i]);
      for (int c{0}; c < 4; ++c) {
        slices[c].push_back(i < columns[c].size() ? columns[c][i] : core::NaN);
      }
    }
    if (columns[2].isEmpty()) {
      slices[2].clear();
    }
    if (columns[3].isEmpty()) {
      slices[3].clear();
    }

    core::RunAnalysis const analysis = core::analyseRun(sliceTimes, slices[0], slices[1], slices[2], slices[3]);
    QString const           name     = runs.size() > 1 ? QStringLiteral("<b>%1</b>: ").arg(title.toHtmlEscaped())
                                                       : QStringLiteral("<b>%1</b> ").arg(tr("Control in range"));
    if (analysis.overall.samples == 0) {
      lines << name + tr("no setpoint (temp_ref) in this range.");
      continue;
    }

    QMap<core::Segment::Kind, int> stageCount;
    for (core::StageAnalysis const &stage : analysis.stages) {
      ++stageCount[stage.segment.kind];
    }
    QStringList stages;
    for (auto it = stageCount.cbegin(); it != stageCount.cend(); ++it) {
      stages << QStringLiteral("<span style=\"color:%1\">%2</span> ×%3").arg(stageColor(it.key()).name(), stageName(it.key())).arg(it.value());
    }
    core::ControlKpis const &k = analysis.overall;
    lines << name
               + tr("(temp vs temp_ref, %1 samples): MAE %2 · RMSE %3 · max |error| %4 · overshoot %5 · undershoot %6 · "
                    "settling %7 · steady-state error %8 · tail MAE %9 · tail std %10 · stages: %11")
                   .arg(k.samples)
                   .arg(statText(k.mae), statText(k.rmse), statText(k.maxAbsError), statText(analysis.overshoot),
                        statText(analysis.undershoot), secondsText(analysis.settlingTimeS), statText(k.steadyStateError),
                        statText(k.tailMae), statText(k.tailStd))
                   .arg(stages.join(QStringLiteral(", ")));
  }
  m_rangeKpiLabel->setText(lines.join(QStringLiteral("<br>")));
}

core::ExperimentAnalysis AnalysisRunPage::analyse(ExperimentData const &data) const {
  QStringList names;
  for (ChannelInfo const &channel : m_library->channels(data)) {
    names << channel.name;
  }
  // rules are per run: the page's own rules apply to its own run only
  return core::analyseExperiment(
    experimentInput(data, names, (data.run.key == m_run.key) ? m_notes->rules(m_run.key) : QVector<AnalysisNotesStore::Rule>{}));
}

void AnalysisRunPage::computeRunAnalysis() {
  m_analysis = analyse(*m_baseData);
  refreshAnomalies();

  core::ExperimentAnalysis const &a = *m_analysis;
  if (!a.hasControl) {
    for (QLabel *tile : std::as_const(m_kpiTiles)) {
      tile->setText(QStringLiteral("—"));
    }
    m_kpiLabel->setText(channelIndex("temp") < 0 || channelIndex("temp_ref") < 0
                          ? tr("KPIs need the channels temp and temp_ref.")
                          : tr("No setpoint (temp_ref) was recorded in this experiment, so there are no KPIs or stages."));
    m_stagesTable->setRowCount(0);
    refreshStages();
    return;
  }

  core::ControlKpis const &k = a.run.overall;
  core::RunConfig const   &c = a.config;
  QStringList              setpoints;
  for (double const setpoint : c.setpoints) {
    setpoints << statText(setpoint, 2);
  }
  double const steadyShare{a.run.stages.isEmpty()
                             ? qQNaN()
                             : core::steadyFraction(a.steady, a.run.stages.first().segment.start, a.run.stages.last().segment.end)};
  // headline KPIs as tiles (°C for the temperature errors)
  {
    auto degrees = [](double v) { return std::isfinite(v) ? statText(v) + QStringLiteral(" °C") : QStringLiteral("—"); };
    QStringList const values{degrees(k.mae),           degrees(k.rmse),          degrees(k.maxAbsError),
                             degrees(a.run.overshoot), degrees(a.run.undershoot), secondsText(a.run.settlingTimeS),
                             degrees(k.tailMae)};
    for (int i{0}; i < m_kpiTiles.size(); ++i) {
      m_kpiTiles[i]->setText(values.value(i));
    }
  }
  m_kpiLabel->setText(
    tr("<b>Experiment</b>: %1 samples · tail std %2.").arg(k.samples).arg(statText(k.tailStd))
    + QStringLiteral("<br>")
    + tr("<b>Control</b>: steady %1 of the time · output saturated %2 (windup %3) · P / I / D share %4 · "
         "<b>configuration</b>: Kp %5, Ki %6, Kd %7%8 · setpoints %9 · %10 anomalies (%11 warning or worse).")
        .arg(analysistext::percent(steadyShare), analysistext::percent(a.saturation.fraction), secondsText(a.saturation.windupS),
             analysistext::pidShares(a.pid), statText(c.kp), statText(c.ki), statText(c.kd),
             c.gainSets > 1 ? tr(" (%1 gain sets)").arg(c.gainSets) : QString(),
             setpoints.isEmpty() ? QStringLiteral("—") : setpoints.join(QStringLiteral(" → ")))
        .arg(a.anomalies.size())
        .arg(a.anomalyCount(core::Anomaly::Severity::WARNING)));

  m_stagesTable->setRowCount(static_cast<int>(a.run.stages.size()));
  for (int r{0}; r < a.run.stages.size(); ++r) {
    core::StageAnalysis const &stage = a.run.stages[r];
    core::StageDetail const   &d     = a.stageDetails.value(r);
    core::Segment const       &s     = stage.segment;
    bool const                 step{s.kind == core::Segment::Kind::SETPOINT};
    bool const                 ramp{s.kind == core::Segment::Kind::RAMP};
    QString const              setpoint = (std::fabs(s.referenceEnd - s.referenceStart) < 0.05)
                                            ? statText(s.referenceEnd, 2)
                                            : QStringLiteral("%1 → %2").arg(statText(s.referenceStart, 2), statText(s.referenceEnd, 2));
    double const               transitionTime{std::isfinite(stage.kpis.riseTimeS) ? stage.kpis.riseTimeS : stage.kpis.fallTimeS};
    QStringList const          cells{QString::number(r + 1),
                            stageName(s.kind),
                            s.interval >= 0 ? QString::number(s.interval) : QStringLiteral("—"),
                            s.cycle >= 0 ? QString::number(s.cycle) : QStringLiteral("—"),
                            ExperimentLibrary::formatDuration(s.start),
                            ExperimentLibrary::formatDuration(s.duration()),
                            setpoint,
                            statText(stage.kpis.mae),
                            step ? statText(stage.kpis.overshoot) : QStringLiteral("—"),
                            step ? secondsText(stage.kpis.settlingTimeS) : QStringLiteral("—"),
                            step ? secondsText(transitionTime) : QStringLiteral("—"),
                            step ? statText(stage.kpis.steadyStateError) : QStringLiteral("—"),
                            secondsText(d.timeToSteadyS),
                            analysistext::percent(d.steadyFraction),
                            ramp ? QStringLiteral("%1 / %2").arg(statText(d.referenceRatePerMin, 3), statText(d.valueRatePerMin, 3))
                                 : QStringLiteral("—"),
                            ramp ? secondsText(d.trackingLagS) : QStringLiteral("—"),
                            analysistext::percent(d.saturation.fraction),
                            analysistext::pidShares(d.pid),
                            QStringLiteral("%1 / %2 / %3").arg(statText(d.kp), statText(d.ki), statText(d.kd))};
    for (int c{0}; c < cells.size(); ++c) {
      auto *item = new QTableWidgetItem(cells[c]);
      if (c == 1) {
        item->setForeground(stageColor(s.kind));
      }
      m_stagesTable->setItem(r, c, item);
    }
  }
  refreshStages();
}

void AnalysisRunPage::refreshAnomalies() {
  QVector<core::Anomaly> const anomalies = m_analysis ? m_analysis->anomalies : QVector<core::Anomaly>{};
  m_anomalyLabel->setText(anomalies.isEmpty()
                            ? tr("No anomalies detected (variable rules, spikes, flat signals, missing data, oscillation, "
                                 "setpoints not reached, long output saturation).")
                            : tr("%1 anomalies: %2 critical, %3 warnings.")
                                .arg(anomalies.size())
                                .arg(m_analysis->anomalyCount(core::Anomaly::Severity::CRITICAL))
                                .arg(m_analysis->anomalyCount(core::Anomaly::Severity::WARNING)
                                     - m_analysis->anomalyCount(core::Anomaly::Severity::CRITICAL)));
  m_anomalyTable->setRowCount(static_cast<int>(anomalies.size()));
  for (int r{0}; r < anomalies.size(); ++r) {
    core::Anomaly const &anomaly = anomalies[r];
    QStringList const    cells{analysistext::severityName(anomaly.severity), analysistext::anomalyName(anomaly.kind), anomaly.channel,
                            ExperimentLibrary::formatDuration(anomaly.start), ExperimentLibrary::formatDuration(anomaly.end - anomaly.start),
                            anomaly.detail};
    for (int c{0}; c < cells.size(); ++c) {
      auto *item = new QTableWidgetItem(cells[c]);
      if (c == 0) {
        item->setForeground(analysistext::severityColor(anomaly.severity));
      }
      m_anomalyTable->setItem(r, c, item);
    }
  }
}

void AnalysisRunPage::refreshComparison() {
  m_compareOffsets.clear();
  if (!m_data || !m_analysis) {
    return;
  }
  int const            channel{compareChannel()};
  QVector<ComparedRun> runs;
  auto                 add = [&](ExperimentData const &data, core::ExperimentAnalysis const *analysis) {
    ComparedRun run;
    run.title     = data.run.title;
    run.run       = data.run;
    run.analysis  = analysis;
    run.times     = elapsed(data);
    run.temp      = channelSeries(data, channelIndex("temp"));
    run.reference = channelSeries(data, channelIndex("temp_ref"));
    run.signal    = channelSeries(data, channel);
    run.manifest  = data.manifest;
    runs.push_back(run);
  };
  add(*m_data, &*m_analysis);
  QStringList keys{m_run.key};
  for (QString const &key : std::as_const(m_compareKeys)) {
    if (auto data = m_compareData.value(key); data && m_compareAnalyses.contains(key)) {
      add(*data, &m_compareAnalyses[key]);
      keys << key;
    }
  }
  m_comparisonPanel->setRuns(compareMode() ? runs : QVector<ComparedRun>{}, m_channels.value(channel).label);
  QVector<double> const offsets = m_comparisonPanel->offsets();
  for (int k{0}; (k < keys.size()) && (k < offsets.size()); ++k) {
    m_compareOffsets.insert(keys[k], offsets[k]);
  }
}

double AnalysisRunPage::compareOffset(ExperimentData const &data) const {
  return m_compareOffsets.value(data.run.key, 0.0);
}

QVector<double> AnalysisRunPage::comparedElapsed(ExperimentData const &data) const {
  QVector<double> times  = elapsed(data);
  double const    offset = compareOffset(data);
  if (offset != 0.0) {
    for (double &time : times) {
      time += offset;
    }
  }
  return times;
}

void AnalysisRunPage::refreshStages() {
  if (!m_analysis || compareMode() || !m_stagesCheck->isChecked()) {
    m_chart->setStages({});
    return;
  }
  QVector<ChartRegion> regions;
  for (core::StageAnalysis const &stage : std::as_const(m_analysis->run.stages)) {
    core::Segment const &s = stage.segment;
    QString              label = stageName(s.kind);
    if (s.interval >= 0) {
      label += QStringLiteral(" %1").arg(s.interval);
    }
    regions.push_back({s.start, std::max(s.end, s.start + 1.0), label, stageColor(s.kind)});
  }
  m_chart->setStages(regions);
}

void AnalysisRunPage::refreshEvents() {
  if (!m_data || compareMode()) {
    m_chart->setEvents({});
    return;
  }
  QVector<ChartEvent> events;
  if (m_eventsCheck->isChecked()) {
    if (!m_manifest.events.isEmpty()) {
      // typed events of the run (captured live and from the recorded signals)
      for (core::RunEvent const &event : std::as_const(m_manifest.events)) {
        events.push_back({event.time - m_run.start, event.message, runrecord::eventColor(event.type)});
      }
    } else {
      for (ExperimentEvent const &event : m_library->events(m_run.start, m_run.end)) {
        events.push_back({event.time - m_run.start, event.msg, eventColor(event.type)});
      }
    }
  }
  if (m_anomaliesCheck->isChecked() && m_analysis) {
    for (core::Anomaly const &anomaly : m_analysis->anomalies) {
      events.push_back({anomaly.start, QStringLiteral("⚠ %1: %2").arg(analysistext::anomalyName(anomaly.kind), anomaly.detail),
                        analysistext::severityColor(anomaly.severity)});
    }
  }
  m_chart->setEvents(events);
}

void AnalysisRunPage::refreshRulesList() {
  m_rulesList->clear();
  for (auto const &rule : m_notes->rules(m_run.key)) {
    QString channelLabel = rule.channel;
    for (ChannelInfo const &channel : std::as_const(m_channels)) {
      if (channel.name == rule.channel) {
        channelLabel = channel.label;
      }
    }
    QString const lo = std::isnan(rule.lo) ? QStringLiteral("−∞") : limitText(rule.lo);
    QString const hi = std::isnan(rule.hi) ? QStringLiteral("+∞") : limitText(rule.hi);
    QPixmap       swatch(14, 14);
    swatch.fill(rule.color);
    auto *item = new QListWidgetItem(QIcon(swatch),
                                     QStringLiteral("%1 · %2  [%3 … %4]  · %5").arg(rule.label, channelLabel, lo, hi, rule.user),
                                     m_rulesList);
    item->setData(Qt::UserRole, rule.id);
  }
}

void AnalysisRunPage::refreshAnnotationsList() {
  if (!m_annotationsTable) {
    return;
  }
  m_annotationsTable->setRowCount(0);
  auto elapsed = [this](double epoch) { return ExperimentLibrary::formatDuration(epoch - m_run.start); };
  for (auto const &annotation : m_notes->annotations(m_run.start, m_run.end, m_run.key)) {
    int const row{m_annotationsTable->rowCount()};
    m_annotationsTable->insertRow(row);
    QPixmap swatch(14, 14);
    swatch.fill(annotation.color);
    auto *colorItem = new QTableWidgetItem(QIcon(swatch), QString());
    colorItem->setData(Qt::UserRole, annotation.id);
    auto *fromItem = new QTableWidgetItem(elapsed(annotation.start));
    fromItem->setData(Qt::UserRole, annotation.start);
    fromItem->setToolTip(QDateTime::fromSecsSinceEpoch(static_cast<qint64>(annotation.start)).toString("yyyy-MM-dd HH:mm:ss"));
    auto *toItem = new QTableWidgetItem(elapsed(annotation.end));
    toItem->setData(Qt::UserRole, annotation.end);
    toItem->setToolTip(QDateTime::fromSecsSinceEpoch(static_cast<qint64>(annotation.end)).toString("yyyy-MM-dd HH:mm:ss"));
    m_annotationsTable->setItem(row, 0, colorItem);
    m_annotationsTable->setItem(row, 1, new QTableWidgetItem(annotation.label));
    m_annotationsTable->setItem(row, 2, fromItem);
    m_annotationsTable->setItem(row, 3, toItem);
    m_annotationsTable->setItem(row, 4, new QTableWidgetItem(ExperimentLibrary::formatDuration(annotation.end - annotation.start)));
    m_annotationsTable->setItem(row, 5, new QTableWidgetItem(annotation.user));
  }
}

void AnalysisRunPage::updateTimeControls() {
  auto const [start, end] = m_chart->dataXRange();
  auto const [viewStart, viewEnd] = m_chart->viewXRange();
  for (QDoubleSpinBox *spin : {m_fromSpin, m_toSpin}) {
    spin->blockSignals(true);
    spin->setRange(start / 60.0, std::max(start, end) / 60.0);
  }
  m_fromSpin->setValue(std::max(start, viewStart) / 60.0);
  m_toSpin->setValue(std::min(end, viewEnd) / 60.0);
  for (QDoubleSpinBox *spin : {m_fromSpin, m_toSpin}) {
    spin->blockSignals(false);
  }
}

void AnalysisRunPage::updateChannelsButton() {
  m_channelsBtn->setText(tr("Channels (%1/%2) ▾").arg(m_selected.size()).arg(m_channels.size()));
}

void AnalysisRunPage::openChannelsMenu() {
  if (!m_data) {
    return;
  }
  auto *menu = new QMenu(this);
  menu->setAttribute(Qt::WA_DeleteOnClose);

  auto  checks   = std::make_shared<QVector<QCheckBox *>>();
  auto *quickRow = new QWidgetAction(menu);
  {
    auto *row      = new QWidget(menu);
    auto *rowLay   = new QHBoxLayout(row);
    rowLay->setContentsMargins(8, 4, 8, 4);
    auto *defaults = new QPushButton(tr("Default"), row);
    auto *none     = new QPushButton(tr("None"), row);
    rowLay->addWidget(defaults);
    rowLay->addWidget(none);
    quickRow->setDefaultWidget(row);

    // apply the whole selection at once instead of one redraw per channel
    auto applySelection = [this, checks](bool defaultsOnly) {
      QVector<int> selection;
      for (int i{0}; i < checks->size(); ++i) {
        bool const selected{defaultsOnly && m_channels[i].defaultVisible};
        QSignalBlocker blocker((*checks)[i]);
        (*checks)[i]->setChecked(selected);
        if (selected) {
          selection.push_back(i);
        }
      }
      setSelectedChannels(selection);
    };
    connect(defaults, &QPushButton::clicked, this, [applySelection] {
      applySelection(true);
    });
    connect(none, &QPushButton::clicked, this, [applySelection] {
      applySelection(false);
    });
  }
  menu->addAction(quickRow);
  menu->addSeparator();

  for (int i{0}; i < m_channels.size(); ++i) {
    if (i == m_baseChannelCount) {
      menu->addSeparator();
      menu->addAction(tr("Derived variables"))->setEnabled(false);
    }
    auto *action = new QWidgetAction(menu);
    auto *row    = new QWidget(menu);
    auto *rowLay = new QHBoxLayout(row);
    rowLay->setContentsMargins(8, 2, 8, 2);
    rowLay->setSpacing(8);

    auto *check = new QCheckBox(m_channels[i].label, row);
    check->setChecked(m_selected.contains(i));
    connect(check, &QCheckBox::toggled, this, [this, i](bool checked) {
      setChannelSelected(i, checked);
    });
    checks->push_back(check);

    auto *swatch = new QPushButton(row);
    swatch->setFixedSize(18, 18);
    swatch->setFocusPolicy(Qt::NoFocus);
    swatch->setStyleSheet(QStringLiteral("background: %1; border: 1px solid #a9afba; border-radius: 3px;")
                            .arg(channelColor(i).name()));
    swatch->setToolTip(tr("Colour, line style and width"));
    connect(swatch, &QPushButton::clicked, this, [this, i, menu]() {
      menu->close();
      QTimer::singleShot(0, this, [this, i] { editChannelStyle(i); }); // after the menu closed
    });

    rowLay->addWidget(check, 1);
    rowLay->addWidget(swatch);
    action->setDefaultWidget(row);
    menu->addAction(action);
  }

  menu->addSeparator();
  connect(menu->addAction(tr("Manage derived variables…")), &QAction::triggered, this, [this] {
    QTimer::singleShot(0, this, &AnalysisRunPage::openDerivedDialog); // after the menu closed
  });

  menu->exec(m_channelsBtn->mapToGlobal(m_channelsBtn->rect().bottomLeft()));
}

void AnalysisRunPage::setChannelSelected(int index, bool selected) {
  if (selected == m_selected.contains(index)) {
    return;
  }
  QVector<int> selection = m_selected;
  if (selected) {
    selection.push_back(index);
  } else {
    selection.removeAll(index);
  }
  setSelectedChannels(selection);
}

void AnalysisRunPage::setSelectedChannels(QVector<int> selection) {
  std::sort(selection.begin(), selection.end());
  if (selection == m_selected) {
    return;
  }
  m_selected = selection;

  QStringList names;
  for (int const i : std::as_const(m_selected)) {
    names.push_back(m_channels[i].name);
  }
  m_notes->setChannels(m_run.key, names);

  updateChannelsButton();
  refreshChart();
}

void AnalysisRunPage::openCompareMenu() {
  if (!m_data) {
    return;
  }
  auto *menu = new QMenu(this);
  menu->setAttribute(Qt::WA_DeleteOnClose);

  auto *header = menu->addAction(tr("Compare \"%1\" with:").arg(m_channels.value(compareChannel()).label));
  header->setEnabled(false);

  auto *clear = menu->addAction(tr("Clear comparison"));
  clear->setEnabled(compareMode());
  connect(clear, &QAction::triggered, this, [this] {
    m_compareKeys.clear();
    m_compareBtn->setText(tr("Compare ▾"));
    refreshChart();
    m_chart->resetView();
    updateTimeControls();
  });
  menu->addSeparator();

  int shown{0};
  for (ExperimentRun const &run : m_library->runs()) {
    if (run.key == m_run.key) {
      continue;
    }
    if (++shown > MAX_COMPARE_MENU_RUNS) {
      break;
    }
    auto *action = new QWidgetAction(menu);
    auto *check  = new QCheckBox(QStringLiteral("%1   (%2)").arg(run.title, ExperimentLibrary::formatDuration(run.duration())), menu);
    check->setContentsMargins(8, 2, 8, 2);
    check->setChecked(m_compareKeys.contains(run.key));
    connect(check, &QCheckBox::toggled, this, [this, run](bool checked) {
      setCompareRun(run, checked);
    });
    action->setDefaultWidget(check);
    menu->addAction(action);
  }
  if (0 == shown) {
    menu->addAction(tr("No other experiments found"))->setEnabled(false);
  }

  menu->exec(m_compareBtn->mapToGlobal(m_compareBtn->rect().bottomLeft()));
}

void AnalysisRunPage::setCompareRun(ExperimentRun const &run, bool compared) {
  bool const wasCompare{compareMode()};
  if (compared) {
    if (!m_compareData.contains(run.key)) {
      QString error;
      auto    data = m_library->load(run, &error);
      if (!data) {
        (new MessageDialog(tr("Compare"), error))->open();
        return;
      }
      m_compareData.insert(run.key, data);
    }
    if (!m_compareAnalyses.contains(run.key)) {
      m_compareAnalyses.insert(run.key, analyse(*m_compareData.value(run.key)));
    }
    if (!m_compareKeys.contains(run.key)) {
      m_compareKeys.push_back(run.key);
    }
  } else {
    m_compareKeys.removeAll(run.key);
  }

  m_compareBtn->setText(compareMode() ? tr("Compare (%1) ▾").arg(m_compareKeys.size()) : tr("Compare ▾"));
  refreshChart();
  if (wasCompare != compareMode()) {
    m_chart->resetView();
  }
  updateTimeControls();
}

void AnalysisRunPage::onAnnotationDrawn(double x0, double x1) {
  QString label;
  QColor  color;
  if (!annotationdialog::ask(this, QStringLiteral("%1 – %2").arg(ExperimentLibrary::formatDuration(x0), ExperimentLibrary::formatDuration(x1)),
                             label, color)) {
    return;
  }

  AnalysisNotesStore::Annotation annotation;
  annotation.runKey   = m_run.key;
  annotation.runStart = m_run.start;
  annotation.start    = m_run.start + x0;
  annotation.end   = m_run.start + x1;
  annotation.label = label;
  if (annotation.label.isEmpty()) {
    annotation.label = tr("t = %1 – %2").arg(ExperimentLibrary::formatDuration(x0), ExperimentLibrary::formatDuration(x1));
  }
  annotation.color = color;
  annotation.user  = m_resHolder->users->getCurrentUserName();
  m_notes->addAnnotation(annotation);
  m_annotateBtn->setChecked(false);
}

void AnalysisRunPage::addRule() {
  QString const channel = m_ruleChannelCombo->currentData().toString();
  double        lo{qQNaN()};
  double        hi{qQNaN()};
  if (channel.isEmpty() || !parseNumber(m_ruleLoEdit->text(), lo) || !parseNumber(m_ruleHiEdit->text(), hi)
      || (std::isnan(lo) && std::isnan(hi))) {
    (new MessageDialog(tr("Variable rule"), tr("Enter a numeric min and/or max value")))->open();
    return;
  }
  if (!std::isnan(lo) && !std::isnan(hi) && (lo > hi)) {
    std::swap(lo, hi);
  }

  AnalysisNotesStore::Rule rule;
  rule.channel = channel;
  rule.lo      = lo;
  rule.hi      = hi;
  rule.label   = m_ruleLabelEdit->text().trimmed();
  if (rule.label.isEmpty()) {
    rule.label = m_ruleChannelCombo->currentText();
  }
  rule.color = QColor(m_ruleColorCombo->currentData().toString());
  rule.user  = m_resHolder->users->getCurrentUserName();

  // make the rule visible right away
  for (int i{0}; i < m_channels.size(); ++i) {
    if (m_channels[i].name == channel && !compareMode()) {
      setChannelSelected(i, true);
    }
  }
  m_notes->addRule(m_run.key, rule);

  m_ruleLoEdit->clear();
  m_ruleHiEdit->clear();
  m_ruleLabelEdit->clear();
}

void AnalysisRunPage::askExportPath(QString const &extension, std::function<bool(QString const &)> writer) {
  // file browser in the export folder, named after the experiment
  QString name = m_run.title;
  name.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]+")), QStringLiteral("_"));
  QDir().mkpath(deepvaccontrol::EXPORT_DIR);
  QString const suggested = QDir(deepvaccontrol::EXPORT_DIR).absoluteFilePath(QStringLiteral("%1.%2").arg(name, extension));
  QString const filePath  = QFileDialog::getSaveFileName(this, tr("Export"), suggested,
                                                         tr("%1 files (*.%2)").arg(extension.toUpper(), extension));
  if (filePath.isEmpty()) {
    return;
  }
  QString const fullPath = filePath.endsWith("." + extension, Qt::CaseInsensitive) ? filePath : filePath + "." + extension;
  bool const    ok{writer(fullPath)};
  (new MessageDialog(tr("Export"), ok ? tr("Saved to %1").arg(fullPath) : tr("Failed to write %1").arg(fullPath)))->open();
}

void AnalysisRunPage::exportChartImage(QString const &format) {
  if (!m_data) {
    return;
  }
  askExportPath(format, [this, format](QString const &path) {
    return m_chart->exportImage(path, format.toLatin1().constData());
  });
}

void AnalysisRunPage::exportRunData(bool visibleOnly, bool tabSeparated) {
  if (!m_data) {
    return;
  }
  auto const [from, to] = m_chart->viewXRange();
  askExportPath(tabSeparated ? "txt" : "csv", [this, visibleOnly, tabSeparated, from, to](QString const &path) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
      return false;
    }
    QTextStream out(&file);
    out.setGenerateByteOrderMark(true);
    QChar const separator{tabSeparated ? QLatin1Char('\t') : QLatin1Char(',')};
    auto        field = [tabSeparated](QString const &text) {
      return tabSeparated ? QString(text).replace('\t', ' ') : csvQuote(text);
    };

    QStringList header{tr("Time"), tr("Elapsed, s")};
    for (ChannelInfo const &channel : std::as_const(m_channels)) {
      header.push_back(field(channel.label));
    }
    out << header.join(separator) << '\n';

    for (qsizetype i{0}; i < m_data->times.size(); ++i) {
      if (visibleOnly && ((m_elapsed[i] < from) || (m_elapsed[i] > to))) {
        continue;
      }
      QStringList row{QDateTime::fromSecsSinceEpoch(static_cast<qint64>(m_data->times[i])).toString("yyyy-MM-dd HH:mm:ss"),
                      csvNumber(m_elapsed[i])};
      for (auto const &channel : m_data->states) {
        row.push_back(csvNumber(i < channel.size() ? channel[i] : qQNaN()));
      }
      out << row.join(separator) << '\n';
    }
    out.flush();
    return file.commit();
  });
}

void AnalysisRunPage::exportComparisonCsv() {
  if (!m_data) {
    return;
  }
  askExportPath("csv", [this](QString const &path) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
      return false;
    }
    QTextStream out(&file);
    out.setGenerateByteOrderMark(true);

    int const          channel{compareChannel()};
    ChannelInfo const &info = m_channels.value(channel);
    out << QStringList{tr("Experiment"),
                       tr("Start"),
                       tr("End"),
                       tr("Duration, s"),
                       tr("Samples"),
                       tr("Channel"),
                       tr("Min"),
                       tr("Max"),
                       tr("Mean"),
                       tr("Final")}
             .join(',')
        << '\n';

    QVector<std::shared_ptr<ExperimentData const>> runs{m_data};
    for (QString const &key : std::as_const(m_compareKeys)) {
      if (auto data = m_compareData.value(key)) {
        runs.push_back(data);
      }
    }
    for (auto const &data : runs) {
      double minValue{qQNaN()};
      double maxValue{qQNaN()};
      double finalValue{qQNaN()};
      double sum{0.0};
      qint64 count{0};
      if (QVector<double> const values = channelSeries(*data, channel); !values.isEmpty()) {
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
      }
      out << QStringList{csvQuote(data->run.title),
                         QDateTime::fromSecsSinceEpoch(static_cast<qint64>(data->run.start)).toString("yyyy-MM-dd HH:mm:ss"),
                         QDateTime::fromSecsSinceEpoch(static_cast<qint64>(data->run.end)).toString("yyyy-MM-dd HH:mm:ss"),
                         csvNumber(data->run.duration()),
                         QString::number(data->run.samples),
                         csvQuote(info.label),
                         csvNumber(minValue),
                         csvNumber(maxValue),
                         csvNumber(count > 0 ? sum / static_cast<double>(count) : qQNaN()),
                         csvNumber(finalValue)}
               .join(',')
          << '\n';
    }
    out.flush();
    return file.commit();
  });
}

void AnalysisRunPage::exportReport(bool pdf) {
  if (!m_data || !m_analysis) {
    return;
  }
  ExperimentReportData const report = buildReport();
  askExportPath(pdf ? "pdf" : "html", [report, pdf](QString const &path) {
    return pdf ? experimentreport::writePdf(path, report) : experimentreport::writeHtml(path, report);
  });
}

ExperimentReportData AnalysisRunPage::buildReport() {
  ExperimentReportData report;
  report.run         = m_run;
  report.analysis    = &*m_analysis;
  report.events      = m_library->events(m_run.start, m_run.end);
  report.annotations = m_notes->annotations(m_run.start, m_run.end, m_run.key);
  report.rules       = m_notes->rules(m_run.key);
  report.manifest    = m_manifest.toJson();
  report.manifestStored = m_manifestStored;
  report.integrity   = m_integrity;
  report.author      = m_resHolder->users->getCurrentUserName();

  int const width{1600};
  int const height{700};
  // the chart as currently shown (in compare mode: the aligned comparison)
  report.charts.push_back({compareMode() ? tr("Comparison of %1, aligned on %2").arg(m_channels.value(compareChannel()).label,
                                                                                      m_comparisonPanel->alignmentText())
                                         : tr("Chart as shown"),
                           m_chart->renderImage(width, height)});
  // process and control charts of this run with its stages
  auto stagedChart = [&](QStringList const &names, QString const &caption) {
    QVector<ChartSeries> series;
    for (QString const &name : names) {
      int const index{channelIndex(name)};
      if ((index < 0) || (index >= m_data->states.size())) {
        continue;
      }
      ChartSeries item;
      item.label    = m_channels[index].label;
      item.color    = channelColor(index);
      item.penStyle = channelPenStyle(index);
      item.width    = channelWidth(index);
      item.x        = m_elapsed;
      item.y     = m_data->states[index];
      series.push_back(item);
    }
    if (series.isEmpty()) {
      return;
    }
    AnalysisChart chart;
    chart.resize(width, height);
    chart.beginUpdate();
    chart.setSeries(series, m_run.start);
    QVector<ChartRegion> stages;
    for (core::StageAnalysis const &stage : std::as_const(m_analysis->run.stages)) {
      core::Segment const &segment = stage.segment;
      stages.push_back({segment.start, std::max(segment.end, segment.start + 1.0), stageName(segment.kind), stageColor(segment.kind)});
    }
    chart.setStages(stages);
    QVector<ChartEvent> markers;
    for (core::Anomaly const &anomaly : m_analysis->anomalies) {
      if (anomaly.severity >= core::Anomaly::Severity::WARNING) {
        markers.push_back({anomaly.start, analysistext::anomalyName(anomaly.kind), analysistext::severityColor(anomaly.severity)});
      }
    }
    chart.setEvents(markers);
    chart.endUpdate();
    chart.resetView();
    report.charts.push_back({caption, chart.renderImage(width, height)});
  };
  stagedChart({"temp", "temp_ref"}, tr("Temperature and setpoint with stages and anomalies (warning or worse)"));
  stagedChart({"temp_u", "temp_u_p", "temp_u_i", "temp_u_d"}, tr("Control output and its P / I / D terms"));
  if (compareMode()) {
    report.comparisonHtml = m_comparisonPanel->html();
  }
  return report;
}

void AnalysisRunPage::loadRecord() {
  m_manifest       = core::RunManifest::fromJson(m_baseData->manifest);
  m_manifestStored = m_manifest.isValid();
  m_integrity      = 2;
  if (m_manifestStored) {
    m_integrity = static_cast<int>(storage::RunStore::verify(m_run.storeKey));
  } else {
    // no manifest (recorded files, older runs): the events visible in the signals, derived now
    m_manifest = core::RunManifest{};
    QHash<QString, QVector<double>> channels;
    QVector<ChannelInfo> const      infos = m_library->channels(*m_baseData);
    for (qsizetype c{0}; (c < infos.size()) && (c < m_baseData->states.size()); ++c) {
      channels.insert(infos[c].name, m_baseData->states[c]);
    }
    m_manifest.events = core::detectEvents(m_baseData->times, channels,
                                           core::alarmLabels(m_resHolder->settings->settingsMap().value("alarms").toList()));
    QVariantList states;
    QStringList  names;
    for (ChannelInfo const &info : infos) {
      names << info.name;
    }
    m_manifest.channels = core::channelMetadata(m_resHolder->settings->settingsMap().value("states").toList(), names);
  }
  refreshRecord();
}

void AnalysisRunPage::refreshRecord() {
  m_eventsLabel->setText(m_manifestStored
                           ? tr("%1 events recorded with the run: captured live by the application (job, operator, PLC alarms, "
                                "PID table writes) and from the recorded signals (setpoint, gains, PID band, status, program, alarm bits).")
                               .arg(m_manifest.events.size())
                           : tr("%1 events derived now from the recorded signals (this run has no manifest, so no live events or "
                                "configuration snapshot).")
                               .arg(m_manifest.events.size()));
  m_eventsTable->setRowCount(static_cast<int>(m_manifest.events.size()));
  for (int r{0}; r < m_manifest.events.size(); ++r) {
    core::RunEvent const &event = m_manifest.events[r];
    QStringList const     cells{ExperimentLibrary::formatDuration(std::max(0.0, event.time - m_run.start)), runrecord::eventTypeName(event.type),
                            event.source == QLatin1String("app") ? tr("live") : tr("signal"), event.message};
    for (int c{0}; c < cells.size(); ++c) {
      auto *item = new QTableWidgetItem(cells[c]);
      if (c == 1) {
        item->setForeground(runrecord::eventColor(event.type));
      }
      m_eventsTable->setItem(r, c, item);
    }
  }
  m_recordView->setHtml(recordHtml());
  refreshStats();
}

QString AnalysisRunPage::recordHtml() const {
  auto esc   = [](QString const &text) { return text.toHtmlEscaped(); };
  auto table = [&esc](QStringList const &columns, QVector<QStringList> const &rows) {
    QString html = QStringLiteral("<table border='1' cellspacing='0' cellpadding='3'><tr>");
    for (QString const &column : columns) {
      html += QStringLiteral("<th bgcolor='#e3e8ee'>%1</th>").arg(esc(column));
    }
    html += QStringLiteral("</tr>");
    for (QStringList const &row : rows) {
      html += QStringLiteral("<tr>");
      for (QString const &value : row) {
        html += QStringLiteral("<td>%1</td>").arg(esc(value));
      }
      html += QStringLiteral("</tr>");
    }
    return html + QStringLiteral("</table>");
  };
  QJsonObject const &configuration = m_manifest.configuration;
  QString            html;
  html += QStringLiteral("<h4>%1</h4>").arg(tr("Identity"));
  if (m_manifestStored) {
    html += QStringLiteral("<p>%1: %2<br>%3: %4 (%5)<br>%6: %7 %8<br>%9</p>")
              .arg(tr("Experiment uid"), esc(m_manifest.runUid), tr("Chamber"), esc(m_manifest.chamber), esc(m_manifest.chamberRef),
                   tr("Recorded by"), esc(m_manifest.appName), esc(m_manifest.appVersion))
              .arg(tr("Time resolution %1 s · %2 samples").arg(m_manifest.timeResolutionS).arg(m_manifest.sampleCount));
  } else {
    html += QStringLiteral("<p>%1</p>").arg(esc(tr("Not saved in the experiment database: no manifest. Save it (Export ▾) to keep its record.")));
  }
  core::RunMetadata const &details = m_manifest.metadata;
  auto dash = [](QString const &value) { return value.isEmpty() ? QStringLiteral("—") : value; };
  html += QStringLiteral("<h4>%1</h4><p>%2: %3<br>%4: %5<br>%6: %7<br>").arg(tr("Experiment details"), tr("DUT"), esc(dash(details.dut)),
                                                                                tr("DUT mass"), esc(dash(details.dutMassText())), tr("DUT density"),
                                                                                esc(dash(details.dutDensityText())));
  html += QStringLiteral("%1: %2<br>%3: %4<br>%5: %6</p>")
            .arg(tr("Operator"), esc(dash(details.operatorName)), tr("Purpose"), esc(dash(details.purpose)), tr("Tags"),
                 esc(dash(details.tags.join(QStringLiteral(", ")))));
  if (!details.notes.isEmpty()) {
    html += QStringLiteral("<p>%1</p>").arg(esc(details.notes));
  }
  html += QStringLiteral("<h4>%1</h4><p>%2<br>%3: %4<br>%5: %6</p>")
            .arg(tr("Configuration snapshot"), esc(runrecord::capturedText(configuration)), tr("Job"), esc(runrecord::jobText(configuration)),
                 tr("User"), esc(configuration.value("user").toObject().value("name").toString(QStringLiteral("—"))));
  if (QVector<QStringList> const program = runrecord::programRows(configuration); !program.isEmpty()) {
    html += QStringLiteral("<p><b>%1</b></p>").arg(tr("Program sent to the PLC")) + table(runrecord::programColumns(), program);
  }
  if (QVector<QStringList> const pid = runrecord::pidRows(configuration); !pid.isEmpty()) {
    html += QStringLiteral("<p><b>%1</b></p>").arg(tr("PID table")) + table(runrecord::pidColumns(), pid);
  }
  if (configuration.contains("plc_settings")) {
    html += QStringLiteral("<p><b>%1</b>: %2</p>").arg(tr("Calibration"), esc(runrecord::calibrationSummary(configuration)));
  }
  QVector<QStringList> channels;
  for (core::ChannelMeta const &channel : m_manifest.channels) {
    channels.push_back({channel.name, channel.label, channel.unit, channel.quantity, channel.role});
  }
  html += QStringLiteral("<h4>%1</h4>").arg(tr("Channels")) + table({tr("Name"), tr("Description"), tr("Unit"), tr("Quantity"), tr("Role")}, channels);
  html += QStringLiteral("<h4>%1</h4><p>%2<br>SHA-256 %3</p>")
            .arg(tr("Integrity"), esc(runrecord::integrityText(m_integrity)),
                 esc(m_manifest.samplesSha256.isEmpty() ? QStringLiteral("—") : m_manifest.samplesSha256));
  return html;
}

void AnalysisRunPage::exportBundle() {
  if (m_run.storeKey.isEmpty()) {
    return;
  }
  askExportPath("json", [this](QString const &path) {
    QString           error;
    QJsonObject const bundle = storage::RunStore::bundle(m_run.storeKey, &error);
    if (bundle.isEmpty()) {
      return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
      return false;
    }
    file.write(QJsonDocument(bundle).toJson(QJsonDocument::Indented));
    return file.commit();
  });
}

void AnalysisRunPage::editRunDetails() {
  if (m_run.storeKey.isEmpty()) {
    return;
  }
  RunDetailsDialog dialog(m_manifest.metadata, tr("Experiment details: %1").arg(m_run.title), this);
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }
  if (!storage::RunStore::updateMetadata(m_run.storeKey, dialog.metadata())) {
    (new MessageDialog(tr("Experiment details"), tr("This experiment has no manifest yet; it gets one at the next start of the application.")))->open();
    return;
  }
  m_manifest.metadata = dialog.metadata();
  refreshRecord();
  m_library->refresh(); // searchable details in the browser
}

void AnalysisRunPage::saveToDatabase() {
  if (!m_data || !m_resHolder->recorder) {
    return;
  }

  QDialog dialog(this);
  dialog.setWindowTitle(tr("Save to experiment database"));
  auto *lay      = new QVBoxLayout(&dialog);
  auto *nameEdit = new LineEditScreen(&dialog, VirtualKeyboard::EN);
  nameEdit->setText(m_run.title);
  nameEdit->setMinimumHeight(buttonHeight());
  lay->addWidget(new QLabel(tr("Name of the experiment (shared with paired computers):"), &dialog));
  lay->addWidget(nameEdit);
  auto *buttons   = new QHBoxLayout();
  auto *cancelBtn = makeButton(tr("Cancel"), &dialog);
  auto *saveBtn   = makeButton(tr("Save"), &dialog);
  buttons->addWidget(cancelBtn);
  buttons->addStretch();
  buttons->addWidget(saveBtn);
  lay->addLayout(buttons);
  connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);
  connect(saveBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
  dialog.resize(theme::dp(975), dialog.sizeHint().height());
  if ((dialog.exec() != QDialog::Accepted) || nameEdit->text().trimmed().isEmpty()) {
    return;
  }

  storage::RunStore::NewRun run;
  run.name    = nameEdit->text().trimmed();
  run.chamber = m_resHolder->recorder->chamberName();
  run.runUid  = storage::RunStore::makeRunUid(m_resHolder->recorder->chamberRef(), m_run.start, m_run.end);
  run.times   = m_data->times;
  for (int c{0}; c < m_baseChannelCount; ++c) { // recorded channels only, derived ones are recomputed
    run.columns.push_back(m_channels[c].name);
    run.values.push_back(m_data->states.value(c));
  }
  if (auto existing = storage::RunStore::findByUid(run.runUid)) {
    (new MessageDialog(tr("Save to experiment database"), tr("Already saved as \"%1\".").arg(existing->id)))->open();
    return;
  }
  connect(m_resHolder->recorder, &ExperimentRecorder::runSaved, this, [this](QString const &, QString const &name) {
    (new MessageDialog(tr("Save to experiment database"), tr("Saved \"%1\".").arg(name)))->open();
    m_library->refresh();
  }, Qt::SingleShotConnection);
  m_resHolder->recorder->saveAsync(run);
}

void AnalysisRunPage::handleAccessChange(QString const &name, AccessManager::AccessLevel level) {
  Q_UNUSED(name)
  bool const canExport{level >= AccessManager::AccessLevel::USER_START_1};
  m_exportBtn->setEnabled(canExport);
  if (QMenu *exportMenu = m_exportBtn->menu()) {
    exportMenu->menuAction()->setEnabled(canExport); // the Export entry of the ⋮ menu
  }
}

}
