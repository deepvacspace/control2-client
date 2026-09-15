#include "drivepathselect.h"

#include <QDir>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QRadioButton>

#include "utils.h"

namespace deepvaccontrol {
DrivePathSelect::DrivePathSelect(QList<QString> pathList, QWidget *parent) : QWidget{parent} {
  m_selectedPath = pathList.last();

  QDir pathDir(pathList.first());
  if (!pathDir.exists()) {
    pathDir.mkpath(".");
  }

  auto *mainLay = new QHBoxLayout(this);
  mainLay->setContentsMargins(0, 0, 0, 0);
  auto *box           = new QGroupBox(tr("Расположение:"), this);
  auto *pathSelectLay = new QHBoxLayout(box);

  qDebug() << pathList.first() << pathDir.exists();

  QStringList const labels{tr("Внутренний накопитель"), tr("Внешний")};
  for (int i{0U}; i < pathList.size(); ++i) {
    auto const &pathString = pathList[i];
    auto       *radioBtn   = new QRadioButton(labels[qMin(i, 1)] + "\n( " + pathString + " )", this);
    radioBtn->setFixedHeight(radioBtn->height() * 2 * utils::ssf());
    pathSelectLay->addWidget(radioBtn);
    if (i == (pathList.size() - 1)) {
      radioBtn->setChecked(true);
    }

    connect(radioBtn, &QRadioButton::toggled, this, [this, pathString](bool val) {
      if (val) {
        m_selectedPath = pathString;
        emit selectedPathChanged(m_selectedPath);
      }
    });
  }

  if (1 == pathList.size()) {
    box->setEnabled(false);
  }

  setStyleSheet(utils::readStyleFromFile(":/styles/fileSelect.qss"));
  mainLay->addWidget(box);
}
} // namespace deepvaccontrol
