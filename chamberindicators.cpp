#include "chamberindicators.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "accessmanager.h"
#include "chamberview.h"
#include "resholder.h"
#include "theme.h"
#include "utils.h"

namespace deepvaccontrol {
namespace {
int px(double value) {
  return static_cast<int>(value * utils::ssf());
}
}

ChamberIndicators::ChamberIndicators(ResHolder *resHolder, ChamberView *view, QWidget *parent)
    : QFrame{parent}, m_resHolder{resHolder}, m_view{view} {
  setObjectName("indicatorList");
  auto *lay = new QVBoxLayout(this);
  lay->setContentsMargins(0, 0, 0, 0);
  lay->setSpacing(px(6));

  for (ChamberView::Indicator const &indicator : m_view->indicators()) {
    Row row;
    row.iconKind = indicator.icon;
    row.frame    = new QFrame(this);
    row.frame->setObjectName("indicatorRow");
    auto *rowLay = new QHBoxLayout(row.frame);
    rowLay->setContentsMargins(px(12), px(8), px(10), px(8));
    rowLay->setSpacing(px(10));
    row.icon  = new QLabel(row.frame);
    row.title = new QLabel(indicator.title, row.frame);
    row.title->setObjectName("indicatorTitle");
    row.note = new QLabel(row.frame);
    row.note->setWordWrap(true);
    row.note->setVisible(false);
    auto *texts = new QVBoxLayout();
    texts->setSpacing(0);
    texts->addWidget(row.title);
    texts->addWidget(row.note);
    row.status = new QLabel(row.frame);
    row.status->setAlignment(Qt::AlignCenter);
    row.status->setMinimumWidth(px(84));
    rowLay->addWidget(row.icon);
    rowLay->addLayout(texts, 1);
    rowLay->addWidget(row.status);
    if (indicator.key != QLatin1String("plc")) { // the PLC is what the application talks to: always there
      row.fit = new QPushButton(row.frame);
      row.fit->setFixedSize(px(96), px(28));
      row.fit->setFocusPolicy(Qt::NoFocus);
      QString const key = indicator.key;
      connect(row.fit, &QPushButton::clicked, this, [this, key] {
        for (ChamberView::Indicator const &now : m_view->indicators()) {
          if (now.key == key) {
            ChamberView::setFittedOverride(key, !now.fitted); // the other way round, for every diagram
          }
        }
      });
      rowLay->addWidget(row.fit);
    } else {
      rowLay->addSpacing(px(96) + rowLay->spacing());
    }
    lay->addWidget(row.frame);
    m_rows.insert(indicator.key, row);
  }

  m_resetBtn = new QPushButton(tr("Reset to detected"), this);
  m_resetBtn->setProperty("link", true);
  m_resetBtn->setToolTip(tr("Show every component as the PLC reports it (or the settings, without a report)"));
  connect(m_resetBtn, &QPushButton::clicked, this, [] { ChamberView::clearFittedOverrides(); });
  lay->addWidget(m_resetBtn, 0, Qt::AlignLeft);
  lay->addStretch();

  theme::Colors const &c = theme::colors();
  setStyleSheet(QStringLiteral("QFrame#indicatorRow { background: %1; border: 1px solid %2; border-radius: %3px; }"
                               "QLabel#indicatorTitle { font-weight: bold; }")
                  .arg(c.panel.name(), c.border.name())
                  .arg(px(8)));
  connect(m_view, &ChamberView::refreshed, this, &ChamberIndicators::refresh);
  connect(m_resHolder->users, &AccessManager::userSelected, this, &ChamberIndicators::refresh);
  refresh();
}

void ChamberIndicators::refresh() {
  theme::Colors const &c = theme::colors();
  bool                 anyOverride{false};
  // only a Service Engineer says what the chamber has
  bool const engineer{m_resHolder->users && (m_resHolder->users->getCurrentAccessLevel() == AccessManager::AccessLevel::USER_ADMIN_3)};
  for (ChamberView::Indicator const &indicator : m_view->indicators()) {
    auto it = m_rows.find(indicator.key);
    if (it == m_rows.end()) {
      continue;
    }
    Row &row = it.value();
    anyOverride = anyOverride || indicator.overridden;
    theme::setThemedIcon(row.icon, row.iconKind, px(22), indicator.fitted ? QStringLiteral("text") : QStringLiteral("muted"));
    row.title->setStyleSheet(QStringLiteral("color: %1;").arg((indicator.fitted ? c.text : c.muted).name()));
    // the state now: a pill, green while on, grey while ready, a dash when not fitted
    QString pill;
    if (!indicator.fitted || indicator.status == QStringLiteral("—")) {
      pill = QStringLiteral("color: %1; background: transparent;").arg(c.muted.name());
    } else if (indicator.state == ChamberView::State::ACTIVE) {
      pill = QStringLiteral("color: %1; background: %2; border-radius: %3px; padding: %4px %5px;")
               .arg(c.active.name(), QColor(c.active.red(), c.active.green(), c.active.blue(), 40).name(QColor::HexArgb))
               .arg(px(6))
               .arg(px(3))
               .arg(px(10));
    } else {
      pill = QStringLiteral("color: %1; background: %2; border-radius: %3px; padding: %4px %5px;")
               .arg(c.muted.name(), c.border.name())
               .arg(px(6))
               .arg(px(3))
               .arg(px(10));
    }
    row.note->setText(QStringLiteral("⚠ %1").arg(indicator.note));
    row.note->setStyleSheet(QStringLiteral("color: %1;").arg(c.warning.name()));
    row.note->setVisible(!indicator.note.isEmpty());
    row.status->setText(indicator.status);
    row.status->setStyleSheet(pill);
    if (row.fit) {
      row.fit->setText(indicator.fitted ? tr("Fitted") : tr("Not fitted"));
      row.fit->setVisible(engineer); // only a Service Engineer may change it
      row.fit->setToolTip(!engineer ? tr("Only a Service Engineer can change what the chamber has")
                          : indicator.overridden
                            ? tr("Set by a Service Engineer for this chamber; Reset to detected undoes it")
                            : tr("As the PLC reports it (or the settings, without a report); tap to change it for this chamber"));
      // a hand-set state stands out
      row.fit->setStyleSheet(indicator.overridden ? QStringLiteral("font-weight: bold; border: 1px solid %1;").arg(c.accent.name()) : QString{});
    }
  }
  m_resetBtn->setVisible(anyOverride && engineer);
}

}
