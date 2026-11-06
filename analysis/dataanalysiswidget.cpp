#include "dataanalysiswidget.h"

#include <QDateTime>
#include <QDir>
#include <QRegularExpression>
#include <QHBoxLayout>
#include <QFrame>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>

#include "analysiseditorarea.h"
#include "analysisservices.h"
#include "analysisnotesstore.h"
#include "analysisrunpage.h"
#include "experimentbrowser.h"
#include "collabmanager.h"
#include "constants.h"
#include "experimentreport.h"
#include "dialogs/messagedialog.h"
#include "experimentrecorder.h"
#include "resholder.h"
#include "theme.h"
#include "utils.h"

namespace deepvaccontrol {


DataAnalysisWidget::DataAnalysisWidget(ResHolder *resHolder, QWidget *parent)
    : QWidget{parent}
    , m_resHolder{resHolder}
    , m_library{resHolder->analysis->library()} // shared with the Analysis page and the assistant
    , m_notes{resHolder->analysis->notes()}
    , m_editor{new AnalysisEditorArea(this)} {
  auto *mainLay = new QVBoxLayout(this);
  mainLay->setContentsMargins(0, 0, 0, 0);
  mainLay->setSpacing(4 * utils::ssf());

  // browser: hide button + filter | list | buttons
  m_browser = new QFrame(this);
  m_browser->setObjectName("pageCard");
  auto *browserLay = new QVBoxLayout(m_browser);
  browserLay->setContentsMargins(10 * utils::ssf(), 10 * utils::ssf(), 10 * utils::ssf(), 10 * utils::ssf());
  browserLay->setSpacing(8 * utils::ssf());

  auto *hideBtn = new QPushButton(QStringLiteral("‹"), m_browser);
  hideBtn->setMinimumHeight(40 * utils::ssf());
  hideBtn->setFocusPolicy(Qt::NoFocus);
  hideBtn->setFixedWidth(40 * utils::ssf());
  hideBtn->setToolTip(tr("Hide the experiment list"));

  auto *list = new ExperimentBrowser(m_library, true, m_browser);
  list->addButton(hideBtn);
  connect(list, &ExperimentBrowser::openRequested, this, [this](ExperimentRun const &run, bool splitRight) {
    openRun(run, splitRight);
  });
  browserLay->addWidget(list, 1);

  auto *splitter = new QSplitter(Qt::Horizontal, this);
  splitter->setHandleWidth(10 * utils::ssf());
  splitter->setStyleSheet(QStringLiteral("QSplitter::handle { background: transparent; }"));
  splitter->addWidget(m_browser);
  splitter->addWidget(m_editor);
  splitter->setStretchFactor(0, 0);
  splitter->setStretchFactor(1, 1);
  splitter->setCollapsible(0, false);
  splitter->setCollapsible(1, false);
  splitter->setSizes({static_cast<int>(270 * utils::ssf()), static_cast<int>(1000 * utils::ssf())});

  // thin full-height strip that brings the hidden list back
  auto *showStrip = new QPushButton(QStringLiteral("▶"), this);
  showStrip->setToolTip(tr("Show the experiment list"));
  showStrip->setFocusPolicy(Qt::NoFocus);
  showStrip->setFixedWidth(24 * utils::ssf());
  showStrip->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
  showStrip->setVisible(false);

  auto *bodyLay = new QHBoxLayout();
  bodyLay->setContentsMargins(0, 0, 0, 0);
  bodyLay->setSpacing(0);
  bodyLay->addWidget(showStrip);
  bodyLay->addWidget(splitter, 1);
  mainLay->addLayout(bodyLay, 1);

  connect(hideBtn, &QPushButton::clicked, this, [this, showStrip] {
    m_browser->setVisible(false);
    showStrip->setVisible(true);
  });
  connect(showStrip, &QPushButton::clicked, this, [this, showStrip] {
    showStrip->setVisible(false);
    m_browser->setVisible(true);
  });

}

ExperimentLibrary *DataAnalysisWidget::library() const {
  return m_library;
}

AnalysisNotesStore *DataAnalysisWidget::notes() const {
  return m_notes;
}

void DataAnalysisWidget::ensureScanned() {
  if (!m_library->hasScanned()) {
    m_library->refresh();
  }
}

QString DataAnalysisWidget::showRun(ExperimentRun const &run, QStringList const &channels, double from, double to,
                                    QVector<ExperimentRun> const &compare) {
  openRun(run);
  AnalysisRunPage *page = m_editor->page(run.key);
  return page ? page->applyView(channels, from, to, compare) : tr("Could not open %1").arg(run.title);
}

QString DataAnalysisWidget::writeReport(ExperimentRun const &run, QVector<ExperimentRun> const &compare, bool pdf, QString &path) {
  return m_resHolder->analysis->writeReport(run, compare, pdf, path); // built without a page
}

void DataAnalysisWidget::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);
  ensureScanned(); // lazily: the page may never be opened
}

void DataAnalysisWidget::openRun(ExperimentRun const &run, bool splitRight) {
  if (splitRight ? m_editor->splitPageRight(run.key) : m_editor->focusPage(run.key)) {
    return;
  }
  m_editor->addPage(new AnalysisRunPage(m_resHolder, m_library, m_notes, run), splitRight);
}

}
