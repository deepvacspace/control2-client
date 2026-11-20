#include "chartpane.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDrag>
#include <QDragEnterEvent>
#include <QEvent>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QStackedWidget>
#include <QStyle>
#include <QStyleOption>
#include <QTabBar>
#include <QTimer>
#include <QHBoxLayout>
#include <QToolButton>
#include <QVBoxLayout>

#include "analysisnotesstore.h"
#include "datamanager.h"
#include "dataviewer.h"
#include "theme.h"
#include "touchsupport.h"
#include "utils.h"

namespace deepvaccontrol {

QString const ChartPane::LIVE_KEY{QStringLiteral("live")};

namespace {
char const TAB_MIME[]{"application/x-deepvac-chart-tab"};
int const  MAX_SHOWN{7};                              // tabs in the row (Live included); the others under the chevron
ChartPane *g_dragSource{nullptr}; // pane a tab is being dragged out of

// small turning arc in a tab whose data is being read
class TabSpinner : public QWidget {
public:
  explicit TabSpinner(QWidget *parent) : QWidget{parent}, m_timer{new QTimer(this)} {
    setFixedSize(theme::dp(22), theme::dp(22));
    setAttribute(Qt::WA_TransparentForMouseEvents);
    connect(m_timer, &QTimer::timeout, this, [this] {
      m_angle = (m_angle + 30) % 360;
      update();
    });
    m_timer->start(60);
  }

protected:
  void paintEvent(QPaintEvent *) override {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    double const pen{1.6 * utils::ssf()};
    QRectF const r = QRectF(rect()).adjusted(pen, pen, -pen, -pen);
    p.setPen(QPen(theme::colors().border, pen));
    p.drawEllipse(r);
    p.setPen(QPen(theme::colors().accent, pen, Qt::SolidLine, Qt::RoundCap));
    p.drawArc(r, -m_angle * 16, 100 * 16);
  }

private:
  QTimer *m_timer;
  int     m_angle{0};
};

// the panes' tabs: never wider than its tabs (its own minimum, room for the scroll arrows, left a gap before the +)
class PaneTabBar : public QTabBar {
public:
  using QTabBar::QTabBar;
  [[nodiscard]] QSize minimumSizeHint() const override {
    return QTabBar::minimumSizeHint().boundedTo(sizeHint());
  }

protected:
  // as wide as the name in bold (the selected tab's), so that selecting a tab does not change its size
  [[nodiscard]] QSize tabSizeHint(int index) const override {
    QSize size = QTabBar::tabSizeHint(index);
    size.rwidth() += QFontMetrics(boldFont()).horizontalAdvance(tabText(index)) - fontMetrics().horizontalAdvance(tabText(index));
    return size;
  }

  // a fixed tab (Live) keeps its whole name; the others shorten when the strip is narrow
  [[nodiscard]] QSize minimumTabSizeHint(int index) const override {
    return (tabData(index).toString() == QLatin1String("fixed")) ? tabSizeHint(index) : QTabBar::minimumTabSizeHint(index);
  }

  // the selected tab's name is drawn in bold: shortened (at its end) to the room it has in bold, read from its start
  void initStyleOption(QStyleOptionTab *option, int tabIndex) const override {
    QTabBar::initStyleOption(option, tabIndex);
    if (tabIndex == currentIndex()) {
      int const room{style()->subElementRect(QStyle::SE_TabBarTabText, option, this).width()};
      option->text = QFontMetrics(boldFont()).elidedText(tabText(tabIndex), Qt::ElideRight, room);
    }
  }

private:
  [[nodiscard]] QFont boldFont() const {
    QFont bold = font();
    bold.setBold(true);
    return bold;
  }
};

// before "Live": a dot, green while the chamber runs a job
class LiveDot : public QWidget {
public:
  explicit LiveDot(QWidget *parent) : QWidget{parent} {
    setFixedSize(theme::dp(22), theme::dp(22));
    setAttribute(Qt::WA_TransparentForMouseEvents);
  }
  void setActive(bool active) {
    m_active = active;
    update();
  }

protected:
  void paintEvent(QPaintEvent *) override {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(m_active ? theme::colors().active : theme::colors().muted);
    double const d{8 * utils::ssf()};
    p.drawEllipse(QRectF((width() - d) / 2, (height() - d) / 2, d, d));
  }

private:
  bool m_active{false};
};
}

ChartPane::ChartPane(DataViewer *viewer, DataManager *dataManager, QStringList const &channelNames, QWidget *parent)
    : QFrame{parent}
    , m_viewer{viewer}
    , m_dataManager{dataManager}
    , m_channelNames{channelNames}
    , m_tabBar{new PaneTabBar(this)}
    , m_stack{new QStackedWidget(this)}
    , m_dropHint{new QWidget(this)} {
  double const ssf{utils::ssf()};
  setObjectName("chartPane");
  setAcceptDrops(true);
  auto *lay = new QVBoxLayout(this);
  lay->setContentsMargins(0, 0, 0, 0);
  lay->setSpacing(theme::gap());

  // the tabs in a rounded strip, reordered by dragging
  m_tabBar->setDocumentMode(true);
  m_tabBar->setTabsClosable(true);
  m_tabBar->setMovable(true);
  m_tabBar->setExpanding(false);
  m_tabBar->setElideMode(Qt::ElideRight);
  m_tabBar->setFocusPolicy(Qt::NoFocus);
  m_tabBar->setContextMenuPolicy(Qt::CustomContextMenu);
  // plain labels, the selected one a white box (the theme's QTabBar#paneTabs), tall enough for a finger
  m_tabBar->setObjectName("paneTabs");
  m_tabBar->setDrawBase(false);
  m_tabBar->setStyleSheet(QStringLiteral("QTabBar::scroller { width: %1px; }"
                                         "QTabBar QToolButton::left-arrow { image: url(:/imgs/listArrowLeft.png); }"
                                         "QTabBar QToolButton::right-arrow { image: url(:/imgs/listArrowRight.png); }")
                            .arg(theme::dp(60)));
  {
    QFont font = m_tabBar->font();
    font.setPointSizeF(font.pointSizeF() * 0.9);
    m_tabBar->setFont(font);
  }
  // the tabs, then a + to open an experiment or a comparison; the corner widget at the strip's right end
  auto *headerFrame = new QFrame(this);
  headerFrame->setObjectName("paneHeader");
  auto *header = new QHBoxLayout(headerFrame);
  m_header     = header;
  header->setContentsMargins(theme::dp(5), theme::dp(5), theme::dp(5), theme::dp(5));
  header->setSpacing(theme::dp(6));
  m_tabBar->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
  header->addWidget(m_tabBar);
  // more tabs than fit in the row: a chevron listing the hidden ones
  m_moreBtn = new QToolButton(headerFrame);
  m_moreBtn->setObjectName("tabMore");
  m_moreBtn->setToolTip(tr("More tabs"));
  m_moreBtn->setFocusPolicy(Qt::NoFocus);
  m_moreBtn->setFixedSize(theme::dp(42), theme::dp(42));
  theme::setThemedIcon(m_moreBtn, QStringLiteral("down"), theme::dp(20));
  m_moreBtn->hide();
  header->addWidget(m_moreBtn, 0, Qt::AlignVCenter);
  connect(m_moreBtn, &QToolButton::clicked, this, &ChartPane::showOverflowMenu);
  m_addBtn = new QToolButton(headerFrame);
  m_addBtn->setObjectName("tabAdd");
  m_addBtn->setToolTip(tr("Open an experiment, or a new comparison"));
  m_addBtn->setFocusPolicy(Qt::NoFocus);
  m_addBtn->setFixedSize(theme::dp(42), theme::dp(42)); // a square, as tall as the tabs
  theme::setThemedIcon(m_addBtn, QStringLiteral("plus"), theme::dp(22));
  header->addWidget(m_addBtn, 0, Qt::AlignVCenter);
  header->addStretch();
  lay->addWidget(headerFrame);
  connect(m_addBtn, &QToolButton::clicked, this, [this] {
    emit activated();
    emit addRequested(m_addBtn->mapToGlobal(QPoint(0, m_addBtn->height())));
  });

  // the pane's chart in a card (comparison tabs bring their own), the footer under it (the first pane: the chart's
  // tools); right of it the side widget (the Live tab's status and the options)
  m_stack->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  m_chartCard = new QFrame(this);
  m_chartCard->setObjectName("chartCard");
  m_cardLay = new QVBoxLayout(m_chartCard);
  m_cardLay->setContentsMargins(theme::gap(), theme::gap(), theme::gap(), theme::gap());
  m_cardLay->setSpacing(theme::dp(8));
  m_cardLay->addWidget(m_stack, 1);
  m_chartRow = new QHBoxLayout();
  m_chartRow->setSpacing(theme::gap());
  m_chartRow->addWidget(m_chartCard, 1);
  lay->addLayout(m_chartRow, 1);
  m_toolbar = new ChartToolbar(this);
  m_pad     = new ChartPad(this);
  m_pad->hide();
  connect(this, &QObject::destroyed, m_toolbar, &QObject::deleteLater); // floating, it is the card's child
  connect(m_toolbar, &ChartToolbar::sizeChanged, this, &ChartPane::placeOverlays);
  connect(m_toolbar, &ChartToolbar::dragged, this, [this](QPoint const &delta) {
    if (!m_toolbarMoved) {
      m_toolbarOffset = m_toolbar->pos() - toolbarBounds().topLeft(); // from the corner it stood in
      m_toolbarMoved  = true;
    }
    m_toolbarOffset += delta;
    placeOverlays();
  });
  connect(m_pad, &ChartPad::zoomRequested, this, [this](double x, double y) {
    emit activated();
    this->viewer()->zoomView(x, y);
  });
  connect(m_pad, &ChartPad::moveRequested, this, [this](double x, double y) {
    emit activated();
    this->viewer()->moveView(x, y);
  });
  connect(m_pad, &ChartPad::closeRequested, this, &ChartPane::toolResetRequested);
  attachViewer(m_viewer);

  // where a dragged tab would go
  theme::Colors const &c = theme::colors();
  m_dropHint->setAttribute(Qt::WA_TransparentForMouseEvents);
  m_dropHint->setAttribute(Qt::WA_StyledBackground);
  m_dropHint->setStyleSheet(QStringLiteral("background: %1; border: %2px dashed %3; border-radius: %4px;")
                              .arg(QColor(c.accent.red(), c.accent.green(), c.accent.blue(), 40).name(QColor::HexArgb))
                              .arg(theme::dp(3))
                              .arg(c.accent.name())
                              .arg(theme::radius()));
  m_dropHint->hide();

  connect(m_tabBar, &QTabBar::currentChanged, this, &ChartPane::showTab);
  connect(m_tabBar, &QTabBar::tabCloseRequested, this, &ChartPane::closeTab);
  connect(m_tabBar, &QTabBar::tabMoved, this, [this](int from, int to) {
    m_tabs.move(from, to);
    emit tabsChanged();
    // the live tab stays first
    if (int const live{indexOf(LIVE_KEY)}; live > 0) {
      m_tabBar->moveTab(live, 0);
    }
    updateOverflow();
  });
  connect(m_tabBar, &QWidget::customContextMenuRequested, this, [this](QPoint const &pos) {
    int const index{m_tabBar->tabAt(pos)};
    if (index >= 0) {
      emit activated();
      emit tabMenuRequested(index, m_tabBar->mapToGlobal(pos));
    }
  });

  // a click in the pane makes it the one the buttons act on
  m_tabBar->installEventFilter(this);
  touch::enableLongPress(m_tabBar, nullptr, true); // press and hold a tab: its menu
  setHighlighted(false);
}

void ChartPane::attachViewer(DataViewer *viewer) {
  m_stack->addWidget(viewer);
  // the tool in use applies to it too; the overlays follow its plot area
  viewer->setAnnotateMode(m_tool == ChartToolbar::Tool::ANNOTATE);
  viewer->setBoxZoomMode(m_tool == ChartToolbar::Tool::BOX_ZOOM);
  connect(viewer->getPlot(), &QCustomPlot::afterReplot, this, [this, viewer] {
    if (viewer == this->viewer()) {
      placeOverlays();
    }
  });
  viewer->getPlot()->installEventFilter(this); // a click in it makes the pane the focused one
  connect(viewer, &DataViewer::annotationDrawn, this, &ChartPane::annotationDrawn);
  connect(viewer, &DataViewer::valueCursorMoved, this, &ChartPane::cursorMoved);
  connect(viewer, &DataViewer::valueCursorHidden, this, &ChartPane::cursorHidden);
}

void ChartPane::detachViewer(DataViewer *viewer) {
  disconnect(viewer, nullptr, this, nullptr);
  viewer->getPlot()->removeEventFilter(this);
  m_stack->removeWidget(viewer);
  viewer->setParent(nullptr);
}

DataViewer *ChartPane::viewer() const {
  Tab const *shown = currentTab();
  return (shown && shown->ownViewer) ? shown->ownViewer : m_viewer;
}

QVector<DataViewer *> ChartPane::viewers() const {
  QVector<DataViewer *> all{m_viewer};
  for (Tab const &each : m_tabs) {
    if (each.ownViewer) {
      all.push_back(each.ownViewer);
    }
  }
  return all;
}

ChartPane::Tab const *ChartPane::tab(int index) const {
  return ((index >= 0) && (index < m_tabs.size())) ? &m_tabs[index] : nullptr;
}

ChartPane::Tab const *ChartPane::currentTab() const {
  return tab(m_tabBar->currentIndex());
}

QJsonObject ChartPane::viewOf(int index) const {
  Tab const *each = tab(index);
  if (!each) {
    return {};
  }
  if (each->ownViewer) {
    return each->ownViewer->getData() ? each->ownViewer->viewState() : each->view;
  }
  return (each->key == m_shownKey) ? m_viewer->viewState() : each->view;
}

void ChartPane::setTabView(QString const &key, QJsonObject const &view) {
  int const index{indexOf(key)};
  if (index < 0) {
    return;
  }
  m_tabs[index].view = view;
  if (key == m_shownKey) {
    m_viewer->setViewState(view);
  }
}

int ChartPane::indexOf(QString const &key) const {
  for (int i{0}; i < m_tabs.size(); ++i) {
    if (m_tabs[i].key == key) {
      return i;
    }
  }
  return -1;
}

bool ChartPane::liveShown() const {
  Tab const *shown = currentTab();
  return shown && (shown->key == LIVE_KEY);
}

double ChartPane::dataStart() const {
  auto const data = viewer()->getData();
  if (!data) {
    return qQNaN();
  }
  QVector<double> const times = data->getTimes();
  return times.isEmpty() ? qQNaN() : times.first();
}

bool ChartPane::selectTab(QString const &key) {
  int const index{indexOf(key)};
  if (index >= 0) {
    m_tabBar->setCurrentIndex(index);
    updateOverflow();
  }
  return index >= 0;
}

void ChartPane::updateOverflow() {
  if (m_inOverflow) {
    return;
  }
  m_inOverflow = true;
  int const count{m_tabBar->count()};
  if (int const current{m_tabBar->currentIndex()}; current >= MAX_SHOWN) {
    m_tabBar->moveTab(current, MAX_SHOWN - 1); // the last shown one goes under the chevron
  }
  for (int i{0}; i < count; ++i) {
    m_tabBar->setTabVisible(i, i < MAX_SHOWN);
  }
  m_moreBtn->setVisible(count > MAX_SHOWN);
  m_inOverflow = false;
}

void ChartPane::showOverflowMenu() {
  emit activated();
  QMenu menu(this);
  menu.setToolTipsVisible(true);
  for (int i{MAX_SHOWN}; i < m_tabs.size(); ++i) {
    QAction *action = menu.addAction(m_tabs[i].label);
    action->setToolTip(m_tabs[i].name);
    action->setData(m_tabs[i].key);
  }
  QAction const *chosen = menu.exec(m_moreBtn->mapToGlobal(QPoint(0, m_moreBtn->height())));
  if (chosen) {
    selectTab(chosen->data().toString());
  }
}

void ChartPane::addTab(Tab const &tab, int at) {
  if (selectTab(tab.key)) {
    return;
  }
  if ((at < 0) || (at > m_tabs.size())) {
    at = static_cast<int>(m_tabs.size());
  }
  if ((tab.key != LIVE_KEY) && (indexOf(LIVE_KEY) == 0)) {
    at = std::max(1, at); // after the live tab
  }
  m_tabs.insert(at, tab);
  if (tab.ownViewer) {
    attachViewer(tab.ownViewer);
  }
  int const index{m_tabBar->insertTab(at, tab.label)}; // the first tab gets selected (and shown) right away
  m_tabBar->setTabToolTip(index, tab.name);
  if (tab.key == LIVE_KEY) {
    m_tabBar->setTabData(index, QStringLiteral("fixed")); // its name never shortened
    m_tabBar->setTabButton(index, QTabBar::RightSide, nullptr); // cannot be closed
    auto *dot = new LiveDot(m_tabBar);
    dot->setActive(m_liveActive);
    m_tabBar->setTabButton(index, QTabBar::LeftSide, dot);
  } else {
    // the ×: the icons' own, in a square a finger finds (the style's close indicator stays small)
    auto *close = new QToolButton(m_tabBar);
    close->setObjectName("tabClose");
    close->setFocusPolicy(Qt::NoFocus);
    close->setToolTip(tr("Close"));
    close->setFixedSize(theme::dp(28), theme::dp(28));
    theme::setThemedIcon(close, QStringLiteral("close"), theme::dp(16), QStringLiteral("muted"));
    connect(close, &QToolButton::clicked, this, [this, close] {
      for (int i{0}; i < m_tabBar->count(); ++i) {
        if (m_tabBar->tabButton(i, QTabBar::RightSide) == close) {
          closeTab(i);
          return;
        }
      }
    });
    m_tabBar->setTabButton(index, QTabBar::RightSide, close);
    if (tab.loading) {
      m_tabBar->setTabButton(index, QTabBar::LeftSide, new TabSpinner(m_tabBar));
    }
  }
  m_tabBar->setCurrentIndex(index);
  updateOverflow();
  emit tabsChanged();
}

ChartPane::Tab ChartPane::takeTab(int index) {
  if ((index < 0) || (index >= m_tabs.size()) || (m_tabs[index].key == LIVE_KEY)) {
    return {};
  }
  Tab taken = m_tabs.takeAt(index); // before the tab bar selects another tab
  if (taken.key == m_shownKey) { // its zoom goes with it
    taken.view = m_viewer->viewState();
    m_shownKey.clear();
  }
  m_tabBar->removeTab(index);
  updateOverflow(); // the first hidden tab comes into the row
  if (taken.ownViewer) {
    detachViewer(taken.ownViewer);
  }
  emit tabsChanged();
  if (m_tabs.isEmpty()) {
    emit emptied();
  }
  return taken;
}

void ChartPane::closeTab(int index) {
  Tab const closed = takeTab(index);
  if (closed.ownViewer) {
    closed.ownViewer->deleteLater();
  }
}

void ChartPane::updateTab(QString const &key, QString const &label, QString const &name,
                          std::shared_ptr<ChartComparison const> const &comparison) {
  int const index{indexOf(key)};
  if (index < 0) {
    return;
  }
  Tab &changed       = m_tabs[index];
  changed.label      = label;
  changed.name       = name;
  changed.comparison = comparison;
  changed.loading    = true;
  m_tabBar->setTabText(index, label);
  m_tabBar->setTabToolTip(index, name);
  if (!m_tabBar->tabButton(index, QTabBar::LeftSide)) {
    m_tabBar->setTabButton(index, QTabBar::LeftSide, new TabSpinner(m_tabBar));
  }
  if (index == m_tabBar->currentIndex()) {
    showTab(index);
  }
}

void ChartPane::setTabData(QString const &key, std::shared_ptr<DataHolder const> const &data) {
  int const index{indexOf(key)};
  if (index < 0) {
    return;
  }
  Tab &loaded    = m_tabs[index];
  loaded.data    = data;
  loaded.loading = false;
  if (QWidget *spinner = m_tabBar->tabButton(index, QTabBar::LeftSide)) {
    m_tabBar->setTabButton(index, QTabBar::LeftSide, nullptr);
    spinner->deleteLater();
  }
  if (loaded.ownViewer) {
    loaded.ownViewer->setData(data);
    loaded.ownViewer->updateTextItem(QString{}); // what is compared is in the tab's tooltip and the legend
    loaded.ownViewer->setViewState(loaded.view); // as it was (restarts)
  }
  bool const zoom{!std::isnan(loaded.zoomFrom) && !std::isnan(loaded.zoomTo)};
  double const from{loaded.zoomFrom};
  double const to{loaded.zoomTo};
  loaded.zoomFrom = qQNaN(); // once
  if (index == m_tabBar->currentIndex()) {
    showTab(index);
    if (zoom && data && !data->getTimes().isEmpty()) {
      double const start{data->getTimes().first()};
      viewer()->setTimeRange(start + from, start + to);
    }
  }
}

void ChartPane::showTab(int index) {
  Tab const *shown = tab(index);
  if (!shown) {
    return;
  }
  if (shown->ownViewer) { // a comparison: its own chart, filled when its data is there
    m_stack->setCurrentWidget(shown->ownViewer);
    if (shown->loading || !shown->data) {
      shown->ownViewer->clear(tr("Comparing…"));
    }
    refreshRegions();
    emit currentTabChanged();
    return;
  }
  m_stack->setCurrentWidget(m_viewer);
  // the zoom of the tab shown so far stays with it
  if (int const previous{indexOf(m_shownKey)}; (previous >= 0) && (previous != index)) {
    m_tabs[previous].view = m_viewer->viewState();
  }
  QString const key  = shown->key;
  QJsonObject const view = shown->view;
  if (key == LIVE_KEY) {
    // the whole day as it is recorded (today's Parquet file), connected or not; while connected the new samples are
    // added. Not connected: why, beside the date; nothing recorded yet today: only why
    double const todayKey = utils::calculateKey(QDateTime::currentSecsSinceEpoch());
    if (m_dataManager->prepareIntervalToDisplay(todayKey)) {
      m_viewer->setData(m_dataManager->getIntervalToDisplay());
      if (!m_liveActive) {
        m_viewer->updateTextItem(QStringLiteral("%1 · %2").arg(QDate::currentDate().toString("yyyy-MM-dd"), m_liveIdleText));
      }
      m_shownKey = key;
    } else if (!m_liveActive) {
      m_viewer->clear(m_liveIdleText);
      m_shownKey.clear();
    } else {
      m_shownKey = key;
    }
  } else if (shown->loading || !shown->data) {
    m_viewer->clear(tr("Loading %1…").arg(shown->name));
    m_shownKey.clear();
  } else {
    m_viewer->setData(shown->data);
    m_viewer->updateTextItem(shown->name);
    m_shownKey = key;
  }
  if (m_shownKey == key) {
    m_viewer->setViewState(view); // its own zoom again
  }
  refreshRegions();
  applyTool();
  QTimer::singleShot(0, this, &ChartPane::placeOverlays);
  emit currentTabChanged();
}

void ChartPane::reloadLive(double key) {
  if (m_liveActive && liveShown() && m_dataManager->prepareIntervalToDisplay(key)) {
    m_viewer->setData(m_dataManager->getIntervalToDisplay());
    refreshRegions();
  }
}

void ChartPane::setLiveActive(bool active, QString const &idleText) {
  bool const changed{(m_liveActive != active) || (!active && (m_liveIdleText != idleText))};
  m_liveActive   = active;
  m_liveIdleText = idleText;
  if (int const live{indexOf(LIVE_KEY)}; live >= 0) {
    if (auto *dot = dynamic_cast<LiveDot *>(m_tabBar->tabButton(live, QTabBar::LeftSide))) {
      dot->setActive(active);
    }
  }
  if (changed && liveShown()) {
    showTab(m_tabBar->currentIndex());
  }
}

void ChartPane::setNotes(AnalysisNotesStore *notes) {
  m_notes = notes;
  connect(m_notes, &AnalysisNotesStore::annotationsChanged, this, &ChartPane::refreshRegions);
  connect(m_notes, &AnalysisNotesStore::rulesChanged, this, &ChartPane::refreshRegions);
  refreshRegions();
}

void ChartPane::refreshRegions() {
  Tab const *shown = currentTab();
  if (shown && shown->comparison) { // aligned times: no annotations or rules
    viewer()->setRegions({});
    viewer()->setBands({});
    return;
  }
  auto const data  = m_viewer->getData();
  QVector<double> const times = data ? data->getTimes() : QVector<double>{};
  if (!m_notes || !shown || times.isEmpty()) {
    m_viewer->setRegions({});
    m_viewer->setBands({});
    return;
  }
  QVector<DataViewer::Band> bands;
  for (auto const &rule : m_notes->rules(shown->runKey)) {
    bands.push_back({rule.lo, rule.hi, rule.label, rule.color, static_cast<int>(m_channelNames.indexOf(rule.channel))});
  }
  m_viewer->setBands(bands);
  // annotations are kept in absolute time: those of the analysis pages show here too
  double const end{liveShown() ? static_cast<double>(QDateTime::currentSecsSinceEpoch()) : times.last()};
  QVector<DataViewer::Region> regions;
  for (auto const &annotation : m_notes->annotations(times.first(), end, shown->runKey)) {
    regions.push_back({annotation.start, annotation.end, annotation.label, annotation.color});
  }
  m_viewer->setRegions(regions);
}

void ChartPane::setHighlighted(bool highlighted) {
  // the chart's card outlined in the accent
  m_chartCard->setStyleSheet(highlighted ? QStringLiteral("QFrame#chartCard { border-color: %1; }").arg(theme::colors().accent.name())
                                         : QString{});
}

void ChartPane::setPrimary(bool primary) {
  m_primary = primary;
}

// ---------------------------------------------------------------- dragging tabs
bool ChartPane::eventFilter(QObject *watched, QEvent *event) {
  if (watched == m_toolbarArea) {
    if (event->type() == QEvent::Resize) {
      placeOverlays();
    }
    return QFrame::eventFilter(watched, event);
  }
  if (event->type() == QEvent::MouseButtonPress) {
    emit activated();
  }
  if (watched != m_tabBar) {
    return QFrame::eventFilter(watched, event);
  }
  auto *mouse = static_cast<QMouseEvent *>(event);
  switch (event->type()) {
    case QEvent::MouseButtonPress: {
      int const index{m_tabBar->tabAt(mouse->position().toPoint())};
      bool const draggable{(mouse->button() == Qt::LeftButton) && (index >= 0) && (m_tabs[index].key != LIVE_KEY)};
      m_pressKey = draggable ? m_tabs[index].key : QString{};
      m_pressPos = mouse->position().toPoint();
      break;
    }
    case QEvent::MouseMove: {
      // out of the tab row: the tab leaves the row (to the other pane, or to split)
      int const margin{static_cast<int>(14 * utils::ssf())};
      QPoint const pos = mouse->position().toPoint();
      bool const   out{(pos.y() < -margin) || (pos.y() > m_tabBar->height() + margin)};
      if (!m_pressKey.isEmpty() && (mouse->buttons() & Qt::LeftButton) && out) {
        QString const key = m_pressKey;
        m_pressKey.clear();
        // end the tab bar's own move first
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(m_pressPos), mouse->globalPosition(), Qt::LeftButton,
                            Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(m_tabBar, &release);
        startTabDrag(key);
        return true;
      }
      break;
    }
    case QEvent::MouseButtonRelease:
      m_pressKey.clear();
      break;
    default:
      break;
  }
  return QFrame::eventFilter(watched, event);
}

void ChartPane::startTabDrag(QString const &key) {
  int const index{indexOf(key)};
  if (index < 0) {
    return;
  }
  auto *mime = new QMimeData();
  mime->setData(TAB_MIME, key.toUtf8());
  auto *drag = new QDrag(this);
  drag->setMimeData(mime);
  QRect const rect = m_tabBar->tabRect(index);
  drag->setPixmap(m_tabBar->grab(rect));
  drag->setHotSpot(QPoint(rect.width() / 2, rect.height() / 2));

  emit tabDragStarted();
  g_dragSource = this;
  drag->exec(Qt::MoveAction);
  g_dragSource = nullptr;
  emit tabDragFinished();
}

bool ChartPane::dropTarget(ChartPane *source, QPoint const &pos, int &at, bool &rightHalf, QRect &hint) const {
  at        = -1;
  rightHalf = false;
  QRect const bar(m_tabBar->mapTo(this, QPoint(0, 0)), m_tabBar->size());
  if (bar.adjusted(0, -bar.height(), 0, bar.height() / 2).contains(pos)) {
    // in the tab row: before the tab under the mouse
    int const index{m_tabBar->tabAt(m_tabBar->mapFrom(this, QPoint(pos.x(), bar.center().y())))};
    at   = (index >= 0) ? index : static_cast<int>(m_tabs.size());
    hint = bar;
    return true;
  }
  if (source != this) {
    hint = rect(); // into this pane
    return true;
  }
  if (m_primary && (pos.x() > width() / 2)) {
    QRect const chart(m_stack->mapTo(this, QPoint(0, 0)), m_stack->size());
    hint              = QRect(chart.center().x(), chart.top(), chart.width() / 2, chart.height()); // split right
    rightHalf         = true;
    return true;
  }
  return false;
}

void ChartPane::dragEnterEvent(QDragEnterEvent *event) {
  if (event->mimeData()->hasFormat(TAB_MIME) && g_dragSource) {
    event->acceptProposedAction();
  }
}

void ChartPane::dragMoveEvent(QDragMoveEvent *event) {
  int   at{-1};
  bool  rightHalf{false};
  QRect hint;
  if (!g_dragSource || !dropTarget(g_dragSource, event->position().toPoint(), at, rightHalf, hint)) {
    m_dropHint->hide();
    event->ignore();
    return;
  }
  m_dropHint->setGeometry(hint);
  m_dropHint->show();
  m_dropHint->raise();
  event->acceptProposedAction();
}

void ChartPane::dragLeaveEvent(QDragLeaveEvent *event) {
  m_dropHint->hide();
  QFrame::dragLeaveEvent(event);
}

void ChartPane::dropEvent(QDropEvent *event) {
  m_dropHint->hide();
  int        at{-1};
  bool       rightHalf{false};
  QRect      hint;
  ChartPane *source = g_dragSource;
  if (!source || !dropTarget(source, event->position().toPoint(), at, rightHalf, hint)) {
    event->ignore();
    return;
  }
  QString const key = QString::fromUtf8(event->mimeData()->data(TAB_MIME));
  event->acceptProposedAction();
  if ((source == this) && !rightHalf) {
    // back into its own row: a new place
    int const from{indexOf(key)};
    if (from >= 0) {
      m_tabBar->moveTab(from, std::clamp(at > from ? at - 1 : at, 0, static_cast<int>(m_tabs.size()) - 1));
    }
    return;
  }
  emit tabDropped(source, key, at, rightHalf);
}

void ChartPane::resizeEvent(QResizeEvent *event) {
  QFrame::resizeEvent(event);
  m_dropHint->hide();
  placeOverlays();
}

ChartToolbar *ChartPane::toolbar() const {
  return m_toolbar;
}

void ChartPane::setToolbarPinned(bool pinned) {
  m_toolbar->setPinned(pinned);
  m_chartRow->removeWidget(m_toolbar);
  if (pinned) {
    m_chartRow->insertWidget(0, m_toolbar, 0, Qt::AlignTop); // a strip beside the chart
  } else if (m_toolbar->parentWidget() != toolbarHost()) {
    m_toolbar->setParent(toolbarHost()); // floating over the card
  }
  m_toolbar->setVisible(m_toolbarShown);
  placeOverlays();
}

void ChartPane::setToolbarShown(bool shown) {
  m_toolbarShown = shown;
  m_toolbar->setVisible(shown);
  placeOverlays();
}

void ChartPane::setToolbarArea(QWidget *area) {
  if (m_toolbarArea) {
    m_toolbarArea->removeEventFilter(this);
  }
  m_toolbarArea = area;
  if (m_toolbarArea) {
    m_toolbarArea->installEventFilter(this); // placed again when it changes size
  }
  setToolbarPinned(m_toolbar->pinned());
}

void ChartPane::setCornerWidget(QWidget *widget) {
  m_corner = widget;
  widget->setParent(m_chartCard); // over the chart, placed with the overlays
  widget->raise();
  placeOverlays();
}

void ChartPane::setFooterWidget(QWidget *widget) {
  auto *line = new QFrame(m_chartCard);
  line->setObjectName("cardDivider");
  line->setFixedHeight(1);
  m_cardLay->addWidget(line);
  widget->setParent(m_chartCard);
  m_cardLay->addWidget(widget);
}

QWidget *ChartPane::chartCard() const {
  return m_chartCard;
}

void ChartPane::setSideWidget(QWidget *widget) {
  widget->setParent(this);
  // the chart 5 parts, the widget 2 (about 29 %), whatever their contents ask for
  widget->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  m_chartRow->addWidget(widget, 2);
  m_chartRow->setStretchFactor(m_chartCard, 5);
}

QWidget *ChartPane::toolbarHost() const {
  return m_toolbarArea ? m_toolbarArea : const_cast<ChartPane *>(this);
}

QRect ChartPane::toolbarBounds() const {
  // inside the card's border; the pane: all of it
  int const inset{m_toolbarArea ? static_cast<int>(2 * utils::ssf()) : 0};
  return toolbarHost()->rect().adjusted(inset, inset, -inset, -inset);
}

void ChartPane::setTool(ChartToolbar::Tool tool, bool pad) {
  m_tool     = tool;
  m_padShown = pad;
  applyTool();
}

void ChartPane::applyTool() {
  ChartToolbar::Tool const tool = m_tool;
  Tab const               *shown = currentTab();
  for (DataViewer *chart : viewers()) {
    // a comparison's aligned axis is not recorded time: no annotations there
    chart->setAnnotateMode((tool == ChartToolbar::Tool::ANNOTATE) && !(shown && shown->comparison));
    chart->setBoxZoomMode(tool == ChartToolbar::Tool::BOX_ZOOM);
  }
  bool const pad{m_padShown && ((tool == ChartToolbar::Tool::ZOOM) || (tool == ChartToolbar::Tool::PAN))};
  if (pad) {
    m_pad->setMode(tool);
  }
  m_pad->setVisible(pad);
  placeOverlays();
}

void ChartPane::placeOverlays() {
  DataViewer *chart = viewer();
  if (!chart || !m_toolbar) {
    return;
  }
  // the plot area (inside the axes) of the chart shown, in the pane
  QCustomPlot *plot = chart->getPlot();
  QPoint const end  = plot->mapTo(this, plot->axisRect()->rect().bottomLeft());
  int const    gap{static_cast<int>(6 * utils::ssf())};
  // the corner widget in the card's top right corner, inside its border (over the right axis' top, not the plot)
  if (m_corner && m_corner->isVisible()) {
    int const inset{theme::dp(3)};
    m_corner->move(m_chartCard->width() - m_corner->width() - inset, inset);
    m_corner->raise();
  }
  bool const   floating{m_toolbarShown && !m_toolbar->pinned()};
  // floating from the start: at the card's left edge, level with the top of the chart (over its margin more than the
  // value axis' labels); the chart's layout stays as it is
  if (floating) {
    m_toolbar->adjustSize();
    QRect const bounds = toolbarBounds();
    QPoint      at     = bounds.topLeft() + m_toolbarOffset;
    if (!m_toolbarMoved) {
      at = QPoint(bounds.left(), plot->mapTo(toolbarHost(), QPoint(0, 0)).y() + gap);
    }
    // anywhere on the card (over both charts), however far it was dragged
    at.setX(std::clamp(at.x(), bounds.left(), std::max(bounds.left(), bounds.right() + 1 - m_toolbar->width())));
    at.setY(std::clamp(at.y(), bounds.top(), std::max(bounds.top(), bounds.bottom() + 1 - m_toolbar->height())));
    if (m_toolbarMoved) {
      m_toolbarOffset = at - bounds.topLeft();
    }
    m_toolbar->move(at);
    m_toolbar->raise();
  }
  if (m_pad->isVisible()) {
    m_pad->adjustSize();
    m_pad->move(end.x() + gap, end.y() - gap - m_pad->height());
    m_pad->raise();
  }
}

}
