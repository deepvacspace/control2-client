#include "annotationdialog.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QComboBox>
#include <QCoreApplication>
#include <QGridLayout>

#include "datetimefield.h"
#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

#include "lineeditscreen.h"
#include "theme.h"
#include "utils.h"

namespace deepvaccontrol::annotationdialog {

namespace {
struct NamedColor {
  char const *name;
  char const *color;
};

NamedColor const NOTE_COLORS[]{{QT_TRANSLATE_NOOP("AnalysisRunPage", "Green"), "#16a34a"},
                               {QT_TRANSLATE_NOOP("AnalysisRunPage", "Amber"), "#d97706"},
                               {QT_TRANSLATE_NOOP("AnalysisRunPage", "Red"), "#dc2626"},
                               {QT_TRANSLATE_NOOP("AnalysisRunPage", "Blue"), "#2563eb"},
                               {QT_TRANSLATE_NOOP("AnalysisRunPage", "Purple"), "#9333ea"},
                               {QT_TRANSLATE_NOOP("AnalysisRunPage", "Teal"), "#0d9488"}};

QString tr(char const *text) {
  return QCoreApplication::translate("AnalysisRunPage", text);
}

int px(double value) {
  return qMax(1, static_cast<int>(value * utils::ssf()));
}

QLabel *caption(QString const &text, QWidget *parent) {
  auto *label = new QLabel(text, parent);
  label->setProperty("muted", true);
  QFont font = label->font();
  font.setPointSizeF(font.pointSizeF() * 0.9);
  label->setFont(font);
  return label;
}

// The dialog: a heading, the segment (text or editable times), the label field, a row of colour chips below it,
// and Cancel / primary button on the right.
class AnnotationForm {
public:
  AnnotationForm(QWidget *parent, QString const &heading, QString const &primary, QString const &label, QColor const &color)
      : dialog{parent} {
    dialog.setWindowTitle(heading);
    lay = new QVBoxLayout(&dialog);
    lay->setContentsMargins(px(20), px(18), px(20), px(16));
    lay->setSpacing(px(6));

    auto *title = new QLabel(heading, &dialog);
    QFont font  = title->font();
    font.setBold(true);
    font.setPointSizeF(font.pointSizeF() * 1.25);
    title->setFont(font);
    lay->addWidget(title);
    segmentLay = new QHBoxLayout();
    segmentLay->setSpacing(px(6));
    lay->addLayout(segmentLay);
    lay->addSpacing(px(12));

    lay->addWidget(caption(tr("Label"), &dialog));
    labelEdit = new LineEditScreen(&dialog, VirtualKeyboard::EN);
    labelEdit->setText(label);
    labelEdit->setPlaceholderText(tr("e.g. Steady state"));
    labelEdit->setMinimumHeight(px(32));
    lay->addWidget(labelEdit);
    lay->addSpacing(px(10));

    lay->addWidget(caption(tr("Colour"), &dialog));
    auto *chips = new QHBoxLayout();
    chips->setSpacing(px(10));
    group = new QButtonGroup(&dialog);
    group->setExclusive(true);
    theme::Colors const &c = theme::colors();
    for (auto const &option : NOTE_COLORS) {
      auto *chip = new QPushButton(&dialog);
      chip->setCheckable(true);
      chip->setFixedSize(px(28), px(28));
      chip->setToolTip(tr(option.name));
      chip->setProperty("color", QString(option.color));
      chip->setCursor(Qt::PointingHandCursor);
      chip->setFocusPolicy(Qt::NoFocus);
      // a filled circle; the chosen one gets a ring
      chip->setStyleSheet(QStringLiteral("QPushButton { background: %1; border: %2px solid %3; border-radius: %4px; }"
                                         "QPushButton:checked { border: %5px solid %6; }")
                            .arg(option.color)
                            .arg(px(3))
                            .arg(c.panel.name())
                            .arg(px(14))
                            .arg(px(3))
                            .arg(c.text.name()));
      group->addButton(chip);
      chips->addWidget(chip);
      if (QColor(option.color) == color) {
        chip->setChecked(true);
      }
    }
    if (!group->checkedButton()) {
      group->buttons().first()->setChecked(true);
    }
    chips->addStretch();
    lay->addLayout(chips);
    lay->addSpacing(px(16));

    auto *buttons   = new QHBoxLayout();
    auto *cancelBtn = new QPushButton(tr("Cancel"), &dialog);
    auto *okBtn     = new QPushButton(primary, &dialog);
    cancelBtn->setProperty("outline", true);
    okBtn->setObjectName("primaryButton");
    okBtn->setDefault(true);
    for (QPushButton *button : {cancelBtn, okBtn}) {
      button->setMinimumSize(px(96), px(32));
      button->setFocusPolicy(Qt::NoFocus);
    }
    buttons->addStretch();
    buttons->addWidget(cancelBtn);
    buttons->addWidget(okBtn);
    lay->addLayout(buttons);
    QObject::connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);
    QObject::connect(okBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
    QObject::connect(labelEdit, &QLineEdit::returnPressed, &dialog, &QDialog::accept);
  }

  bool exec(QString &label, QColor &color) {
    dialog.resize(px(440), dialog.sizeHint().height());
    labelEdit->setFocus();
    if (dialog.exec() != QDialog::Accepted) {
      return false;
    }
    label = labelEdit->text().trimmed();
    color = QColor(group->checkedButton()->property("color").toString());
    return true;
  }

  QDialog       dialog;
  QVBoxLayout  *lay{nullptr};
  QHBoxLayout  *segmentLay{nullptr};
  QLineEdit    *labelEdit{nullptr};
  QButtonGroup *group{nullptr};
};
} // namespace

QComboBox *makeColorCombo(QWidget *parent, int height) {
  auto *combo = new QComboBox(parent);
  for (auto const &option : NOTE_COLORS) {
    QPixmap swatch(14, 14);
    swatch.fill(QColor(option.color));
    combo->addItem(QIcon(swatch), tr(option.name), QString(option.color));
  }
  combo->setMinimumHeight(height);
  return combo;
}

bool ask(QWidget *parent, QString const &segmentText, QString &label, QColor &color) {
  AnnotationForm form(parent, tr("New annotation"), tr("Add"), QString{}, QColor(NOTE_COLORS[0].color));
  auto *name = new QLabel(tr("Segment:"), &form.dialog);
  name->setProperty("muted", true);
  auto *range = new QLabel(segmentText, &form.dialog);
  QFont font  = range->font();
  font.setBold(true);
  range->setFont(font);
  form.segmentLay->addWidget(name);
  form.segmentLay->addWidget(range);
  form.segmentLay->addStretch();
  return form.exec(label, color);
}

bool edit(QWidget *parent, QString &label, QColor &color, QDateTime &from, QDateTime &to) {
  AnnotationForm form(parent, tr("Edit annotation"), tr("Save"), label, color);
  // date from a calendar, time from the number pad
  auto *name = new QLabel(tr("Segment:"), &form.dialog);
  name->setProperty("muted", true);
  auto *fromEdit = new DateTimeField(from, &form.dialog);
  auto *toEdit   = new DateTimeField(to, &form.dialog);
  auto *rows     = new QGridLayout();
  rows->setHorizontalSpacing(px(8));
  rows->addWidget(new QLabel(tr("From"), &form.dialog), 0, 0);
  rows->addWidget(fromEdit, 0, 1);
  rows->addWidget(new QLabel(tr("To"), &form.dialog), 1, 0);
  rows->addWidget(toEdit, 1, 1);
  form.segmentLay->addWidget(name, 0, Qt::AlignTop);
  form.segmentLay->addLayout(rows, 1);
  if (!form.exec(label, color)) {
    return false;
  }
  from = std::min(fromEdit->dateTime(), toEdit->dateTime());
  to   = std::max(fromEdit->dateTime(), toEdit->dateTime());
  return true;
}

} // namespace deepvaccontrol::annotationdialog
