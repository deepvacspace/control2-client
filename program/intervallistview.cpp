#include "intervallistview.h"

#include <QHeaderView>

#include "constants.h"
#include "utils.h"

namespace deepvaccontrol {

IntervalListView::IntervalListView(QWidget *parent) : QTableWidget{parent} {
  setEditTriggers(QAbstractItemView::NoEditTriggers);
  setSelectionBehavior(QAbstractItemView::SelectRows);
  setSelectionMode(QAbstractItemView::SingleSelection);
  setShowGrid(false);
  setFocusPolicy(Qt::NoFocus);
  verticalHeader()->setVisible(false);
  verticalHeader()->setDefaultSectionSize(static_cast<int>(22 * utils::ssf()));
  horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  horizontalHeader()->setHighlightSections(false);

  connect(this, &QTableWidget::itemSelectionChanged, this, [this] {
    if (m_selecting || !m_model) {
      return;
    }
    QList<QTableWidgetItem *> const selected = selectedItems();
    if (!selected.isEmpty()) {
      m_model->selectInterval(selected.first()->row());
    }
  });
}

void IntervalListView::setModel(ProgramModel *model) {
  if (m_model) {
    disconnect(m_model, nullptr, this, nullptr);
  }
  m_model = model;
  if (m_model) {
    connect(m_model, &ProgramModel::numberOfIntervalChanged, this, &IntervalListView::rebuild);
    connect(m_model, &ProgramModel::modelChanged, this, &IntervalListView::rebuild);
    connect(m_model, &ProgramModel::intervalSelected, this, &IntervalListView::selectRow);
    connect(m_model->getStandardModel(), &QStandardItemModel::dataChanged, this, &IntervalListView::rebuild);
  }
  rebuild();
}

void IntervalListView::setLabels(QStringList const &labels) {
  m_labels = labels;
  rebuild();
}

void IntervalListView::rebuild() {
  if (!m_model) {
    return;
  }
  QStringList const  order  = m_model->getParamListOrder();
  QVariantList const params = m_model->getParamListVariant();
  int const          flagsRow{m_model->getFlagsIndex()};
  int const          durationRow{static_cast<int>(order.indexOf("duration"))};
  QVector<int>       valueRows; // setpoint parameters (temperature, humidity, ...)
  QStringList        headers{QStringLiteral("#"), tr("Label")};
  for (int i{0}; i < order.size(); ++i) {
    if ((i != flagsRow) && (i != durationRow)) {
      valueRows << i;
      headers << params.value(i).toMap().value("label").toString();
    }
  }
  headers << tr("Duration") << tr("Mode");

  int const count{m_model->getStandardModel()->columnCount()};
  m_selecting = true;
  clear();
  setColumnCount(headers.size());
  setHorizontalHeaderLabels(headers);
  setRowCount(count);
  for (int interval{0}; interval < count; ++interval) {
    auto item = [&](int column, QString const &text) { setItem(interval, column, new QTableWidgetItem(text)); };
    QStandardItem const *flagsItem = (flagsRow >= 0) ? m_model->getStandardModel()->item(flagsRow, interval) : nullptr;
    quint32 const        flags{flagsItem ? flagsItem->data(Qt::UserRole + 1).toUInt() : 0U};
    item(0, QString::number(interval + 1));
    item(1, m_labels.value(interval));
    int column{2};
    for (int row : std::as_const(valueRows)) {
      QVariantMap const param = params.value(row).toMap();
      QStandardItem const *cell = m_model->getStandardModel()->item(row, interval);
      bool const           used{utils::getBitOfDword(flags, row)};
      QString              text = QStringLiteral("—");
      if (cell && used) {
        double const value = cell->data(param.value("type").toInt()).toDouble();
        text               = QString::number(value, 'f', param.value("prec", 1).toInt()).replace('-', QChar(0x2212));
        if (order.value(row) == QLatin1String("temp")) {
          text += QStringLiteral(" °C");
        }
      }
      item(column++, text);
    }
    QStandardItem const *durationItem = (durationRow >= 0) ? m_model->getStandardModel()->item(durationRow, interval) : nullptr;
    item(column++, durationItem ? utils::timeFromSec(static_cast<long>(durationItem->data(Qt::UserRole).toDouble())) : QString());
    QString const mode = utils::getBitOfDword(flags, RAMP_BIT)
                           ? tr("Ramp over the interval")
                           : (utils::getBitOfDword(flags, AWAIT_BIT) ? tr("Full speed, wait until reached") : tr("Full speed"));
    item(column, mode);
  }
  QHeaderView *header = horizontalHeader();
  header->setSectionResizeMode(QHeaderView::ResizeToContents);
  header->setSectionResizeMode(1, QHeaderView::Stretch);
  header->setSectionResizeMode(columnCount() - 1, QHeaderView::Stretch);
  m_selecting = false;
  selectRow(m_model->getSelectedInterval());
}

void IntervalListView::selectRow(int interval) {
  if ((interval < 0) || (interval >= rowCount())) {
    return;
  }
  m_selecting = true;
  QTableWidget::selectRow(interval);
  m_selecting = false;
}

} // namespace deepvaccontrol
