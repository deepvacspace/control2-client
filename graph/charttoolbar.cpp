#include "charttoolbar.h"

#include <QAction>
#include <QButtonGroup>
#include <QCoreApplication>
#include <QGraphicsOpacityEffect>
#include <QGridLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "chartsidebar.h"
#include "theme.h"
#include "utils.h"

namespace deepvaccontrol {

namespace {
struct ToolInfo {
  char const *icon;
  char const *name;
  char const *tip;
};

ToolInfo info(ChartToolbar::Tool tool) {
  switch (tool) {
    case ChartToolbar::Tool::BOX_ZOOM:
      return {"boxzoom", QT_TRANSLATE_NOOP("deepvaccontrol::ChartToolbar", "Box zoom"),
              QT_TRANSLATE_NOOP("deepvaccontrol::ChartToolbar", "Box zoom: drag a box over the chart to zoom into it")};
    case ChartToolbar::Tool::ZOOM:
      return {"zoom", QT_TRANSLATE_NOOP("deepvaccontrol::ChartToolbar", "Zoom"),
              QT_TRANSLATE_NOOP("deepvaccontrol::ChartToolbar", "Zoom: buttons to zoom the time and the values in and out")};
    case ChartToolbar::Tool::PAN:
      return {"pan", QT_TRANSLATE_NOOP("deepvaccontrol::ChartToolbar", "Pan"),
              QT_TRANSLATE_NOOP("deepvaccontrol::ChartToolbar", "Pan: buttons to move the view earlier, later, up or down")};
    case ChartToolbar::Tool::ANNOTATE:
      return {"annotate", QT_TRANSLATE_NOOP("deepvaccontrol::ChartToolbar", "Annotate"),
              QT_TRANSLATE_NOOP("deepvaccontrol::ChartToolbar", "Annotate: drag across the chart to mark and label a time range")};
    case ChartToolbar::Tool::INSPECT:
    default:
      return {"pointer", QT_TRANSLATE_NOOP("deepvaccontrol::ChartToolbar", "Inspect"),
              QT_TRANSLATE_NOOP("deepvaccontrol::ChartToolbar", "Inspect: drag to move the view; the tooltip follows the pointer")};
  }
}

QString translated(char const *text) {
  return QCoreApplication::translate("deepvaccontrol::ChartToolbar", text);
}
}

ChartToolbar::ChartToolbar(QWidget *parent) : QFrame{parent}, m_collapseTimer{new QTimer(this)} {
  setObjectName("chartToolbar");
  setAttribute(Qt::WA_StyledBackground);
  auto *lay = new QVBoxLayout(this);
  lay->setContentsMargins(theme::dp(4), theme::dp(4), theme::dp(4), theme::dp(4));
  lay->setSpacing(theme::dp(4));

  // the handle: the tool in use; a tap opens the tools below it
  m_handle = new QToolButton(this);
  m_handle->setObjectName("toolHandle");
  m_handle->setFocusPolicy(Qt::NoFocus);
  m_handle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  // small while it waits: it stands over the value axis' labels
  m_handle->setIconSize(QSize(theme::dp(26), theme::dp(26)));
  m_handle->setMinimumSize(theme::dp(56), theme::dp(48));
  m_handle->setStyleSheet(QStringLiteral("QToolButton#toolHandle { padding: 0 %1px; }").arg(theme::dp(5)));
  m_handle->installEventFilter(this); // a tap opens the tools, a drag moves them
  lay->addWidget(m_handle);

  m_tools      = new QWidget(this);
  auto *column = new QVBoxLayout(m_tools);
  column->setContentsMargins(0, 0, 0, 0);
  column->setSpacing(theme::dp(4));
  lay->addWidget(m_tools);

  m_toolGroup = new QButtonGroup(this);
  m_toolGroup->setExclusive(true);
  for (Tool const tool : {Tool::INSPECT, Tool::BOX_ZOOM, Tool::ZOOM, Tool::PAN}) {
    ToolInfo const t      = info(tool);
    QToolButton   *button = addButton(QString::fromLatin1(t.icon), translated(t.name), translated(t.tip), true);
    m_toolGroup->addButton(button, static_cast<int>(tool));
    m_toolButtons.insert(tool, button);
  }
  QToolButton *fitBtn = addButton(QStringLiteral("fit"), tr("Fit"), tr("Fit: the whole data, following new values"), false);
  addSeparator();
  {
    ToolInfo const t      = info(Tool::ANNOTATE);
    QToolButton   *button = addButton(QString::fromLatin1(t.icon), translated(t.name), translated(t.tip), true);
    m_toolGroup->addButton(button, static_cast<int>(Tool::ANNOTATE));
    m_toolButtons.insert(Tool::ANNOTATE, button);
  }
  addSeparator();
  m_labelsBtn  = addButton(QStringLiteral("labels"), tr("Labels"), tr("Labels: the box with the line names and colours"), true);
  m_tooltipBtn = addButton(QStringLiteral("tooltip"), tr("Tooltip"), tr("Tooltip: the values where the pointer is"), true);
  addSeparator();
  m_pinBtn      = addButton(QStringLiteral("pin"), tr("Pin"), tr("Pin: keep the tools open beside the chart"), true);
  m_collapseBtn = addButton(QStringLiteral("collapse"), tr("Close"), tr("Close the tools"), false);
  m_toolButtons.value(Tool::INSPECT)->setChecked(true);

  m_opacity = new QGraphicsOpacityEffect(this);
  setGraphicsEffect(m_opacity);
  m_collapseTimer->setSingleShot(true);
  m_collapseTimer->setInterval(2000);

  connect(m_handle, &QToolButton::clicked, this, [this] { setExpanded(!m_expanded); });
  connect(m_toolGroup, &QButtonGroup::idClicked, this, [this](int id) {
    setTool(static_cast<Tool>(id));
    if (!m_pinned && !m_embedded) {
      setExpanded(false); // a tool picked: out of the way
    }
  });
  connect(fitBtn, &QToolButton::clicked, this, [this] {
    emit fitRequested();
    if (!m_pinned) {
      setExpanded(false);
    }
  });
  connect(m_pinBtn, &QToolButton::toggled, this, [this](bool on) {
    setPinned(on);
    emit pinnedChanged(on);
  });
  connect(m_collapseBtn, &QToolButton::clicked, this, [this] { setExpanded(false); });
  connect(m_collapseTimer, &QTimer::timeout, this, [this] {
    if (!m_hovered && !m_pinned) {
      setExpanded(false);
    }
  });
  updateView();
}

QToolButton *ChartToolbar::addButton(QString const &icon, QString const &name, QString const &tip, bool checkable) {
  auto *button = new QToolButton(m_tools);
  button->setObjectName("toolButton");
  button->setText(name); // shown in the side rail only
  button->setToolTip(tip);
  button->setCheckable(checkable);
  button->setFocusPolicy(Qt::NoFocus);
  button->setFixedSize(theme::dp(60), theme::dp(60));
  theme::setThemedIcon(button, icon, theme::dp(30));
  m_tools->layout()->addWidget(button);
  return button;
}

void ChartToolbar::addSeparator() {
  auto *line = new QFrame(m_tools);
  line->setObjectName("cardDivider");
  line->setFixedSize(theme::dp(45), 1);
  static_cast<QVBoxLayout *>(m_tools->layout())->addWidget(line, 0, Qt::AlignHCenter);
}

void ChartToolbar::bindToggles(QAction *labels, QAction *tooltip) {
  for (auto const &[button, action] : {std::pair{m_labelsBtn, labels}, std::pair{m_tooltipBtn, tooltip}}) {
    button->setChecked(action->isChecked());
    connect(button, &QToolButton::toggled, action, &QAction::setChecked);
    connect(action, &QAction::toggled, button, &QToolButton::setChecked);
  }
}

void ChartToolbar::setTool(Tool tool) {
  if (QToolButton *button = m_toolButtons.value(tool)) {
    button->setChecked(true);
  }
  if (tool == m_tool) {
    return;
  }
  m_tool = tool;
  updateView();
  emit toolChanged(tool);
}

ChartToolbar::Tool ChartToolbar::tool() const {
  return m_tool;
}

void ChartToolbar::setPinned(bool pinned) {
  if (pinned == m_pinned) {
    return;
  }
  m_pinned = pinned;
  m_pinBtn->setChecked(pinned);
  updateView();
}

bool ChartToolbar::pinned() const {
  return m_pinned;
}

void ChartToolbar::setAnnotateEnabled(bool enabled) {
  m_toolButtons.value(Tool::ANNOTATE)->setEnabled(enabled);
  if (!enabled && (m_tool == Tool::ANNOTATE)) {
    setTool(Tool::INSPECT);
  }
}

void ChartToolbar::setEmbedded(bool embedded) {
  m_embedded = embedded;
  if (embedded) {
    // a row of icons under the chart, on the card's background; upright lines between the groups
    auto *row = static_cast<QBoxLayout *>(m_tools->layout());
    row->setDirection(QBoxLayout::LeftToRight);
    row->setSpacing(theme::dp(6));
    for (QToolButton *button : m_tools->findChildren<QToolButton *>()) {
      button->setFixedSize(ChartSideBar::barButtonSize());
      theme::setThemedIcon(button, button->property("themeIcon").toString(), theme::dp(28), button->property("themeIconRole").toString());
    }
    for (QFrame *line : m_tools->findChildren<QFrame *>(QStringLiteral("cardDivider"))) {
      line->setFixedSize(1, theme::dp(30));
      row->setAlignment(line, Qt::AlignVCenter);
    }
    layout()->setContentsMargins(0, 0, 0, 0);
    setStyleSheet(QStringLiteral("QFrame#chartToolbar { background: transparent; border: none; }"));
  }
  updateView();
}

void ChartToolbar::setExpanded(bool expanded) {
  if (expanded == m_expanded) {
    return;
  }
  m_expanded = expanded;
  m_collapseTimer->stop();
  updateView();
}

void ChartToolbar::updateView() {
  bool const open{m_pinned || m_expanded || m_embedded};
  m_handle->setVisible(!m_pinned && !m_embedded);
  m_tools->setVisible(open);
  m_collapseBtn->setVisible(!m_pinned && !m_embedded);
  m_pinBtn->setVisible(!m_embedded);
  // the handle: the tool in use, named and tinted when it is not Inspect
  ToolInfo const t = info(m_tool);
  bool const     special{m_tool != Tool::INSPECT};
  theme::setThemedIcon(m_handle, QString::fromLatin1(t.icon), theme::dp(26), special ? QStringLiteral("accent") : QString{});
  m_handle->setText(QStringLiteral("▾")); // the icon tells the tool in use
  m_handle->setToolTip(tr("Chart tools · %1").arg(translated(t.name)));
  m_handle->setProperty("active", special);
  m_handle->style()->unpolish(m_handle);
  m_handle->style()->polish(m_handle);
  // faint while it waits, clear when used
  m_opacity->setOpacity((open || m_hovered || special || m_embedded) ? 1.0 : 0.75);
  adjustSize();
  emit sizeChanged();
}

bool ChartToolbar::dragPress(QPoint const &global) {
  m_pressed   = true;
  m_dragging  = false;
  m_dragStart = global;
  m_dragLast  = global;
  return false;
}

bool ChartToolbar::dragMove(QPoint const &global) {
  if (!m_pressed || m_pinned || m_embedded) {
    return false;
  }
  if (!m_dragging && ((global - m_dragStart).manhattanLength() < theme::dp(11))) {
    return false; // still a tap
  }
  if (!m_dragging) {
    m_dragging = true;
    m_handle->setDown(false);
    setCursor(Qt::SizeAllCursor);
  }
  emit dragged(global - m_dragLast);
  m_dragLast = global;
  return true;
}

bool ChartToolbar::dragRelease() {
  bool const dragged{m_dragging};
  m_pressed  = false;
  m_dragging = false;
  if (dragged) {
    m_handle->setDown(false);
    unsetCursor();
  }
  return dragged; // a drag is no tap: the handle does not open the tools
}

bool ChartToolbar::eventFilter(QObject *watched, QEvent *event) {
  if (watched == m_handle) {
    auto *mouse = static_cast<QMouseEvent *>(event);
    switch (event->type()) {
      case QEvent::MouseButtonPress:
        if (mouse->button() == Qt::LeftButton) {
          return dragPress(mouse->globalPosition().toPoint());
        }
        break;
      case QEvent::MouseMove:
        return dragMove(mouse->globalPosition().toPoint());
      case QEvent::MouseButtonRelease:
        if (mouse->button() == Qt::LeftButton) {
          return dragRelease();
        }
        break;
      default:
        break;
    }
  }
  return QFrame::eventFilter(watched, event);
}

void ChartToolbar::mousePressEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    dragPress(event->globalPosition().toPoint()); // its border
  }
  event->accept();
}

void ChartToolbar::mouseMoveEvent(QMouseEvent *event) {
  dragMove(event->globalPosition().toPoint());
  event->accept();
}

void ChartToolbar::mouseReleaseEvent(QMouseEvent *event) {
  dragRelease();
  event->accept();
}

void ChartToolbar::enterEvent(QEnterEvent *event) {
  QFrame::enterEvent(event);
  m_hovered = true;
  m_collapseTimer->stop();
  updateView();
}

void ChartToolbar::leaveEvent(QEvent *event) {
  QFrame::leaveEvent(event);
  m_hovered = false;
  if (m_expanded && !m_pinned) {
    m_collapseTimer->start(); // closes 2 s after the pointer left
  }
  updateView();
}

// ---------------------------------------------------------------- pad

ChartPad::ChartPad(QWidget *parent) : QFrame{parent} {
  setObjectName("chartPad");
  setAttribute(Qt::WA_StyledBackground);
  auto *grid = new QGridLayout(this);
  grid->setContentsMargins(theme::dp(12), theme::dp(12), theme::dp(12), theme::dp(12));
  grid->setSpacing(theme::dp(4));
  auto button = [this](int size) {
    auto *b = new QToolButton(this);
    b->setObjectName("toolButton");
    b->setFocusPolicy(Qt::NoFocus);
    b->setFixedSize(theme::dp(size), theme::dp(size));
    b->setAutoRepeat(true); // held: keeps going
    b->setAutoRepeatDelay(400);
    b->setAutoRepeatInterval(150);
    return b;
  };
  m_up     = button(64);
  m_down   = button(64);
  m_left   = button(64);
  m_right  = button(64);
  m_center = button(64);
  m_center->setAutoRepeat(false);
  grid->addWidget(m_up, 0, 1);
  grid->addWidget(m_left, 1, 0);
  grid->addWidget(m_center, 1, 1);
  grid->addWidget(m_right, 1, 2);
  grid->addWidget(m_down, 2, 1);

  connect(m_up, &QToolButton::clicked, this, [this] {
    m_mode == ChartToolbar::Tool::ZOOM ? emit zoomRequested(1.0, 0.8) : emit moveRequested(0.0, 0.2);
  });
  connect(m_down, &QToolButton::clicked, this, [this] {
    m_mode == ChartToolbar::Tool::ZOOM ? emit zoomRequested(1.0, 1.25) : emit moveRequested(0.0, -0.2);
  });
  connect(m_left, &QToolButton::clicked, this, [this] {
    m_mode == ChartToolbar::Tool::ZOOM ? emit zoomRequested(1.25, 1.0) : emit moveRequested(-0.2, 0.0);
  });
  connect(m_right, &QToolButton::clicked, this, [this] {
    m_mode == ChartToolbar::Tool::ZOOM ? emit zoomRequested(0.8, 1.0) : emit moveRequested(0.2, 0.0);
  });
  connect(m_center, &QToolButton::clicked, this, &ChartPad::closeRequested);
  setMode(ChartToolbar::Tool::ZOOM);
}

void ChartPad::setMode(ChartToolbar::Tool tool) {
  m_mode = tool;
  bool const zoom{tool == ChartToolbar::Tool::ZOOM};
  // zoom: up / down zoom the values in / out, right / left the time in / out; pan: the four directions
  theme::setThemedIcon(m_up, zoom ? QStringLiteral("plus") : QStringLiteral("up"), theme::dp(26));
  theme::setThemedIcon(m_down, zoom ? QStringLiteral("minus") : QStringLiteral("down"), theme::dp(26));
  theme::setThemedIcon(m_left, zoom ? QStringLiteral("minus") : QStringLiteral("left"), theme::dp(26));
  theme::setThemedIcon(m_right, zoom ? QStringLiteral("plus") : QStringLiteral("right"), theme::dp(26));
  theme::setThemedIcon(m_center, zoom ? QStringLiteral("zoom") : QStringLiteral("pan"), theme::dp(30), QStringLiteral("accent"));
  m_up->setToolTip(zoom ? tr("Zoom in on the values") : tr("Move up"));
  m_down->setToolTip(zoom ? tr("Zoom out of the values") : tr("Move down"));
  m_left->setToolTip(zoom ? tr("Zoom out of the time") : tr("Earlier"));
  m_right->setToolTip(zoom ? tr("Zoom in on the time") : tr("Later"));
  m_center->setToolTip(tr("Back to Inspect"));
  adjustSize();
}

}
