#include "programstepper.h"

#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>
#include <QVBoxLayout>

#include "theme.h"
#include "touchsupport.h"
#include "utils.h"

namespace deepvaccontrol {

ProgramStepper::ProgramStepper(QWidget *parent) : QWidget{parent} {
  auto *outer = new QVBoxLayout(this);
  outer->setContentsMargins(0, 0, 0, 0);
  // the steps scroll (a long list of intervals); the interval list keeps its open card in sight in this area
  auto *area = new QScrollArea(this);
  area->setWidgetResizable(true);
  area->setFrameShape(QFrame::NoFrame);
  area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  area->viewport()->setAutoFillBackground(false);
  area->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; }"));
  auto *column = new QWidget(area);
  column->setAutoFillBackground(false);
  m_lay = new QVBoxLayout(column);
  m_lay->setContentsMargins(0, 0, 0, 0);
  m_lay->setSpacing(theme::gap());
  m_lay->addStretch();
  area->setWidget(column);
  touch::enableScrolling(area);
  outer->addWidget(area);
  // the gap to what is beside the steps (the profile): the scroll bar's lane when it shows, its handle in the middle;
  // otherwise a margin as wide
  m_scrollBar = area->verticalScrollBar();
  m_scrollBar->setStyleSheet(QStringLiteral("QScrollBar:vertical { width: %1px; margin: 0 %2px; }").arg(theme::gap()).arg(theme::dp(4)));
  m_scrollBar->installEventFilter(this);
  outer->setContentsMargins(0, 0, theme::gap(), 0);
}

int ProgramStepper::addStep(QString const &title, QWidget *content) {
  int const index{static_cast<int>(m_steps.size())};
  Step      step;
  step.card = new QFrame(this);
  step.card->setObjectName("stepCard");
  auto *cardLay = new QVBoxLayout(step.card);
  cardLay->setContentsMargins(theme::dp(28), theme::dp(20), theme::dp(28), theme::dp(24));
  cardLay->setSpacing(theme::dp(20));

  // the head: number, title and summary (a tap opens a step done again), the header widget on the right
  step.head = new QFrame(step.card);
  step.head->setProperty("step", index);
  step.head->installEventFilter(this);
  auto *headLay = new QHBoxLayout(step.head);
  headLay->setContentsMargins(0, 0, 0, 0);
  headLay->setSpacing(theme::dp(20));
  step.number = new QLabel(QString::number(index + 1), step.head);
  step.number->setObjectName("stepNumber");
  step.number->setAlignment(Qt::AlignCenter);
  step.number->setFixedSize(theme::dp(48), theme::dp(48));
  auto *texts = new QVBoxLayout();
  texts->setSpacing(0);
  step.title = new QLabel(title, step.head);
  QFont titleFont = step.title->font();
  titleFont.setBold(true);
  titleFont.setPointSizeF(titleFont.pointSizeF() * 1.1);
  step.title->setFont(titleFont);
  step.summary = new QLabel(step.head);
  step.summary->setProperty("muted", true);
  step.summary->setWordWrap(true);
  texts->addWidget(step.title);
  texts->addWidget(step.summary);
  headLay->addWidget(step.number, 0, Qt::AlignTop);
  headLay->addLayout(texts, 1);
  cardLay->addWidget(step.head);

  // the body: the content, then Back · Next
  step.body    = new QWidget(step.card);
  auto *bodyLay = new QVBoxLayout(step.body);
  bodyLay->setContentsMargins(0, 0, 0, 0);
  bodyLay->setSpacing(theme::dp(24));
  content->setParent(step.body);
  bodyLay->addWidget(content);
  auto *buttons = new QHBoxLayout();
  buttons->setSpacing(theme::dp(16));
  step.back = new QPushButton(tr("Back"), step.body);
  step.next = new QPushButton(tr("Next"), step.body);
  step.next->setObjectName("primaryButton");
  step.why = new QLabel(step.body);
  step.why->setProperty("muted", true);
  step.why->setWordWrap(true);
  for (QPushButton *button : {step.back, step.next}) {
    button->setFixedHeight(theme::dp(60));
    button->setMinimumWidth(theme::dp(168));
    button->setFocusPolicy(Qt::NoFocus);
  }
  buttons->addWidget(step.back);
  buttons->addWidget(step.why, 1);
  buttons->addWidget(step.next);
  bodyLay->addLayout(buttons);
  cardLay->addWidget(step.body);

  connect(step.back, &QPushButton::clicked, this, [this, index] {
    if (!m_locked && (index > 0)) {
      setCurrent(index - 1);
    }
  });
  connect(step.next, &QPushButton::clicked, this, [this, index] { emit nextRequested(index); });

  m_lay->insertWidget(m_lay->count() - 1, step.card); // before the stretch
  m_steps.push_back(step);
  refresh();
  restyle();
  return index;
}

void ProgramStepper::setHeaderWidget(int step, QWidget *widget) {
  if ((step < 0) || (step >= m_steps.size())) {
    return;
  }
  m_steps[step].headerWidget = widget;
  static_cast<QHBoxLayout *>(m_steps[step].head->layout())->addWidget(widget, 0, Qt::AlignTop);
  refresh();
}

void ProgramStepper::setSummary(int step, QString const &text) {
  if ((step >= 0) && (step < m_steps.size()) && (m_steps[step].summary->text() != text)) {
    m_steps[step].summary->setText(text);
    refresh();
  }
}

void ProgramStepper::setNextEnabled(int step, bool enabled, QString const &why) {
  if ((step >= 0) && (step < m_steps.size())) {
    m_steps[step].next->setEnabled(enabled);
    m_steps[step].why->setText(enabled ? QString{} : why);
  }
}

void ProgramStepper::setCurrent(int step) {
  if ((step < 0) || (step >= m_steps.size()) || (step == m_current)) {
    return;
  }
  m_current = step;
  refresh();
  emit currentChanged(m_current);
}

void ProgramStepper::setLocked(bool locked) {
  if (locked != m_locked) {
    m_locked = locked;
    refresh();
  }
}

void ProgramStepper::refresh() {
  int const last{static_cast<int>(m_steps.size()) - 1};
  for (int i{0}; i < m_steps.size(); ++i) {
    Step const &step = m_steps[i];
    bool const  open{i == m_current};
    bool const  done{i < m_current};
    step.body->setVisible(open);
    step.summary->setVisible(!open && !step.summary->text().isEmpty());
    if (step.headerWidget) {
      step.headerWidget->setVisible(open);
    }
    step.number->setText(done ? QStringLiteral("✓") : QString::number(i + 1));
    step.head->setCursor((done && !m_locked) ? Qt::PointingHandCursor : Qt::ArrowCursor);
    step.back->setVisible(i > 0);
    step.back->setEnabled(!m_locked);
    step.next->setVisible(i < last);
    step.card->setProperty("state", open ? "open" : (done ? "done" : "todo"));
    for (QWidget *widget : {static_cast<QWidget *>(step.card), static_cast<QWidget *>(step.number), static_cast<QWidget *>(step.title)}) {
      widget->style()->unpolish(widget);
      widget->style()->polish(widget);
    }
  }
}

void ProgramStepper::restyle() {
  static bool busy{false}; // style sheet changes may raise a palette change again
  if (busy) {
    return;
  }
  busy                   = true;
  theme::Colors const &c = theme::colors();
  setStyleSheet(QStringLiteral("QFrame#stepCard { background: %1; border: %3px solid %2; border-radius: %4px; }"
                               "QFrame#stepCard[state=\"open\"] { border: %5px solid %6; }"
                               "QLabel#stepNumber { background: %7; color: %8; border-radius: %9px; font-weight: bold; }"
                               "QFrame#stepCard[state=\"open\"] QLabel#stepNumber { background: %6; color: white; }"
                               "QFrame#stepCard[state=\"done\"] QLabel#stepNumber { background: %10; color: white; }"
                               "QFrame#stepCard[state=\"todo\"] QLabel { color: %8; }")
                  .arg(c.panel.name(), c.border.name())
                  .arg(theme::dp(1))
                  .arg(theme::radius())
                  .arg(theme::dp(3))
                  .arg(c.accent.name(), c.panelAlt.name(), c.muted.name())
                  .arg(theme::radius())
                  .arg(c.active.name()));
  busy = false;
}

bool ProgramStepper::eventFilter(QObject *watched, QEvent *event) {
  if ((watched == m_scrollBar) && ((event->type() == QEvent::Show) || (event->type() == QEvent::Hide))) {
    layout()->setContentsMargins(0, 0, m_scrollBar->isVisibleTo(this) ? 0 : theme::gap(), 0);
    return false;
  }
  if (auto *head = qobject_cast<QFrame *>(watched); head && head->property("step").isValid()) {
    if (event->type() == QEvent::MouseButtonPress) {
      m_pressAt = static_cast<QMouseEvent *>(event)->globalPosition().toPoint();
    } else if (event->type() == QEvent::MouseButtonRelease) {
      QPoint const moved = static_cast<QMouseEvent *>(event)->globalPosition().toPoint() - m_pressAt;
      int const    step{head->property("step").toInt()};
      if ((moved.manhattanLength() < theme::dp(22)) && !m_locked && (step < m_current)) { // a tap on a step done
        setCurrent(step);
      }
    }
  }
  return QWidget::eventFilter(watched, event);
}

void ProgramStepper::changeEvent(QEvent *event) {
  QWidget::changeEvent(event);
  if (event->type() == QEvent::PaletteChange) {
    restyle();
  }
}

}
