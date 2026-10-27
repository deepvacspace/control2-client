#include "statemismatchdialog.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "theme.h"
#include "utils.h"

namespace deepvaccontrol {

namespace {
// "a, b, c and 4 more"
QString nameList(QStringList const &names) {
  constexpr int SHOWN{8};
  if (names.size() <= SHOWN) {
    return names.join(QStringLiteral(", "));
  }
  return QObject::tr("%1 and %2 more").arg(names.mid(0, SHOWN).join(QStringLiteral(", "))).arg(names.size() - SHOWN);
}
}

StateMismatchDialog::StateMismatchDialog(QString const     &address,
                                         QStringList const &chamberNames,
                                         QStringList const &appNames,
                                         QString const     &backAddress,
                                         QStringList const &missing,
                                         QWidget           *parent)
    : QDialog{parent} {
  double const ssf{utils::ssf()};
  setWindowTitle(tr("Chamber configuration"));
  setAttribute(Qt::WA_DeleteOnClose);
  setWindowModality(Qt::WindowModal);

  auto *mainLay = new QVBoxLayout(this);
  mainLay->setContentsMargins(22 * ssf, 20 * ssf, 22 * ssf, 18 * ssf);
  mainLay->setSpacing(18 * ssf);

  auto *top = new QHBoxLayout();
  top->setSpacing(14 * ssf);
  auto *icon = new QLabel(this);
  icon->setPixmap(theme::dialogIcon(QStringLiteral("error"), static_cast<int>(34 * ssf)));
  top->addWidget(icon, 0, Qt::AlignTop);
  auto *texts = new QVBoxLayout();
  texts->setSpacing(8 * ssf);

  auto *title = new QLabel(tr("This chamber is set up differently"), this);
  title->setObjectName("cardTitle");
  QFont titleFont = title->font();
  titleFont.setPointSizeF(titleFont.pointSizeF() * 1.15);
  title->setFont(titleFont);
  texts->addWidget(title);

  auto addText = [&](QString const &text, bool muted = false) {
    auto *label = new QLabel(text, this);
    label->setWordWrap(true);
    label->setProperty("muted", muted);
    texts->addWidget(label);
  };
  addText(tr("The chamber at %1 reports %2 values; this application is set up for %3 (settings.json). "
             "Its values are not read and nothing is sent to it.")
            .arg(address)
            .arg(chamberNames.size())
            .arg(appNames.size()));

  QStringList onlyChamber;
  for (QString const &name : chamberNames) {
    if (!appNames.contains(name)) {
      onlyChamber.push_back(name);
    }
  }
  QStringList onlyApp;
  for (QString const &name : appNames) {
    if (!chamberNames.contains(name)) {
      onlyApp.push_back(name);
    }
  }
  if (!onlyChamber.isEmpty()) {
    addText(tr("Only on the chamber: %1").arg(nameList(onlyChamber)), true);
  }
  if (!onlyApp.isEmpty()) {
    addText(tr("Only in this application: %1").arg(nameList(onlyApp)), true);
  }
  if (onlyChamber.isEmpty() && onlyApp.isEmpty()) {
    addText(tr("Same values, in a different order."), true);
  }
  if (!missing.isEmpty()) {
    addText(tr("Its list cannot be used here: the chamber lacks %1, which this application needs.").arg(nameList(missing)), true);
  }
  top->addLayout(texts, 1);
  mainLay->addLayout(top);

  auto *buttons = new QHBoxLayout();
  buttons->setSpacing(10 * ssf);
  auto addButton = [&](QString const &text, int choice, QString const &tip, bool primary = false) {
    auto *button = new QPushButton(text, this);
    button->setToolTip(tip);
    button->setMinimumSize(static_cast<int>(130 * ssf), static_cast<int>(36 * ssf));
    if (primary) {
      button->setObjectName("primaryButton");
      button->setDefault(true);
    }
    connect(button, &QPushButton::clicked, this, [this, choice] { done(choice); });
    buttons->addWidget(button);
  };
  if (missing.isEmpty()) {
    addButton(tr("Use the chamber's list…"), ADOPT,
              tr("Write the chamber's value list into settings.json (a backup is kept) and restart the application"));
  }
  buttons->addStretch();
  if (!backAddress.isEmpty()) {
    addButton(tr("Back to %1").arg(backAddress), BACK, tr("Reconnect the last chamber that matched this application"));
  }
  addButton(tr("Stay disconnected"), STAY, tr("Keep the application running without this chamber"), true);
  mainLay->addLayout(buttons);

  setMinimumWidth(static_cast<int>(520 * ssf));
  setMaximumWidth(static_cast<int>(760 * ssf));
  adjustSize();
}

void StateMismatchDialog::reject() {
  done(STAY);
}

}
