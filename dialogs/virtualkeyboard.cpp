#include "virtualkeyboard.h"

#include "utils.h"

namespace control2 {
VirtualKeyboard::VirtualKeyboard(QString const &title, Language lang, QWidget *parent)
    : QDialog{parent}, m_isUpper{false}, m_currLang{Language::NONE} {
  setWindowTitle(title);
  initLangLayouts();
  initCtrlLayout();
  setLangLayout(lang);
  setUpper(false);

  QFont font;
  font.setBold(true);
  font.setStyleHint(QFont::TypeWriter);
  font.setPointSize(20 * utils::ssf());
  m_textLine = new QLineEdit(this);
  m_textLine->setFont(font);
  m_textLine->setStyleSheet("QLabel {border: 1px solid black;"
                            "background-color: white;}");

  auto *mainLay = new QVBoxLayout(this);

  mainLay->addWidget(m_textLine);
  mainLay->addLayout(m_numLayout);
  mainLay->addLayout(m_enLayout);
  mainLay->addLayout(m_ruLayout);
  mainLay->addLayout(m_ctrlLayout);

  QLineEdit *parentLine{qobject_cast<QLineEdit *>(this->parent())};
  if (parentLine) {
    m_textLine->setText(parentLine->text());
  }
  setAttribute(Qt::WA_DeleteOnClose);
}

void VirtualKeyboard::initLangLayouts() {
  m_enLayout  = new QVBoxLayout();
  m_ruLayout  = new QVBoxLayout();
  m_numLayout = new QVBoxLayout();

  QStringList enLetters = {"q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "-", "a", "s", "d", "f",
                           "g", "h", "j", "k", "l", "_", "z", "x", "c", "v", "b", "n", "m", "(", ")"};
  QStringList ruLetters = {"й", "ц", "у", "к", "е", "н", "г", "ш", "щ", "з", "х", "ъ", "ф", "ы", "в", "а", "п",
                           "р", "о", "л", "д", "ж", "э", "я", "ч", "с", "м", "и", "т", "ь", "б", "ю", "."};
  QStringList numLetters = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "del"};

  QList<int> enLetterPerLine  = {11, 10, 9};
  QList<int> ruLetterPerLine  = {12, 11, 10};
  QList<int> numLetterPerLine = {11};

  populateLayout(m_enLayout, &m_enButtonList, enLetters, enLetterPerLine);
  populateLayout(m_ruLayout, &m_ruButtonList, ruLetters, ruLetterPerLine);
  populateLayout(m_numLayout, &m_numButtonList, numLetters, numLetterPerLine);
}

void VirtualKeyboard::populateLayout(
  QVBoxLayout *layout, QList<VirtKeyBtn *> *buttonList, QStringList letters, QList<int> lettersPerRow) {
  int index{0U};
  for (auto numberPerLine : lettersPerRow) {
    auto *rowLay = new QHBoxLayout();
    rowLay->addStretch();
    for (int i{0U}; i < numberPerLine; ++i) {
      auto letter    = letters.at(index);
      auto upperText = letter.toUpper();
      if ('.' == letter) {
        upperText = ",";
      }
      if ("del" == letter) {
        auto *btn = new QPushButton(this);
        btn->setIcon(QIcon(":/imgs/backspace.png"));
        btn->setIconSize(QSize(40 * utils::ssf(), 40 * utils::ssf()));
        btn->setFixedSize(2 * 50 * utils::ssf(), 50 * utils::ssf());
        connect(btn, &QPushButton::clicked, this, &VirtualKeyboard::delPressHandle);
        rowLay->addWidget(btn);
      } else {
        auto *btn = new VirtKeyBtn(letter, upperText, this);
        buttonList->append(btn);
        connect(btn, &VirtKeyBtn::clicked, this, &VirtualKeyboard::letterBtnPressHandle);
        rowLay->addWidget(btn);
      }
      ++index;
    }
    rowLay->addStretch();
    layout->addLayout(rowLay);
  }
}

void VirtualKeyboard::initCtrlLayout() {
  auto *rejectBtn = new VirtKeyBtn(tr("Отмена"), "", this);
  auto *enterBtn  = new VirtKeyBtn(tr("Ввод"), "", this);
  rejectBtn->setFixedSize(QSize(120 * utils::ssf(), 50 * utils::ssf()));
  enterBtn->setFixedSize(QSize(120 * utils::ssf(), 50 * utils::ssf()));

  m_shiftBtn = new QPushButton(this);
  m_shiftBtn->setIcon(QIcon(":/imgs/shift.png"));
  m_shiftBtn->setIconSize(QSize(40 * utils::ssf(), 40 * utils::ssf()));
  m_shiftBtn->setFixedSize(QSize(60 * utils::ssf(), 50 * utils::ssf()));

  auto *spaceBtn = new VirtKeyBtn(" ", " ", this);
  spaceBtn->setFixedWidth(200 * utils::ssf());

  m_langBtn = new QPushButton(this);
  m_langBtn->setIcon(QIcon(":imgs/lang.png"));
  m_langBtn->setIconSize(QSize(40 * utils::ssf(), 40 * utils::ssf()));
  m_langBtn->setFixedSize(QSize(60 * utils::ssf(), 50 * utils::ssf()));

  m_ctrlLayout = new QHBoxLayout();
  m_ctrlLayout->addWidget(rejectBtn);
  m_ctrlLayout->addStretch();
  m_ctrlLayout->addWidget(m_shiftBtn);
  m_ctrlLayout->addWidget(spaceBtn);
  m_ctrlLayout->addWidget(m_langBtn);
  m_ctrlLayout->addStretch();
  m_ctrlLayout->addWidget(enterBtn);

  connect(m_shiftBtn, &QPushButton::clicked, this, [this] {
    this->setUpper(!m_isUpper);
  });
  connect(m_langBtn, &QPushButton::clicked, this, [this] {
    this->setLangLayout((m_currLang == RU) ? EN : RU);
  });
  connect(spaceBtn, &QPushButton::clicked, this, &VirtualKeyboard::letterBtnPressHandle);

  connect(rejectBtn, &QPushButton::clicked, this, &VirtualKeyboard::reject);

  connect(enterBtn, &QPushButton::clicked, this, &VirtualKeyboard::enterPressHandle);
}

void VirtualKeyboard::setLangLayout(Language lang) {
  if (m_currLang == lang) {
    return;
  }

  m_currLang = lang;

  switch (m_currLang) {
    case RU:
      setButtonsVisible(&m_enButtonList, false);
      setButtonsVisible(&m_ruButtonList, true);
      break;
    case EN:
      setButtonsVisible(&m_ruButtonList, false);
      setButtonsVisible(&m_enButtonList, true);
      break;
    case NONE:
      break;
  }
}

void VirtualKeyboard::setUpper(bool isUpper) {
  if (isUpper == isUpper) {
    return;
  }

  isUpper = isUpper;

  m_shiftBtn->setIcon(QIcon((isUpper ? ":/imgs/shift_on.png" : ":/imgs/shift.png")));

  for (auto list : {&m_enButtonList, &m_ruButtonList, &m_numButtonList}) {
    for (auto btn : *list) {
      btn->enableAltText(isUpper);
    }
  }
}

void VirtualKeyboard::setButtonsVisible(QList<VirtKeyBtn *> *buttonList, bool val) {
  for (VirtKeyBtn *btn : *buttonList) {
    btn->setVisible(val);
  }
}

void VirtualKeyboard::letterBtnPressHandle() {
  VirtKeyBtn *clickedButton{qobject_cast<VirtKeyBtn *>(sender())};
  QString     letter = clickedButton->text();

  if (m_textLine->text().length() < 39) {
    m_textLine->setText(QString("%1%2").arg(m_textLine->text()).arg(letter));
  }
}

void VirtualKeyboard::delPressHandle() {
  QString text{m_textLine->text()};
  text.chop(1);
  m_textLine->setText(text);
}

void VirtualKeyboard::enterPressHandle() {
  QLineEdit *parentLine{qobject_cast<QLineEdit *>(this->parent())};
  if (parentLine) {
    parentLine->setText(m_textLine->text());
  } else {
    emit textTyped(m_textLine->text());
  }
  done(0);
}

VirtKeyBtn::VirtKeyBtn(QString const &text, QString const &textAlt, QWidget *parent)
    : QPushButton(parent), m_text(text), m_textAlt(textAlt) {
  setText(m_text);

  QFont font = this->font();
  font.setPointSize(16 * utils::ssf());
  this->setFont(font);

  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  setFixedSize(50 * utils::ssf(), 50 * utils::ssf());
}

void VirtKeyBtn::enableAltText(bool enable) {
  setText(enable ? m_textAlt : m_text);
}
} // namespace control2
