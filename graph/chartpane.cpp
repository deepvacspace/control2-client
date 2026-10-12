#include "chartpane.h"

#include <QDateTime>
#include <QEvent>
#include <QTabBar>
#include <QVBoxLayout>

#include "analysisnotesstore.h"
#include "datamanager.h"
#include "dataviewer.h"
#include "theme.h"
#include "utils.h"

namespace deepvaccontrol {

QString const ChartPane::LIVE_KEY{QStringLiteral("live")};

ChartPane::ChartPane(DataViewer *viewer, DataManager *dataManager, QStringList const &channelNames, QWidget *parent)
    : QFrame{parent}, m_viewer{viewer}, m_dataManager{dataManager}, m_channelNames{channelNames}, m_tabBar{new QTabBar(this)} {
  double const ssf{utils::ssf()};
  setObjectName("chartPane");
  auto *lay = new QVBoxLayout(this);
  lay->setContentsMargins(0, 0, 0, 0);
  lay->setSpacing(4 * ssf);

  // small tabs above the chart
  m_tabBar->setDocumentMode(true);
  m_tabBar->setTabsClosable(true);
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

  m_viewer->setParent(this);
  m_viewer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  lay->addWidget(m_viewer, 1);

  connect(m_tabBar, &QTabBar::currentChanged, this, &ChartPane::showTab);
  connect(m_tabBar, &QTabBar::tabCloseRequested, this, [this](int index) {
    takeTab(index);
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
  m_viewer->getPlot()->installEventFilter(this);
  setHighlighted(false);
}

ChartPane::Tab const *ChartPane::tab(int index) const {
  return ((index >= 0) && (index < m_tabs.size())) ? &m_tabs[index] : nullptr;
}

ChartPane::Tab const *ChartPane::currentTab() const {
  return tab(m_tabBar->currentIndex());
}

bool ChartPane::liveShown() const {
  Tab const *shown = currentTab();
  return shown && (shown->key == LIVE_KEY);
}

double ChartPane::dataStart() const {
  auto const data = m_viewer->getData();
  if (!data) {
    return qQNaN();
  }
  QVector<double> const times = data->getTimes();
  return times.isEmpty() ? qQNaN() : times.first();
}

bool ChartPane::selectTab(QString const &key) {
  for (int i{0}; i < m_tabs.size(); ++i) {
    if (m_tabs[i].key == key) {
      m_tabBar->setCurrentIndex(i);
      return true;
    }
  }
  return false;
}

void ChartPane::addTab(Tab const &tab) {
  if (selectTab(tab.key)) {
    return;
  }
  m_tabs.push_back(tab);
  int const index{m_tabBar->addTab(tab.label)}; // the first tab gets selected (and shown) right away
  m_tabBar->setTabToolTip(index, tab.name);
  if (tab.key == LIVE_KEY) {
    m_tabBar->setTabButton(index, QTabBar::RightSide, nullptr); // cannot be closed
    m_tabBar->setTabButton(index, QTabBar::LeftSide, nullptr);
  }
  m_tabBar->setCurrentIndex(index);
}

ChartPane::Tab ChartPane::takeTab(int index) {
  if ((index < 0) || (index >= m_tabs.size()) || (m_tabs[index].key == LIVE_KEY)) {
    return {};
  }
  Tab const taken = m_tabs.takeAt(index); // before the tab bar selects another tab
  m_tabBar->removeTab(index);
  if (m_tabs.isEmpty()) {
    emit emptied();
  }
  return taken;
}

void ChartPane::showTab(int index) {
  Tab const *shown = tab(index);
  if (!shown) {
    return;
  }
  if (shown->key == LIVE_KEY) {
    double const todayKey = utils::calculateKey(QDateTime::currentSecsSinceEpoch());
    if (m_dataManager->prepareIntervalToDisplay(todayKey)) {
      m_viewer->setData(m_dataManager->getIntervalToDisplay());
    }
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

bool ChartPane::eventFilter(QObject *watched, QEvent *event) {
  if (event->type() == QEvent::MouseButtonPress) {
    emit activated();
  }
  return QFrame::eventFilter(watched, event);
}

} // namespace deepvaccontrol
