#include "chartpane.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDrag>
#include <QDragEnterEvent>
#include <QEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QStackedWidget>
#include <QTabBar>
#include <QTimer>
#include <QVBoxLayout>

#include "analysisnotesstore.h"
#include "datamanager.h"
#include "dataviewer.h"
#include "theme.h"
#include "utils.h"

namespace deepvaccontrol {

QString const ChartPane::LIVE_KEY{QStringLiteral("live")};

namespace {
char const TAB_MIME[]{"application/x-deepvac-chart-tab"};
ChartPane *g_dragSource{nullptr}; // pane a tab is being dragged out of

// small turning arc in a tab whose data is being read
class TabSpinner : public QWidget {
public:
  explicit TabSpinner(QWidget *parent) : QWidget{parent}, m_timer{new QTimer(this)} {
    setFixedSize(static_cast<int>(12 * utils::ssf()), static_cast<int>(12 * utils::ssf()));
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
} // namespace

ChartPane::ChartPane(DataViewer *viewer, DataManager *dataManager, QStringList const &channelNames, QWidget *parent)
    : QFrame{parent}
    , m_viewer{viewer}
    , m_dataManager{dataManager}
    , m_channelNames{channelNames}
    , m_tabBar{new QTabBar(this)}
    , m_stack{new QStackedWidget(this)}
    , m_dropHint{new QWidget(this)} {
  double const ssf{utils::ssf()};
  setObjectName("chartPane");
  setAcceptDrops(true);
  auto *lay = new QVBoxLayout(this);
  lay->setContentsMargins(0, 0, 0, 0);
  lay->setSpacing(4 * ssf);

  // small tabs above the chart, reordered by dragging
  m_tabBar->setDocumentMode(true);
  m_tabBar->setTabsClosable(true);
  m_tabBar->setMovable(true);
  m_tabBar->setExpanding(false);
  m_tabBar->setElideMode(Qt::ElideRight);
  m_tabBar->setFocusPolicy(Qt::NoFocus);
  m_tabBar->setContextMenuPolicy(Qt::CustomContextMenu);
  m_tabBar->setStyleSheet(utils::readStyleFromFile(":/styles/tabBar.qss").arg(24 * ssf).arg(70 * ssf).arg(28 * ssf));
  {
    QFont font = m_tabBar->font();
    font.setPointSizeF(font.pointSizeF() * 0.85);
    m_tabBar->setFont(font);
  }
  lay->addWidget(m_tabBar);

  // the pane's chart; comparison tabs bring their own
  m_stack->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  lay->addWidget(m_stack, 1);
  attachViewer(m_viewer);

  // where a dragged tab would go
  theme::Colors const &c = theme::colors();
  m_dropHint->setAttribute(Qt::WA_TransparentForMouseEvents);
  m_dropHint->setAttribute(Qt::WA_StyledBackground);
  m_dropHint->setStyleSheet(QStringLiteral("background: %1; border: %2px dashed %3; border-radius: %4px;")
                              .arg(QColor(c.accent.red(), c.accent.green(), c.accent.blue(), 40).name(QColor::HexArgb))
                              .arg(qMax(1, static_cast<int>(2 * ssf)))
                              .arg(c.accent.name())
                              .arg(static_cast<int>(6 * ssf)));
  m_dropHint->hide();

  connect(m_tabBar, &QTabBar::currentChanged, this, &ChartPane::showTab);
  connect(m_tabBar, &QTabBar::tabCloseRequested, this, &ChartPane::closeTab);
  connect(m_tabBar, &QTabBar::tabMoved, this, [this](int from, int to) {
    m_tabs.move(from, to);
    // the live tab stays first
    if (int const live{indexOf(LIVE_KEY)}; live > 0) {
      m_tabBar->moveTab(live, 0);
    }
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
  setHighlighted(false);
}

void ChartPane::attachViewer(DataViewer *viewer) {
  m_stack->addWidget(viewer);
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
  }
  return index >= 0;
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
    m_tabBar->setTabButton(index, QTabBar::RightSide, nullptr); // cannot be closed
    m_tabBar->setTabButton(index, QTabBar::LeftSide, nullptr);
  } else if (tab.loading) {
    m_tabBar->setTabButton(index, QTabBar::LeftSide, new TabSpinner(m_tabBar));
  }
  m_tabBar->setCurrentIndex(index);
}

ChartPane::Tab ChartPane::takeTab(int index) {
  if ((index < 0) || (index >= m_tabs.size()) || (m_tabs[index].key == LIVE_KEY)) {
    return {};
  }
  Tab const taken = m_tabs.takeAt(index); // before the tab bar selects another tab
  m_tabBar->removeTab(index);
  if (taken.ownViewer) {
    detachViewer(taken.ownViewer);
  }
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
    loaded.ownViewer->updateTextItem(loaded.name);
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
  if (shown->key == LIVE_KEY) {
    double const todayKey = utils::calculateKey(QDateTime::currentSecsSinceEpoch());
    if (m_dataManager->prepareIntervalToDisplay(todayKey)) {
      m_viewer->setData(m_dataManager->getIntervalToDisplay());
    }
  } else if (shown->loading || !shown->data) {
    m_viewer->clear(tr("Loading %1…").arg(shown->name));
  } else {
    m_viewer->setData(shown->data);
    m_viewer->updateTextItem(shown->name);
  }
  refreshRegions();
  emit currentTabChanged();
}

void ChartPane::reloadLive(double key) {
  if (liveShown() && m_dataManager->prepareIntervalToDisplay(key)) {
    m_viewer->setData(m_dataManager->getIntervalToDisplay());
    refreshRegions();
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
  theme::Colors const &c = theme::colors();
  setStyleSheet(QStringLiteral("QFrame#chartPane { border: %1px solid %2; border-radius: %3px; }")
                  .arg(qMax(1, static_cast<int>(1 * utils::ssf())))
                  .arg(highlighted ? c.accent.name() : QStringLiteral("transparent"))
                  .arg(static_cast<int>(6 * utils::ssf())));
}

void ChartPane::setPrimary(bool primary) {
  m_primary = primary;
}

// ---------------------------------------------------------------- dragging tabs
bool ChartPane::eventFilter(QObject *watched, QEvent *event) {
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
  QRect const bar = m_tabBar->geometry();
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
    QRect const chart = m_stack->geometry();
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
}

} // namespace deepvaccontrol
