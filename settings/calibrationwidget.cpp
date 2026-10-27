#include "calibrationwidget.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QTableView>

#include "calibrationeditwidget.h"
#include "qcustomplot.h"
#include "theme.h"
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

  // the correction per measured value: one straight section between neighbouring points
  auto *chartCard = makeCard(tr("Correction by section"));
  auto *plot      = new QCustomPlot(chartCard);
  plot->setMinimumHeight(static_cast<int>(200 * ssf));
  plot->setInteractions({});
  plot->xAxis->setLabel(tr("Measured value"));
  plot->yAxis->setLabel(tr("Correction"));
  QCPGraph *sections = plot->addGraph();
  sections->setLineStyle(QCPGraph::lsLine);
  QCPGraph *selected = plot->addGraph();
  selected->setLineStyle(QCPGraph::lsNone);
  auto *zero = new QCPItemStraightLine(plot);
  zero->point1->setCoords(0, 0);
  zero->point2->setCoords(1, 0);
  plot->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  static_cast<QVBoxLayout *>(chartCard->layout())->addWidget(plot, 1); // the title stays on top
  auto redraw = [this, plot, sections, selected, zero] {
    theme::stylePlot(plot);
    theme::Colors const &c = theme::colors();
    sections->setPen(QPen(c.accent, 2 * utils::ssf()));
    sections->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssCircle, QPen(c.accent, 1.5 * utils::ssf()), QBrush(c.panel),
                                              7 * utils::ssf()));
    selected->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssCircle, QPen(c.heat, 2 * utils::ssf()), QBrush(c.heat), 9 * utils::ssf()));
    zero->setPen(QPen(c.muted, 1, Qt::DashLine));
    // the points in order of the measured value
    QVector<QPair<double, double>> points;
    for (int row{0}; row < m_calibModel.rowCount(); ++row) {
      points.push_back({m_calibModel.item(row, 0)->data(Qt::DisplayRole).toDouble(), m_calibModel.item(row, 2)->data(Qt::DisplayRole).toDouble()});
    }
    std::sort(points.begin(), points.end());
    QVector<double> xs;
    QVector<double> ys;
    for (auto const &[x, y] : points) {
      xs << x;
      ys << y;
    }
    sections->setData(xs, ys, true);
    int const chosen{m_calibModel.getSelectedIndex()};
    if ((chosen >= 0) && (chosen < m_calibModel.rowCount())) {
      selected->setData({m_calibModel.item(chosen, 0)->data(Qt::DisplayRole).toDouble()},
                        {m_calibModel.item(chosen, 2)->data(Qt::DisplayRole).toDouble()});
    } else {
      selected->data()->clear();
    }
    plot->rescaleAxes();
    plot->xAxis->scaleRange(1.1, plot->xAxis->range().center());
    QCPRange values = plot->yAxis->range();
    values.expand(0.0); // the zero line in view
    plot->yAxis->setRange(values);
    plot->yAxis->scaleRange(1.25, plot->yAxis->range().center());
    plot->replot();
  };
  connect(&m_calibModel, &CalibrationModel::sizeChanged, plot, redraw);
  connect(&m_calibModel, &QStandardItemModel::dataChanged, plot, redraw);
  connect(&m_calibModel, &CalibrationModel::indexSelected, plot, redraw);
  redraw();

  auto *right = new QVBoxLayout();
  right->setSpacing(10 * ssf);
  right->addWidget(tableCard, 3);
  right->addWidget(chartCard, 2);
  mainLay->addLayout(right, 5);

  connect(tableView, &QTableView::clicked, this, [this](QModelIndex const &current) {
    m_calibModel.selectIndex(current.row());
  });

  connect(&m_calibModel, &CalibrationModel::indexSelected, tableView, &QTableView::selectRow);

  if (m_calibModel.rowCount() > 0) {
    m_calibModel.selectIndex(0, /*force*/ true);
  }
}
}
