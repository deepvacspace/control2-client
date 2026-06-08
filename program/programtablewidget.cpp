#include "programtablewidget.h"

#include <QHeaderView>
#include <QScrollBar>
#include <QVBoxLayout>

#include "programtabledelegate.h"
#include "utils.h"
namespace control2 {
ProgramTableWidget::ProgramTableWidget(QWidget *parent) : QFrame{parent} {
  auto *mainLay = new QVBoxLayout(this);
  mainLay->setContentsMargins(0, 0, 0, 0);

  m_tableView = new QTableView(this);
  m_tableView->setSelectionBehavior(QAbstractItemView::SelectColumns);
  m_tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_tableView->setSelectionMode(QAbstractItemView::SingleSelection);
  connect(m_tableView, &QTableView::doubleClicked, this, &ProgramTableWidget::doubleClicked);

  mainLay->addWidget(m_tableView);
}

void ProgramTableWidget::setModel(ProgramModel *model) {
  if (model == m_model) {
    return;
  }

  // unconnect?
  m_model = model;

  m_tableView->setModel(m_model->getStandardModel());
  m_tableView->setItemDelegate(new ProgramTableDelegate(model, this));

  QHeaderView *verticalHeader = m_tableView->verticalHeader();
  verticalHeader->setSectionResizeMode(QHeaderView::Fixed);
  verticalHeader->setDefaultSectionSize(10 * utils::ssf());

  QHeaderView *horizontalHeader = m_tableView->horizontalHeader();
  horizontalHeader->setSectionResizeMode(QHeaderView::Fixed);
  horizontalHeader->setDefaultSectionSize(55 * utils::ssf());

  m_tableView->hideRow(m_tableView->model()->rowCount() - 1);

  setStyleSheet(utils::readStyleFromFile(":/styles/programTable.qss"));

  connect(m_tableView->selectionModel(),
          &QItemSelectionModel::selectionChanged,
          this,
          [this](QItemSelection const &selected, QItemSelection const &) {
            verticalResizeTableViewToContents();

            if (selected.size() > 0) {
              m_model->selectInterval(selected.indexes().first().column());
            }
          });

  verticalResizeTableViewToContents();

  connect(m_model, &ProgramModel::intervalSelected, this, &ProgramTableWidget::selectIndex);
  m_tableView->selectColumn(m_model->getSelectedInterval());
}

int ProgramTableWidget::getSelectedIndex() const {
  return 0;
}

void ProgramTableWidget::selectIndex(int index) {
  m_tableView->selectColumn(index);
}

void ProgramTableWidget::verticalResizeTableViewToContents() {
  m_tableView->resizeRowsToContents();
  int rowTotalHeight{0U};

  // Rows height
  int const count{m_tableView->verticalHeader()->count()};
  for (int i{0U}; i < count; ++i) {
    // 2018-03 edit: only account for row if it is visible
    if (!m_tableView->verticalHeader()->isSectionHidden(i)) {
      rowTotalHeight += m_tableView->verticalHeader()->sectionSize(i);
    }
  }

  // add scrollbar always
  rowTotalHeight += 60; // m_tableView->horizontalScrollBar()->height();

  // Check for header visibility
  if (!m_tableView->horizontalHeader()->isHidden()) {
    rowTotalHeight += m_tableView->horizontalHeader()->height();
  }
  m_tableView->setFixedHeight(rowTotalHeight + 2);
}
} // namespace control2
