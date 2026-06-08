#ifndef VIRTUALKEYBOARD_H
#define VIRTUALKEYBOARD_H

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace control2 {

class VirtKeyBtn : public QPushButton {
  Q_OBJECT
public:
  VirtKeyBtn(QString const &text, QString const &textAlt, QWidget *parent = 0);
  void enableAltText(bool enable);

private:
  QString m_text;
  QString m_textAlt;
};

class VirtualKeyboard : public QDialog {
  Q_OBJECT
public:
  enum Language {
    EN   = 0,
    RU   = 1,
    NONE = 2
  };
  explicit VirtualKeyboard(QString const &title, Language lang, QWidget *parent = nullptr);

signals:
  void textTyped(QString);

private:
  void initLangLayouts();
  void populateLayout(
    QVBoxLayout *layout, QList<VirtKeyBtn *> *buttonList, QStringList letters, QList<int> lettersPerRow);
  void initCtrlLayout();

  void setLangLayout(Language lang);
  void setUpper(bool isUpper);
  void setButtonsVisible(QList<VirtKeyBtn *> *buttonList, bool val);

  void letterBtnPressHandle();
  void delPressHandle();
  void enterPressHandle();

  QVBoxLayout        *m_enLayout;
  QVBoxLayout        *m_ruLayout;
  QVBoxLayout        *m_numLayout;
  QHBoxLayout        *m_ctrlLayout;
  QList<VirtKeyBtn *> m_enButtonList;
  QList<VirtKeyBtn *> m_ruButtonList;
  QList<VirtKeyBtn *> m_numButtonList;
  QLineEdit          *m_textLine;
  QPushButton        *m_shiftBtn;
  QPushButton        *m_langBtn;

  bool     m_isUpper;
  Language m_currLang;
};
} // namespace control2

#endif // VIRTUALKEYBOARD_H
