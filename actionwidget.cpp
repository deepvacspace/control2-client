#include "actionwidget.h"

#include <QDateTime>
#include <QEvent>
#include <QFrame>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "constants.h"
#include "theme.h"
#include "utils.h"

namespace deepvaccontrol {
ActionWidget::ActionWidget(ActionManager *actionManager, QWidget *parent)
    : QWidget{parent}, m_actionManager{actionManager} {
  double const ssf{utils::ssf()};
  auto        *mainLay = new QVBoxLayout(this);
  mainLay->setContentsMargins(0, 8 * ssf, 0, 0);

  auto *card = new QFrame(this);
  card->setObjectName("pageCard");
  mainLay->addWidget(card);
  auto *cardLay = new QVBoxLayout(card);
  cardLay->setContentsMargins(14 * ssf, 10 * ssf, 14 * ssf, 12 * ssf);
  cardLay->setSpacing(8 * ssf);

  // header: title, what the colours mean, more entries
  auto *header = new QHBoxLayout();
  auto *title  = new QLabel(tr("Event log"), card);
  title->setObjectName("cardTitle");
  header->addWidget(title);
  header->addStretch();
  auto *moreBtn = new QPushButton(tr("Show more"), card);
  moreBtn->setMinimumSize(110 * ssf, 26 * ssf);
  header->addWidget(moreBtn);
  cardLay->addLayout(header);

  m_table = new QTableWidget(0, 3, card);
  m_table->setObjectName("flatTable");
  m_table->setHorizontalHeaderLabels({tr("Time").toUpper(), tr("Source").toUpper(), tr("Message").toUpper()});
  m_table->verticalHeader()->setVisible(false);
  m_table->verticalHeader()->setDefaultSectionSize(static_cast<int>(28 * ssf));
  m_table->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  m_table->horizontalHeader()->setHighlightSections(false);
  m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
  m_table->horizontalHeader()->resizeSection(1, static_cast<int>(90 * ssf));
  m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
  {
    QFont font = m_table->horizontalHeader()->font();
    font.setPointSizeF(font.pointSizeF() * 0.85);
    m_table->horizontalHeader()->setFont(font);
  }
  m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_table->setSelectionMode(QAbstractItemView::SingleSelection);
  m_table->setShowGrid(false);
  m_table->setWordWrap(false);
  m_table->setFocusPolicy(Qt::NoFocus);
  cardLay->addWidget(m_table, 1);

  connect(moreBtn, &QPushButton::clicked, this, [this]() {
    addElements(m_actionManager->getNext(), true);
  });

  connect(actionManager, &ActionManager::added, this, &ActionWidget::addElements);

  connect(actionManager, &ActionManager::allDisplayed, this, [moreBtn]() {
    moreBtn->setEnabled(false);
  });

  addElements(m_actionManager->getNext(), true);
}

void ActionWidget::changeEvent(QEvent *event) {
  QWidget::changeEvent(event);
  if (event->type() == QEvent::PaletteChange) {
    // source colours follow the theme
    for (int row{0}; row < m_table->rowCount(); ++row) {
      if (QTableWidgetItem *item = m_table->item(row, 1)) {
        item->setForeground(sourceColor(item->data(Qt::UserRole).toUInt()));
      }
      if (QTableWidgetItem *item = m_table->item(row, 0)) {
        item->setForeground(theme::colors().muted);
      }
    }
  }
}

QColor ActionWidget::sourceColor(quint8 type) {
  theme::Colors const &c = theme::colors();
  switch (static_cast<ActionManager::ActionType>(type)) {
    case ActionManager::ActionType::PLC:
      return c.active;
    case ActionManager::ActionType::ERR:
      return c.danger;
    case ActionManager::ActionType::APP:
      return c.accent;
    default:
      return c.warning;
  }
}

void ActionWidget::addElements(QVariantList const &newElements, bool addToBottom) {
  QFont bold = m_table->font();
  bold.setBold(true);
  for (int i = 0; i < newElements.size(); ++i) {
    QVariantMap const actionMap = newElements[i].toMap();

    quint8 const  type{static_cast<quint8>(actionMap["type"].toUInt())};
    QString const msg{actionMap["msg"].toString()};
    quint64 const date{actionMap["date"].toUInt()};

    QString source;
    switch (static_cast<ActionManager::ActionType>(type)) {
      case ActionManager::ActionType::PLC:
        source = tr("PLC");
        break;
      case ActionManager::ActionType::ERR:
        source = tr("Error");
        break;
      case ActionManager::ActionType::APP:
        source = tr("App");
        break;
      default:
        source = tr("Other");
        break;
    }

    int const row = addToBottom ? m_table->rowCount() : 0;
    m_table->insertRow(row);
    auto *timeItem = new QTableWidgetItem(QDateTime::fromSecsSinceEpoch(date).toString("yyyy-MM-dd  HH:mm:ss"));
    timeItem->setForeground(theme::colors().muted);
    auto *sourceItem = new QTableWidgetItem(source);
    sourceItem->setData(Qt::UserRole, type);
    sourceItem->setForeground(sourceColor(type));
    sourceItem->setFont(bold);
    auto *messageItem = new QTableWidgetItem(msg);
    messageItem->setToolTip(msg);
    m_table->setItem(row, 0, timeItem);
    m_table->setItem(row, 1, sourceItem);
    m_table->setItem(row, 2, messageItem);
  }
}
}
