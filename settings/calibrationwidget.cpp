#include "calibrationwidget.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QTableView>

#include "calibrationeditwidget.h"
#include "utils.h"

namespace deepvaccontrol {
CalibrationWidget::CalibrationWidget(
  QVariantMap const &calibrationVariantMap, SystemValue *curSysValue, SystemValue *curRawSysValue, QWidget *parent)
    : QWidget{parent}, m_calibModel{calibrationVariantMap} {
  auto *mainLay = new QHBoxLayout(this);
  mainLay->setContentsMargins(3 * utils::ssf(), 3 * utils::ssf(), 3 * utils::ssf(), 3 * utils::ssf());

  auto *pointEdit = new CalibrationEditWidget(&m_calibModel, curSysValue, curRawSysValue, this);
  mainLay->addWidget(pointEdit);

  auto *tableView = new QTableView(this);
  tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
  tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
  tableView->setSelectionMode(QAbstractItemView::SingleSelection);
  // Disable resizing for columns
  tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  // tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
  // tableView->horizontalHeader()->setStretchLastSection(true);
  // Disable resizing for rows
  tableView->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);

  tableView->setModel(&m_calibModel);

  QPalette palette = tableView->palette();
  palette.setColor(QPalette::Inactive, QPalette::Highlight, palette.color(QPalette::Active, QPalette::Highlight));
  palette.setColor(
    QPalette::Inactive, QPalette::HighlightedText, palette.color(QPalette::Active, QPalette::HighlightedText));
  tableView->setPalette(palette);

  mainLay->addWidget(tableView);

  connect(tableView, &QTableView::clicked, this, [this](QModelIndex const &current) {
    m_calibModel.selectIndex(current.row());
  });

  connect(&m_calibModel, &CalibrationModel::indexSelected, tableView, &QTableView::selectRow);

  if (m_calibModel.rowCount() > 0) {
    m_calibModel.selectIndex(0, /*force*/ true);
  }
}
} // namespace deepvaccontrol
