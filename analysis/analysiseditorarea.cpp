#include "analysiseditorarea.h"

#include <QLabel>
#include <QMenu>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabBar>
#include <QTabWidget>
#include <QVBoxLayout>

#include "analysisrunpage.h"
#include "crosshairsynchub.h"
#include "touchsupport.h"
#include "utils.h"
#include "theme.h"

namespace deepvaccontrol {

namespace {
void equalize(QSplitter *splitter) {
  int const total{splitter->orientation() == Qt::Horizontal ? splitter->width() : splitter->height()};
  QList<int> sizes;
  for (int i{0}; i < splitter->count(); ++i) {
    sizes.push_back(std::max(1, total / splitter->count()));
  }
  splitter->setSizes(sizes);
}

QSplitter *makeSplitter(Qt::Orientation orientation) {
  auto *splitter = new QSplitter(orientation);
  splitter->setHandleWidth(6 * utils::ssf());
  splitter->setChildrenCollapsible(false);
  return splitter;
}
}

AnalysisEditorArea::AnalysisEditorArea(QWidget *parent)
    : QWidget{parent}
    , m_stack{new QStackedWidget(this)}
    , m_root{makeSplitter(Qt::Horizontal)}
    , m_syncHub{new CrosshairSyncHub(this)} {
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(m_stack);

  auto *placeholder = new QLabel(tr("Select an experiment in the list and press \"Open\" (or double-click it).\n"
                                    "Each experiment opens in its own tab; use the ☰ menu of a pane to split it\n"
                                    "and look at several experiments side by side."),
                                 this);
  placeholder->setAlignment(Qt::AlignCenter);
  placeholder->setStyleSheet("color: #5c6472;");
  m_stack->addWidget(placeholder);
  m_stack->addWidget(m_root);

  m_root->addWidget(makeGroup());
  updatePlaceholder();
}

QTabWidget *AnalysisEditorArea::makeGroup() {
  auto *group = new QTabWidget();
  group->setTabsClosable(true);
  group->setMovable(true);
  group->setDocumentMode(true);
  group->setElideMode(Qt::ElideRight);
  group->tabBar()->setStyleSheet(utils::readStyleFromFile(":/styles/tabBar.qss")
                         .arg(28 * utils::ssf())
                         .arg(100 * utils::ssf())
                         .arg(36 * utils::ssf())
                         .arg(static_cast<int>(20 * utils::ssf())));
  group->setMinimumSize(theme::dp(600), theme::dp(450));
  {
    QFont font = group->tabBar()->font();
    font.setPointSizeF(font.pointSizeF() * 0.9); // smaller experiment tabs
    group->tabBar()->setFont(font);
  }

  group->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
  touch::enableLongPress(group->tabBar(), nullptr, true); // press and hold a tab: its menu
  connect(group->tabBar(), &QWidget::customContextMenuRequested, this, [this, group](QPoint const &pos) {
    int const index{group->tabBar()->tabAt(pos)};
    if (index >= 0) {
      m_activeGroup = group;
      showTabMenu(group, index, group->tabBar()->mapToGlobal(pos));
    }
  });
  connect(group, &QTabWidget::tabBarClicked, this, [this, group] {
    m_activeGroup = group;
  });
  connect(group, &QTabWidget::tabCloseRequested, this, [this, group](int index) {
    closePage(group, index);
  });

  m_groups.push_back(group);
  m_activeGroup = group;
  return group;
}

bool AnalysisEditorArea::focusPage(QString const &key) {
  for (QTabWidget *group : std::as_const(m_groups)) {
    for (int i{0}; i < group->count(); ++i) {
      auto *page = qobject_cast<AnalysisRunPage *>(group->widget(i));
      if (page && (page->key() == key)) {
        group->setCurrentIndex(i);
        m_activeGroup = group;
        return true;
      }
    }
  }
  return false;
}

AnalysisRunPage *AnalysisEditorArea::page(QString const &key) const {
  for (QTabWidget *group : m_groups) {
    for (int i{0}; i < group->count(); ++i) {
      auto *candidate = qobject_cast<AnalysisRunPage *>(group->widget(i));
      if (candidate && (candidate->key() == key)) {
        return candidate;
      }
    }
  }
  return nullptr;
}

void AnalysisEditorArea::addPage(AnalysisRunPage *page, bool splitRight) {
  QTabWidget *group = m_groups.contains(m_activeGroup) ? m_activeGroup : m_groups.first();
  if (splitRight && (group->count() > 0)) {
    QTabWidget *target = makeGroup();
    insertGroupNextTo(group, target, Qt::Horizontal);
    group = target;
  }
  page->chart()->setSyncHub(m_syncHub);
  int const index{group->addTab(page, page->tabTitle())};
  group->setTabToolTip(index, page->title());
  group->setCurrentIndex(index);
  updatePlaceholder();
}

bool AnalysisEditorArea::splitPageRight(QString const &key) {
  for (QTabWidget *group : std::as_const(m_groups)) {
    for (int i{0}; i < group->count(); ++i) {
      auto *page = qobject_cast<AnalysisRunPage *>(group->widget(i));
      if (page && (page->key() == key)) {
        if (group->count() > 1) {
          splitPage(group, i, Qt::Horizontal);
        } else {
          group->setCurrentIndex(i); // alone in its pane: already on its own
          m_activeGroup = group;
        }
        return true;
      }
    }
  }
  return false;
}

int AnalysisEditorArea::pageCount() const {
  int count{0};
  for (QTabWidget *group : m_groups) {
    count += group->count();
  }
  return count;
}

void AnalysisEditorArea::showTabMenu(QTabWidget *group, int index, QPoint const &globalPos) {
  QMenu menu(this);

  QAction *syncAction = menu.addAction(tr("Sync crosshair"));
  syncAction->setCheckable(true);
  syncAction->setChecked(m_syncHub->isEnabled());
  connect(syncAction, &QAction::toggled, m_syncHub, &CrosshairSyncHub::setEnabled);

  if ((index < 0) || (index >= group->count())) {
    menu.exec(globalPos);
    return;
  }
  menu.addSeparator();

  menu.addAction(tr("Split right"), this, [this, group, index] {
        splitPage(group, index, Qt::Horizontal);
      })
    ->setEnabled(group->count() > 1);

  if (m_groups.size() > 1) {
    menu.addSeparator();
    for (int g{0}; g < m_groups.size(); ++g) {
      QTabWidget *target = m_groups[g];
      if (target == group) {
        continue;
      }
      menu.addAction(tr("Move to pane %1").arg(g + 1), this, [this, group, index, target] {
        movePage(group, index, target);
      });
    }
  }

  menu.addSeparator();
  menu.addAction(tr("Close"), this, [this, group, index] {
    closePage(group, index);
  });
  menu.addAction(tr("Close others"), this, [this, group, index] {
    QWidget *keep = group->widget(index);
    for (int i = group->count() - 1; i >= 0; --i) {
      if (group->widget(i) != keep) {
        closePage(group, i);
      }
    }
  })->setEnabled(group->count() > 1);

  menu.exec(globalPos);
}

void AnalysisEditorArea::insertGroupNextTo(QTabWidget *existing, QTabWidget *group, Qt::Orientation orientation) {
  auto *parent = qobject_cast<QSplitter *>(existing->parentWidget());
  if (!parent) {
    m_root->addWidget(group);
    return;
  }

  int const index{parent->indexOf(existing)};
  if ((parent->orientation() == orientation) || (parent->count() == 1)) {
    parent->setOrientation(orientation);
    parent->insertWidget(index + 1, group);
    equalize(parent);
    return;
  }

  // nest a splitter with the other orientation in place of the existing pane
  QSplitter *nested = makeSplitter(orientation);
  parent->replaceWidget(index, nested);
  nested->addWidget(existing);
  nested->addWidget(group);
  existing->show();
  group->show();
  equalize(nested);
}

void AnalysisEditorArea::splitPage(QTabWidget *source, int index, Qt::Orientation orientation) {
  if ((index < 0) || (index >= source->count()) || (source->count() < 2)) {
    return;
  }
  QTabWidget *target = makeGroup();
  insertGroupNextTo(source, target, orientation);
  movePage(source, index, target);
}

void AnalysisEditorArea::movePage(QTabWidget *source, int index, QTabWidget *target) {
  QWidget      *page    = source->widget(index);
  QString const text    = source->tabText(index);
  QString const toolTip = source->tabToolTip(index);
  source->removeTab(index);

  int const newIndex{target->addTab(page, text)};
  target->setTabToolTip(newIndex, toolTip);
  target->setCurrentIndex(newIndex);
  m_activeGroup = target;

  removeGroupIfEmpty(source);
}

void AnalysisEditorArea::closePage(QTabWidget *group, int index) {
  QWidget *page = group->widget(index);
  group->removeTab(index);
  if (page) {
    page->deleteLater();
  }
  removeGroupIfEmpty(group);
  updatePlaceholder();
}

void AnalysisEditorArea::removeGroupIfEmpty(QTabWidget *group) {
  if ((group->count() > 0) || (m_groups.size() <= 1)) {
    return;
  }

  m_groups.removeAll(group);
  if (m_activeGroup == group) {
    m_activeGroup = m_groups.first();
  }

  auto *parent = qobject_cast<QSplitter *>(group->parentWidget());
  group->hide();
  group->setParent(nullptr);
  group->deleteLater();

  // collapse splitters left with a single child
  while (parent && (parent != m_root) && (parent->count() == 1)) {
    QWidget *child = parent->widget(0);
    auto    *grand = qobject_cast<QSplitter *>(parent->parentWidget());
    if (!grand) {
      break;
    }
    grand->replaceWidget(grand->indexOf(parent), child);
    child->show();
    parent->deleteLater();
    parent = grand;
  }

  if (m_root->count() == 1) {
    if (auto *only = qobject_cast<QSplitter *>(m_root->widget(0))) {
      m_root->setOrientation(only->orientation());
      while (only->count() > 0) {
        QWidget *child = only->widget(0);
        m_root->addWidget(child);
        child->show();
      }
      only->setParent(nullptr);
      only->deleteLater();
      equalize(m_root);
    }
  }
}

void AnalysisEditorArea::updatePlaceholder() {
  m_stack->setCurrentIndex(pageCount() > 0 ? 1 : 0);
}

}
