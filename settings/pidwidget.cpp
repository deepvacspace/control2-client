#include "pidwidget.h"

#include <QGridLayout>
#include <QLabel>

namespace control2 {
PidWidget::PidWidget(QVariantMap data, QWidget *parent) : QWidget{parent} {
  m_name     = data["name"].toString();
  m_label    = data["label"].toString();
  m_points   = data["points"].toUInt();
  m_minPoint = data["min"].toFloat();
  m_maxPoint = data["max"].toFloat();

  initGui();
  setDataVariant(data);
}

void PidWidget::setDataVariant(QVariantMap data) {
  if (data["points"].toUInt() != m_points) {
    return;
  }

  auto lineEditIter = m_fields.begin();
  // P
  for (quint32 i{0U}; i < m_points; ++i) {
    (*lineEditIter)->setValue(data["p"].toList()[i].toFloat());
    ++lineEditIter;
  }
  // I
  for (quint32 i{0U}; i < m_points; ++i) {
    (*lineEditIter)->setValue(data["i"].toList()[i].toFloat());
    ++lineEditIter;
  }
  // D
  for (quint32 i{0U}; i < m_points; ++i) {
    (*lineEditIter)->setValue(data["d"].toList()[i].toFloat());
    ++lineEditIter;
  }
}

QVariantMap PidWidget::getDataVariant() const {
  QVariantMap ret;

  ret["name"]   = m_name;
  ret["label"]  = m_label;
  ret["points"] = m_points;
  ret["min"]    = m_minPoint;
  ret["max"]    = m_maxPoint;

  auto lineEditIter = m_fields.begin();

  // P
  QVariantList pList;
  for (quint32 i{0U}; i < m_points; ++i) {
    pList.push_back((*lineEditIter)->text().toFloat());
    ++lineEditIter;
  }
  // I
  QVariantList iList;
  for (quint32 i{0U}; i < m_points; ++i) {
    iList.push_back((*lineEditIter)->text().toFloat());
    ++lineEditIter;
  }
  // D
  QVariantList dList;
  for (quint32 i{0U}; i < m_points; ++i) {
    dList.push_back((*lineEditIter)->text().toFloat());
    ++lineEditIter;
  }

  ret["p"] = pList;
  ret["i"] = iList;
  ret["d"] = dList;

  return ret;
}

void PidWidget::initGui() {
  if (0U == m_points) {
    return;
  }

  auto *mainLay = new QGridLayout(this);
  mainLay->setContentsMargins(3, 3, 3, 3);

  float const rangeWidth{(m_maxPoint - m_minPoint) / m_points};

  // titles
  mainLay->addWidget(new QLabel(tr("П")), 0, 0, Qt::AlignHCenter);
  mainLay->addWidget(new QLabel(tr("И")), 0, 1, Qt::AlignHCenter);
  mainLay->addWidget(new QLabel(tr("Д")), 0, 2, Qt::AlignHCenter);

  // Range labels
  for (quint32 i{0U}; i < m_points; ++i) {
    double const start{m_minPoint + i * rangeWidth};
    double const end{m_minPoint + (i + 1) * rangeWidth};
    auto const   text       = QString("%1 — %2").arg(start).arg(end);
    auto        *rangeLabel = new QLabel(text, this);
    mainLay->addWidget(rangeLabel, 1 + m_points - i, 3);
  }

  // P
  for (quint32 i{0U}; i < m_points; ++i) {
    auto *lineEdit = new LineEditNum(0, 50, 1, false, this);
    mainLay->addWidget(lineEdit, 1 + m_points - i, 0);
    m_fields.push_back(lineEdit);
  }
  // I
  for (quint32 i{0U}; i < m_points; ++i) {
    auto *lineEdit = new LineEditNum(0, 1000, 0, false, this);
    mainLay->addWidget(lineEdit, 1 + m_points - i, 1);
    m_fields.push_back(lineEdit);
  }
  // D
  for (quint32 i{0U}; i < m_points; ++i) {
    auto *lineEdit = new LineEditNum(0, 20, 3, false, this);
    mainLay->addWidget(lineEdit, 1 + m_points - i, 2);
    m_fields.push_back(lineEdit);
  }

  mainLay->setColumnStretch(3 + 1, 1);
  mainLay->setRowStretch(m_points + 1 + 1, 1);
}
} // namespace control2
