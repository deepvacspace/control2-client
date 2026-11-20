#include "layoutreport.h"

#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QPushButton>
#include <QRadioButton>
#include <QStyle>
#include <QStyleOption>
#include <QTabBar>
#include <QTextDocument>
#include <QTextStream>
#include <QToolButton>
#include <QWidget>
#include <algorithm>

namespace deepvaccontrol::layoutreport {

namespace {
QString shortText(QString text) {
  text.replace(QLatin1Char('\n'), QLatin1Char(' '));
  return (text.size() > 60) ? text.left(57) + QStringLiteral("…") : text;
}

QString where(QWidget const *widget, QWidget const *window) {
  QPoint const at = widget->mapTo(window, QPoint(0, 0));
  return QStringLiteral("at %1,%2 %3x%4").arg(at.x()).arg(at.y()).arg(widget->width()).arg(widget->height());
}

// plain text of a label (rich text without its tags)
QString labelText(QLabel const *label) {
  if (label->textFormat() == Qt::PlainText || !Qt::mightBeRichText(label->text())) {
    return label->text();
  }
  QTextDocument document;
  document.setHtml(label->text());
  return document.toPlainText();
}
}

void write(QWidget const *window, QString const &path) {
  QFile file(path);
  if (!window || !file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    return;
  }
  QTextStream out(&file);
  QStringList problems;
  struct Size {
    int         count{0};
    QStringList examples;
  };
  QMap<double, Size> sizes; // font size in points -> the texts drawn in it
  auto               count = [&sizes](QWidget const *widget, QString const &text) {
    if (text.trimmed().isEmpty()) {
      return;
    }
    Size &size = sizes[qRound(widget->font().pointSizeF() * 10.0) / 10.0];
    ++size.count;
    if (size.examples.size() < 6) {
      size.examples << QStringLiteral("%1 \"%2\"").arg(QString::fromLatin1(widget->metaObject()->className()), shortText(text));
    }
  };
  QRect const frame(QPoint(0, 0), window->size());

  for (QWidget const *widget : window->findChildren<QWidget *>()) {
    if (!widget->isVisibleTo(window) || (widget->width() <= 0) || (widget->height() <= 0)) {
      continue;
    }
    QString const place = where(widget, window);
    QString const type  = QString::fromLatin1(widget->metaObject()->className());
    // outside the window (its page does not scroll): not seen at all
    QRect const rect(widget->mapTo(window, QPoint(0, 0)), widget->size());
    bool        scrolled{false};
    for (QWidget const *parent = widget->parentWidget(); parent && (parent != window); parent = parent->parentWidget()) {
      scrolled = scrolled || parent->inherits("QAbstractScrollArea");
    }
    if (!scrolled && !frame.contains(rect) && !frame.intersected(rect).isEmpty()) {
      problems << QStringLiteral("OUTSIDE  %1 '%2' %3 (window %4x%5)").arg(type, widget->objectName(), place).arg(frame.width()).arg(frame.height());
    }

    if (auto const *label = qobject_cast<QLabel const *>(widget)) {
      QString const text = labelText(label);
      if (text.trimmed().isEmpty() || !label->pixmap(Qt::ReturnByValue).isNull()) {
        continue;
      }
      count(label, text);
      QRect const room = label->contentsRect();
      if (label->wordWrap()) {
        int const needed{label->heightForWidth(label->width())};
        if (needed > label->height() + 2) {
          problems << QStringLiteral("CUT      %1 %2: wrapped text needs %3 px of height, has %4: \"%5\"")
                        .arg(type, place)
                        .arg(needed)
                        .arg(label->height())
                        .arg(shortText(text));
        }
      } else {
        int needed{0};
        for (QString const &line : text.split(QLatin1Char('\n'))) {
          needed = std::max(needed, label->fontMetrics().horizontalAdvance(line));
        }
        if (Qt::mightBeRichText(label->text())) {
          needed = label->sizeHint().width() - (label->width() - room.width());
        }
        if (needed > room.width() + 2) {
          problems << QStringLiteral("CUT      %1 %2: text needs %3 px of width, has %4: \"%5\"").arg(type, place).arg(needed).arg(room.width()).arg(shortText(text));
        }
      }
    } else if (auto const *button = qobject_cast<QAbstractButton const *>(widget)) {
      auto const *tool = qobject_cast<QToolButton const *>(button);
      if (button->text().trimmed().isEmpty() || (tool && (tool->toolButtonStyle() == Qt::ToolButtonIconOnly) && !tool->icon().isNull())) {
        continue;
      }
      count(button, button->text());
      QString text = button->text();
      text.remove(QLatin1Char('&'));
      int needed{0};
      for (QString const &line : text.split(QLatin1Char('\n'))) {
        needed = std::max(needed, button->fontMetrics().horizontalAdvance(line));
      }
      if (!button->icon().isNull()) {
        needed += button->iconSize().width() + 4;
      }
      if (qobject_cast<QCheckBox const *>(button) || qobject_cast<QRadioButton const *>(button)) {
        QStyleOptionButton option;
        option.initFrom(button);
        needed += button->style()->pixelMetric(QStyle::PM_IndicatorWidth, &option, button)
                  + button->style()->pixelMetric(QStyle::PM_CheckBoxLabelSpacing, &option, button);
      }
      if (needed > button->width()) {
        problems << QStringLiteral("CUT      %1 %2: text needs %3 px of width, has %4: \"%5\"").arg(type, place).arg(needed).arg(button->width()).arg(shortText(text));
      }
    } else if (auto const *edit = qobject_cast<QLineEdit const *>(widget)) {
      QString const text = edit->text().isEmpty() ? edit->placeholderText() : edit->text();
      count(edit, text);
      // a field inside a combo box or a spin box is theirs
      if (!text.isEmpty() && !edit->parentWidget()->inherits("QAbstractSpinBox") && !edit->parentWidget()->inherits("QComboBox")
          && (edit->fontMetrics().horizontalAdvance(text) > edit->width() - 6)) {
        problems << QStringLiteral("CUT      %1 %2: text needs %3 px of width, has %4: \"%5\"")
                      .arg(type, place)
                      .arg(edit->fontMetrics().horizontalAdvance(text))
                      .arg(edit->width())
                      .arg(shortText(text));
      }
    } else if (auto const *combo = qobject_cast<QComboBox const *>(widget)) {
      count(combo, combo->currentText());
      if (combo->fontMetrics().horizontalAdvance(combo->currentText()) > combo->width() - combo->height() / 2) {
        problems << QStringLiteral("CUT      %1 %2: text needs %3 px of width beside its arrow, has %4: \"%5\"")
                      .arg(type, place)
                      .arg(combo->fontMetrics().horizontalAdvance(combo->currentText()))
                      .arg(combo->width())
                      .arg(shortText(combo->currentText()));
      }
    } else if (auto const *tabs = qobject_cast<QTabBar const *>(widget)) {
      int total{0};
      for (int i{0}; i < tabs->count(); ++i) {
        if (tabs->isTabVisible(i)) {
          count(tabs, tabs->tabText(i));
          total += tabs->tabRect(i).width();
        }
      }
      if (total > tabs->width() + 2) {
        problems << QStringLiteral("CUT      %1 %2: the tabs need %3 px of width, have %4").arg(type, place).arg(total).arg(tabs->width());
      }
    } else if (auto const *header = qobject_cast<QHeaderView const *>(widget)) {
      if (!header->model() || (header->orientation() != Qt::Horizontal)) {
        continue;
      }
      for (int section{0}; section < header->count(); ++section) {
        if (header->isSectionHidden(section)) {
          continue;
        }
        QString const text = header->model()->headerData(section, Qt::Horizontal).toString();
        count(header, text);
        if (header->fontMetrics().horizontalAdvance(text) > header->sectionSize(section) - 4) {
          problems << QStringLiteral("CUT      %1 %2: heading \"%3\" needs %4 px of width, has %5")
                        .arg(type, place, shortText(text))
                        .arg(header->fontMetrics().horizontalAdvance(text))
                        .arg(header->sectionSize(section));
        }
      }
    }
  }

  out << "window " << window->width() << "x" << window->height() << "\n\n";
  out << (problems.isEmpty() ? QStringLiteral("Everything fits.\n") : QStringLiteral("%1 that do not fit:\n").arg(problems.size()));
  for (QString const &problem : std::as_const(problems)) {
    out << "  " << problem << "\n";
  }
  out << "\nFont sizes (points: how many texts, examples):\n";
  for (auto it = sizes.cbegin(); it != sizes.cend(); ++it) {
    out << "  " << QString::number(it.key(), 'f', 1) << " pt: " << it.value().count << " · " << it.value().examples.join(QStringLiteral(" · ")) << "\n";
  }
}

}
