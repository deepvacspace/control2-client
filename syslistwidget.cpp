#include "syslistwidget.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QTableWidget>
#include <QVBoxLayout>
#include <cmath>

#include "theme.h"
#include "utils.h"

namespace deepvaccontrol {

namespace {
// "Chamber temperature, °C" -> {"Chamber temperature", "°C"}
std::pair<QString, QString> nameAndUnit(QString const &label) {
  int const comma = static_cast<int>(label.lastIndexOf(','));
  if (comma < 0) {
    return {label, QString()};
  }
  return {label.left(comma).trimmed(), label.mid(comma + 1).trimmed()};
}
} // namespace

SysListWidget::SysListWidget(ResHolder *resHolder, QWidget *parent)
    : QWidget{parent}, m_resHolder{resHolder}, m_states{resHolder->states} {
  double const ssf{utils::ssf()};
  auto        *mainLay = new QHBoxLayout(this);
  mainLay->setContentsMargins(0, 8 * ssf, 0, 0);
  mainLay->setSpacing(10 * ssf);

  auto makeCard = [this, ssf](QString const &title) {
    auto *card = new QFrame(this);
    card->setObjectName("pageCard");
    auto *lay = new QVBoxLayout(card);
    lay->setContentsMargins(14 * ssf, 10 * ssf, 14 * ssf, 12 * ssf);
    lay->setSpacing(6 * ssf);
    auto *label = new QLabel(title, card);
    label->setProperty("muted", true);
    lay->addWidget(label);
    return card;
  };

  // ---- every value the PLC sends
  auto *valuesCard = makeCard(tr("PLC values"));
  m_table          = new QTableWidget(valuesCard);
  m_table->setColumnCount(3);
  m_table->setHorizontalHeaderLabels({tr("Value"), tr("Current"), tr("Unit")});
  m_table->verticalHeader()->setVisible(false);
  m_table->verticalHeader()->setDefaultSectionSize(24 * ssf);
  m_table->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
  m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_table->setSelectionMode(QAbstractItemView::NoSelection);
  m_table->setShowGrid(false);
  m_table->setFocusPolicy(Qt::NoFocus);
  QStringList const stateNames{m_states->getNames()};
  m_table->setRowCount(static_cast<int>(stateNames.size()));
  for (int i{0}; i < stateNames.size(); ++i) {
    auto const [name, unit] = nameAndUnit(m_states->getSysValue(stateNames[i])->getLabel());
    auto *nameItem          = new QTableWidgetItem(name);
    nameItem->setToolTip(stateNames[i]); // PLC variable name
    m_table->setItem(i, 0, nameItem);
    m_table->setItem(i, 1, new QTableWidgetItem(QString()));
    m_table->setItem(i, 2, new QTableWidgetItem(unit.isEmpty() ? QStringLiteral("—") : unit));
    m_table->item(i, 2)->setForeground(theme::colors().muted);
  }
  valuesCard->layout()->addWidget(m_table);
  mainLay->addWidget(valuesCard, 3);

  // ---- outputs configured in "monitor"
  auto *outputsCard = makeCard(tr("Outputs"));
  auto *outputsLay  = static_cast<QVBoxLayout *>(outputsCard->layout());
  for (QVariant const &entry : m_resHolder->settings->settingsMap()["monitor"].toList()) {
    QVariantMap const item = entry.toMap();
    QString           name = item["name"].toString();
    if (name.isEmpty()) {
      name = "outputs";
    }
    if (!m_states->contains(name)) {
      continue;
    }
    auto *row = new QFrame(outputsCard);
    row->setObjectName("outputRow");
    auto *rowLay = new QHBoxLayout(row);
    rowLay->setContentsMargins(10 * ssf, 4 * ssf, 10 * ssf, 4 * ssf);
    rowLay->addWidget(new QLabel(item["label"].toString(), row), 1);
    auto *badge = new QLabel(row);
    badge->setAlignment(Qt::AlignCenter);
    badge->setFixedWidth(40 * ssf);
    rowLay->addWidget(badge);
    outputsLay->addWidget(row);
    m_outputs.push_back({m_states->getSysValue(name), item["bit"].toInt(), badge});
  }
  outputsLay->addStretch();
  mainLay->addWidget(outputsCard, 2);

  connect(m_states, &SystemValueList::ValuesUpdated, this, &SysListWidget::updateList);
  updateList();
}

void SysListWidget::updateList() {
  QStringList const stateNames{m_states->getNames()};
  for (int i{0U}; (i < m_states->size()) && (i < m_table->rowCount()); ++i) {
    QString const stateName{stateNames[i]};
    auto         *state = m_states->getSysValue(stateName);
    float const   floatValue{state->getValue()};
    QString       text;
    if (stateName.startsWith("t_")) {
      text = utils::timeFromSec(static_cast<quint32>(floatValue), true);
    } else if (std::isnan(floatValue)) {
      text = QStringLiteral("--");
    } else {
      text = QString::number(floatValue, 'f', state->getPrec());
    }
    m_table->item(i, 1)->setText(text);
  }

  theme::Colors const &c = theme::colors();
  for (Output const &output : std::as_const(m_outputs)) {
    bool const   on{output.value->getBitOfFloat(static_cast<quint8>(output.bit))};
    QColor const color = on ? c.active : c.off;
    output.badge->setText(on ? tr("ON") : tr("OFF"));
    output.badge->setStyleSheet(QStringLiteral("color: %1; border: %2px solid %1; border-radius: %3px; background: %4; font-weight: bold;")
                                  .arg(color.name())
                                  .arg(qMax(1, static_cast<int>(1 * utils::ssf())))
                                  .arg(static_cast<int>(3 * utils::ssf()))
                                  .arg(QColor(color.red(), color.green(), color.blue(), on ? 40 : 0).name(QColor::HexArgb)));
  }
}
} // namespace deepvaccontrol
