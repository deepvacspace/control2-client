#ifndef FLOWLAYOUT_H
#define FLOWLAYOUT_H

#include <QLayout>
#include <QList>

namespace deepvaccontrol {

/**
 * @brief Lays out items left to right and wraps them to the next row when the width
 * is not enough (based on the Qt "Flow Layout" example).
 */
class FlowLayout : public QLayout {
public:
  explicit FlowLayout(QWidget *parent = nullptr, int spacing = -1);
  ~FlowLayout() override;

  void             addItem(QLayoutItem *item) override;
  int              count() const override;
  QLayoutItem     *itemAt(int index) const override;
  QLayoutItem     *takeAt(int index) override;
  Qt::Orientations expandingDirections() const override;
  bool             hasHeightForWidth() const override;
  int              heightForWidth(int width) const override;
  QSize            minimumSize() const override;
  QSize            sizeHint() const override;
  void             setGeometry(QRect const &rect) override;

private:
  int doLayout(QRect const &rect, bool testOnly) const;

  QList<QLayoutItem *> m_items;
  int                  m_spacing;
};

} // namespace deepvaccontrol

#endif // FLOWLAYOUT_H
