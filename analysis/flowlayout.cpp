#include "flowlayout.h"

#include <QWidget>

namespace deepvaccontrol {

FlowLayout::FlowLayout(QWidget *parent, int spacing) : QLayout{parent}, m_spacing{spacing} {
  setContentsMargins(0, 0, 0, 0);
}

FlowLayout::~FlowLayout() {
  while (QLayoutItem *item = takeAt(0)) {
    delete item;
  }
}

void FlowLayout::addItem(QLayoutItem *item) {
  m_items.push_back(item);
}

int FlowLayout::count() const {
  return m_items.size();
}

QLayoutItem *FlowLayout::itemAt(int index) const {
  return m_items.value(index);
}

QLayoutItem *FlowLayout::takeAt(int index) {
  return ((index >= 0) && (index < m_items.size())) ? m_items.takeAt(index) : nullptr;
}

Qt::Orientations FlowLayout::expandingDirections() const {
  return {};
}

bool FlowLayout::hasHeightForWidth() const {
  return true;
}

int FlowLayout::heightForWidth(int width) const {
  return doLayout(QRect(0, 0, width, 0), true);
}

QSize FlowLayout::minimumSize() const {
  QSize size;
  for (QLayoutItem const *item : m_items) {
    size = size.expandedTo(item->minimumSize());
  }
  auto const margins = contentsMargins();
  return size + QSize(margins.left() + margins.right(), margins.top() + margins.bottom());
}

QSize FlowLayout::sizeHint() const {
  // preferred: everything in one row
  int width{0};
  int height{0};
  for (QLayoutItem const *item : m_items) {
    width += item->sizeHint().width() + (width > 0 ? std::max(0, m_spacing) : 0);
    height = std::max(height, item->sizeHint().height());
  }
  auto const margins = contentsMargins();
  return {width + margins.left() + margins.right(), height + margins.top() + margins.bottom()};
}

void FlowLayout::setGeometry(QRect const &rect) {
  QLayout::setGeometry(rect);
  doLayout(rect, false);
}

int FlowLayout::doLayout(QRect const &rect, bool testOnly) const {
  auto const  margins   = contentsMargins();
  QRect const area      = rect.adjusted(margins.left(), margins.top(), -margins.right(), -margins.bottom());
  int const   spacing   = std::max(0, m_spacing);
  int         x         = area.x();
  int         y         = area.y();
  int         rowHeight = 0;

  for (QLayoutItem *item : m_items) {
    if (item->widget() && item->widget()->isHidden()) {
      continue;
    }
    QSize const hint = item->sizeHint();
    int         nextX = x + hint.width() + spacing;
    if (((nextX - spacing) > area.right() + 1) && (rowHeight > 0)) {
      x         = area.x();
      y         = y + rowHeight + spacing;
      nextX     = x + hint.width() + spacing;
      rowHeight = 0;
    }
    if (!testOnly) {
      item->setGeometry(QRect(QPoint(x, y), QSize(std::min(hint.width(), area.width()), hint.height())));
    }
    x         = nextX;
    rowHeight = std::max(rowHeight, hint.height());
  }
  return y + rowHeight - rect.y() + margins.bottom();
}

} // namespace deepvaccontrol
