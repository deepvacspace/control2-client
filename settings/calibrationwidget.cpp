#include "calibrationwidget.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QTableView>

#include "calibrationeditwidget.h"
#include "utils.h"

namespace deepvaccontrol {
CalibrationWidget::CalibrationWidget(
  QVariantMap const &calibrationVariantMap, SystemValue *curSysValue, SystemValue *curRawSysValue, QWidget *parent)
    : QWidget{parent}, m_calibModel{calibrationVariantMap} {
  double const ssf{utils::ssf()};
  auto        *mainLay = new QHBoxLayout(this);
  mainLay->setContentsMargins(0, 8 * ssf, 0, 0);
  mainLay->setSpacing(10 * ssf);

  auto makeCard = [this, ssf](QString const &title) {
    auto *card = new QFrame(this);
    card->setObjectName("pageCard");
    auto *lay = new QVBoxLayout(card);
    lay->setContentsMargins(14 * ssf, 10 * ssf, 14 * ssf, 12 * ssf);
    lay->setSpacing(8 * ssf);
    auto *label = new QLabel(title, card);
    label->setObjectName("cardTitle");
    lay->addWidget(label);
    return card;
  };

  // selected point and live readings
  auto *editCard = makeCard(tr("Calibration point"));
  auto *pointEdit = new CalibrationEditWidget(&m_calibModel, curSysValue, curRawSysValue, editCard);
  editCard->layout()->addWidget(pointEdit);
  mainLay->addWidget(editCard, 4);

  // all points
  auto *tableCard = makeCard(tr("Calibration points"));
  auto *tableView = new QTableView(tableCard);
  tableView->setObjectName("flatTable");
  tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
  tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
  tableView->setSelectionMode(QAbstractItemView::SingleSelection);
  tableView->setShowGrid(false);
  tableView->setFocusPolicy(Qt::NoFocus);
  tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  tableView->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  tableView->horizontalHeader()->setHighlightSections(false);
  tableView->verticalHeader()->setVisible(false);
  tableView->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
  tableView->verticalHeader()->setDefaultSectionSize(static_cast<int>(28 * ssf));

  tableView->setModel(&m_calibModel);
  tableCard->layout()->addWidget(tableView);
  mainLay->addWidget(tableCard, 5);

  connect(tableView, &QTableView::clicked, this, [this](QModelIndex const &current) {
    m_calibModel.selectIndex(current.row());
  });

  connect(&m_calibModel, &CalibrationModel::indexSelected, tableView, &QTableView::selectRow);

  if (m_calibModel.rowCount() > 0) {
    m_calibModel.selectIndex(0, /*force*/ true);
  }
}
} // namespace deepvaccontrol
