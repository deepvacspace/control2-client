#include "intervallistview.h"

#include <QEvent>
#include <QHeaderView>
#include <QPainter>
#include <QStyledItemDelegate>

#include "constants.h"
#include "theme.h"
#include "utils.h"

namespace deepvaccontrol {

namespace {
// the selected row gets an accent bar on its left edge
class IntervalRowDelegate : public QStyledItemDelegate {
public:
  using QStyledItemDelegate::QStyledItemDelegate;

  void paint(QPainter *painter, QStyleOptionViewItem const &option, QModelIndex const &index) const override {
    QStyledItemDelegate::paint(painter, option, index);
    if ((index.column() == 0) && (option.state & QStyle::State_Selected)) {
      painter->save();
      painter->fillRect(QRectF(option.rect.left(), option.rect.top(), qMax(2.0, 3 * utils::ssf()), option.rect.height()),
                        theme::colors().accent);
      painter->restore();
    }
  }
};
} // namespace

IntervalListView::IntervalListView(QWidget *parent) : QTableWidget{parent} {
  setObjectName("intervalTable");
  setEditTriggers(QAbstractItemView::NoEditTriggers);
  setSelectionBehavior(QAbstractItemView::SelectRows);
  setSelectionMode(QAbstractItemView::SingleSelection);
  setShowGrid(false);
  setWordWrap(false);
  setTextElideMode(Qt::ElideRight);
  setFocusPolicy(Qt::NoFocus);
  setItemDelegate(new IntervalRowDelegate(this));
  verticalHeader()->setVisible(false);
  verticalHeader()->setDefaultSectionSize(static_cast<int>(30 * utils::ssf()));
  horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  horizontalHeader()->setHighlightSections(false);
  {
    QFont font = horizontalHeader()->font();
    font.setPointSizeF(font.pointSizeF() * 0.85);
    font.setLetterSpacing(QFont::PercentageSpacing, 106);
    horizontalHeader()->setFont(font);
  }

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

void IntervalListView::changeEvent(QEvent *event) {
  QTableWidget::changeEvent(event);
  if (event->type() == QEvent::PaletteChange) {
    rebuild(); // value colours follow the theme
  }
}

void IntervalListView::rebuild() {
  if (!m_model) {
    return;
  }
  theme::Colors const &c      = theme::colors();
  QStringList const    order  = m_model->getParamListOrder();
  QVariantList const   params = m_model->getParamListVariant();
  int const            flagsRow{m_model->getFlagsIndex()};
  int const            durationRow{static_cast<int>(order.indexOf("duration"))};
  QVector<int>         valueRows; // setpoint parameters (temperature, humidity, ...)
  QStringList          headers{QStringLiteral("#"), tr("Label")};
  for (int i{0}; i < order.size(); ++i) {
    if ((i != flagsRow) && (i != durationRow)) {
      valueRows << i;
      headers << ((order.value(i) == QLatin1String("temp")) ? tr("Temp (°C)") : params.value(i).toMap().value("label").toString());
    }
  }
  headers << tr("Duration") << tr("Mode");
  for (QString &header : headers) {
    header = header.toUpper();
  }

  QFont const base = font();
  QFont       bold = base;
  bold.setBold(true);

  int const count{m_model->getStandardModel()->columnCount()};
  m_selecting = true;
  clear();
  setColumnCount(headers.size());
  setHorizontalHeaderLabels(headers);
  setRowCount(count);
  for (int interval{0}; interval < count; ++interval) {
    auto item = [&](int column, QString const &text) {
      auto *cell = new QTableWidgetItem(text);
      setItem(interval, column, cell);
      return cell;
    };
    QStandardItem const *flagsItem = (flagsRow >= 0) ? m_model->getStandardModel()->item(flagsRow, interval) : nullptr;
    quint32 const        flags{flagsItem ? flagsItem->data(Qt::UserRole + 1).toUInt() : 0U};
    item(0, QStringLiteral("%1").arg(interval + 1, 2, 10, QLatin1Char('0')))->setForeground(c.muted);
    QString const label = m_labels.value(interval);
    QTableWidgetItem *labelItem = item(1, label.isEmpty() ? QStringLiteral("—") : label);
    if (label.isEmpty()) {
      labelItem->setForeground(c.muted);
    }
    int column{2};
    for (int row : std::as_const(valueRows)) {
      QVariantMap const    param = params.value(row).toMap();
      QStandardItem const *cell  = m_model->getStandardModel()->item(row, interval);
      bool const           used{utils::getBitOfDword(flags, row)};
      QString              text = QStringLiteral("—");
      if (cell && used) {
        double const value = cell->data(param.value("type").toInt()).toDouble();
        text               = QString::number(value, 'f', param.value("prec", 1).toInt()).replace('-', QChar(0x2212));
      }
      QTableWidgetItem *valueItem = item(column++, text);
      valueItem->setForeground(used ? c.accent : c.muted);
      valueItem->setFont(bold);
    }
    QStandardItem const *durationItem = (durationRow >= 0) ? m_model->getStandardModel()->item(durationRow, interval) : nullptr;
    item(column++, durationItem ? utils::timeFromSec(static_cast<long>(durationItem->data(Qt::UserRole).toDouble())) : QString())
      ->setFont(bold);
    QString const mode = utils::getBitOfDword(flags, RAMP_BIT)
                           ? tr("Ramp over the interval")
                           : (utils::getBitOfDword(flags, AWAIT_BIT) ? tr("Full speed, wait until reached") : tr("Full speed"));
    QTableWidgetItem *modeItem = item(column, mode);
    modeItem->setForeground(c.muted);
  }
  QHeaderView *header = horizontalHeader();
  // label and mode share the free width; setpoints and duration get a fixed column
  header->setSectionResizeMode(QHeaderView::Interactive);
  header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  header->setSectionResizeMode(1, QHeaderView::Stretch);
  header->setSectionResizeMode(columnCount() - 1, QHeaderView::Stretch);
  for (int column{2}; column < columnCount() - 1; ++column) {
    header->resizeSection(column, static_cast<int>(120 * utils::ssf()));
  }
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
