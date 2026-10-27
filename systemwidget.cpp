#include "systemwidget.h"

#include <QScrollArea>
#include "chamberindicators.h"
#include "chamberview.h"
#include "controloutput.h"

#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QEvent>
#include <QFrame>
#include <QLabel>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPalette>
#include <QPushButton>
#include <QVBoxLayout>

#include "actionwidget.h"
#include "confdialog.h"
#include "constants.h"
#include "forceoutlistwidget.h"
#include "offlineadvisorwidget.h"
#include "syslistwidget.h"
#include "theme.h"
namespace deepvaccontrol {
namespace {
char const SYSTEM_TAB_PAGE_OBJECT_NAME[]{"systemTabPage"};

QString systemTabStyleSheet() {
  QString styleSheet = utils::readStyleFromFile(":/styles/tabBar.qss")
                         .arg(40 * utils::ssf())
                         .arg(100 * utils::ssf())
                         .arg(100 * utils::ssf())
                         .arg(static_cast<int>(20 * utils::ssf()));

  QPalette const palette  = QApplication::palette();
  QColor const   tabColor = palette.color(QPalette::Button);
  if (tabColor.lightness() < 128) {
    styleSheet += QString("\n"
                          "QTabWidget::pane {"
                          "background-color: %1;"
                          "}\n"
                          "QWidget#%2 {"
                          "background-color: %1;"
                          "color: white;"
                          "}\n"
                          "QWidget#%2 QLabel {"
                          "color: white;"
                          "}\n"
                          "QWidget#%2 QCheckBox {"
                          "color: white;"
                          "}")
                    .arg(tabColor.name(), SYSTEM_TAB_PAGE_OBJECT_NAME);
  }

  return styleSheet;
}

void prepareSystemTabPage(QWidget *widget) {
  widget->setObjectName(SYSTEM_TAB_PAGE_OBJECT_NAME);
}

}

namespace {
// a card filling the tab, with a title and an optional muted note
QFrame *tabCard(QWidget *page, QString const &title, QString const &note) {
  double const ssf{utils::ssf()};
  auto        *pageLay = new QVBoxLayout(page);
  pageLay->setContentsMargins(0, 8 * ssf, 0, 0);
  auto *card = new QFrame(page);
  card->setObjectName("pageCard");
  pageLay->addWidget(card);
  auto *lay = new QVBoxLayout(card);
  lay->setContentsMargins(14 * ssf, 10 * ssf, 14 * ssf, 12 * ssf);
  lay->setSpacing(8 * ssf);
  auto *header = new QHBoxLayout();
  auto *label  = new QLabel(title, card);
  label->setObjectName("cardTitle");
  header->addWidget(label);
  if (!note.isEmpty()) {
    header->addSpacing(12 * ssf);
    auto *noteLabel = new QLabel(note, card);
    noteLabel->setProperty("muted", true);
    header->addWidget(noteLabel);
  }
  header->addStretch();
  lay->addLayout(header);
  return card;
}
}

SysCmdWidget::SysCmdWidget(ResHolder *resHolder, QWidget *parent) : QWidget{parent} {
  double const ssf{utils::ssf()};
  QFrame      *card    = tabCard(this, tr("PLC commands"), {});
  auto        *mainLay = static_cast<QVBoxLayout *>(card->layout());

  // command line
  auto *commandRow = new QFrame(card);
  commandRow->setObjectName("outputRow");
  auto *lineBtnLay = new QHBoxLayout(commandRow);
  lineBtnLay->setContentsMargins(12 * ssf, 8 * ssf, 10 * ssf, 8 * ssf);
  lineBtnLay->setSpacing(10 * ssf);
  auto *commandLabel = new QLabel(tr("Command"), commandRow);
  commandLabel->setObjectName("rowTitle");
  auto *cmdLine = new QLineEdit("finish", commandRow);
  cmdLine->setMinimumSize(320 * ssf, 30 * ssf);
  auto *sendCmd = new QPushButton(tr("Send"), commandRow);
  sendCmd->setObjectName("primaryButton");
  sendCmd->setFixedSize(100 * ssf, 30 * ssf);
  lineBtnLay->addWidget(commandLabel);
  lineBtnLay->addWidget(cmdLine);
  lineBtnLay->addWidget(sendCmd);
  lineBtnLay->addStretch();
  mainLay->addWidget(commandRow);
  mainLay->addSpacing(6 * ssf);

  // quick commands: fill in the command line
  auto *quickLabel = new QLabel(tr("Quick commands").toUpper(), card);
  quickLabel->setProperty("muted", true);
  {
    QFont font = quickLabel->font();
    font.setPointSizeF(font.pointSizeF() * 0.85);
    font.setLetterSpacing(QFont::PercentageSpacing, 106);
    quickLabel->setFont(font);
  }
  mainLay->addWidget(quickLabel);
  auto *cmdBtnsLay = new QHBoxLayout();
  cmdBtnsLay->setSpacing(8 * ssf);
  mainLay->addLayout(cmdBtnsLay);

  auto addBtn = [cmdLine, cmdBtnsLay, ssf, this](QString const &cmd) {
    auto *cmdBtn = new QPushButton(cmd, this);
    cmdBtn->setMinimumSize(110 * ssf, 30 * ssf);
    cmdBtn->setToolTip(tr("Put \"%1\" in the command line").arg(cmd));
    cmdBtnsLay->addWidget(cmdBtn);

    connect(cmdBtn, &QPushButton::clicked, this, [cmd, cmdLine]() {
      cmdLine->setText(cmd);
    });
  };

  addBtn("next");
  addBtn("get_status");
  addBtn("get_states");
  addBtn("get_settings");
  addBtn("delete_settings");
  addBtn("stop");
  addBtn("confirm_alarm");

  cmdBtnsLay->addStretch();
  mainLay->addStretch();

  connect(sendCmd, &QPushButton::clicked, this, [cmdLine, resHolder]() {
    resHolder->tcpGate->sendTextCmd(cmdLine->text());
  });
};

SysOsWidget::SysOsWidget(ResHolder *resHolder, QWidget *parent) : QWidget{parent} {
  double const ssf{utils::ssf()};
  QFrame      *card    = tabCard(this, tr("Operating system"), {});
  auto        *mainLay = static_cast<QVBoxLayout *>(card->layout());

  // one row per action: icon, title and what it does, button
  auto addRow = [&](QString const &icon, QString const &title, QString const &text, QWidget *button) {
    auto *row = new QFrame(card);
    row->setObjectName("outputRow");
    auto *rowLay = new QHBoxLayout(row);
    rowLay->setContentsMargins(12 * ssf, 10 * ssf, 12 * ssf, 10 * ssf);
    rowLay->setSpacing(12 * ssf);
    auto *iconLabel = new QLabel(row);
    iconLabel->setProperty("iconKind", icon);
    iconLabel->setFixedSize(24 * ssf, 24 * ssf);
    rowLay->addWidget(iconLabel);
    auto *texts = new QVBoxLayout();
    texts->setSpacing(2 * ssf);
    auto *titleLabel = new QLabel(title, row);
    titleLabel->setObjectName("rowTitle");
    auto *textLabel = new QLabel(text, row);
    textLabel->setProperty("muted", true);
    texts->addWidget(titleLabel);
    texts->addWidget(textLabel);
    rowLay->addLayout(texts, 1);
    button->setParent(row);
    button->setMinimumSize(190 * ssf, 32 * ssf);
    rowLay->addWidget(button);
    mainLay->addWidget(row);
  };

  auto *exitBtn = new QPushButton(tr("Exit to the OS"));
  exitBtn->setProperty("outline", true);
  auto *windowBtn = new QPushButton(tr("Full screen / window"));
  auto *themeBtn  = new QPushButton();
  auto updateThemeBtnText = [themeBtn]() {
    themeBtn->setText(utils::isDarkMode() ? QStringLiteral("Switch to Light mode") : QStringLiteral("Switch to Dark mode"));
  };
  updateThemeBtnText();

  addRow("power", tr("Exit to the operating system"), tr("Closes the application; the chamber keeps running its job."), exitBtn);
  addRow("system", tr("Full screen or window"), tr("Switches the application between full screen and a window."), windowBtn);
  addRow("settings", tr("Appearance"), tr("Light design (default) or dark mode."), themeBtn);

  // interface language (read at start: a change applies on the next start)
  auto *languageBox = new QComboBox();
  languageBox->addItem(QStringLiteral("English"), QStringLiteral("en"));
  languageBox->addItem(QStringLiteral("Русский"), QStringLiteral("ru"));
  QString const language = resHolder->settings->settingsMap().value("app_settings").toMap().value("language", "en").toString();
  languageBox->setCurrentIndex(qMax(0, languageBox->findData(language == QLatin1String("en") ? "en" : "ru")));
  addRow("globe", tr("Language"), tr("Language of the interface. Applies the next time the application starts."), languageBox);
  connect(languageBox, &QComboBox::currentIndexChanged, this, [this, resHolder, languageBox] {
    QVariantMap appSettings = resHolder->settings->settingsMap().value("app_settings").toMap();
    appSettings["language"] = languageBox->currentData().toString();
    resHolder->settings->setSection("app_settings", appSettings);
    (new MessageDialog(tr("Language"), tr("The new language is used the next time the application starts."), this))->open();
  });
  mainLay->addStretch();

  connect(exitBtn, &QPushButton::clicked, this, [this, resHolder] {
    auto *confDialog = new ConfDialog(tr("Confirmation"),
                                      tr("Do you really want to exit to the OS?\nThe chamber operation will NOT be stopped."),
                                      tr("Yes"),
                                      tr("No"),
                                      this);
    connect(confDialog, &ConfDialog::finished, this, [&](int result) {
      if (1 == result) { // == accepted()
        emit prepareToCloseApp();
      }
    });
    confDialog->open();
  });

  connect(windowBtn, &QPushButton::clicked, this, &SysOsWidget::switchAppMaximizedClicked);

  connect(themeBtn, &QPushButton::clicked, this, [this, updateThemeBtnText] {
    utils::applyDarkMode(!utils::isDarkMode());
    updateThemeBtnText();
    emit appearanceChanged();
  });
  QEvent paletteChange(QEvent::PaletteChange);
  changeEvent(&paletteChange); // draw the icons
};

void SysOsWidget::changeEvent(QEvent *event) {
  QWidget::changeEvent(event);
  if (event->type() == QEvent::PaletteChange) {
    int const size{static_cast<int>(22 * utils::ssf())};
    for (QLabel *label : findChildren<QLabel *>()) {
      QString const kind = label->property("iconKind").toString();
      if (!kind.isEmpty()) {
        label->setPixmap(theme::menuIcon(kind, theme::colors().muted, theme::colors().muted, size).pixmap(size));
      }
    }
  }
}

SystemWidget::SystemWidget(ResHolder *resHolder, QWidget *parent) : QWidget{parent} {
  auto *mainLay = new QVBoxLayout(this);
  mainLay->setContentsMargins(0, 0, 0, 0);

  m_tabWidget = new QTabWidget(this);
  theme::setFlatTabs(m_tabWidget);
  mainLay->addWidget(m_tabWidget);

  // 0 - список
  auto *sysListWidget = new SysListWidget(resHolder, this);
  prepareSystemTabPage(sysListWidget);
  m_tabWidget->addTab(sysListWidget, tr("States"));

  // the chamber's diagram: every component with its live state
  auto *diagramWidget = new QWidget(this);
  {
    // the drawing, and beside it every component with its state now and a display-only Fitted switch
    auto *lay = new QHBoxLayout(diagramWidget);
    lay->setContentsMargins(0, 8 * utils::ssf(), 0, 0);
    lay->setSpacing(12 * utils::ssf());
    auto *view    = new ChamberView(resHolder, diagramWidget);
    m_chamberView = view;
    // under the drawing: the temperature and pressure control outputs
    auto *drawing = new QVBoxLayout();
    drawing->setSpacing(10 * utils::ssf());
    drawing->addWidget(view, 1);
    auto *gauges = new QHBoxLayout();
    gauges->setSpacing(10 * utils::ssf());
    gauges->addWidget(new ControlOutputGauge(resHolder, ControlOutputGauge::Kind::TEMPERATURE, diagramWidget), 1);
    gauges->addWidget(new ControlOutputGauge(resHolder, ControlOutputGauge::Kind::PRESSURE, diagramWidget), 1);
    drawing->addLayout(gauges);
    lay->addLayout(drawing, 1);
    auto *scroll = new QScrollArea(diagramWidget);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFixedWidth(static_cast<int>(400 * utils::ssf()));
    scroll->setWidget(new ChamberIndicators(resHolder, view, scroll));
    scroll->viewport()->setAutoFillBackground(false);
    scroll->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; }"));
    lay->addWidget(scroll);
  }
  prepareSystemTabPage(diagramWidget);
  m_diagramTab = diagramWidget;
  m_tabWidget->addTab(diagramWidget, tr("Diagram"));

  // the AI Advisor (PID coefficients searched with the digital twin) and the simulation of any P / I / D
  auto *simulatorWidget = new OfflineAdvisorWidget(resHolder, this);
  prepareSystemTabPage(simulatorWidget);
  m_tabWidget->addTab(simulatorWidget, tr("Simulator"));

  // 1 - журнал
  auto *actionWidget = new ActionWidget(resHolder->actions, this);
  prepareSystemTabPage(actionWidget);
  m_tabWidget->addTab(actionWidget, tr("Log"));

  // 2 - ос
  auto *osWidget = new SysOsWidget(resHolder, this);
  prepareSystemTabPage(osWidget);
  m_tabWidget->addTab(osWidget, tr("OS"));
  m_osTab = osWidget;

  // 3 - ручный выходы
  auto *forceOutWidget = new ForceOutListWidget(
    resHolder, resHolder->states->getSysValue("outputs"), resHolder->settings->settingsMap()["monitor"].toList(), this);
  prepareSystemTabPage(forceOutWidget);
  m_tabWidget->addTab(forceOutWidget, tr("Manual output control"));
  m_outputsTab = forceOutWidget;

  // 4 - ПЛК
  auto *cmdWidget = new SysCmdWidget(resHolder, this);
  prepareSystemTabPage(cmdWidget);
  m_tabWidget->addTab(cmdWidget, tr("PLC commands"));
  m_commandsTab = cmdWidget;

  connect(resHolder->users, &AccessManager::userSelected, this, &SystemWidget::handleAccessChange);
  handleAccessChange(QString(), resHolder->users->getCurrentAccessLevel());

  connect(osWidget, &SysOsWidget::switchAppMaximizedClicked, this, &SystemWidget::switchAppMaximizedClicked);
  connect(osWidget, &SysOsWidget::prepareToCloseApp, this, &SystemWidget::prepareToCloseApp);
  connect(osWidget, &SysOsWidget::appearanceChanged, this, [this] {
    m_tabWidget->setStyleSheet(theme::flatTabStyleSheet());
  });
}

void SystemWidget::handleAccessChange(QString const &, AccessManager::AccessLevel newLevel) {
  bool const enableTabUser{newLevel >= AccessManager::AccessLevel::SPECTATOR_0};
  bool const enableTabAll{newLevel == AccessManager::AccessLevel::USER_ADMIN_3};

  m_tabWidget->setTabVisible(m_tabWidget->indexOf(m_osTab), enableTabUser);
  m_tabWidget->setTabVisible(m_tabWidget->indexOf(m_outputsTab), enableTabAll);
  m_tabWidget->setTabVisible(m_tabWidget->indexOf(m_commandsTab), enableTabAll);
}
}

