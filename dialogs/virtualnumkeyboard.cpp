#include "virtualnumkeyboard.h"

#include <QGridLayout>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QScreen>

#include "lineeditnum.h"
#include "utils.h"

namespace deepvaccontrol {
VirtualNumKeyboard::VirtualNumKeyboard(QWidget *parent) : QDialog{parent}, host{nullptr}, notEdited{true} {
  host = qobject_cast<LineEditNum *>(parent);
  if (nullptr == host) {
    close();
  }
  isScientific = host->getIsScientific();

  setWindowFlags(Qt::Popup);

  prev = host->text();

  QFont font;
  font.setPointSize(18 * utils::ssf());
  //    f.setFamily("Helvetica [Cronyx]"); NOTE not every machine has this font
  for (int i{0U}; i < 10; ++i) {
    auto *btn = new QPushButton(QString::number(i), this);
    connect(btn, &QPushButton::clicked, this, &VirtualNumKeyboard::handleNumKeysPress);
    btn->setFixedSize(50 * utils::ssf(), 50 * utils::ssf());
    btn->setFont(font);
    numericalButtons.append(btn);
  }

  point  = new QPushButton(".", this);
  minus  = new QPushButton("-", this);
  del    = new QPushButton(this);
  e      = new QPushButton("E", this);
  enter  = new QPushButton(tr("ВВОД"), this);
  cancel = new QPushButton(tr("Отмена"), this);
  controlButtons << point << minus << del << enter << cancel << e;
  del->setIcon(QIcon(":/imgs/backspace.png"));
  del->setIconSize(QSize(40 * utils::ssf(), 40 * utils::ssf()));

  cancel->setFixedSize(2 * 50 * utils::ssf(), 50 * utils::ssf());
  enter->setFixedSize(2 * 50 * utils::ssf(), 2 * 50 * utils::ssf());
  point->setFixedSize(50 * utils::ssf(), 50 * utils::ssf());
  minus->setFixedSize(50 * utils::ssf(), 50 * utils::ssf());
  e->setFixedSize(50 * utils::ssf(), 50 * utils::ssf());
  for (auto const &btn : controlButtons) {
    btn->setFont(font);
    connect(btn, &QPushButton::clicked, this, &VirtualNumKeyboard::handleKeyPress);
  }

  auto *lay = new QGridLayout(this);
  for (int i{0U}; i < 3; ++i) {
    for (int j{0U}; j < 3; ++j) {
      lay->addWidget(numericalButtons.at((j * 3) + i + 1), 2 - j, i);
    }
  }
  lay->addWidget(numericalButtons.at(0), 3, 1);
  lay->addWidget(point, 3, 0);
  lay->addWidget(minus, 3, 2);

  if (isScientific) {
    lay->addWidget(e, 3, 3);
    lay->addWidget(del, 0, 3, 1, 3);
    lay->addWidget(cancel, 1, 3, 1, 3);
    lay->addWidget(enter, 2, 4, 2, 2);
    del->setFixedSize(3 * 50 * utils::ssf(), 50 * utils::ssf());
    cancel->setFixedSize(3 * 50 * utils::ssf(), 50 * utils::ssf());
  } else {
    lay->addWidget(del, 0, 3, 1, 2);
    lay->addWidget(cancel, 1, 3, 1, 2);
    lay->addWidget(enter, 2, 3, 2, 2);
  }

  lay->setSpacing(0);
  lay->setContentsMargins(0, 0, 0, 0);

  // make some changes correspoint to host's validator
  auto const *validator = host->getValidator();
  minus->setEnabled((validator->bottom() < 0) || isScientific);
  point->setDisabled(validator->decimals() == 0);
  e->setVisible(isScientific);

  // positions
  QPoint globalPos  = parent->mapToGlobal(pos());
  QSize  screenSize = QGuiApplication::screens().first()->geometry().size();
  QSize  parentSize = parent->size();
  QSize  dialogSize(5 * 50 * utils::ssf(), 4 * 50 * utils::ssf());

  QPoint downShift  = QPoint(0, parentSize.height() + 4 * utils::ssf());
  QPoint upShit     = QPoint(0, -dialogSize.height() - 4 * utils::ssf());
  QPoint leftShift  = QPoint(parentSize.width() - dialogSize.width(), 0);
  QPoint rightShift = QPoint(0, 0);

  QPoint showPos = globalPos;
  showPos += QRect(globalPos + rightShift, dialogSize).right() < screenSize.width() ? rightShift : leftShift;
  showPos += QRect(globalPos + downShift, dialogSize).bottom() < screenSize.height() ? downShift : upShit;

  setGeometry(QRect(showPos, dialogSize));
}

void VirtualNumKeyboard::reject() {
  host->setText(prev);
  QDialog::reject();
}

void VirtualNumKeyboard::handleNumKeysPress() {
  if (notEdited) {
    host->setText("");
  }

  auto *btn = qobject_cast<QPushButton *>(sender());
  if (!btn) {
    return;
  }

  Qt::Key   key{static_cast<Qt::Key>(btn->text().at(0).toLatin1())};
  QKeyEvent pressEvent{QEvent::KeyPress, key, Qt::NoModifier, QKeySequence(key).toString()};
  QKeyEvent releaseEvent{QEvent::KeyRelease, key, Qt::NoModifier};
  QCoreApplication::sendEvent(parent(), &pressEvent);
  QCoreApplication::sendEvent(parent(), &releaseEvent);
  notEdited = false;
}

void VirtualNumKeyboard::handleKeyPress() {
  auto *btn = qobject_cast<QPushButton *>(sender());
  if (!btn) {
    return;
  }

  Qt::Key key{Qt::Key_unknown};

  if (btn == numericalButtons[0]) {
    if (!(host->text() == "0")) {
      key = Qt::Key(0);
    }
  } else if (btn == point) {
    if (!host->text().contains('E')) {
      // fix detached point symbol
      if (notEdited) {
        host->setText("");
      }
      if ((host->text() == "") || (host->text() == "-")) {
        host->setText(host->text().append(QChar('0')));
      }

      key       = Qt::Key_Period;
      notEdited = false;
    }
  } else if (btn == minus) {
    if (notEdited) {
      host->setText("");
    }

    if (host->text().contains('E')) {
      if (host->text().endsWith('E')) {
        key = Qt::Key_Minus;
      }
    } else {
      if (host->text() == "") {
        key = Qt::Key_Minus;
      } else if (host->text().startsWith("-")) {
        host->setText(host->text().mid(1));
      } else {
        host->setText(host->text().prepend("-"));
      }

      notEdited = false;
    }
  } else if (btn == e) {
    if (notEdited) {
      ;
    } else {
      key = Qt::Key_E;
    }
  } else if (btn == del) {
    if (notEdited) {
      host->setText("");
    } else {
      key = Qt::Key_Backspace;
    }
  } else if (btn == cancel) {
    host->setText(prev);
    close();
    return;
  } else if (btn == enter) {
    accept();
    return;
  }

  QMap<Qt::Key, QString> map;
  map.insert(Qt::Key(0), "");
  map.insert(Qt::Key_Period, ".");
  map.insert(Qt::Key_Minus, "-");
  map.insert(Qt::Key_Backspace, "");
  map.insert(Qt::Key_E, "E");

  QKeyEvent pressEvent{QEvent::KeyPress, key, Qt::NoModifier, map.value(key)};
  QKeyEvent releaseEvent{QEvent::KeyRelease, key, Qt::NoModifier, ""};
  QCoreApplication::sendEvent(parent(), &pressEvent);
  QCoreApplication::sendEvent(parent(), &releaseEvent);
}

LineEditNum *VirtualNumKeyboard::getLineEditNumOwner() const {
  return host;
}

void VirtualNumKeyboard::setLineEditNum(LineEditNum *value) {
  host = value;
}
} // namespace deepvaccontrol
